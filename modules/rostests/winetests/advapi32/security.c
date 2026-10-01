/*
 * Unit tests for security functions
 *
 * Copyright (c) 2004 Mike McCormack
 * Copyright (c) 2011 Dmitry Timoshkov
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <stdio.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winternl.h"
#ifdef __REACTOS__
#include <limits.h>
#include "lmaccess.h"
#include "lmerr.h"
#include "winioctl.h"
#endif
#include "aclapi.h"
#ifdef __REACTOS__
#include "objbase.h"
#include "iads.h"
#endif
#include "winnt.h"
#include "sddl.h"
#include "ntsecapi.h"
#include "lmcons.h"

#include "wine/test.h"

#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

/* PROCESS_ALL_ACCESS in Vista+ PSDKs is incompatible with older Windows versions */
#define PROCESS_ALL_ACCESS_NT4 (PROCESS_ALL_ACCESS & ~0xf000)
#define PROCESS_ALL_ACCESS_VISTA (PROCESS_ALL_ACCESS | 0xf000)

#ifndef EVENT_QUERY_STATE
#define EVENT_QUERY_STATE 0x0001
#endif

#ifndef SEMAPHORE_QUERY_STATE
#define SEMAPHORE_QUERY_STATE 0x0001
#endif

#ifndef THREAD_SET_LIMITED_INFORMATION
#define THREAD_SET_LIMITED_INFORMATION 0x0400
#define THREAD_QUERY_LIMITED_INFORMATION 0x0800
#endif

#define THREAD_ALL_ACCESS_NT4 (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3ff)
#define THREAD_ALL_ACCESS_VISTA (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0xffff)

#define expect_eq(expr, value, type, format) { type ret_ = expr; ok((value) == ret_, #expr " expected " format "  got " format "\n", (value), (ret_)); }

static BOOL (WINAPI *pAddMandatoryAce)(PACL,DWORD,DWORD,DWORD,PSID);
static VOID (WINAPI *pBuildTrusteeWithSidA)( PTRUSTEEA pTrustee, PSID pSid );
static VOID (WINAPI *pBuildTrusteeWithNameA)( PTRUSTEEA pTrustee, LPSTR pName );
static VOID (WINAPI *pBuildTrusteeWithObjectsAndNameA)( PTRUSTEEA pTrustee,
                                                          POBJECTS_AND_NAME_A pObjName,
                                                          SE_OBJECT_TYPE ObjectType,
                                                          LPSTR ObjectTypeName,
                                                          LPSTR InheritedObjectTypeName,
                                                          LPSTR Name );
static VOID (WINAPI *pBuildTrusteeWithObjectsAndSidA)( PTRUSTEEA pTrustee,
                                                         POBJECTS_AND_SID pObjSid,
                                                         GUID* pObjectGuid,
                                                         GUID* pInheritedObjectGuid,
                                                         PSID pSid );
static LPSTR (WINAPI *pGetTrusteeNameA)( PTRUSTEEA pTrustee );
static DWORD (WINAPI *pRtlAdjustPrivilege)(ULONG,BOOLEAN,BOOLEAN,PBOOLEAN);
static NTSTATUS (WINAPI *pNtAccessCheck)(PSECURITY_DESCRIPTOR, HANDLE, ACCESS_MASK, PGENERIC_MAPPING,
                                         PPRIVILEGE_SET, PULONG, PULONG, NTSTATUS*);
static BOOL     (WINAPI *pRtlDosPathNameToNtPathName_U)(LPCWSTR,PUNICODE_STRING,PWSTR*,CURDIR*);

static HMODULE hmod;
static int     myARGC;
static char**  myARGV;

static const char* debugstr_sid(PSID sid)
{
    LPSTR sidstr;
    DWORD le = GetLastError();
    const char *res;

    if (!ConvertSidToStringSidA(sid, &sidstr))
        res = wine_dbg_sprintf("ConvertSidToStringSidA failed le=%lu", GetLastError());
    else
    {
        res = __wine_dbg_strdup(sidstr);
        LocalFree(sidstr);
    }
    /* Restore the last error in case ConvertSidToStringSidA() modified it */
    SetLastError(le);
    return res;
}

struct sidRef
{
    SID_IDENTIFIER_AUTHORITY auth;
    const char *refStr;
};

static void init(void)
{
    HMODULE hntdll;

    hntdll = GetModuleHandleA("ntdll.dll");
    pNtAccessCheck = (void *)GetProcAddress( hntdll, "NtAccessCheck" );
    pRtlDosPathNameToNtPathName_U = (void *)GetProcAddress(hntdll, "RtlDosPathNameToNtPathName_U");

    hmod = GetModuleHandleA("advapi32.dll");
    pAddMandatoryAce = (void *)GetProcAddress(hmod, "AddMandatoryAce");

    myARGC = winetest_get_mainargs( &myARGV );
}

static SECURITY_DESCRIPTOR* test_get_security_descriptor(HANDLE handle, int line)
{
    /* use free(sd); when done */
    DWORD ret, length, needed;
    SECURITY_DESCRIPTOR *sd;

    needed = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = GetKernelObjectSecurity(handle, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                  NULL, 0, &needed);
    ok_(__FILE__, line)(!ret, "GetKernelObjectSecurity should fail\n");
    ok_(__FILE__, line)(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());
    ok_(__FILE__, line)(needed != 0xdeadbeef, "GetKernelObjectSecurity should return required buffer length\n");

    length = needed;
    sd = malloc(length);

    needed = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = GetKernelObjectSecurity(handle, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                  sd, length, &needed);
    ok_(__FILE__, line)(ret, "GetKernelObjectSecurity error %ld\n", GetLastError());
    ok_(__FILE__, line)(needed == length || needed == 0 /* file, pipe */, "GetKernelObjectSecurity should return %lu instead of %lu\n", length, needed);
    return sd;
}

static void test_owner_equal(HANDLE Handle, PSID expected, int line)
{
    BOOL res;
    SECURITY_DESCRIPTOR *queriedSD = NULL;
    PSID owner;
    BOOL owner_defaulted;

    queriedSD = test_get_security_descriptor( Handle, line );

    res = GetSecurityDescriptorOwner(queriedSD, &owner, &owner_defaulted);
    ok_(__FILE__, line)(res, "GetSecurityDescriptorOwner failed with error %ld\n", GetLastError());

    ok_(__FILE__, line)(EqualSid(owner, expected), "Owner SIDs are not equal %s != %s\n",
                        debugstr_sid(owner), debugstr_sid(expected));
    ok_(__FILE__, line)(!owner_defaulted, "Defaulted is true\n");

    free(queriedSD);
}

static void test_group_equal(HANDLE Handle, PSID expected, int line)
{
    BOOL res;
    SECURITY_DESCRIPTOR *queriedSD = NULL;
    PSID group;
    BOOL group_defaulted;

    queriedSD = test_get_security_descriptor( Handle, line );

    res = GetSecurityDescriptorGroup(queriedSD, &group, &group_defaulted);
    ok_(__FILE__, line)(res, "GetSecurityDescriptorGroup failed with error %ld\n", GetLastError());

    ok_(__FILE__, line)(EqualSid(group, expected), "Group SIDs are not equal %s != %s\n",
                        debugstr_sid(group), debugstr_sid(expected));
    ok_(__FILE__, line)(!group_defaulted, "Defaulted is true\n");

    free(queriedSD);
}

static void test_ConvertStringSidToSid(void)
{
    struct sidRef refs[] = {
     { { {0x00,0x00,0x33,0x44,0x55,0x66} }, "S-1-860116326-1" },
     { { {0x00,0x00,0x01,0x02,0x03,0x04} }, "S-1-16909060-1"  },
     { { {0x00,0x00,0x00,0x01,0x02,0x03} }, "S-1-66051-1"     },
     { { {0x00,0x00,0x00,0x00,0x01,0x02} }, "S-1-258-1"       },
     { { {0x00,0x00,0x00,0x00,0x00,0x02} }, "S-1-2-1"         },
     { { {0x00,0x00,0x00,0x00,0x00,0x0c} }, "S-1-12-1"        },
    };
    static const struct
    {
        const char *name;
        const char *sid;
        unsigned int optional;
    }
    str_to_sid_tests[] =
    {
        { "WD", "S-1-1-0" },
        { "wD", "S-1-1-0" },
        { "CO", "S-1-3-0" },
        { "CG", "S-1-3-1" },
        { "OW", "S-1-3-4", 1 }, /* Vista+ */
        { "NU", "S-1-5-2" },
        { "IU", "S-1-5-4" },
        { "SU", "S-1-5-6" },
        { "AN", "S-1-5-7" },
        { "ED", "S-1-5-9" },
        { "PS", "S-1-5-10" },
        { "AU", "S-1-5-11" },
        { "RC", "S-1-5-12" },
        { "SY", "S-1-5-18" },
        { "LS", "S-1-5-19" },
        { "NS", "S-1-5-20" },
        { "LA", "S-1-5-21-*-*-*-500" },
        { "LG", "S-1-5-21-*-*-*-501" },
        { "BO", "S-1-5-32-551" },
        { "BA", "S-1-5-32-544" },
        { "BU", "S-1-5-32-545" },
        { "BG", "S-1-5-32-546" },
        { "PU", "S-1-5-32-547" },
        { "AO", "S-1-5-32-548" },
        { "SO", "S-1-5-32-549" },
        { "PO", "S-1-5-32-550" },
        { "RE", "S-1-5-32-552" },
        { "RU", "S-1-5-32-554" },
        { "RD", "S-1-5-32-555" },
        { "NO", "S-1-5-32-556" },
        { "AC", "S-1-15-2-1", 1 }, /* Win8+ */
        { "CA", "", 1 },
        { "DA", "", 1 },
        { "DC", "", 1 },
        { "DD", "", 1 },
        { "DG", "", 1 },
        { "DU", "", 1 },
        { "EA", "", 1 },
        { "PA", "", 1 },
        { "RS", "", 1 },
        { "SA", "", 1 },
        { "s-1-12-1", "S-1-12-1" },
        { "S-0x1-0XC-0x1a", "S-1-12-26" },
    };

    const char noSubAuthStr[] = "S-1-5";
    unsigned int i;
    PSID psid = NULL;
    SID *pisid;
    BOOL r, ret;
    LPSTR str = NULL;

    r = ConvertStringSidToSidA( NULL, NULL );
    ok( !r, "expected failure with NULL parameters\n" );
    if( GetLastError() == ERROR_CALL_NOT_IMPLEMENTED )
        return;
    ok( GetLastError() == ERROR_INVALID_PARAMETER,
     "expected GetLastError() is ERROR_INVALID_PARAMETER, got %ld\n",
     GetLastError() );

    r = ConvertStringSidToSidA( refs[0].refStr, NULL );
    ok( !r && GetLastError() == ERROR_INVALID_PARAMETER,
     "expected GetLastError() is ERROR_INVALID_PARAMETER, got %ld\n",
     GetLastError() );

    r = ConvertStringSidToSidA( NULL, &psid );
    ok( !r && GetLastError() == ERROR_INVALID_PARAMETER,
     "expected GetLastError() is ERROR_INVALID_PARAMETER, got %ld\n",
     GetLastError() );

    r = ConvertStringSidToSidA( noSubAuthStr, &psid );
    ok( !r,
     "expected failure with no sub authorities\n" );
    ok( GetLastError() == ERROR_INVALID_SID,
     "expected GetLastError() is ERROR_INVALID_SID, got %ld\n",
     GetLastError() );

    r = ConvertStringSidToSidA( "WDandmorecharacters", &psid );
    ok( !r,
     "expected failure with too many characters\n" );
    ok( GetLastError() == ERROR_INVALID_SID,
     "expected GetLastError() is ERROR_INVALID_SID, got %ld\n",
     GetLastError() );

    r = ConvertStringSidToSidA( "WD)", &psid );
    ok( !r,
     "expected failure with too many characters\n" );
    ok( GetLastError() == ERROR_INVALID_SID,
     "expected GetLastError() is ERROR_INVALID_SID, got %ld\n",
     GetLastError() );

    ok(ConvertStringSidToSidA("S-1-5-21-93476-23408-4576", &psid), "ConvertStringSidToSidA failed\n");
    pisid = psid;
    ok(pisid->SubAuthorityCount == 4, "Invalid sub authority count - expected 4, got %d\n", pisid->SubAuthorityCount);
    ok(pisid->SubAuthority[0] == 21, "Invalid subauthority 0 - expected 21, got %ld\n", pisid->SubAuthority[0]);
    ok(*GetSidSubAuthority(pisid, 3) == 4576, "Invalid subauthority 3 - expected 4576, got %ld\n", *GetSidSubAuthority(pisid, 3));
    LocalFree(str);
    LocalFree(psid);

    for( i = 0; i < ARRAY_SIZE(refs); i++ )
    {
        r = AllocateAndInitializeSid( &refs[i].auth, 1,1,0,0,0,0,0,0,0,
         &psid );
        ok( r, "failed to allocate sid\n" );
        r = ConvertSidToStringSidA( psid, &str );
        ok( r, "failed to convert sid\n" );
        if (r)
        {
            ok( !strcmp( str, refs[i].refStr ),
                "incorrect sid, expected %s, got %s\n", refs[i].refStr, str );
            LocalFree( str );
        }
        if( psid )
            FreeSid( psid );

        r = ConvertStringSidToSidA( refs[i].refStr, &psid );
        ok( r, "failed to parse sid string\n" );
        pisid = psid;
        ok( pisid &&
         !memcmp( pisid->IdentifierAuthority.Value, refs[i].auth.Value,
         sizeof(refs[i].auth) ),
         "string sid %s didn't parse to expected value\n"
         "(got 0x%04x%08lx, expected 0x%04x%08lx)\n",
         refs[i].refStr,
         MAKEWORD( pisid->IdentifierAuthority.Value[1],
         pisid->IdentifierAuthority.Value[0] ),
         MAKELONG( MAKEWORD( pisid->IdentifierAuthority.Value[5],
         pisid->IdentifierAuthority.Value[4] ),
         MAKEWORD( pisid->IdentifierAuthority.Value[3],
         pisid->IdentifierAuthority.Value[2] ) ),
         MAKEWORD( refs[i].auth.Value[1], refs[i].auth.Value[0] ),
         MAKELONG( MAKEWORD( refs[i].auth.Value[5], refs[i].auth.Value[4] ),
         MAKEWORD( refs[i].auth.Value[3], refs[i].auth.Value[2] ) ) );
        if( psid )
            LocalFree( psid );
    }

    for (i = 0; i < ARRAY_SIZE(str_to_sid_tests); i++)
    {
        char *str;

        ret = ConvertStringSidToSidA(str_to_sid_tests[i].name, &psid);
        if (!ret && str_to_sid_tests[i].optional)
        {
            skip("%u: failed to convert %s.\n", i, str_to_sid_tests[i].name);
            continue;
        }
        ok(ret, "%u: failed to convert string to sid.\n", i);

        if (str_to_sid_tests[i].optional || !strcmp(str_to_sid_tests[i].name, "LA") ||
            !strcmp(str_to_sid_tests[i].name, "LG"))
        {
            LocalFree(psid);
            continue;
        }

        ret = ConvertSidToStringSidA(psid, &str);
        ok(ret, "%u: failed to convert SID to string.\n", i);
        ok(!strcmp(str, str_to_sid_tests[i].sid), "%u: unexpected sid %s.\n", i, str);
        LocalFree(psid);
        LocalFree(str);
    }
}

#ifdef __REACTOS__
static void test_sddl_domain_aliases(void)
{
    static const struct
    {
        const WCHAR *name;
        DWORD rid;
        BOOL local;
        BOOL invalid_revision;
    }
    aliases[] =
    {
        { L"LA", DOMAIN_USER_RID_ADMIN, TRUE, FALSE },
        { L"LG", DOMAIN_USER_RID_GUEST, TRUE, FALSE },
        { L"CA", DOMAIN_GROUP_RID_CERT_ADMINS, FALSE, FALSE },
        { L"DA", DOMAIN_GROUP_RID_ADMINS, FALSE, FALSE },
        { L"DC", DOMAIN_GROUP_RID_COMPUTERS, FALSE, FALSE },
        { L"DD", DOMAIN_GROUP_RID_CONTROLLERS, FALSE, FALSE },
        { L"DG", DOMAIN_GROUP_RID_GUESTS, FALSE, FALSE },
        { L"DU", DOMAIN_GROUP_RID_USERS, FALSE, FALSE },
        { L"EA", DOMAIN_GROUP_RID_ENTERPRISE_ADMINS, FALSE, FALSE },
        { L"PA", DOMAIN_GROUP_RID_POLICY_ADMINS, FALSE, FALSE },
        { L"RS", DOMAIN_ALIAS_RID_RAS_SERVERS, FALSE, FALSE },
        { L"SA", DOMAIN_GROUP_RID_SCHEMA_ADMINS, FALSE, FALSE },
        { L"ZZ", 0, FALSE, FALSE },
        { L"S-0-5-1", 0, FALSE, TRUE },
    };
    static const WCHAR *prefixes[] = { L"", L"O:", L"G:", L"D:(A;;GR;;;" };
    static const SID invalid_revision_sid = {0, 1, {SECURITY_NT_AUTHORITY}, {1}};
    LSA_OBJECT_ATTRIBUTES attributes = {0};
    LSA_HANDLE policy = NULL;
    POLICY_ACCOUNT_DOMAIN_INFO *account = NULL;
    POLICY_DNS_DOMAIN_INFO *domain = NULL;
    DWORD expected_buffer[SECURITY_MAX_SID_SIZE / sizeof(DWORD)];
    PSID base, expected = expected_buffer, sid;
    PSECURITY_DESCRIPTOR sd;
    SECURITY_DESCRIPTOR_RELATIVE *relative;
    ACCESS_ALLOWED_ACE *ace;
    ACL *acl;
    WCHAR string[40];
    DWORD error, size, expected_size, offset;
    unsigned int i, j, count;
    NTSTATUS status;
    BOOL ret, valid, present, defaulted, expect_success;

    attributes.Length = sizeof(attributes);
    status = LsaOpenPolicy(NULL, &attributes, POLICY_VIEW_LOCAL_INFORMATION, &policy);
    ok(status == STATUS_SUCCESS, "LsaOpenPolicy returned %#lx.\n", status);
    if (status != STATUS_SUCCESS) return;

    status = LsaQueryInformationPolicy(policy, PolicyAccountDomainInformation, (void **)&account);
    ok(status == STATUS_SUCCESS, "Account domain query returned %#lx.\n", status);
    if (status != STATUS_SUCCESS) goto done;
    valid = account && account->DomainSid && IsValidSid(account->DomainSid);
    ok(valid, "Account domain query did not return a valid SID.\n");
    if (!valid) goto done;

    status = LsaQueryInformationPolicy(policy, PolicyDnsDomainInformation, (void **)&domain);
    ok(status == STATUS_SUCCESS, "DNS domain query returned %#lx.\n", status);
    if (status != STATUS_SUCCESS) goto done;
    valid = domain && (!domain->Sid || IsValidSid(domain->Sid));
    ok(valid, "DNS domain query did not return valid domain information.\n");
    if (!valid) goto done;

    for (i = 0; i < ARRAY_SIZE(aliases); ++i)
    {
        base = !aliases[i].rid ? NULL : aliases[i].local ? account->DomainSid : domain->Sid;
        if (base)
        {
            count = *GetSidSubAuthorityCount(base);
            ok(count < SID_MAX_SUB_AUTHORITIES, "Domain SID has %u subauthorities.\n", count);
            if (count >= SID_MAX_SUB_AUTHORITIES) goto done;
            ret = CopySid(sizeof(expected_buffer), expected, base);
            ok(ret, "CopySid failed: %lu.\n", GetLastError());
            if (!ret) goto done;
            *GetSidSubAuthorityCount(expected) = count + 1;
            *GetSidSubAuthority(expected, count) = aliases[i].rid;
        }

        for (j = 0; j < ARRAY_SIZE(prefixes); ++j)
        {
            winetest_push_context("alias %s form %u", wine_dbgstr_w(aliases[i].name), j);
            lstrcpyW(string, prefixes[j]);
            lstrcatW(string, aliases[i].name);
            if (j == 3) lstrcatW(string, L")");
            sid = NULL;
            sd = NULL;
            size = 0xdeadbeef;
            SetLastError(0xdeadbeef);
            if (!j)
                ret = ConvertStringSidToSidW(string, &sid);
            else
                ret = ConvertStringSecurityDescriptorToSecurityDescriptorW(string, SDDL_REVISION_1, &sd, &size);
            error = GetLastError();
            expect_success = base || (aliases[i].invalid_revision && j < 3);
            ok(ret == expect_success, "Conversion returned %d, error %lu, expected success %u.\n",
               ret, error, expect_success);
            ok(error == (expect_success ? ERROR_SUCCESS : ERROR_INVALID_SID),
               "Conversion error %lu, expected %lu.\n", error,
               (DWORD)(expect_success ? ERROR_SUCCESS : ERROR_INVALID_SID));
            if (!ret)
            {
                ok(!sid && !sd, "Failed conversion returned SID %p, descriptor %p.\n", sid, sd);
                if (j) ok(!size, "Failed conversion returned size %lu.\n", size);
            }

            if (ret && j && aliases[i].invalid_revision)
            {
                expected_size = sizeof(*relative) + sizeof(invalid_revision_sid);
                ok(sd != NULL, "Revision-zero conversion returned no descriptor.\n");
                ok(size == expected_size, "Revision-zero descriptor size %lu, expected %lu.\n",
                   size, expected_size);
                if (sd && size == expected_size)
                {
                    ok(!IsValidSecurityDescriptor(sd), "Revision-zero descriptor was accepted as valid.\n");
                    relative = sd;
                    ok(relative->Revision == SECURITY_DESCRIPTOR_REVISION &&
                       (relative->Control & SE_SELF_RELATIVE),
                       "Descriptor revision %u, control %#x.\n", relative->Revision, relative->Control);
                    offset = j == 1 ? relative->Owner : relative->Group;
                    ok(offset == sizeof(*relative), "Revision-zero SID offset %lu, expected %u.\n",
                       offset, (unsigned int)sizeof(*relative));
                    if (offset == sizeof(*relative)) sid = (BYTE *)sd + offset;
                }
            }
            else if (ret && j)
            {
                valid = sd && IsValidSecurityDescriptor(sd);
                ok(valid, "Conversion returned an invalid descriptor.\n");
                if (valid)
                {
                    ok(size == GetSecurityDescriptorLength(sd), "Descriptor size %lu, actual %lu.\n",
                       size, GetSecurityDescriptorLength(sd));
                    if (base)
                    {
                        expected_size = sizeof(SECURITY_DESCRIPTOR_RELATIVE) + GetLengthSid(expected);
                        if (j == 3) expected_size += sizeof(ACL) + FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart);
                        ok(size == expected_size, "Descriptor size %lu, expected %lu.\n", size, expected_size);
                    }
                    if (j == 1)
                    {
                        valid = GetSecurityDescriptorOwner(sd, &sid, &defaulted);
                        ok(valid, "Owner query failed: %lu.\n", GetLastError());
                    }
                    else if (j == 2)
                    {
                        valid = GetSecurityDescriptorGroup(sd, &sid, &defaulted);
                        ok(valid, "Group query failed: %lu.\n", GetLastError());
                    }
                    else
                    {
                        acl = NULL;
                        present = FALSE;
                        valid = GetSecurityDescriptorDacl(sd, &present, &acl, &defaulted);
                        valid = valid && present && acl && IsValidAcl(acl) && acl->AceCount == 1;
                        ok(valid, "Conversion did not return a one-ACE DACL.\n");
                        if (valid)
                        {
                            valid = GetAce(acl, 0, (void **)&ace);
                            ok(valid, "ACE query failed: %lu.\n", GetLastError());
                            if (valid)
                            {
                                ok(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
                                   !ace->Header.AceFlags && ace->Mask == GENERIC_READ,
                                   "ACE type %u, flags %#x, mask %#lx.\n", ace->Header.AceType,
                                   ace->Header.AceFlags, ace->Mask);
                                sid = &ace->SidStart;
                            }
                        }
                    }
                }
            }

            if (ret)
            {
                valid = sid && IsValidSid(sid);
                if (aliases[i].invalid_revision)
                {
                    ok(sid != NULL, "Revision-zero conversion returned no SID.\n");
                    ok(!valid, "Revision-zero SID was accepted as valid.\n");
                    if (sid)
                        ok(!memcmp(sid, &invalid_revision_sid, sizeof(invalid_revision_sid)),
                           "Revision-zero SID bytes differ.\n");
                }
                else
                {
                    ok(valid, "Conversion did not return a valid SID.\n");
                    if (valid && base)
                        ok(EqualSid(sid, expected), "SID %s differs from expected %s.\n",
                           debugstr_sid(sid), debugstr_sid(expected));
                }
                if (!j) LocalFree(sid);
                else LocalFree(sd);
            }
            winetest_pop_context();
        }
    }

done:
    if (domain) LsaFreeMemory(domain);
    if (account) LsaFreeMemory(account);
    LsaClose(policy);
}

#endif
static void test_trustee(void)
{
    GUID ObjectType = {0x12345678, 0x1234, 0x5678, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};
    GUID InheritedObjectType = {0x23456789, 0x2345, 0x6786, {0x2, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99}};
    GUID ZeroGuid;
    OBJECTS_AND_NAME_A oan;
    OBJECTS_AND_SID oas;
    TRUSTEEA trustee;
    PSID psid;
    char szObjectTypeName[] = "ObjectTypeName";
    char szInheritedObjectTypeName[] = "InheritedObjectTypeName";
    char szTrusteeName[] = "szTrusteeName";
    SID_IDENTIFIER_AUTHORITY auth = { {0x11,0x22,0,0,0, 0} };

    memset( &ZeroGuid, 0x00, sizeof (ZeroGuid) );

    pBuildTrusteeWithSidA = (void *)GetProcAddress( hmod, "BuildTrusteeWithSidA" );
    pBuildTrusteeWithNameA = (void *)GetProcAddress( hmod, "BuildTrusteeWithNameA" );
    pBuildTrusteeWithObjectsAndNameA = (void *)GetProcAddress (hmod, "BuildTrusteeWithObjectsAndNameA" );
    pBuildTrusteeWithObjectsAndSidA = (void *)GetProcAddress (hmod, "BuildTrusteeWithObjectsAndSidA" );
    pGetTrusteeNameA = (void *)GetProcAddress (hmod, "GetTrusteeNameA" );
    if( !pBuildTrusteeWithSidA || !pBuildTrusteeWithNameA ||
        !pBuildTrusteeWithObjectsAndNameA || !pBuildTrusteeWithObjectsAndSidA ||
        !pGetTrusteeNameA )
        return;

    if ( ! AllocateAndInitializeSid( &auth, 1, 42, 0,0,0,0,0,0,0,&psid ) )
    {
        trace( "failed to init SID\n" );
       return;
    }

    /* test BuildTrusteeWithSidA */
    memset( &trustee, 0xff, sizeof trustee );
    pBuildTrusteeWithSidA( &trustee, psid );

    ok( trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok( trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, 
        "MultipleTrusteeOperation wrong\n");
    ok( trustee.TrusteeForm == TRUSTEE_IS_SID, "TrusteeForm wrong\n");
    ok( trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok( trustee.ptstrName == psid, "ptstrName wrong\n" );

    /* test BuildTrusteeWithObjectsAndSidA (test 1) */
    memset( &trustee, 0xff, sizeof trustee );
    memset( &oas, 0xff, sizeof(oas) );
    pBuildTrusteeWithObjectsAndSidA(&trustee, &oas, &ObjectType,
                                    &InheritedObjectType, psid);

    ok(trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok(trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, "MultipleTrusteeOperation wrong\n");
    ok(trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_SID, "TrusteeForm wrong\n");
    ok(trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok(trustee.ptstrName == (LPSTR)&oas, "ptstrName wrong\n");
 
    ok(oas.ObjectsPresent == (ACE_OBJECT_TYPE_PRESENT | ACE_INHERITED_OBJECT_TYPE_PRESENT), "ObjectsPresent wrong\n");
    ok(!memcmp(&oas.ObjectTypeGuid, &ObjectType, sizeof(GUID)), "ObjectTypeGuid wrong\n");
    ok(!memcmp(&oas.InheritedObjectTypeGuid, &InheritedObjectType, sizeof(GUID)), "InheritedObjectTypeGuid wrong\n");
    ok(oas.pSid == psid, "pSid wrong\n");

    /* test GetTrusteeNameA */
    ok(pGetTrusteeNameA(&trustee) == (LPSTR)&oas, "GetTrusteeName returned wrong value\n");

    /* test BuildTrusteeWithObjectsAndSidA (test 2) */
    memset( &trustee, 0xff, sizeof trustee );
    memset( &oas, 0xff, sizeof(oas) );
    pBuildTrusteeWithObjectsAndSidA(&trustee, &oas, NULL,
                                    &InheritedObjectType, psid);

    ok(trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok(trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, "MultipleTrusteeOperation wrong\n");
    ok(trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_SID, "TrusteeForm wrong\n");
    ok(trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok(trustee.ptstrName == (LPSTR)&oas, "ptstrName wrong\n");
 
    ok(oas.ObjectsPresent == ACE_INHERITED_OBJECT_TYPE_PRESENT, "ObjectsPresent wrong\n");
    ok(!memcmp(&oas.ObjectTypeGuid, &ZeroGuid, sizeof(GUID)), "ObjectTypeGuid wrong\n");
    ok(!memcmp(&oas.InheritedObjectTypeGuid, &InheritedObjectType, sizeof(GUID)), "InheritedObjectTypeGuid wrong\n");
    ok(oas.pSid == psid, "pSid wrong\n");

    FreeSid( psid );

    /* test BuildTrusteeWithNameA */
    memset( &trustee, 0xff, sizeof trustee );
    pBuildTrusteeWithNameA( &trustee, szTrusteeName );

    ok( trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok( trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, 
        "MultipleTrusteeOperation wrong\n");
    ok( trustee.TrusteeForm == TRUSTEE_IS_NAME, "TrusteeForm wrong\n");
    ok( trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok( trustee.ptstrName == szTrusteeName, "ptstrName wrong\n" );

    /* test BuildTrusteeWithObjectsAndNameA (test 1) */
    memset( &trustee, 0xff, sizeof trustee );
    memset( &oan, 0xff, sizeof(oan) );
    pBuildTrusteeWithObjectsAndNameA(&trustee, &oan, SE_KERNEL_OBJECT, szObjectTypeName,
                                     szInheritedObjectTypeName, szTrusteeName);

    ok(trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok(trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, "MultipleTrusteeOperation wrong\n");
    ok(trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_NAME, "TrusteeForm wrong\n");
    ok(trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok(trustee.ptstrName == (LPSTR)&oan, "ptstrName wrong\n");
 
    ok(oan.ObjectsPresent == (ACE_OBJECT_TYPE_PRESENT | ACE_INHERITED_OBJECT_TYPE_PRESENT), "ObjectsPresent wrong\n");
    ok(oan.ObjectType == SE_KERNEL_OBJECT, "ObjectType wrong\n");
    ok(oan.InheritedObjectTypeName == szInheritedObjectTypeName, "InheritedObjectTypeName wrong\n");
    ok(oan.ptstrName == szTrusteeName, "szTrusteeName wrong\n");

    /* test GetTrusteeNameA */
    ok(pGetTrusteeNameA(&trustee) == (LPSTR)&oan, "GetTrusteeName returned wrong value\n");

    /* test BuildTrusteeWithObjectsAndNameA (test 2) */
    memset( &trustee, 0xff, sizeof trustee );
    memset( &oan, 0xff, sizeof(oan) );
    pBuildTrusteeWithObjectsAndNameA(&trustee, &oan, SE_KERNEL_OBJECT, NULL,
                                     szInheritedObjectTypeName, szTrusteeName);

    ok(trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok(trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, "MultipleTrusteeOperation wrong\n");
    ok(trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_NAME, "TrusteeForm wrong\n");
    ok(trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok(trustee.ptstrName == (LPSTR)&oan, "ptstrName wrong\n");
 
    ok(oan.ObjectsPresent == ACE_INHERITED_OBJECT_TYPE_PRESENT, "ObjectsPresent wrong\n");
    ok(oan.ObjectType == SE_KERNEL_OBJECT, "ObjectType wrong\n");
    ok(oan.InheritedObjectTypeName == szInheritedObjectTypeName, "InheritedObjectTypeName wrong\n");
    ok(oan.ptstrName == szTrusteeName, "szTrusteeName wrong\n");

    /* test BuildTrusteeWithObjectsAndNameA (test 3) */
    memset( &trustee, 0xff, sizeof trustee );
    memset( &oan, 0xff, sizeof(oan) );
    pBuildTrusteeWithObjectsAndNameA(&trustee, &oan, SE_KERNEL_OBJECT, szObjectTypeName,
                                     NULL, szTrusteeName);

    ok(trustee.pMultipleTrustee == NULL, "pMultipleTrustee wrong\n");
    ok(trustee.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE, "MultipleTrusteeOperation wrong\n");
    ok(trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_NAME, "TrusteeForm wrong\n");
    ok(trustee.TrusteeType == TRUSTEE_IS_UNKNOWN, "TrusteeType wrong\n");
    ok(trustee.ptstrName == (LPSTR)&oan, "ptstrName wrong\n");
 
    ok(oan.ObjectsPresent == ACE_OBJECT_TYPE_PRESENT, "ObjectsPresent wrong\n");
    ok(oan.ObjectType == SE_KERNEL_OBJECT, "ObjectType wrong\n");
    ok(oan.InheritedObjectTypeName == NULL, "InheritedObjectTypeName wrong\n");
    ok(oan.ptstrName == szTrusteeName, "szTrusteeName wrong\n");
}
 
/* If the first isn't defined, assume none is */
#ifndef SE_MIN_WELL_KNOWN_PRIVILEGE
#define SE_MIN_WELL_KNOWN_PRIVILEGE       2L
#define SE_CREATE_TOKEN_PRIVILEGE         2L
#define SE_ASSIGNPRIMARYTOKEN_PRIVILEGE   3L
#define SE_LOCK_MEMORY_PRIVILEGE          4L
#define SE_INCREASE_QUOTA_PRIVILEGE       5L
#define SE_MACHINE_ACCOUNT_PRIVILEGE      6L
#define SE_TCB_PRIVILEGE                  7L
#define SE_SECURITY_PRIVILEGE             8L
#define SE_TAKE_OWNERSHIP_PRIVILEGE       9L
#define SE_LOAD_DRIVER_PRIVILEGE         10L
#define SE_SYSTEM_PROFILE_PRIVILEGE      11L
#define SE_SYSTEMTIME_PRIVILEGE          12L
#define SE_PROF_SINGLE_PROCESS_PRIVILEGE 13L
#define SE_INC_BASE_PRIORITY_PRIVILEGE   14L
#define SE_CREATE_PAGEFILE_PRIVILEGE     15L
#define SE_CREATE_PERMANENT_PRIVILEGE    16L
#define SE_BACKUP_PRIVILEGE              17L
#define SE_RESTORE_PRIVILEGE             18L
#define SE_SHUTDOWN_PRIVILEGE            19L
#define SE_DEBUG_PRIVILEGE               20L
#define SE_AUDIT_PRIVILEGE               21L
#define SE_SYSTEM_ENVIRONMENT_PRIVILEGE  22L
#define SE_CHANGE_NOTIFY_PRIVILEGE       23L
#define SE_REMOTE_SHUTDOWN_PRIVILEGE     24L
#define SE_UNDOCK_PRIVILEGE              25L
#define SE_SYNC_AGENT_PRIVILEGE          26L
#define SE_ENABLE_DELEGATION_PRIVILEGE   27L
#define SE_MANAGE_VOLUME_PRIVILEGE       28L
#define SE_IMPERSONATE_PRIVILEGE         29L
#define SE_CREATE_GLOBAL_PRIVILEGE       30L
#define SE_MAX_WELL_KNOWN_PRIVILEGE      SE_CREATE_GLOBAL_PRIVILEGE
#endif /* ndef SE_MIN_WELL_KNOWN_PRIVILEGE */

static void test_allocateLuid(void)
{
    BOOL (WINAPI *pAllocateLocallyUniqueId)(PLUID);
    LUID luid1, luid2;
    BOOL ret;

    pAllocateLocallyUniqueId = (void*)GetProcAddress(hmod, "AllocateLocallyUniqueId");
    if (!pAllocateLocallyUniqueId) return;

    ret = pAllocateLocallyUniqueId(&luid1);
    if (!ret && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
        return;

    ok(ret,
     "AllocateLocallyUniqueId failed: %ld\n", GetLastError());
    ret = pAllocateLocallyUniqueId(&luid2);
    ok( ret,
     "AllocateLocallyUniqueId failed: %ld\n", GetLastError());
    ok(luid1.LowPart > SE_MAX_WELL_KNOWN_PRIVILEGE || luid1.HighPart != 0,
     "AllocateLocallyUniqueId returned a well-known LUID\n");
    ok(luid1.LowPart != luid2.LowPart || luid1.HighPart != luid2.HighPart,
     "AllocateLocallyUniqueId returned non-unique LUIDs\n");
    ret = pAllocateLocallyUniqueId(NULL);
    ok( !ret && GetLastError() == ERROR_NOACCESS,
     "AllocateLocallyUniqueId(NULL) didn't return ERROR_NOACCESS: %ld\n",
     GetLastError());
}

static void test_lookupPrivilegeName(void)
{
    BOOL (WINAPI *pLookupPrivilegeNameA)(LPCSTR, PLUID, LPSTR, LPDWORD);
    char buf[MAX_PATH]; /* arbitrary, seems long enough */
    DWORD cchName = sizeof(buf);
    LUID luid = { 0, 0 };
    LONG i;
    BOOL ret;

    /* check whether it's available first */
    pLookupPrivilegeNameA = (void*)GetProcAddress(hmod, "LookupPrivilegeNameA");
    if (!pLookupPrivilegeNameA) return;
    luid.LowPart = SE_CREATE_TOKEN_PRIVILEGE;
    ret = pLookupPrivilegeNameA(NULL, &luid, buf, &cchName);
    if (!ret && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
        return;

    /* check with a short buffer */
    cchName = 0;
    luid.LowPart = SE_CREATE_TOKEN_PRIVILEGE;
    ret = pLookupPrivilegeNameA(NULL, &luid, NULL, &cchName);
    ok( !ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
     "LookupPrivilegeNameA didn't fail with ERROR_INSUFFICIENT_BUFFER: %ld\n",
     GetLastError());
    ok(cchName == strlen("SeCreateTokenPrivilege") + 1,
     "LookupPrivilegeNameA returned an incorrect required length for\n"
     "SeCreateTokenPrivilege (got %ld, expected %d)\n", cchName,
     lstrlenA("SeCreateTokenPrivilege") + 1);
    /* check a known value and its returned length on success */
    cchName = sizeof(buf);
    ok(pLookupPrivilegeNameA(NULL, &luid, buf, &cchName) &&
     cchName == strlen("SeCreateTokenPrivilege"),
     "LookupPrivilegeNameA returned an incorrect output length for\n"
     "SeCreateTokenPrivilege (got %ld, expected %d)\n", cchName,
     (int)strlen("SeCreateTokenPrivilege"));
    /* check known values */
    for (i = SE_MIN_WELL_KNOWN_PRIVILEGE; i <= SE_MAX_WELL_KNOWN_PRIVILEGE; i++)
    {
        luid.LowPart = i;
        cchName = sizeof(buf);
        ret = pLookupPrivilegeNameA(NULL, &luid, buf, &cchName);
        ok( ret || GetLastError() == ERROR_NO_SUCH_PRIVILEGE,
         "LookupPrivilegeNameA(0.%ld) failed: %ld\n", i, GetLastError());
    }
    /* check a bogus LUID */
    luid.LowPart = 0xdeadbeef;
    cchName = sizeof(buf);
    ret = pLookupPrivilegeNameA(NULL, &luid, buf, &cchName);
    ok( !ret && GetLastError() == ERROR_NO_SUCH_PRIVILEGE,
     "LookupPrivilegeNameA didn't fail with ERROR_NO_SUCH_PRIVILEGE: %ld\n",
     GetLastError());
    /* check on a bogus system */
    luid.LowPart = SE_CREATE_TOKEN_PRIVILEGE;
    cchName = sizeof(buf);
    ret = pLookupPrivilegeNameA("b0gu5.Nam3", &luid, buf, &cchName);
    ok( !ret && (GetLastError() == RPC_S_SERVER_UNAVAILABLE ||
                 GetLastError() == RPC_S_INVALID_NET_ADDR) /* w2k8 */,
     "LookupPrivilegeNameA didn't fail with RPC_S_SERVER_UNAVAILABLE or RPC_S_INVALID_NET_ADDR: %ld\n",
     GetLastError());
}

struct NameToLUID
{
    const char *name;
    DWORD lowPart;
};

static void test_lookupPrivilegeValue(void)
{
    static const struct NameToLUID privs[] = {
     { "SeCreateTokenPrivilege", SE_CREATE_TOKEN_PRIVILEGE },
     { "SeAssignPrimaryTokenPrivilege", SE_ASSIGNPRIMARYTOKEN_PRIVILEGE },
     { "SeLockMemoryPrivilege", SE_LOCK_MEMORY_PRIVILEGE },
     { "SeIncreaseQuotaPrivilege", SE_INCREASE_QUOTA_PRIVILEGE },
     { "SeMachineAccountPrivilege", SE_MACHINE_ACCOUNT_PRIVILEGE },
     { "SeTcbPrivilege", SE_TCB_PRIVILEGE },
     { "SeSecurityPrivilege", SE_SECURITY_PRIVILEGE },
     { "SeTakeOwnershipPrivilege", SE_TAKE_OWNERSHIP_PRIVILEGE },
     { "SeLoadDriverPrivilege", SE_LOAD_DRIVER_PRIVILEGE },
     { "SeSystemProfilePrivilege", SE_SYSTEM_PROFILE_PRIVILEGE },
     { "SeSystemtimePrivilege", SE_SYSTEMTIME_PRIVILEGE },
     { "SeProfileSingleProcessPrivilege", SE_PROF_SINGLE_PROCESS_PRIVILEGE },
     { "SeIncreaseBasePriorityPrivilege", SE_INC_BASE_PRIORITY_PRIVILEGE },
     { "SeCreatePagefilePrivilege", SE_CREATE_PAGEFILE_PRIVILEGE },
     { "SeCreatePermanentPrivilege", SE_CREATE_PERMANENT_PRIVILEGE },
     { "SeBackupPrivilege", SE_BACKUP_PRIVILEGE },
     { "SeRestorePrivilege", SE_RESTORE_PRIVILEGE },
     { "SeShutdownPrivilege", SE_SHUTDOWN_PRIVILEGE },
     { "SeDebugPrivilege", SE_DEBUG_PRIVILEGE },
     { "SeAuditPrivilege", SE_AUDIT_PRIVILEGE },
     { "SeSystemEnvironmentPrivilege", SE_SYSTEM_ENVIRONMENT_PRIVILEGE },
     { "SeChangeNotifyPrivilege", SE_CHANGE_NOTIFY_PRIVILEGE },
     { "SeRemoteShutdownPrivilege", SE_REMOTE_SHUTDOWN_PRIVILEGE },
     { "SeUndockPrivilege", SE_UNDOCK_PRIVILEGE },
     { "SeSyncAgentPrivilege", SE_SYNC_AGENT_PRIVILEGE },
     { "SeEnableDelegationPrivilege", SE_ENABLE_DELEGATION_PRIVILEGE },
     { "SeManageVolumePrivilege", SE_MANAGE_VOLUME_PRIVILEGE },
     { "SeImpersonatePrivilege", SE_IMPERSONATE_PRIVILEGE },
     { "SeCreateGlobalPrivilege", SE_CREATE_GLOBAL_PRIVILEGE },
    };
    BOOL (WINAPI *pLookupPrivilegeValueA)(LPCSTR, LPCSTR, PLUID);
    unsigned int i;
    LUID luid;
    BOOL ret;

    /* check whether it's available first */
    pLookupPrivilegeValueA = (void*)GetProcAddress(hmod, "LookupPrivilegeValueA");
    if (!pLookupPrivilegeValueA) return;
    ret = pLookupPrivilegeValueA(NULL, "SeCreateTokenPrivilege", &luid);
    if (!ret && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
        return;

    /* check a bogus system name */
    ret = pLookupPrivilegeValueA("b0gu5.Nam3", "SeCreateTokenPrivilege", &luid);
    ok( !ret && (GetLastError() == RPC_S_SERVER_UNAVAILABLE ||
                GetLastError() == RPC_S_INVALID_NET_ADDR) /* w2k8 */,
     "LookupPrivilegeValueA didn't fail with RPC_S_SERVER_UNAVAILABLE or RPC_S_INVALID_NET_ADDR: %ld\n",
     GetLastError());
    /* check a NULL string */
    ret = pLookupPrivilegeValueA(NULL, 0, &luid);
    ok( !ret && GetLastError() == ERROR_NO_SUCH_PRIVILEGE,
     "LookupPrivilegeValueA didn't fail with ERROR_NO_SUCH_PRIVILEGE: %ld\n",
     GetLastError());
    /* check a bogus privilege name */
    ret = pLookupPrivilegeValueA(NULL, "SeBogusPrivilege", &luid);
    ok( !ret && GetLastError() == ERROR_NO_SUCH_PRIVILEGE,
     "LookupPrivilegeValueA didn't fail with ERROR_NO_SUCH_PRIVILEGE: %ld\n",
     GetLastError());
    /* check case insensitive */
    ret = pLookupPrivilegeValueA(NULL, "sEcREATEtOKENpRIVILEGE", &luid);
    ok( ret,
     "LookupPrivilegeValueA(NULL, sEcREATEtOKENpRIVILEGE, &luid) failed: %ld\n",
     GetLastError());
    for (i = 0; i < ARRAY_SIZE(privs); i++)
    {
        /* Not all privileges are implemented on all Windows versions, so
         * don't worry if the call fails
         */
        if (pLookupPrivilegeValueA(NULL, privs[i].name, &luid))
        {
            ok(luid.LowPart == privs[i].lowPart,
             "LookupPrivilegeValueA returned an invalid LUID for %s\n",
             privs[i].name);
        }
    }
}

static void test_FileSecurity(void)
{
    char wintmpdir [MAX_PATH];
    char path [MAX_PATH];
    char file [MAX_PATH];
    HANDLE fh, token;
    DWORD sdSize, retSize, rc, granted, priv_set_len;
    PRIVILEGE_SET priv_set;
    BOOL status;
    BYTE *sd;
    GENERIC_MAPPING mapping = { FILE_READ_DATA, FILE_WRITE_DATA, FILE_EXECUTE, FILE_ALL_ACCESS };
    const SECURITY_INFORMATION request = OWNER_SECURITY_INFORMATION
                                       | GROUP_SECURITY_INFORMATION
                                       | DACL_SECURITY_INFORMATION;

    if (!GetTempPathA (sizeof (wintmpdir), wintmpdir)) {
        win_skip ("GetTempPathA failed\n");
        return;
    }

    /* Create a temporary directory and in it a temporary file */
    strcat (strcpy (path, wintmpdir), "rary");
    SetLastError(0xdeadbeef);
    rc = CreateDirectoryA (path, NULL);
    ok (rc || GetLastError() == ERROR_ALREADY_EXISTS, "CreateDirectoryA "
        "failed for '%s' with %ld\n", path, GetLastError());

    strcat (strcpy (file, path), "\\ess");
    SetLastError(0xdeadbeef);
    fh = CreateFileA (file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok (fh != INVALID_HANDLE_VALUE, "CreateFileA "
        "failed for '%s' with %ld\n", file, GetLastError());
    CloseHandle (fh);

    /* For the temporary file ... */

    /* Get size needed */
    retSize = 0;
    SetLastError(0xdeadbeef);
    rc = GetFileSecurityA (file, request, NULL, 0, &retSize);
    if (!rc && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)) {
        win_skip("GetFileSecurityA is not implemented\n");
        goto cleanup;
    }
    ok (!rc, "GetFileSecurityA "
        "was expected to fail for '%s'\n", file);
    ok (GetLastError() == ERROR_INSUFFICIENT_BUFFER, "GetFileSecurityA "
        "returned %ld; expected ERROR_INSUFFICIENT_BUFFER\n", GetLastError());
    ok (retSize > sizeof (SECURITY_DESCRIPTOR), "GetFileSecurityA returned size %ld\n", retSize);

    sdSize = retSize;
    sd = malloc(sdSize);

    /* Get security descriptor for real */
    retSize = -1;
    SetLastError(0xdeadbeef);
    rc = GetFileSecurityA (file, request, sd, sdSize, &retSize);
    ok (rc, "GetFileSecurityA "
        "was not expected to fail '%s': %ld\n", file, GetLastError());
    ok (retSize == sdSize,
        "GetFileSecurityA returned size %ld; expected %ld\n", retSize, sdSize);

    /* Use it to set security descriptor */
    SetLastError(0xdeadbeef);
    rc = SetFileSecurityA (file, request, sd);
    ok (rc, "SetFileSecurityA "
        "was not expected to fail '%s': %ld\n", file, GetLastError());

    free(sd);

    /* Repeat for the temporary directory ... */

    /* Get size needed */
    retSize = 0;
    SetLastError(0xdeadbeef);
    rc = GetFileSecurityA (path, request, NULL, 0, &retSize);
    ok (!rc, "GetFileSecurityA "
        "was expected to fail for '%s'\n", path);
    ok (GetLastError() == ERROR_INSUFFICIENT_BUFFER, "GetFileSecurityA "
        "returned %ld; expected ERROR_INSUFFICIENT_BUFFER\n", GetLastError());
    ok (retSize > sizeof (SECURITY_DESCRIPTOR), "GetFileSecurityA returned size %ld\n", retSize);

    sdSize = retSize;
    sd = malloc(sdSize);

    /* Get security descriptor for real */
    retSize = -1;
    SetLastError(0xdeadbeef);
    rc = GetFileSecurityA (path, request, sd, sdSize, &retSize);
    ok (rc, "GetFileSecurityA "
        "was not expected to fail '%s': %ld\n", path, GetLastError());
    ok (retSize == sdSize,
        "GetFileSecurityA returned size %ld; expected %ld\n", retSize, sdSize);

    /* Use it to set security descriptor */
    SetLastError(0xdeadbeef);
    rc = SetFileSecurityA (path, request, sd);
    ok (rc, "SetFileSecurityA "
        "was not expected to fail '%s': %ld\n", path, GetLastError());
    free(sd);

    /* Old test */
    strcpy (wintmpdir, "\\Should not exist");
    SetLastError(0xdeadbeef);
    rc = GetFileSecurityA (wintmpdir, OWNER_SECURITY_INFORMATION, NULL, 0, &sdSize);
    ok (!rc, "GetFileSecurityA should fail for not existing directories/files\n");
    ok (GetLastError() == ERROR_FILE_NOT_FOUND,
        "last error ERROR_FILE_NOT_FOUND expected, got %ld\n", GetLastError());

cleanup:
    /* Remove temporary file and directory */
    DeleteFileA(file);
    RemoveDirectoryA(path);

    /* Test file access permissions for a file with FILE_ATTRIBUTE_ARCHIVE */
    SetLastError(0xdeadbeef);
    rc = GetTempPathA(sizeof(wintmpdir), wintmpdir);
    ok(rc, "GetTempPath error %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    rc = GetTempFileNameA(wintmpdir, "tmp", 0, file);
    ok(rc, "GetTempFileName error %ld\n", GetLastError());

    rc = GetFileAttributesA(file);
    rc &= ~(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED|FILE_ATTRIBUTE_COMPRESSED);
    ok(rc == FILE_ATTRIBUTE_ARCHIVE, "expected FILE_ATTRIBUTE_ARCHIVE got %#lx\n", rc);

    rc = GetFileSecurityA(file, OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
                          NULL, 0, &sdSize);
    ok(!rc, "GetFileSecurity should fail\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "expected ERROR_INSUFFICIENT_BUFFER got %ld\n", GetLastError());
    ok(sdSize > sizeof(SECURITY_DESCRIPTOR), "got sd size %ld\n", sdSize);

    sd = malloc(sdSize);
    retSize = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = GetFileSecurityA(file, OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
                          sd, sdSize, &retSize);
    ok(rc, "GetFileSecurity error %ld\n", GetLastError());
    ok(retSize == sdSize, "expected %ld, got %ld\n", sdSize, retSize);

    SetLastError(0xdeadbeef);
    rc = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token);
    ok(!rc, "OpenThreadToken should fail\n");
    ok(GetLastError() == ERROR_NO_TOKEN, "expected ERROR_NO_TOKEN, got %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    rc = ImpersonateSelf(SecurityIdentification);
    ok(rc, "ImpersonateSelf error %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    rc = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token);
    ok(rc, "OpenThreadToken error %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    rc = RevertToSelf();
    ok(rc, "RevertToSelf error %ld\n", GetLastError());

    priv_set_len = sizeof(priv_set);
    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, FILE_READ_DATA, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == FILE_READ_DATA, "expected FILE_READ_DATA, got %#lx\n", granted);

    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, FILE_WRITE_DATA, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == FILE_WRITE_DATA, "expected FILE_WRITE_DATA, got %#lx\n", granted);

    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, FILE_EXECUTE, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == FILE_EXECUTE, "expected FILE_EXECUTE, got %#lx\n", granted);

    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, DELETE, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == DELETE, "expected DELETE, got %#lx\n", granted);

    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, FILE_DELETE_CHILD, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == FILE_DELETE_CHILD, "expected FILE_DELETE_CHILD, got %#lx\n", granted);

    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, 0x1ff, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == 0x1ff, "expected 0x1ff, got %#lx\n", granted);

    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, FILE_ALL_ACCESS, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == FILE_ALL_ACCESS, "expected FILE_ALL_ACCESS, got %#lx\n", granted);

    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, 0xffffffff, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(!rc, "AccessCheck should fail\n");
    ok(GetLastError() == ERROR_GENERIC_NOT_MAPPED, "expected ERROR_GENERIC_NOT_MAPPED, got %ld\n", GetLastError());

    /* Test file access permissions for a file with FILE_ATTRIBUTE_READONLY */
    SetLastError(0xdeadbeef);
    fh = CreateFileA(file, FILE_READ_DATA, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_READONLY, 0);
    ok(fh != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());
    retSize = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = WriteFile(fh, "1", 1, &retSize, NULL);
    ok(!rc, "WriteFile should fail\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "expected ERROR_ACCESS_DENIED, got %ld\n", GetLastError());
    ok(retSize == 0, "expected 0, got %ld\n", retSize);
    CloseHandle(fh);

    rc = GetFileAttributesA(file);
    rc &= ~(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED|FILE_ATTRIBUTE_COMPRESSED);
    todo_wine
    ok(rc == (FILE_ATTRIBUTE_ARCHIVE|FILE_ATTRIBUTE_READONLY),
       "expected FILE_ATTRIBUTE_ARCHIVE|FILE_ATTRIBUTE_READONLY got %#lx\n", rc);

    SetLastError(0xdeadbeef);
    rc = SetFileAttributesA(file, FILE_ATTRIBUTE_ARCHIVE);
    ok(rc, "SetFileAttributes error %ld\n", GetLastError());
    SetLastError(0xdeadbeef);
    rc = DeleteFileA(file);
    ok(rc, "DeleteFile error %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    fh = CreateFileA(file, FILE_READ_DATA, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_READONLY, 0);
    ok(fh != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());
    retSize = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = WriteFile(fh, "1", 1, &retSize, NULL);
    ok(!rc, "WriteFile should fail\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "expected ERROR_ACCESS_DENIED, got %ld\n", GetLastError());
    ok(retSize == 0, "expected 0, got %ld\n", retSize);
    CloseHandle(fh);

    rc = GetFileAttributesA(file);
    rc &= ~(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED|FILE_ATTRIBUTE_COMPRESSED);
    ok(rc == (FILE_ATTRIBUTE_ARCHIVE|FILE_ATTRIBUTE_READONLY),
       "expected FILE_ATTRIBUTE_ARCHIVE|FILE_ATTRIBUTE_READONLY got %#lx\n", rc);

    retSize = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = GetFileSecurityA(file, OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
                          sd, sdSize, &retSize);
    ok(rc, "GetFileSecurity error %ld\n", GetLastError());
    ok(retSize == sdSize, "expected %ld, got %ld\n", sdSize, retSize);

    priv_set_len = sizeof(priv_set);
    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, FILE_READ_DATA, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == FILE_READ_DATA, "expected FILE_READ_DATA, got %#lx\n", granted);

    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, FILE_WRITE_DATA, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
todo_wine {
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == FILE_WRITE_DATA, "expected FILE_WRITE_DATA, got %#lx\n", granted);
}
    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, FILE_EXECUTE, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == FILE_EXECUTE, "expected FILE_EXECUTE, got %#lx\n", granted);

    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, DELETE, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == DELETE, "expected DELETE, got %#lx\n", granted);

    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, WRITE_OWNER, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == WRITE_OWNER, "expected WRITE_OWNER, got %#lx\n", granted);

    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, SYNCHRONIZE, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == SYNCHRONIZE, "expected SYNCHRONIZE, got %#lx\n", granted);

    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, FILE_DELETE_CHILD, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
todo_wine {
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == FILE_DELETE_CHILD, "expected FILE_DELETE_CHILD, got %#lx\n", granted);
}
    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, 0x1ff, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
todo_wine {
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == 0x1ff, "expected 0x1ff, got %#lx\n", granted);
}
    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    rc = AccessCheck(sd, token, FILE_ALL_ACCESS, &mapping, &priv_set, &priv_set_len, &granted, &status);
    ok(rc, "AccessCheck error %ld\n", GetLastError());
todo_wine {
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == FILE_ALL_ACCESS, "expected FILE_ALL_ACCESS, got %#lx\n", granted);
}
    SetLastError(0xdeadbeef);
    rc = DeleteFileA(file);
    ok(!rc, "DeleteFile should fail\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "expected ERROR_ACCESS_DENIED, got %ld\n", GetLastError());
    SetLastError(0xdeadbeef);
    rc = SetFileAttributesA(file, FILE_ATTRIBUTE_ARCHIVE);
    ok(rc, "SetFileAttributes error %ld\n", GetLastError());
    SetLastError(0xdeadbeef);
    rc = DeleteFileA(file);
    ok(rc, "DeleteFile error %ld\n", GetLastError());

    CloseHandle(token);
    free(sd);
}

static void test_AccessCheck(void)
{
    PSID EveryoneSid = NULL, AdminSid = NULL, UsersSid = NULL;
    PACL Acl = NULL;
    SECURITY_DESCRIPTOR *SecurityDescriptor = NULL;
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = { SECURITY_WORLD_SID_AUTHORITY };
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = { SECURITY_NT_AUTHORITY };
    GENERIC_MAPPING Mapping = { KEY_READ, KEY_WRITE, KEY_EXECUTE, KEY_ALL_ACCESS };
    ACCESS_MASK Access;
    BOOL AccessStatus;
    HANDLE Token;
    HANDLE ProcessToken;
    BOOL ret;
    DWORD PrivSetLen;
    PRIVILEGE_SET *PrivSet;
    BOOL res;
    HMODULE NtDllModule;
    BOOLEAN Enabled;
    DWORD err;
    NTSTATUS ntret, ntAccessStatus;

    NtDllModule = GetModuleHandleA("ntdll.dll");
    if (!NtDllModule)
    {
        skip("not running on NT, skipping test\n");
        return;
    }
    pRtlAdjustPrivilege = (void *)GetProcAddress(NtDllModule, "RtlAdjustPrivilege");
    if (!pRtlAdjustPrivilege)
    {
        win_skip("missing RtlAdjustPrivilege, skipping test\n");
        return;
    }

    Acl = malloc(256);
    res = InitializeAcl(Acl, 256, ACL_REVISION);
    if(!res && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        skip("ACLs not implemented - skipping tests\n");
        free(Acl);
        return;
    }
    ok(res, "InitializeAcl failed with error %ld\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &EveryoneSid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &AdminSid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &UsersSid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    SecurityDescriptor = malloc(SECURITY_DESCRIPTOR_MIN_LENGTH);

    res = InitializeSecurityDescriptor(SecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    ok(res, "InitializeSecurityDescriptor failed with error %ld\n", GetLastError());

    res = SetSecurityDescriptorDacl(SecurityDescriptor, TRUE, Acl, FALSE);
    ok(res, "SetSecurityDescriptorDacl failed with error %ld\n", GetLastError());

    PrivSetLen = FIELD_OFFSET(PRIVILEGE_SET, Privilege[16]);
    PrivSet = calloc(1, PrivSetLen);
    PrivSet->PrivilegeCount = 16;

    res = OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE|TOKEN_QUERY, &ProcessToken);
    ok(res, "OpenProcessToken failed with error %ld\n", GetLastError());

    pRtlAdjustPrivilege(SE_SECURITY_PRIVILEGE, FALSE, TRUE, &Enabled);

    res = DuplicateToken(ProcessToken, SecurityImpersonation, &Token);
    ok(res, "DuplicateToken failed with error %ld\n", GetLastError());

    /* SD without owner/group */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    ret = AccessCheck(SecurityDescriptor, Token, KEY_QUERY_VALUE, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_INVALID_SECURITY_DESCR, "AccessCheck should have "
       "failed with ERROR_INVALID_SECURITY_DESCR, instead of %ld\n", err);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* Set owner and group */
    res = SetSecurityDescriptorOwner(SecurityDescriptor, AdminSid, FALSE);
    ok(res, "SetSecurityDescriptorOwner failed with error %ld\n", GetLastError());
    res = SetSecurityDescriptorGroup(SecurityDescriptor, UsersSid, TRUE);
    ok(res, "SetSecurityDescriptorGroup failed with error %ld\n", GetLastError());

    /* Generic access mask */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    ret = AccessCheck(SecurityDescriptor, Token, GENERIC_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_GENERIC_NOT_MAPPED, "AccessCheck should have failed "
       "with ERROR_GENERIC_NOT_MAPPED, instead of %ld\n", err);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* Generic access mask - no privilegeset buffer */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    ret = AccessCheck(SecurityDescriptor, Token, GENERIC_READ, &Mapping,
                      NULL, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_NOACCESS, "AccessCheck should have failed "
       "with ERROR_NOACCESS, instead of %ld\n", err);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* Generic access mask - no returnlength */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    ret = AccessCheck(SecurityDescriptor, Token, GENERIC_READ, &Mapping,
                      PrivSet, NULL, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_NOACCESS, "AccessCheck should have failed "
       "with ERROR_NOACCESS, instead of %ld\n", err);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* Generic access mask - no privilegeset buffer, no returnlength */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    ret = AccessCheck(SecurityDescriptor, Token, GENERIC_READ, &Mapping,
                      NULL, NULL, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_NOACCESS, "AccessCheck should have failed "
       "with ERROR_NOACCESS, instead of %ld\n", err);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* sd with no dacl present */
    Access = AccessStatus = 0x1abe11ed;
    ret = SetSecurityDescriptorDacl(SecurityDescriptor, FALSE, NULL, FALSE);
    ok(ret, "SetSecurityDescriptorDacl failed with error %ld\n", GetLastError());
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret, "AccessCheck failed with error %ld\n", GetLastError());
    ok(AccessStatus && (Access == KEY_READ),
        "AccessCheck failed to grant access with error %ld\n",
        GetLastError());

    /* sd with no dacl present - no privilegeset buffer */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    ret = AccessCheck(SecurityDescriptor, Token, GENERIC_READ, &Mapping,
                      NULL, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_NOACCESS, "AccessCheck should have failed "
       "with ERROR_NOACCESS, instead of %ld\n", err);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    if(pNtAccessCheck)
    {
       DWORD ntPrivSetLen = sizeof(PRIVILEGE_SET);

       /* Generic access mask - no privilegeset buffer */
       SetLastError(0xdeadbeef);
       Access = ntAccessStatus = 0x1abe11ed;
       ntret = pNtAccessCheck(SecurityDescriptor, Token, GENERIC_READ, &Mapping,
                              NULL, &ntPrivSetLen, &Access, &ntAccessStatus);
       err = GetLastError();
       ok(ntret == STATUS_ACCESS_VIOLATION,
          "NtAccessCheck should have failed with STATUS_ACCESS_VIOLATION, got %lx\n", ntret);
       ok(err == 0xdeadbeef,
          "NtAccessCheck shouldn't set last error, got %ld\n", err);
       ok(Access == 0x1abe11ed && ntAccessStatus == 0x1abe11ed,
          "Access and/or AccessStatus were changed!\n");
       ok(ntPrivSetLen == sizeof(PRIVILEGE_SET), "PrivSetLen returns %ld\n", ntPrivSetLen);

      /* Generic access mask - no returnlength */
      SetLastError(0xdeadbeef);
      Access = ntAccessStatus = 0x1abe11ed;
      ntret = pNtAccessCheck(SecurityDescriptor, Token, GENERIC_READ, &Mapping,
                             PrivSet, NULL, &Access, &ntAccessStatus);
      err = GetLastError();
      ok(ntret == STATUS_ACCESS_VIOLATION,
         "NtAccessCheck should have failed with STATUS_ACCESS_VIOLATION, got %lx\n", ntret);
      ok(err == 0xdeadbeef,
         "NtAccessCheck shouldn't set last error, got %ld\n", err);
      ok(Access == 0x1abe11ed && ntAccessStatus == 0x1abe11ed,
         "Access and/or AccessStatus were changed!\n");

      /* Generic access mask - no privilegeset buffer, no returnlength */
      SetLastError(0xdeadbeef);
      Access = ntAccessStatus = 0x1abe11ed;
      ntret = pNtAccessCheck(SecurityDescriptor, Token, GENERIC_READ, &Mapping,
                             NULL, NULL, &Access, &ntAccessStatus);
      err = GetLastError();
      ok(ntret == STATUS_ACCESS_VIOLATION,
         "NtAccessCheck should have failed with STATUS_ACCESS_VIOLATION, got %lx\n", ntret);
      ok(err == 0xdeadbeef,
         "NtAccessCheck shouldn't set last error, got %ld\n", err);
      ok(Access == 0x1abe11ed && ntAccessStatus == 0x1abe11ed,
         "Access and/or AccessStatus were changed!\n");

      /* Generic access mask - zero returnlength */
      SetLastError(0xdeadbeef);
      Access = ntAccessStatus = 0x1abe11ed;
      ntPrivSetLen = 0;
      ntret = pNtAccessCheck(SecurityDescriptor, Token, GENERIC_READ, &Mapping,
                             PrivSet, &ntPrivSetLen, &Access, &ntAccessStatus);
      err = GetLastError();
      ok(ntret == STATUS_GENERIC_NOT_MAPPED,
         "NtAccessCheck should have failed with STATUS_GENERIC_NOT_MAPPED, got %lx\n", ntret);
      ok(err == 0xdeadbeef,
         "NtAccessCheck shouldn't set last error, got %ld\n", err);
      ok(Access == 0x1abe11ed && ntAccessStatus == 0x1abe11ed,
         "Access and/or AccessStatus were changed!\n");
      ok(ntPrivSetLen == 0, "PrivSetLen returns %ld\n", ntPrivSetLen);

      /* Generic access mask - insufficient returnlength */
      SetLastError(0xdeadbeef);
      Access = ntAccessStatus = 0x1abe11ed;
      ntPrivSetLen = sizeof(PRIVILEGE_SET)-1;
      ntret = pNtAccessCheck(SecurityDescriptor, Token, GENERIC_READ, &Mapping,
                             PrivSet, &ntPrivSetLen, &Access, &ntAccessStatus);
      err = GetLastError();
      ok(ntret == STATUS_GENERIC_NOT_MAPPED,
         "NtAccessCheck should have failed with STATUS_GENERIC_NOT_MAPPED, got %lx\n", ntret);
      ok(err == 0xdeadbeef,
         "NtAccessCheck shouldn't set last error, got %ld\n", err);
      ok(Access == 0x1abe11ed && ntAccessStatus == 0x1abe11ed,
         "Access and/or AccessStatus were changed!\n");
      ok(ntPrivSetLen == sizeof(PRIVILEGE_SET)-1, "PrivSetLen returns %ld\n", ntPrivSetLen);

      /* Key access mask - zero returnlength */
      SetLastError(0xdeadbeef);
      Access = ntAccessStatus = 0x1abe11ed;
      ntPrivSetLen = 0;
      ntret = pNtAccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                             PrivSet, &ntPrivSetLen, &Access, &ntAccessStatus);
      err = GetLastError();
      ok(ntret == STATUS_BUFFER_TOO_SMALL,
         "NtAccessCheck should have failed with STATUS_BUFFER_TOO_SMALL, got %lx\n", ntret);
      ok(err == 0xdeadbeef,
         "NtAccessCheck shouldn't set last error, got %ld\n", err);
      ok(Access == 0x1abe11ed && ntAccessStatus == 0x1abe11ed,
         "Access and/or AccessStatus were changed!\n");
      ok(ntPrivSetLen == sizeof(PRIVILEGE_SET), "PrivSetLen returns %ld\n", ntPrivSetLen);

      /* Key access mask - insufficient returnlength */
      SetLastError(0xdeadbeef);
      Access = ntAccessStatus = 0x1abe11ed;
      ntPrivSetLen = sizeof(PRIVILEGE_SET)-1;
      ntret = pNtAccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                             PrivSet, &ntPrivSetLen, &Access, &ntAccessStatus);
      err = GetLastError();
      ok(ntret == STATUS_BUFFER_TOO_SMALL,
         "NtAccessCheck should have failed with STATUS_BUFFER_TOO_SMALL, got %lx\n", ntret);
      ok(err == 0xdeadbeef,
         "NtAccessCheck shouldn't set last error, got %ld\n", err);
      ok(Access == 0x1abe11ed && ntAccessStatus == 0x1abe11ed,
         "Access and/or AccessStatus were changed!\n");
      ok(ntPrivSetLen == sizeof(PRIVILEGE_SET), "PrivSetLen returns %ld\n", ntPrivSetLen);
    }
    else
       win_skip("NtAccessCheck unavailable. Skipping.\n");

    /* sd with NULL dacl */
    Access = AccessStatus = 0x1abe11ed;
    ret = SetSecurityDescriptorDacl(SecurityDescriptor, TRUE, NULL, FALSE);
    ok(ret, "SetSecurityDescriptorDacl failed with error %ld\n", GetLastError());
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret, "AccessCheck failed with error %ld\n", GetLastError());
    ok(AccessStatus && (Access == KEY_READ),
        "AccessCheck failed to grant access with error %ld\n",
        GetLastError());
    ret = AccessCheck(SecurityDescriptor, Token, MAXIMUM_ALLOWED, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret, "AccessCheck failed with error %ld\n", GetLastError());
    ok(AccessStatus && (Access == KEY_ALL_ACCESS),
        "AccessCheck failed to grant access with error %ld\n",
        GetLastError());

    /* sd with blank dacl */
    ret = SetSecurityDescriptorDacl(SecurityDescriptor, TRUE, Acl, FALSE);
    ok(ret, "SetSecurityDescriptorDacl failed with error %ld\n", GetLastError());
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret, "AccessCheck failed with error %ld\n", GetLastError());
    err = GetLastError();
    ok(!AccessStatus && err == ERROR_ACCESS_DENIED, "AccessCheck should have failed "
       "with ERROR_ACCESS_DENIED, instead of %ld\n", err);
    ok(!Access, "Should have failed to grant any access, got 0x%08lx\n", Access);

    res = AddAccessAllowedAce(Acl, ACL_REVISION, KEY_READ, EveryoneSid);
    ok(res, "AddAccessAllowedAce failed with error %ld\n", GetLastError());

    res = AddAccessDeniedAce(Acl, ACL_REVISION, KEY_SET_VALUE, AdminSid);
    ok(res, "AddAccessDeniedAce failed with error %ld\n", GetLastError());

    /* sd with dacl */
    Access = AccessStatus = 0x1abe11ed;
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret, "AccessCheck failed with error %ld\n", GetLastError());
    ok(AccessStatus && (Access == KEY_READ),
        "AccessCheck failed to grant access with error %ld\n",
        GetLastError());

    ret = AccessCheck(SecurityDescriptor, Token, MAXIMUM_ALLOWED, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret, "AccessCheck failed with error %ld\n", GetLastError());
    ok(AccessStatus,
        "AccessCheck failed to grant any access with error %ld\n",
        GetLastError());
    trace("AccessCheck with MAXIMUM_ALLOWED got Access 0x%08lx\n", Access);

    /* Null PrivSet with null PrivSetLen pointer */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      NULL, NULL, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_NOACCESS, "AccessCheck should have "
       "failed with ERROR_NOACCESS, instead of %ld\n", err);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* Null PrivSet with zero PrivSetLen */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    PrivSetLen = 0;
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      0, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    todo_wine
    ok(!ret && err == ERROR_INSUFFICIENT_BUFFER, "AccessCheck should have "
       "failed with ERROR_INSUFFICIENT_BUFFER, instead of %ld\n", err);
    todo_wine
    ok(PrivSetLen == sizeof(PRIVILEGE_SET), "PrivSetLen returns %ld\n", PrivSetLen);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* Null PrivSet with insufficient PrivSetLen */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    PrivSetLen = 1;
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      0, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_NOACCESS, "AccessCheck should have "
       "failed with ERROR_NOACCESS, instead of %ld\n", err);
    ok(PrivSetLen == 1, "PrivSetLen returns %ld\n", PrivSetLen);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* Null PrivSet with insufficient PrivSetLen */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    PrivSetLen = sizeof(PRIVILEGE_SET) - 1;
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      0, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_NOACCESS, "AccessCheck should have "
       "failed with ERROR_NOACCESS, instead of %ld\n", err);
    ok(PrivSetLen == sizeof(PRIVILEGE_SET) - 1, "PrivSetLen returns %ld\n", PrivSetLen);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* Null PrivSet with minimal sufficient PrivSetLen */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    PrivSetLen = sizeof(PRIVILEGE_SET);
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      0, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_NOACCESS, "AccessCheck should have "
       "failed with ERROR_NOACCESS, instead of %ld\n", err);
    ok(PrivSetLen == sizeof(PRIVILEGE_SET), "PrivSetLen returns %ld\n", PrivSetLen);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* Valid PrivSet with zero PrivSetLen */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    PrivSetLen = 0;
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_INSUFFICIENT_BUFFER, "AccessCheck should have "
       "failed with ERROR_INSUFFICIENT_BUFFER, instead of %ld\n", err);
    ok(PrivSetLen == sizeof(PRIVILEGE_SET), "PrivSetLen returns %ld\n", PrivSetLen);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* Valid PrivSet with insufficient PrivSetLen */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    PrivSetLen = 1;
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_INSUFFICIENT_BUFFER, "AccessCheck should have "
       "failed with ERROR_INSUFFICIENT_BUFFER, instead of %ld\n", err);
    ok(PrivSetLen == sizeof(PRIVILEGE_SET), "PrivSetLen returns %ld\n", PrivSetLen);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* Valid PrivSet with insufficient PrivSetLen */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    PrivSetLen = sizeof(PRIVILEGE_SET) - 1;
    PrivSet->PrivilegeCount = 0xdeadbeef;
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_INSUFFICIENT_BUFFER, "AccessCheck should have "
       "failed with ERROR_INSUFFICIENT_BUFFER, instead of %ld\n", err);
    ok(PrivSetLen == sizeof(PRIVILEGE_SET), "PrivSetLen returns %ld\n", PrivSetLen);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");
    ok(PrivSet->PrivilegeCount == 0xdeadbeef, "buffer contents should not be changed\n");

    /* Valid PrivSet with minimal sufficient PrivSetLen */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    PrivSetLen = sizeof(PRIVILEGE_SET);
    memset(PrivSet, 0xcc, PrivSetLen);
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(ret, "AccessCheck failed with error %ld\n", GetLastError());
    ok(PrivSetLen == sizeof(PRIVILEGE_SET), "PrivSetLen returns %ld\n", PrivSetLen);
    ok(AccessStatus && (Access == KEY_READ),
        "AccessCheck failed to grant access with error %ld\n", GetLastError());
    ok(PrivSet->PrivilegeCount == 0, "PrivilegeCount returns %ld, expects 0\n",
        PrivSet->PrivilegeCount);

    /* Valid PrivSet with sufficient PrivSetLen */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    PrivSetLen = sizeof(PRIVILEGE_SET) + 1;
    memset(PrivSet, 0xcc, PrivSetLen);
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(ret, "AccessCheck failed with error %ld\n", GetLastError());
    todo_wine
    ok(PrivSetLen == sizeof(PRIVILEGE_SET) + 1, "PrivSetLen returns %ld\n", PrivSetLen);
    ok(AccessStatus && (Access == KEY_READ),
        "AccessCheck failed to grant access with error %ld\n", GetLastError());
    ok(PrivSet->PrivilegeCount == 0, "PrivilegeCount returns %ld, expects 0\n",
        PrivSet->PrivilegeCount);

    PrivSetLen = FIELD_OFFSET(PRIVILEGE_SET, Privilege[16]);

    /* Null PrivSet with valid PrivSetLen */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      0, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_NOACCESS, "AccessCheck should have "
       "failed with ERROR_NOACCESS, instead of %ld\n", err);
    ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
       "Access and/or AccessStatus were changed!\n");

    /* Access denied by SD */
    SetLastError(0xdeadbeef);
    Access = AccessStatus = 0x1abe11ed;
    ret = AccessCheck(SecurityDescriptor, Token, KEY_WRITE, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret, "AccessCheck failed with error %ld\n", GetLastError());
    err = GetLastError();
    ok(!AccessStatus && err == ERROR_ACCESS_DENIED, "AccessCheck should have failed "
       "with ERROR_ACCESS_DENIED, instead of %ld\n", err);
    ok(!Access, "Should have failed to grant any access, got 0x%08lx\n", Access);

    SetLastError(0xdeadbeef);
    PrivSet->PrivilegeCount = 16;
    ret = AccessCheck(SecurityDescriptor, Token, ACCESS_SYSTEM_SECURITY, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret && !AccessStatus && GetLastError() == ERROR_PRIVILEGE_NOT_HELD,
        "AccessCheck should have failed with ERROR_PRIVILEGE_NOT_HELD, instead of %ld\n",
        GetLastError());

    ret = ImpersonateLoggedOnUser(Token);
    ok(ret, "ImpersonateLoggedOnUser failed with error %ld\n", GetLastError());
    ret = pRtlAdjustPrivilege(SE_SECURITY_PRIVILEGE, TRUE, TRUE, &Enabled);
    if (!ret)
    {
        /* Valid PrivSet with zero PrivSetLen */
        SetLastError(0xdeadbeef);
        Access = AccessStatus = 0x1abe11ed;
        PrivSetLen = 0;
        ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                          PrivSet, &PrivSetLen, &Access, &AccessStatus);
        err = GetLastError();
        ok(!ret && err == ERROR_INSUFFICIENT_BUFFER, "AccessCheck should have "
           "failed with ERROR_INSUFFICIENT_BUFFER, instead of %ld\n", err);
        ok(PrivSetLen == sizeof(PRIVILEGE_SET), "PrivSetLen returns %ld\n", PrivSetLen);
        ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
           "Access and/or AccessStatus were changed!\n");

        /* Valid PrivSet with insufficient PrivSetLen */
        SetLastError(0xdeadbeef);
        Access = AccessStatus = 0x1abe11ed;
        PrivSetLen = sizeof(PRIVILEGE_SET) - 1;
        ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                          PrivSet, &PrivSetLen, &Access, &AccessStatus);
        err = GetLastError();
        ok(!ret && err == ERROR_INSUFFICIENT_BUFFER, "AccessCheck should have "
           "failed with ERROR_INSUFFICIENT_BUFFER, instead of %ld\n", err);
        ok(PrivSetLen == sizeof(PRIVILEGE_SET), "PrivSetLen returns %ld\n", PrivSetLen);
        ok(Access == 0x1abe11ed && AccessStatus == 0x1abe11ed,
           "Access and/or AccessStatus were changed!\n");

        /* Valid PrivSet with minimal sufficient PrivSetLen */
        SetLastError(0xdeadbeef);
        Access = AccessStatus = 0x1abe11ed;
        PrivSetLen = sizeof(PRIVILEGE_SET);
        memset(PrivSet, 0xcc, PrivSetLen);
        ret = AccessCheck(SecurityDescriptor, Token, ACCESS_SYSTEM_SECURITY, &Mapping,
                          PrivSet, &PrivSetLen, &Access, &AccessStatus);
        ok(ret && AccessStatus && GetLastError() == 0xdeadbeef,
            "AccessCheck should have succeeded, error %ld\n",
            GetLastError());
        ok(Access == ACCESS_SYSTEM_SECURITY,
            "Access should be equal to ACCESS_SYSTEM_SECURITY instead of 0x%08lx\n",
            Access);
        ok(PrivSet->PrivilegeCount == 1, "PrivilegeCount returns %ld, expects 1\n",
            PrivSet->PrivilegeCount);

        /* Valid PrivSet with large PrivSetLen */
        SetLastError(0xdeadbeef);
        Access = AccessStatus = 0x1abe11ed;
        PrivSetLen = FIELD_OFFSET(PRIVILEGE_SET, Privilege[16]);
        memset(PrivSet, 0xcc, PrivSetLen);
        ret = AccessCheck(SecurityDescriptor, Token, ACCESS_SYSTEM_SECURITY, &Mapping,
                          PrivSet, &PrivSetLen, &Access, &AccessStatus);
        ok(ret && AccessStatus && GetLastError() == 0xdeadbeef,
            "AccessCheck should have succeeded, error %ld\n",
            GetLastError());
        ok(Access == ACCESS_SYSTEM_SECURITY,
            "Access should be equal to ACCESS_SYSTEM_SECURITY instead of 0x%08lx\n",
            Access);
        ok(PrivSet->PrivilegeCount == 1, "PrivilegeCount returns %ld, expects 1\n",
            PrivSet->PrivilegeCount);
    }
    else
        trace("Couldn't get SE_SECURITY_PRIVILEGE (0x%08x), skipping ACCESS_SYSTEM_SECURITY test\n",
            ret);
    ret = RevertToSelf();
    ok(ret, "RevertToSelf failed with error %ld\n", GetLastError());

    /* test INHERIT_ONLY_ACE */
    ret = InitializeAcl(Acl, 256, ACL_REVISION);
    ok(ret, "InitializeAcl failed with error %ld\n", GetLastError());

    ret = AddAccessAllowedAceEx(Acl, ACL_REVISION, INHERIT_ONLY_ACE, KEY_READ, EveryoneSid);
    ok(ret, "AddAccessAllowedAceEx failed with error %ld\n", GetLastError());

    ret = AccessCheck(SecurityDescriptor, Token, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    ok(ret, "AccessCheck failed with error %ld\n", GetLastError());
    err = GetLastError();
    ok(!AccessStatus && err == ERROR_ACCESS_DENIED, "AccessCheck should have failed "
       "with ERROR_ACCESS_DENIED, instead of %ld\n", err);
    ok(!Access, "Should have failed to grant any access, got 0x%08lx\n", Access);

    CloseHandle(Token);

    res = DuplicateToken(ProcessToken, SecurityAnonymous, &Token);
    ok(res, "DuplicateToken failed with error %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = AccessCheck(SecurityDescriptor, Token, MAXIMUM_ALLOWED, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_BAD_IMPERSONATION_LEVEL, "AccessCheck should have failed "
       "with ERROR_BAD_IMPERSONATION_LEVEL, instead of %ld\n", err);

    CloseHandle(Token);

    SetLastError(0xdeadbeef);
    ret = AccessCheck(SecurityDescriptor, ProcessToken, KEY_READ, &Mapping,
                      PrivSet, &PrivSetLen, &Access, &AccessStatus);
    err = GetLastError();
    ok(!ret && err == ERROR_NO_IMPERSONATION_TOKEN, "AccessCheck should have failed "
       "with ERROR_NO_IMPERSONATION_TOKEN, instead of %ld\n", err);

    CloseHandle(ProcessToken);

    if (EveryoneSid)
        FreeSid(EveryoneSid);
    if (AdminSid)
        FreeSid(AdminSid);
    if (UsersSid)
        FreeSid(UsersSid);
    free(Acl);
    free(SecurityDescriptor);
    free(PrivSet);
}

static TOKEN_USER *get_alloc_token_user( HANDLE token )
{
    TOKEN_USER *token_user;
    DWORD size;
    BOOL ret;

    ret = GetTokenInformation( token, TokenUser, NULL, 0, &size );
    ok(!ret, "Expected failure, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());

    token_user = malloc( size );
    ret = GetTokenInformation( token, TokenUser, token_user, size, &size );
    ok(ret, "GetTokenInformation failed with error %ld\n", GetLastError());

    return token_user;
}

static TOKEN_OWNER *get_alloc_token_owner( HANDLE token )
{
    TOKEN_OWNER *token_owner;
    DWORD size;
    BOOL ret;

    ret = GetTokenInformation( token, TokenOwner, NULL, 0, &size );
    ok(!ret, "Expected failure, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());

    token_owner = malloc( size );
    ret = GetTokenInformation( token, TokenOwner, token_owner, size, &size );
    ok(ret, "GetTokenInformation failed with error %ld\n", GetLastError());

    return token_owner;
}

static TOKEN_PRIMARY_GROUP *get_alloc_token_primary_group( HANDLE token )
{
    TOKEN_PRIMARY_GROUP *token_primary_group;
    DWORD size;
    BOOL ret;

    ret = GetTokenInformation( token, TokenPrimaryGroup, NULL, 0, &size );
    ok(!ret, "Expected failure, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());

    token_primary_group = malloc( size );
    ret = GetTokenInformation( token, TokenPrimaryGroup, token_primary_group, size, &size );
    ok(ret, "GetTokenInformation failed with error %ld\n", GetLastError());

    return token_primary_group;
}

/* test GetTokenInformation for the various attributes */
static void test_token_attr(void)
{
    HANDLE Token, ImpersonationToken;
    DWORD Size, Size2;
    TOKEN_PRIVILEGES *Privileges;
    TOKEN_GROUPS *Groups;
    TOKEN_USER *User;
    TOKEN_OWNER *Owner;
    TOKEN_DEFAULT_DACL *Dacl;
    BOOL ret;
    DWORD i, GLE;
#ifdef __REACTOS__
    DWORD logon_count = 0;
    BYTE logon_sid[SECURITY_MAX_SID_SIZE];
    TOKEN_GROUPS logon_groups;
#endif
    LPSTR SidString;
    SECURITY_IMPERSONATION_LEVEL ImpersonationLevel;
    ACL *acl;

    /* cygwin-like use case */
    SetLastError(0xdeadbeef);
    ret = OpenProcessToken(GetCurrentProcess(), MAXIMUM_ALLOWED, &Token);
    if(!ret && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED))
    {
        win_skip("OpenProcessToken is not implemented\n");
        return;
    }
    ok(ret, "OpenProcessToken failed with error %ld\n", GetLastError());
    if (ret)
    {
        DWORD buf[256]; /* GetTokenInformation wants a dword-aligned buffer */
        Size = sizeof(buf);
        ret = GetTokenInformation(Token, TokenUser,(void*)buf, Size, &Size);
        ok(ret, "GetTokenInformation failed with error %ld\n", GetLastError());
        Size = sizeof(ImpersonationLevel);
        ret = GetTokenInformation(Token, TokenImpersonationLevel, &ImpersonationLevel, Size, &Size);
        GLE = GetLastError();
        ok(!ret && (GLE == ERROR_INVALID_PARAMETER), "GetTokenInformation(TokenImpersonationLevel) on primary token should have failed with ERROR_INVALID_PARAMETER instead of %ld\n", GLE);
        CloseHandle(Token);
    }

    SetLastError(0xdeadbeef);
    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &Token);
    ok(ret, "OpenProcessToken failed with error %ld\n", GetLastError());

    /* groups */
    /* insufficient buffer length */
    SetLastError(0xdeadbeef);
    Size2 = 0;
    ret = GetTokenInformation(Token, TokenGroups, NULL, 0, &Size2);
    ok(Size2 > 1, "got %ld\n", Size2);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "%d with error %ld\n", ret, GetLastError());
    Size2 -= 1;
    Groups = malloc(Size2);
    memset(Groups, 0xcc, Size2);
    Size = 0;
    ret = GetTokenInformation(Token, TokenGroups, Groups, Size2, &Size);
    ok(Size > 1, "got %ld\n", Size);
    ok((!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER) || broken(ret) /* wow64 */,
        "%d with error %ld\n", ret, GetLastError());
    if(!ret)
        ok(*((BYTE*)Groups) == 0xcc, "buffer altered\n");

    free(Groups);

    SetLastError(0xdeadbeef);
    ret = GetTokenInformation(Token, TokenGroups, NULL, 0, &Size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "GetTokenInformation(TokenGroups) %s with error %ld\n",
        ret ? "succeeded" : "failed", GetLastError());
    Groups = malloc(Size);
    SetLastError(0xdeadbeef);
    ret = GetTokenInformation(Token, TokenGroups, Groups, Size, &Size);
    ok(ret, "GetTokenInformation(TokenGroups) failed with error %ld\n", GetLastError());
    ok(GetLastError() == 0xdeadbeef,
       "GetTokenInformation shouldn't have set last error to %ld\n",
       GetLastError());
    trace("TokenGroups:\n");
    for (i = 0; i < Groups->GroupCount; i++)
    {
        DWORD NameLength = 255;
        CHAR Name[255];
        DWORD DomainLength = 255;
        CHAR Domain[255];
        SID_NAME_USE SidNameUse;
#ifdef __REACTOS__
        if ((Groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
        {
            ++logon_count;
            ret = CopySid(sizeof(logon_sid), logon_sid, Groups->Groups[i].Sid);
            ok(ret, "Logon SID copy failed: %lu\n", GetLastError());
        }
#endif
        Name[0] = '\0';
        Domain[0] = '\0';
        ret = LookupAccountSidA(NULL, Groups->Groups[i].Sid, Name, &NameLength, Domain, &DomainLength, &SidNameUse);
        if (ret)
        {
            ConvertSidToStringSidA(Groups->Groups[i].Sid, &SidString);
            trace("%s, %s\\%s use: %d attr: 0x%08lx\n", SidString, Domain, Name, SidNameUse, Groups->Groups[i].Attributes);
            LocalFree(SidString);
        }
        else trace("attr: 0x%08lx LookupAccountSid failed with error %ld\n", Groups->Groups[i].Attributes, GetLastError());
    }
    free(Groups);

    /* user */
    ret = GetTokenInformation(Token, TokenUser, NULL, 0, &Size);
    ok(!ret && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "GetTokenInformation(TokenUser) failed with error %ld\n", GetLastError());
    User = malloc(Size);
    ret = GetTokenInformation(Token, TokenUser, User, Size, &Size);
    ok(ret,
        "GetTokenInformation(TokenUser) failed with error %ld\n", GetLastError());

    ConvertSidToStringSidA(User->User.Sid, &SidString);
    trace("TokenUser: %s attr: 0x%08lx\n", SidString, User->User.Attributes);
    LocalFree(SidString);
    free(User);

    /* owner */
    ret = GetTokenInformation(Token, TokenOwner, NULL, 0, &Size);
    ok(!ret && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "GetTokenInformation(TokenOwner) failed with error %ld\n", GetLastError());
    Owner = malloc(Size);
    ret = GetTokenInformation(Token, TokenOwner, Owner, Size, &Size);
    ok(ret,
        "GetTokenInformation(TokenOwner) failed with error %ld\n", GetLastError());

    ConvertSidToStringSidA(Owner->Owner, &SidString);
    trace("TokenOwner: %s\n", SidString);
    LocalFree(SidString);
    free(Owner);

    /* logon */
#ifdef __REACTOS__
    ok(logon_count <= 1, "Found %lu logon SIDs\n", logon_count);
#endif
    ret = GetTokenInformation(Token, TokenLogonSid, NULL, 0, &Size);
#ifdef __REACTOS__
    if (!logon_count)
    {
        ok(!ret && GetLastError() == ERROR_NOT_FOUND,
           "Token without a logon SID returned %d, error %lu\n", ret, GetLastError());
        ret = GetTokenInformation(Token, TokenLogonSid, &logon_groups, sizeof(logon_groups), &Size);
        ok(!ret && GetLastError() == ERROR_NOT_FOUND,
           "Token without a logon SID returned %d, error %lu\n", ret, GetLastError());
    }
#else
    if (!ret && (GetLastError() == ERROR_INVALID_PARAMETER))
        todo_wine win_skip("TokenLogonSid not supported. Skipping tests\n");
#endif
    else
    {
        ok(!ret && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
            "GetTokenInformation(TokenLogonSid) failed with error %ld\n", GetLastError());
        Groups = malloc(Size);
        ret = GetTokenInformation(Token, TokenLogonSid, Groups, Size, &Size);
        ok(ret,
            "GetTokenInformation(TokenLogonSid) failed with error %ld\n", GetLastError());
        if (ret)
        {
            ok(Groups->GroupCount == 1, "got %ld\n", Groups->GroupCount);
            if(Groups->GroupCount == 1)
            {
#ifdef __REACTOS__
                ok(EqualSid(Groups->Groups[0].Sid, logon_sid), "Logon SID differs from TokenGroups\n");
#endif
                ConvertSidToStringSidA(Groups->Groups[0].Sid, &SidString);
                trace("TokenLogon: %s\n", SidString);
                LocalFree(SidString);

                /* S-1-5-5-0-XXXXXX */
                ret = IsWellKnownSid(Groups->Groups[0].Sid, WinLogonIdsSid);
                ok(ret, "Unknown SID\n");

                ok(Groups->Groups[0].Attributes == (SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED | SE_GROUP_LOGON_ID),
                    "got %lx\n", Groups->Groups[0].Attributes);
            }
        }

        free(Groups);
    }

    /* privileges */
    ret = GetTokenInformation(Token, TokenPrivileges, NULL, 0, &Size);
    ok(!ret && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "GetTokenInformation(TokenPrivileges) failed with error %ld\n", GetLastError());
    Privileges = malloc(Size);
    ret = GetTokenInformation(Token, TokenPrivileges, Privileges, Size, &Size);
    ok(ret,
        "GetTokenInformation(TokenPrivileges) failed with error %ld\n", GetLastError());
    trace("TokenPrivileges:\n");
    for (i = 0; i < Privileges->PrivilegeCount; i++)
    {
        CHAR Name[256];
        DWORD NameLen = ARRAY_SIZE(Name);
        LookupPrivilegeNameA(NULL, &Privileges->Privileges[i].Luid, Name, &NameLen);
        trace("\t%s, 0x%lx\n", Name, Privileges->Privileges[i].Attributes);
    }
    free(Privileges);

    ret = DuplicateToken(Token, SecurityAnonymous, &ImpersonationToken);
    ok(ret, "DuplicateToken failed with error %ld\n", GetLastError());

    Size = sizeof(ImpersonationLevel);
    ret = GetTokenInformation(ImpersonationToken, TokenImpersonationLevel, &ImpersonationLevel, Size, &Size);
    ok(ret, "GetTokenInformation(TokenImpersonationLevel) failed with error %ld\n", GetLastError());
    ok(ImpersonationLevel == SecurityAnonymous, "ImpersonationLevel should have been SecurityAnonymous instead of %d\n", ImpersonationLevel);

    CloseHandle(ImpersonationToken);

    /* default dacl */
    ret = GetTokenInformation(Token, TokenDefaultDacl, NULL, 0, &Size);
    ok(!ret && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "GetTokenInformation(TokenDefaultDacl) failed with error %lu\n", GetLastError());

    Dacl = malloc(Size);
    ret = GetTokenInformation(Token, TokenDefaultDacl, Dacl, Size, &Size);
    ok(ret, "GetTokenInformation(TokenDefaultDacl) failed with error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = SetTokenInformation(Token, TokenDefaultDacl, NULL, 0);
    GLE = GetLastError();
    ok(!ret, "SetTokenInformation(TokenDefaultDacl) succeeded\n");
    ok(GLE == ERROR_BAD_LENGTH, "expected ERROR_BAD_LENGTH got %lu\n", GLE);

    SetLastError(0xdeadbeef);
    ret = SetTokenInformation(Token, TokenDefaultDacl, NULL, Size);
    GLE = GetLastError();
    ok(!ret, "SetTokenInformation(TokenDefaultDacl) succeeded\n");
    ok(GLE == ERROR_NOACCESS, "expected ERROR_NOACCESS got %lu\n", GLE);

    acl = Dacl->DefaultDacl;
    Dacl->DefaultDacl = NULL;

    ret = SetTokenInformation(Token, TokenDefaultDacl, Dacl, Size);
    ok(ret, "SetTokenInformation(TokenDefaultDacl) succeeded\n");

    Size2 = 0;
    Dacl->DefaultDacl = (ACL *)0xdeadbeef;
    ret = GetTokenInformation(Token, TokenDefaultDacl, Dacl, Size, &Size2);
    ok(ret, "GetTokenInformation(TokenDefaultDacl) failed with error %lu\n", GetLastError());
    ok(Dacl->DefaultDacl == NULL, "expected NULL, got %p\n", Dacl->DefaultDacl);
    ok(Size2 == sizeof(TOKEN_DEFAULT_DACL) || broken(Size2 == 2*sizeof(TOKEN_DEFAULT_DACL)), /* WoW64 */
       "got %lu expected sizeof(TOKEN_DEFAULT_DACL)\n", Size2);

    Dacl->DefaultDacl = acl;
    ret = SetTokenInformation(Token, TokenDefaultDacl, Dacl, Size);
    ok(ret, "SetTokenInformation(TokenDefaultDacl) failed with error %lu\n", GetLastError());

    if (Size2 == sizeof(TOKEN_DEFAULT_DACL)) {
        ret = GetTokenInformation(Token, TokenDefaultDacl, Dacl, Size, &Size2);
        ok(ret, "GetTokenInformation(TokenDefaultDacl) failed with error %lu\n", GetLastError());
    } else
        win_skip("TOKEN_DEFAULT_DACL size too small on WoW64\n");

    free(Dacl);
    CloseHandle(Token);
}

static void test_GetTokenInformation(void)
{
    DWORD is_app_container, size;
    HANDLE token;
    BOOL ret;

    ret = OpenProcessToken(GetCurrentProcess(), MAXIMUM_ALLOWED, &token);
    ok(ret, "OpenProcessToken failed: %lu\n", GetLastError());

    size = 0;
    is_app_container = 0xdeadbeef;
    ret = GetTokenInformation(token, TokenIsAppContainer, &is_app_container,
                              sizeof(is_app_container), &size);
    ok(ret || broken(GetLastError() == ERROR_INVALID_PARAMETER ||
                     GetLastError() == ERROR_INVALID_FUNCTION), /* pre-win8 */
       "GetTokenInformation failed: %lu\n", GetLastError());
    if(ret) {
        ok(size == sizeof(is_app_container), "size = %lu\n", size);
        ok(!is_app_container, "is_app_container = %lx\n", is_app_container);
    }

    CloseHandle(token);
}

typedef union _MAX_SID
{
    SID sid;
    char max[SECURITY_MAX_SID_SIZE];
} MAX_SID;

static void test_sid_str(PSID * sid)
{
    char *str_sid;
    BOOL ret = ConvertSidToStringSidA(sid, &str_sid);
    ok(ret, "ConvertSidToStringSidA() failed: %ld\n", GetLastError());
    if (ret)
    {
        char account[MAX_PATH], domain[MAX_PATH];
        SID_NAME_USE use;
        DWORD acc_size = MAX_PATH;
        DWORD dom_size = MAX_PATH;
        ret = LookupAccountSidA (NULL, sid, account, &acc_size, domain, &dom_size, &use);
        ok(ret || GetLastError() == ERROR_NONE_MAPPED,
           "LookupAccountSid(%s) failed: %ld\n", str_sid, GetLastError());
        if (ret)
            trace(" %s %s\\%s %d\n", str_sid, domain, account, use);
        else if (GetLastError() == ERROR_NONE_MAPPED)
            trace(" %s couldn't be mapped\n", str_sid);
        LocalFree(str_sid);
    }
}

static const struct well_known_sid_value
{
    BOOL without_domain;
    const char *sid_string;
} well_known_sid_values[] = {
/*  0 */ {TRUE, "S-1-0-0"},  {TRUE, "S-1-1-0"},  {TRUE, "S-1-2-0"},  {TRUE, "S-1-3-0"},
/*  4 */ {TRUE, "S-1-3-1"},  {TRUE, "S-1-3-2"},  {TRUE, "S-1-3-3"},  {TRUE, "S-1-5"},
/*  8 */ {FALSE, "S-1-5-1"}, {TRUE, "S-1-5-2"},  {TRUE, "S-1-5-3"},  {TRUE, "S-1-5-4"},
/* 12 */ {TRUE, "S-1-5-6"},  {TRUE, "S-1-5-7"},  {TRUE, "S-1-5-8"},  {TRUE, "S-1-5-9"},
/* 16 */ {TRUE, "S-1-5-10"}, {TRUE, "S-1-5-11"}, {TRUE, "S-1-5-12"}, {TRUE, "S-1-5-13"},
/* 20 */ {TRUE, "S-1-5-14"}, {FALSE, NULL},      {TRUE, "S-1-5-18"}, {TRUE, "S-1-5-19"},
/* 24 */ {TRUE, "S-1-5-20"}, {TRUE, "S-1-5-32"},
/* 26 */ {FALSE, "S-1-5-32-544"}, {TRUE, "S-1-5-32-545"}, {TRUE, "S-1-5-32-546"},
/* 29 */ {TRUE, "S-1-5-32-547"},  {TRUE, "S-1-5-32-548"}, {TRUE, "S-1-5-32-549"},
/* 32 */ {TRUE, "S-1-5-32-550"},  {TRUE, "S-1-5-32-551"}, {TRUE, "S-1-5-32-552"},
/* 35 */ {TRUE, "S-1-5-32-554"},  {TRUE, "S-1-5-32-555"}, {TRUE, "S-1-5-32-556"},
#ifdef __REACTOS__
/* 38 */ {FALSE, "S-1-5-21-12-23-34-500"}, {FALSE, "S-1-5-21-12-23-34-501"},
/* 40 */ {FALSE, "S-1-5-21-12-23-34-502"}, {FALSE, "S-1-5-21-12-23-34-512"},
/* 42 */ {FALSE, "S-1-5-21-12-23-34-513"}, {FALSE, "S-1-5-21-12-23-34-514"},
/* 44 */ {FALSE, "S-1-5-21-12-23-34-515"}, {FALSE, "S-1-5-21-12-23-34-516"},
/* 46 */ {FALSE, "S-1-5-21-12-23-34-517"}, {FALSE, "S-1-5-21-12-23-34-518"},
/* 48 */ {FALSE, "S-1-5-21-12-23-34-519"}, {FALSE, "S-1-5-21-12-23-34-520"},
/* 50 */ {FALSE, "S-1-5-21-12-23-34-553"},
#else
/* 38 */ {FALSE, "S-1-5-21-12-23-34-45-56-500"}, {FALSE, "S-1-5-21-12-23-34-45-56-501"},
/* 40 */ {FALSE, "S-1-5-21-12-23-34-45-56-502"}, {FALSE, "S-1-5-21-12-23-34-45-56-512"},
/* 42 */ {FALSE, "S-1-5-21-12-23-34-45-56-513"}, {FALSE, "S-1-5-21-12-23-34-45-56-514"},
/* 44 */ {FALSE, "S-1-5-21-12-23-34-45-56-515"}, {FALSE, "S-1-5-21-12-23-34-45-56-516"},
/* 46 */ {FALSE, "S-1-5-21-12-23-34-45-56-517"}, {FALSE, "S-1-5-21-12-23-34-45-56-518"},
/* 48 */ {FALSE, "S-1-5-21-12-23-34-45-56-519"}, {FALSE, "S-1-5-21-12-23-34-45-56-520"},
/* 50 */ {FALSE, "S-1-5-21-12-23-34-45-56-553"},
#endif
/* Added in Windows Server 2003 */
/* 51 */ {TRUE, "S-1-5-64-10"},   {TRUE, "S-1-5-64-21"},   {TRUE, "S-1-5-64-14"},
/* 54 */ {TRUE, "S-1-5-15"},      {TRUE, "S-1-5-1000"},    {FALSE, "S-1-5-32-557"},
/* 57 */ {TRUE, "S-1-5-32-558"},  {TRUE, "S-1-5-32-559"},  {TRUE, "S-1-5-32-560"},
/* 60 */ {TRUE, "S-1-5-32-561"}, {TRUE, "S-1-5-32-562"},
/* Added in Windows Vista: */
/* 62 */ {TRUE, "S-1-5-32-568"},
/* 63 */ {TRUE, "S-1-5-17"},      {FALSE, "S-1-5-32-569"}, {TRUE, "S-1-16-0"},
/* 66 */ {TRUE, "S-1-16-4096"},   {TRUE, "S-1-16-8192"},   {TRUE, "S-1-16-12288"},
/* 69 */ {TRUE, "S-1-16-16384"},  {TRUE, "S-1-5-33"},      {TRUE, "S-1-3-4"},
#ifdef __REACTOS__
/* 72 */ {FALSE, "S-1-5-21-12-23-34-571"},  {FALSE, "S-1-5-21-12-23-34-572"},
/* 74 */ {TRUE, "S-1-5-22"}, {FALSE, "S-1-5-21-12-23-34-521"}, {TRUE, "S-1-5-32-573"},
/* 77 */ {FALSE, "S-1-5-21-12-23-34-498"}, {TRUE, "S-1-5-32-574"}, {TRUE, "S-1-16-8448"},
#else
/* 72 */ {FALSE, "S-1-5-21-12-23-34-45-56-571"},  {FALSE, "S-1-5-21-12-23-34-45-56-572"},
/* 74 */ {TRUE, "S-1-5-22"}, {FALSE, "S-1-5-21-12-23-34-45-56-521"}, {TRUE, "S-1-5-32-573"},
/* 77 */ {FALSE, "S-1-5-21-12-23-34-45-56-498"}, {TRUE, "S-1-5-32-574"}, {TRUE, "S-1-16-8448"},
#endif
/* 80 */ {FALSE, NULL}, {TRUE, "S-1-2-1"}, {TRUE, "S-1-5-65-1"}, {FALSE, NULL},
/* 84 */ {TRUE, "S-1-15-2-1"},
};

static void test_CreateWellKnownSid(void)
{
    SID_IDENTIFIER_AUTHORITY ident = { SECURITY_NT_AUTHORITY };
    PSID domainsid, sid;
    DWORD size, error;
    BOOL ret;
    unsigned int i;

    size = 0;
    SetLastError(0xdeadbeef);
    ret = CreateWellKnownSid(WinInteractiveSid, NULL, NULL, &size);
    error = GetLastError();
    ok(!ret, "CreateWellKnownSid succeeded\n");
    ok(error == ERROR_INSUFFICIENT_BUFFER, "expected ERROR_INSUFFICIENT_BUFFER, got %lu\n", error);
    ok(size, "expected size > 0\n");

    SetLastError(0xdeadbeef);
    ret = CreateWellKnownSid(WinInteractiveSid, NULL, NULL, &size);
    error = GetLastError();
    ok(!ret, "CreateWellKnownSid succeeded\n");
    ok(error == ERROR_INVALID_PARAMETER, "expected ERROR_INVALID_PARAMETER, got %lu\n", error);

    sid = malloc(size);
    ret = CreateWellKnownSid(WinInteractiveSid, NULL, sid, &size);
    ok(ret, "CreateWellKnownSid failed %lu\n", GetLastError());
    free(sid);

#ifdef __REACTOS__
    ret = AllocateAndInitializeSid(&ident, 4, SECURITY_NT_NON_UNIQUE, 12, 23, 34, 0, 0, 0, 0, &domainsid);
    ok(ret, "AllocateAndInitializeSid failed with %lu\n", GetLastError());
    if (!ret) return;
#else
    /* a domain sid usually have three subauthorities but we test that CreateWellKnownSid doesn't check it */
    AllocateAndInitializeSid(&ident, 6, SECURITY_NT_NON_UNIQUE, 12, 23, 34, 45, 56, 0, 0, &domainsid);
#endif

    for (i = 0; i < ARRAY_SIZE(well_known_sid_values); i++)
    {
        const struct well_known_sid_value *value = &well_known_sid_values[i];
        char sid_buffer[SECURITY_MAX_SID_SIZE];
        LPSTR str;
        DWORD cb;

        if (value->sid_string == NULL)
            continue;

#ifndef __REACTOS__
        /* some SIDs aren't implemented by all Windows versions - detect it */
        cb = sizeof(sid_buffer);
        if (!CreateWellKnownSid(i, NULL, sid_buffer, &cb))
        {
            skip("Well known SID %u not implemented\n", i);
            continue;
        }

#endif
        cb = sizeof(sid_buffer);
#ifdef __REACTOS__
        ret = CreateWellKnownSid(i, value->without_domain ? NULL : domainsid, sid_buffer, &cb);
        ok(ret, "Couldn't create well known sid %u, error %lu\n", i, GetLastError());
        if (!ret) continue;
#else
        ok(CreateWellKnownSid(i, value->without_domain ? NULL : domainsid, sid_buffer, &cb), "Couldn't create well known sid %u\n", i);
#endif
        expect_eq(GetSidLengthRequired(*GetSidSubAuthorityCount(sid_buffer)), cb, DWORD, "%ld");
        ok(IsValidSid(sid_buffer), "The sid is not valid\n");
#ifdef __REACTOS__
        ok(IsWellKnownSid(sid_buffer, i), "SID type %u was not recognized\n", i);
        if (*GetSidSubAuthorityCount(sid_buffer) == 5 &&
            *GetSidSubAuthority(sid_buffer, 0) == SECURITY_NT_NON_UNIQUE)
        {
            char other[SECURITY_MAX_SID_SIZE];
            memcpy(other, sid_buffer, cb);
            ++*GetSidSubAuthority(other, 4);
            ok(!IsWellKnownSid(other, i), "SID type %u accepted a different account RID\n", i);
            memcpy(other, sid_buffer, cb);
            ++*GetSidSubAuthority(other, 0);
            ok(!IsWellKnownSid(other, i), "SID type %u accepted a different domain prefix\n", i);
            memcpy(other, sid_buffer, cb);
            ++GetSidIdentifierAuthority(other)->Value[5];
            ok(!IsWellKnownSid(other, i), "SID type %u accepted a different authority\n", i);
            memcpy(other, sid_buffer, cb);
            --*GetSidSubAuthorityCount(other);
            ok(!IsWellKnownSid(other, i), "SID type %u accepted a domain without an account RID\n", i);
        }
        ret = ConvertSidToStringSidA(sid_buffer, &str);
        ok(ret, "Couldn't convert SID to string, error %lu\n", GetLastError());
        if (ret)
        {
            ok(strcmp(str, value->sid_string) == 0, "%d: SID mismatch - expected %s, got %s\n", i,
                value->sid_string, str);
            LocalFree(str);
        }
#else
        ok(ConvertSidToStringSidA(sid_buffer, &str), "Couldn't convert SID to string\n");
        ok(strcmp(str, value->sid_string) == 0, "%d: SID mismatch - expected %s, got %s\n", i,
            value->sid_string, str);
        LocalFree(str);
#endif

        if (value->without_domain)
        {
            char buf2[SECURITY_MAX_SID_SIZE];
            cb = sizeof(buf2);
#ifdef __REACTOS__
            ret = CreateWellKnownSid(i, domainsid, buf2, &cb);
            ok(ret, "Couldn't create well known sid %u with optional domain, error %lu\n", i, GetLastError());
            if (ret)
            {
                expect_eq(GetSidLengthRequired(*GetSidSubAuthorityCount(sid_buffer)), cb, DWORD, "%ld");
                ok(cb <= sizeof(buf2) && !memcmp(buf2, sid_buffer, cb),
                   "SID create with domain is different than without (%u)\n", i);
            }
#else
            ok(CreateWellKnownSid(i, domainsid, buf2, &cb), "Couldn't create well known sid %u with optional domain\n", i);
            expect_eq(GetSidLengthRequired(*GetSidSubAuthorityCount(sid_buffer)), cb, DWORD, "%ld");
            ok(memcmp(buf2, sid_buffer, cb) == 0, "SID create with domain is different than without (%u)\n", i);
#endif
        }
    }

    FreeSid(domainsid);
#ifdef __REACTOS__
    ret = AllocateAndInitializeSid(&ident, 6, SECURITY_NT_NON_UNIQUE, 12, 23, 34, 45, 56, 0, 0, &domainsid);
    ok(ret, "Extended domain SID allocation failed with %lu\n", GetLastError());
    if (ret)
    {
        char sid_buffer[SECURITY_MAX_SID_SIZE], *string;
        size = sizeof(sid_buffer);
        ret = CreateWellKnownSid(WinAccountAdministratorSid, domainsid, sid_buffer, &size);
        ok(ret, "Extended domain SID creation failed with %lu\n", GetLastError());
        if (ret)
        {
            ret = ConvertSidToStringSidA(sid_buffer, &string);
            ok(ret, "Extended SID conversion failed with %lu\n", GetLastError());
            if (ret)
            {
                ok(!strcmp(string, "S-1-5-21-12-23-34-45-56-500"), "Unexpected extended SID %s\n", string);
                LocalFree(string);
            }
        }
        FreeSid(domainsid);
    }
#endif
}

static void test_LookupAccountSid(void)
{
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = { SECURITY_NT_AUTHORITY };
    CHAR accountA[MAX_PATH], domainA[MAX_PATH], usernameA[MAX_PATH];
    DWORD acc_sizeA, dom_sizeA, user_sizeA;
    DWORD real_acc_sizeA, real_dom_sizeA;
    WCHAR accountW[MAX_PATH], domainW[MAX_PATH];
    LSA_OBJECT_ATTRIBUTES object_attributes;
    DWORD acc_sizeW, dom_sizeW;
    DWORD real_acc_sizeW, real_dom_sizeW;
    PSID pUsersSid = NULL;
    SID_NAME_USE use;
    BOOL ret;
    DWORD error, size, cbti = 0;
    MAX_SID  max_sid;
    CHAR *str_sidA;
    int i;
    HANDLE hToken;
    PTOKEN_USER ptiUser = NULL;
    LSA_HANDLE handle;
    NTSTATUS status;

    /* native windows crashes if account size, domain size, or name use is NULL */

    ret = AllocateAndInitializeSid(&SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &pUsersSid);
    ok(ret || (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED),
       "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    /* not running on NT so give up */
    if (!ret && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED))
        return;

    real_acc_sizeA = MAX_PATH;
    real_dom_sizeA = MAX_PATH;
    ret = LookupAccountSidA(NULL, pUsersSid, accountA, &real_acc_sizeA, domainA, &real_dom_sizeA, &use);
    ok(ret, "LookupAccountSidA() Expected TRUE, got FALSE\n");

    /* try NULL account */
    acc_sizeA = MAX_PATH;
    dom_sizeA = MAX_PATH;
    ret = LookupAccountSidA(NULL, pUsersSid, NULL, &acc_sizeA, domainA, &dom_sizeA, &use);
    ok(ret, "LookupAccountSidA() Expected TRUE, got FALSE\n");

    /* try NULL domain */
    acc_sizeA = MAX_PATH;
    dom_sizeA = MAX_PATH;
    ret = LookupAccountSidA(NULL, pUsersSid, accountA, &acc_sizeA, NULL, &dom_sizeA, &use);
    ok(ret, "LookupAccountSidA() Expected TRUE, got FALSE\n");

    /* try a small account buffer */
    acc_sizeA = 1;
    dom_sizeA = MAX_PATH;
    accountA[0] = 0;
    ret = LookupAccountSidA(NULL, pUsersSid, accountA, &acc_sizeA, domainA, &dom_sizeA, &use);
    ok(!ret, "LookupAccountSidA() Expected FALSE got TRUE\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "LookupAccountSidA() Expected ERROR_NOT_ENOUGH_MEMORY, got %lu\n", GetLastError());

    /* try a 0 sized account buffer */
    acc_sizeA = 0;
    dom_sizeA = MAX_PATH;
    accountA[0] = 0;
    LookupAccountSidA(NULL, pUsersSid, accountA, &acc_sizeA, domainA, &dom_sizeA, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(acc_sizeA == real_acc_sizeA + 1,
       "LookupAccountSidA() Expected acc_size = %lu, got %lu\n",
       real_acc_sizeA + 1, acc_sizeA);

    /* try a 0 sized account buffer */
    acc_sizeA = 0;
    dom_sizeA = MAX_PATH;
    LookupAccountSidA(NULL, pUsersSid, NULL, &acc_sizeA, domainA, &dom_sizeA, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(acc_sizeA == real_acc_sizeA + 1,
       "LookupAccountSid() Expected acc_size = %lu, got %lu\n",
       real_acc_sizeA + 1, acc_sizeA);

    /* try a small domain buffer */
    dom_sizeA = 1;
    acc_sizeA = MAX_PATH;
    accountA[0] = 0;
    ret = LookupAccountSidA(NULL, pUsersSid, accountA, &acc_sizeA, domainA, &dom_sizeA, &use);
    ok(!ret, "LookupAccountSidA() Expected FALSE got TRUE\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "LookupAccountSidA() Expected ERROR_NOT_ENOUGH_MEMORY, got %lu\n", GetLastError());

    /* try a 0 sized domain buffer */
    dom_sizeA = 0;
    acc_sizeA = MAX_PATH;
    accountA[0] = 0;
    LookupAccountSidA(NULL, pUsersSid, accountA, &acc_sizeA, domainA, &dom_sizeA, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(dom_sizeA == real_dom_sizeA + 1,
       "LookupAccountSidA() Expected dom_size = %lu, got %lu\n",
       real_dom_sizeA + 1, dom_sizeA);

    /* try a 0 sized domain buffer */
    dom_sizeA = 0;
    acc_sizeA = MAX_PATH;
    LookupAccountSidA(NULL, pUsersSid, accountA, &acc_sizeA, NULL, &dom_sizeA, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(dom_sizeA == real_dom_sizeA + 1,
       "LookupAccountSidA() Expected dom_size = %lu, got %lu\n",
       real_dom_sizeA + 1, dom_sizeA);

    real_acc_sizeW = MAX_PATH;
    real_dom_sizeW = MAX_PATH;
    ret = LookupAccountSidW(NULL, pUsersSid, accountW, &real_acc_sizeW, domainW, &real_dom_sizeW, &use);
    ok(ret, "LookupAccountSidW() Expected TRUE, got FALSE\n");

    /* try an invalid system name */
    real_acc_sizeA = MAX_PATH;
    real_dom_sizeA = MAX_PATH;
    ret = LookupAccountSidA("deepthought", pUsersSid, accountA, &real_acc_sizeA, domainA, &real_dom_sizeA, &use);
    ok(!ret, "LookupAccountSidA() Expected FALSE got TRUE\n");
    ok(GetLastError() == RPC_S_SERVER_UNAVAILABLE || GetLastError() == RPC_S_INVALID_NET_ADDR /* Vista */,
       "LookupAccountSidA() Expected RPC_S_SERVER_UNAVAILABLE or RPC_S_INVALID_NET_ADDR, got %lu\n", GetLastError());

    /* native windows crashes if domainW or accountW is NULL */

    /* try a small account buffer */
    acc_sizeW = 1;
    dom_sizeW = MAX_PATH;
    accountW[0] = 0;
    ret = LookupAccountSidW(NULL, pUsersSid, accountW, &acc_sizeW, domainW, &dom_sizeW, &use);
    ok(!ret, "LookupAccountSidW() Expected FALSE got TRUE\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "LookupAccountSidW() Expected ERROR_NOT_ENOUGH_MEMORY, got %lu\n", GetLastError());

    /* try a 0 sized account buffer */
    acc_sizeW = 0;
    dom_sizeW = MAX_PATH;
    accountW[0] = 0;
    LookupAccountSidW(NULL, pUsersSid, accountW, &acc_sizeW, domainW, &dom_sizeW, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(acc_sizeW == real_acc_sizeW + 1,
       "LookupAccountSidW() Expected acc_size = %lu, got %lu\n",
       real_acc_sizeW + 1, acc_sizeW);

    /* try a 0 sized account buffer */
    acc_sizeW = 0;
    dom_sizeW = MAX_PATH;
    LookupAccountSidW(NULL, pUsersSid, NULL, &acc_sizeW, domainW, &dom_sizeW, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(acc_sizeW == real_acc_sizeW + 1,
       "LookupAccountSidW() Expected acc_size = %lu, got %lu\n",
       real_acc_sizeW + 1, acc_sizeW);

    /* try a small domain buffer */
    dom_sizeW = 1;
    acc_sizeW = MAX_PATH;
    accountW[0] = 0;
    ret = LookupAccountSidW(NULL, pUsersSid, accountW, &acc_sizeW, domainW, &dom_sizeW, &use);
    ok(!ret, "LookupAccountSidW() Expected FALSE got TRUE\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "LookupAccountSidW() Expected ERROR_NOT_ENOUGH_MEMORY, got %lu\n", GetLastError());

    /* try a 0 sized domain buffer */
    dom_sizeW = 0;
    acc_sizeW = MAX_PATH;
    accountW[0] = 0;
    LookupAccountSidW(NULL, pUsersSid, accountW, &acc_sizeW, domainW, &dom_sizeW, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(dom_sizeW == real_dom_sizeW + 1,
       "LookupAccountSidW() Expected dom_size = %lu, got %lu\n",
       real_dom_sizeW + 1, dom_sizeW);

    /* try a 0 sized domain buffer */
    dom_sizeW = 0;
    acc_sizeW = MAX_PATH;
    LookupAccountSidW(NULL, pUsersSid, accountW, &acc_sizeW, NULL, &dom_sizeW, &use);
    /* this can fail or succeed depending on OS version but the size will always be returned */
    ok(dom_sizeW == real_dom_sizeW + 1,
       "LookupAccountSidW() Expected dom_size = %lu, got %lu\n",
       real_dom_sizeW + 1, dom_sizeW);

    acc_sizeW = dom_sizeW = use = 0;
    SetLastError(0xdeadbeef);
    ret = LookupAccountSidW(NULL, pUsersSid, NULL, &acc_sizeW, NULL, &dom_sizeW, &use);
    error = GetLastError();
    ok(!ret, "LookupAccountSidW failed %lu\n", GetLastError());
    ok(error == ERROR_INSUFFICIENT_BUFFER, "expected ERROR_INSUFFICIENT_BUFFER, got %lu\n", error);
    ok(acc_sizeW, "expected non-zero account size\n");
    ok(dom_sizeW, "expected non-zero domain size\n");
    ok(!use, "expected zero use %u\n", use);

    FreeSid(pUsersSid);

    /* Test LookupAccountSid with Sid retrieved from token information.
     This assumes this process is running under the account of the current user.*/
    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY|TOKEN_DUPLICATE, &hToken);
    ok(ret, "OpenProcessToken failed with error %ld\n", GetLastError());
    ret = GetTokenInformation(hToken, TokenUser, NULL, 0, &cbti);
    ok(!ret, "GetTokenInformation failed with error %ld\n", GetLastError());
    ptiUser = malloc(cbti);
    if (GetTokenInformation(hToken, TokenUser, ptiUser, cbti, &cbti))
    {
        acc_sizeA = dom_sizeA = MAX_PATH;
        ret = LookupAccountSidA(NULL, ptiUser->User.Sid, accountA, &acc_sizeA, domainA, &dom_sizeA, &use);
        ok(ret, "LookupAccountSidA() Expected TRUE, got FALSE\n");
        user_sizeA = MAX_PATH;
        ret = GetUserNameA(usernameA , &user_sizeA);
        ok(ret, "GetUserNameA() Expected TRUE, got FALSE\n");
        ok(lstrcmpA(usernameA, accountA) == 0, "LookupAccountSidA() Expected account name: %s got: %s\n", usernameA, accountA );
    }
    free(ptiUser);

    trace("Well Known SIDs:\n");
    for (i = 0; i <= 60; i++)
    {
        size = SECURITY_MAX_SID_SIZE;
        if (CreateWellKnownSid(i, NULL, &max_sid.sid, &size))
        {
            if (ConvertSidToStringSidA(&max_sid.sid, &str_sidA))
            {
                acc_sizeA = MAX_PATH;
                dom_sizeA = MAX_PATH;
                if (LookupAccountSidA(NULL, &max_sid.sid, accountA, &acc_sizeA, domainA, &dom_sizeA, &use))
                    trace(" %d: %s %s\\%s %d\n", i, str_sidA, domainA, accountA, use);
                LocalFree(str_sidA);
            }
        }
        else
        {
            if (GetLastError() != ERROR_INVALID_PARAMETER)
                trace(" CreateWellKnownSid(%d) failed: %ld\n", i, GetLastError());
            else
                trace(" %d: not supported\n", i);
        }
    }

    ZeroMemory(&object_attributes, sizeof(object_attributes));
    object_attributes.Length = sizeof(object_attributes);

    status = LsaOpenPolicy( NULL, &object_attributes, POLICY_ALL_ACCESS, &handle);
    ok(status == STATUS_SUCCESS || status == STATUS_ACCESS_DENIED,
       "LsaOpenPolicy(POLICY_ALL_ACCESS) returned 0x%08lx\n", status);

    /* try a more restricted access mask if necessary */
    if (status == STATUS_ACCESS_DENIED) {
        trace("LsaOpenPolicy(POLICY_ALL_ACCESS) failed, trying POLICY_VIEW_LOCAL_INFORMATION\n");
        status = LsaOpenPolicy( NULL, &object_attributes, POLICY_VIEW_LOCAL_INFORMATION, &handle);
        ok(status == STATUS_SUCCESS, "LsaOpenPolicy(POLICY_VIEW_LOCAL_INFORMATION) returned 0x%08lx\n", status);
    }

    if (status == STATUS_SUCCESS)
    {
        PPOLICY_ACCOUNT_DOMAIN_INFO info;
        status = LsaQueryInformationPolicy(handle, PolicyAccountDomainInformation, (PVOID*)&info);
        ok(status == STATUS_SUCCESS, "LsaQueryInformationPolicy() failed, returned 0x%08lx\n", status);
        if (status == STATUS_SUCCESS)
        {
            ok(info->DomainSid!=0, "LsaQueryInformationPolicy(PolicyAccountDomainInformation) missing SID\n");
            if (info->DomainSid)
            {
                int count = *GetSidSubAuthorityCount(info->DomainSid);
                CopySid(GetSidLengthRequired(count), &max_sid, info->DomainSid);
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = DOMAIN_USER_RID_ADMIN;
                max_sid.sid.SubAuthorityCount = count + 1;
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = DOMAIN_USER_RID_GUEST;
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_ADMINS;
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_USERS;
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_GUESTS;
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_COMPUTERS;
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_CONTROLLERS;
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_CERT_ADMINS;
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_SCHEMA_ADMINS;
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_ENTERPRISE_ADMINS;
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = DOMAIN_GROUP_RID_POLICY_ADMINS;
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = DOMAIN_ALIAS_RID_RAS_SERVERS;
                test_sid_str((PSID)&max_sid.sid);
                max_sid.sid.SubAuthority[count] = 1000;	/* first user account */
                test_sid_str((PSID)&max_sid.sid);
            }

            LsaFreeMemory(info);
        }

        status = LsaClose(handle);
        ok(status == STATUS_SUCCESS, "LsaClose() failed, returned 0x%08lx\n", status);
    }
}

static BOOL get_sid_info(PSID psid, LPSTR *user, LPSTR *dom)
{
    static CHAR account[UNLEN + 1];
    static CHAR domain[UNLEN + 1];
    DWORD size, dom_size;
    SID_NAME_USE use;

    *user = account;
    *dom = domain;

    size = dom_size = UNLEN + 1;
    account[0] = '\0';
    domain[0] = '\0';
    SetLastError(0xdeadbeef);
    return LookupAccountSidA(NULL, psid, account, &size, domain, &dom_size, &use);
}

static void check_wellknown_name(const char* name, WELL_KNOWN_SID_TYPE result)
{
    SID_IDENTIFIER_AUTHORITY ident = { SECURITY_NT_AUTHORITY };
    PSID domainsid = NULL;
    char wk_sid[SECURITY_MAX_SID_SIZE];
    DWORD cb;

    DWORD sid_size, domain_size;
    SID_NAME_USE sid_use;
    LPSTR domain, account, sid_domain, wk_domain, wk_account;
    PSID psid;
    BOOL ret ,ret2;

    sid_size = 0;
    domain_size = 0;
    ret = LookupAccountNameA(NULL, name, NULL, &sid_size, NULL, &domain_size, &sid_use);
    ok(!ret, " %s Should have failed to lookup account name\n", name);
    psid = malloc(sid_size);
    domain = malloc(domain_size);
    ret = LookupAccountNameA(NULL, name, psid, &sid_size, domain, &domain_size, &sid_use);

    if (!result)
    {
        ok(!ret, " %s Should have failed to lookup account name\n",name);
        goto cleanup;
    }

    AllocateAndInitializeSid(&ident, 6, SECURITY_NT_NON_UNIQUE, 12, 23, 34, 45, 56, 0, 0, &domainsid);
    cb = sizeof(wk_sid);
    if (!CreateWellKnownSid(result, domainsid, wk_sid, &cb))
    {
        win_skip("SID %i is not available on the system\n",result);
        goto cleanup;
    }

    ret2 = get_sid_info(wk_sid, &wk_account, &wk_domain);
    if (!ret2 && GetLastError() == ERROR_NONE_MAPPED)
    {
        win_skip("CreateWellKnownSid() succeeded but the account '%s' is not present (W2K)\n", name);
        goto cleanup;
    }

    get_sid_info(psid, &account, &sid_domain);

    ok(ret, "Failed to lookup account name %s\n",name);
    ok(sid_size != 0, "sid_size was zero\n");

    ok(EqualSid(psid,wk_sid),"%s Sid %s fails to match well known sid %s!\n",
       name, debugstr_sid(psid), debugstr_sid(wk_sid));

    ok(!lstrcmpA(account, wk_account), "Expected %s , got %s\n", account, wk_account);
    ok(!lstrcmpA(domain, wk_domain), "Expected %s, got %s\n", wk_domain, domain);
    ok(sid_use == SidTypeWellKnownGroup , "Expected Use (5), got %d\n", sid_use);

cleanup:
    FreeSid(domainsid);
    free(psid);
    free(domain);
}

static void test_LookupAccountName(void)
{
    DWORD sid_size, domain_size, user_size;
    DWORD sid_save, domain_save;
    CHAR user_name[UNLEN + 1];
    CHAR computer_name[UNLEN + 1];
    SID_NAME_USE sid_use;
    LPSTR domain, account, sid_dom;
    PSID psid;
    BOOL ret;

    /* native crashes if (assuming all other parameters correct):
     *  - peUse is NULL
     *  - Sid is NULL and cbSid is > 0
     *  - cbSid or cchReferencedDomainName are NULL
     *  - ReferencedDomainName is NULL and cchReferencedDomainName is the correct size
     */

    user_size = UNLEN + 1;
    SetLastError(0xdeadbeef);
    ret = GetUserNameA(user_name, &user_size);
    ok(ret, "Failed to get user name : %ld\n", GetLastError());

    /* get sizes */
    sid_size = 0;
    domain_size = 0;
    sid_use = 0xcafebabe;
    SetLastError(0xdeadbeef);
    ret = LookupAccountNameA(NULL, user_name, NULL, &sid_size, NULL, &domain_size, &sid_use);
    if(!ret && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED))
    {
        win_skip("LookupAccountNameA is not implemented\n");
        return;
    }
    ok(!ret, "Expected 0, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());
    ok(sid_size != 0, "Expected non-zero sid size\n");
    ok(domain_size != 0, "Expected non-zero domain size\n");
    ok(sid_use == (SID_NAME_USE)0xcafebabe, "Expected 0xcafebabe, got %d\n", sid_use);

    sid_save = sid_size;
    domain_save = domain_size;

    psid = malloc(sid_size);
    domain = malloc(domain_size);

    /* try valid account name */
    ret = LookupAccountNameA(NULL, user_name, psid, &sid_size, domain, &domain_size, &sid_use);
    get_sid_info(psid, &account, &sid_dom);
    ok(ret, "Failed to lookup account name\n");
    ok(sid_size == GetLengthSid(psid), "Expected %ld, got %ld\n", GetLengthSid(psid), sid_size);
    ok(!lstrcmpA(account, user_name), "Expected %s, got %s\n", user_name, account);
    ok(!lstrcmpiA(domain, sid_dom), "Expected %s, got %s\n", sid_dom, domain);
    ok(domain_size == domain_save - 1, "Expected %ld, got %ld\n", domain_save - 1, domain_size);
    ok(strlen(domain) == domain_size, "Expected %d, got %ld\n", lstrlenA(domain), domain_size);
#ifdef __REACTOS__
    if (IsWellKnownSid(psid, WinLocalSystemSid))
        ok(sid_use == SidTypeWellKnownGroup, "Expected SidTypeWellKnownGroup, got %d\n", sid_use);
    else
        ok(sid_use == SidTypeUser, "Expected SidTypeUser (%d), got %d\n", SidTypeUser, sid_use);
#else
    ok(sid_use == SidTypeUser, "Expected SidTypeUser (%d), got %d\n", SidTypeUser, sid_use);
#endif
    domain_size = domain_save;
    sid_size = sid_save;

    if (PRIMARYLANGID(GetSystemDefaultLangID()) != LANG_ENGLISH)
    {
        skip("Non-English locale (test with hardcoded 'Everyone')\n");
    }
    else
    {
        ret = LookupAccountNameA(NULL, "Everyone", psid, &sid_size, domain, &domain_size, &sid_use);
        get_sid_info(psid, &account, &sid_dom);
        ok(ret, "Failed to lookup account name\n");
        ok(sid_size != 0, "sid_size was zero\n");
        ok(!lstrcmpA(account, "Everyone"), "Expected Everyone, got %s\n", account);
        ok(!lstrcmpiA(domain, sid_dom), "Expected %s, got %s\n", sid_dom, domain);
        ok(domain_size == 0, "Expected 0, got %ld\n", domain_size);
        ok(strlen(domain) == domain_size, "Expected %d, got %ld\n", lstrlenA(domain), domain_size);
        ok(sid_use == SidTypeWellKnownGroup, "Expected SidTypeWellKnownGroup (%d), got %d\n", SidTypeWellKnownGroup, sid_use);
        domain_size = domain_save;
    }

    /* NULL Sid with zero sid size */
    SetLastError(0xdeadbeef);
    sid_size = 0;
    ret = LookupAccountNameA(NULL, user_name, NULL, &sid_size, domain, &domain_size, &sid_use);
    ok(!ret, "Expected 0, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());
    ok(sid_size == sid_save, "Expected %ld, got %ld\n", sid_save, sid_size);
    ok(domain_size == domain_save, "Expected %ld, got %ld\n", domain_save, domain_size);

    /* try cchReferencedDomainName - 1 */
    SetLastError(0xdeadbeef);
    domain_size--;
    ret = LookupAccountNameA(NULL, user_name, NULL, &sid_size, domain, &domain_size, &sid_use);
    ok(!ret, "Expected 0, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());
    ok(sid_size == sid_save, "Expected %ld, got %ld\n", sid_save, sid_size);
    ok(domain_size == domain_save, "Expected %ld, got %ld\n", domain_save, domain_size);

    /* NULL ReferencedDomainName with zero domain name size */
    SetLastError(0xdeadbeef);
    domain_size = 0;
    ret = LookupAccountNameA(NULL, user_name, psid, &sid_size, NULL, &domain_size, &sid_use);
    ok(!ret, "Expected 0, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());
    ok(sid_size == sid_save, "Expected %ld, got %ld\n", sid_save, sid_size);
    ok(domain_size == domain_save, "Expected %ld, got %ld\n", domain_save, domain_size);

    free(psid);
    free(domain);

    /* get sizes for NULL account name */
    sid_size = 0;
    domain_size = 0;
    sid_use = 0xcafebabe;
    SetLastError(0xdeadbeef);
    ret = LookupAccountNameA(NULL, NULL, NULL, &sid_size, NULL, &domain_size, &sid_use);
    ok(!ret, "Expected 0, got %d\n", ret);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());
    ok(sid_size != 0, "Expected non-zero sid size\n");
    ok(domain_size != 0, "Expected non-zero domain size\n");
    ok(sid_use == (SID_NAME_USE)0xcafebabe, "Expected 0xcafebabe, got %d\n", sid_use);

    psid = malloc(sid_size);
    domain = malloc(domain_size);

    /* try NULL account name */
    ret = LookupAccountNameA(NULL, NULL, psid, &sid_size, domain, &domain_size, &sid_use);
    get_sid_info(psid, &account, &sid_dom);
    ok(ret, "Failed to lookup account name\n");
    /* Using a fixed string will not work on different locales */
    ok(!lstrcmpiA(account, domain),
       "Got %s for account and %s for domain, these should be the same\n", account, domain);
    ok(sid_use == SidTypeDomain, "Expected SidTypeDomain (%d), got %d\n", SidTypeDomain, sid_use);

    free(psid);
    free(domain);

    /* try an invalid account name */
    SetLastError(0xdeadbeef);
    sid_size = 0;
    domain_size = 0;
    ret = LookupAccountNameA(NULL, "oogabooga", NULL, &sid_size, NULL, &domain_size, &sid_use);
    ok(!ret, "Expected 0, got %d\n", ret);
    ok(GetLastError() == ERROR_NONE_MAPPED ||
       broken(GetLastError() == ERROR_TRUSTED_RELATIONSHIP_FAILURE),
       "Expected ERROR_NONE_MAPPED, got %ld\n", GetLastError());
    ok(sid_size == 0, "Expected 0, got %ld\n", sid_size);
    ok(domain_size == 0, "Expected 0, got %ld\n", domain_size);

    /* try an invalid system name */
    SetLastError(0xdeadbeef);
    sid_size = 0;
    domain_size = 0;
    ret = LookupAccountNameA("deepthought", NULL, NULL, &sid_size, NULL, &domain_size, &sid_use);
    ok(!ret, "Expected 0, got %d\n", ret);
    ok(GetLastError() == RPC_S_SERVER_UNAVAILABLE || GetLastError() == RPC_S_INVALID_NET_ADDR /* Vista */,
       "Expected RPC_S_SERVER_UNAVAILABLE or RPC_S_INVALID_NET_ADDR, got %ld\n", GetLastError());
    ok(sid_size == 0, "Expected 0, got %ld\n", sid_size);
    ok(domain_size == 0, "Expected 0, got %ld\n", domain_size);

    /* try with the computer name as the account name */
    domain_size = sizeof(computer_name);
    GetComputerNameA(computer_name, &domain_size);
    sid_size = 0;
    domain_size = 0;
    ret = LookupAccountNameA(NULL, computer_name, NULL, &sid_size, NULL, &domain_size, &sid_use);
    ok(!ret && (GetLastError() == ERROR_INSUFFICIENT_BUFFER ||
       GetLastError() == ERROR_NONE_MAPPED /* in a domain */ ||
       broken(GetLastError() == ERROR_TRUSTED_DOMAIN_FAILURE) ||
       broken(GetLastError() == ERROR_TRUSTED_RELATIONSHIP_FAILURE)),
       "LookupAccountNameA failed: %ld\n", GetLastError());
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        psid = malloc(sid_size);
        domain = malloc(domain_size);
        ret = LookupAccountNameA(NULL, computer_name, psid, &sid_size, domain, &domain_size, &sid_use);
        ok(ret, "LookupAccountNameA failed: %ld\n", GetLastError());
        ok(sid_use == SidTypeDomain ||
           (sid_use == SidTypeUser && ! strcmp(computer_name, user_name)), "expected SidTypeDomain for %s, got %d\n", computer_name, sid_use);
        free(domain);
        free(psid);
    }

    /* Well Known names */
    if (PRIMARYLANGID(GetSystemDefaultLangID()) != LANG_ENGLISH)
    {
        skip("Non-English locale (skipping well known name creation tests)\n");
        return;
    }

    check_wellknown_name("LocalService", WinLocalServiceSid);
    check_wellknown_name("Local Service", WinLocalServiceSid);
    /* 2 spaces */
    check_wellknown_name("Local  Service", 0);
    check_wellknown_name("NetworkService", WinNetworkServiceSid);
    check_wellknown_name("Network Service", WinNetworkServiceSid);

    /* example of some names where the spaces are not optional */
    check_wellknown_name("Terminal Server User", WinTerminalServerSid);
    check_wellknown_name("TerminalServer User", 0);
    check_wellknown_name("TerminalServerUser", 0);
    check_wellknown_name("Terminal ServerUser", 0);

    check_wellknown_name("enterprise domain controllers",WinEnterpriseControllersSid);
    check_wellknown_name("enterprisedomain controllers", 0);
    check_wellknown_name("enterprise domaincontrollers", 0);
    check_wellknown_name("enterprisedomaincontrollers", 0);

    /* case insensitivity */
    check_wellknown_name("lOCAlServICE", WinLocalServiceSid);

    /* fully qualified account names */
    check_wellknown_name("NT AUTHORITY\\LocalService", WinLocalServiceSid);
    check_wellknown_name("nt authority\\Network Service", WinNetworkServiceSid);
    check_wellknown_name("nt authority test\\Network Service", 0);
    check_wellknown_name("Dummy\\Network Service", 0);
    check_wellknown_name("ntauthority\\Network Service", 0);
}

static void test_security_descriptor(void)
{
    SECURITY_DESCRIPTOR sd, *sd_rel, *sd_rel2, *sd_abs;
    char buf[8192];
    DWORD size, size_dacl, size_sacl, size_owner, size_group;
    BOOL isDefault, isPresent, ret;
    PACL pacl, dacl, sacl;
    PSID psid, owner, group;

    SetLastError(0xdeadbeef);
    ret = InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    if (ret && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        win_skip("InitializeSecurityDescriptor is not implemented\n");
        return;
    }

    ok(GetSecurityDescriptorOwner(&sd, &psid, &isDefault), "GetSecurityDescriptorOwner failed\n");
    expect_eq(psid, NULL, PSID, "%p");
    expect_eq(isDefault, FALSE, BOOL, "%d");
    sd.Control |= SE_DACL_PRESENT | SE_SACL_PRESENT;

    SetLastError(0xdeadbeef);
    size = 5;
    expect_eq(MakeSelfRelativeSD(&sd, buf, &size), FALSE, BOOL, "%d");
    expect_eq(GetLastError(), (DWORD)ERROR_INSUFFICIENT_BUFFER, DWORD, "%lu");
    ok(size > 5, "Size not increased\n");
    if (size <= 8192)
    {
        expect_eq(MakeSelfRelativeSD(&sd, buf, &size), TRUE, BOOL, "%d");
        ok(GetSecurityDescriptorOwner(&sd, &psid, &isDefault), "GetSecurityDescriptorOwner failed\n");
        expect_eq(psid, NULL, PSID, "%p");
        expect_eq(isDefault, FALSE, BOOL, "%d");
        ok(GetSecurityDescriptorGroup(&sd, &psid, &isDefault), "GetSecurityDescriptorGroup failed\n");
        expect_eq(psid, NULL, PSID, "%p");
        expect_eq(isDefault, FALSE, BOOL, "%d");
        ok(GetSecurityDescriptorDacl(&sd, &isPresent, &pacl, &isDefault), "GetSecurityDescriptorDacl failed\n");
        expect_eq(isPresent, TRUE, BOOL, "%d");
        expect_eq(psid, NULL, PSID, "%p");
        expect_eq(isDefault, FALSE, BOOL, "%d");
        ok(GetSecurityDescriptorSacl(&sd, &isPresent, &pacl, &isDefault), "GetSecurityDescriptorSacl failed\n");
        expect_eq(isPresent, TRUE, BOOL, "%d");
        expect_eq(psid, NULL, PSID, "%p");
        expect_eq(isDefault, FALSE, BOOL, "%d");
    }

    ret = ConvertStringSecurityDescriptorToSecurityDescriptorA(
        "O:SYG:S-1-5-21-93476-23408-4576D:(A;NP;GAGXGWGR;;;SU)(A;IOID;CCDC;;;SU)"
        "(D;OICI;0xffffffff;;;S-1-5-21-93476-23408-4576)S:(AU;OICINPIOIDSAFA;CCDCLCSWRPRC;;;SU)"
        "(AU;NPSA;0x12019f;;;SU)", SDDL_REVISION_1, (void **)&sd_rel, NULL);
    ok(ret, "got %lu\n", GetLastError());

    size = 0;
    ret = MakeSelfRelativeSD(sd_rel, NULL, &size);
    todo_wine ok(!ret && GetLastError() == ERROR_BAD_DESCRIPTOR_FORMAT, "got %lu\n", GetLastError());

    /* convert to absolute form */
    size = size_dacl = size_sacl = size_owner = size_group = 0;
    ret = MakeAbsoluteSD(sd_rel, NULL, &size, NULL, &size_dacl, NULL, &size_sacl, NULL, &size_owner, NULL,
                         &size_group);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "got %lu\n", GetLastError());

    sd_abs = malloc(size + size_dacl + size_sacl + size_owner + size_group);
    dacl = (PACL)(sd_abs + 1);
    sacl = (PACL)((char *)dacl + size_dacl);
    owner = (PSID)((char *)sacl + size_sacl);
    group = (PSID)((char *)owner + size_owner);
    ret = MakeAbsoluteSD(sd_rel, sd_abs, &size, dacl, &size_dacl, sacl, &size_sacl, owner, &size_owner,
                         group, &size_group);
    ok(ret, "got %lu\n", GetLastError());

    size = 0;
    ret = MakeSelfRelativeSD(sd_abs, NULL, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "got %lu\n", GetLastError());
    ok(size == 184, "got %lu\n", size);

    size += 4;
    sd_rel2 = malloc(size);
    ret = MakeSelfRelativeSD(sd_abs, sd_rel2, &size);
    ok(ret, "got %lu\n", GetLastError());
    ok(size == 188, "got %lu\n", size);

    free(sd_abs);
    free(sd_rel2);
    LocalFree(sd_rel);
}

#define TEST_GRANTED_ACCESS(a,b) test_granted_access(a,b,0,__LINE__)
#define TEST_GRANTED_ACCESS2(a,b,c) test_granted_access(a,b,c,__LINE__)
static void test_granted_access(HANDLE handle, ACCESS_MASK access,
                                ACCESS_MASK alt, int line)
{
    OBJECT_BASIC_INFORMATION obj_info;
    NTSTATUS status;

    status = NtQueryObject( handle, ObjectBasicInformation, &obj_info,
                            sizeof(obj_info), NULL );
    ok_(__FILE__, line)(!status, "NtQueryObject with err: %08lx\n", status);
    if (alt)
        ok_(__FILE__, line)(obj_info.GrantedAccess == access ||
            obj_info.GrantedAccess == alt, "Granted access should be 0x%08lx "
            "or 0x%08lx, instead of 0x%08lx\n", access, alt, obj_info.GrantedAccess);
    else
        ok_(__FILE__, line)(obj_info.GrantedAccess == access, "Granted access should "
            "be 0x%08lx, instead of 0x%08lx\n", access, obj_info.GrantedAccess);
}

#define CHECK_SET_SECURITY(o,i,e) \
    do{ \
        BOOL res_; \
        DWORD err; \
        SetLastError( 0xdeadbeef ); \
        res_ = SetKernelObjectSecurity( o, i, SecurityDescriptor ); \
        err = GetLastError(); \
        if (e == ERROR_SUCCESS) \
            ok(res_, "SetKernelObjectSecurity failed with %ld\n", err); \
        else \
            ok(!res_ && err == e, "SetKernelObjectSecurity should have failed " \
               "with %s, instead of %ld\n", #e, err); \
    }while(0)

static void test_process_security(void)
{
    BOOL res;
    PTOKEN_USER user;
    PTOKEN_OWNER owner;
    PTOKEN_PRIMARY_GROUP group;
    PSID AdminSid = NULL, UsersSid = NULL, UserSid = NULL;
    PACL Acl = NULL, ThreadAcl = NULL;
    SECURITY_DESCRIPTOR *SecurityDescriptor = NULL, *ThreadSecurityDescriptor = NULL;
    char buffer[MAX_PATH], account[MAX_PATH], domain[MAX_PATH];
    PROCESS_INFORMATION info;
    STARTUPINFOA startup;
    SECURITY_ATTRIBUTES psa, tsa;
    HANDLE token, event;
    DWORD size, acc_size, dom_size, ret;
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = { SECURITY_WORLD_SID_AUTHORITY };
    PSID EveryoneSid = NULL;
    SID_NAME_USE use;

    Acl = malloc(256);
    res = InitializeAcl(Acl, 256, ACL_REVISION);
    if (!res && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        win_skip("ACLs not implemented - skipping tests\n");
        free(Acl);
        return;
    }
    ok(res, "InitializeAcl failed with error %ld\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &EveryoneSid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    /* get owner from the token we might be running as a user not admin */
    res = OpenProcessToken( GetCurrentProcess(), MAXIMUM_ALLOWED, &token );
    ok(res, "OpenProcessToken failed with error %ld\n", GetLastError());
    if (!res)
    {
        free(Acl);
        return;
    }

    res = GetTokenInformation( token, TokenOwner, NULL, 0, &size );
    ok(!res, "Expected failure, got %d\n", res);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());

    owner = malloc(size);
    res = GetTokenInformation( token, TokenOwner, owner, size, &size );
    ok(res, "GetTokenInformation failed with error %ld\n", GetLastError());
    AdminSid = owner->Owner;
    test_sid_str(AdminSid);

    res = GetTokenInformation( token, TokenPrimaryGroup, NULL, 0, &size );
    ok(!res, "Expected failure, got %d\n", res);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());

    group = malloc(size);
    res = GetTokenInformation( token, TokenPrimaryGroup, group, size, &size );
    ok(res, "GetTokenInformation failed with error %ld\n", GetLastError());
    UsersSid = group->PrimaryGroup;
    test_sid_str(UsersSid);

    acc_size = sizeof(account);
    dom_size = sizeof(domain);
    ret = LookupAccountSidA( NULL, UsersSid, account, &acc_size, domain, &dom_size, &use );
    ok(ret, "LookupAccountSid failed with %ld\n", ret);
#ifdef __REACTOS__
    if (IsWellKnownSid(UsersSid, WinLocalSystemSid))
        ok(use == SidTypeUser, "expect SidTypeUser, got %d\n", use);
    else
        ok(use == SidTypeGroup, "expect SidTypeGroup, got %d\n", use);
#else
    ok(use == SidTypeGroup, "expect SidTypeGroup, got %d\n", use);
#endif
    if (PRIMARYLANGID(GetSystemDefaultLangID()) != LANG_ENGLISH)
        skip("Non-English locale (test with hardcoded 'None')\n");
    else
#ifdef __REACTOS__
        ok(!strcmp(account, IsWellKnownSid(UsersSid, WinLocalSystemSid) ? "SYSTEM" : "None"),
           "unexpected primary group account %s\n", account);
#else
        ok(!strcmp(account, "None"), "expect None, got %s\n", account);
#endif

    res = GetTokenInformation( token, TokenUser, NULL, 0, &size );
    ok(!res, "Expected failure, got %d\n", res);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());

    user = malloc(size);
    res = GetTokenInformation( token, TokenUser, user, size, &size );
    ok(res, "GetTokenInformation failed with error %ld\n", GetLastError());
    UserSid = user->User.Sid;
    test_sid_str(UserSid);
    ok(EqualPrefixSid(UsersSid, UserSid), "TokenPrimaryGroup Sid and TokenUser Sid don't match.\n");

    CloseHandle( token );
    if (!res)
    {
        free(group);
        free(owner);
        free(user);
        free(Acl);
        return;
    }

    res = AddAccessDeniedAce(Acl, ACL_REVISION, PROCESS_VM_READ, AdminSid);
    ok(res, "AddAccessDeniedAce failed with error %ld\n", GetLastError());
    res = AddAccessAllowedAce(Acl, ACL_REVISION, PROCESS_ALL_ACCESS, AdminSid);
    ok(res, "AddAccessAllowedAce failed with error %ld\n", GetLastError());

    SecurityDescriptor = malloc(SECURITY_DESCRIPTOR_MIN_LENGTH);
    res = InitializeSecurityDescriptor(SecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    ok(res, "InitializeSecurityDescriptor failed with error %ld\n", GetLastError());

    event = CreateEventA( NULL, TRUE, TRUE, "test_event" );
    ok(event != NULL, "CreateEvent %ld\n", GetLastError());

    SecurityDescriptor->Revision = 0;
    CHECK_SET_SECURITY( event, OWNER_SECURITY_INFORMATION, ERROR_UNKNOWN_REVISION );
    SecurityDescriptor->Revision = SECURITY_DESCRIPTOR_REVISION;

    CHECK_SET_SECURITY( event, OWNER_SECURITY_INFORMATION, ERROR_INVALID_SECURITY_DESCR );
    CHECK_SET_SECURITY( event, GROUP_SECURITY_INFORMATION, ERROR_INVALID_SECURITY_DESCR );
    CHECK_SET_SECURITY( event, SACL_SECURITY_INFORMATION, ERROR_ACCESS_DENIED );
    CHECK_SET_SECURITY( event, DACL_SECURITY_INFORMATION, ERROR_SUCCESS );
    /* NULL DACL is valid and means that everyone has access */
    SecurityDescriptor->Control |= SE_DACL_PRESENT;
    CHECK_SET_SECURITY( event, DACL_SECURITY_INFORMATION, ERROR_SUCCESS );

    /* Set owner and group and dacl */
    res = SetSecurityDescriptorOwner(SecurityDescriptor, AdminSid, FALSE);
    ok(res, "SetSecurityDescriptorOwner failed with error %ld\n", GetLastError());
    CHECK_SET_SECURITY( event, OWNER_SECURITY_INFORMATION, ERROR_SUCCESS );
    test_owner_equal( event, AdminSid, __LINE__ );

    res = SetSecurityDescriptorGroup(SecurityDescriptor, EveryoneSid, FALSE);
    ok(res, "SetSecurityDescriptorGroup failed with error %ld\n", GetLastError());
    CHECK_SET_SECURITY( event, GROUP_SECURITY_INFORMATION, ERROR_SUCCESS );
    test_group_equal( event, EveryoneSid, __LINE__ );

    res = SetSecurityDescriptorDacl(SecurityDescriptor, TRUE, Acl, FALSE);
    ok(res, "SetSecurityDescriptorDacl failed with error %ld\n", GetLastError());
    CHECK_SET_SECURITY( event, DACL_SECURITY_INFORMATION, ERROR_SUCCESS );
    /* setting a dacl should not change the owner or group */
    test_owner_equal( event, AdminSid, __LINE__ );
    test_group_equal( event, EveryoneSid, __LINE__ );

    /* Test again with a different SID in case the previous SID also happens to
     * be the one that is incorrectly replacing the group. */
    res = SetSecurityDescriptorGroup(SecurityDescriptor, UsersSid, FALSE);
    ok(res, "SetSecurityDescriptorGroup failed with error %ld\n", GetLastError());
    CHECK_SET_SECURITY( event, GROUP_SECURITY_INFORMATION, ERROR_SUCCESS );
    test_group_equal( event, UsersSid, __LINE__ );

    res = SetSecurityDescriptorDacl(SecurityDescriptor, TRUE, Acl, FALSE);
    ok(res, "SetSecurityDescriptorDacl failed with error %ld\n", GetLastError());
    CHECK_SET_SECURITY( event, DACL_SECURITY_INFORMATION, ERROR_SUCCESS );
    test_group_equal( event, UsersSid, __LINE__ );

    sprintf(buffer, "%s security test", myARGV[0]);
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    psa.nLength = sizeof(psa);
    psa.lpSecurityDescriptor = SecurityDescriptor;
    psa.bInheritHandle = TRUE;

    ThreadSecurityDescriptor = malloc( SECURITY_DESCRIPTOR_MIN_LENGTH );
    res = InitializeSecurityDescriptor( ThreadSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION );
    ok(res, "InitializeSecurityDescriptor failed with error %ld\n", GetLastError());

    ThreadAcl = malloc( 256 );
    res = InitializeAcl( ThreadAcl, 256, ACL_REVISION );
    ok(res, "InitializeAcl failed with error %ld\n", GetLastError());
    res = AddAccessDeniedAce( ThreadAcl, ACL_REVISION, THREAD_SET_THREAD_TOKEN, AdminSid );
    ok(res, "AddAccessDeniedAce failed with error %ld\n", GetLastError() );
    res = AddAccessAllowedAce( ThreadAcl, ACL_REVISION, THREAD_ALL_ACCESS, AdminSid );
    ok(res, "AddAccessAllowedAce failed with error %ld\n", GetLastError());

    res = SetSecurityDescriptorOwner( ThreadSecurityDescriptor, AdminSid, FALSE );
    ok(res, "SetSecurityDescriptorOwner failed with error %ld\n", GetLastError());
    res = SetSecurityDescriptorGroup( ThreadSecurityDescriptor, UsersSid, FALSE );
    ok(res, "SetSecurityDescriptorGroup failed with error %ld\n", GetLastError());
    res = SetSecurityDescriptorDacl( ThreadSecurityDescriptor, TRUE, ThreadAcl, FALSE );
    ok(res, "SetSecurityDescriptorDacl failed with error %ld\n", GetLastError());

    tsa.nLength = sizeof(tsa);
    tsa.lpSecurityDescriptor = ThreadSecurityDescriptor;
    tsa.bInheritHandle = TRUE;

    /* Doesn't matter what ACL say we should get full access for ourselves */
    res = CreateProcessA( NULL, buffer, &psa, &tsa, FALSE, 0, NULL, NULL, &startup, &info );
    ok(res, "CreateProcess with err:%ld\n", GetLastError());
    TEST_GRANTED_ACCESS2( info.hProcess, PROCESS_ALL_ACCESS_NT4,
                          STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL );
    TEST_GRANTED_ACCESS2( info.hThread, THREAD_ALL_ACCESS_NT4,
                          STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL );
    wait_child_process( &info );

    FreeSid(EveryoneSid);
    CloseHandle( event );
    free(group);
    free(owner);
    free(user);
    free(Acl);
    free(SecurityDescriptor);
    free(ThreadAcl);
    free(ThreadSecurityDescriptor);
}

static void test_process_security_child(void)
{
#ifdef __REACTOS__
    TOKEN_PRIVILEGES privileges, previous;
    HANDLE handle, handle1, token;
#else
    HANDLE handle, handle1;
#endif
    BOOL ret;
#ifdef __REACTOS__
    DWORD err, size;

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &token);
    ok(ret, "OpenProcessToken failed with error %ld\n", GetLastError());
    if (!ret) return;
    ret = LookupPrivilegeValueA(NULL, "SeDebugPrivilege", &privileges.Privileges[0].Luid);
    ok(ret, "LookupPrivilegeValueA failed with error %ld\n", GetLastError());
    if (!ret)
    {
        CloseHandle(token);
        return;
    }
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = 0;
    previous.PrivilegeCount = 0;
    SetLastError(ERROR_SUCCESS);
    ret = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(previous), &previous, &size);
    err = GetLastError();
    ok(ret && (err == ERROR_SUCCESS || err == ERROR_NOT_ALL_ASSIGNED),
       "Disabling SeDebugPrivilege failed with error %ld\n", err);
    if (!ret || (err != ERROR_SUCCESS && err != ERROR_NOT_ALL_ASSIGNED))
    {
        CloseHandle(token);
        return;
    }
#else
    DWORD err;
#endif

    handle = OpenProcess( PROCESS_TERMINATE, FALSE, GetCurrentProcessId() );
    ok(handle != NULL, "OpenProcess(PROCESS_TERMINATE) with err:%ld\n", GetLastError());
    TEST_GRANTED_ACCESS( handle, PROCESS_TERMINATE );

    ret = DuplicateHandle( GetCurrentProcess(), handle, GetCurrentProcess(),
                           &handle1, 0, TRUE, DUPLICATE_SAME_ACCESS );
    ok(ret, "duplicating handle err:%ld\n", GetLastError());
    TEST_GRANTED_ACCESS( handle1, PROCESS_TERMINATE );

    CloseHandle( handle1 );

    SetLastError( 0xdeadbeef );
    ret = DuplicateHandle( GetCurrentProcess(), handle, GetCurrentProcess(),
                           &handle1, PROCESS_ALL_ACCESS, TRUE, 0 );
    err = GetLastError();
    ok(!ret && err == ERROR_ACCESS_DENIED, "duplicating handle should have failed "
       "with STATUS_ACCESS_DENIED, instead of err:%ld\n", err);

    CloseHandle( handle );

    /* These two should fail - they are denied by ACL */
    handle = OpenProcess( PROCESS_VM_READ, FALSE, GetCurrentProcessId() );
    ok(handle == NULL, "OpenProcess(PROCESS_VM_READ) should have failed\n");
#ifdef __REACTOS__
    if (handle) CloseHandle(handle);
#endif
    handle = OpenProcess( PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId() );
    ok(handle == NULL, "OpenProcess(PROCESS_ALL_ACCESS) should have failed\n");
#ifdef __REACTOS__
    if (handle) CloseHandle(handle);
#endif

    /* Documented privilege elevation */
    ret = DuplicateHandle( GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
                           &handle, 0, TRUE, DUPLICATE_SAME_ACCESS );
    ok(ret, "duplicating handle err:%ld\n", GetLastError());
    TEST_GRANTED_ACCESS2( handle, PROCESS_ALL_ACCESS_NT4,
                          STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL );

    CloseHandle( handle );

    /* Same only explicitly asking for all access rights */
    ret = DuplicateHandle( GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(),
                           &handle, PROCESS_ALL_ACCESS, TRUE, 0 );
    ok(ret, "duplicating handle err:%ld\n", GetLastError());
    TEST_GRANTED_ACCESS2( handle, PROCESS_ALL_ACCESS_NT4,
                          PROCESS_ALL_ACCESS | PROCESS_QUERY_LIMITED_INFORMATION );
    ret = DuplicateHandle( GetCurrentProcess(), handle, GetCurrentProcess(),
                           &handle1, PROCESS_VM_READ, TRUE, 0 );
    ok(ret, "duplicating handle err:%ld\n", GetLastError());
    TEST_GRANTED_ACCESS( handle1, PROCESS_VM_READ );
    CloseHandle( handle1 );
    CloseHandle( handle );

    /* Test thread security */
    handle = OpenThread( THREAD_TERMINATE, FALSE, GetCurrentThreadId() );
    ok(handle != NULL, "OpenThread(THREAD_TERMINATE) with err:%ld\n", GetLastError());
    TEST_GRANTED_ACCESS( handle, THREAD_TERMINATE );
    CloseHandle( handle );

    handle = OpenThread( THREAD_SET_THREAD_TOKEN, FALSE, GetCurrentThreadId() );
    ok(handle == NULL, "OpenThread(THREAD_SET_THREAD_TOKEN) should have failed\n");
#ifdef __REACTOS__
    if (handle) CloseHandle(handle);

    if (previous.PrivilegeCount)
    {
        SetLastError(ERROR_SUCCESS);
        ret = AdjustTokenPrivileges(token, FALSE, &previous, 0, NULL, NULL);
        err = GetLastError();
        ok(ret && err == ERROR_SUCCESS, "Restoring SeDebugPrivilege failed with error %ld\n", err);
    }
    CloseHandle(token);
#endif
}

static void test_impersonation_level(void)
{
    HANDLE Token, ProcessToken;
    HANDLE Token2;
    DWORD Size;
    TOKEN_PRIVILEGES *Privileges;
    TOKEN_USER *User;
    PRIVILEGE_SET *PrivilegeSet;
    BOOL AccessGranted;
    BOOL ret;
    HKEY hkey;
    DWORD error;

    SetLastError(0xdeadbeef);
    ret = ImpersonateSelf(SecurityAnonymous);
    if(!ret && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED))
    {
        win_skip("ImpersonateSelf is not implemented\n");
        return;
    }
    ok(ret, "ImpersonateSelf(SecurityAnonymous) failed with error %ld\n", GetLastError());
    ret = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY_SOURCE | TOKEN_IMPERSONATE | TOKEN_ADJUST_DEFAULT, TRUE, &Token);
    ok(!ret, "OpenThreadToken should have failed\n");
    error = GetLastError();
    ok(error == ERROR_CANT_OPEN_ANONYMOUS, "OpenThreadToken on anonymous token should have returned ERROR_CANT_OPEN_ANONYMOUS instead of %ld\n", error);
    /* can't perform access check when opening object against an anonymous impersonation token */
    todo_wine {
    error = RegOpenKeyExA(HKEY_CURRENT_USER, "Software", 0, KEY_READ, &hkey);
    ok(error == ERROR_INVALID_HANDLE || error == ERROR_CANT_OPEN_ANONYMOUS || error == ERROR_BAD_IMPERSONATION_LEVEL,
       "RegOpenKeyEx failed with %ld\n", error);
    }
    RevertToSelf();

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &ProcessToken);
    ok(ret, "OpenProcessToken failed with error %ld\n", GetLastError());

    ret = DuplicateTokenEx(ProcessToken,
        TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE, NULL,
        SecurityAnonymous, TokenImpersonation, &Token);
    ok(ret, "DuplicateTokenEx failed with error %ld\n", GetLastError());
    /* can't increase the impersonation level */
    ret = DuplicateToken(Token, SecurityIdentification, &Token2);
    error = GetLastError();
    ok(!ret && error == ERROR_BAD_IMPERSONATION_LEVEL,
        "Duplicating a token and increasing the impersonation level should have failed with ERROR_BAD_IMPERSONATION_LEVEL instead of %ld\n", error);
    /* we can query anything from an anonymous token, including the user */
    ret = GetTokenInformation(Token, TokenUser, NULL, 0, &Size);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER, "GetTokenInformation(TokenUser) should have failed with ERROR_INSUFFICIENT_BUFFER instead of %ld\n", error);
    User = malloc(Size);
    ret = GetTokenInformation(Token, TokenUser, User, Size, &Size);
    ok(ret, "GetTokenInformation(TokenUser) failed with error %ld\n", GetLastError());
    free(User);

    /* PrivilegeCheck fails with SecurityAnonymous level */
    ret = GetTokenInformation(Token, TokenPrivileges, NULL, 0, &Size);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER, "GetTokenInformation(TokenPrivileges) should have failed with ERROR_INSUFFICIENT_BUFFER instead of %ld\n", error);
    Privileges = malloc(Size);
    ret = GetTokenInformation(Token, TokenPrivileges, Privileges, Size, &Size);
    ok(ret, "GetTokenInformation(TokenPrivileges) failed with error %ld\n", GetLastError());

    PrivilegeSet = malloc(FIELD_OFFSET(PRIVILEGE_SET, Privilege[Privileges->PrivilegeCount]));
    PrivilegeSet->PrivilegeCount = Privileges->PrivilegeCount;
    memcpy(PrivilegeSet->Privilege, Privileges->Privileges, PrivilegeSet->PrivilegeCount * sizeof(PrivilegeSet->Privilege[0]));
    PrivilegeSet->Control = PRIVILEGE_SET_ALL_NECESSARY;
    free(Privileges);

    ret = PrivilegeCheck(Token, PrivilegeSet, &AccessGranted);
    error = GetLastError();
    ok(!ret && error == ERROR_BAD_IMPERSONATION_LEVEL, "PrivilegeCheck for SecurityAnonymous token should have failed with ERROR_BAD_IMPERSONATION_LEVEL instead of %ld\n", error);

    CloseHandle(Token);

    ret = ImpersonateSelf(SecurityIdentification);
    ok(ret, "ImpersonateSelf(SecurityIdentification) failed with error %ld\n", GetLastError());
    ret = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY_SOURCE | TOKEN_IMPERSONATE | TOKEN_ADJUST_DEFAULT, TRUE, &Token);
    ok(ret, "OpenThreadToken failed with error %ld\n", GetLastError());

    /* can't perform access check when opening object against an identification impersonation token */
    error = RegOpenKeyExA(HKEY_CURRENT_USER, "Software", 0, KEY_READ, &hkey);
    todo_wine {
    ok(error == ERROR_INVALID_HANDLE || error == ERROR_BAD_IMPERSONATION_LEVEL || error == ERROR_ACCESS_DENIED,
       "RegOpenKeyEx should have failed with ERROR_INVALID_HANDLE, ERROR_BAD_IMPERSONATION_LEVEL or ERROR_ACCESS_DENIED instead of %ld\n", error);
    }
    ret = PrivilegeCheck(Token, PrivilegeSet, &AccessGranted);
    ok(ret, "PrivilegeCheck for SecurityIdentification failed with error %ld\n", GetLastError());
    CloseHandle(Token);
    RevertToSelf();

    ret = ImpersonateSelf(SecurityImpersonation);
    ok(ret, "ImpersonateSelf(SecurityImpersonation) failed with error %ld\n", GetLastError());
    ret = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY_SOURCE | TOKEN_IMPERSONATE | TOKEN_ADJUST_DEFAULT, TRUE, &Token);
    ok(ret, "OpenThreadToken failed with error %ld\n", GetLastError());
    error = RegOpenKeyExA(HKEY_CURRENT_USER, "Software", 0, KEY_READ, &hkey);
    ok(error == ERROR_SUCCESS, "RegOpenKeyEx should have succeeded instead of failing with %ld\n", error);
    RegCloseKey(hkey);
    ret = PrivilegeCheck(Token, PrivilegeSet, &AccessGranted);
    ok(ret, "PrivilegeCheck for SecurityImpersonation failed with error %ld\n", GetLastError());
    RevertToSelf();

    CloseHandle(Token);
    CloseHandle(ProcessToken);

    free(PrivilegeSet);
}

#ifdef __REACTOS__
static void check_acl_constructor_ace(PACL acl, BYTE type, BYTE flags, DWORD mask, PSID sid)
#else
static void test_SetEntriesInAclW(void)
#endif
{
#ifdef __REACTOS__
    ACCESS_ALLOWED_ACE *ace;
    BOOL ret;
#else
    DWORD res;
    PSID EveryoneSid = NULL, UsersSid = NULL;
    PACL OldAcl = NULL, NewAcl;
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = { SECURITY_WORLD_SID_AUTHORITY };
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = { SECURITY_NT_AUTHORITY };
    EXPLICIT_ACCESSW ExplicitAccess;

    NewAcl = (PACL)0xdeadbeef;
    res = SetEntriesInAclW(0, NULL, NULL, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(NewAcl == NULL, "NewAcl=%p, expected NULL\n", NewAcl);
    LocalFree(NewAcl);

    OldAcl = malloc(256);
    res = InitializeAcl(OldAcl, 256, ACL_REVISION);
    if(!res && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        win_skip("ACLs not implemented - skipping tests\n");
        free(OldAcl);
        return;
    }
    ok(res, "InitializeAcl failed with error %ld\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &EveryoneSid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &UsersSid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());
#endif

#ifdef __REACTOS__
    ret = IsValidAcl(acl);
    ok(ret, "Constructor returned an invalid ACL.\n");
    if (!ret) return;
    ok(acl->AceCount == 1, "Expected one ACE, got %u.\n", acl->AceCount);
    if (acl->AceCount != 1) return;
    ret = GetAce(acl, 0, (void **)&ace);
    ok(ret, "GetAce failed: %lu.\n", GetLastError());
    if (!ret) return;
    ok(ace->Header.AceType == type, "ACE type %#x, expected %#x.\n", ace->Header.AceType, type);
    ok(ace->Header.AceFlags == flags, "ACE flags %#x, expected %#x.\n", ace->Header.AceFlags, flags);
    ok(ace->Header.AceSize == FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + GetLengthSid(sid),
       "Unexpected ACE size %u.\n", ace->Header.AceSize);
    if (ace->Header.AceType != type ||
        ace->Header.AceSize < FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + GetLengthSid(sid)) return;
    ok(ace->Mask == mask, "ACE mask %#lx, expected %#lx.\n", ace->Mask, mask);
    ok(EqualSid(&ace->SidStart, sid), "Constructor changed the trustee SID.\n");
}
#else
    res = AddAccessAllowedAce(OldAcl, ACL_REVISION, KEY_READ, UsersSid);
    ok(res, "AddAccessAllowedAce failed with error %ld\n", GetLastError());
#endif

#ifdef __REACTOS__
static void check_acl_audit_roundtrip(PACL acl, PSID sid)
{
    SYSTEM_AUDIT_ACE *ace;
    DWORD i, flags = 0;
    BOOL ret;
#else
    ExplicitAccess.grfAccessPermissions = KEY_WRITE;
    ExplicitAccess.grfAccessMode = GRANT_ACCESS;
    ExplicitAccess.grfInheritance = NO_INHERITANCE;
    ExplicitAccess.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ExplicitAccess.Trustee.ptstrName = EveryoneSid;
    ExplicitAccess.Trustee.MultipleTrusteeOperation = 0xDEADBEEF;
    ExplicitAccess.Trustee.pMultipleTrustee = (PVOID)0xDEADBEEF;
    res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(NewAcl != NULL, "returned acl was NULL\n");
    LocalFree(NewAcl);
#endif

#ifdef __REACTOS__
    ret = IsValidAcl(acl);
    ok(ret, "Audit round trip returned an invalid ACL.\n");
    if (!ret) return;
    for (i = 0; i < acl->AceCount; ++i)
    {
        ret = GetAce(acl, i, (void **)&ace);
        ok(ret, "GetAce(%lu) failed: %lu.\n", i, GetLastError());
        if (!ret) continue;
        ok(ace->Header.AceType == SYSTEM_AUDIT_ACE_TYPE, "Unexpected audit ACE type %#x.\n", ace->Header.AceType);
        if (ace->Header.AceType != SYSTEM_AUDIT_ACE_TYPE ||
            ace->Header.AceSize < FIELD_OFFSET(SYSTEM_AUDIT_ACE, SidStart) + GetLengthSid(sid)) continue;
        ok(ace->Mask == FILE_READ_DATA, "Unexpected audit mask %#lx.\n", ace->Mask);
        ok(EqualSid(&ace->SidStart, sid), "Audit round trip changed trustee.\n");
        ok((ace->Header.AceFlags & OBJECT_INHERIT_ACE) != 0, "Audit round trip dropped inheritance.\n");
        if (ace->Mask == FILE_READ_DATA && EqualSid(&ace->SidStart, sid))
            flags |= ace->Header.AceFlags & (SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG);
    }
    ok(flags == (SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG),
       "Audit round trip retained flags %#lx.\n", flags);
}
#else
    ExplicitAccess.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    ExplicitAccess.Trustee.pMultipleTrustee = NULL;
    ExplicitAccess.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(NewAcl != NULL, "returned acl was NULL\n");
    LocalFree(NewAcl);
#endif

#ifdef __REACTOS__
static void test_acl_constructor_output(void)
{
    static const struct
#else
    if (PRIMARYLANGID(GetSystemDefaultLangID()) != LANG_ENGLISH)
#endif
    {
#ifdef __REACTOS__
        ACCESS_MODE mode;
        BYTE type, audit_flags;
    } cases[] =
#else
        skip("Non-English locale (test with hardcoded 'Everyone')\n");
    }
    else
#endif
    {
#ifdef __REACTOS__
        {GRANT_ACCESS, ACCESS_ALLOWED_ACE_TYPE, 0},
        {SET_ACCESS, ACCESS_ALLOWED_ACE_TYPE, 0},
        {DENY_ACCESS, ACCESS_DENIED_ACE_TYPE, 0},
        {SET_AUDIT_SUCCESS, SYSTEM_AUDIT_ACE_TYPE, SUCCESSFUL_ACCESS_ACE_FLAG},
        {SET_AUDIT_FAILURE, SYSTEM_AUDIT_ACE_TYPE, FAILED_ACCESS_ACE_FLAG}
    };
    static const BYTE inheritance[] =
    {
        0,
        OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
        OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE | NO_PROPAGATE_INHERIT_ACE
    };
    static const GUID object_guid = {0xbf967a86, 0x0de6, 0x11d0, {0xa2,0x85,0x00,0xaa,0x00,0x30,0x49,0xe2}};
    static const GUID inherited_guid = {0xbf967aba, 0x0de6, 0x11d0, {0xa2,0x85,0x00,0xaa,0x00,0x30,0x49,0xe2}};
    SID everyone = {SID_REVISION, 1, {SECURITY_WORLD_SID_AUTHORITY}, {SECURITY_WORLD_RID}};
    union { DWORD align; BYTE bytes[128]; } audit_buffer;
    EXPLICIT_ACCESSW entry, *entries_w = NULL;
    EXPLICIT_ACCESSA entry_a, *entries_a = NULL;
    ACCESS_ALLOWED_OBJECT_ACE *object_ace;
    OBJECTS_AND_SID objects;
    PACL acl = NULL, audit = (PACL)audit_buffer.bytes;
    DWORD i, j, res, count;
    BOOL ret;
#else
        ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
        ExplicitAccess.Trustee.ptstrName = (WCHAR *)L"Everyone";
        res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
        ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
        ok(NewAcl != NULL, "returned acl was NULL\n");
        LocalFree(NewAcl);

        ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_BAD_FORM;
        res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
        ok(res == ERROR_INVALID_PARAMETER,
            "SetEntriesInAclW failed: %lu\n", res);
        ok(NewAcl == NULL,
            "returned acl wasn't NULL: %p\n", NewAcl);

        ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
        ExplicitAccess.Trustee.MultipleTrusteeOperation = TRUSTEE_IS_IMPERSONATE;
        res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
        ok(res == ERROR_INVALID_PARAMETER,
            "SetEntriesInAclW failed: %lu\n", res);
        ok(NewAcl == NULL,
            "returned acl wasn't NULL: %p\n", NewAcl);
#endif

#ifdef __REACTOS__
    for (i = 0; i < ARRAY_SIZE(cases); ++i)
    {
        for (j = 0; j < ARRAY_SIZE(inheritance); ++j)
        {
            winetest_push_context("constructor mode %u inheritance %#x", cases[i].mode, inheritance[j]);
            memset(&entry, 0, sizeof(entry));
            entry.grfAccessPermissions = FILE_READ_DATA;
            entry.grfAccessMode = cases[i].mode;
            entry.grfInheritance = inheritance[j];
            BuildTrusteeWithSidW(&entry.Trustee, &everyone);
            res = SetEntriesInAclW(1, &entry, NULL, &acl);
            ok(res == ERROR_SUCCESS && acl, "SetEntriesInAclW returned %lu, ACL %p.\n", res, acl);
            if (!res && acl)
                check_acl_constructor_ace(acl, cases[i].type, inheritance[j] | cases[i].audit_flags,
                                          FILE_READ_DATA, &everyone);
            LocalFree(acl);
            acl = NULL;

            memset(&entry_a, 0, sizeof(entry_a));
            entry_a.grfAccessPermissions = entry.grfAccessPermissions;
            entry_a.grfAccessMode = entry.grfAccessMode;
            entry_a.grfInheritance = entry.grfInheritance;
            BuildTrusteeWithSidA(&entry_a.Trustee, &everyone);
            res = SetEntriesInAclA(1, &entry_a, NULL, &acl);
            ok(res == ERROR_SUCCESS && acl, "SetEntriesInAclA returned %lu, ACL %p.\n", res, acl);
            if (!res && acl)
                check_acl_constructor_ace(acl, cases[i].type, inheritance[j] | cases[i].audit_flags,
                                          FILE_READ_DATA, &everyone);
            LocalFree(acl);
            acl = NULL;
            winetest_pop_context();
        }
#else
        ExplicitAccess.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
        ExplicitAccess.grfAccessMode = SET_ACCESS;
        res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
        ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
        ok(NewAcl != NULL, "returned acl was NULL\n");
        LocalFree(NewAcl);
#endif
    }

#ifdef __REACTOS__
    memset(&objects, 0, sizeof(objects));
    objects.ObjectsPresent = ACE_OBJECT_TYPE_PRESENT | ACE_INHERITED_OBJECT_TYPE_PRESENT;
    objects.ObjectTypeGuid = object_guid;
    objects.InheritedObjectTypeGuid = inherited_guid;
    objects.pSid = &everyone;
    memset(&entry, 0, sizeof(entry));
    entry.grfAccessPermissions = FILE_READ_DATA;
    entry.grfAccessMode = GRANT_ACCESS;
    entry.grfInheritance = CONTAINER_INHERIT_ACE;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_OBJECTS_AND_SID;
    entry.Trustee.ptstrName = (WCHAR *)&objects;
    res = SetEntriesInAclW(1, &entry, NULL, &acl);
    ok(res == ERROR_SUCCESS && acl, "Object ACE construction returned %lu, ACL %p.\n", res, acl);
    if (!res && acl && IsValidAcl(acl))
    {
        ok(acl->AclRevision == ACL_REVISION_DS, "Object ACL revision is %u.\n", acl->AclRevision);
        ok(acl->AceCount == 1, "Object ACL contains %u ACEs.\n", acl->AceCount);
        ret = GetAce(acl, 0, (void **)&object_ace);
        ok(ret, "GetAce failed: %lu.\n", GetLastError());
        if (ret && object_ace->Header.AceSize >= FIELD_OFFSET(ACCESS_ALLOWED_OBJECT_ACE, SidStart) + GetLengthSid(&everyone))
        {
            ok(object_ace->Header.AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE,
               "Object ACE type is %#x.\n", object_ace->Header.AceType);
            ok(object_ace->Header.AceFlags == CONTAINER_INHERIT_ACE, "Object ACE flags are %#x.\n", object_ace->Header.AceFlags);
            ok(object_ace->Mask == FILE_READ_DATA, "Object ACE mask is %#lx.\n", object_ace->Mask);
            ok(object_ace->Flags == objects.ObjectsPresent, "Object presence flags are %#lx.\n", object_ace->Flags);
            ok(!memcmp(&object_ace->ObjectType, &object_guid, sizeof(GUID)), "Object GUID changed.\n");
            ok(!memcmp(&object_ace->InheritedObjectType, &inherited_guid, sizeof(GUID)), "Inherited object GUID changed.\n");
            ok(EqualSid(&object_ace->SidStart, &everyone), "Object ACE trustee changed.\n");
        }
        else if (ret)
            ok(0, "Object ACE is too short: %u.\n", object_ace->Header.AceSize);
    }
    else if (!res && acl)
        ok(0, "Object ACL is invalid.\n");
    LocalFree(acl);
    acl = NULL;
#else
    ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ExplicitAccess.Trustee.ptstrName = (WCHAR *)L"CURRENT_USER";
    res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(NewAcl != NULL, "returned acl was NULL\n");
    LocalFree(NewAcl);

    ExplicitAccess.grfAccessMode = REVOKE_ACCESS;
    ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ExplicitAccess.Trustee.ptstrName = UsersSid;
    res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(NewAcl != NULL, "returned acl was NULL\n");
    LocalFree(NewAcl);
#endif

#ifdef __REACTOS__
    ret = InitializeAcl(audit, sizeof(audit_buffer), ACL_REVISION);
    ok(ret, "InitializeAcl failed: %lu.\n", GetLastError());
    if (!ret) return;
    ret = AddAuditAccessAceEx(audit, ACL_REVISION, OBJECT_INHERIT_ACE, FILE_READ_DATA, &everyone, TRUE, TRUE);
    ok(ret, "AddAuditAccessAceEx failed: %lu.\n", GetLastError());
    if (!ret) return;
    count = 0;
    res = GetExplicitEntriesFromAclW(audit, &count, &entries_w);
    ok(res == ERROR_SUCCESS && count && entries_w, "Audit extraction W returned %lu, count %lu.\n", res, count);
    if (!res && count && entries_w)
    {
        trace("Audit extraction W: count %lu, first mode %#lx, second mode %#lx.\n",
              count, (DWORD)entries_w[0].grfAccessMode, count > 1 ? (DWORD)entries_w[1].grfAccessMode : 0);
        ok(count == 2, "Dual audit extraction W returned %lu entries.\n", count);
        if (count == 2)
            ok(entries_w[0].grfAccessMode == SET_AUDIT_SUCCESS && entries_w[1].grfAccessMode == SET_AUDIT_FAILURE,
               "Dual audit extraction W modes are %#lx, %#lx.\n",
               (DWORD)entries_w[0].grfAccessMode, (DWORD)entries_w[1].grfAccessMode);
        res = SetEntriesInAclW(count, entries_w, NULL, &acl);
        ok(res == ERROR_SUCCESS && acl, "Audit reconstruction W returned %lu, ACL %p.\n", res, acl);
        if (!res && acl) check_acl_audit_roundtrip(acl, &everyone);
    }
    LocalFree(entries_w);
    LocalFree(acl);
    acl = NULL;
    count = 0;
    res = GetExplicitEntriesFromAclA(audit, &count, &entries_a);
    ok(res == ERROR_SUCCESS && count && entries_a, "Audit extraction A returned %lu, count %lu.\n", res, count);
    if (!res && count && entries_a)
    {
        trace("Audit extraction A: count %lu, first mode %#lx, second mode %#lx.\n",
              count, (DWORD)entries_a[0].grfAccessMode, count > 1 ? (DWORD)entries_a[1].grfAccessMode : 0);
        ok(count == 2, "Dual audit extraction A returned %lu entries.\n", count);
        if (count == 2)
            ok(entries_a[0].grfAccessMode == SET_AUDIT_SUCCESS && entries_a[1].grfAccessMode == SET_AUDIT_FAILURE,
               "Dual audit extraction A modes are %#lx, %#lx.\n",
               (DWORD)entries_a[0].grfAccessMode, (DWORD)entries_a[1].grfAccessMode);
        res = SetEntriesInAclA(count, entries_a, NULL, &acl);
        ok(res == ERROR_SUCCESS && acl, "Audit reconstruction A returned %lu, ACL %p.\n", res, acl);
        if (!res && acl) check_acl_audit_roundtrip(acl, &everyone);
    }
    LocalFree(entries_a);
    LocalFree(acl);

    ret = InitializeAcl(audit, sizeof(audit_buffer), ACL_REVISION_DS);
    ok(ret, "InitializeAcl(object audit) failed: %lu.\n", GetLastError());
    if (!ret) return;
    ret = AddAuditAccessObjectAce(audit, ACL_REVISION_DS, OBJECT_INHERIT_ACE, FILE_READ_DATA,
                                 &objects.ObjectTypeGuid, &objects.InheritedObjectTypeGuid, &everyone, TRUE, TRUE);
    ok(ret, "AddAuditAccessObjectAce failed: %lu.\n", GetLastError());
    if (!ret) return;
    count = 0;
    entries_w = NULL;
    res = GetExplicitEntriesFromAclW(audit, &count, &entries_w);
    trace("Object audit extraction W: error %lu, count %lu.\n", res, count);
    ok(res == ERROR_SUCCESS && count == 2 && entries_w,
       "Object audit extraction W returned %lu, count %lu.\n", res, count);
    if (!res && count && entries_w)
    {
        trace("Object audit extraction W: first mode %#lx, second mode %#lx, trustee form %u.\n",
              (DWORD)entries_w[0].grfAccessMode, count > 1 ? (DWORD)entries_w[1].grfAccessMode : 0,
              entries_w[0].Trustee.TrusteeForm);
        if (count == 2)
        {
            ok(entries_w[0].grfAccessMode == SET_AUDIT_SUCCESS && entries_w[1].grfAccessMode == SET_AUDIT_FAILURE,
               "Object audit extraction W modes are %#lx, %#lx.\n",
               (DWORD)entries_w[0].grfAccessMode, (DWORD)entries_w[1].grfAccessMode);
            ok(entries_w[0].Trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_SID &&
               entries_w[1].Trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_SID,
               "Object audit extraction W forms are %u, %u.\n",
               entries_w[0].Trustee.TrusteeForm, entries_w[1].Trustee.TrusteeForm);
        }
    }
    LocalFree(entries_w);
    count = 0;
    entries_a = NULL;
    res = GetExplicitEntriesFromAclA(audit, &count, &entries_a);
    trace("Object audit extraction A: error %lu, count %lu.\n", res, count);
    ok(res == ERROR_SUCCESS && count == 2 && entries_a,
       "Object audit extraction A returned %lu, count %lu.\n", res, count);
    if (!res && count && entries_a)
    {
        trace("Object audit extraction A: first mode %#lx, second mode %#lx, trustee form %u.\n",
              (DWORD)entries_a[0].grfAccessMode, count > 1 ? (DWORD)entries_a[1].grfAccessMode : 0,
              entries_a[0].Trustee.TrusteeForm);
        if (count == 2)
        {
            ok(entries_a[0].grfAccessMode == SET_AUDIT_SUCCESS && entries_a[1].grfAccessMode == SET_AUDIT_FAILURE,
               "Object audit extraction A modes are %#lx, %#lx.\n",
               (DWORD)entries_a[0].grfAccessMode, (DWORD)entries_a[1].grfAccessMode);
            ok(entries_a[0].Trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_SID &&
               entries_a[1].Trustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_SID,
               "Object audit extraction A forms are %u, %u.\n",
               entries_a[0].Trustee.TrusteeForm, entries_a[1].Trustee.TrusteeForm);
        }
    }
    LocalFree(entries_a);
#else
    FreeSid(UsersSid);
    FreeSid(EveryoneSid);
    free(OldAcl);
#endif
}

#ifdef __REACTOS__
static BOOL check_acl_file_access(PSECURITY_DESCRIPTOR sd, HANDLE token, DWORD desired, BOOL expected)
#else
static void test_SetEntriesInAclA(void)
#endif
{
#ifdef __REACTOS__
    GENERIC_MAPPING mapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    PRIVILEGE_SET privileges;
    DWORD size = sizeof(privileges), granted = 0xdeadbeef;
    BOOL ret, access = !expected;

    MapGenericMask(&desired, &mapping);
    ret = AccessCheck(sd, token, desired, &mapping, &privileges, &size, &granted, &access);
    ok(ret, "AccessCheck(%#lx) failed: %lu.\n", desired, GetLastError());
    if (!ret) return FALSE;
    ok(access == expected, "AccessCheck(%#lx) returned %d, expected %d.\n", desired, access, expected);
    ok(granted == (expected ? desired : 0), "AccessCheck(%#lx) granted %#lx.\n", desired, granted);
    ok(!privileges.PrivilegeCount, "Data access used %lu privileges.\n", privileges.PrivilegeCount);
    return TRUE;
}
#else
    DWORD res;
    PSID EveryoneSid = NULL, UsersSid = NULL;
    PACL OldAcl = NULL, NewAcl;
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = { SECURITY_WORLD_SID_AUTHORITY };
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = { SECURITY_NT_AUTHORITY };
    EXPLICIT_ACCESSA ExplicitAccess;

    NewAcl = (PACL)0xdeadbeef;
    res = SetEntriesInAclA(0, NULL, NULL, &NewAcl);
    if(res == ERROR_CALL_NOT_IMPLEMENTED)
    {
        win_skip("SetEntriesInAclA is not implemented\n");
        return;
    }
    ok(res == ERROR_SUCCESS, "SetEntriesInAclA failed: %lu\n", res);
    ok(NewAcl == NULL,
        "NewAcl=%p, expected NULL\n", NewAcl);
    LocalFree(NewAcl);
#endif

#ifdef __REACTOS__
static BOOL check_acl_file_enforcement(HANDLE file, const char *path, HANDLE token,
                                      HANDLE previous_token, PSID sid, BOOL deny_present, BOOL denied, BOOL protected, BOOL explicit_allow)
{
    PSECURITY_DESCRIPTOR sd = NULL;
    SECURITY_DESCRIPTOR_CONTROL control;
    ACCESS_ALLOWED_ACE *ace;
    HANDLE reopened;
    PACL dacl = NULL;
    DWORD res, revision, i, inherited_deny = 0, error, explicit_write = 0;
    BOOL ret, restored, inherited_seen = FALSE;

    res = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                          DACL_SECURITY_INFORMATION, NULL, NULL, &dacl, NULL, &sd);
    ok(res == ERROR_SUCCESS && sd && dacl, "GetSecurityInfo returned %lu, SD %p, DACL %p.\n", res, sd, dacl);
    if (res || !sd || !dacl) goto done;
    ret = GetSecurityDescriptorControl(sd, &control, &revision);
    ok(ret, "GetSecurityDescriptorControl failed: %lu.\n", GetLastError());
    if (ret) ok(!!(control & SE_DACL_PROTECTED) == protected, "Unexpected DACL protection %#x.\n", control);
    ret = IsValidAcl(dacl);
    ok(ret, "File DACL is invalid.\n");
    if (!ret) goto done;
    for (i = 0; i < dacl->AceCount; ++i)
    {
        ret = GetAce(dacl, i, (void **)&ace);
        ok(ret, "GetAce(%lu) failed: %lu.\n", i, GetLastError());
        if (!ret) continue;
        if (ace->Header.AceFlags & INHERITED_ACE) inherited_seen = TRUE;
        else ok(!inherited_seen, "Explicit ACE %lu follows an inherited ACE.\n", i);
        if ((ace->Header.AceType != ACCESS_DENIED_ACE_TYPE && ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE) ||
            ace->Header.AceSize < FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + GetLengthSid(sid)) continue;
        if (ace->Mask == FILE_WRITE_DATA && EqualSid(&ace->SidStart, sid))
        {
            if (ace->Header.AceType == ACCESS_DENIED_ACE_TYPE)
            {
                ++inherited_deny;
                ok(ace->Header.AceFlags == INHERITED_ACE, "File deny flags are %#x.\n", ace->Header.AceFlags);
            }
            else if (!ace->Header.AceFlags) ++explicit_write;
        }
#else
    OldAcl = malloc(256);
    res = InitializeAcl(OldAcl, 256, ACL_REVISION);
    if(!res && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        win_skip("ACLs not implemented - skipping tests\n");
        free(OldAcl);
        return;
#endif
    }
#ifdef __REACTOS__
    ok(inherited_deny == !!deny_present, "File contains %lu inherited denies, expected %u.\n", inherited_deny, !!deny_present);
    ok(explicit_write == !!explicit_allow, "Explicit write allow count is %lu, expected %u.\n", explicit_write, !!explicit_allow);
    check_acl_file_access(sd, token, GENERIC_READ, TRUE);
    check_acl_file_access(sd, token, FILE_WRITE_DATA, !denied);
    check_acl_file_access(sd, token, FILE_READ_DATA | FILE_WRITE_DATA, !denied);
#else
    ok(res, "InitializeAcl failed with error %ld\n", GetLastError());
#endif

#ifdef __REACTOS__
    ret = SetThreadToken(NULL, token);
    ok(ret, "SetThreadToken failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    reopened = CreateFileA(path, FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    error = GetLastError();
    restored = SetThreadToken(NULL, previous_token);
    ok(restored, "Thread token restoration failed: %lu.\n", GetLastError());
    ok(reopened != INVALID_HANDLE_VALUE, "Read reopen failed: %lu.\n", error);
    if (reopened != INVALID_HANDLE_VALUE) CloseHandle(reopened);
    if (!restored) { LocalFree(sd); return FALSE; }
#else
    res = AllocateAndInitializeSid( &SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &EveryoneSid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());
#endif

#ifdef __REACTOS__
    ret = SetThreadToken(NULL, token);
    ok(ret, "SetThreadToken failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    reopened = CreateFileA(path, FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    error = GetLastError();
    restored = SetThreadToken(NULL, previous_token);
    ok(restored, "Thread token restoration failed: %lu.\n", GetLastError());
    if (denied)
        ok(reopened == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED,
           "Write reopen returned %p, error %lu; expected access denied.\n", reopened, error);
    else
        ok(reopened != INVALID_HANDLE_VALUE, "Write reopen failed: %lu.\n", error);
    if (reopened != INVALID_HANDLE_VALUE) CloseHandle(reopened);
    if (!restored) { LocalFree(sd); return FALSE; }
#else
    res = AllocateAndInitializeSid( &SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &UsersSid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());
#endif

#ifdef __REACTOS__
done:
    LocalFree(sd);
    return TRUE;
}

static void check_acl_public_access_checks(HANDLE token)
{
    BOOL (WINAPI *check_by_type)(PSECURITY_DESCRIPTOR, PSID, HANDLE, DWORD, POBJECT_TYPE_LIST, DWORD,
                                 PGENERIC_MAPPING, PPRIVILEGE_SET, LPDWORD, LPDWORD, LPBOOL);
    BOOL (WINAPI *check_result_list)(PSECURITY_DESCRIPTOR, PSID, HANDLE, DWORD, POBJECT_TYPE_LIST, DWORD,
                                     PGENERIC_MAPPING, PPRIVILEGE_SET, LPDWORD, LPDWORD, LPDWORD);
    GUID object = {0xbf967aba, 0x0de6, 0x11d0, {0xa2,0x85,0x00,0xaa,0x00,0x30,0x49,0xe2}};
    GUID personal = {0x77b5b886, 0x944a, 0x11d1, {0xae,0xbd,0x00,0x00,0xf8,0x03,0x67,0xc1}};
    GUID general = {0x59ba2f42, 0x79a2, 0x11d0, {0x90,0x20,0x00,0xc0,0x4f,0xc2,0xd3,0xcf}};
    OBJECT_TYPE_LIST objects[] = {{ACCESS_OBJECT_GUID, 0, &object}, {ACCESS_PROPERTY_SET_GUID, 0, &personal},
                                  {ACCESS_PROPERTY_SET_GUID, 0, &general}};
    GENERIC_MAPPING mapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    GENERIC_MAPPING ds_mapping = {READ_CONTROL | ADS_RIGHT_DS_READ_PROP, READ_CONTROL | ADS_RIGHT_DS_WRITE_PROP,
                                  READ_CONTROL, STANDARD_RIGHTS_ALL | ADS_RIGHT_DS_READ_PROP | ADS_RIGHT_DS_WRITE_PROP};
    SID everyone = {SID_REVISION, 1, {SECURITY_WORLD_SID_AUTHORITY}, {SECURITY_WORLD_RID}};
    SECURITY_DESCRIPTOR sd;
    ACL empty;
    union { DWORD align; BYTE bytes[256]; } mixed_buffer;
    PACL mixed = (PACL)mixed_buffer.bytes;
    PRIVILEGE_SET privileges;
    DWORD grants[ARRAY_SIZE(objects)], statuses[ARRAY_SIZE(objects)], size, i, j, error, granted, scalar_granted;
    BOOL ret, access, scalar_access, scalar_ret;

    check_by_type = (void *)GetProcAddress(hmod, "AccessCheckByType");
    check_result_list = (void *)GetProcAddress(hmod, "AccessCheckByTypeResultList");
    ok(!!check_by_type, "AccessCheckByType export is missing.\n");
    ok(!!check_result_list, "AccessCheckByTypeResultList export is missing.\n");
    if (!check_by_type || !check_result_list) return;
    ret = InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorOwner(&sd, &everyone, FALSE) &&
          SetSecurityDescriptorGroup(&sd, &everyone, FALSE) &&
          InitializeAcl(&empty, sizeof(empty), ACL_REVISION);
    ok(ret, "Public access descriptor initialization failed: %lu.\n", GetLastError());
    if (!ret) return;
    for (i = 0; i < 3; ++i)
    {
        winetest_push_context("public %s DACL", i == 0 ? "NULL" : i == 1 ? "absent" : "empty");
        ret = SetSecurityDescriptorDacl(&sd, i != 1, i == 2 ? &empty : NULL, FALSE);
        ok(ret, "SetSecurityDescriptorDacl failed: %lu.\n", GetLastError());
        if (!ret) { winetest_pop_context(); return; }
        granted = 0xdeadbeef;
        access = i == 2;
        memset(&privileges, 0xcc, sizeof(privileges));
        size = sizeof(privileges);
        SetLastError(0xdeadbeef);
        ret = check_by_type(&sd, NULL, token, FILE_READ_DATA, objects, 2, &mapping,
                            &privileges, &size, &granted, &access);
        error = GetLastError();
        ok(ret, "AccessCheckByType failed: %lu.\n", error);
        if (ret)
        {
            ok(!!access == (i != 2), "Scalar access is %d, expected %d.\n", access, i != 2);
            ok(granted == (i == 2 ? 0 : FILE_READ_DATA), "Scalar grant is %#lx.\n", granted);
            ok(!privileges.PrivilegeCount, "Scalar data access used %lu privileges.\n", privileges.PrivilegeCount);
        }
        for (j = 0; j < ARRAY_SIZE(objects); ++j)
        {
            grants[j] = 0xdeadbeef;
            statuses[j] = 0xdeadbeef;
        }
        memset(&privileges, 0xcc, sizeof(privileges));
        size = sizeof(privileges);
        SetLastError(0xdeadbeef);
        ret = check_result_list(&sd, NULL, token, FILE_READ_DATA, objects, 2, &mapping,
                                &privileges, &size, grants, statuses);
        error = GetLastError();
        ok(grants[2] == 0xdeadbeef && statuses[2] == 0xdeadbeef, "Two-entry list API overwrote the guard entry.\n");
        ok(ret, "AccessCheckByTypeResultList failed: %lu.\n", error);
        if (ret)
        {
            for (j = 0; j < 2; ++j)
            {
                ok(grants[j] == (i == 2 ? 0 : FILE_READ_DATA), "List entry %lu grant is %#lx.\n", j, grants[j]);
                ok(statuses[j] == (i == 2 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS),
                   "List entry %lu Win32 status is %#lx.\n", j, statuses[j]);
            }
            ok(!privileges.PrivilegeCount, "List data access used %lu privileges.\n", privileges.PrivilegeCount);
        }
        scalar_granted = 0xdeadbeef;
        scalar_access = i == 2;
        memset(&privileges, 0xcc, sizeof(privileges));
        size = sizeof(privileges);
        scalar_ret = AccessCheck(&sd, token, FILE_READ_DATA, &mapping, &privileges, &size, &scalar_granted, &scalar_access);
        error = GetLastError();
        ok(scalar_ret, "AccessCheck failed: %lu.\n", error);
        granted = 0xdeadbeef;
        access = i == 2;
        memset(&privileges, 0xcc, sizeof(privileges));
        size = sizeof(privileges);
        ret = check_by_type(&sd, NULL, token, FILE_READ_DATA, NULL, 0, &mapping,
                            &privileges, &size, &granted, &access);
        error = GetLastError();
        ok(!!ret == !!scalar_ret, "No-list return %d differs from AccessCheck %d, error %lu.\n", ret, scalar_ret, error);
        if (ret && scalar_ret)
        {
            ok(!!access == !!scalar_access && !!access == (i != 2), "No-list access %d, AccessCheck %d.\n", access, scalar_access);
            ok(granted == scalar_granted && granted == (i == 2 ? 0 : FILE_READ_DATA),
               "No-list grant %#lx, AccessCheck %#lx.\n", granted, scalar_granted);
            ok(!privileges.PrivilegeCount, "No-list data access used %lu privileges.\n", privileges.PrivilegeCount);
        }
        winetest_pop_context();
    }

    for (i = 0; i < 3; ++i)
    {
        winetest_push_context("public missing %s", i == 0 ? "owner" : i == 1 ? "group" : "owner and group");
        ret = SetSecurityDescriptorOwner(&sd, i == 1 ? &everyone : NULL, FALSE) &&
              SetSecurityDescriptorGroup(&sd, i == 0 ? &everyone : NULL, FALSE);
        ok(ret, "Incomplete descriptor setup failed: %lu.\n", GetLastError());
        if (!ret) { winetest_pop_context(); return; }
        granted = 0xdeadbeef;
        access = 0x12345678;
        memset(&privileges, 0xcc, sizeof(privileges));
        size = sizeof(privileges);
        SetLastError(0xdeadbeef);
        ret = check_by_type(&sd, NULL, token, FILE_READ_DATA, objects, 2, &mapping,
                            &privileges, &size, &granted, &access);
        error = GetLastError();
        ok(!ret && error == ERROR_INVALID_SECURITY_DESCR, "Invalid descriptor returned %d, error %lu.\n", ret, error);
        ok(granted == 0xdeadbeef, "Failed scalar API overwrote grant %#lx.\n", granted);
        trace("Incomplete descriptor scalar BOOL output %#x.\n", access);
        for (j = 0; j < ARRAY_SIZE(objects); ++j)
        {
            grants[j] = 0xdeadbeef;
            statuses[j] = 0xdeadbeef;
        }
        memset(&privileges, 0xcc, sizeof(privileges));
        size = sizeof(privileges);
        SetLastError(0xdeadbeef);
        ret = check_result_list(&sd, NULL, token, FILE_READ_DATA, objects, 2, &mapping,
                                &privileges, &size, grants, statuses);
        error = GetLastError();
        ok(grants[2] == 0xdeadbeef && statuses[2] == 0xdeadbeef, "Failed two-entry list API overwrote the guard entry.\n");
        ok(!ret && error == ERROR_INVALID_SECURITY_DESCR, "Invalid list descriptor returned %d, error %lu.\n", ret, error);
        for (j = 0; j < 2; ++j)
        {
            ok(grants[j] == 0xdeadbeef, "Failed list API overwrote entry %lu grant %#lx.\n", j, grants[j]);
            ok(statuses[j] == 0xdeadbeef, "Failed list API overwrote entry %lu status %#lx.\n", j, statuses[j]);
        }
        winetest_pop_context();
    }

    ret = SetSecurityDescriptorOwner(&sd, &everyone, FALSE) &&
          SetSecurityDescriptorGroup(&sd, &everyone, FALSE) &&
          InitializeAcl(mixed, sizeof(mixed_buffer), ACL_REVISION_DS) &&
          AddAccessDeniedObjectAce(mixed, ACL_REVISION_DS, 0, ADS_RIGHT_DS_READ_PROP, &personal, NULL, &everyone) &&
          AddAccessAllowedObjectAce(mixed, ACL_REVISION_DS, 0, ADS_RIGHT_DS_READ_PROP, &object, NULL, &everyone) &&
          SetSecurityDescriptorDacl(&sd, TRUE, mixed, FALSE);
    ok(ret, "Mixed hierarchy setup failed: %lu.\n", GetLastError());
    if (!ret) return;
    winetest_push_context("public mixed property sets");
    for (j = 0; j < ARRAY_SIZE(objects); ++j)
    {
        grants[j] = 0xdeadbeef;
        statuses[j] = 0xdeadbeef;
    }
    memset(&privileges, 0xcc, sizeof(privileges));
    size = sizeof(privileges);
    SetLastError(0xdeadbeef);
    ret = check_result_list(&sd, NULL, token, ADS_RIGHT_DS_READ_PROP, objects, ARRAY_SIZE(objects), &ds_mapping,
                            &privileges, &size, grants, statuses);
    error = GetLastError();
    ok(ret, "Mixed result-list check failed: %lu.\n", error);
    if (ret)
    {
        ok(grants[0] != 0xdeadbeef && statuses[0] != 0xdeadbeef, "Root result was not initialized.\n");
        trace("Mixed root grant %#lx, status %#lx.\n", grants[0], statuses[0]);
        ok(grants[1] == 0 && statuses[1] == ERROR_ACCESS_DENIED,
           "Denied property set grant %#lx, Win32 status %#lx.\n", grants[1], statuses[1]);
        ok(grants[2] == ADS_RIGHT_DS_READ_PROP && statuses[2] == ERROR_SUCCESS,
           "Allowed property set grant %#lx, Win32 status %#lx.\n", grants[2], statuses[2]);
        ok(!privileges.PrivilegeCount, "Mixed property access used %lu privileges.\n", privileges.PrivilegeCount);
    }
    winetest_pop_context();
}

static void check_ntfs_directory_dacl(HANDLE handle, PACL expected, BOOL inherited)
{
    PSECURITY_DESCRIPTOR sd;
    SECURITY_DESCRIPTOR_CONTROL control;
    ACCESS_ALLOWED_ACE *actual_ace, *expected_ace;
    PACL dacl = NULL;
    DWORD size = 0, capacity, revision, i;
    NTSTATUS status;
    BOOL present = FALSE, defaulted = FALSE, ret;

    status = NtQuerySecurityObject(handle, DACL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(status == STATUS_BUFFER_TOO_SMALL, "DACL sizing returned %#lx, size %lu.\n", (DWORD)status, size);
    ok(size >= sizeof(SECURITY_DESCRIPTOR_RELATIVE) && size <= 65536, "Invalid DACL size %lu.\n", size);
    if (size < sizeof(SECURITY_DESCRIPTOR_RELATIVE) || size > 65536) return;
    capacity = size;
    sd = malloc(capacity);
    ok(!!sd, "DACL allocation failed.\n");
    if (!sd) return;
    status = NtQuerySecurityObject(handle, DACL_SECURITY_INFORMATION, sd, capacity, &size);
    ok(!status && size <= capacity, "DACL query returned %#lx, size %lu, capacity %lu.\n", (DWORD)status, size, capacity);
    if (status || size > capacity) goto done;
    ret = IsValidSecurityDescriptor(sd);
    ok(ret, "Queried security descriptor is invalid.\n");
    if (!ret) goto done;
    ret = GetSecurityDescriptorControl(sd, &control, &revision);
    ok(ret, "Descriptor control query failed: %lu.\n", GetLastError());
    if (ret) ok(!!(control & SE_DACL_PROTECTED) == !inherited, "Unexpected DACL control %#x.\n", control);
    ret = GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
    ok(ret && present && dacl, "DACL query returned %d, present %d, DACL %p.\n", ret, present, dacl);
    if (!ret || !present || !dacl) goto done;
    ret = IsValidAcl(dacl);
    ok(ret, "Queried DACL is invalid.\n");
    if (!ret) goto done;
    ok(dacl->AceCount == expected->AceCount, "DACL has %u ACEs, expected %u.\n", dacl->AceCount, expected->AceCount);
    for (i = 0; i < dacl->AceCount && i < expected->AceCount; ++i)
    {
        ret = GetAce(dacl, i, (void **)&actual_ace) && GetAce(expected, i, (void **)&expected_ace);
        ok(ret, "ACE %lu query failed: %lu.\n", i, GetLastError());
        if (!ret) continue;
        ok(actual_ace->Header.AceType == expected_ace->Header.AceType, "ACE %lu type %u, expected %u.\n",
           i, actual_ace->Header.AceType, expected_ace->Header.AceType);
        ok(actual_ace->Header.AceFlags == (inherited ? 0 : expected_ace->Header.AceFlags),
           "ACE %lu flags %#x, expected %#x.\n", i, actual_ace->Header.AceFlags,
           inherited ? 0 : expected_ace->Header.AceFlags);
        if (actual_ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE &&
            actual_ace->Header.AceType != ACCESS_DENIED_ACE_TYPE) continue;
        ok(actual_ace->Header.AceSize >= FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) +
           GetLengthSid(&expected_ace->SidStart), "ACE %lu is too small: %u.\n", i, actual_ace->Header.AceSize);
        if (actual_ace->Header.AceSize < FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) +
            GetLengthSid(&expected_ace->SidStart)) continue;
        ok(actual_ace->Mask == expected_ace->Mask, "ACE %lu mask %#lx, expected %#lx.\n",
           i, actual_ace->Mask, expected_ace->Mask);
        ret = IsValidSid(&actual_ace->SidStart);
        ok(ret && EqualSid(&actual_ace->SidStart, &expected_ace->SidStart), "ACE %lu SID differs.\n", i);
    }
done:
    free(sd);
}

static void check_ntfs_child_access(HANDLE file, const char *path, HANDLE token)
{
    PSECURITY_DESCRIPTOR sd = NULL;
    HANDLE reopened;
    DWORD error;

    error = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                            DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &sd);
    ok(error == ERROR_SUCCESS && sd, "Child access descriptor query returned %lu, SD %p.\n", error, sd);
    if (!error && sd)
    {
        check_acl_file_access(sd, token, GENERIC_READ, TRUE);
        check_acl_file_access(sd, token, FILE_WRITE_DATA, FALSE);
        check_acl_file_access(sd, token, FILE_READ_DATA | FILE_WRITE_DATA, FALSE);
    }
    LocalFree(sd);
    reopened = CreateFileA(path, FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(reopened != INVALID_HANDLE_VALUE, "Child read reopen failed: %lu.\n", GetLastError());
    if (reopened != INVALID_HANDLE_VALUE) CloseHandle(reopened);
    reopened = CreateFileA(path, FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    error = GetLastError();
    ok(reopened == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED,
       "Child write reopen returned %p, error %lu; expected access denied.\n", reopened, error);
    if (reopened != INVALID_HANDLE_VALUE) CloseHandle(reopened);
}

static void check_ntfs_directory_basic(HANDLE handle, LARGE_INTEGER creation)
{
    FILE_BASIC_INFORMATION basic;
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    memset(&basic, 0xcc, sizeof(basic));
    status = NtQueryInformationFile(handle, &io, &basic, sizeof(basic), FileBasicInformation);
    ok(!status, "Basic information query returned %#lx.\n", (DWORD)status);
    if (!status)
        ok(basic.CreationTime.QuadPart == creation.QuadPart, "Creation time %I64d, expected %I64d.\n",
           basic.CreationTime.QuadPart, creation.QuadPart);
}

static void reopen_ntfs_directory_read_handle(HANDLE *handle, const char *path)
{
    if (*handle != INVALID_HANDLE_VALUE) CloseHandle(*handle);
    *handle = CreateFileA(path, READ_CONTROL | FILE_READ_ATTRIBUTES | FILE_READ_EA,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    ok(*handle != INVALID_HANDLE_VALUE, "Fresh directory open failed: %lu.\n", GetLastError());
}

static void check_ntfs_directory_existing_child(HANDLE handle)
{
    union { DWORD align; BYTE bytes[1024]; } buffer;
    FILE_NAMES_INFORMATION *info = (void *)buffer.bytes;
    UNICODE_STRING name;
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    RtlInitUnicodeString(&name, L"existing");
    memset(&buffer, 0xcc, sizeof(buffer));
    status = NtQueryDirectoryFile(handle, NULL, NULL, NULL, &io, info, sizeof(buffer),
                                  FileNamesInformation, TRUE, &name, TRUE);
    ok(!status, "Retained directory enumeration returned %#lx.\n", (DWORD)status);
    if (status) return;
    ok(io.Information >= FIELD_OFFSET(FILE_NAMES_INFORMATION, FileName) + name.Length &&
       io.Information <= sizeof(buffer), "Directory enumeration size %Iu.\n", io.Information);
    if (io.Information < FIELD_OFFSET(FILE_NAMES_INFORMATION, FileName) + name.Length ||
        io.Information > sizeof(buffer)) return;
    ok(info->FileNameLength == name.Length && !memcmp(info->FileName, name.Buffer, name.Length),
       "Retained directory did not enumerate the existing child.\n");
}

static void warm_ntfs_directory_lookup(const char *path)
{
    HANDLE handle = CreateFileA(path, FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, 0, NULL);

    ok(handle != INVALID_HANDLE_VALUE, "Existing child cache-warming open failed: %lu.\n", GetLastError());
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
}

static void check_ntfs_directory_ea(HANDLE handle, FILE_FULL_EA_INFORMATION *expected, DWORD size)
{
    union { DWORD align; BYTE bytes[1024]; } buffer;
    FILE_FULL_EA_INFORMATION *actual = (void *)buffer.bytes;
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    memset(&buffer, 0xcc, sizeof(buffer));
    status = NtQueryEaFile(handle, &io, actual, sizeof(buffer), TRUE, NULL, 0, NULL, TRUE);
    ok(!status, "EA query returned %#lx.\n", (DWORD)status);
    if (status) return;
    ok(io.Information == size, "EA query size %Iu, expected %lu.\n", io.Information, size);
    if (io.Information != size) return;
    ok(!memcmp(actual, expected, size), "Queried EA differs from the last write.\n");
}

static void check_ntfs_directory_reparse(HANDLE handle, const void *expected, DWORD size)
{
    union { DWORD align; BYTE bytes[16384]; } buffer;
    DWORD returned = 0, error;
    BOOL ret;

    ret = DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, NULL, 0, buffer.bytes,
                          sizeof(buffer), &returned, NULL);
    error = GetLastError();
    if (!expected)
    {
        ok(!ret && error == ERROR_NOT_A_REPARSE_POINT, "Deleted reparse query returned %d, error %lu.\n", ret, error);
        return;
    }
    ok(ret, "Reparse query failed: %lu.\n", error);
    if (!ret) return;
    ok(returned == size, "Reparse query size %lu, expected %lu.\n", returned, size);
    if (returned == size) ok(!memcmp(buffer.bytes, expected, size), "Queried reparse value differs from the last write.\n");
}

static void test_ntfs_directory_reparse(const char *root, SECURITY_DESCRIPTOR *initial_sd, PACL denied_acl)
{
    struct ntfs_mount_point
    {
        DWORD tag;
        WORD data_length, reserved;
        WORD substitute_offset, substitute_length, print_offset, print_length;
        WCHAR path[2 * MAX_PATH + 8];
    } reparse;
    struct { DWORD tag; WORD data_length, reserved; } deletion;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), initial_sd, FALSE};
    SECURITY_DESCRIPTOR sd;
    char junction[MAX_PATH], target[MAX_PATH];
    WCHAR wide_target[MAX_PATH];
    HANDLE handles[3] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    DWORD length, size, returned, i;
    NTSTATUS status;
    BOOL ret, junction_created = FALSE, target_created = FALSE, reparse_set = FALSE;

    sprintf(junction, "%s\\junction", root);
    sprintf(target, "%s\\target", root);
    ret = CreateDirectoryA(target, &attributes);
    ok(ret, "Reparse target creation failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    target_created = TRUE;
    ret = CreateDirectoryA(junction, &attributes);
    ok(ret, "Empty reparse directory creation failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    junction_created = TRUE;
    for (i = 0; i < 2; ++i)
    {
        handles[i] = CreateFileA(junction, READ_CONTROL | WRITE_DAC | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        ok(handles[i] != INVALID_HANDLE_VALUE, "Old reparse directory handle %lu failed: %lu.\n", i, GetLastError());
        if (handles[i] == INVALID_HANDLE_VALUE) goto done;
    }
    ret = InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorDacl(&sd, TRUE, denied_acl, FALSE) &&
          SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    ok(ret, "Reparse directory descriptor setup failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    status = NtSetSecurityObject(handles[0], DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &sd);
    ok(!status, "Reparse directory DACL replacement returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    length = MultiByteToWideChar(CP_ACP, 0, target, -1, wide_target, ARRAY_SIZE(wide_target));
    ok(!!length, "Reparse target conversion failed: %lu.\n", GetLastError());
    if (!length) goto done;
    --length;
    memset(&reparse, 0, sizeof(reparse));
    reparse.tag = IO_REPARSE_TAG_MOUNT_POINT;
    reparse.substitute_length = (length + 4) * sizeof(WCHAR);
    reparse.print_offset = reparse.substitute_length + sizeof(WCHAR);
    reparse.print_length = length * sizeof(WCHAR);
    memcpy(reparse.path, L"\\??\\", 4 * sizeof(WCHAR));
    memcpy(reparse.path + 4, wide_target, (length + 1) * sizeof(WCHAR));
    memcpy((BYTE *)reparse.path + reparse.print_offset, wide_target, (length + 1) * sizeof(WCHAR));
    size = FIELD_OFFSET(struct ntfs_mount_point, path) + reparse.print_offset +
           reparse.print_length + sizeof(WCHAR);
    reparse.data_length = size - FIELD_OFFSET(struct ntfs_mount_point, substitute_offset);
    ret = DeviceIoControl(handles[1], FSCTL_SET_REPARSE_POINT, &reparse, size, NULL, 0, &returned, NULL);
    ok(ret, "Mount-point set without enabled privileges failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    reparse_set = TRUE;
    handles[2] = CreateFileA(junction, READ_CONTROL | FILE_READ_ATTRIBUTES,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    ok(handles[2] != INVALID_HANDLE_VALUE, "Fresh reparse directory open failed: %lu.\n", GetLastError());
    for (i = 0; i < ARRAY_SIZE(handles); ++i)
    {
        if (handles[i] == INVALID_HANDLE_VALUE) continue;
        winetest_push_context("reparse retained/fresh handle %lu", i);
        check_ntfs_directory_reparse(handles[i], &reparse, size);
        check_ntfs_directory_dacl(handles[i], denied_acl, FALSE);
        winetest_pop_context();
    }
    memset(&deletion, 0, sizeof(deletion));
    deletion.tag = IO_REPARSE_TAG_MOUNT_POINT;
    ret = DeviceIoControl(handles[1], FSCTL_DELETE_REPARSE_POINT, &deletion, sizeof(deletion), NULL, 0, &returned, NULL);
    ok(ret, "Mount-point deletion failed: %lu.\n", GetLastError());
    if (ret)
    {
        reparse_set = FALSE;
        for (i = 0; i < ARRAY_SIZE(handles); ++i)
        {
            if (handles[i] == INVALID_HANDLE_VALUE) continue;
            winetest_push_context("deleted reparse retained/fresh handle %lu", i);
            check_ntfs_directory_reparse(handles[i], NULL, 0);
            check_ntfs_directory_dacl(handles[i], denied_acl, FALSE);
            winetest_pop_context();
        }
    }
done:
    if (reparse_set && handles[1] != INVALID_HANDLE_VALUE)
    {
        memset(&deletion, 0, sizeof(deletion));
        deletion.tag = IO_REPARSE_TAG_MOUNT_POINT;
        ret = DeviceIoControl(handles[1], FSCTL_DELETE_REPARSE_POINT, &deletion, sizeof(deletion), NULL, 0, &returned, NULL);
        ok(ret, "Cleanup mount-point deletion failed: %lu.\n", GetLastError());
    }
    if (handles[0] != INVALID_HANDLE_VALUE)
    {
        status = NtSetSecurityObject(handles[0], DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, initial_sd);
        ok(!status, "Reparse directory ACL cleanup returned %#lx.\n", (DWORD)status);
    }
    for (i = 0; i < ARRAY_SIZE(handles); ++i)
        if (handles[i] != INVALID_HANDLE_VALUE) CloseHandle(handles[i]);
    if (junction_created) ok(RemoveDirectoryA(junction), "Reparse directory cleanup failed: %lu.\n", GetLastError());
    if (target_created) ok(RemoveDirectoryA(target), "Reparse target cleanup failed: %lu.\n", GetLastError());
}

static void check_ntfs_inheritance_descriptor(PSECURITY_DESCRIPTOR sd, PACL expected,
                                               DWORD expected_control, DWORD expected_flags)
{
    SECURITY_DESCRIPTOR_CONTROL control;
    ACCESS_ALLOWED_ACE *ace, *expected_ace;
    PACL acl = NULL;
    DWORD revision, i, mask;
    BOOL present = FALSE, defaulted = FALSE, ret;

    ret = IsValidSecurityDescriptor(sd);
    ok(ret, "Inheritance observation descriptor is invalid.\n");
    if (!ret) return;
    ret = GetSecurityDescriptorControl(sd, &control, &revision);
    ok(ret, "Inheritance observation control failed: %lu.\n", GetLastError());
    if (!ret) return;
    ret = GetSecurityDescriptorDacl(sd, &present, &acl, &defaulted);
    ok(ret, "Inheritance observation DACL failed: %lu.\n", GetLastError());
    if (!ret) return;
    ok(present && acl, "Inheritance DACL is missing.\n");
    if (!present || !acl) return;
    ret = IsValidAcl(acl);
    ok(ret, "Inheritance observation ACL is invalid.\n");
    if (!ret) return;
    if (!expected)
    {
        trace("Public parent control %#x, ACE count %u.\n", control, acl->AceCount);
        return;
    }
    ok(control == expected_control, "Inheritance control %#x, expected %#lx.\n", control, expected_control);
    ok(!defaulted, "Inheritance DACL is defaulted.\n");
    ok(acl->AceCount == expected->AceCount, "Inheritance ACE count %u, expected %u.\n", acl->AceCount, expected->AceCount);
    for (i = 0; i < acl->AceCount && i < expected->AceCount; ++i)
    {
        ret = GetAce(acl, i, (void **)&ace) && GetAce(expected, i, (void **)&expected_ace);
        ok(ret, "Inheritance observation ACE %lu failed: %lu.\n", i, GetLastError());
        if (!ret) continue;
        ok(ace->Header.AceType == expected_ace->Header.AceType, "Inheritance ACE %lu type %u, expected %u.\n",
           i, ace->Header.AceType, expected_ace->Header.AceType);
        ok(ace->Header.AceFlags == expected_flags, "Inheritance ACE %lu flags %#x, expected %#lx.\n",
           i, ace->Header.AceFlags, expected_flags);
        if (ace->Header.AceType != expected_ace->Header.AceType) continue;
        ret = ace->Header.AceSize >= FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + FIELD_OFFSET(SID, SubAuthority);
        ok(ret, "Inheritance ACE %lu is too small: %u.\n", i, ace->Header.AceSize);
        if (!ret) continue;
        ret = GetLengthSid(&ace->SidStart) <= ace->Header.AceSize - FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) &&
              IsValidSid(&ace->SidStart);
        ok(ret, "Inheritance observation ACE %lu SID is invalid.\n", i);
        if (!ret) continue;
        ok(EqualSid(&ace->SidStart, &expected_ace->SidStart), "Inheritance ACE %lu SID %s, expected %s.\n",
           i, debugstr_sid(&ace->SidStart), debugstr_sid(&expected_ace->SidStart));
        mask = expected_ace->Mask == GENERIC_ALL ? FILE_ALL_ACCESS : expected_ace->Mask;
        ok(ace->Mask == mask, "Inheritance ACE %lu mask %#lx, expected %#lx.\n", i, ace->Mask, mask);
    }
}

static void check_ntfs_inheritance_security(HANDLE handle, const char *path, DWORD api,
                                           PACL expected, DWORD expected_control, DWORD expected_flags)
{
    SECURITY_INFORMATION information = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    PSECURITY_DESCRIPTOR sd = NULL;
    DWORD size = 0, capacity, error;
    NTSTATUS status;

    if (!api)
    {
        status = NtQuerySecurityObject(handle, information, NULL, 0, &size);
        ok(status == STATUS_BUFFER_TOO_SMALL, "Inheritance observation sizing returned %#lx, size %lu.\n", (DWORD)status, size);
        ok(size >= sizeof(SECURITY_DESCRIPTOR_RELATIVE) && size <= 65536, "Invalid inheritance observation size %lu.\n", size);
        if (status != STATUS_BUFFER_TOO_SMALL || size < sizeof(SECURITY_DESCRIPTOR_RELATIVE) || size > 65536) return;
        capacity = size;
        sd = malloc(capacity);
        ok(!!sd, "Inheritance observation allocation failed.\n");
        if (!sd) return;
        status = NtQuerySecurityObject(handle, information, sd, capacity, &size);
        ok(!status && size <= capacity, "Inheritance observation query returned %#lx, size %lu.\n", (DWORD)status, size);
        if (!status && size <= capacity) check_ntfs_inheritance_descriptor(sd, expected, expected_control, expected_flags);
        free(sd);
    }
    else
    {
        if (api == 1) error = GetSecurityInfo(handle, SE_FILE_OBJECT, information, NULL, NULL, NULL, NULL, &sd);
        else error = GetNamedSecurityInfoA((char *)path, SE_FILE_OBJECT, information, NULL, NULL, NULL, NULL, &sd);
        ok(!error && sd, "Inheritance observation public getter %lu returned %lu, SD %p.\n", api, error, sd);
        if (!error && sd) check_ntfs_inheritance_descriptor(sd, expected, expected_control, expected_flags);
        if (sd) LocalFree(sd);
    }
}

static void test_ntfs_inheritance_attribute_denial(HANDLE setter, const char *path, OBJECT_ATTRIBUTES *attr, PSID sid)
{
    union { ULONG_PTR align; BYTE bytes[256]; } acl_buffer;
    PACL acl = (PACL)acl_buffer.bytes, original_acl = NULL;
    SECURITY_DESCRIPTOR denied;
    PSECURITY_DESCRIPTOR original = NULL, result;
    SECURITY_DESCRIPTOR_CONTROL control;
    SECURITY_INFORMATION restore = DACL_SECURITY_INFORMATION;
    IO_STATUS_BLOCK io;
    HANDLE limited = INVALID_HANDLE_VALUE;
    DWORD size = 0, capacity, revision, api, error;
    NTSTATUS status;
    BOOL ret, changed = FALSE, present, defaulted;

    winetest_push_context("denied read attributes");
    status = NtQuerySecurityObject(setter, DACL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(status == STATUS_BUFFER_TOO_SMALL, "Attribute denial original DACL sizing returned %#lx.\n", (DWORD)status);
    ok(size >= sizeof(SECURITY_DESCRIPTOR_RELATIVE) && size <= 65536, "Invalid attribute denial original DACL size %lu.\n", size);
    if (status != STATUS_BUFFER_TOO_SMALL || size < sizeof(SECURITY_DESCRIPTOR_RELATIVE) || size > 65536) goto done;
    capacity = size;
    original = malloc(capacity);
    ok(!!original, "Attribute denial original DACL allocation failed.\n");
    if (!original) goto done;
    status = NtQuerySecurityObject(setter, DACL_SECURITY_INFORMATION, original, capacity, &size);
    ok(!status && size <= capacity, "Attribute denial original DACL query returned %#lx, size %lu.\n", (DWORD)status, size);
    if (status || size > capacity) goto done;
    ret = IsValidSecurityDescriptor(original) && GetSecurityDescriptorControl(original, &control, &revision) &&
          GetSecurityDescriptorDacl(original, &present, &original_acl, &defaulted) && present && original_acl &&
          IsValidAcl(original_acl);
    ok(ret, "Attribute denial original control query failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    restore |= control & SE_DACL_PROTECTED ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION;
    ret = InitializeSecurityDescriptor(&denied, SECURITY_DESCRIPTOR_REVISION) &&
          InitializeAcl(acl, sizeof(acl_buffer), ACL_REVISION) &&
          AddAccessDeniedAceEx(acl, ACL_REVISION, 0, FILE_READ_ATTRIBUTES, sid) &&
          AddAccessAllowedAceEx(acl, ACL_REVISION, 0, READ_CONTROL | WRITE_DAC | DELETE, sid) &&
          SetSecurityDescriptorDacl(&denied, TRUE, acl, FALSE) &&
          SetSecurityDescriptorControl(&denied, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    ok(ret, "Attribute denial DACL setup failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    status = NtSetSecurityObject(setter, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &denied);
    ok(!status, "Attribute denial DACL replacement returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    changed = TRUE;
    status = NtCreateFile(&limited, READ_CONTROL, attr, &io, NULL, 0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN, 0, NULL, 0);
    ok(!status, "Attribute denial READ_CONTROL reopen returned %#lx.\n", (DWORD)status);
    if (status) limited = INVALID_HANDLE_VALUE;
    for (api = 1; api < 3; ++api)
    {
        if (api == 1 && limited == INVALID_HANDLE_VALUE) continue;
        result = NULL;
        if (api == 1) error = GetSecurityInfo(limited, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                             NULL, NULL, NULL, NULL, &result);
        else error = GetNamedSecurityInfoA((char *)path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                           NULL, NULL, NULL, NULL, &result);
        ok(!error && result, "Attribute denial public getter %lu returned %lu, SD %p.\n", api, error, result);
        if (!error && result)
            check_ntfs_inheritance_descriptor(result, acl, SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_DACL_PROTECTED, 0);
        if (result) LocalFree(result);
    }
done:
    if (limited != INVALID_HANDLE_VALUE) CloseHandle(limited);
    if (changed)
    {
        status = NtSetSecurityObject(setter, restore, original);
        ok(!status, "Attribute denial original DACL restore returned %#lx.\n", (DWORD)status);
        winetest_push_context("direct after original restore");
        check_ntfs_inheritance_security(setter, path, 0, original_acl, control, 0);
        winetest_pop_context();
    }
    free(original);
    winetest_pop_context();
}

static void test_ntfs_inheritance_handle_access(const char *path, PSID sid, PACL expected)
{
    static const struct
    {
        ACCESS_MASK access;
        ULONG options;
    } cases[] =
    {
        {READ_CONTROL | SYNCHRONIZE, 0},
        {READ_CONTROL | SYNCHRONIZE | FILE_READ_ATTRIBUTES, 0},
        {READ_CONTROL | SYNCHRONIZE, FILE_SYNCHRONOUS_IO_NONALERT},
        {READ_CONTROL | SYNCHRONIZE | FILE_READ_ATTRIBUTES, FILE_SYNCHRONOUS_IO_NONALERT},
        {FILE_ALL_ACCESS, 0}
    };
    FILE_BASIC_INFORMATION basic;
    FILE_STANDARD_INFORMATION standard;
    OBJECT_ATTRIBUTES attr;
    IO_STATUS_BLOCK io;
    UNICODE_STRING name;
    WCHAR wide[MAX_PATH];
    PSECURITY_DESCRIPTOR sd;
    PSID owner, group;
    HANDLE handle;
    NTSTATUS status, basic_status, standard_status;
    DWORD i, shape, error, expected_control, expected_flags;
    SECURITY_INFORMATION information;
    BOOL ret, owner_defaulted, group_defaulted;

    ret = MultiByteToWideChar(CP_ACP, 0, path, -1, wide, ARRAY_SIZE(wide)) &&
          pRtlDosPathNameToNtPathName_U(wide, &name, NULL, NULL);
    ok(ret, "Native reopen observation path conversion failed: %lu.\n", GetLastError());
    if (!ret) return;
    InitializeObjectAttributes(&attr, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    for (i = 0; i < ARRAY_SIZE(cases); ++i)
    {
        winetest_push_context("native reopen access %#lx options %#lx", cases[i].access, cases[i].options);
        handle = INVALID_HANDLE_VALUE;
        status = NtCreateFile(&handle, cases[i].access, &attr, &io, NULL, 0,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              FILE_OPEN, cases[i].options, NULL, 0);
        ok(!status, "Native reopen observation returned %#lx.\n", (DWORD)status);
        if (!status)
        {
            expected_control = SE_SELF_RELATIVE | SE_DACL_PRESENT;
            expected_flags = 0;
            if (cases[i].access & FILE_READ_ATTRIBUTES) expected_flags = INHERITED_ACE;
            else expected_control |= SE_DACL_PROTECTED;
            winetest_push_context("direct before");
            check_ntfs_inheritance_security(handle, path, 0, expected, SE_SELF_RELATIVE | SE_DACL_PRESENT, 0);
            winetest_pop_context();
            memset(&basic, 0, sizeof(basic));
            memset(&standard, 0, sizeof(standard));
            basic_status = NtQueryInformationFile(handle, &io, &basic, sizeof(basic), FileBasicInformation);
            standard_status = NtQueryInformationFile(handle, &io, &standard, sizeof(standard), FileStandardInformation);
            status = cases[i].access & FILE_READ_ATTRIBUTES ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
            ok(basic_status == status, "Native reopen basic status %#lx, expected %#lx.\n", (DWORD)basic_status, (DWORD)status);
            ok(!standard_status, "Native reopen standard status %#lx.\n", (DWORD)standard_status);
            winetest_push_context("GetSecurityInfo");
            check_ntfs_inheritance_security(handle, path, 1, expected, expected_control, expected_flags);
            winetest_pop_context();
            winetest_push_context("direct after");
            check_ntfs_inheritance_security(handle, path, 0, expected, SE_SELF_RELATIVE | SE_DACL_PRESENT, 0);
            winetest_pop_context();
            if (i == ARRAY_SIZE(cases) - 1)
            {
                for (shape = 0; shape < 2; ++shape)
                {
                    information = DACL_SECURITY_INFORMATION;
                    if (shape) information |= OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION;
                    winetest_push_context("requested information %#lx", information);
                    sd = NULL;
                    error = GetSecurityInfo(handle, SE_FILE_OBJECT, information, NULL, NULL, NULL, NULL, &sd);
                    ok(!error && sd, "Native reopen shape getter returned %lu, SD %p.\n", error, sd);
                    if (!error && sd)
                    {
                        check_ntfs_inheritance_descriptor(sd, expected, expected_control, expected_flags);
                        owner = group = NULL;
                        ret = GetSecurityDescriptorOwner(sd, &owner, &owner_defaulted) &&
                              GetSecurityDescriptorGroup(sd, &group, &group_defaulted);
                        ok(ret && owner && group, "Native reopen shape owner/group query failed: %lu.\n", GetLastError());
                        if (ret && owner && group)
                        {
                            ok(IsValidSid(owner) && IsValidSid(group), "Native reopen shape owner/group SID is invalid.\n");
                            ok(!owner_defaulted && !group_defaulted, "Native reopen shape owner/group is defaulted.\n");
                        }
                    }
                    if (sd) LocalFree(sd);
                    winetest_pop_context();
                }
                winetest_push_context("direct after requested shapes");
                check_ntfs_inheritance_security(handle, path, 0, expected, SE_SELF_RELATIVE | SE_DACL_PRESENT, 0);
                winetest_pop_context();
                test_ntfs_inheritance_attribute_denial(handle, path, &attr, sid);
            }
            CloseHandle(handle);
        }
        winetest_pop_context();
    }
    RtlFreeUnicodeString(&name);
}

static void test_ntfs_public_inheritance(const char *temp, HANDLE token)
{
    static const char *names[] = {"parent", "CreateFile", "NtCreateFile", "post-public child"};
    union { DWORD align; BYTE bytes[256]; } acl_buffer, admin_buffer;
    PSID admin = admin_buffer.bytes;
    PACL acl = (PACL)acl_buffer.bytes;
    SECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), &sd, FALSE};
    OBJECT_ATTRIBUTES attr;
    IO_STATUS_BLOCK io;
    UNICODE_STRING name;
    TOKEN_USER *user = NULL;
    WCHAR wide[MAX_PATH];
    char paths[4][MAX_PATH];
    HANDLE objects[4];
    DWORD size = 0, admin_size = sizeof(admin_buffer), protected, specific, i, api, error;
    BOOL ret, created[4], reserved;
    NTSTATUS status;

    ret = GetTokenInformation(token, TokenUser, NULL, 0, &size);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER, "Inheritance observation TokenUser sizing returned %d, error %lu.\n", ret, error);
    if (ret || error != ERROR_INSUFFICIENT_BUFFER || !size) goto done;
    user = malloc(size);
    ok(!!user, "Inheritance observation TokenUser allocation failed.\n");
    if (!user) goto done;
    ret = GetTokenInformation(token, TokenUser, user, size, &size) &&
          CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, admin, &admin_size);
    ok(ret, "Inheritance observation SID setup failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ok(!!pRtlDosPathNameToNtPathName_U, "Inheritance observation NT path converter is missing.\n");
    if (!pRtlDosPathNameToNtPathName_U) goto done;
    for (protected = 0; protected < 2; ++protected)
    {
        for (specific = 0; specific < 2; ++specific)
        {
            winetest_push_context("NTFS getter inheritance protected %lu specific %lu", protected, specific);
            memset(created, 0, sizeof(created));
            reserved = FALSE;
            for (i = 0; i < ARRAY_SIZE(objects); ++i) objects[i] = INVALID_HANDLE_VALUE;
            ret = InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
                  InitializeAcl(acl, sizeof(acl_buffer), ACL_REVISION) &&
                  AddAccessAllowedAceEx(acl, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
                                        specific ? FILE_ALL_ACCESS : GENERIC_ALL, user->User.Sid) &&
                  AddAccessAllowedAceEx(acl, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
                                        specific ? FILE_ALL_ACCESS : GENERIC_ALL, admin) &&
                  SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE) &&
                  SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED, protected ? SE_DACL_PROTECTED : 0);
            ok(ret, "Inheritance observation descriptor setup failed: %lu.\n", GetLastError());
            if (!ret) goto cleanup;
            ret = GetTempFileNameA(temp, "nio", 0, paths[0]);
            ok(ret, "Inheritance observation reservation failed: %lu.\n", GetLastError());
            if (!ret) goto cleanup;
            reserved = TRUE;
            ret = DeleteFileA(paths[0]);
            ok(ret, "Inheritance observation reservation cleanup failed: %lu.\n", GetLastError());
            if (!ret) goto cleanup;
            reserved = FALSE;
            ret = strlen(paths[0]) + sizeof("\\postchild") < MAX_PATH;
            ok(ret, "Inheritance observation path is too long.\n");
            if (!ret) goto cleanup;
            sprintf(paths[1], "%s\\winchild", paths[0]);
            sprintf(paths[2], "%s\\ntchild", paths[0]);
            sprintf(paths[3], "%s\\postchild", paths[0]);
            created[0] = CreateDirectoryA(paths[0], &attributes);
            ok(created[0], "Inheritance observation parent creation failed: %lu.\n", GetLastError());
            if (!created[0]) goto cleanup;
            objects[0] = CreateFileA(paths[0], READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            objects[1] = CreateFileA(paths[1], GENERIC_WRITE | READ_CONTROL | DELETE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW, 0, NULL);
            created[1] = objects[1] != INVALID_HANDLE_VALUE;
            ret = MultiByteToWideChar(CP_ACP, 0, paths[2], -1, wide, ARRAY_SIZE(wide)) &&
                  pRtlDosPathNameToNtPathName_U(wide, &name, NULL, NULL);
            ok(ret, "Inheritance observation NT path conversion failed: %lu.\n", GetLastError());
            if (ret)
            {
                InitializeObjectAttributes(&attr, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
                status = NtCreateFile(&objects[2], GENERIC_WRITE | READ_CONTROL | DELETE, &attr, &io, NULL, 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_CREATE, 0, NULL, 0);
                ok(!status, "Inheritance observation NtCreateFile returned %#lx.\n", (DWORD)status);
                if (status) objects[2] = INVALID_HANDLE_VALUE;
                else created[2] = TRUE;
                RtlFreeUnicodeString(&name);
            }
            for (i = 0; i < 3; ++i)
            {
                ok(objects[i] != INVALID_HANDLE_VALUE, "Inheritance observation %s handle is invalid.\n", names[i]);
                if (objects[i] == INVALID_HANDLE_VALUE) continue;
                winetest_push_context("%s direct before public", names[i]);
                check_ntfs_inheritance_security(objects[i], paths[i], 0, acl,
                                                SE_SELF_RELATIVE | SE_DACL_PRESENT | (!i && protected ? SE_DACL_PROTECTED : 0),
                                                i ? 0 : OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE);
                winetest_pop_context();
            }
            for (api = 1; api < 3; ++api)
            {
                for (i = 0; i < 3; ++i)
                {
                    if (objects[i] == INVALID_HANDLE_VALUE) continue;
                    winetest_push_context("%s public getter %lu", names[i], api);
                    check_ntfs_inheritance_security(objects[i], paths[i], api, i ? acl : NULL,
                                                    SE_SELF_RELATIVE | SE_DACL_PRESENT | (api == 1 && i == 2 ? SE_DACL_PROTECTED : 0),
                                                    api == 1 && i == 2 ? 0 : INHERITED_ACE);
                    winetest_pop_context();
                }
                for (i = 0; i < 3; ++i)
                {
                    if (objects[i] == INVALID_HANDLE_VALUE) continue;
                    winetest_push_context("%s direct after public getter %lu", names[i], api);
                    check_ntfs_inheritance_security(objects[i], paths[i], 0, acl,
                                                    SE_SELF_RELATIVE | SE_DACL_PRESENT | (!i && protected ? SE_DACL_PROTECTED : 0),
                                                    i ? 0 : OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE);
                    winetest_pop_context();
                }
            }
            objects[3] = CreateFileA(paths[3], GENERIC_WRITE | READ_CONTROL | DELETE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW, 0, NULL);
            created[3] = objects[3] != INVALID_HANDLE_VALUE;
            ok(created[3], "Inheritance observation post-public child creation failed: %lu.\n", GetLastError());
            if (created[3])
            {
                for (api = 0; api < 3; ++api)
                {
                    winetest_push_context("post-public child %s", api == 1 ? "GetNamedSecurityInfo" : api ? "direct after" : "direct before");
                    check_ntfs_inheritance_security(objects[3], paths[3], api == 1 ? 2 : 0, acl,
                                                    SE_SELF_RELATIVE | SE_DACL_PRESENT, api == 1 ? INHERITED_ACE : 0);
                    winetest_pop_context();
                }
            }
            if (!protected && !specific && objects[2] != INVALID_HANDLE_VALUE)
                test_ntfs_inheritance_handle_access(paths[2], user->User.Sid, acl);
cleanup:
            for (i = 0; i < ARRAY_SIZE(objects); ++i)
                if (objects[i] != INVALID_HANDLE_VALUE) CloseHandle(objects[i]);
            for (i = 1; i < ARRAY_SIZE(created); ++i)
                if (created[i]) ok(DeleteFileA(paths[i]), "Inheritance observation child %lu cleanup failed: %lu.\n", i, GetLastError());
            if (created[0]) ok(RemoveDirectoryA(paths[0]), "Inheritance observation parent cleanup failed: %lu.\n", GetLastError());
            if (reserved) ok(DeleteFileA(paths[0]), "Inheritance observation reservation final cleanup failed: %lu.\n", GetLastError());
            winetest_pop_context();
        }
    }
done:
    free(user);
}

static void check_ntfs_large_dacl(HANDLE handle, PACL expected)
{
    PSECURITY_DESCRIPTOR sd;
    PACL acl = NULL;
    ACL_SIZE_INFORMATION info;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD size = 0, capacity, revision;
    ULONG_PTR offset;
    NTSTATUS status;
    BOOL present = FALSE, defaulted = FALSE, ret;

    status = NtQuerySecurityObject(handle, DACL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(status == STATUS_BUFFER_TOO_SMALL, "Large DACL sizing returned %#lx, size %lu.\n", (DWORD)status, size);
    ok(size >= sizeof(SECURITY_DESCRIPTOR_RELATIVE) && size <= 65536, "Invalid large DACL size %lu.\n", size);
    if (status != STATUS_BUFFER_TOO_SMALL || size < sizeof(SECURITY_DESCRIPTOR_RELATIVE) || size > 65536) return;
    capacity = size;
    sd = malloc(capacity);
    ok(!!sd, "Large DACL query allocation failed.\n");
    if (!sd) return;
    status = NtQuerySecurityObject(handle, DACL_SECURITY_INFORMATION, sd, capacity, &size);
    ok(!status && size <= capacity, "Large DACL query returned %#lx, size %lu, capacity %lu.\n",
       (DWORD)status, size, capacity);
    if (status || size > capacity) goto done;
    ret = IsValidSecurityDescriptor(sd);
    ok(ret, "Large descriptor is invalid.\n");
    if (!ret) goto done;
    ret = GetSecurityDescriptorControl(sd, &control, &revision);
    ok(ret && (control & SE_DACL_PROTECTED), "Large descriptor control %#x, error %lu.\n", control, GetLastError());
    ret = GetSecurityDescriptorDacl(sd, &present, &acl, &defaulted);
    ok(ret && present && acl, "Large descriptor DACL returned %d, present %d, ACL %p.\n", ret, present, acl);
    if (!ret || !present || !acl) goto done;
    offset = (ULONG_PTR)acl - (ULONG_PTR)sd;
    ok(offset <= size && sizeof(*acl) <= size - offset, "Large DACL offset %Iu exceeds size %lu.\n", offset, size);
    if (offset > size || sizeof(*acl) > size - offset) goto done;
    ok(acl->AclSize <= size - offset, "Large DACL length %u exceeds descriptor remainder %Iu.\n", acl->AclSize, size - offset);
    if (acl->AclSize > size - offset) goto done;
    ret = IsValidAcl(acl) && GetAclInformation(acl, &info, sizeof(info), AclSizeInformation);
    ok(ret, "Large ACL validation failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ok(info.AceCount == expected->AceCount, "Large ACL has %lu ACEs, expected %u.\n", info.AceCount, expected->AceCount);
    ok(info.AclBytesInUse == expected->AclSize, "Large ACL uses %lu bytes, expected %u.\n", info.AclBytesInUse, expected->AclSize);
    if (info.AclBytesInUse == expected->AclSize)
        ok(!memcmp(acl, expected, expected->AclSize), "Large ACL bytes differ after persistence.\n");
done:
    free(sd);
}

static void test_ntfs_large_security(const char *temp, PSID sid, PACL initial_acl)
{
    static const DWORD counts[] = {64, 256, 1024, 64, 2};
    static const char content[] = "large security descriptor data preservation";
    SID_IDENTIFIER_AUTHORITY authority = {SECURITY_NT_AUTHORITY};
    SECURITY_DESCRIPTOR initial, replacement;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), &initial, FALSE};
    FILE_BASIC_INFORMATION basic;
    IO_STATUS_BLOCK io;
    FILETIME now;
    PACL acl = NULL;
    PSID distinct_sid = NULL;
    HANDLE handles[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE}, stream = INVALID_HANDLE_VALUE, opened;
    char path[MAX_PATH] = "", stream_path[MAX_PATH], readback[sizeof(content)];
    DWORD kind, phase, i, size, ace_size, distinct_ace_size, main_aces, transferred = 0, error, open_flags;
    DWORD access = READ_CONTROL | WRITE_DAC | DELETE | FILE_READ_DATA | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES;
    NTSTATUS status;
    BOOL ret, created, denied;

    ret = InitializeSecurityDescriptor(&initial, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorDacl(&initial, TRUE, initial_acl, FALSE) &&
          SetSecurityDescriptorControl(&initial, SE_DACL_PROTECTED, SE_DACL_PROTECTED) &&
          InitializeSecurityDescriptor(&replacement, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorControl(&replacement, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    ok(ret, "Large security descriptor initialization failed: %lu.\n", GetLastError());
    if (!ret) return;
    ret = AllocateAndInitializeSid(&authority, 5, SECURITY_NT_NON_UNIQUE, 0, 0, 0, 1, 0, 0, 0, &distinct_sid);
    ok(ret, "Distinct ACL test SID initialization failed: %lu.\n", GetLastError());
    if (!ret) return;
    ace_size = FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + GetLengthSid(sid);
    distinct_ace_size = FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + GetLengthSid(distinct_sid);
    GetSystemTimeAsFileTime(&now);
    memset(&basic, 0, sizeof(basic));
    basic.CreationTime.LowPart = now.dwLowDateTime;
    basic.CreationTime.HighPart = now.dwHighDateTime;
    basic.CreationTime.QuadPart -= (LONGLONG)24 * 60 * 60 * 10000000;
    for (kind = 0; kind < 2; ++kind)
    {
        winetest_push_context("large NTFS security %s", kind ? "directory" : "file");
        created = FALSE;
        path[0] = 0;
        open_flags = kind ? FILE_FLAG_BACKUP_SEMANTICS : 0;
        ret = GetTempFileNameA(temp, "lac", 0, path);
        ok(ret, "Large security reservation failed: %lu.\n", GetLastError());
        if (!ret) { path[0] = 0; goto cleanup; }
        ret = DeleteFileA(path);
        ok(ret, "Large security reservation cleanup failed: %lu.\n", GetLastError());
        if (!ret) goto cleanup;
        ret = strlen(path) + sizeof(":securitydata") < MAX_PATH;
        ok(ret, "Large security temporary path is too long.\n");
        if (!ret) goto cleanup;
        if (kind)
        {
            ret = CreateDirectoryA(path, &attributes);
            ok(ret, "Large security directory creation failed: %lu.\n", GetLastError());
            if (!ret) goto cleanup;
            created = TRUE;
        }
        handles[0] = CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 &attributes, kind ? OPEN_EXISTING : CREATE_NEW, open_flags, NULL);
        ok(handles[0] != INVALID_HANDLE_VALUE, "Large security base creation/open failed: %lu.\n", GetLastError());
        if (handles[0] == INVALID_HANDLE_VALUE) goto cleanup;
        created = TRUE;
        handles[1] = CreateFileA(path, READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, open_flags, NULL);
        ok(handles[1] != INVALID_HANDLE_VALUE, "Large security retained reader failed: %lu.\n", GetLastError());
        if (handles[1] == INVALID_HANDLE_VALUE) goto cleanup;
        sprintf(stream_path, "%s:securitydata", path);
        stream = CreateFileA(stream_path, GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW, open_flags, NULL);
        ok(stream != INVALID_HANDLE_VALUE, "Large security stream creation failed: %lu.\n", GetLastError());
        if (stream == INVALID_HANDLE_VALUE) goto cleanup;
        ret = WriteFile(stream, content, sizeof(content), &transferred, NULL);
        ok(ret && transferred == sizeof(content), "Large security initial data write failed: %lu, size %lu.\n", GetLastError(), transferred);
        if (!ret || transferred != sizeof(content)) goto cleanup;
        for (phase = 0; phase < ARRAY_SIZE(counts); ++phase)
        {
            winetest_push_context("replacement %lu ACEs", counts[phase]);
            denied = !(phase & 1);
            main_aces = denied ? 2 : 1;
            size = sizeof(*acl) + main_aces * ace_size + (counts[phase] - main_aces) * distinct_ace_size;
            acl = calloc(1, size);
            ok(!!acl, "Large ACL allocation failed.\n");
            if (!acl) { winetest_pop_context(); goto cleanup; }
            ret = InitializeAcl(acl, size, ACL_REVISION);
            for (i = 0; ret && i < counts[phase]; ++i)
            {
                if (!i && denied)
                    ret = AddAccessDeniedAceEx(acl, ACL_REVISION, 0, FILE_WRITE_DATA, sid);
                else if (i < main_aces)
                    ret = AddAccessAllowedAceEx(acl, ACL_REVISION, 0, FILE_ALL_ACCESS, sid);
                else
                {
                    *GetSidSubAuthority(distinct_sid, 4) = i + 1;
                    ret = AddAccessAllowedAceEx(acl, ACL_REVISION, 0, FILE_READ_DATA, distinct_sid);
                }
            }
            ok(ret, "Large ACL construction failed: %lu.\n", GetLastError());
            if (!ret) { winetest_pop_context(); goto cleanup; }
            ret = SetSecurityDescriptorDacl(&replacement, TRUE, acl, FALSE);
            ok(ret, "Large replacement descriptor failed: %lu.\n", GetLastError());
            if (!ret) { winetest_pop_context(); goto cleanup; }
            status = NtSetSecurityObject(handles[0], DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &replacement);
            ok(!status, "Large descriptor replacement returned %#lx.\n", (DWORD)status);
            if (status) { winetest_pop_context(); goto cleanup; }
            for (i = 0; i < ARRAY_SIZE(handles); ++i) check_ntfs_large_dacl(handles[i], acl);
            check_ntfs_large_dacl(stream, acl);
            status = NtSetInformationFile(handles[0], &io, &basic, sizeof(basic), FileBasicInformation);
            ok(!status, "Large security metadata update returned %#lx.\n", (DWORD)status);
            for (i = 0; i < ARRAY_SIZE(handles); ++i) check_ntfs_large_dacl(handles[i], acl);
            CloseHandle(stream);
            stream = INVALID_HANDLE_VALUE;
            for (i = 0; i < ARRAY_SIZE(handles); ++i)
            {
                CloseHandle(handles[i]);
                handles[i] = INVALID_HANDLE_VALUE;
            }
            for (i = 0; i < ARRAY_SIZE(handles); ++i)
            {
                handles[i] = CreateFileA(path, i ? READ_CONTROL : access,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         NULL, OPEN_EXISTING, open_flags, NULL);
                ok(handles[i] != INVALID_HANDLE_VALUE, "Large security cold reopen %lu failed: %lu.\n", i, GetLastError());
                if (handles[i] == INVALID_HANDLE_VALUE) { winetest_pop_context(); goto cleanup; }
                check_ntfs_large_dacl(handles[i], acl);
            }
            opened = CreateFileA(path, FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, open_flags, NULL);
            error = GetLastError();
            ok(denied ? opened == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED : opened != INVALID_HANDLE_VALUE,
               "Large security write access returned %p, error %lu, denied %d.\n", opened, error, denied);
            if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);
            stream = CreateFileA(stream_path, GENERIC_READ | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, open_flags, NULL);
            ok(stream != INVALID_HANDLE_VALUE, "Large security stream reopen failed: %lu.\n", GetLastError());
            if (stream == INVALID_HANDLE_VALUE) { winetest_pop_context(); goto cleanup; }
            check_ntfs_large_dacl(stream, acl);
            memset(readback, 0, sizeof(readback));
            ret = ReadFile(stream, readback, sizeof(readback), &transferred, NULL);
            ok(ret && transferred == sizeof(readback) && !memcmp(readback, content, sizeof(readback)),
               "Large security stream data differs, read %d, size %lu, error %lu.\n", ret, transferred, GetLastError());
            free(acl);
            acl = NULL;
            winetest_pop_context();
        }
cleanup:
        free(acl);
        acl = NULL;
        if (handles[0] != INVALID_HANDLE_VALUE)
        {
            status = NtSetSecurityObject(handles[0], DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &initial);
            ok(!status, "Large security cleanup descriptor restore returned %#lx.\n", (DWORD)status);
        }
        if (stream != INVALID_HANDLE_VALUE) { CloseHandle(stream); stream = INVALID_HANDLE_VALUE; }
        for (i = 0; i < ARRAY_SIZE(handles); ++i)
            if (handles[i] != INVALID_HANDLE_VALUE) { CloseHandle(handles[i]); handles[i] = INVALID_HANDLE_VALUE; }
        if (created)
        {
            ret = kind ? RemoveDirectoryA(path) : DeleteFileA(path);
            ok(ret, "Large security cleanup failed: %lu.\n", GetLastError());
        }
        else if (path[0]) DeleteFileA(path);
        winetest_pop_context();
    }
    FreeSid(distinct_sid);
}

static NTSTATUS set_ntfs_path_denial(HANDLE handle, PSID sid, DWORD denied)
{
    union { DWORD align; BYTE bytes[256]; } buffer;
    PACL acl = (PACL)buffer.bytes;
    SECURITY_DESCRIPTOR sd;
    BOOL ret;

    ret = InitializeAcl(acl, sizeof(buffer), ACL_REVISION);
    if (ret && denied) ret = AddAccessDeniedAceEx(acl, ACL_REVISION, 0, denied, sid);
    ret = ret && AddAccessAllowedAceEx(acl, ACL_REVISION, 0, FILE_ALL_ACCESS, sid) &&
          InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE) &&
          SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    ok(ret, "Path authorization descriptor failed: %lu.\n", GetLastError());
    if (!ret) return STATUS_INVALID_SECURITY_DESCR;
    return NtSetSecurityObject(handle, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &sd);
}

static void test_ntfs_metadata_streams(const char *path, BOOL directory)
{
    static const char *types[] = {"::$STANDARD_INFORMATION", "::$SECURITY_DESCRIPTOR", "::$ATTRIBUTE_LIST",
                                 "::$FILE_NAME", "::$INDEX_ROOT", "::$INDEX_ALLOCATION", "::$BITMAP", "::$EA",
                                 "::$EA_INFORMATION", "::$REPARSE_POINT", "::$LOGGED_UTILITY_STREAM", "::$DATA",
                                 ":$I30:$INDEX_ALLOCATION", ":other:$INDEX_ALLOCATION", ":$I30"};
    char stream_path[MAX_PATH];
    char byte = 0;
    DWORD i, access, errors[2], transferred, error, expected;
    HANDLE handle;
    BOOL ret;

    for (i = 0; i < ARRAY_SIZE(types); ++i)
    {
        if (strlen(path) + strlen(types[i]) + 1 > MAX_PATH) continue;
        sprintf(stream_path, "%s%s", path, types[i]);
        for (access = 0; access < 2; ++access)
        {
            handle = CreateFileA(stream_path, access ? FILE_WRITE_DATA : FILE_READ_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                 OPEN_EXISTING, directory ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL);
            errors[access] = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
            expected = i == 13 ? ERROR_INVALID_PARAMETER : i == 14 ? ERROR_FILE_NOT_FOUND :
                       ((!directory && i == 11) || (directory && (i == 5 || i == 12))) ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
            ok(errors[access] == expected, "Metadata stream %s %s %s returned %lu, expected %lu.\n",
               directory ? "directory" : "file", types[i], access ? "write" : "read", errors[access], expected);
            if (handle != INVALID_HANDLE_VALUE)
            {
                if (directory && (i == 5 || i == 12 || i == 14))
                {
                    transferred = 0;
                    ret = access ? WriteFile(handle, &byte, 1, &transferred, NULL) :
                                   ReadFile(handle, &byte, 1, &transferred, NULL);
                    error = ret ? ERROR_SUCCESS : GetLastError();
                    ok(!ret && error == ERROR_INVALID_FUNCTION && !transferred,
                       "Directory index %s %s I/O returned %d, error %lu, bytes %lu.\n",
                       types[i], access ? "write" : "read", ret, error, transferred);
                    trace("Directory index %s %s I/O returned %d, error %lu, bytes %lu.\n",
                          types[i], access ? "write" : "read", ret, error, transferred);
                }
                CloseHandle(handle);
            }
        }
        trace("Metadata stream %s %s read %lu, write %lu.\n", directory ? "directory" : "file", types[i], errors[0], errors[1]);
    }
}

static void test_ntfs_control_security(const char *path, HANDLE setter, HANDLE token, PACL initial_acl)
{
    void (WINAPI *set_access_mask)(SECURITY_INFORMATION, LPDWORD);
    static const SECURITY_INFORMATION flags[2][2] =
    {
        {PROTECTED_DACL_SECURITY_INFORMATION, UNPROTECTED_DACL_SECURITY_INFORMATION},
        {PROTECTED_SACL_SECURITY_INFORMATION, UNPROTECTED_SACL_SECURITY_INFORMATION}
    };
    SECURITY_DESCRIPTOR initial, empty;
    union { DWORD align; BYTE bytes[1024]; } buffer;
    union { DWORD align; BYTE bytes[sizeof(ACL)]; } acl_buffer;
    PACL sacl = (PACL)acl_buffer.bytes;
    TOKEN_PRIVILEGES privileges, previous;
    SECURITY_DESCRIPTOR_CONTROL control, before_control, protected_bit;
    SECURITY_INFORMATION information;
    HANDLE reader = INVALID_HANDLE_VALUE, security = INVALID_HANDLE_VALUE, handles[2], queried;
    DWORD kind, access, phase, api, size, revision, error, result, mapped_access;
    NTSTATUS status, query_status;
    BOOL ret, changed = FALSE;

    set_access_mask = (void *)GetProcAddress(GetModuleHandleA("advapi32.dll"), "SetSecurityAccessMask");
    ok(!!set_access_mask, "SetSecurityAccessMask export is missing.\n");
    ret = InitializeSecurityDescriptor(&initial, SECURITY_DESCRIPTOR_REVISION) &&
          InitializeSecurityDescriptor(&empty, SECURITY_DESCRIPTOR_REVISION) &&
          InitializeAcl(sacl, sizeof(acl_buffer), ACL_REVISION) &&
          SetSecurityDescriptorDacl(&initial, TRUE, initial_acl, FALSE) &&
          SetSecurityDescriptorSacl(&initial, TRUE, sacl, FALSE);
    ok(ret, "Control-only descriptor initialization failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    reader = CreateFileA(path, READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, 0, NULL);
    ok(reader != INVALID_HANDLE_VALUE, "Control-only reader open failed: %lu.\n", GetLastError());
    if (reader == INVALID_HANDLE_VALUE) goto done;
    memset(&privileges, 0, sizeof(privileges));
    memset(&previous, 0, sizeof(previous));
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    ret = LookupPrivilegeValueA(NULL, SE_SECURITY_NAME, &privileges.Privileges[0].Luid);
    ok(ret, "Control-only SACL privilege lookup failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ret = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(previous), &previous, &size);
    error = GetLastError();
    ok(ret && error == ERROR_SUCCESS, "Control-only SACL privilege enable returned %d, error %lu.\n", ret, error);
    if (!ret || error != ERROR_SUCCESS) goto done;
    changed = TRUE;
    security = CreateFileA(path, READ_CONTROL | ACCESS_SYSTEM_SECURITY,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    ok(security != INVALID_HANDLE_VALUE, "Control-only SACL handle open failed: %lu.\n", GetLastError());
    ret = AdjustTokenPrivileges(token, FALSE, &previous, 0, NULL, NULL);
    error = GetLastError();
    ok(ret && error == ERROR_SUCCESS, "Control-only SACL privilege restore returned %d, error %lu.\n", ret, error);
    if (!ret || error != ERROR_SUCCESS) goto done;
    changed = FALSE;
    for (kind = 0; kind < 2; ++kind)
    {
        if (kind && security == INVALID_HANDLE_VALUE) continue;
        information = kind ? SACL_SECURITY_INFORMATION : DACL_SECURITY_INFORMATION;
        protected_bit = kind ? SE_SACL_PROTECTED : SE_DACL_PROTECTED;
        queried = kind ? security : setter;
        handles[0] = reader;
        handles[1] = queried;
        for (access = 0; access < 2; ++access)
            for (phase = 0; phase < 2; ++phase)
              for (api = 0; api < 2; ++api)
            {
                if (!access && !api && set_access_mask)
                {
                    mapped_access = 0xdeadbeef;
                    set_access_mask(flags[kind][phase], &mapped_access);
                    ok(!mapped_access, "Control-only access mask flag %#lx returned %#lx.\n",
                       flags[kind][phase], mapped_access);
                    trace("Control-only access mask flag %#lx returned %#lx.\n", flags[kind][phase], mapped_access);
                }
                ret = SetSecurityDescriptorControl(&initial, protected_bit, phase ? protected_bit : 0) &&
                      SetSecurityDescriptorControl(&empty, protected_bit, phase ? 0 : protected_bit);
                ok(ret, "Control-only input control failed: %lu.\n", GetLastError());
                if (!ret) continue;
                status = NtSetSecurityObject(queried, information | flags[kind][phase], &initial);
                ok(!status, "Control-only initial ACL %lu returned %#lx.\n", kind, (DWORD)status);
                if (status) continue;
                before_control = 0;
                query_status = NtQuerySecurityObject(queried, information, buffer.bytes, sizeof(buffer), &size);
                ok(!query_status, "Control-only initial query returned %#lx.\n", (DWORD)query_status);
                if (query_status) continue;
                ret = GetSecurityDescriptorControl(buffer.bytes, &before_control, &revision);
                ok(ret, "Control-only initial control failed: %lu.\n", GetLastError());
                if (!ret) continue;
                ok((before_control & protected_bit) == (phase ? protected_bit : 0),
                   "Raw ACL setter used information flags instead of descriptor control: %#x.\n", before_control);
                if (api)
                    result = SetSecurityInfo(handles[access], SE_FILE_OBJECT, flags[kind][phase], NULL, NULL, NULL, NULL);
                else
                    result = NtSetSecurityObject(handles[access], flags[kind][phase], &empty);
                ok(!result, "Control-only api %lu kind %lu access %lu returned %#lx.\n",
                   api, kind, access, result);
                control = 0;
                query_status = NtQuerySecurityObject(queried, information, buffer.bytes, sizeof(buffer), &size);
                ok(!query_status, "Control-only result query returned %#lx.\n", (DWORD)query_status);
                if (!query_status)
                {
                    ret = GetSecurityDescriptorControl(buffer.bytes, &control, &revision);
                    ok(ret, "Control-only result control failed: %lu.\n", GetLastError());
                    ok(control == before_control, "Control-only call changed control from %#x to %#x.\n",
                       before_control, control);
                }
                trace("Control-only api %lu kind %lu access %lu flag %#lx returned %#lx, before %#x, after %#x.\n",
                      api, kind, access, flags[kind][phase], result, before_control, control);
            }
    }
done:
    if (changed)
    {
        ret = AdjustTokenPrivileges(token, FALSE, &previous, 0, NULL, NULL);
        error = GetLastError();
        ok(ret && error == ERROR_SUCCESS, "Control-only privilege cleanup returned %d, error %lu.\n", ret, error);
    }
    if (reader != INVALID_HANDLE_VALUE) CloseHandle(reader);
    if (security != INVALID_HANDLE_VALUE) CloseHandle(security);
}

static void test_ntfs_path_security(const char *temp, HANDLE token, PSID sid, PACL initial_acl)
{
    SECURITY_DESCRIPTOR initial;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), &initial, FALSE};
    TOKEN_PRIVILEGES privileges, previous_privileges;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING name;
    IO_STATUS_BLOCK io;
    HANDLE root = INVALID_HANDLE_VALUE, parent = INVALID_HANDLE_VALUE, leaf = INVALID_HANDLE_VALUE;
    HANDLE source = INVALID_HANDLE_VALUE, opened;
    char path[MAX_PATH] = "", parent_path[MAX_PATH] = "", leaf_path[MAX_PATH] = "";
    char source_path[MAX_PATH] = "", target_path[MAX_PATH] = "", link_path[MAX_PATH] = "", metadata_path[MAX_PATH] = "";
    DWORD error, size;
    NTSTATUS status;
    BOOL ret, root_created = FALSE, parent_created = FALSE, leaf_created = FALSE, source_created = FALSE;
    BOOL metadata_created = FALSE;
    BOOL moved = FALSE, linked = FALSE, replaced = FALSE, privilege_changed = FALSE;

    winetest_push_context("NTFS path authorization");
    ret = InitializeSecurityDescriptor(&initial, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorDacl(&initial, TRUE, initial_acl, FALSE) &&
          SetSecurityDescriptorControl(&initial, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    ok(ret, "Path authorization initial descriptor failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ret = GetTempFileNameA(temp, "pac", 0, path);
    ok(ret, "Path authorization reservation failed: %lu.\n", GetLastError());
    if (!ret) { path[0] = 0; goto done; }
    ret = DeleteFileA(path);
    ok(ret, "Path authorization reservation cleanup failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ret = strlen(path) + sizeof("\\parent\\linked") < MAX_PATH;
    ok(ret, "Path authorization temporary path is too long.\n");
    if (!ret) goto done;
    root_created = CreateDirectoryA(path, &attributes);
    ok(root_created, "Path authorization root creation failed: %lu.\n", GetLastError());
    if (!root_created) goto done;
    root = CreateFileA(path, FILE_ALL_ACCESS & ~DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(root != INVALID_HANDLE_VALUE, "Path authorization root open failed: %lu.\n", GetLastError());
    if (root == INVALID_HANDLE_VALUE) goto done;
    sprintf(parent_path, "%s\\parent", path);
    sprintf(leaf_path, "%s\\leaf", parent_path);
    sprintf(source_path, "%s\\source", path);
    sprintf(target_path, "%s\\moved", parent_path);
    sprintf(link_path, "%s\\linked", parent_path);
    sprintf(metadata_path, "%s\\metadata", path);
    parent_created = CreateDirectoryA(parent_path, &attributes);
    ok(parent_created, "Path authorization parent creation failed: %lu.\n", GetLastError());
    if (!parent_created) goto done;
    parent = CreateFileA(parent_path, FILE_ALL_ACCESS & ~DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(parent != INVALID_HANDLE_VALUE, "Path authorization parent open failed: %lu.\n", GetLastError());
    if (parent == INVALID_HANDLE_VALUE) goto done;
    leaf = CreateFileA(leaf_path, FILE_ALL_ACCESS, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       &attributes, CREATE_NEW, 0, NULL);
    ok(leaf != INVALID_HANDLE_VALUE, "Path authorization leaf creation failed: %lu.\n", GetLastError());
    if (leaf == INVALID_HANDLE_VALUE) goto done;
    leaf_created = TRUE;
    source = CreateFileA(source_path, FILE_ALL_ACCESS, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         &attributes, CREATE_NEW, 0, NULL);
    ok(source != INVALID_HANDLE_VALUE, "Path authorization source creation failed: %lu.\n", GetLastError());
    if (source == INVALID_HANDLE_VALUE) goto done;
    source_created = TRUE;
    metadata_created = CreateDirectoryA(metadata_path, &attributes);
    ok(metadata_created, "Metadata stream directory creation failed: %lu.\n", GetLastError());
    if (!metadata_created) goto done;

    moved = MoveFileExA(source_path, target_path, 0);
    ok(moved, "Authorized rename to destination failed: %lu.\n", GetLastError());
    if (!moved) goto done;
    ret = MoveFileExA(target_path, source_path, 0);
    ok(ret, "Authorized rename back to source failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    moved = FALSE;
    linked = CreateHardLinkA(link_path, source_path, NULL);
    ok(linked, "Authorized hard link to destination failed: %lu.\n", GetLastError());
    if (!linked) goto done;
    ret = DeleteFileA(link_path);
    ok(ret, "Authorized hard link removal failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    linked = FALSE;
    test_ntfs_metadata_streams(source_path, FALSE);
    test_ntfs_metadata_streams(metadata_path, TRUE);
    test_ntfs_control_security(source_path, source, token, initial_acl);
    status = set_ntfs_path_denial(source, sid, 0);
    ok(!status, "Path source control-test restore returned %#lx.\n", (DWORD)status);
    if (status) goto done;

    status = set_ntfs_path_denial(parent, sid, FILE_TRAVERSE);
    ok(!status, "Parent traverse denial returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    opened = CreateFileA(leaf_path, FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, 0, NULL);
    error = GetLastError();
    ok(opened == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED,
       "Disabled-privilege traversal returned %p, error %lu.\n", opened, error);
    if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);
    RtlInitUnicodeString(&name, L"leaf");
    InitializeObjectAttributes(&attr, &name, OBJ_CASE_INSENSITIVE, parent, NULL);
    opened = NULL;
    status = NtOpenFile(&opened, FILE_READ_DATA, &attr, &io,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_NON_DIRECTORY_FILE);
    ok(status == STATUS_ACCESS_DENIED,
       "Relative traversal using previously granted parent handle returned %#lx.\n", (DWORD)status);
    if (!status) CloseHandle(opened);
    memset(&privileges, 0, sizeof(privileges));
    memset(&previous_privileges, 0, sizeof(previous_privileges));
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    ret = LookupPrivilegeValueA(NULL, SE_CHANGE_NOTIFY_NAME, &privileges.Privileges[0].Luid);
    ok(ret, "Traverse privilege lookup failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    size = sizeof(previous_privileges);
    SetLastError(0xdeadbeef);
    ret = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(previous_privileges), &previous_privileges, &size);
    error = GetLastError();
    ok(ret && error == ERROR_SUCCESS, "Enabling private traverse privilege returned %d, error %lu.\n", ret, error);
    if (!ret || error != ERROR_SUCCESS) goto done;
    privilege_changed = TRUE;
    opened = CreateFileA(leaf_path, FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, 0, NULL);
    ok(opened != INVALID_HANDLE_VALUE, "Enabled traverse privilege did not permit leaf read: %lu.\n", GetLastError());
    if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);
    status = set_ntfs_path_denial(leaf, sid, FILE_READ_DATA);
    ok(!status, "Leaf read denial returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    opened = CreateFileA(leaf_path, FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, 0, NULL);
    error = GetLastError();
    ok(opened == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED,
       "Traverse privilege bypassed leaf read denial: %p, error %lu.\n", opened, error);
    if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);
    status = set_ntfs_path_denial(leaf, sid, 0);
    ok(!status, "Leaf read restore returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    ret = AdjustTokenPrivileges(token, FALSE, &previous_privileges, 0, NULL, NULL);
    error = GetLastError();
    ok(ret && error == ERROR_SUCCESS, "Disabling private traverse privilege returned %d, error %lu.\n", ret, error);
    if (!ret || error != ERROR_SUCCESS) goto done;
    privilege_changed = FALSE;
    opened = CreateFileA(leaf_path, FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, 0, NULL);
    error = GetLastError();
    ok(opened == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED,
       "Restored disabled-privilege traversal returned %p, error %lu.\n", opened, error);
    if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);

    status = set_ntfs_path_denial(parent, sid, 0);
    ok(!status, "Relative parent traverse restore returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    status = set_ntfs_path_denial(root, sid, FILE_TRAVERSE);
    ok(!status, "Relative ancestor traverse denial returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    opened = CreateFileA(leaf_path, FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, 0, NULL);
    error = GetLastError();
    ok(opened == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED,
       "Absolute traversal through denied ancestor returned %p, error %lu.\n", opened, error);
    if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);
    RtlInitUnicodeString(&name, L"leaf");
    InitializeObjectAttributes(&attr, &name, OBJ_CASE_INSENSITIVE, parent, NULL);
    opened = NULL;
    status = NtOpenFile(&opened, FILE_READ_DATA, &attr, &io,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_NON_DIRECTORY_FILE);
    ok(!status, "Relative traversal beneath denied ancestor returned %#lx.\n", (DWORD)status);
    if (!status) CloseHandle(opened);
    RtlInitUnicodeString(&name, L"parent\\leaf");
    InitializeObjectAttributes(&attr, &name, OBJ_CASE_INSENSITIVE, root, NULL);
    opened = NULL;
    status = NtOpenFile(&opened, FILE_READ_DATA, &attr, &io,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_NON_DIRECTORY_FILE);
    ok(status == STATUS_ACCESS_DENIED, "Relative traversal from denied root returned %#lx.\n", (DWORD)status);
    if (!status) CloseHandle(opened);
    status = set_ntfs_path_denial(root, sid, 0);
    ok(!status, "Relative ancestor traverse restore returned %#lx.\n", (DWORD)status);
    if (status) goto done;

    status = set_ntfs_path_denial(parent, sid, FILE_ADD_FILE);
    ok(!status, "Destination add-file denial returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    moved = MoveFileExA(source_path, target_path, 0);
    error = GetLastError();
    ok(!moved && error == ERROR_ACCESS_DENIED, "Rename under destination add denial returned %d, error %lu.\n", moved, error);
    if (moved) goto done;
    linked = CreateHardLinkA(link_path, source_path, NULL);
    error = GetLastError();
    ok(!linked && error == ERROR_ACCESS_DENIED, "Hard link under destination add denial returned %d, error %lu.\n", linked, error);
    status = set_ntfs_path_denial(parent, sid, FILE_DELETE_CHILD);
    ok(!status, "Destination delete-child denial returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    status = set_ntfs_path_denial(leaf, sid, DELETE);
    ok(!status, "Replacement target delete denial returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    CloseHandle(leaf);
    leaf = INVALID_HANDLE_VALUE;
    replaced = MoveFileExA(source_path, leaf_path, MOVEFILE_REPLACE_EXISTING);
    error = GetLastError();
    ok(!replaced && error == ERROR_ACCESS_DENIED,
       "Rename replacement under target/parent delete denial returned %d, error %lu.\n", replaced, error);
    if (replaced) goto done;
    status = set_ntfs_path_denial(parent, sid, 0);
    ok(!status, "Replacement parent delete-child grant returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    replaced = MoveFileExA(source_path, leaf_path, MOVEFILE_REPLACE_EXISTING);
    ok(replaced, "Parent delete-child grant did not permit replacement: %lu.\n", GetLastError());
done:
    if (privilege_changed)
    {
        ret = AdjustTokenPrivileges(token, FALSE, &previous_privileges, 0, NULL, NULL);
        ok(ret && GetLastError() == ERROR_SUCCESS, "Path authorization privilege cleanup failed: %lu.\n", GetLastError());
    }
    if (root != INVALID_HANDLE_VALUE)
    {
        status = set_ntfs_path_denial(root, sid, 0);
        ok(!status, "Path authorization root restore returned %#lx.\n", (DWORD)status);
    }
    if (parent != INVALID_HANDLE_VALUE)
    {
        status = set_ntfs_path_denial(parent, sid, 0);
        ok(!status, "Path authorization parent restore returned %#lx.\n", (DWORD)status);
    }
    if (leaf_created && leaf == INVALID_HANDLE_VALUE && !replaced)
    {
        leaf = CreateFileA(leaf_path, WRITE_DAC, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
        ok(leaf != INVALID_HANDLE_VALUE, "Path authorization leaf cleanup reopen failed: %lu.\n", GetLastError());
    }
    if (leaf != INVALID_HANDLE_VALUE)
    {
        status = set_ntfs_path_denial(leaf, sid, 0);
        ok(!status, "Path authorization leaf restore returned %#lx.\n", (DWORD)status);
        CloseHandle(leaf);
    }
    if (source != INVALID_HANDLE_VALUE) CloseHandle(source);
    if (linked) ok(DeleteFileA(link_path), "Path authorization link cleanup failed: %lu.\n", GetLastError());
    if (leaf_created) ok(DeleteFileA(leaf_path), "Path authorization leaf cleanup failed: %lu.\n", GetLastError());
    if (source_created && !replaced)
        ok(DeleteFileA(moved ? target_path : source_path), "Path authorization source cleanup failed: %lu.\n", GetLastError());
    if (parent != INVALID_HANDLE_VALUE) CloseHandle(parent);
    if (parent_created) ok(RemoveDirectoryA(parent_path), "Path authorization parent cleanup failed: %lu.\n", GetLastError());
    if (metadata_created) ok(RemoveDirectoryA(metadata_path), "Metadata stream directory cleanup failed: %lu.\n", GetLastError());
    if (root != INVALID_HANDLE_VALUE) CloseHandle(root);
    if (root_created) ok(RemoveDirectoryA(path), "Path authorization root cleanup failed: %lu.\n", GetLastError());
    else if (path[0]) DeleteFileA(path);
    winetest_pop_context();
}

static void test_ntfs_propagation_access(const char *temp, PSID sid, PACL initial_acl, PACL denied_acl)
{
    static const DWORD accesses[] = {WRITE_DAC, READ_CONTROL | WRITE_DAC,
        READ_CONTROL | WRITE_DAC | FILE_LIST_DIRECTORY, MAXIMUM_ALLOWED,
        READ_CONTROL | WRITE_DAC | FILE_LIST_DIRECTORY, READ_CONTROL | WRITE_DAC | FILE_LIST_DIRECTORY,
        FILE_ALL_ACCESS};
    SECURITY_DESCRIPTOR initial, blocked;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), &initial, FALSE};
    union { DWORD align; BYTE bytes[SECURITY_MAX_SID_SIZE]; } owner_buffer;
    union { DWORD align; BYTE bytes[256]; } acl_buffer;
    union { DWORD align; BYTE bytes[1024]; } sd_buffer;
    PACL blocked_acl = (PACL)acl_buffer.bytes, acl;
    SECURITY_DESCRIPTOR_CONTROL control;
    FILE_ACCESS_INFORMATION access_info;
    FILE_MODE_INFORMATION mode_info;
    OBJECT_BASIC_INFORMATION object_info;
    IO_STATUS_BLOCK io;
    HANDLE parent = INVALID_HANDLE_VALUE, setter = INVALID_HANDLE_VALUE, children[2], opened;
    char path[MAX_PATH] = "", files[2][MAX_PATH];
    DWORD size = sizeof(owner_buffer), phase, i, result, error, revision;
    NTSTATUS status;
    BOOL ret, created = FALSE, child_created[2], present, defaulted;

    ret = CreateWellKnownSid(WinCreatorOwnerRightsSid, NULL, owner_buffer.bytes, &size) &&
          InitializeAcl(blocked_acl, sizeof(acl_buffer), ACL_REVISION) &&
          AddAccessDeniedAceEx(blocked_acl, ACL_REVISION, 0, WRITE_DAC, owner_buffer.bytes) &&
          AddAccessAllowedAceEx(blocked_acl, ACL_REVISION, 0, FILE_ALL_ACCESS, sid) &&
          InitializeSecurityDescriptor(&blocked, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorDacl(&blocked, TRUE, blocked_acl, FALSE) &&
          InitializeSecurityDescriptor(&initial, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorDacl(&initial, TRUE, initial_acl, FALSE) &&
          SetSecurityDescriptorControl(&initial, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    ok(ret, "Propagation access descriptors failed: %lu.\n", GetLastError());
    if (!ret) return;
    ret = GetTempFileNameA(temp, "pap", 0, path);
    ok(ret, "Propagation access reservation failed: %lu.\n", GetLastError());
    if (!ret) return;
    ret = DeleteFileA(path);
    ok(ret, "Propagation access reservation cleanup failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    if (strlen(path) + sizeof("\\sibling") > MAX_PATH) goto done;
    created = CreateDirectoryA(path, &attributes);
    ok(created, "Propagation access directory creation failed: %lu.\n", GetLastError());
    if (!created) goto done;
    parent = CreateFileA(path, READ_CONTROL | WRITE_DAC, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(parent != INVALID_HANDLE_VALUE, "Propagation access parent open failed: %lu.\n", GetLastError());
    if (parent == INVALID_HANDLE_VALUE) goto done;
    sprintf(files[0], "%s\\child", path);
    sprintf(files[1], "%s\\sibling", path);
    for (phase = 0; phase < ARRAY_SIZE(accesses); ++phase)
    {
        winetest_push_context("propagation access phase %lu", phase);
        children[0] = children[1] = INVALID_HANDLE_VALUE;
        child_created[0] = child_created[1] = FALSE;
        status = NtSetSecurityObject(parent, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &initial);
        ok(!status, "Propagation access parent reset returned %#lx.\n", (DWORD)status);
        if (status) goto phase_done;
        for (i = 0; i < ARRAY_SIZE(children); ++i)
        {
            children[i] = CreateFileA(files[i], READ_CONTROL | WRITE_DAC,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      NULL, CREATE_NEW, 0, NULL);
            child_created[i] = children[i] != INVALID_HANDLE_VALUE;
            ok(child_created[i], "Propagation access child %lu creation failed: %lu.\n", i, GetLastError());
            if (!child_created[i]) goto phase_done;
        }
        if (phase == 5)
        {
            status = NtSetSecurityObject(children[0], DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, &blocked);
            ok(!status, "Propagation child write-DAC denial returned %#lx.\n", (DWORD)status);
            if (status) goto phase_done;
            opened = CreateFileA(files[0], WRITE_DAC, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, 0, NULL);
            error = GetLastError();
            ok(opened == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED,
               "Propagation blocked-child control returned %p, error %lu.\n", opened, error);
            if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);
        }
        setter = CreateFileA(path, accesses[phase], phase == 4 ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        ok(setter != INVALID_HANDLE_VALUE, "Propagation access setter %#lx open failed: %lu.\n", accesses[phase], GetLastError());
        if (setter == INVALID_HANDLE_VALUE) goto phase_done;
        memset(&access_info, 0, sizeof(access_info));
        memset(&mode_info, 0, sizeof(mode_info));
        memset(&object_info, 0, sizeof(object_info));
        status = NtQueryInformationFile(setter, &io, &access_info, sizeof(access_info), FileAccessInformation);
        ok(!status, "Propagation setter access query returned %#lx.\n", (DWORD)status);
        status = NtQueryInformationFile(setter, &io, &mode_info, sizeof(mode_info), FileModeInformation);
        ok(!status, "Propagation setter mode query returned %#lx.\n", (DWORD)status);
        status = NtQueryObject(setter, ObjectBasicInformation, &object_info, sizeof(object_info), &size);
        ok(!status, "Propagation setter object query returned %#lx.\n", (DWORD)status);
        trace("Propagation access requested %#lx file %#lx object %#lx attributes %#lx mode %#lx.\n",
              accesses[phase], access_info.AccessFlags, object_info.GrantedAccess, object_info.Attributes, mode_info.Mode);
        result = SetSecurityInfo(setter, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                  NULL, NULL, denied_acl, NULL);
        trace("Propagation setter %#lx exclusive %d blocked %d returned %lu.\n", accesses[phase], phase == 4, phase == 5, result);
        ok(result == (phase ? ERROR_SUCCESS : ERROR_ACCESS_DENIED),
           "Propagation setter phase %lu returned %lu.\n", phase, result);
        CloseHandle(setter);
        setter = INVALID_HANDLE_VALUE;
        for (i = 0; i < ARRAY_SIZE(children); ++i)
        {
            control = 0;
            acl = NULL;
            status = NtQuerySecurityObject(children[i], DACL_SECURITY_INFORMATION, sd_buffer.bytes, sizeof(sd_buffer), &size);
            ok(!status, "Propagation access child %lu query returned %#lx.\n", i, (DWORD)status);
            if (!status)
            {
                ret = GetSecurityDescriptorControl(sd_buffer.bytes, &control, &revision) &&
                      GetSecurityDescriptorDacl(sd_buffer.bytes, &present, &acl, &defaulted);
                ok(ret, "Propagation access child %lu descriptor failed: %lu.\n", i, GetLastError());
            }
            opened = CreateFileA(files[i], FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL, OPEN_EXISTING, 0, NULL);
            error = opened == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
            trace("Propagation child %lu control %#x ACEs %u write %lu.\n", i, control, acl ? acl->AceCount : 0, error);
            ok(error == ((!phase || phase == 3 || phase == 4 || phase == 6 || (phase == 5 && !i)) ? ERROR_SUCCESS : ERROR_ACCESS_DENIED),
               "Propagation phase %lu child %lu write returned %lu.\n", phase, i, error);
            if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);
        }
phase_done:
        if (setter != INVALID_HANDLE_VALUE) { CloseHandle(setter); setter = INVALID_HANDLE_VALUE; }
        for (i = 0; i < ARRAY_SIZE(children); ++i)
        {
            if (children[i] != INVALID_HANDLE_VALUE)
            {
                status = NtSetSecurityObject(children[i], DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &initial);
                ok(!status, "Propagation access child %lu cleanup reset returned %#lx.\n", i, (DWORD)status);
                CloseHandle(children[i]);
            }
            if (child_created[i])
            {
                ret = DeleteFileA(files[i]);
                ok(ret, "Propagation access child %lu cleanup failed: %lu.\n", i, GetLastError());
            }
        }
        winetest_pop_context();
    }
done:
    if (parent != INVALID_HANDLE_VALUE) CloseHandle(parent);
    if (created)
    {
        ret = RemoveDirectoryA(path);
        ok(ret, "Propagation access directory cleanup failed: %lu.\n", GetLastError());
    }
    else if (path[0]) DeleteFileA(path);
}

static void test_ntfs_nested_propagation(const char *temp, PACL initial_acl, PACL denied_acl)
{
    SECURITY_DESCRIPTOR initial;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), &initial, FALSE};
    HANDLE directories[3] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    HANDLE leaves[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE}, opened;
    char paths[3][MAX_PATH] = {{0}}, files[2][MAX_PATH];
    BOOL created[3] = {FALSE, FALSE, FALSE}, leaf_created[2] = {FALSE, FALSE};
    DWORD i, phase, error, result;
    BOOL ret, denied;

    winetest_push_context("nested NTFS propagation");
    ret = InitializeSecurityDescriptor(&initial, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorDacl(&initial, TRUE, initial_acl, FALSE) &&
          SetSecurityDescriptorControl(&initial, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    ok(ret, "Nested propagation descriptor initialization failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ret = GetTempFileNameA(temp, "nap", 0, paths[0]);
    ok(ret, "Nested propagation reservation failed: %lu.\n", GetLastError());
    if (!ret) { paths[0][0] = 0; goto done; }
    ret = DeleteFileA(paths[0]);
    ok(ret, "Nested propagation reservation cleanup failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    if (strlen(paths[0]) + sizeof("\\protected\\leaf") > MAX_PATH) goto done;
    sprintf(paths[1], "%s\\inherited", paths[0]);
    sprintf(paths[2], "%s\\protected", paths[0]);
    for (i = 0; i < ARRAY_SIZE(directories); ++i)
    {
        created[i] = CreateDirectoryA(paths[i], i == 1 ? NULL : &attributes);
        ok(created[i], "Nested directory %lu creation failed: %lu.\n", i, GetLastError());
        if (!created[i]) goto done;
        directories[i] = CreateFileA(paths[i], READ_CONTROL | WRITE_DAC | FILE_LIST_DIRECTORY,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        ok(directories[i] != INVALID_HANDLE_VALUE, "Nested directory %lu open failed: %lu.\n", i, GetLastError());
        if (directories[i] == INVALID_HANDLE_VALUE) goto done;
    }
    for (i = 0; i < ARRAY_SIZE(leaves); ++i)
    {
        sprintf(files[i], "%s\\leaf", paths[i + 1]);
        leaves[i] = CreateFileA(files[i], READ_CONTROL | WRITE_DAC,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW, 0, NULL);
        leaf_created[i] = leaves[i] != INVALID_HANDLE_VALUE;
        ok(leaf_created[i], "Nested leaf %lu creation failed: %lu.\n", i, GetLastError());
        if (!leaf_created[i]) goto done;
    }
    for (phase = 0; phase < 3; ++phase)
    {
        if (phase)
        {
            result = SetSecurityInfo(directories[0], SE_FILE_OBJECT,
                                      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                      NULL, NULL, phase == 1 ? denied_acl : initial_acl, NULL);
            ok(result == ERROR_SUCCESS, "Nested parent phase %lu returned %lu.\n", phase, result);
            if (result) goto done;
        }
        for (i = 0; i < ARRAY_SIZE(leaves); ++i)
        {
            denied = phase == 1 && !i;
            opened = CreateFileA(files[i], FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, 0, NULL);
            ok(opened != INVALID_HANDLE_VALUE, "Nested phase %lu leaf %lu read failed: %lu.\n", phase, i, GetLastError());
            if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);
            opened = CreateFileA(files[i], FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, 0, NULL);
            error = GetLastError();
            ok(denied ? opened == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED : opened != INVALID_HANDLE_VALUE,
               "Nested phase %lu leaf %lu write returned %p, error %lu, expected denied %d.\n", phase, i, opened, error, denied);
            if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);
        }
    }
done:
    for (i = 0; i < ARRAY_SIZE(leaves); ++i)
    {
        if (leaves[i] != INVALID_HANDLE_VALUE) CloseHandle(leaves[i]);
        if (leaf_created[i])
        {
            ret = DeleteFileA(files[i]);
            ok(ret, "Nested leaf %lu cleanup failed: %lu.\n", i, GetLastError());
        }
    }
    for (i = ARRAY_SIZE(directories); i-- > 0;)
    {
        if (directories[i] != INVALID_HANDLE_VALUE) CloseHandle(directories[i]);
        if (created[i])
        {
            ret = RemoveDirectoryA(paths[i]);
            ok(ret, "Nested directory %lu cleanup failed: %lu.\n", i, GetLastError());
        }
    }
    if (!created[0] && paths[0][0]) DeleteFileA(paths[0]);
    winetest_pop_context();
}

static void test_ntfs_directory_acl_coherence(const char *temp, const char *filesystem, DWORD volume_flags,
                                             HANDLE token, HANDLE previous_token, PSID sid,
                                             PACL initial_acl, PACL denied_acl)
{
    static const char *names[] = {"existing", "inherited", "blocked", "restored"};
    static const char ea_name[] = "ACLCOHERENCE";
    static const BYTE ea_value[] = {0x17, 0x29, 0x43, 0x61};
    static const char stream_value[] = "directory stream ACL coherence";
    SECURITY_DESCRIPTOR initial_sd, denied_sd, add_denied_sd;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), &initial_sd, FALSE};
    union { DWORD align; BYTE bytes[256]; } acl_buffer, ea_buffer;
    PACL add_denied_acl = (PACL)acl_buffer.bytes;
    FILE_FULL_EA_INFORMATION *ea = (void *)ea_buffer.bytes;
    FILE_BASIC_INFORMATION basic;
    FILE_STANDARD_INFORMATION standard;
    FILETIME creation, actual_creation;
    LARGE_INTEGER position, stream_size;
    IO_STATUS_BLOCK io;
    NTSTATUS status;
    char path[MAX_PATH] = "", files[ARRAY_SIZE(names)][MAX_PATH], stream_path[MAX_PATH];
    char stream_case[MAX_PATH], denied_stream[MAX_PATH], readback[sizeof(stream_value)];
    HANDLE parents[3] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    HANDLE children[ARRAY_SIZE(names)] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    HANDLE stream = INVALID_HANDLE_VALUE, reopened;
    DWORD parent_access = READ_CONTROL | WRITE_DAC | FILE_LIST_DIRECTORY | FILE_ADD_FILE |
                          FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | FILE_READ_EA | FILE_WRITE_EA;
    DWORD i, j, ea_size, transferred = 0, error;
    BOOL ret, impersonating = FALSE, directory_created = FALSE;
    BOOL child_created[ARRAY_SIZE(names)] = {FALSE, FALSE, FALSE, FALSE};
    BOOL case_stream_created = FALSE, denied_stream_created = FALSE;

    if (lstrcmpiA(filesystem, "NTFS"))
    {
        win_skip("Directory ACL coherence requires NTFS, found %s.\n", filesystem);
        return;
    }
    if (volume_flags & FILE_READ_ONLY_VOLUME)
    {
        win_skip("NTFS directory ACL coherence needs a writable volume.\n");
        return;
    }
    winetest_push_context("NTFS directory ACL coherence");
    trace("Testing NTFS security primitives independently of FILE_PERSISTENT_ACLS (%#lx).\n", volume_flags);
    memset(files, 0, sizeof(files));
    ret = InitializeSecurityDescriptor(&initial_sd, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorDacl(&initial_sd, TRUE, initial_acl, FALSE) &&
          SetSecurityDescriptorControl(&initial_sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED) &&
          InitializeSecurityDescriptor(&denied_sd, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorDacl(&denied_sd, TRUE, denied_acl, FALSE) &&
          SetSecurityDescriptorControl(&denied_sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED) &&
          InitializeAcl(add_denied_acl, sizeof(acl_buffer), ACL_REVISION) &&
          AddAccessDeniedAceEx(add_denied_acl, ACL_REVISION, 0, FILE_ADD_FILE, sid) &&
          AddAccessAllowedAceEx(add_denied_acl, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE, FILE_ALL_ACCESS, sid) &&
          InitializeSecurityDescriptor(&add_denied_sd, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorDacl(&add_denied_sd, TRUE, add_denied_acl, FALSE) &&
          SetSecurityDescriptorControl(&add_denied_sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    ok(ret, "Directory coherence descriptors failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ret = SetThreadToken(NULL, token);
    ok(ret, "Directory coherence impersonation failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    impersonating = TRUE;
    test_ntfs_public_inheritance(temp, token);
    test_ntfs_large_security(temp, sid, initial_acl);
    test_ntfs_path_security(temp, token, sid, initial_acl);
    test_ntfs_propagation_access(temp, sid, initial_acl, denied_acl);
    test_ntfs_nested_propagation(temp, initial_acl, denied_acl);
    ret = GetTempFileNameA(temp, "nac", 0, path);
    ok(ret, "Directory coherence reservation failed: %lu.\n", GetLastError());
    if (!ret) { path[0] = 0; goto done; }
    ret = DeleteFileA(path);
    ok(ret, "Directory coherence reservation cleanup failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ret = strlen(path) + sizeof(":deniedcoherence") < MAX_PATH;
    ok(ret, "Temporary path is too long for directory coherence.\n");
    if (!ret) goto done;
    ret = CreateDirectoryA(path, &attributes);
    ok(ret, "Directory coherence creation failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    directory_created = TRUE;
    for (i = 0; i < ARRAY_SIZE(names); ++i) sprintf(files[i], "%s\\%s", path, names[i]);
    for (i = 0; i < 2; ++i)
    {
        parents[i] = CreateFileA(path, parent_access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        ok(parents[i] != INVALID_HANDLE_VALUE, "Retained parent %lu open failed: %lu.\n", i, GetLastError());
        if (parents[i] == INVALID_HANDLE_VALUE) goto done;
    }
    children[0] = CreateFileA(files[0], READ_CONTROL | WRITE_DAC | DELETE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW, 0, NULL);
    ok(children[0] != INVALID_HANDLE_VALUE, "Existing child creation failed: %lu.\n", GetLastError());
    if (children[0] == INVALID_HANDLE_VALUE) goto done;
    child_created[0] = TRUE;
    check_ntfs_directory_existing_child(parents[1]);
    sprintf(stream_path, "%s:aclcoherence", path);
    sprintf(stream_case, "%s:ACLCOHERENCE", path);
    sprintf(denied_stream, "%s:deniedcoherence", path);
    stream = CreateFileA(stream_path, GENERIC_READ | GENERIC_WRITE | READ_CONTROL | WRITE_DAC,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW,
                         FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(stream != INVALID_HANDLE_VALUE, "Retained directory stream creation failed: %lu.\n", GetLastError());
    if (stream != INVALID_HANDLE_VALUE)
    {
        memset(&standard, 0, sizeof(standard));
        status = NtQueryInformationFile(stream, &io, &standard, sizeof(standard), FileStandardInformation);
        ok(!status, "Retained directory stream standard query returned %#lx.\n", (DWORD)status);
        if (!status)
        {
            trace("Directory stream initial EOF %I64d, directory %u.\n",
                  standard.EndOfFile.QuadPart, standard.Directory);
            ok(!standard.Directory, "Named directory stream is reported as a directory.\n");
        }
        reopened = CreateFileA(stream_case, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, CREATE_NEW, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        error = GetLastError();
        ok(reopened == INVALID_HANDLE_VALUE && error == ERROR_FILE_EXISTS,
           "Case-variant stream creation returned %p, error %lu.\n", reopened, error);
        if (reopened != INVALID_HANDLE_VALUE)
        {
            case_stream_created = TRUE;
            CloseHandle(reopened);
        }
    }
    warm_ntfs_directory_lookup(files[0]);
    status = NtSetSecurityObject(parents[0], DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &denied_sd);
    ok(!status, "Direct parent DACL replacement returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    children[1] = CreateFileA(files[1], READ_CONTROL | WRITE_DAC | DELETE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW, 0, NULL);
    ok(children[1] != INVALID_HANDLE_VALUE, "New inherited child creation failed: %lu.\n", GetLastError());
    if (children[1] != INVALID_HANDLE_VALUE)
    {
        child_created[1] = TRUE;
        check_ntfs_directory_dacl(children[1], denied_acl, TRUE);
        check_ntfs_child_access(children[1], files[1], token);
    }
    parents[2] = CreateFileA(path, READ_CONTROL | FILE_READ_ATTRIBUTES | FILE_READ_EA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(parents[2] != INVALID_HANDLE_VALUE, "Fresh parent open failed: %lu.\n", GetLastError());
    for (i = 0; i < ARRAY_SIZE(parents); ++i)
    {
        if (parents[i] == INVALID_HANDLE_VALUE) continue;
        winetest_push_context("parent DACL retained/fresh handle %lu", i);
        check_ntfs_directory_dacl(parents[i], denied_acl, FALSE);
        winetest_pop_context();
    }
    reopened = CreateFileA(files[0], FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
    ok(reopened != INVALID_HANDLE_VALUE, "Existing child disappeared or inherited a direct parent update: %lu.\n", GetLastError());
    if (reopened != INVALID_HANDLE_VALUE) CloseHandle(reopened);
    check_ntfs_directory_dacl(children[0], initial_acl, TRUE);
    memset(&basic, 0, sizeof(basic));
    GetSystemTimeAsFileTime(&creation);
    basic.CreationTime.LowPart = creation.dwLowDateTime;
    basic.CreationTime.HighPart = creation.dwHighDateTime;
    basic.CreationTime.QuadPart -= (LONGLONG)24 * 60 * 60 * 10000000;
    creation.dwLowDateTime = basic.CreationTime.LowPart;
    creation.dwHighDateTime = basic.CreationTime.HighPart;
    status = NtSetInformationFile(parents[1], &io, &basic, sizeof(basic), FileBasicInformation);
    ok(!status, "Retained parent basic information set returned %#lx.\n", (DWORD)status);
    reopen_ntfs_directory_read_handle(&parents[2], path);
    for (i = 0; i < ARRAY_SIZE(parents); ++i)
    {
        if (parents[i] == INVALID_HANDLE_VALUE) continue;
        winetest_push_context("basic retained/fresh handle %lu", i);
        if (!status) check_ntfs_directory_basic(parents[i], basic.CreationTime);
        check_ntfs_directory_dacl(parents[i], denied_acl, FALSE);
        winetest_pop_context();
    }
    memset(&ea_buffer, 0, sizeof(ea_buffer));
    ea->EaNameLength = sizeof(ea_name) - 1;
    ea->EaValueLength = sizeof(ea_value);
    memcpy(ea->EaName, ea_name, sizeof(ea_name));
    memcpy(ea->EaName + sizeof(ea_name), ea_value, sizeof(ea_value));
    ea_size = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + sizeof(ea_name) + sizeof(ea_value);
    for (j = 0; j < 2; ++j)
    {
        ea->EaName[sizeof(ea_name)] = ea_value[0] + j;
        status = NtSetEaFile(parents[1], &io, ea, ea_size);
        ok(!status, "Retained parent EA write %lu returned %#lx.\n", j, (DWORD)status);
        reopen_ntfs_directory_read_handle(&parents[2], path);
        for (i = 0; i < ARRAY_SIZE(parents); ++i)
        {
            if (parents[i] == INVALID_HANDLE_VALUE) continue;
            winetest_push_context("EA write %lu retained/fresh handle %lu", j, i);
            if (!status) check_ntfs_directory_ea(parents[i], ea, ea_size);
            check_ntfs_directory_dacl(parents[i], denied_acl, FALSE);
            winetest_pop_context();
        }
    }
    if (stream != INVALID_HANDLE_VALUE)
    {
        stream_size.QuadPart = 0;
        ret = WriteFile(stream, stream_value, sizeof(stream_value), &transferred, NULL);
        ok(ret && transferred == sizeof(stream_value), "Retained stream write returned %d, size %lu, error %lu.\n",
           ret, transferred, GetLastError());
        position.QuadPart = 8192;
        ret = SetFilePointerEx(stream, position, NULL, FILE_BEGIN) && SetEndOfFile(stream);
        ok(ret, "Retained stream EOF set failed: %lu.\n", GetLastError());
        ret = GetFileSizeEx(stream, &stream_size);
        ok(ret && stream_size.QuadPart == position.QuadPart, "Retained stream size %I64d, error %lu.\n",
           stream_size.QuadPart, GetLastError());
        position.QuadPart = 0;
        ret = SetFilePointerEx(stream, position, NULL, FILE_BEGIN) &&
              ReadFile(stream, readback, sizeof(readback), &transferred, NULL);
        ok(ret && transferred == sizeof(readback) && !memcmp(readback, stream_value, sizeof(readback)),
           "Retained directory stream readback returned %d, size %lu, error %lu.\n", ret, transferred, GetLastError());
        ret = GetFileTime(stream, &actual_creation, NULL, NULL);
        ok(ret && actual_creation.dwLowDateTime == creation.dwLowDateTime &&
           actual_creation.dwHighDateTime == creation.dwHighDateTime, "Retained stream creation time differs, error %lu.\n", GetLastError());
        reopened = CreateFileA(stream_case, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        ok(reopened != INVALID_HANDLE_VALUE, "Case-variant stream reopen failed: %lu.\n", GetLastError());
        if (reopened != INVALID_HANDLE_VALUE)
        {
            memset(readback, 0, sizeof(readback));
            ret = ReadFile(reopened, readback, sizeof(readback), &transferred, NULL);
            ok(ret && transferred == sizeof(readback) && !memcmp(readback, stream_value, sizeof(readback)),
               "Case-variant stream readback returned %d, size %lu, error %lu.\n", ret, transferred, GetLastError());
            stream_size.QuadPart = 0;
            ret = GetFileSizeEx(reopened, &stream_size);
            ok(ret && stream_size.QuadPart == 8192, "Case-variant stream size %I64d, error %lu.\n",
               stream_size.QuadPart, GetLastError());
            CloseHandle(reopened);
        }
        reopen_ntfs_directory_read_handle(&parents[2], path);
        for (i = 0; i < ARRAY_SIZE(parents); ++i)
        {
            if (parents[i] == INVALID_HANDLE_VALUE) continue;
            winetest_push_context("directory stream retained/fresh parent %lu", i);
            check_ntfs_directory_dacl(parents[i], denied_acl, FALSE);
            check_ntfs_directory_basic(parents[i], basic.CreationTime);
            winetest_pop_context();
        }
    }
    check_ntfs_directory_existing_child(parents[1]);
    test_ntfs_directory_reparse(path, &initial_sd, denied_acl);

    warm_ntfs_directory_lookup(files[0]);
    status = NtSetSecurityObject(parents[0], DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &add_denied_sd);
    ok(!status, "Parent add-file deny returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    for (i = 0; i < ARRAY_SIZE(parents); ++i)
        if (parents[i] != INVALID_HANDLE_VALUE) check_ntfs_directory_dacl(parents[i], add_denied_acl, FALSE);
    children[2] = CreateFileA(files[2], FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, CREATE_NEW, 0, NULL);
    child_created[2] = children[2] != INVALID_HANDLE_VALUE;
    error = GetLastError();
    ok(children[2] == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED,
       "FILE_ADD_FILE deny returned %p, error %lu.\n", children[2], error);
    reopened = CreateFileA(denied_stream, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, CREATE_NEW, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    error = GetLastError();
    ok(reopened == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED,
       "Read-only requested stream creation under base write deny returned %p, error %lu.\n", reopened, error);
    if (reopened != INVALID_HANDLE_VALUE)
    {
        denied_stream_created = TRUE;
        CloseHandle(reopened);
    }
    reopen_ntfs_directory_read_handle(&parents[2], path);
    if (parents[2] != INVALID_HANDLE_VALUE) check_ntfs_directory_dacl(parents[2], add_denied_acl, FALSE);
    reopened = CreateFileA(path, FILE_ADD_FILE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    error = GetLastError();
    ok(reopened == INVALID_HANDLE_VALUE && error == ERROR_ACCESS_DENIED,
       "Fresh parent FILE_ADD_FILE open returned %p, error %lu.\n", reopened, error);
    if (reopened != INVALID_HANDLE_VALUE) CloseHandle(reopened);
    warm_ntfs_directory_lookup(files[0]);
    status = NtSetSecurityObject(parents[0], DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &initial_sd);
    ok(!status, "Retained setter add-file restore returned %#lx.\n", (DWORD)status);
    if (status) goto done;
    children[3] = CreateFileA(files[3], READ_CONTROL | WRITE_DAC | DELETE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW, 0, NULL);
    ok(children[3] != INVALID_HANDLE_VALUE, "Creation after FILE_ADD_FILE restore failed: %lu.\n", GetLastError());
    child_created[3] = children[3] != INVALID_HANDLE_VALUE;
    if (children[3] != INVALID_HANDLE_VALUE) check_ntfs_directory_dacl(children[3], initial_acl, TRUE);
    reopened = CreateFileA(path, FILE_ADD_FILE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(reopened != INVALID_HANDLE_VALUE, "Restored parent FILE_ADD_FILE open failed: %lu.\n", GetLastError());
    if (reopened != INVALID_HANDLE_VALUE) CloseHandle(reopened);
    reopen_ntfs_directory_read_handle(&parents[2], path);
    for (i = 0; i < ARRAY_SIZE(parents); ++i)
        if (parents[i] != INVALID_HANDLE_VALUE) check_ntfs_directory_dacl(parents[i], initial_acl, FALSE);
    for (i = 0; i < ARRAY_SIZE(parents); ++i)
    {
        if (parents[i] == INVALID_HANDLE_VALUE) continue;
        CloseHandle(parents[i]);
        parents[i] = INVALID_HANDLE_VALUE;
    }
    if (stream != INVALID_HANDLE_VALUE) { CloseHandle(stream); stream = INVALID_HANDLE_VALUE; }
    parents[0] = CreateFileA(path, parent_access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(parents[0] != INVALID_HANDLE_VALUE, "Parent close/reopen failed: %lu.\n", GetLastError());
    if (parents[0] != INVALID_HANDLE_VALUE)
    {
        check_ntfs_directory_dacl(parents[0], initial_acl, FALSE);
        check_ntfs_directory_basic(parents[0], basic.CreationTime);
        check_ntfs_directory_ea(parents[0], ea, ea_size);
    }
    if (children[1] != INVALID_HANDLE_VALUE)
    {
        CloseHandle(children[1]);
        children[1] = CreateFileA(files[1], READ_CONTROL | WRITE_DAC | DELETE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        ok(children[1] != INVALID_HANDLE_VALUE, "Inherited child close/reopen failed: %lu.\n", GetLastError());
        if (children[1] != INVALID_HANDLE_VALUE)
        {
            check_ntfs_directory_dacl(children[1], denied_acl, TRUE);
            check_ntfs_child_access(children[1], files[1], token);
        }
    }
done:
    if (parents[0] != INVALID_HANDLE_VALUE)
    {
        status = NtSetSecurityObject(parents[0], DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &initial_sd);
        ok(!status, "Parent coherence ACL cleanup returned %#lx.\n", (DWORD)status);
    }
    for (i = 0; i < ARRAY_SIZE(children); ++i)
    {
        if (children[i] != INVALID_HANDLE_VALUE) CloseHandle(children[i]);
        if (child_created[i])
        {
            ret = DeleteFileA(files[i]);
            ok(ret, "Child %lu cleanup failed: %lu.\n", i, GetLastError());
        }
    }
    if (stream != INVALID_HANDLE_VALUE) CloseHandle(stream);
    if (case_stream_created) ok(DeleteFileA(stream_case), "Case-variant stream cleanup failed: %lu.\n", GetLastError());
    if (denied_stream_created) ok(DeleteFileA(denied_stream), "Denied stream cleanup failed: %lu.\n", GetLastError());
    for (i = 0; i < ARRAY_SIZE(parents); ++i)
        if (parents[i] != INVALID_HANDLE_VALUE) CloseHandle(parents[i]);
    if (directory_created) ok(RemoveDirectoryA(path), "Directory coherence cleanup failed: %lu.\n", GetLastError());
    if (impersonating) ok(SetThreadToken(NULL, previous_token), "Directory coherence token restoration failed: %lu.\n", GetLastError());
    winetest_pop_context();
}

static void test_acl_file_propagation(void)
{
    SID everyone = {SID_REVISION, 1, {SECURITY_WORLD_SID_AUTHORITY}, {SECURITY_WORLD_RID}};
    EXPLICIT_ACCESSW entries[2], explicit_entry;
    SECURITY_DESCRIPTOR initial_sd, authorization_sd;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), &initial_sd, FALSE};
    union { DWORD align; BYTE bytes[sizeof(ACL)]; } empty_buffer;
    PACL initial_acl = NULL, denied_acl = NULL, authorization_acl = NULL, explicit_acl = NULL, empty_acl = (PACL)empty_buffer.bytes;
    char temp[MAX_PATH], path[MAX_PATH] = "", volume[MAX_PATH], filesystem[MAX_PATH], files[4][MAX_PATH];
    HANDLE source = NULL, previous = NULL, token = NULL, parent = INVALID_HANDLE_VALUE;
    HANDLE children[4] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    TOKEN_PRIVILEGES *privileges = NULL;
    DWORD flags, res, i, error, size, capacity;
    BOOL ret, directory_created = FALSE;

    ret = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE, TRUE, &previous);
    if (ret) source = previous;
    else
    {
        error = GetLastError();
        ok(error == ERROR_NO_TOKEN, "OpenThreadToken failed: %lu.\n", error);
        if (error != ERROR_NO_TOKEN) goto done;
        ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &source);
        ok(ret, "OpenProcessToken failed: %lu.\n", GetLastError());
        if (!ret) goto done;
    }
    ret = DuplicateTokenEx(source, TOKEN_QUERY | TOKEN_IMPERSONATE | TOKEN_ADJUST_PRIVILEGES, NULL,
                           SecurityImpersonation, TokenImpersonation, &token);
    ok(ret, "DuplicateTokenEx failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ret = AdjustTokenPrivileges(token, TRUE, NULL, 0, NULL, NULL);
    ok(ret, "Disabling private token privileges failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    size = 0;
    ret = GetTokenInformation(token, TokenPrivileges, NULL, 0, &size);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER && size >= FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges),
       "Private privilege sizing returned %d, error %lu, size %lu.\n", ret, error, size);
    if (ret || error != ERROR_INSUFFICIENT_BUFFER || size < FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) goto done;
    capacity = size;
    privileges = malloc(capacity);
    ok(!!privileges, "Private privilege buffer allocation failed.\n");
    if (!privileges) goto done;
    ret = GetTokenInformation(token, TokenPrivileges, privileges, capacity, &size);
    ok(ret, "Private privilege query failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ok(size >= FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges) && size <= capacity,
       "Private privilege query returned invalid size %lu, capacity %lu.\n", size, capacity);
    if (size < FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges) || size > capacity) goto done;
    ok(privileges->PrivilegeCount <= (size - FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) / sizeof(LUID_AND_ATTRIBUTES),
       "Private privilege count exceeds returned size.\n");
    if (privileges->PrivilegeCount > (size - FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) / sizeof(LUID_AND_ATTRIBUTES)) goto done;
    ret = TRUE;
    for (i = 0; i < privileges->PrivilegeCount; ++i)
    {
        ok(!(privileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED), "Private token privilege %lu remains enabled.\n", i);
        if (privileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) ret = FALSE;
    }
    if (!ret) goto done;

    memset(entries, 0, sizeof(entries));
    entries[0].grfAccessPermissions = FILE_WRITE_DATA;
    entries[0].grfAccessMode = DENY_ACCESS;
    BuildTrusteeWithSidW(&entries[0].Trustee, &everyone);
    entries[1].grfAccessPermissions = FILE_ALL_ACCESS;
    entries[1].grfAccessMode = GRANT_ACCESS;
    BuildTrusteeWithSidW(&entries[1].Trustee, &everyone);
    res = SetEntriesInAclW(2, entries, NULL, &authorization_acl);
    ok(res == ERROR_SUCCESS && authorization_acl, "Authorization ACL construction returned %lu.\n", res);
    if (res || !authorization_acl) goto done;
    ret = InitializeSecurityDescriptor(&authorization_sd, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorOwner(&authorization_sd, &everyone, FALSE) &&
          SetSecurityDescriptorGroup(&authorization_sd, &everyone, FALSE) &&
          SetSecurityDescriptorDacl(&authorization_sd, TRUE, authorization_acl, FALSE);
    ok(ret, "Authorization SD initialization failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    check_acl_file_access(&authorization_sd, token, GENERIC_READ, TRUE);
    check_acl_file_access(&authorization_sd, token, FILE_WRITE_DATA, FALSE);
    check_acl_file_access(&authorization_sd, token, FILE_READ_DATA | FILE_WRITE_DATA, FALSE);
    check_acl_public_access_checks(token);

    entries[1].grfInheritance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    res = SetEntriesInAclW(1, entries + 1, NULL, &initial_acl);
    ok(res == ERROR_SUCCESS && initial_acl, "Initial ACL construction returned %lu.\n", res);
    if (res || !initial_acl) goto done;
    entries[0].grfInheritance = OBJECT_INHERIT_ACE | INHERIT_ONLY_ACE;
    res = SetEntriesInAclW(2, entries, NULL, &denied_acl);
    ok(res == ERROR_SUCCESS && denied_acl, "Parent deny ACL construction returned %lu.\n", res);
    if (res || !denied_acl) goto done;
    explicit_entry = entries[1];
    explicit_entry.grfAccessPermissions = FILE_WRITE_DATA;
    explicit_entry.grfInheritance = NO_INHERITANCE;
    res = SetEntriesInAclW(1, &explicit_entry, NULL, &explicit_acl);
    ok(res == ERROR_SUCCESS && explicit_acl, "Explicit child ACL construction returned %lu.\n", res);
    if (res || !explicit_acl) goto done;
    ret = InitializeAcl(empty_acl, sizeof(empty_buffer), ACL_REVISION) &&
          InitializeSecurityDescriptor(&initial_sd, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorDacl(&initial_sd, TRUE, initial_acl, FALSE) &&
          SetSecurityDescriptorControl(&initial_sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    ok(ret, "Initial descriptor setup failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    res = GetTempPathA(ARRAY_SIZE(temp), temp);
    ok(res && res < ARRAY_SIZE(temp), "GetTempPathA returned %lu.\n", res);
    if (!res || res >= ARRAY_SIZE(temp)) goto done;
    ret = GetVolumePathNameA(temp, volume, ARRAY_SIZE(volume));
    ok(ret, "GetVolumePathNameA failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ret = GetVolumeInformationA(volume, NULL, 0, NULL, NULL, &flags, filesystem, ARRAY_SIZE(filesystem));
    ok(ret, "GetVolumeInformationA failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    trace("ACL fixture temporary volume %s, filesystem %s, flags %#lx.\n", volume, filesystem, flags);
    test_ntfs_directory_acl_coherence(temp, filesystem, flags, token, previous, &everyone, initial_acl, denied_acl);
    if (!(flags & FILE_PERSISTENT_ACLS))
    {
        if (lstrcmpiA(filesystem, "NTFS") || (flags & FILE_READ_ONLY_VOLUME))
        {
            win_skip("Temporary volume does not support persistent ACLs.\n");
            goto done;
        }
        trace("Testing NTFS ACL propagation independently of FILE_PERSISTENT_ACLS (%#lx).\n", flags);
    }
    ret = GetTempFileNameA(temp, "acl", 0, path);
    ok(ret, "GetTempFileNameA failed: %lu.\n", GetLastError());
    if (!ret) { path[0] = 0; goto done; }
    ret = DeleteFileA(path);
    ok(ret, "Removing temporary reservation failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    if (strlen(path) + sizeof("\\protected") > ARRAY_SIZE(files[0]))
    {
        win_skip("Temporary path is too long for file ACL fixtures.\n");
        goto done;
    }
    ret = CreateDirectoryA(path, &attributes);
    ok(ret, "CreateDirectoryA failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    directory_created = TRUE;
    parent = CreateFileA(path, READ_CONTROL | WRITE_DAC | FILE_LIST_DIRECTORY,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(parent != INVALID_HANDLE_VALUE, "Opening parent failed: %lu.\n", GetLastError());
    if (parent == INVALID_HANDLE_VALUE) goto done;
    sprintf(files[0], "%s\\existing", path);
    sprintf(files[1], "%s\\protected", path);
    sprintf(files[2], "%s\\explicit", path);
    sprintf(files[3], "%s\\new", path);
    for (i = 0; i < 3; ++i)
    {
        children[i] = CreateFileA(files[i], READ_CONTROL | WRITE_DAC | DELETE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, NULL);
        ok(children[i] != INVALID_HANDLE_VALUE, "Creating child %lu failed: %lu.\n", i, GetLastError());
        if (children[i] == INVALID_HANDLE_VALUE) goto done;
    }
    res = SetSecurityInfo(children[1], SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                          NULL, NULL, initial_acl, NULL);
    ok(res == ERROR_SUCCESS, "Protecting child returned %lu.\n", res);
    if (res) goto done;
    res = SetNamedSecurityInfoA(files[2], SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                               NULL, NULL, explicit_acl, NULL);
    ok(res == ERROR_SUCCESS, "Explicit unprotected child ACL returned %lu.\n", res);
    if (res) goto done;
    winetest_push_context("before parent update");
    ret = check_acl_file_enforcement(children[0], files[0], token, previous, &everyone, FALSE, FALSE, FALSE, FALSE);
    winetest_pop_context();
    if (!ret) goto done;
    winetest_push_context("explicit child before parent update");
    ret = check_acl_file_enforcement(children[2], files[2], token, previous, &everyone, FALSE, FALSE, FALSE, TRUE);
    winetest_pop_context();
    if (!ret) goto done;
    res = SetSecurityInfo(parent, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                          NULL, NULL, denied_acl, NULL);
    ok(res == ERROR_SUCCESS, "Parent propagation returned %lu.\n", res);
    if (res) goto done;
    children[3] = CreateFileA(files[3], READ_CONTROL | WRITE_DAC | DELETE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    ok(children[3] != INVALID_HANDLE_VALUE, "Creating new child failed: %lu.\n", GetLastError());
    if (children[3] == INVALID_HANDLE_VALUE) goto done;
    for (i = 0; i < ARRAY_SIZE(children); ++i)
    {
        winetest_push_context("after parent update child %lu", i);
        ret = check_acl_file_enforcement(children[i], files[i], token, previous, &everyone, i != 1, i == 0 || i == 3, i == 1, i == 2);
        winetest_pop_context();
        if (!ret) goto done;
    }
    res = SetNamedSecurityInfoA(files[1], SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                               NULL, NULL, empty_acl, NULL);
    ok(res == ERROR_SUCCESS, "Unprotecting child returned %lu.\n", res);
    if (res) goto done;
    winetest_push_context("unprotected child");
    ret = check_acl_file_enforcement(children[1], files[1], token, previous, &everyone, TRUE, TRUE, FALSE, FALSE);
    winetest_pop_context();
    if (!ret) goto done;
    res = SetSecurityInfo(parent, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                          NULL, NULL, initial_acl, NULL);
    ok(res == ERROR_SUCCESS, "Parent allow propagation returned %lu.\n", res);
    if (res) goto done;
    for (i = 0; i < ARRAY_SIZE(children); ++i)
    {
        winetest_push_context("after deny removal child %lu", i);
        ret = check_acl_file_enforcement(children[i], files[i], token, previous, &everyone, FALSE, FALSE, FALSE, i == 2);
        winetest_pop_context();
        if (!ret) goto done;
    }

done:
    if (token)
    {
        ret = SetThreadToken(NULL, previous);
        ok(ret, "Final thread token restoration failed: %lu.\n", GetLastError());
    }
    for (i = 0; i < ARRAY_SIZE(children); ++i)
    {
        if (children[i] == INVALID_HANDLE_VALUE) continue;
        res = SetSecurityInfo(children[i], SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                              NULL, NULL, initial_acl, NULL);
        ok(res == ERROR_SUCCESS, "Cleanup ACL restore child %lu returned %lu.\n", i, res);
        CloseHandle(children[i]);
        ret = DeleteFileA(files[i]);
        ok(ret, "Cleanup DeleteFileA child %lu failed: %lu.\n", i, GetLastError());
    }
    if (parent != INVALID_HANDLE_VALUE) CloseHandle(parent);
    if (directory_created)
    {
        ret = RemoveDirectoryA(path);
        ok(ret, "Cleanup RemoveDirectoryA failed: %lu.\n", GetLastError());
    }
    else if (path[0]) DeleteFileA(path);
    if (source && source != previous) CloseHandle(source);
    if (previous) CloseHandle(previous);
    if (token) CloseHandle(token);
    LocalFree(initial_acl);
    LocalFree(denied_acl);
    LocalFree(authorization_acl);
    LocalFree(explicit_acl);
    free(privileges);
}

static void test_SetEntriesInAclW(void)
{
    DWORD res;
    PSID EveryoneSid = NULL, UsersSid = NULL;
    PACL OldAcl = NULL, NewAcl;
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = { SECURITY_WORLD_SID_AUTHORITY };
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = { SECURITY_NT_AUTHORITY };
    EXPLICIT_ACCESSW ExplicitAccess;

    NewAcl = (PACL)0xdeadbeef;
    res = SetEntriesInAclW(0, NULL, NULL, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(NewAcl == NULL, "NewAcl=%p, expected NULL\n", NewAcl);
    LocalFree(NewAcl);

    OldAcl = malloc(256);
    res = InitializeAcl(OldAcl, 256, ACL_REVISION);
    if(!res && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        win_skip("ACLs not implemented - skipping tests\n");
        free(OldAcl);
        return;
    }
    ok(res, "InitializeAcl failed with error %ld\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &EveryoneSid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &UsersSid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    res = AddAccessAllowedAce(OldAcl, ACL_REVISION, KEY_READ, UsersSid);
    ok(res, "AddAccessAllowedAce failed with error %ld\n", GetLastError());

    ExplicitAccess.grfAccessPermissions = KEY_WRITE;
    ExplicitAccess.grfAccessMode = GRANT_ACCESS;
    ExplicitAccess.grfInheritance = NO_INHERITANCE;
    ExplicitAccess.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ExplicitAccess.Trustee.ptstrName = EveryoneSid;
    ExplicitAccess.Trustee.MultipleTrusteeOperation = 0xDEADBEEF;
    ExplicitAccess.Trustee.pMultipleTrustee = (PVOID)0xDEADBEEF;
    res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(NewAcl != NULL, "returned acl was NULL\n");
    LocalFree(NewAcl);

    ExplicitAccess.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    ExplicitAccess.Trustee.pMultipleTrustee = NULL;
    ExplicitAccess.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(NewAcl != NULL, "returned acl was NULL\n");
    LocalFree(NewAcl);

    if (PRIMARYLANGID(GetSystemDefaultLangID()) != LANG_ENGLISH)
    {
        skip("Non-English locale (test with hardcoded 'Everyone')\n");
    }
    else
    {
        ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
        ExplicitAccess.Trustee.ptstrName = (WCHAR *)L"Everyone";
        res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
        ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
        ok(NewAcl != NULL, "returned acl was NULL\n");
        LocalFree(NewAcl);

        ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_BAD_FORM;
        res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
        ok(res == ERROR_INVALID_PARAMETER,
            "SetEntriesInAclW failed: %lu\n", res);
        ok(NewAcl == NULL,
            "returned acl wasn't NULL: %p\n", NewAcl);

        ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
        ExplicitAccess.Trustee.MultipleTrusteeOperation = TRUSTEE_IS_IMPERSONATE;
        res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
        ok(res == ERROR_INVALID_PARAMETER,
            "SetEntriesInAclW failed: %lu\n", res);
        ok(NewAcl == NULL,
            "returned acl wasn't NULL: %p\n", NewAcl);

        ExplicitAccess.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
        ExplicitAccess.grfAccessMode = SET_ACCESS;
        res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
        ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
        ok(NewAcl != NULL, "returned acl was NULL\n");
        LocalFree(NewAcl);
    }

    ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ExplicitAccess.Trustee.ptstrName = (WCHAR *)L"CURRENT_USER";
    res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(NewAcl != NULL, "returned acl was NULL\n");
    LocalFree(NewAcl);

    ExplicitAccess.grfAccessMode = REVOKE_ACCESS;
    ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ExplicitAccess.Trustee.ptstrName = UsersSid;
    res = SetEntriesInAclW(1, &ExplicitAccess, OldAcl, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(NewAcl != NULL, "returned acl was NULL\n");
    LocalFree(NewAcl);

    FreeSid(UsersSid);
    FreeSid(EveryoneSid);
    free(OldAcl);
}

static void test_SetEntriesInAclA(void)
{
    DWORD res;
    PSID EveryoneSid = NULL, UsersSid = NULL;
    PACL OldAcl = NULL, NewAcl;
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = { SECURITY_WORLD_SID_AUTHORITY };
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = { SECURITY_NT_AUTHORITY };
    EXPLICIT_ACCESSA ExplicitAccess;

    NewAcl = (PACL)0xdeadbeef;
    res = SetEntriesInAclA(0, NULL, NULL, &NewAcl);
    if(res == ERROR_CALL_NOT_IMPLEMENTED)
    {
        win_skip("SetEntriesInAclA is not implemented\n");
        return;
    }
    ok(res == ERROR_SUCCESS, "SetEntriesInAclA failed: %lu\n", res);
    ok(NewAcl == NULL,
        "NewAcl=%p, expected NULL\n", NewAcl);
    LocalFree(NewAcl);

    OldAcl = malloc(256);
    res = InitializeAcl(OldAcl, 256, ACL_REVISION);
    if(!res && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        win_skip("ACLs not implemented - skipping tests\n");
        free(OldAcl);
        return;
    }
    ok(res, "InitializeAcl failed with error %ld\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &EveryoneSid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    res = AllocateAndInitializeSid( &SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &UsersSid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    res = AddAccessAllowedAce(OldAcl, ACL_REVISION, KEY_READ, UsersSid);
    ok(res, "AddAccessAllowedAce failed with error %ld\n", GetLastError());
#else
    res = AddAccessAllowedAce(OldAcl, ACL_REVISION, KEY_READ, UsersSid);
    ok(res, "AddAccessAllowedAce failed with error %ld\n", GetLastError());
#endif

    ExplicitAccess.grfAccessPermissions = KEY_WRITE;
    ExplicitAccess.grfAccessMode = GRANT_ACCESS;
    ExplicitAccess.grfInheritance = NO_INHERITANCE;
    ExplicitAccess.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ExplicitAccess.Trustee.ptstrName = EveryoneSid;
    ExplicitAccess.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    ExplicitAccess.Trustee.pMultipleTrustee = NULL;
    res = SetEntriesInAclA(1, &ExplicitAccess, OldAcl, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclA failed: %lu\n", res);
    ok(NewAcl != NULL, "returned acl was NULL\n");
    LocalFree(NewAcl);

    ExplicitAccess.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    ExplicitAccess.Trustee.pMultipleTrustee = NULL;
    ExplicitAccess.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    res = SetEntriesInAclA(1, &ExplicitAccess, OldAcl, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclA failed: %lu\n", res);
    ok(NewAcl != NULL, "returned acl was NULL\n");
    LocalFree(NewAcl);

    if (PRIMARYLANGID(GetSystemDefaultLangID()) != LANG_ENGLISH)
    {
        skip("Non-English locale (test with hardcoded 'Everyone')\n");
    }
    else
    {
        ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
        ExplicitAccess.Trustee.ptstrName = (char*)"Everyone";
        res = SetEntriesInAclA(1, &ExplicitAccess, OldAcl, &NewAcl);
        ok(res == ERROR_SUCCESS, "SetEntriesInAclA failed: %lu\n", res);
        ok(NewAcl != NULL, "returned acl was NULL\n");
        LocalFree(NewAcl);

        ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_BAD_FORM;
        res = SetEntriesInAclA(1, &ExplicitAccess, OldAcl, &NewAcl);
        ok(res == ERROR_INVALID_PARAMETER,
            "SetEntriesInAclA failed: %lu\n", res);
        ok(NewAcl == NULL,
            "returned acl wasn't NULL: %p\n", NewAcl);

        ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
        ExplicitAccess.Trustee.MultipleTrusteeOperation = TRUSTEE_IS_IMPERSONATE;
        res = SetEntriesInAclA(1, &ExplicitAccess, OldAcl, &NewAcl);
        ok(res == ERROR_INVALID_PARAMETER,
            "SetEntriesInAclA failed: %lu\n", res);
        ok(NewAcl == NULL,
            "returned acl wasn't NULL: %p\n", NewAcl);

        ExplicitAccess.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
        ExplicitAccess.grfAccessMode = SET_ACCESS;
        res = SetEntriesInAclA(1, &ExplicitAccess, OldAcl, &NewAcl);
        ok(res == ERROR_SUCCESS, "SetEntriesInAclA failed: %lu\n", res);
        ok(NewAcl != NULL, "returned acl was NULL\n");
        LocalFree(NewAcl);
    }

    ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ExplicitAccess.Trustee.ptstrName = (char *)"CURRENT_USER";
    res = SetEntriesInAclA(1, &ExplicitAccess, OldAcl, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclA failed: %lu\n", res);
    ok(NewAcl != NULL, "returned acl was NULL\n");
    LocalFree(NewAcl);

    ExplicitAccess.grfAccessMode = REVOKE_ACCESS;
    ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ExplicitAccess.Trustee.ptstrName = UsersSid;
    res = SetEntriesInAclA(1, &ExplicitAccess, OldAcl, &NewAcl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclA failed: %lu\n", res);
    ok(NewAcl != NULL, "returned acl was NULL\n");
    LocalFree(NewAcl);

    FreeSid(UsersSid);
    FreeSid(EveryoneSid);
    free(OldAcl);
}

/* helper function for test_CreateDirectoryA */
static void get_nt_pathW(const char *name, UNICODE_STRING *nameW)
{
    UNICODE_STRING strW;
    ANSI_STRING str;
    NTSTATUS status;
    BOOLEAN ret;

    RtlInitAnsiString(&str, name);

    status = RtlAnsiStringToUnicodeString(&strW, &str, TRUE);
    ok(!status, "RtlAnsiStringToUnicodeString failed with %08lx\n", status);

    ret = pRtlDosPathNameToNtPathName_U(strW.Buffer, nameW, NULL, NULL);
    ok(ret, "RtlDosPathNameToNtPathName_U failed\n");

    RtlFreeUnicodeString(&strW);
}

static void test_inherited_dacl(PACL dacl, PSID admin_sid, PSID user_sid, DWORD flags, DWORD mask,
                                BOOL todo_count, BOOL todo_sid, BOOL todo_flags, int line)
{
    ACL_SIZE_INFORMATION acl_size;
    ACCESS_ALLOWED_ACE *ace;
    BOOL bret;

    bret = GetAclInformation(dacl, &acl_size, sizeof(acl_size), AclSizeInformation);
    ok_(__FILE__, line)(bret, "GetAclInformation failed\n");

    todo_wine_if (todo_count)
        ok_(__FILE__, line)(acl_size.AceCount == 2,
            "GetAclInformation returned unexpected entry count (%ld != 2)\n",
            acl_size.AceCount);

    if (acl_size.AceCount > 0)
    {
        bret = GetAce(dacl, 0, (VOID **)&ace);
        ok_(__FILE__, line)(bret, "Failed to get Current User ACE\n");

        bret = EqualSid(&ace->SidStart, user_sid);
        todo_wine_if (todo_sid)
            ok_(__FILE__, line)(bret, "Current User ACE (%s) != Current User SID (%s)\n", debugstr_sid(&ace->SidStart), debugstr_sid(user_sid));

        todo_wine_if (todo_flags)
            ok_(__FILE__, line)(((ACE_HEADER *)ace)->AceFlags == flags,
                "Current User ACE has unexpected flags (0x%x != 0x%lx)\n",
                ((ACE_HEADER *)ace)->AceFlags, flags);

        ok_(__FILE__, line)(ace->Mask == mask,
            "Current User ACE has unexpected mask (0x%lx != 0x%lx)\n",
            ace->Mask, mask);
    }
    if (acl_size.AceCount > 1)
    {
        bret = GetAce(dacl, 1, (VOID **)&ace);
        ok_(__FILE__, line)(bret, "Failed to get Administators Group ACE\n");

        bret = EqualSid(&ace->SidStart, admin_sid);
        todo_wine_if (todo_sid)
            ok_(__FILE__, line)(bret, "Administators Group ACE (%s) != Administators Group SID (%s)\n", debugstr_sid(&ace->SidStart), debugstr_sid(admin_sid));

        todo_wine_if (todo_flags)
            ok_(__FILE__, line)(((ACE_HEADER *)ace)->AceFlags == flags,
                "Administators Group ACE has unexpected flags (0x%x != 0x%lx)\n",
                ((ACE_HEADER *)ace)->AceFlags, flags);

        ok_(__FILE__, line)(ace->Mask == mask,
            "Administators Group ACE has unexpected mask (0x%lx != 0x%lx)\n",
            ace->Mask, mask);
    }
}

static void test_CreateDirectoryA(void)
{
    char admin_ptr[sizeof(SID)+sizeof(ULONG)*SID_MAX_SUB_AUTHORITIES], *user;
    DWORD sid_size = sizeof(admin_ptr), user_size;
    PSID admin_sid = (PSID) admin_ptr, user_sid;
    char sd[SECURITY_DESCRIPTOR_MIN_LENGTH];
    PSECURITY_DESCRIPTOR pSD = &sd;
    ACL_SIZE_INFORMATION acl_size;
    UNICODE_STRING tmpfileW;
    SECURITY_ATTRIBUTES sa;
    OBJECT_ATTRIBUTES attr;
    char tmpfile[MAX_PATH];
    char tmpdir[MAX_PATH];
    HANDLE token, hTemp;
    IO_STATUS_BLOCK io;
    struct _SID *owner;
    BOOL bret = TRUE;
    NTSTATUS status;
    DWORD error;
    PACL pDacl;

    if (!OpenThreadToken(GetCurrentThread(), TOKEN_READ, TRUE, &token))
    {
        if (GetLastError() != ERROR_NO_TOKEN) bret = FALSE;
        else if (!OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &token)) bret = FALSE;
    }
    if (!bret)
    {
        win_skip("Failed to get current user token\n");
        return;
    }
    bret = GetTokenInformation(token, TokenUser, NULL, 0, &user_size);
    ok(!bret && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "GetTokenInformation(TokenUser) failed with error %ld\n", GetLastError());
    user = malloc(user_size);
    bret = GetTokenInformation(token, TokenUser, user, user_size, &user_size);
    ok(bret, "GetTokenInformation(TokenUser) failed with error %ld\n", GetLastError());
    CloseHandle( token );
    user_sid = ((TOKEN_USER *)user)->User.Sid;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = TRUE;
    InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION);
    CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, admin_sid, &sid_size);
    pDacl = calloc(1, 100);
    bret = InitializeAcl(pDacl, 100, ACL_REVISION);
    ok(bret, "Failed to initialize ACL.\n");
    bret = AddAccessAllowedAceEx(pDacl, ACL_REVISION, OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE,
                                 GENERIC_ALL, user_sid);
    ok(bret, "Failed to add Current User to ACL.\n");
    bret = AddAccessAllowedAceEx(pDacl, ACL_REVISION, OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE,
                                 GENERIC_ALL, admin_sid);
    ok(bret, "Failed to add Administrator Group to ACL.\n");
    bret = SetSecurityDescriptorDacl(pSD, TRUE, pDacl, FALSE);
    ok(bret, "Failed to add ACL to security descriptor.\n");

    GetTempPathA(MAX_PATH, tmpdir);
    lstrcatA(tmpdir, "Please Remove Me");
    bret = CreateDirectoryA(tmpdir, &sa);
    ok(bret == TRUE, "CreateDirectoryA(%s) failed err=%ld\n", tmpdir, GetLastError());
    free(pDacl);

    SetLastError(0xdeadbeef);
    error = GetNamedSecurityInfoA(tmpdir, SE_FILE_OBJECT,
                                  OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION, (PSID*)&owner,
                                  NULL, &pDacl, NULL, &pSD);
    if (error != ERROR_SUCCESS && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED))
    {
        win_skip("GetNamedSecurityInfoA is not implemented\n");
        goto done;
    }
    ok(!error, "GetNamedSecurityInfo failed with error %ld\n", error);
    test_inherited_dacl(pDacl, admin_sid, user_sid, OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE,
                        0x1f01ff, FALSE, TRUE, FALSE, __LINE__);
    LocalFree(pSD);

    /* Test inheritance of ACLs in CreateFile without security descriptor */
    strcpy(tmpfile, tmpdir);
    lstrcatA(tmpfile, "/tmpfile");

    hTemp = CreateFileA(tmpfile, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(hTemp != INVALID_HANDLE_VALUE, "CreateFile error %lu\n", GetLastError());

    error = GetNamedSecurityInfoA(tmpfile, SE_FILE_OBJECT,
                                  OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                  (PSID *)&owner, NULL, &pDacl, NULL, &pSD);
    ok(error == ERROR_SUCCESS, "Failed to get permissions on file\n");
    test_inherited_dacl(pDacl, admin_sid, user_sid, INHERITED_ACE,
                        0x1f01ff, TRUE, TRUE, TRUE, __LINE__);
    LocalFree(pSD);
    CloseHandle(hTemp);

    /* Test inheritance of ACLs in CreateFile with security descriptor -
     * When a security descriptor is set, then inheritance doesn't take effect */
    pSD = &sd;
    InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION);
    pDacl = malloc(sizeof(ACL));
    bret = InitializeAcl(pDacl, sizeof(ACL), ACL_REVISION);
    ok(bret, "Failed to initialize ACL\n");
    bret = SetSecurityDescriptorDacl(pSD, TRUE, pDacl, FALSE);
    ok(bret, "Failed to add ACL to security descriptor\n");

    strcpy(tmpfile, tmpdir);
    lstrcatA(tmpfile, "/tmpfile");

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = TRUE;
    hTemp = CreateFileA(tmpfile, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                        CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    ok(hTemp != INVALID_HANDLE_VALUE, "CreateFile error %lu\n", GetLastError());
    free(pDacl);

    error = GetSecurityInfo(hTemp, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            (PSID *)&owner, NULL, &pDacl, NULL, &pSD);
    ok(error == ERROR_SUCCESS, "GetNamedSecurityInfo failed with error %ld\n", error);
    bret = GetAclInformation(pDacl, &acl_size, sizeof(acl_size), AclSizeInformation);
    ok(bret, "GetAclInformation failed\n");
    todo_wine
    ok(acl_size.AceCount == 0, "GetAclInformation returned unexpected entry count (%ld != 0).\n",
                               acl_size.AceCount);
    LocalFree(pSD);

    error = GetNamedSecurityInfoA(tmpfile, SE_FILE_OBJECT,
                                  OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                  (PSID *)&owner, NULL, &pDacl, NULL, &pSD);
    todo_wine
    ok(error == ERROR_SUCCESS, "GetNamedSecurityInfo failed with error %ld\n", error);
    if (error == ERROR_SUCCESS)
    {
        bret = GetAclInformation(pDacl, &acl_size, sizeof(acl_size), AclSizeInformation);
        ok(bret, "GetAclInformation failed\n");
        todo_wine
        ok(acl_size.AceCount == 0, "GetAclInformation returned unexpected entry count (%ld != 0).\n",
                                   acl_size.AceCount);
        LocalFree(pSD);
    }
    CloseHandle(hTemp);

    /* Test inheritance of ACLs in NtCreateFile without security descriptor */
    strcpy(tmpfile, tmpdir);
    lstrcatA(tmpfile, "/tmpfile");
    get_nt_pathW(tmpfile, &tmpfileW);

    attr.Length = sizeof(attr);
    attr.RootDirectory = 0;
    attr.ObjectName = &tmpfileW;
    attr.Attributes = OBJ_CASE_INSENSITIVE;
    attr.SecurityDescriptor = NULL;
    attr.SecurityQualityOfService = NULL;

    status = NtCreateFile(&hTemp, GENERIC_WRITE | DELETE, &attr, &io, NULL, 0,
                          FILE_SHARE_READ, FILE_CREATE, FILE_DELETE_ON_CLOSE, NULL, 0);
    ok(!status, "NtCreateFile failed with %08lx\n", status);
    RtlFreeUnicodeString(&tmpfileW);

    error = GetNamedSecurityInfoA(tmpfile, SE_FILE_OBJECT,
                                  OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                  (PSID *)&owner, NULL, &pDacl, NULL, &pSD);
    ok(error == ERROR_SUCCESS, "Failed to get permissions on file\n");
    test_inherited_dacl(pDacl, admin_sid, user_sid, INHERITED_ACE,
                        0x1f01ff, TRUE, TRUE, TRUE, __LINE__);
    LocalFree(pSD);
    CloseHandle(hTemp);

    /* Test inheritance of ACLs in NtCreateFile with security descriptor -
     * When a security descriptor is set, then inheritance doesn't take effect */
    pSD = &sd;
    InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION);
    pDacl = malloc(sizeof(ACL));
    bret = InitializeAcl(pDacl, sizeof(ACL), ACL_REVISION);
    ok(bret, "Failed to initialize ACL\n");
    bret = SetSecurityDescriptorDacl(pSD, TRUE, pDacl, FALSE);
    ok(bret, "Failed to add ACL to security descriptor\n");

    strcpy(tmpfile, tmpdir);
    lstrcatA(tmpfile, "/tmpfile");
    get_nt_pathW(tmpfile, &tmpfileW);

    attr.Length = sizeof(attr);
    attr.RootDirectory = 0;
    attr.ObjectName = &tmpfileW;
    attr.Attributes = OBJ_CASE_INSENSITIVE;
    attr.SecurityDescriptor = pSD;
    attr.SecurityQualityOfService = NULL;

    status = NtCreateFile(&hTemp, GENERIC_WRITE | DELETE, &attr, &io, NULL, 0,
                          FILE_SHARE_READ, FILE_CREATE, FILE_DELETE_ON_CLOSE, NULL, 0);
    ok(!status, "NtCreateFile failed with %08lx\n", status);
    RtlFreeUnicodeString(&tmpfileW);
    free(pDacl);

    error = GetSecurityInfo(hTemp, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            (PSID *)&owner, NULL, &pDacl, NULL, &pSD);
    ok(error == ERROR_SUCCESS, "GetNamedSecurityInfo failed with error %ld\n", error);
    bret = GetAclInformation(pDacl, &acl_size, sizeof(acl_size), AclSizeInformation);
    ok(bret, "GetAclInformation failed\n");
    todo_wine
    ok(acl_size.AceCount == 0, "GetAclInformation returned unexpected entry count (%ld != 0).\n",
                               acl_size.AceCount);
    LocalFree(pSD);

    error = GetNamedSecurityInfoA(tmpfile, SE_FILE_OBJECT,
                                  OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                  (PSID *)&owner, NULL, &pDacl, NULL, &pSD);
    todo_wine
    ok(error == ERROR_SUCCESS, "GetNamedSecurityInfo failed with error %ld\n", error);
    if (error == ERROR_SUCCESS)
    {
        bret = GetAclInformation(pDacl, &acl_size, sizeof(acl_size), AclSizeInformation);
        ok(bret, "GetAclInformation failed\n");
        todo_wine
        ok(acl_size.AceCount == 0, "GetAclInformation returned unexpected entry count (%ld != 0).\n",
                                   acl_size.AceCount);
        LocalFree(pSD);
    }
    CloseHandle(hTemp);

done:
    free(user);
    bret = RemoveDirectoryA(tmpdir);
    ok(bret == TRUE, "RemoveDirectoryA should always succeed\n");
}

static void test_GetNamedSecurityInfoA(void)
{
    char admin_ptr[sizeof(SID)+sizeof(ULONG)*SID_MAX_SUB_AUTHORITIES], *user;
    char system_ptr[sizeof(SID)+sizeof(ULONG)*SID_MAX_SUB_AUTHORITIES];
    char users_ptr[sizeof(SID)+sizeof(ULONG)*SID_MAX_SUB_AUTHORITIES];
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = { SECURITY_NT_AUTHORITY };
    PSID admin_sid = (PSID) admin_ptr, users_sid = (PSID) users_ptr;
    PSID system_sid = (PSID) system_ptr, user_sid, localsys_sid;
    DWORD sid_size = sizeof(admin_ptr), user_size;
    char invalid_path[] = "/an invalid file path";
    int users_ace_id = -1, admins_ace_id = -1, i;
    char software_key[] = "MACHINE\\Software";
    char sd[SECURITY_DESCRIPTOR_MIN_LENGTH+sizeof(void*)];
    SECURITY_DESCRIPTOR_CONTROL control;
    ACL_SIZE_INFORMATION acl_size;
    CHAR windows_dir[MAX_PATH];
    PSECURITY_DESCRIPTOR pSD;
    ACCESS_ALLOWED_ACE *ace;
    BOOL bret = TRUE;
    char tmpfile[MAX_PATH];
    DWORD error, revision;
    BOOL owner_defaulted;
    BOOL group_defaulted;
    BOOL dacl_defaulted;
    HANDLE token, hTemp, h;
    PSID owner, group;
    BOOL dacl_present;
    PACL pDacl;
    BYTE flags;
    NTSTATUS status;

    if (!OpenThreadToken(GetCurrentThread(), TOKEN_READ, TRUE, &token))
    {
        if (GetLastError() != ERROR_NO_TOKEN) bret = FALSE;
        else if (!OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &token)) bret = FALSE;
    }
    if (!bret)
    {
        win_skip("Failed to get current user token\n");
        return;
    }
    bret = GetTokenInformation(token, TokenUser, NULL, 0, &user_size);
    ok(!bret && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "GetTokenInformation(TokenUser) failed with error %ld\n", GetLastError());
    user = malloc(user_size);
    bret = GetTokenInformation(token, TokenUser, user, user_size, &user_size);
    ok(bret, "GetTokenInformation(TokenUser) failed with error %ld\n", GetLastError());
    CloseHandle( token );
    user_sid = ((TOKEN_USER *)user)->User.Sid;

    bret = GetWindowsDirectoryA(windows_dir, MAX_PATH);
    ok(bret, "GetWindowsDirectory failed with error %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    error = GetNamedSecurityInfoA(windows_dir, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        NULL, NULL, NULL, NULL, &pSD);
    if (error != ERROR_SUCCESS && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED))
    {
        win_skip("GetNamedSecurityInfoA is not implemented\n");
        free(user);
        return;
    }
    ok(!error, "GetNamedSecurityInfo failed with error %ld\n", error);

    bret = GetSecurityDescriptorControl(pSD, &control, &revision);
    ok(bret, "GetSecurityDescriptorControl failed with error %ld\n", GetLastError());
    ok((control & (SE_SELF_RELATIVE|SE_DACL_PRESENT)) == (SE_SELF_RELATIVE|SE_DACL_PRESENT),
        "control (0x%x) doesn't have (SE_SELF_RELATIVE|SE_DACL_PRESENT) flags set\n", control);
    ok(revision == SECURITY_DESCRIPTOR_REVISION1, "revision was %ld instead of 1\n", revision);

    bret = GetSecurityDescriptorOwner(pSD, &owner, &owner_defaulted);
    ok(bret, "GetSecurityDescriptorOwner failed with error %ld\n", GetLastError());
    ok(owner != NULL, "owner should not be NULL\n");

    bret = GetSecurityDescriptorGroup(pSD, &group, &group_defaulted);
    ok(bret, "GetSecurityDescriptorGroup failed with error %ld\n", GetLastError());
    ok(group != NULL, "group should not be NULL\n");
    LocalFree(pSD);


    /* NULL descriptor tests */

    error = GetNamedSecurityInfoA(windows_dir, SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,
        NULL, NULL, NULL, NULL, NULL);
    ok(error==ERROR_INVALID_PARAMETER, "GetNamedSecurityInfo failed with error %ld\n", error);

    pDacl = NULL;
    error = GetNamedSecurityInfoA(windows_dir, SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,
        NULL, NULL, &pDacl, NULL, &pSD);
    ok(!error, "GetNamedSecurityInfo failed with error %ld\n", error);
    ok(pDacl != NULL, "DACL should not be NULL\n");
    LocalFree(pSD);

    error = GetNamedSecurityInfoA(windows_dir, SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,
        NULL, NULL, &pDacl, NULL, NULL);
    ok(error==ERROR_INVALID_PARAMETER, "GetNamedSecurityInfo failed with error %ld\n", error);

    /* Test behavior of SetNamedSecurityInfo with an invalid path */
    SetLastError(0xdeadbeef);
    error = SetNamedSecurityInfoA(invalid_path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL,
                                  NULL, NULL, NULL);
    ok(error == ERROR_FILE_NOT_FOUND, "Unexpected error returned: 0x%lx\n", error);
    ok(GetLastError() == 0xdeadbeef, "Expected last error to remain unchanged.\n");

    /* Create security descriptor information and test that it comes back the same */
    pSD = &sd;
    pDacl = malloc(100);
    InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION);
    CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, admin_sid, &sid_size);
    bret = InitializeAcl(pDacl, 100, ACL_REVISION);
    ok(bret, "Failed to initialize ACL.\n");
    bret = AddAccessAllowedAceEx(pDacl, ACL_REVISION, 0, GENERIC_ALL, user_sid);
    ok(bret, "Failed to add Current User to ACL.\n");
    bret = AddAccessAllowedAceEx(pDacl, ACL_REVISION, 0, GENERIC_ALL, admin_sid);
    ok(bret, "Failed to add Administrator Group to ACL.\n");
    bret = SetSecurityDescriptorDacl(pSD, TRUE, pDacl, FALSE);
    ok(bret, "Failed to add ACL to security descriptor.\n");
    GetTempFileNameA(".", "foo", 0, tmpfile);
    hTemp = CreateFileA(tmpfile, WRITE_DAC|GENERIC_WRITE, FILE_SHARE_DELETE|FILE_SHARE_READ,
                        NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    SetLastError(0xdeadbeef);
    error = SetNamedSecurityInfoA(tmpfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL,
                                  NULL, pDacl, NULL);
    free(pDacl);
    if (error != ERROR_SUCCESS && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED))
    {
        win_skip("SetNamedSecurityInfoA is not implemented\n");
        free(user);
        CloseHandle(hTemp);
        return;
    }
    ok(!error, "SetNamedSecurityInfoA failed with error %ld\n", error);
    SetLastError(0xdeadbeef);
    error = GetNamedSecurityInfoA(tmpfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                   NULL, NULL, &pDacl, NULL, &pSD);
    if (error != ERROR_SUCCESS && (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED))
    {
        win_skip("GetNamedSecurityInfoA is not implemented\n");
        free(user);
        CloseHandle(hTemp);
        return;
    }
    ok(!error, "GetNamedSecurityInfo failed with error %ld\n", error);
#ifdef __REACTOS__
    if (error) goto file_security_done;
#endif

    bret = GetAclInformation(pDacl, &acl_size, sizeof(acl_size), AclSizeInformation);
    ok(bret, "GetAclInformation failed\n");
    if (acl_size.AceCount > 0)
    {
        bret = GetAce(pDacl, 0, (VOID **)&ace);
        ok(bret, "Failed to get Current User ACE.\n");
        bret = EqualSid(&ace->SidStart, user_sid);
        todo_wine ok(bret, "Current User ACE (%s) != Current User SID (%s).\n",
                     debugstr_sid(&ace->SidStart), debugstr_sid(user_sid));
        ok(((ACE_HEADER *)ace)->AceFlags == 0,
           "Current User ACE has unexpected flags (0x%x != 0x0)\n", ((ACE_HEADER *)ace)->AceFlags);
        ok(ace->Mask == 0x1f01ff, "Current User ACE has unexpected mask (0x%lx != 0x1f01ff)\n",
                                  ace->Mask);
    }
    if (acl_size.AceCount > 1)
    {
        bret = GetAce(pDacl, 1, (VOID **)&ace);
        ok(bret, "Failed to get Administators Group ACE.\n");
        bret = EqualSid(&ace->SidStart, admin_sid);
        todo_wine ok(bret || broken(!bret) /* win2k */,
                     "Administators Group ACE (%s) != Administators Group SID (%s).\n",
                     debugstr_sid(&ace->SidStart), debugstr_sid(admin_sid));
        ok(((ACE_HEADER *)ace)->AceFlags == 0,
           "Administators Group ACE has unexpected flags (0x%x != 0x0)\n", ((ACE_HEADER *)ace)->AceFlags);
        ok(ace->Mask == 0x1f01ff || broken(ace->Mask == GENERIC_ALL) /* win2k */,
           "Administators Group ACE has unexpected mask (0x%lx != 0x1f01ff)\n", ace->Mask);
    }
    LocalFree(pSD);

    /* show that setting empty DACL is not removing all file permissions */
    pDacl = malloc(sizeof(ACL));
    bret = InitializeAcl(pDacl, sizeof(ACL), ACL_REVISION);
    ok(bret, "Failed to initialize ACL.\n");
    error = SetNamedSecurityInfoA(tmpfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            NULL, NULL, pDacl, NULL);
    ok(!error, "SetNamedSecurityInfoA failed with error %ld\n", error);
    free(pDacl);

    error = GetNamedSecurityInfoA(tmpfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            NULL, NULL, &pDacl, NULL, &pSD);
    ok(!error, "GetNamedSecurityInfo failed with error %ld\n", error);
#ifdef __REACTOS__
    if (error) goto file_security_done;
#endif

    bret = GetAclInformation(pDacl, &acl_size, sizeof(acl_size), AclSizeInformation);
    ok(bret, "GetAclInformation failed\n");
    if (acl_size.AceCount > 0)
    {
        bret = GetAce(pDacl, 0, (VOID **)&ace);
        ok(bret, "Failed to get ACE.\n");
        todo_wine ok(((ACE_HEADER *)ace)->AceFlags & INHERITED_ACE,
                "ACE has unexpected flags: 0x%x\n", ((ACE_HEADER *)ace)->AceFlags);
    }
    LocalFree(pSD);

    h = CreateFileA(tmpfile, GENERIC_READ, FILE_SHARE_DELETE|FILE_SHARE_WRITE|FILE_SHARE_READ,
            NULL, OPEN_EXISTING, 0, NULL);
    ok(h != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());
    CloseHandle(h);

    /* test setting NULL DACL */
    error = SetNamedSecurityInfoA(tmpfile, SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL);
    ok(!error, "SetNamedSecurityInfoA failed with error %ld\n", error);

    error = GetNamedSecurityInfoA(tmpfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                NULL, NULL, &pDacl, NULL, &pSD);
    ok(!error, "GetNamedSecurityInfo failed with error %ld\n", error);
#ifdef __REACTOS__
    if (error) goto file_security_done;
#endif
    todo_wine ok(!pDacl, "pDacl != NULL\n");
    LocalFree(pSD);

    h = CreateFileA(tmpfile, GENERIC_READ, FILE_SHARE_DELETE|FILE_SHARE_WRITE|FILE_SHARE_READ,
            NULL, OPEN_EXISTING, 0, NULL);
    ok(h != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());
    CloseHandle(h);

    /* NtSetSecurityObject doesn't inherit DACL entries */
    pSD = sd+sizeof(void*)-((ULONG_PTR)sd)%sizeof(void*);
    InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION);
    pDacl = malloc(100);
    bret = InitializeAcl(pDacl, sizeof(ACL), ACL_REVISION);
    ok(bret, "Failed to initialize ACL.\n");
    bret = SetSecurityDescriptorDacl(pSD, TRUE, pDacl, FALSE);
    ok(bret, "Failed to add ACL to security descriptor.\n");
    status = NtSetSecurityObject(hTemp, DACL_SECURITY_INFORMATION, pSD);
    ok(status == ERROR_SUCCESS, "NtSetSecurityObject returned %lx\n", status);

    h = CreateFileA(tmpfile, GENERIC_READ, FILE_SHARE_DELETE|FILE_SHARE_WRITE|FILE_SHARE_READ,
            NULL, OPEN_EXISTING, 0, NULL);
    ok(h == INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());
    CloseHandle(h);

    SetSecurityDescriptorControl(pSD, SE_DACL_AUTO_INHERIT_REQ, SE_DACL_AUTO_INHERIT_REQ);
    status = NtSetSecurityObject(hTemp, DACL_SECURITY_INFORMATION, pSD);
    ok(status == ERROR_SUCCESS, "NtSetSecurityObject returned %lx\n", status);

    h = CreateFileA(tmpfile, GENERIC_READ, FILE_SHARE_DELETE|FILE_SHARE_WRITE|FILE_SHARE_READ,
            NULL, OPEN_EXISTING, 0, NULL);
    ok(h == INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());
    CloseHandle(h);

    SetSecurityDescriptorControl(pSD, SE_DACL_AUTO_INHERIT_REQ|SE_DACL_AUTO_INHERITED,
            SE_DACL_AUTO_INHERIT_REQ|SE_DACL_AUTO_INHERITED);
    status = NtSetSecurityObject(hTemp, DACL_SECURITY_INFORMATION, pSD);
    ok(status == ERROR_SUCCESS, "NtSetSecurityObject returned %lx\n", status);

    h = CreateFileA(tmpfile, GENERIC_READ, FILE_SHARE_DELETE|FILE_SHARE_WRITE|FILE_SHARE_READ,
            NULL, OPEN_EXISTING, 0, NULL);
    ok(h == INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());
    CloseHandle(h);

    /* test if DACL is properly mapped to permission */
    bret = InitializeAcl(pDacl, 100, ACL_REVISION);
    ok(bret, "Failed to initialize ACL.\n");
    bret = AddAccessAllowedAceEx(pDacl, ACL_REVISION, 0, GENERIC_ALL, user_sid);
    ok(bret, "Failed to add Current User to ACL.\n");
    bret = AddAccessDeniedAceEx(pDacl, ACL_REVISION, 0, GENERIC_ALL, user_sid);
    ok(bret, "Failed to add Current User to ACL.\n");
    bret = SetSecurityDescriptorDacl(pSD, TRUE, pDacl, FALSE);
    ok(bret, "Failed to add ACL to security descriptor.\n");
    status = NtSetSecurityObject(hTemp, DACL_SECURITY_INFORMATION, pSD);
    ok(status == ERROR_SUCCESS, "NtSetSecurityObject returned %lx\n", status);

    h = CreateFileA(tmpfile, GENERIC_READ, FILE_SHARE_DELETE|FILE_SHARE_WRITE|FILE_SHARE_READ,
            NULL, OPEN_EXISTING, 0, NULL);
    ok(h != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());
    CloseHandle(h);

    bret = InitializeAcl(pDacl, 100, ACL_REVISION);
    ok(bret, "Failed to initialize ACL.\n");
    bret = AddAccessDeniedAceEx(pDacl, ACL_REVISION, 0, GENERIC_ALL, user_sid);
    ok(bret, "Failed to add Current User to ACL.\n");
    bret = AddAccessAllowedAceEx(pDacl, ACL_REVISION, 0, GENERIC_ALL, user_sid);
    ok(bret, "Failed to add Current User to ACL.\n");
    bret = SetSecurityDescriptorDacl(pSD, TRUE, pDacl, FALSE);
    ok(bret, "Failed to add ACL to security descriptor.\n");
    status = NtSetSecurityObject(hTemp, DACL_SECURITY_INFORMATION, pSD);
    ok(status == ERROR_SUCCESS, "NtSetSecurityObject returned %lx\n", status);

    h = CreateFileA(tmpfile, GENERIC_READ, FILE_SHARE_DELETE|FILE_SHARE_WRITE|FILE_SHARE_READ,
            NULL, OPEN_EXISTING, 0, NULL);
    ok(h == INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());
    free(pDacl);
#ifdef __REACTOS__
file_security_done:
#endif
    free(user);
    CloseHandle(hTemp);

    /* Test querying the ownership of a built-in registry key */
    sid_size = sizeof(system_ptr);
    CreateWellKnownSid(WinLocalSystemSid, NULL, system_sid, &sid_size);
    error = GetNamedSecurityInfoA(software_key, SE_REGISTRY_KEY,
                                   OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION,
                                   NULL, NULL, NULL, NULL, &pSD);
    ok(!error, "GetNamedSecurityInfo failed with error %ld\n", error);

    bret = AllocateAndInitializeSid(&SIDAuthNT, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &localsys_sid);
    ok(bret, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    bret = GetSecurityDescriptorOwner(pSD, &owner, &owner_defaulted);
    ok(bret, "GetSecurityDescriptorOwner failed with error %ld\n", GetLastError());
    ok(owner != NULL, "owner should not be NULL\n");
    ok(EqualSid(owner, admin_sid) || EqualSid(owner, localsys_sid),
                "MACHINE\\Software owner SID (%s) != Administrators SID (%s) or Local System Sid (%s).\n",
                debugstr_sid(owner), debugstr_sid(admin_sid), debugstr_sid(localsys_sid));

    bret = GetSecurityDescriptorGroup(pSD, &group, &group_defaulted);
    ok(bret, "GetSecurityDescriptorGroup failed with error %ld\n", GetLastError());
    ok(group != NULL, "group should not be NULL\n");
    ok(EqualSid(group, admin_sid) || broken(EqualSid(group, system_sid)) /* before Win7 */
       || broken(((SID*)group)->SubAuthority[0] == SECURITY_NT_NON_UNIQUE) /* Vista */,
       "MACHINE\\Software group SID (%s) != Local System SID (%s or %s)\n",
       debugstr_sid(group), debugstr_sid(admin_sid), debugstr_sid(system_sid));
    LocalFree(pSD);

    /* Test querying the DACL of a built-in registry key */
    sid_size = sizeof(users_ptr);
    CreateWellKnownSid(WinBuiltinUsersSid, NULL, users_sid, &sid_size);
    error = GetNamedSecurityInfoA(software_key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION,
                                   NULL, NULL, NULL, NULL, &pSD);
    ok(!error, "GetNamedSecurityInfo failed with error %ld\n", error);

    bret = GetSecurityDescriptorDacl(pSD, &dacl_present, &pDacl, &dacl_defaulted);
    ok(bret, "GetSecurityDescriptorDacl failed with error %ld\n", GetLastError());
    ok(dacl_present, "DACL should be present\n");
    ok(pDacl && IsValidAcl(pDacl), "GetSecurityDescriptorDacl returned invalid DACL.\n");
    bret = GetAclInformation(pDacl, &acl_size, sizeof(acl_size), AclSizeInformation);
    ok(bret, "GetAclInformation failed\n");
    ok(acl_size.AceCount != 0, "GetAclInformation returned no ACLs\n");
    for (i=0; i<acl_size.AceCount; i++)
    {
        bret = GetAce(pDacl, i, (VOID **)&ace);
        ok(bret, "Failed to get ACE %d.\n", i);
        bret = EqualSid(&ace->SidStart, users_sid);
        if (bret) users_ace_id = i;
        bret = EqualSid(&ace->SidStart, admin_sid);
        if (bret) admins_ace_id = i;
    }
    ok(users_ace_id != -1 || broken(users_ace_id == -1) /* win2k */,
       "Builtin Users ACE not found.\n");
    if (users_ace_id != -1)
    {
        bret = GetAce(pDacl, users_ace_id, (VOID **)&ace);
        ok(bret, "Failed to get Builtin Users ACE.\n");
        flags = ((ACE_HEADER *)ace)->AceFlags;
        ok(flags == (INHERIT_ONLY_ACE|CONTAINER_INHERIT_ACE)
           || broken(flags == (INHERIT_ONLY_ACE|CONTAINER_INHERIT_ACE|INHERITED_ACE)) /* w2k8 */
           || broken(flags == (CONTAINER_INHERIT_ACE|INHERITED_ACE)) /* win 10 wow64 */
           || broken(flags == CONTAINER_INHERIT_ACE), /* win 10 */
           "Builtin Users ACE has unexpected flags (0x%x != 0x%x)\n", flags,
           INHERIT_ONLY_ACE|CONTAINER_INHERIT_ACE);
#ifdef __REACTOS__
        ok(ace->Mask == GENERIC_READ
           || broken(ace->Mask == KEY_READ), /* win 10 */
           "Builtin Users ACE has unexpected mask (0x%lx != 0x%lx)\n",
                                      ace->Mask, GENERIC_READ);
#else
        ok(ace->Mask == GENERIC_READ
           || broken(ace->Mask == KEY_READ), /* win 10 */
           "Builtin Users ACE has unexpected mask (0x%lx != 0x%x)\n",
                                      ace->Mask, GENERIC_READ);
#endif
    }
    ok(admins_ace_id != -1, "Builtin Admins ACE not found.\n");
    if (admins_ace_id != -1)
    {
        bret = GetAce(pDacl, admins_ace_id, (VOID **)&ace);
        ok(bret, "Failed to get Builtin Admins ACE.\n");
        flags = ((ACE_HEADER *)ace)->AceFlags;
        ok(flags == 0x0
           || broken(flags == (INHERIT_ONLY_ACE|CONTAINER_INHERIT_ACE|INHERITED_ACE)) /* w2k8 */
           || broken(flags == (OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE)) /* win7 */
           || broken(flags == (INHERIT_ONLY_ACE|CONTAINER_INHERIT_ACE)) /* win8+ */
           || broken(flags == (CONTAINER_INHERIT_ACE|INHERITED_ACE)) /* win 10 wow64 */
           || broken(flags == CONTAINER_INHERIT_ACE), /* win 10 */
           "Builtin Admins ACE has unexpected flags (0x%x != 0x0)\n", flags);
        ok(ace->Mask == KEY_ALL_ACCESS || broken(ace->Mask == GENERIC_ALL) /* w2k8 */,
           "Builtin Admins ACE has unexpected mask (0x%lx != 0x%x)\n", ace->Mask, KEY_ALL_ACCESS);
    }

    FreeSid(localsys_sid);
    LocalFree(pSD);
}

static void test_ConvertStringSecurityDescriptor(void)
{
    BOOL ret;
    PSECURITY_DESCRIPTOR pSD;
    static const WCHAR Blank[] = { 0 };
    unsigned int i;
    ULONG size;
    ACL *acl;
    static const struct
    {
        const char *sidstring;
        DWORD      revision;
        BOOL       ret;
        DWORD      GLE;
        DWORD      altGLE;
        DWORD      ace_Mask;
    } cssd[] =
    {
        { "D:(A;;GA;;;WD)",                  0xdeadbeef,      FALSE, ERROR_UNKNOWN_REVISION },
        /* test ACE string type */
        { "D:(A;;GA;;;WD)",                  SDDL_REVISION_1, TRUE },
        { "D:(D;;GA;;;WD)",                  SDDL_REVISION_1, TRUE },
        { "ERROR:(D;;GA;;;WD)",              SDDL_REVISION_1, FALSE, ERROR_INVALID_PARAMETER },
        /* test ACE string with spaces */
        { " D:(D;;GA;;;WD)",                SDDL_REVISION_1, TRUE },
        { "D: (D;;GA;;;WD)",                SDDL_REVISION_1, TRUE },
        { "D:( D;;GA;;;WD)",                SDDL_REVISION_1, TRUE },
        { "D:(D ;;GA;;;WD)",                SDDL_REVISION_1, FALSE, RPC_S_INVALID_STRING_UUID, ERROR_INVALID_ACL }, /* Vista+ */
        { "D:(D; ;GA;;;WD)",                SDDL_REVISION_1, TRUE },
        { "D:(D;; GA;;;WD)",                SDDL_REVISION_1, TRUE },
        { "D:(D;;GA ;;;WD)",                SDDL_REVISION_1, FALSE, ERROR_INVALID_ACL },
        { "D:(D;;GA; ;;WD)",                SDDL_REVISION_1, TRUE },
        { "D:(D;;GA;; ;WD)",                SDDL_REVISION_1, TRUE },
        { "D:(D;;GA;;; WD)",                SDDL_REVISION_1, TRUE },
        { "D:(D;;GA;;;WD )",                SDDL_REVISION_1, TRUE },
        /* test ACE string access rights */
        { "D:(A;;GA;;;WD)",                  SDDL_REVISION_1, TRUE, 0, 0, GENERIC_ALL },
        { "D:(A;;1;;;WD)",                   SDDL_REVISION_1, TRUE, 0, 0, 1 },
        { "D:(A;;020000000000;;;WD)",        SDDL_REVISION_1, TRUE, 0, 0, GENERIC_READ },
        { "D:(A;;0X40000000;;;WD)",          SDDL_REVISION_1, TRUE, 0, 0, GENERIC_WRITE },
        { "D:(A;;GRGWGX;;;WD)",              SDDL_REVISION_1, TRUE, 0, 0, GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE },
        { "D:(A;;RCSDWDWO;;;WD)",            SDDL_REVISION_1, TRUE, 0, 0, READ_CONTROL | DELETE | WRITE_DAC | WRITE_OWNER },
        { "D:(A;;RPWPCCDCLCSWLODTCR;;;WD)",  SDDL_REVISION_1, TRUE },
        { "D:(A;;FAFRFWFX;;;WD)",            SDDL_REVISION_1, TRUE },
        { "D:(A;;KAKRKWKX;;;WD)",            SDDL_REVISION_1, TRUE },
        { "D:(A;;0xFFFFFFFF;;;WD)",          SDDL_REVISION_1, TRUE },
        { "S:(AU;;0xFFFFFFFF;;;WD)",         SDDL_REVISION_1, TRUE },
        { "S:(AU;;0xDeAdBeEf;;;WD)",         SDDL_REVISION_1, TRUE },
        { "S:(AU;;GR0xFFFFFFFF;;;WD)",       SDDL_REVISION_1, TRUE },
        { "S:(AU;;0xFFFFFFFFGR;;;WD)",       SDDL_REVISION_1, TRUE },
        { "S:(AU;;0xFFFFFGR;;;WD)",          SDDL_REVISION_1, TRUE },
        /* test ACE string access right error case */
        { "D:(A;;ROB;;;WD)",                 SDDL_REVISION_1, FALSE, ERROR_INVALID_ACL },
        /* test behaviour with empty strings */
        { "",                                SDDL_REVISION_1, TRUE },
        /* test ACE string SID */
        { "D:(D;;GA;;;S-1-0-0)",             SDDL_REVISION_1, TRUE },
        { "D:(D;;GA;;;WDANDSUCH)",           SDDL_REVISION_1, FALSE, ERROR_INVALID_ACL },
        { "D:(D;;GA;;;Nonexistent account)", SDDL_REVISION_1, FALSE, ERROR_INVALID_ACL, ERROR_INVALID_SID }, /* W2K */
    };

    for (i = 0; i < ARRAY_SIZE(cssd); i++)
    {
        DWORD GLE;

        SetLastError(0xdeadbeef);
        ret = ConvertStringSecurityDescriptorToSecurityDescriptorA(
            cssd[i].sidstring, cssd[i].revision, &pSD, NULL);
        GLE = GetLastError();
        ok(ret == cssd[i].ret, "(%02u) Expected %s (%ld)\n", i, cssd[i].ret ? "success" : "failure", GLE);
        if (!cssd[i].ret)
            ok(GLE == cssd[i].GLE ||
               (cssd[i].altGLE && GLE == cssd[i].altGLE),
               "(%02u) Unexpected last error %ld\n", i, GLE);
        if (ret)
        {
            if (cssd[i].ace_Mask)
            {
                ACCESS_ALLOWED_ACE *ace;

                acl = (ACL *)((char *)pSD + sizeof(SECURITY_DESCRIPTOR_RELATIVE));
                ok(acl->AclRevision == ACL_REVISION, "(%02u) Got %u\n", i, acl->AclRevision);

                ace = (ACCESS_ALLOWED_ACE *)(acl + 1);
                ok(ace->Mask == cssd[i].ace_Mask, "(%02u) Expected %08lx, got %08lx\n",
                   i, cssd[i].ace_Mask, ace->Mask);
            }
            LocalFree(pSD);
        }
    }

    /* test behaviour with NULL parameters */
    SetLastError(0xdeadbeef);
    ret = ConvertStringSecurityDescriptorToSecurityDescriptorA(
        NULL, 0xdeadbeef, &pSD, NULL);
    todo_wine
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER,
        "ConvertStringSecurityDescriptorToSecurityDescriptor should have failed with ERROR_INVALID_PARAMETER instead of %ld\n",
        GetLastError());

    SetLastError(0xdeadbeef);
    ret = ConvertStringSecurityDescriptorToSecurityDescriptorW(
        NULL, 0xdeadbeef, &pSD, NULL);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER,
        "ConvertStringSecurityDescriptorToSecurityDescriptor should have failed with ERROR_INVALID_PARAMETER instead of %ld\n",
        GetLastError());

    SetLastError(0xdeadbeef);
    ret = ConvertStringSecurityDescriptorToSecurityDescriptorA(
        "D:(A;;ROB;;;WD)", 0xdeadbeef, NULL, NULL);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER,
        "ConvertStringSecurityDescriptorToSecurityDescriptor should have failed with ERROR_INVALID_PARAMETER instead of %ld\n",
        GetLastError());

    SetLastError(0xdeadbeef);
    ret = ConvertStringSecurityDescriptorToSecurityDescriptorA(
        "D:(A;;ROB;;;WD)", SDDL_REVISION_1, NULL, NULL);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER,
        "ConvertStringSecurityDescriptorToSecurityDescriptor should have failed with ERROR_INVALID_PARAMETER instead of %ld\n",
        GetLastError());

    /* test behaviour with empty strings */
    SetLastError(0xdeadbeef);
    ret = ConvertStringSecurityDescriptorToSecurityDescriptorW(
        Blank, SDDL_REVISION_1, &pSD, NULL);
    ok(ret, "ConvertStringSecurityDescriptorToSecurityDescriptor failed with error %ld\n", GetLastError());
    LocalFree(pSD);

    SetLastError(0xdeadbeef);
    ret = ConvertStringSecurityDescriptorToSecurityDescriptorA(
        "D:P(A;;GRGW;;;BA)(A;;GRGW;;;S-1-5-21-0-0-0-1000)S:(ML;;NWNR;;;S-1-16-12288)", SDDL_REVISION_1, &pSD, NULL);
    ok(ret || broken(!ret && GetLastError() == ERROR_INVALID_DATATYPE) /* win2k */,
       "ConvertStringSecurityDescriptorToSecurityDescriptor failed with error %lu\n", GetLastError());
    if (ret) LocalFree(pSD);

    SetLastError(0xdeadbeef);
    ret = ConvertStringSecurityDescriptorToSecurityDescriptorA(
        "D: (D;OICI;GA;;;BG) (D;OICI;GA;;;AN) (A;OICI;GAGRGWGX;;;AU) (A;OICI;GA;;;BA)", SDDL_REVISION_1, &pSD, NULL);
    ok(ret || broken(!ret && GetLastError() == ERROR_INVALID_DATATYPE) /* win2k */,
       "ConvertStringSecurityDescriptorToSecurityDescriptor failed with error %lu\n", GetLastError());
    acl = (ACL *)((char *)pSD + sizeof(SECURITY_DESCRIPTOR_RELATIVE));
    ok(acl->AclSize == sizeof(*acl) * 12 /* 96 */, "got %u\n", acl->AclSize);
    ok(acl->AceCount = 4, "got %u\n", acl->AceCount);
    if (ret) LocalFree(pSD);

    /* empty DACL */
    size = 0;
    SetLastError(0xdeadbeef);
    ret = ConvertStringSecurityDescriptorToSecurityDescriptorA("D:", SDDL_REVISION_1, &pSD, &size);
    ok(ret, "unexpected error %lu\n", GetLastError());
    ok(size == sizeof(SECURITY_DESCRIPTOR_RELATIVE) + sizeof(ACL), "got %lu\n", size);
    acl = (ACL *)((char *)pSD + sizeof(SECURITY_DESCRIPTOR_RELATIVE));
    ok(acl->AclRevision == ACL_REVISION, "got %u\n", acl->AclRevision);
    ok(!acl->Sbz1, "got %u\n", acl->Sbz1);
    ok(acl->AclSize == sizeof(*acl), "got %u\n", acl->AclSize);
    ok(!acl->AceCount, "got %u\n", acl->AceCount);
    ok(!acl->Sbz2, "got %u\n", acl->Sbz2);
    LocalFree(pSD);

    /* empty SACL */
    size = 0;
    SetLastError(0xdeadbeef);
    ret = ConvertStringSecurityDescriptorToSecurityDescriptorA("S:", SDDL_REVISION_1, &pSD, &size);
    ok(ret, "unexpected error %lu\n", GetLastError());
    ok(size == sizeof(SECURITY_DESCRIPTOR_RELATIVE) + sizeof(ACL), "got %lu\n", size);
    acl = (ACL *)((char *)pSD + sizeof(SECURITY_DESCRIPTOR_RELATIVE));
    ok(!acl->Sbz1, "got %u\n", acl->Sbz1);
    ok(acl->AclSize == sizeof(*acl), "got %u\n", acl->AclSize);
    ok(!acl->AceCount, "got %u\n", acl->AceCount);
    ok(!acl->Sbz2, "got %u\n", acl->Sbz2);
    LocalFree(pSD);
}

static void test_ConvertSecurityDescriptorToString(void)
{
    SECURITY_DESCRIPTOR desc;
    SECURITY_INFORMATION sec_info = OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|SACL_SECURITY_INFORMATION;
    LPSTR string;
    DWORD size;
    PSID psid, psid2;
    PACL pacl;
    char sid_buf[256];
    char acl_buf[8192];
    ULONG len;

/* It seems Windows XP adds an extra character to the length of the string for each ACE in an ACL. We
 * don't replicate this feature so we only test len >= strlen+1. */
#define CHECK_RESULT_AND_FREE(exp_str) \
    ok(strcmp(string, (exp_str)) == 0, "String mismatch (expected \"%s\", got \"%s\")\n", (exp_str), string); \
    ok(len >= (strlen(exp_str) + 1), "Length mismatch (expected %d, got %ld)\n", lstrlenA(exp_str) + 1, len); \
    LocalFree(string);

#define CHECK_ONE_OF_AND_FREE(exp_str1, exp_str2) \
    ok(strcmp(string, (exp_str1)) == 0 || strcmp(string, (exp_str2)) == 0, "String mismatch (expected\n\"%s\" or\n\"%s\", got\n\"%s\")\n", (exp_str1), (exp_str2), string); \
    ok(len >= (strlen(exp_str1) + 1) || len >= (strlen(exp_str2) + 1), "Length mismatch (expected %d or %d, got %ld)\n", lstrlenA(exp_str1) + 1, lstrlenA(exp_str2) + 1, len); \
    LocalFree(string);

    InitializeSecurityDescriptor(&desc, SECURITY_DESCRIPTOR_REVISION);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("");

    size = 4096;
    CreateWellKnownSid(WinLocalSid, NULL, sid_buf, &size);
    SetSecurityDescriptorOwner(&desc, sid_buf, FALSE);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("O:S-1-2-0");

    SetSecurityDescriptorOwner(&desc, sid_buf, TRUE);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("O:S-1-2-0");

    size = sizeof(sid_buf);
    CreateWellKnownSid(WinLocalSystemSid, NULL, sid_buf, &size);
    SetSecurityDescriptorOwner(&desc, sid_buf, TRUE);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("O:SY");

    ConvertStringSidToSidA("S-1-5-21-93476-23408-4576", &psid);
    SetSecurityDescriptorGroup(&desc, psid, TRUE);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("O:SYG:S-1-5-21-93476-23408-4576");

    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, GROUP_SECURITY_INFORMATION, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("G:S-1-5-21-93476-23408-4576");

    pacl = (PACL)acl_buf;
    InitializeAcl(pacl, sizeof(acl_buf), ACL_REVISION);
    SetSecurityDescriptorDacl(&desc, TRUE, pacl, TRUE);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("O:SYG:S-1-5-21-93476-23408-4576D:");

    SetSecurityDescriptorDacl(&desc, TRUE, pacl, FALSE);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("O:SYG:S-1-5-21-93476-23408-4576D:");

    ConvertStringSidToSidA("S-1-5-6", &psid2);
    AddAccessAllowedAceEx(pacl, ACL_REVISION, NO_PROPAGATE_INHERIT_ACE, 0xf0000000, psid2);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("O:SYG:S-1-5-21-93476-23408-4576D:(A;NP;GAGXGWGR;;;SU)");

    AddAccessAllowedAceEx(pacl, ACL_REVISION, INHERIT_ONLY_ACE|INHERITED_ACE, 0x00000003, psid2);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("O:SYG:S-1-5-21-93476-23408-4576D:(A;NP;GAGXGWGR;;;SU)(A;IOID;CCDC;;;SU)");

    AddAccessDeniedAceEx(pacl, ACL_REVISION, OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE, 0xffffffff, psid);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("O:SYG:S-1-5-21-93476-23408-4576D:(A;NP;GAGXGWGR;;;SU)(A;IOID;CCDC;;;SU)(D;OICI;0xffffffff;;;S-1-5-21-93476-23408-4576)");


    pacl = (PACL)acl_buf;
    InitializeAcl(pacl, sizeof(acl_buf), ACL_REVISION);
    SetSecurityDescriptorSacl(&desc, TRUE, pacl, FALSE);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("O:SYG:S-1-5-21-93476-23408-4576D:S:");

    /* fails in win2k */
    SetSecurityDescriptorDacl(&desc, TRUE, NULL, FALSE);
    AddAuditAccessAceEx(pacl, ACL_REVISION, VALID_INHERIT_FLAGS, KEY_READ|KEY_WRITE, psid2, TRUE, TRUE);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_ONE_OF_AND_FREE("O:SYG:S-1-5-21-93476-23408-4576D:S:(AU;OICINPIOIDSAFA;CCDCLCSWRPRC;;;SU)", /* XP */
        "O:SYG:S-1-5-21-93476-23408-4576D:NO_ACCESS_CONTROLS:(AU;OICINPIOIDSAFA;CCDCLCSWRPRC;;;SU)" /* Vista */);

    /* fails in win2k */
    AddAuditAccessAceEx(pacl, ACL_REVISION, NO_PROPAGATE_INHERIT_ACE, FILE_GENERIC_READ|FILE_GENERIC_WRITE, psid2, TRUE, FALSE);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(&desc, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_ONE_OF_AND_FREE("O:SYG:S-1-5-21-93476-23408-4576D:S:(AU;OICINPIOIDSAFA;CCDCLCSWRPRC;;;SU)(AU;NPSA;0x12019f;;;SU)", /* XP */
        "O:SYG:S-1-5-21-93476-23408-4576D:NO_ACCESS_CONTROLS:(AU;OICINPIOIDSAFA;CCDCLCSWRPRC;;;SU)(AU;NPSA;0x12019f;;;SU)" /* Vista */);

    LocalFree(psid2);
    LocalFree(psid);
}

static void test_SetSecurityDescriptorControl (PSECURITY_DESCRIPTOR sec)
{
    SECURITY_DESCRIPTOR_CONTROL ref;
    SECURITY_DESCRIPTOR_CONTROL test;

    SECURITY_DESCRIPTOR_CONTROL const mutable
        = SE_DACL_AUTO_INHERIT_REQ | SE_SACL_AUTO_INHERIT_REQ
        | SE_DACL_AUTO_INHERITED   | SE_SACL_AUTO_INHERITED
        | SE_DACL_PROTECTED        | SE_SACL_PROTECTED
        | 0x00000040               | 0x00000080        /* not defined in winnt.h */
        ;
    SECURITY_DESCRIPTOR_CONTROL const immutable
        = SE_OWNER_DEFAULTED       | SE_GROUP_DEFAULTED
        | SE_DACL_PRESENT          | SE_DACL_DEFAULTED
        | SE_SACL_PRESENT          | SE_SACL_DEFAULTED
        | SE_RM_CONTROL_VALID      | SE_SELF_RELATIVE
        ;

    int     bit;
    DWORD   dwRevision;
    LPCSTR  fmt = "Expected error %s, got %u\n";

    GetSecurityDescriptorControl (sec, &ref, &dwRevision);

    /* The mutable bits are mutable regardless of the truth of
       SE_DACL_PRESENT and/or SE_SACL_PRESENT */

    /* Check call barfs if any bit-of-interest is immutable */
    for (bit = 0; bit < 16; ++bit)
    {
        SECURITY_DESCRIPTOR_CONTROL const bitOfInterest = 1 << bit;
        SECURITY_DESCRIPTOR_CONTROL setOrClear = ref & bitOfInterest;

        SECURITY_DESCRIPTOR_CONTROL ctrl;

        DWORD   dwExpect  = (bitOfInterest & immutable)
                          ?  ERROR_INVALID_PARAMETER  :  0xbebecaca;
        LPCSTR  strExpect = (bitOfInterest & immutable)
                          ? "ERROR_INVALID_PARAMETER" : "0xbebecaca";

        ctrl = (bitOfInterest & mutable) ? ref + bitOfInterest : ref;
        setOrClear ^= bitOfInterest;
        SetLastError (0xbebecaca);
        SetSecurityDescriptorControl (sec, bitOfInterest, setOrClear);
        ok (GetLastError () == dwExpect, fmt, strExpect, GetLastError ());
        GetSecurityDescriptorControl(sec, &test, &dwRevision);
        expect_eq(test, ctrl, int, "%x");

        setOrClear ^= bitOfInterest;
        SetLastError (0xbebecaca);
        SetSecurityDescriptorControl (sec, bitOfInterest, setOrClear);
        ok (GetLastError () == dwExpect, fmt, strExpect, GetLastError ());
        GetSecurityDescriptorControl (sec, &test, &dwRevision);
        expect_eq(test, ref, int, "%x");
    }

    /* Check call barfs if any bit-to-set is immutable
       even when not a bit-of-interest */
    for (bit = 0; bit < 16; ++bit)
    {
        SECURITY_DESCRIPTOR_CONTROL const bitsOfInterest = mutable;
        SECURITY_DESCRIPTOR_CONTROL setOrClear = ref & bitsOfInterest;

        SECURITY_DESCRIPTOR_CONTROL ctrl;

        DWORD   dwExpect  = ((1 << bit) & immutable)
                          ?  ERROR_INVALID_PARAMETER  :  0xbebecaca;
        LPCSTR  strExpect = ((1 << bit) & immutable)
                          ? "ERROR_INVALID_PARAMETER" : "0xbebecaca";

#ifdef __REACTOS__
        ctrl = ((1 << bit) & immutable) ? test : ref | mutable;
        setOrClear ^= bitsOfInterest;
        SetLastError (0xbebecaca);
        SetSecurityDescriptorControl (sec, bitsOfInterest, setOrClear | (1 << bit));
        ok (GetLastError () == dwExpect, fmt, strExpect, GetLastError ());
        GetSecurityDescriptorControl(sec, &test, &dwRevision);
        expect_eq(test, ctrl, int, "%x");

        ctrl = ((1 << bit) & immutable) ? test : ref | (1 << bit);
        setOrClear ^= bitsOfInterest;
        SetLastError (0xbebecaca);
        SetSecurityDescriptorControl (sec, bitsOfInterest, setOrClear | (1 << bit));
        ok (GetLastError () == dwExpect, fmt, strExpect, GetLastError ());
        GetSecurityDescriptorControl(sec, &test, &dwRevision);
        expect_eq(test, ctrl, int, "%x");
    }
}

static ACCESS_MASK private_object_access(PSECURITY_DESCRIPTOR sd, HANDLE token,
                                        GENERIC_MAPPING *mapping)
{
    PRIVILEGE_SET privileges;
    DWORD size, granted, result = 0, bit;
    BOOL access, ret;

    for (bit = 1; bit <= 4; bit <<= 1)
    {
        size = sizeof(privileges);
        granted = 0;
        access = FALSE;
        ret = AccessCheck(sd, token, bit, mapping, &privileges, &size, &granted, &access);
        ok(ret, "AccessCheck failed: %lu\n", GetLastError());
        if (ret && access) result |= bit;
    }
    return result;
}

static PSECURITY_DESCRIPTOR private_object_descriptor(const char *text)
{
    PSECURITY_DESCRIPTOR sd = NULL;
    BOOL ret;

    ret = ConvertStringSecurityDescriptorToSecurityDescriptorA(text, SDDL_REVISION_1, &sd, NULL);
    ok(ret, "Descriptor conversion failed: %lu\n", GetLastError());
    return sd;
}

static PACL private_object_dacl(PSECURITY_DESCRIPTOR sd, DWORD count,
                               SECURITY_DESCRIPTOR_CONTROL required)
{
    SECURITY_DESCRIPTOR_CONTROL control;
    DWORD revision;
    BOOL present, defaulted, ret;
    PACL acl = NULL;
    ACCESS_ALLOWED_ACE *ace;
    DWORD index;

    ret = GetSecurityDescriptorDacl(sd, &present, &acl, &defaulted);
    ok(ret && present && acl, "Missing DACL, error %lu\n", GetLastError());
    if (!ret || !present || !acl) return NULL;
    ok(acl->AceCount == count, "Got %u ACEs, expected %lu\n", acl->AceCount, count);
    if (acl->AceCount != count)
        for (index = 0; index < acl->AceCount; ++index)
            if (GetAce(acl, index, (void **)&ace))
                trace("Private DACL ACE %lu type %u flags %#x size %u mask %#lx\n",
                      index, ace->Header.AceType, ace->Header.AceFlags, ace->Header.AceSize, ace->Mask);
    ret = GetSecurityDescriptorControl(sd, &control, &revision);
    ok(ret && (control & required) == required, "Unexpected control %#x\n", control);
    return acl;
}

static void test_private_object_parent_inheritance(HANDLE token, GENERIC_MAPPING *mapping,
        BOOL (WINAPI *create_security)(PSECURITY_DESCRIPTOR, PSECURITY_DESCRIPTOR,
                                      PSECURITY_DESCRIPTOR *, GUID *, BOOL, ULONG, HANDLE, PGENERIC_MAPPING))
{
    SID world = {SID_REVISION, 1, {SECURITY_WORLD_SID_AUTHORITY}, {SECURITY_WORLD_RID}};
    union { ULONG_PTR align; BYTE bytes[sizeof(TOKEN_OWNER) + SECURITY_MAX_SID_SIZE]; } owner_buffer;
    union { ULONG_PTR align; BYTE bytes[sizeof(TOKEN_PRIMARY_GROUP) + SECURITY_MAX_SID_SIZE]; } group_buffer;
    TOKEN_OWNER *token_owner = (void *)owner_buffer.bytes;
    TOKEN_PRIMARY_GROUP *token_group = (void *)group_buffer.bytes;
    SECURITY_DESCRIPTOR parent_defaults;
    PSECURITY_DESCRIPTOR parent, result;
    SECURITY_DESCRIPTOR_CONTROL control;
    ACCESS_ALLOWED_ACE *ace;
    PSID owner, group;
    PACL acl;
    NTSTATUS status;
    DWORD api, inherit, defaults, i, revision, expected_control, acl_control, flags, size;
    BOOL ret, owner_defaulted, group_defaulted, present, defaulted;

    parent = private_object_descriptor("O:SYG:SYD:(D;OIIO;0x2;;;WD)(A;OICI;0x7;;;WD)");
    if (!parent) return;
    ret = GetTokenInformation(token, TokenOwner, owner_buffer.bytes, sizeof(owner_buffer), &size) &&
          GetTokenInformation(token, TokenPrimaryGroup, group_buffer.bytes, sizeof(group_buffer), &size) &&
          GetSecurityDescriptorDacl(parent, &present, &acl, &defaulted) && present && acl &&
          InitializeSecurityDescriptor(&parent_defaults, SECURITY_DESCRIPTOR_REVISION) &&
          SetSecurityDescriptorOwner(&parent_defaults, token_owner->Owner, FALSE) &&
          SetSecurityDescriptorGroup(&parent_defaults, token_group->PrimaryGroup, FALSE) &&
          SetSecurityDescriptorDacl(&parent_defaults, TRUE, acl, FALSE);
    ok(ret, "Constructor parent-default setup failed: %lu.\n", GetLastError());
    if (!ret) { LocalFree(parent); return; }
    for (api = 0; api < 2; ++api)
    {
        for (inherit = 0; inherit < 2; ++inherit)
        {
            for (defaults = 0; defaults < 2; ++defaults)
            {
                winetest_push_context("parent-only %s auto %lu defaults %s", api ? "RTL" : "public", inherit,
                                      defaults ? "parent" : "token");
                flags = (inherit ? SEF_DACL_AUTO_INHERIT : 0) |
                        (defaults ? SEF_DEFAULT_OWNER_FROM_PARENT | SEF_DEFAULT_GROUP_FROM_PARENT : 0);
                result = NULL;
                if (api)
                {
                    status = RtlNewSecurityObjectEx(defaults ? &parent_defaults : parent, NULL, &result, NULL,
                                                    FALSE, flags, token, mapping);
                    ok(!status && result, "Constructor returned %#lx, SD %p.\n", (DWORD)status, result);
                    ret = !status && result;
                }
                else
                {
                    ret = create_security(defaults ? &parent_defaults : parent, NULL, &result, NULL,
                                           FALSE, flags, token, mapping);
                    ok(ret && result, "Constructor returned %d, error %lu, SD %p.\n", ret, GetLastError(), result);
                }
                if (ret && result)
                {
                    ret = IsValidSecurityDescriptor(result);
                    ok(ret, "Constructor descriptor is invalid.\n");
                    if (!ret) goto release;
                    ret = GetSecurityDescriptorControl(result, &control, &revision);
                    ok(ret, "Constructor control query failed: %lu.\n", GetLastError());
                    if (ret)
                    {
                        ok(control & SE_SELF_RELATIVE, "Constructor descriptor is not self-relative: %#x.\n", control);
                        acl_control = SE_DACL_PRESENT | SE_DACL_DEFAULTED | SE_DACL_PROTECTED |
                                      SE_DACL_AUTO_INHERIT_REQ | SE_DACL_AUTO_INHERITED;
                        expected_control = SE_DACL_PRESENT | (inherit ? SE_DACL_AUTO_INHERITED : 0);
                        ok((control & acl_control) == expected_control, "DACL control %#lx, expected %#lx.\n",
                           control & acl_control, expected_control);
                        ok(control == (expected_control | SE_SELF_RELATIVE), "Constructor full control %#x, expected %#lx.\n",
                           control, expected_control | SE_SELF_RELATIVE);
                    }
                    ret = GetSecurityDescriptorOwner(result, &owner, &owner_defaulted) && owner && IsValidSid(owner);
                    ok(ret, "Constructor owner is invalid.\n");
                    if (ret && defaults) ok(EqualSid(owner, token_owner->Owner), "Constructor parent-default owner differs: %s.\n", debugstr_sid(owner));
                    ret = GetSecurityDescriptorGroup(result, &group, &group_defaulted) && group && IsValidSid(group);
                    ok(ret, "Constructor group is invalid.\n");
                    if (ret && defaults) ok(EqualSid(group, token_group->PrimaryGroup), "Constructor parent-default group differs: %s.\n", debugstr_sid(group));
                    acl = private_object_dacl(result, 2, SE_DACL_PRESENT);
                    if (!acl) goto release;
                    ret = IsValidAcl(acl);
                    ok(ret, "Constructor DACL is invalid.\n");
                    if (!ret) goto release;
                    for (i = 0; i < acl->AceCount && i < 2; ++i)
                    {
                        ret = GetAce(acl, i, (void **)&ace);
                        ok(ret, "Constructor ACE %lu query failed: %lu.\n", i, GetLastError());
                        if (!ret) continue;
                        ok(ace->Header.AceType == (i ? ACCESS_ALLOWED_ACE_TYPE : ACCESS_DENIED_ACE_TYPE),
                           "Constructor ACE %lu type %u.\n", i, ace->Header.AceType);
                        ok(ace->Header.AceFlags == (inherit ? INHERITED_ACE : 0),
                           "Constructor ACE %lu flags %#x, expected %#x.\n", i, ace->Header.AceFlags,
                           inherit ? INHERITED_ACE : 0);
                        ok(ace->Header.AceSize >= FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + GetLengthSid(&world),
                           "Constructor ACE %lu is too small: %u.\n", i, ace->Header.AceSize);
                        if (ace->Header.AceSize < FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + GetLengthSid(&world)) continue;
                        ok(ace->Mask == (i ? mapping->GenericAll : mapping->GenericWrite),
                           "Constructor ACE %lu mask %#lx.\n", i, ace->Mask);
                        ret = IsValidSid(&ace->SidStart);
                        ok(ret && EqualSid(&ace->SidStart, &world), "Constructor ACE %lu World SID differs.\n", i);
                    }
                }
release:
                if (result)
                {
                    if (api)
                    {
                        status = RtlDeleteSecurityObject(&result);
                        ok(!status, "RTL constructor cleanup returned %#lx.\n", (DWORD)status);
                    }
                    else
                        ok(DestroyPrivateObjectSecurity(&result), "Public constructor cleanup failed: %lu.\n", GetLastError());
                }
                winetest_pop_context();
            }
        }
    }
    LocalFree(parent);
}

static void test_private_object_inheritance(void)
{
    BOOL (WINAPI *pCreatePrivateObjectSecurityEx)(PSECURITY_DESCRIPTOR, PSECURITY_DESCRIPTOR,
        PSECURITY_DESCRIPTOR *, GUID *, BOOL, ULONG, HANDLE, PGENERIC_MAPPING);
    BOOL (WINAPI *pSetPrivateObjectSecurityEx)(SECURITY_INFORMATION, PSECURITY_DESCRIPTOR,
        PSECURITY_DESCRIPTOR *, ULONG, PGENERIC_MAPPING, HANDLE);
    BOOL (WINAPI *pConvertToAutoInheritPrivateObjectSecurity)(PSECURITY_DESCRIPTOR, PSECURITY_DESCRIPTOR,
        PSECURITY_DESCRIPTOR *, GUID *, BOOLEAN, PGENERIC_MAPPING);
    HMODULE advapi = GetModuleHandleA("advapi32.dll");
    static const ULONG avoid = SEF_AVOID_PRIVILEGE_CHECK | SEF_AVOID_OWNER_CHECK |
                               SEF_AVOID_OWNER_RESTRICTION;
    static const GENERIC_MAPPING file_mapping = {1, 2, 4, 7};
    GENERIC_MAPPING mapping = file_mapping;
    PSECURITY_DESCRIPTOR parent, creator, current, result = NULL, saved;
    SECURITY_DESCRIPTOR modification;
    SECURITY_DESCRIPTOR_CONTROL control;
    ACCESS_ALLOWED_ACE *ace;
    HANDLE primary = NULL, token = NULL, restricted = NULL;
    PACL acl;
    BOOL ret, present, defaulted;
    DWORD revision, error, size;
    ACCESS_MASK before, after;
    BYTE sid_buffer[SECURITY_MAX_SID_SIZE];
    PSID world = sid_buffer;
    ULONG index;
    static const struct
    {
        const char *parent;
        BOOL container;
        DWORD count;
        BYTE flags;
    } propagation[] =
    {
        {"O:SYG:SYD:(A;OIIO;0x1;;;WD)", FALSE, 1, INHERITED_ACE},
        {"O:SYG:SYD:(A;OINP;0x1;;;WD)", TRUE, 0, 0},
        {"O:SYG:SYD:(A;CINP;0x1;;;WD)", TRUE, 1, INHERITED_ACE},
        {"O:SYG:SYD:(A;OI;0x1;;;WD)", TRUE, 1, INHERITED_ACE | OBJECT_INHERIT_ACE | INHERIT_ONLY_ACE},
        {"O:SYG:SYD:(A;CI;0x1;;;WD)", FALSE, 0, 0}
    };

    pCreatePrivateObjectSecurityEx = (void *)GetProcAddress(advapi, "CreatePrivateObjectSecurityEx");
    pSetPrivateObjectSecurityEx = (void *)GetProcAddress(advapi, "SetPrivateObjectSecurityEx");
    pConvertToAutoInheritPrivateObjectSecurity = (void *)GetProcAddress(advapi, "ConvertToAutoInheritPrivateObjectSecurity");
    if (!pCreatePrivateObjectSecurityEx || !pSetPrivateObjectSecurityEx ||
        !pConvertToAutoInheritPrivateObjectSecurity)
    {
        win_skip("Private-object inheritance APIs are unavailable\n");
        return;
    }
    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &primary);
    ok(ret, "OpenProcessToken failed: %lu\n", GetLastError());
    if (!ret) return;
    ret = DuplicateToken(primary, SecurityImpersonation, &token);
    ok(ret, "DuplicateToken failed: %lu\n", GetLastError());
    if (!ret) goto done;

    test_private_object_parent_inheritance(token, &mapping, pCreatePrivateObjectSecurityEx);

    parent = private_object_descriptor("O:SYG:SYD:(A;OICI;GR;;;CO)");
    creator = private_object_descriptor("O:WDG:WDD:");
    if (!parent || !creator) goto create_done;
    ret = pCreatePrivateObjectSecurityEx(parent, creator, &result, NULL, FALSE,
                                        avoid | SEF_DACL_AUTO_INHERIT, NULL, &mapping);
    ok(ret, "Leaf inheritance failed: %lu\n", GetLastError());
    if (ret)
    {
        acl = private_object_dacl(result, 1, SE_DACL_AUTO_INHERITED);
        if (acl && acl->AceCount)
        {
            GetAce(acl, 0, (void **)&ace);
            ok(ace->Mask == 1 && ace->Header.AceFlags == INHERITED_ACE,
               "Leaf ACE mask %#lx flags %#x\n", ace->Mask, ace->Header.AceFlags);
            size = sizeof(sid_buffer);
            CreateWellKnownSid(WinWorldSid, NULL, world, &size);
            ok(EqualSid(&ace->SidStart, world), "Creator owner was not substituted\n");
        }
        DestroyPrivateObjectSecurity(&result);
    }
    result = NULL;
    ret = pCreatePrivateObjectSecurityEx(parent, creator, &result, NULL, TRUE,
                                        avoid | SEF_DACL_AUTO_INHERIT, NULL, &mapping);
    ok(ret, "Container inheritance failed: %lu\n", GetLastError());
    if (ret)
    {
        acl = private_object_dacl(result, 2, SE_DACL_AUTO_INHERITED);
        if (acl && acl->AceCount == 2)
        {
            GetAce(acl, 0, (void **)&ace);
            ok(ace->Mask == 1 && !(ace->Header.AceFlags & INHERIT_ONLY_ACE),
               "Missing effective mapped ACE\n");
            GetAce(acl, 1, (void **)&ace);
            ok(ace->Mask == GENERIC_READ && (ace->Header.AceFlags & INHERIT_ONLY_ACE),
               "Missing inheritable generic ACE\n");
        }
        DestroyPrivateObjectSecurity(&result);
    }
    result = NULL;
    ret = pCreatePrivateObjectSecurityEx(NULL, creator, &result, NULL, FALSE,
                                        SEF_AVOID_PRIVILEGE_CHECK | SEF_AVOID_OWNER_RESTRICTION,
                                        NULL, &mapping);
    error = GetLastError();
    ok(!ret && error == ERROR_NO_TOKEN, "Missing owner-validation token: %u, %lu\n", ret, error);
    if (ret) DestroyPrivateObjectSecurity(&result);
    result = NULL;
    ret = pCreatePrivateObjectSecurityEx(NULL, creator, &result, NULL, FALSE,
                                        SEF_AVOID_PRIVILEGE_CHECK | SEF_AVOID_OWNER_RESTRICTION,
                                        primary, &mapping);
    error = GetLastError();
    ok(!ret && error == ERROR_INVALID_OWNER, "Unassignable owner accepted: %u, %lu\n", ret, error);
    if (ret) DestroyPrivateObjectSecurity(&result);
    result = NULL;
    SetSecurityDescriptorControl(creator, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    ret = pCreatePrivateObjectSecurityEx(parent, creator, &result, NULL, FALSE,
                                        avoid | SEF_DACL_AUTO_INHERIT, NULL, &mapping);
    ok(ret, "Protected empty ACL failed: %lu\n", GetLastError());
    if (ret)
    {
        private_object_dacl(result, 0, SE_DACL_PROTECTED);
        DestroyPrivateObjectSecurity(&result);
    }
    result = NULL;
    InitializeSecurityDescriptor(&modification, SECURITY_DESCRIPTOR_REVISION);
    GetSecurityDescriptorOwner(creator, &modification.Owner, &defaulted);
    GetSecurityDescriptorGroup(creator, &modification.Group, &defaulted);
    SetSecurityDescriptorDacl(&modification, TRUE, NULL, FALSE);
    SetSecurityDescriptorControl(&modification, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    ret = pCreatePrivateObjectSecurityEx(parent, &modification, &result, NULL, FALSE,
                                        avoid | SEF_DACL_AUTO_INHERIT, NULL, &mapping);
    ok(ret, "Protected NULL ACL failed: %lu\n", GetLastError());
    if (ret)
    {
        GetSecurityDescriptorDacl(result, &present, &acl, &defaulted);
        ok(present && !acl, "NULL ACL became an empty ACL\n");
        DestroyPrivateObjectSecurity(&result);
    }
    result = NULL;
    ret = pCreatePrivateObjectSecurityEx(NULL, NULL, &result, NULL, FALSE,
                                        SEF_AVOID_OWNER_RESTRICTION, primary, &mapping);
    ok(ret, "Token-default descriptor failed: %lu\n", GetLastError());
    if (ret)
    {
        GetSecurityDescriptorOwner(result, &world, &defaulted);
        ok(world && IsValidSid(world), "Missing token-default owner\n");
        DestroyPrivateObjectSecurity(&result);
    }
create_done:
    if (creator) LocalFree(creator);
    if (parent) LocalFree(parent);

    for (index = 0; index < ARRAY_SIZE(propagation); ++index)
    {
        parent = private_object_descriptor(propagation[index].parent);
        creator = private_object_descriptor("O:WDG:WDD:");
        result = NULL;
        if (parent && creator)
        {
            ret = pCreatePrivateObjectSecurityEx(parent, creator, &result, NULL, propagation[index].container,
                                                avoid | SEF_DACL_AUTO_INHERIT, NULL, &mapping);
            ok(ret, "Propagation case %lu failed: %lu\n", index, GetLastError());
            if (ret)
            {
                acl = private_object_dacl(result, propagation[index].count, SE_DACL_AUTO_INHERITED);
                if (acl && acl->AceCount)
                {
                    GetAce(acl, 0, (void **)&ace);
                    ok(ace->Header.AceFlags == propagation[index].flags,
                       "Propagation case %lu flags %#x, expected %#x\n",
                       index, ace->Header.AceFlags, propagation[index].flags);
                }
                DestroyPrivateObjectSecurity(&result);
            }
        }
        if (creator) LocalFree(creator);
        if (parent) LocalFree(parent);
    }

    parent = private_object_descriptor("O:SYG:SYD:(A;OI;0x1;;;WD)(A;OI;0x2;;;WD)");
    current = private_object_descriptor("O:SYG:SYD:(A;;0x3;;;WD)(A;;0x4;;;WD)");
    if (parent && current)
    {
        before = private_object_access(current, token, &mapping);
        result = NULL;
        ret = pConvertToAutoInheritPrivateObjectSecurity(parent, current, &result, NULL, FALSE, &mapping);
        ok(ret, "Combined-mask conversion failed: %lu\n", GetLastError());
        if (ret)
        {
            after = private_object_access(result, token, &mapping);
            ok(before == after && after == 7, "Conversion changed permissions %#lx -> %#lx\n", before, after);
            acl = private_object_dacl(result, 3, SE_DACL_AUTO_INHERITED);
            GetSecurityDescriptorControl(result, &control, &revision);
            ok(!(control & SE_DACL_PROTECTED), "Equivalent conversion was protected\n");
            if (acl && acl->AceCount == 3)
            {
                for (index = 0; index < 3; ++index)
                {
                    ret = GetAce(acl, index, (void **)&ace);
                    ok(ret, "Converted ACE %lu query failed: %lu\n", index, GetLastError());
                    if (!ret) continue;
                    ok(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE,
                       "Converted ACE %lu type %u\n", index, ace->Header.AceType);
                    ok(ace->Mask == (index ? 1u << (index - 1) : 4) &&
                       ace->Header.AceFlags == (index ? INHERITED_ACE : 0),
                       "Converted ACE %lu mask %#lx flags %#x\n", index, ace->Mask, ace->Header.AceFlags);
                    ok(IsWellKnownSid(&ace->SidStart, WinWorldSid), "Converted ACE %lu SID changed\n", index);
                }
            }
            DestroyPrivateObjectSecurity(&result);
        }
    }
    if (current) LocalFree(current);
    current = private_object_descriptor("O:SYG:SYD:(A;;0x3;;;WD)(D;;0x1;;;WD)");
    if (parent && current)
    {
        before = private_object_access(current, token, &mapping);
        result = NULL;
        ret = pConvertToAutoInheritPrivateObjectSecurity(parent, current, &result, NULL, FALSE, &mapping);
        ok(ret, "Order-preserving conversion failed: %lu\n", GetLastError());
        if (ret)
        {
            after = private_object_access(result, token, &mapping);
            ok(before == after && after == 3, "Protected fallback changed permissions %#lx -> %#lx\n", before, after);
            private_object_dacl(result, 2, SE_DACL_AUTO_INHERITED | SE_DACL_PROTECTED);
            DestroyPrivateObjectSecurity(&result);
        }
    }
    if (current) LocalFree(current);
    if (parent) LocalFree(parent);

    creator = private_object_descriptor("O:WDG:WDD:(A;;0x2;;;WD)");
    parent = private_object_descriptor("O:WDG:WDD:(A;OI;0x1;;;WD)");
    result = NULL;
    if (!creator || !parent) goto set_done;
    ret = pCreatePrivateObjectSecurityEx(parent, creator, &result, NULL, FALSE,
                                        avoid, NULL, &mapping);
    ok(ret, "Explicit ACL without auto-inheritance failed: %lu\n", GetLastError());
    if (ret)
    {
        after = private_object_access(result, token, &mapping);
        ok(after == 2, "Unexpected implicit merge: %#lx\n", after);
        DestroyPrivateObjectSecurity(&result);
    }
    result = NULL;
    ret = pCreatePrivateObjectSecurityEx(NULL, creator, &result, NULL, FALSE,
                                        avoid | SEF_DACL_AUTO_INHERIT | SEF_DEFAULT_DESCRIPTOR_FOR_OBJECT,
                                        NULL, &mapping);
    ok(ret, "Default creator ACL failed: %lu\n", GetLastError());
    if (ret)
    {
        after = private_object_access(result, token, &mapping);
        ok(after == 2, "Default creator ACL was lost without a parent: %#lx\n", after);
        DestroyPrivateObjectSecurity(&result);
    }
    result = NULL;
    InitializeSecurityDescriptor(&modification, SECURITY_DESCRIPTOR_REVISION);
    GetSecurityDescriptorOwner(creator, &modification.Owner, &defaulted);
    GetSecurityDescriptorGroup(creator, &modification.Group, &defaulted);
    GetSecurityDescriptorDacl(creator, &present, &acl, &defaulted);
    SetSecurityDescriptorDacl(&modification, present, acl, TRUE);
    ret = pCreatePrivateObjectSecurityEx(parent, &modification, &result, NULL, FALSE,
                                        avoid | SEF_DACL_AUTO_INHERIT, NULL, &mapping);
    ok(ret, "Defaulted creator ACL inheritance failed: %lu\n", GetLastError());
    if (ret)
    {
        after = private_object_access(result, token, &mapping);
        ok(after == 1, "Defaulted creator overrode inherited permissions: %#lx\n", after);
        private_object_dacl(result, 1, SE_DACL_AUTO_INHERITED);
        DestroyPrivateObjectSecurity(&result);
    }
    result = NULL;
    ret = pCreatePrivateObjectSecurityEx(parent, creator, &result, NULL, FALSE,
                                        avoid | SEF_DACL_AUTO_INHERIT, NULL, &mapping);
    ok(ret, "Setter fixture creation failed: %lu\n", GetLastError());
    if (!ret) goto set_done;
    current = private_object_descriptor("O:WDG:WDD:(A;;0x4;;;WD)(A;ID;0x2;;;WD)");
    if (!current) goto set_done;
    ret = pSetPrivateObjectSecurityEx(DACL_SECURITY_INFORMATION, current, &result,
                                     avoid | SEF_DACL_AUTO_INHERIT, &mapping, NULL);
    ok(ret, "Private DACL update failed: %lu\n", GetLastError());
    if (ret)
    {
        after = private_object_access(result, token, &mapping);
        ok(after == 5, "Inherited permissions were replaced by supplied inherited ACE: %#lx\n", after);
        private_object_dacl(result, 2, SE_DACL_AUTO_INHERITED);
    }
    SetSecurityDescriptorControl(current, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    ret = pSetPrivateObjectSecurityEx(DACL_SECURITY_INFORMATION, current, &result,
                                     avoid | SEF_DACL_AUTO_INHERIT, &mapping, NULL);
    ok(ret, "Private protected DACL update failed: %lu\n", GetLastError());
    if (ret)
    {
        after = private_object_access(result, token, &mapping);
        ok(after == 6, "Protected DACL did not replace old inheritance: %#lx\n", after);
        acl = private_object_dacl(result, 2, SE_DACL_PROTECTED);
        if (acl)
            for (index = 0; index < acl->AceCount; ++index)
            {
                GetAce(acl, index, (void **)&ace);
                ok(!(ace->Header.AceFlags & INHERITED_ACE), "Protected ACE remained inherited\n");
            }
    }
    LocalFree(current);
    current = private_object_descriptor("O:WDG:WDD:(A;ID;0x1;;;WD)(A;;0x4;;;WD)");
    if (current)
    {
        ret = pSetPrivateObjectSecurityEx(DACL_SECURITY_INFORMATION, current, &result,
                                         avoid | SEF_DACL_AUTO_INHERIT, &mapping, NULL);
        ok(ret, "Unprotecting private DACL failed: %lu\n", GetLastError());
        if (ret)
        {
            after = private_object_access(result, token, &mapping);
            ok(after == 5, "Unprotecting lost supplied ACL: %#lx\n", after);
            acl = private_object_dacl(result, 2, SE_DACL_AUTO_INHERITED);
            GetSecurityDescriptorControl(result, &control, &revision);
            ok(!(control & SE_DACL_PROTECTED), "Descriptor remained protected\n");
            if (acl && acl->AceCount)
            {
                GetAce(acl, 0, (void **)&ace);
                ok(ace->Header.AceFlags & INHERITED_ACE, "Supplied inherited marking was lost\n");
            }
        }
        LocalFree(current);
    }
    saved = result;
    InitializeSecurityDescriptor(&modification, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorOwner(&modification, NULL, FALSE);
    ret = pSetPrivateObjectSecurityEx(OWNER_SECURITY_INFORMATION, &modification, &result,
                                     avoid, &mapping, NULL);
    error = GetLastError();
    ok(!ret && error == ERROR_INVALID_OWNER, "Invalid owner accepted: %u, %lu\n", ret, error);
    ok(result == saved, "Failed update replaced the descriptor\n");
set_done:
    if (result) DestroyPrivateObjectSecurity(&result);
    if (creator) LocalFree(creator);
    if (parent) LocalFree(parent);


    {
        static const GUID type = {0x372ccb92, 0x5f48, 0x42fb, {0x95, 0x8d, 0x64, 0x2b, 0x7b, 0x9e, 0x12, 0x34}};
        static const BYTE payload[] = {0x31, 0x45, 0x9c, 0x72, 0x18, 0x26, 0x5e, 0xa4};
        DWORD ace_buffer[32], acl_buffer[40];
        ACCESS_ALLOWED_CALLBACK_OBJECT_ACE *object_ace = (void *)ace_buffer;
        ACCESS_ALLOWED_CALLBACK_OBJECT_ACE *inherited_ace;
        SECURITY_DESCRIPTOR object_parent;
        GUID selected_type = type;
        DWORD sid_length, ace_length;
        PACL object_acl = (void *)acl_buffer;

        world = sid_buffer;
        size = sizeof(sid_buffer);
        ret = CreateWellKnownSid(WinWorldSid, NULL, world, &size);
        ok(ret, "World SID creation failed: %lu\n", GetLastError());
        sid_length = GetLengthSid(world);
        ace_length = FIELD_OFFSET(ACCESS_ALLOWED_CALLBACK_OBJECT_ACE, SidStart) + sid_length + sizeof(payload);
        memset(ace_buffer, 0, sizeof(ace_buffer));
        object_ace->Header.AceType = ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE;
        object_ace->Header.AceFlags = OBJECT_INHERIT_ACE;
        object_ace->Header.AceSize = ace_length;
        object_ace->Mask = GENERIC_READ;
        object_ace->Flags = ACE_OBJECT_TYPE_PRESENT | ACE_INHERITED_OBJECT_TYPE_PRESENT;
        object_ace->ObjectType = type;
        object_ace->InheritedObjectType = type;
        CopySid(sid_length, &object_ace->SidStart, world);
        memcpy((BYTE *)&object_ace->SidStart + sid_length, payload, sizeof(payload));
        InitializeAcl(object_acl, sizeof(acl_buffer), ACL_REVISION_DS);
        ret = AddAce(object_acl, ACL_REVISION_DS, MAXDWORD, object_ace, ace_length);
        ok(ret, "Callback object ACE fixture failed: %lu\n", GetLastError());
        InitializeSecurityDescriptor(&object_parent, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorOwner(&object_parent, world, FALSE);
        SetSecurityDescriptorGroup(&object_parent, world, FALSE);
        SetSecurityDescriptorDacl(&object_parent, TRUE, object_acl, FALSE);
        creator = private_object_descriptor("O:WDG:WDD:");
        result = NULL;
        if (creator && ret)
        {
            ret = pCreatePrivateObjectSecurityEx(&object_parent, creator, &result, &selected_type, FALSE,
                                                avoid | SEF_DACL_AUTO_INHERIT, NULL, &mapping);
            ok(ret, "Callback object inheritance failed: %lu\n", GetLastError());
            if (ret)
            {
                acl = private_object_dacl(result, 1, SE_DACL_AUTO_INHERITED);
                if (acl && acl->AceCount)
                {
                    ret = GetAce(acl, 0, (void **)&inherited_ace);
                    ok(ret, "Callback GetAce failed: %lu\n", GetLastError());
                    if (ret)
                    {
                        ok(inherited_ace->Header.AceSize == ace_length, "Callback object size changed\n");
                        if (inherited_ace->Header.AceSize == ace_length)
                        {
                            ok(inherited_ace->Mask == GENERIC_READ, "Callback mask %#lx\n", inherited_ace->Mask);
                            ok(inherited_ace->Flags == object_ace->Flags, "Callback object flags changed\n");
                            ok(!memcmp((BYTE *)inherited_ace + FIELD_OFFSET(ACCESS_ALLOWED_CALLBACK_OBJECT_ACE, ObjectType),
                                       (BYTE *)object_ace + FIELD_OFFSET(ACCESS_ALLOWED_CALLBACK_OBJECT_ACE, ObjectType),
                                       ace_length - FIELD_OFFSET(ACCESS_ALLOWED_CALLBACK_OBJECT_ACE, ObjectType)),
                               "Callback GUIDs, SID, or opaque payload changed\n");
                        }
                    }
                }
                DestroyPrivateObjectSecurity(&result);
            }
            result = NULL;
            selected_type.Data1 ^= 1;
            ret = pCreatePrivateObjectSecurityEx(&object_parent, creator, &result, &selected_type, FALSE,
                                                avoid | SEF_DACL_AUTO_INHERIT, NULL, &mapping);
            ok(ret, "Nonmatching object inheritance failed: %lu\n", GetLastError());
            if (ret)
            {
                acl = private_object_dacl(result, 1, SE_DACL_AUTO_INHERITED);
                if (acl && acl->AceCount == 1)
                {
                    ret = GetAce(acl, 0, (void **)&inherited_ace);
                    ok(ret, "Nonmatching callback GetAce failed: %lu\n", GetLastError());
                    if (ret)
                    {
                        ok(inherited_ace->Header.AceType == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE &&
                           inherited_ace->Header.AceFlags == INHERITED_ACE &&
                           inherited_ace->Header.AceSize == ace_length,
                           "Nonmatching callback header type %u flags %#x size %u\n",
                           inherited_ace->Header.AceType, inherited_ace->Header.AceFlags,
                           inherited_ace->Header.AceSize);
                        if (inherited_ace->Header.AceSize == ace_length)
                            ok(!memcmp((BYTE *)inherited_ace + sizeof(ACE_HEADER),
                                       (BYTE *)object_ace + sizeof(ACE_HEADER), ace_length - sizeof(ACE_HEADER)),
                               "Nonmatching callback mask, GUIDs, SID, or payload changed\n");
                    }
                }
                DestroyPrivateObjectSecurity(&result);
            }
        }
        if (creator) LocalFree(creator);
    }

    {
        SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
        DWORD domain_buffer[8], acl_size, sd_size;
        PSID domain = domain_buffer;
        SECURITY_DESCRIPTOR large_parent, large_modification;
        PACL large_acl;
        BYTE *snapshot = NULL;

        InitializeSid(domain, &authority, 5);
        *GetSidSubAuthority(domain, 0) = 21;
        *GetSidSubAuthority(domain, 1) = 1;
        *GetSidSubAuthority(domain, 2) = 2;
        *GetSidSubAuthority(domain, 3) = 3;
        acl_size = sizeof(ACL) + 1024 * (FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + GetLengthSid(domain));
        large_acl = HeapAlloc(GetProcessHeap(), 0, acl_size);
        ok(large_acl != NULL, "Large ACL allocation failed\n");
        creator = private_object_descriptor("O:WDG:WDD:");
        result = NULL;
        if (large_acl && creator)
        {
            InitializeAcl(large_acl, acl_size, ACL_REVISION);
            for (index = 0; index < 1024; ++index)
            {
                *GetSidSubAuthority(domain, 4) = 1000 + index;
                if (!AddAccessAllowedAceEx(large_acl, ACL_REVISION, OBJECT_INHERIT_ACE, 1, domain)) break;
            }
            ok(index == 1024, "Large ACL fixture stopped at %lu: %lu\n", index, GetLastError());
            InitializeSecurityDescriptor(&large_parent, SECURITY_DESCRIPTOR_REVISION);
            SetSecurityDescriptorDacl(&large_parent, TRUE, large_acl, FALSE);
            if (index == 1024)
            {
                ret = pCreatePrivateObjectSecurityEx(&large_parent, creator, &result, NULL, FALSE,
                                                    avoid | SEF_DACL_AUTO_INHERIT, NULL, &mapping);
                ok(ret, "Large inherited ACL creation failed: %lu\n", GetLastError());
                if (ret)
                {
                    for (index = 0; index < large_acl->AceCount; ++index)
                    {
                        GetAce(large_acl, index, (void **)&ace);
                        ace->Header.AceFlags = 0;
                    }
                    sd_size = GetSecurityDescriptorLength(result);
                    snapshot = HeapAlloc(GetProcessHeap(), 0, sd_size);
                    ok(snapshot != NULL, "Descriptor snapshot allocation failed\n");
                    if (snapshot)
                    {
                        memcpy(snapshot, result, sd_size);
                        saved = result;
                        InitializeSecurityDescriptor(&large_modification, SECURITY_DESCRIPTOR_REVISION);
                        SetSecurityDescriptorDacl(&large_modification, TRUE, large_acl, FALSE);
                        ret = pSetPrivateObjectSecurityEx(DACL_SECURITY_INFORMATION, &large_modification,
                                                         &result, avoid | SEF_DACL_AUTO_INHERIT, &mapping, NULL);
                        ok(!ret, "Oversized combined ACL was accepted\n");
                        ok(result == saved && !memcmp(result, snapshot, sd_size),
                           "Failed ACL-size allocation changed the original descriptor\n");
                    }
                    DestroyPrivateObjectSecurity(&result);
                }
            }
        }
        if (snapshot) HeapFree(GetProcessHeap(), 0, snapshot);
        if (large_acl) HeapFree(GetProcessHeap(), 0, large_acl);
        if (creator) LocalFree(creator);
    }


    {
        DWORD acl_storage[32];
        BYTE owner_rights_buffer[SECURITY_MAX_SID_SIZE];
        SECURITY_DESCRIPTOR token_descriptor, object_descriptor;
        TOKEN_STATISTICS statistics;
        HANDLE managed_token = NULL, query_token = NULL, read_token = NULL;
        PACL token_acl = (void *)acl_storage;
        PSID owner_rights = owner_rights_buffer;
        DWORD needed;
#else
        ctrl = ((1 << bit) & immutable) ? test : ref | mutable;
        setOrClear ^= bitsOfInterest;
        SetLastError (0xbebecaca);
        SetSecurityDescriptorControl (sec, bitsOfInterest, setOrClear | (1 << bit));
        ok (GetLastError () == dwExpect, fmt, strExpect, GetLastError ());
        GetSecurityDescriptorControl(sec, &test, &dwRevision);
        expect_eq(test, ctrl, int, "%x");
#endif

#ifdef __REACTOS__
        world = sid_buffer;
        size = sizeof(sid_buffer);
        ret = CreateWellKnownSid(WinWorldSid, NULL, world, &size);
        ok(ret, "Query-token world SID creation failed: %lu\n", GetLastError());
        ret = DuplicateTokenEx(primary, TOKEN_QUERY | READ_CONTROL | WRITE_DAC, NULL,
                               SecurityImpersonation, TokenPrimary, &managed_token);
        ok(ret, "Query-token fixture duplication failed: %lu\n", GetLastError());
        if (ret)
        {
            InitializeAcl(token_acl, sizeof(acl_storage), ACL_REVISION);
            AddAccessAllowedAce(token_acl, ACL_REVISION, TOKEN_QUERY | READ_CONTROL, world);
            InitializeSecurityDescriptor(&token_descriptor, SECURITY_DESCRIPTOR_REVISION);
            SetSecurityDescriptorDacl(&token_descriptor, TRUE, token_acl, FALSE);
            ret = SetKernelObjectSecurity(managed_token, DACL_SECURITY_INFORMATION, &token_descriptor);
            ok(ret, "Readable token DACL setup failed: %lu\n", GetLastError());
            if (ret)
            {
                ret = DuplicateHandle(GetCurrentProcess(), managed_token, GetCurrentProcess(),
                                      &query_token, TOKEN_QUERY, FALSE, 0);
                ok(ret, "Query-only handle creation failed: %lu\n", GetLastError());
            }
            if (ret)
            {
                ret = GetKernelObjectSecurity(query_token, DACL_SECURITY_INFORMATION, NULL, 0, &needed);
                error = GetLastError();
                ok(!ret && error == ERROR_ACCESS_DENIED,
                   "Query-only source has READ_CONTROL: %u, %lu\n", ret, error);
                InitializeSecurityDescriptor(&object_descriptor, SECURITY_DESCRIPTOR_REVISION);
                for (index = 0; index < 2; ++index)
                {
                    result = NULL;
                    ret = pCreatePrivateObjectSecurityEx(NULL, &object_descriptor, &result, NULL,
                                                        FALSE, 0, query_token, &mapping);
                    ok(ret, "Creation with TOKEN_QUERY only failed: %lu\n", GetLastError());
                    if (ret)
                    {
                        ok(IsValidSecurityDescriptor(result), "Invalid query-token result\n");
                        DestroyPrivateObjectSecurity(&result);
                    }
                    ret = GetTokenInformation(query_token, TokenStatistics, &statistics,
                                              sizeof(statistics), &needed);
                    ok(ret, "Source token was closed: %lu\n", GetLastError());
                    ret = GetKernelObjectSecurity(query_token, DACL_SECURITY_INFORMATION, NULL, 0, &needed);
                    error = GetLastError();
                    ok(!ret && error == ERROR_ACCESS_DENIED,
                       "Source handle access changed: %u, %lu\n", ret, error);
                }

                size = sizeof(owner_rights_buffer);
                ret = CreateWellKnownSid(WinCreatorOwnerRightsSid, NULL, owner_rights, &size);
                ok(ret, "Owner Rights SID creation failed: %lu\n", GetLastError());
                if (ret)
                {
                    InitializeAcl(token_acl, sizeof(acl_storage), ACL_REVISION);
                    AddAccessDeniedAce(token_acl, ACL_REVISION, READ_CONTROL, world);
                    AddAccessAllowedAce(token_acl, ACL_REVISION, TOKEN_QUERY, world);
                    AddAccessAllowedAce(token_acl, ACL_REVISION, 0, owner_rights);
                    ret = SetKernelObjectSecurity(managed_token, DACL_SECURITY_INFORMATION, &token_descriptor);
                    ok(ret, "Unreadable token DACL setup failed: %lu\n", GetLastError());
                    if (ret)
                    {
                        ret = DuplicateHandle(GetCurrentProcess(), query_token, GetCurrentProcess(),
                                              &read_token, READ_CONTROL, FALSE, 0);
                        error = GetLastError();
                        ok(!ret && error == ERROR_ACCESS_DENIED,
                           "DuplicateHandle bypassed token DACL: %u, %lu\n", ret, error);
                        if (ret) CloseHandle(read_token);
                        result = (PSECURITY_DESCRIPTOR)(ULONG_PTR)0xdeadbeef;
                        saved = result;
                        ret = pCreatePrivateObjectSecurityEx(NULL, &object_descriptor, &result, NULL,
                                                            FALSE, 0, query_token, &mapping);
                        if (ret) DestroyPrivateObjectSecurity(&result);
                        else ok(result == saved, "Failed restricted creation changed output\n");
                        ret = GetTokenInformation(query_token, TokenStatistics, &statistics,
                                                  sizeof(statistics), &needed);
                        ok(ret, "Restricted source token was closed: %lu\n", GetLastError());
                    }
                }
            }
        }
        if (query_token) CloseHandle(query_token);
        if (managed_token) CloseHandle(managed_token);
        result = NULL;
    }

    ret = CreateRestrictedToken(primary, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, 0, NULL, &restricted);
    ok(ret, "Privilege-test token failed: %lu\n", GetLastError());
    if (ret)
    {
        creator = private_object_descriptor("O:WDG:WDS:(AU;SA;0x1;;;WD)");
        result = NULL;
        if (creator)
        {
            ret = pCreatePrivateObjectSecurityEx(NULL, creator, &result, NULL, FALSE,
                                                SEF_AVOID_OWNER_CHECK | SEF_AVOID_OWNER_RESTRICTION,
                                                restricted, &mapping);
            error = GetLastError();
            ok(!ret && error == ERROR_PRIVILEGE_NOT_HELD, "SACL privilege check: %u, %lu\n", ret, error);
            if (ret) DestroyPrivateObjectSecurity(&result);
            LocalFree(creator);
        }
#else
        ctrl = ((1 << bit) & immutable) ? test : ref | (1 << bit);
        setOrClear ^= bitsOfInterest;
        SetLastError (0xbebecaca);
        SetSecurityDescriptorControl (sec, bitsOfInterest, setOrClear | (1 << bit));
        ok (GetLastError () == dwExpect, fmt, strExpect, GetLastError ());
        GetSecurityDescriptorControl(sec, &test, &dwRevision);
        expect_eq(test, ctrl, int, "%x");
#endif
    }
#ifdef __REACTOS__
done:
    if (restricted) CloseHandle(restricted);
    if (token) CloseHandle(token);
    if (primary) CloseHandle(primary);
#endif
}

static void test_PrivateObjectSecurity(void)
{
    SECURITY_INFORMATION sec_info = OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|SACL_SECURITY_INFORMATION;
    SECURITY_DESCRIPTOR_CONTROL ctrl;
    PSECURITY_DESCRIPTOR sec;
    DWORD dwDescSize;
    DWORD dwRevision;
    DWORD retSize;
    LPSTR string;
    ULONG len;
    PSECURITY_DESCRIPTOR buf;
    BOOL ret;

#ifdef __REACTOS__
    test_private_object_inheritance();

#endif
    ok(ConvertStringSecurityDescriptorToSecurityDescriptorA(
        "O:SY"
        "G:S-1-5-21-93476-23408-4576"
        "D:(A;NP;GAGXGWGR;;;SU)(A;IOID;CCDC;;;SU)"
          "(D;OICI;0xffffffff;;;S-1-5-21-93476-23408-4576)"
        "S:(AU;OICINPIOIDSAFA;CCDCLCSWRPRC;;;SU)(AU;NPSA;0x12019f;;;SU)",
        SDDL_REVISION_1, &sec, &dwDescSize), "Creating descriptor failed\n");

    test_SetSecurityDescriptorControl(sec);

    LocalFree(sec);

    ok(ConvertStringSecurityDescriptorToSecurityDescriptorA(
        "O:SY"
        "G:S-1-5-21-93476-23408-4576",
        SDDL_REVISION_1, &sec, &dwDescSize), "Creating descriptor failed\n");

    test_SetSecurityDescriptorControl(sec);

    LocalFree(sec);

    ok(ConvertStringSecurityDescriptorToSecurityDescriptorA(
        "O:SY"
        "G:S-1-5-21-93476-23408-4576"
        "D:(A;NP;GAGXGWGR;;;SU)(A;IOID;CCDC;;;SU)(D;OICI;0xffffffff;;;S-1-5-21-93476-23408-4576)"
        "S:(AU;OICINPIOIDSAFA;CCDCLCSWRPRC;;;SU)(AU;NPSA;0x12019f;;;SU)", SDDL_REVISION_1, &sec, &dwDescSize), "Creating descriptor failed\n");
    buf = malloc(dwDescSize);
    SetSecurityDescriptorControl(sec, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    GetSecurityDescriptorControl(sec, &ctrl, &dwRevision);
    expect_eq(ctrl, 0x9014, int, "%x");

    ret = GetPrivateObjectSecurity(sec, GROUP_SECURITY_INFORMATION, buf, dwDescSize, &retSize);
    ok(ret, "GetPrivateObjectSecurity failed (err=%lu)\n", GetLastError());
    ok(retSize <= dwDescSize, "Buffer too small (%ld vs %ld)\n", retSize, dwDescSize);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(buf, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_RESULT_AND_FREE("G:S-1-5-21-93476-23408-4576");
    GetSecurityDescriptorControl(buf, &ctrl, &dwRevision);
    expect_eq(ctrl, 0x8000, int, "%x");

    ret = GetPrivateObjectSecurity(sec, GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION, buf, dwDescSize, &retSize);
    ok(ret, "GetPrivateObjectSecurity failed (err=%lu)\n", GetLastError());
    ok(retSize <= dwDescSize, "Buffer too small (%ld vs %ld)\n", retSize, dwDescSize);
    ret = ConvertSecurityDescriptorToStringSecurityDescriptorA(buf, SDDL_REVISION_1, sec_info, &string, &len);
    ok(ret, "Conversion failed err=%lu\n", GetLastError());
    CHECK_ONE_OF_AND_FREE("G:S-1-5-21-93476-23408-4576D:(A;NP;GAGXGWGR;;;SU)(A;IOID;CCDC;;;SU)(D;OICI;0xffffffff;;;S-1-5-21-93476-23408-4576)",
        "G:S-1-5-21-93476-23408-4576D:P(A;NP;GAGXGWGR;;;SU)(A;IOID;CCDC;;;SU)(D;OICI;0xffffffff;;;S-1-5-21-93476-23408-4576)"); /* Win7 */
    GetSecurityDescriptorControl(buf, &ctrl, &dwRevision);
    expect_eq(ctrl & (~ SE_DACL_PROTECTED), 0x8004, int, "%x");

    ret = GetPrivateObjectSecurity(sec, sec_info, buf, dwDescSize, &retSize);
    ok(ret, "GetPrivateObjectSecurity failed (err=%lu)\n", GetLastError());
    ok(retSize == dwDescSize, "Buffer too small (%ld vs %ld)\n", retSize, dwDescSize);
    ok(ConvertSecurityDescriptorToStringSecurityDescriptorA(buf, SDDL_REVISION_1, sec_info, &string, &len), "Conversion failed\n");
    CHECK_ONE_OF_AND_FREE("O:SY"
        "G:S-1-5-21-93476-23408-4576"
        "D:(A;NP;GAGXGWGR;;;SU)(A;IOID;CCDC;;;SU)(D;OICI;0xffffffff;;;S-1-5-21-93476-23408-4576)"
        "S:(AU;OICINPIOIDSAFA;CCDCLCSWRPRC;;;SU)(AU;NPSA;0x12019f;;;SU)",
      "O:SY"
        "G:S-1-5-21-93476-23408-4576"
        "D:P(A;NP;GAGXGWGR;;;SU)(A;IOID;CCDC;;;SU)(D;OICI;0xffffffff;;;S-1-5-21-93476-23408-4576)"
        "S:(AU;OICINPIOIDSAFA;CCDCLCSWRPRC;;;SU)(AU;NPSA;0x12019f;;;SU)"); /* Win7 */
    GetSecurityDescriptorControl(buf, &ctrl, &dwRevision);
    expect_eq(ctrl & (~ SE_DACL_PROTECTED), 0x8014, int, "%x");

    SetLastError(0xdeadbeef);
    ok(GetPrivateObjectSecurity(sec, sec_info, buf, 5, &retSize) == FALSE, "GetPrivateObjectSecurity should have failed\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Expected error ERROR_INSUFFICIENT_BUFFER, got %lu\n", GetLastError());

    LocalFree(sec);
    free(buf);
}
#undef CHECK_RESULT_AND_FREE
#undef CHECK_ONE_OF_AND_FREE

static void test_InitializeAcl(void)
{
    char buffer[256];
    PACL pAcl = (PACL)buffer;
    BOOL ret;

    SetLastError(0xdeadbeef);
    ret = InitializeAcl(pAcl, sizeof(ACL) - 1, ACL_REVISION);
    if (!ret && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        win_skip("InitializeAcl is not implemented\n");
        return;
    }

    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "InitializeAcl with too small a buffer should have failed with ERROR_INSUFFICIENT_BUFFER instead of %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = InitializeAcl(pAcl, 0xffffffff, ACL_REVISION);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER, "InitializeAcl with too large a buffer should have failed with ERROR_INVALID_PARAMETER instead of %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = InitializeAcl(pAcl, sizeof(buffer), ACL_REVISION1);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER, "InitializeAcl(ACL_REVISION1) should have failed with ERROR_INVALID_PARAMETER instead of %ld\n", GetLastError());

    ret = InitializeAcl(pAcl, sizeof(buffer), ACL_REVISION2);
    ok(ret, "InitializeAcl(ACL_REVISION2) failed with error %ld\n", GetLastError());

    ret = IsValidAcl(pAcl);
    ok(ret, "IsValidAcl failed with error %ld\n", GetLastError());

    ret = InitializeAcl(pAcl, sizeof(buffer), ACL_REVISION3);
    ok(ret, "InitializeAcl(ACL_REVISION3) failed with error %ld\n", GetLastError());

    ret = IsValidAcl(pAcl);
    ok(ret, "IsValidAcl failed with error %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = InitializeAcl(pAcl, sizeof(buffer), ACL_REVISION4);
    ok(ret, "InitializeAcl(ACL_REVISION4) failed with error %ld\n", GetLastError());

    ret = IsValidAcl(pAcl);
    ok(ret, "IsValidAcl failed with error %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = InitializeAcl(pAcl, sizeof(buffer), -1);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER, "InitializeAcl(-1) failed with error %ld\n", GetLastError());
}

static void test_GetSecurityInfo(void)
{
    char domain_users_ptr[sizeof(TOKEN_USER) + sizeof(SID) + sizeof(DWORD)*SID_MAX_SUB_AUTHORITIES];
#ifdef __REACTOS__
    char domain_sid_buffer[SECURITY_MAX_SID_SIZE];
    struct
    {
        TOKEN_PRIMARY_GROUP group;
        BYTE sid[SECURITY_MAX_SID_SIZE];
    } primary_group;
#endif
    char b[sizeof(TOKEN_USER) + sizeof(SID) + sizeof(DWORD)*SID_MAX_SUB_AUTHORITIES];
    char admin_ptr[sizeof(SID)+sizeof(ULONG)*SID_MAX_SUB_AUTHORITIES], dacl[100];
#ifdef __REACTOS__
    PSID domain_users_sid = (PSID) domain_users_ptr;
    int domain_users_ace_id = -1, admins_ace_id = -1, user_ace_id = -1, i;
#else
    PSID domain_users_sid = (PSID) domain_users_ptr, domain_sid;
    SID_IDENTIFIER_AUTHORITY sia = { SECURITY_NT_AUTHORITY };
    int domain_users_ace_id = -1, admins_ace_id = -1, i;
#endif
    DWORD sid_size = sizeof(admin_ptr), l = sizeof(b);
    SECURITY_ATTRIBUTES sa = {.nLength = sizeof(sa)};
    PSID admin_sid = (PSID) admin_ptr, user_sid;
    char sd[SECURITY_DESCRIPTOR_MIN_LENGTH];
#ifdef __REACTOS__
    char process_dacl[200], process_cmdline[2 * MAX_PATH];
    SECURITY_DESCRIPTOR process_sd;
    SECURITY_ATTRIBUTES process_sa = {.nLength = sizeof(process_sa)};
    STARTUPINFOA startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process_info = {0};
#endif
    BOOL owner_defaulted, group_defaulted;
    BOOL dacl_defaulted, dacl_present;
    ACL_SIZE_INFORMATION acl_size;
    PSECURITY_DESCRIPTOR pSD;
    ACCESS_ALLOWED_ACE *ace;
#ifdef __REACTOS__
    HANDLE token, obj, closed_obj;
#else
    HANDLE token, obj;
#endif
    PSID owner, group;
    BOOL bret = TRUE;
    PACL pDacl;
    BYTE flags;
    DWORD ret;

    static const SE_OBJECT_TYPE kernel_types[] =
    {
        SE_FILE_OBJECT,
        SE_KERNEL_OBJECT,
        SE_WMIGUID_OBJECT,
    };

    static const SE_OBJECT_TYPE invalid_types[] =
    {
        SE_UNKNOWN_OBJECT_TYPE,
        SE_DS_OBJECT,
        SE_DS_OBJECT_ALL,
        SE_PROVIDER_DEFINED_OBJECT,
        SE_REGISTRY_WOW64_32KEY,
        SE_REGISTRY_WOW64_64KEY,
        0xdeadbeef,
    };

    if (!OpenThreadToken(GetCurrentThread(), TOKEN_READ, TRUE, &token))
    {
        if (GetLastError() != ERROR_NO_TOKEN) bret = FALSE;
        else if (!OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &token)) bret = FALSE;
    }
    if (!bret)
    {
        win_skip("Failed to get current user token\n");
        return;
    }
    bret = GetTokenInformation(token, TokenUser, b, l, &l);
    ok(bret, "GetTokenInformation(TokenUser) failed with error %ld\n", GetLastError());
#ifdef __REACTOS__
    bret = GetTokenInformation(token, TokenPrimaryGroup, &primary_group, sizeof(primary_group), &l);
    ok(bret, "GetTokenInformation(TokenPrimaryGroup) failed with error %ld\n", GetLastError());
#endif
    CloseHandle( token );
#ifdef __REACTOS__
    if (!bret) return;
#endif
    user_sid = ((TOKEN_USER *)b)->User.Sid;

    /* Create something.  Files have lots of associated security info.  */
    obj = CreateFileA(myARGV[0], GENERIC_READ|WRITE_DAC, FILE_SHARE_READ, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (obj == INVALID_HANDLE_VALUE)
    {
        skip("Couldn't create an object for GetSecurityInfo test\n");
        return;
    }

    ret = GetSecurityInfo(obj, SE_FILE_OBJECT,
                          OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                          &owner, &group, &pDacl, NULL, &pSD);
    if (ret == ERROR_CALL_NOT_IMPLEMENTED)
    {
        win_skip("GetSecurityInfo is not implemented\n");
        CloseHandle(obj);
        return;
    }
    ok(ret == ERROR_SUCCESS, "GetSecurityInfo returned %ld\n", ret);
    ok(pSD != NULL, "GetSecurityInfo\n");
    ok(owner != NULL, "GetSecurityInfo\n");
    ok(group != NULL, "GetSecurityInfo\n");
    if (pDacl != NULL)
        ok(IsValidAcl(pDacl), "GetSecurityInfo\n");
    else
        win_skip("No ACL information returned\n");

    LocalFree(pSD);

    /* If we don't ask for the security descriptor, Windows will still give us
       the other stuff, leaving us no way to free it.  */
    ret = GetSecurityInfo(obj, SE_FILE_OBJECT,
                          OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                          &owner, &group, &pDacl, NULL, NULL);
    ok(ret == ERROR_SUCCESS, "GetSecurityInfo returned %ld\n", ret);
    ok(owner != NULL, "GetSecurityInfo\n");
    ok(group != NULL, "GetSecurityInfo\n");
    if (pDacl != NULL)
        ok(IsValidAcl(pDacl), "GetSecurityInfo\n");
    else
        win_skip("No ACL information returned\n");

    /* Create security descriptor information and test that it comes back the same */
    pSD = &sd;
    pDacl = (PACL)&dacl;
    InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION);
    CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, admin_sid, &sid_size);
    bret = InitializeAcl(pDacl, sizeof(dacl), ACL_REVISION);
    ok(bret, "Failed to initialize ACL.\n");
    bret = AddAccessAllowedAceEx(pDacl, ACL_REVISION, 0, GENERIC_ALL, user_sid);
    ok(bret, "Failed to add Current User to ACL.\n");
    bret = AddAccessAllowedAceEx(pDacl, ACL_REVISION, 0, GENERIC_ALL, admin_sid);
    ok(bret, "Failed to add Administrator Group to ACL.\n");
    bret = SetSecurityDescriptorDacl(pSD, TRUE, pDacl, FALSE);
    ok(bret, "Failed to add ACL to security descriptor.\n");
    ret = SetSecurityInfo(obj, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                         NULL, NULL, pDacl, NULL);
    ok(ret == ERROR_SUCCESS, "SetSecurityInfo returned %ld\n", ret);
    ret = GetSecurityInfo(obj, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                          NULL, NULL, &pDacl, NULL, &pSD);
    ok(ret == ERROR_SUCCESS, "GetSecurityInfo returned %ld\n", ret);
    ok(pDacl && IsValidAcl(pDacl), "GetSecurityInfo returned invalid DACL.\n");
    bret = GetAclInformation(pDacl, &acl_size, sizeof(acl_size), AclSizeInformation);
    ok(bret, "GetAclInformation failed\n");
    if (acl_size.AceCount > 0)
    {
        bret = GetAce(pDacl, 0, (VOID **)&ace);
        ok(bret, "Failed to get Current User ACE.\n");
        bret = EqualSid(&ace->SidStart, user_sid);
        todo_wine ok(bret, "Current User ACE (%s) != Current User SID (%s).\n",
                     debugstr_sid(&ace->SidStart), debugstr_sid(user_sid));
        ok(((ACE_HEADER *)ace)->AceFlags == 0,
           "Current User ACE has unexpected flags (0x%x != 0x0)\n", ((ACE_HEADER *)ace)->AceFlags);
        ok(ace->Mask == 0x1f01ff, "Current User ACE has unexpected mask (0x%lx != 0x1f01ff)\n",
                                    ace->Mask);
    }
    if (acl_size.AceCount > 1)
    {
        bret = GetAce(pDacl, 1, (VOID **)&ace);
        ok(bret, "Failed to get Administators Group ACE.\n");
        bret = EqualSid(&ace->SidStart, admin_sid);
        todo_wine ok(bret, "Administators Group ACE (%s) != Administators Group SID (%s).\n", debugstr_sid(&ace->SidStart), debugstr_sid(admin_sid));
        ok(((ACE_HEADER *)ace)->AceFlags == 0,
           "Administators Group ACE has unexpected flags (0x%x != 0x0)\n", ((ACE_HEADER *)ace)->AceFlags);
        ok(ace->Mask == 0x1f01ff, "Administators Group ACE has unexpected mask (0x%lx != 0x1f01ff)\n",
                                  ace->Mask);
    }
    LocalFree(pSD);
    CloseHandle(obj);

    /* Obtain the "domain users" SID from the user SID */
#ifdef __REACTOS__
    sid_size = sizeof(domain_sid_buffer);
    bret = GetWindowsAccountDomainSid(user_sid, domain_sid_buffer, &sid_size);
    if (bret)
#else
    if (!AllocateAndInitializeSid(&sia, 4, *GetSidSubAuthority(user_sid, 0),
                                  *GetSidSubAuthority(user_sid, 1),
                                  *GetSidSubAuthority(user_sid, 2),
                                  *GetSidSubAuthority(user_sid, 3), 0, 0, 0, 0, &domain_sid))
#endif
    {
#ifdef __REACTOS__
        sid_size = sizeof(domain_users_ptr);
        bret = CreateWellKnownSid(WinAccountDomainUsersSid, domain_sid_buffer, domain_users_sid, &sid_size);
        ok(bret, "CreateWellKnownSid failed with error %ld\n", GetLastError());
        if (!bret) return;
#else
        win_skip("Failed to get current domain SID\n");
        return;
#endif
    }
#ifdef __REACTOS__
    else
    {
        ok(GetLastError() == ERROR_NON_ACCOUNT_SID, "GetWindowsAccountDomainSid failed with error %ld\n", GetLastError());
        sid_size = sizeof(domain_users_ptr);
        bret = CreateWellKnownSid(WinBuiltinUsersSid, NULL, domain_users_sid, &sid_size);
        ok(bret, "CreateWellKnownSid failed with error %ld\n", GetLastError());
        if (!bret) return;
    }

    bret = InitializeAcl((PACL)process_dacl, sizeof(process_dacl), ACL_REVISION);
    ok(bret, "InitializeAcl failed with error %ld\n", GetLastError());
    if (!bret) return;
    bret = AddAccessAllowedAceEx((PACL)process_dacl, ACL_REVISION, 0, PROCESS_ALL_ACCESS, user_sid);
    ok(bret, "AddAccessAllowedAceEx(user) failed with error %ld\n", GetLastError());
    if (!bret) return;
    bret = AddAccessAllowedAceEx((PACL)process_dacl, ACL_REVISION, 0, PROCESS_ALL_ACCESS, admin_sid);
    ok(bret, "AddAccessAllowedAceEx(administrators) failed with error %ld\n", GetLastError());
    if (!bret) return;
    bret = AddAccessAllowedAceEx((PACL)process_dacl, ACL_REVISION,
                               INHERIT_ONLY_ACE | CONTAINER_INHERIT_ACE, GENERIC_READ, domain_users_sid);
    ok(bret, "AddAccessAllowedAceEx(group) failed with error %ld\n", GetLastError());
    if (!bret) return;
    bret = InitializeSecurityDescriptor(&process_sd, SECURITY_DESCRIPTOR_REVISION);
    ok(bret, "InitializeSecurityDescriptor failed with error %ld\n", GetLastError());
    if (!bret) return;
    bret = SetSecurityDescriptorOwner(&process_sd, user_sid, FALSE);
    ok(bret, "SetSecurityDescriptorOwner failed with error %ld\n", GetLastError());
    if (!bret) return;
    bret = SetSecurityDescriptorGroup(&process_sd, primary_group.group.PrimaryGroup, FALSE);
    ok(bret, "SetSecurityDescriptorGroup failed with error %ld\n", GetLastError());
    if (!bret) return;
    bret = SetSecurityDescriptorDacl(&process_sd, TRUE, (PACL)process_dacl, FALSE);
    ok(bret, "SetSecurityDescriptorDacl failed with error %ld\n", GetLastError());
    if (!bret) return;
    process_sa.lpSecurityDescriptor = &process_sd;
    snprintf(process_cmdline, sizeof(process_cmdline), "\"%s\" security descriptor", myARGV[0]);
    bret = CreateProcessA(NULL, process_cmdline, &process_sa, NULL, FALSE, CREATE_SUSPENDED,
                         NULL, NULL, &startup, &process_info);
    ok(bret, "CreateProcessA failed with error %ld\n", GetLastError());
    if (!bret) return;
#else
    sid_size = sizeof(domain_users_ptr);
    CreateWellKnownSid(WinAccountDomainUsersSid, domain_sid, domain_users_sid, &sid_size);
    FreeSid(domain_sid);
#endif

    /* Test querying the ownership of a process */
#ifdef __REACTOS__
    ret = GetSecurityInfo(process_info.hProcess, SE_KERNEL_OBJECT,
#else
    ret = GetSecurityInfo(GetCurrentProcess(), SE_KERNEL_OBJECT,
#endif
                           OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION,
                           NULL, NULL, NULL, NULL, &pSD);
    ok(!ret, "GetNamedSecurityInfo failed with error %ld\n", ret);
#ifdef __REACTOS__
    if (ret) goto process_done;
#endif

    bret = GetSecurityDescriptorOwner(pSD, &owner, &owner_defaulted);
    ok(bret, "GetSecurityDescriptorOwner failed with error %ld\n", GetLastError());
    ok(owner != NULL, "owner should not be NULL\n");
#ifdef __REACTOS__
    ok(owner && EqualSid(owner, user_sid), "Process owner SID != supplied owner SID.\n");
#else
    ok(EqualSid(owner, admin_sid) || EqualSid(owner, user_sid),
       "Process owner SID != Administrators SID.\n");
#endif

    bret = GetSecurityDescriptorGroup(pSD, &group, &group_defaulted);
    ok(bret, "GetSecurityDescriptorGroup failed with error %ld\n", GetLastError());
    ok(group != NULL, "group should not be NULL\n");
#ifdef __REACTOS__
    ok(group && EqualSid(group, primary_group.group.PrimaryGroup), "Process group SID != token primary group SID.\n");
#else
    ok(EqualSid(group, domain_users_sid), "Process group SID != Domain Users SID.\n");
#endif
    LocalFree(pSD);

    /* Test querying the DACL of a process */
#ifdef __REACTOS__
    ret = GetSecurityInfo(process_info.hProcess, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION,
#else
    ret = GetSecurityInfo(GetCurrentProcess(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION,
#endif
                                   NULL, NULL, NULL, NULL, &pSD);
    ok(!ret, "GetSecurityInfo failed with error %ld\n", ret);
#ifdef __REACTOS__
    if (ret) goto process_done;
#endif

    bret = GetSecurityDescriptorDacl(pSD, &dacl_present, &pDacl, &dacl_defaulted);
    ok(bret, "GetSecurityDescriptorDacl failed with error %ld\n", GetLastError());
    ok(dacl_present, "DACL should be present\n");
    ok(pDacl && IsValidAcl(pDacl), "GetSecurityDescriptorDacl returned invalid DACL.\n");
#ifdef __REACTOS__
    if (!bret || !dacl_present || !pDacl || !IsValidAcl(pDacl))
    {
        LocalFree(pSD);
        goto process_done;
    }
#endif
    bret = GetAclInformation(pDacl, &acl_size, sizeof(acl_size), AclSizeInformation);
    ok(bret, "GetAclInformation failed\n");
#ifdef __REACTOS__
    if (!bret)
    {
        LocalFree(pSD);
        goto process_done;
    }
    ok(acl_size.AceCount == 3, "Process DACL has %lu ACEs, expected 3\n", acl_size.AceCount);
#else
    ok(acl_size.AceCount != 0, "GetAclInformation returned no ACLs\n");
#endif
    for (i=0; i<acl_size.AceCount; i++)
    {
        bret = GetAce(pDacl, i, (VOID **)&ace);
        ok(bret, "Failed to get ACE %d.\n", i);
#ifdef __REACTOS__
        if (!bret) continue;
        ok(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE, "Process ACE %d has type %u\n", i, ace->Header.AceType);
        bret = domain_users_sid && EqualSid(&ace->SidStart, domain_users_sid);
#else
        bret = EqualSid(&ace->SidStart, domain_users_sid);
#endif
        if (bret) domain_users_ace_id = i;
        bret = EqualSid(&ace->SidStart, admin_sid);
        if (bret) admins_ace_id = i;
#ifdef __REACTOS__
        bret = EqualSid(&ace->SidStart, user_sid);
        if (bret) user_ace_id = i;
#endif
    }
#ifdef __REACTOS__
    ok(user_ace_id != -1, "Current User ACE not found.\n");
    if (user_ace_id != -1)
    {
        bret = GetAce(pDacl, user_ace_id, (VOID **)&ace);
        ok(bret, "Failed to get Current User ACE.\n");
        ok(ace->Header.AceFlags == 0, "Current User ACE has unexpected flags %#x\n", ace->Header.AceFlags);
        ok(ace->Mask == PROCESS_ALL_ACCESS, "Current User ACE has unexpected mask %#lx\n", ace->Mask);
    }
    ok(domain_users_ace_id != -1,
#else
    ok(domain_users_ace_id != -1 || broken(domain_users_ace_id == -1) /* win2k */,
#endif
       "Domain Users ACE not found.\n");
    if (domain_users_ace_id != -1)
    {
        bret = GetAce(pDacl, domain_users_ace_id, (VOID **)&ace);
        ok(bret, "Failed to get Domain Users ACE.\n");
        flags = ((ACE_HEADER *)ace)->AceFlags;
        ok(flags == (INHERIT_ONLY_ACE|CONTAINER_INHERIT_ACE),
           "Domain Users ACE has unexpected flags (0x%x != 0x%x)\n", flags,
           INHERIT_ONLY_ACE|CONTAINER_INHERIT_ACE);
#ifdef __REACTOS__
        ok(ace->Mask == GENERIC_READ, "Domain Users ACE has unexpected mask (0x%lx != 0x%lx)\n",
                                      ace->Mask, GENERIC_READ);
#else
        ok(ace->Mask == GENERIC_READ, "Domain Users ACE has unexpected mask (0x%lx != 0x%x)\n",
                                      ace->Mask, GENERIC_READ);
#endif
    }
#ifdef __REACTOS__
    ok(admins_ace_id != -1,
#else
    ok(admins_ace_id != -1 || broken(admins_ace_id == -1) /* xp */,
#endif
       "Builtin Admins ACE not found.\n");
    if (admins_ace_id != -1)
    {
        bret = GetAce(pDacl, admins_ace_id, (VOID **)&ace);
        ok(bret, "Failed to get Builtin Admins ACE.\n");
        flags = ((ACE_HEADER *)ace)->AceFlags;
        ok(flags == 0x0, "Builtin Admins ACE has unexpected flags (0x%x != 0x0)\n", flags);
#ifdef __REACTOS__
        ok(ace->Mask == PROCESS_ALL_ACCESS,
           "Builtin Admins ACE has unexpected mask (0x%lx != 0x%lx)\n", ace->Mask, PROCESS_ALL_ACCESS);
#else
#ifdef __REACTOS__
        ok(ace->Mask == PROCESS_ALL_ACCESS,
#else
        ok(ace->Mask == PROCESS_ALL_ACCESS || broken(ace->Mask == 0x1f0fff) /* win2k */,
#endif
           "Builtin Admins ACE has unexpected mask (0x%lx != 0x%x)\n", ace->Mask, PROCESS_ALL_ACCESS);
#endif
    }
    LocalFree(pSD);

#ifdef __REACTOS__
process_done:
    bret = TerminateProcess(process_info.hProcess, 0);
    ok(bret, "TerminateProcess failed with error %lu\n", GetLastError());
    ret = WaitForSingleObject(process_info.hProcess, 1000);
    ok(ret == WAIT_OBJECT_0, "Waiting for controlled process returned %#lx\n", ret);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);

#endif
    ret = GetSecurityInfo(NULL, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD);
    ok(ret == ERROR_INVALID_HANDLE, "got error %lu\n", ret);

    ret = GetSecurityInfo(GetCurrentProcess(), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD);
    ok(!ret, "got error %lu\n", ret);
    LocalFree(pSD);

    sa.lpSecurityDescriptor = sd;
    obj = CreateEventA(&sa, TRUE, TRUE, NULL);
#ifdef __REACTOS__
    ok(!!obj, "CreateEventA failed with error %lu\n", GetLastError());
    if (!obj) return;
#endif
    pDacl = (PACL)&dacl;

    for (size_t i = 0; i < ARRAY_SIZE(kernel_types); ++i)
    {
        winetest_push_context("Type %#x", kernel_types[i]);

        ret = GetSecurityInfo(NULL, kernel_types[i],
                DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD);
        ok(ret == ERROR_INVALID_HANDLE, "got error %lu\n", ret);

        ret = GetSecurityInfo(GetCurrentProcess(), kernel_types[i],
                DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD);
        ok(!ret, "got error %lu\n", ret);
        LocalFree(pSD);

        ret = GetSecurityInfo(obj, kernel_types[i],
                DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD);
        ok(!ret, "got error %lu\n", ret);
        LocalFree(pSD);

        ret = SetSecurityInfo(NULL, kernel_types[i],
                DACL_SECURITY_INFORMATION, NULL, NULL, pDacl, NULL);
        ok(ret == ERROR_INVALID_HANDLE, "got error %lu\n", ret);

        ret = SetSecurityInfo(obj, kernel_types[i],
                DACL_SECURITY_INFORMATION, NULL, NULL, pDacl, NULL);
        ok(!ret || ret == ERROR_NO_SECURITY_ON_OBJECT /* win 7 */, "got error %lu\n", ret);

        winetest_pop_context();
    }

    ret = GetSecurityInfo(GetCurrentProcess(), SE_REGISTRY_KEY,
            DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD);
    todo_wine ok(ret == ERROR_INVALID_HANDLE, "got error %lu\n", ret);

    ret = GetSecurityInfo(obj, SE_REGISTRY_KEY,
            DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD);
    todo_wine ok(ret == ERROR_INVALID_HANDLE, "got error %lu\n", ret);

#ifdef __REACTOS__
    sid_size = 0;
    ret = RegGetKeySecurity((HKEY)obj, DACL_SECURITY_INFORMATION, NULL, &sid_size);
    ok(ret == ERROR_INSUFFICIENT_BUFFER, "RegGetKeySecurity returned %lu\n", ret);
    ok(sid_size >= SECURITY_DESCRIPTOR_MIN_LENGTH, "RegGetKeySecurity size %lu\n", sid_size);
    pDacl = (PACL)dacl;
    bret = InitializeAcl(pDacl, sizeof(dacl), ACL_REVISION);
    ok(bret, "InitializeAcl failed with error %lu\n", GetLastError());
    bret = AddAccessDeniedAce(pDacl, ACL_REVISION, EVENT_MODIFY_STATE, user_sid);
    ok(bret, "AddAccessDeniedAce failed with error %lu\n", GetLastError());
    bret = AddAccessAllowedAce(pDacl, ACL_REVISION, SYNCHRONIZE, user_sid);
    ok(bret, "AddAccessAllowedAce failed with error %lu\n", GetLastError());
    ret = RegSetKeySecurity((HKEY)obj, DACL_SECURITY_INFORMATION, sd);
    ok(ret == ERROR_SUCCESS, "RegSetKeySecurity returned %lu\n", ret);

    for (i = 0; i < 2; ++i)
    {
        pSD = NULL;
        if (!i)
        {
            sid_size = 0;
            ret = RegGetKeySecurity((HKEY)obj, DACL_SECURITY_INFORMATION, NULL, &sid_size);
            ok(ret == ERROR_INSUFFICIENT_BUFFER, "RegGetKeySecurity returned %lu\n", ret);
            if (ret != ERROR_INSUFFICIENT_BUFFER) continue;
            pSD = LocalAlloc(LMEM_FIXED, sid_size);
            ok(!!pSD, "Failed to allocate %lu bytes\n", sid_size);
            if (!pSD) continue;
            ret = RegGetKeySecurity((HKEY)obj, DACL_SECURITY_INFORMATION, pSD, &sid_size);
            ok(ret == ERROR_SUCCESS, "RegGetKeySecurity returned %lu\n", ret);
        }
        else
        {
            ret = GetSecurityInfo(obj, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION,
                                  NULL, NULL, NULL, NULL, &pSD);
            ok(ret == ERROR_SUCCESS, "GetSecurityInfo after RegSetKeySecurity returned %lu\n", ret);
        }
        if (!ret)
        {
            pDacl = NULL;
            dacl_present = FALSE;
            bret = GetSecurityDescriptorDacl(pSD, &dacl_present, &pDacl, &dacl_defaulted);
            ok(bret && dacl_present && pDacl && IsValidAcl(pDacl), "Getter %d returned invalid event DACL\n", i);
            if (bret && dacl_present && pDacl && IsValidAcl(pDacl))
            {
                ok(pDacl->AceCount == 2, "Getter %d returned %u ACEs\n", i, pDacl->AceCount);
                for (unsigned int j = 0; j < pDacl->AceCount && j < 2; ++j)
                {
                    bret = GetAce(pDacl, j, (void **)&ace);
                    ok(bret, "GetAce %u failed with error %lu\n", j, GetLastError());
                    if (!bret) continue;
                    ok(ace->Header.AceType == (j ? ACCESS_ALLOWED_ACE_TYPE : ACCESS_DENIED_ACE_TYPE),
                       "Getter %d ACE %u type %u\n", i, j, ace->Header.AceType);
                    ok(!ace->Header.AceFlags, "Getter %d ACE %u flags %#x\n", i, j, ace->Header.AceFlags);
                    ok(ace->Mask == (j ? SYNCHRONIZE : EVENT_MODIFY_STATE),
                       "Getter %d ACE %u mask %#lx\n", i, j, ace->Mask);
                    ok(EqualSid(&ace->SidStart, user_sid), "Getter %d ACE %u SID changed\n", i, j);
                }
            }
        }
        LocalFree(pSD);
    }

    pDacl = (PACL)dacl;
    sid_size = 0;
    ret = RegGetKeySecurity(NULL, DACL_SECURITY_INFORMATION, NULL, &sid_size);
    ok(ret == ERROR_INVALID_HANDLE, "RegGetKeySecurity(NULL) returned %lu\n", ret);
    ret = RegSetKeySecurity(NULL, DACL_SECURITY_INFORMATION, sd);
    ok(ret == ERROR_INVALID_HANDLE, "RegSetKeySecurity(NULL) returned %lu\n", ret);

    closed_obj = obj;
#endif
    CloseHandle(obj);
#ifdef __REACTOS__
    sid_size = 0;
    ret = RegGetKeySecurity((HKEY)closed_obj, DACL_SECURITY_INFORMATION, NULL, &sid_size);
    ok(ret == ERROR_INVALID_HANDLE, "RegGetKeySecurity(closed) returned %lu\n", ret);
    ret = RegSetKeySecurity((HKEY)closed_obj, DACL_SECURITY_INFORMATION, sd);
    ok(ret == ERROR_INVALID_HANDLE, "RegSetKeySecurity(closed) returned %lu\n", ret);
#endif

    for (size_t i = 0; i < ARRAY_SIZE(invalid_types); ++i)
    {
        winetest_push_context("Type %#x", invalid_types[i]);

        ret = GetSecurityInfo(NULL, invalid_types[i],
                DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD);
        ok(ret == ERROR_INVALID_HANDLE, "got error %lu\n", ret);

        ret = GetSecurityInfo((HANDLE)0xdeadbeef, invalid_types[i],
                DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD);
        todo_wine ok(ret == ERROR_INVALID_PARAMETER, "got error %lu\n", ret);

        ret = SetSecurityInfo(NULL, invalid_types[i],
                DACL_SECURITY_INFORMATION, NULL, NULL, pDacl, NULL);
        ok(ret == ERROR_INVALID_HANDLE, "got error %lu\n", ret);

        ret = SetSecurityInfo((HANDLE)0xdeadbeef, invalid_types[i],
                DACL_SECURITY_INFORMATION, NULL, NULL, pDacl, NULL);
        todo_wine ok(ret == ERROR_INVALID_PARAMETER, "got error %lu\n", ret);

        winetest_pop_context();
    }
}

static void test_GetSidSubAuthority(void)
{
    PSID psid = NULL;

    /* Note: on windows passing in an invalid index like -1, lets GetSidSubAuthority return 0x05000000 but
             still GetLastError returns ERROR_SUCCESS then. We don't test these unlikely cornercases here for now */
    ok(ConvertStringSidToSidA("S-1-5-21-93476-23408-4576",&psid),"ConvertStringSidToSidA failed\n");
    ok(IsValidSid(psid),"Sid is not valid\n");
    SetLastError(0xbebecaca);
    ok(*GetSidSubAuthorityCount(psid) == 4,"GetSidSubAuthorityCount gave %d expected 4\n", *GetSidSubAuthorityCount(psid));
    ok(GetLastError() == 0,"GetLastError returned %ld instead of 0\n",GetLastError());
    SetLastError(0xbebecaca);
    ok(*GetSidSubAuthority(psid,0) == 21,"GetSidSubAuthority gave %ld expected 21\n", *GetSidSubAuthority(psid,0));
    ok(GetLastError() == 0,"GetLastError returned %ld instead of 0\n",GetLastError());
    SetLastError(0xbebecaca);
    ok(*GetSidSubAuthority(psid,1) == 93476,"GetSidSubAuthority gave %ld expected 93476\n", *GetSidSubAuthority(psid,1));
    ok(GetLastError() == 0,"GetLastError returned %ld instead of 0\n",GetLastError());
    SetLastError(0xbebecaca);
    ok(GetSidSubAuthority(psid,4) != NULL,"Expected out of bounds GetSidSubAuthority to return a non-NULL pointer\n");
    ok(GetLastError() == 0,"GetLastError returned %ld instead of 0\n",GetLastError());
    LocalFree(psid);
}

static void test_CheckTokenMembership(void)
{
    PTOKEN_GROUPS token_groups;
    DWORD size;
    HANDLE process_token, token;
    BOOL is_member;
    BOOL ret;
    DWORD i;

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE|TOKEN_QUERY, &process_token);
    ok(ret, "OpenProcessToken failed with error %ld\n", GetLastError());

    ret = DuplicateToken(process_token, SecurityImpersonation, &token);
    ok(ret, "DuplicateToken failed with error %ld\n", GetLastError());

    /* groups */
    ret = GetTokenInformation(token, TokenGroups, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "GetTokenInformation(TokenGroups) %s with error %ld\n",
        ret ? "succeeded" : "failed", GetLastError());
    token_groups = malloc(size);
    ret = GetTokenInformation(token, TokenGroups, token_groups, size, &size);
    ok(ret, "GetTokenInformation(TokenGroups) failed with error %ld\n", GetLastError());

    for (i = 0; i < token_groups->GroupCount; i++)
    {
        if (token_groups->Groups[i].Attributes & SE_GROUP_ENABLED)
            break;
    }

    if (i == token_groups->GroupCount)
    {
        free(token_groups);
        CloseHandle(token);
        skip("user not a member of any group\n");
        return;
    }

    is_member = FALSE;
    ret = CheckTokenMembership(token, token_groups->Groups[i].Sid, &is_member);
    ok(ret, "CheckTokenMembership failed with error %ld\n", GetLastError());
    ok(is_member, "CheckTokenMembership should have detected sid as member\n");

    is_member = FALSE;
    ret = CheckTokenMembership(NULL, token_groups->Groups[i].Sid, &is_member);
    ok(ret, "CheckTokenMembership failed with error %ld\n", GetLastError());
    ok(is_member, "CheckTokenMembership should have detected sid as member\n");

    is_member = TRUE;
    SetLastError(0xdeadbeef);
    ret = CheckTokenMembership(process_token, token_groups->Groups[i].Sid, &is_member);
    ok(!ret && GetLastError() == ERROR_NO_IMPERSONATION_TOKEN,
        "CheckTokenMembership with process token %s with error %ld\n",
        ret ? "succeeded" : "failed", GetLastError());
    ok(!is_member, "CheckTokenMembership should have cleared is_member\n");

    free(token_groups);
    CloseHandle(token);
    CloseHandle(process_token);
}

static void test_EqualSid(void)
{
    PSID sid1, sid2;
    BOOL ret;
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = { SECURITY_WORLD_SID_AUTHORITY };
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = { SECURITY_NT_AUTHORITY };

    SetLastError(0xdeadbeef);
    ret = AllocateAndInitializeSid(&SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid1);
    if (!ret && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        win_skip("AllocateAndInitializeSid is not implemented\n");
        return;
    }
    ok(ret, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());
    ok(GetLastError() == 0xdeadbeef,
       "AllocateAndInitializeSid shouldn't have set last error to %ld\n",
       GetLastError());

    ret = AllocateAndInitializeSid(&SIDAuthWorld, 1, SECURITY_WORLD_RID,
        0, 0, 0, 0, 0, 0, 0, &sid2);
    ok(ret, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = EqualSid(sid1, sid2);
    ok(!ret, "World and domain admins sids shouldn't have been equal\n");
    ok(GetLastError() == ERROR_SUCCESS,
       "EqualSid should have set last error to ERROR_SUCCESS instead of %ld\n",
       GetLastError());

    SetLastError(0xdeadbeef);
    sid2 = FreeSid(sid2);
    ok(!sid2, "FreeSid should have returned NULL instead of %p\n", sid2);
    ok(GetLastError() == 0xdeadbeef,
       "FreeSid shouldn't have set last error to %ld\n",
       GetLastError());

    ret = AllocateAndInitializeSid(&SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid2);
    ok(ret, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = EqualSid(sid1, sid2);
    ok(ret, "Same sids should have been equal %s != %s\n",
       debugstr_sid(sid1), debugstr_sid(sid2));
    ok(GetLastError() == ERROR_SUCCESS,
       "EqualSid should have set last error to ERROR_SUCCESS instead of %ld\n",
       GetLastError());

    ((SID *)sid2)->Revision = 2;
    SetLastError(0xdeadbeef);
    ret = EqualSid(sid1, sid2);
    ok(!ret, "EqualSid with invalid sid should have returned FALSE\n");
    ok(GetLastError() == ERROR_SUCCESS,
       "EqualSid should have set last error to ERROR_SUCCESS instead of %ld\n",
       GetLastError());
    ((SID *)sid2)->Revision = SID_REVISION;

    FreeSid(sid1);
    FreeSid(sid2);
}

static void test_GetUserNameA(void)
{
    char buffer[UNLEN + 1], filler[UNLEN + 1];
    DWORD required_len, buffer_len;
    BOOL ret;

    /* Test crashes on Windows. */
    if (0)
    {
        SetLastError(0xdeadbeef);
        GetUserNameA(NULL, NULL);
    }

    SetLastError(0xdeadbeef);
    required_len = 0;
    ret = GetUserNameA(NULL, &required_len);
    ok(ret == FALSE, "GetUserNameA returned %d\n", ret);
    ok(required_len != 0, "Outputted buffer length was %lu\n", required_len);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Last error was %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    required_len = 1;
    ret = GetUserNameA(NULL, &required_len);
    ok(ret == FALSE, "GetUserNameA returned %d\n", ret);
    ok(required_len != 0 && required_len != 1, "Outputted buffer length was %lu\n", required_len);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Last error was %lu\n", GetLastError());

    /* Tests crashes on Windows. */
    if (0)
    {
        SetLastError(0xdeadbeef);
        required_len = UNLEN + 1;
        GetUserNameA(NULL, &required_len);

        SetLastError(0xdeadbeef);
        GetUserNameA(buffer, NULL);
    }

    memset(filler, 'x', sizeof(filler));

    /* Note that GetUserNameA on XP and newer outputs the number of bytes
     * required for a Unicode string, which affects a test in the next block. */
    SetLastError(0xdeadbeef);
    memcpy(buffer, filler, sizeof(filler));
    required_len = 0;
    ret = GetUserNameA(buffer, &required_len);
    ok(ret == FALSE, "GetUserNameA returned %d\n", ret);
    ok(!memcmp(buffer, filler, sizeof(filler)), "Output buffer was altered\n");
    ok(required_len != 0, "Outputted buffer length was %lu\n", required_len);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Last error was %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    memcpy(buffer, filler, sizeof(filler));
    buffer_len = required_len;
    ret = GetUserNameA(buffer, &buffer_len);
    ok(ret == TRUE, "GetUserNameA returned %d, last error %lu\n", ret, GetLastError());
    ok(memcmp(buffer, filler, sizeof(filler)) != 0, "Output buffer was untouched\n");
    ok(buffer_len == required_len ||
       broken(buffer_len == required_len / sizeof(WCHAR)), /* XP+ */
       "Outputted buffer length was %lu\n", buffer_len);
    ok(GetLastError() == 0xdeadbeef, "Last error was %lu\n", GetLastError());

    /* Use the reported buffer size from the last GetUserNameA call and pass
     * a length that is one less than the required value. */
    SetLastError(0xdeadbeef);
    memcpy(buffer, filler, sizeof(filler));
    buffer_len--;
    ret = GetUserNameA(buffer, &buffer_len);
    ok(ret == FALSE, "GetUserNameA returned %d\n", ret);
    ok(!memcmp(buffer, filler, sizeof(filler)), "Output buffer was untouched\n");
    ok(buffer_len == required_len, "Outputted buffer length was %lu\n", buffer_len);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Last error was %lu\n", GetLastError());
}

static void test_GetUserNameW(void)
{
    WCHAR buffer[UNLEN + 1], filler[UNLEN + 1];
    DWORD required_len, buffer_len;
    BOOL ret;

    /* Test crashes on Windows. */
    if (0)
    {
        SetLastError(0xdeadbeef);
        GetUserNameW(NULL, NULL);
    }

    SetLastError(0xdeadbeef);
    required_len = 0;
    ret = GetUserNameW(NULL, &required_len);
    ok(ret == FALSE, "GetUserNameW returned %d\n", ret);
    ok(required_len != 0, "Outputted buffer length was %lu\n", required_len);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Last error was %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    required_len = 1;
    ret = GetUserNameW(NULL, &required_len);
    ok(ret == FALSE, "GetUserNameW returned %d\n", ret);
    ok(required_len != 0 && required_len != 1, "Outputted buffer length was %lu\n", required_len);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Last error was %lu\n", GetLastError());

    /* Tests crash on Windows. */
    if (0)
    {
        SetLastError(0xdeadbeef);
        required_len = UNLEN + 1;
        GetUserNameW(NULL, &required_len);

        SetLastError(0xdeadbeef);
        GetUserNameW(buffer, NULL);
    }

    memset(filler, 'x', sizeof(filler));

    SetLastError(0xdeadbeef);
    memcpy(buffer, filler, sizeof(filler));
    required_len = 0;
    ret = GetUserNameW(buffer, &required_len);
    ok(ret == FALSE, "GetUserNameW returned %d\n", ret);
    ok(!memcmp(buffer, filler, sizeof(filler)), "Output buffer was altered\n");
    ok(required_len != 0, "Outputted buffer length was %lu\n", required_len);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Last error was %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    memcpy(buffer, filler, sizeof(filler));
    buffer_len = required_len;
    ret = GetUserNameW(buffer, &buffer_len);
    ok(ret == TRUE, "GetUserNameW returned %d, last error %lu\n", ret, GetLastError());
    ok(memcmp(buffer, filler, sizeof(filler)) != 0, "Output buffer was untouched\n");
    ok(buffer_len == required_len, "Outputted buffer length was %lu\n", buffer_len);
    ok(GetLastError() == 0xdeadbeef, "Last error was %lu\n", GetLastError());

    /* GetUserNameW on XP and newer writes a truncated portion of the username string to the buffer. */
    SetLastError(0xdeadbeef);
    memcpy(buffer, filler, sizeof(filler));
    buffer_len--;
    ret = GetUserNameW(buffer, &buffer_len);
    ok(ret == FALSE, "GetUserNameW returned %d\n", ret);
    ok(!memcmp(buffer, filler, sizeof(filler)) ||
       broken(memcmp(buffer, filler, sizeof(filler)) != 0), /* XP+ */
       "Output buffer was altered\n");
    ok(buffer_len == required_len, "Outputted buffer length was %lu\n", buffer_len);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Last error was %lu\n", GetLastError());
}

static void test_CreateRestrictedToken(void)
{
    HANDLE process_token, token, r_token;
    PTOKEN_GROUPS token_groups, groups2;
    LUID_AND_ATTRIBUTES lattr;
    SID_AND_ATTRIBUTES sattr;
    SECURITY_IMPERSONATION_LEVEL level;
    SID *removed_sid = NULL;
    char privs_buffer[1000];
    TOKEN_PRIVILEGES *privs = (TOKEN_PRIVILEGES *)privs_buffer;
    PRIVILEGE_SET priv_set;
    TOKEN_TYPE type;
    BOOL is_member;
    DWORD size;
    LUID luid = { 0, 0 };
    BOOL ret;
    DWORD i;

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE|TOKEN_QUERY, &process_token);
    ok(ret, "got error %ld\n", GetLastError());

    ret = DuplicateTokenEx(process_token, TOKEN_DUPLICATE|TOKEN_ADJUST_GROUPS|TOKEN_QUERY,
        NULL, SecurityImpersonation, TokenImpersonation, &token);
    ok(ret, "got error %ld\n", GetLastError());

    ret = GetTokenInformation(token, TokenGroups, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "got %d with error %ld\n", ret, GetLastError());
    token_groups = malloc(size);
    ret = GetTokenInformation(token, TokenGroups, token_groups, size, &size);
    ok(ret, "got error %ld\n", GetLastError());

    for (i = 0; i < token_groups->GroupCount; i++)
    {
        if (token_groups->Groups[i].Attributes & SE_GROUP_ENABLED)
        {
            removed_sid = token_groups->Groups[i].Sid;
            break;
        }
    }
    ok(!!removed_sid, "user is not a member of any group\n");

    is_member = FALSE;
    ret = CheckTokenMembership(token, removed_sid, &is_member);
    ok(ret, "got error %ld\n", GetLastError());
    ok(is_member, "not a member\n");

    sattr.Sid = removed_sid;
    sattr.Attributes = 0;
    r_token = NULL;
    ret = CreateRestrictedToken(token, 0, 1, &sattr, 0, NULL, 0, NULL, &r_token);
    ok(ret, "got error %ld\n", GetLastError());

    is_member = TRUE;
    ret = CheckTokenMembership(r_token, removed_sid, &is_member);
    ok(ret, "got error %ld\n", GetLastError());
    ok(!is_member, "not a member\n");

    ret = GetTokenInformation(r_token, TokenGroups, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "got %d with error %ld\n",
        ret, GetLastError());
    groups2 = malloc(size);
    ret = GetTokenInformation(r_token, TokenGroups, groups2, size, &size);
    ok(ret, "got error %ld\n", GetLastError());

    for (i = 0; i < groups2->GroupCount; i++)
    {
        if (EqualSid(groups2->Groups[i].Sid, removed_sid))
        {
            DWORD attr = groups2->Groups[i].Attributes;
            ok(attr & SE_GROUP_USE_FOR_DENY_ONLY, "got wrong attributes %#lx\n", attr);
            ok(!(attr & SE_GROUP_ENABLED), "got wrong attributes %#lx\n", attr);
            break;
        }
    }

    free(groups2);

    size = sizeof(type);
    ret = GetTokenInformation(r_token, TokenType, &type, size, &size);
    ok(ret, "got error %ld\n", GetLastError());
    ok(type == TokenImpersonation, "got type %u\n", type);

    size = sizeof(level);
    ret = GetTokenInformation(r_token, TokenImpersonationLevel, &level, size, &size);
    ok(ret, "got error %ld\n", GetLastError());
    ok(level == SecurityImpersonation, "got level %u\n", type);

    CloseHandle(r_token);

    r_token = NULL;
    ret = CreateRestrictedToken(process_token, 0, 1, &sattr, 0, NULL, 0, NULL, &r_token);
    ok(ret, "got error %lu\n", GetLastError());

    size = sizeof(type);
    ret = GetTokenInformation(r_token, TokenType, &type, size, &size);
    ok(ret, "got error %lu\n", GetLastError());
    ok(type == TokenPrimary, "got type %u\n", type);

    CloseHandle(r_token);

    ret = GetTokenInformation(token, TokenPrivileges, privs, sizeof(privs_buffer), &size);
    ok(ret, "got error %lu\n", GetLastError());

    for (i = 0; i < privs->PrivilegeCount; i++)
    {
        if (privs->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED)
        {
            luid = privs->Privileges[i].Luid;
            break;
        }
    }
    ok(i < privs->PrivilegeCount, "user has no privileges\n");

    lattr.Luid = luid;
    lattr.Attributes = 0;
    r_token = NULL;
    ret = CreateRestrictedToken(token, 0, 0, NULL, 1, &lattr, 0, NULL, &r_token);
    ok(ret, "got error %lu\n", GetLastError());

    priv_set.PrivilegeCount = 1;
    priv_set.Control = 0;
    priv_set.Privilege[0].Luid = luid;
    priv_set.Privilege[0].Attributes = 0;
    ret = PrivilegeCheck(r_token, &priv_set, &is_member);
    ok(ret, "got error %lu\n", GetLastError());
    ok(!is_member, "privilege should not be enabled\n");

    ret = GetTokenInformation(r_token, TokenPrivileges, privs, sizeof(privs_buffer), &size);
    ok(ret, "got error %lu\n", GetLastError());

    is_member = FALSE;
    for (i = 0; i < privs->PrivilegeCount; i++)
    {
        if (!memcmp(&privs->Privileges[i].Luid, &luid, sizeof(luid)))
            is_member = TRUE;
    }
    ok(!is_member, "disabled privilege should not be present\n");

    CloseHandle(r_token);

    removed_sid->SubAuthority[0] = 0xdeadbeef;
    lattr.Luid.LowPart = 0xdeadbeef;
    r_token = NULL;
    ret = CreateRestrictedToken(token, 0, 1, &sattr, 1, &lattr, 0, NULL, &r_token);
    ok(ret, "got error %lu\n", GetLastError());
    CloseHandle(r_token);

    free(token_groups);
    CloseHandle(token);
    CloseHandle(process_token);
}

static void validate_default_security_descriptor(SECURITY_DESCRIPTOR *sd)
{
    BOOL ret, present, defaulted;
    ACL *acl;
    void *sid;

    ret = IsValidSecurityDescriptor(sd);
    ok(ret, "security descriptor is not valid\n");

    present = -1;
    defaulted = -1;
    acl = (void *)0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = GetSecurityDescriptorDacl(sd, &present, &acl, &defaulted);
    ok(ret, "GetSecurityDescriptorDacl error %ld\n", GetLastError());
    todo_wine
    ok(present == 1, "acl is not present\n");
    todo_wine
    ok(acl != (void *)0xdeadbeef && acl != NULL, "acl pointer is not set\n");
    ok(defaulted == 0, "defaulted is set to TRUE\n");

    defaulted = -1;
    sid = (void *)0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = GetSecurityDescriptorOwner(sd, &sid, &defaulted);
    ok(ret, "GetSecurityDescriptorOwner error %ld\n", GetLastError());
    todo_wine
    ok(sid != (void *)0xdeadbeef && sid != NULL, "sid pointer is not set\n");
    ok(defaulted == 0, "defaulted is set to TRUE\n");

    defaulted = -1;
    sid = (void *)0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = GetSecurityDescriptorGroup(sd, &sid, &defaulted);
    ok(ret, "GetSecurityDescriptorGroup error %ld\n", GetLastError());
    todo_wine
    ok(sid != (void *)0xdeadbeef && sid != NULL, "sid pointer is not set\n");
    ok(defaulted == 0, "defaulted is set to TRUE\n");
}

static void test_default_handle_security(HANDLE token, HANDLE handle, GENERIC_MAPPING *mapping)
{
    DWORD ret, granted, priv_set_len;
    BOOL status;
    PRIVILEGE_SET priv_set;
    SECURITY_DESCRIPTOR *sd;

    sd = test_get_security_descriptor(handle, __LINE__);
    validate_default_security_descriptor(sd);

    priv_set_len = sizeof(priv_set);
    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = AccessCheck(sd, token, MAXIMUM_ALLOWED, mapping, &priv_set, &priv_set_len, &granted, &status);
todo_wine {
    ok(ret, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == mapping->GenericAll, "expected all access %#lx, got %#lx\n", mapping->GenericAll, granted);
}
    priv_set_len = sizeof(priv_set);
    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = AccessCheck(sd, token, 0, mapping, &priv_set, &priv_set_len, &granted, &status);
todo_wine {
    ok(ret, "AccessCheck error %ld\n", GetLastError());
    ok(status == 0, "expected 0, got %d\n", status);
    ok(granted == 0, "expected 0, got %#lx\n", granted);
}
    priv_set_len = sizeof(priv_set);
    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = AccessCheck(sd, token, ACCESS_SYSTEM_SECURITY, mapping, &priv_set, &priv_set_len, &granted, &status);
todo_wine {
    ok(ret, "AccessCheck error %ld\n", GetLastError());
    ok(status == 0, "expected 0, got %d\n", status);
    ok(granted == 0, "expected 0, got %#lx\n", granted);
}
    priv_set_len = sizeof(priv_set);
    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = AccessCheck(sd, token, mapping->GenericRead, mapping, &priv_set, &priv_set_len, &granted, &status);
todo_wine {
    ok(ret, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == mapping->GenericRead, "expected read access %#lx, got %#lx\n", mapping->GenericRead, granted);
}
    priv_set_len = sizeof(priv_set);
    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = AccessCheck(sd, token, mapping->GenericWrite, mapping, &priv_set, &priv_set_len, &granted, &status);
todo_wine {
    ok(ret, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == mapping->GenericWrite, "expected write access %#lx, got %#lx\n", mapping->GenericWrite, granted);
}
    priv_set_len = sizeof(priv_set);
    granted = 0xdeadbeef;
    status = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = AccessCheck(sd, token, mapping->GenericExecute, mapping, &priv_set, &priv_set_len, &granted, &status);
todo_wine {
    ok(ret, "AccessCheck error %ld\n", GetLastError());
    ok(status == 1, "expected 1, got %d\n", status);
    ok(granted == mapping->GenericExecute, "expected execute access %#lx, got %#lx\n", mapping->GenericExecute, granted);
}
    free(sd);
}

static ACCESS_MASK get_obj_access(HANDLE obj)
{
    OBJECT_BASIC_INFORMATION info;
    NTSTATUS status;

    status = NtQueryObject(obj, ObjectBasicInformation, &info, sizeof(info), NULL);
    ok(!status, "NtQueryObject error %#lx\n", status);

    return info.GrantedAccess;
}

static void test_mutex_security(HANDLE token)
{
    DWORD ret, i, access;
    HANDLE mutex, dup;
    GENERIC_MAPPING mapping = { STANDARD_RIGHTS_READ | MUTANT_QUERY_STATE | SYNCHRONIZE,
                                STANDARD_RIGHTS_WRITE | MUTEX_MODIFY_STATE | SYNCHRONIZE,
                                STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
                                STANDARD_RIGHTS_ALL | MUTEX_ALL_ACCESS };
    static const struct
    {
        int generic, mapped;
    } map[] =
    {
        { 0, 0 },
        { GENERIC_READ, STANDARD_RIGHTS_READ | MUTANT_QUERY_STATE },
        { GENERIC_WRITE, STANDARD_RIGHTS_WRITE },
        { GENERIC_EXECUTE, STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE },
        { GENERIC_ALL, STANDARD_RIGHTS_ALL | MUTANT_QUERY_STATE }
    };

    SetLastError(0xdeadbeef);
    mutex = OpenMutexA(0, FALSE, "WineTestMutex");
    ok(!mutex, "mutex should not exist\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "wrong error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    mutex = CreateMutexA(NULL, FALSE, "WineTestMutex");
    ok(mutex != 0, "CreateMutex error %ld\n", GetLastError());

    access = get_obj_access(mutex);
    ok(access == MUTANT_ALL_ACCESS, "expected MUTANT_ALL_ACCESS, got %#lx\n", access);

    for (i = 0; i < ARRAY_SIZE(map); i++)
    {
        SetLastError( 0xdeadbeef );
        ret = DuplicateHandle(GetCurrentProcess(), mutex, GetCurrentProcess(), &dup,
                              map[i].generic, FALSE, 0);
        ok(ret, "DuplicateHandle error %ld\n", GetLastError());

        access = get_obj_access(dup);
        ok(access == map[i].mapped, "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);

        CloseHandle(dup);

        SetLastError(0xdeadbeef);
        dup = OpenMutexA(0, FALSE, "WineTestMutex");
        ok(!dup, "OpenMutex should fail\n");
        ok(GetLastError() == ERROR_ACCESS_DENIED, "wrong error %lu\n", GetLastError());
    }

    test_default_handle_security(token, mutex, &mapping);

    CloseHandle (mutex);
}

static void test_event_security(HANDLE token)
{
    DWORD ret, i, access;
    HANDLE event, dup;
    GENERIC_MAPPING mapping = { STANDARD_RIGHTS_READ | EVENT_QUERY_STATE | SYNCHRONIZE,
                                STANDARD_RIGHTS_WRITE | EVENT_MODIFY_STATE | SYNCHRONIZE,
                                STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
                                STANDARD_RIGHTS_ALL | EVENT_ALL_ACCESS };
    static const struct
    {
        int generic, mapped;
    } map[] =
    {
        { 0, 0 },
        { GENERIC_READ, STANDARD_RIGHTS_READ | EVENT_QUERY_STATE },
        { GENERIC_WRITE, STANDARD_RIGHTS_WRITE | EVENT_MODIFY_STATE },
        { GENERIC_EXECUTE, STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE },
        { GENERIC_ALL, STANDARD_RIGHTS_ALL | EVENT_QUERY_STATE | EVENT_MODIFY_STATE }
    };

    SetLastError(0xdeadbeef);
    event = OpenEventA(0, FALSE, "WineTestEvent");
    ok(!event, "event should not exist\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "wrong error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    event = CreateEventA(NULL, FALSE, FALSE, "WineTestEvent");
    ok(event != 0, "CreateEvent error %ld\n", GetLastError());

    access = get_obj_access(event);
    ok(access == EVENT_ALL_ACCESS, "expected EVENT_ALL_ACCESS, got %#lx\n", access);

    for (i = 0; i < ARRAY_SIZE(map); i++)
    {
        SetLastError( 0xdeadbeef );
        ret = DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &dup,
                              map[i].generic, FALSE, 0);
        ok(ret, "DuplicateHandle error %ld\n", GetLastError());

        access = get_obj_access(dup);
        ok(access == map[i].mapped, "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);

        CloseHandle(dup);

        SetLastError(0xdeadbeef);
        dup = OpenEventA(0, FALSE, "WineTestEvent");
        ok(!dup, "OpenEvent should fail\n");
        ok(GetLastError() == ERROR_ACCESS_DENIED, "wrong error %lu\n", GetLastError());
    }

    test_default_handle_security(token, event, &mapping);

    CloseHandle(event);
}

static void test_semaphore_security(HANDLE token)
{
    DWORD ret, i, access;
    HANDLE sem, dup;
    GENERIC_MAPPING mapping = { STANDARD_RIGHTS_READ | SEMAPHORE_QUERY_STATE,
                                STANDARD_RIGHTS_WRITE | SEMAPHORE_MODIFY_STATE,
                                STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
                                STANDARD_RIGHTS_ALL | SEMAPHORE_ALL_ACCESS };
    static const struct
    {
        int generic, mapped;
    } map[] =
    {
        { 0, 0 },
        { GENERIC_READ, STANDARD_RIGHTS_READ | SEMAPHORE_QUERY_STATE },
        { GENERIC_WRITE, STANDARD_RIGHTS_WRITE | SEMAPHORE_MODIFY_STATE },
        { GENERIC_EXECUTE, STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE },
        { GENERIC_ALL, STANDARD_RIGHTS_ALL | SEMAPHORE_QUERY_STATE | SEMAPHORE_MODIFY_STATE }
    };

    SetLastError(0xdeadbeef);
    sem = OpenSemaphoreA(0, FALSE, "WineTestSemaphore");
    ok(!sem, "semaphore should not exist\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "wrong error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    sem = CreateSemaphoreA(NULL, 0, 10, "WineTestSemaphore");
    ok(sem != 0, "CreateSemaphore error %ld\n", GetLastError());

    access = get_obj_access(sem);
    ok(access == SEMAPHORE_ALL_ACCESS, "expected SEMAPHORE_ALL_ACCESS, got %#lx\n", access);

    for (i = 0; i < ARRAY_SIZE(map); i++)
    {
        SetLastError( 0xdeadbeef );
        ret = DuplicateHandle(GetCurrentProcess(), sem, GetCurrentProcess(), &dup,
                              map[i].generic, FALSE, 0);
        ok(ret, "DuplicateHandle error %ld\n", GetLastError());

        access = get_obj_access(dup);
        ok(access == map[i].mapped, "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);

        CloseHandle(dup);
    }

    test_default_handle_security(token, sem, &mapping);

    CloseHandle(sem);
}

#define WINE_TEST_PIPE "\\\\.\\pipe\\WineTestPipe"
static void test_named_pipe_security(HANDLE token)
{
    DWORD ret, i, access;
    HANDLE pipe, file, dup;
    GENERIC_MAPPING mapping = { FILE_GENERIC_READ,
                                FILE_GENERIC_WRITE,
                                FILE_GENERIC_EXECUTE,
                                STANDARD_RIGHTS_ALL | FILE_ALL_ACCESS };
    static const struct
    {
        int generic, mapped;
    } map[] =
    {
        { 0, 0 },
        { GENERIC_READ, FILE_GENERIC_READ },
        { GENERIC_WRITE, FILE_GENERIC_WRITE },
        { GENERIC_EXECUTE, FILE_GENERIC_EXECUTE },
        { GENERIC_ALL, STANDARD_RIGHTS_ALL | FILE_ALL_ACCESS }
    };
    static const struct
    {
        DWORD open_mode;
        DWORD access;
    } creation_access[] =
    {
        { PIPE_ACCESS_INBOUND, FILE_GENERIC_READ },
        { PIPE_ACCESS_OUTBOUND, FILE_GENERIC_WRITE },
        { PIPE_ACCESS_DUPLEX, FILE_GENERIC_READ|FILE_GENERIC_WRITE },
        { PIPE_ACCESS_INBOUND|WRITE_DAC, FILE_GENERIC_READ|WRITE_DAC },
        { PIPE_ACCESS_INBOUND|WRITE_OWNER, FILE_GENERIC_READ|WRITE_OWNER }
        /* ACCESS_SYSTEM_SECURITY is also valid, but will fail with ERROR_PRIVILEGE_NOT_HELD */
    };

    /* Test the different security access options for pipes */
    for (i = 0; i < ARRAY_SIZE(creation_access); i++)
    {
        SetLastError(0xdeadbeef);
        pipe = CreateNamedPipeA(WINE_TEST_PIPE, creation_access[i].open_mode,
                                PIPE_TYPE_BYTE | PIPE_NOWAIT, PIPE_UNLIMITED_INSTANCES, 0, 0,
                                NMPWAIT_USE_DEFAULT_WAIT, NULL);
        ok(pipe != INVALID_HANDLE_VALUE, "CreateNamedPipe(0x%lx) error %ld\n",
                                         creation_access[i].open_mode, GetLastError());
        access = get_obj_access(pipe);
        ok(access == creation_access[i].access,
           "CreateNamedPipeA(0x%lx) pipe expected access 0x%lx (got 0x%lx)\n",
           creation_access[i].open_mode, creation_access[i].access, access);
        CloseHandle(pipe);
    }

    SetLastError(0xdeadbeef);
    pipe = CreateNamedPipeA(WINE_TEST_PIPE, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                            PIPE_TYPE_BYTE | PIPE_NOWAIT, PIPE_UNLIMITED_INSTANCES,
                            0, 0, NMPWAIT_USE_DEFAULT_WAIT, NULL);
    ok(pipe != INVALID_HANDLE_VALUE, "CreateNamedPipe error %ld\n", GetLastError());

    test_default_handle_security(token, pipe, &mapping);

    SetLastError(0xdeadbeef);
    file = CreateFileA(WINE_TEST_PIPE, FILE_ALL_ACCESS, 0, NULL, OPEN_EXISTING, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());

    access = get_obj_access(file);
    ok(access == FILE_ALL_ACCESS, "expected FILE_ALL_ACCESS, got %#lx\n", access);

    for (i = 0; i < ARRAY_SIZE(map); i++)
    {
        SetLastError( 0xdeadbeef );
        ret = DuplicateHandle(GetCurrentProcess(), file, GetCurrentProcess(), &dup,
                              map[i].generic, FALSE, 0);
        ok(ret, "DuplicateHandle error %ld\n", GetLastError());

        access = get_obj_access(dup);
        ok(access == map[i].mapped, "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);

        CloseHandle(dup);
    }

    CloseHandle(file);
    CloseHandle(pipe);

    SetLastError(0xdeadbeef);
    file = CreateFileA("\\\\.\\pipe\\", FILE_ALL_ACCESS, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0);
    ok(file != INVALID_HANDLE_VALUE || broken(file == INVALID_HANDLE_VALUE) /* before Vista */, "CreateFile error %ld\n", GetLastError());

    if (file != INVALID_HANDLE_VALUE)
    {
        access = get_obj_access(file);
        ok(access == FILE_ALL_ACCESS, "expected FILE_ALL_ACCESS, got %#lx\n", access);

        for (i = 0; i < ARRAY_SIZE(map); i++)
        {
            SetLastError( 0xdeadbeef );
            ret = DuplicateHandle(GetCurrentProcess(), file, GetCurrentProcess(), &dup,
                                  map[i].generic, FALSE, 0);
            ok(ret, "DuplicateHandle error %ld\n", GetLastError());

            access = get_obj_access(dup);
            ok(access == map[i].mapped, "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);
            CloseHandle(dup);
        }
    }

    CloseHandle(file);
}

static void test_file_security(HANDLE token)
{
    DWORD ret, i, access, bytes;
    HANDLE file, dup;
    static const struct
    {
        int generic, mapped;
    } map[] =
    {
        { 0, 0 },
        { GENERIC_READ, FILE_GENERIC_READ },
        { GENERIC_WRITE, FILE_GENERIC_WRITE },
        { GENERIC_EXECUTE, FILE_GENERIC_EXECUTE },
        { GENERIC_ALL, STANDARD_RIGHTS_ALL | FILE_ALL_ACCESS }
    };
    char temp_path[MAX_PATH];
    char file_name[MAX_PATH];
    char buf[16];

    GetTempPathA(MAX_PATH, temp_path);
    GetTempFileNameA(temp_path, "tmp", 0, file_name);

    /* file */
    SetLastError(0xdeadbeef);
    file = CreateFileA(file_name, GENERIC_ALL, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(file != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());

    access = get_obj_access(file);
    ok(access == FILE_ALL_ACCESS, "expected FILE_ALL_ACCESS, got %#lx\n", access);

    for (i = 0; i < ARRAY_SIZE(map); i++)
    {
        SetLastError( 0xdeadbeef );
        ret = DuplicateHandle(GetCurrentProcess(), file, GetCurrentProcess(), &dup,
                              map[i].generic, FALSE, 0);
        ok(ret, "DuplicateHandle error %ld\n", GetLastError());

        access = get_obj_access(dup);
        ok(access == map[i].mapped, "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);

        CloseHandle(dup);
    }

    CloseHandle(file);

    SetLastError(0xdeadbeef);
    file = CreateFileA(file_name, 0, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(file != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());

    access = get_obj_access(file);
    ok(access == (FILE_READ_ATTRIBUTES | SYNCHRONIZE), "expected FILE_READ_ATTRIBUTES | SYNCHRONIZE, got %#lx\n", access);

    bytes = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = ReadFile(file, buf, sizeof(buf), &bytes, NULL);
    ok(!ret, "ReadFile should fail\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "expected ERROR_ACCESS_DENIED, got %ld\n", GetLastError());
    ok(bytes == 0, "expected 0, got %lu\n", bytes);

    CloseHandle(file);

    SetLastError(0xdeadbeef);
    file = CreateFileA(file_name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());

    access = get_obj_access(file);
    ok(access == (FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES), "expected FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES, got %#lx\n", access);

    bytes = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = ReadFile(file, buf, sizeof(buf), &bytes, NULL);
    ok(!ret, "ReadFile should fail\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "expected ERROR_ACCESS_DENIED, got %ld\n", GetLastError());
    ok(bytes == 0, "expected 0, got %lu\n", bytes);

    CloseHandle(file);
    DeleteFileA(file_name);

    /* directory */
    SetLastError(0xdeadbeef);
    file = CreateFileA(temp_path, GENERIC_ALL, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    ok(file != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());

    access = get_obj_access(file);
    ok(access == FILE_ALL_ACCESS, "expected FILE_ALL_ACCESS, got %#lx\n", access);

    for (i = 0; i < ARRAY_SIZE(map); i++)
    {
        SetLastError( 0xdeadbeef );
        ret = DuplicateHandle(GetCurrentProcess(), file, GetCurrentProcess(), &dup,
                              map[i].generic, FALSE, 0);
        ok(ret, "DuplicateHandle error %ld\n", GetLastError());

        access = get_obj_access(dup);
        ok(access == map[i].mapped, "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);

        CloseHandle(dup);
    }

    CloseHandle(file);

    SetLastError(0xdeadbeef);
    file = CreateFileA(temp_path, 0, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    ok(file != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());

    access = get_obj_access(file);
    ok(access == (FILE_READ_ATTRIBUTES | SYNCHRONIZE), "expected FILE_READ_ATTRIBUTES | SYNCHRONIZE, got %#lx\n", access);

    CloseHandle(file);

    SetLastError(0xdeadbeef);
    file = CreateFileA(temp_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    ok(file != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());

    access = get_obj_access(file);
    ok(access == (FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES), "expected FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES, got %#lx\n", access);

    CloseHandle(file);
}

static void test_filemap_security(void)
{
    char temp_path[MAX_PATH];
    char file_name[MAX_PATH];
    DWORD ret, i, access;
    HANDLE file, mapping, dup, created_mapping;
    static const struct
    {
        int generic, mapped;
        BOOL open_only;
    } map[] =
    {
        { 0, 0 },
        { GENERIC_READ, STANDARD_RIGHTS_READ | SECTION_QUERY | SECTION_MAP_READ },
        { GENERIC_WRITE, STANDARD_RIGHTS_WRITE | SECTION_MAP_WRITE },
        { GENERIC_EXECUTE, STANDARD_RIGHTS_EXECUTE | SECTION_MAP_EXECUTE },
        { GENERIC_ALL, STANDARD_RIGHTS_REQUIRED | SECTION_ALL_ACCESS },
        { SECTION_MAP_READ | SECTION_MAP_WRITE, SECTION_MAP_READ | SECTION_MAP_WRITE },
        { SECTION_MAP_WRITE, SECTION_MAP_WRITE },
        { SECTION_MAP_READ | SECTION_QUERY, SECTION_MAP_READ | SECTION_QUERY },
        { SECTION_QUERY, SECTION_MAP_READ, TRUE },
        { SECTION_QUERY | SECTION_MAP_READ, SECTION_QUERY | SECTION_MAP_READ }
    };
    static const struct
    {
        int prot, mapped;
    } prot_map[] =
    {
        { 0, 0 },
        { PAGE_NOACCESS, 0 },
        { PAGE_READONLY, STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ },
        { PAGE_READWRITE, STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_WRITE },
        { PAGE_WRITECOPY, STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ },
        { PAGE_EXECUTE, 0 },
        { PAGE_EXECUTE_READ, STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_EXECUTE },
        { PAGE_EXECUTE_READWRITE, STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_MAP_EXECUTE },
        { PAGE_EXECUTE_WRITECOPY, STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_EXECUTE }
    };

    GetTempPathA(MAX_PATH, temp_path);
    GetTempFileNameA(temp_path, "tmp", 0, file_name);

    SetLastError(0xdeadbeef);
    file = CreateFileA(file_name, GENERIC_READ|GENERIC_WRITE|GENERIC_EXECUTE, 0, NULL, CREATE_ALWAYS, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "CreateFile error %ld\n", GetLastError());
    SetFilePointer(file, 4096, NULL, FILE_BEGIN);
    SetEndOfFile(file);

    for (i = 0; i < ARRAY_SIZE(prot_map); i++)
    {
        if (map[i].open_only) continue;

        SetLastError(0xdeadbeef);
        mapping = CreateFileMappingW(file, NULL, prot_map[i].prot, 0, 4096, NULL);
        if (prot_map[i].mapped)
        {
            ok(mapping != 0, "CreateFileMapping(%04x) error %ld\n", prot_map[i].prot, GetLastError());
        }
        else
        {
            ok(!mapping, "CreateFileMapping(%04x) should fail\n", prot_map[i].prot);
            ok(GetLastError() == ERROR_INVALID_PARAMETER, "expected ERROR_INVALID_PARAMETER, got %ld\n", GetLastError());
            continue;
        }

        access = get_obj_access(mapping);
        ok(access == prot_map[i].mapped, "%ld: expected %#x, got %#lx\n", i, prot_map[i].mapped, access);

        CloseHandle(mapping);
    }

    SetLastError(0xdeadbeef);
    mapping = CreateFileMappingW(file, NULL, PAGE_EXECUTE_READWRITE, 0, 4096, NULL);
    ok(mapping != 0, "CreateFileMapping error %ld\n", GetLastError());

    access = get_obj_access(mapping);
    ok(access == (STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_MAP_EXECUTE),
       "expected STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_MAP_EXECUTE, got %#lx\n", access);

    for (i = 0; i < ARRAY_SIZE(map); i++)
    {
        if (map[i].open_only) continue;

        SetLastError( 0xdeadbeef );
        ret = DuplicateHandle(GetCurrentProcess(), mapping, GetCurrentProcess(), &dup,
                              map[i].generic, FALSE, 0);
        ok(ret, "DuplicateHandle error %ld\n", GetLastError());

        access = get_obj_access(dup);
        ok(access == map[i].mapped, "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);

        CloseHandle(dup);
    }

    CloseHandle(mapping);
    CloseHandle(file);
    DeleteFileA(file_name);

    created_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x1000,
                                         "Wine Test Open Mapping");
    ok(created_mapping != NULL, "CreateFileMapping failed with error %lu\n", GetLastError());

    for (i = 0; i < ARRAY_SIZE(map); i++)
    {
        if (!map[i].generic) continue;

        mapping = OpenFileMappingA(map[i].generic, FALSE, "Wine Test Open Mapping");
        ok(mapping != NULL, "OpenFileMapping failed with error %ld\n", GetLastError());
        access = get_obj_access(mapping);
        ok(access == map[i].mapped, "%ld: unexpected access flags %#lx, expected %#x\n",
           i, access, map[i].mapped);
        CloseHandle(mapping);
    }

    CloseHandle(created_mapping);
}

static void test_thread_security(void)
{
    DWORD ret, i, access;
    HANDLE thread, dup;
    static const struct
    {
        int generic, mapped;
    } map[] =
    {
        { 0, 0 },
        { GENERIC_READ, STANDARD_RIGHTS_READ | THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT },
        { GENERIC_WRITE, STANDARD_RIGHTS_WRITE | THREAD_SET_INFORMATION | THREAD_SET_CONTEXT | THREAD_TERMINATE | THREAD_SUSPEND_RESUME | 0x4 },
        { GENERIC_EXECUTE, STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE },
        { GENERIC_ALL, THREAD_ALL_ACCESS_NT4 }
    };

    SetLastError(0xdeadbeef);
    thread = CreateThread(NULL, 0, (void *)0xdeadbeef, NULL, CREATE_SUSPENDED, &ret);
    ok(thread != 0, "CreateThread error %ld\n", GetLastError());

    access = get_obj_access(thread);
    ok(access == THREAD_ALL_ACCESS_NT4 || access == THREAD_ALL_ACCESS_VISTA, "expected THREAD_ALL_ACCESS, got %#lx\n", access);

    for (i = 0; i < ARRAY_SIZE(map); i++)
    {
        SetLastError( 0xdeadbeef );
        ret = DuplicateHandle(GetCurrentProcess(), thread, GetCurrentProcess(), &dup,
                              map[i].generic, FALSE, 0);
        ok(ret, "DuplicateHandle error %ld\n", GetLastError());

        access = get_obj_access(dup);
        switch (map[i].generic)
        {
        case GENERIC_READ:
        case GENERIC_EXECUTE:
            ok(access == map[i].mapped ||
               access == (map[i].mapped | THREAD_QUERY_LIMITED_INFORMATION) /* Vista+ */ ||
               access == (map[i].mapped | THREAD_QUERY_LIMITED_INFORMATION | THREAD_RESUME) /* win8 */,
               "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);
            break;
        case GENERIC_WRITE:
            ok(access == map[i].mapped ||
               access == (map[i].mapped | THREAD_SET_LIMITED_INFORMATION) /* Vista+ */ ||
               access == (map[i].mapped | THREAD_SET_LIMITED_INFORMATION | THREAD_RESUME) /* win8 */,
               "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);
            break;
        case GENERIC_ALL:
            ok(access == map[i].mapped || access == THREAD_ALL_ACCESS_VISTA,
               "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);
            break;
        default:
            ok(access == map[i].mapped, "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);
            break;
        }

        CloseHandle(dup);
    }

    SetLastError( 0xdeadbeef );
    ret = DuplicateHandle(GetCurrentProcess(), thread, GetCurrentProcess(), &dup,
                          THREAD_QUERY_INFORMATION, FALSE, 0);
    ok(ret, "DuplicateHandle error %ld\n", GetLastError());
    access = get_obj_access(dup);
    ok(access == (THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION) /* Vista+ */ ||
       access == THREAD_QUERY_INFORMATION /* before Vista */,
       "expected THREAD_QUERY_INFORMATION|THREAD_QUERY_LIMITED_INFORMATION, got %#lx\n", access);
    CloseHandle(dup);

    TerminateThread(thread, 0);
    CloseHandle(thread);
}

static void test_process_access(void)
{
    DWORD ret, i, access;
    HANDLE process, dup;
    STARTUPINFOA sti;
    PROCESS_INFORMATION pi;
    char cmdline[] = "winver.exe";
    static const struct
    {
        int generic, mapped;
    } map[] =
    {
        { 0, 0 },
        { GENERIC_READ, STANDARD_RIGHTS_READ | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ },
        { GENERIC_WRITE, STANDARD_RIGHTS_WRITE | PROCESS_SET_QUOTA | PROCESS_SET_INFORMATION | PROCESS_SUSPEND_RESUME |
                         PROCESS_VM_WRITE | PROCESS_DUP_HANDLE | PROCESS_CREATE_PROCESS | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION },
        { GENERIC_EXECUTE, STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE },
        { GENERIC_ALL, PROCESS_ALL_ACCESS_NT4 }
    };

    memset(&sti, 0, sizeof(sti));
    sti.cb = sizeof(sti);
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &sti, &pi);
    ok(ret, "CreateProcess() error %ld\n", GetLastError());

    CloseHandle(pi.hThread);
    process = pi.hProcess;

    access = get_obj_access(process);
    ok(access == PROCESS_ALL_ACCESS_NT4 || access == PROCESS_ALL_ACCESS_VISTA, "expected PROCESS_ALL_ACCESS, got %#lx\n", access);

    for (i = 0; i < ARRAY_SIZE(map); i++)
    {
        SetLastError( 0xdeadbeef );
        ret = DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &dup,
                              map[i].generic, FALSE, 0);
        ok(ret, "DuplicateHandle error %ld\n", GetLastError());

        access = get_obj_access(dup);
        switch (map[i].generic)
        {
        case GENERIC_READ:
            ok(access == map[i].mapped || access == (map[i].mapped | PROCESS_QUERY_LIMITED_INFORMATION) /* Vista+ */,
               "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);
            break;
        case GENERIC_WRITE:
            ok(access == map[i].mapped ||
               access == (map[i].mapped | PROCESS_TERMINATE) /* before Vista */ ||
               access == (map[i].mapped | PROCESS_SET_LIMITED_INFORMATION) /* win8 */ ||
               access == (map[i].mapped | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_LIMITED_INFORMATION) /* Win10 Anniversary Update */,
               "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);
            break;
        case GENERIC_EXECUTE:
            ok(access == map[i].mapped || access == (map[i].mapped | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE) /* Vista+ */,
               "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);
            break;
        case GENERIC_ALL:
            ok(access == map[i].mapped || access == PROCESS_ALL_ACCESS_VISTA,
               "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);
            break;
        default:
            ok(access == map[i].mapped, "%ld: expected %#x, got %#lx\n", i, map[i].mapped, access);
            break;
        }

        CloseHandle(dup);
    }

    SetLastError( 0xdeadbeef );
    ret = DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &dup,
                          PROCESS_QUERY_INFORMATION, FALSE, 0);
    ok(ret, "DuplicateHandle error %ld\n", GetLastError());
    access = get_obj_access(dup);
    ok(access == (PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION) /* Vista+ */ ||
       access == PROCESS_QUERY_INFORMATION /* before Vista */,
       "expected PROCESS_QUERY_INFORMATION|PROCESS_QUERY_LIMITED_INFORMATION, got %#lx\n", access);
    CloseHandle(dup);

    SetLastError( 0xdeadbeef );
    ret = DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &dup,
                          PROCESS_VM_OPERATION, FALSE, 0);
    ok(ret, "DuplicateHandle error %ld\n", GetLastError());
    access = get_obj_access(dup);
    ok(access == PROCESS_VM_OPERATION, "unexpected access right %lx\n", access);
    CloseHandle(dup);

    SetLastError( 0xdeadbeef );
    ret = DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &dup,
                          PROCESS_VM_WRITE, FALSE, 0);
    ok(ret, "DuplicateHandle error %ld\n", GetLastError());
    access = get_obj_access(dup);
    ok(access == PROCESS_VM_WRITE, "unexpected access right %lx\n", access);
    CloseHandle(dup);

    SetLastError( 0xdeadbeef );
    ret = DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &dup,
                          PROCESS_VM_OPERATION | PROCESS_VM_WRITE, FALSE, 0);
    ok(ret, "DuplicateHandle error %ld\n", GetLastError());
    access = get_obj_access(dup);
    ok(access == (PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_QUERY_LIMITED_INFORMATION) ||
       broken(access == (PROCESS_VM_OPERATION | PROCESS_VM_WRITE)) /* Win8 and before */,
       "expected PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_QUERY_LIMITED_INFORMATION, got %#lx\n", access);
    CloseHandle(dup);

    SetLastError( 0xdeadbeef );
    ret = DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &dup,
                          PROCESS_VM_OPERATION | PROCESS_VM_READ, FALSE, 0);
    ok(ret, "DuplicateHandle error %ld\n", GetLastError());
    access = get_obj_access(dup);
    ok(access == (PROCESS_VM_OPERATION | PROCESS_VM_READ), "unexpected access right %lx\n", access);
    CloseHandle(dup);

    TerminateProcess(process, 0);
    CloseHandle(process);
}

static BOOL validate_impersonation_token(HANDLE token, DWORD *token_type)
{
    DWORD ret, needed;
    TOKEN_TYPE type;
    SECURITY_IMPERSONATION_LEVEL sil;

    type = 0xdeadbeef;
    needed = 0;
    SetLastError(0xdeadbeef);
    ret = GetTokenInformation(token, TokenType, &type, sizeof(type), &needed);
    ok(ret, "GetTokenInformation error %ld\n", GetLastError());
    ok(needed == sizeof(type), "GetTokenInformation should return required buffer length\n");
    ok(type == TokenPrimary || type == TokenImpersonation, "expected TokenPrimary or TokenImpersonation, got %d\n", type);

    *token_type = type;
    if (type != TokenImpersonation) return FALSE;

    needed = 0;
    SetLastError(0xdeadbeef);
    ret = GetTokenInformation(token, TokenImpersonationLevel, &sil, sizeof(sil), &needed);
    ok(ret, "GetTokenInformation error %ld\n", GetLastError());
    ok(needed == sizeof(sil), "GetTokenInformation should return required buffer length\n");
    ok(sil == SecurityImpersonation, "expected SecurityImpersonation, got %d\n", sil);

    needed = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = GetTokenInformation(token, TokenDefaultDacl, NULL, 0, &needed);
    ok(!ret, "GetTokenInformation should fail\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());
    ok(needed != 0xdeadbeef, "GetTokenInformation should return required buffer length\n");
    ok(needed > sizeof(TOKEN_DEFAULT_DACL), "GetTokenInformation returned empty default DACL\n");

    needed = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = GetTokenInformation(token, TokenOwner, NULL, 0, &needed);
    ok(!ret, "GetTokenInformation should fail\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());
    ok(needed != 0xdeadbeef, "GetTokenInformation should return required buffer length\n");
    ok(needed > sizeof(TOKEN_OWNER), "GetTokenInformation returned empty token owner\n");

    needed = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = GetTokenInformation(token, TokenPrimaryGroup, NULL, 0, &needed);
    ok(!ret, "GetTokenInformation should fail\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());
    ok(needed != 0xdeadbeef, "GetTokenInformation should return required buffer length\n");
    ok(needed > sizeof(TOKEN_PRIMARY_GROUP), "GetTokenInformation returned empty token primary group\n");

    return TRUE;
}

static void test_kernel_objects_security(void)
{
    HANDLE token, process_token;
    DWORD ret, token_type;

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &process_token);
    ok(ret, "OpenProcessToken error %ld\n", GetLastError());

    ret = validate_impersonation_token(process_token, &token_type);
    ok(token_type == TokenPrimary, "expected TokenPrimary, got %ld\n", token_type);
    ok(!ret, "access token should not be an impersonation token\n");

    ret = DuplicateToken(process_token, SecurityImpersonation, &token);
    ok(ret, "DuplicateToken error %ld\n", GetLastError());

    ret = validate_impersonation_token(token, &token_type);
    ok(ret, "access token should be a valid impersonation token\n");
    ok(token_type == TokenImpersonation, "expected TokenImpersonation, got %ld\n", token_type);

    test_mutex_security(token);
    test_event_security(token);
    test_named_pipe_security(token);
    test_semaphore_security(token);
    test_file_security(token);
    test_filemap_security();
    test_thread_security();
    test_process_access();
    /* FIXME: test other kernel object types */

    CloseHandle(process_token);
    CloseHandle(token);
}

static void test_TokenIntegrityLevel(void)
{
    TOKEN_MANDATORY_LABEL *tml;
#ifdef __REACTOS__
    TOKEN_USER *user;
#endif
    BYTE buffer[64];        /* using max. 28 byte in win7 x64 */
    HANDLE token;
    DWORD size;
    DWORD res;
    static SID medium_level = {SID_REVISION, 1, {SECURITY_MANDATORY_LABEL_AUTHORITY},
                                                    {SECURITY_MANDATORY_HIGH_RID}};
    static SID high_level = {SID_REVISION, 1, {SECURITY_MANDATORY_LABEL_AUTHORITY},
                                                    {SECURITY_MANDATORY_MEDIUM_RID}};
#ifdef __REACTOS__
    static SID system_level = {SID_REVISION, 1, {SECURITY_MANDATORY_LABEL_AUTHORITY},
                                                    {SECURITY_MANDATORY_SYSTEM_RID}};
#endif

    SetLastError(0xdeadbeef);
    res = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    ok(res, "got %ld with %ld (expected TRUE)\n", res, GetLastError());

    SetLastError(0xdeadbeef);
    res = GetTokenInformation(token, TokenIntegrityLevel, buffer, sizeof(buffer), &size);

    /* not supported before Vista */
    if (!res && ((GetLastError() == ERROR_INVALID_PARAMETER) || GetLastError() == ERROR_INVALID_FUNCTION))
    {
        win_skip("TokenIntegrityLevel not supported\n");
        CloseHandle(token);
        return;
    }

    ok(res, "got %lu with %lu (expected TRUE)\n", res, GetLastError());
    if (!res)
    {
        CloseHandle(token);
        return;
    }

    tml = (TOKEN_MANDATORY_LABEL*) buffer;
    ok(tml->Label.Attributes == (SE_GROUP_INTEGRITY | SE_GROUP_INTEGRITY_ENABLED),
        "got 0x%lx (expected 0x%x)\n", tml->Label.Attributes, (SE_GROUP_INTEGRITY | SE_GROUP_INTEGRITY_ENABLED));

#ifdef __REACTOS__
    user = get_alloc_token_user(token);
    if (IsWellKnownSid(user->User.Sid, WinLocalSystemSid))
        ok(EqualSid(tml->Label.Sid, &system_level), "Expected system integrity, got %s\n",
           debugstr_sid(tml->Label.Sid));
    else
        ok(EqualSid(tml->Label.Sid, &medium_level) || EqualSid(tml->Label.Sid, &high_level),
           "got %s (expected %s or %s)\n", debugstr_sid(tml->Label.Sid),
           debugstr_sid(&medium_level), debugstr_sid(&high_level));
    free(user);
#else
    ok(EqualSid(tml->Label.Sid, &medium_level) || EqualSid(tml->Label.Sid, &high_level),
       "got %s (expected %s or %s)\n", debugstr_sid(tml->Label.Sid),
       debugstr_sid(&medium_level), debugstr_sid(&high_level));
#endif

    CloseHandle(token);
}

static void test_default_dacl_owner_group_sid(void)
{
    TOKEN_USER *token_user;
    TOKEN_OWNER *token_owner;
    TOKEN_PRIMARY_GROUP *token_primary_group;
#ifdef __REACTOS__
    TOKEN_DEFAULT_DACL *token_dacl;
#endif
    HANDLE handle, token;
#ifdef __REACTOS__
    BOOL ret, defaulted, present, found, expected_found;
    DWORD size = 0, index;
#else
    BOOL ret, defaulted, present, found;
    DWORD size, index;
#endif
    SECURITY_DESCRIPTOR *sd;
    SECURITY_ATTRIBUTES sa;
    PSID owner, group;
    ACL *dacl;
    ACCESS_ALLOWED_ACE *ace;

    ret = OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token );
    ok(ret, "OpenProcessToken failed with error %ld\n", GetLastError());

    token_user = get_alloc_token_user( token );
    token_owner = get_alloc_token_owner( token );
    token_primary_group = get_alloc_token_primary_group( token );

#ifdef __REACTOS__
    ret = GetTokenInformation(token, TokenDefaultDacl, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "GetTokenInformation(TokenDefaultDacl) failed with error %ld\n", GetLastError());
    token_dacl = malloc(size);
    ret = GetTokenInformation(token, TokenDefaultDacl, token_dacl, size, &size);
    ok(ret, "GetTokenInformation(TokenDefaultDacl) failed with error %ld\n", GetLastError());

#endif
    CloseHandle( token );
#ifdef __REACTOS__
    if (!ret) goto done;
#endif

    sd = malloc( SECURITY_DESCRIPTOR_MIN_LENGTH );
    ret = InitializeSecurityDescriptor( sd, SECURITY_DESCRIPTOR_REVISION );
    ok( ret, "error %lu\n", GetLastError() );

    sa.nLength              = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle       = FALSE;
    handle = CreateEventA( &sa, TRUE, TRUE, "test_event" );
    ok( handle != NULL, "error %lu\n", GetLastError() );

    size = 0;
    ret = GetKernelObjectSecurity( handle, OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION, NULL, 0, &size );
    ok( !ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "error %lu\n", GetLastError() );

    sd = malloc( size );
    ret = GetKernelObjectSecurity( handle, OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION, sd, size, &size );
    ok( ret, "error %lu\n", GetLastError() );

    owner = (void *)0xdeadbeef;
    defaulted = TRUE;
    ret = GetSecurityDescriptorOwner( sd, &owner, &defaulted );
    ok( ret, "error %lu\n", GetLastError() );
    ok( owner != (void *)0xdeadbeef, "owner not set\n" );
    ok( !defaulted, "owner defaulted\n" );
    ok( EqualSid( owner, token_owner->Owner ), "owner shall equal token owner\n" );

    group = (void *)0xdeadbeef;
    defaulted = TRUE;
    ret = GetSecurityDescriptorGroup( sd, &group, &defaulted );
    ok( ret, "error %lu\n", GetLastError() );
    ok( group != (void *)0xdeadbeef, "group not set\n" );
    ok( !defaulted, "group defaulted\n" );
    ok( EqualSid( group, token_primary_group->PrimaryGroup ), "group shall equal token primary group\n" );

    dacl = (void *)0xdeadbeef;
    present = FALSE;
    defaulted = TRUE;
    ret = GetSecurityDescriptorDacl( sd, &present, &dacl, &defaulted );
    ok( ret, "error %lu\n", GetLastError() );
    ok( present, "dacl not present\n" );
    ok( dacl != (void *)0xdeadbeef, "dacl not set\n" );
    ok( !defaulted, "dacl defaulted\n" );

    index = 0;
    found = FALSE;
    while (GetAce( dacl, index++, (void **)&ace ))
    {
        ok( ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE,
            "expected ACCESS_ALLOWED_ACE_TYPE, got %d\n", ace->Header.AceType );
        if (EqualSid( &ace->SidStart, owner )) found = TRUE;
    }
    ok( found, "owner sid not found in dacl\n" );

    if (!EqualSid( token_user->User.Sid, token_owner->Owner ))
    {
        index = 0;
        found = FALSE;
        while (GetAce( dacl, index++, (void **)&ace ))
        {
            ok( ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE,
                "expected ACCESS_ALLOWED_ACE_TYPE, got %d\n", ace->Header.AceType );
            if (EqualSid( &ace->SidStart, token_user->User.Sid )) found = TRUE;
        }
#ifdef __REACTOS__
        index = 0;
        expected_found = FALSE;
        while (token_dacl->DefaultDacl && GetAce(token_dacl->DefaultDacl, index++, (void **)&ace))
        {
            if (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
                EqualSid(&ace->SidStart, token_user->User.Sid))
                expected_found = TRUE;
        }
        ok(found == expected_found, "User ACE presence %d differs from token default DACL %d\n",
           found, expected_found);
#else
        ok( !found, "DACL shall not reference token user if it is different from token owner\n" );
#endif
    }

    free( sa.lpSecurityDescriptor );
    free( sd );
    CloseHandle( handle );

#ifdef __REACTOS__
done:
#endif
    free( token_primary_group );
    free( token_owner );
    free( token_user );
#ifdef __REACTOS__
    free( token_dacl );
#endif
}

static void test_AdjustTokenPrivileges(void)
{
    TOKEN_PRIVILEGES tp;
    HANDLE token;
    DWORD len;
    LUID luid;
    BOOL ret;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token))
        return;

    if (!LookupPrivilegeValueA(NULL, SE_BACKUP_NAME, &luid))
    {
        CloseHandle(token);
        return;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    len = 0xdeadbeef;
    ret = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, &len);
    ok(ret, "got %d\n", ret);
    ok(len == 0xdeadbeef, "got length %ld\n", len);

    /* revert */
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = 0;
    ret = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
    ok(ret, "got %d\n", ret);

    CloseHandle(token);
}

static void test_AddAce(void)
{
    static SID const sidWorld = { SID_REVISION, 1, { SECURITY_WORLD_SID_AUTHORITY} , { SECURITY_WORLD_RID } };

    char acl_buf[1024], ace_buf[256];
    ACCESS_ALLOWED_ACE *ace = (ACCESS_ALLOWED_ACE*)ace_buf;
    PACL acl = (PACL)acl_buf;
    BOOL ret;

    memset(ace, 0, sizeof(ace_buf));
    ace->Header.AceType = ACCESS_ALLOWED_ACE_TYPE;
    ace->Header.AceSize = sizeof(ACCESS_ALLOWED_ACE)-sizeof(DWORD)+sizeof(SID);
    memcpy(&ace->SidStart, &sidWorld, sizeof(sidWorld));

    ret = InitializeAcl(acl, sizeof(acl_buf), ACL_REVISION2);
    ok(ret, "InitializeAcl failed: %ld\n", GetLastError());

    ret = AddAce(acl, ACL_REVISION1, MAXDWORD, ace, ace->Header.AceSize);
    ok(ret, "AddAce failed: %ld\n", GetLastError());
    ret = AddAce(acl, ACL_REVISION2, MAXDWORD, ace, ace->Header.AceSize);
    ok(ret, "AddAce failed: %ld\n", GetLastError());
    ret = AddAce(acl, ACL_REVISION3, MAXDWORD, ace, ace->Header.AceSize);
    ok(ret, "AddAce failed: %ld\n", GetLastError());
    ok(acl->AclRevision == ACL_REVISION3, "acl->AclRevision = %d\n", acl->AclRevision);
    ret = AddAce(acl, ACL_REVISION4, MAXDWORD, ace, ace->Header.AceSize);
    ok(ret, "AddAce failed: %ld\n", GetLastError());
    ok(acl->AclRevision == ACL_REVISION4, "acl->AclRevision = %d\n", acl->AclRevision);
    ret = AddAce(acl, ACL_REVISION1, MAXDWORD, ace, ace->Header.AceSize);
    ok(ret, "AddAce failed: %ld\n", GetLastError());
    ok(acl->AclRevision == ACL_REVISION4, "acl->AclRevision = %d\n", acl->AclRevision);
    ret = AddAce(acl, ACL_REVISION2, MAXDWORD, ace, ace->Header.AceSize);
    ok(ret, "AddAce failed: %ld\n", GetLastError());

    ret = AddAce(acl, MIN_ACL_REVISION-1, MAXDWORD, ace, ace->Header.AceSize);
    ok(ret, "AddAce failed: %ld\n", GetLastError());
    /* next test succeededs but corrupts ACL */
    ret = AddAce(acl, MAX_ACL_REVISION+1, MAXDWORD, ace, ace->Header.AceSize);
    ok(ret, "AddAce failed: %ld\n", GetLastError());
    ok(acl->AclRevision == MAX_ACL_REVISION+1, "acl->AclRevision = %d\n", acl->AclRevision);
    SetLastError(0xdeadbeef);
    ret = AddAce(acl, ACL_REVISION1, MAXDWORD, ace, ace->Header.AceSize);
    ok(!ret, "AddAce succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "GetLastError() = %ld\n", GetLastError());
}

static void test_AddMandatoryAce(void)
{
    static SID low_level = {SID_REVISION, 1, {SECURITY_MANDATORY_LABEL_AUTHORITY},
                            {SECURITY_MANDATORY_LOW_RID}};
    static SID medium_level = {SID_REVISION, 1, {SECURITY_MANDATORY_LABEL_AUTHORITY},
                               {SECURITY_MANDATORY_MEDIUM_RID}};
    static SID_IDENTIFIER_AUTHORITY sia_world = {SECURITY_WORLD_SID_AUTHORITY};
    char buffer_sd[SECURITY_DESCRIPTOR_MIN_LENGTH];
    SECURITY_DESCRIPTOR *sd2, *sd = (SECURITY_DESCRIPTOR *)&buffer_sd;
    BOOL defaulted, present, ret;
    ACL_SIZE_INFORMATION acl_size_info;
    SYSTEM_MANDATORY_LABEL_ACE *ace;
    char buffer_acl[256];
    ACL *acl = (ACL *)&buffer_acl;
    SECURITY_ATTRIBUTES sa;
    DWORD size;
    HANDLE handle;
    SID *everyone;
    ACL *sacl;

    if (!pAddMandatoryAce)
    {
        win_skip("AddMandatoryAce not supported, skipping test\n");
        return;
    }

    ret = InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
    ok(ret, "InitializeSecurityDescriptor failed with error %lu\n", GetLastError());

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;

    handle = CreateEventA(&sa, TRUE, TRUE, "test_event");
    ok(handle != NULL, "CreateEventA failed with error %lu\n", GetLastError());

    ret = GetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Unexpected GetKernelObjectSecurity return value %u, error %lu\n", ret, GetLastError());

    sd2 = malloc(size);
    ret = GetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, sd2, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    sacl = (void *)0xdeadbeef;
    present = TRUE;
    ret = GetSecurityDescriptorSacl(sd2, &present, &sacl, &defaulted);
    ok(ret, "GetSecurityDescriptorSacl failed with error %lu\n", GetLastError());
    ok(!present, "SACL is present\n");
    ok(sacl == (void *)0xdeadbeef, "SACL is set\n");

    free(sd2);
    CloseHandle(handle);

    memset(buffer_acl, 0, sizeof(buffer_acl));
    ret = InitializeAcl(acl, 256, ACL_REVISION);
    ok(ret, "InitializeAcl failed with %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = pAddMandatoryAce(acl, ACL_REVISION, 0, 0x1234, &low_level);
    ok(!ret, "AddMandatoryAce succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER,
       "Expected ERROR_INVALID_PARAMETER got %lu\n", GetLastError());

    ret = pAddMandatoryAce(acl, ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, &low_level);
    ok(ret, "AddMandatoryAce failed with %lu\n", GetLastError());

    ret = GetAce(acl, 0, (void **)&ace);
    ok(ret, "got error %lu\n", GetLastError());
    ok(ace->Header.AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE, "got type %#x\n", ace->Header.AceType);
    ok(!ace->Header.AceFlags, "got flags %#x\n", ace->Header.AceFlags);
    ok(ace->Mask == SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, "got mask %#lx\n", ace->Mask);
    ok(EqualSid(&ace->SidStart, &low_level), "wrong sid\n");

    SetLastError(0xdeadbeef);
    ret = GetAce(acl, 1, (void **)&ace);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError());

    ret = SetSecurityDescriptorSacl(sd, TRUE, acl, FALSE);
    ok(ret, "SetSecurityDescriptorSacl failed with error %lu\n", GetLastError());

    handle = CreateEventA(&sa, TRUE, TRUE, "test_event");
    ok(handle != NULL, "CreateEventA failed with error %lu\n", GetLastError());

    ret = GetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Unexpected GetKernelObjectSecurity return value %u, error %lu\n", ret, GetLastError());

    sd2 = malloc(size);
    ret = GetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, sd2, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    sacl = (void *)0xdeadbeef;
    present = FALSE;
    defaulted = TRUE;
    ret = GetSecurityDescriptorSacl(sd2, &present, &sacl, &defaulted);
    ok(ret, "GetSecurityDescriptorSacl failed with error %lu\n", GetLastError());
    ok(present, "SACL not present\n");
    ok(sacl != (void *)0xdeadbeef, "SACL not set\n");
    ok(!defaulted, "SACL defaulted\n");
    ret = GetAclInformation(sacl, &acl_size_info, sizeof(acl_size_info), AclSizeInformation);
    ok(ret, "GetAclInformation failed with error %lu\n", GetLastError());
    ok(acl_size_info.AceCount == 1, "SACL contains an unexpected ACE count %lu\n", acl_size_info.AceCount);

    ret = GetAce(sacl, 0, (void **)&ace);
    ok(ret, "GetAce failed with error %lu\n", GetLastError());
    ok (ace->Header.AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE, "Unexpected ACE type %#x\n", ace->Header.AceType);
    ok(!ace->Header.AceFlags, "Unexpected ACE flags %#x\n", ace->Header.AceFlags);
    ok(ace->Mask == SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, "Unexpected ACE mask %#lx\n", ace->Mask);
    ok(EqualSid(&ace->SidStart, &low_level), "Expected low integrity level\n");

    free(sd2);

    ret = pAddMandatoryAce(acl, ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP, &medium_level);
    ok(ret, "AddMandatoryAce failed with error %lu\n", GetLastError());

    ret = SetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, sd);
    ok(ret, "SetKernelObjectSecurity failed with error %lu\n", GetLastError());

    ret = GetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Unexpected GetKernelObjectSecurity return value %u, error %lu\n", ret, GetLastError());

    sd2 = malloc(size);
    ret = GetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, sd2, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    sacl = (void *)0xdeadbeef;
    present = FALSE;
    defaulted = TRUE;
    ret = GetSecurityDescriptorSacl(sd2, &present, &sacl, &defaulted);
    ok(ret, "GetSecurityDescriptorSacl failed with error %lu\n", GetLastError());
    ok(present, "SACL not present\n");
    ok(sacl != (void *)0xdeadbeef, "SACL not set\n");
    ok(sacl->AceCount == 2, "Expected 2 ACEs, got %d\n", sacl->AceCount);
    ok(!defaulted, "SACL defaulted\n");

    ret = GetAce(acl, 0, (void **)&ace);
    ok(ret, "got error %lu\n", GetLastError());
    ok(ace->Header.AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE, "got type %#x\n", ace->Header.AceType);
    ok(!ace->Header.AceFlags, "got flags %#x\n", ace->Header.AceFlags);
    ok(ace->Mask == SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, "got mask %#lx\n", ace->Mask);
    ok(EqualSid(&ace->SidStart, &low_level), "wrong sid\n");

    ret = GetAce(acl, 1, (void **)&ace);
    ok(ret, "got error %lu\n", GetLastError());
    ok(ace->Header.AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE, "got type %#x\n", ace->Header.AceType);
    ok(!ace->Header.AceFlags, "got flags %#x\n", ace->Header.AceFlags);
    ok(ace->Mask == SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP, "got mask %#lx\n", ace->Mask);
    ok(EqualSid(&ace->SidStart, &medium_level), "wrong sid\n");

    SetLastError(0xdeadbeef);
    ret = GetAce(acl, 2, (void **)&ace);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError());

    free(sd2);

    ret = SetSecurityDescriptorSacl(sd, FALSE, NULL, FALSE);
    ok(ret, "SetSecurityDescriptorSacl failed with error %lu\n", GetLastError());

    ret = SetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, sd);
    ok(ret, "SetKernelObjectSecurity failed with error %lu\n", GetLastError());

    ret = GetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Unexpected GetKernelObjectSecurity return value %d, error %lu\n", ret, GetLastError());

    sd2 = malloc(size);
    ret = GetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, sd2, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    sacl = (void *)0xdeadbeef;
    present = FALSE;
    defaulted = TRUE;
    ret = GetSecurityDescriptorSacl(sd2, &present, &sacl, &defaulted);
    ok(ret, "GetSecurityDescriptorSacl failed with error %lu\n", GetLastError());
    ok(present, "SACL not present\n");
    ok(sacl && sacl != (void *)0xdeadbeef, "SACL not set\n");
    ok(!defaulted, "SACL defaulted\n");
    ok(!sacl->AceCount, "SACL contains an unexpected ACE count %u\n", sacl->AceCount);

    free(sd2);

    ret = InitializeAcl(acl, 256, ACL_REVISION);
    ok(ret, "InitializeAcl failed with error %lu\n", GetLastError());

    ret = pAddMandatoryAce(acl, ACL_REVISION3, 0, SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP, &medium_level);
    ok(ret, "AddMandatoryAce failed with error %lu\n", GetLastError());

    ret = SetSecurityDescriptorSacl(sd, TRUE, acl, FALSE);
    ok(ret, "SetSecurityDescriptorSacl failed with error %lu\n", GetLastError());

    ret = SetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, sd);
    ok(ret, "SetKernelObjectSecurity failed with error %lu\n", GetLastError());

    ret = GetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Unexpected GetKernelObjectSecurity return value %d, error %lu\n", ret, GetLastError());

    sd2 = malloc(size);
    ret = GetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, sd2, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    sacl = (void *)0xdeadbeef;
    present = FALSE;
    defaulted = TRUE;
    ret = GetSecurityDescriptorSacl(sd2, &present, &sacl, &defaulted);
    ok(ret, "GetSecurityDescriptorSacl failed with error %lu\n", GetLastError());
    ok(present, "SACL not present\n");
    ok(sacl != (void *)0xdeadbeef, "SACL not set\n");
    ok(sacl->AclRevision == ACL_REVISION3, "Expected revision 3, got %d\n", sacl->AclRevision);
    ok(!defaulted, "SACL defaulted\n");

    free(sd2);

    ret = InitializeAcl(acl, 256, ACL_REVISION);
    ok(ret, "InitializeAcl failed with error %lu\n", GetLastError());

    ret = AllocateAndInitializeSid(&sia_world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, (void **)&everyone);
    ok(ret, "AllocateAndInitializeSid failed with error %lu\n", GetLastError());

    ret = AddAccessAllowedAce(acl, ACL_REVISION, KEY_READ, everyone);
    ok(ret, "AddAccessAllowedAce failed with error %lu\n", GetLastError());

    ret = SetSecurityDescriptorSacl(sd, TRUE, acl, FALSE);
    ok(ret, "SetSecurityDescriptorSacl failed with error %lu\n", GetLastError());

    ret = SetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, sd);
    ok(ret, "SetKernelObjectSecurity failed with error %lu\n", GetLastError());

    ret = GetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Unexpected GetKernelObjectSecurity return value %d, error %lu\n", ret, GetLastError());

    sd2 = malloc(size);
    ret = GetKernelObjectSecurity(handle, LABEL_SECURITY_INFORMATION, sd2, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    sacl = (void *)0xdeadbeef;
    present = FALSE;
    defaulted = TRUE;
    ret = GetSecurityDescriptorSacl(sd2, &present, &sacl, &defaulted);
    ok(ret, "GetSecurityDescriptorSacl failed with error %lu\n", GetLastError());
    ok(present, "SACL not present\n");
    ok(sacl && sacl != (void *)0xdeadbeef, "SACL not set\n");
    ok(!defaulted, "SACL defaulted\n");
    ok(!sacl->AceCount, "SACL contains an unexpected ACE count %u\n", sacl->AceCount);

    FreeSid(everyone);
    free(sd2);
    CloseHandle(handle);
}

static void test_system_security_access(void)
{
    static const WCHAR testkeyW[] = L"SOFTWARE\\Wine\\SACLtest";
    LONG res;
    HKEY hkey;
    PSECURITY_DESCRIPTOR sd;
    ACL *sacl;
    DWORD err, len = 128;
    TOKEN_PRIVILEGES priv, *priv_prev;
    HANDLE token;
    LUID luid;
    BOOL ret;

    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &token )) return;
    if (!LookupPrivilegeValueA( NULL, SE_SECURITY_NAME, &luid ))
    {
        CloseHandle( token );
        return;
    }

    /* ACCESS_SYSTEM_SECURITY requires special privilege */
    res = RegCreateKeyExW( HKEY_LOCAL_MACHINE, testkeyW, 0, NULL, 0, KEY_READ|ACCESS_SYSTEM_SECURITY, NULL, &hkey, NULL );
    if (res == ERROR_ACCESS_DENIED)
    {
        skip( "unprivileged user\n" );
        CloseHandle( token );
        return;
    }
    todo_wine ok( res == ERROR_PRIVILEGE_NOT_HELD, "got %ld\n", res );

    priv.PrivilegeCount = 1;
    priv.Privileges[0].Luid = luid;
    priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    priv_prev = malloc( len );
    ret = AdjustTokenPrivileges( token, FALSE, &priv, len, priv_prev, &len );
    ok( ret, "got %lu\n", GetLastError());

    res = RegCreateKeyExW( HKEY_LOCAL_MACHINE, testkeyW, 0, NULL, 0, KEY_READ|ACCESS_SYSTEM_SECURITY, NULL, &hkey, NULL );
    if (res == ERROR_PRIVILEGE_NOT_HELD)
    {
        win_skip( "privilege not held\n" );
        free( priv_prev );
        CloseHandle( token );
        return;
    }
    ok( !res, "got %ld\n", res );

    /* restore privileges */
    ret = AdjustTokenPrivileges( token, FALSE, priv_prev, 0, NULL, NULL );
    ok( ret, "got %lu\n", GetLastError() );
    free( priv_prev );

    /* privilege is checked on access */
    err = GetSecurityInfo( hkey, SE_REGISTRY_KEY, SACL_SECURITY_INFORMATION, NULL, NULL, NULL, &sacl, &sd );
    todo_wine ok( err == ERROR_PRIVILEGE_NOT_HELD || err == ERROR_ACCESS_DENIED, "got %lu\n", err );
    if (err == ERROR_SUCCESS)
        LocalFree( sd );

    priv.PrivilegeCount = 1;
    priv.Privileges[0].Luid = luid;
    priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    priv_prev = malloc( len );
    ret = AdjustTokenPrivileges( token, FALSE, &priv, len, priv_prev, &len );
    ok( ret, "got %lu\n", GetLastError());

    err = GetSecurityInfo( hkey, SE_REGISTRY_KEY, SACL_SECURITY_INFORMATION, NULL, NULL, NULL, &sacl, &sd );
    ok( err == ERROR_SUCCESS, "got %lu\n", err );
    RegCloseKey( hkey );
    LocalFree( sd );

    /* handle created without ACCESS_SYSTEM_SECURITY, privilege held */
    res = RegCreateKeyExW( HKEY_LOCAL_MACHINE, testkeyW, 0, NULL, 0, KEY_READ, NULL, &hkey, NULL );
    ok( res == ERROR_SUCCESS, "got %ld\n", res );

    sd = NULL;
    err = GetSecurityInfo( hkey, SE_REGISTRY_KEY, SACL_SECURITY_INFORMATION, NULL, NULL, NULL, &sacl, &sd );
    todo_wine ok( err == ERROR_SUCCESS, "got %lu\n", err );
    RegCloseKey( hkey );
    LocalFree( sd );

    /* restore privileges */
    ret = AdjustTokenPrivileges( token, FALSE, priv_prev, 0, NULL, NULL );
    ok( ret, "got %lu\n", GetLastError() );
    free( priv_prev );

    /* handle created without ACCESS_SYSTEM_SECURITY, privilege not held */
    res = RegCreateKeyExW( HKEY_LOCAL_MACHINE, testkeyW, 0, NULL, 0, KEY_READ, NULL, &hkey, NULL );
    ok( res == ERROR_SUCCESS, "got %ld\n", res );

    err = GetSecurityInfo( hkey, SE_REGISTRY_KEY, SACL_SECURITY_INFORMATION, NULL, NULL, NULL, &sacl, &sd );
    ok( err == ERROR_PRIVILEGE_NOT_HELD || err == ERROR_ACCESS_DENIED, "got %lu\n", err );
    RegCloseKey( hkey );

    res = RegDeleteKeyW( HKEY_LOCAL_MACHINE, testkeyW );
    ok( !res, "got %ld\n", res );
    CloseHandle( token );
}

static void test_GetWindowsAccountDomainSid(void)
{
#ifdef __REACTOS__
    char buffer1[SECURITY_MAX_SID_SIZE], buffer2[SECURITY_MAX_SID_SIZE];
#else
    char *user, buffer1[SECURITY_MAX_SID_SIZE], buffer2[SECURITY_MAX_SID_SIZE];
#endif
    SID_IDENTIFIER_AUTHORITY domain_ident = { SECURITY_NT_AUTHORITY };
    PSID domain_sid = (PSID *)&buffer1;
    PSID domain_sid2 = (PSID *)&buffer2;
    DWORD sid_size;
    PSID user_sid;
#ifdef __REACTOS__
    BOOL bret;
#else
    HANDLE token;
    BOOL bret = TRUE;
#endif
    int i;

#ifdef __REACTOS__
    bret = ConvertStringSidToSidA("S-1-5-21-1-2-3-4", &user_sid);
    ok(bret, "ConvertStringSidToSidA failed with error %ld\n", GetLastError());
    if (!bret) return;
#else
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_READ, TRUE, &token))
    {
        if (GetLastError() != ERROR_NO_TOKEN) bret = FALSE;
        else if (!OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &token)) bret = FALSE;
    }
    if (!bret)
    {
        win_skip("Failed to get current user token\n");
        return;
    }

    bret = GetTokenInformation(token, TokenUser, NULL, 0, &sid_size);
    ok(!bret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "GetTokenInformation(TokenUser) failed with error %ld\n", GetLastError());
    user = malloc(sid_size);
    bret = GetTokenInformation(token, TokenUser, user, sid_size, &sid_size);
    ok(bret, "GetTokenInformation(TokenUser) failed with error %ld\n", GetLastError());
    CloseHandle(token);
    user_sid = ((TOKEN_USER *)user)->User.Sid;
#endif

    SetLastError(0xdeadbeef);
    bret = GetWindowsAccountDomainSid(0, 0, 0);
    ok(!bret, "GetWindowsAccountDomainSid succeeded\n");
    ok(GetLastError() == ERROR_INVALID_SID, "expected ERROR_INVALID_SID, got %ld\n", GetLastError());

    SetLastError(0xdeadbeef);
    bret = GetWindowsAccountDomainSid(user_sid, 0, 0);
    ok(!bret, "GetWindowsAccountDomainSid succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "expected ERROR_INVALID_PARAMETER, got %ld\n", GetLastError());

    sid_size = SECURITY_MAX_SID_SIZE;
    SetLastError(0xdeadbeef);
    bret = GetWindowsAccountDomainSid(user_sid, 0, &sid_size);
    ok(!bret, "GetWindowsAccountDomainSid succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "expected ERROR_INVALID_PARAMETER, got %ld\n", GetLastError());
    ok(sid_size == GetSidLengthRequired(4), "expected size %ld, got %ld\n", GetSidLengthRequired(4), sid_size);

    SetLastError(0xdeadbeef);
    bret = GetWindowsAccountDomainSid(user_sid, domain_sid, 0);
    ok(!bret, "GetWindowsAccountDomainSid succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "expected ERROR_INVALID_PARAMETER, got %ld\n", GetLastError());

    sid_size = 1;
    SetLastError(0xdeadbeef);
    bret = GetWindowsAccountDomainSid(user_sid, domain_sid, &sid_size);
    ok(!bret, "GetWindowsAccountDomainSid succeeded\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "expected ERROR_INSUFFICIENT_BUFFER, got %ld\n", GetLastError());
    ok(sid_size == GetSidLengthRequired(4), "expected size %ld, got %ld\n", GetSidLengthRequired(4), sid_size);

    sid_size = SECURITY_MAX_SID_SIZE;
    bret = GetWindowsAccountDomainSid(user_sid, domain_sid, &sid_size);
    ok(bret, "GetWindowsAccountDomainSid failed with error %ld\n", GetLastError());
    ok(sid_size == GetSidLengthRequired(4), "expected size %ld, got %ld\n", GetSidLengthRequired(4), sid_size);
    InitializeSid(domain_sid2, &domain_ident, 4);
    for (i = 0; i < 4; i++)
        *GetSidSubAuthority(domain_sid2, i) = *GetSidSubAuthority(user_sid, i);
    ok(EqualSid(domain_sid, domain_sid2), "unexpected domain sid %s != %s\n",
       debugstr_sid(domain_sid), debugstr_sid(domain_sid2));

#ifdef __REACTOS__
    LocalFree(user_sid);
#else
    free(user);
#endif
}

static void test_GetSidIdentifierAuthority(void)
{
    char buffer[SECURITY_MAX_SID_SIZE];
    PSID authority_sid = (PSID *)buffer;
    PSID_IDENTIFIER_AUTHORITY id;
    BOOL ret;

    memset(buffer, 0xcc, sizeof(buffer));
    ret = IsValidSid(authority_sid);
    ok(!ret, "expected FALSE, got %u\n", ret);

    SetLastError(0xdeadbeef);
    id = GetSidIdentifierAuthority(authority_sid);
    ok(id != NULL, "got NULL pointer as identifier authority\n");
    ok(GetLastError() == ERROR_SUCCESS, "expected ERROR_SUCCESS, got %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    id = GetSidIdentifierAuthority(NULL);
    ok(id != NULL, "got NULL pointer as identifier authority\n");
    ok(GetLastError() == ERROR_SUCCESS, "expected ERROR_SUCCESS, got %lu\n", GetLastError());
}

#ifdef __REACTOS__
static void check_pseudo_token(HANDLE pseudo, HANDLE actual, NTSTATUS expected, unsigned int context)
#else
static void test_pseudo_tokens(void)
#endif
{
#ifdef __REACTOS__
    NTSTATUS (WINAPI *query_token)(HANDLE, TOKEN_INFORMATION_CLASS, void *, ULONG, ULONG *);
    TOKEN_STATISTICS expected_stats, stats;
    TOKEN_SOURCE expected_source, source;
    HANDLE duplicate = NULL;
    DWORD retlen, error;
    NTSTATUS status;
#else
    TOKEN_STATISTICS statistics1, statistics2;
    HANDLE token;
    DWORD retlen;
#endif
    BOOL ret;

#ifdef __REACTOS__
    query_token = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationToken");
    ok(query_token != NULL, "NtQueryInformationToken is missing\n");
    if (!query_token) return;
#else
    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    ok(ret, "OpenProcessToken failed with error %lu\n", GetLastError());
    memset(&statistics1, 0x11, sizeof(statistics1));
    ret = GetTokenInformation(token, TokenStatistics, &statistics1, sizeof(statistics1), &retlen);
    ok(ret, "GetTokenInformation failed with %lu\n", GetLastError());
    CloseHandle(token);
#endif

#ifdef __REACTOS__
    if (actual)
    {
        ret = GetTokenInformation(actual, TokenStatistics, &expected_stats, sizeof(expected_stats), &retlen);
        ok(ret, "Context %u: reference statistics failed with %lu\n", context, GetLastError());
        if (!ret) return;
        ret = GetTokenInformation(actual, TokenSource, &expected_source, sizeof(expected_source), &retlen);
        ok(ret, "Context %u: reference source failed with %lu\n", context, GetLastError());
        if (!ret) return;
    }
#else
    /* test GetCurrentProcessToken() */
    SetLastError(0xdeadbeef);
    memset(&statistics2, 0x22, sizeof(statistics2));
    ret = GetTokenInformation(GetCurrentProcessToken(), TokenStatistics,
                              &statistics2, sizeof(statistics2), &retlen);
    ok(ret || broken(GetLastError() == ERROR_INVALID_HANDLE),
       "GetTokenInformation failed with %lu\n", GetLastError());
    if (ret)
        ok(!memcmp(&statistics1, &statistics2, sizeof(statistics1)), "Token statistics do not match\n");
    else
        win_skip("CurrentProcessToken not supported, skipping test\n");
#endif

#ifdef __REACTOS__
    memset(&stats, 0xcc, sizeof(stats));
    status = query_token(pseudo, TokenStatistics, &stats, sizeof(stats), &retlen);
    ok(status == expected, "Context %u handle %p: native query returned %#lx, expected %#lx\n",
       context, pseudo, status, expected);
    if (status == STATUS_SUCCESS && actual)
    {
        ok(retlen == sizeof(stats), "Context %u handle %p: statistics length %lu\n", context, pseudo, retlen);
        ok(!memcmp(&stats.TokenId, &expected_stats.TokenId, sizeof(LUID)),
           "Context %u handle %p: token identity differs\n", context, pseudo);
        ok(!memcmp(&stats.AuthenticationId, &expected_stats.AuthenticationId, sizeof(LUID)),
           "Context %u handle %p: authentication identity differs\n", context, pseudo);
        ok(stats.TokenType == expected_stats.TokenType, "Context %u handle %p: token type %u, expected %u\n",
           context, pseudo, stats.TokenType, expected_stats.TokenType);
        if (stats.TokenType == TokenImpersonation)
            ok(stats.ImpersonationLevel == expected_stats.ImpersonationLevel,
               "Context %u handle %p: impersonation level %u, expected %u\n",
               context, pseudo, stats.ImpersonationLevel, expected_stats.ImpersonationLevel);
    }
#else
    /* test GetCurrentThreadEffectiveToken() */
    SetLastError(0xdeadbeef);
    memset(&statistics2, 0x22, sizeof(statistics2));
    ret = GetTokenInformation(GetCurrentThreadEffectiveToken(), TokenStatistics,
                              &statistics2, sizeof(statistics2), &retlen);
    ok(ret || broken(GetLastError() == ERROR_INVALID_HANDLE),
       "GetTokenInformation failed with %lu\n", GetLastError());
    if (ret)
        ok(!memcmp(&statistics1, &statistics2, sizeof(statistics1)), "Token statistics do not match\n");
    else
        win_skip("CurrentThreadEffectiveToken not supported, skipping test\n");
#endif

    SetLastError(0xdeadbeef);
#ifdef __REACTOS__
    ret = GetTokenInformation(pseudo, TokenStatistics, &stats, sizeof(stats), &retlen);
    error = GetLastError();
    ok(ret == (expected == STATUS_SUCCESS), "Context %u handle %p: Win32 query returned %d, error %lu\n",
       context, pseudo, ret, error);
    if (expected != STATUS_SUCCESS)
        ok(error == RtlNtStatusToDosError(expected), "Context %u handle %p: query error %lu\n",
           context, pseudo, error);
    if (ret && actual)
        ok(!memcmp(&stats.TokenId, &expected_stats.TokenId, sizeof(LUID)),
           "Context %u handle %p: Win32 token identity differs\n", context, pseudo);

    memset(&source, 0xcc, sizeof(source));
    status = query_token(pseudo, TokenSource, &source, sizeof(source), &retlen);
    ok(status == expected, "Context %u handle %p: source query returned %#lx, expected %#lx\n",
       context, pseudo, status, expected);
    if (status == STATUS_SUCCESS && actual)
    {
        ok(retlen == sizeof(source), "Context %u handle %p: source length %lu\n", context, pseudo, retlen);
        ok(!memcmp(&source, &expected_source, sizeof(source)),
           "Context %u handle %p: token source differs\n", context, pseudo);
    }
#else
    ret = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token);
    ok(!ret, "OpenThreadToken should have failed\n");
    ok(GetLastError() == ERROR_NO_TOKEN, "Expected ERROR_NO_TOKEN, got %lu\n", GetLastError());
#endif

#ifndef __REACTOS__
    /* test GetCurrentThreadToken() */
#endif
    SetLastError(0xdeadbeef);
#ifdef __REACTOS__
    ret = DuplicateTokenEx(pseudo, TOKEN_QUERY, NULL, SecurityImpersonation, TokenImpersonation, &duplicate);
    error = GetLastError();
    ok(!ret, "Context %u handle %p: pseudo-token duplication succeeded\n", context, pseudo);
    ok(!ret && error == ERROR_INVALID_HANDLE, "Context %u handle %p: DuplicateTokenEx error %lu\n",
       context, pseudo, error);
    if (ret) CloseHandle(duplicate);
    duplicate = NULL;
    SetLastError(0xdeadbeef);
    ret = DuplicateHandle(GetCurrentProcess(), pseudo, GetCurrentProcess(), &duplicate,
                          0, FALSE, DUPLICATE_SAME_ACCESS);
    error = GetLastError();
    ok(!ret, "Context %u handle %p: pseudo-handle duplication succeeded\n", context, pseudo);
    ok(!ret && error == ERROR_INVALID_HANDLE, "Context %u handle %p: DuplicateHandle error %lu\n",
       context, pseudo, error);
    if (ret) CloseHandle(duplicate);
}

static void test_pseudo_tokens(void)
{
    HANDLE process_token = NULL, thread_token = NULL, saved_token = NULL, limited = NULL;
    TOKEN_STATISTICS statistics;
    TOKEN_SOURCE source;
    SECURITY_IMPERSONATION_LEVEL level;
    BOOL ret;
    DWORD error, retlen;

    ret = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_IMPERSONATE, TRUE, &saved_token);
    error = GetLastError();
    ok(ret || error == ERROR_NO_TOKEN, "OpenThreadToken failed with %lu\n", error);
    if (!ret && error != ERROR_NO_TOKEN) return;
    ret = RevertToSelf();
    ok(ret, "RevertToSelf failed with %lu\n", GetLastError());
    if (!ret) goto done;
    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_QUERY_SOURCE | TOKEN_DUPLICATE,
                           &process_token);
    ok(ret, "OpenProcessToken failed with %lu\n", GetLastError());
    if (!ret) goto done;

    check_pseudo_token(GetCurrentProcessToken(), process_token, STATUS_SUCCESS, 0);
    check_pseudo_token(GetCurrentThreadToken(), NULL, STATUS_NO_TOKEN, 0);
    check_pseudo_token(GetCurrentThreadEffectiveToken(), process_token, STATUS_SUCCESS, 0);
    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &limited);
    ok(ret, "Query-only token open failed with %lu\n", GetLastError());
    if (ret)
    {
        ret = GetTokenInformation(limited, TokenStatistics, &statistics, sizeof(statistics), &retlen);
        ok(ret, "Query-only statistics failed with %lu\n", GetLastError());
        SetLastError(0xdeadbeef);
        ret = GetTokenInformation(limited, TokenSource, &source, sizeof(source), &retlen);
        error = GetLastError();
        ok(!ret && error == ERROR_ACCESS_DENIED, "Query-only source returned %d, error %lu\n", ret, error);
        CloseHandle(limited);
        limited = NULL;
    }
    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY_SOURCE, &limited);
    ok(ret, "Source-only token open failed with %lu\n", GetLastError());
    if (ret)
    {
        ret = GetTokenInformation(limited, TokenSource, &source, sizeof(source), &retlen);
        ok(ret, "Source-only query failed with %lu\n", GetLastError());
        SetLastError(0xdeadbeef);
        ret = GetTokenInformation(limited, TokenStatistics, &statistics, sizeof(statistics), &retlen);
        error = GetLastError();
        ok(!ret && error == ERROR_ACCESS_DENIED, "Source-only statistics returned %d, error %lu\n", ret, error);
        CloseHandle(limited);
        limited = NULL;
    }
    limited = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(!!limited, "Event creation failed with %lu\n", GetLastError());
    if (limited)
    {
        SetLastError(0xdeadbeef);
        ret = GetTokenInformation(limited, TokenStatistics, &statistics, sizeof(statistics), &retlen);
        error = GetLastError();
        ok(!ret && error == ERROR_INVALID_HANDLE, "Event token query returned %d, error %lu\n", ret, error);
        CloseHandle(limited);
        limited = NULL;
    }
    for (level = SecurityAnonymous; level <= SecurityDelegation; ++level)
    {
        ret = DuplicateTokenEx(process_token, TOKEN_QUERY | TOKEN_QUERY_SOURCE | TOKEN_IMPERSONATE,
                                NULL, level, TokenImpersonation, &thread_token);
        ok(ret, "DuplicateTokenEx level %u failed with %lu\n", level, GetLastError());
        if (!ret) continue;
        ret = SetThreadToken(NULL, thread_token);
        ok(ret, "SetThreadToken level %u failed with %lu\n", level, GetLastError());
        if (ret)
        {
            check_pseudo_token(GetCurrentProcessToken(), process_token, STATUS_SUCCESS, level + 1);
            check_pseudo_token(GetCurrentThreadToken(), thread_token,
                               level == SecurityAnonymous ? STATUS_CANT_OPEN_ANONYMOUS : STATUS_SUCCESS, level + 1);
            check_pseudo_token(GetCurrentThreadEffectiveToken(), thread_token,
                               level == SecurityAnonymous ? STATUS_CANT_OPEN_ANONYMOUS : STATUS_SUCCESS, level + 1);
            ret = RevertToSelf();
            ok(ret, "RevertToSelf level %u failed with %lu\n", level, GetLastError());
        }
        CloseHandle(thread_token);
        thread_token = NULL;
        if (!ret) break;
    }

done:
    ret = SetThreadToken(NULL, saved_token);
    ok(ret, "Restoring thread token failed with %lu\n", GetLastError());
    if (saved_token) CloseHandle(saved_token);
    if (process_token) CloseHandle(process_token);
#else
    ret = GetTokenInformation(GetCurrentThreadToken(), TokenStatistics,
                              &statistics2, sizeof(statistics2), &retlen);
    todo_wine ok(GetLastError() == ERROR_NO_TOKEN || broken(GetLastError() == ERROR_INVALID_HANDLE),
                 "Expected ERROR_NO_TOKEN, got %lu\n", GetLastError());
#endif
}

static void test_maximum_allowed(void)
{
    HANDLE (WINAPI *pCreateEventExA)(SECURITY_ATTRIBUTES *, LPCSTR, DWORD, DWORD);
    char buffer_sd[SECURITY_DESCRIPTOR_MIN_LENGTH], buffer_acl[256];
    SECURITY_DESCRIPTOR *sd = (SECURITY_DESCRIPTOR *)&buffer_sd;
    SECURITY_ATTRIBUTES sa;
    ACL *acl = (ACL *)&buffer_acl;
    HMODULE hkernel32 = GetModuleHandleA("kernel32.dll");
    ACCESS_MASK mask;
    HANDLE handle;
    BOOL ret;

    pCreateEventExA = (void *)GetProcAddress(hkernel32, "CreateEventExA");
    if (!pCreateEventExA)
    {
        win_skip("CreateEventExA is not available\n");
        return;
    }

    ret = InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
    ok(ret, "InitializeSecurityDescriptor failed with %lu\n", GetLastError());
    memset(buffer_acl, 0, sizeof(buffer_acl));
    ret = InitializeAcl(acl, 256, ACL_REVISION);
    ok(ret, "InitializeAcl failed with %lu\n", GetLastError());
    ret = SetSecurityDescriptorDacl(sd, TRUE, acl, FALSE);
    ok(ret, "SetSecurityDescriptorDacl failed with %lu\n", GetLastError());

    sa.nLength              = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle       = FALSE;

    handle = pCreateEventExA(&sa, NULL, 0, MAXIMUM_ALLOWED | 0x4);
    ok(handle != NULL, "CreateEventExA failed with error %lu\n", GetLastError());
    mask = get_obj_access(handle);
    ok(mask == EVENT_ALL_ACCESS, "Expected %x, got %lx\n", EVENT_ALL_ACCESS, mask);
    CloseHandle(handle);
}

#ifdef __REACTOS__
static void check_token_label(HANDLE token, DWORD *level, BOOL sacl_inherited, BOOL system_token)
#else
static void check_token_label(HANDLE token, DWORD *level, BOOL sacl_inherited)
#endif
{
    static SID medium_sid = {SID_REVISION, 1, {SECURITY_MANDATORY_LABEL_AUTHORITY},
                             {SECURITY_MANDATORY_MEDIUM_RID}};
    static SID high_sid = {SID_REVISION, 1, {SECURITY_MANDATORY_LABEL_AUTHORITY},
                           {SECURITY_MANDATORY_HIGH_RID}};
#ifdef __REACTOS__
    static SID system_sid = {SID_REVISION, 1, {SECURITY_MANDATORY_LABEL_AUTHORITY},
                             {SECURITY_MANDATORY_SYSTEM_RID}};
#endif
    SECURITY_DESCRIPTOR_CONTROL control;
    SYSTEM_MANDATORY_LABEL_ACE *ace;
    BOOL ret, present, defaulted;
    SECURITY_DESCRIPTOR *sd;
    ACL *sacl = NULL, *dacl;
    DWORD size, revision;
    char *str;
    SID *sid;

    ret = GetKernelObjectSecurity(token, LABEL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Unexpected GetKernelObjectSecurity return value %d, error %lu\n", ret, GetLastError());

    sd = malloc(size);
    ret = GetKernelObjectSecurity(token, LABEL_SECURITY_INFORMATION, sd, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    ret = GetSecurityDescriptorControl(sd, &control, &revision);
    ok(ret, "GetSecurityDescriptorControl failed with error %lu\n", GetLastError());
    if (sacl_inherited)
        todo_wine ok(control == (SE_SELF_RELATIVE | SE_SACL_AUTO_INHERITED | SE_SACL_PRESENT),
                     "Unexpected security descriptor control %#x\n", control);
    else
        todo_wine ok(control == (SE_SELF_RELATIVE | SE_SACL_PRESENT),
                     "Unexpected security descriptor control %#x\n", control);
    ok(revision == 1, "Unexpected security descriptor revision %lu\n", revision);

    sid = (void *)0xdeadbeef;
    defaulted = TRUE;
    ret = GetSecurityDescriptorOwner(sd, (void **)&sid, &defaulted);
    ok(ret, "GetSecurityDescriptorOwner failed with error %lu\n", GetLastError());
    ok(!sid, "Owner present\n");
    ok(!defaulted, "Owner defaulted\n");

    sid = (void *)0xdeadbeef;
    defaulted = TRUE;
    ret = GetSecurityDescriptorGroup(sd, (void **)&sid, &defaulted);
    ok(ret, "GetSecurityDescriptorGroup failed with error %lu\n", GetLastError());
    ok(!sid, "Group present\n");
    ok(!defaulted, "Group defaulted\n");

    ret = GetSecurityDescriptorSacl(sd, &present, &sacl, &defaulted);
    ok(ret, "GetSecurityDescriptorSacl failed with error %lu\n", GetLastError());
    ok(present, "No SACL in the security descriptor\n");
    ok(!!sacl, "NULL SACL in the security descriptor\n");
    ok(!defaulted, "SACL defaulted\n");
    ok(sacl->AceCount == 1, "SACL contains an unexpected ACE count %u\n", sacl->AceCount);

    ret = GetAce(sacl, 0, (void **)&ace);
    ok(ret, "GetAce failed with error %lu\n", GetLastError());

    ok(ace->Header.AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE,
       "Unexpected ACE type %#x\n", ace->Header.AceType);
    ok(!ace->Header.AceFlags, "Unexpected ACE flags %#x\n", ace->Header.AceFlags);
    ok(ace->Header.AceSize, "Unexpected ACE size %u\n", ace->Header.AceSize);
    ok(ace->Mask == SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, "Unexpected ACE mask %#lx\n", ace->Mask);

    sid = (SID *)&ace->SidStart;
    ConvertSidToStringSidA(sid, &str);
#ifdef __REACTOS__
    if (system_token)
        ok(EqualSid(sid, &system_sid), "Expected system integrity, got %s\n", str);
    else
        ok(EqualSid(sid, &medium_sid) || EqualSid(sid, &high_sid), "Got unexpected SID %s\n", str);
#else
    ok(EqualSid(sid, &medium_sid) || EqualSid(sid, &high_sid), "Got unexpected SID %s\n", str);
#endif
    *level = sid->SubAuthority[0];
    LocalFree(str);

    ret = GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
    ok(ret, "GetSecurityDescriptorDacl failed with error %lu\n", GetLastError());
    todo_wine ok(!present, "DACL present\n");

    free(sd);
}

static void test_token_label(void)
{
    SID low_sid = {SID_REVISION, 1, {SECURITY_MANDATORY_LABEL_AUTHORITY},
                   {SECURITY_MANDATORY_LOW_RID}};
    char sacl_buffer[50];
    SECURITY_ATTRIBUTES attr = {.nLength = sizeof(SECURITY_ATTRIBUTES)};
    ACL *sacl = (ACL *)sacl_buffer;
    TOKEN_LINKED_TOKEN linked;
#ifdef __REACTOS__
    TOKEN_ELEVATION_TYPE elevation_type;
    TOKEN_USER *user;
    DWORD level, level2, size, error;
#else
    DWORD level, level2, size;
#endif
    PSECURITY_DESCRIPTOR sd;
    HANDLE token, token2;
#ifdef __REACTOS__
    BOOL ret, system_token;
#else
    BOOL ret;
#endif

    if (!pAddMandatoryAce)
    {
        win_skip("Mandatory integrity control is not supported.\n");
        return;
    }

    ret = OpenProcessToken(GetCurrentProcess(), READ_CONTROL | TOKEN_QUERY | TOKEN_DUPLICATE, &token);
    ok(ret, "OpenProcessToken failed with error %lu\n", GetLastError());

#ifdef __REACTOS__
    user = get_alloc_token_user(token);
    system_token = IsWellKnownSid(user->User.Sid, WinLocalSystemSid);
    free(user);

    check_token_label(token, &level, TRUE, system_token);
#else
    check_token_label(token, &level, TRUE);
#endif

    ret = DuplicateTokenEx(token, READ_CONTROL, NULL, SecurityAnonymous, TokenPrimary, &token2);
    ok(ret, "Failed to duplicate token, error %lu\n", GetLastError());

#ifdef __REACTOS__
    check_token_label(token2, &level2, TRUE, system_token);
#else
    check_token_label(token2, &level2, TRUE);
#endif
    ok(level2 == level, "Expected level %#lx, got %#lx.\n", level, level2);

    CloseHandle(token2);

    ret = DuplicateTokenEx(token, READ_CONTROL, NULL, SecurityImpersonation, TokenImpersonation, &token2);
    ok(ret, "Failed to duplicate token, error %lu\n", GetLastError());

#ifdef __REACTOS__
    check_token_label(token2, &level2, TRUE, system_token);
#else
    check_token_label(token2, &level2, TRUE);
#endif
    ok(level2 == level, "Expected level %#lx, got %#lx.\n", level, level2);

    CloseHandle(token2);

    /* Any label set in the SD when calling DuplicateTokenEx() is ignored. */

    ret = GetKernelObjectSecurity(token, LABEL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "got error %lu\n", GetLastError());

    sd = malloc(size);
    ret = GetKernelObjectSecurity(token, LABEL_SECURITY_INFORMATION, sd, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    InitializeAcl(sacl, sizeof(sacl_buffer), ACL_REVISION);
    AddMandatoryAce(sacl, ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, &low_sid);
    SetSecurityDescriptorSacl(sd, TRUE, sacl, FALSE);

    attr.lpSecurityDescriptor = sd;
    ret = DuplicateTokenEx(token, TOKEN_ALL_ACCESS, &attr, SecurityImpersonation, TokenImpersonation, &token2);
    ok(ret, "Failed to duplicate token, error %lu\n", GetLastError());

#ifdef __REACTOS__
    check_token_label(token2, &level2, TRUE, system_token);
#else
    check_token_label(token2, &level2, TRUE);
#endif
    ok(level2 == level, "Expected level %#lx, got %#lx.\n", level, level2);

    /* Trying to set a SD on the token also claims success but has no effect. */

    ret = SetKernelObjectSecurity(token2, LABEL_SECURITY_INFORMATION, sd);
    ok(ret, "Failed to set SD, error %lu\n", GetLastError());

#ifdef __REACTOS__
    check_token_label(token2, &level2, FALSE, system_token);
#else
    check_token_label(token2, &level2, FALSE);
#endif
    ok(level2 == level, "Expected level %#lx, got %#lx.\n", level, level2);

    free(sd);

    /* Test the linked token. */

#ifdef __REACTOS__
    ret = GetTokenInformation(token, TokenElevationType, &elevation_type, sizeof(elevation_type), &size);
    ok(ret, "Failed to get elevation type, error %lu\n", GetLastError());
    if (ret)
    {
        ret = GetTokenInformation(token, TokenLinkedToken, &linked, sizeof(linked), &size);
        error = GetLastError();
        if (system_token && elevation_type == TokenElevationTypeDefault)
            ok(!ret && error == ERROR_NO_SUCH_LOGON_SESSION,
               "Unsplit SYSTEM token linked query returned %d, error %lu\n", ret, error);
        else
            ok(ret, "Failed to get linked token, error %lu\n", error);
#else
    ret = GetTokenInformation(token, TokenLinkedToken, &linked, sizeof(linked), &size);
    ok(ret, "Failed to get linked token, error %lu\n", GetLastError());

    check_token_label(linked.LinkedToken, &level2, TRUE);
    ok(level2 == level, "Expected level %#lx, got %#lx.\n", level, level2);
#endif

#ifdef __REACTOS__
        if (ret)
        {
            check_token_label(linked.LinkedToken, &level2, TRUE, system_token);
            ok(level2 == level, "Expected level %#lx, got %#lx.\n", level, level2);
            CloseHandle(linked.LinkedToken);
        }
    }
#else
    CloseHandle(linked.LinkedToken);
#endif

    CloseHandle(token);
}

static void test_token_security_descriptor(void)
{
    static SID low_level = {SID_REVISION, 1, {SECURITY_MANDATORY_LABEL_AUTHORITY},
                            {SECURITY_MANDATORY_LOW_RID}};
    char buffer_sd[SECURITY_DESCRIPTOR_MIN_LENGTH];
    SECURITY_DESCRIPTOR *sd = (SECURITY_DESCRIPTOR *)&buffer_sd, *sd2;
    char buffer_acl[256], buffer[MAX_PATH];
    ACL *acl = (ACL *)&buffer_acl, *acl2, *acl_child;
    BOOL defaulted, present, ret, found;
    HANDLE token, token2, token3;
    EXPLICIT_ACCESSW exp_access;
    PROCESS_INFORMATION info;
    DWORD size, index, retd;
    ACCESS_ALLOWED_ACE *ace;
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA startup;
    PSID psid;

    /* Test whether we can create tokens with security descriptors */
    ret = OpenProcessToken(GetCurrentProcess(), MAXIMUM_ALLOWED, &token);
    ok(ret, "OpenProcessToken failed with error %lu\n", GetLastError());

    ret = InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
    ok(ret, "InitializeSecurityDescriptor failed with error %lu\n", GetLastError());

    memset(buffer_acl, 0, sizeof(buffer_acl));
    ret = InitializeAcl(acl, 256, ACL_REVISION);
    ok(ret, "InitializeAcl failed with error %lu\n", GetLastError());

    ret = ConvertStringSidToSidA("S-1-5-6", &psid);
    ok(ret, "ConvertStringSidToSidA failed with error %lu\n", GetLastError());

    ret = AddAccessAllowedAceEx(acl, ACL_REVISION, NO_PROPAGATE_INHERIT_ACE, GENERIC_ALL, psid);
    ok(ret, "AddAccessAllowedAceEx failed with error %lu\n", GetLastError());

    ret = SetSecurityDescriptorDacl(sd, TRUE, acl, FALSE);
    ok(ret, "SetSecurityDescriptorDacl failed with error %lu\n", GetLastError());

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;

    ret = DuplicateTokenEx(token, MAXIMUM_ALLOWED, &sa, SecurityImpersonation, TokenImpersonation, &token2);
    ok(ret, "DuplicateTokenEx failed with error %lu\n", GetLastError());

    ret = GetKernelObjectSecurity(token2, DACL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Unexpected GetKernelObjectSecurity return value %d, error %lu\n", ret, GetLastError());

    sd2 = malloc(size);
    ret = GetKernelObjectSecurity(token2, DACL_SECURITY_INFORMATION, sd2, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    acl2 = (void *)0xdeadbeef;
    present = FALSE;
    defaulted = TRUE;
    ret = GetSecurityDescriptorDacl(sd2, &present, &acl2, &defaulted);
    ok(ret, "GetSecurityDescriptorDacl failed with error %lu\n", GetLastError());
    ok(present, "acl2 not present\n");
    ok(acl2 != (void *)0xdeadbeef, "acl2 not set\n");
    ok(acl2->AceCount == 1, "Expected 1 ACE, got %d\n", acl2->AceCount);
    ok(!defaulted, "acl2 defaulted\n");

    ret = GetAce(acl2, 0, (void **)&ace);
    ok(ret, "GetAce failed with error %lu\n", GetLastError());
    ok(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE, "Unexpected ACE type %#x\n", ace->Header.AceType);
    ok(EqualSid(&ace->SidStart, psid), "Expected access allowed ACE\n");
    ok(ace->Header.AceFlags == NO_PROPAGATE_INHERIT_ACE,
       "Expected NO_PROPAGATE_INHERIT_ACE as flags, got %x\n", ace->Header.AceFlags);

    free(sd2);

    /* Duplicate token without security attributes.
     * Tokens do not inherit the security descriptor in DuplicateToken. */
    ret = DuplicateTokenEx(token2, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenImpersonation, &token3);
    ok(ret, "DuplicateTokenEx failed with error %lu\n", GetLastError());

    ret = GetKernelObjectSecurity(token3, DACL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Unexpected GetKernelObjectSecurity return value %d, error %lu\n", ret, GetLastError());

    sd2 = malloc(size);
    ret = GetKernelObjectSecurity(token3, DACL_SECURITY_INFORMATION, sd2, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    acl2 = (void *)0xdeadbeef;
    present = FALSE;
    defaulted = TRUE;
    ret = GetSecurityDescriptorDacl(sd2, &present, &acl2, &defaulted);
    ok(ret, "GetSecurityDescriptorDacl failed with error %lu\n", GetLastError());
    ok(present, "DACL not present\n");

    ok(acl2 != (void *)0xdeadbeef, "DACL not set\n");
    ok(!defaulted, "DACL defaulted\n");

    index = 0;
    found = FALSE;
    while (GetAce(acl2, index++, (void **)&ace))
    {
        if (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && EqualSid(&ace->SidStart, psid))
            found = TRUE;
    }
    ok(!found, "Access allowed ACE was inherited\n");

    free(sd2);

    /* When creating a child process, the process does inherit the token of
     * the parent but not the DACL of the token */
    ret = GetKernelObjectSecurity(token, DACL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Unexpected GetKernelObjectSecurity return value %d, error %lu\n", ret, GetLastError());

    sd2 = malloc(size);
    ret = GetKernelObjectSecurity(token, DACL_SECURITY_INFORMATION, sd2, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    acl2 = (void *)0xdeadbeef;
    present = FALSE;
    defaulted = TRUE;
    ret = GetSecurityDescriptorDacl(sd2, &present, &acl2, &defaulted);
    ok(ret, "GetSecurityDescriptorDacl failed with error %lu\n", GetLastError());
    ok(present, "DACL not present\n");
    ok(acl2 != (void *)0xdeadbeef, "DACL not set\n");
    ok(!defaulted, "DACL defaulted\n");

    exp_access.grfAccessPermissions = GENERIC_ALL;
    exp_access.grfAccessMode = GRANT_ACCESS;
    exp_access.grfInheritance = NO_PROPAGATE_INHERIT_ACE;
    exp_access.Trustee.pMultipleTrustee = NULL;
    exp_access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    exp_access.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    exp_access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    exp_access.Trustee.ptstrName = (void*)psid;

    retd = SetEntriesInAclW(1, &exp_access, acl2, &acl_child);
    ok(retd == ERROR_SUCCESS, "Expected ERROR_SUCCESS, got %lu\n", retd);

    memset(sd, 0, sizeof(buffer_sd));
    ret = InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
    ok(ret, "InitializeSecurityDescriptor failed with error %lu\n", GetLastError());

    ret = SetSecurityDescriptorDacl(sd, TRUE, acl_child, FALSE);
    ok(ret, "SetSecurityDescriptorDacl failed with error %lu\n", GetLastError());

    ret = SetKernelObjectSecurity(token, DACL_SECURITY_INFORMATION, sd);
    ok(ret, "SetKernelObjectSecurity failed with error %lu\n", GetLastError());

    /* The security label is also not inherited */
    if (pAddMandatoryAce)
    {
        ret = InitializeAcl(acl, 256, ACL_REVISION);
        ok(ret, "InitializeAcl failed with error %lu\n", GetLastError());

        ret = pAddMandatoryAce(acl, ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, &low_level);
        ok(ret, "AddMandatoryAce failed with error %lu\n", GetLastError());

        memset(sd, 0, sizeof(buffer_sd));
        ret = InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
        ok(ret, "InitializeSecurityDescriptor failed with error %lu\n", GetLastError());

        ret = SetSecurityDescriptorSacl(sd, TRUE, acl, FALSE);
        ok(ret, "SetSecurityDescriptorSacl failed with error %lu\n", GetLastError());

        ret = SetKernelObjectSecurity(token, LABEL_SECURITY_INFORMATION, sd);
        ok(ret, "SetKernelObjectSecurity failed with error %lu\n", GetLastError());
    }
    else
        win_skip("SYSTEM_MANDATORY_LABEL not supported\n");

    /* Start child process with our modified token */
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    sprintf(buffer, "%s security test_token_sd", myARGV[0]);
    ret = CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info);
    ok(ret, "CreateProcess failed with error %lu\n", GetLastError());
    wait_child_process(&info);

    LocalFree(acl_child);
    free(sd2);
    LocalFree(psid);

    CloseHandle(token3);
    CloseHandle(token2);
    CloseHandle(token);
}

static void test_child_token_sd(void)
{
    static SID low_level = {SID_REVISION, 1, {SECURITY_MANDATORY_LABEL_AUTHORITY},
                            {SECURITY_MANDATORY_LOW_RID}};
    SYSTEM_MANDATORY_LABEL_ACE *ace_label;
    BOOL ret, present, defaulted;
    ACCESS_ALLOWED_ACE *acc_ace;
    SECURITY_DESCRIPTOR *sd;
    DWORD size, i;
    HANDLE token;
    PSID psid;
    ACL *acl;

    ret = ConvertStringSidToSidA("S-1-5-6", &psid);
    ok(ret, "ConvertStringSidToSidA failed with error %lu\n", GetLastError());

    ret = OpenProcessToken(GetCurrentProcess(), MAXIMUM_ALLOWED, &token);
    ok(ret, "OpenProcessToken failed with error %lu\n", GetLastError());

    ret = GetKernelObjectSecurity(token, DACL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Unexpected GetKernelObjectSecurity return value %d, error %lu\n", ret, GetLastError());

    sd = malloc(size);
    ret = GetKernelObjectSecurity(token, DACL_SECURITY_INFORMATION, sd, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    acl = NULL;
    present = FALSE;
    defaulted = TRUE;
    ret = GetSecurityDescriptorDacl(sd, &present, &acl, &defaulted);
    ok(ret, "GetSecurityDescriptorDacl failed with error %lu\n", GetLastError());
    ok(present, "DACL not present\n");
    ok(acl && acl != (void *)0xdeadbeef, "Got invalid DACL\n");
    ok(!defaulted, "DACL defaulted\n");

    ok(acl->AceCount, "Expected at least one ACE\n");
    for (i = 0; i < acl->AceCount; i++)
    {
        ret = GetAce(acl, i, (void **)&acc_ace);
        ok(ret, "GetAce failed with error %lu\n", GetLastError());
        ok(acc_ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || !EqualSid(&acc_ace->SidStart, psid),
           "ACE inherited from the parent\n");
    }

    LocalFree(psid);
    free(sd);

    if (!pAddMandatoryAce)
    {
        win_skip("SYSTEM_MANDATORY_LABEL not supported\n");
        return;
    }

    ret = GetKernelObjectSecurity(token, LABEL_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Unexpected GetKernelObjectSecurity return value %d, error %lu\n", ret, GetLastError());

    sd = malloc(size);
    ret = GetKernelObjectSecurity(token, LABEL_SECURITY_INFORMATION, sd, size, &size);
    ok(ret, "GetKernelObjectSecurity failed with error %lu\n", GetLastError());

    acl = NULL;
    present = FALSE;
    defaulted = TRUE;
    ret = GetSecurityDescriptorSacl(sd, &present, &acl, &defaulted);
    ok(ret, "GetSecurityDescriptorSacl failed with error %lu\n", GetLastError());
    ok(present, "SACL not present\n");
    ok(acl && acl != (void *)0xdeadbeef, "Got invalid SACL\n");
    ok(!defaulted, "SACL defaulted\n");
    ok(acl->AceCount == 1, "Expected exactly one ACE\n");
    ret = GetAce(acl, 0, (void **)&ace_label);
    ok(ret, "GetAce failed with error %lu\n", GetLastError());
    ok(ace_label->Header.AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE,
       "Unexpected ACE type %#x\n", ace_label->Header.AceType);
    ok(!EqualSid(&ace_label->SidStart, &low_level),
       "Low integrity level should not have been inherited\n");

    free(sd);
}

static void test_GetExplicitEntriesFromAclW(void)
{
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = { SECURITY_WORLD_SID_AUTHORITY };
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = { SECURITY_NT_AUTHORITY };
    PSID everyone_sid = NULL, users_sid = NULL;
    EXPLICIT_ACCESSW access;
    EXPLICIT_ACCESSW *access2;
    PACL new_acl, old_acl = NULL;
    ULONG count;
    DWORD res;

    old_acl = malloc(256);
    res = InitializeAcl(old_acl, 256, ACL_REVISION);
    ok(res, "InitializeAcl failed with error %ld\n", GetLastError());

    res = AllocateAndInitializeSid(&SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &everyone_sid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    res = AllocateAndInitializeSid(&SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                   DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &users_sid);
    ok(res, "AllocateAndInitializeSid failed with error %ld\n", GetLastError());

    res = AddAccessAllowedAce(old_acl, ACL_REVISION, KEY_READ, users_sid);
    ok(res, "AddAccessAllowedAce failed with error %ld\n", GetLastError());

    access2 = NULL;
    res = GetExplicitEntriesFromAclW(old_acl, &count, &access2);
    ok(res == ERROR_SUCCESS, "GetExplicitEntriesFromAclW failed with error %ld\n", GetLastError());
    ok(count == 1, "Expected count == 1, got %ld\n", count);
    ok(access2[0].grfAccessMode == GRANT_ACCESS, "Expected GRANT_ACCESS, got %d\n", access2[0].grfAccessMode);
    ok(access2[0].grfAccessPermissions == KEY_READ, "Expected KEY_READ, got %ld\n", access2[0].grfAccessPermissions);
    ok(access2[0].Trustee.TrusteeForm == TRUSTEE_IS_SID, "Expected SID trustee, got %d\n", access2[0].Trustee.TrusteeForm);
    ok(access2[0].grfInheritance == NO_INHERITANCE, "Expected NO_INHERITANCE, got %lx\n", access2[0].grfInheritance);
    ok(EqualSid(access2[0].Trustee.ptstrName, users_sid), "Expected equal SIDs\n");
    LocalFree(access2);

    access.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    access.Trustee.pMultipleTrustee = NULL;

    access.grfAccessPermissions = KEY_WRITE;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.ptstrName = everyone_sid;
    res = SetEntriesInAclW(1, &access, old_acl, &new_acl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(new_acl != NULL, "returned acl was NULL\n");

    access2 = NULL;
    res = GetExplicitEntriesFromAclW(new_acl, &count, &access2);
    ok(res == ERROR_SUCCESS, "GetExplicitEntriesFromAclW failed with error %ld\n", GetLastError());
    ok(count == 2, "Expected count == 2, got %ld\n", count);
    ok(access2[0].grfAccessMode == GRANT_ACCESS, "Expected GRANT_ACCESS, got %d\n", access2[0].grfAccessMode);
    ok(access2[0].grfAccessPermissions == KEY_WRITE, "Expected KEY_WRITE, got %ld\n", access2[0].grfAccessPermissions);
    ok(access2[0].Trustee.TrusteeType == TRUSTEE_IS_UNKNOWN,
       "Expected TRUSTEE_IS_UNKNOWN trustee type, got %d\n", access2[0].Trustee.TrusteeType);
    ok(access2[0].Trustee.TrusteeForm == TRUSTEE_IS_SID, "Expected SID trustee, got %d\n", access2[0].Trustee.TrusteeForm);
    ok(access2[0].grfInheritance == NO_INHERITANCE, "Expected NO_INHERITANCE, got %lx\n", access2[0].grfInheritance);
    ok(EqualSid(access2[0].Trustee.ptstrName, everyone_sid), "Expected equal SIDs\n");
    LocalFree(access2);
    LocalFree(new_acl);

    access.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    res = SetEntriesInAclW(1, &access, old_acl, &new_acl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(new_acl != NULL, "returned acl was NULL\n");

    access2 = NULL;
    res = GetExplicitEntriesFromAclW(new_acl, &count, &access2);
    ok(res == ERROR_SUCCESS, "GetExplicitEntriesFromAclW failed with error %ld\n", GetLastError());
    ok(count == 2, "Expected count == 2, got %ld\n", count);
    ok(access2[0].grfAccessMode == GRANT_ACCESS, "Expected GRANT_ACCESS, got %d\n", access2[0].grfAccessMode);
    ok(access2[0].grfAccessPermissions == KEY_WRITE, "Expected KEY_WRITE, got %ld\n", access2[0].grfAccessPermissions);
    ok(access2[0].Trustee.TrusteeType == TRUSTEE_IS_UNKNOWN,
       "Expected TRUSTEE_IS_UNKNOWN trustee type, got %d\n", access2[0].Trustee.TrusteeType);
    ok(access2[0].Trustee.TrusteeForm == TRUSTEE_IS_SID, "Expected SID trustee, got %d\n", access2[0].Trustee.TrusteeForm);
    ok(access2[0].grfInheritance == NO_INHERITANCE, "Expected NO_INHERITANCE, got %lx\n", access2[0].grfInheritance);
    ok(EqualSid(access2[0].Trustee.ptstrName, everyone_sid), "Expected equal SIDs\n");
    LocalFree(access2);
    LocalFree(new_acl);

    access.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    access.Trustee.ptstrName = (WCHAR *)L"CURRENT_USER";
    res = SetEntriesInAclW(1, &access, old_acl, &new_acl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(new_acl != NULL, "returned acl was NULL\n");

    access2 = NULL;
    res = GetExplicitEntriesFromAclW(new_acl, &count, &access2);
    ok(res == ERROR_SUCCESS, "GetExplicitEntriesFromAclW failed with error %ld\n", GetLastError());
    ok(count == 2, "Expected count == 2, got %ld\n", count);
    ok(access2[0].grfAccessMode == GRANT_ACCESS, "Expected GRANT_ACCESS, got %d\n", access2[0].grfAccessMode);
    ok(access2[0].grfAccessPermissions == KEY_WRITE, "Expected KEY_WRITE, got %ld\n", access2[0].grfAccessPermissions);
    ok(access2[0].Trustee.TrusteeType == TRUSTEE_IS_UNKNOWN,
       "Expected TRUSTEE_IS_UNKNOWN trustee type, got %d\n", access2[0].Trustee.TrusteeType);
    ok(access2[0].Trustee.TrusteeForm == TRUSTEE_IS_SID, "Expected SID trustee, got %d\n", access2[0].Trustee.TrusteeForm);
    ok(access2[0].grfInheritance == NO_INHERITANCE, "Expected NO_INHERITANCE, got %lx\n", access2[0].grfInheritance);
    LocalFree(access2);
    LocalFree(new_acl);

    access.grfAccessMode = REVOKE_ACCESS;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.ptstrName = users_sid;
    res = SetEntriesInAclW(1, &access, old_acl, &new_acl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(new_acl != NULL, "returned acl was NULL\n");

    access2 = (void *)0xdeadbeef;
    res = GetExplicitEntriesFromAclW(new_acl, &count, &access2);
    ok(res == ERROR_SUCCESS, "GetExplicitEntriesFromAclW failed with error %ld\n", GetLastError());
    ok(count == 0, "Expected count == 0, got %ld\n", count);
    ok(access2 == NULL, "access2 was not NULL\n");
    LocalFree(new_acl);

    /* Make the ACL both Allow and Deny Everyone. */
    res = AddAccessAllowedAce(old_acl, ACL_REVISION, KEY_READ, everyone_sid);
    ok(res, "AddAccessAllowedAce failed with error %ld\n", GetLastError());
    res = AddAccessDeniedAce(old_acl, ACL_REVISION, KEY_WRITE, everyone_sid);
    ok(res, "AddAccessDeniedAce failed with error %ld\n", GetLastError());
    /* Revoke Everyone. */
    access.Trustee.ptstrName = everyone_sid;
    access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access.grfAccessPermissions = 0;
    new_acl = NULL;
    res = SetEntriesInAclW(1, &access, old_acl, &new_acl);
    ok(res == ERROR_SUCCESS, "SetEntriesInAclW failed: %lu\n", res);
    ok(new_acl != NULL, "returned acl was NULL\n");
    /* Deny Everyone should remain (along with Grant Users from earlier). */
    access2 = NULL;
    res = GetExplicitEntriesFromAclW(new_acl, &count, &access2);
    ok(res == ERROR_SUCCESS, "GetExplicitEntriesFromAclW failed with error %ld\n", GetLastError());
    ok(count == 2, "Expected count == 2, got %ld\n", count);
    ok(access2[0].grfAccessMode == GRANT_ACCESS, "Expected GRANT_ACCESS, got %d\n", access2[0].grfAccessMode);
    ok(access2[0].grfAccessPermissions == KEY_READ , "Expected KEY_READ, got %ld\n", access2[0].grfAccessPermissions);
    ok(EqualSid(access2[0].Trustee.ptstrName, users_sid), "Expected equal SIDs\n");
    ok(access2[1].grfAccessMode == DENY_ACCESS, "Expected DENY_ACCESS, got %d\n", access2[1].grfAccessMode);
    ok(access2[1].grfAccessPermissions == KEY_WRITE, "Expected KEY_WRITE, got %ld\n", access2[1].grfAccessPermissions);
    ok(EqualSid(access2[1].Trustee.ptstrName, everyone_sid), "Expected equal SIDs\n");
    LocalFree(access2);

    FreeSid(users_sid);
    FreeSid(everyone_sid);
    free(old_acl);
}

static void test_BuildSecurityDescriptorW(void)
{
    SECURITY_DESCRIPTOR old_sd, *new_sd, *rel_sd;
    ULONG new_sd_size;
    DWORD buf_size;
    char buf[1024];
    BOOL success;
    DWORD ret;

    InitializeSecurityDescriptor(&old_sd, SECURITY_DESCRIPTOR_REVISION);

    buf_size = sizeof(buf);
    rel_sd = (SECURITY_DESCRIPTOR *)buf;
    success = MakeSelfRelativeSD(&old_sd, rel_sd, &buf_size);
    ok(success, "MakeSelfRelativeSD failed with %lu\n", GetLastError());

    new_sd = NULL;
    new_sd_size = 0;
    ret = BuildSecurityDescriptorW(NULL, NULL, 0, NULL, 0, NULL, NULL, &new_sd_size, (void **)&new_sd);
    ok(ret == ERROR_SUCCESS, "BuildSecurityDescriptor failed with %lu\n", ret);
    ok(new_sd != NULL, "expected new_sd != NULL\n");
    LocalFree(new_sd);

    new_sd = (void *)0xdeadbeef;
    ret = BuildSecurityDescriptorW(NULL, NULL, 0, NULL, 0, NULL, &old_sd, &new_sd_size, (void **)&new_sd);
    ok(ret == ERROR_INVALID_SECURITY_DESCR, "expected ERROR_INVALID_SECURITY_DESCR, got %lu\n", ret);
    ok(new_sd == (void *)0xdeadbeef, "expected new_sd == 0xdeadbeef, got %p\n", new_sd);

    new_sd = NULL;
    new_sd_size = 0;
    ret = BuildSecurityDescriptorW(NULL, NULL, 0, NULL, 0, NULL, rel_sd, &new_sd_size, (void **)&new_sd);
    ok(ret == ERROR_SUCCESS, "BuildSecurityDescriptor failed with %lu\n", ret);
    ok(new_sd != NULL, "expected new_sd != NULL\n");
    LocalFree(new_sd);
}

static void test_EqualDomainSid(void)
{
    SID_IDENTIFIER_AUTHORITY ident = { SECURITY_NT_AUTHORITY };
    char sid_buffer[SECURITY_MAX_SID_SIZE], sid_buffer2[SECURITY_MAX_SID_SIZE];
    PSID domainsid, sid = sid_buffer, sid2 = sid_buffer2;
    DWORD size;
    BOOL ret, equal;
    unsigned int i;

    ret = AllocateAndInitializeSid(&ident, 6, SECURITY_NT_NON_UNIQUE, 12, 23, 34, 45, 56, 0, 0, &domainsid);
    ok(ret, "AllocateAndInitializeSid error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = EqualDomainSid(NULL, NULL, NULL);
    ok(!ret, "got %d\n", ret);
    ok(GetLastError() == ERROR_INVALID_SID, "got %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = EqualDomainSid(domainsid, domainsid, NULL);
    ok(!ret, "got %d\n", ret);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "got %lu\n", GetLastError());

    for (i = 0; i < ARRAY_SIZE(well_known_sid_values); i++)
    {
        SID *pisid = sid;

        size = sizeof(sid_buffer);
        if (!CreateWellKnownSid(i, NULL, sid, &size))
        {
            trace("Well known SID %u not supported\n", i);
            continue;
        }

        equal = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        ret = EqualDomainSid(sid, domainsid, &equal);
        if (pisid->SubAuthority[0] != SECURITY_BUILTIN_DOMAIN_RID)
        {
            ok(!ret, "%u: got %d\n", i, ret);
            ok(GetLastError() == ERROR_NON_DOMAIN_SID, "%u: got %lu\n", i, GetLastError());
            ok(equal == 0xdeadbeef, "%u: got %d\n", i, equal);
            continue;
        }

        ok(ret, "%u: got %d\n", i, ret);
        ok(GetLastError() == 0, "%u: got %lu\n", i, GetLastError());
        ok(equal == 0, "%u: got %d\n", i, equal);

        size = sizeof(sid_buffer2);
        ret = CreateWellKnownSid(i, well_known_sid_values[i].without_domain ? NULL : domainsid, sid2, &size);
        ok(ret, "%u: CreateWellKnownSid error %lu\n", i, GetLastError());

        equal = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        ret = EqualDomainSid(sid, sid2, &equal);
        ok(ret, "%u: got %d\n", i, ret);
        ok(GetLastError() == 0, "%u: got %lu\n", i, GetLastError());
        ok(equal == 1, "%u: got %d\n", i, equal);
    }

    FreeSid(domainsid);
}

static DWORD WINAPI duplicate_handle_access_thread(void *arg)
{
    HANDLE event = arg, event2;
    BOOL ret;

    event2 = OpenEventA(SYNCHRONIZE, FALSE, "test_dup");
    ok(!!event2, "got error %lu\n", GetLastError());
    CloseHandle(event2);

    event2 = OpenEventA(EVENT_MODIFY_STATE, FALSE, "test_dup");
    ok(!!event2, "got error %lu\n", GetLastError());
    CloseHandle(event2);

    ret = DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(),
            &event2, EVENT_MODIFY_STATE, FALSE, 0);
    ok(ret, "got error %lu\n", GetLastError());
    CloseHandle(event2);

    return 0;
}

#ifdef __REACTOS__
static BOOL check_token_group_attributes(HANDLE token, PSID sid, DWORD expected)
{
    TOKEN_GROUPS *groups;
    DWORD size = 0, attributes = 0;
    BOOL ret, found = FALSE;

    ret = GetTokenInformation(token, TokenGroups, NULL, 0, &size);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "GetTokenInformation(TokenGroups) returned %d, error %lu\n", ret, GetLastError());
    if (ret || GetLastError() != ERROR_INSUFFICIENT_BUFFER) return FALSE;
    groups = malloc(size);
    ok(!!groups, "Failed to allocate %lu bytes\n", size);
    if (!groups) return FALSE;
    ret = GetTokenInformation(token, TokenGroups, groups, size, &size);
    ok(ret, "GetTokenInformation(TokenGroups) failed with error %lu\n", GetLastError());
    if (ret)
    {
        for (DWORD i = 0; i < groups->GroupCount; ++i)
        {
            if (EqualSid(groups->Groups[i].Sid, sid))
            {
                found = TRUE;
                attributes = groups->Groups[i].Attributes;
                break;
            }
        }
        ok(found, "Fixture group %s is absent from token\n", debugstr_sid(sid));
        if (found)
            ok((attributes & (SE_GROUP_ENABLED | SE_GROUP_USE_FOR_DENY_ONLY)) == expected,
               "Fixture group %s attributes %#lx, expected %#lx\n", debugstr_sid(sid), attributes, expected);
    }
    free(groups);
    return ret && found && (attributes & (SE_GROUP_ENABLED | SE_GROUP_USE_FOR_DENY_ONLY)) == expected;
}

#endif
static void test_duplicate_handle_access(void)
{
#ifdef __REACTOS__
    char acl_buffer[200], everyone_sid_buffer[100], cmdline[300];
    union
    {
        SID sid;
        BYTE buffer[SECURITY_MAX_SID_SIZE];
    } group_sid;
#else
    char acl_buffer[200], everyone_sid_buffer[100], local_sid_buffer[100], cmdline[300];
#endif
    HANDLE token, restricted, impersonation, all_event, sync_event, event2, thread;
    SECURITY_ATTRIBUTES sa = {.nLength = sizeof(sa)};
    SID *everyone_sid = (SID *)everyone_sid_buffer;
#ifndef __REACTOS__
    SID *local_sid = (SID *)local_sid_buffer;
#endif
    ACL *acl = (ACL *)acl_buffer;
    SID_AND_ATTRIBUTES sid_attr;
    SECURITY_DESCRIPTOR sd;
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = {0};
    DWORD size;
    BOOL ret;

    /* DuplicateHandle() validates access against the calling thread's token and
     * the target process's token. It does *not* validate access against the
     * calling process's token, even if the calling thread is not impersonating.
     */

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &token);
    ok(ret, "got error %lu\n", GetLastError());
#ifdef __REACTOS__
    if (!ret) return;

    size = sizeof(group_sid);
    ret = CreateWellKnownSid(WinAuthenticatedUserSid, NULL, &group_sid.sid, &size);
    ok(ret, "CreateWellKnownSid failed with error %lu\n", GetLastError());
    if (!ret || !check_token_group_attributes(token, &group_sid.sid, SE_GROUP_ENABLED))
    {
        CloseHandle(token);
        return;
    }
#endif

    size = sizeof(everyone_sid_buffer);
    ret = CreateWellKnownSid(WinWorldSid, NULL, everyone_sid, &size);
    ok(ret, "got error %lu\n", GetLastError());
#ifndef __REACTOS__
    size = sizeof(local_sid_buffer);
    ret = CreateWellKnownSid(WinLocalSid, NULL, local_sid, &size);
    ok(ret, "got error %lu\n", GetLastError());
#endif

    InitializeAcl(acl, sizeof(acl_buffer), ACL_REVISION);
    ret = AddAccessAllowedAce(acl, ACL_REVISION, SYNCHRONIZE, everyone_sid);
    ok(ret, "got error %lu\n", GetLastError());
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
#ifdef __REACTOS__
    ret = AddAccessAllowedAce(acl, ACL_REVISION, EVENT_MODIFY_STATE, &group_sid.sid);
#else
    ret = AddAccessAllowedAce(acl, ACL_REVISION, EVENT_MODIFY_STATE, local_sid);
#endif
    ok(ret, "got error %lu\n", GetLastError());
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    ret = SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE);
    ok(ret, "got error %lu\n", GetLastError());
    sa.lpSecurityDescriptor = &sd;

#ifdef __REACTOS__
    sid_attr.Sid = &group_sid.sid;
#else
    sid_attr.Sid = local_sid;
#endif
    sid_attr.Attributes = 0;
    ret = CreateRestrictedToken(token, 0, 1, &sid_attr, 0, NULL, 0, NULL, &restricted);
    ok(ret, "got error %lu\n", GetLastError());
#ifdef __REACTOS__
    if (!ret)
    {
        CloseHandle(token);
        return;
    }
    check_token_group_attributes(restricted, &group_sid.sid, SE_GROUP_USE_FOR_DENY_ONLY);
#endif
    ret = DuplicateTokenEx(restricted, TOKEN_IMPERSONATE, NULL,
            SecurityImpersonation, TokenImpersonation, &impersonation);
    ok(ret, "got error %lu\n", GetLastError());

    all_event = CreateEventA(&sa, TRUE, TRUE, "test_dup");
    ok(!!all_event, "got error %lu\n", GetLastError());
    sync_event = OpenEventA(SYNCHRONIZE, FALSE, "test_dup");
    ok(!!sync_event, "got error %lu\n", GetLastError());

    event2 = OpenEventA(SYNCHRONIZE, FALSE, "test_dup");
    ok(!!event2, "got error %lu\n", GetLastError());
    CloseHandle(event2);

    event2 = OpenEventA(EVENT_MODIFY_STATE, FALSE, "test_dup");
    ok(!!event2, "got error %lu\n", GetLastError());
    CloseHandle(event2);

    ret = DuplicateHandle(GetCurrentProcess(), all_event, GetCurrentProcess(), &event2, EVENT_MODIFY_STATE, FALSE, 0);
    ok(ret, "got error %lu\n", GetLastError());
    CloseHandle(event2);

    ret = DuplicateHandle(GetCurrentProcess(), sync_event, GetCurrentProcess(), &event2, EVENT_MODIFY_STATE, FALSE, 0);
    ok(ret, "got error %lu\n", GetLastError());
    CloseHandle(event2);

    ret = SetThreadToken(NULL, impersonation);
    ok(ret, "got error %lu\n", GetLastError());

    thread = CreateThread(NULL, 0, duplicate_handle_access_thread, sync_event, 0, NULL);
    ret = WaitForSingleObject(thread, 1000);
    ok(!ret, "wait failed\n");

    event2 = OpenEventA(SYNCHRONIZE, FALSE, "test_dup");
    ok(!!event2, "got error %lu\n", GetLastError());
    CloseHandle(event2);

    SetLastError(0xdeadbeef);
    event2 = OpenEventA(EVENT_MODIFY_STATE, FALSE, "test_dup");
    ok(!event2, "expected failure\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "got error %lu\n", GetLastError());

    ret = DuplicateHandle(GetCurrentProcess(), all_event, GetCurrentProcess(), &event2, EVENT_MODIFY_STATE, FALSE, 0);
    ok(ret, "got error %lu\n", GetLastError());
    CloseHandle(event2);

    SetLastError(0xdeadbeef);
    ret = DuplicateHandle(GetCurrentProcess(), sync_event, GetCurrentProcess(), &event2, EVENT_MODIFY_STATE, FALSE, 0);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "got error %lu\n", GetLastError());

    ret = RevertToSelf();
    ok(ret, "got error %lu\n", GetLastError());

    sprintf(cmdline, "%s security duplicate %Iu %lu %Iu", myARGV[0],
            (ULONG_PTR)sync_event, GetCurrentProcessId(), (ULONG_PTR)impersonation );
    ret = CreateProcessAsUserA(restricted, NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    ok(ret, "got error %lu\n", GetLastError());

    ret = DuplicateHandle(GetCurrentProcess(), all_event, pi.hProcess, &event2, EVENT_MODIFY_STATE, FALSE, 0);
    ok(ret, "got error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = DuplicateHandle(GetCurrentProcess(), sync_event, pi.hProcess, &event2, EVENT_MODIFY_STATE, FALSE, 0);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "got error %lu\n", GetLastError());

    ret = WaitForSingleObject(pi.hProcess, 1000);
    ok(!ret, "wait failed\n");

    CloseHandle(impersonation);
    CloseHandle(restricted);
    CloseHandle(token);
    CloseHandle(sync_event);
    CloseHandle(all_event);
}

static void test_duplicate_handle_access_child(void)
{
    HANDLE event, event2, process, token;
    BOOL ret;

    event = (HANDLE)(ULONG_PTR)_atoi64(myARGV[3]);
    process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, atoi(myARGV[4]));
    ok(!!process, "failed to open process, error %lu\n", GetLastError());

    event2 = OpenEventA(SYNCHRONIZE, FALSE, "test_dup");
    ok(!!event2, "got error %lu\n", GetLastError());
    CloseHandle(event2);

    SetLastError(0xdeadbeef);
    event2 = OpenEventA(EVENT_MODIFY_STATE, FALSE, "test_dup");
    ok(!event2, "expected failure\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "got error %lu\n", GetLastError());

    ret = DuplicateHandle(process, event, process, &event2, EVENT_MODIFY_STATE, FALSE, 0);
    ok(ret, "got error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = DuplicateHandle(process, event, GetCurrentProcess(), &event2, EVENT_MODIFY_STATE, FALSE, 0);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "got error %lu\n", GetLastError());

    ret = DuplicateHandle(process, (HANDLE)(ULONG_PTR)_atoi64(myARGV[5]),
            GetCurrentProcess(), &token, 0, FALSE, DUPLICATE_SAME_ACCESS);
    ok(ret, "failed to retrieve token, error %lu\n", GetLastError());
    ret = SetThreadToken(NULL, token);
    ok(ret, "failed to set thread token, error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = DuplicateHandle(process, event, process, &event2, EVENT_MODIFY_STATE, FALSE, 0);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "got error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = DuplicateHandle(process, event, GetCurrentProcess(), &event2, EVENT_MODIFY_STATE, FALSE, 0);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "got error %lu\n", GetLastError());

    ret = RevertToSelf();
    ok(ret, "failed to revert, error %lu\n", GetLastError());
    CloseHandle(token);
    CloseHandle(process);
}

#define join_process(a) join_process_(__LINE__, a)
static void join_process_(int line, const PROCESS_INFORMATION *pi)
{
    DWORD ret = WaitForSingleObject(pi->hProcess, 1000);
    ok_(__FILE__, line)(!ret, "wait failed\n");
    CloseHandle(pi->hProcess);
    CloseHandle(pi->hThread);
}

static void test_create_process_token(void)
{
#ifdef __REACTOS__
    static const SECURITY_IMPERSONATION_LEVEL levels[] = {SecurityAnonymous, SecurityIdentification};
    unsigned int i;
    char cmdline[300], acl_buffer[200];
    union
    {
        SID sid;
        BYTE buffer[SECURITY_MAX_SID_SIZE];
    } group_sid;
#else
    char cmdline[300], acl_buffer[200], sid_buffer[100];
#endif
    SECURITY_ATTRIBUTES sa = {.nLength = sizeof(sa)};
    ACL *acl = (ACL *)acl_buffer;
#ifndef __REACTOS__
    SID *sid = (SID *)sid_buffer;
#endif
    SID_AND_ATTRIBUTES sid_attr;
    HANDLE event, token, token2;
    PROCESS_INFORMATION pi;
    SECURITY_DESCRIPTOR sd;
    STARTUPINFOA si = {0};
#ifdef __REACTOS__
    DWORD size, error;
#else
    DWORD size;
#endif
    BOOL ret;

#ifdef __REACTOS__
    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
#else
    size = sizeof(sid_buffer);
    ret = CreateWellKnownSid(WinLocalSid, NULL, sid, &size);
#endif
    ok(ret, "got error %lu\n", GetLastError());
#ifdef __REACTOS__
    if (!ret) return;
    size = sizeof(group_sid);
    ret = CreateWellKnownSid(WinAuthenticatedUserSid, NULL, &group_sid.sid, &size);
    ok(ret, "CreateWellKnownSid failed with error %lu\n", GetLastError());
    if (ret) ret = check_token_group_attributes(token, &group_sid.sid, SE_GROUP_ENABLED);
    CloseHandle(token);
    if (!ret) return;
#endif
    ret = InitializeAcl(acl, sizeof(acl_buffer), ACL_REVISION);
    ok(ret, "got error %lu\n", GetLastError());
#ifdef __REACTOS__
    ret = AddAccessAllowedAce(acl, ACL_REVISION, EVENT_MODIFY_STATE, &group_sid.sid);
#else
    ret = AddAccessAllowedAce(acl, ACL_REVISION, EVENT_MODIFY_STATE, sid);
#endif
    ok(ret, "got error %lu\n", GetLastError());
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    ret = SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE);
    ok(ret, "got error %lu\n", GetLastError());
    sa.lpSecurityDescriptor = &sd;
    event = CreateEventA(&sa, TRUE, TRUE, "test_event");
    ok(!!event, "got error %lu\n", GetLastError());

    sprintf(cmdline, "%s security restricted 0", myARGV[0]);

    ret = CreateProcessAsUserA(NULL, NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    ok(ret, "got error %lu\n", GetLastError());
    join_process(&pi);

    ret = CreateProcessAsUserA(GetCurrentProcessToken(), NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    todo_wine ok(!ret, "expected failure\n");
    todo_wine ok(GetLastError() == ERROR_INVALID_HANDLE, "got error %lu\n", GetLastError());
    if (ret) join_process(&pi);

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &token);
    ok(ret, "got error %lu\n", GetLastError());
    ret = CreateProcessAsUserA(token, NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
#ifdef __REACTOS__
    ok(ret, "got error %lu\n", GetLastError());
#else
    ok(ret || broken(GetLastError() == ERROR_ACCESS_DENIED) /* < 7 */, "got error %lu\n", GetLastError());
#endif
    if (ret) join_process(&pi);
    CloseHandle(token);

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    ok(ret, "got error %lu\n", GetLastError());
    ret = CreateProcessAsUserA(token, NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "got error %lu\n", GetLastError());
    CloseHandle(token);

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_ASSIGN_PRIMARY, &token);
    ok(ret, "got error %lu\n", GetLastError());
    ret = CreateProcessAsUserA(token, NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "got error %lu\n", GetLastError());
    CloseHandle(token);

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE, &token);
    ok(ret, "got error %lu\n", GetLastError());

    ret = DuplicateTokenEx(token, TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, NULL,
            SecurityImpersonation, TokenImpersonation, &token2);
    ok(ret, "got error %lu\n", GetLastError());
    ret = CreateProcessAsUserA(token2, NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
#ifdef __REACTOS__
    ok(ret, "got error %lu\n", GetLastError());
#else
    ok(ret || broken(GetLastError() == ERROR_BAD_TOKEN_TYPE) /* < 7 */, "got error %lu\n", GetLastError());
#endif
    if (ret) join_process(&pi);
    CloseHandle(token2);

#ifdef __REACTOS__
    for (i = 0; i < ARRAY_SIZE(levels); ++i)
    {
        ret = DuplicateTokenEx(token, TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, NULL,
                levels[i], TokenImpersonation, &token2);
        ok(ret, "DuplicateTokenEx level %u failed with error %lu\n", levels[i], GetLastError());
        if (!ret) continue;
        sprintf(cmdline, "%s security restricted 0", myARGV[0]);
        SetLastError(0xdeadbeef);
        ret = CreateProcessAsUserA(token2, NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        error = GetLastError();
        ok(!ret, "CreateProcessAsUserA level %u returned %d, error %lu\n", levels[i], ret, error);
        ok(error == ERROR_BAD_IMPERSONATION_LEVEL,
                "CreateProcessAsUserA level %u returned error %lu, expected %u\n",
                levels[i], error, ERROR_BAD_IMPERSONATION_LEVEL);
        if (ret) join_process(&pi);
        CloseHandle(token2);
    }

#endif
    sprintf(cmdline, "%s security restricted 1", myARGV[0]);
#ifdef __REACTOS__
    sid_attr.Sid = &group_sid.sid;
#else
    sid_attr.Sid = sid;
#endif
    sid_attr.Attributes = 0;
    ret = CreateRestrictedToken(token, 0, 1, &sid_attr, 0, NULL, 0, NULL, &token2);
    ok(ret, "got error %lu\n", GetLastError());
#ifdef __REACTOS__
    if (!ret)
    {
        CloseHandle(token);
        CloseHandle(event);
        return;
    }
    check_token_group_attributes(token2, &group_sid.sid, SE_GROUP_USE_FOR_DENY_ONLY);
#endif
    ret = CreateProcessAsUserA(token2, NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    ok(ret, "got error %lu\n", GetLastError());
    join_process(&pi);
    CloseHandle(token2);

    CloseHandle(token);

    CloseHandle(event);
}

static void test_create_process_token_child(void)
{
    HANDLE event;

    SetLastError(0xdeadbeef);
    event = OpenEventA(EVENT_MODIFY_STATE, FALSE, "test_event");
    if (!atoi(myARGV[3]))
    {
        ok(!!event, "got error %lu\n", GetLastError());
        CloseHandle(event);
    }
    else
    {
        ok(!event, "expected failure\n");
        ok(GetLastError() == ERROR_ACCESS_DENIED, "got error %lu\n", GetLastError());
    }
}

static void test_pseudo_handle_security(void)
{
    char buffer[200];
    PSECURITY_DESCRIPTOR sd = buffer, sd_ptr;
    unsigned int i;
    DWORD size;
    BOOL ret;

    static const HKEY keys[] =
    {
        HKEY_CLASSES_ROOT,
        HKEY_CURRENT_USER,
        HKEY_LOCAL_MACHINE,
        HKEY_USERS,
        HKEY_PERFORMANCE_DATA,
        HKEY_CURRENT_CONFIG,
        HKEY_DYN_DATA,
    };

    ret = GetKernelObjectSecurity(GetCurrentProcess(), OWNER_SECURITY_INFORMATION, &sd, sizeof(buffer), &size);
    ok(ret, "got error %lu\n", GetLastError());

    ret = GetKernelObjectSecurity(GetCurrentThread(), OWNER_SECURITY_INFORMATION, &sd, sizeof(buffer), &size);
    ok(ret, "got error %lu\n", GetLastError());

    for (i = 0; i < ARRAY_SIZE(keys); ++i)
    {
        SetLastError(0xdeadbeef);
        ret = GetKernelObjectSecurity(keys[i], OWNER_SECURITY_INFORMATION, &sd, sizeof(buffer), &size);
        ok(!ret, "key %p: expected failure\n", keys[i]);
        ok(GetLastError() == ERROR_INVALID_HANDLE, "key %p: got error %lu\n", keys[i], GetLastError());

        ret = GetSecurityInfo(keys[i], SE_REGISTRY_KEY,
                DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &sd_ptr);
        if (keys[i] == HKEY_PERFORMANCE_DATA)
            ok(ret == ERROR_INVALID_HANDLE, "key %p: got error %u\n", keys[i], ret);
        else if (keys[i] == HKEY_DYN_DATA)
            todo_wine ok(ret == ERROR_CALL_NOT_IMPLEMENTED || broken(ret == ERROR_INVALID_HANDLE) /* <7 */,
                    "key %p: got error %u\n", keys[i], ret);
        else
            ok(!ret, "key %p: got error %u\n", keys[i], ret);
        if (!ret) LocalFree(sd_ptr);

        ret = GetSecurityInfo(keys[i], SE_KERNEL_OBJECT,
                DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &sd_ptr);
        ok(ret == ERROR_INVALID_HANDLE, "key %p: got error %u\n", keys[i], ret);
    }
}

static const LUID_AND_ATTRIBUTES *find_privilege(const TOKEN_PRIVILEGES *privs, const LUID *luid)
{
    DWORD i;

    for (i = 0; i < privs->PrivilegeCount; ++i)
    {
        if (!memcmp(luid, &privs->Privileges[i].Luid, sizeof(LUID)))
            return &privs->Privileges[i];
    }

    return NULL;
}

static void test_duplicate_token(void)
{
    const DWORD orig_access = TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_PRIVILEGES;
    char prev_privs_buffer[128], ret_privs_buffer[1024];
    TOKEN_PRIVILEGES *prev_privs = (void *)prev_privs_buffer;
    TOKEN_PRIVILEGES *ret_privs = (void *)ret_privs_buffer;
    const LUID_AND_ATTRIBUTES *priv;
    TOKEN_PRIVILEGES privs;
    SECURITY_QUALITY_OF_SERVICE qos = {.Length = sizeof(qos)};
    OBJECT_ATTRIBUTES attr = {.Length = sizeof(attr)};
    SECURITY_IMPERSONATION_LEVEL level;
    HANDLE token, token2;
    DWORD size;
    BOOL ret;

    ret = OpenProcessToken(GetCurrentProcess(), orig_access, &token);
    ok(ret, "got error %lu\n", GetLastError());

    /* Disable a privilege, to see if that privilege modification is preserved
     * in the duplicated tokens. */
    privs.PrivilegeCount = 1;
    ret = LookupPrivilegeValueA(NULL, "SeChangeNotifyPrivilege", &privs.Privileges[0].Luid);
    ok(ret, "got error %lu\n", GetLastError());
    privs.Privileges[0].Attributes = 0;
    ret = AdjustTokenPrivileges(token, FALSE, &privs, sizeof(prev_privs_buffer), prev_privs, &size);
    ok(ret, "got error %lu\n", GetLastError());

    ret = DuplicateToken(token, SecurityAnonymous, &token2);
    ok(ret, "got error %lu\n", GetLastError());
    TEST_GRANTED_ACCESS(token2, TOKEN_QUERY | TOKEN_IMPERSONATE);
    ret = GetTokenInformation(token2, TokenImpersonationLevel, &level, sizeof(level), &size);
    ok(ret, "got error %lu\n", GetLastError());
    ok(level == SecurityAnonymous, "got impersonation level %#x\n", level);
    ret = GetTokenInformation(token2, TokenPrivileges, ret_privs, sizeof(ret_privs_buffer), &size);
    ok(ret, "got error %lu\n", GetLastError());
    priv = find_privilege(ret_privs, &privs.Privileges[0].Luid);
    ok(!!priv, "Privilege should exist\n");
    todo_wine ok(priv->Attributes == SE_GROUP_MANDATORY, "Got attributes %#lx\n", priv->Attributes);
    CloseHandle(token2);

    ret = DuplicateTokenEx(token, 0, NULL, SecurityAnonymous, TokenPrimary, &token2);
    ok(ret, "got error %lu\n", GetLastError());
    TEST_GRANTED_ACCESS(token2, orig_access);
    ret = GetTokenInformation(token2, TokenImpersonationLevel, &level, sizeof(level), &size);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Got error %lu.\n", GetLastError());
    ret = GetTokenInformation(token2, TokenPrivileges, ret_privs, sizeof(ret_privs_buffer), &size);
    ok(ret, "got error %lu\n", GetLastError());
    priv = find_privilege(ret_privs, &privs.Privileges[0].Luid);
    ok(!!priv, "Privilege should exist\n");
    todo_wine ok(priv->Attributes == SE_GROUP_MANDATORY, "Got attributes %#lx\n", priv->Attributes);
    CloseHandle(token2);

    ret = DuplicateTokenEx(token, MAXIMUM_ALLOWED, NULL, SecurityAnonymous, TokenPrimary, &token2);
    ok(ret, "got error %lu\n", GetLastError());
    TEST_GRANTED_ACCESS(token2, TOKEN_ALL_ACCESS);
    CloseHandle(token2);

    ret = DuplicateTokenEx(token, TOKEN_QUERY_SOURCE, NULL, SecurityAnonymous, TokenPrimary, &token2);
    ok(ret, "got error %lu\n", GetLastError());
    TEST_GRANTED_ACCESS(token2, TOKEN_QUERY_SOURCE);
    CloseHandle(token2);

    ret = DuplicateTokenEx(token, 0, NULL, SecurityIdentification, TokenImpersonation, &token2);
    ok(ret, "got error %lu\n", GetLastError());
    TEST_GRANTED_ACCESS(token2, orig_access);
    ret = GetTokenInformation(token2, TokenImpersonationLevel, &level, sizeof(level), &size);
    ok(ret, "got error %lu\n", GetLastError());
    ok(level == SecurityIdentification, "got impersonation level %#x\n", level);
    ret = GetTokenInformation(token2, TokenPrivileges, ret_privs, sizeof(ret_privs_buffer), &size);
    ok(ret, "got error %lu\n", GetLastError());
    priv = find_privilege(ret_privs, &privs.Privileges[0].Luid);
    ok(!!priv, "Privilege should exist\n");
    todo_wine ok(priv->Attributes == SE_GROUP_MANDATORY, "Got attributes %#lx\n", priv->Attributes);
    CloseHandle(token2);

    ret = NtDuplicateToken(token, 0, &attr, FALSE, TokenImpersonation, &token2);
    ok(ret == STATUS_SUCCESS, "Got status %#x.\n", ret);
    TEST_GRANTED_ACCESS(token2, orig_access);
    ret = GetTokenInformation(token2, TokenImpersonationLevel, &level, sizeof(level), &size);
    ok(ret, "got error %lu\n", GetLastError());
    ok(level == SecurityAnonymous, "got impersonation level %#x\n", level);
    ret = GetTokenInformation(token2, TokenPrivileges, ret_privs, sizeof(ret_privs_buffer), &size);
    ok(ret, "got error %lu\n", GetLastError());
    priv = find_privilege(ret_privs, &privs.Privileges[0].Luid);
    ok(!!priv, "Privilege should exist\n");
    todo_wine ok(priv->Attributes == SE_GROUP_MANDATORY, "Got attributes %#lx\n", priv->Attributes);
    CloseHandle(token2);

    ret = NtDuplicateToken(token, 0, &attr, TRUE, TokenImpersonation, &token2);
    ok(ret == STATUS_SUCCESS, "Got status %#x.\n", ret);
    TEST_GRANTED_ACCESS(token2, orig_access);
    ret = GetTokenInformation(token2, TokenPrivileges, ret_privs, sizeof(ret_privs_buffer), &size);
    ok(ret, "got error %lu\n", GetLastError());
    priv = find_privilege(ret_privs, &privs.Privileges[0].Luid);
    todo_wine ok(!priv, "Privilege shouldn't exist\n");
    CloseHandle(token2);

    qos.ImpersonationLevel = SecurityIdentification;
    qos.ContextTrackingMode = SECURITY_STATIC_TRACKING;
    qos.EffectiveOnly = FALSE;
    attr.SecurityQualityOfService = &qos;
    ret = NtDuplicateToken(token, 0, &attr, FALSE, TokenImpersonation, &token2);
    ok(ret == STATUS_SUCCESS, "Got status %#x.\n", ret);
    TEST_GRANTED_ACCESS(token2, orig_access);
    ret = GetTokenInformation(token2, TokenImpersonationLevel, &level, sizeof(level), &size);
    ok(ret, "got error %lu\n", GetLastError());
    ok(level == SecurityIdentification, "got impersonation level %#x\n", level);
    CloseHandle(token2);

    privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    ret = AdjustTokenPrivileges(token, FALSE, &privs, sizeof(prev_privs_buffer), prev_privs, &size);
    ok(ret, "got error %lu\n", GetLastError());

    CloseHandle(token);
}

static void test_GetKernelObjectSecurity(void)
{
    /* Basic tests for parameter validation. */

    SECURITY_DESCRIPTOR_CONTROL control;
    DWORD size, ret_size, revision;
    BOOL ret, present, defaulted;
    PSECURITY_DESCRIPTOR sd;
    PSID sid;
    ACL *acl;

    SetLastError(0xdeadbeef);
    size = 0xdeadbeef;
    ret = GetKernelObjectSecurity(NULL, OWNER_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "got error %lu\n", GetLastError());
    ok(size == 0xdeadbeef, "got size %lu\n", size);

    SetLastError(0xdeadbeef);
    ret = GetKernelObjectSecurity(GetCurrentProcess(), OWNER_SECURITY_INFORMATION, NULL, 0, NULL);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_NOACCESS, "got error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    size = 0xdeadbeef;
    ret = GetKernelObjectSecurity(GetCurrentProcess(), OWNER_SECURITY_INFORMATION, NULL, 0, &size);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "got error %lu\n", GetLastError());
    ok(size > 0 && size != 0xdeadbeef, "got size 0\n");

    sd = malloc(size + 1);

    SetLastError(0xdeadbeef);
    ret = GetKernelObjectSecurity(GetCurrentProcess(), OWNER_SECURITY_INFORMATION, sd, size - 1, &ret_size);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "got error %lu\n", GetLastError());
    ok(ret_size == size, "expected size %lu, got %lu\n", size, ret_size);

    SetLastError(0xdeadbeef);
    ret = GetKernelObjectSecurity(GetCurrentProcess(), OWNER_SECURITY_INFORMATION, sd, size + 1, &ret_size);
    ok(ret, "expected success\n");
    ok(GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError());
    ok(ret_size == size, "expected size %lu, got %lu\n", size, ret_size);

    free(sd);

    /* Calling the function with flags not defined succeeds and yields an empty
     * descriptor. */

    SetLastError(0xdeadbeef);
    ret = GetKernelObjectSecurity(GetCurrentProcess(), 0x100000, NULL, 0, &size);
    ok(!ret, "expected failure\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "got error %lu\n", GetLastError());

    sd = malloc(size);
    SetLastError(0xdeadbeef);
    ret = GetKernelObjectSecurity(GetCurrentProcess(), 0x100000, sd, size, &ret_size);
    ok(ret, "expected success\n");
    ok(GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError());
    ok(ret_size == size, "expected size %lu, got %lu\n", size, ret_size);

    ret = GetSecurityDescriptorControl(sd, &control, &revision);
    ok(ret, "got error %lu\n", GetLastError());
    todo_wine ok(control == SE_SELF_RELATIVE, "got control %#x\n", control);
    ok(revision == SECURITY_DESCRIPTOR_REVISION1, "got revision %lu\n", revision);

    ret = GetSecurityDescriptorOwner(sd, &sid, &defaulted);
    ok(ret, "got error %lu\n", GetLastError());
    ok(!sid, "expected no owner SID\n");
    ok(!defaulted, "expected owner not defaulted\n");

    ret = GetSecurityDescriptorGroup(sd, &sid, &defaulted);
    ok(ret, "got error %lu\n", GetLastError());
    ok(!sid, "expected no group SID\n");
    ok(!defaulted, "expected group not defaulted\n");

    ret = GetSecurityDescriptorDacl(sd, &present, &acl, &defaulted);
    ok(ret, "got error %lu\n", GetLastError());
    todo_wine ok(!present, "expected no DACL present\n");
    /* the descriptor is defaulted only on Windows >= 7 */

    ret = GetSecurityDescriptorSacl(sd, &present, &acl, &defaulted);
    ok(ret, "got error %lu\n", GetLastError());
    ok(!present, "expected no SACL present\n");
    /* the descriptor is defaulted only on Windows >= 7 */

    free(sd);
}

static void check_different_token(HANDLE token1, HANDLE token2)
{
    TOKEN_STATISTICS stats1, stats2;
    DWORD size;
    BOOL ret;

    ret = GetTokenInformation(token1, TokenStatistics, &stats1, sizeof(stats1), &size);
    ok(ret, "got error %lu\n", GetLastError());
    ret = GetTokenInformation(token2, TokenStatistics, &stats2, sizeof(stats2), &size);
    ok(ret, "got error %lu\n", GetLastError());

    ok(memcmp(&stats1.TokenId, &stats2.TokenId, sizeof(LUID)), "expected different IDs\n");
}

static void test_elevation(void)
{
    TOKEN_LINKED_TOKEN linked, linked2;
    DWORD orig_type, type, size;
    TOKEN_ELEVATION elevation;
    HANDLE token, token2;
    BOOL ret;

    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | READ_CONTROL | TOKEN_DUPLICATE
            | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_PRIVILEGES | TOKEN_ADJUST_DEFAULT, &token);
    ok(ret, "got error %lu\n", GetLastError());

    ret = GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &size);
    ok(ret, "got error %lu\n", GetLastError());
    orig_type = type;
    ret = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    ok(ret, "got error %lu\n", GetLastError());
    ret = GetTokenInformation(token, TokenLinkedToken, &linked, sizeof(linked), &size);
    if (!ret && GetLastError() == ERROR_NO_SUCH_LOGON_SESSION) /* fails on w2008s64 */
    {
        win_skip("Failed to get linked token.\n");
        CloseHandle(token);
        return;
    }
    ok(ret, "got error %lu\n", GetLastError());

    if (type == TokenElevationTypeDefault)
    {
        ok(elevation.TokenIsElevated == FALSE, "got elevation %#lx\n", elevation.TokenIsElevated);
        ok(!linked.LinkedToken, "expected no linked token\n");
    }
    else if (type == TokenElevationTypeLimited)
    {
        ok(elevation.TokenIsElevated == FALSE, "got elevation %#lx\n", elevation.TokenIsElevated);
        ok(!!linked.LinkedToken, "expected a linked token\n");

        TEST_GRANTED_ACCESS(linked.LinkedToken, TOKEN_ALL_ACCESS);
        ret = GetTokenInformation(linked.LinkedToken, TokenElevationType, &type, sizeof(type), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(type == TokenElevationTypeFull, "got type %#lx\n", type);
        ret = GetTokenInformation(linked.LinkedToken, TokenElevation, &elevation, sizeof(elevation), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(elevation.TokenIsElevated == TRUE, "got elevation %#lx\n", elevation.TokenIsElevated);
        ret = GetTokenInformation(linked.LinkedToken, TokenType, &type, sizeof(type), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(type == TokenImpersonation, "got type %#lx\n", type);
        ret = GetTokenInformation(linked.LinkedToken, TokenImpersonationLevel, &type, sizeof(type), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(type == SecurityIdentification, "got impersonation level %#lx\n", type);

        /* Asking for the linked token again gives us a different token. */
        ret = GetTokenInformation(token, TokenLinkedToken, &linked2, sizeof(linked2), &size);
        ok(ret, "got error %lu\n", GetLastError());

        ret = GetTokenInformation(linked2.LinkedToken, TokenElevationType, &type, sizeof(type), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(type == TokenElevationTypeFull, "got type %#lx\n", type);
        ret = GetTokenInformation(linked2.LinkedToken, TokenElevation, &elevation, sizeof(elevation), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(elevation.TokenIsElevated == TRUE, "got elevation %#lx\n", elevation.TokenIsElevated);

        check_different_token(linked.LinkedToken, linked2.LinkedToken);

        CloseHandle(linked2.LinkedToken);

        /* Asking for the linked token's linked token gives us a new limited token. */
        ret = GetTokenInformation(linked.LinkedToken, TokenLinkedToken, &linked2, sizeof(linked2), &size);
        ok(ret, "got error %lu\n", GetLastError());

        ret = GetTokenInformation(linked2.LinkedToken, TokenElevationType, &type, sizeof(type), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(type == TokenElevationTypeLimited, "got type %#lx\n", type);
        ret = GetTokenInformation(linked2.LinkedToken, TokenElevation, &elevation, sizeof(elevation), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(elevation.TokenIsElevated == FALSE, "got elevation %#lx\n", elevation.TokenIsElevated);

        check_different_token(token, linked2.LinkedToken);

        CloseHandle(linked2.LinkedToken);

        CloseHandle(linked.LinkedToken);

        type = TokenElevationTypeLimited;
        ret = SetTokenInformation(token, TokenElevationType, &type, sizeof(type));
        ok(!ret, "expected failure\n");
        todo_wine ok(GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError());

        elevation.TokenIsElevated = FALSE;
        ret = SetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation));
        ok(!ret, "expected failure\n");
        todo_wine ok(GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError());
    }
    else
    {
        ok(elevation.TokenIsElevated == TRUE, "got elevation %#lx\n", elevation.TokenIsElevated);
        ok(!!linked.LinkedToken, "expected a linked token\n");

        TEST_GRANTED_ACCESS(linked.LinkedToken, TOKEN_ALL_ACCESS);
        ret = GetTokenInformation(linked.LinkedToken, TokenElevationType, &type, sizeof(type), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(type == TokenElevationTypeLimited, "got type %#lx\n", type);
        ret = GetTokenInformation(linked.LinkedToken, TokenElevation, &elevation, sizeof(elevation), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(elevation.TokenIsElevated == FALSE, "got elevation %#lx\n", elevation.TokenIsElevated);
        ret = GetTokenInformation(linked.LinkedToken, TokenType, &type, sizeof(type), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(type == TokenImpersonation, "got type %#lx\n", type);
        ret = GetTokenInformation(linked.LinkedToken, TokenImpersonationLevel, &type, sizeof(type), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(type == SecurityIdentification, "got impersonation level %#lx\n", type);

        /* Asking for the linked token again gives us a different token. */
        ret = GetTokenInformation(token, TokenLinkedToken, &linked2, sizeof(linked2), &size);
        ok(ret, "got error %lu\n", GetLastError());

        ret = GetTokenInformation(linked2.LinkedToken, TokenElevationType, &type, sizeof(type), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(type == TokenElevationTypeLimited, "got type %#lx\n", type);
        ret = GetTokenInformation(linked2.LinkedToken, TokenElevation, &elevation, sizeof(elevation), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(elevation.TokenIsElevated == FALSE, "got elevation %#lx\n", elevation.TokenIsElevated);

        check_different_token(linked.LinkedToken, linked2.LinkedToken);

        CloseHandle(linked2.LinkedToken);

        /* Asking for the linked token's linked token gives us a new elevated token. */
        ret = GetTokenInformation(linked.LinkedToken, TokenLinkedToken, &linked2, sizeof(linked2), &size);
        ok(ret, "got error %lu\n", GetLastError());

        ret = GetTokenInformation(linked2.LinkedToken, TokenElevationType, &type, sizeof(type), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(type == TokenElevationTypeFull, "got type %#lx\n", type);
        ret = GetTokenInformation(linked2.LinkedToken, TokenElevation, &elevation, sizeof(elevation), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(elevation.TokenIsElevated == TRUE, "got elevation %#lx\n", elevation.TokenIsElevated);

        check_different_token(token, linked2.LinkedToken);

        CloseHandle(linked2.LinkedToken);

        CloseHandle(linked.LinkedToken);

        type = TokenElevationTypeLimited;
        ret = SetTokenInformation(token, TokenElevationType, &type, sizeof(type));
        ok(!ret, "expected failure\n");
        todo_wine ok(GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError());

        elevation.TokenIsElevated = FALSE;
        ret = SetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation));
        ok(!ret, "expected failure\n");
        todo_wine ok(GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError());
    }

    ret = DuplicateTokenEx(token, TOKEN_ALL_ACCESS, NULL, SecurityAnonymous, TokenPrimary, &token2);
    ok(ret, "got error %lu\n", GetLastError());
    ret = GetTokenInformation(token2, TokenElevationType, &type, sizeof(type), &size);
    ok(ret, "got error %lu\n", GetLastError());
    ok(type == orig_type, "expected same type\n");
    ret = GetTokenInformation(token2, TokenElevation, &elevation, sizeof(elevation), &size);
    ok(ret, "got error %lu\n", GetLastError());
    ok(elevation.TokenIsElevated == (type == TokenElevationTypeFull), "got elevation %#lx\n", elevation.TokenIsElevated);
    ret = GetTokenInformation(token2, TokenLinkedToken, &linked, sizeof(linked), &size);
    ok(ret, "got error %lu\n", GetLastError());
    if (type == TokenElevationTypeDefault)
    {
        ok(!linked.LinkedToken, "expected no linked token\n");
        ret = GetTokenInformation(linked.LinkedToken, TokenType, &type, sizeof(type), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(type == TokenImpersonation, "got type %#lx\n", type);
        ret = GetTokenInformation(linked.LinkedToken, TokenImpersonationLevel, &type, sizeof(type), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(type == SecurityIdentification, "got impersonation level %#lx\n", type);
        CloseHandle(linked.LinkedToken);
    }
    else
        ok(!!linked.LinkedToken, "expected a linked token\n");
    CloseHandle(token2);

    ret = CreateRestrictedToken(token, 0, 0, NULL, 0, NULL, 0, NULL, &token2);
    ok(ret, "got error %lu\n", GetLastError());
    ret = GetTokenInformation(token2, TokenElevationType, &type, sizeof(type), &size);
    ok(ret, "got error %lu\n", GetLastError());
    ok(type == orig_type, "expected same type\n");
    ret = GetTokenInformation(token2, TokenElevation, &elevation, sizeof(elevation), &size);
    ok(ret, "got error %lu\n", GetLastError());
    ok(elevation.TokenIsElevated == (type == TokenElevationTypeFull), "got elevation %#lx\n", elevation.TokenIsElevated);
    ret = GetTokenInformation(token2, TokenLinkedToken, &linked, sizeof(linked), &size);
    ok(ret, "got error %lu\n", GetLastError());
    if (type == TokenElevationTypeDefault)
        ok(!linked.LinkedToken, "expected no linked token\n");
    else
        ok(!!linked.LinkedToken, "expected a linked token\n");
    CloseHandle(linked.LinkedToken);
    CloseHandle(token2);

    if (type != TokenElevationTypeDefault)
    {
        char prev_privs_buffer[128], acl_buffer[256], prev_acl_buffer[256];
        TOKEN_PRIVILEGES privs, *prev_privs = (TOKEN_PRIVILEGES *)prev_privs_buffer;
        TOKEN_DEFAULT_DACL *prev_acl = (TOKEN_DEFAULT_DACL *)prev_acl_buffer;
        TOKEN_DEFAULT_DACL *ret_acl = (TOKEN_DEFAULT_DACL *)acl_buffer;
        TOKEN_DEFAULT_DACL default_acl;
        PRIVILEGE_SET priv_set;
        BOOL ret, is_member;
        DWORD size;
        ACL acl;

        /* Linked tokens do not preserve privilege modifications. */

        privs.PrivilegeCount = 1;
        ret = LookupPrivilegeValueA(NULL, "SeChangeNotifyPrivilege", &privs.Privileges[0].Luid);
        ok(ret, "got error %lu\n", GetLastError());
        privs.Privileges[0].Attributes = SE_PRIVILEGE_REMOVED;
        ret = AdjustTokenPrivileges(token, FALSE, &privs, sizeof(prev_privs_buffer), prev_privs, &size);
        ok(ret, "got error %lu\n", GetLastError());

        priv_set.PrivilegeCount = 1;
        priv_set.Control = 0;
        priv_set.Privilege[0] = privs.Privileges[0];
        ret = PrivilegeCheck(token, &priv_set, &is_member);
        ok(ret, "got error %lu\n", GetLastError());
        ok(!is_member, "not a member\n");

        ret = GetTokenInformation(token, TokenLinkedToken, &linked, sizeof(linked), &size);
        ok(ret, "got error %lu\n", GetLastError());

        ret = PrivilegeCheck(linked.LinkedToken, &priv_set, &is_member);
        ok(ret, "got error %lu\n", GetLastError());
        ok(is_member, "not a member\n");

        CloseHandle(linked.LinkedToken);

        ret = AdjustTokenPrivileges(token, FALSE, prev_privs, 0, NULL, NULL);
        ok(ret, "got error %lu\n", GetLastError());

        /* Linked tokens do not preserve default DACL modifications. */

        ret = GetTokenInformation(token, TokenDefaultDacl, prev_acl, sizeof(prev_acl_buffer), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(prev_acl->DefaultDacl->AceCount, "expected non-empty default DACL\n");

        InitializeAcl(&acl, sizeof(acl), ACL_REVISION);
        default_acl.DefaultDacl = &acl;
        ret = SetTokenInformation(token, TokenDefaultDacl, &default_acl, sizeof(default_acl));
        ok(ret, "got error %lu\n", GetLastError());

        ret = GetTokenInformation(token, TokenDefaultDacl, ret_acl, sizeof(acl_buffer), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(!ret_acl->DefaultDacl->AceCount, "expected empty default DACL\n");

        ret = GetTokenInformation(token, TokenLinkedToken, &linked, sizeof(linked), &size);
        ok(ret, "got error %lu\n", GetLastError());

        ret = GetTokenInformation(linked.LinkedToken, TokenDefaultDacl, ret_acl, sizeof(acl_buffer), &size);
        ok(ret, "got error %lu\n", GetLastError());
        ok(ret_acl->DefaultDacl->AceCount, "expected non-empty default DACL\n");

        CloseHandle(linked.LinkedToken);

        ret = SetTokenInformation(token, TokenDefaultDacl, prev_acl, sizeof(*prev_acl));
        ok(ret, "got error %lu\n", GetLastError());
    }

    CloseHandle(token);
}

static void test_admin_elevation(void)
{
    /* Tokens with elevation type TokenElevationTypeDefault should still come
       back as elevated from a TokenElevation query if they belong to the admin
       group. The owner of the desktop window should have such a token. */
    DWORD tid, pid;
    HANDLE hproc, htok;
    TOKEN_ELEVATION_TYPE elevation_type;
    TOKEN_ELEVATION elevation;
    DWORD size;
    BOOL ret;

    tid = GetWindowThreadProcessId(GetDesktopWindow(), &pid);
    ok(tid, "got error %lu\n", GetLastError());

    hproc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hproc)
    {
        skip("could not open process, error %lu\n", GetLastError());
        return;
    }

    ret = OpenProcessToken(hproc, TOKEN_READ, &htok);
    ok(ret, "got error %lu\n", GetLastError());

    CloseHandle(hproc);

    size = sizeof(elevation_type);
    ret = GetTokenInformation(htok, TokenElevationType, &elevation_type, size, &size);
    ok(ret, "got error %lu\n", GetLastError());
    ok(elevation_type == TokenElevationTypeDefault, "unexpected elevation type %d\n", elevation_type);

    size = sizeof(elevation);
    ret = GetTokenInformation(htok, TokenElevation, &elevation, size, &size);
    ok(ret, "got error %lu\n", GetLastError());
    ok(elevation.TokenIsElevated, "expected token to be elevated\n");

    CloseHandle(htok);
}

static void test_group_as_file_owner(void)
{
    char sd_buffer[200], sid_buffer[100];
    SECURITY_DESCRIPTOR *sd = (SECURITY_DESCRIPTOR *)sd_buffer;
    char temp_path[MAX_PATH], path[MAX_PATH];
    SID *admin_sid = (SID *)sid_buffer;
    BOOL ret, present, defaulted;
    SECURITY_DESCRIPTOR new_sd;
    HANDLE file;
    DWORD size;
    ACL *dacl;

    /* The EA Origin client sets the SD owner of a directory to Administrators,
     * while using the default DACL, and subsequently tries to create
     * subdirectories. */

    size = sizeof(sid_buffer);
    CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, admin_sid, &size);

    ret = CheckTokenMembership(NULL, admin_sid, &present);
    ok(ret, "got error %lu\n", GetLastError());
    if (!present)
    {
        skip("user is not an administrator\n");
        return;
    }

    GetTempPathA(ARRAY_SIZE(temp_path), temp_path);
    sprintf(path, "%s\\testdir", temp_path);

    ret = CreateDirectoryA(path, NULL);
    ok(ret, "got error %lu\n", GetLastError());

    file = CreateFileA(path, FILE_ALL_ACCESS, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    ok(file != INVALID_HANDLE_VALUE, "got error %lu\n", GetLastError());

    ret = GetKernelObjectSecurity(file, DACL_SECURITY_INFORMATION, sd_buffer, sizeof(sd_buffer), &size);
    ok(ret, "got error %lu\n", GetLastError());
    ret = GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
    ok(ret, "got error %lu\n", GetLastError());

    InitializeSecurityDescriptor(&new_sd, SECURITY_DESCRIPTOR_REVISION);

    ret = SetSecurityDescriptorOwner(&new_sd, admin_sid, FALSE);
    ok(ret, "got error %lu\n", GetLastError());

    ret = GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
    ok(ret, "got error %lu\n", GetLastError());

    ret = SetSecurityDescriptorDacl(&new_sd, present, dacl, defaulted);
    ok(ret, "got error %lu\n", GetLastError());

    ret = SetKernelObjectSecurity(file, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &new_sd);
    ok(ret, "got error %lu\n", GetLastError());

    CloseHandle(file);

    sprintf(path, "%s\\testdir\\subdir", temp_path);
    ret = CreateDirectoryA(path, NULL);
    ok(ret, "got error %lu\n", GetLastError());

    ret = RemoveDirectoryA(path);
    ok(ret, "got error %lu\n", GetLastError());
    sprintf(path, "%s\\testdir", temp_path);
    ret = RemoveDirectoryA(path);
    ok(ret, "got error %lu\n", GetLastError());
}

static void test_IsValidSecurityDescriptor(void)
{
    SECURITY_DESCRIPTOR *sd;
    BOOL ret;

    SetLastError(0xdeadbeef);
    ret = IsValidSecurityDescriptor(NULL);
    ok(!ret, "Unexpected return value %d.\n", ret);
    ok(GetLastError() == ERROR_INVALID_SECURITY_DESCR, "Unexpected error %ld.\n", GetLastError());

    sd = calloc(1, SECURITY_DESCRIPTOR_MIN_LENGTH);

    SetLastError(0xdeadbeef);
    ret = IsValidSecurityDescriptor(sd);
    ok(!ret, "Unexpected return value %d.\n", ret);
    ok(GetLastError() == ERROR_INVALID_SECURITY_DESCR, "Unexpected error %ld.\n", GetLastError());

    ret = InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
    ok(ret, "Unexpected return value %d, error %ld.\n", ret, GetLastError());

    SetLastError(0xdeadbeef);
    ret = IsValidSecurityDescriptor(sd);
    ok(ret, "Unexpected return value %d.\n", ret);
    ok(GetLastError() == 0xdeadbeef, "Unexpected error %ld.\n", GetLastError());

    free(sd);
}

static void test_window_security(void)
{
    PSECURITY_DESCRIPTOR sd;
    BOOL present, defaulted;
    HDESK desktop;
    DWORD ret;
    ACL *dacl;

    desktop = GetThreadDesktop(GetCurrentThreadId());

    ret = GetSecurityInfo(desktop, SE_WINDOW_OBJECT,
            DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &sd);
    ok(!ret, "got error %lu\n", ret);

    ret = GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
    ok(ret == TRUE, "got error %lu\n", GetLastError());
    todo_wine ok(present == TRUE, "got present %d\n", present);
    ok(defaulted == FALSE, "got defaulted %d\n", defaulted);

    LocalFree(sd);
}

#ifdef __REACTOS__
static PSECURITY_DESCRIPTOR query_registry_security(HKEY key)
{
    PSECURITY_DESCRIPTOR descriptor;
    DWORD size = 0;
    LONG ret;

    ret = RegGetKeySecurity(key, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                           DACL_SECURITY_INFORMATION, NULL, &size);
    ok(ret == ERROR_INSUFFICIENT_BUFFER, "Registry security size returned %ld.\n", ret);
    if (ret != ERROR_INSUFFICIENT_BUFFER) return NULL;
    descriptor = malloc(size);
    if (!descriptor) return NULL;
    ret = RegGetKeySecurity(key, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                           DACL_SECURITY_INFORMATION, descriptor, &size);
    ok(!ret, "Registry security query returned %ld.\n", ret);
    if (ret)
    {
        free(descriptor);
        return NULL;
    }
    return descriptor;
}

static void test_registry_security_persistence(BOOL unicode, REGSAM view)
{
    static const char path[] = "Software\\Wine\\TestSecurityPersistence\\Parent";
    static const WCHAR pathW[] = L"Software\\Wine\\TestSecurityPersistence\\Parent";
    PSECURITY_DESCRIPTOR initial = NULL, queried = NULL;
    SECURITY_DESCRIPTOR empty_sd;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    SECURITY_ATTRIBUTES attributes;
    ACCESS_ALLOWED_ACE *ace;
    HKEY parent = NULL, child = NULL, sibling = NULL, reopened = NULL;
    BOOL present, defaulted, result;
    DWORD revision, disposition, flags = 0;
    ACL empty_acl, *dacl;
    LONG ret;

    result = ConvertStringSecurityDescriptorToSecurityDescriptorA(
            "D:P(A;CIID;KA;;;WD)", SDDL_REVISION_1, &initial, NULL);
    ok(result, "Security descriptor creation failed: %lu.\n", GetLastError());
    if (!result) return;
    winetest_push_context("Registry %s, view %#lx", unicode ? "W" : "A", view);
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = initial;
    attributes.bInheritHandle = TRUE;
    if (unicode)
        ret = RegCreateKeyExW(HKEY_CURRENT_USER, pathW, 0, NULL, REG_OPTION_VOLATILE,
                              KEY_ALL_ACCESS | view, &attributes, &parent, &disposition);
    else
        ret = RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, NULL, REG_OPTION_VOLATILE,
                              KEY_ALL_ACCESS | view, &attributes, &parent, &disposition);
    ok(!ret, "Parent creation returned %ld.\n", ret);
    if (ret) goto done;
    ok(disposition == REG_CREATED_NEW_KEY, "Parent disposition %lu.\n", disposition);
    result = GetHandleInformation(parent, &flags);
    ok(result && (flags & HANDLE_FLAG_INHERIT), "Created parent handle flags %#lx.\n", flags);
    queried = query_registry_security(parent);
    if (!queried) goto done;
    result = GetSecurityDescriptorControl(queried, &control, &revision);
    ok(result && (control & SE_DACL_PROTECTED), "Protected parent control %#x.\n", control);
    result = GetSecurityDescriptorDacl(queried, &present, &dacl, &defaulted);
    ok(result && present && dacl && dacl->AceCount == 1,
       "Parent DACL was not retained.\n");
    if (result && present && dacl && dacl->AceCount == 1)
    {
        result = GetAce(dacl, 0, (void **)&ace);
        ok(result && ace->Mask == KEY_ALL_ACCESS &&
           ace->Header.AceFlags == (CONTAINER_INHERIT_ACE | INHERITED_ACE),
           "Parent ACE query %d, mask %#lx, flags %#x.\n", result,
           result ? ace->Mask : 0, result ? ace->Header.AceFlags : 0);
    }
    free(queried);
    queried = NULL;

    InitializeSecurityDescriptor(&empty_sd, SECURITY_DESCRIPTOR_REVISION);
    InitializeAcl(&empty_acl, sizeof(empty_acl), ACL_REVISION);
    SetSecurityDescriptorDacl(&empty_sd, TRUE, &empty_acl, FALSE);
    SetSecurityDescriptorControl(&empty_sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    attributes.lpSecurityDescriptor = &empty_sd;
    if (unicode)
        ret = RegCreateKeyExW(HKEY_CURRENT_USER, pathW, 0, NULL, REG_OPTION_VOLATILE,
                              KEY_ALL_ACCESS | view, &attributes, &reopened, &disposition);
    else
        ret = RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, NULL, REG_OPTION_VOLATILE,
                              KEY_ALL_ACCESS | view, &attributes, &reopened, &disposition);
    ok(!ret, "Existing parent creation returned %ld.\n", ret);
    if (ret) goto done;
    ok(disposition == REG_OPENED_EXISTING_KEY, "Existing parent disposition %lu.\n", disposition);
    result = GetHandleInformation(reopened, &flags);
    ok(result && (flags & HANDLE_FLAG_INHERIT), "Existing parent handle flags %#lx.\n", flags);
    queried = query_registry_security(reopened);
    if (!queried) goto done;
    result = GetSecurityDescriptorDacl(queried, &present, &dacl, &defaulted);
    ok(result && present && dacl && dacl->AceCount == 1,
       "Existing parent DACL was replaced.\n");
    free(queried);
    queried = NULL;
    RegCloseKey(reopened);
    reopened = NULL;
    if (unicode)
        ret = RegCreateKeyExW(HKEY_CURRENT_USER, pathW, 0, NULL, REG_OPTION_VOLATILE,
                              KEY_ALL_ACCESS | view, NULL, &reopened, &disposition);
    else
        ret = RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, NULL, REG_OPTION_VOLATILE,
                              KEY_ALL_ACCESS | view, NULL, &reopened, &disposition);
    ok(!ret, "Noninheritable parent open returned %ld.\n", ret);
    if (ret) goto done;
    result = GetHandleInformation(reopened, &flags);
    ok(result && !(flags & HANDLE_FLAG_INHERIT), "Noninheritable parent handle flags %#lx.\n", flags);
    RegCloseKey(reopened);
    reopened = NULL;
    result = GetHandleInformation(parent, &flags);
    ok(result && (flags & HANDLE_FLAG_INHERIT), "Original parent handle flags changed to %#lx.\n", flags);

    ret = RegCreateKeyExA(parent, "Child", 0, NULL, REG_OPTION_VOLATILE,
                          KEY_ALL_ACCESS, NULL, &child, &disposition);
    ok(!ret, "Child creation returned %ld.\n", ret);
    if (ret) goto done;
    ret = RegCreateKeyExA(parent, "Sibling", 0, NULL, REG_OPTION_VOLATILE,
                          KEY_ALL_ACCESS, NULL, &sibling, &disposition);
    ok(!ret, "Sibling creation returned %ld.\n", ret);
    if (ret) goto done;
    queried = query_registry_security(child);
    if (!queried) goto done;
    result = GetSecurityDescriptorDacl(queried, &present, &dacl, &defaulted);
    ok(result && present && dacl && dacl->AceCount == 1,
       "Child did not inherit parent DACL.\n");
    if (result && present && dacl && dacl->AceCount == 1)
    {
        result = GetAce(dacl, 0, (void **)&ace);
        ok(result && ace->Mask == KEY_ALL_ACCESS &&
           ace->Header.AceFlags == CONTAINER_INHERIT_ACE,
           "Child ACE query %d, mask %#lx, flags %#x.\n", result,
           result ? ace->Mask : 0, result ? ace->Header.AceFlags : 0);
    }
    free(queried);
    queried = NULL;

    ret = RegSetKeySecurity(child, DACL_SECURITY_INFORMATION, &empty_sd);
    ok(!ret, "Empty DACL update returned %ld.\n", ret);
    ret = RegOpenKeyExA(parent, "Child", 0, KEY_QUERY_VALUE, &reopened);
    ok(ret == ERROR_ACCESS_DENIED, "Denied child reopen returned %ld.\n", ret);
    if (!ret) RegCloseKey(reopened);
    reopened = NULL;
    queried = query_registry_security(child);
    if (queried)
    {
        result = GetSecurityDescriptorDacl(queried, &present, &dacl, &defaulted);
        ok(result && present && dacl && !dacl->AceCount, "Empty DACL was not retained.\n");
        free(queried);
        queried = NULL;
    }
    ret = RegOpenKeyExA(parent, "Sibling", 0, KEY_QUERY_VALUE, &reopened);
    ok(!ret, "Sibling access changed with child DACL: %ld.\n", ret);
    if (!ret) RegCloseKey(reopened);
    reopened = NULL;
    ret = RegSetKeySecurity(child, DACL_SECURITY_INFORMATION, initial);
    ok(!ret, "DACL restore returned %ld.\n", ret);
    RegCloseKey(child);
    child = NULL;
    ret = RegOpenKeyExA(parent, "Child", 0, KEY_QUERY_VALUE, &reopened);
    ok(!ret, "Restored child reopen returned %ld.\n", ret);

done:
    if (reopened) RegCloseKey(reopened);
    if (sibling) RegCloseKey(sibling);
    if (child)
    {
        RegSetKeySecurity(child, DACL_SECURITY_INFORMATION, initial);
        RegCloseKey(child);
    }
    if (parent)
    {
        RegSetKeySecurity(parent, DACL_SECURITY_INFORMATION, initial);
        RegDeleteKeyA(parent, "Child");
        RegDeleteKeyA(parent, "Sibling");
        RegCloseKey(parent);
        RegDeleteKeyA(HKEY_CURRENT_USER, path);
    }
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Wine\\TestSecurityPersistence");
    free(queried);
    LocalFree(initial);
    winetest_pop_context();
}

static void test_registry_acl_propagation(void)
{
    static const WCHAR path[] = L"Software\\Wine\\TestRegistryAclPropagation";
    static WCHAR named_path[] = L"CURRENT_USER\\Software\\Wine\\TestRegistryAclPropagation";
    static const WCHAR *names[] = {L"", L"Existing", L"Nested", L"Nested\\Grandchild",
                                  L"Protected", L"Protected\\Grandchild", L"NewPublic",
                                  L"NewRaw", L"RawRestored"};
    const REGSAM access = READ_CONTROL | WRITE_DAC | DELETE | KEY_CREATE_SUB_KEY | KEY_ENUMERATE_SUB_KEYS;
    SID everyone = {SID_REVISION, 1, {SECURITY_WORLD_SID_AUTHORITY}, {SECURITY_WORLD_RID}};
    PSECURITY_DESCRIPTOR initial = NULL, denied = NULL, queried = NULL, before[ARRAY_SIZE(names)] = {0};
    HANDLE source = NULL, token = NULL, previous = NULL;
    HKEY keys[ARRAY_SIZE(names)] = {0}, reopened = NULL;
    BOOL created[ARRAY_SIZE(names)] = {0};
    TOKEN_PRIVILEGES *privileges = NULL;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, FALSE};
    SECURITY_DESCRIPTOR_CONTROL control, expected_control;
    PSID owner, old_owner, group, old_group;
    ACCESS_ALLOWED_ACE *ace;
    ACL *initial_acl, *denied_acl, *dacl, *old_dacl;
    WCHAR full_path[256];
    DWORD size, capacity, error, disposition, revision, phase, i, j, value;
    BOOL result, present, defaulted, old_present, old_defaulted, impersonating = FALSE, valid;
    BOOL expect_denied, child_auto;
    LONG ret;

    winetest_push_context("Registry existing descendant DACL propagation");
    result = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_IMPERSONATE, TRUE, &previous);
    error = GetLastError();
    ok(result || error == ERROR_NO_TOKEN, "Previous thread token query returned %d, error %lu.\n", result, error);
    if (!result && error != ERROR_NO_TOKEN) goto done;
    result = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &source);
    ok(result, "Source token open failed: %lu.\n", GetLastError());
    if (!result) goto done;
    result = DuplicateTokenEx(source, TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES | TOKEN_IMPERSONATE,
                              NULL, SecurityImpersonation, TokenImpersonation, &token);
    ok(result, "Private token duplication failed: %lu.\n", GetLastError());
    if (!result) goto done;
    result = AdjustTokenPrivileges(token, TRUE, NULL, 0, NULL, NULL);
    ok(result, "Private privilege disable failed: %lu.\n", GetLastError());
    if (!result) goto done;
    size = 0;
    result = GetTokenInformation(token, TokenPrivileges, NULL, 0, &size);
    error = GetLastError();
    ok(!result && error == ERROR_INSUFFICIENT_BUFFER && size >= FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges),
       "Privilege sizing returned %d, error %lu, size %lu.\n", result, error, size);
    if (result || error != ERROR_INSUFFICIENT_BUFFER || size < FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) goto done;
    capacity = size;
    privileges = malloc(capacity);
    ok(!!privileges, "Privilege buffer allocation failed.\n");
    if (!privileges) goto done;
    result = GetTokenInformation(token, TokenPrivileges, privileges, capacity, &size);
    ok(result, "Private privilege query failed: %lu.\n", GetLastError());
    if (!result) goto done;
    valid = size >= FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges) && size <= capacity;
    if (valid)
        valid = privileges->PrivilegeCount <= (size - FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) / sizeof(LUID_AND_ATTRIBUTES);
    ok(valid, "Private privilege buffer size/count is invalid.\n");
    if (!valid) goto done;
    for (i = 0; i < privileges->PrivilegeCount; ++i)
    {
        ok(!(privileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED), "Privilege %lu remains enabled.\n", i);
        if (privileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) goto done;
    }
    result = SetThreadToken(NULL, token);
    ok(result, "Private token impersonation failed: %lu.\n", GetLastError());
    if (!result) goto done;
    impersonating = TRUE;
    result = ConvertStringSecurityDescriptorToSecurityDescriptorA("D:P(A;CI;KA;;;WD)",
                                                                 SDDL_REVISION_1, &initial, NULL);
    ok(result, "Initial DACL construction failed: %lu.\n", GetLastError());
    if (!result) goto done;
    result = ConvertStringSecurityDescriptorToSecurityDescriptorA("D:P(D;CI;0x2;;;WD)(A;CI;KA;;;WD)",
                                                                 SDDL_REVISION_1, &denied, NULL);
    ok(result, "Denied DACL construction failed: %lu.\n", GetLastError());
    if (!result) goto done;
    result = GetSecurityDescriptorDacl(initial, &present, &initial_acl, &defaulted);
    ok(result && present && initial_acl, "Initial DACL is missing.\n");
    if (!result || !present || !initial_acl) goto done;
    result = GetSecurityDescriptorDacl(denied, &present, &denied_acl, &defaulted);
    ok(result && present && denied_acl, "Denied DACL is missing.\n");
    if (!result || !present || !denied_acl) goto done;
    attributes.lpSecurityDescriptor = initial;
    ret = RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, REG_OPTION_VOLATILE,
                          access, &attributes, &keys[0], &disposition);
    ok(!ret, "Root creation returned %ld.\n", ret);
    if (ret) goto done;
    created[0] = disposition == REG_CREATED_NEW_KEY;
    ok(created[0], "Root disposition %lu.\n", disposition);
    if (!created[0]) goto done;
    for (i = 1; i < 6; ++i)
    {
        attributes.lpSecurityDescriptor = i == 4 ? initial : NULL;
        ret = RegCreateKeyExW(keys[0], names[i], 0, NULL, REG_OPTION_VOLATILE,
                              access, i == 4 ? &attributes : NULL, &keys[i], &disposition);
        ok(!ret, "Child %lu creation returned %ld.\n", i, ret);
        if (ret) goto done;
        created[i] = disposition == REG_CREATED_NEW_KEY;
        ok(created[i], "Child %lu disposition %lu.\n", i, disposition);
        if (!created[i]) goto done;
    }
    for (i = 0; i < 6; ++i)
    {
        before[i] = query_registry_security(keys[i]);
        ok(!!before[i], "Initial descriptor %lu could not be queried.\n", i);
        if (!before[i]) goto done;
    }
    for (phase = 0; phase < 5; ++phase)
    {
        winetest_push_context("phase %lu", phase);
        if (phase == 1)
            ret = SetNamedSecurityInfoW(named_path, SE_REGISTRY_KEY,
                                        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                        NULL, NULL, denied_acl, NULL);
        else if (phase == 2)
            ret = SetSecurityInfo(keys[0], SE_REGISTRY_KEY,
                                  DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                  NULL, NULL, initial_acl, NULL);
        else if (phase == 3)
            ret = RegSetKeySecurity(keys[0], DACL_SECURITY_INFORMATION, denied);
        else if (phase == 4)
            ret = RegSetKeySecurity(keys[0], DACL_SECURITY_INFORMATION, initial);
        else ret = ERROR_SUCCESS;
        ok(!ret, "Parent DACL update returned %ld.\n", ret);
        if (ret)
        {
            winetest_pop_context();
            goto done;
        }
        if (phase == 1 || phase == 3 || phase == 4)
        {
            i = phase == 1 ? 6 : phase == 3 ? 7 : 8;
            ret = RegCreateKeyExW(keys[0], names[i], 0, NULL, REG_OPTION_VOLATILE,
                                  access, NULL, &keys[i], &disposition);
            ok(!ret, "New child %lu creation returned %ld.\n", i, ret);
            if (ret)
            {
                winetest_pop_context();
                goto done;
            }
            created[i] = disposition == REG_CREATED_NEW_KEY;
            ok(created[i], "New child %lu disposition %lu.\n", i, disposition);
            before[i] = query_registry_security(keys[i]);
            ok(!!before[i], "New child %lu descriptor query failed.\n", i);
            if (!created[i] || !before[i])
            {
                winetest_pop_context();
                goto done;
            }
        }
        for (i = 0; i < ARRAY_SIZE(keys); ++i)
        {
            if (!keys[i]) continue;
            winetest_push_context("key %lu", i);
            expect_denied = (phase == 1 && i != 4 && i != 5) ||
                            (phase == 3 && (i == 0 || i == 7)) || (phase == 4 && i == 7);
            child_auto = phase && i && i < 7 && i != 4 && i != 5;
            expected_control = SE_SELF_RELATIVE | SE_DACL_PRESENT;
            if (!i || i == 4) expected_control |= SE_DACL_PROTECTED;
            if (child_auto || (!i && (phase == 1 || phase == 2)))
                expected_control |= SE_DACL_AUTO_INHERITED;
            queried = query_registry_security(keys[i]);
            ok(!!queried, "Retained descriptor query failed.\n");
            if (queried)
            {
                dacl = NULL;
                present = FALSE;
                result = GetSecurityDescriptorDacl(queried, &present, &dacl, &defaulted);
                ok(result && present && dacl && dacl->AceCount == (expect_denied ? 2 : 1),
                   "DACL query %d, present %d, ACL %p, expected %u ACEs.\n",
                   result, present, dacl, expect_denied ? 2 : 1);
                if (result && present && dacl)
                {
                    for (j = 0; j < dacl->AceCount; ++j)
                    {
                        result = GetAce(dacl, j, (void **)&ace);
                        ok(result, "ACE %lu query failed: %lu.\n", j, GetLastError());
                        if (!result) continue;
                        ok(ace->Header.AceType == (expect_denied && !j ? ACCESS_DENIED_ACE_TYPE : ACCESS_ALLOWED_ACE_TYPE),
                           "ACE %lu type %#x.\n", j, ace->Header.AceType);
                        ok(ace->Mask == (expect_denied && !j ? KEY_SET_VALUE : KEY_ALL_ACCESS),
                           "ACE %lu mask %#lx.\n", j, ace->Mask);
                        ok(EqualSid(&ace->SidStart, &everyone), "ACE %lu SID differs from Everyone.\n", j);
                        ok(ace->Header.AceFlags == (CONTAINER_INHERIT_ACE | (child_auto ? INHERITED_ACE : 0)),
                           "ACE %lu flags %#x, expected %#x.\n", j, ace->Header.AceFlags,
                           CONTAINER_INHERIT_ACE | (child_auto ? INHERITED_ACE : 0));
                    }
                    result = GetSecurityDescriptorControl(queried, &control, &revision);
                    ok(result, "Descriptor control query failed.\n");
                    if (result)
                        ok(control == expected_control, "Descriptor control %#x, expected %#x.\n",
                           control, expected_control);
                    if (i == 4 || i == 5)
                    {
                        result = GetSecurityDescriptorDacl(before[i], &old_present, &old_dacl, &old_defaulted);
                        ok(result && old_present && old_dacl && dacl->AclSize == old_dacl->AclSize &&
                           !memcmp(dacl, old_dacl, dacl->AclSize), "Protected branch DACL changed.\n");
                    }
                }
                owner = old_owner = group = old_group = NULL;
                result = GetSecurityDescriptorOwner(queried, &owner, &defaulted) &&
                         GetSecurityDescriptorOwner(before[i], &old_owner, &old_defaulted);
                ok(result && !!owner == !!old_owner && defaulted == old_defaulted,
                   "Owner presence/defaulting changed.\n");
                if (result && owner && old_owner) ok(EqualSid(owner, old_owner), "Owner SID changed.\n");
                result = GetSecurityDescriptorGroup(queried, &group, &defaulted) &&
                         GetSecurityDescriptorGroup(before[i], &old_group, &old_defaulted);
                ok(result && !!group == !!old_group && defaulted == old_defaulted,
                   "Group presence/defaulting changed.\n");
                if (result && group && old_group) ok(EqualSid(group, old_group), "Group SID changed.\n");
                free(queried);
                queried = NULL;
            }
            wcscpy(full_path, path);
            if (i) { wcscat(full_path, L"\\"); wcscat(full_path, names[i]); }
            ret = RegOpenKeyExW(HKEY_CURRENT_USER, full_path, 0, KEY_SET_VALUE, &reopened);
            ok(ret == (expect_denied ? ERROR_ACCESS_DENIED : ERROR_SUCCESS),
               "Fresh KEY_SET_VALUE open returned %ld, expected %ld.\n", ret,
               (LONG)(expect_denied ? ERROR_ACCESS_DENIED : ERROR_SUCCESS));
            if (!ret)
            {
                value = phase;
                ret = RegSetValueExW(reopened, L"Value", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
                ok(!ret, "Actual value write returned %ld.\n", ret);
                RegCloseKey(reopened);
                reopened = NULL;
            }
            ret = RegOpenKeyExW(HKEY_CURRENT_USER, full_path, 0, KEY_QUERY_VALUE | READ_CONTROL, &reopened);
            ok(!ret, "Fresh read/control open returned %ld.\n", ret);
            if (!ret)
            {
                ret = RegQueryInfoKeyW(reopened, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
                ok(!ret, "Actual key information read returned %ld.\n", ret);
                queried = query_registry_security(reopened);
                ok(!!queried, "Fresh descriptor query failed.\n");
                if (queried)
                {
                    result = GetSecurityDescriptorDacl(queried, &present, &dacl, &defaulted);
                    ok(result && present && dacl && dacl->AceCount == (expect_denied ? 2 : 1),
                       "Fresh DACL does not match retained-handle expectation.\n");
                    free(queried);
                    queried = NULL;
                }
                RegCloseKey(reopened);
                reopened = NULL;
            }
            winetest_pop_context();
        }
        winetest_pop_context();
    }

done:
    if (reopened) RegCloseKey(reopened);
    if (created[0])
        for (i = 0; i < ARRAY_SIZE(keys); ++i)
            if (keys[i])
            {
                ret = RegSetKeySecurity(keys[i], DACL_SECURITY_INFORMATION, initial);
                ok(!ret, "Cleanup DACL %lu restore returned %ld.\n", i, ret);
            }
    for (i = ARRAY_SIZE(keys); i-- > 1;)
    {
        if (keys[i]) RegCloseKey(keys[i]);
        if (created[i])
        {
            ret = RegDeleteKeyW(keys[0], names[i]);
            ok(!ret, "Child %lu cleanup returned %ld.\n", i, ret);
        }
    }
    if (keys[0]) RegCloseKey(keys[0]);
    if (created[0])
    {
        ret = RegDeleteKeyW(HKEY_CURRENT_USER, path);
        ok(!ret, "Root cleanup returned %ld.\n", ret);
    }
    if (impersonating)
    {
        result = SetThreadToken(NULL, previous);
        ok(result, "Previous thread token restore failed: %lu.\n", GetLastError());
    }
    if (token) CloseHandle(token);
    if (source) CloseHandle(source);
    if (previous) CloseHandle(previous);
    for (i = 0; i < ARRAY_SIZE(before); ++i) free(before[i]);
    free(queried);
    free(privileges);
    if (denied) LocalFree(denied);
    if (initial) LocalFree(initial);
    winetest_pop_context();
}

static HANDLE registry_matrix_token(HANDLE source, const LUID *security, UINT mode)
{
    TOKEN_PRIVILEGES adjust, *privileges = NULL;
    HANDLE token = NULL;
    DWORD size = 0, capacity, error, i;
    BOOL ret, found = FALSE, valid = FALSE;

    ret = DuplicateTokenEx(source, TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES | TOKEN_IMPERSONATE,
                           NULL, SecurityImpersonation, TokenImpersonation, &token);
    ok(ret, "Matrix token duplication failed: %lu.\n", GetLastError());
    if (!ret) return NULL;
    ret = AdjustTokenPrivileges(token, TRUE, NULL, 0, NULL, NULL);
    ok(ret, "Matrix privilege disable failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    if (mode)
    {
        adjust.PrivilegeCount = 1;
        adjust.Privileges[0].Luid = *security;
        adjust.Privileges[0].Attributes = mode == 1 ? SE_PRIVILEGE_ENABLED : SE_PRIVILEGE_REMOVED;
        SetLastError(0xdeadbeef);
        ret = AdjustTokenPrivileges(token, FALSE, &adjust, 0, NULL, NULL);
        error = GetLastError();
        ok(ret && error == ERROR_SUCCESS, "Security privilege mode %u returned %d, error %lu.\n", mode, ret, error);
        if (!ret || error != ERROR_SUCCESS) goto done;
    }
    ret = GetTokenInformation(token, TokenPrivileges, NULL, 0, &size);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER && size >= FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges),
       "Matrix privilege sizing returned %d, error %lu, size %lu.\n", ret, error, size);
    if (ret || error != ERROR_INSUFFICIENT_BUFFER || size < FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) goto done;
    capacity = size;
    privileges = malloc(capacity);
    ok(!!privileges, "Matrix privilege allocation failed.\n");
    if (!privileges) goto done;
    ret = GetTokenInformation(token, TokenPrivileges, privileges, capacity, &size);
    ok(ret, "Matrix privilege query failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    valid = size <= capacity && size >= FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges);
    if (valid) valid = privileges->PrivilegeCount <= (size - FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) / sizeof(LUID_AND_ATTRIBUTES);
    ok(valid, "Matrix privilege array is invalid.\n");
    if (!valid) goto done;
    for (i = 0; i < privileges->PrivilegeCount; ++i)
    {
        BOOL match = privileges->Privileges[i].Luid.LowPart == security->LowPart &&
                     privileges->Privileges[i].Luid.HighPart == security->HighPart;
        BOOL enabled = !!(privileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED);
        if (match) found = TRUE;
        ok(enabled == (mode == 1 && match), "Matrix mode %u privilege %lu enabled %u.\n", mode, i, enabled);
        if (enabled != (mode == 1 && match)) valid = FALSE;
    }
    ok(found == (mode != 2), "Matrix mode %u security privilege present %u.\n", mode, found);
    valid = valid && found == (mode != 2);
    trace("Registry matrix token mode %u: security present %u, enabled %u, other privileges disabled.\n",
          mode, found, mode == 1);
done:
    free(privileges);
    if (!valid) { CloseHandle(token); token = NULL; }
    return token;
}

static PSECURITY_DESCRIPTOR registry_matrix_descriptor(HKEY key, BOOL sacl)
{
    SECURITY_INFORMATION information = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    PSECURITY_DESCRIPTOR sd;
    DWORD size = 0, capacity;
    BOOL valid;
    LONG ret;

    if (sacl) information |= SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION;
    ret = RegGetKeySecurity(key, information, NULL, &size);
    ok(ret == ERROR_INSUFFICIENT_BUFFER && size >= sizeof(SECURITY_DESCRIPTOR_RELATIVE),
       "Matrix descriptor sizing returned %ld, size %lu.\n", ret, size);
    if (ret != ERROR_INSUFFICIENT_BUFFER || size < sizeof(SECURITY_DESCRIPTOR_RELATIVE)) return NULL;
    capacity = size;
    sd = malloc(capacity);
    ok(!!sd, "Matrix descriptor allocation failed.\n");
    if (!sd) return NULL;
    ret = RegGetKeySecurity(key, information, sd, &size);
    valid = !ret && size >= sizeof(SECURITY_DESCRIPTOR_RELATIVE) && size <= capacity;
    if (valid) valid = RtlValidRelativeSecurityDescriptor(sd, size, information & ~LABEL_SECURITY_INFORMATION);
    ok(valid, "Matrix descriptor query returned %ld, size %lu, capacity %lu.\n", ret, size, capacity);
    if (!valid) { free(sd); return NULL; }
    return sd;
}

static void registry_matrix_acl_text(ACL *acl, BOOL present, char *text, size_t capacity)
{
    SID everyone = {SID_REVISION, 1, {SECURITY_WORLD_SID_AUTHORITY}, {SECURITY_WORLD_RID}};
    SID owner_rights = {SID_REVISION, 1, {SECURITY_CREATOR_SID_AUTHORITY}, {SECURITY_CREATOR_OWNER_RIGHTS_RID}};
    ACCESS_ALLOWED_ACE *ace;
    const char *sid;
    char label[32];
    SID_IDENTIFIER_AUTHORITY mandatory = SECURITY_MANDATORY_LABEL_AUTHORITY;
    DWORD i;
    int length;
    BOOL ret;

    text[0] = 0;
    if (!present || !acl)
    {
        snprintf(text, capacity, "%s", !present ? "absent" : "null");
        return;
    }
    ret = IsValidAcl(acl);
    ok(ret, "Matrix returned invalid ACL.\n");
    if (!ret) return;
    for (i = 0; i < acl->AceCount; ++i)
    {
        ret = GetAce(acl, i, (void **)&ace);
        ok(ret, "Matrix ACE %lu query failed.\n", i);
        if (!ret) return;
        if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE && ace->Header.AceType != ACCESS_DENIED_ACE_TYPE &&
            ace->Header.AceType != SYSTEM_AUDIT_ACE_TYPE && ace->Header.AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE)
            length = snprintf(text, capacity, "%lu:%u/%02x/size%u;", i, ace->Header.AceType,
                              ace->Header.AceFlags, ace->Header.AceSize);
        else
        {
            ret = ace->Header.AceSize >= FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + FIELD_OFFSET(SID, SubAuthority);
            ok(ret, "Matrix ACE %lu is too short for a SID.\n", i);
            if (!ret) return;
            ret = GetSidLengthRequired(*GetSidSubAuthorityCount(&ace->SidStart)) <=
                  ace->Header.AceSize - FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart);
            ok(ret, "Matrix ACE %lu SID exceeds its bounds.\n", i);
            if (!ret) return;
            ret = IsValidSid(&ace->SidStart);
            ok(ret, "Matrix ACE %lu SID is invalid.\n", i);
            if (!ret) return;
            sid = EqualSid(&ace->SidStart, &everyone) ? "WD" :
                  EqualSid(&ace->SidStart, &owner_rights) ? "OW" : "other";
            if (!memcmp(GetSidIdentifierAuthority(&ace->SidStart), &mandatory, sizeof(mandatory)) &&
                *GetSidSubAuthorityCount(&ace->SidStart) == 1)
            {
                snprintf(label, sizeof(label), "IL%lu", *GetSidSubAuthority(&ace->SidStart, 0));
                sid = label;
            }
            ok(strcmp(sid, "other") != 0, "Matrix ACE %lu has an unexpected principal.\n", i);
            length = snprintf(text, capacity, "%lu:%u/%02x/%08lx/%s;", i, ace->Header.AceType,
                              ace->Header.AceFlags, ace->Mask, sid);
        }
        ok(length >= 0 && length < capacity, "Matrix ACL observation truncated.\n");
        if (length < 0 || length >= capacity) return;
        text += length;
        capacity -= length;
    }
}

static void registry_matrix_check_labels(PSECURITY_DESCRIPTOR before, PSECURITY_DESCRIPTOR after)
{
    ACL *old_acl = NULL, *new_acl = NULL;
    ACE_HEADER *old_ace, *new_ace;
    BOOL present, defaulted, ret;
    DWORD i = 0, j = 0;

    ret = GetSecurityDescriptorSacl(before, &present, &old_acl, &defaulted) &&
          GetSecurityDescriptorSacl(after, &present, &new_acl, &defaulted);
    ok(ret, "Matrix label ACL query failed.\n");
    if (!ret) return;
    for (;;)
    {
        old_ace = new_ace = NULL;
        while (old_acl && i < old_acl->AceCount)
        {
            ret = GetAce(old_acl, i++, (void **)&old_ace);
            ok(ret, "Matrix original label ACE %lu query failed.\n", i - 1);
            if (!ret) return;
            if (old_ace->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE) break;
            old_ace = NULL;
        }
        while (new_acl && j < new_acl->AceCount)
        {
            ret = GetAce(new_acl, j++, (void **)&new_ace);
            ok(ret, "Matrix current label ACE %lu query failed.\n", j - 1);
            if (!ret) return;
            if (new_ace->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE) break;
            new_ace = NULL;
        }
        ok(!!old_ace == !!new_ace, "SACL update changed label ACE count.\n");
        if (!old_ace || !new_ace) break;
        ok(old_ace->AceSize == new_ace->AceSize && !memcmp(old_ace, new_ace, old_ace->AceSize),
           "SACL update changed label ACE bytes.\n");
    }
}

struct registry_matrix_result
{
    SECURITY_DESCRIPTOR_CONTROL control;
    const char *dacl, *sacl;
    LONG retained_write, fresh_read, read_value, fresh_write;
};

static void registry_matrix_snapshot(HKEY root, HKEY key, const WCHAR *name, UINT index,
        PSECURITY_DESCRIPTOR before, BOOL sacl, const struct registry_matrix_result *expected)
{
    PSECURITY_DESCRIPTOR sd, fresh_sd = NULL;
    SECURITY_DESCRIPTOR_CONTROL control, old_control;
    DWORD revision, value = index, size;
    ACL *dacl, *audit, *old_dacl;
    PSID owner, old_owner, group, old_group;
    HKEY fresh = NULL;
    BOOL ret, present, defaulted, old_present, old_defaulted;
    LONG retained, fresh_read, fresh_write, operation = ERROR_INVALID_HANDLE;
    char dacl_text[1024], sacl_text[1024];

    winetest_push_context("key %u", index);
    sd = registry_matrix_descriptor(key, sacl);
    if (!sd) goto done;
    ret = GetSecurityDescriptorControl(sd, &control, &revision);
    ok(ret, "Matrix descriptor control query failed.\n");
    if (!ret) goto done;
    ret = GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
    ok(ret, "Matrix DACL query failed.\n");
    if (!ret) goto done;
    registry_matrix_acl_text(dacl, present, dacl_text, sizeof(dacl_text));
    sacl_text[0] = 0;
    if (sacl)
    {
        ret = GetSecurityDescriptorSacl(sd, &present, &audit, &defaulted);
        ok(ret, "Matrix SACL query failed.\n");
        if (ret) registry_matrix_acl_text(audit, present, sacl_text, sizeof(sacl_text));
    }
    if (before)
    {
        ret = GetSecurityDescriptorOwner(sd, &owner, &defaulted) &&
              GetSecurityDescriptorOwner(before, &old_owner, &old_defaulted);
        ok(ret && !!owner == !!old_owner && defaulted == old_defaulted, "Matrix owner presence/defaulting changed.\n");
        if (ret && owner && old_owner)
            ok(GetLengthSid(owner) == GetLengthSid(old_owner) && !memcmp(owner, old_owner, GetLengthSid(owner)),
               "Matrix owner bytes changed.\n");
        ret = GetSecurityDescriptorGroup(sd, &group, &defaulted) &&
              GetSecurityDescriptorGroup(before, &old_group, &old_defaulted);
        ok(ret && !!group == !!old_group && defaulted == old_defaulted, "Matrix group presence/defaulting changed.\n");
        if (ret && group && old_group)
            ok(GetLengthSid(group) == GetLengthSid(old_group) && !memcmp(group, old_group, GetLengthSid(group)),
               "Matrix group bytes changed.\n");
        if (sacl)
        {
            registry_matrix_check_labels(before, sd);
            ret = GetSecurityDescriptorDacl(before, &old_present, &old_dacl, &old_defaulted) &&
                  GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
            ok(ret && present == old_present && defaulted == old_defaulted && !!dacl == !!old_dacl,
               "SACL update changed DACL presence/defaulting.\n");
            if (ret && dacl && old_dacl)
                ok(dacl->AclSize == old_dacl->AclSize && !memcmp(dacl, old_dacl, dacl->AclSize),
                   "SACL update changed DACL bytes.\n");
            ret = GetSecurityDescriptorControl(before, &old_control, &revision);
            ok(ret && !((control ^ old_control) & (SE_DACL_PRESENT | SE_DACL_DEFAULTED |
               SE_DACL_PROTECTED | SE_DACL_AUTO_INHERITED | SE_DACL_AUTO_INHERIT_REQ)), "SACL update changed DACL control.\n");
        }
    }
    retained = RegSetValueExW(key, L"MatrixValue", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    fresh_read = RegOpenKeyExW(root, name, 0, KEY_QUERY_VALUE | READ_CONTROL, &fresh);
    if (!fresh_read)
    {
        size = sizeof(value);
        operation = RegQueryValueExW(fresh, L"MatrixValue", NULL, NULL, (BYTE *)&value, &size);
        fresh_sd = registry_matrix_descriptor(fresh, FALSE);
        if (fresh_sd)
        {
            ACL *fresh_dacl;
            BOOL fresh_present, fresh_defaulted;
            ret = GetSecurityDescriptorDacl(fresh_sd, &fresh_present, &fresh_dacl, &fresh_defaulted) &&
                  GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
            ok(ret && fresh_present == present && !!fresh_dacl == !!dacl, "Fresh descriptor DACL presence differs.\n");
            if (ret && fresh_dacl && dacl)
                ok(fresh_dacl->AclSize == dacl->AclSize && !memcmp(fresh_dacl, dacl, dacl->AclSize),
                   "Fresh descriptor DACL bytes differ.\n");
        }
        RegCloseKey(fresh);
        fresh = NULL;
    }
    fresh_write = RegOpenKeyExW(root, name, 0, KEY_SET_VALUE, &fresh);
    if (!fresh_write)
    {
        value = index;
        fresh_write = RegSetValueExW(fresh, L"MatrixValue", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
        RegCloseKey(fresh);
        fresh = NULL;
    }
    ok(control == expected->control, "Descriptor control %#x, expected %#x.\n", control, expected->control);
    ok(!strcmp(dacl_text, expected->dacl), "DACL [%s], expected [%s].\n", dacl_text, expected->dacl);
    if (sacl) ok(!strcmp(sacl_text, expected->sacl), "SACL [%s], expected [%s].\n", sacl_text, expected->sacl);
    ok(retained == expected->retained_write, "Retained write returned %ld, expected %ld.\n", retained, expected->retained_write);
    ok(fresh_read == expected->fresh_read, "Fresh read open returned %ld, expected %ld.\n", fresh_read, expected->fresh_read);
    if (!fresh_read) ok(operation == expected->read_value, "Value read returned %ld, expected %ld.\n", operation, expected->read_value);
    ok(fresh_write == expected->fresh_write, "Fresh write returned %ld, expected %ld.\n", fresh_write, expected->fresh_write);
done:
    free(fresh_sd);
    free(sd);
    winetest_pop_context();
}

static void test_registry_dacl_rights_matrix(void)
{
    static const struct registry_matrix_result results[] =
    {
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_DACL_PROTECTED | SE_DACL_AUTO_INHERITED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "", 0, 0, 0, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_DACL_AUTO_INHERITED,
         "0:1/12/00000002/WD;1:0/12/000f003f/WD;", "", 0, 0, 0, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_DACL_PROTECTED,
         "0:0/02/000f003f/WD;", "", 0, 0, 0, 0},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT,
         "0:0/02/000f003f/WD;", "", 0, 0, 0, 0},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_DACL_PROTECTED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "", 0, 0, 0, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_DACL_AUTO_INHERITED,
         "0:1/00/00000008/WD;1:1/12/00000002/WD;2:0/12/000f003f/WD;", "", 0, 0, 0, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT,
         "0:1/00/00040000/WD;1:0/00/00020000/OW;2:0/02/000f003f/WD;", "", 0, 0, 0, 0},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_DACL_AUTO_INHERITED,
         "0:1/00/00000001/WD;1:1/12/00000002/WD;2:0/12/000f003f/WD;", "", 0, ERROR_ACCESS_DENIED, ERROR_INVALID_HANDLE, ERROR_ACCESS_DENIED}
    };
    static const BYTE expected[][14] =
    {
        {0, 1, 1, 1, 2, 3, 1, 1, 1, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 2, 3, 1, 1, 1, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 2, 3, 1, 1, 1, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 2, 3, 1, 1, 1, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 2, 3, 1, 1, 1, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 2, 3, 1, 1, 1, 1, 1, 1, 1, 1},
        {4, 3, 3, 3, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3},
        {0, 1, 1, 1, 2, 3, 5, 3, 1, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 2, 3, 1, 1, 6, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 2, 3, 1, 1, 1, 1, 7, 3, 1, 1},
        {0, 1, 1, 1, 2, 3, 1, 1, 1, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 2, 3, 1, 1, 1, 1, 1, 1, 1, 1}
    };
    static const WCHAR path[] = L"Software\\Wine\\TestRegistryRightsMatrix";
    static WCHAR named[] = L"CURRENT_USER\\Software\\Wine\\TestRegistryRightsMatrix";
    static const WCHAR *names[] = {L"", L"Existing", L"Nested", L"Nested\\Grandchild", L"Protected",
        L"Protected\\Grandchild", L"NoEnum", L"NoEnum\\Grandchild", L"NoWriteDac", L"Sibling",
        L"NoQuery", L"NoQuery\\Grandchild", L"OrdinaryLinkValue", L"OrdinaryLinkValue\\Grandchild"};
    static const REGSAM access[] = {WRITE_DAC, READ_CONTROL | WRITE_DAC, KEY_ALL_ACCESS,
        MAXIMUM_ALLOWED, READ_CONTROL, KEY_ALL_ACCESS, KEY_ALL_ACCESS, KEY_ALL_ACCESS,
        KEY_ALL_ACCESS, KEY_ALL_ACCESS, KEY_ALL_ACCESS & ~KEY_QUERY_VALUE, KEY_ALL_ACCESS};
    static const char *strings[] = {"D:P(A;CI;KA;;;WD)", "D:P(D;CI;0x2;;;WD)(A;CI;KA;;;WD)",
        "D:(D;;0x8;;;WD)(A;CI;KA;;;WD)", "D:(D;;WD;;;WD)(A;;RC;;;OW)(A;CI;KA;;;WD)",
        "D:(D;;0x1;;;WD)(A;CI;KA;;;WD)"};
    static const WCHAR ordinary_value[] = L"Sibling";
    PSECURITY_DESCRIPTOR descriptors[ARRAY_SIZE(strings)] = {0}, before[ARRAY_SIZE(names)] = {0};
    HKEY keys[ARRAY_SIZE(names)] = {0}, setter = NULL, probe = NULL;
    BOOL created[ARRAY_SIZE(names)] = {0}, present, defaulted, result;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, FALSE};
    ACL *dacl;
    DWORD disposition, i, phase, setters = 0;
    LONG ret;

    winetest_push_context("Registry DACL rights matrix");
    for (i = 0; i < ARRAY_SIZE(strings); ++i)
    {
        result = ConvertStringSecurityDescriptorToSecurityDescriptorA(strings[i], SDDL_REVISION_1, &descriptors[i], NULL);
        ok(result, "Matrix DACL %lu construction failed: %lu.\n", i, GetLastError());
        if (!result) goto done;
    }
    for (i = 0; i < ARRAY_SIZE(names); ++i)
    {
        attributes.lpSecurityDescriptor = descriptors[0];
        ret = RegCreateKeyExW(i ? keys[0] : HKEY_CURRENT_USER, i ? names[i] : path, 0, NULL,
                              REG_OPTION_VOLATILE, KEY_ALL_ACCESS, i == 0 || i == 4 ? &attributes : NULL,
                              &keys[i], &disposition);
        ok(!ret, "Matrix key %lu creation returned %ld.\n", i, ret);
        if (ret) goto done;
        created[i] = disposition == REG_CREATED_NEW_KEY;
        ok(created[i], "Matrix key %lu already existed.\n", i);
        if (!created[i]) goto done;
        before[i] = registry_matrix_descriptor(keys[i], FALSE);
        if (!before[i]) goto done;
    }
    for (phase = 0; phase < ARRAY_SIZE(access); ++phase)
    {
        winetest_push_context("case %lu", phase);
        for (i = 0; i < ARRAY_SIZE(names); ++i)
        {
            ret = RegSetKeySecurity(keys[i], DACL_SECURITY_INFORMATION |
                  (i == 0 || i == 4 ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION), before[i]);
            ok(!ret, "Matrix reset key %lu returned %ld.\n", i, ret);
            if (ret) goto end_phase;
        }
        ret = RegDeleteValueW(keys[12], L"SymbolicLinkValue");
        ok(ret == ERROR_SUCCESS || ret == ERROR_FILE_NOT_FOUND, "Matrix marker reset returned %ld.\n", ret);
        if (ret != ERROR_SUCCESS && ret != ERROR_FILE_NOT_FOUND) goto end_phase;
        if (phase >= 7 && phase <= 10)
        {
            UINT boundary = phase == 7 ? 6 : phase == 8 ? 8 : phase == 9 ? 10 : 0;
            UINT descriptor = phase == 7 ? 2 : phase == 8 ? 3 : 4;
            REGSAM denied = phase == 7 ? KEY_ENUMERATE_SUB_KEYS : phase == 8 ? WRITE_DAC : KEY_QUERY_VALUE;
            ret = RegSetKeySecurity(keys[boundary], DACL_SECURITY_INFORMATION, descriptors[descriptor]);
            ok(!ret, "Boundary key %u DACL setup returned %ld.\n", boundary, ret);
            if (ret) goto end_phase;
            ret = RegOpenKeyExW(keys[0], names[boundary], 0, denied, &probe);
            ok(ret == ERROR_ACCESS_DENIED, "Boundary key %u access %#lx negative control returned %ld.\n", boundary, denied, ret);
            if (!ret) { RegCloseKey(probe); probe = NULL; }
            if (ret != ERROR_ACCESS_DENIED) goto end_phase;
            ret = RegOpenKeyExW(keys[0], names[boundary], 0,
                               (READ_CONTROL | WRITE_DAC | KEY_ENUMERATE_SUB_KEYS) & ~denied, &probe);
            ok(!ret, "Boundary key %u remaining rights positive control returned %ld.\n", boundary, ret);
            if (ret) goto end_phase;
            RegCloseKey(probe);
            probe = NULL;
        }
        if (phase == 11)
        {
            ret = RegSetValueExW(keys[12], L"SymbolicLinkValue", 0, REG_LINK,
                                 (const BYTE *)ordinary_value, sizeof(ordinary_value) - sizeof(WCHAR));
            ok(!ret, "Ordinary-key REG_LINK value setup returned %ld.\n", ret);
            if (ret) goto end_phase;
        }
        ret = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, access[phase], &setter);
        ok(!ret, "Matrix root handle %#lx open returned %ld.\n", access[phase], ret);
        if (ret) goto end_phase;
        result = GetSecurityDescriptorDacl(descriptors[1], &present, &dacl, &defaulted);
        ok(result && present && dacl, "Matrix target DACL missing.\n");
        if (!result || !present || !dacl) goto end_phase;
        if (phase == 5)
            ret = SetNamedSecurityInfoW(named, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                        NULL, NULL, dacl, NULL);
        else if (phase == 6)
            ret = RegSetKeySecurity(setter, DACL_SECURITY_INFORMATION, descriptors[1]);
        else ret = SetSecurityInfo(setter, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                    NULL, NULL, dacl, NULL);
        ok(ret == ERROR_SUCCESS, "Registry DACL setter requested %#lx returned %ld.\n", access[phase], ret);
        ++setters;
        for (i = 0; i < ARRAY_SIZE(names); ++i)
            registry_matrix_snapshot(keys[0], keys[i], names[i], i, before[i], FALSE, &results[expected[phase][i]]);
end_phase:
        if (setter) { RegCloseKey(setter); setter = NULL; }
        winetest_pop_context();
    }
    ok(setters == ARRAY_SIZE(access), "Registry DACL matrix exercised %lu of %u setters.\n", setters, (UINT)ARRAY_SIZE(access));
    trace("Registry DACL matrix exercised %lu of %u setters.\n", setters, (UINT)ARRAY_SIZE(access));
done:
    if (probe) RegCloseKey(probe);
    if (setter) RegCloseKey(setter);
    for (i = 0; i < ARRAY_SIZE(names); ++i)
        if (created[i] && keys[i])
        {
            ret = RegSetKeySecurity(keys[i], DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptors[0]);
            ok(!ret, "Matrix cleanup DACL %lu returned %ld.\n", i, ret);
        }
    for (i = ARRAY_SIZE(names); i-- > 1;)
    {
        if (keys[i]) RegCloseKey(keys[i]);
        if (created[i]) { ret = RegDeleteKeyW(keys[0], names[i]); ok(!ret, "Matrix cleanup key %lu returned %ld.\n", i, ret); }
    }
    if (keys[0]) RegCloseKey(keys[0]);
    if (created[0]) { ret = RegDeleteKeyW(HKEY_CURRENT_USER, path); ok(!ret, "Matrix root cleanup returned %ld.\n", ret); }
    for (i = 0; i < ARRAY_SIZE(before); ++i) free(before[i]);
    for (i = 0; i < ARRAY_SIZE(descriptors); ++i) if (descriptors[i]) LocalFree(descriptors[i]);
    winetest_pop_context();
}

static void test_registry_sacl_privilege_matrix(HANDLE *tokens)
{
    static const struct registry_matrix_result results[] =
    {
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_PROTECTED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "",
         ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "0:2/40/00000001/WD;",
         ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "",
         ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_SACL_PROTECTED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "0:2/40/00000001/WD;",
         ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "absent",
         ERROR_ACCESS_DENIED, ERROR_SUCCESS, ERROR_FILE_NOT_FOUND, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_PROTECTED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "0:2/c2/00000002/WD;",
         ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "0:2/c2/00000002/WD;",
         ERROR_ACCESS_DENIED, ERROR_SUCCESS, ERROR_FILE_NOT_FOUND, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_PROTECTED | SE_SACL_AUTO_INHERITED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "0:2/c2/00000002/WD;",
         ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_AUTO_INHERITED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "0:2/40/00000001/WD;1:2/d2/00000002/WD;",
         ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_AUTO_INHERITED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "0:2/d2/00000002/WD;",
         ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_SACL_AUTO_INHERITED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "0:2/d2/00000002/WD;",
         ERROR_ACCESS_DENIED, ERROR_SUCCESS, ERROR_FILE_NOT_FOUND, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_PROTECTED | SE_SACL_AUTO_INHERITED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "",
         ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_AUTO_INHERITED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "0:2/40/00000001/WD;",
         ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_AUTO_INHERITED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "null",
         ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SUCCESS, ERROR_ACCESS_DENIED},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_AUTO_INHERITED,
         "0:1/02/00000002/WD;1:0/02/000f003f/WD;", "absent",
         ERROR_ACCESS_DENIED, ERROR_SUCCESS, ERROR_FILE_NOT_FOUND, ERROR_ACCESS_DENIED}
    };
    static const BYTE expected[3][6][2][7] =
    {
        {
            {{0, 1, 2, 2, 3, 2, 4}, {0, 1, 2, 2, 3, 2, 4}},
            {{0, 1, 2, 2, 3, 2, 4}, {0, 1, 2, 2, 3, 2, 4}},
            {{5, 1, 2, 2, 3, 2, 6}, {0, 1, 2, 2, 3, 2, 4}},
            {{0, 1, 2, 2, 3, 2, 4}, {0, 1, 2, 2, 3, 2, 4}},
            {{0, 1, 2, 2, 3, 2, 4}, {0, 1, 2, 2, 3, 2, 4}},
            {{0, 1, 2, 2, 3, 2, 4}, {0, 1, 2, 2, 3, 2, 4}}
        },
        {
            {{7, 8, 9, 9, 3, 2, 10}, {11, 12, 13, 13, 3, 2, 14}},
            {{7, 8, 9, 9, 3, 2, 10}, {11, 12, 13, 13, 3, 2, 14}},
            {{5, 1, 2, 2, 3, 2, 6}, {0, 1, 2, 2, 3, 2, 4}},
            {{0, 1, 2, 2, 3, 2, 4}, {0, 1, 2, 2, 3, 2, 4}},
            {{7, 8, 9, 9, 3, 2, 10}, {11, 12, 13, 13, 3, 2, 14}},
            {{7, 8, 9, 9, 3, 2, 10}, {11, 12, 13, 13, 3, 2, 14}}
        },
        {
            {{0, 1, 2, 2, 3, 2, 4}, {0, 1, 2, 2, 3, 2, 4}},
            {{0, 1, 2, 2, 3, 2, 4}, {0, 1, 2, 2, 3, 2, 4}},
            {{5, 1, 2, 2, 3, 2, 6}, {0, 1, 2, 2, 3, 2, 4}},
            {{0, 1, 2, 2, 3, 2, 4}, {0, 1, 2, 2, 3, 2, 4}},
            {{0, 1, 2, 2, 3, 2, 4}, {0, 1, 2, 2, 3, 2, 4}},
            {{0, 1, 2, 2, 3, 2, 4}, {0, 1, 2, 2, 3, 2, 4}}
        }
    };
    static const WCHAR path[] = L"Software\\Wine\\TestRegistrySaclMatrix";
    static WCHAR named[] = L"CURRENT_USER\\Software\\Wine\\TestRegistrySaclMatrix";
    static const WCHAR *names[] = {L"", L"Existing", L"Nested", L"Nested\\Grandchild", L"Protected", L"Protected\\Grandchild"};
    static const char *strings[] = {"D:P(A;CI;KA;;;WD)S:P", "D:P(D;CI;0x2;;;WD)(A;CI;KA;;;WD)",
        "S:(AU;SA;0x1;;;WD)", "S:P(AU;SA;0x1;;;WD)", "S:P(AU;CISAFA;0x2;;;WD)", "S:P",
        "D:(D;CI;0x2;;;WD)(A;CI;KA;;;WD)", "S:"};
    PSECURITY_DESCRIPTOR descriptors[ARRAY_SIZE(strings)] = {0}, before[ARRAY_SIZE(names)] = {0}, queried;
    HKEY keys[ARRAY_SIZE(names)] = {0}, security_keys[ARRAY_SIZE(names)] = {0}, fresh = NULL, child = NULL;
    HKEY security_only = NULL;
    BOOL created[ARRAY_SIZE(names)] = {0}, result, present, defaulted, child_created = FALSE;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, FALSE};
    ACL *audit;
    SECURITY_DESCRIPTOR_CONTROL control, expected_control;
    DWORD i, disposition, mode, kind, remove, revision, setters = 0;
    LONG ret;

    winetest_push_context("Registry SACL privilege matrix");
    result = SetThreadToken(NULL, tokens[1]);
    ok(result, "SACL setup impersonation failed: %lu.\n", GetLastError());
    if (!result) goto done;
    for (i = 0; i < ARRAY_SIZE(strings); ++i)
    {
        result = ConvertStringSecurityDescriptorToSecurityDescriptorA(strings[i], SDDL_REVISION_1, &descriptors[i], NULL);
        ok(result, "SACL descriptor %lu construction failed: %lu.\n", i, GetLastError());
        if (!result) goto done;
    }
    for (i = 0; i < ARRAY_SIZE(names); ++i)
    {
        attributes.lpSecurityDescriptor = descriptors[0];
        ret = RegCreateKeyExW(i ? keys[0] : HKEY_CURRENT_USER, i ? names[i] : path, 0, NULL,
                              REG_OPTION_VOLATILE, KEY_ALL_ACCESS, i == 0 || i == 4 ? &attributes : NULL,
                              &keys[i], &disposition);
        ok(!ret, "SACL key %lu creation returned %ld.\n", i, ret);
        if (ret) goto done;
        created[i] = disposition == REG_CREATED_NEW_KEY;
        ok(created[i], "SACL key %lu already existed.\n", i);
        if (!created[i]) goto done;
        ret = RegOpenKeyExW(keys[0], names[i], 0, KEY_ALL_ACCESS | ACCESS_SYSTEM_SECURITY, &security_keys[i]);
        ok(!ret, "SACL retained handle %lu open returned %ld.\n", i, ret);
        if (ret) goto done;
        ret = RegSetKeySecurity(security_keys[i], SACL_SECURITY_INFORMATION |
              (i == 0 || i == 4 ? PROTECTED_SACL_SECURITY_INFORMATION : UNPROTECTED_SACL_SECURITY_INFORMATION),
              descriptors[i == 1 ? 2 : i == 4 ? 3 : i == 0 ? 5 : 7]);
        ok(!ret, "SACL initial key %lu set returned %ld.\n", i, ret);
        if (ret) goto done;
    }
    ret = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, ACCESS_SYSTEM_SECURITY, &security_only);
    ok(!ret, "SACL-only retained handle open returned %ld.\n", ret);
    if (ret) goto done;
    for (i = 0; i < ARRAY_SIZE(names); ++i)
    {
        ret = RegSetKeySecurity(keys[i], DACL_SECURITY_INFORMATION |
              (i == 4 ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION), descriptors[i == 4 ? 6 : 1]);
        ok(!ret, "SACL negative DACL %lu setup returned %ld.\n", i, ret);
        if (ret) goto done;
        before[i] = registry_matrix_descriptor(security_keys[i], TRUE);
        if (!before[i]) goto done;
        result = GetSecurityDescriptorControl(before[i], &control, &revision);
        expected_control = (i == 4 ? 0 : SE_DACL_PROTECTED) | (i == 0 || i == 4 ? SE_SACL_PROTECTED : 0);
        ok(result && (control & (SE_DACL_PROTECTED | SE_SACL_PROTECTED)) == expected_control,
           "SACL baseline key %lu protection %#x, expected %#x.\n", i, control, expected_control);
        if (!result || (control & (SE_DACL_PROTECTED | SE_SACL_PROTECTED)) != expected_control) goto done;
    }
    for (mode = 0; mode < 3; ++mode)
        for (kind = 0; kind < 6; ++kind)
        {
            winetest_push_context("token %lu setter %lu", mode, kind);
            result = SetThreadToken(NULL, tokens[1]);
            ok(result, "SACL reset impersonation failed: %lu.\n", GetLastError());
            if (!result) goto end_case;
            for (i = 0; i < ARRAY_SIZE(names); ++i)
            {
                ret = RegSetKeySecurity(security_keys[i], DACL_SECURITY_INFORMATION |
                      (i == 4 ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION), before[i]);
                ok(!ret, "SACL reset DACL %lu returned %ld.\n", i, ret);
                if (ret) goto end_case;
                ret = RegSetKeySecurity(security_keys[i], SACL_SECURITY_INFORMATION |
                      (i == 0 || i == 4 ? PROTECTED_SACL_SECURITY_INFORMATION : UNPROTECTED_SACL_SECURITY_INFORMATION), before[i]);
                ok(!ret, "SACL reset key %lu returned %ld.\n", i, ret);
                if (ret) goto end_case;
                queried = registry_matrix_descriptor(security_keys[i], TRUE);
                if (!queried) goto end_case;
                result = GetSecurityDescriptorControl(queried, &control, &revision);
                expected_control = (i == 4 ? 0 : SE_DACL_PROTECTED) | (i == 0 || i == 4 ? SE_SACL_PROTECTED : 0);
                ok(result && (control & (SE_DACL_PROTECTED | SE_SACL_PROTECTED)) == expected_control,
                   "SACL reset key %lu protection %#x, expected %#x.\n", i, control, expected_control);
                free(queried);
                if (!result || (control & (SE_DACL_PROTECTED | SE_SACL_PROTECTED)) != expected_control) goto end_case;
            }
            for (remove = 0; remove < 2; ++remove)
            {
                winetest_push_context("remove %lu", remove);
                result = SetThreadToken(NULL, tokens[mode]);
                ok(result, "SACL case impersonation failed: %lu.\n", GetLastError());
                if (!result) { winetest_pop_context(); goto end_case; }
                for (i = 0; i < ARRAY_SIZE(names); ++i)
                {
                    ret = RegOpenKeyExW(keys[0], names[i], 0, KEY_SET_VALUE, &fresh);
                    ok(ret == ERROR_ACCESS_DENIED, "SACL key %lu pre-update value-write negative control returned %ld.\n", i, ret);
                    if (!ret) { RegCloseKey(fresh); fresh = NULL; }
                    if (ret != ERROR_ACCESS_DENIED) { winetest_pop_context(); goto end_case; }
                }
                ret = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, ACCESS_SYSTEM_SECURITY, &fresh);
                ok(ret == (mode == 1 ? ERROR_SUCCESS : ERROR_ACCESS_DENIED), "Registry SACL fresh security open returned %ld.\n", ret);
                if (!ret) { RegCloseKey(fresh); fresh = NULL; }
                {
                    OBJECT_ATTRIBUTES native_attributes;
                    UNICODE_STRING native_name;
                    HANDLE native_key = NULL;
                    NTSTATUS status;

                    RtlInitUnicodeString(&native_name, L"");
                    InitializeObjectAttributes(&native_attributes, &native_name, OBJ_CASE_INSENSITIVE, keys[0], NULL);
                    status = NtOpenKeyEx(&native_key, ACCESS_SYSTEM_SECURITY, &native_attributes, 0);
                    ok(status == (mode == 1 ? STATUS_SUCCESS : STATUS_ACCESS_DENIED),
                       "Registry SACL native root security open returned %#lx.\n", status);
                    if (NT_SUCCESS(status)) NtClose(native_key);
                    native_key = NULL;
                    RtlInitUnicodeString(&native_name, L"Existing");
                    status = NtOpenKeyEx(&native_key, ACCESS_SYSTEM_SECURITY, &native_attributes, 0);
                    ok(status == (mode == 1 ? STATUS_SUCCESS : STATUS_ACCESS_DENIED),
                       "Registry SACL native child security open returned %#lx.\n", status);
                    if (NT_SUCCESS(status)) NtClose(native_key);
                }
                result = GetSecurityDescriptorSacl(descriptors[remove ? 5 : 4], &present, &audit, &defaulted);
                ok(result && present && audit, "Matrix target SACL missing.\n");
                if (!result || !present || !audit) { winetest_pop_context(); goto end_case; }
                if (kind == 4)
                    ret = SetNamedSecurityInfoW(named, SE_REGISTRY_KEY, SACL_SECURITY_INFORMATION | PROTECTED_SACL_SECURITY_INFORMATION,
                                                NULL, NULL, NULL, audit);
                else if (kind == 2 || kind == 3)
                    ret = RegSetKeySecurity(kind == 2 ? security_keys[0] : keys[0], SACL_SECURITY_INFORMATION, descriptors[remove ? 5 : 4]);
                else ret = SetSecurityInfo(kind == 5 ? security_only : kind ? keys[0] : security_keys[0], SE_REGISTRY_KEY,
                                           SACL_SECURITY_INFORMATION | PROTECTED_SACL_SECURITY_INFORMATION, NULL, NULL, NULL, audit);
                ok(ret == (kind == 2 || (mode == 1 && kind != 3) ? ERROR_SUCCESS : ERROR_ACCESS_DENIED),
                   "Registry SACL setter returned %ld.\n", ret);
                ++setters;
                result = SetThreadToken(NULL, tokens[1]);
                ok(result, "SACL observation impersonation failed: %lu.\n", GetLastError());
                if (!result) { winetest_pop_context(); goto end_case; }
                for (i = 0; i < ARRAY_SIZE(names); ++i)
                    registry_matrix_snapshot(security_keys[0], security_keys[i], names[i], i, before[i], TRUE,
                                             &results[expected[mode][kind][remove][i]]);
                ret = RegCreateKeyExW(keys[0], L"NewChild", 0, NULL, REG_OPTION_VOLATILE,
                                      READ_CONTROL | WRITE_DAC | DELETE | KEY_QUERY_VALUE | ACCESS_SYSTEM_SECURITY,
                                      NULL, &child, &disposition);
                ok(!ret, "SACL new child creation returned %ld.\n", ret);
                if (!ret)
                {
                    child_created = disposition == REG_CREATED_NEW_KEY;
                    ok(child_created, "SACL new child already existed.\n");
                    if (child_created)
                        registry_matrix_snapshot(security_keys[0], child, L"NewChild", ARRAY_SIZE(names), NULL, TRUE,
                                                 &results[expected[mode][kind][remove][ARRAY_SIZE(names)]]);
                    RegCloseKey(child);
                    child = NULL;
                    if (child_created)
                    {
                        ret = RegDeleteKeyW(keys[0], L"NewChild");
                        ok(!ret, "SACL new child cleanup returned %ld.\n", ret);
                        if (!ret) child_created = FALSE;
                    }
                }
                winetest_pop_context();
            }
end_case:
            winetest_pop_context();
        }
    ok(setters == 36, "Registry SACL matrix exercised %lu of 36 setters.\n", setters);
    trace("Registry SACL matrix exercised %lu of 36 setters.\n", setters);
done:
    result = SetThreadToken(NULL, tokens[1]);
    ok(result, "SACL cleanup impersonation failed: %lu.\n", GetLastError());
    if (fresh) RegCloseKey(fresh);
    if (child) RegCloseKey(child);
    if (child_created) { ret = RegDeleteKeyW(keys[0], L"NewChild"); ok(!ret, "SACL remaining child cleanup returned %ld.\n", ret); }
    for (i = ARRAY_SIZE(names); i-- > 1;)
    {
        if (security_keys[i]) RegCloseKey(security_keys[i]);
        if (keys[i]) RegCloseKey(keys[i]);
        if (created[i]) { ret = RegDeleteKeyW(keys[0], names[i]); ok(!ret, "SACL cleanup key %lu returned %ld.\n", i, ret); }
    }
    if (security_only) RegCloseKey(security_only);
    if (security_keys[0]) RegCloseKey(security_keys[0]);
    if (keys[0]) RegCloseKey(keys[0]);
    if (created[0]) { ret = RegDeleteKeyW(HKEY_CURRENT_USER, path); ok(!ret, "SACL root cleanup returned %ld.\n", ret); }
    for (i = 0; i < ARRAY_SIZE(before); ++i) free(before[i]);
    for (i = 0; i < ARRAY_SIZE(descriptors); ++i) if (descriptors[i]) LocalFree(descriptors[i]);
    winetest_pop_context();
}

static void test_registry_create_privilege_matrix(HANDLE *tokens)
{
    static const WCHAR path[] = L"Software\\Wine\\TestRegistryCreatePrivilege";
    static const WCHAR *names[] = {L"Existing", L"RawMissing\\Leaf", L"PublicMissing\\Leaf"};
    static const WCHAR *observed[] = {L"RawMissing", L"RawMissing\\Leaf", L"PublicMissing", L"PublicMissing\\Leaf"};
    PSECURITY_DESCRIPTOR descriptor = NULL;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, FALSE};
    HKEY root = NULL, existing = NULL, key = NULL;
    OBJECT_ATTRIBUTES object_attributes;
    UNICODE_STRING name;
    NTSTATUS status;
    DWORD mode, kind, i, disposition;
    LONG ret;
    BOOL result, created = FALSE;

    winetest_push_context("Registry create privilege matrix");
    result = SetThreadToken(NULL, tokens[1]);
    ok(result, "Create setup impersonation failed: %lu.\n", GetLastError());
    if (!result) goto done;
    result = ConvertStringSecurityDescriptorToSecurityDescriptorA("D:P(A;CI;KA;;;WD)",
              SDDL_REVISION_1, &descriptor, NULL);
    ok(result, "Create descriptor construction failed: %lu.\n", GetLastError());
    if (!result) goto done;
    attributes.lpSecurityDescriptor = descriptor;
    ret = RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, REG_OPTION_VOLATILE,
                          KEY_ALL_ACCESS, &attributes, &root, &disposition);
    ok(!ret, "Create fixture root returned %ld.\n", ret);
    if (ret) goto done;
    created = disposition == REG_CREATED_NEW_KEY;
    ok(created, "Create fixture root already existed.\n");
    if (!created) goto done;
    ret = RegCreateKeyExW(root, L"Existing", 0, NULL, REG_OPTION_VOLATILE,
                          KEY_ALL_ACCESS, NULL, &existing, &disposition);
    ok(!ret, "Create existing fixture returned %ld.\n", ret);
    if (ret) goto done;
    for (mode = 0; mode < 3; ++mode)
    {
        winetest_push_context("token %lu", mode);
        result = SetThreadToken(NULL, tokens[mode]);
        ok(result, "Create case impersonation failed: %lu.\n", GetLastError());
        if (!result) { winetest_pop_context(); goto done; }
        for (kind = 0; kind < ARRAY_SIZE(names); ++kind)
        {
            disposition = 0xdeadbeef;
            if (kind == 2)
            {
                ret = RegCreateKeyExW(root, names[kind], 0, NULL, REG_OPTION_VOLATILE,
                                      KEY_READ | ACCESS_SYSTEM_SECURITY, NULL, &key, &disposition);
                ok(ret == (mode == 1 ? ERROR_SUCCESS : ERROR_PRIVILEGE_NOT_HELD),
                   "Public missing-path create returned %ld.\n", ret);
                if (!ret)
                {
                    ok(disposition == REG_CREATED_NEW_KEY, "Public create disposition %#lx.\n", disposition);
                    RegCloseKey(key);
                }
            }
            else
            {
                RtlInitUnicodeString(&name, names[kind]);
                InitializeObjectAttributes(&object_attributes, &name, OBJ_CASE_INSENSITIVE, root, NULL);
                status = NtCreateKey((HANDLE *)&key, KEY_READ | ACCESS_SYSTEM_SECURITY, &object_attributes,
                                     0, NULL, REG_OPTION_VOLATILE, &disposition);
                ok(status == (kind ? STATUS_OBJECT_NAME_NOT_FOUND : mode == 1 ? STATUS_SUCCESS : STATUS_ACCESS_DENIED),
                   "Raw create kind %lu returned %#lx.\n", kind, status);
                if (NT_SUCCESS(status))
                {
                    ok(disposition == REG_OPENED_EXISTING_KEY, "Raw create disposition %#lx.\n", disposition);
                    NtClose(key);
                }
            }
            key = NULL;
        }
        result = SetThreadToken(NULL, tokens[1]);
        ok(result, "Create observation impersonation failed: %lu.\n", GetLastError());
        if (!result) { winetest_pop_context(); goto done; }
        for (i = 0; i < ARRAY_SIZE(observed); ++i)
        {
            ret = RegOpenKeyExW(root, observed[i], 0, KEY_READ, &key);
            ok(ret == (i < 2 ? ERROR_FILE_NOT_FOUND : ERROR_SUCCESS),
               "Create path %lu observation returned %ld.\n", i, ret);
            if (!ret) RegCloseKey(key);
            key = NULL;
        }
        for (i = ARRAY_SIZE(observed); i-- > 0;)
        {
            ret = RegDeleteKeyW(root, observed[i]);
            ok(!ret || ret == ERROR_FILE_NOT_FOUND || ret == ERROR_PATH_NOT_FOUND,
               "Create path %lu cleanup returned %ld.\n", i, ret);
        }
        winetest_pop_context();
    }
done:
    result = SetThreadToken(NULL, tokens[1]);
    ok(result, "Create cleanup impersonation failed: %lu.\n", GetLastError());
    if (key) RegCloseKey(key);
    if (existing) RegCloseKey(existing);
    if (created)
    {
        for (i = ARRAY_SIZE(observed); i-- > 0;) RegDeleteKeyW(root, observed[i]);
        ret = RegDeleteKeyW(root, L"Existing");
        ok(!ret || ret == ERROR_FILE_NOT_FOUND, "Create existing cleanup returned %ld.\n", ret);
    }
    if (root) RegCloseKey(root);
    if (created)
    {
        ret = RegDeleteKeyW(HKEY_CURRENT_USER, path);
        ok(!ret, "Create fixture root cleanup returned %ld.\n", ret);
    }
    if (descriptor) LocalFree(descriptor);
    winetest_pop_context();
}

struct acl_group_netapi
{
    NET_API_STATUS (WINAPI *user_add)(LPCWSTR, DWORD, LPBYTE, LPDWORD);
    NET_API_STATUS (WINAPI *user_get_info)(LPCWSTR, LPCWSTR, DWORD, LPBYTE *);
    NET_API_STATUS (WINAPI *user_del)(LPCWSTR, LPCWSTR);
    NET_API_STATUS (WINAPI *group_add)(LPCWSTR, DWORD, LPBYTE, LPDWORD);
    NET_API_STATUS (WINAPI *group_del)(LPCWSTR, LPCWSTR);
    NET_API_STATUS (WINAPI *group_get_members)(LPCWSTR, LPCWSTR, DWORD, LPBYTE *, DWORD, LPDWORD, LPDWORD, PDWORD_PTR);
    NET_API_STATUS (WINAPI *group_add_members)(LPCWSTR, LPCWSTR, DWORD, LPBYTE, DWORD);
    NET_API_STATUS (WINAPI *group_del_members)(LPCWSTR, LPCWSTR, DWORD, LPBYTE, DWORD);
    NET_API_STATUS (WINAPI *buffer_free)(LPVOID);
};

static BOOL acl_group_resolve_account(const WCHAR *name, SID_NAME_USE expected_use, PSID *sid, WCHAR **qualified)
{
    DWORD sid_size = 0, domain_size = 0, domain_capacity, error, i, name_size = lstrlenW(name);
    SID_NAME_USE use = SidTypeUnknown;
    WCHAR *domain = NULL;
    BOOL ret = FALSE;

    *sid = NULL;
    *qualified = NULL;
    ret = LookupAccountNameW(NULL, name, NULL, &sid_size, NULL, &domain_size, &use);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER && sid_size && sid_size <= SECURITY_MAX_SID_SIZE &&
       domain_size && domain_size <= USHRT_MAX / sizeof(WCHAR),
       "Group fixture account sizing returned %d, error %lu, sizes %lu/%lu.\n", ret, error, sid_size, domain_size);
    if (ret || error != ERROR_INSUFFICIENT_BUFFER || !sid_size || sid_size > SECURITY_MAX_SID_SIZE ||
        !domain_size || domain_size > USHRT_MAX / sizeof(WCHAR)) return FALSE;
    domain_capacity = domain_size;
    *sid = calloc(1, SECURITY_MAX_SID_SIZE);
    domain = malloc(domain_capacity * sizeof(*domain));
    ok(!!*sid && !!domain, "Group fixture account allocation failed.\n");
    if (!*sid || !domain) goto failed;
    memset(domain, 0xff, domain_capacity * sizeof(*domain));
    sid_size = SECURITY_MAX_SID_SIZE;
    ret = LookupAccountNameW(NULL, name, *sid, &sid_size, domain, &domain_size, &use);
    error = GetLastError();
    for (i = 0; i < domain_capacity && domain[i]; ++i) {}
    ret = ret && sid_size <= SECURITY_MAX_SID_SIZE && IsValidSid(*sid) &&
          GetLengthSid(*sid) <= sid_size && use == expected_use && i && i < domain_capacity;
    ok(ret, "Group fixture account lookup failed: error %lu, size %lu, use %u, domain length %lu.\n",
       error, sid_size, use, i);
    if (!ret) goto failed;
    *qualified = malloc((i + name_size + 2) * sizeof(**qualified));
    ok(!!*qualified, "Group fixture qualified account allocation failed.\n");
    if (!*qualified) goto failed;
    memcpy(*qualified, domain, i * sizeof(*domain));
    (*qualified)[i] = '\\';
    memcpy(*qualified + i + 1, name, (name_size + 1) * sizeof(*name));
    free(domain);
    return TRUE;
failed:
    free(domain);
    free(*sid);
    free(*qualified);
    *sid = NULL;
    *qualified = NULL;
    return FALSE;
}

static BOOL acl_group_check_members(const struct acl_group_netapi *api, const WCHAR *group, PSID user, DWORD expected)
{
    LOCALGROUP_MEMBERS_INFO_0 *members;
    DWORD read, total, i, count = 0, matches = 0;
    DWORD_PTR resume = 0;
    NET_API_STATUS status, free_status;
    BOOL valid = TRUE, sid_valid;

    do
    {
        members = NULL;
        read = total = 0;
        status = api->group_get_members(NULL, group, 0, (BYTE **)&members, MAX_PREFERRED_LENGTH, &read, &total, &resume);
        ok(status == NERR_Success || status == ERROR_MORE_DATA, "Group fixture membership query returned %lu.\n", status);
        if (status != NERR_Success && status != ERROR_MORE_DATA) valid = FALSE;
        if (status == NERR_Success || status == ERROR_MORE_DATA)
        {
            ok(!read || !!members, "Group fixture membership query returned no buffer for %lu entries.\n", read);
            if (read && !members) valid = FALSE;
            if (members)
            {
                for (i = 0; i < read; ++i)
                {
                    sid_valid = members[i].lgrmi0_sid && IsValidSid(members[i].lgrmi0_sid);
                    ok(sid_valid, "Group fixture member %lu has an invalid SID.\n", i);
                    if (sid_valid && EqualSid(members[i].lgrmi0_sid, user)) ++matches;
                    if (!sid_valid) valid = FALSE;
                }
            }
            count += read;
        }
        if (members)
        {
            free_status = api->buffer_free(members);
            ok(free_status == NERR_Success, "Group fixture membership buffer free returned %lu.\n", free_status);
            if (free_status) valid = FALSE;
        }
    } while (status == ERROR_MORE_DATA);
    ok(count == expected && matches == expected,
       "Group fixture contains %lu entries, %lu user matches, expected %lu.\n", count, matches, expected);
    return valid && count == expected && matches == expected;
}

static void test_acl_group_membership_queries(void)
{
    static const struct
    {
        DWORD effective_status;
        ACCESS_MASK effective;
        DWORD audit_status;
        ACCESS_MASK success, failure;
    } expected[3][3] =
    {
        {
            {ERROR_SUCCESS, 0x0, ERROR_INVALID_ACL, 0x0, 0x0},
            {ERROR_SUCCESS, 0x3, ERROR_INVALID_ACL, 0x0, 0x0},
            {ERROR_INVALID_ACL, 0x0, ERROR_SUCCESS, 0x0, 0x0}
        },
        {
            {ERROR_SUCCESS, 0x40, ERROR_INVALID_ACL, 0x0, 0x0},
            {ERROR_SUCCESS, 0x2, ERROR_INVALID_ACL, 0x0, 0x0},
            {ERROR_INVALID_ACL, 0x0, ERROR_SUCCESS, 0x80, 0x100}
        },
        {
            {ERROR_SUCCESS, 0x0, ERROR_INVALID_ACL, 0x0, 0x0},
            {ERROR_SUCCESS, 0x3, ERROR_INVALID_ACL, 0x0, 0x0},
            {ERROR_INVALID_ACL, 0x0, ERROR_SUCCESS, 0x0, 0x0}
        }
    };
    static const WCHAR alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
    struct acl_group_netapi api = {0};
    BOOLEAN (WINAPI *random_bytes)(PVOID, ULONG);
    union { DWORD words[10]; BYTE bytes[40]; } random;
    SID world = {SID_REVISION, 1, {SECURITY_WORLD_SID_AUTHORITY}, {SECURITY_WORLD_RID}};
    union { ACL acl; BYTE bytes[256]; } buffer;
    BYTE saved_acl[sizeof(buffer)];
    union { DWORD align; BYTE bytes[SECURITY_MAX_SID_SIZE]; } check_sid;
    USER_INFO_1 user_info = {0}, *queried_user = NULL;
    LOCALGROUP_INFO_0 group_info;
    LOCALGROUP_MEMBERS_INFO_0 member;
    struct { ACCESS_MASK before, value, after; } effective, success, failure;
    TRUSTEE_A trustee_a;
    TRUSTEE_W trustee_w;
    WCHAR user_name[20], group_name[20], password[37], *user_qualified = NULL, *group_qualified = NULL;
    WCHAR *roundtrip = NULL;
    char check_domain[256], *user_ansi = NULL;
    PSID user_sid = NULL, group_sid = NULL;
    HMODULE module = NULL;
    NET_API_STATUS status;
    SID_NAME_USE use = SidTypeUnknown;
    DWORD parameter = 0, sid_size, domain_size, effective_status, audit_status;
    UINT phase, shape, form, i, calls = 0, phases = 0;
    int ansi_size, name_size;
    BOOL ret, user_created = FALSE, group_created = FALSE, membership_added = FALSE;

    winetest_push_context("ACL group membership");
    module = LoadLibraryA("netapi32.dll");
    ok(!!module, "Group fixture could not load netapi32: %lu.\n", GetLastError());
    if (!module) goto done;
    api.user_add = (void *)GetProcAddress(module, "NetUserAdd");
    api.user_get_info = (void *)GetProcAddress(module, "NetUserGetInfo");
    api.user_del = (void *)GetProcAddress(module, "NetUserDel");
    api.group_add = (void *)GetProcAddress(module, "NetLocalGroupAdd");
    api.group_del = (void *)GetProcAddress(module, "NetLocalGroupDel");
    api.group_get_members = (void *)GetProcAddress(module, "NetLocalGroupGetMembers");
    api.group_add_members = (void *)GetProcAddress(module, "NetLocalGroupAddMembers");
    api.group_del_members = (void *)GetProcAddress(module, "NetLocalGroupDelMembers");
    api.buffer_free = (void *)GetProcAddress(module, "NetApiBufferFree");
    ret = api.user_add && api.user_get_info && api.user_del && api.group_add && api.group_del &&
          api.group_get_members && api.group_add_members && api.group_del_members && api.buffer_free;
    ok(ret, "Group fixture requires every NetAPI entry point.\n");
    if (!ret) goto done;
    random_bytes = (void *)GetProcAddress(GetModuleHandleA("advapi32.dll"), "SystemFunction036");
    ok(!!random_bytes, "Group fixture requires SystemFunction036.\n");
    if (!random_bytes) goto done;
    ret = random_bytes(&random, sizeof(random));
    ok(ret, "Group fixture random generation failed.\n");
    if (!ret) goto done;
    swprintf(user_name, ARRAY_SIZE(user_name), L"lnu%08lx%08lx", random.words[0], random.words[1]);
    swprintf(group_name, ARRAY_SIZE(group_name), L"lng%08lx%08lx", random.words[0], random.words[1]);
    memcpy(password, L"Aa7!", 4 * sizeof(*password));
    for (i = 0; i < 32; ++i) password[i + 4] = alphabet[random.bytes[i + 8] & 63];
    password[36] = 0;
    user_info.usri1_name = user_name;
    user_info.usri1_password = password;
    user_info.usri1_priv = USER_PRIV_USER;
    user_info.usri1_flags = UF_SCRIPT | UF_ACCOUNTDISABLE;
    status = api.user_add(NULL, 1, (BYTE *)&user_info, &parameter);
    ok(status == NERR_Success, "Group fixture user creation returned %lu, parameter %lu.\n", status, parameter);
    SecureZeroMemory(password, sizeof(password));
    SecureZeroMemory(&random, sizeof(random));
    if (status) goto done;
    user_created = TRUE;
    status = api.user_get_info(NULL, user_name, 1, (BYTE **)&queried_user);
    ok(status == NERR_Success && !!queried_user, "Group fixture user query returned %lu.\n", status);
    if (status || !queried_user) goto done;
    ret = (queried_user->usri1_flags & (UF_SCRIPT | UF_ACCOUNTDISABLE | UF_NORMAL_ACCOUNT)) ==
          (UF_SCRIPT | UF_ACCOUNTDISABLE | UF_NORMAL_ACCOUNT) && queried_user->usri1_priv == USER_PRIV_GUEST;
    ok(ret, "Group fixture user flags %#lx, privilege %lu.\n", queried_user->usri1_flags, queried_user->usri1_priv);
    if (!ret) goto done;
    status = api.buffer_free(queried_user);
    queried_user = NULL;
    ok(status == NERR_Success, "Group fixture user buffer free returned %lu.\n", status);
    if (status) goto done;
    group_info.lgrpi0_name = group_name;
    status = api.group_add(NULL, 0, (BYTE *)&group_info, &parameter);
    ok(status == NERR_Success, "Group fixture local group creation returned %lu, parameter %lu.\n", status, parameter);
    if (status) goto done;
    group_created = TRUE;
    if (!acl_group_resolve_account(user_name, SidTypeUser, &user_sid, &user_qualified) ||
        !acl_group_resolve_account(group_name, SidTypeAlias, &group_sid, &group_qualified)) goto done;
    ret = !EqualSid(user_sid, group_sid);
    ok(ret, "Group fixture user and local group SIDs are equal.\n");
    if (!ret) goto done;
    name_size = lstrlenW(user_qualified) + 1;
    ansi_size = WideCharToMultiByte(CP_ACP, 0, user_qualified, -1, NULL, 0, NULL, NULL);
    ok(ansi_size > 0, "Group fixture ANSI name sizing failed: %lu.\n", GetLastError());
    if (!ansi_size) goto done;
    user_ansi = malloc(ansi_size);
    roundtrip = malloc(name_size * sizeof(*roundtrip));
    ok(!!user_ansi && !!roundtrip, "Group fixture name conversion allocation failed.\n");
    if (!user_ansi || !roundtrip) goto done;
    ret = WideCharToMultiByte(CP_ACP, 0, user_qualified, -1, user_ansi, ansi_size, NULL, NULL) == ansi_size &&
          MultiByteToWideChar(CP_ACP, 0, user_ansi, -1, roundtrip, name_size) == name_size &&
          !lstrcmpW(user_qualified, roundtrip);
    ok(ret, "Group fixture account name did not round trip through ANSI.\n");
    if (!ret) goto done;
    sid_size = sizeof(check_sid);
    domain_size = ARRAY_SIZE(check_domain);
    ret = LookupAccountNameA(NULL, user_ansi, check_sid.bytes, &sid_size, check_domain, &domain_size, &use);
    ret = ret && sid_size <= sizeof(check_sid) && IsValidSid(check_sid.bytes) && use == SidTypeUser &&
          EqualSid(check_sid.bytes, user_sid);
    ok(ret, "Group fixture qualified ANSI name did not resolve to the owned user: %lu.\n", GetLastError());
    if (!ret) goto done;
    member.lgrmi0_sid = user_sid;
    for (phase = 0; phase < 3; ++phase)
    {
        if (phase == 1)
        {
            status = api.group_add_members(NULL, group_name, 0, (BYTE *)&member, 1);
            ok(status == NERR_Success, "Group fixture add member returned %lu.\n", status);
            if (status) goto done;
            membership_added = TRUE;
        }
        else if (phase == 2)
        {
            status = api.group_del_members(NULL, group_name, 0, (BYTE *)&member, 1);
            ok(status == NERR_Success, "Group fixture remove member returned %lu.\n", status);
            if (status) goto done;
            membership_added = FALSE;
        }
        if (!acl_group_check_members(&api, group_name, user_sid, phase == 1)) goto done;
        ++phases;
        for (shape = 0; shape < 3; ++shape)
        {
            memset(&buffer, 0, sizeof(buffer));
            ret = InitializeAcl(&buffer.acl, sizeof(buffer), ACL_REVISION);
            if (ret && shape == 0)
                ret = AddAccessAllowedAceEx(&buffer.acl, ACL_REVISION, 0, 0x40, group_sid);
            else if (ret && shape == 1)
                ret = AddAccessDeniedAceEx(&buffer.acl, ACL_REVISION, 0, 1, group_sid) &&
                      AddAccessAllowedAceEx(&buffer.acl, ACL_REVISION, 0, 3, &world);
            else if (ret)
                ret = AddAuditAccessAceEx(&buffer.acl, ACL_REVISION, 0, 0x80, group_sid, TRUE, FALSE) &&
                      AddAuditAccessAceEx(&buffer.acl, ACL_REVISION, 0, 0x100, group_sid, FALSE, TRUE);
            ok(ret && IsValidAcl(&buffer.acl), "Group fixture phase %u shape %u ACL construction failed: %lu.\n",
               phase, shape, GetLastError());
            if (!ret || !IsValidAcl(&buffer.acl)) goto done;
            memcpy(saved_acl, &buffer, sizeof(buffer));
            for (form = 0; form < 4; ++form)
            {
                if (form < 2)
                {
                    BuildTrusteeWithSidA(&trustee_a, user_sid);
                    BuildTrusteeWithSidW(&trustee_w, user_sid);
                }
                else
                {
                    BuildTrusteeWithNameA(&trustee_a, user_ansi);
                    BuildTrusteeWithNameW(&trustee_w, user_qualified);
                }
                effective.before = success.before = failure.before = 0x11223344;
                effective.value = success.value = failure.value = 0xdeadbeef;
                effective.after = success.after = failure.after = 0x55667788;
                effective_status = (form & 1) ? GetEffectiveRightsFromAclW(&buffer.acl, &trustee_w, &effective.value) :
                                               GetEffectiveRightsFromAclA(&buffer.acl, &trustee_a, &effective.value);
                ++calls;
                ret = !memcmp(saved_acl, &buffer, sizeof(buffer));
                ok(ret, "Group fixture phase %u shape %u form %u effective query changed its input ACL.\n", phase, shape, form);
                if (!ret) goto done;
                audit_status = (form & 1) ? GetAuditedPermissionsFromAclW(&buffer.acl, &trustee_w, &success.value, &failure.value) :
                                           GetAuditedPermissionsFromAclA(&buffer.acl, &trustee_a, &success.value, &failure.value);
                ++calls;
                ret = !memcmp(saved_acl, &buffer, sizeof(buffer));
                ok(ret, "Group fixture phase %u shape %u form %u audit query changed its input ACL.\n", phase, shape, form);
                if (!ret) goto done;
                ok(effective.before == 0x11223344 && effective.after == 0x55667788 &&
                   success.before == 0x11223344 && success.after == 0x55667788 &&
                   failure.before == 0x11223344 && failure.after == 0x55667788,
                   "Group fixture phase %u shape %u form %u query overwrote a guard.\n", phase, shape, form);
                ok(effective_status == expected[phase][shape].effective_status,
                   "Group fixture phase %u shape %u form %u effective status %lu, expected %lu.\n",
                   phase, shape, form, effective_status, expected[phase][shape].effective_status);
                if (!effective_status && !expected[phase][shape].effective_status)
                    ok(effective.value == expected[phase][shape].effective,
                       "Group fixture phase %u shape %u form %u effective mask %#lx, expected %#lx.\n",
                       phase, shape, form, effective.value, expected[phase][shape].effective);
                ok(audit_status == expected[phase][shape].audit_status,
                   "Group fixture phase %u shape %u form %u audit status %lu, expected %lu.\n",
                   phase, shape, form, audit_status, expected[phase][shape].audit_status);
                if (!audit_status && !expected[phase][shape].audit_status)
                    ok(success.value == expected[phase][shape].success && failure.value == expected[phase][shape].failure,
                       "Group fixture phase %u shape %u form %u audit masks %#lx/%#lx, expected %#lx/%#lx.\n",
                       phase, shape, form, success.value, failure.value, expected[phase][shape].success,
                       expected[phase][shape].failure);
            }
        }
    }
done:
    if (membership_added)
    {
        status = api.group_del_members(NULL, group_name, 0, (BYTE *)&member, 1);
        ok(status == NERR_Success, "Group fixture cleanup remove member returned %lu.\n", status);
    }
    if (queried_user)
    {
        status = api.buffer_free(queried_user);
        ok(status == NERR_Success, "Group fixture cleanup user buffer free returned %lu.\n", status);
    }
    if (group_created)
    {
        status = api.group_del(NULL, group_name);
        ok(status == NERR_Success, "Group fixture cleanup group deletion returned %lu.\n", status);
    }
    if (user_created)
    {
        status = api.user_del(NULL, user_name);
        ok(status == NERR_Success, "Group fixture cleanup user deletion returned %lu.\n", status);
    }
    free(roundtrip);
    free(user_ansi);
    free(user_qualified);
    free(group_qualified);
    free(user_sid);
    free(group_sid);
    SecureZeroMemory(password, sizeof(password));
    SecureZeroMemory(&random, sizeof(random));
    if (module) FreeLibrary(module);
    ok(phases == 3 && calls == 72, "ACL group fixture completed %u/3 phases and %u/72 calls.\n", phases, calls);
    trace("ACL group fixture completed %u/3 phases and %u/72 calls.\n", phases, calls);
    winetest_pop_context();
}

static void test_acl_object_rights_queries(void)
{
    static const struct
    {
        DWORD effective_status;
        ACCESS_MASK effective;
        DWORD audit_status;
        ACCESS_MASK success, failure;
    } results[] =
    {
        {ERROR_SUCCESS, 0x7, ERROR_INVALID_ACL, 0x0, 0x0},
        {ERROR_INVALID_SID, 0x0, ERROR_INVALID_SID, 0x0, 0x0},
        {ERROR_NONE_MAPPED, 0x0, ERROR_INVALID_ACL, 0x0, 0x0},
        {ERROR_INVALID_ACL, 0x0, ERROR_SUCCESS, 0x1, 0x0},
        {ERROR_INVALID_ACL, 0x0, ERROR_NONE_MAPPED, 0x0, 0x0},
        {ERROR_UNKNOWN_PROPERTY, 0x0, ERROR_INVALID_ACL, 0x0, 0x0},
        {ERROR_INVALID_ACL, 0x0, ERROR_INVALID_PARAMETER, 0x0, 0x0},
        {ERROR_INVALID_ACL, 0x0, ERROR_SUCCESS, 0x0, 0x2}
    };
    static const BYTE expected[10][4][2] =
    {
        {{0, 0}, {1, 0}, {2, 2}, {2, 2}},
        {{3, 3}, {1, 3}, {4, 4}, {4, 4}},
        {{0, 0}, {1, 0}, {2, 2}, {2, 2}},
        {{5, 5}, {1, 5}, {5, 5}, {5, 5}},
        {{5, 5}, {1, 5}, {5, 5}, {5, 5}},
        {{0, 0}, {1, 0}, {2, 2}, {2, 2}},
        {{6, 6}, {1, 6}, {6, 6}, {6, 6}},
        {{7, 7}, {1, 7}, {4, 4}, {4, 4}},
        {{6, 6}, {1, 6}, {6, 6}, {6, 6}},
        {{5, 5}, {1, 5}, {5, 5}, {5, 5}}
    };
    static const char *expected_acl[] =
    {
        "04000001010000000000140007000000010100000000000100000000",
        "04000001010000000240140001000000010100000000000100000000",
        "04000001010000000000140007000000010100000000000100000000",
        "04000001010000000500280007000000010000000000000000000000c000000000000046010100000000000100000000",
        "04000001010000000500380007000000030000000000000000000000c0000000000000460004020000000000c000000000000046010100000000000100000000",
        "04000001020000000600280001000000010000000000000000000000c0000000000000460101000000000001000000000000140007000000010100000000000100000000",
        "04000001010000000740280001000000010000000000000000000000c000000000000046010100000000000100000000",
        "04000001010000000790280002000000020000000004020000000000c000000000000046010100000000000100000000",
        "040000010100000007c0380004000000030000000000000000000000c0000000000000460004020000000000c000000000000046010100000000000100000000",
        "04000001010000000508280007000000010000000000000000000000c000000000000046010100000000000100000000"
    };
    GUID object = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
    GUID other = {0x00020400, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
    SID world = {SID_REVISION, 1, {SECURITY_WORLD_SID_AUTHORITY}, {SECURITY_WORLD_RID}};
    union { ACL acl; BYTE bytes[256]; } buffer;
    BYTE saved_acl[sizeof(buffer)];
    ACL_SIZE_INFORMATION information;
    char acl_bytes[sizeof(buffer) * 2 + 1];
    static const char hex[] = "0123456789abcdef";
    struct { ACCESS_MASK before, value, after; } effective[2], success[2], failure[2];
    TRUSTEE_A trustee_a;
    TRUSTEE_W trustee_w;
    OBJECTS_AND_SID objects_a, objects_w;
    GUID empty = {0}, *type;
    DWORD object_flags;
    DWORD effective_status[2], audit_status[2];
    UINT shape, trustee, form, index, result_index, calls = 0;
    BOOL ret;

    for (shape = 0; shape < 10; ++shape)
    {
        winetest_push_context("Object ACL rights case %u", shape);
        memset(&buffer, 0, sizeof(buffer));
        ret = InitializeAcl(&buffer.acl, sizeof(buffer), ACL_REVISION_DS);
        ok(ret, "Object rights ACL initialization failed: %lu.\n", GetLastError());
        if (!ret) goto next_case;
        switch (shape)
        {
            case 0:
                ret = AddAccessAllowedAceEx(&buffer.acl, ACL_REVISION_DS, 0, 7, &world);
                break;
            case 1:
                ret = AddAuditAccessAceEx(&buffer.acl, ACL_REVISION_DS, 0, 1, &world, TRUE, FALSE);
                break;
            case 2:
                ret = AddAccessAllowedObjectAce(&buffer.acl, ACL_REVISION_DS, 0, 7, NULL, NULL, &world);
                break;
            case 3:
                ret = AddAccessAllowedObjectAce(&buffer.acl, ACL_REVISION_DS, 0, 7, &object, NULL, &world);
                break;
            case 4:
                ret = AddAccessAllowedObjectAce(&buffer.acl, ACL_REVISION_DS, 0, 7, &object, &other, &world);
                break;
            case 5:
                ret = AddAccessDeniedObjectAce(&buffer.acl, ACL_REVISION_DS, 0, 1, &object, NULL, &world);
                if (ret) ret = AddAccessAllowedAceEx(&buffer.acl, ACL_REVISION_DS, 0, 7, &world);
                break;
            case 6:
                ret = AddAuditAccessObjectAce(&buffer.acl, ACL_REVISION_DS, 0, 1, &object, NULL, &world, TRUE, FALSE);
                break;
            case 7:
                ret = AddAuditAccessObjectAce(&buffer.acl, ACL_REVISION_DS, INHERITED_ACE, 2, NULL, &other, &world, FALSE, TRUE);
                break;
            case 8:
                ret = AddAuditAccessObjectAce(&buffer.acl, ACL_REVISION_DS, 0, 4, &object, &other, &world, TRUE, TRUE);
                break;
            default:
                ret = AddAccessAllowedObjectAce(&buffer.acl, ACL_REVISION_DS, INHERIT_ONLY_ACE, 7, &object, NULL, &world);
                break;
        }
        ok(ret, "Object rights ACE construction failed: %lu.\n", GetLastError());
        if (!ret) goto next_case;
        ret = GetAclInformation(&buffer.acl, &information, sizeof(information), AclSizeInformation);
        ok(ret && information.AclBytesInUse <= sizeof(buffer), "Object rights ACL size query failed.\n");
        if (!ret || information.AclBytesInUse > sizeof(buffer)) goto next_case;
        for (index = 0; index < information.AclBytesInUse; ++index)
        {
            acl_bytes[2 * index] = hex[buffer.bytes[index] >> 4];
            acl_bytes[2 * index + 1] = hex[buffer.bytes[index] & 15];
        }
        acl_bytes[2 * index] = 0;
        ok(!strcmp(acl_bytes, expected_acl[shape]), "Object rights ACL bytes %s, expected %s.\n",
           acl_bytes, expected_acl[shape]);
        memcpy(saved_acl, &buffer, sizeof(buffer));
        for (trustee = 0; trustee < 4; ++trustee)
        {
            memset(&trustee_a, 0, sizeof(trustee_a));
            memset(&trustee_w, 0, sizeof(trustee_w));
            memset(&objects_a, 0, sizeof(objects_a));
            memset(&objects_w, 0, sizeof(objects_w));
            type = trustee == 1 ? NULL : trustee == 2 ? &object : &other;
            if (!trustee)
            {
                BuildTrusteeWithSidA(&trustee_a, &world);
                BuildTrusteeWithSidW(&trustee_w, &world);
            }
            else
            {
                BuildTrusteeWithObjectsAndSidA(&trustee_a, &objects_a, type, NULL, &world);
                BuildTrusteeWithObjectsAndSidW(&trustee_w, &objects_w, type, NULL, &world);
            }
            ret = !trustee_a.pMultipleTrustee && !trustee_w.pMultipleTrustee &&
                  trustee_a.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE &&
                  trustee_w.MultipleTrusteeOperation == NO_MULTIPLE_TRUSTEE &&
                  trustee_a.TrusteeType == TRUSTEE_IS_UNKNOWN && trustee_w.TrusteeType == TRUSTEE_IS_UNKNOWN &&
                  trustee_a.TrusteeForm == (trustee ? TRUSTEE_IS_OBJECTS_AND_SID : TRUSTEE_IS_SID) &&
                  trustee_w.TrusteeForm == (trustee ? TRUSTEE_IS_OBJECTS_AND_SID : TRUSTEE_IS_SID) &&
                  trustee_a.ptstrName == (char *)(trustee ? (void *)&objects_a : (void *)&world) &&
                  trustee_w.ptstrName == (WCHAR *)(trustee ? (void *)&objects_w : (void *)&world);
            ok(ret, "Trustee %u builder identity or default fields differ.\n", trustee);
            if (!ret) goto next_case;
            if (trustee)
            {
                object_flags = type ? ACE_OBJECT_TYPE_PRESENT : 0;
                ret = objects_a.ObjectsPresent == object_flags && objects_w.ObjectsPresent == object_flags &&
                      objects_a.pSid == &world && objects_w.pSid == &world &&
                      !memcmp(&objects_a.ObjectTypeGuid, type ? type : &empty, sizeof(GUID)) &&
                      !memcmp(&objects_w.ObjectTypeGuid, type ? type : &empty, sizeof(GUID)) &&
                      !memcmp(&objects_a.InheritedObjectTypeGuid, &empty, sizeof(GUID)) &&
                      !memcmp(&objects_w.InheritedObjectTypeGuid, &empty, sizeof(GUID));
                ok(ret, "Trustee %u object builder SID, flags or GUIDs differ.\n", trustee);
                if (!ret) goto next_case;
            }
            for (form = 0; form < 2; ++form)
            {
                effective[form].before = success[form].before = failure[form].before = 0x11223344;
                effective[form].value = success[form].value = failure[form].value = 0xdeadbeef;
                effective[form].after = success[form].after = failure[form].after = 0x55667788;
                effective_status[form] = form ? GetEffectiveRightsFromAclW(&buffer.acl, &trustee_w, &effective[form].value) :
                                               GetEffectiveRightsFromAclA(&buffer.acl, &trustee_a, &effective[form].value);
                ok(!memcmp(saved_acl, &buffer, sizeof(buffer)), "Effective query changed its input ACL.\n");
                audit_status[form] = form ? GetAuditedPermissionsFromAclW(&buffer.acl, &trustee_w, &success[form].value, &failure[form].value) :
                                           GetAuditedPermissionsFromAclA(&buffer.acl, &trustee_a, &success[form].value, &failure[form].value);
                ok(!memcmp(saved_acl, &buffer, sizeof(buffer)), "Audit query changed its input ACL.\n");
                calls += 2;
                ok(effective[form].before == 0x11223344 && effective[form].after == 0x55667788 &&
                   success[form].before == 0x11223344 && success[form].after == 0x55667788 &&
                   failure[form].before == 0x11223344 && failure[form].after == 0x55667788,
                   "Trustee %u form %u object rights query overwrote a guard.\n", trustee, form);
                result_index = expected[shape][trustee][form];
                ok(effective_status[form] == results[result_index].effective_status,
                   "Trustee %u form %u effective status %lu, expected %lu.\n", trustee, form,
                   effective_status[form], results[result_index].effective_status);
                if (!effective_status[form] && !results[result_index].effective_status)
                    ok(effective[form].value == results[result_index].effective,
                       "Trustee %u form %u effective mask %#lx, expected %#lx.\n", trustee, form,
                       effective[form].value, results[result_index].effective);
                ok(audit_status[form] == results[result_index].audit_status,
                   "Trustee %u form %u audit status %lu, expected %lu.\n", trustee, form,
                   audit_status[form], results[result_index].audit_status);
                if (!audit_status[form] && !results[result_index].audit_status)
                    ok(success[form].value == results[result_index].success &&
                       failure[form].value == results[result_index].failure,
                       "Trustee %u form %u audit masks %#lx/%#lx, expected %#lx/%#lx.\n", trustee, form,
                       success[form].value, failure[form].value, results[result_index].success, results[result_index].failure);
            }
        }
next_case:
        winetest_pop_context();
    }
    ok(calls == 160, "Object ACL rights queries exercised %u/160 calls.\n", calls);
    trace("Object ACL rights queries exercised %u/160 calls.\n", calls);
}

static void test_acl_rights_queries(void)
{
    static const struct
    {
        BYTE count;
        struct { BYTE type, flags, sid; ACCESS_MASK mask; } aces[3];
    } cases[] =
    {
        {0},
        {1, {{ACCESS_ALLOWED_ACE_TYPE, 0, 0, 1}}},
        {2, {{ACCESS_DENIED_ACE_TYPE, 0, 0, 1}, {ACCESS_ALLOWED_ACE_TYPE, 0, 0, 3}}},
        {2, {{ACCESS_ALLOWED_ACE_TYPE, 0, 0, 3}, {ACCESS_DENIED_ACE_TYPE, 0, 0, 1}}},
        {1, {{ACCESS_ALLOWED_ACE_TYPE, INHERIT_ONLY_ACE, 0, 4}}},
        {1, {{ACCESS_ALLOWED_ACE_TYPE, INHERITED_ACE, 0, 8}}},
        {2, {{ACCESS_DENIED_ACE_TYPE, INHERITED_ACE, 0, 1}, {ACCESS_ALLOWED_ACE_TYPE, 0, 0, 3}}},
        {1, {{ACCESS_ALLOWED_ACE_TYPE, 0, 1, 16}}},
        {1, {{ACCESS_ALLOWED_ACE_TYPE, 0, 0, GENERIC_READ}}},
        {1, {{SYSTEM_AUDIT_ACE_TYPE, SUCCESSFUL_ACCESS_ACE_FLAG, 0, 1}}},
        {1, {{SYSTEM_AUDIT_ACE_TYPE, FAILED_ACCESS_ACE_FLAG, 0, 2}}},
        {1, {{SYSTEM_AUDIT_ACE_TYPE, SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG, 0, 4}}},
        {3, {{SYSTEM_AUDIT_ACE_TYPE, SUCCESSFUL_ACCESS_ACE_FLAG, 0, 1},
             {SYSTEM_AUDIT_ACE_TYPE, FAILED_ACCESS_ACE_FLAG, 0, 2},
             {SYSTEM_AUDIT_ACE_TYPE, SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG, 0, 4}}},
        {1, {{SYSTEM_AUDIT_ACE_TYPE, INHERIT_ONLY_ACE | SUCCESSFUL_ACCESS_ACE_FLAG, 0, 8}}},
        {1, {{SYSTEM_AUDIT_ACE_TYPE, INHERITED_ACE | FAILED_ACCESS_ACE_FLAG, 0, 16}}},
        {1, {{SYSTEM_AUDIT_ACE_TYPE, SUCCESSFUL_ACCESS_ACE_FLAG, 1, 32}}},
        {1, {{ACCESS_ALLOWED_ACE_TYPE, 0, 2, 0x40}}},
        {2, {{ACCESS_DENIED_ACE_TYPE, 0, 2, 1}, {ACCESS_ALLOWED_ACE_TYPE, 0, 0, 3}}},
        {1, {{SYSTEM_AUDIT_ACE_TYPE, SUCCESSFUL_ACCESS_ACE_FLAG, 2, 0x80}}}
    };
    static const struct
    {
        DWORD effective_status, audit_status;
        ACCESS_MASK effective[3], success[3], failure[3];
    } expected[] =
    {
        {ERROR_SUCCESS, ERROR_SUCCESS, {0x0, 0x0, 0x0}, {0x0, 0x0, 0x0}, {0x0, 0x0, 0x0}},
        {ERROR_SUCCESS, ERROR_INVALID_ACL, {0x1, 0x1, 0x1}, {0, 0, 0}, {0, 0, 0}},
        {ERROR_SUCCESS, ERROR_INVALID_ACL, {0x2, 0x2, 0x2}, {0, 0, 0}, {0, 0, 0}},
        {ERROR_INVALID_ACL, ERROR_INVALID_ACL, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
        {ERROR_SUCCESS, ERROR_INVALID_ACL, {0x0, 0x0, 0x0}, {0, 0, 0}, {0, 0, 0}},
        {ERROR_SUCCESS, ERROR_INVALID_ACL, {0x8, 0x8, 0x8}, {0, 0, 0}, {0, 0, 0}},
        {ERROR_SUCCESS, ERROR_INVALID_ACL, {0x2, 0x2, 0x2}, {0, 0, 0}, {0, 0, 0}},
        {ERROR_SUCCESS, ERROR_INVALID_ACL, {0x0, 0x10, 0x0}, {0, 0, 0}, {0, 0, 0}},
        {ERROR_SUCCESS, ERROR_INVALID_ACL, {0x0, 0x0, 0x0}, {0, 0, 0}, {0, 0, 0}},
        {ERROR_INVALID_ACL, ERROR_SUCCESS, {0, 0, 0}, {0x1, 0x1, 0x1}, {0x0, 0x0, 0x0}},
        {ERROR_INVALID_ACL, ERROR_SUCCESS, {0, 0, 0}, {0x0, 0x0, 0x0}, {0x2, 0x2, 0x2}},
        {ERROR_INVALID_ACL, ERROR_SUCCESS, {0, 0, 0}, {0x0, 0x0, 0x0}, {0x0, 0x0, 0x0}},
        {ERROR_INVALID_ACL, ERROR_SUCCESS, {0, 0, 0}, {0x1, 0x1, 0x1}, {0x2, 0x2, 0x2}},
        {ERROR_INVALID_ACL, ERROR_SUCCESS, {0, 0, 0}, {0x8, 0x8, 0x8}, {0x0, 0x0, 0x0}},
        {ERROR_INVALID_ACL, ERROR_SUCCESS, {0, 0, 0}, {0x0, 0x0, 0x0}, {0x10, 0x10, 0x10}},
        {ERROR_INVALID_ACL, ERROR_SUCCESS, {0, 0, 0}, {0x0, 0x20, 0x0}, {0x0, 0x0, 0x0}},
        {ERROR_SUCCESS, ERROR_INVALID_ACL, {0x0, 0x0, 0x40}, {0, 0, 0}, {0, 0, 0}},
        {ERROR_SUCCESS, ERROR_INVALID_ACL, {0x3, 0x3, 0x2}, {0, 0, 0}, {0, 0, 0}},
        {ERROR_INVALID_ACL, ERROR_SUCCESS, {0, 0, 0}, {0x0, 0x0, 0x80}, {0x0, 0x0, 0x0}}
    };
    SID world = {SID_REVISION, 1, {SECURITY_WORLD_SID_AUTHORITY}, {SECURITY_WORLD_RID}};
    SID system = {SID_REVISION, 1, {SECURITY_NT_AUTHORITY}, {SECURITY_LOCAL_SYSTEM_RID}};
    union { DWORD align; BYTE bytes[SECURITY_MAX_SID_SIZE]; } administrators;
    PSID sids[] = {&world, &system, administrators.bytes};
    union { ACL acl; BYTE bytes[256]; } buffer;
    struct { ACCESS_MASK before, value, after; } effective[2], success[2], failure[2];
    TRUSTEE_A trustee_a;
    TRUSTEE_W trustee_w;
    DWORD effective_status[2], audit_status[2];
    UINT i, j, trustee, form, calls = 0;
    DWORD sid_size = sizeof(administrators);
    BYTE flags;
    BOOL ret;

    ret = CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, administrators.bytes, &sid_size);
    ok(ret, "Administrators SID creation failed: %lu.\n", GetLastError());
    if (!ret) return;
    ret = sid_size <= sizeof(administrators) && IsValidSid(administrators.bytes);
    ok(ret, "Administrators SID result is invalid, size %lu.\n", sid_size);
    if (!ret) return;
    for (i = 0; i < ARRAY_SIZE(cases); ++i)
    {
        winetest_push_context("ACL rights case %u", i);
        ret = InitializeAcl(&buffer.acl, sizeof(buffer), ACL_REVISION);
        ok(ret, "Rights ACL initialization failed: %lu.\n", GetLastError());
        if (!ret) goto next_case;
        for (j = 0; j < cases[i].count; ++j)
        {
            flags = cases[i].aces[j].flags;
            if (cases[i].aces[j].type == ACCESS_ALLOWED_ACE_TYPE)
                ret = AddAccessAllowedAceEx(&buffer.acl, ACL_REVISION, flags,
                                           cases[i].aces[j].mask, sids[cases[i].aces[j].sid]);
            else if (cases[i].aces[j].type == ACCESS_DENIED_ACE_TYPE)
                ret = AddAccessDeniedAceEx(&buffer.acl, ACL_REVISION, flags,
                                          cases[i].aces[j].mask, sids[cases[i].aces[j].sid]);
            else
                ret = AddAuditAccessAceEx(&buffer.acl, ACL_REVISION,
                                         flags & ~(SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG),
                                         cases[i].aces[j].mask, sids[cases[i].aces[j].sid],
                                         !!(flags & SUCCESSFUL_ACCESS_ACE_FLAG), !!(flags & FAILED_ACCESS_ACE_FLAG));
            ok(ret, "Rights ACE %u construction failed: %lu.\n", j, GetLastError());
            if (!ret) goto next_case;
        }
        for (trustee = 0; trustee < ARRAY_SIZE(sids); ++trustee)
        {
            BuildTrusteeWithSidA(&trustee_a, sids[trustee]);
            BuildTrusteeWithSidW(&trustee_w, sids[trustee]);
            for (form = 0; form < 2; ++form)
            {
                effective[form].before = success[form].before = failure[form].before = 0x11223344;
                effective[form].value = success[form].value = failure[form].value = 0xdeadbeef;
                effective[form].after = success[form].after = failure[form].after = 0x55667788;
                effective_status[form] = form ? GetEffectiveRightsFromAclW(&buffer.acl, &trustee_w, &effective[form].value) :
                                               GetEffectiveRightsFromAclA(&buffer.acl, &trustee_a, &effective[form].value);
                audit_status[form] = form ? GetAuditedPermissionsFromAclW(&buffer.acl, &trustee_w, &success[form].value, &failure[form].value) :
                                           GetAuditedPermissionsFromAclA(&buffer.acl, &trustee_a, &success[form].value, &failure[form].value);
                calls += 2;
                ok(effective_status[form] == expected[i].effective_status,
                   "Trustee %u form %u effective status %lu, expected %lu.\n",
                   trustee, form, effective_status[form], expected[i].effective_status);
                if (!effective_status[form] && !expected[i].effective_status)
                    ok(effective[form].value == expected[i].effective[trustee],
                       "Trustee %u form %u effective mask %#lx, expected %#lx.\n",
                       trustee, form, effective[form].value, expected[i].effective[trustee]);
                ok(audit_status[form] == expected[i].audit_status,
                   "Trustee %u form %u audit status %lu, expected %lu.\n",
                   trustee, form, audit_status[form], expected[i].audit_status);
                if (!audit_status[form] && !expected[i].audit_status)
                    ok(success[form].value == expected[i].success[trustee] && failure[form].value == expected[i].failure[trustee],
                       "Trustee %u form %u audit masks %#lx/%#lx, expected %#lx/%#lx.\n", trustee, form,
                       success[form].value, failure[form].value, expected[i].success[trustee], expected[i].failure[trustee]);
                ok(effective[form].before == 0x11223344 && effective[form].after == 0x55667788 &&
                   success[form].before == 0x11223344 && success[form].after == 0x55667788 &&
                   failure[form].before == 0x11223344 && failure[form].after == 0x55667788,
                   "Trustee %u form %u rights query overwrote a guard.\n", trustee, form);
            }
            ok(effective_status[0] == effective_status[1], "Trustee %u A/W effective status differs: %lu/%lu.\n",
               trustee, effective_status[0], effective_status[1]);
            if (!effective_status[0] && !effective_status[1])
                ok(effective[0].value == effective[1].value, "Trustee %u A/W effective masks differ.\n", trustee);
            ok(audit_status[0] == audit_status[1], "Trustee %u A/W audit status differs: %lu/%lu.\n",
               trustee, audit_status[0], audit_status[1]);
            if (!audit_status[0] && !audit_status[1])
                ok(success[0].value == success[1].value && failure[0].value == failure[1].value,
                   "Trustee %u A/W audit masks differ.\n", trustee);
        }
next_case:
        winetest_pop_context();
    }
    ok(calls == 228, "ACL rights queries exercised %u/228 calls.\n", calls);
    trace("ACL rights queries exercised %u/228 calls.\n", calls);
}

static PSECURITY_DESCRIPTOR file_sacl_matrix_descriptor(HANDLE handle)
{
    SECURITY_INFORMATION information = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                       DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION;
    PSECURITY_DESCRIPTOR sd;
    DWORD size = 0, capacity, error;
    BOOL ret, valid;

    ret = GetKernelObjectSecurity(handle, information, NULL, 0, &size);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER && size >= sizeof(SECURITY_DESCRIPTOR_RELATIVE),
       "File matrix descriptor sizing returned %d, error %lu, size %lu.\n", ret, error, size);
    if (ret || error != ERROR_INSUFFICIENT_BUFFER || size < sizeof(SECURITY_DESCRIPTOR_RELATIVE)) return NULL;
    capacity = size;
    sd = malloc(capacity);
    ok(!!sd, "File matrix descriptor allocation failed.\n");
    if (!sd) return NULL;
    ret = GetKernelObjectSecurity(handle, information, sd, capacity, &size);
    valid = ret && size >= sizeof(SECURITY_DESCRIPTOR_RELATIVE) && size <= capacity;
    if (valid) valid = RtlValidRelativeSecurityDescriptor(sd, size, information & ~LABEL_SECURITY_INFORMATION);
    ok(valid, "File matrix descriptor query returned %d, error %lu, size %lu, capacity %lu.\n",
       ret, GetLastError(), size, capacity);
    if (!valid) { free(sd); return NULL; }
    return sd;
}

static void file_sacl_matrix_preserved(PSECURITY_DESCRIPTOR before, PSECURITY_DESCRIPTOR after, BOOL full)
{
    SECURITY_DESCRIPTOR_CONTROL old_control = 0, control = 0;
    ACL *old_acl, *acl;
    PSID old_sid, sid;
    BOOL old_present, present, old_defaulted, defaulted, ret;
    DWORD revision;

    ret = GetSecurityDescriptorOwner(before, &old_sid, &old_defaulted) &&
          GetSecurityDescriptorOwner(after, &sid, &defaulted);
    ok(ret && !!old_sid == !!sid && old_defaulted == defaulted, "File matrix owner presence/defaulting changed.\n");
    if (ret && old_sid && sid)
        ok(GetLengthSid(old_sid) == GetLengthSid(sid) && !memcmp(old_sid, sid, GetLengthSid(sid)),
           "File matrix owner bytes changed.\n");
    ret = GetSecurityDescriptorGroup(before, &old_sid, &old_defaulted) &&
          GetSecurityDescriptorGroup(after, &sid, &defaulted);
    ok(ret && !!old_sid == !!sid && old_defaulted == defaulted, "File matrix group presence/defaulting changed.\n");
    if (ret && old_sid && sid)
        ok(GetLengthSid(old_sid) == GetLengthSid(sid) && !memcmp(old_sid, sid, GetLengthSid(sid)),
           "File matrix group bytes changed.\n");
    ret = GetSecurityDescriptorDacl(before, &old_present, &old_acl, &old_defaulted) &&
          GetSecurityDescriptorDacl(after, &present, &acl, &defaulted);
    ok(ret && old_present == present && old_defaulted == defaulted && !!old_acl == !!acl,
       "File matrix DACL presence/defaulting changed.\n");
    if (ret && old_acl && acl)
        ok(old_acl->AclSize == acl->AclSize && !memcmp(old_acl, acl, acl->AclSize), "File matrix DACL bytes changed.\n");
    ret = GetSecurityDescriptorControl(before, &old_control, &revision) &&
          GetSecurityDescriptorControl(after, &control, &revision);
    ok(ret && !((old_control ^ control) & (SE_DACL_PRESENT | SE_DACL_DEFAULTED | SE_DACL_PROTECTED |
       SE_DACL_AUTO_INHERITED | SE_DACL_AUTO_INHERIT_REQ)), "File matrix DACL control changed.\n");
    registry_matrix_check_labels(before, after);
    if (full)
    {
        ok(ret && old_control == control, "Fresh file descriptor control %#x differs from retained %#x.\n", control, old_control);
        ret = GetSecurityDescriptorSacl(before, &old_present, &old_acl, &old_defaulted) &&
              GetSecurityDescriptorSacl(after, &present, &acl, &defaulted);
        ok(ret && old_present == present && old_defaulted == defaulted && !!old_acl == !!acl,
           "Fresh file SACL presence/defaulting differs.\n");
        if (ret && old_acl && acl)
            ok(old_acl->AclSize == acl->AclSize && !memcmp(old_acl, acl, acl->AclSize), "Fresh file SACL bytes differ.\n");
    }
}

struct file_sacl_matrix_result
{
    SECURITY_DESCRIPTOR_CONTROL control;
    const char *dacl, *sacl;
};

static BOOL file_sacl_matrix_snapshot(HANDLE handle, const char *path, UINT index, BOOL directory,
        PSECURITY_DESCRIPTOR before, const struct file_sacl_matrix_result *expected)
{
    PSECURITY_DESCRIPTOR sd = NULL, fresh_sd = NULL;
    SECURITY_DESCRIPTOR_CONTROL control;
    HANDLE fresh = INVALID_HANDLE_VALUE;
    ACL *dacl, *audit;
    DWORD revision, bytes = 0, retained = ERROR_INVALID_HANDLE, read_error = ERROR_INVALID_HANDLE, write_error;
    BYTE value = index, read_value = 0;
    BOOL ret, present, defaulted, complete = FALSE;
    char dacl_text[1024], sacl_text[1024];

    winetest_push_context("file %u", index);
    sd = file_sacl_matrix_descriptor(handle);
    if (!sd) goto done;
    ret = GetSecurityDescriptorControl(sd, &control, &revision) &&
          GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
    ok(ret, "File matrix descriptor fields unavailable.\n");
    if (!ret) goto done;
    registry_matrix_acl_text(dacl, present, dacl_text, sizeof(dacl_text));
    ret = GetSecurityDescriptorSacl(sd, &present, &audit, &defaulted);
    ok(ret, "File matrix SACL unavailable.\n");
    if (!ret) goto done;
    registry_matrix_acl_text(audit, present, sacl_text, sizeof(sacl_text));
    if (before) file_sacl_matrix_preserved(before, sd, FALSE);
    fresh = CreateFileA(path, READ_CONTROL | ACCESS_SYSTEM_SECURITY | FILE_READ_DATA,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(fresh != INVALID_HANDLE_VALUE, "Fresh file descriptor handle failed: %lu.\n", GetLastError());
    if (fresh == INVALID_HANDLE_VALUE) goto done;
    fresh_sd = file_sacl_matrix_descriptor(fresh);
    if (!fresh_sd) goto done;
    file_sacl_matrix_preserved(sd, fresh_sd, TRUE);
    if (!directory)
    {
        if (before)
        {
            SetLastError(ERROR_SUCCESS);
            ret = SetFilePointer(handle, 0, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER || GetLastError() == ERROR_SUCCESS;
            ok(ret, "Retained file seek failed: %lu.\n", GetLastError());
            if (!ret) goto done;
            ret = WriteFile(handle, &value, sizeof(value), &bytes, NULL);
            retained = ret ? ERROR_SUCCESS : GetLastError();
            ok(ret && bytes == sizeof(value), "Retained file write returned %d, error %lu, bytes %lu.\n", ret, retained, bytes);
        }
        ret = ReadFile(fresh, &read_value, sizeof(read_value), &bytes, NULL);
        read_error = ret ? ERROR_SUCCESS : GetLastError();
        ok(ret && bytes == (before ? sizeof(read_value) : 0),
           "Fresh file read returned %d, error %lu, bytes %lu.\n", ret, read_error, bytes);
        if (ret && before && bytes == sizeof(read_value)) ok(read_value == value, "Fresh file data differs from retained write.\n");
    }
    CloseHandle(fresh);
    fresh = INVALID_HANDLE_VALUE;
    write_error = ERROR_INVALID_HANDLE;
    if (!directory)
    {
        fresh = CreateFileA(path, FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, 0, NULL);
        write_error = fresh == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
        ok(write_error == ERROR_ACCESS_DENIED, "Post-update file write-open negative control returned %lu.\n", write_error);
    }
    ok(control == expected->control, "File descriptor control %#x, expected %#x.\n", control, expected->control);
    ok(!strcmp(dacl_text, expected->dacl), "File DACL [%s], expected [%s].\n", dacl_text, expected->dacl);
    ok(!strcmp(sacl_text, expected->sacl), "File SACL [%s], expected [%s].\n", sacl_text, expected->sacl);
    complete = TRUE;
done:
    if (fresh != INVALID_HANDLE_VALUE) CloseHandle(fresh);
    free(fresh_sd);
    free(sd);
    winetest_pop_context();
    return complete;
}

static void test_file_sacl_privilege_matrix(HANDLE *tokens)
{
    static const struct file_sacl_matrix_result results[] =
    {
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_PROTECTED | SE_SACL_AUTO_INHERITED,
         "0:1/09/00000002/WD;1:0/03/001f01ff/WD;", "0:2/c3/00000002/WD;"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED,
         "0:1/00/00000002/WD;1:0/00/001f01ff/WD;", "0:2/40/00000001/WD;1:17/00/00000001/IL8192;"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED,
         "0:1/09/00000002/WD;1:0/03/001f01ff/WD;", "null"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED,
         "0:1/00/00000002/WD;1:0/00/001f01ff/WD;", "null"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_SACL_PROTECTED,
         "0:1/09/00000002/WD;1:0/03/001f01ff/WD;", "0:2/40/00000001/WD;"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT,
         "0:1/09/00000002/WD;1:0/03/001f01ff/WD;", "null"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT,
         "0:1/00/00000002/WD;1:0/00/001f01ff/WD;", "null"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_SACL_AUTO_INHERITED,
         "0:1/00/00000002/WD;1:0/00/001f01ff/WD;", "0:2/d0/00000002/WD;"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_PROTECTED | SE_SACL_AUTO_INHERITED,
         "0:1/09/00000002/WD;1:0/03/001f01ff/WD;", "null"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_AUTO_INHERITED,
         "0:1/00/00000002/WD;1:0/00/001f01ff/WD;", "absent"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_PROTECTED,
         "0:1/09/00000002/WD;1:0/03/001f01ff/WD;", "null"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT,
         "0:1/00/00000002/WD;1:0/00/001f01ff/WD;", "absent"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_PROTECTED,
         "0:1/09/00000002/WD;1:0/03/001f01ff/WD;", "0:2/c3/00000002/WD;"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT,
         "0:1/00/00000002/WD;1:0/00/001f01ff/WD;", "0:2/c0/00000002/WD;"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_AUTO_INHERITED,
         "0:1/09/00000002/WD;1:0/03/001f01ff/WD;", "0:2/c3/00000002/WD;"},
        {SE_SELF_RELATIVE | SE_DACL_PRESENT | SE_SACL_PRESENT | SE_DACL_PROTECTED | SE_SACL_AUTO_INHERITED,
         "0:1/09/00000002/WD;1:0/03/001f01ff/WD;", "null"}
    };
    static const BYTE expected[3][6][2][9] =
    {
        {
            {{0, 1, 2, 3, 4, 3, 5, 6, 7}, {8, 1, 2, 3, 4, 3, 5, 6, 9}},
            {{10, 1, 2, 3, 4, 3, 5, 6, 11}, {10, 1, 2, 3, 4, 3, 5, 6, 11}},
            {{12, 1, 2, 3, 4, 3, 5, 6, 13}, {10, 1, 2, 3, 4, 3, 5, 6, 11}},
            {{10, 1, 2, 3, 4, 3, 5, 6, 11}, {10, 1, 2, 3, 4, 3, 5, 6, 11}},
            {{10, 1, 2, 3, 4, 3, 5, 6, 11}, {10, 1, 2, 3, 4, 3, 5, 6, 11}},
            {{10, 1, 2, 3, 4, 3, 5, 6, 11}, {10, 1, 2, 3, 4, 3, 5, 6, 11}}
        },
        {
            {{0, 1, 2, 3, 4, 3, 5, 6, 7}, {8, 1, 2, 3, 4, 3, 5, 6, 9}},
            {{10, 1, 2, 3, 4, 3, 5, 6, 11}, {10, 1, 2, 3, 4, 3, 5, 6, 11}},
            {{12, 1, 2, 3, 4, 3, 5, 6, 13}, {10, 1, 2, 3, 4, 3, 5, 6, 11}},
            {{10, 1, 2, 3, 4, 3, 5, 6, 11}, {10, 1, 2, 3, 4, 3, 5, 6, 11}},
            {{0, 1, 2, 3, 4, 3, 5, 6, 7}, {8, 1, 2, 3, 4, 3, 5, 6, 9}},
            {{10, 1, 2, 3, 4, 3, 5, 6, 11}, {10, 1, 2, 3, 4, 3, 5, 6, 11}}
        },
        {
            {{0, 1, 2, 3, 4, 3, 5, 6, 7}, {8, 1, 2, 3, 4, 3, 5, 6, 9}},
            {{10, 1, 2, 3, 4, 3, 5, 6, 11}, {10, 1, 2, 3, 4, 3, 5, 6, 11}},
            {{12, 1, 2, 3, 4, 3, 5, 6, 13}, {10, 1, 2, 3, 4, 3, 5, 6, 11}},
            {{10, 1, 2, 3, 4, 3, 5, 6, 11}, {10, 1, 2, 3, 4, 3, 5, 6, 11}},
            {{10, 1, 2, 3, 4, 3, 5, 6, 11}, {10, 1, 2, 3, 4, 3, 5, 6, 11}},
            {{10, 1, 2, 3, 4, 3, 5, 6, 11}, {10, 1, 2, 3, 4, 3, 5, 6, 11}}
        }
    };
    static const BYTE unprotected_expected[2][9] =
    {
        {14, 1, 2, 3, 4, 3, 5, 6, 7},
        {15, 1, 2, 3, 4, 3, 5, 6, 9}
    };
    static const char *names[] = {"", "explicit", "nested", "nested\\file", "protected", "protected\\file", "open", "open\\file", "new"};
    static const BOOL directories[] = {TRUE, FALSE, TRUE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE};
    static const char *strings[] = {"D:P(A;OICI;FA;;;WD)S:P", "D:P(D;OIIO;0x2;;;WD)(A;OICI;FA;;;WD)",
        "D:P(D;;0x2;;;WD)(A;;FA;;;WD)", "D:(D;OIIO;0x2;;;WD)(A;OICI;FA;;;WD)",
        "S:(AU;SA;0x1;;;WD)", "S:P(AU;SA;0x1;;;WD)", "S:", "S:P", "S:P(AU;OICISAFA;0x2;;;WD)",
        "D:P(A;OICI;FA;;;WD)S:P(ML;;NW;;;ME)", "D:(D;;0x2;;;WD)(A;;FA;;;WD)"};
    PSECURITY_DESCRIPTOR descriptors[ARRAY_SIZE(strings)] = {0}, before[8] = {0}, queried = NULL, unprotected_before = NULL;
    HANDLE handles[8], ordinary = INVALID_HANDLE_VALUE, security_only = INVALID_HANDLE_VALUE;
    HANDLE fresh = INVALID_HANDLE_VALUE, child = INVALID_HANDLE_VALUE, container_handle = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, FALSE};
    SECURITY_DESCRIPTOR_CONTROL control = 0, expected_control;
    OBJECT_BASIC_INFORMATION object_info;
    BOOL created[ARRAY_SIZE(names)] = {0}, ret, present, defaulted, reserved = FALSE, container_created = FALSE;
    char temp[MAX_PATH], container[MAX_PATH] = "", path[MAX_PATH] = "", paths[ARRAY_SIZE(names)][MAX_PATH];
    char volume[MAX_PATH], filesystem[MAX_PATH];
    DWORD size, flags, i, j, labels, mode, kind, remove, result, revision, setters = 0, snapshots = 0;
    DWORD unprotected_setters = 0, unprotected_snapshots = 0;
    ACCESS_MASK access;
    ACL *audit;
    ACE_HEADER *ace;
    NTSTATUS status;

    winetest_push_context("File SACL privilege matrix");
    for (i = 0; i < ARRAY_SIZE(handles); ++i) handles[i] = INVALID_HANDLE_VALUE;
    ret = SetThreadToken(NULL, tokens[1]);
    ok(ret, "File SACL setup impersonation failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    size = GetTempPathA(ARRAY_SIZE(temp), temp);
    ok(size && size < ARRAY_SIZE(temp), "File SACL temporary path returned %lu.\n", size);
    if (!size || size >= ARRAY_SIZE(temp)) goto done;
    ret = GetVolumePathNameA(temp, volume, ARRAY_SIZE(volume));
    ok(ret, "File SACL volume path failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ret = GetVolumeInformationA(volume, NULL, 0, NULL, NULL, &flags, filesystem, ARRAY_SIZE(filesystem));
    ok(ret, "File SACL volume query failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    trace("File SACL fixture volume %s filesystem %s flags %#lx.\n", volume, filesystem, flags);
    if (lstrcmpiA(filesystem, "NTFS") || (flags & FILE_READ_ONLY_VOLUME))
    {
        win_skip("File SACL matrix needs a writable NTFS temporary volume.\n");
        goto done;
    }
    for (i = 0; i < ARRAY_SIZE(strings); ++i)
    {
        ret = ConvertStringSecurityDescriptorToSecurityDescriptorA(strings[i], SDDL_REVISION_1, &descriptors[i], NULL);
        ok(ret, "File SACL descriptor %lu construction failed: %lu.\n", i, GetLastError());
        if (!ret) goto done;
    }
    ret = GetTempFileNameA(temp, "sac", 0, container);
    ok(ret, "File SACL temporary reservation failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    reserved = TRUE;
    ret = DeleteFileA(container);
    ok(ret, "File SACL reservation removal failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    reserved = FALSE;
    if (strlen(container) + sizeof("\\root\\protected\\file") > ARRAY_SIZE(paths[0]))
    {
        win_skip("File SACL temporary path is too long.\n");
        goto done;
    }
    attributes.lpSecurityDescriptor = descriptors[0];
    ret = CreateDirectoryA(container, &attributes);
    ok(ret, "File SACL controlled container creation failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    container_created = TRUE;
    container_handle = CreateFileA(container, READ_CONTROL | WRITE_DAC | DELETE | FILE_LIST_DIRECTORY | ACCESS_SYSTEM_SECURITY,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(container_handle != INVALID_HANDLE_VALUE, "File SACL controlled container handle failed: %lu.\n", GetLastError());
    if (container_handle == INVALID_HANDLE_VALUE) goto done;
    status = NtSetSecurityObject(container_handle, SACL_SECURITY_INFORMATION | PROTECTED_SACL_SECURITY_INFORMATION, descriptors[7]);
    ok(!status, "File SACL controlled container audit setup returned %#lx.\n", status);
    if (status) goto done;
    queried = file_sacl_matrix_descriptor(container_handle);
    if (!queried) goto done;
    ret = GetSecurityDescriptorControl(queried, &control, &revision) &&
          GetSecurityDescriptorSacl(queried, &present, &audit, &defaulted);
    ok(ret && (control & SE_SACL_PROTECTED) && present && (!audit || !audit->AceCount),
       "File SACL controlled container audit is not protected and empty.\n");
    if (!ret || !(control & SE_SACL_PROTECTED) || !present || (audit && audit->AceCount)) goto done;
    free(queried);
    queried = NULL;
    sprintf(path, "%s\\root", container);
    for (i = 0; i < ARRAY_SIZE(names); ++i)
        if (i) sprintf(paths[i], "%s\\%s", path, names[i]);
        else strcpy(paths[i], path);
    for (i = 0; i < ARRAY_SIZE(handles); ++i)
    {
        attributes.lpSecurityDescriptor = descriptors[i == 1 ? 9 : 0];
        access = READ_CONTROL | WRITE_DAC | DELETE | FILE_READ_DATA | ACCESS_SYSTEM_SECURITY;
        if (directories[i])
        {
            ret = CreateDirectoryA(paths[i], &attributes);
            ok(ret, "File SACL directory %lu creation failed: %lu.\n", i, GetLastError());
            if (!ret) goto done;
            created[i] = TRUE;
        }
        else access |= FILE_WRITE_DATA;
        handles[i] = CreateFileA(paths[i], access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                directories[i] ? NULL : &attributes, directories[i] ? OPEN_EXISTING : CREATE_NEW,
                                FILE_FLAG_BACKUP_SEMANTICS, NULL);
        ok(handles[i] != INVALID_HANDLE_VALUE, "File SACL retained handle %lu failed: %lu.\n", i, GetLastError());
        if (handles[i] == INVALID_HANDLE_VALUE) goto done;
        created[i] = TRUE;
        status = NtQueryObject(handles[i], ObjectBasicInformation, &object_info, sizeof(object_info), &size);
        ok(!status, "File SACL retained grant %lu query returned %#lx.\n", i, status);
        if (status) goto done;
        access |= SYNCHRONIZE | FILE_READ_ATTRIBUTES;
        ok(object_info.GrantedAccess == access, "File SACL retained grant %lu is %#lx, expected %#lx.\n",
           i, object_info.GrantedAccess, access);
    }
    ordinary = CreateFileA(path, READ_CONTROL | FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    security_only = CreateFileA(path, ACCESS_SYSTEM_SECURITY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(ordinary != INVALID_HANDLE_VALUE && security_only != INVALID_HANDLE_VALUE, "File SACL setter handle setup failed: %lu.\n", GetLastError());
    if (ordinary == INVALID_HANDLE_VALUE || security_only == INVALID_HANDLE_VALUE) goto done;
    for (i = 0; i < 2; ++i)
    {
        status = NtQueryObject(i ? security_only : ordinary, ObjectBasicInformation, &object_info, sizeof(object_info), &size);
        ok(!status, "File SACL setter grant %lu query returned %#lx.\n", i, status);
        if (status) goto done;
        ok(!!(object_info.GrantedAccess & ACCESS_SYSTEM_SECURITY) == !!i, "File SACL setter %lu has wrong SYS grant %#lx.\n", i, object_info.GrantedAccess);
        if (i) ok(!(object_info.GrantedAccess & (READ_CONTROL | WRITE_DAC)),
                  "File SACL SYS-only handle unexpectedly grants descriptor access %#lx.\n", object_info.GrantedAccess);
        access = (i ? ACCESS_SYSTEM_SECURITY : READ_CONTROL | FILE_LIST_DIRECTORY) | SYNCHRONIZE | FILE_READ_ATTRIBUTES;
        ok(object_info.GrantedAccess == access, "File SACL setter grant %lu is %#lx, expected %#lx.\n",
           i, object_info.GrantedAccess, access);
    }
    for (i = 0; i < ARRAY_SIZE(handles); ++i)
    {
        status = NtSetSecurityObject(handles[i], DACL_SECURITY_INFORMATION |
                  (i == 4 || i >= 6 ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION),
                  descriptors[i == 4 || i == 6 ? 3 : i == 7 ? 10 : directories[i] ? 1 : 2]);
        ok(!status, "File SACL negative DACL %lu setup returned %#lx.\n", i, status);
        if (status) goto done;
        status = NtSetSecurityObject(handles[i], SACL_SECURITY_INFORMATION |
                  (i == 0 || i == 4 ? PROTECTED_SACL_SECURITY_INFORMATION : UNPROTECTED_SACL_SECURITY_INFORMATION),
                  descriptors[i == 1 ? 4 : i == 4 ? 5 : i == 0 ? 7 : 6]);
        ok(!status, "File SACL baseline %lu setup returned %#lx.\n", i, status);
        if (status) goto done;
        before[i] = file_sacl_matrix_descriptor(handles[i]);
        if (!before[i]) goto done;
        ret = GetSecurityDescriptorSacl(before[i], &present, &audit, &defaulted);
        ok(ret, "File SACL baseline %lu ACL query failed.\n", i);
        if (!ret) goto done;
        labels = 0;
        for (j = 0; audit && j < audit->AceCount; ++j)
        {
            ret = GetAce(audit, j, (void **)&ace);
            ok(ret, "File SACL baseline %lu ACE %lu unavailable.\n", i, j);
            if (!ret) goto done;
            if (ace->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE) ++labels;
        }
        if (i == 1)
        {
            ok(present && audit && labels == 1, "File SACL explicit label leaf has %lu labels, expected one.\n", labels);
            if (!present || !audit || labels != 1) goto done;
        }
        ok(present && !!audit == (i == 1 || i == 4) && labels == (i == 1),
           "File SACL baseline %lu present %u ACL %u labels %lu.\n", i, present, !!audit, labels);
        ret = GetSecurityDescriptorControl(before[i], &control, &revision);
        expected_control = (i == 4 || i >= 6 ? 0 : SE_DACL_PROTECTED) | (i == 0 || i == 4 ? SE_SACL_PROTECTED : 0);
        ok(ret && (control & (SE_DACL_PROTECTED | SE_SACL_PROTECTED)) == expected_control,
           "File SACL baseline %lu protection %#x, expected %#x.\n", i, control, expected_control);
        if (!ret || (control & (SE_DACL_PROTECTED | SE_SACL_PROTECTED)) != expected_control) goto done;
    }
    for (mode = 0; mode < 3; ++mode)
        for (kind = 0; kind < 6; ++kind)
        {
            winetest_push_context("token %lu setter %lu", mode, kind);
            ret = SetThreadToken(NULL, tokens[1]);
            ok(ret, "File SACL reset impersonation failed: %lu.\n", GetLastError());
            if (!ret) goto end_case;
            for (i = 0; i < ARRAY_SIZE(handles); ++i)
            {
                status = NtSetSecurityObject(handles[i], DACL_SECURITY_INFORMATION |
                          (i == 4 || i >= 6 ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION), before[i]);
                ok(!status, "File SACL reset DACL %lu returned %#lx.\n", i, status);
                if (status) goto end_case;
                status = NtSetSecurityObject(handles[i], SACL_SECURITY_INFORMATION |
                          (i == 0 || i == 4 ? PROTECTED_SACL_SECURITY_INFORMATION : UNPROTECTED_SACL_SECURITY_INFORMATION), before[i]);
                ok(!status, "File SACL reset audit %lu returned %#lx.\n", i, status);
                if (status) goto end_case;
                queried = file_sacl_matrix_descriptor(handles[i]);
                if (!queried) goto end_case;
                file_sacl_matrix_preserved(before[i], queried, TRUE);
                free(queried);
                queried = NULL;
            }
            for (remove = 0; remove < 2; ++remove)
            {
                winetest_push_context("remove %lu", remove);
                ret = SetThreadToken(NULL, tokens[mode]);
                ok(ret, "File SACL operation impersonation failed: %lu.\n", GetLastError());
                if (!ret) { winetest_pop_context(); goto end_case; }
                for (i = 0; i < ARRAY_SIZE(handles); ++i)
                    if (!directories[i])
                    {
                        fresh = CreateFileA(paths[i], FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                            NULL, OPEN_EXISTING, 0, NULL);
                        result = fresh == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
                        ok(result == ERROR_ACCESS_DENIED, "File SACL pre-update file %lu write-open negative control returned %lu.\n", i, result);
                        if (fresh != INVALID_HANDLE_VALUE) { CloseHandle(fresh); fresh = INVALID_HANDLE_VALUE; }
                        if (result != ERROR_ACCESS_DENIED) { winetest_pop_context(); goto end_case; }
                    }
                fresh = CreateFileA(path, ACCESS_SYSTEM_SECURITY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
                result = fresh == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
                ok(result == (mode == 1 ? ERROR_SUCCESS : ERROR_PRIVILEGE_NOT_HELD),
                   "File SACL fresh SYS open returned %lu.\n", result);
                if (fresh != INVALID_HANDLE_VALUE) { CloseHandle(fresh); fresh = INVALID_HANDLE_VALUE; }
                ret = GetSecurityDescriptorSacl(descriptors[remove ? 7 : 8], &present, &audit, &defaulted);
                ok(ret && present && audit, "File SACL target ACL missing.\n");
                if (!ret || !present || !audit) { winetest_pop_context(); goto end_case; }
                if (kind == 2 || kind == 3)
                {
                    status = NtSetSecurityObject(kind == 2 ? handles[0] : ordinary, SACL_SECURITY_INFORMATION, descriptors[remove ? 7 : 8]);
                    ok(status == (kind == 2 ? STATUS_SUCCESS : STATUS_ACCESS_DENIED),
                       "File SACL native setter returned %#lx.\n", status);
                }
                else
                {
                    if (kind == 4)
                        result = SetNamedSecurityInfoA(path, SE_FILE_OBJECT, SACL_SECURITY_INFORMATION | PROTECTED_SACL_SECURITY_INFORMATION,
                                                       NULL, NULL, NULL, audit);
                    else result = SetSecurityInfo(kind == 5 ? security_only : kind ? ordinary : handles[0], SE_FILE_OBJECT,
                                                  SACL_SECURITY_INFORMATION | PROTECTED_SACL_SECURITY_INFORMATION, NULL, NULL, NULL, audit);
                    ok(result == (kind == 0 || (kind == 4 && mode == 1) ? ERROR_SUCCESS :
                       kind == 4 ? ERROR_PRIVILEGE_NOT_HELD : ERROR_ACCESS_DENIED),
                       "File SACL public setter returned %lu.\n", result);
                }
                ++setters;
                ret = SetThreadToken(NULL, tokens[1]);
                ok(ret, "File SACL observation impersonation failed: %lu.\n", GetLastError());
                if (!ret) { winetest_pop_context(); goto end_case; }
                for (i = 0; i < ARRAY_SIZE(handles); ++i)
                    snapshots += file_sacl_matrix_snapshot(handles[i], paths[i], i, directories[i], before[i],
                                             &results[expected[mode][kind][remove][i]]);
                child = CreateFileA(paths[8], READ_CONTROL | ACCESS_SYSTEM_SECURITY | FILE_READ_DATA,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW, 0, NULL);
                ok(child != INVALID_HANDLE_VALUE, "File SACL new child creation failed: %lu.\n", GetLastError());
                if (child != INVALID_HANDLE_VALUE)
                {
                    created[8] = TRUE;
                    snapshots += file_sacl_matrix_snapshot(child, paths[8], 8, FALSE, NULL, &results[expected[mode][kind][remove][8]]);
                    CloseHandle(child);
                    child = INVALID_HANDLE_VALUE;
                    ret = DeleteFileA(paths[8]);
                    ok(ret, "File SACL new child cleanup failed: %lu.\n", GetLastError());
                    if (ret) created[8] = FALSE;
                }
                winetest_pop_context();
                if (created[8]) goto end_case;
            }
end_case:
            winetest_pop_context();
        }
    ok(setters == 36 && snapshots == 324, "File SACL matrix exercised %lu/36 setters and %lu/324 snapshots.\n", setters, snapshots);
    trace("File SACL matrix exercised %lu/36 setters and %lu/324 snapshots.\n", setters, snapshots);
    for (kind = 0; kind < 2; ++kind)
    {
        winetest_push_context("unprotected setter %lu", kind);
        ret = SetThreadToken(NULL, tokens[1]);
        ok(ret, "Unprotected file SACL impersonation failed: %lu.\n", GetLastError());
        if (!ret) goto end_unprotected_case;
        for (i = 0; i < ARRAY_SIZE(handles); ++i)
        {
            status = NtSetSecurityObject(handles[i], DACL_SECURITY_INFORMATION |
                      (i == 4 || i >= 6 ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION), before[i]);
            ok(!status, "Unprotected file SACL reset DACL %lu returned %#lx.\n", i, status);
            if (status) goto end_unprotected_case;
            status = NtSetSecurityObject(handles[i], SACL_SECURITY_INFORMATION |
                      (i == 0 || i == 4 ? PROTECTED_SACL_SECURITY_INFORMATION : UNPROTECTED_SACL_SECURITY_INFORMATION), before[i]);
            ok(!status, "Unprotected file SACL reset audit %lu returned %#lx.\n", i, status);
            if (status) goto end_unprotected_case;
            queried = file_sacl_matrix_descriptor(handles[i]);
            if (!queried) goto end_unprotected_case;
            file_sacl_matrix_preserved(before[i], queried, TRUE);
            free(queried);
            queried = NULL;
        }
        status = NtSetSecurityObject(handles[0], SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION, descriptors[6]);
        ok(!status, "Unprotected file SACL root setup returned %#lx.\n", status);
        if (status) goto end_unprotected_case;
        unprotected_before = file_sacl_matrix_descriptor(handles[0]);
        if (!unprotected_before) goto end_unprotected_case;
        ret = GetSecurityDescriptorControl(unprotected_before, &control, &revision);
        ok(ret && (control & (SE_DACL_PROTECTED | SE_SACL_PROTECTED)) == SE_DACL_PROTECTED,
           "Unprotected file SACL root control %#x.\n", control);
        if (!ret || (control & (SE_DACL_PROTECTED | SE_SACL_PROTECTED)) != SE_DACL_PROTECTED) goto end_unprotected_case;
        file_sacl_matrix_preserved(before[0], unprotected_before, FALSE);
        for (remove = 0; remove < 2; ++remove)
        {
            winetest_push_context("remove %lu", remove);
            for (i = 0; i < ARRAY_SIZE(handles); ++i)
                if (!directories[i])
                {
                    fresh = CreateFileA(paths[i], FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        NULL, OPEN_EXISTING, 0, NULL);
                    result = fresh == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
                    ok(result == ERROR_ACCESS_DENIED, "Unprotected file SACL pre-update file %lu write-open returned %lu.\n", i, result);
                    if (fresh != INVALID_HANDLE_VALUE) { CloseHandle(fresh); fresh = INVALID_HANDLE_VALUE; }
                    if (result != ERROR_ACCESS_DENIED) { winetest_pop_context(); goto end_unprotected_case; }
                }
            ret = GetSecurityDescriptorSacl(descriptors[remove ? 7 : 8], &present, &audit, &defaulted);
            ok(ret && present && audit, "Unprotected file SACL target ACL missing.\n");
            if (!ret || !present || !audit) { winetest_pop_context(); goto end_unprotected_case; }
            if (kind)
                result = SetNamedSecurityInfoA(path, SE_FILE_OBJECT, SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION,
                                               NULL, NULL, NULL, audit);
            else result = SetSecurityInfo(handles[0], SE_FILE_OBJECT, SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION,
                                          NULL, NULL, NULL, audit);
            ok(!result, "Unprotected file SACL setter returned %lu.\n", result);
            ++unprotected_setters;
            for (i = 0; i < ARRAY_SIZE(handles); ++i)
                unprotected_snapshots += file_sacl_matrix_snapshot(handles[i], paths[i], i, directories[i],
                                                                  i ? before[i] : unprotected_before,
                                                                  &results[unprotected_expected[remove][i]]);
            child = CreateFileA(paths[8], READ_CONTROL | ACCESS_SYSTEM_SECURITY | FILE_READ_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW, 0, NULL);
            ok(child != INVALID_HANDLE_VALUE, "Unprotected file SACL new child creation failed: %lu.\n", GetLastError());
            if (child != INVALID_HANDLE_VALUE)
            {
                created[8] = TRUE;
                unprotected_snapshots += file_sacl_matrix_snapshot(child, paths[8], 8, FALSE, NULL,
                                                                  &results[unprotected_expected[remove][8]]);
                CloseHandle(child);
                child = INVALID_HANDLE_VALUE;
                ret = DeleteFileA(paths[8]);
                ok(ret, "Unprotected file SACL new child cleanup failed: %lu.\n", GetLastError());
                if (ret) created[8] = FALSE;
            }
            winetest_pop_context();
            if (created[8]) goto end_unprotected_case;
        }
end_unprotected_case:
        free(unprotected_before);
        unprotected_before = NULL;
        winetest_pop_context();
    }
    ok(unprotected_setters == 4 && unprotected_snapshots == 36,
       "Unprotected file SACL matrix exercised %lu/4 setters and %lu/36 snapshots.\n", unprotected_setters, unprotected_snapshots);
    trace("Unprotected file SACL matrix exercised %lu/4 setters and %lu/36 snapshots.\n", unprotected_setters, unprotected_snapshots);
done:
    ret = SetThreadToken(NULL, tokens[1]);
    ok(ret, "File SACL cleanup impersonation failed: %lu.\n", GetLastError());
    if (child != INVALID_HANDLE_VALUE) CloseHandle(child);
    if (fresh != INVALID_HANDLE_VALUE) CloseHandle(fresh);
    if (ordinary != INVALID_HANDLE_VALUE) CloseHandle(ordinary);
    if (security_only != INVALID_HANDLE_VALUE) CloseHandle(security_only);
    for (i = 0; i < ARRAY_SIZE(handles); ++i)
        if (handles[i] != INVALID_HANDLE_VALUE)
        {
            status = NtSetSecurityObject(handles[i], DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptors[0]);
            ok(!status, "File SACL cleanup DACL %lu restoration returned %#lx.\n", i, status);
        }
    if (container_handle != INVALID_HANDLE_VALUE)
    {
        status = NtSetSecurityObject(container_handle, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptors[0]);
        ok(!status, "File SACL container cleanup DACL restoration returned %#lx.\n", status);
        CloseHandle(container_handle);
    }
    for (i = ARRAY_SIZE(handles); i-- > 0;)
        if (handles[i] != INVALID_HANDLE_VALUE) CloseHandle(handles[i]);
    for (i = ARRAY_SIZE(created); i-- > 0;)
        if (created[i])
        {
            ret = directories[i] ? RemoveDirectoryA(paths[i]) : DeleteFileA(paths[i]);
            ok(ret, "File SACL object %lu cleanup failed: %lu.\n", i, GetLastError());
        }
    if (container_created) ok(RemoveDirectoryA(container), "File SACL controlled container cleanup failed: %lu.\n", GetLastError());
    if (reserved) ok(DeleteFileA(container), "File SACL reservation cleanup failed: %lu.\n", GetLastError());
    for (i = 0; i < ARRAY_SIZE(before); ++i) free(before[i]);
    for (i = 0; i < ARRAY_SIZE(descriptors); ++i) if (descriptors[i]) LocalFree(descriptors[i]);
    free(queried);
    free(unprotected_before);
    winetest_pop_context();
}

#ifndef TREE_SEC_INFO_SET
#define TREE_SEC_INFO_SET 1
#define TREE_SEC_INFO_RESET 2
#define TREE_SEC_INFO_RESET_KEEP_EXPLICIT 3
#endif

#define ACL_TREE_PRE_POST_ERROR ((PROG_INVOKE_SETTING)6)

typedef VOID (CALLBACK *acl_tree_progress_fn)(LPWSTR, DWORD, PPROG_INVOKE_SETTING, PVOID, BOOL);

enum acl_tree_callback_mode
{
    ACL_TREE_CALLBACK_NONE,
    ACL_TREE_CALLBACK_ROOT_FAILURE,
    ACL_TREE_CALLBACK_FULL,
    ACL_TREE_CALLBACK_ERRORS,
    ACL_TREE_CALLBACK_STOP,
    ACL_TREE_CALLBACK_CANCEL,
    ACL_TREE_CALLBACK_RETRY
};

struct acl_tree_node_result
{
    SECURITY_DESCRIPTOR_CONTROL control;
    const char *dacl;
    DWORD read, write;
};

struct acl_tree_row_result
{
    BYTE registry, phase, variant, form;
    DWORD status;
    enum acl_tree_callback_mode callback_mode;
    BYTE callback_count, nodes[6], callback_nodes[6];
};

static const struct acl_tree_node_result acl_tree_node_results[] =
{
    {SE_SELF_RELATIVE | SE_DACL_PROTECTED | SE_DACL_PRESENT, "0:0/03/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_PRESENT, "0:0/10/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_PRESENT, "0:1/00/00000002/WD;1:0/10/001f01ff/WD;", ERROR_SUCCESS, ERROR_ACCESS_DENIED},
    {SE_SELF_RELATIVE | SE_DACL_PROTECTED | SE_DACL_PRESENT, "0:1/00/00040000/OW;1:1/00/00040000/WD;2:0/03/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/03/00000002/WD;1:0/03/001f01ff/WD;2:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_ACCESS_DENIED},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/09/00000002/WD;1:0/03/001f01ff/WD;2:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:0/03/00120089/WD;1:0/03/001f01ff/WD;2:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:0/10/00120089/WD;1:0/10/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:0/03/001f01ff/WD;1:0/13/00120089/WD;2:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:0/10/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:0/13/00120089/WD;1:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00000002/WD;1:0/10/00120089/WD;2:0/10/001f01ff/WD;", ERROR_SUCCESS, ERROR_ACCESS_DENIED},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00000001/WD;1:0/03/00120089/WD;2:0/03/001f01ff/WD;3:0/13/001f01ff/WD;", ERROR_ACCESS_DENIED, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00000002/WD;1:0/03/00120089/WD;2:0/03/001f01ff/WD;3:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_ACCESS_DENIED},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00000004/WD;1:0/03/00120089/WD;2:0/03/001f01ff/WD;3:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00000008/WD;1:0/03/00120089/WD;2:0/03/001f01ff/WD;3:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00000010/WD;1:0/03/00120089/WD;2:0/03/001f01ff/WD;3:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00000020/WD;1:0/03/00120089/WD;2:0/03/001f01ff/WD;3:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:0/10/00120089/WD;1:0/10/001f01ff/WD;", ERROR_ACCESS_DENIED, ERROR_ACCESS_DENIED},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:0/03/001f01ff/WD;1:0/13/00120089/WD;2:0/13/001f01ff/WD;", ERROR_ACCESS_DENIED, ERROR_ACCESS_DENIED},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:0/10/001f01ff/WD;", ERROR_ACCESS_DENIED, ERROR_ACCESS_DENIED},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00000040/WD;1:0/03/00120089/WD;2:0/03/001f01ff/WD;3:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00000080/WD;1:0/03/00120089/WD;2:0/03/001f01ff/WD;3:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00000100/WD;1:0/03/00120089/WD;2:0/03/001f01ff/WD;3:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00010000/WD;1:0/03/00120089/WD;2:0/03/001f01ff/WD;3:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00020000/OW;1:1/00/00020000/WD;2:0/03/00120089/WD;3:0/03/001f01ff/WD;4:0/13/001f01ff/WD;", ERROR_ACCESS_DENIED, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00040000/OW;1:1/00/00040000/WD;2:0/03/00120089/WD;3:0/03/001f01ff/WD;4:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00080000/WD;1:0/03/00120089/WD;2:0/03/001f01ff/WD;3:0/13/001f01ff/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00100000/WD;1:0/03/00120089/WD;2:0/03/001f01ff/WD;3:0/13/001f01ff/WD;", ERROR_ACCESS_DENIED, ERROR_ACCESS_DENIED},
    {SE_SELF_RELATIVE | SE_DACL_PROTECTED | SE_DACL_PRESENT, "0:0/02/000f003f/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_PRESENT, "0:0/12/000f003f/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_PRESENT, "0:1/00/00000002/WD;1:0/12/000f003f/WD;", ERROR_SUCCESS, ERROR_ACCESS_DENIED},
    {SE_SELF_RELATIVE | SE_DACL_PROTECTED | SE_DACL_PRESENT, "0:1/00/00040000/OW;1:1/00/00040000/WD;2:0/02/000f003f/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/02/00000002/WD;1:0/02/000f003f/WD;2:0/12/000f003f/WD;", ERROR_SUCCESS, ERROR_ACCESS_DENIED},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/12/00000002/WD;1:0/12/000f003f/WD;", ERROR_SUCCESS, ERROR_ACCESS_DENIED},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:1/00/00000002/WD;1:1/12/00000002/WD;2:0/12/000f003f/WD;", ERROR_SUCCESS, ERROR_ACCESS_DENIED},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:0/02/000f003f/WD;1:1/12/00000002/WD;2:0/12/000f003f/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
    {SE_SELF_RELATIVE | SE_DACL_AUTO_INHERITED | SE_DACL_PRESENT, "0:0/12/000f003f/WD;1:1/12/00000002/WD;", ERROR_SUCCESS, ERROR_SUCCESS},
};

static const struct acl_tree_row_result acl_tree_row_results[] =
{
    {0, 0, 0, 0, ERROR_CALL_NOT_IMPLEMENTED, ACL_TREE_CALLBACK_NONE, 0, {0, 1, 2, 3, 1, 0}, {0, 0, 0, 0, 0, 0}},
    {0, 0, 0, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_ROOT_FAILURE, 2, {4, 1, 2, 3, 1, 0}, {2, 0, 0, 0, 0, 0}},
    {0, 0, 1, 0, ERROR_CALL_NOT_IMPLEMENTED, ACL_TREE_CALLBACK_NONE, 0, {0, 1, 2, 3, 1, 0}, {0, 0, 0, 0, 0, 0}},
    {0, 0, 1, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_ROOT_FAILURE, 2, {4, 1, 2, 3, 1, 0}, {2, 0, 0, 0, 0, 0}},
    {0, 0, 2, 0, ERROR_CALL_NOT_IMPLEMENTED, ACL_TREE_CALLBACK_NONE, 0, {0, 1, 2, 3, 1, 0}, {0, 0, 0, 0, 0, 0}},
    {0, 0, 2, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_ROOT_FAILURE, 2, {4, 1, 2, 3, 1, 0}, {2, 0, 0, 0, 0, 0}},
    {0, 0, 3, 0, ERROR_CALL_NOT_IMPLEMENTED, ACL_TREE_CALLBACK_NONE, 0, {0, 1, 2, 3, 1, 0}, {0, 0, 0, 0, 0, 0}},
    {0, 0, 3, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_ROOT_FAILURE, 1, {4, 1, 2, 3, 1, 0}, {2, 0, 0, 0, 0, 0}},
    {0, 0, 4, 0, ERROR_CALL_NOT_IMPLEMENTED, ACL_TREE_CALLBACK_NONE, 0, {0, 1, 2, 3, 1, 0}, {0, 0, 0, 0, 0, 0}},
    {0, 0, 4, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_NONE, 0, {4, 1, 2, 3, 1, 0}, {0, 0, 0, 0, 0, 0}},
    {0, 1, 0, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_ROOT_FAILURE, 2, {5, 1, 2, 3, 1, 0}, {2, 0, 0, 0, 0, 0}},
    {0, 1, 1, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_ROOT_FAILURE, 2, {5, 1, 2, 3, 1, 0}, {2, 0, 0, 0, 0, 0}},
    {0, 1, 2, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_ROOT_FAILURE, 2, {5, 1, 2, 3, 1, 0}, {2, 0, 0, 0, 0, 0}},
    {0, 1, 3, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_ROOT_FAILURE, 1, {5, 1, 2, 3, 1, 0}, {2, 0, 0, 0, 0, 0}},
    {0, 1, 4, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_NONE, 0, {5, 1, 2, 3, 1, 0}, {0, 0, 0, 0, 0, 0}},
    {0, 2, 0, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 2, 1, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 10, 7, 10}, {1, 1, 1, 1, 1, 1}},
    {0, 2, 2, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_STOP, 1, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 2, 3, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_NONE, 0, {6, 7, 7, 10, 7, 10}, {0, 0, 0, 0, 0, 0}},
    {0, 2, 4, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_NONE, 0, {6, 7, 7, 8, 9, 8}, {0, 0, 0, 0, 0, 0}},
    {0, 3, 0, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 11, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 3, 1, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 10, 7, 10}, {1, 1, 1, 1, 1, 1}},
    {0, 4, 0, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 7, {6, 7, 7, 3, 1, 8}, {1, 1, 1, 2, 0, 1}},
    {0, 4, 1, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 7, {6, 7, 7, 3, 1, 10}, {1, 1, 1, 2, 0, 1}},
    {0, 6, 0, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_ROOT_FAILURE, 2, {6, 1, 1, 0, 1, 0}, {2, 0, 0, 0, 0, 0}},
    {0, 6, 1, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_ROOT_FAILURE, 2, {6, 1, 1, 0, 1, 0}, {2, 0, 0, 0, 0, 0}},
    {0, 6, 2, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_ROOT_FAILURE, 2, {6, 1, 1, 0, 1, 0}, {2, 0, 0, 0, 0, 0}},
    {0, 6, 3, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 6, 4, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 6, 5, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 7, 0, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 7, 1, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 7, 2, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_ROOT_FAILURE, 2, {6, 1, 1, 0, 1, 0}, {2, 0, 0, 0, 0, 0}},
    {0, 7, 3, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 7, 4, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 7, 5, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 7, 6, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 7, 7, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 7, 8, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {6, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 0, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {12, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 1, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {13, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 2, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {14, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 3, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {15, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 4, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {16, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 5, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {17, 18, 18, 19, 20, 19}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 6, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {21, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 7, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {22, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 8, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {23, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 9, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {24, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 10, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {25, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 11, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {26, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 12, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {27, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {0, 5, 13, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {28, 7, 7, 8, 9, 8}, {1, 1, 1, 1, 1, 1}},
    {1, 0, 0, 0, ERROR_CALL_NOT_IMPLEMENTED, ACL_TREE_CALLBACK_NONE, 0, {29, 30, 31, 32, 30, 29}, {0, 0, 0, 0, 0, 0}},
    {1, 0, 0, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 7, {33, 34, 35, 32, 30, 36}, {1, 1, 1, 2, 0, 1}},
    {1, 0, 1, 0, ERROR_CALL_NOT_IMPLEMENTED, ACL_TREE_CALLBACK_NONE, 0, {29, 30, 31, 32, 30, 29}, {0, 0, 0, 0, 0, 0}},
    {1, 0, 1, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 7, {33, 34, 34, 32, 30, 34}, {1, 1, 1, 2, 0, 1}},
    {1, 0, 2, 0, ERROR_CALL_NOT_IMPLEMENTED, ACL_TREE_CALLBACK_NONE, 0, {29, 30, 31, 32, 30, 29}, {0, 0, 0, 0, 0, 0}},
    {1, 0, 2, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_STOP, 1, {33, 34, 35, 32, 30, 36}, {1, 1, 1, 2, 0, 1}},
    {1, 0, 3, 0, ERROR_CALL_NOT_IMPLEMENTED, ACL_TREE_CALLBACK_NONE, 0, {29, 30, 31, 32, 30, 29}, {0, 0, 0, 0, 0, 0}},
    {1, 0, 3, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_ERRORS, 1, {33, 34, 34, 32, 30, 34}, {0, 0, 0, 2, 0, 0}},
    {1, 0, 4, 0, ERROR_CALL_NOT_IMPLEMENTED, ACL_TREE_CALLBACK_NONE, 0, {29, 30, 31, 32, 30, 29}, {0, 0, 0, 0, 0, 0}},
    {1, 0, 4, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_NONE, 0, {33, 34, 35, 32, 30, 36}, {0, 0, 0, 0, 0, 0}},
    {1, 1, 0, 1, ERROR_ACCESS_DENIED, ACL_TREE_CALLBACK_CANCEL, 1, {33, 30, 34, 32, 30, 29}, {1, 1, 1, 2, 0, 1}},
    {1, 2, 0, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_RETRY, 4, {33, 34, 34, 29, 30, 34}, {1, 1, 1, 2, 0, 1}},
    {1, 3, 0, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 7, {33, 35, 34, 32, 30, 36}, {1, 1, 1, 2, 0, 1}},
    {1, 4, 0, 1, ERROR_SUCCESS, ACL_TREE_CALLBACK_FULL, 9, {33, 34, 35, 36, 37, 36}, {1, 1, 1, 1, 1, 1}},
};

static const BYTE acl_tree_registry_baseline[] = {29, 30, 31, 32, 30, 29};
static const BYTE acl_tree_registry_reset[] = {33, 34, 34, 32, 30, 34};

struct acl_tree_progress_state
{
    WCHAR (*paths)[MAX_PATH];
    UINT count;
    BOOL stop_notifications;
    UINT action, action_count;
    HANDLE repair_handle;
    PSECURITY_DESCRIPTOR repair_descriptor;
    DWORD repair_status;
    struct { UINT node; DWORD status; PROG_INVOKE_SETTING setting; BOOL security_set; } records[32];
};

static struct acl_tree_progress_state *acl_tree_active_progress;
static DWORD acl_tree_set_raw(BOOL registry, HANDLE handle, PSECURITY_DESCRIPTOR descriptor);

static void CALLBACK acl_tree_progress(LPWSTR name, DWORD status, PPROG_INVOKE_SETTING setting,
                                      PVOID args, BOOL security_set)
{
    struct acl_tree_progress_state *state = acl_tree_active_progress;
    UINT i, node = 6, index;

    ok(state && args == state && name && setting, "Tree callback arguments are invalid.\n");
    if (!state || args != state || !name || !setting) return;
    index = state->count++;
    ok(index < ARRAY_SIZE(state->records), "Tree callback count exceeded its bound.\n");
    if (index >= ARRAY_SIZE(state->records))
    {
        *setting = state->action ? ProgressCancelOperation : ProgressInvokeNever;
        return;
    }
    for (i = 0; i < 6; ++i)
        if (!lstrcmpiW(name, state->paths[i])) { node = i; break; }
    state->records[index].node = node;
    state->records[index].status = status;
    state->records[index].setting = *setting;
    state->records[index].security_set = security_set;
    if (node == 6) trace("ACL_TREE_CALLBACK_NAME %s\n", wine_dbgstr_w(name));
    if (state->stop_notifications) *setting = ProgressInvokeNever;
    if (state->action == 1 && !state->action_count)
    {
        ++state->action_count;
        *setting = ProgressCancelOperation;
    }
    if (state->action == 2)
    {
        *setting = ProgressInvokeEveryObject;
        if (node == 3 && status && !state->action_count)
        {
            ++state->action_count;
            state->repair_status = acl_tree_set_raw(TRUE, state->repair_handle, state->repair_descriptor);
            ok(!state->repair_status, "Tree callback owned-node repair returned %lu.\n", state->repair_status);
            *setting = state->repair_status ? ProgressCancelOperation : ProgressRetryOperation;
        }
    }
}

static void acl_tree_check_progress(const struct acl_tree_progress_state *state,
                                    const struct acl_tree_row_result *expected, PROG_INVOKE_SETTING invocation)
{
    UINT counts[6] = {0}, starts[6] = {0}, ends[6] = {0}, parents[6] = {0, 0, 0, 0, 3, 0};
    BOOL first_child[6] = {0}, seen_children[6] = {0}, seen_success[6] = {0}, failed_first[6] = {0};
    UINT i, node, parent, kind, needed;
    enum acl_tree_callback_mode mode = expected->callback_mode;

    ok(state->count <= ARRAY_SIZE(state->records), "Tree callback output exceeds its bound.\n");
    if (state->count > ARRAY_SIZE(state->records)) return;
    if (mode != ACL_TREE_CALLBACK_FULL && mode != ACL_TREE_CALLBACK_RETRY)
        ok(state->count == expected->callback_count, "Tree callback count %u, expected %u.\n",
           state->count, expected->callback_count);
    for (i = 0; i < state->count; ++i)
    {
        node = state->records[i].node;
        ok(node < 6, "Tree callback %u has unknown node %u.\n", i, node);
        if (node >= 6) continue;
        ok(state->records[i].setting == invocation, "Tree callback %u setting %u, expected %u.\n",
           i, state->records[i].setting, invocation);
        if (mode == ACL_TREE_CALLBACK_ROOT_FAILURE)
        {
            ok(!node && state->records[i].status == ERROR_ACCESS_DENIED && state->records[i].security_set,
               "Tree root failure callback %u returned node %u, status %lu, set %u.\n", i, node,
               state->records[i].status, state->records[i].security_set);
            continue;
        }
        kind = expected->callback_nodes[node];
        ok(kind != 0, "Tree unexpected callback for node %u.\n", node);
        ok(state->records[i].status == (kind == 2 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS),
           "Tree node %u callback status %lu, expected %u.\n", node, state->records[i].status,
           kind == 2 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS);
        if (mode == ACL_TREE_CALLBACK_STOP || mode == ACL_TREE_CALLBACK_CANCEL)
        {
            ok(node && state->records[i].security_set == (kind == 1),
               "Tree initial callback node %u set %u, expected %u.\n",
               node, state->records[i].security_set, kind == 1);
            continue;
        }
        if (mode == ACL_TREE_CALLBACK_ERRORS)
        {
            ok(kind == 2 && !state->records[i].security_set,
               "Tree error-only callback node %u has kind %u, set %u.\n", node, kind, state->records[i].security_set);
            ++counts[node];
            continue;
        }
        if (mode != ACL_TREE_CALLBACK_FULL && mode != ACL_TREE_CALLBACK_RETRY) continue;
        parent = parents[node];
        if (!counts[node])
        {
            starts[node] = i;
            if (node)
            {
                first_child[node] = !seen_children[parent];
                if (kind == 2 && first_child[node]) failed_first[parent] = TRUE;
                if (kind == 1)
                {
                    if (failed_first[parent] && !seen_success[parent])
                    {
                        first_child[node] = state->records[i].security_set;
                        trace("INFO_NEEDED: Tree first successful child after an initial error may omit its pre-callback.\n");
                    }
                    seen_success[parent] = TRUE;
                }
                seen_children[parent] = TRUE;
            }
        }
        needed = kind == 2 ? FALSE : !node || first_child[node] || counts[node] != 0;
        ok(state->records[i].security_set == needed,
           "Tree node %u callback %u set %u, expected %u.\n", node, counts[node], state->records[i].security_set, needed);
        ++counts[node];
        ends[node] = i;
    }
    if (mode == ACL_TREE_CALLBACK_FULL || mode == ACL_TREE_CALLBACK_RETRY || mode == ACL_TREE_CALLBACK_ERRORS)
    {
        for (node = 0; node < 6; ++node)
        {
            kind = expected->callback_nodes[node];
            if (mode == ACL_TREE_CALLBACK_ERRORS) needed = kind == 2;
            else if (mode == ACL_TREE_CALLBACK_RETRY && (!node || (!counts[node] && node != 3))) needed = 0;
            else needed = !kind ? 0 : kind == 2 || !node || first_child[node] ? 1 : 2;
            ok(counts[node] == needed, "Tree node %u callback count %u, expected %u.\n", node, counts[node], needed);
            if (!node || !counts[node]) continue;
            parent = parents[node];
            if (counts[parent] && expected->callback_nodes[parent] == 1)
            {
                ok(ends[node] < ends[parent], "Tree child %u completed after parent %u.\n", node, parent);
                if (counts[parent] == 2)
                    ok(starts[parent] < starts[node], "Tree child %u began before parent %u pre-callback.\n", node, parent);
            }
        }
    }
    if (mode == ACL_TREE_CALLBACK_RETRY)
        ok(state->count && state->records[state->count - 1].node == 3 &&
           state->records[state->count - 1].status == ERROR_ACCESS_DENIED &&
           !state->records[state->count - 1].security_set,
           "Tree retry callback prefix did not end at the owned denied branch.\n");
}

static PSECURITY_DESCRIPTOR acl_tree_query(BOOL registry, HANDLE handle)
{
    const SECURITY_INFORMATION information = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    PSECURITY_DESCRIPTOR descriptor;
    DWORD size = 0, capacity, error;
    BOOL ret;

    if (registry) return registry_matrix_descriptor((HKEY)handle, FALSE);
    ret = GetKernelObjectSecurity(handle, information, NULL, 0, &size);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER && size >= sizeof(SECURITY_DESCRIPTOR_RELATIVE),
       "Tree descriptor sizing returned %d, error %lu, size %lu.\n", ret, error, size);
    if (ret || error != ERROR_INSUFFICIENT_BUFFER || size < sizeof(SECURITY_DESCRIPTOR_RELATIVE)) return NULL;
    capacity = size;
    descriptor = malloc(capacity);
    ok(!!descriptor, "Tree descriptor allocation failed.\n");
    if (!descriptor) return NULL;
    ret = GetKernelObjectSecurity(handle, information, descriptor, capacity, &size);
    ret = ret && size >= sizeof(SECURITY_DESCRIPTOR_RELATIVE) && size <= capacity;
    if (ret) ret = RtlValidRelativeSecurityDescriptor(descriptor, size, information);
    ok(ret, "Tree descriptor query or bounds validation failed.\n");
    if (!ret) { free(descriptor); return NULL; }
    return descriptor;
}

static DWORD acl_tree_set_raw(BOOL registry, HANDLE handle, PSECURITY_DESCRIPTOR descriptor)
{
    SECURITY_DESCRIPTOR_CONTROL control;
    SECURITY_INFORMATION information = DACL_SECURITY_INFORMATION;
    DWORD revision;
    BOOL ret;

    ret = GetSecurityDescriptorControl(descriptor, &control, &revision);
    ok(ret, "Tree source descriptor control failed.\n");
    if (!ret) return GetLastError();
    information |= control & SE_DACL_PROTECTED ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION;
    if (registry) return RegSetKeySecurity((HKEY)handle, information, descriptor);
    if (SetKernelObjectSecurity(handle, information, descriptor)) return ERROR_SUCCESS;
    return GetLastError();
}

static DWORD acl_tree_open(BOOL registry, const WCHAR *path, BOOL directory, ACCESS_MASK access, BOOL exercise)
{
    HANDLE handle;
    HKEY key = NULL;
    DWORD result, bytes, value = 0x12345678;
    BOOL ret;

    if (registry)
    {
        result = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, access, &key);
        if (!result && exercise)
        {
            if (access & KEY_SET_VALUE)
                result = RegSetValueExW(key, L"TreeValue", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
            else result = RegQueryInfoKeyW(key, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
            ok(!result, "Tree granted registry operation returned %lu.\n", result);
        }
        if (key) RegCloseKey(key);
        return result;
    }
    handle = CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, directory ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) return GetLastError();
    result = ERROR_SUCCESS;
    if (exercise && !directory)
    {
        if (access & FILE_WRITE_DATA) ret = WriteFile(handle, &value, sizeof(value), &bytes, NULL);
        else ret = ReadFile(handle, &value, sizeof(value), &bytes, NULL);
        result = ret ? ERROR_SUCCESS : GetLastError();
        ok(ret && ((access & FILE_WRITE_DATA) ? bytes == sizeof(value) : bytes <= sizeof(value)),
           "Tree granted file operation returned %d, error %lu, bytes %lu.\n", ret, result, bytes);
    }
    CloseHandle(handle);
    return result;
}

static BOOL acl_tree_snapshot(BOOL registry, HANDLE handle, const WCHAR *path, BOOL directory,
                             PSECURITY_DESCRIPTOR before, UINT node, const struct acl_tree_node_result *expected)
{
    PSECURITY_DESCRIPTOR descriptor = acl_tree_query(registry, handle);
    SECURITY_DESCRIPTOR_CONTROL control;
    ACL *dacl;
    PSID sid, old_sid;
    BOOL ret, present, defaulted, old_defaulted;
    DWORD revision, read_status, write_status;
    char text[1024];

    if (!descriptor) return FALSE;
    ret = GetSecurityDescriptorOwner(descriptor, &sid, &defaulted) &&
          GetSecurityDescriptorOwner(before, &old_sid, &old_defaulted);
    ok(ret && sid && old_sid && EqualSid(sid, old_sid) && defaulted == old_defaulted,
       "Tree owner changed at node %u.\n", node);
    ret = GetSecurityDescriptorGroup(descriptor, &sid, &defaulted) &&
          GetSecurityDescriptorGroup(before, &old_sid, &old_defaulted);
    ok(ret && sid && old_sid && EqualSid(sid, old_sid) && defaulted == old_defaulted,
       "Tree group changed at node %u.\n", node);
    ret = GetSecurityDescriptorControl(descriptor, &control, &revision) &&
          GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted);
    ok(ret, "Tree descriptor fields failed at node %u.\n", node);
    if (!ret) { free(descriptor); return FALSE; }
    registry_matrix_acl_text(dacl, present, text, sizeof(text));
    read_status = acl_tree_open(registry, path, directory, READ_CONTROL | (registry ? KEY_QUERY_VALUE : FILE_READ_DATA), TRUE);
    write_status = acl_tree_open(registry, path, directory, registry ? KEY_SET_VALUE : FILE_WRITE_DATA, TRUE);
    ok(control == expected->control, "Tree node %u control %#x, expected %#x.\n", node, control, expected->control);
    ok(!strcmp(text, expected->dacl), "Tree node %u DACL [%s], expected [%s].\n", node, text, expected->dacl);
    ok(read_status == expected->read, "Tree node %u read status %lu, expected %lu.\n", node, read_status, expected->read);
    ok(write_status == expected->write, "Tree node %u write status %lu, expected %lu.\n", node, write_status, expected->write);
    free(descriptor);
    return TRUE;
}

static HANDLE acl_tree_context_token(HANDLE source, UINT mode)
{
    HANDLE token = NULL;
    TOKEN_PRIVILEGES adjust, *privileges = NULL;
    LUID change_notify;
    DWORD size = 0, capacity, error, i, enabled = 0;
    BOOL ret, valid = FALSE, found = FALSE;

    ret = LookupPrivilegeValueA(NULL, SE_CHANGE_NOTIFY_NAME, &change_notify);
    ok(ret, "Tree change-notify privilege lookup failed: %lu.\n", GetLastError());
    if (!ret) return NULL;
    ret = DuplicateTokenEx(source, TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES | TOKEN_IMPERSONATE,
                           NULL, SecurityImpersonation, TokenImpersonation, &token);
    ok(ret, "Tree context token duplication failed: %lu.\n", GetLastError());
    if (!ret) return NULL;
    if (mode < 2)
    {
        ret = AdjustTokenPrivileges(token, TRUE, NULL, 0, NULL, NULL);
        ok(ret, "Tree context privilege disable failed: %lu.\n", GetLastError());
        if (!ret) goto done;
    }
    if (mode == 1)
    {
        adjust.PrivilegeCount = 1;
        adjust.Privileges[0].Luid = change_notify;
        adjust.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        SetLastError(0xdeadbeef);
        ret = AdjustTokenPrivileges(token, FALSE, &adjust, 0, NULL, NULL);
        error = GetLastError();
        ok(ret && !error, "Tree change-notify enable returned %d, error %lu.\n", ret, error);
        if (!ret || error) goto done;
    }
    ret = GetTokenInformation(token, TokenPrivileges, NULL, 0, &size);
    error = GetLastError();
    ok(!ret && error == ERROR_INSUFFICIENT_BUFFER && size >= FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges),
       "Tree context privilege sizing returned %d, error %lu, size %lu.\n", ret, error, size);
    if (ret || error != ERROR_INSUFFICIENT_BUFFER || size < FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) goto done;
    capacity = size;
    privileges = malloc(capacity);
    ok(!!privileges, "Tree context privilege allocation failed.\n");
    if (!privileges) goto done;
    ret = GetTokenInformation(token, TokenPrivileges, privileges, capacity, &size);
    valid = ret && size <= capacity && size >= FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges);
    if (valid) valid = privileges->PrivilegeCount <= (size - FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) / sizeof(LUID_AND_ATTRIBUTES);
    ok(valid, "Tree context privilege query or bounds failed.\n");
    if (!valid) goto done;
    for (i = 0; i < privileges->PrivilegeCount; ++i)
    {
        BOOL match = privileges->Privileges[i].Luid.LowPart == change_notify.LowPart &&
                     privileges->Privileges[i].Luid.HighPart == change_notify.HighPart;
        BOOL active = !!(privileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED);
        if (match) found = TRUE;
        if (active) ++enabled;
        if (mode < 2)
        {
            ok(active == (mode == 1 && match), "Tree context %u privilege %lu enabled %u.\n", mode, i, active);
            if (active != (mode == 1 && match)) valid = FALSE;
        }
    }
    ok(found, "Tree context change-notify privilege is absent.\n");
    valid = valid && found;
    ok(mode == 2 || enabled == mode, "Tree context %u enabled %lu privileges.\n", mode, enabled);
done:
    free(privileges);
    if (!valid) { CloseHandle(token); token = NULL; }
    return token;
}

static BOOL acl_tree_replace_handles(WCHAR paths[6][MAX_PATH], const BOOL *directories, const WCHAR *parent,
                                     HANDLE handles[6], HANDLE *parent_handle, BOOL narrow)
{
    HANDLE replacements[7];
    ACCESS_MASK access;
    UINT i, j, count = 0;
    BOOL ret;

    for (i = 0; i < ARRAY_SIZE(replacements); ++i)
    {
        access = READ_CONTROL | WRITE_DAC;
        if (!narrow) access |= i == 6 ? DELETE | FILE_LIST_DIRECTORY : DELETE | FILE_READ_DATA | FILE_WRITE_DATA;
        replacements[i] = CreateFileW(i == 6 ? parent : paths[i], access,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       NULL, OPEN_EXISTING, i == 6 || directories[i] ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL);
        ok(replacements[i] != INVALID_HANDLE_VALUE, "Tree replacement handle %u narrow=%u failed: %lu.\n", i, narrow, GetLastError());
        if (replacements[i] == INVALID_HANDLE_VALUE)
        {
            while (count) CloseHandle(replacements[--count]);
            return FALSE;
        }
        ++count;
    }
    for (i = 0; i < 6; ++i)
    {
        ret = CloseHandle(handles[i]);
        ok(ret, "Tree original handle %u close failed: %lu.\n", i, GetLastError());
        if (!ret)
        {
            for (j = i; j < ARRAY_SIZE(replacements); ++j) CloseHandle(replacements[j]);
            return FALSE;
        }
        handles[i] = replacements[i];
    }
    ret = CloseHandle(*parent_handle);
    ok(ret, "Tree original parent handle close failed: %lu.\n", GetLastError());
    if (!ret)
    {
        CloseHandle(replacements[6]);
        return FALSE;
    }
    *parent_handle = replacements[6];
    return TRUE;
}

static void acl_tree_file_controls(const WCHAR *root, const WCHAR *parent, UINT stage)
{
    static const ACCESS_MASK masks[] = {FILE_ALL_ACCESS, READ_CONTROL | WRITE_DAC, FILE_LIST_DIRECTORY};
    static const WCHAR *names[] = {L".", L"..", L"Inherited", L"Explicit", L"Protected", L"WritableProtected"};
    WCHAR pattern[MAX_PATH], ancestor[MAX_PATH];
    WIN32_FIND_DATAW data;
    PSECURITY_DESCRIPTOR descriptor = NULL;
    SECURITY_DESCRIPTOR_CONTROL control;
    HANDLE search;
    DWORD attributes, result, revision, i, j, k, count = 0, ancestors = 0;
    BOOL ret, seen[ARRAY_SIZE(names)] = {0};

    attributes = GetFileAttributesW(root);
    result = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    ok(!result && (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
       "Tree root stage %u attributes %#lx, error %lu.\n", stage, attributes, result);
    for (i = 0; i < 2; ++i)
    {
        const WCHAR *path = i ? parent : root;
        for (j = 0; j < ARRAY_SIZE(masks); ++j)
        {
            result = acl_tree_open(FALSE, path, TRUE, masks[j], FALSE);
            ok(!result, "Tree stage %u parent %lu requested %#lx open returned %lu.\n", stage, i, masks[j], result);
        }
        result = GetNamedSecurityInfoW((WCHAR *)path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                       NULL, NULL, NULL, NULL, &descriptor);
        control = 0;
        if (!result)
        {
            ret = descriptor && IsValidSecurityDescriptor(descriptor) &&
                  GetSecurityDescriptorControl(descriptor, &control, &revision);
            ok(ret, "Tree public descriptor control query failed.\n");
        }
        ok(!result && control == (SE_SELF_RELATIVE | SE_DACL_PRESENT |
           (i || !stage ? SE_DACL_PROTECTED : SE_DACL_AUTO_INHERITED)),
           "Tree stage %u parent %lu descriptor returned %lu, control %#x.\n", stage, i, result, control);
        if (descriptor) { LocalFree(descriptor); descriptor = NULL; }
    }
    ret = lstrlenW(root) + 3 <= ARRAY_SIZE(pattern);
    ok(ret, "Tree enumeration pattern exceeds its bound.\n");
    if (!ret) return;
    wcscpy(pattern, root);
    wcscat(pattern, L"\\*");
    search = FindFirstFileW(pattern, &data);
    result = search == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    ok(!result, "Tree stage %u public enumeration failed: %lu.\n", stage, result);
    if (search != INVALID_HANDLE_VALUE)
    {
        do
        {
            ++count;
            for (k = 0; k < ARRAY_SIZE(names); ++k)
                if (!lstrcmpW(data.cFileName, names[k])) break;
            ok(k < ARRAY_SIZE(names), "Tree enumeration returned unknown name %s.\n", wine_dbgstr_w(data.cFileName));
            if (k < ARRAY_SIZE(names))
            {
                ok(!seen[k], "Tree enumeration repeated %s.\n", wine_dbgstr_w(data.cFileName));
                seen[k] = TRUE;
                ok(!!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == (k < 2 || k >= 4),
                   "Tree enumeration name %s has attributes %#lx.\n", wine_dbgstr_w(data.cFileName), data.dwFileAttributes);
            }
            if (count >= 16) break;
            ret = FindNextFileW(search, &data);
            if (!ret) result = GetLastError();
        } while (ret);
        ok(count < 16, "Tree public enumeration exceeded its owned-tree bound.\n");
        ok(FindClose(search), "Tree public enumeration close failed: %lu.\n", GetLastError());
    }
    ok(count == ARRAY_SIZE(names) && result == ERROR_NO_MORE_FILES,
       "Tree stage %u enumeration completed %lu entries, status %lu.\n", stage, count, result);
    for (k = 0; k < ARRAY_SIZE(names); ++k)
        ok(seen[k], "Tree enumeration did not return %s.\n", wine_dbgstr_w(names[k]));
    wcscpy(ancestor, root);
    for (i = 0; root[i]; ++i)
        if (root[i] == '\\' || root[i] == '/')
        {
            if (ancestors >= MAX_PATH / 2)
            {
                ok(FALSE, "Tree ancestor controls exceeded their bound.\n");
                break;
            }
            ancestor[i + 1] = 0;
            result = acl_tree_open(FALSE, ancestor, TRUE, FILE_TRAVERSE, FALSE);
            ok(!result, "Tree stage %u ancestor %lu requested FILE_TRAVERSE returned %lu.\n", stage, ancestors++, result);
            wcscpy(ancestor, root);
        }
}

static void test_acl_tree_operations(HANDLE token, HANDLE source)
{
    static const WCHAR *names[] = {L"", L"Inherited", L"Explicit", L"Protected", L"Protected\\Child", L"WritableProtected"};
    static const BOOL directories[] = {TRUE, FALSE, FALSE, TRUE, FALSE, TRUE};
    static const char *file_strings[] =
    {
        "D:P(A;OICI;FA;;;WD)", "D:AI(A;ID;FA;;;WD)", "D:AI(D;;0x2;;;WD)(A;ID;FA;;;WD)",
        "D:P(D;;0x40000;;;OW)(D;;0x40000;;;WD)(A;OICI;FA;;;WD)",
        "D:(D;OICI;0x2;;;WD)(A;OICI;FA;;;WD)",
        "D:(D;OIIO;0x2;;;WD)(A;OICI;FA;;;WD)",
        "D:(A;OICI;FR;;;WD)(A;OICI;FA;;;WD)"
    };
    static const char *key_strings[] =
    {
        "D:P(A;CI;KA;;;WD)", "D:AI(A;CIID;KA;;;WD)", "D:AI(D;;0x2;;;WD)(A;CIID;KA;;;WD)",
        "D:P(D;;0x40000;;;OW)(D;;0x40000;;;WD)(A;CI;KA;;;WD)",
        "D:(D;CI;0x2;;;WD)(A;CI;KA;;;WD)"
    };
    static const BYTE baseline[] = {0, 1, 2, 3, 1, 0};
    static const ACCESS_MASK root_rights[] =
    {
        FILE_LIST_DIRECTORY, FILE_ADD_FILE, FILE_ADD_SUBDIRECTORY, FILE_READ_EA, FILE_WRITE_EA,
        FILE_TRAVERSE, FILE_DELETE_CHILD, FILE_READ_ATTRIBUTES, FILE_WRITE_ATTRIBUTES,
        DELETE, READ_CONTROL, WRITE_DAC, WRITE_OWNER, SYNCHRONIZE
    };
    static const ACCESS_MASK sharing_rights[] = {FILE_READ_DATA, FILE_WRITE_DATA, DELETE};
    static const DWORD actions[] = {TREE_SEC_INFO_SET, TREE_SEC_INFO_RESET, TREE_SEC_INFO_RESET_KEEP_EXPLICIT};
    static const PROG_INVOKE_SETTING settings[5][2] =
    {
        {ProgressInvokeNever, ProgressInvokeEveryObject},
        {ProgressInvokeOnError, ACL_TREE_PRE_POST_ERROR},
        {ProgressInvokeEveryObject, ProgressInvokeEveryObject},
        {ProgressInvokeOnError, ProgressInvokeOnError},
        {ProgressInvokeEveryObject, ProgressInvokeNever}
    };
    DWORD (WINAPI *set_a)(LPSTR, SE_OBJECT_TYPE, SECURITY_INFORMATION, PSID, PSID, PACL, PACL, DWORD,
                          acl_tree_progress_fn, PROG_INVOKE_SETTING, PVOID);
    DWORD (WINAPI *set_w)(LPWSTR, SE_OBJECT_TYPE, SECURITY_INFORMATION, PSID, PSID, PACL, PACL, DWORD,
                          acl_tree_progress_fn, PROG_INVOKE_SETTING, PVOID);
    DWORD (WINAPI *reset_a)(LPSTR, SE_OBJECT_TYPE, SECURITY_INFORMATION, PSID, PSID, PACL, PACL, BOOL,
                            acl_tree_progress_fn, PROG_INVOKE_SETTING, PVOID);
    DWORD (WINAPI *reset_w)(LPWSTR, SE_OBJECT_TYPE, SECURITY_INFORMATION, PSID, PSID, PACL, PACL, BOOL,
                            acl_tree_progress_fn, PROG_INVOKE_SETTING, PVOID);
    struct acl_tree_progress_state progress;
    const struct acl_tree_row_result *expected;
    const struct acl_tree_node_result *expected_node;
    PSECURITY_DESCRIPTOR descriptors[7] = {0}, before[6] = {0}, queried = NULL, target = NULL;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, FALSE};
    SECURITY_DESCRIPTOR_CONTROL control;
    WCHAR paths[6][MAX_PATH], callback_paths[6][MAX_PATH], root[MAX_PATH], named[MAX_PATH];
    WCHAR temp[MAX_PATH], roundtrip[MAX_PATH], container[MAX_PATH];
    char named_a[MAX_PATH * 2], target_sddl[256];
    HANDLE handles[6], container_handle, context_tokens[3] = {0}, sharing_handle = INVALID_HANDLE_VALUE;
    HKEY key;
    ACL *dacl, *actual;
    BOOL created[6] = {0}, reserved, container_created, ret, present, defaulted, clean_success = FALSE;
    DWORD result, disposition, revision, i, j, registry, phase, phase_index, variant, form, action_variant, source_index;
    DWORD operations = 0, snapshots = 0, snapshot_start, callback_actions = 0, context_rows = 0, sharing_rows = 0, expected_index;
    PROG_INVOKE_SETTING invocation;
    int saved_debug = winetest_debug;

    winetest_push_context("ACL tree matrix");
    if (winetest_debug < 1) winetest_debug = 1;
    set_a = (void *)GetProcAddress(hmod, "TreeSetNamedSecurityInfoA");
    set_w = (void *)GetProcAddress(hmod, "TreeSetNamedSecurityInfoW");
    reset_a = (void *)GetProcAddress(hmod, "TreeResetNamedSecurityInfoA");
    reset_w = (void *)GetProcAddress(hmod, "TreeResetNamedSecurityInfoW");
    ok(!!set_a, "TreeSetNamedSecurityInfoA export missing.\n");
    ok(!!set_w, "TreeSetNamedSecurityInfoW export missing.\n");
    ok(!!reset_a, "TreeResetNamedSecurityInfoA export missing.\n");
    ok(!!reset_w, "TreeResetNamedSecurityInfoW export missing.\n");
    if (!set_a || !set_w || !reset_a || !reset_w) goto done;
    ret = SetThreadToken(NULL, token);
    ok(ret, "Tree private token activation failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    for (registry = 0; registry < 2; ++registry)
    {
        winetest_push_context("object %lu", registry);
        reserved = container_created = FALSE;
        container_handle = INVALID_HANDLE_VALUE;
        memset(created, 0, sizeof(created));
        for (i = 0; i < ARRAY_SIZE(handles); ++i) handles[i] = INVALID_HANDLE_VALUE;
        for (i = 0; i < (registry ? ARRAY_SIZE(key_strings) : ARRAY_SIZE(file_strings)); ++i)
        {
            ret = ConvertStringSecurityDescriptorToSecurityDescriptorA(registry ? key_strings[i] : file_strings[i],
                                                                        SDDL_REVISION_1, &descriptors[i], NULL);
            ok(ret, "Tree descriptor %lu creation failed: %lu.\n", i, GetLastError());
            if (!ret) goto cleanup_tree;
        }
        if (registry)
            swprintf(container, ARRAY_SIZE(container), L"Software\\WineTree%08lx%08lx", GetCurrentProcessId(), GetTickCount());
        else
        {
            result = GetTempPathW(ARRAY_SIZE(temp), temp);
            ok(result && result < ARRAY_SIZE(temp), "Tree temporary path length %lu.\n", result);
            if (!result || result >= ARRAY_SIZE(temp)) goto cleanup_tree;
            ret = GetTempFileNameW(temp, L"act", 0, container) != 0;
            ok(ret, "Tree temporary reservation failed: %lu.\n", GetLastError());
            if (!ret) goto cleanup_tree;
            reserved = TRUE;
            ret = DeleteFileW(container);
            ok(ret, "Tree reservation deletion failed: %lu.\n", GetLastError());
            if (!ret) goto cleanup_tree;
            reserved = FALSE;
        }
        ret = lstrlenW(container) + ARRAY_SIZE(L"\\Root\\WritableProtected") + ARRAY_SIZE(L"CURRENT_USER\\") < MAX_PATH;
        ok(ret, "Tree root path exceeds fixture bounds.\n");
        if (!ret) goto cleanup_tree;
        attributes.lpSecurityDescriptor = descriptors[0];
        if (registry)
        {
            key = NULL;
            disposition = 0;
            result = RegCreateKeyExW(HKEY_CURRENT_USER, container, 0, NULL, REG_OPTION_VOLATILE,
                                     KEY_ALL_ACCESS, &attributes, &key, &disposition);
            ok(!result && disposition == REG_CREATED_NEW_KEY,
               "Tree parent creation returned %lu, disposition %lu.\n", result, disposition);
            if (result) goto cleanup_tree;
            container_handle = key;
            if (disposition != REG_CREATED_NEW_KEY) goto cleanup_tree;
            container_created = TRUE;
        }
        else
        {
            ret = CreateDirectoryW(container, &attributes);
            ok(ret, "Tree parent creation failed: %lu.\n", GetLastError());
            if (!ret) goto cleanup_tree;
            container_created = TRUE;
            container_handle = CreateFileW(container, READ_CONTROL | WRITE_DAC | DELETE | FILE_LIST_DIRECTORY,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            ok(container_handle != INVALID_HANDLE_VALUE, "Tree parent handle failed: %lu.\n", GetLastError());
            if (container_handle == INVALID_HANDLE_VALUE) goto cleanup_tree;
        }
        queried = acl_tree_query(registry, container_handle);
        if (!queried) goto cleanup_tree;
        ret = GetSecurityDescriptorControl(queried, &control, &revision) && (control & SE_DACL_PROTECTED) &&
              GetSecurityDescriptorDacl(queried, &present, &actual, &defaulted) && present && actual &&
              GetSecurityDescriptorDacl(descriptors[0], &present, &dacl, &defaulted) && present && dacl &&
              actual->AclSize == dacl->AclSize && !memcmp(actual, dacl, dacl->AclSize);
        ok(ret, "Tree parent DACL/protection differs from controlled setup.\n");
        free(queried); queried = NULL;
        if (!ret) goto cleanup_tree;
        wcscpy(root, container);
        wcscat(root, L"\\Root");
        for (i = 0; i < ARRAY_SIZE(handles); ++i)
        {
            wcscpy(paths[i], root);
            if (i) { wcscat(paths[i], L"\\"); wcscat(paths[i], names[i]); }
            if (registry)
            {
                key = NULL;
                disposition = 0;
                result = RegCreateKeyExW(i ? (HKEY)handles[0] : HKEY_CURRENT_USER, i ? names[i] : root,
                                         0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, &attributes, &key, &disposition);
                ok(!result && disposition == REG_CREATED_NEW_KEY, "Tree key %lu creation returned %lu, disposition %lu.\n",
                   i, result, disposition);
                if (result) goto cleanup_tree;
                handles[i] = key;
                if (disposition != REG_CREATED_NEW_KEY) goto cleanup_tree;
                created[i] = TRUE;
                wcscpy(callback_paths[i], L"CURRENT_USER\\");
                wcscat(callback_paths[i], paths[i]);
            }
            else
            {
                if (directories[i])
                {
                    ret = CreateDirectoryW(paths[i], &attributes);
                    ok(ret, "Tree directory %lu creation failed: %lu.\n", i, GetLastError());
                    if (!ret) goto cleanup_tree;
                    created[i] = TRUE;
                }
                handles[i] = CreateFileW(paths[i], READ_CONTROL | WRITE_DAC | DELETE | FILE_READ_DATA | FILE_WRITE_DATA,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &attributes,
                                         directories[i] ? OPEN_EXISTING : CREATE_NEW,
                                         directories[i] ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL);
                ok(handles[i] != INVALID_HANDLE_VALUE, "Tree file handle %lu failed: %lu.\n", i, GetLastError());
                if (handles[i] == INVALID_HANDLE_VALUE) goto cleanup_tree;
                created[i] = TRUE;
                wcscpy(callback_paths[i], paths[i]);
            }
            before[i] = acl_tree_query(registry, handles[i]);
            if (!before[i]) goto cleanup_tree;
        }
        wcscpy(named, callback_paths[0]);
        ret = WideCharToMultiByte(CP_ACP, 0, named, -1, named_a, sizeof(named_a), NULL, NULL) &&
              MultiByteToWideChar(CP_ACP, 0, named_a, -1, roundtrip, ARRAY_SIZE(roundtrip)) && !lstrcmpW(named, roundtrip);
        ok(ret, "Tree ANSI root does not round trip.\n");
        if (!ret) goto cleanup_tree;
        for (phase_index = 0; phase_index < (registry ? 5 : 8); ++phase_index)
        {
        phase = !registry && phase_index == 5 ? 6 : !registry && phase_index == 6 ? 7 :
                !registry && phase_index == 7 ? 5 : phase_index;
        for (variant = 0; variant < (registry ? (phase ? 1 : 5) :
                                    phase == 7 ? 9 : phase == 6 ? 6 : phase == 5 ? ARRAY_SIZE(root_rights) : phase >= 3 ? 2 : 5); ++variant)
            for (form = phase ? 1 : 0; form < 2; ++form)
            {
                winetest_push_context("phase %lu variant %lu form %lu", phase, variant, form);
                expected = NULL;
                for (j = 0; j < ARRAY_SIZE(acl_tree_row_results); ++j)
                    if (acl_tree_row_results[j].registry == registry && acl_tree_row_results[j].phase == phase &&
                        acl_tree_row_results[j].variant == variant && acl_tree_row_results[j].form == form)
                    {
                        expected = &acl_tree_row_results[j];
                        break;
                    }
                ok(!!expected, "Tree operation has no native expectation.\n");
                if (!expected) { winetest_pop_context(); goto cleanup_tree; }
                if (!registry && phase == 5 && !clean_success)
                {
                    ok(FALSE, "Tree root-right matrix requires a complete successful clean SET control.\n");
                    winetest_pop_context();
                    goto cleanup_tree;
                }
                action_variant = registry && phase ? (phase >= 3 ? 0 : 1) : !registry && phase >= 5 ? 0 : variant;
                invocation = registry && phase ? (phase == 1 ? ACL_TREE_PRE_POST_ERROR : ProgressInvokeEveryObject) :
                                                  settings[action_variant][form];
                if (!registry && phase >= 2 && phase != 6)
                {
                    result = acl_tree_set_raw(FALSE, container_handle, descriptors[0]);
                    ok(!result, "Tree narrow parent DACL restoration returned %lu.\n", result);
                    if (result) { winetest_pop_context(); goto cleanup_tree; }
                    for (i = 0; i < ARRAY_SIZE(handles); ++i)
                    {
                        result = acl_tree_set_raw(FALSE, handles[i], descriptors[0]);
                        ok(!result, "Tree narrow node %lu DACL restoration returned %lu.\n", i, result);
                        if (result) { winetest_pop_context(); goto cleanup_tree; }
                    }
                    ret = acl_tree_replace_handles(paths, directories, container, handles, &container_handle, TRUE);
                    if (!ret) { winetest_pop_context(); goto cleanup_tree; }
                }
                for (i = 0; i < ARRAY_SIZE(handles); ++i)
                {
                    source_index = baseline[i];
                    if (registry && phase == 3 && (i == 1 || i == 2)) source_index = 3 - i;
                    if (registry && phase == 4 && i == 3) source_index = 0;
                    if (!registry && phase >= 2)
                    {
                        if (i == 2 && phase != 3) source_index = 1;
                        if (i == 3 && phase != 4) source_index = 0;
                    }
                    result = acl_tree_set_raw(registry, handles[i], descriptors[source_index]);
                    ok(!result, "Tree node %lu reset returned %lu.\n", i, result);
                    if (result) { winetest_pop_context(); goto cleanup_tree; }
                    queried = acl_tree_query(registry, handles[i]);
                    if (!queried) { winetest_pop_context(); goto cleanup_tree; }
                    ret = GetSecurityDescriptorControl(queried, &control, &revision) &&
                          GetSecurityDescriptorDacl(queried, &present, &actual, &defaulted) && present && actual &&
                          GetSecurityDescriptorDacl(descriptors[source_index], &present, &dacl, &defaulted);
                    ret = ret && !!(control & SE_DACL_PROTECTED) == (i == 0 || i == 3 || i == 5) &&
                          actual->AclSize == dacl->AclSize && !memcmp(actual, dacl, dacl->AclSize);
                    ok(ret, "Tree node %lu reset did not retain its exact DACL/protection.\n", i);
                    free(queried); queried = NULL;
                    if (!ret) { winetest_pop_context(); goto cleanup_tree; }
                    result = acl_tree_open(registry, paths[i], directories[i], registry ? KEY_SET_VALUE : FILE_WRITE_DATA, TRUE);
                    ok(result == (source_index == 2 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS),
                       "Tree node %lu initial write control returned %lu.\n", i, result);
                    if (result != (source_index == 2 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS))
                    { winetest_pop_context(); goto cleanup_tree; }
                }
                result = acl_tree_open(registry, paths[3], TRUE, WRITE_DAC, FALSE);
                ok(result == ((registry ? phase != 4 : phase < 2 || phase == 4) ? ERROR_ACCESS_DENIED : ERROR_SUCCESS),
                   "Tree protected branch WRITE_DAC control returned %lu.\n", result);
                if (result != ((registry ? phase != 4 : phase < 2 || phase == 4) ? ERROR_ACCESS_DENIED : ERROR_SUCCESS))
                { winetest_pop_context(); goto cleanup_tree; }
                result = acl_tree_open(registry, paths[1], FALSE, WRITE_DAC, FALSE);
                ok(!result, "Tree accessible sibling WRITE_DAC returned %lu.\n", result);
                if (result) { winetest_pop_context(); goto cleanup_tree; }
                if (!registry && phase == 6)
                {
                    if (!context_tokens[variant % 3]) context_tokens[variant % 3] = acl_tree_context_token(source, variant % 3);
                    if (!context_tokens[variant % 3]) { winetest_pop_context(); goto cleanup_tree; }
                    ret = acl_tree_replace_handles(paths, directories, container, handles, &container_handle, variant >= 3);
                    if (!ret) { winetest_pop_context(); goto cleanup_tree; }
                    ret = SetThreadToken(NULL, context_tokens[variant % 3]);
                    ok(ret, "Tree context token activation failed: %lu.\n", GetLastError());
                    if (!ret) { winetest_pop_context(); goto cleanup_tree; }
                    acl_tree_file_controls(paths[0], container, 0);
                }
                if (!registry && phase == 5)
                {
                    if (root_rights[variant] & (READ_CONTROL | WRITE_DAC))
                        snprintf(target_sddl, sizeof(target_sddl), "D:(D;;0x%lx;;;OW)(D;;0x%lx;;;WD)(A;OICI;FR;;;WD)(A;OICI;FA;;;WD)",
                                 root_rights[variant], root_rights[variant]);
                    else
                        snprintf(target_sddl, sizeof(target_sddl), "D:(D;;0x%lx;;;WD)(A;OICI;FR;;;WD)(A;OICI;FA;;;WD)",
                                 root_rights[variant]);
                    ret = ConvertStringSecurityDescriptorToSecurityDescriptorA(target_sddl, SDDL_REVISION_1, &target, NULL);
                    ok(ret, "Tree root-right target creation failed: %lu.\n", GetLastError());
                    if (!ret) { winetest_pop_context(); goto cleanup_tree; }
                }
                ret = GetSecurityDescriptorDacl(target ? target : descriptors[registry || !phase ? 4 : phase == 1 ? 5 : 6],
                                                &present, &dacl, &defaulted);
                ok(ret && present && dacl, "Tree target DACL missing.\n");
                if (!ret || !present || !dacl) { winetest_pop_context(); goto cleanup_tree; }
                if (!registry && phase == 7)
                {
                    sharing_handle = CreateFileW(variant / 3 == 2 ? container : paths[variant / 3 == 1 ? 2 : 0],
                                                  sharing_rights[variant % 3],
                                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                  NULL, OPEN_EXISTING, variant / 3 == 1 ? 0 : FILE_FLAG_BACKUP_SEMANTICS, NULL);
                    ok(sharing_handle != INVALID_HANDLE_VALUE, "Tree sharing-control handle failed: %lu.\n", GetLastError());
                    if (sharing_handle == INVALID_HANDLE_VALUE) { winetest_pop_context(); goto cleanup_tree; }
                }
                memset(&progress, 0, sizeof(progress));
                progress.paths = callback_paths;
                progress.stop_notifications = action_variant == 2 && form == 1;
                progress.action = registry && phase < 3 ? phase : 0;
                progress.repair_handle = handles[3];
                progress.repair_descriptor = descriptors[0];
                acl_tree_active_progress = &progress;
                if (action_variant < 3)
                    result = form ? set_w(named, registry ? SE_REGISTRY_KEY : SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                           NULL, NULL, dacl, NULL, actions[action_variant], acl_tree_progress, invocation, &progress) :
                                    set_a(named_a, registry ? SE_REGISTRY_KEY : SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                           NULL, NULL, dacl, NULL, actions[action_variant], acl_tree_progress, invocation, &progress);
                else
                    result = form ? reset_w(named, registry ? SE_REGISTRY_KEY : SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                             NULL, NULL, dacl, NULL, action_variant == 4, acl_tree_progress, invocation, &progress) :
                                    reset_a(named_a, registry ? SE_REGISTRY_KEY : SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                             NULL, NULL, dacl, NULL, action_variant == 4, acl_tree_progress, invocation, &progress);
                acl_tree_active_progress = NULL;
                ++operations;
                if (sharing_handle != INVALID_HANDLE_VALUE)
                {
                    ret = CloseHandle(sharing_handle);
                    ok(ret, "Tree sharing-control handle close failed: %lu.\n", GetLastError());
                    if (!ret) { winetest_pop_context(); goto cleanup_tree; }
                    sharing_handle = INVALID_HANDLE_VALUE;
                    ++sharing_rows;
                }
                if (invocation == ProgressInvokeNever)
                    ok(!progress.count, "Tree Never setting invoked %u callbacks.\n", progress.count);
                ok(result == expected->status, "Tree operation status %lu, expected %lu.\n", result, expected->status);
                acl_tree_check_progress(&progress, expected, invocation);
                if (progress.action)
                {
                    ok(progress.action_count == 1, "Tree callback action %u occurred %u times.\n", progress.action, progress.action_count);
                    callback_actions += progress.action_count == 1;
                    if (progress.action == 2)
                        ok(!progress.repair_status, "Tree callback repair status %lu.\n", progress.repair_status);
                }
                snapshot_start = snapshots;
                for (i = 0; i < ARRAY_SIZE(handles); ++i)
                {
                    expected_index = expected->nodes[i];
                    if (expected->callback_mode == ACL_TREE_CALLBACK_CANCEL && i)
                        expected_index = progress.count == 1 && progress.records[0].node == i &&
                                         progress.records[0].security_set && !progress.records[0].status ?
                                         acl_tree_registry_reset[i] : acl_tree_registry_baseline[i];
                    expected_node = &acl_tree_node_results[expected_index];
                    snapshots += acl_tree_snapshot(registry, handles[i], paths[i], directories[i], before[i], i, expected_node);
                }
                if (!registry && phase == 6)
                {
                    acl_tree_file_controls(paths[0], container, 1);
                    ++context_rows;
                    ret = SetThreadToken(NULL, token);
                    ok(ret, "Tree base token restoration failed: %lu.\n", GetLastError());
                    if (!ret) { winetest_pop_context(); goto cleanup_tree; }
                    result = acl_tree_set_raw(FALSE, container_handle, descriptors[0]);
                    ok(!result, "Tree context parent DACL restoration returned %lu.\n", result);
                    if (result) { winetest_pop_context(); goto cleanup_tree; }
                    for (i = 0; i < ARRAY_SIZE(handles); ++i)
                    {
                        result = acl_tree_set_raw(FALSE, handles[i], descriptors[i == 0 || i == 3 || i == 5 ? 0 : 1]);
                        ok(!result, "Tree context node %lu DACL restoration returned %lu.\n", i, result);
                        if (result) { winetest_pop_context(); goto cleanup_tree; }
                    }
                    ret = acl_tree_replace_handles(paths, directories, container, handles, &container_handle, TRUE);
                    if (!ret) { winetest_pop_context(); goto cleanup_tree; }
                }
                if (!registry && phase == 2 && !variant)
                {
                    clean_success = !result && snapshots - snapshot_start == ARRAY_SIZE(handles);
                    ok(clean_success, "Tree clean SET control returned %lu with %lu snapshots.\n", result, snapshots - snapshot_start);
                }
                if (!registry && phase == 5)
                {
                    result = acl_tree_open(FALSE, paths[0], TRUE, root_rights[variant], FALSE);
                    ok(result == (root_rights[variant] == FILE_READ_ATTRIBUTES || root_rights[variant] == DELETE ?
                                  ERROR_SUCCESS : ERROR_ACCESS_DENIED),
                       "Tree root requested-right %#lx open returned %lu.\n", root_rights[variant], result);
                }
                if (target) { LocalFree(target); target = NULL; }
                winetest_pop_context();
            }
        }
cleanup_tree:
        if (sharing_handle != INVALID_HANDLE_VALUE)
        {
            ok(CloseHandle(sharing_handle), "Tree sharing-control cleanup close failed: %lu.\n", GetLastError());
            sharing_handle = INVALID_HANDLE_VALUE;
        }
        ret = SetThreadToken(NULL, token);
        ok(ret, "Tree cleanup base token restoration failed: %lu.\n", GetLastError());
        if (container_created && container_handle != INVALID_HANDLE_VALUE)
        {
            result = acl_tree_set_raw(registry, container_handle, descriptors[0]);
            ok(!result, "Tree parent cleanup DACL restore returned %lu.\n", result);
        }
        for (i = 0; i < ARRAY_SIZE(handles); ++i)
            if (created[i] && handles[i] != INVALID_HANDLE_VALUE && descriptors[0])
            {
                result = acl_tree_set_raw(registry, handles[i], descriptors[0]);
                ok(!result, "Tree cleanup DACL %lu restore returned %lu.\n", i, result);
            }
        for (i = ARRAY_SIZE(handles); i-- > 0;)
        {
            if (handles[i] != INVALID_HANDLE_VALUE)
            {
                if (registry) RegCloseKey((HKEY)handles[i]);
                else CloseHandle(handles[i]);
                handles[i] = INVALID_HANDLE_VALUE;
            }
            if (created[i])
            {
                if (registry) result = RegDeleteKeyW(HKEY_CURRENT_USER, paths[i]);
                else result = (directories[i] ? RemoveDirectoryW(paths[i]) : DeleteFileW(paths[i])) ? ERROR_SUCCESS : GetLastError();
                ok(!result, "Tree node %lu cleanup returned %lu.\n", i, result);
            }
            free(before[i]); before[i] = NULL;
        }
        if (container_handle != INVALID_HANDLE_VALUE)
        {
            if (registry) RegCloseKey((HKEY)container_handle);
            else CloseHandle(container_handle);
        }
        if (container_created)
        {
            if (registry) result = RegDeleteKeyW(HKEY_CURRENT_USER, container);
            else result = RemoveDirectoryW(container) ? ERROR_SUCCESS : GetLastError();
            ok(!result, "Tree parent cleanup returned %lu.\n", result);
        }
        if (reserved) ok(DeleteFileW(container), "Tree reservation cleanup failed: %lu.\n", GetLastError());
        for (i = 0; i < ARRAY_SIZE(descriptors); ++i)
        {
            if (descriptors[i]) LocalFree(descriptors[i]);
            descriptors[i] = NULL;
        }
        free(queried); queried = NULL;
        if (target) { LocalFree(target); target = NULL; }
        winetest_pop_context();
    }
done:
    for (i = 0; i < ARRAY_SIZE(context_tokens); ++i) if (context_tokens[i]) CloseHandle(context_tokens[i]);
    ok(operations == 67 && snapshots == 402 && callback_actions == 2 && context_rows == 6 && sharing_rows == 9,
       "ACL tree matrix completed %lu/67 operations, %lu/402 snapshots, %lu/2 callback actions, %lu/6 context rows and %lu/9 sharing rows.\n",
       operations, snapshots, callback_actions, context_rows, sharing_rows);
    trace("ACL tree matrix completed %lu/67 operations, %lu/402 snapshots, %lu/2 callback actions, %lu/6 context rows and %lu/9 sharing rows.\n",
          operations, snapshots, callback_actions, context_rows, sharing_rows);
    winetest_debug = saved_debug;
    winetest_pop_context();
}

static void test_registry_security_matrix(void)
{
    HANDLE previous = NULL, source = NULL, tokens[3] = {0};
    DWORD error;
    UINT i;
    LUID security;
    BOOL ret, impersonating = FALSE;
    ret = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_IMPERSONATE, TRUE, &previous);
    error = GetLastError();
    ok(ret || error == ERROR_NO_TOKEN, "Matrix saved thread token returned %d, error %lu.\n", ret, error);
    if (!ret && error != ERROR_NO_TOKEN) goto done;
    ret = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &source);
    ok(ret, "Matrix source token open failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    ret = LookupPrivilegeValueA(NULL, SE_SECURITY_NAME, &security);
    ok(ret, "Matrix security privilege lookup failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    for (i = 0; i < ARRAY_SIZE(tokens); ++i)
    {
        tokens[i] = registry_matrix_token(source, &security, i);
        if (!tokens[i]) goto done;
    }
    ret = SetThreadToken(NULL, tokens[0]);
    ok(ret, "DACL matrix impersonation failed: %lu.\n", GetLastError());
    if (!ret) goto done;
    impersonating = TRUE;
    test_registry_dacl_rights_matrix();
    test_registry_sacl_privilege_matrix(tokens);
    test_registry_create_privilege_matrix(tokens);
    test_file_sacl_privilege_matrix(tokens);
    test_acl_tree_operations(tokens[0], source);
done:
    if (impersonating)
    {
        ret = SetThreadToken(NULL, previous);
        ok(ret, "Matrix original thread token restoration failed: %lu.\n", GetLastError());
    }
    for (i = 0; i < ARRAY_SIZE(tokens); ++i) if (tokens[i]) CloseHandle(tokens[i]);
    if (source) CloseHandle(source);
    if (previous) CloseHandle(previous);
}

#endif
START_TEST(security)
{
    init();
    if (!hmod) return;

    if (myARGC >= 3)
    {
        if (!strcmp(myARGV[2], "test_token_sd"))
            test_child_token_sd();
        else if (!strcmp(myARGV[2], "test"))
            test_process_security_child();
        else if (!strcmp(myARGV[2], "duplicate"))
            test_duplicate_handle_access_child();
        else if (!strcmp(myARGV[2], "restricted"))
            test_create_process_token_child();
        return;
    }
    test_kernel_objects_security();
    test_ConvertStringSidToSid();
#ifdef __REACTOS__
    test_sddl_domain_aliases();
#endif
    test_trustee();
    test_allocateLuid();
    test_lookupPrivilegeName();
    test_lookupPrivilegeValue();
    test_CreateWellKnownSid();
    test_FileSecurity();
    test_AccessCheck();
    test_token_attr();
    test_GetTokenInformation();
    test_LookupAccountSid();
    test_LookupAccountName();
    test_security_descriptor();
    test_process_security();
    test_impersonation_level();
    test_SetEntriesInAclW();
    test_SetEntriesInAclA();
#ifdef __REACTOS__
    test_acl_constructor_output();
    test_acl_rights_queries();
    test_acl_object_rights_queries();
    test_acl_group_membership_queries();
    test_acl_file_propagation();
#endif
    test_CreateDirectoryA();
    test_GetNamedSecurityInfoA();
#ifdef __REACTOS__
    test_registry_security_persistence(FALSE, 0);
    test_registry_security_persistence(TRUE, 0);
    test_registry_security_persistence(FALSE, KEY_WOW64_32KEY);
    test_registry_security_persistence(TRUE, KEY_WOW64_32KEY);
    test_registry_acl_propagation();
    test_registry_security_matrix();
#endif
    test_ConvertStringSecurityDescriptor();
    test_ConvertSecurityDescriptorToString();
    test_PrivateObjectSecurity();
    test_InitializeAcl();
    test_GetWindowsAccountDomainSid();
    test_EqualDomainSid();
    test_GetSecurityInfo();
    test_GetSidSubAuthority();
    test_CheckTokenMembership();
    test_EqualSid();
    test_GetUserNameA();
    test_GetUserNameW();
    test_CreateRestrictedToken();
    test_TokenIntegrityLevel();
    test_default_dacl_owner_group_sid();
    test_AdjustTokenPrivileges();
    test_AddAce();
    test_AddMandatoryAce();
    test_system_security_access();
    test_GetSidIdentifierAuthority();
    test_pseudo_tokens();
    test_maximum_allowed();
    test_token_label();
    test_GetExplicitEntriesFromAclW();
    test_BuildSecurityDescriptorW();
    test_duplicate_handle_access();
    test_create_process_token();
    test_pseudo_handle_security();
    test_duplicate_token();
    test_GetKernelObjectSecurity();
    test_elevation();
    test_admin_elevation();
    test_group_as_file_owner();
    test_IsValidSecurityDescriptor();
    test_window_security();

    /* Must be the last test, modifies process token */
    test_token_security_descriptor();
}
