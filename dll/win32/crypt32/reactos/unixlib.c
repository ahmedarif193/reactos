/*
 * PROJECT:     ReactOS win32 DLLs
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ReactOS emulation layer for crypt32 unixlib calls
 * COPYRIGHT:   Copyright 2026 Timo Kreuzer <timo.kreuzer@reactos.org>
 */
#include <ntstatus.h>
#include <assert.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <wine/debug.h>
#include "crypt32_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(crypt);

// See https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-gpef/e051aba9-c9df-4f82-a42a-c13012c9d381
typedef struct _CRYPT_CERT_PROP
{
    DWORD dwPropId;
    DWORD dwReserved;
    DWORD cbData;
    BYTE ajData[ANYSIZE_ARRAY];
} CRYPT_CERT_PROP, *PCRYPT_CERT_PROP;

static
BOOL
FindCertInRegBlob(
    _In_ const CRYPT_DATA_BLOB* RegBlob,
    _Out_ PCRYPT_DER_BLOB OutCertBlob)
{
    PCRYPT_CERT_PROP prop;
    DWORD offset = 0;

    /* Parse the registry blob */
    while ((offset + sizeof(CRYPT_CERT_PROP)) < RegBlob->cbData)
    {
        prop = (PCRYPT_CERT_PROP)(RegBlob->pbData + offset);

        if (prop->dwReserved != 0x00000001)
        {
            /* Invalid reserved field */
            return FALSE;
        }

        /* Check for the certificate property (ID 32) */
        if (prop->dwPropId == 32)
        {
            if ((offset + prop->cbData) > RegBlob->cbData)
            {
                /* Invalid data size */
                return FALSE;
            }

            OutCertBlob->cbData = prop->cbData;
            OutCertBlob->pbData = prop->ajData;
            return TRUE;
        }

        /* Move to the next property */
        offset += FIELD_OFFSET(CRYPT_CERT_PROP, ajData) + prop->cbData;
    }

    return FALSE;
}

static
BOOL
LoadCertBlobFromReg(
    _In_ HKEY hRootKey,
    _In_z_ PWSTR pwszSubkeyName,
    _Out_ PCRYPT_DATA_BLOB RegDataBlob)
{
    HKEY hCert;
    DWORD dwLength = 0;
    DWORD dwType;
    LSTATUS ret;
    PVOID pvBuffer = NULL;

    RegDataBlob->cbData = 0;
    RegDataBlob->pbData = NULL;

    /* Open the certificate subkey */
    ret = RegOpenKeyExW(hRootKey, pwszSubkeyName, 0, KEY_READ, &hCert);
    if (ret != ERROR_SUCCESS)
    {
        return FALSE;
    }

    /* Query blob size */
    ret = RegQueryValueExW(hCert, L"Blob", NULL, &dwType, NULL, &dwLength);
    if (ret != ERROR_SUCCESS || dwType != REG_BINARY)
    {
        RegCloseKey(hCert);
        return FALSE;
    }

    /* Allocate the buffer */
    pvBuffer = HeapAlloc(GetProcessHeap(), 0, dwLength);
    if (pvBuffer == NULL)
    {
        RegCloseKey(hCert);
        return FALSE;
    }
        
    /* Fetch the registry blob */
    ret = RegQueryValueExW(hCert, L"Blob", NULL, &dwType, pvBuffer, &dwLength);
    RegCloseKey(hCert);

    if ((ret != ERROR_SUCCESS) || (dwType != REG_BINARY))
    {
        HeapFree(GetProcessHeap(), 0, pvBuffer);
        return FALSE;
    }

    RegDataBlob->pbData = pvBuffer;
    RegDataBlob->cbData = dwLength;
    return TRUE;
}

static
LSTATUS
LoadCertificateFromStore(
    HKEY hRoot,
    DWORD dwIndex,
    PVOID pvBuffer,
    DWORD cbBufferSize,
    PDWORD pcbRequired)
{
    WCHAR awcSubkey[64];
    CRYPT_DATA_BLOB regBlob;
    CRYPT_DER_BLOB certBlob;
    LSTATUS ret;

    if (pcbRequired == 0)
    {
        return ERROR_INVALID_PARAMETER;
    }

    /* Enumerate next subkey */
    ret = RegEnumKeyW(hRoot, dwIndex, awcSubkey, ARRAYSIZE(awcSubkey));
    if (ret != ERROR_SUCCESS)
    {
        return ret;
    }

    /* Load the registry blob */
    if (!LoadCertBlobFromReg(hRoot, awcSubkey, &regBlob))
    {
        return ERROR_NO_DATA;
    }

    /* Extract the certificate from the registry blob */
    if (!FindCertInRegBlob(&regBlob, &certBlob))
    {
        HeapFree(GetProcessHeap(), 0, regBlob.pbData);
        return ERROR_NO_DATA;
    }

    *pcbRequired = certBlob.cbData;

    /* Check if we have a usable buffer */
    if ((pvBuffer != NULL) && (cbBufferSize >= certBlob.cbData))
    {
        /* Copy the certificate data to the output buffer */
        memcpy(pvBuffer, certBlob.pbData, certBlob.cbData);
        ret = ERROR_SUCCESS;
    }
    else
    {
        ret = SEC_E_BUFFER_TOO_SMALL;
    }

    /* Free the registry blob buffer */
    HeapFree(GetProcessHeap(), 0, regBlob.pbData);

    /* Successfully retrieved a certificate */
    return ret;
}

// Note: this function is not thread-safe! It is called under a lock in crypt32.
static
NTSTATUS
EnumerateRootCertificates(
    PVOID pvBuffer,
    DWORD cbBufferSize,
    PDWORD pcbRequired)
{
    const WCHAR* aszStoreKeyNames[] = {
        L"Software\\Microsoft\\SystemCertificates\\Root\\Certificates",
        L"Software\\Microsoft\\SystemCertificates\\AuthRoot\\Certificates"
    };
    static DWORD dwStoreIndex = 0;
    static HKEY hStoreKey = NULL;
    static DWORD dwCertIndex = 0;
    LSTATUS ret;

    assert(pcbRequired != NULL);

    /* Root store enumeration loop */
    for (; dwStoreIndex < ARRAYSIZE(aszStoreKeyNames); dwStoreIndex++)
    {
        if (hStoreKey == NULL)
        {
            /* Open the key for the root certificates */
            ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                                aszStoreKeyNames[dwStoreIndex],
                                0,
                                KEY_READ,
                                &hStoreKey);
            if (ret != ERROR_SUCCESS)
                continue;
        }

        /* Cert enumeration loop */
        for (; dwCertIndex < MAXDWORD; dwCertIndex++)
        {

            ret = LoadCertificateFromStore(hStoreKey,
                                           dwCertIndex,
                                           pvBuffer,
                                           cbBufferSize,
                                           pcbRequired);
            if (ret == ERROR_SUCCESS)
            {
                /* Successfully retrieved a certificate */
                dwCertIndex++;
                return STATUS_SUCCESS;
            }

            if (ret == SEC_E_BUFFER_TOO_SMALL)
            {
                /* Return the required size without incrementing the index. */
                return STATUS_SUCCESS;
            }

            if (ret == ERROR_NO_MORE_ITEMS)
            {
                /* No more certificates in this store, break to move to next store */
                break;
            }
        }

        /* Move to the next root store */
        RegCloseKey(hStoreKey);
        hStoreKey = NULL;
        dwCertIndex = 0;
    }

    /* No more certificates in any store */
    if (hStoreKey != NULL)
        RegCloseKey(hStoreKey);
    hStoreKey = NULL;
    dwStoreIndex = 0;
    dwCertIndex = 0;
    return STATUS_NO_MORE_ENTRIES;
}

