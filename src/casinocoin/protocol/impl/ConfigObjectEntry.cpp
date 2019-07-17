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


#include <casinocoin/protocol/ConfigObjectEntry.h>
#include <casinocoin/protocol/Serializer.h>

#include <casinocoin/ledger/ReadView.h>

#include <casinocoin/json/json_reader.h>
#include <casinocoin/json/json_writer.h>
#include <stdexcept>

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
    aMap.insert(ConfigObjectEntry::bimap_string_type::value_type("Blacklist_Signer", ConfigObjectEntry::Blacklist_Signer));
    return aMap;
}

ConfigObjectEntry::ConfigObjectEntry()
    : mId(0)
    , mType(Invalid)
    , mJournal(beast::Journal::getNullSink())
{

}

ConfigObjectEntry::ConfigObjectEntry(beast::Journal const& journal)
    : mId(0)
    , mType(Invalid)
    , mJournal(journal)
{}

ConfigObjectEntry::~ConfigObjectEntry()
{
    for (std::size_t i = 0; i < mData.size(); ++i)
    {
        delete mData[i];
    }
    mData.clear();
}

ConfigObjectEntry::ConfigObjectEntry(const ConfigObjectEntry &other)
    : mId(other.mId)
    , mType(other.mType)
    , mData(other.mData.size())
    , mJournal(other.mJournal)
{
    for (std::size_t i = 0; i < other.mData.size(); ++i)
    {
        mData[i] = other.mData[i]->clone();
    }
}

ConfigObjectEntry& ConfigObjectEntry::operator=(ConfigObjectEntry const& other)
{
    mId = other.mId;
    mType = other.mType;
    mData.resize(other.mData.size());
    for (std::size_t i = 0; i < other.mData.size(); ++i)
    {
        mData[i] = other.mData[i]->clone();
    }
    return *this;
}

bool ConfigObjectEntry::fromJson(Json::Value const& data)
{
    if (!data.isObject())
        return false;

    mId = data.get(sfConfigID.getName(), Json::nullValue).asInt();
    try
    {
        mType = typeMap.left.at(data.get(sfConfigType.getName(),Json::nullValue).asString());
    }
    catch(std::out_of_range e)
    {
        JLOG(mJournal.info())
                << "ConfigObjectEntry::fromJson exception: " << e.what()
                << " when trying to map " << data.get(sfConfigType.getName(),Json::nullValue).asString()
                << " to known types. aborting.";
        return false;
    }


    Json::Value jvSrcArray = data.get(sfConfigData.getName(), Json::arrayValue);
    if (!jvSrcArray.isArray())
        return false;

    for (Json::UInt index = 0; index < jvSrcArray.size(); index++)
    {
        DataDescriptorInterface* pItem = nullptr;
        switch(mType)
        {
        case KYC_Signer:
            pItem = new KYC_SignerDescriptor(mJournal);
            break;
        case Message_PubKey:
            pItem = new Message_PubKeyDescriptor(mJournal);
            break;
        case Token:
            pItem = new TokenDescriptor(mJournal);
            break;
        case Blacklist_Signer:
            pItem = new Blacklist_SignerDescriptor(mJournal);
            break;
        case Invalid:
            return false;
        }

        if (pItem == nullptr || !pItem->fromJson(jvSrcArray[index]))
        {
            JLOG(mJournal.info()) << "ConfigObjectEntry::fromJson pItem " << pItem << " failed to parse json";
            continue;
        }
        mData.push_back(pItem);
    }
    return true;
}

