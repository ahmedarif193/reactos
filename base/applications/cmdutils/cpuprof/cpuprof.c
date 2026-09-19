#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <setupapi.h>
#include <initguid.h>
#include <poclass.h>
#include <acpiioct.h>
#include <reactos/drivers/sensorprovider.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>
#include <reactos/cpuaudit.h>

WINBASEAPI DWORD WINAPI GetCurrentProcessorNumber(VOID);

typedef LONG (NTAPI *QUERY_SYSTEM)(ULONG, PVOID, ULONG, PULONG);
typedef struct
{
    LARGE_INTEGER Idle, Kernel, User, Dpc, Interrupt;
    ULONG InterruptCount;
} CPU_TIMES;
typedef struct
{
    ULONG Switches, Dpcs, Rate, Increment, DpcBypass, ApcBypass;
} CPU_INTERRUPTS;
typedef struct
{
    HANDLE Handle;
    DWORD Pid;
    WCHAR Name[MAX_PATH];
    ULONGLONG Kernel, User;
} PROCESS_SAMPLE;

typedef struct
{
    HANDLE Start;
    DWORD Cpu, Error;
    ULONGLONG Cycles[5], Ticks[5];
} CLOCK_SAMPLE;

typedef struct
{
    PVOID Section, MappedBase, ImageBase;
    ULONG ImageSize, Flags;
    USHORT LoadOrderIndex, InitOrderIndex, LoadCount, OffsetToFileName;
    CHAR FullPathName[256];
} KERNEL_MODULE;
typedef struct { ULONG Count; KERNEL_MODULE Modules[]; } KERNEL_MODULES;
typedef LONG (NTAPI *CREATE_PROFILE)(PHANDLE, HANDLE, PVOID, SIZE_T, ULONG, PVOID, ULONG, ULONG, ULONG_PTR);
typedef LONG (NTAPI *PROFILE_OPERATION)(HANDLE);
typedef LONG (NTAPI *ADJUST_PRIVILEGE)(ULONG, BOOLEAN, BOOLEAN, PBOOLEAN);
typedef struct
{
    HANDLE Handle;
    ULONG *Buckets, Count;
    KERNEL_MODULE Module;
} SAMPLE_PROFILE;
static SAMPLE_PROFILE Profiles[128];
static ULONG ProfileCount;
static PROFILE_OPERATION StopProfile;

static void StartSampling(QUERY_SYSTEM Query, const WCHAR *TargetName)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    CREATE_PROFILE CreateProfile = (CREATE_PROFILE)GetProcAddress(Ntdll, "NtCreateProfile");
    PROFILE_OPERATION StartProfile = (PROFILE_OPERATION)GetProcAddress(Ntdll, "NtStartProfile");
    ADJUST_PRIVILEGE Adjust = (ADJUST_PRIVILEGE)GetProcAddress(Ntdll, "RtlAdjustPrivilege");
    KERNEL_MODULES *Modules;
    ULONG Length = 0;
    BOOLEAN WasEnabled = FALSE, PrivilegeAdjusted = FALSE;
    LONG Status;
    HANDLE Target = NULL;
    StopProfile = (PROFILE_OPERATION)GetProcAddress(Ntdll, "NtStopProfile");
    if (!CreateProfile || !StartProfile || !StopProfile || !Adjust) return;
    if (TargetName)
    {
        PROCESSENTRY32W Entry = { .dwSize = sizeof(Entry) };
        HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        DWORD Pid = 0;
        if (Snapshot != INVALID_HANDLE_VALUE && Process32FirstW(Snapshot, &Entry))
            do
            {
                if (!_wcsicmp(Entry.szExeFile, TargetName))
                {
                    Pid = Entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(Snapshot, &Entry));
        if (Snapshot != INVALID_HANDLE_VALUE) CloseHandle(Snapshot);
        if (Pid) Target = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, Pid);
        if (!Target)
        {
            printf("SAMPLE_ERROR process=%ls pid=%lu error=%lu\n", TargetName, Pid, GetLastError());
            return;
        }
        printf("SAMPLE_SCOPE process=%ls pid=%lu\n", TargetName, Pid);
    }
    else if ((Status = Adjust(11, TRUE, FALSE, &WasEnabled)) < 0)
    {
        Target = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, 4);
        if (!Target)
        {
            printf("SAMPLE_ERROR privilege=0x%08lx process=%lu\n", Status, GetLastError());
            return;
        }
        printf("SAMPLE_SCOPE process=System privilege=0x%08lx\n", Status);
    }
    else
    {
        PrivilegeAdjusted = TRUE;
        printf("SAMPLE_SCOPE process=all\n");
    }
    Query(11, NULL, 0, &Length);
    Modules = calloc(1, Length + 4096);
    if (!Modules) goto Cleanup;
    Status = Query(11, Modules, Length + 4096, NULL);
    if (Status < 0)
    {
        printf("SAMPLE_ERROR modules=0x%08lx\n", Status);
        free(Modules);
        goto Cleanup;
    }
    for (ULONG i = 0; i < Modules->Count && ProfileCount < 128; ++i)
    {
        SAMPLE_PROFILE *Profile = &Profiles[ProfileCount];
        Profile->Module = Modules->Modules[i];
        if (!Profile->Module.ImageSize || Profile->Module.ImageSize > 16 * 1024 * 1024) continue;
        Profile->Count = (Profile->Module.ImageSize + 63) / 64;
        Profile->Buckets = calloc(Profile->Count, sizeof(ULONG));
        if (!Profile->Buckets) continue;
        Status = CreateProfile(&Profile->Handle, Target, Profile->Module.ImageBase,
                               Profile->Module.ImageSize, 6, Profile->Buckets,
                               Profile->Count * sizeof(ULONG), 0, 0xF);
        if (Status >= 0) Status = StartProfile(Profile->Handle);
        if (Status < 0)
        {
            printf("SAMPLE_ERROR module=%s status=0x%08lx\n",
                   Profile->Module.FullPathName + Profile->Module.OffsetToFileName, Status);
            if (Profile->Handle) CloseHandle(Profile->Handle);
            free(Profile->Buckets);
            memset(Profile, 0, sizeof(*Profile));
            continue;
        }
        ++ProfileCount;
    }
    free(Modules);