NTSTATUS __reactos_call_unix_enum_root_certs(void* Args)
{
    struct enum_root_certs_params* params = (struct enum_root_certs_params*)Args;
    return EnumerateRootCertificates(params->buffer,
                                     params->size,
                                     params->needed);
}

struct der_view
{
    const BYTE *data;
    DWORD size;
};

struct reactos_cert_store_data
{
    BYTE **certs;
    DWORD *cert_sizes;
    DWORD cert_count;
    BYTE *key;
    DWORD key_size;
};

static const BYTE oid_data[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x07,0x01};
static const BYTE oid_encrypted_data[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x07,0x06};
static const BYTE oid_key_bag[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x0c,0x0a,0x01,0x01};
static const BYTE oid_shrouded_key_bag[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x0c,0x0a,0x01,0x02};
static const BYTE oid_cert_bag[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x0c,0x0a,0x01,0x03};
static const BYTE oid_x509_cert[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x09,0x16,0x01};
static const BYTE oid_pbe_sha1_3des[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x0c,0x01,0x03};
static const BYTE oid_pbe_sha1_rc2_40[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x0c,0x01,0x06};
static const BYTE oid_rsa_encryption[] = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01};
static const BYTE oid_sha1[] = {0x2b,0x0e,0x03,0x02,0x1a};
static const BYTE oid_sha256[] = {0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01};

static BOOL der_read(struct der_view *input, BYTE tag, struct der_view *value)
{
    DWORD header, length, length_bytes, i;

    if (input->size < 2 || input->data[0] != tag) return FALSE;
    if (!(input->data[1] & 0x80))
    {
        header = 2;
        length = input->data[1];
    }
    else
    {
        length_bytes = input->data[1] & 0x7f;
        if (!length_bytes || length_bytes > sizeof(DWORD) || input->size < 2 + length_bytes) return FALSE;
        header = 2 + length_bytes;
        length = 0;
        for (i = 0; i < length_bytes; i++)
        {
            if (length > (MAXDWORD >> 8)) return FALSE;
            length = (length << 8) | input->data[2 + i];
        }
    }
    if (length > input->size - header) return FALSE;
    value->data = input->data + header;
    value->size = length;
    input->data += header + length;
    input->size -= header + length;
    return TRUE;
}

static BOOL der_read_integer(struct der_view *input, DWORD *value)
{
    struct der_view integer;
    DWORD i;

    if (!der_read(input, 0x02, &integer) || !integer.size || integer.size > sizeof(DWORD) + 1) return FALSE;
    if (integer.size == sizeof(DWORD) + 1 && *integer.data++) return FALSE;
    if (integer.size == sizeof(DWORD) + 1) integer.size--;
    *value = 0;
    for (i = 0; i < integer.size; i++) *value = (*value << 8) | integer.data[i];
    return TRUE;
}

static BOOL der_oid_is(const struct der_view *oid, const BYTE *expected, DWORD expected_size)
{
    return oid->size == expected_size && !memcmp(oid->data, expected, expected_size);
}

static BOOL hash_buffer(BCRYPT_ALG_HANDLE algorithm, const BYTE *data, DWORD size, BYTE *digest, DWORD digest_size)
{
    return BCryptHash(algorithm, NULL, 0, (BYTE *)data, size, digest, digest_size) == STATUS_SUCCESS;
}

static BOOL pkcs12_password(const WCHAR *password, BYTE **bytes, DWORD *size)
{
    DWORD i, length = password ? lstrlenW(password) : 0;

    if (length > (MAXDWORD / 2) - 1) return FALSE;
    *size = (length + 1) * 2;
    if (!(*bytes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *size))) return FALSE;
    for (i = 0; i < length; i++)
    {
        (*bytes)[i * 2] = password[i] >> 8;
        (*bytes)[i * 2 + 1] = password[i];
    }
    return TRUE;
}

static BOOL pkcs12_derive(const WCHAR *algorithm_name, DWORD digest_size, const BYTE *password, DWORD password_size, const BYTE *salt, DWORD salt_size, DWORD id, DWORD iterations, BYTE *output, DWORD output_size)
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BYTE *input = NULL, *hash_input = NULL, *block;
    BYTE digest[32], next_digest[32], repeated[64];
    DWORD input_size, password_repeated, salt_repeated, offset, copy, i, j, carry;
    BOOL ret = FALSE;

    if (!iterations || digest_size > sizeof(digest)) return FALSE;
    salt_repeated = salt_size ? ((salt_size + 63) / 64) * 64 : 0;
    password_repeated = password_size ? ((password_size + 63) / 64) * 64 : 0;
    if (salt_repeated > MAXDWORD - password_repeated) return FALSE;
    input_size = salt_repeated + password_repeated;
    if (!(input = HeapAlloc(GetProcessHeap(), 0, input_size ? input_size : 1))) goto done;
    for (i = 0; i < salt_repeated; i++) input[i] = salt[i % salt_size];
    for (i = 0; i < password_repeated; i++) input[salt_repeated + i] = password[i % password_size];
    if (!(hash_input = HeapAlloc(GetProcessHeap(), 0, 64 + input_size))) goto done;
    memset(hash_input, id, 64);
    if (input_size) memcpy(hash_input + 64, input, input_size);
    if (BCryptOpenAlgorithmProvider(&algorithm, algorithm_name, NULL, 0) != STATUS_SUCCESS) goto done;

    for (offset = 0; offset < output_size; offset += digest_size)
    {
        if (!hash_buffer(algorithm, hash_input, 64 + input_size, digest, digest_size)) goto done;
        for (i = 1; i < iterations; i++)
        {
            if (!hash_buffer(algorithm, digest, digest_size, next_digest, digest_size)) goto done;
            memcpy(digest, next_digest, digest_size);
        }
        for (i = 0; i < sizeof(repeated); i++) repeated[i] = digest[i % digest_size];
        for (i = 0; i < input_size; i += sizeof(repeated))
        {
            block = input + i;
            carry = 1;
            for (j = sizeof(repeated); j; j--)
            {
                carry += block[j - 1] + repeated[j - 1];
                block[j - 1] = carry;
                carry >>= 8;
            }
        }
        copy = min(digest_size, output_size - offset);
        memcpy(output + offset, digest, copy);
        if (input_size) memcpy(hash_input + 64, input, input_size);
    }
    ret = TRUE;

