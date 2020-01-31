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

#include <BeastConfig.h>
#include <casinocoin/protocol/STPerformanceReport.h>
#include <casinocoin/protocol/HashPrefix.h>
#include <casinocoin/basics/contract.h>
#include <casinocoin/basics/Log.h>
#include <casinocoin/json/to_string.h>
#include <casinocoin/basics/StringUtilities.h>

namespace casinocoin {

STPerformanceReport::STPerformanceReport (SerialIter& sit, bool checkSignature)
    : STObject (getFormat (), sit, sfPerformanceReport)
{
    if  (checkSignature && !isValid ())
    {
        JLOG (debugLog().error())
            << "Invalid performance report" << getJson (0);
        Throw<std::runtime_error> ("Invalid performance report");
    }
}

STPerformanceReport::STPerformanceReport (
        NetClock::time_point signTime,
        PublicKey const& publicKey,
        Blob const& domain,
        Blob const& signature
        )
    : STObject (getFormat (), sfPerformanceReport)
    , mSeen (signTime)
{
    // Does not sign
    setFieldU32 (sfSigningTime, signTime.time_since_epoch().count());

    setFieldVL (sfCRN_PublicKey, publicKey.slice());
    setFieldVL (sfSignature, signature);
    setFieldVL (sfCRN_DomainName, domain);
    setFieldU8 (sfCRNActivated, 0);
}

NetClock::time_point
STPerformanceReport::getSignTime () const
{
    return NetClock::time_point{NetClock::duration{getFieldU32(sfSigningTime)}};
}

NetClock::time_point STPerformanceReport::getSeenTime () const
{
    return mSeen;
}

std::uint32_t STPerformanceReport::getFlags () const
{
    return getFieldU32 (sfFlags);
}

bool STPerformanceReport::isValid () const
{
    try
    {
        return casinocoin::verify(getSignerPublic(),
            makeSlice(getFieldVL(sfCRN_DomainName)),
            makeSlice(getFieldVL(sfSignature)));
    }
    catch (std::exception const&)
    {
        JLOG (debugLog().error())
            << "Exception validating performance report";
        return false;
    }
}

PublicKey STPerformanceReport::getSignerPublic () const
{
    return PublicKey(makeSlice (getFieldVL (sfCRN_PublicKey)));
}

PublicKey STPerformanceReport::getNodePublic () const
{
    if (isFieldPresent(sfPublicKey))
        return PublicKey(makeSlice (getFieldVL (sfPublicKey)));
    return PublicKey();
}

uint32_t STPerformanceReport::getLastLedgerIndex() const
{
    return getFieldU32 (sfLastLedgerSequence);
}

uint256 STPerformanceReport::getSigningHash () const
{
    return STObject::getSigningHash (HashPrefix::performanceReport);
}

std::string STPerformanceReport::getDomainName () const
{
    Blob hexedDomain = getFieldVL(sfCRN_DomainName);
    std::string hexDomainStr(hexedDomain.begin(), hexedDomain.end());
    Blob unhexedDomain = strUnHex(hexDomainStr).first;
    return std::string(unhexedDomain.begin(), unhexedDomain.end());
}

Blob STPerformanceReport::getSignature () const
{
    return getFieldVL (sfSignature);
}

bool STPerformanceReport::getActivated () const
{
    return ((getFieldU8(sfCRNActivated) > 0) ? true : false);
}

uint16_t STPerformanceReport::getWSPort() const
{
    if (isFieldPresent(sfPort))
        return getFieldU16(sfPort);
    return 0;
}

Blob STPerformanceReport::getSerialized () const
{
    Serializer s;
    add (s);
    return s.peekData ();
}

std::uint32_t STPerformanceReport::getLatency() const
{
    return getFieldU32 (sfCRN_LatencyAvg);
}

SOTemplate const& STPerformanceReport::getFormat ()
{
    struct FormatHolder
    {
        SOTemplate format;

        FormatHolder ()
        {
            format.push_back (SOElement (sfFlags,                   SOE_REQUIRED));
            format.push_back (SOElement (sfCRN_PublicKey,           SOE_REQUIRED));
            format.push_back (SOElement (sfSignature,               SOE_REQUIRED));
            format.push_back (SOElement (sfCRN_DomainName,          SOE_REQUIRED));
            format.push_back (SOElement (sfSigningTime,             SOE_REQUIRED));
            format.push_back (SOElement (sfCRN_LatencyAvg,          SOE_OPTIONAL));
            format.push_back (SOElement (sfFirstLedgerSequence,     SOE_OPTIONAL));
            format.push_back (SOElement (sfLastLedgerSequence,      SOE_OPTIONAL));
            format.push_back (SOElement (sfStatusMode,              SOE_OPTIONAL));
            format.push_back (SOElement (sfCRNPerformance,          SOE_OPTIONAL));
            format.push_back (SOElement (sfCRNActivated,            SOE_OPTIONAL));
            format.push_back (SOElement (sfPublicKey,               SOE_OPTIONAL));
            format.push_back (SOElement (sfPort,                    SOE_OPTIONAL));
        }
    };

    static FormatHolder holder;

    return holder.format;
}

} // casinocoin
