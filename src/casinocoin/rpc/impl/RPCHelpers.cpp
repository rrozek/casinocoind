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
#include <casinocoin/app/main/Application.h>
#include <casinocoin/app/misc/Transaction.h>
#include <casinocoin/ledger/View.h>
#include <casinocoin/net/RPCErr.h>
#include <casinocoin/protocol/AccountID.h>
#include <casinocoin/protocol/STTx.h>
#include <casinocoin/protocol/Feature.h>
#include <casinocoin/protocol/ConfigObjectEntry.h>
#include <casinocoin/rpc/Context.h>
#include <casinocoin/rpc/impl/RPCHelpers.h>
#include <boost/algorithm/string/case_conv.hpp>
#include <casinocoin/crypto/ECIES.h>
#include <casinocoin/basics/Log.h>

namespace casinocoin {
namespace RPC {

boost::optional<AccountID>
accountFromStringStrict(std::string const& account)
{
    boost::optional <AccountID> result;

    auto const publicKey = parseBase58<PublicKey> (
        TokenType::TOKEN_ACCOUNT_PUBLIC,
        account);

    if (publicKey)
        result = calcAccountID (*publicKey);
    else
        result = parseBase58<AccountID> (account);

    return result;
}

Json::Value
accountFromString(
    AccountID& result, std::string const& strIdent, bool bStrict)
{
    if (auto accountID = accountFromStringStrict (strIdent))
    {
        result = *accountID;
        return Json::objectValue;
    }

    if (bStrict)
    {
        auto id = deprecatedParseBitcoinAccountID (strIdent);
        return rpcError (id ? rpcACT_BITCOIN : rpcACT_MALFORMED);
    }

    // We allow the use of the seeds which is poor practice
    // and merely for debugging convenience.
    auto const seed = parseGenericSeed (strIdent);

    if (!seed)
        return rpcError (rpcBAD_SEED);

    auto const keypair = generateKeyPair (
        KeyType::secp256k1,
        *seed);

    result = calcAccountID (keypair.first);
    return Json::objectValue;
}

bool
getAccountObjects(ReadView const& ledger, AccountID const& account,
    LedgerEntryType const type, uint256 dirIndex, uint256 const& entryIndex,
    std::uint32_t const limit, Json::Value& jvResult)
{
    auto const rootDirIndex = getOwnerDirIndex (account);
    auto found = false;

    if (dirIndex.isZero ())
    {
        dirIndex = rootDirIndex;
        found = true;
    }

    auto dir = ledger.read({ltDIR_NODE, dirIndex});
    if (! dir)
        return false;

    std::uint32_t i = 0;
    auto& jvObjects = jvResult[jss::account_objects];
    for (;;)
    {
        auto const& entries = dir->getFieldV256 (sfIndexes);
        auto iter = entries.begin ();

        if (! found)
        {
            iter = std::find (iter, entries.end (), entryIndex);
            if (iter == entries.end ())
                return false;

            found = true;
        }

        for (; iter != entries.end (); ++iter)
        {
            auto const sleNode = ledger.read(keylet::child(*iter));
            if (type == ltINVALID || sleNode->getType () == type)
            {
                jvObjects.append (sleNode->getJson (0));

                if (++i == limit)
                {
                    if (++iter != entries.end ())
                    {
                        jvResult[jss::limit] = limit;
                        jvResult[jss::marker] = to_string (dirIndex) + ',' +
                            to_string (*iter);
                        return true;
                    }

                    break;
                }
            }
        }

        auto const nodeIndex = dir->getFieldU64 (sfIndexNext);
        if (nodeIndex == 0)
            return true;

        dirIndex = getDirNodeIndex (rootDirIndex, nodeIndex);
        dir = ledger.read({ltDIR_NODE, dirIndex});
        if (! dir)
            return true;

        if (i == limit)
        {
            auto const& e = dir->getFieldV256 (sfIndexes);
            if (! e.empty ())
            {
                jvResult[jss::limit] = limit;
                jvResult[jss::marker] = to_string (dirIndex) + ',' +
                    to_string (*e.begin ());
            }

            return true;
        }
    }
}

std::shared_ptr<const casinocoin::SLE>
getAccountSLE ( LedgerMaster& ledgerMaster, std::string const& strIdent, LedgerIndex ledgerSeq, bool bStrict)
{
    AccountID uAccount;
    if (accountFromString(uAccount, strIdent, bStrict))
        return std::shared_ptr<const casinocoin::SLE>();

    auto const accountKeylet = keylet::account (uAccount);

    std::shared_ptr<Ledger const> queriedLedger;
    if (ledgerSeq == 0)
        queriedLedger = ledgerMaster.getValidatedLedger();
    else
        queriedLedger = ledgerMaster.getLedgerBySeq(ledgerSeq);
    if (queriedLedger)
        return queriedLedger->read(accountKeylet);
    return std::shared_ptr<const casinocoin::SLE>();
}

namespace {

bool
isValidatedOld(LedgerMaster& ledgerMaster, bool standalone)
{
    if (standalone)
        return false;

    return ledgerMaster.getValidatedLedgerAge () >
        Tuning::maxValidatedLedgerAge;
}

template <class T>
Status
ledgerFromRequest(T& ledger, Context& context)
{
    static auto const minSequenceGap = 10;

    ledger.reset();

    auto& params = context.params;
    auto& ledgerMaster = context.ledgerMaster;

    auto indexValue = params[jss::ledger_index];
    auto hashValue = params[jss::ledger_hash];

    // We need to support the legacy "ledger" field.
    auto& legacyLedger = params[jss::ledger];
    if (legacyLedger)
    {
        if (legacyLedger.asString().size () > 12)
            hashValue = legacyLedger;
        else
            indexValue = legacyLedger;
    }

    if (hashValue)
    {
        if (! hashValue.isString ())
            return {rpcINVALID_PARAMS, "ledgerHashNotString"};

        uint256 ledgerHash;
        if (! ledgerHash.SetHex (hashValue.asString ()))
            return {rpcINVALID_PARAMS, "ledgerHashMalformed"};

        ledger = ledgerMaster.getLedgerByHash (ledgerHash);
        if (ledger == nullptr)
            return {rpcLGR_NOT_FOUND, "ledgerNotFound"};
    }
    else if (indexValue.isNumeric())
    {
        ledger = ledgerMaster.getLedgerBySeq (indexValue.asInt ());

        if (ledger == nullptr)
        {
            auto cur = ledgerMaster.getCurrentLedger();
            if (cur->info().seq == indexValue.asInt())
                ledger = cur;
        }

        if (ledger == nullptr)
            return {rpcLGR_NOT_FOUND, "ledgerNotFound"};

        if (ledger->info().seq > ledgerMaster.getValidLedgerIndex() &&
            isValidatedOld(ledgerMaster, context.app.config().standalone()))
        {
            ledger.reset();
            return {rpcNO_NETWORK, "InsufficientNetworkMode"};
        }
    }
    else
    {
        if (isValidatedOld (ledgerMaster, context.app.config().standalone()))
            return {rpcNO_NETWORK, ""};

        auto const index = indexValue.asString ();
        if (index == "validated")
        {
            ledger = ledgerMaster.getValidatedLedger ();
            if (ledger == nullptr)
            {
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};
            }

            assert (! ledger->open());
        }
        else
        {
            if (index.empty () || index == "current")
            {
                ledger = ledgerMaster.getCurrentLedger ();
                assert (ledger->open());
            }
            else if (index == "closed")
            {
                ledger = ledgerMaster.getClosedLedger ();
                assert (! ledger->open());
            }
            else
            {
                return {rpcINVALID_PARAMS, "ledgerIndexMalformed"};
            }

            if (ledger == nullptr)
            {
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};
            }

            if (ledger->info().seq + minSequenceGap <
                ledgerMaster.getValidLedgerIndex ())
            {
                ledger.reset ();
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};
            }
        }
    }

