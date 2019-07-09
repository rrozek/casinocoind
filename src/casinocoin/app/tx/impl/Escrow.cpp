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
#include <casinocoin/app/tx/impl/Escrow.h>
#include <casinocoin/app/misc/HashRouter.h>
#include <casinocoin/app/paths/CasinocoinState.h>
#include <casinocoin/basics/chrono.h>
#include <casinocoin/basics/Log.h>
#include <casinocoin/conditions/Condition.h>
#include <casinocoin/conditions/Fulfillment.h>
#include <casinocoin/protocol/digest.h>
#include <casinocoin/protocol/st.h>
#include <casinocoin/protocol/Feature.h>
#include <casinocoin/protocol/Indexes.h>
#include <casinocoin/protocol/TxFlags.h>
#include <casinocoin/protocol/CSCAmount.h>
#include <casinocoin/protocol/Quality.h>
#include <casinocoin/ledger/View.h>

// During an EscrowFinish, the transaction must specify both
// a condition and a fulfillment. We track whether that
// fulfillment matches and validates the condition.
#define SF_CF_INVALID  SF_PRIVATE5
#define SF_CF_VALID    SF_PRIVATE6

namespace casinocoin {

/*
    Escrow allows an account holder to sequester any amount
    of CSC in its own ledger entry, until the escrow process
    either finishes or is canceled.

    If the escrow process finishes successfully, then the
    destination account (which must exist) will receives the
    sequestered CSC. If the escrow is, instead, canceled,
    the account which created the escrow will receive the
    sequestered CSC back instead.

    EscrowCreate

        When an escrow is created, an optional condition may
        be attached. If present, that condition must be
        fulfilled for the escrow to successfully finish.

        At the time of creation, one or both of the fields
        sfCancelAfter and sfFinishAfter may be provided. If
        neither field is specified, the transaction is
        malformed.

        Since the escrow eventually becomes a payment, an
        optional DestinationTag and an optional SourceTag
        are supported in the EscrowCreate transaction.

        Validation rules:

            sfCondition
                If present, specifies a condition; the same
                condition along with its matching fulfillment
                are required during EscrowFinish.

            sfCancelAfter
                If present, escrow may be canceled after the
                specified time (seconds after the Casinocoin epoch).

            sfFinishAfter
                If present, must be prior to sfCancelAfter.
                A EscrowFinish succeeds only in ledgers after
                sfFinishAfter but before sfCancelAfter.

                If absent, same as parentCloseTime

            Malformed if both sfCancelAfter, sfFinishAfter
            are absent.

            Malformed if both sfFinishAfter, sfCancelAfter
            specified and sfCancelAfter <= sfFinishAfter

    EscrowFinish

        Any account may submit a EscrowFinish. If the escrow
        ledger entry specifies a condition, the EscrowFinish
        must provide the same condition and its associated
        fulfillment in the sfCondition and sfFulfillment
        fields, or else the EscrowFinish will fail.

        If the escrow ledger entry specifies sfFinishAfter, the
        transaction will fail if parentCloseTime <= sfFinishAfter.

        EscrowFinish transactions must be submitted before
        the escrow's sfCancelAfter if present.

        If the escrow ledger entry specifies sfCancelAfter, the
        transaction will fail if sfCancelAfter <= parentCloseTime.

        NOTE: The reason the condition must be specified again
              is because it must always be possible to verify
              the condition without retrieving the escrow
              ledger entry.

    EscrowCancel

        Any account may submit a EscrowCancel transaction.

        If the escrow ledger entry does not specify a
        sfCancelAfter, the cancel transaction will fail.

        If parentCloseTime <= sfCancelAfter, the transaction
        will fail.

        When a escrow is canceled, the funds are returned to
        the source account.

    By careful selection of fields in each transaction,
    these operations may be achieved:

        * Lock up CSC for a time period
        * Execute a payment conditionally
*/

//------------------------------------------------------------------------------

CSCAmount
EscrowCreate::calculateMaxSpend(STTx const& tx)
{
    return tx[sfAmount].csc();
}

TER
EscrowCreate::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled(featureEscrow))
        return temDISABLED;

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    if (! isCSC(ctx.tx[sfAmount]))
    {
        if (!ctx.rules.enabled(featureEscrowIOU))
            return temBAD_AMOUNT;
    }

    if (ctx.tx[sfAmount] <= beast::zero)
        return temBAD_AMOUNT;

    if (! ctx.tx[~sfCancelAfter] &&
            ! ctx.tx[~sfFinishAfter])
        return temBAD_EXPIRATION;

    if (ctx.tx[~sfCancelAfter] && ctx.tx[~sfFinishAfter] &&
            ctx.tx[sfCancelAfter] <= ctx.tx[sfFinishAfter])
        return temBAD_EXPIRATION;

    if (auto const cb = ctx.tx[~sfCondition])
    {
        using namespace casinocoin::cryptoconditions;

        std::error_code ec;

        auto condition = Condition::deserialize(*cb, ec);
        if (!condition)
        {
            JLOG(ctx.j.debug()) <<
                "Malformed condition during escrow creation: " << ec.message();
            return temMALFORMED;
        }

        // Conditions other than PrefixSha256 require the
        // "CryptoConditionsSuite" amendment:
        if (condition->type != Type::preimageSha256 &&
                !ctx.rules.enabled(featureCryptoConditionsSuite))
            return temDISABLED;
    }

    return preflight2 (ctx);
}

