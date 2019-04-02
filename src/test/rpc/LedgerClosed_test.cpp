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

#include <casinocoin/protocol/JsonFields.h>
#include <casinocoin/protocol/Feature.h>
#include <test/jtx.h>

namespace casinocoin {

class LedgerClosed_test : public beast::unit_test::suite
{
public:

    void testMonitorRoot()
    {
        using namespace test::jtx;
        Env env {*this};
        Account const alice {"alice"};
        env.fund(CSC(10000), alice);

        auto lc_result = env.rpc("ledger_closed") [jss::result];
        BEAST_EXPECT(lc_result[jss::ledger_hash]  == "4BE3C23DC8B17F0AF2385F7C9C49772390012AA97E3743C705DA86614DE12DDC");
        BEAST_EXPECT(lc_result[jss::ledger_index] == 2);

        env.close();
        auto const ar_master = env.le(env.master);
        BEAST_EXPECT(ar_master->getAccountID(sfAccount) == env.master.id());
        BEAST_EXPECT((*ar_master)[sfBalance] == drops( 3999998999998000000 ));

        auto const ar_alice = env.le(alice);
        BEAST_EXPECT(ar_alice->getAccountID(sfAccount) == alice.id());
        BEAST_EXPECT((*ar_alice)[sfBalance] == CSC( 10000 ));

        lc_result = env.rpc("ledger_closed") [jss::result];
        BEAST_EXPECT(lc_result[jss::ledger_hash]  == "44753D8A0C037534947DD8E3EF0DAE4AD0FF30EA47742C6649C90A923C8D911C");
        BEAST_EXPECT(lc_result[jss::ledger_index] == 3);
    }

    void run()
    {
        testMonitorRoot();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerClosed,app,casinocoin);

}

