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
    2018-07-19  jrojek          Created
*/
//==============================================================================

#ifndef CASINOCOIN_PROTOCOL_STPERFORMANCEREPORT_H_INCLUDED
#define CASINOCOIN_PROTOCOL_STPERFORMANCEREPORT_H_INCLUDED

#include <casinocoin/protocol/PublicKey.h>
#include <casinocoin/protocol/SecretKey.h>
#include <casinocoin/protocol/STObject.h>
#include <cstdint>
#include <memory>

namespace casinocoin {

class STPerformanceReport final
    : public STObject
    , public CountedObject <STPerformanceReport>
{
public:
    static char const* getCountedObjectName () { return "STPerformanceReport"; }

    using pointer = std::shared_ptr<STPerformanceReport>;
    using ref     = const std::shared_ptr<STPerformanceReport>&;

    // These throw if the object is not valid
    STPerformanceReport (SerialIter & sit, bool checkSignature = true);

    // Does not sign the report
    STPerformanceReport (
            NetClock::time_point signTime,
            PublicKey const& publicKey,
            Blob const& domain,
            Blob const& signature);

    STBase*
    copy (std::size_t n, void* buf) const override
    {
        return emplace(n, buf, *this);
    }

    STBase*
    move (std::size_t n, void* buf) override
    {
        return emplace(n, buf, std::move(*this));
    }

    NetClock::time_point getSignTime ()  const;
    NetClock::time_point getSeenTime ()  const;
    std::uint32_t   getFlags ()          const;
    PublicKey       getSignerPublic ()   const;
    PublicKey       getNodePublic ()     const;
    std::uint32_t   getLastLedgerIndex() const;
    uint256         getSigningHash ()    const;

    bool            isValid ()           const;

    void            setSeen (NetClock::time_point s)
    {
        mSeen = s;
    }
    Blob          getSerialized ()       const;
    Blob          getSignature ()        const;
    std::string   getDomainName ()       const;
    std::uint32_t getLatency ()          const;
    bool          getActivated ()        const;
    std::uint16_t getWSPort ()           const;

private:
    static SOTemplate const& getFormat ();

    void setNode ();

    NetClock::time_point mSeen = {};
};

} // casinocoin

#endif // CASINOCOIN_PROTOCOL_STPERFORMANCEREPORT_H_INCLUDED
