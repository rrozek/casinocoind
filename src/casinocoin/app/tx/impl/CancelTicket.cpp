//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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
#include <casinocoin/app/tx/impl/CancelTicket.h>
#include <casinocoin/basics/Log.h>
#include <casinocoin/protocol/Feature.h>
#include <casinocoin/protocol/Indexes.h>
#include <casinocoin/protocol/TxFlags.h>
#include <casinocoin/ledger/View.h>

namespace casinocoin {

TER
CancelTicket::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled(featureTickets))
        return temDISABLED;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    return preflight2 (ctx);
}

TER
CancelTicket::doApply ()
{
    uint256 const ticketId = ctx_.tx.getFieldH256 (sfTicketID);

    // VFALCO This is highly suspicious, we're requiring that the
    //        transaction provide the return value of getTicketIndex?
    SLE::pointer sleTicket = view().peek (keylet::ticket(ticketId));

    if (!sleTicket)
        return tecNO_ENTRY;

    auto const ticket_owner =
        sleTicket->getAccountID (sfAccount);

    bool authorized =
        account_ == ticket_owner;

    // The target can also always remove a ticket
    if (!authorized && sleTicket->isFieldPresent (sfTarget))
        authorized = (account_ == sleTicket->getAccountID (sfTarget));

    // And finally, anyone can remove an expired ticket
    if (!authorized && sleTicket->isFieldPresent (sfExpiration))
    {
        using tp = NetClock::time_point;
        using d = tp::duration;
        auto const expiration = tp{d{sleTicket->getFieldU32(sfExpiration)}};

        if (view().parentCloseTime() >= expiration)
            authorized = true;
    }

    if (!authorized)
        return tecNO_PERMISSION;

    std::uint64_t const hint (sleTicket->getFieldU64 (sfOwnerNode));

    auto viewJ = ctx_.app.journal ("View");
    TER const result = dirDelete (ctx_.view (), false, hint,
        keylet::ownerDir (ticket_owner), ticketId, false, (hint == 0), viewJ);

    adjustOwnerCount(view(), view().peek(
        keylet::account(ticket_owner)), -1, viewJ);
    ctx_.view ().erase (sleTicket);

    return result;
}

}

