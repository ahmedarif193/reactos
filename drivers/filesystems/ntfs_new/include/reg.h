// io/registry.cpp
HANDLE
OpenRegistryKey();

#define CloseRegistryKey(Key) ZwClose(Key)

INT
QueryDwordRegistryValueEx(_In_ HANDLE RegistryKey,
                          _In_ PWCHAR Name,
                          _In_ INT Default);

INT
QueryDwordRegistryValue(_In_ PWCHAR Name,
                        _In_ INT Default);

BOOLEAN
QueryBooleanRegistryValueEx(_In_ HANDLE RegistryKey,
                            _In_ PWCHAR Name,
                            _In_ BOOLEAN Default);
BOOLEAN
QueryBooleanRegistryValue(_In_ PWCHAR Name,
                          _In_ BOOLEAN Default);

NTSTATUS
SetDwordRegistryValue(_In_ HANDLE RegistryKey,
                      _In_ PWCHAR Name,
                      _In_ INT Data);

NTSTATUS
SetBooleanRegistryValue(_In_ HANDLE RegistryKey,
                        _In_ PWCHAR Name,
                        _In_ BOOLEAN Data);

VOID
GetGlobalSettingsFromRegistry();
