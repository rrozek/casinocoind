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
//   src_public_key_hex: <pub_key>
//   encrypted_message: <string>,
// }
Json::Value doDecryptMsg (RPC::Context& context)
{
    auto j = context.app.journal("RPCHandler");

    if (! context.params.isMember (jss::encrypted_message))
        return RPC::missing_field_error (jss::encrypted_message);
    if (! context.params.isMember (jss::src_public_key_hex))
        return RPC::missing_field_error (jss::src_public_key_hex);
    if (! context.params.isMember (jss::secret))
        return RPC::missing_field_error (jss::secret);
    JLOG(j.debug()) << "doDecryptMsg: " << context.params;

    Json::Value jvResult;
    auto const keypair = RPC::keypairForSignature (context.params, jvResult);
    if (RPC::contains_error (jvResult))
        return std::move (jvResult);

    auto unHexedPubKey = strUnHex(context.params[jss::src_public_key_hex].asString());
    if (!unHexedPubKey.second)
        return RPC::make_param_error("src_public_key_hex is malformed");

    auto unHexedEncryptedMsg = strUnHex(context.params[jss::encrypted_message].asString());
    if (!unHexedEncryptedMsg.second)
        return RPC::make_param_error("encrypted_message is malformed");

    Serializer s(unHexedEncryptedMsg.first.data(), unHexedEncryptedMsg.first.size());

    Blob decryptedMsgBlob;
    try
    {
        uint256 secretInt = uint256::fromVoid(keypair.second.data());
        decryptedMsgBlob = decryptECIES(secretInt, unHexedPubKey.first, s.peekData());
    }
    catch (std::runtime_error const& error)
    {
        JLOG(j.info()) << "doDecryptMsg: thrown " << error.what();
        return RPC::make_param_error("cannot decrypt msg");
    }
    if (decryptedMsgBlob.empty())
    {
        JLOG(j.info()) << "doDecryptMsg: result is empty";
        return RPC::make_param_error("result is empty");
    }

    std::string decryptedMsg(decryptedMsgBlob.begin(), decryptedMsgBlob.end());

    Slice pubKeySlice = keypair.first.slice();

    jvResult[jss::encrypted_message] = context.params[jss::encrypted_message];
    jvResult[jss::message] = Json::Value(decryptedMsg);
    jvResult[jss::src_public_key_hex] = context.params[jss::src_public_key_hex];
    jvResult[jss::dest_public_key_hex] = Json::Value(strHex(pubKeySlice.data(), pubKeySlice.size()));

    return jvResult;
}

} // casinocoin
