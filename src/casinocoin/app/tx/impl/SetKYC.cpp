//------------------------------------------------------------------------------
/*
    This file is part of casinocoind: https://github.com/casinocoin/casinocoind

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
    2018-01-11  jrojek          created
*/
//==============================================================================

#include <BeastConfig.h>
#include <casinocoin/app/tx/impl/SetKYC.h>
#include <casinocoin/basics/Log.h>
#include <casinocoin/core/Config.h>
#include <casinocoin/protocol/Feature.h>
#include <casinocoin/protocol/Indexes.h>
#include <casinocoin/protocol/PublicKey.h>
#include <casinocoin/protocol/Quality.h>
#include <casinocoin/protocol/st.h>
#include <casinocoin/ledger/View.h>

namespace casinocoin {

TER
SetKYC::preflight (PreflightContext const& ctx)
{
    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    // SetKYC transaction can only be issued with Multi-sign
    if (!ctx.rules.enabled (featureMultiSign))
    {
        JLOG(j.trace()) << "KYC Set transaction can only be Multi-signed. "
                           "This feature is disabled";
        return temDISABLED;
    }

    Blob const& signingPubKey = tx.getFieldVL (sfSigningPubKey);
    if (!signingPubKey.empty ())
    {
        JLOG(j.trace()) << "KYC Set transaction can only be Multi-signed.";
        return temMALFORMED;
    }

    std::uint32_t const uSetFlag = tx.getFieldU32 (sfSetFlag);
    std::uint32_t const uClearFlag = tx.getFieldU32 (sfClearFlag);

    if ((uSetFlag != 0) && (uSetFlag == uClearFlag))
    {
        JLOG(j.trace()) << "Malformed transaction: Set and clear same flag.";
        return temINVALID_FLAG;
    }

    // KYC Validation flag
    if (uSetFlag == kycfValidated || uClearFlag == kycfValidated)
    {
        if (!ctx.rules.enabled(featureKYC))
        {
            JLOG(j.trace()) << "Feature KYC disabled.";
            return temDISABLED;
        }
    }

    // KYC Verification
    if (tx.isFieldPresent (sfKYCVerifications))
    {
        if (!ctx.rules.enabled(featureKYC))
        {
            JLOG(j.trace()) << "Feature KYC disabled.";
            return temDISABLED;
        }
    }

    return preflight2(ctx);
}

TER
SetKYC::doApply ()
{
    auto const sle = view().peek(
        keylet::account(account_));

    std::uint32_t const uFlagsIn = sle->getFieldU32 (sfFlags);
    std::uint32_t uFlagsOut = uFlagsIn;

    std::uint32_t const uSetFlag = ctx_.tx.getFieldU32 (sfSetFlag);
    std::uint32_t const uClearFlag = ctx_.tx.getFieldU32 (sfClearFlag);

    JLOG(j_.trace()) << "SetKYC doApply!";
    //
    // KYC Validated
    //
    if (uSetFlag == kycfValidated)
    {
        JLOG(j_.trace()) << "set KYC Validated";
        uFlagsOut   |= lsfKYCValidated;
    }
    else if (uClearFlag == kycfValidated)
    {
        JLOG(j_.trace()) << "unset KYC Validated";
        uFlagsOut   &= ~lsfKYCValidated;
    }

    auto KYCObject = sle->peekFieldObject(sfKYC);

    //
    // KYC Verification
    //
    if (ctx_.tx.isFieldPresent (sfKYCVerifications))
    {
        // TODO jrojek: get acc object from ledger, figure out unique list, setfieldv128(sfKYCVerifications, verifications);
        STVector128 newVerifications = ctx_.tx.getFieldV128 (sfKYCVerifications);
        // STVector128 existingVerifications = KYCObject.getFieldV128(sfKYCVerifications);

        if (newVerifications.size() == 0)
        {
            JLOG(j_.trace()) << "clear verifications array";
            KYCObject.makeFieldAbsent (sfKYCVerifications);
        }
        else
        {
            JLOG(j_.trace()) << "set verifications array";
            KYCObject.setFieldV128 (sfKYCVerifications, newVerifications);
        }

    }

    KYCObject.setFieldU32 (sfKYCTime, view().parentCloseTime().time_since_epoch().count());
    sle->setFieldObject (sfKYC, KYCObject);

    if (uFlagsIn != uFlagsOut)
        sle->setFieldU32 (sfFlags, uFlagsOut);

    return tesSUCCESS;
}

}
