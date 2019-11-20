//------------------------------------------------------------------------------
/*
    This file is part of casinocoind: https://github.com/casinocoin/casinocoind
    Copyright (c) 2017 - 2019 CasinoCoin Foundation.

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
    2018-02-15  jrojek        Created
*/
//==============================================================================


#ifndef CONFIGOBJECTENTRY_H
#define CONFIGOBJECTENTRY_H

#include <casinocoin/basics/StringUtilities.h>

#include <casinocoin/protocol/AccountID.h>
#include <casinocoin/protocol/PublicKey.h>
#include <casinocoin/protocol/STAmount.h>
#include <casinocoin/protocol/TER.h>
#include <casinocoin/protocol/JsonFields.h>
#include <casinocoin/ledger/ReadView.h>
#include <casinocoin/json/json_value.h>
#include <casinocoin/basics/Log.h>

#include <boost/bimap.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/optional.hpp>

namespace casinocoin
{

struct TokenDescriptor;
struct KYC_SignerDescriptor;
struct Message_PubKeyDescriptor;
struct Blacklist_SignerDescriptor;
struct CRN_SettingsDescriptor;

struct DataDescriptorInterface
{
    DataDescriptorInterface(beast::Journal const& journal);
    DataDescriptorInterface() = default;
    DataDescriptorInterface(DataDescriptorInterface const& other) = default;
    DataDescriptorInterface& operator=(DataDescriptorInterface const& other) = default;
    virtual ~DataDescriptorInterface() {}

    virtual DataDescriptorInterface* clone() const = 0;

    virtual bool fromJson(Json::Value const& data) = 0;
    virtual bool toJson(Json::Value& result) const = 0;

protected:
    beast::Journal mJournal;
};

class ConfigObjectEntry
{
public:
    enum Type
    {
        Invalid             = 0,
        KYC_Signer          = 1,
        Message_PubKey      = 2,
        Token               = 3,
        Blacklist_Signer    = 4,
        CRN_Settings        = 5
    };

    using bimap_string_type = boost::bimap<std::string, Type>;
    static bimap_string_type typeMap;
    static bimap_string_type initializeTypeMap();

    ConfigObjectEntry();
    ConfigObjectEntry(beast::Journal const& journal);
    virtual ~ConfigObjectEntry();

    ConfigObjectEntry(ConfigObjectEntry const& other);
    ConfigObjectEntry& operator=(ConfigObjectEntry const& other);

    bool fromJson(Json::Value const& data);
    bool toJson(Json::Value& result) const;

    bool fromBytes(Blob const& data);
    bool toBytes(Blob& result) const;

    uint32_t getId() const { return mId; }
    Type getType() const { return mType; }
    const std::vector<const DataDescriptorInterface*>& getData() const { return mData; }

private:

    uint32_t mId;
    Type mType;
    std::vector<const DataDescriptorInterface*> mData;
    beast::Journal mJournal;
};

struct TokenDescriptor : public DataDescriptorInterface
{
    enum TokenFlags
    {
        KYCRequired     = 0x0001,
        AuthRequired    = 0x0002,
        Private         = 0x0004
    };

    TokenDescriptor(beast::Journal const& journal);
    TokenDescriptor(TokenDescriptor const& other);
    TokenDescriptor& operator=(const TokenDescriptor& other);

    DataDescriptorInterface* clone() const override;

    bool fromJson(Json::Value const& data) override;
    bool toJson(Json::Value& result) const override;

    std::string fullName;
    STAmount totalSupply;
    uint32_t extraFeeFactor = 0u; /*percent of network fee*/
    TokenFlags flags;
    std::string website;
    std::string contactEmail;
    std::string iconURL;
    std::string apiEndpoint;
};

struct KYC_SignerDescriptor : public DataDescriptorInterface
{
    KYC_SignerDescriptor(beast::Journal const& journal);

    DataDescriptorInterface* clone() const override;

    bool fromJson(Json::Value const& data) override;
    bool toJson(Json::Value& result) const override;

    AccountID kycSigner;
};

struct Message_PubKeyDescriptor : public DataDescriptorInterface
{
    Message_PubKeyDescriptor(beast::Journal const& journal);

    DataDescriptorInterface* clone() const override;

    bool fromJson(Json::Value const& data) override;
    bool toJson(Json::Value& result) const override;

    PublicKey pubKey;
};

struct Blacklist_SignerDescriptor : public DataDescriptorInterface
{
    Blacklist_SignerDescriptor(beast::Journal const& journal);
    
    DataDescriptorInterface* clone() const override;

    bool fromJson(Json::Value const& data) override;
    bool toJson(Json::Value& result) const override;

    AccountID blacklistSigner;
};

struct CRN_SettingsDescriptor : public DataDescriptorInterface
{
    CRN_SettingsDescriptor(beast::Journal const& journal);
    CRN_SettingsDescriptor(CRN_SettingsDescriptor const& other);
    CRN_SettingsDescriptor& operator=(const CRN_SettingsDescriptor& other);

    DataDescriptorInterface* clone() const override;

    bool fromJson(Json::Value const& data) override;
    bool toJson(Json::Value& result) const override;

    uint32_t foundationFeeFactor = 0u; /* percentage of fees for the foundation */
    PublicKey foundationFeesPublicKey;
    bool activated;
};

//------------------------------------------------------------------------------
//
// WLT Helpers
//
//------------------------------------------------------------------------------
std::pair<TER, boost::optional<TokenDescriptor>>
isWLTCompliant(STAmount const& amount,
               ConfigObjectEntry const& tokenConfig,
               boost::optional<beast::Journal> j = boost::optional<beast::Journal>());

boost::optional<TokenDescriptor>
getWLT(STAmount const& amount,
       ConfigObjectEntry const& tokenConfig,
       boost::optional<beast::Journal> j = boost::optional<beast::Journal>());

//------------------------------------------------------------------------------
//
// CRN Helpers
//
//------------------------------------------------------------------------------
bool 
isCRNRoundsActivated(std::shared_ptr<ReadView const> const& ledger,
                     boost::optional<beast::Journal> j = boost::optional<beast::Journal>());

boost::optional<CRN_SettingsDescriptor>
getCRNSettings(std::shared_ptr<ReadView const> const& ledger,
                     boost::optional<beast::Journal> j = boost::optional<beast::Journal>());

} // casinocoin

#endif // CONFIGOBJECTENTRY_H
