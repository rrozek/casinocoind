//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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

 
#include <casinocoin/protocol/JsonFields.h>
#include <test/jtx.h>
#include <casinocoin/beast/unit_test.h>

#include <boost/format.hpp>

namespace casinocoin {

namespace test {

namespace validator_data {
static auto const public_key =
    "nHD3pRojfDnPNyzMXsaj5hJkVRSTzG6RG5XJPq1EercmGAEyAe8Y";

static auto const token =
        "eyJtYW5pZmVzdCI6IkpBQUFBQUZ4SWUzbzhEeGhCMjhsejdOa0xCMWY0UkVEbHNWTlVESUpk\n"
        "SWFzbjkwaGRTVCtFbk1oQTN2dEpxbmtDdHM4cUgxZnpSRjdZUzVMTEtpYWpTMzBMdWJPQVlx\n"
        "UVYvY0Nka2N3UlFJaEFLSStDc2c4ZENYcGJ4T1o2WnFndndpRVBFeHd6YlR2c1MyK0I2WGdF\n"
        "V1FxQWlBcE83Rm1kNGwzNGxzNmw2TEZ1SHJ6VDc1K2pKTm04QXV1Skk3SUh5RXNoWEFTUUo3\n"
        "YVI2c2FjWEQ5bXNqVS93NmtiWVFnR1c4ZlIrSnF2b2JmakxPUXBDbkJEcGxTVUloM2pKSHBn\n"
        "dldYQTNnREUzbjNJTzZOYnhjTXJ3K0JqVHA0amdjPSIsInZhbGlkYXRpb25fc2VjcmV0X2tl\n"
        "eSI6IkFGRDlDOTJCMzFDQzk1NkQ1Q0QzQUYwN0UxMUZBRDFBRDEzNzQwRTk5MDkyQUZDNjQ4\n"
        "MDlENzEzRkU5Q0M2ODgifQ==\n";
}

class ServerInfo_test : public beast::unit_test::suite
{
public:
    static
    std::unique_ptr<Config>
    makeValidatorConfig()
    {
        auto p = std::make_unique<Config>();
        boost::format toLoad(R"rippleConfig(
[validator_token]
%1%

[validators]
%2%
)rippleConfig");

        p->loadFromString (boost::str (
            toLoad % validator_data::token % validator_data::public_key));

        setupConfigForUnitTests(*p);

        return p;
    }

    void testServerInfo()
    {
        using namespace test::jtx;

        {
            Env env(*this);
            auto const result = env.rpc("server_info");
            BEAST_EXPECT(!result[jss::result].isMember (jss::error));
            BEAST_EXPECT(result[jss::result][jss::status] == "success");
            BEAST_EXPECT(result[jss::result].isMember(jss::info));
        }
        {
            Env env(*this, makeValidatorConfig());
            auto const result = env.rpc("server_info");
            BEAST_EXPECT(!result[jss::result].isMember (jss::error));
            BEAST_EXPECT(result[jss::result][jss::status] == "success");
            BEAST_EXPECT(result[jss::result].isMember(jss::info));
            BEAST_EXPECT(result[jss::result][jss::info]
                [jss::pubkey_validator] == validator_data::public_key);
        }
    }

    void run () override
    {
        testServerInfo ();
    }
};

BEAST_DEFINE_TESTSUITE(ServerInfo,app,casinocoin);

} // test
} // casinocoin

