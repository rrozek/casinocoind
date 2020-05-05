//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/casinocoin/casinocoind
    Copyright (c) 2018 CasinoCoin Foundation

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
#include <casinocoin/app/misc/CRNRound.h>
#include <casinocoin/app/misc/Validations.h>
#include <casinocoin/core/DatabaseCon.h>
#include <casinocoin/core/ConfigSections.h>
#include <casinocoin/protocol/JsonFields.h>
#include <casinocoin/protocol/TxFlags.h>
#include <casinocoin/protocol/Feature.h>
#include <casinocoin/protocol/ConfigObjectEntry.h>
#include <casinocoin/app/misc/FeeVote.h>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <mutex>

namespace casinocoin {

/** The status of all nodes requested in a given period. */
struct NodesEligibilitySet
{
private:
    // How many yes/no votes each node received
    // kept in two distinct lists in case some sopthisticated logic needs to be applied
    hash_map<PublicKey, uint32> yesVotes_;
    hash_map<PublicKey, uint32> nayVotes_;
    CSCAmount feeDistributionVote_;
    CSCAmount feeRemainFromShare_;
    bool votingFinished_ = false;

    CRN::EligibilityPaymentMap paymentMap_;
public:
    using YesNayVotes = std::pair<uint32, uint32>;

    // number of trusted validations
    int mTrustedValidations = 0;

    // number of votes needed
    int mThreshold = 0;

    NodesEligibilitySet () = default;

    void tally (CRN::EligibilityMap const& nodes)
    {
        if (votingFinished_)
            return;

        ++mTrustedValidations;

        for (auto iter = nodes.begin(); iter != nodes.end() ; ++iter)
        {
            if (iter->second)
                ++yesVotes_[iter->first];
            else
                ++nayVotes_[iter->first];
        }
    }

    void setVotingFinished(boost::optional<CRN_SettingsDescriptor> crnSettings)
    {
        votingFinished_ = true;

        std::vector<PublicKey> eligibleList;
        for ( auto const& yesVote : yesVotes_)
        {
            if (isEligible(yesVote.first))
                eligibleList.push_back(yesVote.first);
        }
        // if there are no CRN nodes eligible we do not distribute fees back .... as only the foundation would get some in that case ;)
        if (eligibleList.size() == 0)
            return;

        // we first get the foundation percentage of fees from the total
        uint64_t foundationFeeDrops = feeDistributionVote_.drops() / 100 * crnSettings->foundationFeeFactor;
        // add the foundation to the paymentMap
        paymentMap_.insert(std::pair<PublicKey, CSCAmount>(crnSettings->foundationFeesPublicKey, CSCAmount(foundationFeeDrops)));
        // distribute the rest of the fees to the CRN's
        uint64_t remainingFees = feeDistributionVote_.drops() - foundationFeeDrops;
        feeRemainFromShare_ = CSCAmount(remainingFees % eligibleList.size());
        uint64_t sharePerNode = remainingFees / eligibleList.size();
        for ( auto const& eligibleNode : eligibleList)
            paymentMap_.insert(std::pair<PublicKey, CSCAmount>(eligibleNode, CSCAmount(sharePerNode)));
    }

    CRN::EligibilityPaymentMap votes () const
    {
        if (!votingFinished_)
            return CRN::EligibilityPaymentMap();

        return paymentMap_;
    }

    void setFeeDistributionVote(CSCAmount const& feeDistributionVote)
    {
        if (votingFinished_)
            return;

        feeDistributionVote_ = feeDistributionVote;
    }

    CSCAmount feeDistributionVote() const
    {
        return CSCAmount(feeDistributionVote_ - feeRemainFromShare_);
    }

private:
    bool isEligible(PublicKey const& crnNode) const
    {
        uint32_t votesCombined = 0;

        auto const& itYes = yesVotes_.find (crnNode);
        if (itYes != yesVotes_.end())
            votesCombined += itYes->second;

        auto const& itNay = nayVotes_.find (crnNode);
        if (itNay != nayVotes_.end())
            votesCombined -= itNay->second;

        return votesCombined >= mThreshold;
    }
};

class CRNRoundImpl final : public CRNRound
{

private:
    Application& app_;

public:
    CRNRoundImpl (
        Application& app,
        int majorityFraction,
        beast::Journal journal);

    void
    doValidation (std::shared_ptr<ReadView const> const& lastClosedLedger,
        STObject& baseValidation) override;

    void
    doVoting (std::shared_ptr<ReadView const> const& lastClosedLedger,
        ValidationSet const& parentValidations,
        std::shared_ptr<SHAMap> const& initialPosition) override;

