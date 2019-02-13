//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
    2017-06-25  ajochems        Refactored for casinocoin
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/app/consensus/CCLConsensus.h>
#include <casinocoin/app/ledger/InboundLedgers.h>
#include <casinocoin/app/ledger/InboundTransactions.h>
#include <casinocoin/app/ledger/LedgerMaster.h>
#include <casinocoin/app/ledger/LocalTxs.h>
#include <casinocoin/app/ledger/OpenLedger.h>
#include <casinocoin/app/misc/AmendmentTable.h>
#include <casinocoin/app/misc/HashRouter.h>
#include <casinocoin/app/misc/LoadFeeTrack.h>
#include <casinocoin/app/misc/NetworkOPs.h>
#include <casinocoin/app/misc/TxQ.h>
#include <casinocoin/app/misc/ValidatorList.h>
#include <casinocoin/app/misc/VotableConfiguration.h>
#include <casinocoin/app/tx/apply.h>
#include <casinocoin/basics/make_lock.h>
#include <casinocoin/beast/core/LexicalCast.h>
#include <casinocoin/consensus/LedgerTiming.h>
#include <casinocoin/overlay/Overlay.h>
#include <casinocoin/overlay/predicates.h>
#include <casinocoin/protocol/Feature.h>
#include <casinocoin/protocol/digest.h>

