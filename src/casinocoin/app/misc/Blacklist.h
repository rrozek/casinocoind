//------------------------------------------------------------------------------
/*
    This file is part of casinocoind: https://github.com/casinocoin/casinocoind
    Copyright (c) 2019 CasinoCoin Foundation

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
    2019-04-04  ajochems        Initial version
*/
//==============================================================================

#ifndef CASINOCOIN_APP_MISC_BLACKLIST_H_INCLUDED
#define CASINOCOIN_APP_MISC_BLACKLIST_H_INCLUDED

#include <casinocoin/basics/Log.h>
#include <casinocoin/basics/UnorderedContainers.h>
#include <casinocoin/core/TimeKeeper.h>
#include <casinocoin/crypto/csprng.h>
#include <casinocoin/protocol/PublicKey.h>
#include <boost/iterator/counting_iterator.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <mutex>
#include <numeric>

namespace casinocoin {

/**
    Blacklist containing the list with all blocked accounts
    -----------------------
    [Description]

*/

struct BlacklistItem 
{
    std::string accountID;
    std::string signature;
    std::string publicKeySigner;
    std::string creationDate;
    std::string lastUpdatedDate;
    bool enabled;
};

class Blacklist
{
    TimeKeeper& timeKeeper_;
    beast::Journal j_;

    // Blacklisted account ID's     
    std::vector<BlacklistItem> blacklistVector_;

public:
    Blacklist (
        TimeKeeper& timeKeeper,
        beast::Journal j);
    ~Blacklist ();

    /** Returns `true` if AccountID is included on the list

        @param blacklisted account id

        @par Thread Safety

        May be called concurrently
    */
    bool
    listed (std::string const& accountID ) const;

    BlacklistItem
    getAccount (std::string const& accountID ) const;

    void
    refreshAccountOnList (
        std::string const& accountID,
        std::string const& signature,
        std::string const& publicKeySigner,
        std::string const& creationDate,
        std::string const& lastUpdatedDate,
        bool const& enabled);

    /** Return JSON representation of configured blacklist
     */
    Json::Value
    getJson() const;

    /** Return the size of current account list
     * */
    size_t 
    getSize() const;
};

//------------------------------------------------------------------------------

} // casinocoin

#endif
