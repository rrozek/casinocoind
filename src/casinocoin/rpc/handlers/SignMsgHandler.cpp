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
    2018-04-10  jrojek        Created
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/app/ledger/LedgerMaster.h>
#include <casinocoin/protocol/ErrorCodes.h>
#include <casinocoin/resource/Fees.h>
#include <casinocoin/rpc/Context.h>

#include <casinocoin/rpc/impl/RPCHelpers.h>
#include <casinocoin/basics/StringUtilities.h>

namespace casinocoin {

// {
//   message: <string>,
//   secret: <secret>
// }
Json::Value doSignMsg (RPC::Context& context)
{
    auto j = context.app.journal("RPCHandler");
    Json::Value jvResult;
    auto keypair = RPC::keypairForSignature(context.params, jvResult);
    if (RPC::contains_error (jvResult))
        return std::move (jvResult);

    if (! context.params.isMember (jss::message))
        return RPC::missing_field_error (jss::message);

    auto const signature = sign(
                keypair.first,
                keypair.second,
                makeSlice(strHex(context.params[jss::message].asString())));
    Slice pubKeySlice = keypair.first.slice();
    jvResult[jss::signature] = Json::Value(strHex(signature.data(), signature.size()));
    jvResult[jss::public_key_hex] = Json::Value(strHex(pubKeySlice.data(), pubKeySlice.size()));
    jvResult[jss::message] = context.params[jss::message];

    return jvResult;
}

} // casinocoin
