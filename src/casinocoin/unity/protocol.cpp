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

#include <casinocoin/protocol/impl/AccountID.cpp>
#include <casinocoin/protocol/impl/Book.cpp>
#include <casinocoin/protocol/impl/BuildInfo.cpp>
#include <casinocoin/protocol/impl/ByteOrder.cpp>
#include <casinocoin/protocol/impl/digest.cpp>
#include <casinocoin/protocol/impl/ErrorCodes.cpp>
#include <casinocoin/protocol/impl/Feature.cpp>
#include <casinocoin/protocol/impl/HashPrefix.cpp>
#include <casinocoin/protocol/impl/Indexes.cpp>
#include <casinocoin/protocol/impl/Issue.cpp>
#include <casinocoin/protocol/impl/Keylet.cpp>
#include <casinocoin/protocol/impl/LedgerFormats.cpp>
#include <casinocoin/protocol/impl/PublicKey.cpp>
#include <casinocoin/protocol/impl/Quality.cpp>
#include <casinocoin/protocol/impl/Rate2.cpp>
#include <casinocoin/protocol/impl/SecretKey.cpp>
#include <casinocoin/protocol/impl/Seed.cpp>
#include <casinocoin/protocol/impl/Serializer.cpp>
#include <casinocoin/protocol/impl/SField.cpp>
#include <casinocoin/protocol/impl/Sign.cpp>
#include <casinocoin/protocol/impl/SOTemplate.cpp>
#include <casinocoin/protocol/impl/TER.cpp>
#include <casinocoin/protocol/impl/tokens.cpp>
#include <casinocoin/protocol/impl/TxFormats.cpp>
#include <casinocoin/protocol/impl/UintTypes.cpp>

#include <casinocoin/protocol/impl/STAccount.cpp>
#include <casinocoin/protocol/impl/STArray.cpp>
#include <casinocoin/protocol/impl/STAmount.cpp>
#include <casinocoin/protocol/impl/STBase.cpp>
#include <casinocoin/protocol/impl/STBlob.cpp>
#include <casinocoin/protocol/impl/STInteger.cpp>
#include <casinocoin/protocol/impl/STLedgerEntry.cpp>
#include <casinocoin/protocol/impl/STObject.cpp>
#include <casinocoin/protocol/impl/STParsedJSON.cpp>
#include <casinocoin/protocol/impl/InnerObjectFormats.cpp>
#include <casinocoin/protocol/impl/STPathSet.cpp>
#include <casinocoin/protocol/impl/STTx.cpp>
#include <casinocoin/protocol/impl/STValidation.cpp>
#include <casinocoin/protocol/impl/STVar.cpp>
#include <casinocoin/protocol/impl/STVector256.cpp>
#include <casinocoin/protocol/impl/STVector128.cpp>
#include <casinocoin/protocol/impl/IOUAmount.cpp>

#if DOXYGEN
#include <casinocoin/protocol/README.md>
#endif
