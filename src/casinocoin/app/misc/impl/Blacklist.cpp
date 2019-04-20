//------------------------------------------------------------------------------
/*
    This file is part of casinocoind: https://github.com/casinocoin/casinocoind
    Copyright (c) 2019 CasinoCoin Foundation

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
    2019-04-04  ajochems        Initial version
*/
//==============================================================================

#include <casinocoin/app/misc/Blacklist.h>
#include <casinocoin/basics/Slice.h>
#include <casinocoin/basics/StringUtilities.h>
#include <casinocoin/json/json_reader.h>
#include <beast/core/detail/base64.hpp>
#include <boost/regex.hpp>

namespace casinocoin {

Blacklist::Blacklist (
    TimeKeeper& timeKeeper,
    beast::Journal j)
    : timeKeeper_ (timeKeeper)
    , j_ (j)
{
}

Blacklist::~Blacklist()
{
}

bool
Blacklist::listed (std::string const& accountID) const
{
    bool found = false;
    for(size_t i=0; i<blacklistVector_.size(); i++)
    {
        if(blacklistVector_[i].accountID == accountID)
        {
            found = true;
            break;
        }
    }
    return found;
}

BlacklistItem
Blacklist::getAccount (std::string const& accountID ) const
{
    BlacklistItem account;
    for(size_t i=0; i<blacklistVector_.size(); i++)
    {
        if(blacklistVector_[i].accountID == accountID)
        {
            account = blacklistVector_[i];
            break;
        }
    }
    return account;
}

void
Blacklist::refreshAccountOnList (
        std::string const& accountID,
        std::string const& signature,
        std::string const& publicKeySigner,
        std::string const& creationDate,
        std::string const& lastUpdatedDate,
        bool const& enabled)
{
    JLOG (j_.debug()) << "refreshAccountOnList: " << accountID;

    // check if signature is valid
    auto unHexedPubKey = strUnHex(publicKeySigner);
    if (!unHexedPubKey.second)
    {
        JLOG (j_.error()) << "Account to refresh " << accountID << " has an invalid Public Key Hex value" << publicKeySigner;  
        return;
    }

    if (publicKeyType(makeSlice(unHexedPubKey.first)))
    {
        auto unHexedSignature = strUnHex(signature);
        if (!unHexedSignature.second)
        {
            JLOG (j_.error()) << "Account to refresh " << accountID << " has an invalid Signature Hex value" << signature;  
            return;
        }

        bool validSignature = verify(
          PublicKey(makeSlice(unHexedPubKey.first)),
          makeSlice(strHex(accountID)),
          makeSlice(unHexedSignature.first));
        
        if(!validSignature)
        {
            JLOG (j_.error()) << "Account to refresh " << accountID << " has an invalid Signature" << signature;  
            return;
        }
    }
    else
    {
        JLOG (j_.error()) << "Account to refresh " << accountID << " has an invalid Public Key" << publicKeySigner;  
        return;
    }
    

    // check if account is already listed
    bool accountListed = Blacklist::listed(accountID);
    JLOG (j_.debug()) << "Account: " << accountID << " Listed: " << accountListed;

    if(!accountListed && enabled)
    {
        // add account to list
        blacklistVector_.push_back({accountID, signature, publicKeySigner, creationDate, lastUpdatedDate, enabled});
        JLOG (j_.debug()) << "Added AccountID: " << accountID;
    }
    else if(accountListed && !enabled)
    {
        // remove account from list if it exists
        auto it = std::find_if(blacklistVector_.begin(), blacklistVector_.end(), boost::bind(&BlacklistItem::accountID, _1) == accountID);
        if (it != blacklistVector_.end())
            blacklistVector_.erase(it);
        JLOG (j_.debug()) << "Removed AccountID: " << accountID;
    }
    else if(accountListed && enabled)
    {
        // update listed account
        auto it = std::find_if(blacklistVector_.begin(), blacklistVector_.end(), boost::bind(&BlacklistItem::accountID, _1) == accountID);
        if (it != blacklistVector_.end())
            *it = {accountID, signature, publicKeySigner, creationDate, lastUpdatedDate, enabled};
        JLOG (j_.debug()) << "Updated AccountID: " << accountID;
    }
}

Json::Value
Blacklist::getJson() const
{
    Json::Value jrr(Json::arrayValue);
    {
        for (BlacklistItem const& item : blacklistVector_)
        {
            Json::Value& v = jrr.append(Json::objectValue);
            v[jss::account_id] = item.accountID;
            v[jss::signature] = item.signature;
            v[jss::public_key_hex] = item.publicKeySigner;
            v[jss::date] = item.lastUpdatedDate;
        }
    }
    return jrr;
}

size_t 
Blacklist::getSize() const
{
    return blacklistVector_.size();
}

} // casinocoin