done:
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    HeapFree(GetProcessHeap(), 0, hash_input);
    HeapFree(GetProcessHeap(), 0, input);
    return ret;
}

static BOOL pkcs12_hmac(const WCHAR *algorithm_name, const BYTE *key, DWORD key_size, const BYTE *data, DWORD data_size, BYTE *digest, DWORD digest_size)
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BOOL ret = FALSE;

    if (BCryptOpenAlgorithmProvider(&algorithm, algorithm_name, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) != STATUS_SUCCESS) return FALSE;
    if (BCryptHash(algorithm, (BYTE *)key, key_size, (BYTE *)data, data_size, digest, digest_size) == STATUS_SUCCESS) ret = TRUE;
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ret;
}

static BOOL parse_pbe_algorithm(struct der_view *algorithm, ALG_ID *alg_id, DWORD *key_size, DWORD *effective_key_size, struct der_view *salt, DWORD *iterations)
{
    struct der_view sequence, oid, parameters;

    if (!der_read(algorithm, 0x30, &sequence) || algorithm->size) return FALSE;
    if (!der_read(&sequence, 0x06, &oid) || !der_read(&sequence, 0x30, &parameters) || sequence.size) return FALSE;
    if (!der_read(&parameters, 0x04, salt) || !der_read_integer(&parameters, iterations) || parameters.size) return FALSE;
    if (der_oid_is(&oid, oid_pbe_sha1_rc2_40, sizeof(oid_pbe_sha1_rc2_40)))
    {
        *alg_id = CALG_RC2;
        *key_size = 5;
        *effective_key_size = 40;
        return TRUE;
    }
    if (der_oid_is(&oid, oid_pbe_sha1_3des, sizeof(oid_pbe_sha1_3des)))
    {
        *alg_id = CALG_3DES;
        *key_size = 24;
        *effective_key_size = 0;
        return TRUE;
    }
    return FALSE;
}

static BOOL decrypt_pbe(struct der_view *algorithm, const WCHAR *password, const BYTE *encrypted, DWORD encrypted_size, BYTE **decrypted, DWORD *decrypted_size)
{
    struct plaintext_key_blob
    {
        BLOBHEADER header;
        DWORD size;
        BYTE key[24];
    } key_blob;
    struct der_view salt;
    HCRYPTPROV provider = 0;
    HCRYPTKEY key = 0;
    ALG_ID alg_id;
    BYTE *password_bytes = NULL, iv[8];
    CRYPT_INTEGER_BLOB empty_salt = {0, NULL};
    DWORD password_size, key_size, effective_key_size, iterations, mode = CRYPT_MODE_CBC;
    BOOL ret = FALSE;

    *decrypted = NULL;
    *decrypted_size = 0;
    if (!parse_pbe_algorithm(algorithm, &alg_id, &key_size, &effective_key_size, &salt, &iterations)) return FALSE;
    if (!pkcs12_password(password, &password_bytes, &password_size)) goto done;
    memset(&key_blob, 0, sizeof(key_blob));
    key_blob.header.bType = PLAINTEXTKEYBLOB;
    key_blob.header.bVersion = CUR_BLOB_VERSION;
    key_blob.header.aiKeyAlg = alg_id;
    key_blob.size = key_size;
    if (!pkcs12_derive(BCRYPT_SHA1_ALGORITHM, 20, password_bytes, password_size, salt.data, salt.size, 1, iterations, key_blob.key, key_size)) goto done;
    if (!pkcs12_derive(BCRYPT_SHA1_ALGORITHM, 20, password_bytes, password_size, salt.data, salt.size, 2, iterations, iv, sizeof(iv))) goto done;
    if (!CryptAcquireContextW(&provider, NULL, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) goto done;
    if (!CryptImportKey(provider, (BYTE *)&key_blob, FIELD_OFFSET(struct plaintext_key_blob, key) + key_size, 0, alg_id == CALG_RC2 ? CRYPT_NO_SALT : 0, &key)) goto done;
    if (alg_id == CALG_RC2 && !CryptSetKeyParam(key, KP_SALT_EX, (BYTE *)&empty_salt, 0)) goto done;
    if (!CryptSetKeyParam(key, KP_MODE, (BYTE *)&mode, 0) || !CryptSetKeyParam(key, KP_IV, iv, 0)) goto done;
    if (effective_key_size && !CryptSetKeyParam(key, KP_EFFECTIVE_KEYLEN, (BYTE *)&effective_key_size, 0)) goto done;
    if (!(*decrypted = HeapAlloc(GetProcessHeap(), 0, encrypted_size ? encrypted_size : 1))) goto done;
    memcpy(*decrypted, encrypted, encrypted_size);
    *decrypted_size = encrypted_size;
    if (!CryptDecrypt(key, 0, TRUE, 0, *decrypted, decrypted_size))
    {
        HeapFree(GetProcessHeap(), 0, *decrypted);
        *decrypted = NULL;
        *decrypted_size = 0;
        goto done;
    }
    ret = TRUE;

done:
    if (key) CryptDestroyKey(key);
    if (provider) CryptReleaseContext(provider, 0);
    HeapFree(GetProcessHeap(), 0, password_bytes);
    return ret;
}

static void free_cert_store_data(struct reactos_cert_store_data *data)
{
    DWORD i;

    if (!data) return;
    for (i = 0; i < data->cert_count; i++) HeapFree(GetProcessHeap(), 0, data->certs[i]);
    HeapFree(GetProcessHeap(), 0, data->certs);
    HeapFree(GetProcessHeap(), 0, data->cert_sizes);
    HeapFree(GetProcessHeap(), 0, data->key);
    HeapFree(GetProcessHeap(), 0, data);
}

static BOOL add_store_cert(struct reactos_cert_store_data *data, const BYTE *cert, DWORD cert_size)
{
    BYTE **certs;
    DWORD *sizes;

    certs = data->cert_count ? HeapReAlloc(GetProcessHeap(), 0, data->certs, (data->cert_count + 1) * sizeof(*certs)) : HeapAlloc(GetProcessHeap(), 0, sizeof(*certs));
    if (!certs) return FALSE;
    data->certs = certs;
    sizes = data->cert_count ? HeapReAlloc(GetProcessHeap(), 0, data->cert_sizes, (data->cert_count + 1) * sizeof(*sizes)) : HeapAlloc(GetProcessHeap(), 0, sizeof(*sizes));
    if (!sizes) return FALSE;
    data->cert_sizes = sizes;
    if (!(data->certs[data->cert_count] = HeapAlloc(GetProcessHeap(), 0, cert_size))) return FALSE;
    memcpy(data->certs[data->cert_count], cert, cert_size);
    data->cert_sizes[data->cert_count] = cert_size;
    data->cert_count++;
    return TRUE;
}

static BOOL import_private_key_info(struct reactos_cert_store_data *data, const BYTE *encoded, DWORD encoded_size)
{
    struct der_view input = {encoded, encoded_size}, sequence, algorithm, oid, private_key;
    DWORD version, size = 0;
    BYTE *key = NULL;

    if (!der_read(&input, 0x30, &sequence) || input.size) return FALSE;
    if (!der_read_integer(&sequence, &version) || version) return FALSE;
    if (!der_read(&sequence, 0x30, &algorithm) || !der_read(&algorithm, 0x06, &oid)) return FALSE;
    if (!der_oid_is(&oid, oid_rsa_encryption, sizeof(oid_rsa_encryption))) return FALSE;
    if (!der_read(&sequence, 0x04, &private_key)) return FALSE;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY, private_key.data, private_key.size, CRYPT_DECODE_ALLOC_FLAG, NULL, &key, &size)) return FALSE;
    HeapFree(GetProcessHeap(), 0, data->key);
    data->key = HeapAlloc(GetProcessHeap(), 0, size);
    if (!data->key)
    {
        LocalFree(key);
        return FALSE;
    }
    memcpy(data->key, key, size);
    data->key_size = size;
    LocalFree(key);
    return TRUE;
}

