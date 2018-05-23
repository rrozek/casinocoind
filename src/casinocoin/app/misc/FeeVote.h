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
    2017-06-27  ajochems        Refactored for casinocoin
*/
//==============================================================================

#ifndef CASINOCOIN_APP_MISC_FEEVOTE_H_INCLUDED
#define CASINOCOIN_APP_MISC_FEEVOTE_H_INCLUDED

#include <casinocoin/ledger/ReadView.h>
#include <casinocoin/shamap/SHAMap.h>
#include <casinocoin/app/misc/Validations.h>
#include <casinocoin/basics/BasicConfig.h>
#include <casinocoin/protocol/SystemParameters.h>

namespace casinocoin {

/** Manager to process fee votes. */
class FeeVote
{
public:
    /** Fee schedule to vote for.
        During voting ledgers, the FeeVote logic will try to move towards
        these values when injecting fee-setting transactions.
        A default-constructed Setup contains recommended values.
    */
    struct Setup
    {
        /** The cost of a reference transaction in drops. */
        std::uint64_t reference_fee = 1000000;

        /** The cost of a reference transaction in fee units. */
        // jrojek 23.05.2018 don't know about our ancestor,
        // but im confident that the reference_fee_units is abstract value
        // which actually is not votable and makes no sense. removing it.
        // std::uint64_t const reference_fee_units = 1000000;

        /** The account reserve requirement in drops. */
        std::uint64_t account_reserve = 10 * SYSTEM_CURRENCY_PARTS;

        /** The per-owned item reserve requirement in drops. */
        std::uint64_t owner_reserve = 1 * SYSTEM_CURRENCY_PARTS;

        Setup(){}
        ~Setup(){}
        Setup(const Setup& other)
            : reference_fee(other.reference_fee)
            , account_reserve(other.account_reserve)
            , owner_reserve(other.owner_reserve)
        {}
        Setup& operator=(const Setup& other)
        {
            reference_fee = other.reference_fee;
            account_reserve = other.account_reserve;
            owner_reserve = other.owner_reserve;
            return *this;
        }
    };

    virtual ~FeeVote () = default;

    /** Add local fee preference to validation.

        @param lastClosedLedger
        @param baseValidation
    */
    virtual
    void
    doValidation (std::shared_ptr<ReadView const> const& lastClosedLedger,
        STObject& baseValidation) = 0;

    /** Cast our local vote on the fee.

        @param lastClosedLedger
        @param initialPosition
    */
    virtual
    void
    doVoting (std::shared_ptr<ReadView const> const& lastClosedLedger,
        ValidationSet const& parentValidations,
            std::shared_ptr<SHAMap> const& initialPosition) = 0;

    virtual
    void
    updatePosition(Setup const& setup) = 0;
};

/** Build FeeVote::Setup from a config section. */
FeeVote::Setup
setup_FeeVote (Section const& section);

/** Create an instance of the FeeVote logic.
    @param setup The fee schedule to vote for.
    @param journal Where to log.
*/
std::unique_ptr <FeeVote>
make_FeeVote (FeeVote::Setup const& setup, beast::Journal journal);

} // casinocoin

#endif
