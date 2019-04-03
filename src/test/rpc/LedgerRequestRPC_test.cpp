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

#include <BeastConfig.h>
#include <casinocoin/protocol/ErrorCodes.h>
#include <casinocoin/protocol/JsonFields.h>
#include <test/jtx.h>
#include <casinocoin/beast/unit_test.h>
#include <casinocoin/app/ledger/LedgerMaster.h>

namespace casinocoin {

namespace RPC {

class LedgerRequestRPC_test : public beast::unit_test::suite
{
public:

    void testLedgerRequest()
    {
        using namespace test::jtx;

        Env env(*this);

        env.close();
        env.close();
        BEAST_EXPECT(env.current()->info().seq == 5);

        {
            // arbitrary text is converted to 0.
            auto const result = env.rpc("ledger_request", "arbitrary_text");
            BEAST_EXPECT(RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                "Ledger index too small");
        }

        {
            auto const result = env.rpc("ledger_request", "-1");
            BEAST_EXPECT(RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                "Ledger index too small");
        }

        {
            auto const result = env.rpc("ledger_request", "0");
            BEAST_EXPECT(RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too small");
        }

        {
            auto const result = env.rpc("ledger_request", "1");
            BEAST_EXPECT(!RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::ledger_index] == 1 &&
                    result[jss::result].isMember(jss::ledger));
            BEAST_EXPECT(result[jss::result][jss::ledger].
                isMember(jss::ledger_hash) &&
                    result[jss::result][jss::ledger]
                        [jss::ledger_hash].isString());
        }

        {
            auto const result = env.rpc("ledger_request", "2");
            BEAST_EXPECT(!RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::ledger_index] == 2 &&
                    result[jss::result].isMember(jss::ledger));
            BEAST_EXPECT(result[jss::result][jss::ledger].
                isMember(jss::ledger_hash) &&
                    result[jss::result][jss::ledger]
                        [jss::ledger_hash].isString());
        }

        {
            auto const result = env.rpc("ledger_request", "3");
            BEAST_EXPECT(!RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::ledger_index] == 3 &&
                    result[jss::result].isMember(jss::ledger));
            BEAST_EXPECT(result[jss::result][jss::ledger].
                isMember(jss::ledger_hash) &&
                    result[jss::result][jss::ledger]
                        [jss::ledger_hash].isString());

            auto const ledgerHash = result[jss::result]
                [jss::ledger][jss::ledger_hash].asString();

            {
                // Intentionally shadow `result` here to avoid reuing it.
                auto const result = env.rpc("ledger_request", ledgerHash);
                BEAST_EXPECT(!RPC::contains_error(result[jss::result]) &&
                    result[jss::result][jss::ledger_index] == 3 &&
                        result[jss::result].isMember(jss::ledger));
                BEAST_EXPECT(result[jss::result][jss::ledger].
                    isMember(jss::ledger_hash) &&
                        result[jss::result][jss::ledger]
                            [jss::ledger_hash] == ledgerHash);
            }
        }

        {
            std::string ledgerHash(64, 'q');

            auto const result = env.rpc("ledger_request", ledgerHash);

            BEAST_EXPECT(RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Invalid field 'ledger_hash'.");
        }

        {
            std::string ledgerHash(64, '1');

            auto const result = env.rpc("ledger_request", ledgerHash);

            BEAST_EXPECT(!RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::have_header] == false);
        }

