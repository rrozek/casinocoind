//------------------------------------------------------------------------------
/*
    This file is part of casinocoind: https://github.com/casinocoin/casinocoind
    Copyright (c) 2019 CasinoCoin Foundation
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
    2018-05-14  jrojek          Created
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/app/main/Application.h>
#include <casinocoin/app/misc/configuration/VotableConfiguration.h>
#include <casinocoin/core/DatabaseCon.h>
#include <casinocoin/core/ConfigSections.h>
#include <casinocoin/protocol/digest.h>
#include <casinocoin/protocol/JsonFields.h>
#include <casinocoin/protocol/TxFlags.h>
#include <casinocoin/protocol/Feature.h>
#include <casinocoin/protocol/ConfigObjectEntry.h>
#include <casinocoin/protocol/STAccount.h>
#include <casinocoin/protocol/STValidation.h>
#include <casinocoin/app/ledger/LedgerMaster.h>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <mutex>

namespace casinocoin {

/** The status of all nodes requested in a given period. */
struct ObjectHashSet
{
private:
    // How many yes votes each config object hash received
    hash_map<VotableConfiguration::ObjectHash, uint32> votes_;

public:
    // number of trusted validations
    int mTrustedValidations = 0;

    // number of votes needed
    int mThreshold = 0;

    ObjectHashSet () = default;

    void tally (std::set<VotableConfiguration::ObjectHash> const& objectHashes)
    {
        ++mTrustedValidations;

        for (auto const& objHash : objectHashes)
            ++votes_[objHash];
    }

    uint32 votes (VotableConfiguration::ObjectHash const& objectHash) const
    {
        auto const& it = votes_.find (objectHash);

        if (it == votes_.end())
            return 0;

        return it->second;
    }
};

class VotableConfigurationImpl final : public VotableConfiguration
{

private:
    Application& app_;

public:
    VotableConfigurationImpl (
        Application& app,
        int majorityFraction,
        Json::Value const& configurationJson,
        beast::Journal journal);

    void
    doValidation (std::shared_ptr<ReadView const> const& lastClosedLedger,
        STObject& baseValidation) override;

    void
    doVoting (std::shared_ptr<ReadView const> const& lastClosedLedger,
        std::vector<STValidation::pointer> const& parentValidations,
        std::shared_ptr<SHAMap> const& initialPosition) override;

    void
    updatePosition(Json::Value const& jvVotableConfig) override;

protected:
    std::mutex mutex_;

    std::map<ObjectHash,ConfigObjectEntry> entryList_;

    // The amount of support that an amendment must receive
    // 0 = 0% and 256 = 100%
    int const majorityFraction_;

    // The results of the last voting round - may be empty if
    // we haven't participated in one yet.
    std::unique_ptr <ObjectHashSet> lastVote_;

    beast::Journal j_;

};

VotableConfigurationImpl::VotableConfigurationImpl(
    Application& app,
        int majorityFraction,
            Json::Value const& configurationJson,
                beast::Journal journal)
    : app_(app)
    , majorityFraction_(majorityFraction)
    , j_(journal)
{
    assert (majorityFraction_ != 0);
    updatePosition(configurationJson);
}

void VotableConfigurationImpl::doValidation(std::shared_ptr<const ReadView> const& lastClosedLedger, STObject &baseValidation)
{
    if (!lastClosedLedger->rules().enabled(featureConfigObject))
    {
        JLOG(j_.info()) << "VotableConfigurationImpl::doValidation feature is not enabled. aborting";
        return;
    }
    JLOG (j_.info()) <<
        "VotableConfigurationImpl::doValidation with " << entryList_.size() << " candidates";

    std::lock_guard <std::mutex> sl (mutex_);

    std::vector <ObjectHash> ourConfig;
    for (auto const& it : entryList_)
    {
        JLOG (j_.debug())
            << "VotableConfigurationImpl::doValidation configID: " << it.second.getId()
            << " hash: " << to_string(it.first);
        ourConfig.push_back(it.first);
    }
    if (! ourConfig.empty())
        std::sort (ourConfig.begin (), ourConfig.end ());

    baseValidation.setFieldV256 (sfConfigHashes,
       STVector256 (sfConfigHashes, ourConfig));
}