Cleanup:
    if (Target) CloseHandle(Target);
    if (PrivilegeAdjusted) Adjust(11, WasEnabled, FALSE, &WasEnabled);
}

static void StopSampling(void)
{
    for (ULONG i = 0; i < ProfileCount; ++i) StopProfile(Profiles[i].Handle);
}

static void ReportSamples(void)
{
    for (ULONG i = 0; i < ProfileCount; ++i)
    {
        SAMPLE_PROFILE *Profile = &Profiles[i];
        ULONGLONG Total = 0;
        CHAR *Name = Profile->Module.FullPathName + Profile->Module.OffsetToFileName;
        for (ULONG j = 0; j < Profile->Count; ++j) Total += Profile->Buckets[j];
        if (Total) printf("SAMPLE_MODULE module=%s base=%p total=%llu bucket_bytes=64\n",
                          Name, Profile->Module.ImageBase, Total);
        for (unsigned rank = 0; rank < 20; ++rank)
        {
            ULONG Best = 0;
            for (ULONG j = 1; j < Profile->Count; ++j)
                if (Profile->Buckets[j] > Profile->Buckets[Best]) Best = j;
            if (!Profile->Buckets[Best]) break;
            printf("SAMPLE_PC module=%s offset=0x%lx count=%lu\n", Name, Best * 64, Profile->Buckets[Best]);
            Profile->Buckets[Best] = 0;
        }
        CloseHandle(Profile->Handle);
        free(Profile->Buckets);
    }
    printf("SAMPLE_COMPLETE profiles=%lu\n", ProfileCount);
}

static ULONGLONG ReadClockTicks(void)
{
    ULONGLONG Value;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(Value));
    return Value;
}

static ULONGLONG ReadClockCycles(void)
{
    ULONGLONG Value;
    __asm__ __volatile__("isb\n\tmrs %0, pmccntr_el0" : "=r"(Value) :: "memory");
    return Value;
}

static DWORD WINAPI ClockWorker(void *Parameter)
{
    CLOCK_SAMPLE *Sample = Parameter;
    ULONGLONG Frequency, Start, Now, Cycles;
    if (!SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << Sample->Cpu))
    {
        Sample->Error = GetLastError();
        return 1;
    }
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(Frequency));
    WaitForSingleObject(Sample->Start, INFINITE);
    Start = ReadClockTicks();
    do { Now = ReadClockTicks(); } while (Now - Start < Frequency * 3);
    for (unsigned i = 0; i < 5; ++i)
    {
        Start = ReadClockTicks();
        Cycles = ReadClockCycles();
        do { Now = ReadClockTicks(); } while (Now - Start < Frequency);
        Sample->Cycles[i] = ReadClockCycles() - Cycles;
        Sample->Ticks[i] = Now - Start;
        if (GetCurrentProcessorNumber() != Sample->Cpu) Sample->Error = ERROR_INVALID_DATA;
    }
    return 0;
}

typedef struct { ULONGLONG Start, End, Cycles; } CLOCK_PROBE_RECORD;
static CLOCK_PROBE_RECORD ProbeRecords[4][1024];
static DWORD ProbeCounts[4], ProbeErrors[4];
static HANDLE ProbeStop, ProbeThreads[4];
static DWORD WINAPI ClockProbeWorker(PVOID Parameter)
{
    ULONG Cpu = (ULONG)(ULONG_PTR)Parameter;
    ULONGLONG Frequency;
    if (!SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << Cpu))
    { ProbeErrors[Cpu] = GetLastError(); return 1; }
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(Frequency));
    while (WaitForSingleObject(ProbeStop, 167 + (ProbeCounts[Cpu] * 37 + Cpu * 29) % 71) == WAIT_TIMEOUT)
    {
        ULONG Index = ProbeCounts[Cpu];
        ULONGLONG Cycles;
        if (Index >= 1024) { ProbeErrors[Cpu] = ERROR_BUFFER_OVERFLOW; break; }
        CLOCK_PROBE_RECORD *Record = &ProbeRecords[Cpu][Index];
        Record->Start = ReadClockTicks(); Cycles = ReadClockCycles();
        do { Record->End = ReadClockTicks(); } while (Record->End - Record->Start < Frequency / 10000);
        Record->Cycles = ReadClockCycles() - Cycles;
        if (GetCurrentProcessorNumber() != Cpu) ProbeErrors[Cpu] = ERROR_INVALID_DATA;
        ProbeCounts[Cpu]++;
    }
    return 0;
}
static BOOL StartClockProbe(void)
{
    ProbeStop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ProbeStop) return FALSE;
    for (ULONG Cpu = 0; Cpu < 4; Cpu++)
    {
        ProbeThreads[Cpu] = CreateThread(NULL, 0, ClockProbeWorker, (PVOID)(ULONG_PTR)Cpu, 0, NULL);
        if (!ProbeThreads[Cpu]) { SetEvent(ProbeStop); return FALSE; }
    }
    return TRUE;
}
static BOOL FinishClockProbe(const WCHAR *Path)
{
    BOOL Success = TRUE;
    FILE *File;
    ULONGLONG Frequency;
    if (!ProbeStop) return FALSE;
    SetEvent(ProbeStop);
    for (ULONG Cpu = 0; Cpu < 4; Cpu++)
    {
        if (ProbeThreads[Cpu])
        {
            if (WaitForSingleObject(ProbeThreads[Cpu], 5000) != WAIT_OBJECT_0) ExitProcess(4);
            CloseHandle(ProbeThreads[Cpu]);
        }
        else Success = FALSE;
        if (ProbeErrors[Cpu]) Success = FALSE;
    }
    CloseHandle(ProbeStop); ProbeStop = NULL;
    File = _wfopen(Path, L"wb");
    if (!File) return FALSE;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(Frequency));
    fprintf(File, "cpu,start_tick,end_tick,cycles\n");
    for (ULONG Cpu = 0; Cpu < 4; Cpu++)
    {
        ULONGLONG Cycles = 0, Ticks = 0;
        for (ULONG i = 0; i < ProbeCounts[Cpu]; i++)
        {
            CLOCK_PROBE_RECORD *R = &ProbeRecords[Cpu][i];
            fprintf(File, "%lu,%llu,%llu,%llu\n", Cpu, R->Start, R->End, R->Cycles);
            Cycles += R->Cycles; Ticks += R->End - R->Start;
        }
        printf("PROFILE_CLOCK_PROBE cpu=%lu samples=%lu sampled_mhz=%.3f error=%lu\n", Cpu, ProbeCounts[Cpu],
               Ticks ? (double)Cycles * Frequency / Ticks / 1e6 : 0, ProbeErrors[Cpu]);
    }
    if (fclose(File)) Success = FALSE;
    return Success;
}

