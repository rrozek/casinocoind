//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/casinocoin/casinocoind
    Copyright (c) 2018 CasinoCoin Foundation

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
    2018-07-20  jrojek          Created
*/
//==============================================================================

#ifndef CASINOCOIN_APP_MISC_CRNREPORTS_H_INCLUDED
#define CASINOCOIN_APP_MISC_CRNREPORTS_H_INCLUDED

#include <casinocoin/app/main/Application.h>
#include <casinocoin/protocol/Protocol.h>
#include <casinocoin/protocol/STPerformanceReport.h>
#include <memory>
#include <vector>

namespace casinocoin {

// nodes reporting and highest node ID reporting
using CRNReportSet = hash_map<PublicKey, STPerformanceReport::pointer>;

class CRNReports
{
public:
    virtual ~CRNReports() = default;

    virtual bool addReport (STPerformanceReport::ref, std::string const& source) = 0;

    virtual bool current (STPerformanceReport::ref) const = 0;

    virtual std::list <STPerformanceReport::pointer>
    getCurrentReports () = 0;

    virtual hash_set<PublicKey> getCurrentPublicKeys () = 0;

    virtual void flush () = 0;
};

extern
std::unique_ptr<CRNReports>
make_CRNReports(Application& app);

} // casinocoin

#endif // CASINOCOIN_APP_MISC_CRNREPORTS_H_INCLUDED
