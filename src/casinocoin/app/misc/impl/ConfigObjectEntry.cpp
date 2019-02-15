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
    2018-05-15  jrojek          Created
*/
//==============================================================================


#include <casinocoin/app/misc/ConfigObjectEntry.h>
#include <casinocoin/protocol/Serializer.h>

#include <casinocoin/json/json_writer.h>

namespace casinocoin
{


TokenDescription::TokenDescription() {}

TokenDescription::TokenDescription(TokenDescription const& other)
{
    fullName = other.fullName;
    flags = other.flags;
    website = other.website;
    contactEmail = other.contactEmail;
    iconURL = other.iconURL;
    apiEndpoint = other.apiEndpoint;

    Serializer s;
    other.totalSupply.add(s);
    SerialIter sit(s.slice());
    totalSupply = STAmount(sit, sfGeneric);
}

TokenDescription& TokenDescription::operator=(TokenDescription const& other)
{
    fullName = other.fullName;
    flags = other.flags;
    website = other.website;
    contactEmail = other.contactEmail;
    iconURL = other.iconURL;
    apiEndpoint = other.apiEndpoint;

    Serializer s;
    other.totalSupply.add(s);
    SerialIter sit(s.slice());
    totalSupply = STAmount(sit, sfGeneric);
    return *this;
}

bool TokenDescription::fromJson(Json::Value const& data)
{
    if (!data.isObject())
        return false;

    fullName = data["fullName"].asString();
    flags = static_cast<TokenFlags>(data["flags"].asInt());
    website = data["website"].asString();
    contactEmail = data["contactEmail"].asString();
    iconURL = data["iconURL"].asString();
    apiEndpoint = data["apiEndpoint"].asString();
    if (data.isMember("totalSupply"))
        amountFromJsonNoThrow(totalSupply, data["totalSupply"]);
    else
        amountFromJsonNoThrow(totalSupply, data);

    return true;
}

bool TokenDescription::toJson(Json::Value &result) const
{
    result["totalSupply"] = totalSupply.getJson(0);
    result["fullName"] = fullName;
    result["flags"] = flags;
    result["website"] = website;
    result["contactEmail"] = contactEmail;
    result["iconURL"] = iconURL;
    result["apiEndpoint"] = apiEndpoint;
    return true;
}

ConfigObjectEntry::bimap_string_type ConfigObjectEntry::typeMap = initializeTypeMap();

ConfigObjectEntry::bimap_string_type ConfigObjectEntry::initializeTypeMap()
{
    ConfigObjectEntry::bimap_string_type aMap;
    aMap.insert(ConfigObjectEntry::bimap_string_type::value_type("Invalid", ConfigObjectEntry::Invalid));
    aMap.insert(ConfigObjectEntry::bimap_string_type::value_type("KYC_Signer", ConfigObjectEntry::KYC_Signer));
    aMap.insert(ConfigObjectEntry::bimap_string_type::value_type("Message_PubKey", ConfigObjectEntry::Message_PubKey));
    aMap.insert(ConfigObjectEntry::bimap_string_type::value_type("Token", ConfigObjectEntry::Token));
    return aMap;
}

