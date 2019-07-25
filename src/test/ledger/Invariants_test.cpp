//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

 
#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <casinocoin/beast/utility/Journal.h>
#include <casinocoin/app/tx/apply.h>
#include <casinocoin/app/tx/impl/Transactor.h>
#include <casinocoin/app/tx/impl/ApplyContext.h>
#include <casinocoin/protocol/STLedgerEntry.h>
#include <boost/algorithm/string/predicate.hpp>

namespace casinocoin {

class Invariants_test : public beast::unit_test::suite
{

    class TestSink : public beast::Journal::Sink
    {
    public:
        std::stringstream strm_;

        TestSink () : Sink (beast::severities::kWarning, false) {  }

        void
        write (beast::severities::Severity level,
            std::string const& text) override
        {
            if (level < threshold())
                return;

            strm_ << text << std::endl;
        }
    };

    // this is common setup/method for running a failing invariant check. The
    // precheck function is used to manipulate the ApplyContext with view
    // changes that will cause the check to fail.
    using Precheck = std::function <
        bool (
            test::jtx::Account const& a,
            test::jtx::Account const& b,
            ApplyContext& ac)>;

    using TXMod = std::function <
        void (STTx& tx)>;

    void
    doInvariantCheck( bool enabled,
        std::vector<std::string> const& expect_logs,
        Precheck const& precheck,
        CSCAmount fee = CSCAmount{},
        TXMod txmod = [](STTx&){})
    {
        using namespace test::jtx;
        Env env {*this};
        if (! enabled)
        {
            auto& features = env.app().config().features;
            auto it = features.find(featureEnforceInvariants);
            if (it != features.end())
                features.erase(it);
        }

        Account A1 {"A1"};
        Account A2 {"A2"};
        env.fund (CSC (1000), A1, A2);
        env.close();

        // dummy/empty tx to setup the AccountContext
        auto tx = STTx {ttACCOUNT_SET, [](STObject&){  } };
        txmod(tx);
        OpenView ov {*env.current()};
        TestSink sink;
        beast::Journal jlog {sink};
        ApplyContext ac {
            env.app(),
            ov,
            tx,
            tesSUCCESS,
            env.current()->fees().base,
            tapNONE,
            jlog
        };

        BEAST_EXPECT(precheck(A1, A2, ac));

        TER tr = tesSUCCESS;
        // invoke check twice to cover tec and tef cases
        for (auto i : {0,1})
        {
            tr = ac.checkInvariants(tr, fee);
            if (enabled)
            {
                BEAST_EXPECT(tr == (i == 0
                    ? TER {tecINVARIANT_FAILED}
                    : TER {tefINVARIANT_FAILED}));
                BEAST_EXPECT(
                    boost::starts_with(sink.strm_.str(), "Invariant failed:") ||
                    boost::starts_with(sink.strm_.str(),
                        "Transaction caused an exception"));

                for (auto const& m : expect_logs)
                {
                    bool condition = sink.strm_.str().find(m) != std::string::npos;
                    BEAST_EXPECT(condition);
                    if (!condition)
                    {
                        //uncomment if you want to log the invariant failure message
                        log << "   --> " << sink.strm_.str() << std::endl;
                        log << "exp -> " << m << std::endl;
                    }
                }
            }
            else
            {
                BEAST_EXPECT(tr == tesSUCCESS);
                BEAST_EXPECT(sink.strm_.str().empty());
            }
        }
    }

    void
    testEnabled ()
    {
        using namespace test::jtx;
        testcase ("feature enabled");
        Env env {*this};

        auto hasInvariants =
            env.app().config().features.find (featureEnforceInvariants);
        BEAST_EXPECT(hasInvariants != env.app().config().features.end());

        BEAST_EXPECT(env.current()->rules().enabled(featureEnforceInvariants));
    }