    return Status::OK;
}

bool
isValidated(LedgerMaster& ledgerMaster, ReadView const& ledger,
    Application& app)
{
    if (ledger.open())
        return false;

    if (ledger.info().validated)
        return true;

    auto seq = ledger.info().seq;
    try
    {
        // Use the skip list in the last validated ledger to see if ledger
        // comes before the last validated ledger (and thus has been
        // validated).
        auto hash = ledgerMaster.walkHashBySeq (seq);

        if (!hash || ledger.info().hash != *hash)
        {
            // This ledger's hash is not the hash of the validated ledger
            if (hash)
            {
                assert(hash->isNonZero());
                uint256 valHash = getHashByIndex (seq, app);
                if (valHash == ledger.info().hash)
                {
                    // SQL database doesn't match ledger chain
                    ledgerMaster.clearLedger (seq);
                }
            }
            return false;
        }
    }
    catch (SHAMapMissingNode const&)
    {
        auto stream = app.journal ("RPCHandler").warn();
        JLOG (stream)
            << "Missing SHANode " << std::to_string (seq);
        return false;
    }

    // Mark ledger as validated to save time if we see it again.
    ledger.info().validated = true;
    return true;
}

} // namespace

// The previous version of the lookupLedger command would accept the
// "ledger_index" argument as a string and silently treat it as a request to
// return the current ledger which, while not strictly wrong, could cause a lot
// of confusion.
//
// The code now robustly validates the input and ensures that the only possible
// values for the "ledger_index" parameter are the index of a ledger passed as
// an integer or one of the strings "current", "closed" or "validated".
// Additionally, the code ensures that the value passed in "ledger_hash" is a
// string and a valid hash. Invalid values will return an appropriate error
// code.
//
// In the absence of the "ledger_hash" or "ledger_index" parameters, the code
// assumes that "ledger_index" has the value "current".
//
// Returns a Json::objectValue.  If there was an error, it will be in that
// return value.  Otherwise, the object contains the field "validated" and
// optionally the fields "ledger_hash", "ledger_index" and
// "ledger_current_index", if they are defined.
Status
lookupLedger(std::shared_ptr<ReadView const>& ledger, Context& context,
    Json::Value& result)
{
    if (auto status = ledgerFromRequest (ledger, context))
        return status;

    auto& info = ledger->info();

    if (!ledger->open())
    {
        result[jss::ledger_hash] = to_string (info.hash);
        result[jss::ledger_index] = info.seq;
    }
    else
    {
        result[jss::ledger_current_index] = info.seq;
    }

    result[jss::validated] = isValidated (context.ledgerMaster, *ledger, context.app);
    return Status::OK;
}

