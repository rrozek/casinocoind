//------------------------------------------------------------------------------
/*
    This file is part of casinocoind: https://github.com/casinocoin/casinocoind
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
    2018-05-17  ajochems        Initial version
*/
//==============================================================================

#ifndef CASINOCOIN_APP_MISC_CRNLIST_H_INCLUDED
#define CASINOCOIN_APP_MISC_CRNLIST_H_INCLUDED

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
    Trusted CRN List
    -----------------------
    [Description]

*/

struct CRNListItem 
{
    std::string publicKey;
    std::string domainName;
};

class CRNList
{
    TimeKeeper& timeKeeper_;
    beast::Journal j_;

    // Listed CRN public keys with their registered domain names
    // std::unordered_map<PublicKey, std::string> keyListings_;
    // hash_map<PublicKey, std::string> keyListings_;
    
    std::vector<CRNListItem> crnList_;

public:
    CRNList (
        TimeKeeper& timeKeeper,
        beast::Journal j);
    ~CRNList ();

    /** Load configured trusted keys.

        @param configKeys List of trusted keys from config. Each entry consists
        of a base58 encoded crn public key, followed by the nodes domain name.

        @par Thread Safety

        May be called concurrently

        @return `false` if an entry is invalid or unparsable
    */
    bool
    load (
        std::vector<std::string> const& configKeys);

    /** Update trusted keys

        Reset the trusted keys based on latest list.

        @par Thread Safety

        May be called concurrently
    */
    template<class KeySet>
    void
    onConsensusStart ();

    /** Returns `true` if public key is included on any lists

        @param identity CRN public key

        @par Thread Safety

        May be called concurrently
    */
    bool
    listed (PublicKey const& identity) const;

    void
    refreshNodeOnList (
        std::string const& publicKeyString,
        std::string const& domainName,
        bool const& enabled);

    /** Return JSON representation of configured nodelist
     */
    Json::Value
    getJson() const;
};

//------------------------------------------------------------------------------

} // casinocoin

#endif
