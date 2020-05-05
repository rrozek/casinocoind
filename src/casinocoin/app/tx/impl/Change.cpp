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
    2017-06-28  ajochems        Refactored for casinocoin
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/app/tx/impl/Change.h>
#include <casinocoin/app/main/Application.h>
#include <casinocoin/app/misc/AmendmentTable.h>
#include <casinocoin/app/misc/NetworkOPs.h>
#include <casinocoin/basics/Log.h>
#include <casinocoin/protocol/Indexes.h>
#include <casinocoin/protocol/TxFlags.h>
#include <casinocoin/protocol/Feature.h>

namespace casinocoin {

TER
Change::preflight (PreflightContext const& ctx)
{
    auto const ret = preflight0(ctx);
    if (!isTesSuccess(ret))
        return ret;

    auto account = ctx.tx.getAccountID(sfAccount);
    if (account != zero)
    {
        JLOG(ctx.j.warn()) << "Change: Bad source id";
        return temBAD_SRC_ACCOUNT;
    }

    // check for ttCONFIG and ttCRN_ROUND before 'Regular' transactions
    if (ctx.tx.getTxnType() == ttCONFIG)
    {
        TER status = preflightConfiguration(ctx);
        if (status != tesSUCCESS)
            return status;
    }

    if (ctx.tx.getTxnType() == ttCRN_ROUND)
    {
        TER status = preflightCRNRound(ctx);
        if (status != tesSUCCESS)
            return status;
    }

    // No point in going any further if the transaction fee is malformed.
    auto const fee = ctx.tx.getFieldAmount (sfFee);
    if (!fee.native () || fee != beast::zero)
    {
        JLOG(ctx.j.warn()) << "Change: invalid fee";
        return temBAD_FEE;
    }

    if (!ctx.tx.getSigningPubKey ().empty () ||
        !ctx.tx.getSignature ().empty () ||
        ctx.tx.isFieldPresent (sfSigners))
    {
        JLOG(ctx.j.warn()) << "Change: Bad signature";
        return temBAD_SIGNATURE;
    }

    if (ctx.tx.getSequence () != 0 || ctx.tx.isFieldPresent (sfPreviousTxnID))
    {
        JLOG(ctx.j.warn()) << "Change: Bad sequence";
        return temBAD_SEQUENCE;
    }

    return tesSUCCESS;
}

TER
Change::preclaim(PreclaimContext const &ctx)
{
    // If tapOPEN_LEDGER is resurrected into ApplyFlags,
    // this block can be moved to preflight.
    if (ctx.view.open())
    {
        JLOG(ctx.j.warn()) << "Change transaction against open ledger";
        return temINVALID;
    }

    if (ctx.tx.getTxnType() != ttAMENDMENT
        && ctx.tx.getTxnType() != ttFEE
        && ctx.tx.getTxnType() != ttCONFIG
        && ctx.tx.getTxnType() != ttCRN_ROUND)
        return temUNKNOWN;

    return tesSUCCESS;
}

TER
Change::preflightConfiguration(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureConfigObject))
    {
        JLOG(ctx.j.warn()) << "Change: ConfigObject feature disabled";
        return temDISABLED;
    }
    return tesSUCCESS;
}

TER Change::preflightCRNRound(const PreflightContext &ctx)
{
    if (!ctx.rules.enabled(featureCRN))
    {
        JLOG(ctx.j.warn()) << "Change: CRN feature disabled";
        return temDISABLED;
    }
    STArray crnArray = ctx.tx.getFieldArray(sfCRNs);
    for ( auto const& crnObject : crnArray)
    {
        if (!crnObject.isFieldPresent(sfCRN_PublicKey))
        {
            JLOG(ctx.j.warn()) << "CRNRound malformed transaction";
            return temMALFORMED;
        }
    }
    return tesSUCCESS;
}


TER
Change::doApply()
{
    if (ctx_.tx.getTxnType () == ttAMENDMENT)
        return applyAmendment ();

    if (ctx_.tx.getTxnType () == ttCONFIG)
        return applyConfiguration ();

    if (ctx_.tx.getTxnType () == ttFEE)
        return applyFee();

    assert(ctx_.tx.getTxnType () == ttCRN_ROUND);
    return applyCRN_Round ();
}