static void ReportSensors(const char *Phase)
{
    HDEVINFO Devices = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_REACTOS_SENSOR_PROVIDER,
                                           NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    ULONG Channels = 0;
    if (Devices == INVALID_HANDLE_VALUE) return;
    for (DWORD i = 0; ; ++i)
    {
        SP_DEVICE_INTERFACE_DATA Interface = { .cbSize = sizeof(Interface) };
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
        REACTOS_SENSOR_PROVIDER_INFORMATION Provider;
        DWORD Size = 0, Returned;
        HANDLE Device;
        if (!SetupDiEnumDeviceInterfaces(Devices, NULL, &GUID_DEVINTERFACE_REACTOS_SENSOR_PROVIDER, i, &Interface)) break;
        SetupDiGetDeviceInterfaceDetailW(Devices, &Interface, NULL, 0, &Size, NULL);
        Detail = calloc(1, Size);
        if (!Detail) continue;
        Detail->cbSize = sizeof(*Detail);
        if (!SetupDiGetDeviceInterfaceDetailW(Devices, &Interface, Detail, Size, NULL, NULL))
        {
            free(Detail);
            continue;
        }
        Device = CreateFileW(Detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, 0, NULL);
        free(Detail);
        if (Device == INVALID_HANDLE_VALUE) continue;
        if (DeviceIoControl(Device, IOCTL_REACTOS_SENSOR_QUERY_PROVIDER, NULL, 0,
                            &Provider, sizeof(Provider), &Returned, NULL) &&
            Provider.Version == REACTOS_SENSOR_PROVIDER_INTERFACE_VERSION &&
            Provider.ChannelCount <= REACTOS_SENSOR_PROVIDER_MAX_CHANNELS)
        {
            for (ULONG j = 0; j < Provider.ChannelCount; ++j)
            {
                REACTOS_SENSOR_READ_INPUT Input = { .Version = REACTOS_SENSOR_PROVIDER_INTERFACE_VERSION,
                    .Size = sizeof(Input), .ChannelIndex = j };
                REACTOS_SENSOR_READING Reading;
                if (DeviceIoControl(Device, IOCTL_REACTOS_SENSOR_READ_CHANNEL, &Input, sizeof(Input),
                                    &Reading, sizeof(Reading), &Returned, NULL))
                {
                    printf("PROFILE_SENSOR phase=%s name=%ls type=%lu unit=%lu value=%lld raw=%lu flags=0x%lx\n",
                           Phase, Provider.Channels[j].Name, Reading.Type, Reading.Unit,
                           Reading.Value, Reading.RawValue, Reading.Flags);
                    ++Channels;
                }
            }
        }
        CloseHandle(Device);
    }
    SetupDiDestroyDeviceInfoList(Devices);
    printf("PROFILE_SENSORS phase=%s channels=%lu\n", Phase, Channels);
}

