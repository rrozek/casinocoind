//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2016 Ripple Labs Inc.

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

#include <casinocoin/app/tx/impl/InvariantCheck.h>
#include <casinocoin/basics/Log.h>

namespace casinocoin {

void
CSCNotCreated::visitEntry(
    uint256 const&,
    bool isDelete,
    std::shared_ptr <SLE const> const& before,
    std::shared_ptr <SLE const> const& after)
{
    if(before)
    {
        switch (before->getType())
        {
        case ltACCOUNT_ROOT:
            drops_ -= (*before)[sfBalance].csc().drops();
            break;
        case ltPAYCHAN:
            drops_ -= ((*before)[sfAmount] - (*before)[sfBalance]).csc().drops();
            break;
        case ltESCROW:
            drops_ -= (*before)[sfAmount].csc().drops();
            break;
            // jrojek: inverted logic, because we store here the amount of drops distributed
        case ltCRN_ROUND:
            drops_ += (*before)[sfCRN_FeeDistributed].csc().drops();
            break;
        default:
            break;
        }
    }

    if(after)
    {
        switch (after->getType())
        {
        case ltACCOUNT_ROOT:
            drops_ += (*after)[sfBalance].csc().drops();
            break;
        case ltPAYCHAN:
            if (! isDelete)
                drops_ += ((*after)[sfAmount] - (*after)[sfBalance]).csc().drops();
            break;
        case ltESCROW:
            if (! isDelete)
                drops_ += (*after)[sfAmount].csc().drops();
            break;
            // jrojek: inverted logic, because we store here the amount of drops distributed
        case ltCRN_ROUND:
            drops_ -= (*after)[sfCRN_FeeDistributed].csc().drops();
            break;
        default:
            break;
        }
    }
}

bool
CSCNotCreated::finalize(STTx const& tx, TER /*tec*/, beast::Journal const& j)
{
    auto fee = tx.getFieldAmount(sfFee).csc().drops();
    if (tx.isFieldPresent(sfDestination) &&
            tx.isFieldPresent(sfAmount) &&
            tx.getAccountID(sfDestination) == burnThreeAccount())
    {
        auto burnedAmount = tx.getFieldAmount(sfAmount).csc().drops();
        JLOG(j.warn()) << "Invariant CSCNotCreated allowing burning CSC on burn#3 account " << toBase58(burnThreeAccount())
                       << " amount of CSC burned: " << burnedAmount;
        drops_ += burnedAmount;
    }
    if(-1*fee <= drops_ && drops_ <= 0)
        return true;

    JLOG(j.fatal()) << "Invariant failed: CSC net change was " << drops_ <<
        " on a fee of " << fee;
    return false;
}

//------------------------------------------------------------------------------

void
CSCBalanceChecks::visitEntry(
    uint256 const&,
    bool,
    std::shared_ptr <SLE const> const& before,
    std::shared_ptr <SLE const> const& after)
{
    auto isBad = [](STAmount const& balance)
    {
        if (!balance.native())
            return true;

        auto const drops = balance.csc().drops();

        // Can't have more than the number of drops instantiated
        // in the genesis ledger.
        if (drops > SYSTEM_CURRENCY_START)
            return true;

        // Can't have a negative balance (0 is OK)
        if (drops < 0)
            return true;

        return false;
    };

    if(before && before->getType() == ltACCOUNT_ROOT)
        bad_ |= isBad ((*before)[sfBalance]);

    if(after && after->getType() == ltACCOUNT_ROOT)
        bad_ |= isBad ((*after)[sfBalance]);
}

bool
CSCBalanceChecks::finalize(STTx const&, TER, beast::Journal const& j)
{
    if (bad_)
    {
        JLOG(j.fatal()) << "Invariant failed: incorrect account CSC balance";
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
NoBadOffers::visitEntry(
    uint256 const&,
    bool isDelete,
    std::shared_ptr <SLE const> const& before,
    std::shared_ptr <SLE const> const& after)
{
    auto isBad = [](STAmount const& pays, STAmount const& gets)
    {
        // An offer should never be negative
        if (pays < beast::zero)
            return true;

        if (gets < beast::zero)
            return true;

        // Can't have an CSC to CSC offer:
        return pays.native() && gets.native();
    };

    if(before && before->getType() == ltOFFER)
        bad_ |= isBad ((*before)[sfTakerPays], (*before)[sfTakerGets]);

    if(after && after->getType() == ltOFFER)
        bad_ |= isBad((*after)[sfTakerPays], (*after)[sfTakerGets]);
}

bool
NoBadOffers::finalize(STTx const& tx, TER, beast::Journal const& j)
{
    if (bad_)
    {
        JLOG(j.fatal()) << "Invariant failed: offer with a bad amount";
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
NoZeroEscrow::visitEntry(
    uint256 const&,
    bool isDelete,
    std::shared_ptr <SLE const> const& before,
    std::shared_ptr <SLE const> const& after)
{
    auto isBad = [](STAmount const& amount)
    {
        if (!amount.native())
            return true;

        if (amount.csc().drops() <= 0)
            return true;

        if (amount.csc().drops() >= SYSTEM_CURRENCY_START)
            return true;

        return false;
    };

    if(before && before->getType() == ltESCROW)
        bad_ |= isBad((*before)[sfAmount]);

    if(after && after->getType() == ltESCROW)
        bad_ |= isBad((*after)[sfAmount]);
}

bool
NoZeroEscrow::finalize(STTx const& tx, TER, beast::Journal const& j)
{
    if (bad_)
    {
        JLOG(j.fatal()) << "Invariant failed: escrow specifies invalid amount";
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------

void
AccountRootsNotDeleted::visitEntry(
    uint256 const&,
    bool isDelete,
    std::shared_ptr <SLE const> const& before,
    std::shared_ptr <SLE const> const&)
{
    if (isDelete && before && before->getType() == ltACCOUNT_ROOT)
        accountDeleted_ = true;
}

bool
AccountRootsNotDeleted::finalize(STTx const&, TER, beast::Journal const& j)
{
    if (! accountDeleted_)
        return true;

    JLOG(j.fatal()) << "Invariant failed: an account root was deleted";
    return false;
}

//------------------------------------------------------------------------------

void
LedgerEntryTypesMatch::visitEntry(
    uint256 const&,
    bool,
    std::shared_ptr <SLE const> const& before,
    std::shared_ptr <SLE const> const& after)
{
    if (before && after && before->getType() != after->getType())
        typeMismatch_ = true;

    if (after)
    {
        switch (after->getType())
        {
        case ltACCOUNT_ROOT:
        case ltDIR_NODE:
        case ltCASINOCOIN_STATE:
        case ltTICKET:
        case ltSIGNER_LIST:
        case ltOFFER:
        case ltLEDGER_HASHES:
        case ltAMENDMENTS:
        case ltCONFIGURATION:
        case ltFEE_SETTINGS:
        case ltCRN_ROUND:
        case ltESCROW:
        case ltPAYCHAN:
            break;
        default:
            invalidTypeAdded_ = true;
            break;
        }
    }
}

bool
LedgerEntryTypesMatch::finalize(STTx const&, TER, beast::Journal const& j)
{
    if ((! typeMismatch_) && (! invalidTypeAdded_))
        return true;

    if (typeMismatch_)
    {
        JLOG(j.fatal()) << "Invariant failed: ledger entry type mismatch";
    }

    if (invalidTypeAdded_)
    {
        JLOG(j.fatal()) << "Invariant failed: invalid ledger entry type added";
    }

    return false;
}

//------------------------------------------------------------------------------

void
NoCSCTrustLines::visitEntry(
    uint256 const&,
    bool,
    std::shared_ptr <SLE const> const&,
    std::shared_ptr <SLE const> const& after)
{
    if (after && after->getType() == ltCASINOCOIN_STATE)
    {
        // checking the issue directly here instead of
        // relying on .native() just in case native somehow
        // were systematically incorrect
        cscTrustLine_ =
            after->getFieldAmount (sfLowLimit).issue() == cscIssue() ||
            after->getFieldAmount (sfHighLimit).issue() == cscIssue();
    }
}

bool
NoCSCTrustLines::finalize(STTx const&, TER, beast::Journal const& j)
{
    if (! cscTrustLine_)
        return true;

    JLOG(j.fatal()) << "Invariant failed: an CSC trust line was created";
    return false;
}

}  // casinocoin

