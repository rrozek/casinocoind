//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2016 Ripple Labs Inc.

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

#include <casinocoin/app/misc/ValidatorSite.h>
#include <casinocoin/basics/base64.h>
#include <casinocoin/basics/Slice.h>
#include <casinocoin/basics/strHex.h>
#include <casinocoin/protocol/digest.h>
#include <casinocoin/protocol/HashPrefix.h>
#include <casinocoin/protocol/JsonFields.h>
#include <casinocoin/protocol/PublicKey.h>
#include <casinocoin/protocol/SecretKey.h>
#include <casinocoin/protocol/Sign.h>
#include <test/jtx.h>
#include <test/jtx/TrustedPublisherServer.h>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <chrono>
namespace casinocoin {
namespace test {
auto constexpr default_expires = std::chrono::seconds{3600};

class ValidatorSite_test : public beast::unit_test::suite
{
private:

    using Validator = TrustedPublisherServer::Validator;

    static
    PublicKey
    randomNode ()
    {
        return derivePublicKey (KeyType::secp256k1, randomSecretKey());
    }

    static
    std::string
    makeManifestString (
        PublicKey const& pk,
        SecretKey const& sk,
        PublicKey const& spk,
        SecretKey const& ssk,
        int seq)
    {
        STObject st(sfGeneric);
        st[sfSequence] = seq;
        st[sfPublicKey] = pk;
        st[sfSigningPubKey] = spk;

        sign(st, HashPrefix::manifest, *publicKeyType(spk), ssk);
        sign(st, HashPrefix::manifest, *publicKeyType(pk), sk,
            sfMasterSignature);

        Serializer s;
        st.add(s);

        return base64_encode (std::string(
            static_cast<char const*> (s.data()), s.size()));
    }

    static
    Validator
    randomValidator ()
    {
        auto const secret = randomSecretKey();
        auto const masterPublic =
            derivePublicKey(KeyType::ed25519, secret);
        auto const signingKeys = randomKeyPair(KeyType::secp256k1);
        return { masterPublic, signingKeys.first, makeManifestString (
            masterPublic, secret, signingKeys.first, signingKeys.second, 1) };
    }

    void
    testConfigLoad ()
    {
        testcase ("Config Load");

        using namespace jtx;

        Env env (*this);
        auto trustedSites = std::make_unique<ValidatorSite> (
            env.app().getIOService(), env.app().validators(), env.journal);

        // load should accept empty sites list
        std::vector<std::string> emptyCfgSites;
        BEAST_EXPECT(trustedSites->load (emptyCfgSites));

        // load should accept valid validator site uris
        std::vector<std::string> cfgSites({
            "http://ripple.com/",
            "http://ripple.com/validators",
            "http://ripple.com:8080/validators",
            "http://207.261.33.37/validators",
            "http://207.261.33.37:8080/validators",
            "https://ripple.com/validators",
            "https://ripple.com:443/validators"});
        BEAST_EXPECT(trustedSites->load (cfgSites));

        // load should reject validator site uris with invalid schemes
        std::vector<std::string> badSites(
            {"ftp://ripple.com/validators"});
        BEAST_EXPECT(!trustedSites->load (badSites));

        badSites[0] = "wss://ripple.com/validators";
        BEAST_EXPECT(!trustedSites->load (badSites));

        badSites[0] = "ripple.com/validators";
        BEAST_EXPECT(!trustedSites->load (badSites));
    }

    class TestSink : public beast::Journal::Sink
    {
    public:
        std::stringstream strm_;

        TestSink () : Sink (beast::severities::kDebug, false) {  }

        void
        write (beast::severities::Severity level,
            std::string const& text) override
        {
            if (level < threshold())
                return;

            strm_ << text << std::endl;
        }
    };