TER
EscrowCreate::doApply()
{
    auto const closeTime = view ().info ().parentCloseTime;

    if (ctx_.tx[~sfCancelAfter])
    {
        auto const cancelAfter = ctx_.tx[sfCancelAfter];

        if (closeTime.time_since_epoch().count() >= cancelAfter)
            return tecNO_PERMISSION;
    }

    if (ctx_.tx[~sfFinishAfter])
    {
        auto const finishAfter = ctx_.tx[sfFinishAfter];

        if (closeTime.time_since_epoch().count() >= finishAfter)
            return tecNO_PERMISSION;
    }

    auto const account = ctx_.tx[sfAccount];
    auto const sle = view().peek(
        keylet::account(account));
    auto const& amount = ctx_.tx[sfAmount];
    bool isCsc = isCSC(amount);
    // Check reserve and funds availability
    {
        // if not operating on CSC and escrowIOU is disabled -  bail out
        if (!isCsc && !ctx_.view().rules().enabled(featureEscrowIOU))
            return tecINVARIANT_FAILED;

        auto const balance = STAmount((*sle)[sfBalance]).csc();
        auto const reserve = view().fees().accountReserve(
            (*sle)[sfOwnerCount] + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;

        if (isCsc)
        {
            if(balance < reserve + STAmount(ctx_.tx[sfAmount]).csc())
                return tecUNFUNDED;
        }
        // if src is not issuer
        else if (account_ != amount.getIssuer())
        {
            SLE::pointer sleCSCStateSrc = view().peek(
                        keylet::line(account_, amount.getIssuer(), amount.getCurrency()));
            auto cscLine = CasinocoinState::makeItem(account_, sleCSCStateSrc);
            if (!cscLine)
                return tecNO_LINE;

            auto balance = cscLine->getBalance();
            bool negate = false;
            if (cscLine->getAccountID() == account_ && balance.negative())
            {
                negate = true;
            }
            if (negate)
            {
                STAmount amountTmp = amount;
                amountTmp.negate();
                if (balance > amountTmp)
                    return tecUNFUNDED_ESCROW;
            }
            else
            {
                if (balance.negative() || balance < amount)
                    return tecUNFUNDED_ESCROW;
            }
        }
        // applySteps grant that involved tokens are WLT tokens (if WLT amendment is enabled)
    }

    auto const& dest = ctx_.tx[sfDestination];
    // Check destination account
    {
        auto const sled = ctx_.view().read(
            keylet::account(dest));
        if (! sled)
            return tecNO_DST;
        if (((*sled)[sfFlags] & lsfRequireDestTag) &&
                ! ctx_.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;

        if (isCsc)
        {
            if ((*sled)[sfFlags] & lsfDisallowCSC)
                return tecNO_TARGET;
        }
        else
        {
            SLE::pointer sleDstLine = view().peek(
                keylet::line(dest, amount.getIssuer(), amount.getCurrency()));
            if (!sleDstLine)
                return tecNO_LINE;

            // check if src/dest line is frozen or issuer is frozen
            // check if requireAuth and if so - is authorized
            bool const bHigh = dest > amount.getIssuer();
            auto limit = sleDstLine->getFieldAmount(!bHigh ? sfLowLimit : sfHighLimit);
            if (limit < amount)
                return tecPATH_DRY;

        }
    }

    // Create escrow in ledger
    auto const slep = std::make_shared<SLE>(
        keylet::escrow(account, (*sle)[sfSequence] - 1));
    (*slep)[sfAmount] = ctx_.tx[sfAmount];
    (*slep)[sfAccount] = account;
    (*slep)[~sfCondition] = ctx_.tx[~sfCondition];
    (*slep)[~sfSourceTag] = ctx_.tx[~sfSourceTag];
    (*slep)[sfDestination] = ctx_.tx[sfDestination];
    (*slep)[~sfCancelAfter] = ctx_.tx[~sfCancelAfter];
    (*slep)[~sfFinishAfter] = ctx_.tx[~sfFinishAfter];
    (*slep)[~sfDestinationTag] = ctx_.tx[~sfDestinationTag];

    ctx_.view().insert(slep);

    // add to issuer's owner directory
    if (amount.getIssuer() != account && amount.getIssuer() != dest)
    {
        uint64_t page;
        auto result = dirAdd(ctx_.view(), page,
                             keylet::ownerDir(amount.getIssuer()), slep->key(),
                             describeOwnerDir(amount.getIssuer()), ctx_.app.journal("View"));
        if (! isTesSuccess(result.first))
            return result.first;
        (*slep)[sfIssuerNode] = page;
    }

    // Deduct owner's balance, increment owner count

    if (isCsc)
    {
        (*sle)[sfBalance] = (*sle)[sfBalance] - ctx_.tx[sfAmount];
    }
    else if (account_ != amount.getIssuer())
    {
        SLE::pointer sleSrc = view().peek(
            keylet::line(account_, amount.getIssuer(), amount.getCurrency()));
        STAmount const& balance = (*sleSrc)[sfBalance];
        if (balance.negative())
            (*sleSrc)[sfBalance] = (*sleSrc)[sfBalance] + ctx_.tx[sfAmount];
        else
            (*sleSrc)[sfBalance] = (*sleSrc)[sfBalance] - ctx_.tx[sfAmount];

        ctx_.view().update(sleSrc);
    }

    (*sle)[sfOwnerCount] = (*sle)[sfOwnerCount] + 1;
    ctx_.view().update(sle);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

static
bool
checkCondition (Slice f, Slice c)
{
    using namespace casinocoin::cryptoconditions;

    std::error_code ec;

    auto condition = Condition::deserialize(c, ec);
    if (!condition)
        return false;

    auto fulfillment = Fulfillment::deserialize(f, ec);
    if (!fulfillment)
        return false;

    return validate (*fulfillment, *condition);
}

TER
EscrowFinish::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled(featureEscrow))
        return temDISABLED;

    {
        auto const ret = preflight1 (ctx);
        if (!isTesSuccess (ret))
            return ret;
    }

    auto const cb = ctx.tx[~sfCondition];
    auto const fb = ctx.tx[~sfFulfillment];

    // If you specify a condition, then you must also specify
    // a fulfillment.
    if (static_cast<bool>(cb) != static_cast<bool>(fb))
        return temMALFORMED;

    // Verify the transaction signature. If it doesn't work
    // then don't do any more work.
    {
        auto const ret = preflight2 (ctx);
        if (!isTesSuccess (ret))
            return ret;
    }

    if (cb && fb)
    {
        auto& router = ctx.app.getHashRouter();

        auto const id = ctx.tx.getTransactionID();
        auto const flags = router.getFlags (id);

        // If we haven't checked the condition, check it
        // now. Whether it passes or not isn't important
        // in preflight.
        if (!(flags & (SF_CF_INVALID | SF_CF_VALID)))
        {
            if (checkCondition (*fb, *cb))
                router.setFlags (id, SF_CF_VALID);
            else
                router.setFlags (id, SF_CF_INVALID);
        }
    }

    return tesSUCCESS;
}

std::uint64_t
EscrowFinish::calculateBaseFee (PreclaimContext const& ctx)
{
    std::uint64_t extraFeeFactor = 0;

    if (auto const fb = ctx.tx[~sfFulfillment])
    {
        extraFeeFactor += ctx.view.fees().units *
            (32 + static_cast<std::uint64_t> (fb->size() / 16));
    }

    return Transactor::calculateBaseFee (ctx) + extraFeeFactor;
}

TER
EscrowFinish::doApply()
{
    auto const k = keylet::escrow(
        ctx_.tx[sfOwner], ctx_.tx[sfOfferSequence]);
    auto const slep = ctx_.view().peek(k);
    if (! slep)
        return tecNO_TARGET;

    // Too soon?
    if ((*slep)[~sfFinishAfter] &&
        ctx_.view().info().parentCloseTime.time_since_epoch().count() <=
            (*slep)[sfFinishAfter])
        return tecNO_PERMISSION;

    // Too late?
    if ((*slep)[~sfCancelAfter] &&
        (*slep)[sfCancelAfter] <=
            ctx_.view().info().parentCloseTime.time_since_epoch().count())
        return tecNO_PERMISSION;

    // Check cryptocondition fulfillment
    {
        auto const id = ctx_.tx.getTransactionID();
        auto flags = ctx_.app.getHashRouter().getFlags (id);

        auto const cb = ctx_.tx[~sfCondition];

        // It's unlikely that the results of the check will
        // expire from the hash router, but if it happens,
        // simply re-run the check.
        if (cb && ! (flags & (SF_CF_INVALID | SF_CF_VALID)))
        {
            auto const fb = ctx_.tx[~sfFulfillment];

            if (!fb)
                return tecINTERNAL;

            if (checkCondition (*fb, *cb))
                flags = SF_CF_VALID;
            else
                flags = SF_CF_INVALID;

            ctx_.app.getHashRouter().setFlags (id, flags);
        }

        // If the check failed, then simply return an error
        // and don't look at anything else.
        if (flags & SF_CF_INVALID)
            return tecCRYPTOCONDITION_ERROR;

        // Check against condition in the ledger entry:
        auto const cond = (*slep)[~sfCondition];

        // If a condition wasn't specified during creation,
        // one shouldn't be included now.
        if (!cond && cb)
            return tecCRYPTOCONDITION_ERROR;

        // If a condition was specified during creation of
        // the suspended payment, the identical condition
        // must be presented again. We don't check if the
        // fulfillment matches the condition since we did
        // that in preflight.
        if (cond && (cond != cb))
            return tecCRYPTOCONDITION_ERROR;
    }

    AccountID const account = (*slep)[sfAccount];
    STAmount const& amount = (*slep)[sfAmount];

    // Remove escrow from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        TER const ter = dirDelete(ctx_.view(), true,
            page, keylet::ownerDir(account).key,
                k.key, false, page == 0, ctx_.app.journal ("View"));
        if (! isTesSuccess(ter))
            return ter;
    }


    // Fetch Destination SLE
