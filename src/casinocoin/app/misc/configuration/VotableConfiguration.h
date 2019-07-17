//------------------------------------------------------------------------------
/*
    This file is part of casinocoind: https://github.com/casinocoin/casinocoind
    Copyright (c) 2017 - 2019 CasinoCoin Foundation.

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
    2018-02-03  jrojek        Created
*/
//==============================================================================

#ifndef VOTABLECONFIGURATION_H
#define VOTABLECONFIGURATION_H

#include <casinocoin/app/ledger/Ledger.h>
#include <casinocoin/protocol/STValidation.h>
#include <casinocoin/basics/StringUtilities.h>
#include <casinocoin/protocol/Protocol.h>
#include <casinocoin/json/json_value.h>

#include <boost/assign/list_of.hpp>

namespace casinocoin {

class VotableConfiguration
{
public:
    using ObjectHash = uint256;

    virtual ~VotableConfiguration() = default;

    /** Add local Configuration to validation
        @param lastClosedLedger
    */
    virtual
    std::vector <uint256>
    doValidation (std::shared_ptr<ReadView const> const& lastClosedLedger) = 0;

    /** Cast Configuration conclusion to the ledger
        @param lastClosedLedger
        @param initialPosition
    */
    virtual
    void
    doVoting (std::shared_ptr<ReadView const> const& lastClosedLedger,
        std::vector<STValidation::pointer> const& parentValidations,
        std::shared_ptr<SHAMap> const& initialPosition) = 0;

    /** Update our current voting position with our config file
        @param reports
    */
    virtual
    void
    updatePosition(Json::Value const& jvVotableConfig) = 0;

    // The highest-sequence ledger we have done a VotableConfiguration
    std::atomic <std::uint32_t> mVotableConfigurationLedgerSeq = {0};

};

std::unique_ptr<VotableConfiguration> make_VotableConfig (Application& app, int majorityFraction, Json::Value const& configurationJson, beast::Journal journal);

} // casinocoin

#endif // VOTABLECONFIGURATION_H