    struct FetchListConfig
    {
        std::string path;
        std::string msg;
        bool failFetch = false;
        bool failApply = false;
        int serverVersion = 1;
        std::chrono::seconds expiresFromNow = default_expires;
        int expectedRefreshMin = 0;
    };
    void
    testFetchList (std::vector<FetchListConfig> const& paths)
    {
        testcase << "Fetch list - " <<
            boost::algorithm::join (paths |
                boost::adaptors::transformed(
                    [](FetchListConfig const& cfg){ return cfg.path; }),
                ", ");
        using namespace jtx;

        Env env (*this);
        auto& trustedKeys = env.app ().validators ();

        TestSink sink;
        beast::Journal journal{sink};

        PublicKey emptyLocalKey;
        std::vector<std::string> emptyCfgKeys;
        struct publisher
        {
            publisher(FetchListConfig const& c) : cfg{c} {}
            std::unique_ptr<TrustedPublisherServer> server;
            std::vector<Validator> list;
            std::string uri;
            FetchListConfig const& cfg;
            bool isRetry;
        };
        std::vector<publisher> servers;

        auto const sequence = 1;
        auto constexpr listSize = 20;
        std::vector<std::string> cfgPublishers;

        for (auto const& cfg : paths)
        {
            auto const publisherSecret = randomSecretKey();
            auto const publisherPublic =
                derivePublicKey(KeyType::ed25519, publisherSecret);
            auto const pubSigningKeys = randomKeyPair(KeyType::secp256k1);
            cfgPublishers.push_back(strHex(publisherPublic));

            auto const manifest = makeManifestString (
                publisherPublic, publisherSecret,
                pubSigningKeys.first, pubSigningKeys.second, 1);

            servers.push_back(cfg);
            auto& item = servers.back();
            item.isRetry = cfg.path == "/bad-resource";
            item.list.reserve (listSize);
            while (item.list.size () < listSize)
                item.list.push_back (randomValidator());

            item.server = std::make_unique<TrustedPublisherServer> (
                env.app().getIOService(),
                pubSigningKeys,
                manifest,
                sequence,
                env.timeKeeper().now() + cfg.expiresFromNow,
                cfg.serverVersion,
                item.list);

            std::stringstream uri;
            uri << "http://" << item.server->local_endpoint() << cfg.path;
            item.uri = uri.str();
        }

        BEAST_EXPECT(trustedKeys.load (
            emptyLocalKey, emptyCfgKeys, cfgPublishers));

        using namespace std::chrono_literals;
        auto sites = std::make_unique<ValidatorSite> (
            env.app().getIOService(),
            env.app().validators(),
            journal,
            2s);

        std::vector<std::string> uris;
        for (auto const& u : servers)
            uris.push_back(u.uri);
        sites->load (uris);
        sites->start();
        sites->join();

        auto const jv = sites->getJson();
        for (auto const& u : servers)
        {
            for (auto const& val : u.list)
            {
                BEAST_EXPECT(
                    trustedKeys.listed (val.masterPublic) != u.cfg.failApply);
                BEAST_EXPECT(
                    trustedKeys.listed (val.signingPublic) != u.cfg.failApply);
            }

            Json::Value myStatus;
            for (auto const& vs : jv[jss::validator_sites])
                if (vs[jss::uri].asString().find(u.uri) != std::string::npos)
                    myStatus = vs;
            BEAST_EXPECTS(
                myStatus[jss::last_refresh_message].asString().empty()
                    != u.cfg.failFetch,
                to_string(myStatus) + "\n" + sink.strm_.str());

            if (! u.cfg.msg.empty())
            {
                BEAST_EXPECTS(
                    sink.strm_.str().find(u.cfg.msg) != std::string::npos,
                    sink.strm_.str());
            }


            if (u.cfg.expectedRefreshMin)
            {
                BEAST_EXPECTS(
                    myStatus[jss::refresh_interval_min].asInt()
                        == u.cfg.expectedRefreshMin,
                    to_string(myStatus));
            }

            if (u.cfg.failFetch)
            {
                using namespace std::chrono;
                log << " -- Msg: " <<
                    myStatus[jss::last_refresh_message].asString() << std::endl;
                std::stringstream nextRefreshStr
                    {myStatus[jss::next_refresh_time].asString()};
                system_clock::time_point nextRefresh;
                date::from_stream (nextRefreshStr, "%Y-%b-%d %T", nextRefresh);
                BEAST_EXPECT(!nextRefreshStr.fail());
                auto now = system_clock::now();
                BEAST_EXPECTS(
                    nextRefresh <= now + (u.isRetry ? seconds{30} : minutes{5}),
                    "Now: " + to_string(now) + ", NR: " + nextRefreshStr.str());
            }
        }
    }

public:
    void
    run() override
    {
        testConfigLoad ();

        // fetch single site
        testFetchList ({{"/validators", ""}});
        // fetch multiple sites
        testFetchList ({{"/validators", ""}, {"/validators", ""}});
        // fetch single site with single redirects
        testFetchList ({{"/redirect_once/301", ""}});
        testFetchList ({{"/redirect_once/302", ""}});
        testFetchList ({{"/redirect_once/307", ""}});
        testFetchList ({{"/redirect_once/308", ""}});
        // one redirect, one not
        testFetchList ({{"/validators", ""}, {"/redirect_once/302", ""}});
        // fetch single site with undending redirect (fails to load)
        testFetchList ({
            {"/redirect_forever/301", "Exceeded max redirects", true, true}});
        // two that redirect forever
        testFetchList ({
            {"/redirect_forever/307", "Exceeded max redirects", true, true},
            {"/redirect_forever/308", "Exceeded max redirects", true, true}});
        // one undending redirect, one not
        testFetchList (
            {{"/validators", ""},
            {"/redirect_forever/302", "Exceeded max redirects", true, true}});
        // invalid redir Location
        testFetchList ({
            {"/redirect_to/ftp://invalid-url/302",
             "Invalid redirect location",
             true,
             true}});
        testFetchList ({
            {"/redirect_to/file://invalid-url/302",
             "Invalid redirect location",
             true,
             true}});
        // invalid json
        testFetchList ({
            {"/validators/bad", "Unable to parse JSON response", true, true}});
        // error status returned
        testFetchList ({
            {"/bad-resource", "returned bad status", true, true}});
        // location field missing
        testFetchList ({
            {"/redirect_nolo/308",
            "returned a redirect with no Location",
            true,
            true}});
        // json fields missing
        testFetchList ({
            {"/validators/missing",
            "Missing fields in JSON response",
            true,
            true}});
        // timeout
        testFetchList ({
            {"/sleep/3", "took too long", true, true}});
        // bad manifest version
        testFetchList ({
            {"/validators", "Unsupported version", false, true, 4}});
        using namespace std::chrono_literals;
        // get old validator list
        testFetchList ({
            {"/validators", "Stale validator list", false, true, 1, 0s}});
        // force an out-of-range expiration value
        testFetchList ({
            {"/validators",
            "Invalid validator list",
            false,
            true,
            1,
            std::chrono::seconds{Json::Value::maxInt + 1}}});
        // verify refresh intervals are properly clamped
        testFetchList ({
            {"/validators/refresh/0",
            "",
            false,
            false,
            1,
            default_expires,
            1}}); // minimum of 1 minute
        testFetchList ({
            {"/validators/refresh/10",
            "",
            false,
            false,
            1,
            default_expires,
            10}}); // 10 minutes is fine
        testFetchList ({
            {"/validators/refresh/2000",
            "",
            false,
            false,
            1,
           	default_expires,
            60*24}}); // max of 24 hours
        
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorSite, app, casinocoin);

} // test
} // casinocoin

