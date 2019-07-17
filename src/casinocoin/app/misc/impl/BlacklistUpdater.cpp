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

#include <casinocoin/app/misc/detail/WorkPlain.h>
#include <casinocoin/app/misc/detail/WorkSSL.h>
#include <casinocoin/app/misc/Blacklist.h>
#include <casinocoin/app/misc/BlacklistUpdater.h>
#include <casinocoin/basics/Slice.h>
#include <casinocoin/json/json_writer.h>
#include <casinocoin/json/json_reader.h>
#include <beast/core/detail/base64.hpp>
#include <boost/regex.hpp>

namespace casinocoin {

// default site query frequency - 5 minutes
auto constexpr DEFAULT_BLACKLIST_REFRESH_INTERVAL = std::chrono::minutes{5};

BlacklistUpdater::BlacklistUpdater (
    boost::asio::io_service& ios,
    Blacklist& blacklist,
    beast::Journal j)
    : ios_ (ios)
    , blacklist_ (blacklist)
    , j_ (j)
    , timer_ (ios_)
    , fetching_ (false)
    , pending_ (false)
    , stopping_ (false)
{
}

BlacklistUpdater::~BlacklistUpdater()
{
    std::unique_lock<std::mutex> lock{state_mutex_};
    if (timer_.expires_at() > clock_type::time_point{})
    {
        if (! stopping_)
        {
            lock.unlock();
            stop();
        }
        else
        {
            cv_.wait(lock, [&]{ return ! fetching_; });
        }
    }
}

bool
BlacklistUpdater::load (std::vector<std::string> const& siteURIs)
{
    JLOG (j_.info()) << "Loading configured blacklist sites";

    std::lock_guard <std::mutex> lock{sites_mutex_};

    for (auto uri : siteURIs)
    {
        parsedURL pUrl;
        if (! parseUrl (pUrl, uri) ||
            (pUrl.scheme != "http" && pUrl.scheme != "https"))
        {
            JLOG (j_.error()) << "Invalid blacklist site uri: " << uri;
            return false;
        }

        if (! pUrl.port)
            pUrl.port = (pUrl.scheme == "https") ? 443 : 80;
        // add site to array and set first refresh in 1 minute from now
        sites_.push_back ({uri, pUrl, DEFAULT_BLACKLIST_REFRESH_INTERVAL, clock_type::now()+std::chrono::minutes{1}});
    }

    JLOG (j_.info()) << "Loaded " << siteURIs.size() << " sites";

    return true;
}

void
BlacklistUpdater::start ()
{
    JLOG (j_.debug()) << "BlacklistUpdater::start ()";
    std::lock_guard <std::mutex> lock{state_mutex_};
    if (timer_.expires_at() == clock_type::time_point{})
        setTimer ();
}

void
BlacklistUpdater::join ()
{
    JLOG (j_.debug()) << "BlacklistUpdater::join ()";
    std::unique_lock<std::mutex> lock{state_mutex_};
    cv_.wait(lock, [&]{ return ! pending_; });
}

void
BlacklistUpdater::stop()
{
    JLOG (j_.debug()) << "BlacklistUpdater::stop ()";
    std::unique_lock<std::mutex> lock{state_mutex_};
    stopping_ = true;
    cv_.wait(lock, [&]{ return ! fetching_; });

    if(auto sp = work_.lock())
        sp->cancel();

    error_code ec;
    timer_.cancel(ec);
    stopping_ = false;
    pending_ = false;
    cv_.notify_all();
}

void
BlacklistUpdater::setTimer ()
{
    std::lock_guard <std::mutex> lock{sites_mutex_};
    auto next = sites_.end();

    for (auto it = sites_.begin (); it != sites_.end (); ++it)
    {
        JLOG (j_.debug()) << "Set Timer to Update from URI: " << it->uri;
        if (next == sites_.end () || it->nextRefresh < next->nextRefresh)
            next = it;
    }

    if (next != sites_.end ())
    {
        pending_ = next->nextRefresh <= clock_type::now();
        cv_.notify_all();

        timer_.expires_at (next->nextRefresh);
        timer_.async_wait (std::bind (&BlacklistUpdater::onTimer, this,
            std::distance (sites_.begin (), next),
                std::placeholders::_1));
    }
}

void
BlacklistUpdater::onTimer (
    std::size_t siteIdx,
    error_code const& ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
    {
        JLOG(j_.error()) << "BlacklistUpdater::onTimer: " << ec.message();
        return;
    }

    std::lock_guard <std::mutex> lock{sites_mutex_};
    sites_[siteIdx].nextRefresh = clock_type::now() + DEFAULT_BLACKLIST_REFRESH_INTERVAL;

    assert(! fetching_);
    fetching_ = true;

    JLOG(j_.debug()) << "BlacklistUpdater::onTimer uri: " << sites_[siteIdx].uri;
    std::shared_ptr<detail::Work> sp;
    if (sites_[siteIdx].pUrl.scheme == "https")
    {
        sp = std::make_shared<detail::WorkSSL>(
            sites_[siteIdx].pUrl.domain,
            sites_[siteIdx].pUrl.path,
            std::to_string(*sites_[siteIdx].pUrl.port),
            ios_,
            j_,
            [this, siteIdx](error_code const& err, detail::response_type&& resp)
            {
                onSiteFetch (err, std::move(resp), siteIdx);
            });
    }
    else
    {
        sp = std::make_shared<detail::WorkPlain>(
            sites_[siteIdx].pUrl.domain,
            sites_[siteIdx].pUrl.path,
            std::to_string(*sites_[siteIdx].pUrl.port),
            ios_,
            [this, siteIdx](error_code const& err, detail::response_type&& resp)
            {
                onSiteFetch (err, std::move(resp), siteIdx);
            });
    }

    work_ = sp;
    sp->run ();
}

void
BlacklistUpdater::onSiteFetch(
    boost::system::error_code const& ec,
    detail::response_type&& res,
    std::size_t siteIdx)
{
    if (! ec && res.result() != beast::http::status::ok)
    {
        std::lock_guard <std::mutex> lock{sites_mutex_};
        JLOG (j_.warn()) << "Request for blacklist at " <<
            sites_[siteIdx].uri << " returned " << res.result();
    }
    else if (! ec)
    {
        std::lock_guard <std::mutex> lock{sites_mutex_};
        Json::Reader r;
        Json::Value body;
        if (r.parse(res.body.data(), body))
        {
            // add all defined accounts to the blacklist
            JLOG(j_.debug()) << "Site endpoint blacklisted accounts: " << body.size();
            for (Json::Value::ArrayIndex i = 0; i != body.size(); i++)
            {
                Json::FastWriter fastWriter;
                std::string jsonOutput = fastWriter.write(body[i]);
                JLOG(j_.debug()) << "Refresh: " << jsonOutput;
                if ( body[i].isMember("accountID") && 
                     body[i].isMember("signature") && 
                     body[i].isMember("signerPublicKey") &&
                     body[i].isMember("creationDate") && 
                     body[i].isMember("lastUpdateDate") && 
                     body[i].isMember("enabled"))
                {
                    blacklist_.refreshAccountOnList ( 
                        body[i]["accountID"].asString(), 
                        body[i]["signature"].asString(),
                        body[i]["signerPublicKey"].asString(),
                        body[i]["creationDate"].asString(),
                        body[i]["lastUpdateDate"].asString(),
                        body[i]["enabled"].asBool()
                    );
                }
                else
                {
                    JLOG(j_.warn()) << "Site: " << sites_[siteIdx].uri << " does not return all required attributes.";
                    break;
                }
            }
            // set last refresh status
            sites_[siteIdx].lastRefreshStatus.emplace(Site::Status{clock_type::now(), true});
        }
        else
        {
            JLOG (j_.warn()) <<
                "Unable to parse JSON response from  " <<
                sites_[siteIdx].uri;
            // set last refresh status
            sites_[siteIdx].lastRefreshStatus.emplace(Site::Status{clock_type::now(), false});
        }
    }

    JLOG (j_.info()) << "Blacklisted AccountID List size: " << blacklist_.getSize();
    std::lock_guard <std::mutex> lock{state_mutex_};
    fetching_ = false;
    if (! stopping_)
        setTimer ();
    cv_.notify_all();
}

Json::Value
BlacklistUpdater::getJson() const
{
    using namespace std::chrono;
    using Int = Json::Value::Int;

    Json::Value jrr(Json::arrayValue);
    {
        std::lock_guard<std::mutex> lock{sites_mutex_};
        for (Site const& site : sites_)
        {
            Json::Value& v = jrr.append(Json::objectValue);
            v[jss::url] = site.uri;
            if (site.lastRefreshStatus)
            {
                v[jss::last_refresh_time] =
                    to_string(site.lastRefreshStatus->refreshed);
                v[jss::last_refresh_status] =
                    to_string(site.lastRefreshStatus->disposition);
            }

            v[jss::refresh_interval_min] =
                static_cast<Int>(site.refreshInterval.count());
        }
    }
    return jrr;
}

} // casinocoin