void VotableConfigurationImpl::doVoting(std::shared_ptr<const ReadView> const& lastClosedLedger, std::vector<STValidation::pointer> const& parentValidations, const std::shared_ptr<SHAMap> &initialPosition)
{
    if (!lastClosedLedger->rules().enabled(featureConfigObject))
    {
        JLOG(j_.info()) << "VotableConfigurationImpl::doVoting feature is not enabled. aborting";
        return;
    }
    JLOG(j_.info()) << "VotableConfigurationImpl::doVoting. validations: " << parentValidations.size();

    auto configVote = std::make_unique<ObjectHashSet>();

    for (auto const& singleValidation : parentValidations)
    {
        if (!(singleValidation->isTrusted()))
            continue;
        std::set<ObjectHash> singleNodePosition;
        if (singleValidation->isFieldPresent(sfConfigHashes))
        {
            auto const choices =
                singleValidation->getFieldV256 (sfConfigHashes);
            singleNodePosition.insert (choices.begin (), choices.end ());
        }
        configVote->tally (singleNodePosition);
    }
    configVote->mThreshold = std::max(1,
        (configVote->mTrustedValidations * majorityFraction_) / 256);

    JLOG (j_.info()) <<
        "Received " << configVote->mTrustedValidations <<
        " trusted validations, threshold is: " << configVote->mThreshold;

    auto const seq = lastClosedLedger->info().seq + 1;

    for (auto const& it : entryList_)
    {
        if (!(configVote->votes (it.first) >= configVote->mThreshold))
        {
            JLOG (j_.info()) << "configId: " << it.second.getId() << " hash: " << to_string(it.first) << " didn't reach majority";
            continue;
        }

        JLOG (j_.warn()) << "We are voting for: configId: " << it.second.getId() << " hash: " << to_string(it.first);

        Blob configData;
        it.second.toBytes(configData);
        STTx configVoteTx (ttCONFIG,
            [seq, &it, configData](auto& obj)
            {
                obj[sfAccount] = AccountID();
                obj[sfLedgerSequence] = seq;
                obj[sfConfigID] = it.second.getId();
                obj[sfConfigType] = it.second.getType();
                obj.setFieldVL(sfConfigData, configData);
            });

        uint256 txID = configVoteTx.getTransactionID ();

        JLOG(j_.warn()) << "ConfigVote tx id: " << to_string (txID);

        Serializer s;
        configVoteTx.add (s);

        auto tItem = std::make_shared<SHAMapItem> (txID, s.peekData ());

        if (!initialPosition->addGiveItem (tItem, true, false))
        {
            JLOG(j_.warn()) << "Ledger already had this ConfigVote change";
        }
    }

    // Stash for reporting
    lastVote_ = std::move(configVote);
}

void VotableConfigurationImpl::updatePosition(Json::Value const& jvVotableConfig)
{
    if (!app_.getLedgerMaster().getValidatedRules().enabled(featureConfigObject))
    {
        JLOG(j_.info()) << "VotableConfigurationImpl::updatePosition feature is not enabled. aborting";
        return;
    }
    entryList_.clear();

    if (jvVotableConfig.isObject())
    {
        ConfigObjectEntry entry(j_);
        if (!entry.fromJson(jvVotableConfig))
        {
            JLOG(j_.info()) << "VotableConfigurationImpl::updatePosition failed to parse json, return";
            return;
        }

        Blob objData;
        if (!entry.toBytes(objData))
        {
            JLOG(j_.info()) << "VotableConfigurationImpl::updatePosition failed to convert json to binary, continue";
            return;
        }
        ObjectHash hash = sha512Half(makeSlice(objData));

        JLOG(j_.info()) << "VotableConfigurationImpl::updatePosition added " << entry.getId() << " with hash: " << to_string(hash);
        entryList_[hash] = entry;
    }
    else if (jvVotableConfig.isArray())
    {
        for (Json::UInt index = 0; index < jvVotableConfig.size(); index++)
        {
            ConfigObjectEntry anEntry(j_);
            if (!anEntry.fromJson(jvVotableConfig[index]))
            {
                JLOG(j_.info()) << "VotableConfigurationImpl::updatePosition failed to parse json, continue";
                continue;
            }

            Blob objData;
            if (!anEntry.toBytes(objData))
            {
                JLOG(j_.info()) << "VotableConfigurationImpl::updatePosition failed to convert json to binary, continue";
                continue;
            }
            ObjectHash hash = sha512Half(makeSlice(objData));
            JLOG(j_.info()) << "VotableConfigurationImpl::updatePosition added " << anEntry.getId() << " with hash: " << to_string(hash);
            entryList_[hash] = anEntry;
        }
    }
}

std::unique_ptr<VotableConfiguration> make_VotableConfig (Application& app, int majorityFraction, Json::Value const& configurationJson, beast::Journal journal)
{
    return std::make_unique<VotableConfigurationImpl> (app, majorityFraction, configurationJson, journal);
}


} // casinocoin
