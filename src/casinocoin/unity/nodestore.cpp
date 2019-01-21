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

#include <casinocoin/nodestore/backend/MemoryFactory.cpp>
#include <casinocoin/nodestore/backend/NuDBFactory.cpp>
#include <casinocoin/nodestore/backend/NullFactory.cpp>
#include <casinocoin/nodestore/backend/RocksDBFactory.cpp>
#include <casinocoin/nodestore/backend/RocksDBQuickFactory.cpp>

#include <casinocoin/nodestore/impl/BatchWriter.cpp>
#include <casinocoin/nodestore/impl/Database.cpp>
#include <casinocoin/nodestore/impl/DatabaseNodeImp.cpp>
#include <casinocoin/nodestore/impl/DatabaseRotatingImp.cpp>
#include <casinocoin/nodestore/impl/DatabaseShardImp.cpp>
#include <casinocoin/nodestore/impl/DummyScheduler.cpp>
#include <casinocoin/nodestore/impl/DecodedBlob.cpp>
#include <casinocoin/nodestore/impl/EncodedBlob.cpp>
#include <casinocoin/nodestore/impl/ManagerImp.cpp>
#include <casinocoin/nodestore/impl/NodeObject.cpp>
#include <casinocoin/nodestore/impl/Shard.cpp>
