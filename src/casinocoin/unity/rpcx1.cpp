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

//==============================================================================
/*
    2017-06-30  ajochems        Refactored for casinocoin
*/
//==============================================================================

 

// This has to be included early to prevent an obscure MSVC compile error
#include <boost/asio/deadline_timer.hpp>

#include <casinocoin/protocol/JsonFields.h>
#include <casinocoin/rpc/RPCHandler.h>
#include <casinocoin/rpc/handlers/Handlers.h>

#include <casinocoin/rpc/handlers/AccountCurrenciesHandler.cpp>
#include <casinocoin/rpc/handlers/AccountInfo.cpp>
#include <casinocoin/rpc/handlers/AccountLines.cpp>
#include <casinocoin/rpc/handlers/AccountChannels.cpp>
#include <casinocoin/rpc/handlers/AccountObjects.cpp>
#include <casinocoin/rpc/handlers/AccountOffers.cpp>
#include <casinocoin/rpc/handlers/AccountTx.cpp>
#include <casinocoin/rpc/handlers/AccountTxOld.cpp>
#include <casinocoin/rpc/handlers/AccountTxSwitch.cpp>
#include <casinocoin/rpc/handlers/BlackList.cpp>
#include <casinocoin/rpc/handlers/BookOffers.cpp>
#include <casinocoin/rpc/handlers/CanDelete.cpp>
#include <casinocoin/rpc/handlers/Connect.cpp>
#include <casinocoin/rpc/handlers/ConsensusInfo.cpp>
#include <casinocoin/rpc/handlers/Feature1.cpp>
#include <casinocoin/rpc/handlers/Fee1.cpp>
#include <casinocoin/rpc/handlers/FetchInfo.cpp>
#include <casinocoin/rpc/handlers/GatewayBalances.cpp>
#include <casinocoin/rpc/handlers/GetCounts.cpp>
#include <casinocoin/rpc/handlers/LedgerHandler.cpp>
#include <casinocoin/rpc/handlers/LedgerAccept.cpp>
#include <casinocoin/rpc/handlers/LedgerCleanerHandler.cpp>
#include <casinocoin/rpc/handlers/LedgerClosed.cpp>
#include <casinocoin/rpc/handlers/LedgerCurrent.cpp>
#include <casinocoin/rpc/handlers/LedgerData.cpp>
#include <casinocoin/rpc/handlers/LedgerEntry.cpp>
#include <casinocoin/rpc/handlers/LedgerHeader.cpp>
#include <casinocoin/rpc/handlers/LedgerRequest.cpp>
#include <casinocoin/rpc/handlers/LogLevel.cpp>
#include <casinocoin/rpc/handlers/LogRotate.cpp>
#include <casinocoin/rpc/handlers/NoCasinocoinCheck.cpp>
#include <casinocoin/rpc/handlers/OwnerInfo.cpp>