    bool ConfigObjectEntry::fromBytes(ConfigObjectEntry& result, Blob const& data)
{
    return result.fromBytes(data);
}

bool ConfigObjectEntry::toBytes(Blob& result, ConfigObjectEntry const& data)
{
    return data.toBytes(result);
}

bool ConfigObjectEntry::fromJson(ConfigObjectEntry& result, Json::Value const& data)
{
    return result.fromJson(data);
}

bool ConfigObjectEntry::toJson(Json::Value& result, ConfigObjectEntry const& data)
{
    return data.toJson(result);
}

ConfigObjectEntry::ConfigObjectEntry()
    : mId(0)
    , mType(Invalid)
{}

ConfigObjectEntry::~ConfigObjectEntry()
{

}

ConfigObjectEntry::ConfigObjectEntry(const ConfigObjectEntry &other)
    : mId(other.mId)
    , mType(other.mType)
    , mData(other.mData)
{}

ConfigObjectEntry& ConfigObjectEntry::operator=(ConfigObjectEntry const& other)
{
    mId = other.mId;
    mType = other.mType;
    mData = other.mData;
    return *this;
}

bool ConfigObjectEntry::fromJson(Json::Value const& data)
{
    if (!data.isObject())
        return false;

    mId = data.get(sfConfigID.getName(), Json::nullValue).asInt();
    mType = typeMap.left[data.get(sfConfigType.getName(),Json::nullValue).asString()];

    switch(mType)
    {
    case KYC_Signer: // array of base58 accountIds or single accountId
    {
        Json::Value jvKYCAccounts = data.get(sfConfigData.getName(), Json::arrayValue);
        if (jvKYCAccounts.isArray())
        {
            for (Json::UInt index = 0; index < jvKYCAccounts.size(); index++)
            {
                AccountID acc;
                if (!to_issuer(acc, jvKYCAccounts[index].asString()))
                    continue;
                mData.kycAccountList.push_back(acc);
            }
        }
        else if (jvKYCAccounts.isString())
        {
            AccountID acc;
            if (to_issuer(acc, jvKYCAccounts.asString()))
                mData.kycAccountList.push_back(acc);
        }
        break;
    }
    case Message_PubKey: //array of hex pubkeys or single hex pubkey
    {
        Json::Value jvPubKeys = data.get(sfConfigData.getName(), Json::arrayValue);
        if (jvPubKeys.isArray())
        {
            for (Json::UInt index = 0; index < jvPubKeys.size(); index++)
            {
                auto unHexedPubKey = strUnHex(jvPubKeys[index].asString());
                if (!unHexedPubKey.second)
                    continue;
                PublicKey pubKey(Slice(unHexedPubKey.first.data(), unHexedPubKey.first.size()));
                mData.msgDestPubKey.push_back(pubKey);
            }
        }
        else if (jvPubKeys.isString())
        {
            auto unHexedPubKey = strUnHex(jvPubKeys.asString());
            if (unHexedPubKey.second)
            {
                PublicKey pubKey(Slice(unHexedPubKey.first.data(), unHexedPubKey.first.size()));
                mData.msgDestPubKey.push_back(pubKey);
            }
        }
        break;
    }
    case Token: // array of jsonified  STAmounts {"value":123.45, "currency":"TST", "issuer":<base58AccountID>}
    {
        Json::Value jvTokenArray = data.get(sfConfigData.getName(), Json::arrayValue);
        if (jvTokenArray.isArray())
        {
            for (Json::UInt index = 0; index < jvTokenArray.size(); index++)
            {
                TokenDescription token;
                if (!token.fromJson(jvTokenArray[index]))
                    continue;
                mData.tokenList.push_back(token);
            }
        }
        break;
    }
    case Invalid:
        return false;
    }
    return true;
}

bool ConfigObjectEntry::toJson(Json::Value& result) const
{
    result[sfConfigID.getName()] = mId;
    result[sfConfigType.getName()] = typeMap.right[sfConfigType.getName()];

    Json::Value jvData(Json::arrayValue);
    switch(mType)
    {
    case KYC_Signer: // array of base58 accountIds or single accountId
    {
        for (AccountID const& account : mData.kycAccountList)
        {
            Json::Value jvAccount(toBase58(account));
            jvData.append(jvAccount);
        }
        break;
    }
    case Message_PubKey: //array of hex pubkeys or single hex pubkey
    {
        for (PublicKey const& pubKey : mData.msgDestPubKey)
        {
            Json::Value jvPubKey(strHex(pubKey.data(), pubKey.size()));
            jvData.append(jvPubKey);
        }
        break;
    }
    case Token: // array of jsonified STAmounts {"value":123.45, "currency":"TST", "issuer":<base58AccountID>} + additional fields
    {
        for (TokenDescription const& token : mData.tokenList)
        {
            Json::Value jvToken(Json::objectValue);
            if (!token.toJson(jvToken))
                continue;
            jvData.append(jvToken);
        }
        break;
    }
    case Invalid:
        return false;
    }

    result[sfConfigData.getName()] = jvData;
    return true;
}

bool ConfigObjectEntry::fromBytes(Blob const& data)
{
    Json::Value jvData(data.data(), data.size());
    return fromJson(jvData);
}

bool ConfigObjectEntry::toBytes(Blob& result) const
{
    Json::Value data(Json::objectValue);
    if (!toJson(data))
        return false;
    Json::stream(data,
        [&result](auto const dataPointer, auto const dataSize)
        {
            Slice sli(dataPointer, dataSize);
            result = Blob(sli.data(), sli.size());
        });
    return true;
}

} // casinocoin
