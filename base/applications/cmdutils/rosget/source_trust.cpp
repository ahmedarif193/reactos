/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Authenticode and MSIX block-map validation for the WinGet source
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <mssip.h>

#include "source_trust.hpp"
#include "hash.hpp"
#include "util.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace rosget
{

namespace
{

constexpr DWORD P7xMagic = 0x58434b50;
constexpr const char *SpcIndirectDataObjectId = "1.3.6.1.4.1.311.2.1.4";
constexpr std::string_view ExpectedPackageName = "Microsoft.Winget.Source";
constexpr std::string_view ExpectedPublisher = "CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US";

struct CertificateDeleter
{
    void operator()(const CERT_CONTEXT *certificate) const { if (certificate) CertFreeCertificateContext(certificate); }
};

struct ChainDeleter
{
    void operator()(const CERT_CHAIN_CONTEXT *chain) const { if (chain) CertFreeCertificateChain(chain); }
};

struct StoreDeleter
{
    void operator()(void *store) const { if (store) CertCloseStore(static_cast<HCERTSTORE>(store), 0); }
};

struct MessageDeleter
{
    void operator()(void *message) const { if (message) CryptMsgClose(static_cast<HCRYPTMSG>(message)); }
};

std::optional<std::string> Attribute(std::string_view tag, std::string_view name)
{
    const std::string needle = std::string(name) + "=";
    std::size_t offset = 0;
    while ((offset = tag.find(needle, offset)) != std::string_view::npos)
    {
        if (offset && ((tag[offset - 1] >= 'A' && tag[offset - 1] <= 'Z') ||
                       (tag[offset - 1] >= 'a' && tag[offset - 1] <= 'z') || tag[offset - 1] == '_'))
        {
            offset += needle.size();
            continue;
        }
        const std::size_t quote = offset + needle.size();
        if (quote >= tag.size() || (tag[quote] != '"' && tag[quote] != '\''))
            return std::nullopt;
        const std::size_t end = tag.find(tag[quote], quote + 1);
        if (end == std::string_view::npos)
            return std::nullopt;
        return std::string(tag.substr(quote + 1, end - quote - 1));
    }
    return std::nullopt;
}

bool ParseSize(std::string_view text, std::size_t &result)
{
    const std::string owned(text);
    errno = 0;
    char *end = nullptr;
    const unsigned long long value = std::strtoull(owned.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end || value > static_cast<unsigned long long>(SIZE_MAX))
        return false;
    result = static_cast<std::size_t>(value);
    return true;
}

Status DecodeHash(std::string_view base64, std::array<std::uint8_t, 32> &hash)
{
    DWORD size = static_cast<DWORD>(hash.size());
    if (!CryptStringToBinaryA(base64.data(), static_cast<DWORD>(base64.size()), CRYPT_STRING_BASE64,
                              hash.data(), &size, nullptr, nullptr) || size != hash.size())
        return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map contains an invalid SHA-256 hash");
    return Status::Ok();
}

Status VerifyBlockMapFile(std::string_view xml, std::string_view wantedName, const std::vector<std::uint8_t> &bytes)
{
    std::size_t fileOffset = 0;
    std::string_view body;
    for (;;)
    {
        fileOffset = xml.find("<File ", fileOffset);
        if (fileOffset == std::string_view::npos)
            return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map does not contain " + std::string(wantedName));
        const std::size_t tagEnd = xml.find('>', fileOffset);
        if (tagEnd == std::string_view::npos)
            return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map contains a truncated File element");
        const std::string_view tag = xml.substr(fileOffset, tagEnd - fileOffset + 1);
        const auto name = Attribute(tag, "Name");
        if (name && *name == wantedName)
        {
            const std::size_t fileEnd = xml.find("</File>", tagEnd + 1);
            if (fileEnd == std::string_view::npos)
                return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map contains an unterminated File element");
            body = xml.substr(tagEnd + 1, fileEnd - tagEnd - 1);
            const auto declaredSize = Attribute(tag, "Size");
            std::size_t size = 0;
            if (!declaredSize || !ParseSize(*declaredSize, size) || size != bytes.size())
                return Status::Fail(ERROR_CRC, "MSIX block map file size does not match " + std::string(wantedName));
            break;
        }
        fileOffset = tagEnd + 1;
    }

    std::size_t blockOffset = 0;
    std::size_t dataOffset = 0;
    while ((blockOffset = body.find("<Block ", blockOffset)) != std::string_view::npos)
    {
        const std::size_t tagEnd = body.find('>', blockOffset);
        if (tagEnd == std::string_view::npos)
            return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map contains a truncated Block element");
        const std::string_view tag = body.substr(blockOffset, tagEnd - blockOffset + 1);
        const auto encodedHash = Attribute(tag, "Hash");
        if (!encodedHash)
            return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map Block element is missing Hash");
        std::array<std::uint8_t, 32> expected{};
        Status status = DecodeHash(*encodedHash, expected);
        if (!status) return status;
        const std::size_t chunkSize = std::min<std::size_t>(64 * 1024, bytes.size() - dataOffset);
        if (!chunkSize)
            return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map has too many blocks for " + std::string(wantedName));
        std::array<std::uint8_t, 32> actual{};
        status = HashBytesSha256(bytes.data() + dataOffset, chunkSize, actual);
        if (!status) return status;
        if (actual != expected)
            return Status::Fail(ERROR_CRC, "MSIX signed block hash mismatch for " + std::string(wantedName));
        dataOffset += chunkSize;
        blockOffset = tagEnd + 1;
    }
    if (dataOffset != bytes.size())
        return Status::Fail(ERROR_BAD_FORMAT, "MSIX block map has too few blocks for " + std::string(wantedName));
    return Status::Ok();
}

Status VerifyPublisher(const CERT_CONTEXT *certificate, std::string_view appxManifest)
{
    const std::size_t identityOffset = appxManifest.find("<Identity ");
    if (identityOffset == std::string_view::npos)
        return Status::Fail(ERROR_BAD_FORMAT, "source AppxManifest.xml does not contain an Identity element");
    const std::size_t identityEnd = appxManifest.find('>', identityOffset);
    if (identityEnd == std::string_view::npos)
        return Status::Fail(ERROR_BAD_FORMAT, "source AppxManifest.xml has a truncated Identity element");
    const std::string_view identity = appxManifest.substr(identityOffset, identityEnd - identityOffset + 1);
    const auto name = Attribute(identity, "Name");
    const auto publisher = Attribute(identity, "Publisher");
    if (!name || *name != ExpectedPackageName || !publisher || *publisher != ExpectedPublisher)
        return Status::Fail(ERROR_ACCESS_DENIED, "source package identity is not Microsoft.Winget.Source");

    CERT_NAME_INFO *rawSubject = nullptr;
    DWORD subjectSize = 0;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_NAME, certificate->pCertInfo->Subject.pbData,
                             certificate->pCertInfo->Subject.cbData, CRYPT_DECODE_ALLOC_FLAG,
                             nullptr, &rawSubject, &subjectSize))
        return Status::Fail(GetLastError(), "cannot decode source signature certificate subject");
    std::unique_ptr<CERT_NAME_INFO, decltype(&LocalFree)> subject(rawSubject, &LocalFree);
    const auto attributeEquals = [rawSubject](const char *objectId, const wchar_t *expected) {
        const CERT_RDN_ATTR *attribute = CertFindRDNAttr(objectId, rawSubject);
        if (!attribute) return false;
        auto value = const_cast<PCERT_RDN_VALUE_BLOB>(&attribute->Value);
        const DWORD required = CertRDNValueToStrW(attribute->dwValueType, value, nullptr, 0);
        if (!required) return false;
        std::vector<wchar_t> text(required);
        return CertRDNValueToStrW(attribute->dwValueType, value, text.data(), required) == required &&
               _wcsicmp(text.data(), expected) == 0;
    };
    if (!attributeEquals(szOID_COMMON_NAME, L"Microsoft Corporation") ||
        !attributeEquals(szOID_ORGANIZATION_NAME, L"Microsoft Corporation") ||
        !attributeEquals(szOID_LOCALITY_NAME, L"Redmond") ||
        !attributeEquals(szOID_STATE_OR_PROVINCE_NAME, L"Washington") ||
        !attributeEquals(szOID_COUNTRY_NAME, L"US"))
        return Status::Fail(ERROR_ACCESS_DENIED, "source signature certificate does not match the MSIX publisher");
    return Status::Ok();
}

} // namespace

