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

 

#include <casinocoin/basics/impl/BasicConfig.cpp>
#include <casinocoin/basics/impl/CheckLibraryVersions.cpp>
#include <casinocoin/basics/impl/contract.cpp>
#include <casinocoin/basics/impl/CountedObject.cpp>
#include <casinocoin/basics/impl/Log.cpp>
#include <casinocoin/basics/impl/make_SSLContext.cpp>
#include <casinocoin/basics/impl/mulDiv.cpp>
#include <casinocoin/basics/impl/PerfLogImp.cpp>
#include <casinocoin/basics/impl/ResolverAsio.cpp>
#include <casinocoin/basics/impl/strHex.cpp>
#include <casinocoin/basics/impl/StringUtilities.cpp>
#include <casinocoin/basics/impl/Sustain.cpp>
#include <casinocoin/basics/impl/UptimeClock.cpp>

#if DOXYGEN
#include <casinocoin/basics/README.md>
#endif

