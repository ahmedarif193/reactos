// SPDX-License-Identifier: MIT
#pragma once

#include <exception>
#include <winternl.h>

static inline __TEB* GetCurrentTEB() {
  return reinterpret_cast<__TEB*>(NtCurrentTeb());
}

static inline __PEB* GetCurrentPEB() {
  return GetCurrentTEB()->Peb;
}

static inline bool WinAPIReturn(NTSTATUS Status) {
  if (!Status) {
    return true;
  }
  GetCurrentTEB()->LastErrorValue = RtlNtStatusToDosError(Status);
  return false;
}

static inline UNICODE_STRING InitUnicodeString(const wchar_t* String) {
  UNICODE_STRING StringDesc;
  RtlInitUnicodeString(&StringDesc, String);
  return StringDesc;
}

static inline STRING InitAnsiString(const char* String) {
  STRING StringDesc;
  RtlInitAnsiString(&StringDesc, String);
  return StringDesc;
}

class ScopedUnicodeString {
private:
  UNICODE_STRING Str {};
public:
  ScopedUnicodeString() = default;

  ScopedUnicodeString(const char* AStr) {
    RtlCreateUnicodeStringFromAsciiz(&Str, AStr);
  }

  ~ScopedUnicodeString() {
    RtlFreeUnicodeString(&Str);
  }

  UNICODE_STRING* operator->() {
    return &Str;
  }

  UNICODE_STRING& operator*() {
    return Str;
  }
};


#define UNIMPLEMENTED()                        \
  do {                                         \
    DbgPrint("[FEX] unimplemented %s (%s:%d)\n", __func__, __FILE__, __LINE__);     \
    NtTerminateProcess(NtCurrentProcess(), static_cast<NTSTATUS>(0xC0000002));     \
    __fastfail(0);                             \
  } while (0)

#define DLLEXPORT_FUNC(Ret, Name, Args) \
  extern "C" Ret Name Args;             \
  Ret(*__imp_##Name) Args = Name;       \
  Ret(*__imp_aux_##Name) Args = Name;   \
  Ret Name Args