static BOOL BufferFirmware;
static HANDLE FirmwareStop, FirmwareThread;
static unsigned FirmwareCount;
static char FirmwareLines[256][4096];
static void FirmwarePrint(const char *Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    if (BufferFirmware)
    {
        if (FirmwareCount < 256)
            vsnprintf(FirmwareLines[FirmwareCount++], sizeof(FirmwareLines[0]), Format, Args);
    }
    else vprintf(Format, Args);
    va_end(Args);
}
static void ReportThermals(const char *Phase)
{
    LARGE_INTEGER SampleBegin, SampleEnd;
    QueryPerformanceCounter(&SampleBegin);
    HDEVINFO Devices = SetupDiGetClassDevsW(&GUID_DEVICE_THERMAL_ZONE, NULL, NULL,
                                           DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (Devices == INVALID_HANDLE_VALUE) return;
    for (DWORD i = 0; ; ++i)
    {
        SP_DEVICE_INTERFACE_DATA Interface = { .cbSize = sizeof(Interface) };
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
        DWORD Size = 0, Returned;
        HANDLE Device;
        if (!SetupDiEnumDeviceInterfaces(Devices, NULL, &GUID_DEVICE_THERMAL_ZONE, i, &Interface)) break;
        SetupDiGetDeviceInterfaceDetailW(Devices, &Interface, NULL, 0, &Size, NULL);
        Detail = calloc(1, Size);
        if (!Detail) continue;
        Detail->cbSize = sizeof(*Detail);
        if (!SetupDiGetDeviceInterfaceDetailW(Devices, &Interface, Detail, Size, NULL, NULL))
        {
            free(Detail);
            continue;
        }
        Device = CreateFileW(Detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        free(Detail);
        if (Device == INVALID_HANDLE_VALUE)
        {
            FirmwarePrint("PROFILE_THERMAL_ERROR phase=%s open=%lu\n", Phase, GetLastError());
            continue;
        }
        if (i == 0)
        {
            const char *Commands[] = { "get_throttled", "measure_clock arm", "measure_temp", "pmic_read_adc" };
            for (unsigned j = 0; j < sizeof(Commands) / sizeof(Commands[0]); ++j)
            {
                BYTE InputBytes[512] = {0}, OutputBytes[4096] = {0};
                PACPI_EVAL_INPUT_BUFFER_COMPLEX_EX Input = (void *)InputBytes;
                PACPI_EVAL_OUTPUT_BUFFER Output = (void *)OutputBytes;
                Input->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE_EX;
                strcpy(Input->MethodName, "\\_SB.GCMQ");
                Input->ArgumentCount = 1;
                Input->Argument[0].Type = ACPI_METHOD_ARGUMENT_STRING;
                Input->Argument[0].DataLength = (USHORT)(strlen(Commands[j]) + 1);
                memcpy(Input->Argument[0].Data, Commands[j], Input->Argument[0].DataLength);
                Input->Size = FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX_EX, Argument) +
                              ACPI_METHOD_ARGUMENT_LENGTH(Input->Argument[0].DataLength);
                if (DeviceIoControl(Device, IOCTL_ACPI_EVAL_METHOD_EX, Input, Input->Size,
                                    Output, sizeof(OutputBytes), &Returned, NULL) &&
                    Returned >= FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument[0].Data) &&
                    Output->Count && Output->Argument[0].Type == ACPI_METHOD_ARGUMENT_BUFFER &&
                    Output->Argument[0].DataLength < sizeof(OutputBytes) - FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument[0].Data))
                    FirmwarePrint("PROFILE_FIRMWARE phase=%s command=%s result=%.*s\n", Phase, Commands[j],
                           Output->Argument[0].DataLength, Output->Argument[0].Data);
                else FirmwarePrint("PROFILE_FIRMWARE_ERROR phase=%s command=%s error=%lu\n", Phase, Commands[j], GetLastError());
            }
        }
        CloseHandle(Device);
    }
    SetupDiDestroyDeviceInfoList(Devices);
    QueryPerformanceCounter(&SampleEnd);
    FirmwarePrint("PROFILE_FIRMWARE_TIME phase=%s begin=%lld end=%lld\n", Phase, SampleBegin.QuadPart, SampleEnd.QuadPart);
}

static DWORD WINAPI FirmwareWorker(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    while (WaitForSingleObject(FirmwareStop, 2000) == WAIT_TIMEOUT)
        ReportThermals("audit-live");
    return 0;
}
static BOOL StartFirmwareWorker(void)
{
    FirmwareStop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!FirmwareStop) return FALSE;
    BufferFirmware = TRUE;
    FirmwareThread = CreateThread(NULL, 0, FirmwareWorker, NULL, 0, NULL);
    if (!FirmwareThread)
    {
        BufferFirmware = FALSE;
        CloseHandle(FirmwareStop); FirmwareStop = NULL;
        return FALSE;
    }
    return TRUE;
}
static BOOL StopFirmwareWorker(void)
{
    if (!FirmwareThread) return TRUE;
    SetEvent(FirmwareStop);
    if (WaitForSingleObject(FirmwareThread, 30000) != WAIT_OBJECT_0)
        return FALSE;
    CloseHandle(FirmwareThread); CloseHandle(FirmwareStop);
    FirmwareThread = FirmwareStop = NULL;
    BufferFirmware = FALSE;
    return TRUE;
}

static int ProfileClocks(void)
{
    SYSTEM_INFO System;
    CLOCK_SAMPLE Samples[4] = {0};
    HANDLE Threads[4] = {0}, Start;
    ULONGLONG Frequency;
    int Errors = 0;
    GetSystemInfo(&System);
    if (System.dwNumberOfProcessors != 4) return 2;
    ReportSensors("before");
    ReportThermals("before");
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(Frequency));
    Start = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Start) return 2;
    for (unsigned i = 0; i < 4; ++i)
    {
        Samples[i].Start = Start;
        Samples[i].Cpu = i;
        Threads[i] = CreateThread(NULL, 0, ClockWorker, &Samples[i], 0, NULL);
        if (!Threads[i]) ExitProcess(2);
    }
    SetEvent(Start);
    for (unsigned poll = 0; ; ++poll)
    {
        DWORD Wait = WaitForMultipleObjects(4, Threads, TRUE, 500);
        if (Wait == WAIT_OBJECT_0) break;
        if (Wait != WAIT_TIMEOUT || poll >= 39) ExitProcess(3);
        ReportThermals("load");
    }
    for (unsigned cpu = 0; cpu < 4; ++cpu)
    {
        Errors += Samples[cpu].Error != 0;
        for (unsigned i = 0; i < 5; ++i)
            printf("PROFILE_CLOCK cpu=%u sample=%u warmup_s=3 cycles=%llu ticks=%llu mhz=%.3f error=%lu\n",
                   cpu, i, Samples[cpu].Cycles[i], Samples[cpu].Ticks[i],
                   Samples[cpu].Ticks[i] ? (double)Samples[cpu].Cycles[i] * Frequency / Samples[cpu].Ticks[i] / 1e6 : 0,
                   Samples[cpu].Error);
        CloseHandle(Threads[cpu]);
    }
    CloseHandle(Start);
    ReportSensors("after");
    ReportThermals("after");
    return Errors ? 1 : 0;
}