static BOOL parse_cert_bag(struct reactos_cert_store_data *data, struct der_view *bag_value)
{
    struct der_view sequence, oid, explicit_value, cert;

    if (!der_read(bag_value, 0x30, &sequence) || bag_value->size) return FALSE;
    if (!der_read(&sequence, 0x06, &oid) || !der_oid_is(&oid, oid_x509_cert, sizeof(oid_x509_cert))) return FALSE;
    if (!der_read(&sequence, 0xa0, &explicit_value) || !der_read(&explicit_value, 0x04, &cert)) return FALSE;
    return add_store_cert(data, cert.data, cert.size);
}

static BOOL parse_shrouded_key_bag(struct reactos_cert_store_data *data, struct der_view *bag_value, const WCHAR *password)
{
    struct der_view sequence, algorithm, algorithm_value, encrypted;
    BYTE *decrypted = NULL;
    DWORD algorithm_size, decrypted_size;
    BOOL ret;

    if (!der_read(bag_value, 0x30, &sequence) || bag_value->size) return FALSE;
    algorithm = sequence;
    if (!der_read(&sequence, 0x30, &algorithm_value)) return FALSE;
    algorithm_size = algorithm.size - sequence.size;
    algorithm.size = algorithm_size;
    if (!der_read(&sequence, 0x04, &encrypted) || sequence.size) return FALSE;
    ret = decrypt_pbe(&algorithm, password, encrypted.data, encrypted.size, &decrypted, &decrypted_size);
    if (ret) ret = import_private_key_info(data, decrypted, decrypted_size);
    HeapFree(GetProcessHeap(), 0, decrypted);
    return ret;
}

static BOOL parse_safe_contents(struct reactos_cert_store_data *data, const BYTE *encoded, DWORD encoded_size, const WCHAR *password)
{
    struct der_view input = {encoded, encoded_size}, contents, bag, oid, explicit_value;

    if (!der_read(&input, 0x30, &contents) || input.size) return FALSE;
    while (contents.size)
    {
        if (!der_read(&contents, 0x30, &bag) || !der_read(&bag, 0x06, &oid) || !der_read(&bag, 0xa0, &explicit_value)) return FALSE;
        if (der_oid_is(&oid, oid_cert_bag, sizeof(oid_cert_bag)))
        {
            if (!parse_cert_bag(data, &explicit_value)) return FALSE;
        }
        else if (der_oid_is(&oid, oid_shrouded_key_bag, sizeof(oid_shrouded_key_bag)))
        {
            if (!parse_shrouded_key_bag(data, &explicit_value, password)) return FALSE;
        }
        else if (der_oid_is(&oid, oid_key_bag, sizeof(oid_key_bag)))
        {
            if (!import_private_key_info(data, explicit_value.data, explicit_value.size)) return FALSE;
        }
    }
    return TRUE;
}

static BOOL parse_data_content_info(struct der_view *content_info, struct der_view *content)
{
    struct der_view sequence, oid, explicit_value;

    if (!der_read(content_info, 0x30, &sequence) || content_info->size) return FALSE;
    if (!der_read(&sequence, 0x06, &oid) || !der_oid_is(&oid, oid_data, sizeof(oid_data))) return FALSE;
    if (!der_read(&sequence, 0xa0, &explicit_value) || !der_read(&explicit_value, 0x04, content)) return FALSE;
    return TRUE;
}

static BOOL parse_encrypted_content(struct reactos_cert_store_data *data, struct der_view *explicit_value, const WCHAR *password)
{
    struct der_view encrypted_data, encrypted_info, content_type, algorithm, algorithm_value, encrypted;
    BYTE *decrypted = NULL;
    DWORD version, algorithm_size, decrypted_size;
    BOOL ret;

    if (!der_read(explicit_value, 0x30, &encrypted_data) || explicit_value->size) return FALSE;
    if (!der_read_integer(&encrypted_data, &version) || !der_read(&encrypted_data, 0x30, &encrypted_info)) return FALSE;
    if (!der_read(&encrypted_info, 0x06, &content_type) || !der_oid_is(&content_type, oid_data, sizeof(oid_data))) return FALSE;
    algorithm = encrypted_info;
    if (!der_read(&encrypted_info, 0x30, &algorithm_value)) return FALSE;
    algorithm_size = algorithm.size - encrypted_info.size;
    algorithm.size = algorithm_size;
    if (!der_read(&encrypted_info, 0x80, &encrypted)) return FALSE;
    ret = decrypt_pbe(&algorithm, password, encrypted.data, encrypted.size, &decrypted, &decrypted_size);
    if (ret) ret = parse_safe_contents(data, decrypted, decrypted_size, password);
    HeapFree(GetProcessHeap(), 0, decrypted);
    return ret;
}

