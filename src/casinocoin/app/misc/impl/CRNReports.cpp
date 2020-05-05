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
    2018-07-20  jrojek          Created
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/app/misc/CRNReports.h>
#include <casinocoin/core/DatabaseCon.h>
#include <casinocoin/app/ledger/LedgerMaster.h>
#include <casinocoin/consensus/LedgerTiming.h>
#include <casinocoin/app/main/Application.h>
#include <casinocoin/app/misc/NetworkOPs.h>
#include <casinocoin/app/misc/CRNList.h>
#include <casinocoin/app/misc/CRNPerformance.h>
#include <casinocoin/basics/Log.h>
#include <casinocoin/basics/StringUtilities.h>
#include <casinocoin/basics/chrono.h>
#include <casinocoin/core/JobQueue.h>
#include <casinocoin/core/TimeKeeper.h>
#include <memory>
#include <mutex>
#include <thread>

namespace casinocoin {

class CRNReportsImp : public CRNReports
{
private:
    using LockType = std::mutex;
    using ScopedLockType = std::lock_guard <LockType>;
    using ScopedUnlockType = GenericScopedUnlock <LockType>;

    Application& app_;
    std::mutex mutable mLock;

    CRNReportSet mCurrentCRNReports;

    beast::Journal j_;

public:
    explicit
    CRNReportsImp (Application& app)
        : app_ (app)
        , j_ (app.journal ("CRNReports"))
    {
    }

private:
    bool addReport (STPerformanceReport::ref report, std::string const& source) override
    {
        auto crnPubKey = report->getSignerPublic ();
        auto lastLedgerIndex = report->getLastLedgerIndex ();
        bool isCurrent = current (report);

        bool isListed = app_.relaynodes().listed(crnPubKey);

        if (!isListed)
        {
            JLOG (j_.debug()) <<
                "Node " << toBase58 (TokenType::TOKEN_NODE_PUBLIC, crnPubKey) <<
                " not in UNL st=" << report->getSignTime().time_since_epoch().count() <<
                ", lastLedgerIndex=" << lastLedgerIndex <<
                ", shash=" << report->getSigningHash () <<
                " src=" << source;
        }

        if (isCurrent && isListed)
        {
            ScopedLockType sl (mLock);
            auto it = mCurrentCRNReports.find (crnPubKey);

            if (it == mCurrentCRNReports.end ())
            {
                // No previous report from this node
                mCurrentCRNReports.emplace (crnPubKey, report);
            }
            else if (!it->second)
            {
                // Previous report has expired
                it->second = report;
            }
            else
            {
                auto const oldSeq = (*it->second)[~sfLastLedgerSequence];
                auto const newSeq = (*report)[~sfLastLedgerSequence];

                if (oldSeq && newSeq && *oldSeq == *newSeq)
                {
                    JLOG (j_.warn()) <<
                        "Listed node " <<
                        toBase58 (TokenType::TOKEN_NODE_PUBLIC, crnPubKey) <<
                        " published multiple CRNReports for ledger " <<
                        *oldSeq;
                }

                if (report->getSignTime () > it->second->getSignTime () ||
                    crnPubKey != it->second->getSignerPublic())
                {
                    // This is either a newer CRNReport or a new signing key
                    JLOG (j_.debug()) << "Newer report or report with new signing key received";
                    it->second = report;
                }
                else
                {
                    // We already have a newer report from this source
                    JLOG (j_.warn()) << "We already have newer report from this source";
                    isCurrent = false;
                }
            }
        }

        JLOG (j_.debug()) <<
            "Performance report for " << lastLedgerIndex <<
            " from " << toBase58 (TokenType::TOKEN_NODE_PUBLIC, crnPubKey) <<
            " added " << (isListed ? "confirmedListed/" : "UNconfirmedNOTListed/") <<
            (isCurrent ? "current" : "stale");

        if (isListed && isCurrent)
            return true;

        return false;
    }

    bool current (STPerformanceReport::ref report) const override
    {
        // Because this can be called on untrusted, possibly
        // malicious CRNReports, we do our math in a way
        // that avoids any chance of overflowing or underflowing
        // the report time.

        LedgerIndex const validatedLedgerIndex = app_.getLedgerMaster().getValidLedgerIndex();
        LedgerIndex const reportFinalLedgerIndex = static_cast<LedgerIndex>(report->getLastLedgerIndex());
        LedgerIndex const bigger = std::max(validatedLedgerIndex, reportFinalLedgerIndex);
        LedgerIndex const smaller = std::min(validatedLedgerIndex, reportFinalLedgerIndex);

        bool retVal = (bigger - smaller) < CRNPerformance::getReportingPeriod();
        JLOG (j_.debug()) << "CRNReportsImp::current() validated ledger: "
                          << validatedLedgerIndex
                          << " finalLedgerInReport: "
                          << reportFinalLedgerIndex
                          << " max offest: "
                          << CRNPerformance::getReportingStartOffset()
                          << " return: " << retVal;

        return retVal;
    }

    std::list<STPerformanceReport::pointer> getCurrentReports () override
    {
        std::list<STPerformanceReport::pointer> ret;

        ScopedLockType sl (mLock);
        auto it = mCurrentCRNReports.begin ();

        while (it != mCurrentCRNReports.end ())
        {
            if (!it->second) // contains no record
                it = mCurrentCRNReports.erase (it);
            else if (! current (it->second))
            {
                // contains a stale record
                it->second.reset ();
                it = mCurrentCRNReports.erase (it);
            }
            else
            {
                // contains a live record
                ret.push_back (it->second);
                ++it;
            }
        }

        return ret;
    }

    hash_set<PublicKey> getCurrentPublicKeys () override
    {
        hash_set<PublicKey> ret;

        ScopedLockType sl (mLock);
        auto it = mCurrentCRNReports.begin ();

        while (it != mCurrentCRNReports.end ())
        {
            if (!it->second) // contains no record
                it = mCurrentCRNReports.erase (it);
            else if (! current (it->second))
            {
                // contains a stale record
                it->second.reset ();
                it = mCurrentCRNReports.erase (it);
            }
            else
            {
                // contains a live record
                ret.insert (it->first);
                ++it;
            }
        }

        return ret;
    }

    void flush () override
    {
        mCurrentCRNReports.clear ();
        JLOG (j_.debug()) << "CRNReports flushed";
    }
};

std::unique_ptr <CRNReports> make_CRNReports (Application& app)
{
    return std::make_unique <CRNReportsImp> (app);
}

} // casinocoin
