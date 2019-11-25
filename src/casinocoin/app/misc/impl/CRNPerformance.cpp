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
    2018-05-15  jrojek          Created
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/app/ledger/OpenLedger.h>
#include <casinocoin/app/main/Application.h>
#include <casinocoin/overlay/Overlay.h>
#include <casinocoin/overlay/Message.h>
#include <casinocoin/overlay/predicates.h>
#include <casinocoin/app/misc/CRNPerformance.h>
#include <casinocoin/app/misc/CRNId.h>
#include <casinocoin/app/misc/Transaction.h>


namespace casinocoin {

CRNPerformance::CRNPerformance(
        NetworkOPs& networkOps,
        LedgerIndex const& startupSeq,
        CRNId const& crnId,
        beast::Journal journal)
    : networkOps(networkOps)
    , lastSnapshotSeq_(startupSeq)
    , id(crnId)
    , j_(journal)
{
}

STPerformanceReport::pointer
CRNPerformance::prepareReport (
    LedgerIndex const& lastClosedLedgerSeq,
    Application &app)
{
    if ((lastSnapshotSeq_ + CRNPerformance::getReportingStartOffset()) > lastClosedLedgerSeq)
    {
        JLOG(j_.trace()) << "CRNPerformance::prepareReport lclSeq " << lastClosedLedgerSeq << " report already send in " << lastSnapshotSeq_;
        return nullptr;
    }

    std::lock_guard<std::mutex> sl(recentLock_);
    protocol::NodeStatus currentStatus = networkOps.getNodeStatus();
    std::array<StatusAccounting::Counters, 5> counters =
            mapServerAccountingToPeerAccounting(networkOps.getServerAccountingInfo());


    auto signTime = app.timeKeeper().closeTime();
    std::shared_ptr<STPerformanceReport> report;
    try
    {
        report = std::make_shared<STPerformanceReport>(
                                                      signTime,
                                                      id.publicKey(),
                                                      id.domainBlob(),
                                                      id.signatureBlob());
    }
    catch (std::exception const& e)
    {
        JLOG(j_.info()) << "CRNPerformance::prepareReport exception creating performance report"
                                << e.what();
        return nullptr;
    }

    report->setFieldU16(sfPort, id.wsPort()); // WS port for peers command reporting and node validation
    report->setFieldVL(sfPublicKey, app.nodeIdentity().first.slice()); // node public key for peers command

    STArray performanceArray (sfCRNPerformance);
    for (uint32_t i = 0; i < 5; i++)
    {
        StatusAccounting::Counters counterToReport;
        counterToReport.dur = std::chrono::duration_cast<std::chrono::seconds>(counters[i].dur - lastSnapshot_[i].dur);
        counterToReport.transitions = counters[i].transitions - lastSnapshot_[i].transitions;

        lastSnapshot_[i].dur = counters[i].dur;
        lastSnapshot_[i].transitions = counters[i].transitions;

        performanceArray.push_back (STObject (sfCRNStatus));
        auto& entry = performanceArray.back ();
        entry.emplace_back (STUInt8 (sfStatusMode, i));
        entry.emplace_back (STUInt32 (sfTransitions, counterToReport.transitions));
        entry.emplace_back (STUInt32(sfDuration, counterToReport.dur.count()));
    }

    report->setFieldU8 ( sfCRNActivated, (id.activated() ? 1 : 0) );
    report->setFieldArray (sfCRNPerformance, performanceArray);
    report->setFieldU8 (sfStatusMode, currentStatus - 1);
    report->setFieldU32 (sfFirstLedgerSequence, lastSnapshotSeq_);
    report->setFieldU32 (sfLastLedgerSequence, lastClosedLedgerSeq);
    lastSnapshotSeq_ = lastClosedLedgerSeq;

    // jrojek TODO? for now latency reported is the minimum latency to sane peers
    // (since latency reported by Peer is already averaged from last 8 mesaurements
    uint32_t myLatency = std::numeric_limits<uint32_t>::max();
    {
        auto peerList = app.overlay().getActivePeers();
        for (auto const& peer : peerList)
        {
            if (peer->sanity() == Peer::Sanity::sane)
                myLatency = std::min(myLatency, peer->latency());
        }
    }
    latency_ = myLatency;
    report->setFieldU32( sfCRN_LatencyAvg, latency_);

    return report;

}

void CRNPerformance::broadcast(STPerformanceReport::ref report, Application& app)
{
    std::lock_guard<std::mutex> sl(recentLock_);
    Blob sreport = report->getSerialized();
    protocol::TMPerformanceReport performanceReport;
    performanceReport.set_report(&sreport[0], sreport.size());
    // Send signed report to all of our directly connected peers
    app.overlay().send(performanceReport);
}

std::array<CRNPerformance::StatusAccounting::Counters, 5> CRNPerformance::mapServerAccountingToPeerAccounting(std::array<NetworkOPs::StateAccounting::Counters, 5> const& serverAccounting)
{
    std::array<StatusAccounting::Counters, 5> ret;
    for (uint32_t i = 0; i < 5; i++)
    {
        auto &entry = ret[(networkOps.getNodeStatus(static_cast<NetworkOPs::OperatingMode>(i))) - 1];
        entry.dur += std::chrono::seconds(serverAccounting[i].dur.count() / 1000 / 1000);
        entry.transitions += serverAccounting[i].transitions;
    }
    return ret;
}

//-------------------------------------------------------------------------------------
static std::array<char const*, 5> const statusNames {{
    "connecting",
    "connected",
    "monitoring",
    "validating",
    "shutting"}};

std::array<Json::StaticString const, 5> const
CRNPerformance::StatusAccounting::statuses_ = {{
    Json::StaticString(statusNames[0]),
    Json::StaticString(statusNames[1]),
    Json::StaticString(statusNames[2]),
    Json::StaticString(statusNames[3]),
    Json::StaticString(statusNames[4])}};

CRNPerformance::StatusAccounting::StatusAccounting(beast::Journal journal)
    : j_(journal)
{
    mode_ = protocol::nsCONNECTING;
    counters_[protocol::nsCONNECTING-1].transitions = 1;
    start_ = std::chrono::system_clock::now();
}

void CRNPerformance::StatusAccounting::reset()
{
    std::unique_lock<std::mutex> lock (mutex_);

    for (auto counter : counters_)
        counter.reset();
    start_ = std::chrono::system_clock::now();
}

void CRNPerformance::StatusAccounting::mode (protocol::NodeStatus nodeStatus)
{
    if (nodeStatus == mode_)
        return;

    JLOG(j_.debug()) << "changing operating mode from " << mode_ << " to " << nodeStatus << " trans before: " << counters_[nodeStatus-1].transitions;
    auto now = std::chrono::system_clock::now();

    std::lock_guard<std::mutex> lock (mutex_);
    ++counters_[nodeStatus-1].transitions;
    counters_[mode_-1].dur += std::chrono::duration_cast<
        std::chrono::seconds>(now - start_);

    JLOG(j_.debug()) << "trans after: " << counters_[nodeStatus-1].transitions;
    mode_ = nodeStatus;
    start_ = now;
}

Json::Value CRNPerformance::StatusAccounting::json() const
{
    auto counters = snapshot();

    Json::Value ret = Json::objectValue;

    for (std::underlying_type_t<protocol::NodeStatus> i = protocol::nsCONNECTING;
        i <= protocol::nsSHUTTING; ++i)
    {
        uint8_t index = i-1;
        ret[statuses_[index]] = Json::objectValue;
        auto& status = ret[statuses_[index]];
        status[jss::transitions] = counters[index].transitions;
        status[jss::duration_sec] = std::to_string (counters[index].dur.count());
    }

    return ret;
}

std::array<CRNPerformance::StatusAccounting::Counters, 5> CRNPerformance::StatusAccounting::snapshot() const
{
    std::unique_lock<std::mutex> lock (mutex_);

    auto counters = counters_;
    auto const start = start_;
    auto const mode = mode_;

    lock.unlock();

    counters[mode-1].dur += std::chrono::duration_cast<
        std::chrono::seconds>(std::chrono::system_clock::now() - start);

    return counters;
}

}
