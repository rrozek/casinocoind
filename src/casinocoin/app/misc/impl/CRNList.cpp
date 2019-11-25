//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2015 Ripple Labs Inc.

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
    2018-05-17  ajochems        Initial version
*/
//==============================================================================

#include <casinocoin/app/misc/CRNList.h>
#include <casinocoin/basics/Slice.h>
#include <casinocoin/basics/StringUtilities.h>
#include <casinocoin/json/json_reader.h>
#include <beast/core/detail/base64.hpp>
#include <boost/regex.hpp>

namespace casinocoin {

CRNList::CRNList (
    TimeKeeper& timeKeeper,
    beast::Journal j)
    : timeKeeper_ (timeKeeper)
    , j_ (j)
{
}

CRNList::~CRNList()
{
}

bool
CRNList::load (
    std::vector<std::string> const& configKeys)
{
    static boost::regex const re (
        "[[:space:]]*"            // skip leading whitespace
        "([[:alnum:]]+)"          // node identity
        "(?:"                     // begin optional comment block
        "[[:space:]]+"            // (skip all leading whitespace)
        "(?:"                     // begin optional comment
        "(.*[^[:space:]]+)"       // the comment
        "[[:space:]]*"            // (skip all trailing whitespace)
        ")?"                      // end optional comment
        ")?"                      // end optional comment block
    );

    JLOG (j_.debug()) << "Loading configured trusted CRN public keys";

    std::size_t count = 0;

    for (auto const& n : configKeys)
    {
        JLOG (j_.trace()) << "Processing '" << n << "'";

        boost::smatch match;
        if (!boost::regex_match (n, match, re))
        {
            JLOG (j_.error()) <<
                "Malformed entry: '" << n << "'";
            return false;
        }

        boost::optional<PublicKey> publicKey = parseBase58<PublicKey>(TokenType::TOKEN_NODE_PUBLIC, match[1]);

        JLOG (j_.info()) << "Loading CRN " << match[1].str() << " / " << match[2].str();
        if (!(toBase58(TokenType::TOKEN_NODE_PUBLIC, *publicKey).length() > 0))
        {
            JLOG (j_.error()) << "Invalid node identity: " << toBase58(TokenType::TOKEN_NODE_PUBLIC, *publicKey);
            return false;
        }

        crnList_.push_back({match[1], match[2]});
        ++count;
    }

    JLOG (j_.info()) << "Loaded " << count << " CRN Public Keys from local file.";
    return true;
}

bool
CRNList::listed (
    PublicKey const& identity) const
{
    bool found = false;
    std::string searchNode = toBase58(TokenType::TOKEN_NODE_PUBLIC, identity);
    for(size_t i=0; i<crnList_.size(); i++)
    {
        if(crnList_[i].publicKey == searchNode)
        {
            found = true;
            break;
        }
    }
    return found;
}

void
CRNList::refreshNodeOnList (
    std::string const& publicKeyString,
    std::string const& domainName,
    bool const& enabled)
{
    auto const publicKey = parseBase58<PublicKey>(TokenType::TOKEN_NODE_PUBLIC, publicKeyString);
    JLOG (j_.debug()) << "refreshNodeOnList: " << publicKeyString;

    bool nodeListed = CRNList::listed(*publicKey);
    JLOG (j_.debug()) << "Node: " << publicKeyString << " Listed: " << nodeListed;  

    if(!nodeListed && enabled)
    {
        // add node to list
        crnList_.push_back({publicKeyString, domainName});
        JLOG (j_.debug()) << "Add Node: " << publicKeyString;
    }
    else if(nodeListed && !enabled)
    {
        // remove node from list
        auto it = std::find_if(crnList_.begin(), crnList_.end(), boost::bind(&CRNListItem::publicKey, _1) == publicKeyString);
        if (it != crnList_.end())
            crnList_.erase(it);
        JLOG (j_.debug()) << "Remove Node: " << publicKeyString;
    }
    else if(nodeListed && enabled)
    {
        // update listed node
        auto it = std::find_if(crnList_.begin(), crnList_.end(), boost::bind(&CRNListItem::publicKey, _1) == publicKeyString);
        if (it != crnList_.end())
            *it = {publicKeyString, domainName};
        JLOG (j_.debug()) << "Update Node: " << publicKeyString;
    }
    JLOG (j_.info()) << "CRN Public Keys List size: " << crnList_.size();
}

Json::Value
CRNList::getJson() const
{
    Json::Value jrr(Json::arrayValue);
    {
        for (CRNListItem const& node : crnList_)
        {
            Json::Value& v = jrr.append(Json::objectValue);
            v[jss::crn_public_key] = node.publicKey;
            v[jss::crn_domain_name] = node.domainName;
            
        }
    }
    return jrr;
}

} // casinocoin
