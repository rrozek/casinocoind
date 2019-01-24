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
#include <casinocoin/protocol/ErrorCodes.h>
#include <casinocoin/resource/Fees.h>
#include <casinocoin/rpc/Context.h>

#include <casinocoin/rpc/impl/RPCHelpers.h>
#include <casinocoin/basics/StringUtilities.h>
#include <casinocoin/crypto/ECIES.h>

namespace casinocoin {

// {
//   secret: <secret>,
//   dest_public_key_hex: <pub_key>
//   message: <string>,
// }
Json::Value doEncryptMsg (RPC::Context& context)
{
    auto j = context.app.journal("RPCHandler");

    if (! context.params.isMember (jss::message))
        return RPC::missing_field_error (jss::message);
    if (! context.params.isMember (jss::dest_public_key_hex))
        return RPC::missing_field_error (jss::dest_public_key_hex);
    if (! context.params.isMember (jss::secret))
        return RPC::missing_field_error (jss::secret);
    JLOG(j.debug()) << "doEncryptMsg: " << context.params;

    Json::Value jvResult;
    auto const keypair = RPC::keypairForSignature (context.params, jvResult);
    if (RPC::contains_error (jvResult))
        return std::move (jvResult);

    auto unHexedPubKey = strUnHex(context.params[jss::dest_public_key_hex].asString());
    if (!unHexedPubKey.second)
        return RPC::make_param_error("dest_public_key_hex is malformed");

    std::string msg = context.params[jss::message].asString();
    Serializer s(msg.data(), msg.size());

    Blob encryptedMsgBlob;
    try
    {
        uint256 secretInt = uint256::fromVoid(keypair.second.data());
        encryptedMsgBlob = encryptECIES(secretInt, unHexedPubKey.first, s.peekData());
    }
    catch (std::runtime_error const& error)
    {
        JLOG(j.info()) << "doEncryptMsg: thrown " << error.what();
        return RPC::make_param_error("cannot encrypt msg");
    }
    if (encryptedMsgBlob.empty())
    {
        JLOG(j.info()) << "doEncryptMsg: result is empty";
        return RPC::make_param_error("result is empty");
    }

    Slice pubKeySlice = keypair.first.slice();

    jvResult[jss::message] = context.params[jss::message];
    jvResult[jss::encrypted_message] = Json::Value(strHex(encryptedMsgBlob.data(), encryptedMsgBlob.size()));
    jvResult[jss::dest_public_key_hex] = context.params[jss::dest_public_key_hex];
    jvResult[jss::src_public_key_hex] = Json::Value(strHex(pubKeySlice.data(), pubKeySlice.size()));

    return jvResult;
}

} // casinocoin