    void
    testCSCNotCreated (bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - CSC created";
        doInvariantCheck (enabled,
            {{ "CSC net change was positive: 500" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // put a single account in the view and "manufacture" some CSC
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto amt = sle->getFieldAmount (sfBalance);
                sle->setFieldAmount (sfBalance, amt + 500);
                ac.view().update (sle);
                return true;
            });
    }

    void
    testAccountsNotRemoved(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - account root removed";
        doInvariantCheck (enabled,
            {{ "an account root was deleted" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // remove an account from the view
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                ac.view().erase (sle);
                return true;
            });
    }

    void
    testTypesMatch(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - LE types don't match";
        doInvariantCheck (enabled,
            {{ "ledger entry type mismatch" },
             { "CSC net change of -100000000000 doesn't match fee 0" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // replace an entry in the table with an SLE of a different type
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto sleNew = std::make_shared<SLE> (ltTICKET, sle->key());
                ac.rawView().rawReplace (sleNew);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "invalid ledger entry type added" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // add an entry in the table with an SLE of an invalid type
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                // make a dummy escrow ledger entry, then change the type to an
                // unsupported value so that the valid type invariant check
                // will fail.
                auto sleNew = std::make_shared<SLE> (
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));
                sleNew->type_ = ltNICKNAME;
                ac.view().insert (sleNew);
                return true;
            });
    }

    void
    testNoCSCTrustLine(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - trust lines with CSC not allowed";
        doInvariantCheck (enabled,
            {{ "an CSC trust line was created" }},
            [](Account const& A1, Account const& A2, ApplyContext& ac)
            {
                // create simple trust SLE with csc currency
                auto index = getCasinocoinStateIndex (A1, A2, cscIssue().currency);
                auto const sleNew = std::make_shared<SLE>(
                    ltCASINOCOIN_STATE, index);
                ac.view().insert (sleNew);
                return true;
            });
    }

    void
    testCSCBalanceCheck(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - CSC balance checks";

        doInvariantCheck (enabled,
            {{ "Cannot return non-native STAmount as CSCAmount" }},
            [](Account const& A1, Account const& A2, ApplyContext& ac)
            {
                //non-native balance
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                STAmount nonNative (A2["USD"](51));
                sle->setFieldAmount (sfBalance, nonNative);
                ac.view().update (sle);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "incorrect account CSC balance" },
             {  "CSC net change was positive: 3999999900000000001" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // balance exceeds genesis amount
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                sle->setFieldAmount (sfBalance, SYSTEM_CURRENCY_START + 1);
                ac.view().update (sle);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "incorrect account CSC balance" },
             { "CSC net change of -100000000001 doesn't match fee 0" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // balance is negative
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                sle->setFieldAmount (sfBalance, -1);
                ac.view().update (sle);
                return true;
            });
    }

    void
    testTransactionFeeCheck(bool enabled)
    {
        using namespace test::jtx;
        using namespace std::string_literals;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - Transaction fee checks";

        doInvariantCheck (enabled,
            {{ "fee paid was negative: -1" },
             { "CSC net change of 0 doesn't match fee -1" }},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            CSCAmount{-1});

        doInvariantCheck (enabled,
            {{ "fee paid exceeds system limit: "s +
                std::to_string(SYSTEM_CURRENCY_START) },
             { "CSC net change of 0 doesn't match fee "s +
                std::to_string(SYSTEM_CURRENCY_START) }},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            CSCAmount{SYSTEM_CURRENCY_START});

         doInvariantCheck (enabled,
            {{ "fee paid is 20 exceeds fee specified in transaction." },
             { "CSC net change of 0 doesn't match fee 20" }},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            CSCAmount{20},
            [](STTx& tx) { tx.setFieldAmount(sfFee, CSCAmount{10}); } );
    }


    void
    testNoBadOffers(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - no bad offers";

        doInvariantCheck (enabled,
            {{ "offer with a bad amount" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // offer with negative takerpays
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto const offer_index =
                    getOfferIndex (A1.id(), (*sle)[sfSequence]);
                auto sleNew = std::make_shared<SLE> (ltOFFER, offer_index);
                sleNew->setAccountID (sfAccount, A1.id());
                sleNew->setFieldU32 (sfSequence, (*sle)[sfSequence]);
                sleNew->setFieldAmount (sfTakerPays, CSC(-1));
                ac.view().insert (sleNew);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "offer with a bad amount" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // offer with negative takergets
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto const offer_index =
                    getOfferIndex (A1.id(), (*sle)[sfSequence]);
                auto sleNew = std::make_shared<SLE> (ltOFFER, offer_index);
                sleNew->setAccountID (sfAccount, A1.id());
                sleNew->setFieldU32 (sfSequence, (*sle)[sfSequence]);
                sleNew->setFieldAmount (sfTakerPays, A1["USD"](10));
                sleNew->setFieldAmount (sfTakerGets, CSC(-1));
                ac.view().insert (sleNew);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "offer with a bad amount" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // offer CSC to CSC
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto const offer_index =
                    getOfferIndex (A1.id(), (*sle)[sfSequence]);
                auto sleNew = std::make_shared<SLE> (ltOFFER, offer_index);
                sleNew->setAccountID (sfAccount, A1.id());
                sleNew->setFieldU32 (sfSequence, (*sle)[sfSequence]);
                sleNew->setFieldAmount (sfTakerPays, CSC(10));
                sleNew->setFieldAmount (sfTakerGets, CSC(11));
                ac.view().insert (sleNew);
                return true;
            });
    }

    void
    testNoZeroEscrow(bool enabled)
    {
        using namespace test::jtx;
        testcase << "checks " << (enabled ? "enabled" : "disabled") <<
            " - no zero escrow";

        // jrojek TODO: ENABLE THIS TEST BUT ADJUST IT
        //              after TokenEscrow amendment that will actually be possible
//        doInvariantCheck (enabled,
//            {{ "Cannot return non-native STAmount as CSCAmount" }},
//            [](Account const& A1, Account const& A2, ApplyContext& ac)
//            {
//                // escrow with nonnative amount
//                auto const sle = ac.view().peek (keylet::account(A1.id()));
//                if(! sle)
//                    return false;
//                auto sleNew = std::make_shared<SLE> (
//                    keylet::escrow(A1, (*sle)[sfSequence] + 2));
//                STAmount nonNative (A2["USD"](51));
//                sleNew->setFieldAmount (sfAmount, nonNative);
//                ac.view().insert (sleNew);
//                return true;
//            });

        doInvariantCheck (enabled,
            {{ "CSC net change of -100000000 doesn't match fee 0"},
             {  "escrow specifies invalid amount" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // escrow with negative amount
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto sleNew = std::make_shared<SLE> (
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));
                sleNew->setFieldAmount (sfAmount, CSC(-1));
                ac.view().insert (sleNew);
                return true;
            });

        doInvariantCheck (enabled,
            {{ "CSC net change was positive: 4000000000000000001" },
             {  "escrow specifies invalid amount" }},
            [](Account const& A1, Account const&, ApplyContext& ac)
            {
                // escrow with too-large amount
                auto const sle = ac.view().peek (keylet::account(A1.id()));
                if(! sle)
                    return false;
                auto sleNew = std::make_shared<SLE> (
                    keylet::escrow(A1, (*sle)[sfSequence] + 2));
                sleNew->setFieldAmount (sfAmount, SYSTEM_CURRENCY_START + 1);
                ac.view().insert (sleNew);
                return true;
            });
    }

public:
    void run () override
    {
        testEnabled ();
        // all invariant checks are run with
        // the checks enabled and disabled
        for(auto const& b : {true, false})
        {
            testCSCNotCreated (b);
            testAccountsNotRemoved (b);
            testTypesMatch (b);
            testNoCSCTrustLine (b);
            testCSCBalanceCheck (b);
            testTransactionFeeCheck(b);
            testNoBadOffers (b);
            testNoZeroEscrow (b);
        }
    }
};

BEAST_DEFINE_TESTSUITE (Invariants, ledger, casinocoin);

}  // casinocoin