void
Change::preCompute()
{
    account_ = ctx_.tx.getAccountID(sfAccount);
    assert(account_ == zero);
}

TER
Change::applyAmendment()
{
    uint256 amendment (ctx_.tx.getFieldH256 (sfAmendment));

    auto const k = keylet::amendments();

    SLE::pointer amendmentObject =
        view().peek (k);

    if (!amendmentObject)
    {
        amendmentObject = std::make_shared<SLE>(k);
        view().insert(amendmentObject);
    }

    STVector256 amendments =
        amendmentObject->getFieldV256(sfAmendments);

    if (std::find (amendments.begin(), amendments.end(),
            amendment) != amendments.end ())
        return tefALREADY;

    auto flags = ctx_.tx.getFlags ();

    const bool gotMajority = (flags & tfGotMajority) != 0;
    const bool lostMajority = (flags & tfLostMajority) != 0;

    if (gotMajority && lostMajority)
        return temINVALID_FLAG;

    STArray newMajorities (sfMajorities);

    bool found = false;
    if (amendmentObject->isFieldPresent (sfMajorities))
    {
        const STArray &oldMajorities = amendmentObject->getFieldArray (sfMajorities);
        for (auto const& majority : oldMajorities)
        {
            if (majority.getFieldH256 (sfAmendment) == amendment)
            {
                if (gotMajority)
                    return tefALREADY;
                found = true;
            }
            else
            {
                // pass through
                newMajorities.push_back (majority);
            }
        }
    }

    if (! found && lostMajority)
        return tefALREADY;

    if (gotMajority)
    {
        // This amendment now has a majority
        newMajorities.push_back (STObject (sfMajority));
        auto& entry = newMajorities.back ();
        entry.emplace_back (STHash256 (sfAmendment, amendment));
        entry.emplace_back (STUInt32 (sfCloseTime,
            view().parentCloseTime().time_since_epoch().count()));

        if (!ctx_.app.getAmendmentTable ().isSupported (amendment))
        {
            JLOG (j_.warn()) <<
                "Unsupported amendment " << amendment <<
                " received a majority.";
        }
    }
    else if (!lostMajority)
    {
        // No flags, enable amendment
        amendments.push_back (amendment);
        amendmentObject->setFieldV256 (sfAmendments, amendments);

        ctx_.app.getAmendmentTable ().enable (amendment);

        if (!ctx_.app.getAmendmentTable ().isSupported (amendment))
        {
            JLOG (j_.error()) <<
                "Unsupported amendment " << amendment <<
                " activated: server blocked.";
            ctx_.app.getOPs ().setAmendmentBlocked ();
        }
    }

    if (newMajorities.empty ())
        amendmentObject->makeFieldAbsent (sfMajorities);
    else
        amendmentObject->setFieldArray (sfMajorities, newMajorities);

    view().update (amendmentObject);

    return tesSUCCESS;
}

TER
Change::applyFee()
{
    auto const k = keylet::fees();

    SLE::pointer feeObject = view().peek (k);

    if (!feeObject)
    {
        feeObject = std::make_shared<SLE>(k);
        view().insert(feeObject);
    }

    feeObject->setFieldU64 (
        sfBaseFee, ctx_.tx.getFieldU64 (sfBaseFee));
    feeObject->setFieldU32 (
        sfReferenceFeeUnits, ctx_.tx.getFieldU32 (sfReferenceFeeUnits));
    feeObject->setFieldU32 (
        sfReserveBase, ctx_.tx.getFieldU32 (sfReserveBase));
    feeObject->setFieldU32 (
        sfReserveIncrement, ctx_.tx.getFieldU32 (sfReserveIncrement));

    view().update (feeObject);

    JLOG(j_.warn()) << "Fees have been changed";
    return tesSUCCESS;
}