bool ConfigObjectEntry::toJson(Json::Value& result) const
{
    result[sfConfigID.getName()] = mId;
    try
    {
        result[sfConfigType.getName()] = typeMap.right.at(mType);
    }
    catch(std::out_of_range e)
    {
        JLOG(mJournal.info())
                << "ConfigObjectEntry::toJson exception: " << e.what()
                << " when trying to map " << mType
                << " to string type. aborting.";
        return false;
    }

    Json::Value jvData(Json::arrayValue);
    for (const DataDescriptorInterface* pItem : mData)
    {
        Json::Value jvObject(Json::objectValue);
        if (!pItem->toJson(jvObject))
        {
            JLOG(mJournal.info()) << "ConfigObjectEntry::toJson failed to create json";
            continue;
        }
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

    JLOG(mJournal.debug()) << "ConfigObjectEntry::fromBytes inputSize " <<inputSize;
    std::string stringified;
    stringified.resize(inputSize);
    int outBufSize = LZ4_decompress_safe(reinterpret_cast<const char*>(&data.front()),
                                         &stringified.front(),
                                         data.size() - SIZE_AMEND,
                                         stringified.size());
    if (outBufSize <= 0)
    {
        JLOG(mJournal.info()) << "ConfigObjectEntry::fromBytes invalid outBufSize " <<outBufSize;
        return false;
    }
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

KYC_SignerDescriptor::KYC_SignerDescriptor(beast::Journal const& journal)
    : DataDescriptorInterface(journal)
{}

DataDescriptorInterface* KYC_SignerDescriptor::clone() const
{
    return new KYC_SignerDescriptor(*this);
}

bool KYC_SignerDescriptor::fromJson(Json::Value const& data)
{
    if (!data.isObject())
        return false;

    if (!data.isMember(jss::account))
        return false;

    if (!to_issuer(kycSigner, data[jss::account].asString()))
        return false;
    return true;
}

bool KYC_SignerDescriptor::toJson(Json::Value &result) const
{
    result[jss::account] = toBase58(kycSigner);
    return true;
}

Message_PubKeyDescriptor::Message_PubKeyDescriptor(beast::Journal const& journal)
    : DataDescriptorInterface(journal)
{}

DataDescriptorInterface* Message_PubKeyDescriptor::clone() const
{
    return new Message_PubKeyDescriptor(*this);
}

bool Message_PubKeyDescriptor::fromJson(Json::Value const& data)
{

    if (!data.isObject())
        return false;

    if (!data.isMember(jss::public_key_hex))
        return false;

    auto unHexedPubKey = strUnHex(data[jss::public_key_hex].asString());
    if (!unHexedPubKey.second)
        return false;
    pubKey = PublicKey(Slice(unHexedPubKey.first.data(), unHexedPubKey.first.size()));

    return true;
}

bool Message_PubKeyDescriptor::toJson(Json::Value &result) const
{

    result[jss::public_key_hex] = strHex(pubKey.begin(), pubKey.end());
    return true;
}

TokenDescriptor::TokenDescriptor(beast::Journal const& journal)
    : DataDescriptorInterface(journal)
{}

TokenDescriptor::TokenDescriptor(TokenDescriptor const& other)
    : DataDescriptorInterface(other.mJournal)
{
    fullName = other.fullName;
    flags = other.flags;
    website = other.website;
    contactEmail = other.contactEmail;
    iconURL = other.iconURL;
    apiEndpoint = other.apiEndpoint;
    extraFeeFactor = other.extraFeeFactor;

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
    extraFeeFactor = other.extraFeeFactor;

    Serializer s;
    other.totalSupply.add(s);
    SerialIter sit(s.slice());
    totalSupply = STAmount(sit, sfGeneric);

    return *this;
}

DataDescriptorInterface* TokenDescriptor::clone() const
{
    return new TokenDescriptor(*this);
}

bool TokenDescriptor::fromJson(Json::Value const& data)
{
    if (!data.isObject())
        return false;

    fullName = data[jss::fullName].asString();
    flags = static_cast<TokenFlags>(data[jss::flags].asInt());
    website = data[jss::website].asString();
    contactEmail = data[jss::contactEmail].asString();
    iconURL = data[jss::iconURL].asString();
    apiEndpoint = data[jss::apiEndpoint].asString();
    if (data.isMember(jss::extraFeeFactor))
        extraFeeFactor = data[jss::extraFeeFactor].asInt();
    else
        extraFeeFactor = 0u;

    Json::Value combinedAmountObj(Json::objectValue);
    combinedAmountObj[jss::value] = data[jss::totalSupply];
    combinedAmountObj[jss::currency] = data[jss::token];
    combinedAmountObj[jss::issuer] = data[jss::issuer];

    JLOG(mJournal.debug()) << "TokenDescriptor::fromJson combinedAmountObj" << combinedAmountObj;

    return amountFromJsonNoThrow(totalSupply, combinedAmountObj);
}

bool TokenDescriptor::toJson(Json::Value &result) const
{
    if (totalSupply.isDefault())
    {
        JLOG(mJournal.info()) << "TokenDescriptor::toJson totalSupply is not defined";
        return false;
    }
    Json::Value combinedAmountObj(Json::objectValue);
    combinedAmountObj = totalSupply.getJson(0);
    result[jss::totalSupply] = combinedAmountObj[jss::value];
    result[jss::token] = combinedAmountObj[jss::currency];
    result[jss::issuer] = combinedAmountObj[jss::issuer];

    result[jss::extraFeeFactor] = extraFeeFactor;

    result[jss::fullName] = fullName;
    result[jss::flags] = flags;
    result[jss::website] = website;
    result[jss::contactEmail] = contactEmail;
    result[jss::iconURL] = iconURL;
    result[jss::apiEndpoint] = apiEndpoint;
    return true;
}

Blacklist_SignerDescriptor::Blacklist_SignerDescriptor(beast::Journal const& journal)
    : DataDescriptorInterface(journal)
{}

DataDescriptorInterface* Blacklist_SignerDescriptor::clone() const
{
    return new Blacklist_SignerDescriptor(*this);
}

bool Blacklist_SignerDescriptor::fromJson(Json::Value const& data)
{
    if (!data.isObject())
        return false;

    if (!data.isMember(jss::account))
        return false;

    if (!to_issuer(blacklistSigner, data[jss::account].asString()))
        return false;
    return true;
}

bool Blacklist_SignerDescriptor::toJson(Json::Value &result) const
{
    result[jss::account] = toBase58(blacklistSigner);
    return true;
}

DataDescriptorInterface::DataDescriptorInterface(beast::Journal const& journal)
    : mJournal(journal)
{}

/////////////////////////////////////////////////////////////////
//////////////////////////// Helpers ////////////////////////////
/////////////////////////////////////////////////////////////////

std::pair<TER, boost::optional<TokenDescriptor>>
isWLTCompliant(const STAmount &amount,
               ConfigObjectEntry const& tokenConfig,
               boost::optional<beast::Journal> j)
{
    boost::optional<TokenDescriptor> theToken;
    if (isCSC(amount))
    {
        return {tesSUCCESS, theToken};
    }
    if (tokenConfig.getType() != ConfigObjectEntry::Token)
    {
        if (j) { JLOG((*j).warn()) << "isWLTCompliant() non-Token config object passed"; }
        return {tefFAILURE, theToken};
    }

    theToken = getWLT(amount, tokenConfig, j);
    if (theToken)
        return {tesSUCCESS, theToken};

    return {tefNOT_WLT, theToken};
}

boost::optional<TokenDescriptor>
getWLT(const STAmount &amount,
       ConfigObjectEntry const& tokenConfig,
       boost::optional<beast::Journal> j)
{
    boost::optional<TokenDescriptor> theToken;

    auto const& definedTokens = tokenConfig.getData();
    for (auto const entry : definedTokens)
    {
        const TokenDescriptor* token = static_cast<const TokenDescriptor*>(entry);
        try
        {
            if (token->totalSupply.issue() == amount.issue()
                    && token->totalSupply >= amount)
            {
                if (j) { JLOG((*j).debug()) << "getWLT() found matching entry, return OK"; }
                theToken = *token;
                return theToken;
            }
        }
        catch( std::runtime_error const& err)
        {
            if (j) { JLOG((*j).warn()) << "getWLT() caught exception. what: " << err.what(); }
            return theToken;
        }
    }

    if (j) { JLOG((*j).info()) << "getWLT() not compliant"
                        << " token: " << to_string(amount.issue().currency)
                        << " issuer: " << toBase58(amount.issue().account); }
    return theToken;
}

} // casinocoin