//    auto const sled = ctx_.view().peek(
//        keylet::account((*slep)[sfDestination]));
//    if (! sled)
//        return tecNO_DST;

//    // Transfer amount to destination
//    (*sled)[sfBalance] = (*sled)[sfBalance] + (*slep)[sfAmount];
//    ctx_.view().update(sled);

    bool const isCsc = isCSC(amount);
    if (!isCsc)
    {
    }

    // Fetch Destination SLE,transfer amount to destination
    if (isCsc)
    {
        SLE::pointer sled = ctx_.view().peek(
                    keylet::account((*slep)[sfDestination]));
        if (!sled)
            return tecNO_DST;
        (*sled)[sfBalance] = (*sled)[sfBalance] + (*slep)[sfAmount];
        ctx_.view().update(sled);
    }
    else
    {
        //Remove escrow from issuer's owner directory
        AccountID const destination = (*slep)[sfDestination];
        if (amount.getIssuer() != account && amount.getIssuer() != destination)
        {
            TER const ter = dirDelete(ctx_.view(), true,
                (*slep)[sfIssuerNode], keylet::ownerDir(amount.getIssuer()),
                k.key, false, false, ctx_.app.journal("View"));
            if (!isTesSuccess(ter))
                return ter;
        }

        SLE::pointer sled = view().peek(
                    keylet::line((*slep)[sfDestination], amount.getIssuer(), amount.getCurrency()));
        if (!sled)
            return tecNO_LINE;
        bool const bHigh = destination > amount.getIssuer();
        auto limit = sled->getFieldAmount(!bHigh ? sfLowLimit : sfHighLimit);
        if (limit < amount)
            return tecPATH_DRY;
        // If the gateway has a transfer rate, accommodate that.
        Rate gatewayXferRate{ QUALITY_ONE };
        STAmount amountSend = amount;
        if (account_ != amount.getIssuer())
        {
            gatewayXferRate = transferRate(ctx_.view(), amount.getIssuer());
            if (gatewayXferRate.value != QUALITY_ONE)
            {
                amountSend = divideRound(amount,
                                         gatewayXferRate, true);
            }
        }

        //transfer amount to destination
        if (bHigh)
            (*sled)[sfBalance] = (*sled)[sfBalance] - amountSend;
        else
            (*sled)[sfBalance] = (*sled)[sfBalance] + amountSend;
        ctx_.view().update(sled);
    }

    // Adjust source owner count
    auto const sle = ctx_.view().peek(
        keylet::account(account));
    (*sle)[sfOwnerCount] = (*sle)[sfOwnerCount] - 1;
    // NOTE: These payments cannot be used to fund accounts
    ctx_.view().update(sle);

    // Remove escrow from ledger
    ctx_.view().erase(slep);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

