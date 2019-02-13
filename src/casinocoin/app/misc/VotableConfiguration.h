//------------------------------------------------------------------------------
/*
    This file is part of casinocoind: https://github.com/casinocoin/casinocoind
    Copyright (c) 2017 - 2019 CasinoCoin Foundation.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

//==============================================================================
/*
    2018-02-03  jrojek        Created
*/
//==============================================================================

#ifndef VOTABLECONFIGURATION_H
#define VOTABLECONFIGURATION_H

#include <casinocoin/app/ledger/Ledger.h>
#include <casinocoin/app/misc/Validations.h>
#include <casinocoin/basics/StringUtilities.h>
#include <casinocoin/core/ConfigSections.h>
#include <casinocoin/protocol/Protocol.h>
#include <casinocoin/json/json_value.h>

#include <boost/assign/list_of.hpp>

namespace casinocoin {

class VotableConfiguration
{
public:
    using ObjectHash = uint256;

    /** Fee schedule to vote for.
        During voting ledgers, the FeeVote logic will try to move towards
        these values when injecting fee-setting transactions.
        A default-constructed Setup contains recommended values.
    */
    struct ConfigEntry
    {
        enum Type
        {
            Invalid             = 0,
            KYC_Signer          = 1,
            Message_PubKey      = 2,
            Token               = 3
        };
        std::map<std::string, Type> typeMap =
            boost::assign::map_list_of("Invalid", Invalid)
            ("KYC_Signer", KYC_Signer)
            ("Message_PubKey", Message_PubKey)
            ("Token", Token);

        // jrojek TODO: change to union and const it...
        struct Field
        {
            std::vector<AccountID> kycAccountList;
            std::vector<PublicKey> msgDestPubKey;
            std::vector<STAmount> tokenList;
        };

        uint32_t entryID;
        Type entryType;
        Field entryData;

        ConfigEntry()
            : entryID(0)
            , entryType(Invalid)
        {}

        ConfigEntry(Json::Value const& source)
            : entryID(source.get(sfConfigID.getName(), Json::nullValue).asInt())
            , entryType(typeMap[source.get(sfConfigType.getName(),Json::nullValue).asString()])
        {
            switch(entryType)
            {
            case KYC_Signer: // array of base58 accountIds or single accountId
            {
                Json::Value jvKYCAccounts = source.get(sfConfigData.getName(), Json::arrayValue);
                if (jvKYCAccounts.isArray())
                {
                    for (Json::UInt index = 0; index < jvKYCAccounts.size(); index++)
                    {
                        AccountID acc;
                        if (!to_issuer(acc, jvKYCAccounts[index].asString()))
                            continue;
                        entryData.kycAccountList.push_back(acc);
                    }
                }
                else if (jvKYCAccounts.isString())
                {
                    AccountID acc;
                    if (to_issuer(acc, jvKYCAccounts.asString()))
                        entryData.kycAccountList.push_back(acc);
                }
                break;
            }
            case Message_PubKey: //array of hex pubkeys or single hex pubkey
            {
                Json::Value jvPubKeys = source.get(sfConfigData.getName(), Json::arrayValue);
                if (jvPubKeys.isArray())
                {
                    for (Json::UInt index = 0; index < jvPubKeys.size(); index++)
                    {
                        auto unHexedPubKey = strUnHex(jvPubKeys[index].asString());
                        if (!unHexedPubKey.second)
                            continue;
                        PublicKey pubKey(Slice(unHexedPubKey.first.data(), unHexedPubKey.first.size()));
                        entryData.msgDestPubKey.push_back(pubKey);
                    }
                }
                else if (jvPubKeys.isString())
                {
                    auto unHexedPubKey = strUnHex(jvPubKeys.asString());
                    if (unHexedPubKey.second)
                    {
                        PublicKey pubKey(Slice(unHexedPubKey.first.data(), unHexedPubKey.first.size()));
                        entryData.msgDestPubKey.push_back(pubKey);
                    }
                }
                break;
            }
            case Token: // array of jsonified STAmounts {"value":123.45, "currency":"TST", "issuer":<base58AccountID>}
            {
                Json::Value jvTokenArray = source.get(sfConfigData.getName(), Json::arrayValue);
                if (jvTokenArray.isArray())
                {
                    for (Json::UInt index = 0; index < jvTokenArray.size(); index++)
                    {
                        STAmount amount;
                        if (!amountFromJsonNoThrow(amount, jvTokenArray[index]))
                            continue;
                        entryData.tokenList.push_back(amount);
                    }
                }
                break;
            }
            case Invalid:
                break;
            }
        }

        ~ConfigEntry(){}
        ConfigEntry(const ConfigEntry& other)
            : entryID(other.entryID)
            , entryType(other.entryType)
            , entryData(other.entryData)
        {}
        ConfigEntry& operator=(const ConfigEntry& other)
        {
            entryID = other.entryID;
            entryType = other.entryType;
            entryData = other.entryData;
            return *this;
        }
    };

    virtual ~VotableConfiguration() = default;

    /** Add local Configuration to validation
        @param lastClosedLedger
        @param baseValidation
    */
    virtual
    void
    doValidation (std::shared_ptr<ReadView const> const& lastClosedLedger,
        STObject& baseValidation) = 0;

    /** Cast Configuration conclusion to the ledger
        @param lastClosedLedger
        @param initialPosition
    */
    virtual
    void
    doVoting (std::shared_ptr<ReadView const> const& lastClosedLedger,
        ValidationSet const& parentValidations,
        std::shared_ptr<SHAMap> const& initialPosition) = 0;

    /** Update our current voting position with our config file
        @param reports
    */
    virtual
    void
    updatePosition(Json::Value const& jvVotableConfig) = 0;

    // The highest-sequence ledger we have done a VotableConfiguration
    std::atomic <std::uint32_t> mVotableConfigurationLedgerSeq = {0};

};

std::unique_ptr<VotableConfiguration> make_VotableConfig (Application& app, int majorityFraction, Json::Value const& configurationJson, beast::Journal journal);

} // casinocoin

#endif // VOTABLECONFIGURATION_H