namespace casinocoin {

CCLConsensus::CCLConsensus(
    Application& app,
    std::unique_ptr<FeeVote>&& feeVote,
    LedgerMaster& ledgerMaster,
    LocalTxs& localTxs,
    InboundTransactions& inboundTransactions,
    typename Base::clock_type const& clock,
    beast::Journal journal)
    : Base(clock, journal)
    , app_(app)
    , feeVote_(std::move(feeVote))
    , ledgerMaster_(ledgerMaster)
    , localTxs_(localTxs)
    , inboundTransactions_{inboundTransactions}
    , j_(journal)
    , nodeID_{calcNodeID(app.nodeIdentity().first)}
{
}

boost::optional<CCLCxLedger>
CCLConsensus::acquireLedger(LedgerHash const& ledger)
{
    // we need to switch the ledger we're working from
    auto buildLCL = ledgerMaster_.getLedgerByHash(ledger);
    if (!buildLCL)
    {
        if (acquiringLedger_ != ledger)
        {
            // need to start acquiring the correct consensus LCL
            JLOG(j_.warn()) << "Need consensus ledger " << ledger;

            // Tell the ledger acquire system that we need the consensus ledger
            acquiringLedger_ = ledger;

            auto app = &app_;
            auto hash = acquiringLedger_;
            app_.getJobQueue().addJob(
                jtADVANCE, "getConsensusLedger", [app, hash](Job&) {
                    app->getInboundLedgers().acquire(
                        hash, 0, InboundLedger::fcCONSENSUS);
                });
        }
        return boost::none;
    }

    assert(!buildLCL->open() && buildLCL->isImmutable());
    assert(buildLCL->info().hash == ledger);

    // Notify inbound transactions of the new ledger sequence number
    inboundTransactions_.newRound(buildLCL->info().seq);

    return CCLCxLedger(buildLCL);
}

std::vector<CCLCxPeerPos>
CCLConsensus::proposals(LedgerHash const& prevLedger)
{
    std::vector<CCLCxPeerPos> ret;
    {
        std::lock_guard<std::mutex> _(peerPositionsLock_);

        for (auto const& it : peerPositions_)
            for (auto const& pos : it.second)
                if (pos->proposal().prevLedger() == prevLedger)
                    ret.emplace_back(*pos);
    }

    return ret;
}

void
CCLConsensus::storeProposal(CCLCxPeerPos::ref peerPos, NodeID const& nodeID)
{
    std::lock_guard<std::mutex> _(peerPositionsLock_);

    auto& props = peerPositions_[nodeID];

    if (props.size() >= 10)
        props.pop_front();

    props.push_back(peerPos);
}

void
CCLConsensus::relay(CCLCxPeerPos const& peerPos)
{
    protocol::TMProposeSet prop;

    auto const& proposal = peerPos.proposal();

    prop.set_proposeseq(proposal.proposeSeq());
    prop.set_closetime(proposal.closeTime().time_since_epoch().count());

    prop.set_currenttxhash(
        proposal.position().begin(), proposal.position().size());
    prop.set_previousledger(
        proposal.prevLedger().begin(), proposal.position().size());

    auto const pk = peerPos.getPublicKey().slice();
    prop.set_nodepubkey(pk.data(), pk.size());

    auto const sig = peerPos.getSignature();
    prop.set_signature(sig.data(), sig.size());

    app_.overlay().relay(prop, peerPos.getSuppressionID());
}

void
CCLConsensus::relay(CCLCxTx const& tx)
{
    // If we didn't relay this transaction recently, relay it to all peers
    if (app_.getHashRouter().shouldRelay(tx.id()))
    {
        auto const slice = tx.tx_.slice();
        protocol::TMTransaction msg;
        msg.set_rawtransaction(slice.data(), slice.size());
        msg.set_status(protocol::tsNEW);
        msg.set_receivetimestamp(
            app_.timeKeeper().now().time_since_epoch().count());
        app_.overlay().foreach (send_always(
            std::make_shared<Message>(msg, protocol::mtTRANSACTION)));
    }
}
void
CCLConsensus::propose(CCLCxPeerPos::Proposal const& proposal)
{
    JLOG(j_.trace()) << "We propose: "
                     << (proposal.isBowOut()
                             ? std::string("bowOut")
                             : casinocoin::to_string(proposal.position()));

    protocol::TMProposeSet prop;

    prop.set_currenttxhash(
        proposal.position().begin(), proposal.position().size());
    prop.set_previousledger(
        proposal.prevLedger().begin(), proposal.position().size());
    prop.set_proposeseq(proposal.proposeSeq());
    prop.set_closetime(proposal.closeTime().time_since_epoch().count());

    prop.set_nodepubkey(valPublic_.data(), valPublic_.size());

    auto signingHash = sha512Half(
        HashPrefix::proposal,
        std::uint32_t(proposal.proposeSeq()),
        proposal.closeTime().time_since_epoch().count(),
        proposal.prevLedger(),
        proposal.position());

    auto sig = signDigest(valPublic_, valSecret_, signingHash);

    prop.set_signature(sig.data(), sig.size());

    app_.overlay().send(prop);
}

void
CCLConsensus::relay(CCLTxSet const& set)
{
    inboundTransactions_.giveSet(set.id(), set.map_, false);
}

boost::optional<CCLTxSet>
CCLConsensus::acquireTxSet(CCLTxSet::ID const& setId)
{
    if (auto set = inboundTransactions_.getSet(setId, true))
    {
        return CCLTxSet{std::move(set)};
    }
    return boost::none;
}

bool
CCLConsensus::hasOpenTransactions() const
{
    return !app_.openLedger().empty();
}

std::size_t
CCLConsensus::proposersValidated(LedgerHash const& h) const
{
    return app_.getValidations().getTrustedValidationCount(h);
}

std::size_t
CCLConsensus::proposersFinished(LedgerHash const& h) const
{
    return app_.getValidations().getNodesAfter(h);
}

uint256
CCLConsensus::getPrevLedger(
    uint256 ledgerID,
    CCLCxLedger const& ledger,
    Mode mode)
{
    uint256 parentID;
    // Only set the parent ID if we believe ledger is the right ledger
    if (mode != Mode::wrongLedger)
        parentID = ledger.parentID();

    // Get validators that are on our ledger, or "close" to being on
    // our ledger.
    auto vals = app_.getValidations().getCurrentValidations(
        ledgerID, parentID, ledgerMaster_.getValidLedgerIndex());

    uint256 netLgr = ledgerID;
    int netLgrCount = 0;
    for (auto& it : vals)
    {
        // Switch to ledger supported by more peers
        // Or stick with ours on a tie
        if ((it.second.first > netLgrCount) ||
            ((it.second.first == netLgrCount) && (it.first == ledgerID)))
        {
            netLgr = it.first;
            netLgrCount = it.second.first;
        }
    }

    if (netLgr != ledgerID)
    {
        if (mode != Mode::wrongLedger)
            app_.getOPs().consensusViewChange();

        if (auto stream = j_.debug())
        {
            for (auto& it : vals)
                stream << "V: " << it.first << ", " << it.second.first;
            stream << getJson(true);
        }
    }

    return netLgr;
}

CCLConsensus::Result
CCLConsensus::onClose(
    CCLCxLedger const& ledger,
    NetClock::time_point const& closeTime,
    Mode mode)
{
    const bool wrongLCL = mode == Mode::wrongLedger;
    const bool proposing = mode == Mode::proposing;

    notify(protocol::neCLOSING_LEDGER, ledger, !wrongLCL);

    auto const& prevLedger = ledger.ledger_;

    ledgerMaster_.applyHeldTransactions();
    // Tell the ledger master not to acquire the ledger we're probably building
    ledgerMaster_.setBuildingLedger(prevLedger->info().seq + 1);

    auto initialLedger = app_.openLedger().current();

    auto initialSet = std::make_shared<SHAMap>(
        SHAMapType::TRANSACTION, app_.family(), SHAMap::version{1});
    initialSet->setUnbacked();

    // Build SHAMap containing all transactions in our open ledger
    for (auto const& tx : initialLedger->txs)
    {
        Serializer s(2048);
        tx.first->add(s);
        initialSet->addItem(
            SHAMapItem(tx.first->getTransactionID(), std::move(s)),
            true,
            false);
    }

    // Add pseudo-transactions to the set
    if ((app_.config().standalone() || (proposing && !wrongLCL)) &&
        ((prevLedger->info().seq % 256) == 0))
    {
        // previous ledger was flag ledger, add pseudo-transactions
        auto const validations =
            app_.getValidations().getValidations(prevLedger->info().parentHash);

        std::size_t const count = std::count_if(
            validations.begin(), validations.end(), [](auto const& v) {
                return v.second->isTrusted();
            });

        if (count >= app_.validators().quorum())
        {
            feeVote_->doVoting(prevLedger, validations, initialSet);
            app_.getAmendmentTable().doVoting(
                prevLedger, validations, initialSet);

            if (ledger.ledger_->rules().enabled(featureConfigObject))
            {
                app_.getVotableConfig().doVoting(
                    prevLedger, validations, initialSet);
            }
        }
    }

    // Now we need an immutable snapshot
    initialSet = initialSet->snapShot(false);
    auto setHash = initialSet->getHash().as_uint256();

    return Result{
        std::move(initialSet),
        CCLCxPeerPos::Proposal{
            initialLedger->info().parentHash,
            CCLCxPeerPos::Proposal::seqJoin,
            setHash,
            closeTime,
            app_.timeKeeper().closeTime(),
            nodeID_}};
}

void
CCLConsensus::onForceAccept(
    Result const& result,
    CCLCxLedger const& prevLedger,
    NetClock::duration const& closeResolution,
    CloseTimes const& rawCloseTimes,
    Mode const& mode)
{
    doAccept(result, prevLedger, closeResolution, rawCloseTimes, mode);
}

void
CCLConsensus::onAccept(
    Result const& result,
    CCLCxLedger const& prevLedger,
    NetClock::duration const& closeResolution,
    CloseTimes const& rawCloseTimes,
    Mode const& mode)
{
    app_.getJobQueue().addJob(
        jtACCEPT, "acceptLedger", [&, that = this->shared_from_this() ](auto&) {
            // note that no lock is held inside this thread, which
            // is fine since once a ledger is accepted, consensus
            // will not touch any internal state until startRound is called
            that->doAccept(
                result, prevLedger, closeResolution, rawCloseTimes, mode);
            that->app_.getOPs().endConsensus();
        });
}

void
CCLConsensus::doAccept(
    Result const& result,
    CCLCxLedger const& prevLedger,
    NetClock::duration closeResolution,
    CloseTimes const& rawCloseTimes,
    Mode const& mode)
{
    bool closeTimeCorrect;

    const bool proposing = mode == Mode::proposing;
    const bool haveCorrectLCL = mode != Mode::wrongLedger;
    const bool consensusFail = result.state == ConsensusState::MovedOn;

    auto consensusCloseTime = result.position.closeTime();

    if (consensusCloseTime == NetClock::time_point{})
    {
        // We agreed to disagree on the close time
        consensusCloseTime = prevLedger.closeTime() + 1s;
        closeTimeCorrect = false;
    }
    else
    {
        // We agreed on a close time
        consensusCloseTime = effCloseTime(
            consensusCloseTime, closeResolution, prevLedger.closeTime());
        closeTimeCorrect = true;
    }

    JLOG(j_.debug()) << "Report: Prop=" << (proposing ? "yes" : "no")
                     << " val=" << (validating_ ? "yes" : "no")
                     << " corLCL=" << (haveCorrectLCL ? "yes" : "no")
                     << " fail=" << (consensusFail ? "yes" : "no");
    JLOG(j_.debug()) << "Report: Prev = " << prevLedger.id() << ":"
                     << prevLedger.seq();

    //--------------------------------------------------------------------------
    // Put transactions into a deterministic, but unpredictable, order
    CanonicalTXSet retriableTxs{result.set.id()};

    auto sharedLCL = buildLCL(
        prevLedger,
        result.set,
        consensusCloseTime,
        closeTimeCorrect,
        closeResolution,
        result.roundTime.read(),
        retriableTxs);

    auto const newLCLHash = sharedLCL.id();
    JLOG(j_.debug()) << "Report: NewL  = " << newLCLHash << ":"
                     << sharedLCL.seq();

    // Tell directly connected peers that we have a new LCL
    notify(protocol::neACCEPTED_LEDGER, sharedLCL, haveCorrectLCL);

    if (validating_)
        validating_ = ledgerMaster_.isCompatible(
            *sharedLCL.ledger_,
            app_.journal("LedgerConsensus").warn(),
            "Not validating");

    if (validating_ && !consensusFail)
    {
        validate(sharedLCL, proposing);
        JLOG(j_.info()) << "CNF Val " << newLCLHash;
    }
    else
        JLOG(j_.info()) << "CNF buildLCL " << newLCLHash;

    // See if we can accept a ledger as fully-validated
    ledgerMaster_.consensusBuilt(sharedLCL.ledger_, getJson(true));

    //-------------------------------------------------------------------------
    {
        // Apply disputed transactions that didn't get in
        //
        // The first crack of transactions to get into the new
        // open ledger goes to transactions proposed by a validator
        // we trust but not included in the consensus set.
        //
        // These are done first because they are the most likely
        // to receive agreement during consensus. They are also
        // ordered logically "sooner" than transactions not mentioned
        // in the previous consensus round.
        //
        bool anyDisputes = false;
        for (auto& it : result.disputes)
        {
            if (!it.second.getOurVote())
            {
                // we voted NO
                try
                {
                    JLOG(j_.debug())
                        << "Test applying disputed transaction that did"
                        << " not get in";

                    SerialIter sit(it.second.tx().tx_.slice());
                    auto txn = std::make_shared<STTx const>(sit);

                    // Disputed pseudo-transactions that were not accepted
                    // can't be succesfully applied in the next ledger
                    if (isPseudoTx(*txn))
                        continue;

                    retriableTxs.insert(txn);

                    anyDisputes = true;
                }
                catch (std::exception const&)
                {
                    JLOG(j_.debug())
                        << "Failed to apply transaction we voted NO on";
                }
            }
        }

        // Build new open ledger
        auto lock = make_lock(app_.getMasterMutex(), std::defer_lock);
        auto sl = make_lock(ledgerMaster_.peekMutex(), std::defer_lock);
        std::lock(lock, sl);

        auto const lastVal = ledgerMaster_.getValidatedLedger();
        boost::optional<Rules> rules;
        if (lastVal)
            rules.emplace(*lastVal, app_.config().features);
        else
            rules.emplace(app_.config().features);
        app_.openLedger().accept(
            app_,
            *rules,
            sharedLCL.ledger_,
            localTxs_.getTxSet(),
            anyDisputes,
            retriableTxs,
            tapNONE,
            "consensus",
            [&](OpenView& view, beast::Journal j) {
                // Stuff the ledger with transactions from the queue.
                return app_.getTxQ().accept(app_, view);
            });

        // Signal a potential fee change to subscribers after the open ledger
        // is created
        app_.getOPs().reportFeeChange();
    }

    //-------------------------------------------------------------------------
    {
        ledgerMaster_.switchLCL(sharedLCL.ledger_);

        // Do these need to exist?
        assert(ledgerMaster_.getClosedLedger()->info().hash == sharedLCL.id());
        assert(
            app_.openLedger().current()->info().parentHash == sharedLCL.id());
    }

    //-------------------------------------------------------------------------
    // we entered the round with the network,
    // see how close our close time is to other node's
    //  close time reports, and update our clock.
    if ((mode == Mode::proposing || mode == Mode::observing) && !consensusFail)
    {
        auto closeTime = rawCloseTimes.self;

        JLOG(j_.info()) << "We closed at "
                        << closeTime.time_since_epoch().count();
        using usec64_t = std::chrono::duration<std::uint64_t>;
        usec64_t closeTotal =
            std::chrono::duration_cast<usec64_t>(closeTime.time_since_epoch());
        int closeCount = 1;

        for (auto const& p : rawCloseTimes.peers)
        {
            // FIXME: Use median, not average
            JLOG(j_.info())
                << std::to_string(p.second) << " time votes for "
                << std::to_string(p.first.time_since_epoch().count());
            closeCount += p.second;
            closeTotal += std::chrono::duration_cast<usec64_t>(
                              p.first.time_since_epoch()) *
                p.second;
        }

        closeTotal += usec64_t(closeCount / 2);  // for round to nearest
        closeTotal /= closeCount;

        // Use signed times since we are subtracting
        using duration = std::chrono::duration<std::int32_t>;
        using time_point = std::chrono::time_point<NetClock, duration>;
        auto offset = time_point{closeTotal} -
            std::chrono::time_point_cast<duration>(closeTime);
        JLOG(j_.info()) << "Our close offset is estimated at " << offset.count()
                        << " (" << closeCount << ")";

        app_.timeKeeper().adjustCloseTime(offset);
    }
}

void
CCLConsensus::notify(
    protocol::NodeEvent ne,
    CCLCxLedger const& ledger,
    bool haveCorrectLCL)
{
    protocol::TMStatusChange s;

    if (!haveCorrectLCL)
        s.set_newevent(protocol::neLOST_SYNC);
    else
        s.set_newevent(ne);

    s.set_ledgerseq(ledger.seq());
    s.set_networktime(app_.timeKeeper().now().time_since_epoch().count());
    s.set_ledgerhashprevious(
        ledger.parentID().begin(),
        std::decay_t<decltype(ledger.parentID())>::bytes);
    s.set_ledgerhash(
        ledger.id().begin(), std::decay_t<decltype(ledger.id())>::bytes);

    std::uint32_t uMin, uMax;
    if (!ledgerMaster_.getFullValidatedRange(uMin, uMax))
    {
        uMin = 0;
        uMax = 0;
    }
    else
    {
        // Don't advertise ledgers we're not willing to serve
        uMin = std::max(uMin, ledgerMaster_.getEarliestFetch());
    }
    s.set_firstseq(uMin);
    s.set_lastseq(uMax);
    app_.overlay().foreach (
        send_always(std::make_shared<Message>(s, protocol::mtSTATUS_CHANGE)));
    JLOG(j_.trace()) << "send status change to peer";
}

/** Apply a set of transactions to a ledger.

  Typically the txFilter is used to reject transactions
  that already accepted in the prior ledger.

  @param set            set of transactions to apply
  @param view           ledger to apply to
  @param txFilter       callback, return false to reject txn
  @return               retriable transactions
*/

CanonicalTXSet
applyTransactions(
    Application& app,
    CCLTxSet const& cSet,
    OpenView& view,
    std::function<bool(uint256 const&)> txFilter)
{
    auto j = app.journal("LedgerConsensus");

    auto& set = *(cSet.map_);
    CanonicalTXSet retriableTxs(set.getHash().as_uint256());

    for (auto const& item : set)
    {
        if (!txFilter(item.key()))
            continue;

        // The transaction wan't filtered
        // Add it to the set to be tried in canonical order
        JLOG(j.debug()) << "Processing candidate transaction: " << item.key();
        try
        {
            retriableTxs.insert(
                std::make_shared<STTx const>(SerialIter{item.slice()}));
        }
        catch (std::exception const&)
        {
            JLOG(j.warn()) << "Txn " << item.key() << " throws";
        }
    }

    bool certainRetry = true;
    // Attempt to apply all of the retriable transactions
    for (int pass = 0; pass < LEDGER_TOTAL_PASSES; ++pass)
    {
        JLOG(j.debug()) << "Pass: " << pass << " Txns: " << retriableTxs.size()
                        << (certainRetry ? " retriable" : " final");
        int changes = 0;

        auto it = retriableTxs.begin();

        while (it != retriableTxs.end())
        {
            try
            {
                switch (applyTransaction(
                    app, view, *it->second, certainRetry, tapNO_CHECK_SIGN, j))
                {
                    case ApplyResult::Success:
                        it = retriableTxs.erase(it);
                        ++changes;
                        break;

                    case ApplyResult::Fail:
                        it = retriableTxs.erase(it);
                        break;

                    case ApplyResult::Retry:
                        ++it;
                }
            }
            catch (std::exception const&)
            {
                JLOG(j.warn()) << "Transaction throws";
                it = retriableTxs.erase(it);
            }
        }

        JLOG(j.debug()) << "Pass: " << pass << " finished " << changes
                        << " changes";

        // A non-retry pass made no changes
        if (!changes && !certainRetry)
            return retriableTxs;

        // Stop retriable passes
        if (!changes || (pass >= LEDGER_RETRY_PASSES))
            certainRetry = false;
    }

    // If there are any transactions left, we must have
    // tried them in at least one final pass
    assert(retriableTxs.empty() || !certainRetry);
    return retriableTxs;
}

CCLCxLedger
CCLConsensus::buildLCL(
    CCLCxLedger const& previousLedger,
    CCLTxSet const& set,
    NetClock::time_point closeTime,
    bool closeTimeCorrect,
    NetClock::duration closeResolution,
    std::chrono::milliseconds roundTime,
    CanonicalTXSet& retriableTxs)
{
    auto replay = ledgerMaster_.releaseReplay();
    if (replay)
    {
        // replaying, use the time the ledger we're replaying closed
        closeTime = replay->closeTime_;
        closeTimeCorrect = ((replay->closeFlags_ & sLCF_NoConsensusTime) == 0);
    }

    JLOG(j_.debug()) << "Report: TxSt = " << set.id() << ", close "
                     << closeTime.time_since_epoch().count()
                     << (closeTimeCorrect ? "" : "X");

    // Build the new last closed ledger
    auto buildLCL =
        std::make_shared<Ledger>(*previousLedger.ledger_, closeTime);

    auto const v2_enabled = buildLCL->rules().enabled(featureSHAMapV2);

    auto v2_transition = false;
    if (v2_enabled && !buildLCL->stateMap().is_v2())
    {
        buildLCL->make_v2();
        v2_transition = true;
    }

    // Set up to write SHAMap changes to our database,
    //   perform updates, extract changes
    JLOG(j_.debug()) << "Applying consensus set transactions to the"
                     << " last closed ledger";

    {
        OpenView accum(&*buildLCL);
        assert(!accum.open());
        if (replay)
        {
            // Special case, we are replaying a ledger close
            for (auto& tx : replay->txns_)
                applyTransaction(
                    app_, accum, *tx.second, false, tapNO_CHECK_SIGN, j_);
        }
        else
        {
            // Normal case, we are not replaying a ledger close
            retriableTxs = applyTransactions(
                app_, set, accum, [&buildLCL](uint256 const& txID) {
                    return !buildLCL->txExists(txID);
                });
        }
        // Update fee computations.
        app_.getTxQ().processClosedLedger(app_, accum, roundTime > 5s);
        accum.apply(*buildLCL);
    }

    // retriableTxs will include any transactions that
    // made it into the consensus set but failed during application
    // to the ledger.

    buildLCL->updateSkipList();

    {
        // Write the final version of all modified SHAMap
        // nodes to the node store to preserve the new LCL

        int asf = buildLCL->stateMap().flushDirty(
            hotACCOUNT_NODE, buildLCL->info().seq);
        int tmf = buildLCL->txMap().flushDirty(
            hotTRANSACTION_NODE, buildLCL->info().seq);
        JLOG(j_.debug()) << "Flushed " << asf << " accounts and " << tmf
                         << " transaction nodes";
    }
    buildLCL->unshare();

    // Accept ledger
    buildLCL->setAccepted(
        closeTime, closeResolution, closeTimeCorrect, app_.config());

    // And stash the ledger in the ledger master
    if (ledgerMaster_.storeLedger(buildLCL))
        JLOG(j_.debug()) << "Consensus built ledger we already had";
    else if (app_.getInboundLedgers().find(buildLCL->info().hash))
        JLOG(j_.debug()) << "Consensus built ledger we were acquiring";
    else
        JLOG(j_.debug()) << "Consensus built new ledger";
    return CCLCxLedger{std::move(buildLCL)};
}

void
CCLConsensus::validate(CCLCxLedger const& ledger, bool proposing)
{
    auto validationTime = app_.timeKeeper().closeTime();
    if (validationTime <= lastValidationTime_)
        validationTime = lastValidationTime_ + 1s;
    lastValidationTime_ = validationTime;

    // Build validation
    auto v = std::make_shared<STValidation>(
        ledger.id(), validationTime, valPublic_, proposing);
    v->setFieldU32(sfLedgerSequence, ledger.seq());

    // Add our load fee to the validation
    auto const& feeTrack = app_.getFeeTrack();
    std::uint32_t fee =
        std::max(feeTrack.getLocalFee(), feeTrack.getClusterFee());

    if (fee > feeTrack.getLoadBase())
        v->setFieldU32(sfLoadFee, fee);

    if (((ledger.seq() + 1) % 256) == 0)
    // next ledger is flag ledger
    {
        // Suggest fee changes and new features
        app_.config().reloadFeeVoteParams();
        feeVote_->updatePosition(setup_FeeVote(app_.config().section ("voting")));
        feeVote_->doValidation(ledger.ledger_, *v);
        app_.getAmendmentTable().doValidation(ledger.ledger_, *v);

        if (ledger.ledger_->rules().enabled(featureConfigObject))
        {
            Json::Value const& jvVotableConfig = app_.config().reloadConfigurationVoteParams();
            app_.getVotableConfig().updatePosition(jvVotableConfig);
            app_.getVotableConfig().doValidation(ledger.ledger_, *v);
        }
    }

    auto const signingHash = v->sign(valSecret_);
    v->setTrusted();
    // suppress it if we receive it - FIXME: wrong suppression
    app_.getHashRouter().addSuppression(signingHash);
    app_.getValidations().addValidation(v, "local");
    Blob validation = v->getSerialized();
    protocol::TMValidation val;
    val.set_validation(&validation[0], validation.size());
    // Send signed validation to all of our directly connected peers
    app_.overlay().send(val);
}

Json::Value
CCLConsensus::getJson(bool full) const
{
    auto ret = Base::getJson(full);
    ret["validating"] = validating_;
    return ret;
}

PublicKey const&
CCLConsensus::getValidationPublicKey() const
{
    return valPublic_;
}

void
CCLConsensus::setValidationKeys(
    SecretKey const& valSecret,
    PublicKey const& valPublic)
{
    valSecret_ = valSecret;
    valPublic_ = valPublic;
}

void
CCLConsensus::timerEntry(NetClock::time_point const& now)
{
    try
    {
        Base::timerEntry(now);
    }
    catch (SHAMapMissingNode const& mn)
    {
        // This should never happen
        leaveConsensus();
        JLOG(j_.error()) << "Missing node during consensus process " << mn;
        Rethrow();
    }
}

void
CCLConsensus::gotTxSet(NetClock::time_point const& now, CCLTxSet const& txSet)
{
    try
    {
        Base::gotTxSet(now, txSet);
    }
    catch (SHAMapMissingNode const& mn)
    {
        // This should never happen
        leaveConsensus();
        JLOG(j_.error()) << "Missing node during consensus process " << mn;
        Rethrow();
    }
}

void
CCLConsensus::startRound(
    NetClock::time_point const& now,
    CCLCxLedger::ID const& prevLgrId,
    CCLCxLedger const& prevLgr)
{
    // We have a key, and we have some idea what the ledger is
    validating_ =
        !app_.getOPs().isNeedNetworkLedger() && (valPublic_.size() != 0);

    // propose only if we're in sync with the network (and validating)
    bool proposing =
        validating_ && (app_.getOPs().getOperatingMode() == NetworkOPs::omFULL);

    if (validating_)
    {
        JLOG(j_.info()) << "Entering consensus process, validating";
    }
    else
    {
        // Otherwise we just want to monitor the validation process.
        JLOG(j_.info()) << "Entering consensus process, watching";
    }

    // Notify inbOund ledgers that we are starting a new round
    inboundTransactions_.newRound(prevLgr.seq());

    Base::startRound(now, prevLgrId, prevLgr, proposing);
}
}