TER
Change::applyConfiguration()
{
     JLOG(j_.info()) << "applyConfiguration";
     auto const k = keylet::configuration();

    SLE::pointer configObject = view().peek (k);

    if (!configObject)
    {
        configObject = std::make_shared<SLE>(k);
        view().insert(configObject);
    }

    const uint32_t txObjId = ctx_.tx.getFieldU32(sfConfigID);
    const uint32_t txObjType = ctx_.tx.getFieldU32(sfConfigType);
    const Blob txConfigData = ctx_.tx.getFieldVL(sfConfigData);

    STArray ledgerConfigArray(sfConfiguration);
    if (configObject->isFieldPresent(sfConfiguration))
        ledgerConfigArray = configObject->getFieldArray(sfConfiguration);

    auto ledgerConfigArrayIter = std::find_if(ledgerConfigArray.begin(), ledgerConfigArray.end(),
        [&txObjId](STObject const& ledgerConfigObject)
        {
            return ledgerConfigObject.getFieldU32(sfConfigID) == txObjId;
        });
    if (ledgerConfigArrayIter != ledgerConfigArray.end())
    {
        if ((*ledgerConfigArrayIter).getFieldVL(sfConfigData) == txConfigData
             && (*ledgerConfigArrayIter).getFieldU32(sfConfigType) == txObjType)
        {
            JLOG(j_.info()) << "applyConfiguration: no change";
            return tefALREADY;
        }
        // we are updating existing config entry
        (*ledgerConfigArrayIter).setFieldU32(sfConfigType, txObjType);
        (*ledgerConfigArrayIter).setFieldVL(sfConfigData, txConfigData);
    }
    else
    {
        // we are adding new config entry
        ledgerConfigArray.push_back(STObject(sfConfigEntry));
        auto& entry = ledgerConfigArray.back();
        entry.emplace_back(STUInt32(sfConfigID, txObjId));
        entry.emplace_back(STUInt32(sfConfigType, txObjType));
        entry.emplace_back(STBlob(sfConfigData, txConfigData.data(), txConfigData.size()));
    }

    if (ledgerConfigArray.empty())
        configObject->makeFieldAbsent(sfConfiguration);
    else
        configObject->setFieldArray(sfConfiguration, ledgerConfigArray);
    JLOG(j_.info()) << "applyConfiguration: update view";

    view().update(configObject);
    JLOG(j_.info()) << "applyConfiguration: tesSUCCESS";
    return tesSUCCESS;
}

