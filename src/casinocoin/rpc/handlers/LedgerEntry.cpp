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

 
#include <casinocoin/app/main/Application.h>
#include <casinocoin/basics/strHex.h>
#include <casinocoin/basics/StringUtilities.h>
#include <casinocoin/ledger/ReadView.h>
#include <casinocoin/net/RPCErr.h>
#include <casinocoin/protocol/ErrorCodes.h>
#include <casinocoin/protocol/Indexes.h>
#include <casinocoin/protocol/JsonFields.h>
#include <casinocoin/rpc/Context.h>
#include <casinocoin/rpc/impl/RPCHelpers.h>

namespace casinocoin {

// {
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   ...
// }
Json::Value doLedgerEntry (RPC::Context& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger (lpLedger, context);

    if (!lpLedger)
        return jvResult;

    uint256     uNodeIndex;
    bool        bNodeBinary = false;

    if (context.params.isMember (jss::index))
    {
        // XXX Needs to provide proof.
        uNodeIndex.SetHex (context.params[jss::index].asString ());
        bNodeBinary = true;
    }
    else if (context.params.isMember (jss::account_root))
    {
        auto const account = parseBase58<AccountID>(
            context.params[jss::account_root].asString());
        if (! account || account->isZero())
            jvResult[jss::error]   = "malformedAddress";
        else
            uNodeIndex = keylet::account(*account).key;
    }
    else if (context.params.isMember (jss::directory))
    {
        if (context.params[jss::directory].isNull())
        {
            jvResult[jss::error]   = "malformedRequest";
        }
        else if (!context.params[jss::directory].isObject())
        {
            uNodeIndex.SetHex (context.params[jss::directory].asString ());
        }
        else if (context.params[jss::directory].isMember (jss::sub_index)
                 && !context.params[jss::directory][jss::sub_index].isIntegral ())
        {
            jvResult[jss::error]   = "malformedRequest";
        }
        else
        {
            std::uint64_t  uSubIndex
                    = context.params[jss::directory].isMember (jss::sub_index)
                    ? context.params[jss::directory][jss::sub_index].asUInt () : 0;

            if (context.params[jss::directory].isMember (jss::dir_root))
            {
                uint256 uDirRoot;

                uDirRoot.SetHex (context.params[jss::dir_root].asString ());

                uNodeIndex  = getDirNodeIndex (uDirRoot, uSubIndex);
            }
            else if (context.params[jss::directory].isMember (jss::owner))
            {
                auto const ownerID = parseBase58<AccountID>(
                    context.params[jss::directory][jss::owner].asString());

                if (! ownerID)
                {
                    jvResult[jss::error]   = "malformedAddress";
                }
                else
                {
                    uint256 uDirRoot = getOwnerDirIndex (*ownerID);
                    uNodeIndex  = getDirNodeIndex (uDirRoot, uSubIndex);
                }
            }
            else
            {
                jvResult[jss::error]   = "malformedRequest";
            }
        }
    }
    else if (context.params.isMember (jss::generator))
    {
        jvResult[jss::error]   = "deprecatedFeature";
    }
    else if (context.params.isMember (jss::offer))
    {
        if (!context.params[jss::offer].isObject())
        {
            uNodeIndex.SetHex (context.params[jss::offer].asString ());
        }
        else if (!context.params[jss::offer].isMember (jss::account)
                 || !context.params[jss::offer].isMember (jss::seq)
                 || !context.params[jss::offer][jss::seq].isIntegral ())
        {
            jvResult[jss::error]   = "malformedRequest";
        }
        else
        {
            auto const id = parseBase58<AccountID>(
                context.params[jss::offer][jss::account].asString());
            if (! id)
                jvResult[jss::error]   = "malformedAddress";
            else
                uNodeIndex  = getOfferIndex (*id,
                    context.params[jss::offer][jss::seq].asUInt ());
        }
    }
    else if (context.params.isMember (jss::casinocoin_state))
    {
        Currency         uCurrency;
        Json::Value     jvCasinocoinState   = context.params[jss::casinocoin_state];

        if (!jvCasinocoinState.isObject()
            || !jvCasinocoinState.isMember (jss::currency)
            || !jvCasinocoinState.isMember (jss::accounts)
            || !jvCasinocoinState[jss::accounts].isArray ()
            || 2 != jvCasinocoinState[jss::accounts].size ()
            || !jvCasinocoinState[jss::accounts][0u].isString ()
            || !jvCasinocoinState[jss::accounts][1u].isString ()
            || (jvCasinocoinState[jss::accounts][0u].asString ()
                == jvCasinocoinState[jss::accounts][1u].asString ())
           )
        {
            jvResult[jss::error]   = "malformedRequest";
        }
        else
        {
            auto const id1 = parseBase58<AccountID>(
                jvCasinocoinState[jss::accounts][0u].asString());
            auto const id2 = parseBase58<AccountID>(
                jvCasinocoinState[jss::accounts][1u].asString());
            if (! id1 || ! id2)
            {
                jvResult[jss::error]   = "malformedAddress";
            }
            else if (!to_currency (uCurrency,
                jvCasinocoinState[jss::currency].asString()))
            {
                jvResult[jss::error]   = "malformedCurrency";
            }
            else
            {
                uNodeIndex  = getCasinocoinStateIndex(
                    *id1, *id2, uCurrency);
            }
        }
    }
    else
    {
        jvResult[jss::error]   = "unknownOption";
    }

    if (uNodeIndex.isNonZero ())
    {
        auto const sleNode = lpLedger->read(keylet::unchecked(uNodeIndex));
        if (context.params.isMember(jss::binary))
            bNodeBinary = context.params[jss::binary].asBool();

        if (!sleNode)
        {
            // Not found.
            // XXX Should also provide proof.
            jvResult[jss::error]       = "entryNotFound";
        }
        else if (bNodeBinary)
        {
            // XXX Should also provide proof.
            Serializer s;

            sleNode->add (s);

            jvResult[jss::node_binary] = strHex (s.peekData ());
            jvResult[jss::index]       = to_string (uNodeIndex);
        }
        else
        {
            jvResult[jss::node]        = sleNode->getJson (0);
            jvResult[jss::index]       = to_string (uNodeIndex);
        }
    }

    return jvResult;
}

} // casinocoin

