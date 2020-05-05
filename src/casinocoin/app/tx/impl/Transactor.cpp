//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
    2017-06-29  ajochems        Refactored for casinocoin
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/app/main/Application.h>
#include <casinocoin/app/misc/LoadFeeTrack.h>
#include <casinocoin/app/tx/apply.h>
#include <casinocoin/app/tx/impl/Transactor.h>
#include <casinocoin/app/tx/impl/SignerEntries.h>
#include <casinocoin/basics/contract.h>
#include <casinocoin/basics/Log.h>
#include <casinocoin/basics/mulDiv.h>
#include <casinocoin/core/Config.h>
#include <casinocoin/json/to_string.h>
#include <casinocoin/ledger/View.h>
#include <casinocoin/protocol/Feature.h>
#include <casinocoin/protocol/Indexes.h>
#include <casinocoin/protocol/types.h>
#include <casinocoin/app/misc/Blacklist.h>

namespace casinocoin {

/** Performs early sanity checks on the txid */
TER
preflight0(PreflightContext const& ctx)
{
    auto const txID = ctx.tx.getTransactionID();

    if (txID == beast::zero)
    {
        JLOG(ctx.j.warn()) <<
            "applyTransaction: transaction id may not be zero";
        return temINVALID;
    }

    return tesSUCCESS;
}

/** Performs early sanity checks on the account and fee fields */
TER
preflight1 (PreflightContext const& ctx)
{
    auto const ret = preflight0(ctx);
    if (!isTesSuccess(ret))
        return ret;

    auto const id = ctx.tx.getAccountID(sfAccount);
    if (id == zero)
    {
        JLOG(ctx.j.warn()) << "preflight1: bad account id";
        return temBAD_SRC_ACCOUNT;
    }

    // No point in going any further if the transaction fee is malformed.
    auto const fee = ctx.tx.getFieldAmount (sfFee);
    if (!fee.native () || fee.negative () || !isLegalAmount (fee.csc ()))
    {
        JLOG(ctx.j.debug()) << "preflight1: invalid fee";
        return temBAD_FEE;
    }

    auto const spk = ctx.tx.getSigningPubKey();

    if (!spk.empty () && !publicKeyType (makeSlice (spk)))
    {
        JLOG(ctx.j.debug()) << "preflight1: invalid signing key";
        return temBAD_SIGNATURE;
    }

    return tesSUCCESS;
}

/** Checks whether the signature appears valid */
TER
preflight2 (PreflightContext const& ctx)
{
    if(!( ctx.flags & tapNO_CHECK_SIGN))
    {
        auto const sigValid = checkValidity(ctx.app.getHashRouter(),
            ctx.tx, ctx.rules, ctx.app.config(), ctx.j);
        if (sigValid.first == Validity::SigBad)
        {
            JLOG(ctx.j.debug()) <<
                "preflight2: bad signature. " << sigValid.second;
            return temINVALID;
        }
    }
    return tesSUCCESS;
}

static
CSCAmount
calculateFee(Application& app, std::uint64_t const baseFee,
    Fees const& fees, ApplyFlags flags)
{
    return scaleFeeLoad(baseFee, app.getFeeTrack(),
        fees, flags & tapUNLIMITED);
}

//------------------------------------------------------------------------------

PreflightContext::PreflightContext(Application& app_, STTx const& tx_,
    Rules const& rules_, ApplyFlags flags_,
        beast::Journal j_)
    : app(app_)
    , tx(tx_)
    , rules(rules_)
    , flags(flags_)
    , j(j_)
{
}

//------------------------------------------------------------------------------

Transactor::Transactor(
    ApplyContext& ctx)
    : ctx_ (ctx)
    , j_ (ctx.journal)
{
}

std::uint64_t Transactor::calculateBaseFee (
    PreclaimContext const& ctx)
{
    // Returns the fee in fee units.

    // The computation has two parts:
    //  * The base fee, which is the same for most transactions.
    //  * The additional cost of each multisignature on the transaction.
    std::uint64_t baseFee = ctx.view.fees().units;

    // Each signer adds one more baseFee to the minimum required fee
    // for the transaction.
    std::uint32_t signerCount = 0;
    if (ctx.tx.isFieldPresent (sfSigners))
        signerCount = ctx.tx.getFieldArray (sfSigners).size();

    return baseFee + (signerCount * baseFee);
}

CSCAmount
Transactor::calculateFeePaid(STTx const& tx)
{
    return tx[sfFee].csc();
}

CSCAmount
Transactor::calculateMaxSpend(STTx const& tx)
{
    return beast::zero;
}

TER
Transactor::checkFee (PreclaimContext const& ctx, std::uint64_t baseFee)
{
    auto const feePaid = calculateFeePaid(ctx.tx);
    if (!isLegalAmount (feePaid) || feePaid < beast::zero)
        return temBAD_FEE;

    auto const feeDue = casinocoin::calculateFee(ctx.app,
        baseFee, ctx.view.fees(), ctx.flags);

    // Only check fee is sufficient when the ledger is open.
    if (ctx.view.open() && feePaid < feeDue)
    {
        JLOG(ctx.j.trace()) << "Insufficient fee paid: " <<
            to_string (feePaid) << "/" << to_string (feeDue);
        return telINSUF_FEE_P;
    }

    if (feePaid == zero)
        return tesSUCCESS;

    auto const id = ctx.tx.getAccountID(sfAccount);
    auto const sle = ctx.view.read(
        keylet::account(id));
    auto const balance = (*sle)[sfBalance].csc();

    if (balance < feePaid)
    {
        JLOG(ctx.j.trace()) << "Insufficient balance:" <<
            " balance=" << to_string(balance) <<
            " paid=" << to_string(feePaid);

        if ((balance > zero) && !ctx.view.open())
        {
            // Closed ledger, non-zero balance, less than fee
            return tecINSUFF_FEE;
        }

        return terINSUF_FEE_B;
    }

    return tesSUCCESS;
}

TER Transactor::checkFeeToken(PreclaimContext const& ctx,
                              TokenDescriptor const& theToken)
{
    if (!ctx.view.rules().enabled(featureWLT))
    {
        return tesSUCCESS;
    }

    auto const feePaid = calculateFeePaid(ctx.tx);
    if (!isLegalAmount (feePaid) || feePaid < beast::zero)
        return temBAD_FEE;

    auto const feeRequired = mulDiv(ctx.app.config().TRANSACTION_FEE_BASE,
                               ctx.view.fees().base, ctx.view.fees().units);
    if (!feeRequired.first)
    {
        JLOG(ctx.j.debug()) << "Overflow in default fee calculation";
        return temBAD_FEE;
    }

    auto const feeToken = mulDiv (feeRequired.second, theToken.extraFeeFactor, 100/*percent*/);
    if (!feeToken.first)
        Throw<std::overflow_error>("Overflow in token fee calculation");
    CSCAmount requiredTokenFee(feeRequired.second + feeToken.second);


    // Only check fee is sufficient when the ledger is open.
    if (ctx.view.open() && feePaid < requiredTokenFee)
    {
        JLOG(ctx.j.debug()) << "Insufficient fee paid: " <<
            to_string(feePaid) << "/" << to_string(requiredTokenFee);
        return telINSUF_FEE_P;
    }

    if (feePaid == zero)
        return tesSUCCESS;

    auto const id = ctx.tx.getAccountID(sfAccount);
    auto const sle = ctx.view.read(
        keylet::account(id));
    auto const balance = (*sle)[sfBalance].csc();

    if (balance < feePaid)
    {
        JLOG(ctx.j.trace()) << "Insufficient balance:" <<
            " balance=" << to_string(balance) <<
            " paid=" << to_string(feePaid);

        if ((balance > zero) && !ctx.view.open())
        {
            // Closed ledger, non-zero balance, less than fee
            return tecINSUFF_FEE;
        }

        return terINSUF_FEE_B;
    }

    return tesSUCCESS;
}

TER Transactor::payFee ()
{
    auto const feePaid = calculateFeePaid(ctx_.tx);

    auto const sle = view().peek(
        keylet::account(account_));

    // Deduct the fee, so it's not available during the transaction.
    // Will only write the account back if the transaction succeeds.

    mSourceBalance -= feePaid;
    sle->setFieldAmount (sfBalance, mSourceBalance);

    // VFALCO Should we call view().rawDestroyCSC() here as well?

    return tesSUCCESS;
}

TER
Transactor::checkSeq (PreclaimContext const& ctx)
{
    auto const id = ctx.tx.getAccountID(sfAccount);

    auto const sle = ctx.view.read(
        keylet::account(id));

    if (!sle)
    {
        JLOG(ctx.j.trace()) <<
            "applyTransaction: delay: source account does not exist " <<
            toBase58(ctx.tx.getAccountID(sfAccount));
        return terNO_ACCOUNT;
    }

    std::uint32_t const t_seq = ctx.tx.getSequence ();
    std::uint32_t const a_seq = sle->getFieldU32 (sfSequence);

    if (t_seq != a_seq)
    {
        if (a_seq < t_seq)
        {
            JLOG(ctx.j.trace()) <<
                "applyTransaction: has future sequence number " <<
                "a_seq=" << a_seq << " t_seq=" << t_seq;
            return terPRE_SEQ;
        }

        if (ctx.view.txExists(ctx.tx.getTransactionID ()))
            return tefALREADY;

        JLOG(ctx.j.trace()) << "applyTransaction: has past sequence number " <<
            "a_seq=" << a_seq << " t_seq=" << t_seq;
        return tefPAST_SEQ;
    }

    if (ctx.tx.isFieldPresent (sfAccountTxnID) &&
            (sle->getFieldH256 (sfAccountTxnID) != ctx.tx.getFieldH256 (sfAccountTxnID)))
        return tefWRONG_PRIOR;

    if (ctx.tx.isFieldPresent (sfLastLedgerSequence) &&
            (ctx.view.seq() > ctx.tx.getFieldU32 (sfLastLedgerSequence)))
        return tefMAX_LEDGER;

    return tesSUCCESS;
}

void
Transactor::setSeq ()
{
    auto const sle = view().peek(
        keylet::account(account_));

    std::uint32_t const t_seq = ctx_.tx.getSequence ();

    sle->setFieldU32 (sfSequence, t_seq + 1);

    if (sle->isFieldPresent (sfAccountTxnID))
        sle->setFieldH256 (sfAccountTxnID, ctx_.tx.getTransactionID ());
}

// check stuff before you bother to lock the ledger
void Transactor::preCompute ()
{
    account_ = ctx_.tx.getAccountID(sfAccount);
    assert(account_ != zero);
}

TER Transactor::apply ()
{
    preCompute();

    // If the transactor requires a valid account and the transaction doesn't
    // list one, preflight will have already a flagged a failure.
    auto const sle = view().peek (keylet::account(account_));

    // sle must exist except for transactions
    // that allow zero account.
    assert(sle != nullptr || account_ == zero);

    mFeeDue = calculateFee(ctx_.app, ctx_.baseFee,
        view().fees(), view().flags());

    if (sle)
    {
        mPriorBalance   = STAmount ((*sle)[sfBalance]).csc ();
        mSourceBalance  = mPriorBalance;

        setSeq();

        auto terResult = payFee ();

        if (terResult != tesSUCCESS) return terResult;

        view().update (sle);
    }

    return doApply ();
}

TER
Transactor::checkSign (PreclaimContext const& ctx)
{
    // Make sure multisigning is enabled before we check for multisignatures.
    if (ctx.view.rules().enabled(featureMultiSign))
    {
        // If the pk is empty, then we must be multi-signing.
        if (ctx.tx.getSigningPubKey().empty ())
            return checkMultiSign (ctx);
    }

    return checkSingleSign (ctx);
}

TER
Transactor::checkWLT (PreclaimContext const& ctx, boost::optional<TokenDescriptor>& theToken)
{
    // Narrow allowed tokens only if WLT amendment is enabled
    if (!ctx.view.rules().enabled(featureWLT))
    {
        return tesSUCCESS;
    }

    if (ctx.tx.isNative())
    {
        return tesSUCCESS;
    }

    if (ctx.tx.getTxnType() == ttCONFIG)
    {
        JLOG(ctx.j.info()) << "checkWLT() SetConfiguration tx can operate on non-WLT for obvious reason.";
        return tesSUCCESS;
    }

    LedgerConfig const& ledgerConfiguration = ctx.view.ledgerConfig();
    auto tokenConfigIter = std::find_if(ledgerConfiguration.entries.begin(),
                                        ledgerConfiguration.entries.end(),
                                        [](ConfigObjectEntry const& obj)
    {
            return obj.getType() == ConfigObjectEntry::Token;
    });

    if (tokenConfigIter == ledgerConfiguration.entries.end())
    {
        JLOG(ctx.j.info()) << "No WLT entries found. tx forbidden.";
        return tefNOT_WLT;
    }

    auto result = ctx.tx.isAllowedWLT(*tokenConfigIter);
    theToken = result.second;
    return result.first;
}

TER
Transactor::checkSingleSign (PreclaimContext const& ctx)
{
    auto const id = ctx.tx.getAccountID(sfAccount);

    auto const sle = ctx.view.read(
        keylet::account(id));
    auto const hasAuthKey     = sle->isFieldPresent (sfRegularKey);

    // Consistency: Check signature
    // Verify the transaction's signing public key is authorized for signing.
    auto const spk = ctx.tx.getSigningPubKey();
    if (!publicKeyType (makeSlice (spk)))
    {
        JLOG(ctx.j.trace()) <<
            "checkSingleSign: signing public key type is unknown";
        return tefBAD_AUTH; // FIXME: should be better error!
    }

    auto const pkAccount = calcAccountID (
        PublicKey (makeSlice (spk)));

    if (pkAccount == id)
    {
        // Authorized to continue.
        if (sle->isFlag(lsfDisableMaster))
            return tefMASTER_DISABLED;
    }
    else if (hasAuthKey &&
        (pkAccount == sle->getAccountID (sfRegularKey)))
    {
        // Authorized to continue.
    }
    else if (hasAuthKey)
    {
        JLOG(ctx.j.trace()) <<
            "checkSingleSign: Not authorized to use account.";
        return tefBAD_AUTH;
    }
    else
    {
        JLOG(ctx.j.trace()) << "checkSingleSign: Check if account is blacklisted";
        // check if source accountid is blacklisted and signing accountid is whitelisted
        if (ctx.app.blacklistedAccounts().listed(toBase58(id)))
        {
            JLOG(ctx.j.trace()) << "checkSingleSign: Check if signer account is whitelisted";
            bool bIsTrusted = false;
            for ( const std::string& entry : ctx.app.config().WhitelistAccounts )
            {
                JLOG(ctx.j.trace()) << "checkSingleSign: Whitelist account: " << entry << " read from config";
                auto trustedAccountID = parseBase58<AccountID>(entry);
                if (!trustedAccountID)
                {
                    JLOG(ctx.j.trace()) << "Whitelist account: " << entry << " seems to be invalid";
                    continue;
                }
                if (pkAccount == trustedAccountID)
                {
                    bIsTrusted = true;
                    break;
                }
            }
            if(bIsTrusted)
            {
                JLOG(ctx.j.info()) << "!!! temporarily allowed to use blacklisted account due to whitelisted signing account ";
                return tesSUCCESS;
            }
        }
        JLOG(ctx.j.trace()) << "checkSingleSign: Not authorized to use account.";
        return tefBAD_AUTH_MASTER;
    }

    return tesSUCCESS;
}

TER Transactor::checkMultiSign (PreclaimContext const& ctx)
{
    auto const id = ctx.tx.getAccountID(sfAccount);
    // Get mTxnAccountID's SignerList and Quorum.
    std::shared_ptr<STLedgerEntry const> sleAccountSigners =
        ctx.view.read (keylet::signers(id));
    // If the signer list doesn't exist the account is not multi-signing.
    if (!sleAccountSigners)
    {
        JLOG(ctx.j.trace()) <<
            "applyTransaction: Invalid: Not a multi-signing account.";
        return tefNOT_MULTI_SIGNING;
    }

    // We have plans to support multiple SignerLists in the future.  The
    // presence and defaulted value of the SignerListID field will enable that.
    assert (sleAccountSigners->isFieldPresent (sfSignerListID));
    assert (sleAccountSigners->getFieldU32 (sfSignerListID) == 0);

    auto accountSigners =
        SignerEntries::deserialize (*sleAccountSigners, ctx.j, "ledger");
    if (accountSigners.second != tesSUCCESS)
        return accountSigners.second;

    // Get the array of transaction signers.
    STArray const& txSigners (ctx.tx.getFieldArray (sfSigners));

    // Walk the accountSigners performing a variety of checks and see if
    // the quorum is met.

    // Both the multiSigners and accountSigners are sorted by account.  So
    // matching multi-signers to account signers should be a simple
    // linear walk.  *All* signers must be valid or the transaction fails.
    std::uint32_t weightSum = 0;
    auto iter = accountSigners.first.begin ();
    for (auto const& txSigner : txSigners)
    {
        AccountID const txSignerAcctID = txSigner.getAccountID (sfAccount);

        // Attempt to match the SignerEntry with a Signer;
        while (iter->account < txSignerAcctID)
        {
            if (++iter == accountSigners.first.end ())
            {
                JLOG(ctx.j.trace()) <<
                    "applyTransaction: Invalid SigningAccount.Account.";
                return tefBAD_SIGNATURE;
            }
        }
        if (iter->account != txSignerAcctID)
        {
            // The SigningAccount is not in the SignerEntries.
            JLOG(ctx.j.trace()) <<
                "applyTransaction: Invalid SigningAccount.Account.";
            return tefBAD_SIGNATURE;
        }

        // We found the SigningAccount in the list of valid signers.  Now we
        // need to compute the accountID that is associated with the signer's
        // public key.
        auto const spk = txSigner.getFieldVL (sfSigningPubKey);

        if (!publicKeyType (makeSlice(spk)))
        {
            JLOG(ctx.j.trace()) <<
                "checkMultiSign: signing public key type is unknown";
            return tefBAD_SIGNATURE;
        }

        AccountID const signingAcctIDFromPubKey =
            calcAccountID(PublicKey (makeSlice(spk)));

        // Verify that the signingAcctID and the signingAcctIDFromPubKey
        // belong together.  Here is are the rules:
        //
        //   1. "Phantom account": an account that is not in the ledger
        //      A. If signingAcctID == signingAcctIDFromPubKey and the
        //         signingAcctID is not in the ledger then we have a phantom
        //         account.
        //      B. Phantom accounts are always allowed as multi-signers.
        //
        //   2. "Master Key"
        //      A. signingAcctID == signingAcctIDFromPubKey, and signingAcctID
        //         is in the ledger.
        //      B. If the signingAcctID in the ledger does not have the
        //         asfDisableMaster flag set, then the signature is allowed.
        //
        //   3. "Regular Key"
        //      A. signingAcctID != signingAcctIDFromPubKey, and signingAcctID
        //         is in the ledger.
        //      B. If signingAcctIDFromPubKey == signingAcctID.RegularKey (from
        //         ledger) then the signature is allowed.
        //
        // No other signatures are allowed.  (January 2015)

        // In any of these cases we need to know whether the account is in
        // the ledger.  Determine that now.
        auto sleTxSignerRoot =
            ctx.view.read (keylet::account(txSignerAcctID));

        if (signingAcctIDFromPubKey == txSignerAcctID)
        {
            // Either Phantom or Master.  Phantoms automatically pass.
            if (sleTxSignerRoot)
            {
                // Master Key.  Account may not have asfDisableMaster set.
                std::uint32_t const signerAccountFlags =
                    sleTxSignerRoot->getFieldU32 (sfFlags);

                if (signerAccountFlags & lsfDisableMaster)
                {
                    JLOG(ctx.j.trace()) <<
                        "applyTransaction: Signer:Account lsfDisableMaster.";
                    return tefMASTER_DISABLED;
                }
            }
        }
        else
        {
            // May be a Regular Key.  Let's find out.
            // Public key must hash to the account's regular key.
            if (!sleTxSignerRoot)
            {
                JLOG(ctx.j.trace()) <<
                    "applyTransaction: Non-phantom signer lacks account root.";
                return tefBAD_SIGNATURE;
            }

            if (!sleTxSignerRoot->isFieldPresent (sfRegularKey))
            {
                JLOG(ctx.j.trace()) <<
                    "applyTransaction: Account lacks RegularKey.";
                return tefBAD_SIGNATURE;
            }
            if (signingAcctIDFromPubKey !=
                sleTxSignerRoot->getAccountID (sfRegularKey))
            {
                JLOG(ctx.j.trace()) <<
                    "applyTransaction: Account doesn't match RegularKey.";
                return tefBAD_SIGNATURE;
            }
        }
        // The signer is legitimate.  Add their weight toward the quorum.
        weightSum += iter->weight;
    }

    // Cannot perform transaction if quorum is not met.
    if (weightSum < sleAccountSigners->getFieldU32 (sfSignerQuorum))
    {
        JLOG(ctx.j.trace()) <<
            "applyTransaction: Signers failed to meet quorum.";
        return tefBAD_QUORUM;
    }

    // Met the quorum.  Continue.
    return tesSUCCESS;
}

TER Transactor::checkBlacklist (PreclaimContext const& ctx)
{
    auto const accountid = ctx.tx.getAccountID(sfAccount);
    if (accountid == zero)
        return temBAD_SRC_ACCOUNT;

    if (false == ctx.app.blacklistedAccounts().listed(toBase58(accountid)))
    {
        // account is not blacklisted
        return tesSUCCESS;
    }

    // account is blacklisted, get full blacklisted account info
    JLOG(ctx.j.debug()) <<  "Account " << toBase58(accountid) << " is blacklisted!";
    BlacklistItem listedAccount = ctx.app.blacklistedAccounts().getAccount(toBase58(accountid));
    auto unHexedPubKey = strUnHex(listedAccount.publicKeySigner);
    if (!unHexedPubKey.second)
        return temMALFORMED;
    PublicKey signerPublicKey = PublicKey(Slice(unHexedPubKey.first.data(), unHexedPubKey.first.size()));

    std::string accountIDSigner = toBase58(calcAccountID(signerPublicKey));
    JLOG(ctx.j.debug()) <<  "Account Blacklist Signer: " << accountIDSigner;

    // get allowed signers from Config
    LedgerConfig const& ledgerConfiguration = ctx.view.ledgerConfig();
    auto blacklistConfigIter = std::find_if(ledgerConfiguration.entries.begin(),
                                            ledgerConfiguration.entries.end(),
                                            [](ConfigObjectEntry const& obj)
    {
        return obj.getType() == ConfigObjectEntry::Blacklist_Signer;
    });
    if (blacklistConfigIter == ledgerConfiguration.entries.end())
    {
        JLOG(ctx.j.info()) << "Account " << toBase58(accountid) << " is blacklisted but no autorised blacklist signer entries found in ConfigObject." <<
                              "TX is forbidden to prevent miss configuration.";
        return tefBLACKLISTED;
    }

    // loop over defined allowed signers and check if blacklisted account is signed by an allowed signer
    auto const& definedBlacklistSigners = (*blacklistConfigIter).getData();
    bool signerValid = false;
    for (auto signer : definedBlacklistSigners)
    {
        const Blacklist_SignerDescriptor* blacklistEntry = static_cast<const Blacklist_SignerDescriptor*>(signer);
        JLOG(ctx.j.debug()) <<  "Defined Blacklist Signer: " << toBase58(blacklistEntry->blacklistSigner);
        // check if allowed signer is the same as blacklisted signer account
        if(toBase58(blacklistEntry->blacklistSigner).compare(accountIDSigner) == 0)
        {
            signerValid = true;
            break;
        }

    }

    if(signerValid)
    {
        JLOG(ctx.j.info()) <<  "!!! Transaction not allowed !!! Account " << toBase58(accountid) << " is blacklisted!";
        return tefBLACKLISTED;
    }
    else
    {
        JLOG(ctx.j.info()) <<  "Account " << toBase58(accountid) << " is blacklisted but Signer is not on the defined signers list!";
        return tesSUCCESS;
    }
}


TER Transactor::checkWhitelist (PreclaimContext const& ctx)
{
    // check for specific account id
    auto const spk = ctx.tx.getSigningPubKey();
    auto const pkAccount = calcAccountID (PublicKey (makeSlice (spk)));
    bool bIsTrusted = false;
    for ( const std::string& entry : ctx.app.config().WhitelistAccounts )
    {
        JLOG(ctx.j.debug()) << "Whitelist account: " << entry << " read from config";
        auto trustedAccountID = parseBase58<AccountID>(entry);
        if (!trustedAccountID)
        {
            JLOG(ctx.j.info()) << "Whitelist account: " << entry << " seems to be invalid";
            continue;
        }
        if (pkAccount == trustedAccountID)
        {
            bIsTrusted = true;
            break;
        }
    }
    if(bIsTrusted)
    {
        JLOG(ctx.j.info()) << "!!! temporarily allowed to use blacklisted account with whitlisted account";
        // signer account is whitelisted
        return tesSUCCESS;
    }
    else
    {
        JLOG(ctx.j.info()) <<  "!!! Transactions for blacklisted accounts are not allowed !!!";
        return tefEXCEPTION;
    }
}

TER Transactor::checkMemoSize (PreclaimContext const& ctx)
{
    // get the tx memos
    auto const& memos = ctx.tx.getFieldArray (sfMemos);
    // The number 2048 is a preallocation hint, not a hard limit
    // to avoid allocate/copy/free's
    Serializer s (2048);
    memos.add (s);

    if (s.getDataLength () > ctx.app.config().MAX_MEMO_SIZE)
    {
        JLOG(ctx.j.info()) <<  "Memo size " + std::to_string(s.getDataLength()) + " exceded, max memo size "+std::to_string(ctx.app.config().MAX_MEMO_SIZE);
        return tefMAX_MEMO_SIZE;
    } else {
        return tesSUCCESS;
    }
}

TER Transactor::checkBurning(const PreclaimContext &ctx)
{
    if (!ctx.tx.isFieldPresent(sfDestination))
        return tesSUCCESS;
    AccountID const uSrcAccountID (ctx.tx.getAccountID (sfAccount));
    AccountID const uDstAccountID (ctx.tx.getAccountID (sfDestination));
    if (uDstAccountID == burnThreeAccount()
            || uDstAccountID == burnTwoAccount()) /* for now also disable burnTwo account as it is incomplete!!*/
    {
        if (ctx.app.blacklistedAccounts().listed(toBase58(uSrcAccountID)))
            return tesSUCCESS;
        JLOG(ctx.j.warn()) << "Tried to submit tx to burn CSC from non-blacklisted account";
        return tefFAILURE;
    }
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

static
void removeUnfundedOffers (ApplyView& view, std::vector<uint256> const& offers, beast::Journal viewJ)
{
    int removed = 0;

    for (auto const& index : offers)
    {
        auto const sleOffer = view.peek (keylet::offer (index));
        if (sleOffer)
        {
            // offer is unfunded
            offerDelete (view, sleOffer, viewJ);
            if (++removed == 1000)
                return;
        }
    }
}

void
Transactor::claimFee (CSCAmount& fee, TER terResult, std::vector<uint256> const& removedOffers)
{
    ctx_.discard();

    auto const txnAcct = view().peek(
        keylet::account(ctx_.tx.getAccountID(sfAccount)));

    auto const balance = txnAcct->getFieldAmount (sfBalance).csc ();

    // balance should have already been
    // checked in checkFee / preFlight.
    assert(balance != zero && (!view().open() || balance >= fee));
    // We retry/reject the transaction if the account
    // balance is zero or we're applying against an open
    // ledger and the balance is less than the fee
    if (fee > balance)
        fee = balance;
    txnAcct->setFieldAmount (sfBalance, balance - fee);
    txnAcct->setFieldU32 (sfSequence, ctx_.tx.getSequence() + 1);

    if (terResult == tecOVERSIZE)
        removeUnfundedOffers (view(), removedOffers, ctx_.app.journal ("View"));

    view().update (txnAcct);
}

//------------------------------------------------------------------------------
std::pair<TER, bool>
Transactor::operator()()
{
    JLOG(j_.trace()) <<
        "applyTransaction>";

    auto const txID = ctx_.tx.getTransactionID ();

    JLOG(j_.debug()) << "Transactor for id: " << txID;

//#ifdef BEAST_DEBUG
    {
        Serializer ser;
        ctx_.tx.add (ser);
        SerialIter sit(ser.slice());
        STTx s2 (sit);

        if (! s2.isEquivalent(ctx_.tx))
        {
            JLOG(j_.fatal()) <<
                "Transaction serdes mismatch";
            JLOG(j_.info()) << to_string(ctx_.tx.getJson (0));
            JLOG(j_.fatal()) << s2.getJson (0);
            assert (false);
        }
    }
//#endif

    auto terResult = ctx_.preclaimResult;
    if (terResult == tesSUCCESS)
        terResult = apply();

    // No transaction can return temUNKNOWN from apply,
    // and it can't be passed in from a preclaim.
    assert(terResult != temUNKNOWN);

    if (auto stream = j_.debug())
    {
        std::string strToken;
        std::string strHuman;

        transResultInfo (terResult, strToken, strHuman);

        stream <<
            "applyTransaction: terResult=" << strToken <<
            " : " << terResult <<
            " : " << strHuman;
    }

    bool didApply = isTesSuccess (terResult);
    auto fee = ctx_.tx.getFieldAmount(sfFee).csc ();

    if (ctx_.size() > 5200)
        terResult = tecOVERSIZE;

    if ((terResult == tecOVERSIZE) ||
        (isTecClaim (terResult) && !(view().flags() & tapRETRY)))
    {
        // only claim the transaction fee
        JLOG(j_.debug()) <<
            "Reprocessing tx " << txID << " to only claim fee";

        std::vector<uint256> removedOffers;
        if (terResult == tecOVERSIZE)
        {
            ctx_.visit (
                [&removedOffers](
                    uint256 const& index,
                    bool isDelete,
                    std::shared_ptr <SLE const> const& before,
                    std::shared_ptr <SLE const> const& after)
                {
                    if (isDelete)
                    {
                        assert (before && after);
                        if (before && after &&
                            (before->getType() == ltOFFER) &&
                            (before->getFieldAmount(sfTakerPays) == after->getFieldAmount(sfTakerPays)))
                        {
                            // Removal of offer found or made unfunded
                            removedOffers.push_back (index);
                        }
                    }
                });
        }

        claimFee(fee, terResult, removedOffers);
        didApply = true;
    }

    if (didApply)
    {
        // Check invariants
        // if `tecINVARIANT_FAILED` not returned, we can proceed to apply the tx
        terResult = ctx_.checkInvariants(terResult);
        if (terResult == tecINVARIANT_FAILED)
        {
            // if invariants failed, claim a fee still
            claimFee(fee, terResult, {});
            //Check invariants *again* to ensure the fee claiming doesn't
            //violate invariants.
            terResult = ctx_.checkInvariants(terResult);
            didApply = isTecClaim(terResult);
        }
    }

    if (didApply)
    {
        // Transaction succeeded fully or (retries are
        // not allowed and the transaction could claim a fee)

        if (!view().open())
        {
            // Charge whatever fee they specified.

            // The transactor guarantees this will never trigger
            if (fee < zero)
            {
                // VFALCO Log to journal here
                // JLOG(journal.fatal()) << "invalid fee";
                Throw<std::logic_error> ("amount is negative!");
            }

            if (fee != zero)
                ctx_.destroyCSC (fee);
        }

        ctx_.apply(terResult);
        // since we called apply(), it is not okay to look
        // at view() past this point.
    }
    else
    {
        JLOG(j_.debug()) << "Not applying transaction " << txID;
    }


    JLOG(j_.trace()) <<
        "apply: " << transToken(terResult) <<
        ", " << (didApply ? "true" : "false");

    return { terResult, didApply };
}

}