static BOOL parse_authenticated_safe(struct reactos_cert_store_data *data, const BYTE *encoded, DWORD encoded_size, const WCHAR *password)
{
    struct der_view input = {encoded, encoded_size}, authenticated_safe, content_info, sequence, oid, explicit_value, content;

    if (!der_read(&input, 0x30, &authenticated_safe) || input.size) return FALSE;
    while (authenticated_safe.size)
    {
        content_info = authenticated_safe;
        if (!der_read(&authenticated_safe, 0x30, &sequence) || !der_read(&sequence, 0x06, &oid) || !der_read(&sequence, 0xa0, &explicit_value)) return FALSE;
        if (der_oid_is(&oid, oid_data, sizeof(oid_data)))
        {
            if (!der_read(&explicit_value, 0x04, &content) || !parse_safe_contents(data, content.data, content.size, password)) return FALSE;
        }
        else if (der_oid_is(&oid, oid_encrypted_data, sizeof(oid_encrypted_data)))
        {
            if (!parse_encrypted_content(data, &explicit_value, password)) return FALSE;
        }
        else
        {
            WARN("unsupported PKCS#12 content type\n");
        }
        (void)content_info;
    }
    return TRUE;
}

static BOOL verify_pfx_mac(struct der_view *mac_data, const WCHAR *password, const BYTE *authenticated_safe, DWORD authenticated_safe_size)
{
    struct der_view sequence, digest_info, algorithm, oid, digest, salt;
    const WCHAR *algorithm_name;
    BYTE *password_bytes = NULL, key[32], calculated[32];
    DWORD digest_size, password_size, iterations = 1;
    BOOL ret = FALSE;

    if (!der_read(mac_data, 0x30, &sequence)) return FALSE;
    if (!der_read(&sequence, 0x30, &digest_info) || !der_read(&digest_info, 0x30, &algorithm)) return FALSE;
    if (!der_read(&algorithm, 0x06, &oid) || !der_read(&digest_info, 0x04, &digest) || !der_read(&sequence, 0x04, &salt)) return FALSE;
    if (sequence.size && !der_read_integer(&sequence, &iterations)) return FALSE;
    if (der_oid_is(&oid, oid_sha1, sizeof(oid_sha1)))
    {
        algorithm_name = BCRYPT_SHA1_ALGORITHM;
        digest_size = 20;
    }
    else if (der_oid_is(&oid, oid_sha256, sizeof(oid_sha256)))
    {
        algorithm_name = BCRYPT_SHA256_ALGORITHM;
        digest_size = 32;
    }
    else return FALSE;
    if (digest.size != digest_size || !pkcs12_password(password, &password_bytes, &password_size)) goto done;
    if (!pkcs12_derive(algorithm_name, digest_size, password_bytes, password_size, salt.data, salt.size, 3, iterations, key, digest_size)) goto done;
    if (!pkcs12_hmac(algorithm_name, key, digest_size, authenticated_safe, authenticated_safe_size, calculated, digest_size)) goto done;
    ret = !memcmp(calculated, digest.data, digest_size);

done:
    HeapFree(GetProcessHeap(), 0, password_bytes);
    return ret;
}

struct der_buffer
{
    BYTE *data;
    DWORD size;
};

