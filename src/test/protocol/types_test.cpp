//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

 
#include <casinocoin/protocol/types.h>
#include <casinocoin/beast/unit_test.h>

namespace casinocoin {

struct types_test : public beast::unit_test::suite
{
    void
    testAccountID()
    {
        auto const s =
            "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
        if (BEAST_EXPECT(parseBase58<AccountID>(s)))
            BEAST_EXPECT(toBase58(
                *parseBase58<AccountID>(s)) == s);
    }

    void
    run() override
    {
        testAccountID();
    }
};

BEAST_DEFINE_TESTSUITE(types,protocol,ripple);

}
