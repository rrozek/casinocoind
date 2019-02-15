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
    2018-02-15  jrojek        Created
*/
//==============================================================================


#ifndef CONFIGOBJECTENTRY_H
#define CONFIGOBJECTENTRY_H

#include <casinocoin/basics/StringUtilities.h>

#include <casinocoin/protocol/AccountID.h>
#include <casinocoin/protocol/PublicKey.h>
#include <casinocoin/protocol/STAmount.h>

#include <casinocoin/json/json_value.h>

#include <boost/bimap.hpp>
#include <boost/assign/list_of.hpp>
/** Fee schedule to vote for.
    During voting ledgers, the FeeVote logic will try to move towards
    these values when injecting fee-setting transactions.
    A default-constructed Setup contains recommended values.
*/

namespace casinocoin
{

struct TokenDescription

{
    enum TokenFlags
    {
        KYCRequired = 0x0001,
        AuthRequired = 0x0002,
        FeesRequired = 0x0004
    };

    TokenDescription();
    TokenDescription(const TokenDescription& other);
    TokenDescription& operator=(const TokenDescription& other);

    bool fromJson(Json::Value const& data);
    bool toJson(Json::Value& result) const;

    std::string fullName;
    STAmount totalSupply;
    TokenFlags flags;
    std::string website;
    std::string contactEmail;
    std::string iconURL;
    std::string apiEndpoint;
};

class ConfigObjectEntry
{
public:
    enum Type
    {
        Invalid             = 0,
        KYC_Signer          = 1,
        Message_PubKey      = 2,
        Token               = 3
    };

    using bimap_string_type = boost::bimap<std::string, Type>;
    static
    bimap_string_type typeMap;
    static bimap_string_type initializeTypeMap();

    union Field
    {
        std::vector<AccountID> kycAccountList;
        std::vector<PublicKey> msgDestPubKey;
        std::vector<TokenDescription> tokenList;
    };

    // helper methods
    // internally does bytes->json->classStructure
    static bool fromBytes(ConfigObjectEntry& result, Blob const& data);
    // internally does classStructure->json->bytes
    static bool toBytes(Blob& result, ConfigObjectEntry const& data);

    static bool fromJson(ConfigObjectEntry& result, Json::Value const& data);
    static bool toJson(Json::Value& result, ConfigObjectEntry const& data);

    ConfigObjectEntry();
    virtual ~ConfigObjectEntry();

    ConfigObjectEntry(const ConfigObjectEntry& other);
    ConfigObjectEntry& operator=(const ConfigObjectEntry& other);

    bool fromJson(Json::Value const& data);
    bool toJson(Json::Value& result) const;

    bool fromBytes(Blob const& data);
    bool toBytes(Blob& result) const;

    uint32_t getId() const { return mId; }
    Type getType() const { return mType; }
private:

    uint32_t mId;
    Type mType;
    Field mData;
};

} // casinocoin

#endif // CONFIGOBJECTENTRY_H