Status VerifySourcePackageTrust(const SourcePackageContents &contents)
{
    if (contents.signature.size() < 5 || contents.blockMap.empty() || contents.manifest.empty() || contents.index.empty())
        return Status::Fail(ERROR_BAD_FORMAT, "source MSIX is missing signed package content");
    DWORD magic = 0;
    std::memcpy(&magic, contents.signature.data(), sizeof(magic));
    if (magic != P7xMagic)
        return Status::Fail(ERROR_BAD_FORMAT, "AppxSignature.p7x has an invalid PKCX header");

    CRYPT_DATA_BLOB blob{static_cast<DWORD>(contents.signature.size() - 4), const_cast<BYTE *>(contents.signature.data() + 4)};
    DWORD encoding = 0;
    DWORD contentType = 0;
    DWORD formatType = 0;
    HCERTSTORE rawStore = nullptr;
    HCRYPTMSG rawMessage = nullptr;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_BLOB, &blob,
                          CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED | CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding, &contentType, &formatType,
                          &rawStore, &rawMessage, nullptr))
        return Status::Fail(GetLastError(), "cannot decode the source AppX signature");
    std::unique_ptr<void, StoreDeleter> store(rawStore);
    std::unique_ptr<void, MessageDeleter> message(rawMessage);

    DWORD signerSize = 0;
    if (!CryptMsgGetParam(rawMessage, CMSG_SIGNER_CERT_INFO_PARAM, 0, nullptr, &signerSize) || !signerSize)
        return Status::Fail(GetLastError(), "source AppX signature has no signer");
    std::vector<std::uint8_t> signerBytes(signerSize);
    if (!CryptMsgGetParam(rawMessage, CMSG_SIGNER_CERT_INFO_PARAM, 0, signerBytes.data(), &signerSize))
        return Status::Fail(GetLastError(), "cannot read source AppX signer information");
    const auto signer = reinterpret_cast<PCERT_INFO>(signerBytes.data());
    std::unique_ptr<const CERT_CONTEXT, CertificateDeleter> certificate(
        CertFindCertificateInStore(rawStore, encoding, 0, CERT_FIND_SUBJECT_CERT, signer, nullptr));
    if (!certificate)
        return Status::Fail(GetLastError(), "source AppX signer certificate is missing");

    CMSG_CTRL_VERIFY_SIGNATURE_EX_PARA signatureParameters{};
    signatureParameters.cbSize = sizeof(signatureParameters);
    signatureParameters.dwSignerType = CMSG_VERIFY_SIGNER_CERT;
    signatureParameters.pvSigner = const_cast<PCERT_CONTEXT>(certificate.get());
    if (!CryptMsgControl(rawMessage, 0, CMSG_CTRL_VERIFY_SIGNATURE_EX, &signatureParameters))
        return Status::Fail(GetLastError(), "source AppX cryptographic signature is invalid");

    CERT_CHAIN_PARA chainParameters{};
    chainParameters.cbSize = sizeof(chainParameters);
    PCCERT_CHAIN_CONTEXT rawChain = nullptr;
    if (!CertGetCertificateChain(nullptr, certificate.get(), nullptr, rawStore, &chainParameters,
                                 CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT, nullptr, &rawChain))
        return Status::Fail(GetLastError(), "cannot build the source publisher certificate chain");
    std::unique_ptr<const CERT_CHAIN_CONTEXT, ChainDeleter> chain(rawChain);
    CERT_CHAIN_POLICY_PARA policyParameters{};
    policyParameters.cbSize = sizeof(policyParameters);
    CERT_CHAIN_POLICY_STATUS policyStatus{};
    policyStatus.cbSize = sizeof(policyStatus);
    if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_AUTHENTICODE, rawChain, &policyParameters, &policyStatus))
        return Status::Fail(GetLastError(), "cannot validate the source Authenticode certificate policy");
    if (rawChain->TrustStatus.dwErrorStatus != CERT_TRUST_NO_ERROR || policyStatus.dwError != ERROR_SUCCESS)
        return Status::Fail(policyStatus.dwError ? policyStatus.dwError : ERROR_ACCESS_DENIED,
                            "source publisher certificate chain is not trusted");

    Status status = VerifyPublisher(certificate.get(), std::string_view(reinterpret_cast<const char *>(contents.manifest.data()), contents.manifest.size()));
    if (!status) return status;

    DWORD contentSize = 0;
    if (!CryptMsgGetParam(rawMessage, CMSG_CONTENT_PARAM, 0, nullptr, &contentSize) || !contentSize)
        return Status::Fail(GetLastError(), "source AppX signature has no indirect digest");
    std::vector<std::uint8_t> content(contentSize);
    if (!CryptMsgGetParam(rawMessage, CMSG_CONTENT_PARAM, 0, content.data(), &contentSize))
        return Status::Fail(GetLastError(), "cannot read source AppX indirect digest");
    SIP_INDIRECT_DATA *rawIndirect = nullptr;
    DWORD indirectSize = 0;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, SpcIndirectDataObjectId, content.data(), contentSize,
                             CRYPT_DECODE_ALLOC_FLAG, nullptr, &rawIndirect, &indirectSize))
        return Status::Fail(GetLastError(), "cannot decode source AppX indirect digest");
    std::unique_ptr<SIP_INDIRECT_DATA, decltype(&LocalFree)> indirect(rawIndirect, &LocalFree);
    if (!rawIndirect->DigestAlgorithm.pszObjId || std::strcmp(rawIndirect->DigestAlgorithm.pszObjId, szOID_NIST_sha256) != 0 ||
        rawIndirect->Digest.cbData < 4 || std::memcmp(rawIndirect->Digest.pbData, "APPX", 4) != 0)
        return Status::Fail(ERROR_BAD_FORMAT, "source AppX signature does not use the expected SHA-256 APPX digest");

    const std::uint8_t *blockMapDigest = nullptr;
    for (DWORD offset = 4; offset + 36 <= rawIndirect->Digest.cbData; offset += 36)
    {
        if (std::memcmp(rawIndirect->Digest.pbData + offset, "AXBM", 4) == 0)
            blockMapDigest = rawIndirect->Digest.pbData + offset + 4;
    }
    if (!blockMapDigest)
        return Status::Fail(ERROR_BAD_FORMAT, "source AppX signature does not contain an AXBM digest");
    std::array<std::uint8_t, 32> actualBlockMapDigest{};
    status = HashBytesSha256(contents.blockMap.data(), contents.blockMap.size(), actualBlockMapDigest);
    if (!status) return status;
    if (!std::equal(actualBlockMapDigest.begin(), actualBlockMapDigest.end(), blockMapDigest))
        return Status::Fail(ERROR_CRC, "source AppxBlockMap.xml does not match the signed AXBM digest");

    const std::string_view blockMap(reinterpret_cast<const char *>(contents.blockMap.data()), contents.blockMap.size());
    if (blockMap.find("HashMethod=\"http://www.w3.org/2001/04/xmlenc#sha256\"") == std::string_view::npos)
        return Status::Fail(ERROR_NOT_SUPPORTED, "source MSIX block map does not use SHA-256");
    status = VerifyBlockMapFile(blockMap, "Public\\index.db", contents.index);
    if (!status) return status;
    return VerifyBlockMapFile(blockMap, "AppxManifest.xml", contents.manifest);
}

} // namespace rosget
