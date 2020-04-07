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

#ifndef CASINOCOIN_APP_MISC_CRNPERFORMANCE_H
#define CASINOCOIN_APP_MISC_CRNPERFORMANCE_H

#include <casinocoin/app/ledger/Ledger.h>
#include <casinocoin/core/ConfigSections.h>
#include <casinocoin/overlay/impl/ProtocolMessage.h>
#include <casinocoin/app/misc/NetworkOPs.h>
#include <casinocoin/protocol/Protocol.h>
#include <casinocoin/protocol/STPerformanceReport.h>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <mutex>

namespace casinocoin {

class NetworkOPs;
class Application;

class CRNId;

class CRNPerformance
{
public:
    CRNPerformance (NetworkOPs& networkOps,
                    LedgerIndex const& startupSeq,
                    CRNId const& crnId,
                    beast::Journal journal);

    virtual ~CRNPerformance() = default;

    /**
     * ---------------
     * info methods
     * ---------------
    */
    // voting period
    static uint32_t getReportingPeriod() { return 64; }
    // start reporting offset in ledgers before voting
    static uint32_t getReportingStartOffset() { return 10; }

    STPerformanceReport::pointer
    prepareReport (
        LedgerIndex const& lastClosedLedgerSeq,
        Application &app);

    void broadcast (STPerformanceReport::ref report, Application &app);

    Json::Value json () const;

    /**
     * Status accounting records two attributes for each possible node status:
     * 1) Amount of time spent in each status (in seconds). This value is
     *    updated upon each status transition.
     * 2) Number of transitions to each status.
     *
     * This data can be polled through peers with 'details' attribute set.
     */
    class StatusAccounting
    {
    public:
        struct Counters
        {
            std::uint32_t transitions = 0;
            std::chrono::seconds dur = std::chrono::seconds (0);

            void reset()
            {
                transitions = 0;
                dur = std::chrono::seconds (0);
            }
        };

        explicit StatusAccounting (beast::Journal journal);

        /**
         * Reset status counters to their default state
         */
        void reset();

        /**
         * Record status transition. Update duration spent in previous
         * status.
         *
         * @param nodeStatus New status.
         */
        void mode (protocol::NodeStatus nodeStatus);

        /**
         * Output status counters in JSON format.
         *
         * @return JSON object.
         */
        Json::Value json() const;

        /**
         * Returns current status of counters + duration in current status
         *
         * @return array of Counters.
         */
        std::array<Counters, 5> snapshot() const;

        static std::array<Json::StaticString const, 5> const statuses_;
    private:
        mutable std::mutex mutex_;

        std::chrono::system_clock::time_point start_;
        protocol::NodeStatus mode_;
        std::array<Counters, 5> counters_;

        beast::Journal j_;

        static Json::StaticString const transitions_;
        static Json::StaticString const dur_;
    };


protected:
    NetworkOPs& networkOps;
    LedgerIndex lastSnapshotSeq_;
    CRNId const& id;
    LedgerIndex lclOfLastReport_;
    beast::Journal j_;

    std::array<StatusAccounting::Counters,5> lastSnapshot_;
    uint32_t latency_;

private:

    std::array<StatusAccounting::Counters,5>
    mapServerAccountingToPeerAccounting(std::array<NetworkOPs::StateAccounting::Counters, 5> const& serverAccounting);

    std::mutex mutable recentLock_;

};

}

#endif // CASINOCOIN_APP_MISC_CRNPERFORMANCE_H