TER Change::applyCRN_Round()
{
    JLOG(j_.info()) << "applyCRN_Round";
    auto const keyletCrnRound = keylet::crnRound();

    SLE::pointer ledgerCrnRoundObject = view().peek(keyletCrnRound);

    if (!ledgerCrnRoundObject)
    {
        ledgerCrnRoundObject = std::make_shared<SLE>(keyletCrnRound);
        view().insert(ledgerCrnRoundObject);
    }

    STArray ledgerCrnArray(sfCRNs);
    if (ledgerCrnRoundObject->isFieldPresent(sfCRNs))
        ledgerCrnArray = ledgerCrnRoundObject->getFieldArray(sfCRNs);

    STArray txCrnArray = ctx_.tx.getFieldArray(sfCRNs);
    // extra sanity check. verify that math won't overflow
    {
        STAmount const& feeToDistributeST = ctx_.tx.getFieldAmount(sfCRN_FeeDistributed);
        CSCAmount sumOfDistribution(beast::zero);
        for ( STObject const& txCrnObject : txCrnArray)
        {
            CSCAmount before = sumOfDistribution;
            sumOfDistribution += txCrnObject.getFieldAmount(sfCRN_FeeDistributed).csc();
            if (before > sumOfDistribution)
            {
                JLOG(j_.error()) << "Math overflow. sumBefore: " << before.drops()
                                << " after adding: " << txCrnObject.getFieldAmount(sfCRN_FeeDistributed).csc().drops()
                                << " sum: " << sumOfDistribution.drops()
                                << " quit!";
                return tefEXCEPTION;
            }
        }
        if (sumOfDistribution != feeToDistributeST.csc())
        {
            JLOG(j_.error()) << "Math error. sumOfDistribution: " << sumOfDistribution.drops()
                            << " feeToDistributeST: " << feeToDistributeST.csc().drops()
                            << " quit!";
            return tefEXCEPTION;
        }
    }
    for ( STObject const& txCrnObject : txCrnArray)
    {
        if (!txCrnObject.isFieldPresent(sfCRN_PublicKey))
        {
            JLOG(j_.error()) << "CRNRound malformed transaction. Should be caught in preflight";
            return temMALFORMED;
        }
        Blob pkBlob = txCrnObject.getFieldVL(sfCRN_PublicKey);
        PublicKey crnPubKey(Slice(pkBlob.data(), pkBlob.size()));
        AccountID dstAccountID = calcAccountID(crnPubKey);

        auto ledgerCrnArrayIter = std::find_if(ledgerCrnArray.begin(), ledgerCrnArray.end(), [&pkBlob](STObject const& ledgerCRNObject)
        {
            return ledgerCRNObject.getFieldVL(sfCRN_PublicKey) == pkBlob;
        });
        if (ledgerCrnArrayIter != ledgerCrnArray.end())
        {
            (*ledgerCrnArrayIter).setFieldAmount(sfCRN_FeeDistributed,
                                                 (*ledgerCrnArrayIter).getFieldAmount(sfCRN_FeeDistributed) +
                                                 txCrnObject.getFieldAmount(sfCRN_FeeDistributed));
        }
        else
        {
            ledgerCrnArray.push_back (STObject (sfCRN));
            auto& entry = ledgerCrnArray.back ();
            entry.emplace_back (STBlob (sfCRN_PublicKey, pkBlob.data(), pkBlob.size()));
            entry.emplace_back (txCrnObject.getFieldAmount(sfCRN_FeeDistributed));
        }

        auto const keyletDstAccount = keylet::account(dstAccountID);
        SLE::pointer sleDst = view().peek (keyletDstAccount);
        if (!sleDst)
        {
            JLOG(j_.error()) << "CRNRound malformed transaction. Fee receiver account: " <<toBase58(dstAccountID)
                             << "does not exist.";
            return temMALFORMED;
        }
        view().update (sleDst);

        sleDst->setFieldAmount(sfBalance,
                               sleDst->getFieldAmount(sfBalance) +
                               txCrnObject.getFieldAmount(sfCRN_FeeDistributed));

        // Re-arm the password change fee if we can and need to.
        if ((sleDst->getFlags () & lsfPasswordSpent))
            sleDst->clearFlag (lsfPasswordSpent);
    }

    // add new tx id to tx array
    STVector256 crnTxHistory;
    if (ledgerCrnRoundObject->isFieldPresent(sfCRNTxHistory))
    {
        crnTxHistory = ledgerCrnRoundObject->getFieldV256(sfCRNTxHistory);
    }
    crnTxHistory.push_back (ctx_.tx.getTransactionID());

    // update the ledger with the new values
    view().update (ledgerCrnRoundObject);
    ledgerCrnRoundObject->setFieldArray(sfCRNs, ledgerCrnArray);
    ledgerCrnRoundObject->setFieldAmount(sfCRN_FeeDistributed,
                                         (ledgerCrnRoundObject->getFieldAmount(sfCRN_FeeDistributed) +
                                         ctx_.tx.getFieldAmount(sfCRN_FeeDistributed)));
    if (ctx_.tx.isFieldPresent(sfLedgerSequence))
    {
        ledgerCrnRoundObject->setFieldU32(sfLedgerSequence, ctx_.tx.getFieldU32(sfLedgerSequence));
    }
    ledgerCrnRoundObject->setFieldV256(sfCRNTxHistory, crnTxHistory);

    // here, drops are added back to the pool
    ctx_.redistributeCSC(ctx_.tx.getFieldAmount(sfCRN_FeeDistributed).csc());

    JLOG(j_.info()) << "CRN_Round: " << ctx_.tx.getFieldAmount(sfCRN_FeeDistributed).getFullText() << " Fees have been re-distributed";
    return tesSUCCESS;
}


}