    void
    updatePosition(std::list<STPerformanceReport::pointer> const& reports) override;

protected:
    std::mutex mutex_;

    CRN::EligibilityMap eligibilityMap_;

    // The amount of support that an amendment must receive
    // 0 = 0% and 256 = 100%
    int const majorityFraction_;

    // The results of the last voting round - may be empty if
    // we haven't participated in one yet.
    std::unique_ptr <NodesEligibilitySet> lastVote_;
    CSCAmount lastFeeDistributionPosition_;

    beast::Journal j_;

};

CRNRoundImpl::CRNRoundImpl(Application& app, int majorityFraction, beast::Journal journal)
    : app_(app)
    , majorityFraction_(majorityFraction)
    , j_(journal)
{
    assert (majorityFraction_ != 0);
}

void CRNRoundImpl::doValidation(std::shared_ptr<const ReadView> const& lastClosedLedger, STObject &baseValidation)
{
    if (!lastClosedLedger->rules().enabled(featureCRN))
    {
        JLOG(j_.info()) << "CRNRoundImpl::doValidation CRN feature is not enabled. aborting";
        return;
    }
    if (!isCRNRoundsActivated( lastClosedLedger, j_ ))
    {
        JLOG(j_.info()) << "CRNRoundImpl::doValidation CRN Rounds are de-activated. aborting";
        return;
    }
    CSCAmount totalCoinsSupply(SYSTEM_CURRENCY_START);

    if (lastClosedLedger->info().drops > totalCoinsSupply)
    {
        JLOG(j_.warn()) << "CRNRoundImpl::doValidation. more drops in LCL then SYSTEM_CURRENCY_START.";
        JLOG(j_.warn()) << "CRNRoundImpl::doValidation. drops in LCL: " << static_cast<uint64_t>(lastClosedLedger->info().drops.drops());
        return;
    }
    std::lock_guard <std::mutex> sl (mutex_);

    lastFeeDistributionPosition_ = CSCAmount(totalCoinsSupply - lastClosedLedger->info().drops);
    JLOG (j_.info()) <<
        "CRNRoundImpl::doValidation with " << eligibilityMap_.size() << " candidates";
    JLOG(j_.info()) << "CRNRoundImpl::doValidation. Total coins in circulation: " << totalCoinsSupply.drops();

    STArray crnArray(sfCRNs);
    for ( auto iter = eligibilityMap_.begin(); iter != eligibilityMap_.end(); ++iter)
    {
        crnArray.push_back (STObject (sfCRN));
        auto& entry = crnArray.back ();
        entry.emplace_back (STBlob (sfCRN_PublicKey, iter->first.data(), iter->first.size()));
        entry.emplace_back (STUInt8 (sfCRNEligibility, iter->second ? 1 : 0));
    }

    JLOG(j_.info()) << "CRNRoundImpl::doValidation we propose lastLedger: "
                    << lastClosedLedger->info().seq << " drops: " << lastClosedLedger->info().drops.drops();

    baseValidation.setFieldArray(sfCRNs, crnArray);
    baseValidation.setFieldAmount(sfCRN_FeeDistributed, STAmount(lastFeeDistributionPosition_));
    // clear eligibilityMap_ for next round
    eligibilityMap_.clear();
}

void CRNRoundImpl::doVoting(std::shared_ptr<const ReadView> const& lastClosedLedger, const ValidationSet &parentValidations, const std::shared_ptr<SHAMap> &initialPosition)
{
    if (!lastClosedLedger->rules().enabled(featureCRN))
    {
        JLOG(j_.info()) << "CRNRoundImpl::doVoting CRN feature is not enabled. aborting";
        return;
    }
    if (!isCRNRoundsActivated( lastClosedLedger, j_ ))
    {
        JLOG(j_.info()) << "CRNRoundImpl::doVoting CRN Rounds are de-activated. aborting";
        return;
    }
    CSCAmount totalCoinsSupply(SYSTEM_CURRENCY_START);
    // jrojek: once we figure out the way to trace burn account two tx on ledger, we should substract it here
    CSCAmount totalCoinsInCirculation(totalCoinsSupply);
    if (lastClosedLedger->info().drops > totalCoinsSupply)
    {
        JLOG(j_.warn()) << "CRNRoundImpl::doValidation. more drops in LCL then SYSTEM_CURRENCY_START.";
        JLOG(j_.warn()) << "CRNRoundImpl::doValidation. drops in LCL: " << static_cast<uint64_t>(lastClosedLedger->info().drops.drops());
        return;
    }
    if (lastClosedLedger->info().drops > totalCoinsInCirculation)
    {
        JLOG(j_.warn()) << "CRNRoundImpl::doValidation. more drops in LCL then in circulation.";
        JLOG(j_.warn()) << "CRNRoundImpl::doValidation. drops in LCL: "
                        << static_cast<uint64_t>(lastClosedLedger->info().drops.drops())
                        << " in circulation: " <<
                           static_cast<uint64_t>(totalCoinsInCirculation.drops());
        return;
    }
    // get the CRN Settings
    boost::optional<CRN_SettingsDescriptor> crnSettings = getCRNSettings(lastClosedLedger, j_);
    if(crnSettings)
    {
     JLOG(j_.info()) << "CRNRoundImpl::doVoting. CRNSettings Foundation Account: " << toBase58(calcAccountID(crnSettings->foundationFeesPublicKey))
                     << " - Foundation Percentage: " << crnSettings->foundationFeeFactor;
    }

    uint64_t maxDistribution = CSCAmount(totalCoinsInCirculation - lastClosedLedger->info().drops).drops();
    JLOG(j_.info()) << "CRNRoundImpl::doVoting. validations: " << parentValidations.size()
                    << " voting range: 0 - " << maxDistribution;
    JLOG(j_.info()) << "CRNRoundImpl::doVoting. Total coins in circulation: " << totalCoinsInCirculation.drops();

    detail::VotableInteger<std::int64_t> feeToDistribute (0, maxDistribution);
    auto crnVote = std::make_unique<NodesEligibilitySet>();

    // based on other votes, conclude what in our POV elibigible nodes should look like
    for ( auto const& singleValidation : parentValidations)
    {
        if (!(singleValidation.second->isTrusted()))
            continue;
        CRN::EligibilityMap singleNodePosition;
        if (singleValidation.second->isFieldPresent(sfCRNs))
        {
            // get all votes for CRNs of given validator
            STArray const& crnVotesOfNode =
                    singleValidation.second->getFieldArray(sfCRNs);
            for ( auto voteOfNodeIter = crnVotesOfNode.begin(); voteOfNodeIter != crnVotesOfNode.end(); ++voteOfNodeIter)
            {
                STObject const& crnSTObject = *voteOfNodeIter;
                // *voteOfNodeIter is a single STObject containing CRN vote data
                if (crnSTObject.isFieldPresent(sfCRN_PublicKey) && crnSTObject.isFieldPresent(sfCRNEligibility))
                {
                    PublicKey crnPubKey(Slice(crnSTObject.getFieldVL(sfCRN_PublicKey).data(), crnSTObject.getFieldVL(sfCRN_PublicKey).size()));
                    bool crnEligibility = (crnSTObject.getFieldU8(sfCRNEligibility) > 0) ? true : false;

                    singleNodePosition.insert(
                                std::pair<PublicKey, bool>(crnPubKey, crnEligibility));
                }
            }
        }
        if (singleValidation.second->isFieldPresent(sfCRN_FeeDistributed))
        {

            JLOG(j_.info()) << "CRNRoundImpl::doVoting got validation with fee vote: "
                            << singleValidation.second->getFieldAmount(sfCRN_FeeDistributed).csc().drops();
            feeToDistribute.addVote(singleValidation.second->getFieldAmount(sfCRN_FeeDistributed).csc().drops());
        }
        else
        {
            feeToDistribute.noVote();
        }
        crnVote->tally (singleNodePosition);
    }
    crnVote->mThreshold = std::max(1, (crnVote->mTrustedValidations * majorityFraction_) / 256);
    crnVote->setFeeDistributionVote(CSCAmount(feeToDistribute.getVotes()));
    crnVote->setVotingFinished(crnSettings);

    JLOG (j_.info()) <<
        "Received " << crnVote->mTrustedValidations <<
        " trusted validations, threshold is: " << crnVote->mThreshold;
    JLOG (j_.info()) <<
        " feeDistribution. our position: " << lastFeeDistributionPosition_.drops() <<
        " concluded vote: " << crnVote->feeDistributionVote().drops();


    {
        auto const seq = lastClosedLedger->info().seq + 1;
        mCRNRoundLedgerSeq = seq;

        STArray crnArray(sfCRNs);
        STAmount feeToDistributeST(crnVote->feeDistributionVote());

        CRN::EligibilityPaymentMap txVoteMap = crnVote->votes();
        if (txVoteMap.size() == 0)
        {
            JLOG(j_.warn()) << "No nodes eligible for payout. giving up this time";
            return;
        }

        // extra sanity check. verify that math won't overflow
        {
            CSCAmount sumOfDistribution(beast::zero);
            for ( auto iter = txVoteMap.begin(); iter != txVoteMap.end(); ++iter)
            {
                CSCAmount before = sumOfDistribution;
                sumOfDistribution += iter->second;
                if (before > sumOfDistribution)
                {
                    JLOG(j_.error()) << "Math overflow. sumBefore: " << before.drops()
                                    << " after adding: " << iter->second.drops()
                                    << " sum: " << sumOfDistribution.drops()
                                    << " quit!";
                    return;
                }
            }
            if (sumOfDistribution != feeToDistributeST.csc())
            {
                JLOG(j_.error()) << "Math error. sumOfDistribution: " << sumOfDistribution.drops()
                                << " feeToDistributeST: " << feeToDistributeST.csc().drops()
                                << " quit!";
                return;
            }
        }
        for ( auto iter = txVoteMap.begin(); iter != txVoteMap.end(); ++iter)
        {
            crnArray.push_back (STObject (sfCRN));
            auto& entry = crnArray.back ();
            entry.emplace_back (STBlob (sfCRN_PublicKey, iter->first.data(), iter->first.size()));
            STAmount crnFeeDistributed(iter->second);
            crnFeeDistributed.setFName(sfCRN_FeeDistributed);
            entry.emplace_back (crnFeeDistributed);
        }

        JLOG(j_.warn()) << "We are voting for a CRNEligibility";

        STTx crnRoundTx (ttCRN_ROUND,
            [seq, crnArray, feeToDistributeST](auto& obj)
            {
                obj[sfAccount] = AccountID();
                obj[sfLedgerSequence] = seq;
                obj[sfCRN_FeeDistributed] = feeToDistributeST;
                obj.setFieldArray(sfCRNs, crnArray);
            });

        uint256 txID = crnRoundTx.getTransactionID ();

        JLOG(j_.warn()) << "CRNRound tx id: " << to_string (txID);

        Serializer s;
        crnRoundTx.add (s);

        auto tItem = std::make_shared<SHAMapItem> (txID, s.peekData ());

        if (!initialPosition->addGiveItem (tItem, true, false))
        {
            JLOG(j_.warn()) << "Ledger already had crn eligibility vote change";
        }
    }
    lastVote_ = std::move(crnVote);
}

void CRNRoundImpl::updatePosition(std::list<STPerformanceReport::pointer> const& reports)
{
    if (!app_.getLedgerMaster().getValidatedRules().enabled(featureCRN))
    {
        JLOG(j_.info()) << "CRNRoundImpl::updatePosition CRN feature is not enabled. aborting";
        return;
    }
    std::shared_ptr<const Ledger> closedLedger = app_.getLedgerMaster().getClosedLedger();
    if (!isCRNRoundsActivated( closedLedger, j_ ))
    {
        JLOG(j_.info()) << "CRNRoundImpl::updatePosition CRN Rounds are de-activated. aborting";
        return;
    }

    CSCAmount totalCoinsSupply(SYSTEM_CURRENCY_START);
    // jrojek: once we figure out the way to trace burn account two tx on ledger, we should substract it here
    CSCAmount totalCoinsInCirculation(totalCoinsSupply);
    if (closedLedger->info().drops > totalCoinsSupply)
    {
        JLOG(j_.warn()) << "CRNRoundImpl::updatePosition. more drops in LCL then SYSTEM_CURRENCY_START.";
        JLOG(j_.warn()) << "CRNRoundImpl::updatePosition. drops in LCL: " << static_cast<uint64_t>(closedLedger->info().drops.drops());
        return;
    }
    if (closedLedger->info().drops > totalCoinsInCirculation)
    {
        JLOG(j_.warn()) << "CRNRoundImpl::updatePosition. more drops in LCL then in circulation.";
        JLOG(j_.warn()) << "CRNRoundImpl::updatePosition. drops in LCL: "
                        << static_cast<uint64_t>(closedLedger->info().drops.drops())
                        << " in circulation: " <<
                           static_cast<uint64_t>(totalCoinsInCirculation.drops());
        return;
    }
    eligibilityMap_.clear();
    for (STPerformanceReport::ref report : reports)
    {
       // jrojek TODO: Unindent it!
       boost::optional<PublicKey> pk = report->getSignerPublic();
       bool eligible = true;

       // check if node is on CRNList
       if(app_.relaynodes().listed(*pk))
       {
           // check if signature is valid
           if (pk)
           {
               eligible &= casinocoin::verify(
                   *pk,
                   makeSlice(report->getFieldVL(sfCRN_DomainName)),
                   makeSlice(report->getFieldVL(sfSignature))
               );
           }
           else
           {
               eligible &= false;
               JLOG(j_.debug()) << "CRNRound - failed to read PubKey or signature of CRN candidate";
           }
           if(eligible)
           {
               // check if account is funded
               if (!CRNId::activated(*pk, app_.getLedgerMaster(), j_, app_.config()))
               {
                   JLOG(j_.debug()) << "CRNRound - Account " << toBase58(calcAccountID(*pk)) << " assigned to: " << toBase58(TOKEN_NODE_PUBLIC,*pk) << " has insufficient funds";
                   eligible &= false;
               }
               // check if latency is acceptable
               if(report->getLatency() > app_.config().CRN_MAX_LATENCY)
               {
                   JLOG(j_.debug()) << "CRNRound - Latency to high: " << toBase58(TOKEN_NODE_PUBLIC,*pk);
                   eligible &= false;
               }

               // check if time spent in full state satisfy min ratio
               const auto& performanceArray = report->getFieldArray(sfCRNPerformance);
               std::uint64_t sum {};
               std::uint32_t fullTime {};
               std::map<int, std::string> statusNames
               {
                   {1, "connecting"},
                   {2, "connected"},
                   {3, "monitoring"},
                   {4, "validating"},
                   {5, "shutting"}
               };

               for (const auto& field : performanceArray)
               {
                   auto dur = field.getFieldU32(sfDuration);
                   sum += dur;
                   auto mode = static_cast<protocol::NodeStatus>(field.getFieldU8(sfStatusMode) + 1);
                   if (mode == protocol::NodeStatus::nsMONITORING
                        || mode == protocol::NodeStatus::nsVALIDATING)
                   {
                       fullTime += dur;
                   }
               }

               auto prevReportLedgerSeq = app_.getLedgerMaster().getCurrentLedgerIndex() - CRNPerformance::getReportingPeriod();
               if (prevReportLedgerSeq > 0)
               {
                    auto lastReportedLedger = app_.getLedgerMaster().getLedgerBySeq(prevReportLedgerSeq);

                    if (lastReportedLedger)
                    {
                        auto now = app_.timeKeeper().now().time_since_epoch();
                        auto closed = lastReportedLedger->info().closeTime.time_since_epoch();

                        sum = now.count() - closed.count();
                        sum = sum >= 0 ? sum : 0;
                    }
               }

               if (sum)
               {
                   long double ratio = fullTime / static_cast<long double>(sum);
                   eligible &= (ratio > app_.config().CRN_MIN_FULL_STATE_RATIO);
                   JLOG(j_.debug()) << "CRNRound - Full time ratio: " << fullTime << "/" << sum << " = " << ratio << " " <<  toBase58(TOKEN_NODE_PUBLIC, *pk);
               }
               else
               {
                   JLOG(j_.debug()) << "CRNRound - Eligible == false: time sum == 0 " <<  toBase58(TOKEN_NODE_PUBLIC, *pk);
                   eligible &= false;
               }
           }
           else
           {
               JLOG(j_.debug()) << "CRNRound - Signature is invalid: " << toBase58(TOKEN_NODE_PUBLIC,*pk);
           }
       }
       else
       {
           JLOG(j_.debug()) << "CRNRound - PublicKey not in CRNList: " << toBase58(TOKEN_NODE_PUBLIC,*pk);
           eligible &= false;
       }
       JLOG(j_.info()) << "CRNRound - PublicKey: " << toBase58(TOKEN_NODE_PUBLIC,*pk) << " Eligible:" << eligible;
       eligibilityMap_.insert(std::pair<PublicKey, bool>(*pk, eligible));

    }
    JLOG (j_.info()) <<
        "CRNRoundImpl::updatePosition with " << eligibilityMap_.size() << " candidates";
    for (auto const& entry : eligibilityMap_)
    {
        JLOG(j_.debug()) <<
            "CRNRoundImpl::updatePosition map entry PubKey: " << toBase58(TOKEN_NODE_PUBLIC,entry.first) << " Eligible:" << entry.second;
    }
}

std::unique_ptr<CRNRound> make_CRNRound(Application& app, int majorityFraction, beast::Journal journal)
{
    return std::make_unique<CRNRoundImpl> (app, majorityFraction, journal);
}


}  // casinocoin