TER
EscrowCancel::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled(featureEscrow))
        return temDISABLED;

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    return preflight2 (ctx);
}

TER
EscrowCancel::doApply()
{
    auto const k = keylet::escrow(
        ctx_.tx[sfOwner], ctx_.tx[sfOfferSequence]);
    auto const slep = ctx_.view().peek(k);
    if (! slep)
        return tecNO_TARGET;

    // Too soon?
    if (! (*slep)[~sfCancelAfter] ||
        ctx_.view().info().parentCloseTime.time_since_epoch().count() <=
            (*slep)[sfCancelAfter])
        return tecNO_PERMISSION;

    AccountID const account = (*slep)[sfAccount];

    // Remove escrow from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        TER const ter = dirDelete(ctx_.view(), true,
            page, keylet::ownerDir(account).key,
                k.key, false, page == 0, ctx_.app.journal ("View"));
        if (! isTesSuccess(ter))
            return ter;
    }

    // Transfer amount back to owner, decrement owner count
//    auto const sle = ctx_.view().peek(
//        keylet::account(account));
//    (*sle)[sfBalance] = (*sle)[sfBalance] + (*slep)[sfAmount];

    //Remove escrow from issuer's owner directory
    AccountID const destination = (*slep)[sfDestination];
    if (amount.getIssuer() != account && amount.getIssuer() != destination)
    {
        TER const ter = dirDelete(ctx_.view(), true,
            (*slep)[sfIssuerNode], keylet::ownerDir(amount.getIssuer()),
            k.key, false, false, ctx_.app.journal("View"));
        if (!isTesSuccess(ter))
            return ter;
    }

    // Transfer amount back to owner
    bool isCsc = isCSC(amount);
    // Fetch Destination SLE,transfer amount to src
    if (isCsc)
    {
        SLE::pointer sled = ctx_.view().peek(
            keylet::account(account));
        (*sled)[sfBalance] = (*sled)[sfBalance] + (*slep)[sfAmount];
        ctx_.view().update(sled);
    }
    else if (account != amount.getIssuer())
    {
        SLE::pointer sled = view().peek(
            keylet::line(account, amount.getIssuer(), amount.getCurrency()));
        bool const bHigh = account > amount.getIssuer();
        if (bHigh)
            (*sled)[sfBalance] = (*sled)[sfBalance] - (*slep)[sfAmount];
        else
            (*sled)[sfBalance] = (*sled)[sfBalance] + (*slep)[sfAmount];
        ctx_.view().update(sled);
    }
    // Decrement owner count
    auto const sle = ctx_.view().peek(
        keylet::account(account));
    (*sle)[sfOwnerCount] = (*sle)[sfOwnerCount] - 1;
    ctx_.view().update(sle);

    // Remove escrow from ledger
    ctx_.view().erase(slep);

    return tesSUCCESS;
}

} // casinocoin