static ULONGLONG FileTimeValue(FILETIME Value)
{
    return ((ULONGLONG)Value.dwHighDateTime << 32) | Value.dwLowDateTime;
}

static BOOL ProcessTimes(HANDLE Process, ULONGLONG *Kernel, ULONGLONG *User)
{
    FILETIME Created, Exited, K, U;
    if (!GetProcessTimes(Process, &Created, &Exited, &K, &U)) return FALSE;
    *Kernel = FileTimeValue(K);
    *User = FileTimeValue(U);
    return TRUE;
}

static CPU_AUDIT_PACKET *AuditPacket;
static CPU_AUDIT_EVENT *AuditEvents;
static CPU_AUDIT_IRQ AuditBefore[CPU_AUDIT_CPUS][CPU_AUDIT_IRQS];
static unsigned AuditCount, AuditLost, AuditErrors;
static ULONG_PTR AuditCoreBase;
static ULONGLONG AuditCoreTimes[4];
#define AUDIT_CAPACITY 524288
static LONG AuditCall(QUERY_SYSTEM Query, ULONG Command, ULONG Cpu)
{
    AuditPacket->Version = CPU_AUDIT_VERSION;
    AuditPacket->Command = Command; AuditPacket->Cpu = Cpu;
    return Query(CPU_AUDIT_CLASS, AuditPacket, sizeof(*AuditPacket), NULL);
}
static void AuditAppend(void)
{
    ULONG Count = AuditPacket->Count;
    if (Count > CPU_AUDIT_EVENTS) { AuditErrors |= 0x10000000; return; }
    AuditLost = AuditPacket->Dropped; AuditErrors |= AuditPacket->Errors;
    if (AuditCount + Count <= AUDIT_CAPACITY)
    {
        memcpy(AuditEvents + AuditCount, AuditPacket->Data.Events, Count * sizeof(*AuditEvents));
        AuditCount += Count;
    }
    else AuditErrors |= 0x20000000;
}
static void AuditDrain(QUERY_SYSTEM Query)
{
    unsigned Tries = 0;
    do
    {
        LONG Status = AuditCall(Query, CPU_AUDIT_DRAIN, 0);
        if (Status < 0) { AuditErrors |= 0x40000000; break; }
        AuditAppend();
    } while (AuditPacket->Count == CPU_AUDIT_EVENTS && ++Tries < 32);
}
static BOOL AuditBegin(QUERY_SYSTEM Query, HANDLE Process, DWORD Pid, BOOL CoreMark)
{
    ADJUST_PRIVILEGE Adjust = (ADJUST_PRIVILEGE)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlAdjustPrivilege");
    BOOLEAN WasEnabled;
    LONG Status;
    AuditPacket = calloc(1, sizeof(*AuditPacket));
    AuditEvents = VirtualAlloc(NULL, AUDIT_CAPACITY * sizeof(*AuditEvents), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!AuditPacket || !AuditEvents || !Adjust || Adjust(20, TRUE, FALSE, &WasEnabled) < 0) return FALSE;
    /* Touch capture storage before timing the child, without disk writes during scoring. */
    memset(AuditEvents, 0, AUDIT_CAPACITY * sizeof(*AuditEvents));
    memset(FirmwareLines, 0, sizeof(FirmwareLines));
    AuditPacket->Pid = Pid;
    Status = AuditCall(Query, CPU_AUDIT_START, 0);
    printf("AUDIT_BEGIN status=0x%08lx pid=%lu event_bytes=%u packet_bytes=%u\n", Status, Pid, (unsigned)sizeof(*AuditEvents), (unsigned)sizeof(*AuditPacket));
    if (Status < 0) return FALSE;
    AuditAppend();
    for (ULONG Cpu = 0; Cpu < CPU_AUDIT_CPUS; ++Cpu)
    {
        Status = AuditCall(Query, CPU_AUDIT_INTERRUPTS, Cpu);
        if (Status < 0) AuditErrors |= 0x40000000;
        else memcpy(AuditBefore[Cpu], AuditPacket->Data.Irqs, sizeof(AuditBefore[Cpu]));
    }
    if (CoreMark)
    {
        typedef LONG (NTAPI *QUERY_PROCESS)(HANDLE, ULONG, PVOID, ULONG, PULONG);
        QUERY_PROCESS Qp = (QUERY_PROCESS)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
        ULONG_PTR Basic[6] = {0}; SIZE_T Done;
        if (Qp && Qp(Process, 0, Basic, sizeof(Basic), NULL) >= 0)
            ReadProcessMemory(Process, (PVOID)(Basic[1] + 0x10), &AuditCoreBase, sizeof(AuditCoreBase), &Done);
        printf("AUDIT_COREMARK image=0x%llx start_rva=0x110d0 stop_rva=0x110e0\n", (unsigned long long)AuditCoreBase);
    }
    if (AuditErrors)
    {
        printf("AUDIT_ERROR pmu=0x%x\n", AuditErrors);
        AuditCall(Query, CPU_AUDIT_STOP, 0); return FALSE;
    }
    return TRUE;
}
static void AuditObserveCore(HANDLE Process)
{
    ULONGLONG Values[4]; SIZE_T Done; LARGE_INTEGER Tick; FILETIME Ft;
    if (!AuditCoreBase || AuditCount >= AUDIT_CAPACITY) return;
    if (!ReadProcessMemory(Process, (PVOID)(AuditCoreBase + 0x110d0), Values, sizeof(Values), &Done) || Done != sizeof(Values)) return;
    if (!memcmp(Values, AuditCoreTimes, sizeof(Values))) return;
    memcpy(AuditCoreTimes, Values, sizeof(Values));
    QueryPerformanceCounter(&Tick); GetSystemTimeAsFileTime(&Ft);
    CPU_AUDIT_EVENT *Event = &AuditEvents[AuditCount++];
    memset(Event, 0, sizeof(*Event)); Event->Kind = CPU_AUDIT_MARK; Event->Tick = Tick.QuadPart;
    memcpy(Event->Values, Values, sizeof(Values)); Event->Values[4] = FileTimeValue(Ft);
}
static void AuditFinish(QUERY_SYSTEM Query, const WCHAR *Path)
{
    ULONG64 Totals[6] = {0}, InstrumentTicks = 0;
    LONG Status = AuditCall(Query, CPU_AUDIT_STOP, 0);
    if (Status < 0) AuditErrors |= 0x40000000; else AuditAppend();
    AuditDrain(Query);
    for (ULONG Cpu = 0; Cpu < CPU_AUDIT_CPUS; ++Cpu)
    {
        if (AuditCall(Query, CPU_AUDIT_INTERRUPTS, Cpu) < 0) { AuditErrors |= 0x40000000; continue; }
        for (ULONG Vector = 0; Vector < CPU_AUDIT_IRQS; ++Vector)
        {
            CPU_AUDIT_IRQ *Now = &AuditPacket->Data.Irqs[Vector], *Before = &AuditBefore[Cpu][Vector];
            if (Now->Count != Before->Count)
                printf("AUDIT_IRQ cpu=%lu vector=%lu count=%llu dispatch_ticks=%llu envelope_ticks=%llu max_dispatch_ticks=%llu handler_calls=%llu\n", Cpu, Vector,
                       Now->Count - Before->Count, Now->DispatchTicks - Before->DispatchTicks, Now->EnvelopeTicks - Before->EnvelopeTicks, Now->MaximumTicks, Now->HandlerCalls - Before->HandlerCalls);
            if (Vector == 282 && Now->Count - Before->Count > 10 && Now->HandlerCalls == Before->HandlerCalls)
                AuditErrors |= 0x01000000;
        }
    }
    for (unsigned i = 0; i < AuditCount; ++i)
    {
        CPU_AUDIT_EVENT *Event = &AuditEvents[i];
        if (Event->Kind == CPU_AUDIT_SWITCH || Event->Kind == CPU_AUDIT_SAMPLE)
        {
            for (unsigned j = 0; j < 6; ++j) Totals[j] += Event->Values[j];
            InstrumentTicks += Event->Aux;
        }
        if (Event->Kind == CPU_AUDIT_CONFIG)
            printf("AUDIT_PMU cpu=%lu pmcr=0x%llx previous_enable=0x%llx supported=0x%llx counter5_event=0x%llx error=0x%lx\n", Event->Cpu, Event->Values[0], Event->Values[1], Event->Values[2], Event->Values[3], Event->Aux);
        if (Event->Kind == CPU_AUDIT_MARK)
            printf("AUDIT_PHASE tick=%llu start_sec=%llu start_ns=%llu stop_sec=%llu stop_ns=%llu filetime=%llu\n", Event->Tick,Event->Values[0],Event->Values[1],Event->Values[2],Event->Values[3],Event->Values[4]);
    }
    FILE *File = _wfopen(Path, L"wb");
    if (!File || fwrite(AuditEvents, sizeof(*AuditEvents), AuditCount, File) != AuditCount) AuditErrors |= 0x80000000;
    if (File) fclose(File);
    printf("AUDIT_COMPLETE records=%u lost=%u errors=0x%x frequency=%llu instrumentation_ticks=%llu path=%ls\n", AuditCount, AuditLost, AuditErrors, AuditPacket->Frequency, InstrumentTicks, Path);
    printf("AUDIT_TOTAL cycles_all=%llu instructions_el0=%llu l1d_refill_el0=%llu l2d_refill_el0=%llu dtlb_refill_el0=%llu backend_stall_el0=%llu\n",Totals[0],Totals[1],Totals[2],Totals[3],Totals[4],Totals[5]);
    VirtualFree(AuditEvents, 0, MEM_RELEASE); free(AuditPacket);
}