Json::Value
lookupLedger(std::shared_ptr<ReadView const>& ledger, Context& context)
{
    Json::Value result;
    if (auto status = lookupLedger (ledger, context, result))
        status.inject (result);

    return result;
}

hash_set<AccountID>
parseAccountIds(Json::Value const& jvArray)
{
    hash_set<AccountID> result;
    for (auto const& jv: jvArray)
    {
        if (! jv.isString())
            return hash_set<AccountID>();
        auto const id =
            parseBase58<AccountID>(jv.asString());
        if (! id)
            return hash_set<AccountID>();
        result.insert(*id);
    }
    return result;
}

boost::optional<TokenDescriptor>
getWLT(Json::Value const& jvRequest,
       std::shared_ptr<ReadView const> const& ledger,
       beast::Journal const& j)
{
    boost::optional<TokenDescriptor> theToken;
    if (!ledger->rules().enabled(featureWLT))
        return theToken;

    LedgerConfig const& ledgerConfiguration = ledger->ledgerConfig();
    auto tokenConfigIter = std::find_if(ledgerConfiguration.entries.begin(),
                                        ledgerConfiguration.entries.end(),
                                        [](ConfigObjectEntry const& obj)
        {
            return obj.getType() == ConfigObjectEntry::Token;
        });
    if (tokenConfigIter == ledgerConfiguration.entries.end())
    {
        JLOG(j.info()) << "No WLT entries found. tx forbidden.";
        return theToken;
    }

    for (auto iter = jvRequest.begin(); iter != jvRequest.end(); ++iter)
    {
        STAmount amountCandidate;
        if (amountFromJsonNoThrow(amountCandidate, *iter))
        {
            if (isCSC(amountCandidate))
                continue;

            theToken = getWLT(amountCandidate, *tokenConfigIter, j);
            if (theToken)
                return theToken;
        }
    }
    return theToken;
}

bool
isWLT(Json::Value const& jvRequest,
      std::shared_ptr<ReadView const> const& ledger,
      beast::Journal const& j)
{
    if (getWLT(jvRequest, ledger, j))
        return true;
    return false;
}

