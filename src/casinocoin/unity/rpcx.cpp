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

#include <BeastConfig.h>

// This has to be included early to prevent an obscure MSVC compile error
#include <boost/asio/deadline_timer.hpp>

#include <casinocoin/protocol/JsonFields.h>

#include <casinocoin/rpc/RPCHandler.h>

#include <casinocoin/rpc/impl/RPCHandler.cpp>
#include <casinocoin/rpc/impl/Status.cpp>

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
#include <casinocoin/rpc/handlers/ConfigInfo.cpp>
#include <casinocoin/rpc/handlers/Connect.cpp>
#include <casinocoin/rpc/handlers/ConsensusInfo.cpp>
#include <casinocoin/rpc/handlers/DecryptMsgHandler.cpp>
#include <casinocoin/rpc/handlers/EncryptMsgHandler.cpp>
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
#include <casinocoin/rpc/handlers/PathFind.cpp>
#include <casinocoin/rpc/handlers/PayChanClaim.cpp>
#include <casinocoin/rpc/handlers/Peers.cpp>
#include <casinocoin/rpc/handlers/Ping.cpp>
#include <casinocoin/rpc/handlers/Print.cpp>
#include <casinocoin/rpc/handlers/Random.cpp>
#include <casinocoin/rpc/handlers/CasinocoinPathFind.cpp>
#include <casinocoin/rpc/handlers/ServerInfo.cpp>
#include <casinocoin/rpc/handlers/ServerState.cpp>
#include <casinocoin/rpc/handlers/SignFor.cpp>
#include <casinocoin/rpc/handlers/SignHandler.cpp>
#include <casinocoin/rpc/handlers/SignMsgHandler.cpp>
#include <casinocoin/rpc/handlers/Stop.cpp>
#include <casinocoin/rpc/handlers/Submit.cpp>
#include <casinocoin/rpc/handlers/SubmitMultiSigned.cpp>
#include <casinocoin/rpc/handlers/Subscribe.cpp>
#include <casinocoin/rpc/handlers/TransactionEntry.cpp>
#include <casinocoin/rpc/handlers/Tx.cpp>
#include <casinocoin/rpc/handlers/TxHistory.cpp>
#include <casinocoin/rpc/handlers/UnlList.cpp>
#include <casinocoin/rpc/handlers/Unsubscribe.cpp>
#include <casinocoin/rpc/handlers/VerifyMsgHandler.cpp>
#include <casinocoin/rpc/handlers/ValidationCreate.cpp>
#include <casinocoin/rpc/handlers/ValidationSeed.cpp>
#include <casinocoin/rpc/handlers/WalletPropose.cpp>
#include <casinocoin/rpc/handlers/WalletSeed.cpp>

#include <casinocoin/rpc/impl/Handler.cpp>
#include <casinocoin/rpc/impl/LegacyPathFind.cpp>
#include <casinocoin/rpc/impl/Role.cpp>
#include <casinocoin/rpc/impl/RPCHelpers.cpp>
#include <casinocoin/rpc/impl/ServerHandlerImp.cpp>
#include <casinocoin/rpc/impl/TransactionSign.cpp>
