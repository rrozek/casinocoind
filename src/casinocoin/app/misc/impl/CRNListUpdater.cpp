//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <casinocoin/app/misc/detail/WorkPlain.h>
#include <casinocoin/app/misc/detail/WorkSSL.h>
#include <casinocoin/app/misc/CRNList.h>
#include <casinocoin/app/misc/CRNListUpdater.h>
#include <casinocoin/basics/Slice.h>
#include <casinocoin/json/json_reader.h>
#include <beast/core/detail/base64.hpp>
#include <boost/regex.hpp>

namespace casinocoin {

// default site query frequency - 30 minutes
auto constexpr DEFAULT_CRN_REFRESH_INTERVAL = std::chrono::minutes{30};

CRNListUpdater::CRNListUpdater (
    boost::asio::io_service& ios,
    CRNList& crnlist,
    beast::Journal j)
    : ios_ (ios)
    , crnlist_ (crnlist)
    , j_ (j)
    , timer_ (ios_)
    , fetching_ (false)
    , pending_ (false)
    , stopping_ (false)
{
}

CRNListUpdater::~CRNListUpdater()
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
CRNListUpdater::load (std::vector<std::string> const& siteURIs)
{
    JLOG (j_.info()) << "Loading configured relaynodes list sites";

    std::lock_guard <std::mutex> lock{sites_mutex_};

    for (auto uri : siteURIs)
    {
        parsedURL pUrl;
        if (! parseUrl (pUrl, uri) ||
            (pUrl.scheme != "http" && pUrl.scheme != "https"))
        {
            JLOG (j_.error()) << "Invalid relaynode site uri: " << uri;
            return false;
        }

        if (! pUrl.port)
            pUrl.port = (pUrl.scheme == "https") ? 443 : 80;
        // add site to array and set first refresh in 1 minute from now
        sites_.push_back ({uri, pUrl, DEFAULT_CRN_REFRESH_INTERVAL, clock_type::now()+std::chrono::minutes{1}});
    }

    JLOG (j_.info()) << "Loaded " << siteURIs.size() << " sites";

    return true;
}

void
CRNListUpdater::start ()
{
    JLOG (j_.debug()) << "CRNListUpdater::start ()";
    std::lock_guard <std::mutex> lock{state_mutex_};
    if (timer_.expires_at() == clock_type::time_point{})
        setTimer ();
}

void
CRNListUpdater::join ()
{
    JLOG (j_.debug()) << "CRNListUpdater::join ()";
    std::unique_lock<std::mutex> lock{state_mutex_};
    cv_.wait(lock, [&]{ return ! pending_; });
}

void
CRNListUpdater::stop()
{
    JLOG (j_.debug()) << "CRNListUpdater::stop ()";
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
CRNListUpdater::setTimer ()
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
        timer_.async_wait (std::bind (&CRNListUpdater::onTimer, this,
            std::distance (sites_.begin (), next),
                std::placeholders::_1));
    }
}

void
CRNListUpdater::onTimer (
    std::size_t siteIdx,
    error_code const& ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
    {
        JLOG(j_.error()) << "CRNListUpdater::onTimer: " << ec.message();
        return;
    }

    std::lock_guard <std::mutex> lock{sites_mutex_};
    sites_[siteIdx].nextRefresh = clock_type::now() + DEFAULT_CRN_REFRESH_INTERVAL;

    assert(! fetching_);
    fetching_ = true;

    JLOG(j_.debug()) << "CRNListUpdater::onTimer uri: " << sites_[siteIdx].uri;
    std::shared_ptr<detail::Work> sp;
    if (sites_[siteIdx].pUrl.scheme == "https")
    {
        sp = std::make_shared<detail::WorkSSL>(
            sites_[siteIdx].pUrl.domain,
            sites_[siteIdx].pUrl.path,
            std::to_string(*sites_[siteIdx].pUrl.port),
            ios_,
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
CRNListUpdater::onSiteFetch(
    boost::system::error_code const& ec,
    detail::response_type&& res,
    std::size_t siteIdx)
{
    if (! ec && res.status != 200)
    {
        std::lock_guard <std::mutex> lock{sites_mutex_};
        JLOG (j_.warn()) << "Request for relaynode list at " <<
            sites_[siteIdx].uri << " returned " << res.status;
    }
    else if (! ec)
    {
        std::lock_guard <std::mutex> lock{sites_mutex_};
        Json::Reader r;
        Json::Value body;
        if (r.parse(res.body.data(), body))
        {
            // add all defined nodes to the crn list
            for (Json::Value::ArrayIndex i = 0; i != body.size(); i++)
            {
                Json::FastWriter fastWriter;
                std::string jsonOutput = fastWriter.write(body[i]);
                JLOG(j_.debug()) << "Refresh: " << jsonOutput;
                if (body[i].isMember("publicKey") && body[i].isMember("serverName") && body[i].isMember("enabled"))
                {
                    crnlist_.refreshNodeOnList ( body[i]["publicKey"].asString(), body[i]["serverName"].asString(),body[i]["enabled"].asBool() );
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
        JLOG (j_.info()) << "CRN Public Keys List size: " << crnlist_.size();
    }

    std::lock_guard <std::mutex> lock{state_mutex_};
    fetching_ = false;
    if (! stopping_)
        setTimer ();
    cv_.notify_all();
}

Json::Value
CRNListUpdater::getJson() const
{
    using namespace std::chrono;
    using Int = Json::Value::Int;

    Json::Value jrr(Json::arrayValue);
    {
        std::lock_guard<std::mutex> lock{sites_mutex_};
        for (Site const& site : sites_)
        {
            Json::Value& v = jrr.append(Json::objectValue);
            v[jss::uri] = site.uri;
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