        {
            auto const result = env.rpc("ledger_request", "4");
            BEAST_EXPECT(RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too large");
        }

        {
            auto const result = env.rpc("ledger_request", "5");
            BEAST_EXPECT(RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too large");
        }

    }

    void testEvolution()
    {
        using namespace test::jtx;
        Env env { *this };
        Account const gw { "gateway" };
        auto const USD = gw["USD"];
        env.fund(CSC(100000), gw);
        env.close();

        env.memoize("bob");
        env.fund(CSC(1000), "bob");
        env.close();

        env.memoize("alice");
        env.fund(CSC(1000), "alice");
        env.close();

        env.memoize("carol");
        env.fund(CSC(1000), "carol");
        env.close();

        auto result = env.rpc ( "ledger_request", "1" ) [jss::result];

        BEAST_EXPECT(result[jss::ledger][jss::ledger_index]     == "1");
        BEAST_EXPECT(result[jss::ledger][jss::total_coins]      == "4000000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed]           == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash]      == "C7AAD8A09ADA998B963A56ADAD4CC35F501CD183DD4F19087C5A466D55676FF4");
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash]      == "0000000000000000000000000000000000000000000000000000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::account_hash]     == "D8939DBDA628BC984D65B08CDA1DCC351A06B53204A4E4F62D0BBB796FAA146E");
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == "0000000000000000000000000000000000000000000000000000000000000000");

        result = env.rpc ( "ledger_request", "2" ) [jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index]     == "2");
        BEAST_EXPECT(result[jss::ledger][jss::total_coins]      == "4000000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed]           == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash]      == "4BE3C23DC8B17F0AF2385F7C9C49772390012AA97E3743C705DA86614DE12DDC");
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash]      == "C7AAD8A09ADA998B963A56ADAD4CC35F501CD183DD4F19087C5A466D55676FF4");
        BEAST_EXPECT(result[jss::ledger][jss::account_hash]     == "C6F8AE44160CCC21A233080DC4BBA63EA640AE758B09F06CCD9637297EC30AB1");
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == "0000000000000000000000000000000000000000000000000000000000000000");

        result = env.rpc ( "ledger_request", "3" ) [jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index]     == "3");
        BEAST_EXPECT(result[jss::ledger][jss::total_coins]      == "3999999999998000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed]           == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash]      == "E7054DA21007FC2C02F57CAE1821C3E30169884F2745380D4AA50E2BB1E7631D");
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash]      == "4BE3C23DC8B17F0AF2385F7C9C49772390012AA97E3743C705DA86614DE12DDC");
        BEAST_EXPECT(result[jss::ledger][jss::account_hash]     == "B5681057FE6D20FEF088B0D1F09AF94441F8E35A2F3C762FC2D2364859D27A4D");
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == "8CC09BA8E76A55D651C454A758954E58EFE82BE6C454A496C4CF89E669275158");

        result = env.rpc ( "ledger_request", "4" ) [jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index]     == "4");
        BEAST_EXPECT(result[jss::ledger][jss::total_coins]      == "3999999999996000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed]           == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash]      == "2F48424AEAF3CD2102E66877E056AF7221EF76EE6FD4C6ABB0A2284771B02094");
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash]      == "E7054DA21007FC2C02F57CAE1821C3E30169884F2745380D4AA50E2BB1E7631D");
        BEAST_EXPECT(result[jss::ledger][jss::account_hash]     == "213516122552D4D51E2EB0D408768A6D19C8240EE9BBAC277D53D1409C8525AD");
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == "4F3ED0AE9BAB74F27A8E663460AF5014C9876474C7E33A98EED4581D810A809F");

        result = env.rpc ( "ledger_request", "5" ) [jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index]     == "5");
        BEAST_EXPECT(result[jss::ledger][jss::total_coins]      == "3999999999994000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed]           == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash]      == "5EDBE8FB65A3CF570C8F132B54E8B6A82559DF0097238BF4768DCF39F4565F09");
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash]      == "2F48424AEAF3CD2102E66877E056AF7221EF76EE6FD4C6ABB0A2284771B02094");
        BEAST_EXPECT(result[jss::ledger][jss::account_hash]     == "88BD3CB08056ADE3C29FD076D2D6C82D1065FC6B3CBE97CAC063B0179FC1964D");
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == "994EE05311F1B81B1585FF8F63FEA2FC56251FD105C736726C95D58A3ED2E2D2");

        result = env.rpc ( "ledger_request", "6" ) [jss::result];
        BEAST_EXPECT(result[jss::error]         == "invalidParams");
        BEAST_EXPECT(result[jss::status]        == "error");
        BEAST_EXPECT(result[jss::error_message] == "Ledger index too large");
    }

    void testBadInput()
    {
        using namespace test::jtx;
        Env env { *this };
        Account const gw { "gateway" };
        auto const USD = gw["USD"];
        env.fund(CSC(100000), gw);
        env.close();

        Json::Value jvParams;
        jvParams[jss::ledger_hash] = "AB868A6CFEEC779C2FF845C0AF00A642259986AF40C01976A7F842B6918936C7";
        jvParams[jss::ledger_index] = "1";
        auto result = env.rpc ("json", "ledger_request", jvParams.toStyledString()) [jss::result];

        BEAST_EXPECT(result[jss::error]         == "invalidParams");
        BEAST_EXPECT(result[jss::status]        == "error");
        BEAST_EXPECT(result[jss::error_message] == "Exactly one of ledger_hash and ledger_index can be set.");

        // the purpose in this test is to force the ledger expiration/out of
        // date check to trigger
        env.timeKeeper().adjustCloseTime(weeks{3});
        result = env.rpc ( "ledger_request", "1" ) [jss::result];
        BEAST_EXPECT(result[jss::error]         == "noCurrent");
        BEAST_EXPECT(result[jss::status]        == "error");
        BEAST_EXPECT(result[jss::error_message] == "Current ledger is unavailable.");

    }

    void testMoreThan256Closed()
    {
        using namespace test::jtx;
        Env env {*this};
        Account const gw {"gateway"};
        env.app().getLedgerMaster().tune(0, 3600);
        auto const USD = gw["USD"];
        env.fund(CSC(100000), gw);

        int const max_limit = 256;

        for (auto i = 0; i < max_limit + 10; i++)
        {
            Account const bob {std::string("bob") + std::to_string(i)};
            env.fund(CSC(1000), bob);
            env.close();
        }

        auto result = env.rpc ( "ledger_request", "1" ) [jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index]     == "1");
        BEAST_EXPECT(result[jss::ledger][jss::total_coins]      == "4000000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed]           == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash]      == "C7AAD8A09ADA998B963A56ADAD4CC35F501CD183DD4F19087C5A466D55676FF4");
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash]      == "0000000000000000000000000000000000000000000000000000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::account_hash]     == "D8939DBDA628BC984D65B08CDA1DCC351A06B53204A4E4F62D0BBB796FAA146E");
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == "0000000000000000000000000000000000000000000000000000000000000000");
    }

    void testNonAdmin()
    {
        using namespace test::jtx;
        Env env { *this, envconfig(no_admin) };
        Account const gw { "gateway" };
        auto const USD = gw["USD"];
        env.fund(CSC(100000), gw);
        env.close();

        auto const result = env.rpc ( "ledger_request", "1" )  [jss::result];
        // The current HTTP/S ServerHandler returns an HTTP 403 error code here
        // rather than a noPermission JSON error.  The JSONRPCClient just eats that
        // error and returns an null result.
        BEAST_EXPECT(result.type() == Json::nullValue);

    }

    void run ()
    {
        testLedgerRequest();
        testEvolution();
        testBadInput();
        testMoreThan256Closed();
        testNonAdmin();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerRequestRPC,app,casinocoin);

} // RPC
} // casinocoin

