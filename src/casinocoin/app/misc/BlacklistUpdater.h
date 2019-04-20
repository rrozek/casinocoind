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

#ifndef CASINOCOIN_APP_MISC_BLACKLISTUPDATER_H_INCLUDED
#define CASINOCOIN_APP_MISC_BLACKLISTUPDATER_H_INCLUDED

#include <casinocoin/app/misc/Blacklist.h>
#include <casinocoin/app/misc/detail/Work.h>
#include <casinocoin/basics/Log.h>
#include <casinocoin/basics/StringUtilities.h>
#include <casinocoin/json/json_value.h>
#include <boost/asio.hpp>
#include <mutex>

namespace casinocoin {

/**
    Blacklist Site
    ---------------

    This class manages the set of configured remote sites used to fetch the
    latest published list of blacklisted accounts.

    Lists are fetched at a regular interval.
    Fetched lists are expected to be in JSON format and contain the following
    fields:

    @li @c "accountID": Account ID

    @li @c "signature": Signature created with a registered foundation account id

    @li @c "enabled": Indication if the blacklisting is enabled

*/
class BlacklistUpdater
{
    friend class Work;

private:
    using error_code = boost::system::error_code;
    using clock_type = std::chrono::system_clock;

    struct Site
    {
        struct Status
        {
            clock_type::time_point refreshed;
            bool disposition;
        };
        std::string uri;
        parsedURL pUrl;
        std::chrono::minutes refreshInterval;
        clock_type::time_point nextRefresh;
        boost::optional<Status> lastRefreshStatus;
    };

    boost::asio::io_service& ios_;
    Blacklist& blacklist_;
    beast::Journal j_;
    std::mutex mutable sites_mutex_;
    std::mutex mutable state_mutex_;

    std::condition_variable cv_;
    std::weak_ptr<detail::Work> work_;
    boost::asio::basic_waitable_timer<clock_type> timer_;

    // A list is currently being fetched from a site
    std::atomic<bool> fetching_;

    // One or more lists are due to be fetched
    std::atomic<bool> pending_;
    std::atomic<bool> stopping_;

    // The configured list of URIs for fetching lists
    std::vector<Site> sites_;

public:
    BlacklistUpdater (
        boost::asio::io_service& ios,
        Blacklist& blacklist,
        beast::Journal j);
    ~BlacklistUpdater ();

    /** Load configured site URIs.

        @param siteURIs List of URIs to fetch published blacklists

        @par Thread Safety

        May be called concurrently

        @return `false` if an entry is invalid or unparsable
    */
    bool
    load (
        std::vector<std::string> const& siteURIs);

    /** Start fetching lists from sites

        This does nothing if list fetching has already started

        @par Thread Safety

        May be called concurrently
    */
    void
    start ();

    /** Wait for current fetches from sites to complete

        @par Thread Safety

        May be called concurrently
    */
    void
    join ();

    /** Stop fetching lists from sites

        This blocks until list fetching has stopped

        @par Thread Safety

        May be called concurrently
    */
    void
    stop ();

    /** Return JSON representation of configured blacklist sites
     */
    Json::Value
    getJson() const;

private:
    /// Queue next site to be fetched
    void
    setTimer ();

    /// Fetch site whose time has come
    void
    onTimer (
        std::size_t siteIdx,
        error_code const& ec);

    /// Store latest list fetched from site
    void
    onSiteFetch (
        boost::system::error_code const& ec,
        detail::response_type&& res,
        std::size_t siteIdx);
};

} // casinocoin

#endif