static void der_buffer_free(struct der_buffer *buffer)
{
    HeapFree(GetProcessHeap(), 0, buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
}

static BOOL der_buffer_append(struct der_buffer *buffer, const BYTE *data, DWORD size)
{
    BYTE *new_data;

    if (!size) return TRUE;
    if (buffer->size > MAXDWORD - size) return FALSE;
    new_data = buffer->data ? HeapReAlloc(GetProcessHeap(), 0, buffer->data, buffer->size + size) : HeapAlloc(GetProcessHeap(), 0, size);
    if (!new_data) return FALSE;
    buffer->data = new_data;
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return TRUE;
}

static BOOL der_buffer_add_tlv(struct der_buffer *buffer, BYTE tag, const BYTE *data, DWORD size)
{
    BYTE header[6];
    DWORD length_bytes = 0, value = size, i;

    header[0] = tag;
    if (size < 0x80)
    {
        header[1] = size;
        length_bytes = 2;
    }
    else
    {
        while (value)
        {
            header[5 - length_bytes] = value;
            value >>= 8;
            length_bytes++;
        }
        header[1] = 0x80 | length_bytes;
        for (i = 0; i < length_bytes; i++) header[2 + i] = header[6 - length_bytes + i];
        length_bytes += 2;
    }
    return der_buffer_append(buffer, header, length_bytes) && der_buffer_append(buffer, data, size);
}

static BOOL der_buffer_add_oid(struct der_buffer *buffer, const BYTE *oid, DWORD oid_size)
{
    return der_buffer_add_tlv(buffer, 0x06, oid, oid_size);
}

static BOOL der_buffer_add_integer(struct der_buffer *buffer, DWORD value)
{
    BYTE bytes[5];
    DWORD offset = sizeof(bytes), count;

    do
    {
        bytes[--offset] = value;
        value >>= 8;
    } while (value);
    if (bytes[offset] & 0x80) bytes[--offset] = 0;
    count = sizeof(bytes) - offset;
    return der_buffer_add_tlv(buffer, 0x02, bytes + offset, count);
}

static BOOL der_buffer_add_positive_integer(struct der_buffer *buffer, const BYTE *bytes, DWORD size)
{
    BYTE zero = 0;

    while (size > 1 && !*bytes)
    {
        bytes++;
        size--;
    }
    if (*bytes & 0x80)
    {
        struct der_buffer value = {0};
        BOOL ret = der_buffer_append(&value, &zero, 1) && der_buffer_append(&value, bytes, size) && der_buffer_add_tlv(buffer, 0x02, value.data, value.size);
        der_buffer_free(&value);
        return ret;
    }
    return der_buffer_add_tlv(buffer, 0x02, bytes, size);
}

static BOOL der_buffer_wrap(struct der_buffer *output, BYTE tag, const struct der_buffer *contents)
{
    return der_buffer_add_tlv(output, tag, contents->data, contents->size);
}

static BOOL generate_random(BYTE *buffer, DWORD size)
{
    return BCryptGenRandom(NULL, buffer, size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == STATUS_SUCCESS;
}

static BOOL encrypt_pbe_3des(const WCHAR *password, const BYTE *plain, DWORD plain_size, const BYTE *salt, DWORD salt_size, DWORD iterations, BYTE **encrypted, DWORD *encrypted_size)
{
    struct plaintext_key_blob
    {
        BLOBHEADER header;
        DWORD size;
        BYTE key[24];
    } key_blob;
    HCRYPTPROV provider = 0;
    HCRYPTKEY key = 0;
    BYTE *password_bytes = NULL, iv[8];
    DWORD password_size, mode = CRYPT_MODE_CBC, capacity;
    BOOL ret = FALSE;

    *encrypted = NULL;
    *encrypted_size = 0;
    if (plain_size > MAXDWORD - 8 || !pkcs12_password(password, &password_bytes, &password_size)) goto done;
    memset(&key_blob, 0, sizeof(key_blob));
    key_blob.header.bType = PLAINTEXTKEYBLOB;
    key_blob.header.bVersion = CUR_BLOB_VERSION;
    key_blob.header.aiKeyAlg = CALG_3DES;
    key_blob.size = sizeof(key_blob.key);
    if (!pkcs12_derive(BCRYPT_SHA1_ALGORITHM, 20, password_bytes, password_size, salt, salt_size, 1, iterations, key_blob.key, sizeof(key_blob.key))) goto done;
    if (!pkcs12_derive(BCRYPT_SHA1_ALGORITHM, 20, password_bytes, password_size, salt, salt_size, 2, iterations, iv, sizeof(iv))) goto done;
    if (!CryptAcquireContextW(&provider, NULL, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) goto done;
    if (!CryptImportKey(provider, (BYTE *)&key_blob, sizeof(key_blob), 0, 0, &key)) goto done;
    if (!CryptSetKeyParam(key, KP_MODE, (BYTE *)&mode, 0) || !CryptSetKeyParam(key, KP_IV, iv, 0)) goto done;
    capacity = plain_size + 8;
    if (!(*encrypted = HeapAlloc(GetProcessHeap(), 0, capacity))) goto done;
    memcpy(*encrypted, plain, plain_size);
    *encrypted_size = plain_size;
    if (!CryptEncrypt(key, 0, TRUE, 0, *encrypted, encrypted_size, capacity))
    {
        HeapFree(GetProcessHeap(), 0, *encrypted);
        *encrypted = NULL;
        *encrypted_size = 0;
        goto done;
    }
    ret = TRUE;

done:
    if (key) CryptDestroyKey(key);
    if (provider) CryptReleaseContext(provider, 0);
    HeapFree(GetProcessHeap(), 0, password_bytes);
    return ret;
}

static BOOL build_algorithm_identifier(struct der_buffer *output, const BYTE *oid, DWORD oid_size, const BYTE *salt, DWORD salt_size, DWORD iterations)
{
    struct der_buffer parameters_contents = {0}, parameters = {0}, algorithm_contents = {0};
    BOOL ret;

    ret = der_buffer_add_tlv(&parameters_contents, 0x04, salt, salt_size) && der_buffer_add_integer(&parameters_contents, iterations);
    if (ret) ret = der_buffer_wrap(&parameters, 0x30, &parameters_contents);
    if (ret) ret = der_buffer_add_oid(&algorithm_contents, oid, oid_size) && der_buffer_append(&algorithm_contents, parameters.data, parameters.size);
    if (ret) ret = der_buffer_wrap(output, 0x30, &algorithm_contents);
    der_buffer_free(&algorithm_contents);
    der_buffer_free(&parameters);
    der_buffer_free(&parameters_contents);
    return ret;
}

static BOOL build_rsa_private_key(const BYTE *key_blob, DWORD key_blob_size, struct der_buffer *private_key_info)
{
    const BCRYPT_RSAKEY_BLOB *header;
    const BYTE *public_exponent, *modulus, *prime1, *prime2, *exponent1, *exponent2, *coefficient, *private_exponent;
    ULONGLONG required;
    struct der_buffer rsa_contents = {0}, rsa_key = {0}, rsa_algorithm_contents = {0}, rsa_algorithm = {0}, private_contents = {0};
    BYTE null_value = 0;
    BOOL ret = FALSE;

    if (key_blob_size < sizeof(*header)) return FALSE;
    header = (const BCRYPT_RSAKEY_BLOB *)key_blob;
    if (header->Magic != BCRYPT_RSAFULLPRIVATE_MAGIC || !header->cbPublicExp || !header->cbModulus || !header->cbPrime1 || !header->cbPrime2) return FALSE;
    required = sizeof(*header) + header->cbPublicExp + (ULONGLONG)header->cbModulus * 2 + (ULONGLONG)header->cbPrime1 * 3 + (ULONGLONG)header->cbPrime2 * 2;
    if (required > key_blob_size) return FALSE;
    public_exponent = key_blob + sizeof(*header);
    modulus = public_exponent + header->cbPublicExp;
    prime1 = modulus + header->cbModulus;
    prime2 = prime1 + header->cbPrime1;
    exponent1 = prime2 + header->cbPrime2;
    exponent2 = exponent1 + header->cbPrime1;
    coefficient = exponent2 + header->cbPrime2;
    private_exponent = coefficient + header->cbPrime1;
    if (!der_buffer_add_integer(&rsa_contents, 0)) goto done;
    if (!der_buffer_add_positive_integer(&rsa_contents, modulus, header->cbModulus)) goto done;
    if (!der_buffer_add_positive_integer(&rsa_contents, public_exponent, header->cbPublicExp)) goto done;
    if (!der_buffer_add_positive_integer(&rsa_contents, private_exponent, header->cbModulus)) goto done;
    if (!der_buffer_add_positive_integer(&rsa_contents, prime1, header->cbPrime1)) goto done;
    if (!der_buffer_add_positive_integer(&rsa_contents, prime2, header->cbPrime2)) goto done;
    if (!der_buffer_add_positive_integer(&rsa_contents, exponent1, header->cbPrime1)) goto done;
    if (!der_buffer_add_positive_integer(&rsa_contents, exponent2, header->cbPrime2)) goto done;
    if (!der_buffer_add_positive_integer(&rsa_contents, coefficient, header->cbPrime1)) goto done;
    if (!der_buffer_wrap(&rsa_key, 0x30, &rsa_contents)) goto done;
    if (!der_buffer_add_oid(&rsa_algorithm_contents, oid_rsa_encryption, sizeof(oid_rsa_encryption))) goto done;
    if (!der_buffer_add_tlv(&rsa_algorithm_contents, 0x05, &null_value, 0) || !der_buffer_wrap(&rsa_algorithm, 0x30, &rsa_algorithm_contents)) goto done;
    if (!der_buffer_add_integer(&private_contents, 0) || !der_buffer_append(&private_contents, rsa_algorithm.data, rsa_algorithm.size)) goto done;
    if (!der_buffer_add_tlv(&private_contents, 0x04, rsa_key.data, rsa_key.size) || !der_buffer_wrap(private_key_info, 0x30, &private_contents)) goto done;
    ret = TRUE;

done:
    der_buffer_free(&private_contents);
    der_buffer_free(&rsa_algorithm);
    der_buffer_free(&rsa_algorithm_contents);
    der_buffer_free(&rsa_key);
    der_buffer_free(&rsa_contents);
    return ret;
}

static BOOL add_certificate_bag(struct der_buffer *safe_contents, const BYTE *cert, DWORD cert_size)
{
    struct der_buffer cert_explicit_contents = {0}, cert_explicit = {0}, cert_bag_contents = {0}, cert_bag = {0};
    struct der_buffer safe_bag_contents = {0};
    BOOL ret;

    ret = der_buffer_add_tlv(&cert_explicit_contents, 0x04, cert, cert_size) && der_buffer_wrap(&cert_explicit, 0xa0, &cert_explicit_contents);
    if (ret) ret = der_buffer_add_oid(&cert_bag_contents, oid_x509_cert, sizeof(oid_x509_cert)) && der_buffer_append(&cert_bag_contents, cert_explicit.data, cert_explicit.size);
    if (ret) ret = der_buffer_wrap(&cert_bag, 0x30, &cert_bag_contents);
    if (ret) ret = der_buffer_add_oid(&safe_bag_contents, oid_cert_bag, sizeof(oid_cert_bag)) && der_buffer_add_tlv(&safe_bag_contents, 0xa0, cert_bag.data, cert_bag.size);
    if (ret) ret = der_buffer_add_tlv(safe_contents, 0x30, safe_bag_contents.data, safe_bag_contents.size);
    der_buffer_free(&safe_bag_contents);
    der_buffer_free(&cert_bag);
    der_buffer_free(&cert_bag_contents);
    der_buffer_free(&cert_explicit);
    der_buffer_free(&cert_explicit_contents);
    return ret;
}

static BOOL add_private_key_bag(struct der_buffer *safe_contents, const BYTE *key_blob, DWORD key_blob_size, const WCHAR *password)
{
    BYTE salt[16], *encrypted = NULL;
    DWORD encrypted_size, iterations = 2048;
    struct der_buffer private_key_info = {0}, algorithm = {0}, encrypted_info_contents = {0}, encrypted_info = {0};
    struct der_buffer safe_bag_contents = {0};
    BOOL ret = FALSE;

    if (!generate_random(salt, sizeof(salt))) goto done;
    if (!build_rsa_private_key(key_blob, key_blob_size, &private_key_info)) goto done;
    if (!encrypt_pbe_3des(password, private_key_info.data, private_key_info.size, salt, sizeof(salt), iterations, &encrypted, &encrypted_size)) goto done;
    if (!build_algorithm_identifier(&algorithm, oid_pbe_sha1_3des, sizeof(oid_pbe_sha1_3des), salt, sizeof(salt), iterations)) goto done;
    if (!der_buffer_append(&encrypted_info_contents, algorithm.data, algorithm.size) || !der_buffer_add_tlv(&encrypted_info_contents, 0x04, encrypted, encrypted_size)) goto done;
    if (!der_buffer_wrap(&encrypted_info, 0x30, &encrypted_info_contents)) goto done;
    if (!der_buffer_add_oid(&safe_bag_contents, oid_shrouded_key_bag, sizeof(oid_shrouded_key_bag)) || !der_buffer_add_tlv(&safe_bag_contents, 0xa0, encrypted_info.data, encrypted_info.size)) goto done;
    if (!der_buffer_add_tlv(safe_contents, 0x30, safe_bag_contents.data, safe_bag_contents.size)) goto done;
    ret = TRUE;

done:
    der_buffer_free(&safe_bag_contents);
    der_buffer_free(&encrypted_info);
    der_buffer_free(&encrypted_info_contents);
    der_buffer_free(&algorithm);
    der_buffer_free(&private_key_info);
    HeapFree(GetProcessHeap(), 0, encrypted);
    return ret;
}

static BOOL build_data_content_info(struct der_buffer *output, const BYTE *data, DWORD size)
{
    struct der_buffer explicit_contents = {0}, explicit_value = {0}, content_info_contents = {0};
    BOOL ret;

    ret = der_buffer_add_tlv(&explicit_contents, 0x04, data, size) && der_buffer_wrap(&explicit_value, 0xa0, &explicit_contents);
    if (ret) ret = der_buffer_add_oid(&content_info_contents, oid_data, sizeof(oid_data)) && der_buffer_append(&content_info_contents, explicit_value.data, explicit_value.size);
    if (ret) ret = der_buffer_wrap(output, 0x30, &content_info_contents);
    der_buffer_free(&content_info_contents);
    der_buffer_free(&explicit_value);
    der_buffer_free(&explicit_contents);
    return ret;
}

static BOOL build_mac_data(struct der_buffer *output, const WCHAR *password, const BYTE *authenticated_safe, DWORD authenticated_safe_size)
{
    BYTE salt[16], key[32], digest[32], *password_bytes = NULL;
    DWORD password_size, iterations = 2048;
    struct der_buffer algorithm_contents = {0}, algorithm = {0}, digest_info_contents = {0}, digest_info = {0}, mac_contents = {0};
    BYTE null_value = 0;
    BOOL ret = FALSE;

    if (!generate_random(salt, sizeof(salt)) || !pkcs12_password(password, &password_bytes, &password_size)) goto done;
    if (!pkcs12_derive(BCRYPT_SHA256_ALGORITHM, sizeof(digest), password_bytes, password_size, salt, sizeof(salt), 3, iterations, key, sizeof(key))) goto done;
    if (!pkcs12_hmac(BCRYPT_SHA256_ALGORITHM, key, sizeof(key), authenticated_safe, authenticated_safe_size, digest, sizeof(digest))) goto done;
    if (!der_buffer_add_oid(&algorithm_contents, oid_sha256, sizeof(oid_sha256)) || !der_buffer_add_tlv(&algorithm_contents, 0x05, &null_value, 0)) goto done;
    if (!der_buffer_wrap(&algorithm, 0x30, &algorithm_contents)) goto done;
    if (!der_buffer_append(&digest_info_contents, algorithm.data, algorithm.size) || !der_buffer_add_tlv(&digest_info_contents, 0x04, digest, sizeof(digest))) goto done;
    if (!der_buffer_wrap(&digest_info, 0x30, &digest_info_contents)) goto done;
    if (!der_buffer_append(&mac_contents, digest_info.data, digest_info.size) || !der_buffer_add_tlv(&mac_contents, 0x04, salt, sizeof(salt))) goto done;
    if (!der_buffer_add_integer(&mac_contents, iterations) || !der_buffer_wrap(output, 0x30, &mac_contents)) goto done;
    ret = TRUE;

done:
    der_buffer_free(&mac_contents);
    der_buffer_free(&digest_info);
    der_buffer_free(&digest_info_contents);
    der_buffer_free(&algorithm);
    der_buffer_free(&algorithm_contents);
    HeapFree(GetProcessHeap(), 0, password_bytes);
    return ret;
}

static BOOL build_pfx(const struct export_cert_store_params *params, struct der_buffer *pfx)
{
    struct der_buffer safe_contents_body = {0}, safe_contents = {0}, safe_content_info = {0};
    struct der_buffer authenticated_safe_body = {0}, authenticated_safe = {0}, auth_safe_info = {0}, mac_data = {0}, pfx_contents = {0};
    BOOL ret = FALSE;

    if (params->cert_data && params->cert_size && !add_certificate_bag(&safe_contents_body, params->cert_data, params->cert_size)) goto done;
    if (params->key_blob && params->key_blob_size && !add_private_key_bag(&safe_contents_body, params->key_blob, params->key_blob_size, params->password)) goto done;
    if (!der_buffer_wrap(&safe_contents, 0x30, &safe_contents_body)) goto done;
    if (!build_data_content_info(&safe_content_info, safe_contents.data, safe_contents.size)) goto done;
    if (!der_buffer_append(&authenticated_safe_body, safe_content_info.data, safe_content_info.size) || !der_buffer_wrap(&authenticated_safe, 0x30, &authenticated_safe_body)) goto done;
    if (!build_data_content_info(&auth_safe_info, authenticated_safe.data, authenticated_safe.size)) goto done;
    if (!build_mac_data(&mac_data, params->password, authenticated_safe.data, authenticated_safe.size)) goto done;
    if (!der_buffer_add_integer(&pfx_contents, 3) || !der_buffer_append(&pfx_contents, auth_safe_info.data, auth_safe_info.size)) goto done;
    if (!der_buffer_append(&pfx_contents, mac_data.data, mac_data.size) || !der_buffer_wrap(pfx, 0x30, &pfx_contents)) goto done;
    ret = TRUE;

done:
    der_buffer_free(&pfx_contents);
    der_buffer_free(&mac_data);
    der_buffer_free(&auth_safe_info);
    der_buffer_free(&authenticated_safe);
    der_buffer_free(&authenticated_safe_body);
    der_buffer_free(&safe_content_info);
    der_buffer_free(&safe_contents);
    der_buffer_free(&safe_contents_body);
    return ret;
}

static
NTSTATUS
OpenCertStore(
    CRYPT_DATA_BLOB *pfx,
    const WCHAR *password,
    cert_store_data_t *data_ret,
    unsigned int *key_count_ret)
{
    struct reactos_cert_store_data *data;
    struct der_view input, pfx_sequence, auth_safe_info, auth_safe_value, authenticated_safe, mac_data;
    DWORD version, auth_safe_size;

    *data_ret = 0;
    *key_count_ret = 0;
    if (!pfx || !pfx->pbData || !pfx->cbData) return STATUS_INVALID_PARAMETER;
    if (!(data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*data)))) return STATUS_NO_MEMORY;
    input.data = pfx->pbData;
    input.size = pfx->cbData;
    if (!der_read(&input, 0x30, &pfx_sequence) || input.size || !der_read_integer(&pfx_sequence, &version) || version != 3) goto invalid;
    auth_safe_info = pfx_sequence;
    if (!der_read(&pfx_sequence, 0x30, &auth_safe_value)) goto invalid;
    auth_safe_size = auth_safe_info.size - pfx_sequence.size;
    auth_safe_info.size = auth_safe_size;
    if (!parse_data_content_info(&auth_safe_info, &authenticated_safe)) goto invalid;
    if (pfx_sequence.size)
    {
        mac_data = pfx_sequence;
        if (!verify_pfx_mac(&mac_data, password, authenticated_safe.data, authenticated_safe.size)) goto invalid;
    }
    if (!parse_authenticated_safe(data, authenticated_safe.data, authenticated_safe.size, password)) goto invalid;
    *data_ret = (ULONG_PTR)data;
    *key_count_ret = data->key ? 1 : 0;
    return STATUS_SUCCESS;

invalid:
    free_cert_store_data(data);
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS __reactos_call_unix_open_cert_store(void* Args)
{
    struct open_cert_store_params* params = (struct open_cert_store_params*)Args;
    return OpenCertStore(params->pfx,
                         params->password,
                         params->data_ret,
                         params->key_count_ret);
}

static
NTSTATUS
CloseCertStore(cert_store_data_t data)
{
    free_cert_store_data((struct reactos_cert_store_data *)(ULONG_PTR)data);
    return STATUS_SUCCESS;
}

NTSTATUS __reactos_call_unix_close_cert_store(void* Args)
{
    struct close_cert_store_params* params = (struct close_cert_store_params*)Args;
    return CloseCertStore(params->data);
}

static
NTSTATUS
ImportStoreKey(
    cert_store_data_t data,
    void *buf,
    DWORD *buf_size)
{
    struct reactos_cert_store_data *store = (struct reactos_cert_store_data *)(ULONG_PTR)data;

    if (!store || !store->key) return STATUS_NOT_FOUND;
    if (!buf || *buf_size < store->key_size)
    {
        *buf_size = store->key_size;
        return STATUS_BUFFER_TOO_SMALL;
    }
    memcpy(buf, store->key, store->key_size);
    *buf_size = store->key_size;
    return STATUS_SUCCESS;
}

NTSTATUS __reactos_call_unix_import_store_key(void* Args)
{
    struct import_store_key_params* params = (struct import_store_key_params*)Args;
    return ImportStoreKey(params->data,
                          params->buf,
                          params->buf_size);
}

static
NTSTATUS
ImportStoreCert(
    cert_store_data_t data,
    unsigned int index,
    void *buf,
    DWORD *buf_size)
{
    struct reactos_cert_store_data *store = (struct reactos_cert_store_data *)(ULONG_PTR)data;

    if (!store || index >= store->cert_count) return STATUS_NO_MORE_ENTRIES;
    if (!buf || *buf_size < store->cert_sizes[index])
    {
        *buf_size = store->cert_sizes[index];
        return STATUS_BUFFER_TOO_SMALL;
    }
    memcpy(buf, store->certs[index], store->cert_sizes[index]);
    *buf_size = store->cert_sizes[index];
    return STATUS_SUCCESS;
}

NTSTATUS __reactos_call_unix_import_store_cert(void* Args)
{
    struct import_store_cert_params* params = (struct import_store_cert_params*)Args;
    return ImportStoreCert(params->data,
                           params->index,
                           params->buf,
                           params->buf_size);
}

NTSTATUS __reactos_call_unix_export_cert_store(void* Args)
{
    struct export_cert_store_params *params = (struct export_cert_store_params *)Args;
    struct der_buffer pfx = {0};
    NTSTATUS status = STATUS_SUCCESS;

    if (!params || !params->pfx_size) return STATUS_INVALID_PARAMETER;
    if (!build_pfx(params, &pfx)) return STATUS_INVALID_PARAMETER;
    if (!params->pfx_data)
    {
        *params->pfx_size = pfx.size;
    }
    else if (*params->pfx_size < pfx.size)
    {
        *params->pfx_size = pfx.size;
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        memcpy(params->pfx_data, pfx.data, pfx.size);
        *params->pfx_size = pfx.size;
    }
    der_buffer_free(&pfx);
    return status;
}
