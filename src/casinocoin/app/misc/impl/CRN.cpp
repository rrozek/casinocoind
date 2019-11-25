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
    2018-05-26  jrojek          Created
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/app/ledger/OpenLedger.h>
#include <casinocoin/app/main/Application.h>
#include <casinocoin/app/misc/NetworkOPs.h>
#include <casinocoin/overlay/Overlay.h>
#include <casinocoin/overlay/impl/PeerImp.h>
#include <casinocoin/overlay/Message.h>
#include <casinocoin/overlay/predicates.h>
#include <casinocoin/app/misc/CRN.h>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <mutex>

namespace casinocoin {

const CRN::EligibilityMap CRN::eligibilityMapNone = CRN::EligibilityMap();

CRN::CRN(const PublicKey &pubKey,
         const std::string &domain,
         const std::string &domainSignature,
         const uint16_t wsPort,
         NetworkOPs &networkOps,
         const LedgerIndex &startupSeq,
         beast::Journal j,
         Config &conf,
         LedgerMaster& ledgerMaster)
    : id_(pubKey, domain, domainSignature, wsPort, j, conf, ledgerMaster)
    , performance_(networkOps, startupSeq, id_, j)
    , j_(j)
{

}

CRN::CRN(Config &conf,
         NetworkOPs& networkOps,
         LedgerIndex const& startupSeq,
         beast::Journal j,
         LedgerMaster& ledgerMaster)
    : id_(conf, j, ledgerMaster)
    , performance_(networkOps, startupSeq, id_, j)
    , j_(j)
{

}

Json::Value CRN::json() const
{
    return id_.json();
}

CRNId const& CRN::id() const
{
    return id_;
}

bool CRN::activated() const
{
    bool crnActivated = false;
    JLOG(j_.info()) << "CRN Activated: " << crnActivated;
    return crnActivated;
}

STPerformanceReport::pointer
CRN::prepareReport(
    LedgerIndex const& lastClosedLedgerSeq,
    Application &app)
{
    return performance_.prepareReport(lastClosedLedgerSeq, app);
}

void CRN::broadcast(STPerformanceReport::ref report, Application &app)
{
    performance_.broadcast(report, app);
}

std::unique_ptr<CRN> make_CRN(Config &conf,
                              NetworkOPs &networkOps,
                              const LedgerIndex &startupSeq,
                              beast::Journal j,
                              LedgerMaster& ledgerMaster)
{
    return std::make_unique<CRN>(conf,
                                 networkOps,
                                 startupSeq,
                                 j,
                                 ledgerMaster);
}

std::unique_ptr<CRN> make_CRN(PublicKey const& pubKey,
                              std::string const& domain,
                              std::string const& domainSignature,
                              uint16_t const& wsPort,
                              NetworkOPs &networkOps,
                              LedgerIndex const& startupSeq,
                              beast::Journal j,
                              Config &conf,
                              LedgerMaster& ledgerMaster)
{
    return std::make_unique<CRN>(pubKey,
                                 domain,
                                 domainSignature,
                                 wsPort,
                                 networkOps,
                                 startupSeq,
                                 j,
                                 conf,
                                 ledgerMaster);
}


}
