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

#ifndef CASINOCOIN_PROTOCOL_CRN_H
#define CASINOCOIN_PROTOCOL_CRN_H

#include <casinocoin/beast/utility/Journal.h>
#include <casinocoin/basics/Log.h>
#include <casinocoin/app/misc/CRNPerformance.h>
#include <casinocoin/app/misc/CRNId.h>
#include <casinocoin/overlay/impl/ProtocolMessage.h>
#include <casinocoin/protocol/STPerformanceReport.h>

namespace casinocoin {

class NetworkOPs;
class Application;

class CRN
{
public:
    using EligibilityMap = std::map<PublicKey, bool>;
    using EligibilityPaymentMap = std::map<PublicKey, CSCAmount>;

    CRN(PublicKey const& pubKey,
        std::string const& domain,
        std::string const& domainSignature,
        const uint16_t wsPort,
        NetworkOPs& networkOps,
        LedgerIndex const& startupSeq,
        beast::Journal j,
        Config &conf,
        LedgerMaster& ledgerMaster);

    CRN(Config &conf,
        NetworkOPs& networkOps,
        LedgerIndex const& startupSeq,
        beast::Journal j,
        LedgerMaster& ledgerMaster);

    Json::Value json() const;

    CRNId const& id() const;

    bool activated() const;

    STPerformanceReport::pointer
    prepareReport (
        LedgerIndex const& lastClosedLedgerSeq,
        Application& app);

    void broadcast (STPerformanceReport::ref report, Application& app);

    static const EligibilityMap eligibilityMapNone;
private:
    CRNId id_;
    CRNPerformance performance_;
    beast::Journal j_;
};

std::unique_ptr<CRN> make_CRN(Config &conf,
                              NetworkOPs& networkOps,
                              LedgerIndex const& startupSeq,
                              beast::Journal j,
                              LedgerMaster& ledgerMaster);

std::unique_ptr<CRN> make_CRN(PublicKey const& pubKey,
                              std::string const& domain,
                              std::string const& domainSignature,
                              uint16_t const& wsPort,
                              NetworkOPs& networkOps,
                              LedgerIndex const& startupSeq,
                              beast::Journal j,
                              Config &conf,
                              LedgerMaster& ledgerMaster);

}
#endif // CASINOCOIN_PROTOCOL_CRN_H

