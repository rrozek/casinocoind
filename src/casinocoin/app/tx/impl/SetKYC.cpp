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

    // KYCSet transaction is enabled via amendment
    if (!ctx.rules.enabled(featureKYC))
    {
        JLOG(j.info()) << "Feature KYC disabled.";
        return temDISABLED;
    }

    auto const uDstAccountID = tx.getAccountID (sfDestination);
    if (!uDstAccountID)
    {
        JLOG(j.trace()) << "Malformed transaction: " <<
            "Destination account not specified.";
        return temDST_NEEDED;
    }

    std::uint32_t const uSetFlag = tx.getFieldU32 (sfSetFlag);
    std::uint32_t const uClearFlag = tx.getFieldU32 (sfClearFlag);

    if ((uSetFlag != 0) && (uSetFlag == uClearFlag))
    {
        JLOG(j.info()) << "Malformed transaction: Set and clear same flag.";
        return temINVALID_FLAG;
    }

    auto const id = tx.getAccountID(sfAccount);
    if (id == zero)
        return temBAD_SRC_ACCOUNT;

    bool bIsTrusted = false;
    for ( const std::string& entry : ctx.app.config().KYCTrustedAccounts )
    {
        JLOG(j.info()) << "address: " << entry << " read from config";
        auto trustedAccountID = parseBase58<AccountID>(entry);
        if (!trustedAccountID)
        {
            JLOG(j.info()) << "address: " << entry << " seems to be invalid";
            continue;
        }
        if (id == trustedAccountID)
        {
            bIsTrusted = true;
            break;
        }
    }
    if (!bIsTrusted)
    {
        JLOG(j.info()) << "KYCSet tx can be only issued from trusted address";
        return temBAD_SRC_ACCOUNT;
    }

    if (uSetFlag == kycfValidated)
    {
        if (ctx.tx.isFieldPresent(sfKYCVerifications))
        {
            const STArray& kycVerifications = ctx.tx.getFieldArray(sfKYCVerifications);
            if (kycVerifications.size() == 0)
            {
                JLOG(j.info()) << "Cannot set KYC verified flag with empty Verifications array";
                return temMALFORMED;
            }
        }
        else
        {
            JLOG(j.info()) << "Cannot set KYC verified flag without Verifications array";
            return temMALFORMED;
        }
    }

    return preflight2(ctx);
}

TER
SetKYC::doApply ()
{
    JLOG(j_.info()) << "SetKYC doApply!";
    AccountID const uDstAccountID (ctx_.tx.getAccountID (sfDestination));

    // Open a ledger for editing.
    SLE::pointer sleDst = view().peek (keylet::account(uDstAccountID));

    if (!sleDst)
    {
        JLOG(j_.warn()) << "Destination account does not exist.";
        return tecNO_DST;
    }
    else
    {
        // Tell the engine that we are intending to change the destination
        // account.  The source account gets always charged a fee so it's always
        // marked as modified.
        view().update (sleDst);
    }

    std::uint32_t const uFlagsIn = sleDst->getFieldU32 (sfFlags);
    std::uint32_t uFlagsOut = uFlagsIn;

    std::uint32_t const uSetFlag = ctx_.tx.getFieldU32 (sfSetFlag);
    std::uint32_t const uClearFlag = ctx_.tx.getFieldU32 (sfClearFlag);

    //
    // KYC Validated
    //
    if (uSetFlag == kycfValidated)
    {
        JLOG(j_.info()) << "set KYC Validated";
        uFlagsOut   |= lsfKYCValidated;
    }
    else if (uClearFlag == kycfValidated)
    {
        JLOG(j_.info()) << "unset KYC Validated";
        uFlagsOut   &= ~lsfKYCValidated;
    }

    STObject ledgerDstKYCObject(sfKYC);
    if (sleDst->isFieldPresent(sfKYC))
    {
        ledgerDstKYCObject = sleDst->getFieldObject(sfKYC);
    }

    //
    // KYC Verification
    //
    if (ctx_.tx.isFieldPresent (sfKYCVerifications))
    {
        // TODO jrojek: get acc object from ledger, figure out unique list, setfieldv128(sfKYCVerifications, verifications);
        STVector128 newVerifications = ctx_.tx.getFieldV128 (sfKYCVerifications);
        // STVector128 existingVerifications = ledgerDstKYCObject.getFieldV128(sfKYCVerifications);

        if (newVerifications.size() == 0)
        {
            JLOG(j_.info()) << "clear verifications array";
            if (ledgerDstKYCObject.isFieldPresent(sfKYCVerifications))
                ledgerDstKYCObject.makeFieldAbsent (sfKYCVerifications);
        }
        else
        {
            JLOG(j_.info()) << "set verifications array";
            ledgerDstKYCObject.setFieldV128 (sfKYCVerifications, newVerifications);
        }

    }

    // Set flag KYCVerified with empty Verifications array is caught in preflight, so we can safely update ledger object here

    ledgerDstKYCObject.setFieldU32 (sfKYCTime, view().parentCloseTime().time_since_epoch().count());
    sleDst->setFieldObject (sfKYC, ledgerDstKYCObject);

    if (uFlagsIn != uFlagsOut)
        sleDst->setFieldU32 (sfFlags, uFlagsOut);

    return tesSUCCESS;
}

}
