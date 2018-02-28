//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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
    2017-06-30  ajochems        Refactored for casinocoin
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/app/ledger/LedgerMaster.h>
#include <casinocoin/protocol/ErrorCodes.h>
#include <casinocoin/protocol/Feature.h>
#include <casinocoin/resource/Fees.h>
#include <casinocoin/rpc/Context.h>
#include <casinocoin/rpc/impl/TransactionSign.h>

namespace casinocoin {

// {
//   SigningAccounts <array>,
//   tx_json: <object>,
// }
Json::Value doSubmitMultiSigned (RPC::Context& context)
{
    // Bail if multisign is not enabled.
    if (! context.app.getLedgerMaster().getValidatedRules().
        enabled (featureMultiSign))
    {
        RPC::inject_error (rpcNOT_ENABLED, context.params);
        return context.params;
    }
    context.loadType = Resource::feeHighBurdenRPC;
    auto const failHard = context.params[jss::fail_hard].asBool();
    auto const failType = NetworkOPs::doFailHard (failHard);

    // jrojek 28.02.2018 enrich tx_json with clientIP
    if (context.params.isMember (jss::tx_json))
    {
        std::string clientIPStr = context.clientAddress.address().to_string();
        Json::Value ipAddress(strHex(clientIPStr.begin(), clientIPStr.size()));
        context.params[jss::tx_json][jss::ClientIP] = ipAddress;
    }

    return RPC::transactionSubmitMultiSigned (
        context.params,
        failType,
        context.role,
        context.ledgerMaster.getValidatedLedgerAge(),
        context.app,
        RPC::getProcessTxnFn (context.netOps));
}

} // casinocoin