int wmain(int argc, WCHAR **argv)
{
    CPU_TIMES Before[64] = {0}, After[64] = {0};
    CPU_INTERRUPTS IrqBefore[64] = {0}, IrqAfter[64] = {0};
    PROCESS_SAMPLE Processes[256] = {0};
    PROCESSENTRY32W Entry = { .dwSize = sizeof(Entry) };
    STARTUPINFOW Startup = { .cb = sizeof(Startup) };
    PROCESS_INFORMATION Child = {0};
    PROCESS_MEMORY_COUNTERS Memory = { .cb = sizeof(Memory) };
    LARGE_INTEGER Frequency, Start, End;
    HANDLE Snapshot;
    QUERY_SYSTEM Query = (QUERY_SYSTEM)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
    DWORD_PTR Affinity = 0;
    BOOL Sampling = FALSE, Telemetry = FALSE, LiveTelemetry = FALSE;
    const WCHAR *ClockProbePath = NULL;
    const WCHAR *AuditPath = NULL;
    const WCHAR *SampleName = NULL;
    const WCHAR *StopFile = NULL;
    DWORD Idle = 0, ExitCode = 0, Count = 0, TickBefore, TickAfter;
    ULONG Length = 0, Cpus, Index;
    ULONGLONG Kernel, User;
    double Seconds;
    WCHAR *Command = wcsstr(GetCommandLineW(), L" -- ");

    if (!Query) return 2;
    if (argc == 2 && !wcscmp(argv[1], L"--clocks")) return ProfileClocks();
    for (int i = 1; i < argc && wcscmp(argv[i], L"--"); ++i)
    {
        if (!wcsncmp(argv[i], L"--affinity=", 11)) Affinity = wcstoull(argv[i] + 11, NULL, 0);
        else if (!wcsncmp(argv[i], L"--idle=", 7)) Idle = wcstoul(argv[i] + 7, NULL, 10);
        else if (!wcsncmp(argv[i], L"--stop-file=", 12)) StopFile = argv[i] + 12;
        else if (!wcsncmp(argv[i], L"--audit=", 8)) AuditPath = argv[i] + 8;
        else if (!wcscmp(argv[i], L"--telemetry")) Telemetry = LiveTelemetry = TRUE;
        else if (!wcscmp(argv[i], L"--telemetry-edges")) Telemetry = TRUE;
        else if (!wcsncmp(argv[i], L"--clock-probe=", 14)) ClockProbePath = argv[i] + 14;
        else if (!wcscmp(argv[i], L"--sample")) Sampling = TRUE;
        else if (!wcsncmp(argv[i], L"--sample-name=", 14))
        {
            Sampling = TRUE;
            SampleName = argv[i] + 14;
        }
        else return 2;
    }
    if (!Command && !Idle) return 2;
    if (StopFile && (!Command || !*StopFile)) return 2;
    if (Command)
    {
        if (!CreateProcessW(NULL, Command + 4, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &Startup, &Child))
        {
            printf("PROFILE_ERROR CreateProcess=%lu\n", GetLastError());
            return 2;
        }
        if (Affinity && !SetProcessAffinityMask(Child.hProcess, Affinity))
        {
            printf("PROFILE_ERROR SetProcessAffinityMask=%lu\n", GetLastError());
            TerminateProcess(Child.hProcess, 2);
            CloseHandle(Child.hThread);
            CloseHandle(Child.hProcess);
            return 2;
        }
    }
    if (Sampling) StartSampling(Query, SampleName);
    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Snapshot != INVALID_HANDLE_VALUE && Process32FirstW(Snapshot, &Entry))
    {
        do
        {
            PROCESS_SAMPLE *Sample = &Processes[Count];
            Sample->Handle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, Entry.th32ProcessID);
            if (!Sample->Handle) continue;
            if (!ProcessTimes(Sample->Handle, &Sample->Kernel, &Sample->User))
            {
                CloseHandle(Sample->Handle);
                continue;
            }
            Sample->Pid = Entry.th32ProcessID;
            wcsncpy(Sample->Name, Entry.szExeFile, MAX_PATH - 1);
            ++Count;
        } while (Count < 256 && Process32NextW(Snapshot, &Entry));
    }
    if (Snapshot != INVALID_HANDLE_VALUE) CloseHandle(Snapshot);
    if (Query(8, Before, sizeof(Before), &Length) < 0 ||
        Query(23, IrqBefore, sizeof(IrqBefore), NULL) < 0)
    {
        puts("PROFILE_ERROR NtQuerySystemInformation");
        if (Child.hProcess) TerminateProcess(Child.hProcess, 2);
        return 2;
    }
    Cpus = Length / sizeof(Before[0]);
    QueryPerformanceFrequency(&Frequency);
    if (AuditPath || Telemetry) ReportThermals("audit-before");
    if (AuditPath)
    {
        if (!AuditBegin(Query, Child.hProcess, Child.dwProcessId, Command && wcsstr(Command, L"coremark.exe")))
        {
            if (Child.hProcess) TerminateProcess(Child.hProcess, 4);
            puts("AUDIT_ERROR begin_failed"); return 4;
        }
    }
    TickBefore = GetTickCount();
    QueryPerformanceCounter(&Start);
    if (Child.hProcess)
    {
        DWORD WaitResult;
        if ((AuditPath || LiveTelemetry) && !StartFirmwareWorker()) AuditErrors |= 0x02000000;
        if (ClockProbePath && !StartClockProbe()) AuditErrors |= 0x04000000;
        ResumeThread(Child.hThread);
        if (StopFile)
        {
            DWORD WaitStarted = GetTickCount();
            do
            {
                WaitResult = WaitForSingleObject(Child.hProcess, 100);
                if (WaitResult != WAIT_TIMEOUT) break;
                if (GetFileAttributesW(StopFile) != INVALID_FILE_ATTRIBUTES)
                {
                    if (!TerminateProcess(Child.hProcess, 0))
                    {
                        printf("PROFILE_ERROR stop_child=%lu\n", GetLastError());
                        WaitResult = WAIT_FAILED;
                        break;
                    }
                    WaitResult = WaitForSingleObject(Child.hProcess, 10000);
                    if (WaitResult == WAIT_OBJECT_0)
                        printf("PROFILE_LOAD_STOP requested=1 pid=%lu\n", Child.dwProcessId);
                    break;
                }
            } while ((DWORD)(GetTickCount() - WaitStarted) < 240000);
        }
        else if (AuditPath)
        {
            DWORD AuditStarted = GetTickCount();
            do
            {
                WaitResult = WaitForSingleObject(Child.hProcess, 100);
                AuditObserveCore(Child.hProcess);
                AuditDrain(Query);
            } while (WaitResult == WAIT_TIMEOUT && (DWORD)(GetTickCount() - AuditStarted) < 240000);
        }
        else WaitResult = WaitForSingleObject(Child.hProcess, 240000);
        if (WaitResult != WAIT_OBJECT_0)
        {
            puts("PROFILE_ERROR child_timeout");
            TerminateProcess(Child.hProcess, 3);
            WaitForSingleObject(Child.hProcess, 10000);
        }
        GetExitCodeProcess(Child.hProcess, &ExitCode);
    }
    else if (AuditPath)
    {
        for (DWORD t = 0; t < Idle * 10; ++t) { Sleep(100); AuditDrain(Query); }
    }
    else Sleep(Idle * 1000);
    if (FirmwareStop) SetEvent(FirmwareStop);
    if (ProbeStop) SetEvent(ProbeStop);
    QueryPerformanceCounter(&End);
    TickAfter = GetTickCount();
    if (Sampling) StopSampling();
    Query(8, After, sizeof(After), NULL);
    Query(23, IrqAfter, sizeof(IrqAfter), NULL);
    Seconds = (double)(End.QuadPart - Start.QuadPart) / Frequency.QuadPart;
    printf("PROFILE wall_s=%.6f tick_s=%.6f qpc_hz=%lld cpus=%lu affinity=0x%llx exit=%lu\n",
           Seconds, (TickAfter - TickBefore) / 1000.0, Frequency.QuadPart, Cpus,
           (unsigned long long)Affinity, ExitCode);
    for (Index = 0; Index < Cpus; ++Index)
    {
        CPU_TIMES *A = &After[Index], *B = &Before[Index];
        double Scale = 100.0 / (Seconds * 1e7);
        printf("PROFILE_CPU cpu=%lu user=%.3f kernel=%.3f idle=%.3f dpc=%.3f isr=%.3f accounted=%.3f interrupts=%lu switches=%lu dpcs=%lu\n",
               Index, (A->User.QuadPart - B->User.QuadPart) * Scale,
               (A->Kernel.QuadPart - B->Kernel.QuadPart - A->Idle.QuadPart + B->Idle.QuadPart) * Scale,
               (A->Idle.QuadPart - B->Idle.QuadPart) * Scale,
               (A->Dpc.QuadPart - B->Dpc.QuadPart) * Scale,
               (A->Interrupt.QuadPart - B->Interrupt.QuadPart) * Scale,
               (A->Kernel.QuadPart - B->Kernel.QuadPart + A->User.QuadPart - B->User.QuadPart) * Scale,
               A->InterruptCount - B->InterruptCount,
               IrqAfter[Index].Switches - IrqBefore[Index].Switches,
               IrqAfter[Index].Dpcs - IrqBefore[Index].Dpcs);
    }
    for (Index = 0; Index < Count; ++Index)
    {
        PROCESS_SAMPLE *Sample = &Processes[Index];
        if (ProcessTimes(Sample->Handle, &Kernel, &User))
        {
            Kernel -= Sample->Kernel;
            User -= Sample->User;
            if (Kernel + User)
                printf("PROFILE_PROCESS pid=%lu name=%ls user_s=%.6f kernel_s=%.6f cpu_pct=%.3f\n",
                       Sample->Pid, Sample->Name, User / 1e7, Kernel / 1e7,
                       (Kernel + User) / (Seconds * 1e5));
        }
        CloseHandle(Sample->Handle);
    }
    if (Child.hProcess)
    {
        if (ProcessTimes(Child.hProcess, &Kernel, &User))
            printf("PROFILE_CHILD user_s=%.6f kernel_s=%.6f cpu_pct=%.3f\n",
                   User / 1e7, Kernel / 1e7, (Kernel + User) / (Seconds * 1e5));
        if (GetProcessMemoryInfo(Child.hProcess, &Memory, sizeof(Memory)))
            printf("PROFILE_MEMORY pagefaults=%lu peak_working_set=%llu\n",
                   Memory.PageFaultCount, (unsigned long long)Memory.PeakWorkingSetSize);
        CloseHandle(Child.hThread);
        CloseHandle(Child.hProcess);
    }
    if (AuditPath) AuditFinish(Query, AuditPath);
    if (ClockProbePath && !FinishClockProbe(ClockProbePath)) AuditErrors |= 0x04000000;
    if (AuditPath || Telemetry)
    {
        if (!StopFirmwareWorker())
        {
            puts("PROFILE_ERROR firmware_worker_timeout");
            ExitProcess(4);
        }
        ReportThermals("audit-after");
    }
    if (AuditErrors || AuditLost) ExitCode = 4;
    for (unsigned i = 0; i < FirmwareCount; ++i) fputs(FirmwareLines[i], stdout);
    if (Sampling) ReportSamples();
    return ExitCode;
}
