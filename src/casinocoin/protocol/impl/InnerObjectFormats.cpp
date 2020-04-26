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
    2018-01-11  jrojek          KYC object added
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/protocol/InnerObjectFormats.h>

namespace casinocoin {

InnerObjectFormats::InnerObjectFormats ()
{
    add (sfSignerEntry.getJsonName ().c_str (), sfSignerEntry.getCode ())
        << SOElement (sfAccount,              SOE_REQUIRED)
        << SOElement (sfSignerWeight,         SOE_REQUIRED)
        ;

    add (sfSigner.getJsonName ().c_str (), sfSigner.getCode ())
        << SOElement (sfAccount,              SOE_REQUIRED)
        << SOElement (sfSigningPubKey,        SOE_REQUIRED)
        << SOElement (sfTxnSignature,         SOE_REQUIRED)
        ;

    add (sfKYC.getJsonName ().c_str (), sfKYC.getCode ())
        << SOElement (sfKYCTime,              SOE_REQUIRED)
        << SOElement (sfKYCVerifications,     SOE_OPTIONAL)
        ;

    add (sfConfigEntry.getJsonName ().c_str (), sfConfigEntry.getCode ())
        << SOElement (sfConfigID,             SOE_REQUIRED)
        << SOElement (sfConfigType,           SOE_REQUIRED)
        << SOElement (sfConfigData,           SOE_REQUIRED)
        ;

    add (sfCRNStatus.getJsonName ().c_str(), sfCRNStatus.getCode ())
        << SOElement (sfStatusMode,           SOE_OPTIONAL)
        << SOElement (sfTransitions,          SOE_OPTIONAL)
        << SOElement (sfDuration,             SOE_OPTIONAL)
        ;
}

void InnerObjectFormats::addCommonFields (Item& item)
{
}

InnerObjectFormats const&
InnerObjectFormats::getInstance ()
{
    static InnerObjectFormats instance;
    return instance;
}

SOTemplate const*
InnerObjectFormats::findSOTemplateBySField (SField const& sField) const
{
    SOTemplate const* ret = nullptr;
    auto itemPtr = findByType (sField.getCode ());
    if (itemPtr)
        ret = &(itemPtr->elements);

    return ret;
}

} // casinocoin
