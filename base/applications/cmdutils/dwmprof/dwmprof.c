/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 * Standalone userspace consumer of the shared profiling SDK.
 */
#include <reactos/dwmprof.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

static BOOL Serial;
static FILE *Output;
static void Print(const char *Format, ...)
{
    char Buffer[1024];
    va_list Args;
    va_start(Args, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Args);
    va_end(Args);
    Buffer[sizeof(Buffer) - 1] = 0;
    fputs(Buffer, Output);
    if (Serial)
        OutputDebugStringA(Buffer);
}

static int Dump(const DPT_SNAPSHOT *Snapshot)
{
    static const char *Names[DPT_METRIC_COUNT] = {
        "dirty_frame_fetch_to_ack", "metadata_fetch", "scene_prepare", "gpu_begin",
        "window", "texture_layer", "blur", "shadow", "swap", "surface_ack",
        "mesa_present", "primary_query", "scanout_blit_issue", "flush",
        "device_lock_wait", "primary_fence_wait", "primary_cache_invalidate",
        "kmt_present", "kmt_submit", "winsys_ioctl", "sampler_shadow_update",
        "kernel_command_admit", "kernel_track", "miniport_render", "miniport_submit",
        "kernel_present", "admit_to_dispatch", "dispatch_to_retire", "fence_publish",
        "blur_cache_hit", "blur_filter", "bo_create", "cpu_texture_upload"
    };
    ULONG Domain, Metric;
    int Result = Snapshot->Available == 7 ? 0 : 2;
    Print("DWM_CAPTURE_BEGIN version=%lu session=%lu available=%lx "
          "cpu_kernel_100ns=%I64u cpu_user_100ns=%I64u\n",
          Snapshot->Version, Snapshot->Session, Snapshot->Available,
          Snapshot->CpuKernel100ns, Snapshot->CpuUser100ns);
    for (Domain = 0; Domain < 3; ++Domain)
    {
        const DPT_DOMAIN *Data = &Snapshot->Domain[Domain];
        Print("DWM_CAPTURE_DOMAIN id=%lu status=%08lx pid=%lu frequency=%I64u "
              "start=%I64u stop=%I64u\n", Domain, Snapshot->Status[Domain],
              Data->ProcessId, Data->Frequency, Data->Start, Data->Stop);
        if (!(Snapshot->Available & (1 << Domain)) || Snapshot->Status[Domain] < 0)
        {
            Result = 2;
            continue;
        }
        for (Metric = 0; Metric < DPT_METRIC_COUNT; ++Metric)
        {
            const DPT_COUNTER *Counter = &Data->Counter[Metric];
            if (!Counter->Entered)
                continue;
            Print("DWM_CAPTURE_COUNTER domain=%lu stage=%s entered=%I64u completed=%I64u "
                  "unfinished=%I64u failed=%I64u ticks=%I64u max_ticks=%I64u bytes=%I64u\n",
                  Domain, Names[Metric], Counter->Entered, Counter->Completed,
                  Counter->Entered - Counter->Completed, Counter->Failed,
                  Counter->Ticks, Counter->MaxTicks, Counter->Bytes);
            if (Counter->Completed > Counter->Entered || Counter->Failed > Counter->Completed)
                Result = 1;
        }
    }
    Print("DWM_CAPTURE_END result=%d\n", Result);
    return Result;
}

static BOOL Number(const char *Text, ULONG *Value)
{
    char *End;
    unsigned long Parsed;
    errno = 0;
    if (!Text[0] || Text[0] == '-')
        return FALSE;
    Parsed = strtoul(Text, &End, 10);
    if (errno || *End || Parsed > 0xffffffffUL)
        return FALSE;
    *Value = Parsed;
    return TRUE;
}