void
addPaymentDeliveredAmount(Json::Value& meta, RPC::Context& context,
    std::shared_ptr<Transaction> transaction, TxMeta::pointer transactionMeta)
{
    // We only want to add a "delivered_amount" field if the transaction
    // succeeded - otherwise nothing could have been delivered.
    if (! transaction)
        return;

    auto serializedTx = transaction->getSTransaction ();
    if (! serializedTx || serializedTx->getTxnType () != ttPAYMENT)
        return;

    if (transactionMeta)
    {
        if (transactionMeta->getResultTER() != tesSUCCESS)
            return;

        // If the transaction explicitly specifies a DeliveredAmount in the
        // metadata then we use it.
        if (transactionMeta->hasDeliveredAmount ())
        {
            meta[jss::delivered_amount] =
                transactionMeta->getDeliveredAmount ().getJson (1);
            return;
        }
    }
    else if (transaction->getResult() != tesSUCCESS)
    {
        return;
    }

    meta[jss::delivered_amount] = serializedTx->getFieldAmount (sfAmount).getJson (1);
}

void
injectSLE(Json::Value& jv, SLE const& sle)
{
    jv = sle.getJson(0);
    if (sle.getType() == ltACCOUNT_ROOT)
    {
        if (sle.isFieldPresent(sfEmailHash))
        {
            auto const& hash =
                sle.getFieldH128(sfEmailHash);
            Blob const b (hash.begin(), hash.end());
            std::string md5 = strHex(makeSlice(b));
            boost::to_lower(md5);
            // VFALCO TODO Give a name and move this constant
            //             to a more visible location. Also
            //             shouldn't this be https?
            jv[jss::urlgravatar] = str(boost::format(
                "http://www.gravatar.com/avatar/%s") % md5);
        }
    }
    else
    {
        jv[jss::Invalid] = true;
    }
}

Blob injectClientIPHelper (Context& context, std::string const& srcAccountString)
{
    Blob encryptedIP;

    // only check it if KYC feature and KYCIPTracking feature are enabled
    LedgerMaster& ledgerMaster = context.app.getLedgerMaster();
    if (ledgerMaster.getValidatedRules().enabled (featureKYC)
            && ledgerMaster.getValidatedRules().enabled (featureKYCIPTracking)
            && ledgerMaster.getValidatedRules().enabled(featureConfigObject))
    {
        // only add if account is KYC-validated and config object has some pubkeys stored
        std::shared_ptr<Ledger const> validatedLedger = ledgerMaster.getValidatedLedger();
        if (validatedLedger)
        {
            LedgerConfig const& ledgerConfiguration = ledgerMaster.getValidatedLedger()->ledgerConfig();
            auto pubKeyConfigIter = std::find_if(ledgerConfiguration.entries.begin(),
                                                 ledgerConfiguration.entries.end(),
                                                 [](ConfigObjectEntry const& obj)
            {
                    return obj.getType() == ConfigObjectEntry::Message_PubKey;
        });
            if (pubKeyConfigIter != ledgerConfiguration.entries.end())
            {
                std::shared_ptr<const casinocoin::SLE> sleSender =
                        getAccountSLE(ledgerMaster, srcAccountString);
                if (sleSender && (sleSender->isFlag(lsfKYCValidated)))
                {
                    auto const& definedMsgPubKeys = (*pubKeyConfigIter).getData();
                    if (definedMsgPubKeys.size() > 0)
                    {
                        const Message_PubKeyDescriptor* pubKeyEntry = static_cast<const Message_PubKeyDescriptor*>(definedMsgPubKeys[0]);
                        JLOG(context.j.debug()) << "pubkey used for ip encryption: " << pubKeyEntry->pubKey.data() << " read from config ledger object";

                        std::string clientIP = context.clientAddress.address().to_string();
                        Serializer s(clientIP.data(), clientIP.size());
                        try
                        {
                            encryptedIP = encryptECIES(pubKeyEntry->pubKey, s.peekData());
                        }
                        catch (std::runtime_error const& error)
                        {
                            JLOG(context.j.info()) << "ip encryption thrown " << error.what();
                            encryptedIP.clear();
                        }
                        return encryptedIP;
                    }
                    else
                    {
                        JLOG(context.j.info()) << "No PubKeys in entries found. Fall through without IP enrich";
                    }
                }
            }
            else
            {
                JLOG(context.j.info()) << "No PubKey entries found. Fall through without IP enrich";
            }
        }
    }
    return encryptedIP;
}

