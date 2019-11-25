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
    2018-05-10  ajochems        CRN Creation
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/basics/Log.h>
#include <casinocoin/crypto/KeyType.h>
#include <casinocoin/net/RPCErr.h>
#include <casinocoin/protocol/ErrorCodes.h>
#include <casinocoin/protocol/JsonFields.h>
#include <casinocoin/protocol/Seed.h>
#include <casinocoin/protocol/PublicKey.h>
#include <casinocoin/rpc/Context.h>
#include <casinocoin/app/misc/CRN.h>
#include <casinocoin/app/misc/CRNRound.h>
#include <casinocoin/app/misc/CRNList.h>
#include <casinocoin/app/misc/CRNListUpdater.h>
#include <casinocoin/protocol/ConfigObjectEntry.h>

namespace casinocoin {

// {
//   domain_name: <string>   // CRN Domain Name
// }
//
// This command requires Role::ADMIN access because it makes
// no sense to ask an untrusted server for this.
Json::Value doCRNCreate (RPC::Context& context)
{
    if (! context.params.isMember (jss::crn_domain_name))
        return RPC::missing_field_error (jss::crn_domain_name);

    Json::Value obj (Json::objectValue);
    KeyType keyType = KeyType::secp256k1;
    auto seed = randomSeed();
    std::pair<PublicKey,SecretKey> keyPair = generateKeyPair (keyType, seed);

    auto const publicKey = keyPair.first;
    auto const secretKey = keyPair.second;

    obj[jss::crn_seed] = toBase58 (seed);
    obj[jss::crn_key] = seedAs1751 (seed);
    obj[jss::crn_account_id] = toBase58(calcAccountID(publicKey));
    obj[jss::crn_public_key] = toBase58(TOKEN_NODE_PUBLIC, publicKey);
    obj[jss::crn_private_key] = toBase58(TOKEN_NODE_PRIVATE, secretKey);
    obj[jss::crn_key_type] = to_string (keyType);
    obj[jss::crn_public_key_hex] = strHex (publicKey.data(), publicKey.size());
    obj[jss::crn_domain_name] = context.params[jss::crn_domain_name].asString();
    auto const signature = casinocoin::sign (publicKey, secretKey, makeSlice (strHex(context.params[jss::crn_domain_name].asString())));
    obj[jss::crn_domain_signature] = strHex(signature.data(), signature.size());
    return obj;
}

Json::Value doCRNVerify (RPC::Context& context)
{
    if (! context.params.isMember (jss::crn_domain_name))
        return RPC::missing_field_error (jss::crn_domain_name);
    if (! context.params.isMember (jss::crn_domain_signature))
        return RPC::missing_field_error (jss::crn_domain_signature);
    if (! context.params.isMember (jss::crn_public_key))
        return RPC::missing_field_error (jss::crn_public_key);

    Json::Value obj (Json::objectValue);

    bool verifyResult = false;

    std::string domainName = context.params[jss::crn_domain_name].asString();
    auto unHexedSignature = strUnHex(context.params[jss::crn_domain_signature].asString());
    boost::optional<PublicKey> publicKey = parseBase58<PublicKey>(TokenType::TOKEN_NODE_PUBLIC, context.params[jss::crn_public_key].asString());

    if (unHexedSignature.second && publicKey)
    {
        JLOG(context.j.info()) << "Do Verify";
        verifyResult = casinocoin::verify(
          *publicKey,
          makeSlice(strHex(domainName)),
          makeSlice(unHexedSignature.first));
    }


    JLOG(context.j.info()) << "Domain: " << domainName << " Result: " << verifyResult;

    obj[jss::crn_public_key] = toBase58(TokenType::TOKEN_NODE_PUBLIC, *publicKey);
    obj[jss::crn_valid] = verifyResult;
    return obj;

    // auto signature = strUnHex(context.params[jss::crn_domain_signature].asString());
    // std::string domainName = context.params[jss::crn_domain_name].asString();
    // boost::optional<PublicKey> publicKey = parseBase58<PublicKey>(TokenType::TOKEN_NODE_PUBLIC, context.params[jss::crn_public_key].asString());

    // bool verifyResult = casinocoin::verify(*publicKey, makeSlice (strHex(domainName)), makeSlice (signature.first));
    // JLOG(context.j.info()) << "Domain: " << domainName << " Result: " << verifyResult;

    // obj[jss::crn_public_key] = toBase58(TokenType::TOKEN_NODE_PUBLIC, *publicKey);
    // obj[jss::crn_valid] = verifyResult;
    // return obj;
}

Json::Value doCRNInfo (RPC::Context& context)
{
    // get the CRNRound object from the last validated ledger
    auto const valLedger = context.ledgerMaster.getValidatedLedger();
    if (valLedger)
    {
        // check if CRNRounds are activated on the ledger config
        bool activated = isCRNRoundsActivated(valLedger, context.j);
        JLOG(context.j.info()) << "CRN ConfigInfo Activated: " << activated;
        // read the last crn round from ledger
        auto const crnRound = valLedger->read(keylet::crnRound());
        // create json output
        Json::Value jvReply = Json::objectValue;
        // show CRNRound info if we have some
        if (crnRound)
        {
            jvReply[jss::crn_rounds] = (crnRound->getFieldU32(sfLedgerSequence) > 0);
            jvReply[jss::seq] = Json::UInt (valLedger->info().seq);
            jvReply[jss::hash] = to_string (valLedger->info().hash);
            jvReply[jss::crn_fee_distributed] = crnRound->getFieldAmount(sfCRN_FeeDistributed).getText();
            jvReply[jss::total_coins] = to_string (valLedger->info().drops);
            jvReply[jss::crn_last_ledger] = Json::UInt (crnRound->getFieldU32(sfLedgerSequence) );
            auto&& array = Json::setArray (jvReply, jss::crns);
            // loop over CRN array
            STArray crnArray = crnRound->getFieldArray(sfCRNs);
            for ( auto const& crnObject : crnArray)
            {
                JLOG(context.j.debug()) << "CRN: " << crnObject.getJson(0);
                // Format Public Key
                Blob pkBlob = crnObject.getFieldVL(sfCRN_PublicKey);
                PublicKey crnPubKey(Slice(pkBlob.data(), pkBlob.size()));
                AccountID dstAccountID = calcAccountID(crnPubKey);
                JLOG(context.j.debug()) << "CRN PublicKey: " << toBase58 (TokenType::TOKEN_NODE_PUBLIC, crnPubKey);

                auto&& obj = appendObject(array);
                obj[jss::crn_public_key] = toBase58 (TokenType::TOKEN_NODE_PUBLIC, crnPubKey);
                obj[jss::crn_account_id] = toBase58(dstAccountID);
                obj[jss::crn_fee_distributed] = crnObject.getFieldAmount(sfCRN_FeeDistributed).getText();

            }
            // get the last 10 node fee transactions and add them
            STVector256 crnTxHistory = crnRound->getFieldV256(sfCRNTxHistory);
            auto&& feeTxArray = Json::setArray (jvReply, jss::crn_fee_txs);
            auto txCounter = crnTxHistory.size() - 11;
            for (unsigned i = txCounter; i++ < (crnTxHistory.size()-1); )
            {
                feeTxArray.append(to_string(crnTxHistory[i]));                
            }

            // check if we are a CRN
            if(context.app.isCRN()){
                jvReply[jss::crn_public_key] = toBase58 (TokenType::TOKEN_NODE_PUBLIC, context.app.getCRN().id().publicKey());
                auto const account = calcAccountID(context.app.getCRN().id().publicKey());
                jvReply[jss::crn_account_id] = toBase58(account);
                jvReply[jss::crn_domain_name] = context.app.getCRN().id().domain();
                jvReply[jss::crn_activated] = context.app.getCRN().id().activated();
            }
        }
        else
        {
            jvReply[jss::crn_rounds] = true;
        }
        if(context.app.isValidator())
        {
            // add site info
            jvReply[jss::crn_update_sites] = context.app.crnListUpdater().getJson();
            // add node info
            jvReply[jss::crn_nodes] = context.app.relaynodes().getJson();
        }
        // return result
        return jvReply;
    }
    else
        return RPC::make_error(casinocoin::error_code_i::rpcLGR_NOT_VALIDATED);
}

} // casinocoin
