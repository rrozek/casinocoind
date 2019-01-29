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

 
#include <casinocoin/app/ledger/LedgerMaster.h>
#include <casinocoin/protocol/ErrorCodes.h>
#include <casinocoin/resource/Fees.h>
#include <casinocoin/rpc/Context.h>

#include <casinocoin/rpc/impl/RPCHelpers.h>
#include <casinocoin/basics/StringUtilities.h>

namespace casinocoin {

// {
//   message: <string>,
//   signature: <sign>,
//   public_key_hex: <pub_key>
// }
Json::Value doVerifyMsg (RPC::Context& context)
{
    auto j = context.app.journal("RPCHandler");

    Json::Value jvResult;

    if (! context.params.isMember (jss::message))
        return RPC::missing_field_error (jss::message);
    if (! context.params.isMember (jss::public_key_hex))
        return RPC::missing_field_error (jss::public_key_hex);
    if (! context.params.isMember (jss::signature))
        return RPC::missing_field_error (jss::signature);

    bool validSignature = false;
    auto unHexedPubKey = strUnHex(context.params[jss::public_key_hex].asString());
    if (!unHexedPubKey.second)
        return RPC::make_param_error("public_key_hex is malformed");

    if (publicKeyType(makeSlice(unHexedPubKey.first)))
    {
        auto unHexedSignature = strUnHex(context.params[jss::signature].asString());
        if (!unHexedSignature.second)
            return RPC::make_param_error("signature is malformed");

        validSignature = verify(
          PublicKey(makeSlice(unHexedPubKey.first)),
          makeSlice(strHex(context.params[jss::message].asString())),
          makeSlice(unHexedSignature.first));
    }

    if (!validSignature)
        return RPC::make_param_error("invalid signature");

    jvResult[jss::message] = context.params[jss::message];
    return jvResult;
}

} // casinocoin