bool
injectClientIP (Context& context)
{
    // jrojek TODO: temporary don't add client ip and remove it if it existed!!
    if (context.params[jss::tx_json].isMember(jss::ClientIP))
    {
        context.params[jss::tx_json].removeMember(jss::ClientIP);
        {
            JLOG(context.j.debug()) << "removed pre-added ip";
        }
    }
    return true;
    // non executing code for now
    if (context.params[jss::tx_json].isMember(jss::ClientIP))
    {
        JLOG(context.j.debug()) << "ip already there" << context.params[jss::tx_json][jss::ClientIP].asString();
        return true;
    }
    Blob encryptedIP = injectClientIPHelper(context, context.params[jss::tx_json][jss::Account].asString());
    if (encryptedIP.size() == 0)
        return false;

    context.params[jss::tx_json][jss::ClientIP] = strHex(encryptedIP.data(), encryptedIP.size());
    return true;
}

bool injectClientIP (Context& context, STTx* stpTrans)
{
    // jrojek TODO: temporary don't add client ip and remove it if it existed!!
    if (stpTrans->isFieldPresent(sfClientIP))
    {
        stpTrans->makeFieldAbsent(sfClientIP);
        {
            JLOG(context.j.debug()) << "removed pre-added ip";
        }
    }
    return true;
    // non executing code for now
    if (stpTrans->isFieldPresent(sfClientIP))
    {
        JLOG(context.j.debug()) << "ip already there" << strHex(makeSlice(stpTrans->getFieldVL(sfClientIP)));
        return true;
    }
    Blob encryptedIP = injectClientIPHelper(context, toBase58(stpTrans->getAccountID(sfAccount)));
    if (encryptedIP.size() == 0)
        return false;

    stpTrans->setFieldVL(sfClientIP, encryptedIP);
    return true;
}

boost::optional<Json::Value>
readLimitField(unsigned int& limit, Tuning::LimitRange const& range,
    Context const& context)
{
    limit = range.rdefault;
    if (auto const& jvLimit = context.params[jss::limit])
    {
        if (! (jvLimit.isUInt() || (jvLimit.isInt() && jvLimit.asInt() >= 0)))
            return RPC::expected_field_error (jss::limit, "unsigned integer");

        limit = jvLimit.asUInt();
        if (! isUnlimited (context.role))
            limit = std::max(range.rmin, std::min(range.rmax, limit));
    }
    return boost::none;
}

boost::optional<Seed>
getSeedFromRPC(Json::Value const& params, Json::Value& error)
{
    // The array should be constexpr, but that makes Visual Studio unhappy.
    static char const* const seedTypes[]
    {
        jss::passphrase.c_str(),
        jss::seed.c_str(),
        jss::seed_hex.c_str()
    };

    // Identify which seed type is in use.
    char const* seedType = nullptr;
    int count = 0;
    for (auto t : seedTypes)
    {
        if (params.isMember (t))
        {
            ++count;
            seedType = t;
        }
    }

    if (count != 1)
    {
        error = RPC::make_param_error (
            "Exactly one of the following must be specified: " +
            std::string(jss::passphrase) + ", " +
            std::string(jss::seed) + " or " +
            std::string(jss::seed_hex));
        return boost::none;
    }

    // Make sure a string is present
    if (! params[seedType].isString())
    {
        error = RPC::expected_field_error (seedType, "string");
        return boost::none;
    }

    auto const fieldContents = params[seedType].asString();

    // Convert string to seed.
    boost::optional<Seed> seed;

    if (seedType == jss::seed.c_str())
        seed = parseBase58<Seed> (fieldContents);
    else if (seedType == jss::passphrase.c_str())
        seed = parseGenericSeed (fieldContents);
    else if (seedType == jss::seed_hex.c_str())
    {
        uint128 s;

        if (s.SetHexExact (fieldContents))
            seed.emplace (Slice(s.data(), s.size()));
    }

    if (!seed)
        error = rpcError (rpcBAD_SEED);

    return seed;
}