static HRESULT SelfTest(ULONG Duration, DPT_SNAPSHOT *Snapshot)
{
    DPT_SNAPSHOT *Again = HeapAlloc(GetProcessHeap(), 0, sizeof(*Again));
    ULONG Session = 0, Other;
    HRESULT Status;
    if (!Again)
        return E_OUTOFMEMORY;
    Status = DwmProfileCapture(Duration, Snapshot, sizeof(*Snapshot));
    if (FAILED(Status))
        goto Done;
    /* Frozen data remains byte-identical, even while another capture runs. */
    Status = DwmProfileStartCapture(&Session);
    if (FAILED(Status))
        goto Done;
    if (SUCCEEDED(DwmProfileStartCapture(&Other)) ||
        SUCCEEDED(DwmProfileStopCapture(Session ^ 0x80000000u, Again, sizeof(*Again))))
    {
        Status = E_FAIL;
        goto Done;
    }
    Sleep(Duration);
    Status = DwmProfileReadLastCapture(Again, sizeof(*Again));
    if (FAILED(Status) || memcmp(Snapshot, Again, sizeof(*Again)))
    {
        Status = E_FAIL;
        goto Done;
    }
    Status = DwmProfileStopCapture(Session, Snapshot, sizeof(*Snapshot));
    if (FAILED(Status))
        goto Done;
    Session = 0;
    Sleep(200);
    Status = DwmProfileReadLastCapture(Again, sizeof(*Again));
    if (SUCCEEDED(Status) && memcmp(Snapshot, Again, sizeof(*Again)))
        Status = E_FAIL;
Done:
    if (Session)
        DwmProfileStopCapture(Session, Again, sizeof(*Again));
    HeapFree(GetProcessHeap(), 0, Again);
    return Status;
}

int main(int argc, char **argv)
{
    DPT_SNAPSHOT *Snapshot;
    HRESULT Status = E_INVALIDARG;
    ULONG Value = 10000;
    int i, Count = 1, Result = 1;
    BOOL Raw = FALSE, Start = FALSE;
    Output = stdout;
    for (i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--serial"))
            Serial = TRUE;
        else
            argv[Count++] = argv[i];
    argc = Count;
    if (argc < 2 || argc > 3)
        goto Usage;
    Snapshot = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, DwmProfileGetSnapshotSize());
    if (!Snapshot)
        return 1;
    if (!strcmp(argv[1], "capture") || !strcmp(argv[1], "selftest"))
    {
        if (argc == 3 && !Number(argv[2], &Value))
            goto Done;
        Status = !strcmp(argv[1], "capture") ?
            DwmProfileCapture(Value, Snapshot, sizeof(*Snapshot)) : SelfTest(Value, Snapshot);
    }
    else if (!strcmp(argv[1], "start") && argc == 2)
    {
        Status = DwmProfileStartCapture(&Value);
        Start = TRUE;
    }
    else if (!strcmp(argv[1], "stop") && argc == 3 && Number(argv[2], &Value))
        Status = DwmProfileStopCapture(Value, Snapshot, sizeof(*Snapshot));
    else if (!strcmp(argv[1], "dump") || !strcmp(argv[1], "raw"))
    {
        Raw = !strcmp(argv[1], "raw");
        if (Raw && argc != 3)
            goto Done;
        Status = DwmProfileReadLastCapture(Snapshot, sizeof(*Snapshot));
        if (SUCCEEDED(Status) && argc == 3)
        {
            Output = fopen(argv[2], Raw ? "wb" : "w");
            if (!Output)
            {
                Output = stdout;
                Status = HRESULT_FROM_WIN32(ERROR_OPEN_FAILED);
            }
        }
    }
    if (FAILED(Status))
    {
        Print("DWM_CAPTURE_ERROR status=%08lx\n", Status);
        goto Done;
    }
    if (Start)
    {
        Print("DWM_CAPTURE_STARTED session=%lu; stop with: dwmprof stop %lu\n", Value, Value);
        Result = 0;
    }
    else if (Raw)
        Result = fwrite(Snapshot, sizeof(*Snapshot), 1, Output) == 1 ? 0 : 1;
    else
        Result = Dump(Snapshot);
Done:
    HeapFree(GetProcessHeap(), 0, Snapshot);
    if (Output != stdout && fclose(Output))
        Result = 1;
    return Result;
Usage:
    Print("dwmprof capture [milliseconds] | start | stop SESSION | dump [file] | raw file\n"
          "dwmprof selftest [milliseconds]\n"
          "Add --serial to mirror the final dump to the debugger.\n");
    return 1;
}
