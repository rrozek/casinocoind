//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/casinocoin/casinocoind
    Copyright (c) 2018 CasinoCoin Foundation

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
    2018-05-14  jrojek          Created
*/
//==============================================================================

#ifndef CASINOCOIN_APP_MISC_CRNROUND_H
#define CASINOCOIN_APP_MISC_CRNROUND_H

#include <casinocoin/app/ledger/Ledger.h>
#include <casinocoin/app/misc/Validations.h>
#include <casinocoin/core/ConfigSections.h>
#include <casinocoin/protocol/Protocol.h>
#include <casinocoin/app/misc/CRN.h>

namespace casinocoin {

class CRNRound
{
public:
    virtual ~CRNRound() = default;

    /** Add local CRN node eligibility conclusion to validation

        @param lastClosedLedger
        @param baseValidation
    */
    virtual
    void
    doValidation (std::shared_ptr<ReadView const> const& lastClosedLedger,
        STObject& baseValidation) = 0;

    /** Cast CRN eligibility conclusion to the ledger

        @param lastClosedLedger
        @param initialPosition
    */
    virtual
    void
    doVoting (std::shared_ptr<ReadView const> const& lastClosedLedger,
        ValidationSet const& parentValidations,
        std::shared_ptr<SHAMap> const& initialPosition) = 0;

    /** Update our current voting position with conclusion of last peer network crawl

        @param reports
    */
    virtual
    void
    updatePosition(std::list<STPerformanceReport::pointer> const& reports) = 0;

    // The highest-sequence ledger we have done a CRNRound
    std::atomic <std::uint32_t> mCRNRoundLedgerSeq = {0};

};

std::unique_ptr<CRNRound> make_CRNRound (Application& app, int majorityFraction, beast::Journal journal);

}  // casinocoin

#endif // CASINOCOIN_APP_MISC_CRNROUND_H
