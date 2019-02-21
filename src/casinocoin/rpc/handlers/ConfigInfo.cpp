//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/casinocoin/casinocoind
    Copyright (c) 2019 Casinocoin Foundation.

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
    2019-01-18  jrojek        Created
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/app/ledger/LedgerMaster.h>
#include <casinocoin/app/misc/configuration/ConfigObjectEntry.h>
#include <casinocoin/protocol/ErrorCodes.h>
#include <casinocoin/resource/Fees.h>
#include <casinocoin/rpc/Context.h>

#include <casinocoin/rpc/impl/RPCHelpers.h>
#include <casinocoin/basics/StringUtilities.h>
#include <casinocoin/json/json_reader.h>
#include <stdexcept>

namespace casinocoin {

// {
//   ledger_hash: <ledger>
//   ledger_index: <ledger_index>
// }
Json::Value doConfigInfo (RPC::Context& context)
{
    auto j = context.app.journal("RPCHandler");

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger (ledger, context);

    if (!ledger)
        return result;

    auto const sleConfig = ledger->read(keylet::configuration());
    if (sleConfig)
    {
        if (sleConfig->isFieldPresent(sfConfiguration))
        {
            STArray ledgerConfigArray(sfConfiguration);
            ledgerConfigArray = sleConfig->getFieldArray(sfConfiguration);
            Json::Value configJson(Json::arrayValue);
            for (STArray::const_iterator iter = ledgerConfigArray.begin(); iter != ledgerConfigArray.end(); ++iter)
            {
                Json::Value singleObj(Json::objectValue);
                ConfigObjectEntry entry(j);
                if (!entry.fromBytes((*iter).getFieldVL(sfConfigData)))
                {
                    JLOG(j.info()) << "doConfigInfo parsing entry fromBytes failed";
                    RPC::inject_error(rpcLGR_IDX_MALFORMED, result);
                    break;;
                }

                if (!entry.toJson(singleObj))
                {
                    JLOG(j.info()) << "doConfigInfo converting entry toJson failed";
                    RPC::inject_error(rpcLGR_IDX_MALFORMED, result);
                    break;;
                }
                configJson.append(singleObj);
            }
            result[jss::configuration] = configJson;
        }
        else
        {
            JLOG(j.info()) << "doConfigInfo Configuration field in Configuration Ledger Object not found. return rpcLGR_IDX_MALFORMED";
            RPC::inject_error(rpcLGR_IDX_MALFORMED, result);
        }
    }
    else
    {
        JLOG(j.info()) << "doConfigInfo Configuration Ledger Object not found. return rpcLGR_IDXS_INVALID";
        RPC::inject_error (rpcLGR_IDXS_INVALID, result);
    }
    return result;
}

} // casinocoin
