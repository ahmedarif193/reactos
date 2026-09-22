/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests I_CertUpdateStore
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>

#define SERIAL_OFFSET 7

typedef BOOL (WINAPI *PFN_I_CERTUPDATESTORE)(HCERTSTORE, HCERTSTORE, DWORD, DWORD);

static const BYTE SignedCert[] = {
 0x30, 0x81, 0x93, 0x30, 0x7a, 0x02, 0x01, 0x01, 0x30, 0x02, 0x06, 0x00, 0x30,
 0x15, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0a, 0x4a,
 0x75, 0x61, 0x6e, 0x20, 0x4c, 0x61, 0x6e, 0x67, 0x00, 0x30, 0x22, 0x18, 0x0f,
 0x31, 0x36, 0x30, 0x31, 0x30, 0x31, 0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x30,
 0x30, 0x5a, 0x18, 0x0f, 0x31, 0x36, 0x30, 0x31, 0x30, 0x31, 0x30, 0x31, 0x30,
 0x30, 0x30, 0x30, 0x30, 0x30, 0x5a, 0x30, 0x15, 0x31, 0x13, 0x30, 0x11, 0x06,
 0x03, 0x55, 0x04, 0x03, 0x13, 0x0a, 0x4a, 0x75, 0x61, 0x6e, 0x20, 0x4c, 0x61,
 0x6e, 0x67, 0x00, 0x30, 0x07, 0x30, 0x02, 0x06, 0x00, 0x03, 0x01, 0x00, 0xa3,
 0x16, 0x30, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01, 0x01, 0xff,
 0x04, 0x08, 0x30, 0x06, 0x01, 0x01, 0xff, 0x02, 0x01, 0x01, 0x30, 0x02, 0x06,
 0x00, 0x03, 0x11, 0x00, 0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07,
 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00 };

START_TEST(CertUpdateStore)
{
    PFN_I_CERTUPDATESTORE pI_CertUpdateStore;
    BYTE SecondCert[sizeof(SignedCert)];
    PCCERT_CONTEXT Context;
    HCERTSTORE Target, Source;
    ULONG Count;

    pI_CertUpdateStore = (PFN_I_CERTUPDATESTORE)GetProcAddress(GetModuleHandleW(L"crypt32.dll"), "I_CertUpdateStore");
    if (!pI_CertUpdateStore)
    {
        skip("I_CertUpdateStore is not exported\n");
        return;
    }

    CopyMemory(SecondCert, SignedCert, sizeof(SignedCert));
    SecondCert[SERIAL_OFFSET] = 0x02;

    Target = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    Source = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    ok(Target && Source, "CertOpenStore failed: %lu\n", GetLastError());
    if (!Target || !Source)
        return;

    ok(CertAddEncodedCertificateToStore(Target, X509_ASN_ENCODING, SignedCert, sizeof(SignedCert),
                                        CERT_STORE_ADD_ALWAYS, NULL),
       "CertAddEncodedCertificateToStore failed: %lu\n", GetLastError());
    ok(CertAddEncodedCertificateToStore(Source, X509_ASN_ENCODING, SecondCert, sizeof(SecondCert),
                                        CERT_STORE_ADD_ALWAYS, NULL),
       "CertAddEncodedCertificateToStore failed: %lu\n", GetLastError());

    ok(pI_CertUpdateStore(Target, Source, 0, 0), "I_CertUpdateStore failed: %lu\n", GetLastError());

    Count = 0;
    Context = NULL;
    while ((Context = CertEnumCertificatesInStore(Target, Context)))
    {
        Count++;
        ok(Context->cbCertEncoded == sizeof(SecondCert) &&
           !memcmp(Context->pbCertEncoded, SecondCert, sizeof(SecondCert)),
           "Target holds a certificate that is not in the source\n");
    }
    ok(Count == 1, "Target holds %lu certificates\n", Count);

    CertCloseStore(Source, 0);
    CertCloseStore(Target, 0);
}
