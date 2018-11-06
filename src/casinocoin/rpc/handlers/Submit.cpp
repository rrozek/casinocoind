//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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
#include <casinocoin/app/misc/HashRouter.h>
#include <casinocoin/app/misc/Transaction.h>
#include <casinocoin/app/tx/apply.h>
#include <casinocoin/net/RPCErr.h>
#include <casinocoin/protocol/ErrorCodes.h>
#include <casinocoin/protocol/TxFlags.h>
#include <casinocoin/protocol/Feature.h>
#include <casinocoin/resource/Fees.h>
#include <casinocoin/rpc/Context.h>
#include <casinocoin/rpc/impl/TransactionSign.h>

namespace casinocoin {

static NetworkOPs::FailHard getFailHard (RPC::Context const& context)
{
    return NetworkOPs::doFailHard (
        context.params.isMember ("fail_hard")
        && context.params["fail_hard"].asBool ());
}

// {
//   tx_json: <object>,
//   secret: <secret>
// }
Json::Value doSubmit (RPC::Context& context)
{
    context.loadType = Resource::feeMediumBurdenRPC;

    if (!context.params.isMember (jss::tx_blob))
    {
        auto const failType = getFailHard (context);

// jrojek ip tracking postponed
//        if (context.params.isMember (jss::tx_json))
//        {
//            // only add it if KYC feature is enabled
//            if (context.app.getLedgerMaster().getValidatedRules().
//                enabled (featureKYC))
//            {
//                // jrojek 15.03.2018 only add if account is KYC-validated
//                AccountID uCallerAccount;
//                if (to_issuer(uCallerAccount, context.params[jss::tx_json][jss::Account].asString()))
//                {
//                    auto const callerAccountKeylet = keylet::account (uCallerAccount);
//                    std::shared_ptr<const casinocoin::STLedgerEntry> sleCaller = context.app.getLedgerMaster().getValidatedLedger()->read(callerAccountKeylet);
//                    if (sleCaller && (sleCaller->isFlag(lsfKYCValidated)))
//                    {
//                        // jrojek 28.02.2018 enrich tx_json with clientIP
//                        std::string clientIPStr = context.clientAddress.address().to_string();
//                        Json::Value ipAddress(strHex(clientIPStr.begin(), clientIPStr.size()));
//                        context.params[jss::tx_json][jss::ClientIP] = ipAddress;
//                    }
//                }
//            }
//        }

        return RPC::transactionSubmit (
        context.params,
        failType,
        context.role,
        context.ledgerMaster.getValidatedLedgerAge(),
        context.app,
        RPC::getProcessTxnFn (context.netOps));
    }

    Json::Value jvResult;

    std::pair<Blob, bool> ret(strUnHex (context.params[jss::tx_blob].asString ()));

    if (!ret.second || !ret.first.size ())
        return rpcError (rpcINVALID_PARAMS);

    SerialIter sitTrans (makeSlice(ret.first));

    std::shared_ptr<STTx const> stpTrans;

    try
    {
        stpTrans = std::make_shared<STTx const> (std::ref (sitTrans));
    }
    catch (std::exception& e)
    {
        jvResult[jss::error]        = "invalidTransaction";
        jvResult[jss::error_exception] = e.what ();

        return jvResult;
    }

// jrojek ip tracking postponed
// only add it if KYC feature is enabled
//    if (context.app.getLedgerMaster().getValidatedRules().
//        enabled (featureKYC))
//    {
//        // jrojek 15.03.2018 only add if account is KYC-validated
//        AccountID const uCallerAccount (stpTrans->getAccountID(sfAccount));
//        auto const callerAccountKeylet = keylet::account (uCallerAccount);
//        std::shared_ptr<const casinocoin::STLedgerEntry> sleCaller = context.app.getLedgerMaster().getValidatedLedger()->read(callerAccountKeylet);
//        if (sleCaller && (sleCaller->isFlag(lsfKYCValidated)))
//        {
//            std::string clientIPStr = context.clientAddress.address().to_string();
//            auto clientIPStrIter = clientIPStr.begin();
//            Blob ipAddress;
//            while (clientIPStrIter != clientIPStr.end())
//                ipAddress.push_back(*clientIPStrIter++);

//            STTx* stpTransUnconsted = const_cast<STTx*>(stpTrans.get());
//            stpTransUnconsted->setFieldVL(sfClientIP, ipAddress);
//        }
//    }

    {
        if (!context.app.checkSigs())
            forceValidity(context.app.getHashRouter(),
                stpTrans->getTransactionID(), Validity::SigGoodOnly);
        auto validity = checkValidity(context.app.getHashRouter(),
            *stpTrans, context.ledgerMaster.getCurrentLedger()->rules(),
                context.app.config());
        if (validity.first != Validity::Valid)
        {
            jvResult[jss::error] = "invalidTransaction";
            jvResult[jss::error_exception] = "fails local checks: " + validity.second;

            return jvResult;
        }
    }

    std::string reason;
    auto tpTrans = std::make_shared<Transaction> (
        stpTrans, reason, context.app);
    if (tpTrans->getStatus() != NEW)
    {
        jvResult[jss::error]            = "invalidTransaction";
        jvResult[jss::error_exception] = "fails local checks: " + reason;

        return jvResult;
    }

    try
    {
        auto const failType = getFailHard (context);

        context.netOps.processTransaction (
            tpTrans, isUnlimited (context.role), true, failType);
    }
    catch (std::exception& e)
    {
        jvResult[jss::error]           = "internalSubmit";
        jvResult[jss::error_exception] = e.what ();

        return jvResult;
    }


    try
    {
        jvResult[jss::tx_json] = tpTrans->getJson (0);
        jvResult[jss::tx_blob] = strHex (
            tpTrans->getSTransaction ()->getSerializer ().peekData ());

        if (temUNCERTAIN != tpTrans->getResult ())
        {
            std::string sToken;
            std::string sHuman;

            transResultInfo (tpTrans->getResult (), sToken, sHuman);

            jvResult[jss::engine_result]           = sToken;
            jvResult[jss::engine_result_code]      = tpTrans->getResult ();
            jvResult[jss::engine_result_message]   = sHuman;
        }

        return jvResult;
    }
    catch (std::exception& e)
    {
        jvResult[jss::error]           = "internalJson";
        jvResult[jss::error_exception] = e.what ();

        return jvResult;
    }
}

} // casinocoin
