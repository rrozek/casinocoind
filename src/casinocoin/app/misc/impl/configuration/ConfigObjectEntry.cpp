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


#include <casinocoin/app/misc/configuration/ConfigObjectEntry.h>
#include <casinocoin/protocol/Serializer.h>
#include <casinocoin/json/json_reader.h>
#include <casinocoin/json/json_writer.h>

#include <lz4/lib/lz4.h>

namespace casinocoin
{

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
{}

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
    mType = typeMap.left.at(data.get(sfConfigType.getName(),Json::nullValue).asString());

    Json::Value jvSrcArray = data.get(sfConfigData.getName(), Json::arrayValue);
    if (!jvSrcArray.isArray())
        return false;

    for (Json::UInt index = 0; index < jvSrcArray.size(); index++)
    {
        DataDescriptorInterface* pItem = nullptr;
        switch(mType)
        {
        case KYC_Signer: // array of base58 accountIds or single accountId
            pItem = new KYC_SignerDescriptor;
            break;
        case Message_PubKey: //array of hex pubkeys or single hex pubkey
            pItem = new Message_PubKeyDescriptor;
            break;
        case Token: // array of jsonified  STAmounts {"value":123.45, "currency":"TST", "issuer":<base58AccountID>}
            pItem = new TokenDescriptor;
            break;
        case Invalid:
            return false;
        }

        if (!pItem->fromJson(jvSrcArray[index]))
            continue;
        mData.push_back(pItem);
    }
    return true;
}

bool ConfigObjectEntry::toJson(Json::Value& result) const
{
    result[sfConfigID.getName()] = mId;
    result[sfConfigType.getName()] = typeMap.right.at(mType);

    Json::Value jvData(Json::arrayValue);
    for (DataDescriptorInterface* pItem : mData)
    {
        Json::Value jvObject(Json::objectValue);
        pItem->toJson(jvObject);
        jvData.append(jvObject);
    }

    result[sfConfigData.getName()] = jvData;
    return true;
}

bool ConfigObjectEntry::fromBytes(Blob const& data)
{
    const uint8_t SIZE_AMEND = 4;
    int inputSize = 0;
    auto reverseIter = data.rbegin();
    for (uint8_t i = 1; i <= SIZE_AMEND; ++i, ++reverseIter)
    {
        inputSize += *reverseIter << ((SIZE_AMEND-i)*8);
    }

    std::string stringified;
    stringified.resize(inputSize);
    int outBufSize = LZ4_decompress_safe(reinterpret_cast<const char*>(&data.front()), &stringified.front(), data.size() - SIZE_AMEND, stringified.size());
    if (outBufSize <= 0)
        return false;
    stringified.resize(outBufSize);

    Json::Reader reader;
    Json::Value jvData;
    if (!reader.parse(stringified,jvData))
        return false;
    return fromJson(jvData);
}

bool ConfigObjectEntry::toBytes(Blob& result) const
{
    const uint8_t SIZE_AMEND = 4;
    Json::Value data(Json::objectValue);
    if (!toJson(data))
        return false;
    Json::FastWriter writer;
    std::string stringified = writer.write(data);
    int outBufSize = LZ4_compressBound(stringified.size());
    result.resize(outBufSize);
    int finalResultSize = LZ4_compress_default(stringified.data(), reinterpret_cast<char*>(&result.front()), stringified.size(), result.size());
    if (finalResultSize <= 0)
        return false;
    result.resize(finalResultSize);
    for (uint8_t i = 0; i < SIZE_AMEND; ++i)
    {
        result.push_back(stringified.size() >> (i*8) & 0xFF);
    }
    return true;
}

KYC_SignerDescriptor::KYC_SignerDescriptor() {}

KYC_SignerDescriptor::KYC_SignerDescriptor(KYC_SignerDescriptor const& other)
{
    kycSigner = other.kycSigner;
}

KYC_SignerDescriptor& KYC_SignerDescriptor::operator=(KYC_SignerDescriptor const& other)
{
    kycSigner = other.kycSigner;
    return *this;
}

bool KYC_SignerDescriptor::fromJson(Json::Value const& data)
{
    if (!data.isObject())
        return false;

    if (!to_issuer(kycSigner, data["KYC_Signer"].asString()))
        return false;
    return true;
}

bool KYC_SignerDescriptor::toJson(Json::Value &result) const
{
    result["KYC_Signer"] = toBase58(kycSigner);
    return true;
}

Message_PubKeyDescriptor::Message_PubKeyDescriptor() {}

Message_PubKeyDescriptor::Message_PubKeyDescriptor(Message_PubKeyDescriptor const& other)
{
    pubKey = other.pubKey;
}

Message_PubKeyDescriptor& Message_PubKeyDescriptor::operator=(Message_PubKeyDescriptor const& other)
{
    pubKey = other.pubKey;
    return *this;
}

bool Message_PubKeyDescriptor::fromJson(Json::Value const& data)
{
    if (!data.isObject())
        return false;

    auto unHexedPubKey = strUnHex(data["Message_PubKey"].asString());
    if (!unHexedPubKey.second)
        return false;
    pubKey = PublicKey(Slice(unHexedPubKey.first.data(), unHexedPubKey.first.size()));

    return true;
}

bool Message_PubKeyDescriptor::toJson(Json::Value &result) const
{
    result["Message_PubKey"] = strHex(pubKey.data(), pubKey.size());
    return true;
}

TokenDescriptor::TokenDescriptor() {}

TokenDescriptor::TokenDescriptor(TokenDescriptor const& other)
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

TokenDescriptor& TokenDescriptor::operator=(TokenDescriptor const& other)
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

bool TokenDescriptor::fromJson(Json::Value const& data)
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

bool TokenDescriptor::toJson(Json::Value &result) const
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

} // casinocoin