std::pair<PublicKey, SecretKey>
keypairForSignature(Json::Value const& params, Json::Value& error)
{
    bool const has_key_type  = params.isMember (jss::key_type);

    // All of the secret types we allow, but only one at a time.
    // The array should be constexpr, but that makes Visual Studio unhappy.
    static char const* const secretTypes[]
    {
        jss::passphrase.c_str(),
        jss::secret.c_str(),
        jss::seed.c_str(),
        jss::seed_hex.c_str()
    };

    // Identify which secret type is in use.
    char const* secretType = nullptr;
    int count = 0;
    for (auto t : secretTypes)
    {
        if (params.isMember (t))
        {
            ++count;
            secretType = t;
        }
    }

    if (count == 0 || secretType == nullptr)
    {
        error = RPC::missing_field_error (jss::secret);
        return { };
    }

    if (count > 1)
    {
        error = RPC::make_param_error (
            "Exactly one of the following must be specified: " +
            std::string(jss::passphrase) + ", " +
            std::string(jss::secret) + ", " +
            std::string(jss::seed) + " or " +
            std::string(jss::seed_hex));
        return { };
    }

    KeyType keyType = KeyType::secp256k1;
    boost::optional<Seed> seed;

    if (has_key_type)
    {
        if (! params[jss::key_type].isString())
        {
            error = RPC::expected_field_error (
                jss::key_type, "string");
            return { };
        }

        keyType = keyTypeFromString (
            params[jss::key_type].asString());

        if (keyType == KeyType::invalid)
        {
            error = RPC::invalid_field_error(jss::key_type);
            return { };
        }

        if (secretType == jss::secret.c_str())
        {
            error = RPC::make_param_error (
                "The secret field is not allowed if " +
                std::string(jss::key_type) + " is used.");
            return { };
        }

        seed = getSeedFromRPC (params, error);
    }
    else
    {
        if (! params[jss::secret].isString())
        {
            error = RPC::expected_field_error (
                jss::secret, "string");
            return { };
        }

        seed = parseGenericSeed (
            params[jss::secret].asString ());
    }

    if (!seed)
    {
        if (!contains_error (error))
        {
            error = RPC::make_error (rpcBAD_SEED,
            RPC::invalid_field_message (secretType));
        }

        return { };
    }

    if (keyType != KeyType::secp256k1 && keyType != KeyType::ed25519)
        LogicError ("keypairForSignature: invalid key type");

    return generateKeyPair (keyType, *seed);
}

std::pair<RPC::Status, LedgerEntryType>
chooseLedgerEntryType(Json::Value const& params)
{
    std::pair<RPC::Status, LedgerEntryType> result{ RPC::Status::OK, ltINVALID };
    if (params.isMember(jss::type))
    {
        static
            std::array<std::pair<char const *, LedgerEntryType>, 11> const types
        { {
            { jss::account,         ltACCOUNT_ROOT },
            { jss::amendments,      ltAMENDMENTS },
            { jss::directory,       ltDIR_NODE },
            { jss::fee,             ltFEE_SETTINGS },
            { jss::hashes,          ltLEDGER_HASHES },
            { jss::offer,           ltOFFER },
            { jss::signer_list,     ltSIGNER_LIST },
            { jss::state,           ltCASINOCOIN_STATE },
            { jss::ticket,          ltTICKET },
            { jss::escrow,          ltESCROW },
            { jss::payment_channel, ltPAYCHAN }
            } };

        auto const& p = params[jss::type];
        if (!p.isString())
        {
            result.first = RPC::Status{ rpcINVALID_PARAMS,
                "Invalid field 'type', not string." };
            assert(result.first.type() == RPC::Status::Type::error_code_i);
            return result;
        }

        auto const filter = p.asString();
        auto iter = std::find_if(types.begin(), types.end(),
            [&filter](decltype (types.front())& t)
        {
            return t.first == filter;
        });
        if (iter == types.end())
        {
            result.first = RPC::Status{ rpcINVALID_PARAMS,
                "Invalid field 'type'." };
            assert(result.first.type() == RPC::Status::Type::error_code_i);
            return result;
        }
        result.second = iter->second;
    }
    return result;
}

beast::SemanticVersion const firstVersion("1.0.0");
beast::SemanticVersion const goodVersion("1.0.0");
beast::SemanticVersion const lastVersion("1.0.0");

} // RPC
} // casinocoin
