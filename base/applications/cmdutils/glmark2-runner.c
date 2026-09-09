/*
 * PROJECT:     ReactOS Raspberry Pi graphics validation
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Relay the unmodified glmark2 Win32 benchmark to the debug log
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Allow time for the full suite and scene initialization. */
#define GLMARK2_FULL_TIMEOUT_MILLISECONDS 600000
#define GLMARK2_OUTPUT_LINE_LENGTH 512
typedef enum _RUNNER_SCENE
{
    RunnerSceneNone,
    RunnerSceneIdeas,
    RunnerSceneJellyfish,
    RunnerSceneTerrain,
    RunnerSceneShadow
} RUNNER_SCENE;

#define RUNNER_SCENE_BIT(Scene) (1UL << ((ULONG)(Scene) - 1))
#define RUNNER_SCENE_ALL_MASK \
    (RUNNER_SCENE_BIT(RunnerSceneIdeas) | \
     RUNNER_SCENE_BIT(RunnerSceneJellyfish) | \
     RUNNER_SCENE_BIT(RunnerSceneTerrain) | \
     RUNNER_SCENE_BIT(RunnerSceneShadow))

typedef struct _RUNNER_OUTPUT_SCAN
{
    ULONG UnsupportedMatchLength;
    BOOL Unsupported;
    CHAR Line[GLMARK2_OUTPUT_LINE_LENGTH];
    ULONG LineLength;
    BOOL LineOverflow;
    RUNNER_SCENE PendingScene;
    ULONG IdeasFps;
    ULONG JellyfishFps;
    ULONG TerrainFps;
    ULONG ShadowFps;
    ULONG Score;
    BOOL IdeasSeen;
    BOOL JellyfishSeen;
    BOOL TerrainSeen;
    BOOL ShadowSeen;
    BOOL ScoreSeen;
    ULONG SeenMask;
} RUNNER_OUTPUT_SCAN, *PRUNNER_OUTPUT_SCAN;

static VOID
RunnerPrint(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[1024];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Arguments);
    va_end(Arguments);
    Buffer[sizeof(Buffer) - 1] = '\0';
    OutputDebugStringA(Buffer);
    fputs(Buffer, stdout);
    fflush(stdout);
}

static VOID
ScanSceneFps(
    _Inout_ PRUNNER_OUTPUT_SCAN Scan,
    _In_z_ PCSTR Line,
    _In_z_ PCSTR Prefix,
    _In_z_ PCSTR Name,
    _In_ RUNNER_SCENE Scene,
    _Out_ ULONG *Fps,
    _Out_ BOOL *Seen)
{
    PCSTR Marker;
    PCHAR End;
    ULONG Value;

    if (strncmp(Line, Prefix, strlen(Prefix)) == 0)
        Scan->PendingScene = Scene;
    if (Scan->PendingScene != Scene)
        return;
    Marker = strstr(Line, " FPS: ");
    if (Marker == NULL)
        return;
    Marker += sizeof(" FPS: ") - 1;
    Value = strtoul(Marker, &End, 10);
    if (End == Marker)
        return;
    *Fps = Value;
    *Seen = TRUE;
    Scan->SeenMask |= RUNNER_SCENE_BIT(Scene);
    Scan->PendingScene = RunnerSceneNone;
    RunnerPrint("RPI5_GLMARK2_SCENE name=%s fps=%lu\n", Name, Value);
}

static VOID
ScanChildOutputLine(
    _Inout_ PRUNNER_OUTPUT_SCAN Scan)
{
    PCSTR Marker;
    PCHAR End;
    ULONG Value;

    if (Scan->LineOverflow)
        return;
    Scan->Line[Scan->LineLength] = '\0';
    ScanSceneFps(Scan, Scan->Line, "[ideas]", "ideas", RunnerSceneIdeas,
                 &Scan->IdeasFps,
                 &Scan->IdeasSeen);
    ScanSceneFps(Scan, Scan->Line, "[jellyfish]", "jellyfish",
                 RunnerSceneJellyfish,
                 &Scan->JellyfishFps,
                 &Scan->JellyfishSeen);
    ScanSceneFps(Scan, Scan->Line, "[terrain]", "terrain",
                 RunnerSceneTerrain,
                 &Scan->TerrainFps,
                 &Scan->TerrainSeen);
    ScanSceneFps(Scan, Scan->Line, "[shadow]", "shadow",
                 RunnerSceneShadow,
                 &Scan->ShadowFps,
                 &Scan->ShadowSeen);

    Marker = strstr(Scan->Line, "glmark2 Score:");
    if (Marker == NULL)
        return;
    Marker += sizeof("glmark2 Score:") - 1;
    Value = strtoul(Marker, &End, 10);
    if (End != Marker)
    {
        Scan->Score = Value;
        Scan->ScoreSeen = TRUE;
    }
}

static VOID
ScanChildOutput(
    _Inout_ PRUNNER_OUTPUT_SCAN Scan,
    _In_reads_bytes_(Length) const CHAR *Buffer,
    _In_ DWORD Length)
{
    static const CHAR Unsupported[] = "Unsupported";
    DWORD Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (!Scan->Unsupported &&
            Buffer[Index] == Unsupported[Scan->UnsupportedMatchLength])
        {
            Scan->UnsupportedMatchLength++;
            if (Scan->UnsupportedMatchLength == sizeof(Unsupported) - 1)
                Scan->Unsupported = TRUE;
        }
        else if (!Scan->Unsupported)
        {
            Scan->UnsupportedMatchLength =
                Buffer[Index] == Unsupported[0] ? 1 : 0;
        }

        if (Buffer[Index] == '\n')
        {
            ScanChildOutputLine(Scan);
            Scan->LineLength = 0;
            Scan->LineOverflow = FALSE;
        }
        else if (Buffer[Index] != '\r')
        {
            if (Scan->LineLength < sizeof(Scan->Line) - 1)
                Scan->Line[Scan->LineLength++] = Buffer[Index];
            else
                Scan->LineOverflow = TRUE;
        }
    }
}

static RUNNER_SCENE
RunnerSceneFromSpec(
    _In_z_ PCSTR Spec)
{
    SIZE_T Length;

    Length = strcspn(Spec, ":");
    if (Length == sizeof("ideas") - 1 &&
        strncmp(Spec, "ideas", Length) == 0)
        return RunnerSceneIdeas;
    if (Length == sizeof("jellyfish") - 1 &&
        strncmp(Spec, "jellyfish", Length) == 0)
        return RunnerSceneJellyfish;
    if (Length == sizeof("terrain") - 1 &&
        strncmp(Spec, "terrain", Length) == 0)
        return RunnerSceneTerrain;
    if (Length == sizeof("shadow") - 1 &&
        strncmp(Spec, "shadow", Length) == 0)
        return RunnerSceneShadow;
    return RunnerSceneNone;
}

static VOID
DrainChildOutput(
    _In_ HANDLE Pipe,
    _Inout_ PRUNNER_OUTPUT_SCAN Scan)
{
    CHAR Buffer[1024];
    DWORD Available;
    DWORD BytesRead;
    DWORD BytesToRead;

    while (PeekNamedPipe(Pipe, NULL, 0, NULL, &Available, NULL) &&
           Available != 0)
    {
        BytesToRead = Available;
        if (BytesToRead >= sizeof(Buffer))
            BytesToRead = sizeof(Buffer) - 1;
        if (!ReadFile(Pipe, Buffer, BytesToRead, &BytesRead, NULL) ||
            BytesRead == 0)
        {
            break;
        }

        ScanChildOutput(Scan, Buffer, BytesRead);
        Buffer[BytesRead] = '\0';
        OutputDebugStringA(Buffer);
        fwrite(Buffer, 1, BytesRead, stdout);
        fflush(stdout);
    }
}

int
main(int argc, char **argv)
{
    CHAR ApplicationPath[MAX_PATH];
    CHAR CommandLine[MAX_PATH * 8];
    CHAR DataPath[MAX_PATH];
    CHAR SystemDirectory[MAX_PATH];
    SECURITY_ATTRIBUTES SecurityAttributes;
    STARTUPINFOA StartupInfo;
    PROCESS_INFORMATION ProcessInformation;
    HANDLE ReadPipe = NULL;
    HANDLE WritePipe = NULL;
    DWORD ExitCode = ERROR_GEN_FAILURE;
    DWORD StartTick;
    DWORD WaitStatus;
    UINT Length;
    BOOL Forced = FALSE;
    BOOL FullSuite;
    DWORD TimeoutMilliseconds;
    int Argument;
    CHAR BenchList[MAX_PATH * 4];
    UINT BenchCount = 0;
    ULONG ExpectedSceneMask = 0;
    BOOL Complete;
    RUNNER_SCENE Scene;
    RUNNER_OUTPUT_SCAN OutputScan;

    ZeroMemory(&OutputScan, sizeof(OutputScan));
    /* Run glmark2's complete default benchmark list unless --bench selects
     * a custom sequence. --full remains accepted for existing launchers. */
    /* --bench SPEC (repeatable) runs exactly the named scenes instead: an
     * isolated reproduction of one scene sequence with a short boot. */
    BenchList[0] = '\0';
    for (Argument = 1; Argument < argc; Argument++)
    {
        if (strcmp(argv[Argument], "--full") == 0)
            continue;
        else if (strcmp(argv[Argument], "--bench") == 0 && Argument + 1 < argc)
        {
            size_t Used = strlen(BenchList);

            Scene = RunnerSceneFromSpec(argv[Argument + 1]);
            if (Scene != RunnerSceneNone)
                ExpectedSceneMask |= RUNNER_SCENE_BIT(Scene);

            if (_snprintf(BenchList + Used, sizeof(BenchList) - Used,
                          " -b %s", argv[++Argument]) < 0)
            {
                RunnerPrint("RPI5_GLMARK2_ERROR bench_list_too_long\n");
                return 1;
            }
            BenchCount++;
        }
    }
    FullSuite = (BenchCount == 0);
    if (FullSuite)
        ExpectedSceneMask = RUNNER_SCENE_ALL_MASK;
    if (BenchCount != 0)
        TimeoutMilliseconds = 60000 + BenchCount * 20000;
    else
        TimeoutMilliseconds = GLMARK2_FULL_TIMEOUT_MILLISECONDS;

    Length = GetSystemDirectoryA(SystemDirectory, sizeof(SystemDirectory));
    if (Length == 0 || Length >= sizeof(SystemDirectory))
    {
        RunnerPrint("RPI5_GLMARK2_ERROR get_system_directory=%lu\n",
                    GetLastError());
        return 1;
    }

    if (_snprintf(ApplicationPath,
                  sizeof(ApplicationPath),
                  "%s\\glmark2.exe",
                  SystemDirectory) < 0 ||
        _snprintf(DataPath,
                  sizeof(DataPath),
                  "%s\\glmark2",
                  SystemDirectory) < 0 ||
        _snprintf(CommandLine,
                  sizeof(CommandLine),
                  "\"%s\" --data-path \"%s\" -s 800x600 "
                  "--swap-mode immediate%s",
                  ApplicationPath,
                  DataPath,
                  BenchList) < 0)
    {
        RunnerPrint("RPI5_GLMARK2_ERROR path_too_long\n");
        return 1;
    }
    ApplicationPath[sizeof(ApplicationPath) - 1] = '\0';
    DataPath[sizeof(DataPath) - 1] = '\0';
    CommandLine[sizeof(CommandLine) - 1] = '\0';

    ZeroMemory(&SecurityAttributes, sizeof(SecurityAttributes));
    SecurityAttributes.nLength = sizeof(SecurityAttributes);
    SecurityAttributes.bInheritHandle = TRUE;
    if (!CreatePipe(&ReadPipe, &WritePipe, &SecurityAttributes, 0) ||
        !SetHandleInformation(ReadPipe, HANDLE_FLAG_INHERIT, 0))
    {
        RunnerPrint("RPI5_GLMARK2_ERROR create_pipe=%lu\n",
                    GetLastError());
        if (ReadPipe != NULL)
            CloseHandle(ReadPipe);
        if (WritePipe != NULL)
            CloseHandle(WritePipe);
        return 1;
    }

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    StartupInfo.hStdOutput = WritePipe;
    StartupInfo.hStdError = WritePipe;
    ZeroMemory(&ProcessInformation, sizeof(ProcessInformation));

    RunnerPrint("RPI5_GLMARK2_BEGIN source=glmark2 "
                "commit=22c527cb0556f3a1ac4445aaa52cc532760928d5 "
                "suite=%s bench_count=%u expected_scene_mask=0x%lx "
                "size=800x600 swap_mode=immediate\n",
                FullSuite ? "full" : "custom",
                BenchCount,
                ExpectedSceneMask);
    if (!CreateProcessA(ApplicationPath,
                        CommandLine,
                        NULL,
                        NULL,
                        TRUE,
                        0,
                        NULL,
                        NULL,
                        &StartupInfo,
                        &ProcessInformation))
    {
        RunnerPrint("RPI5_GLMARK2_ERROR create_process=%lu\n",
                    GetLastError());
        CloseHandle(ReadPipe);
        CloseHandle(WritePipe);
        return 1;
    }

    CloseHandle(WritePipe);
    WritePipe = NULL;
    CloseHandle(ProcessInformation.hThread);
    StartTick = GetTickCount();

    do
    {
        WaitStatus = WaitForSingleObject(ProcessInformation.hProcess, 20);
        DrainChildOutput(ReadPipe, &OutputScan);
        if (WaitStatus == WAIT_OBJECT_0)
            break;
    } while (GetTickCount() - StartTick < TimeoutMilliseconds);

    if (WaitStatus != WAIT_OBJECT_0)
    {
        Forced = TRUE;
        TerminateProcess(ProcessInformation.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(ProcessInformation.hProcess, 1000);
    }

    DrainChildOutput(ReadPipe, &OutputScan);
    if (OutputScan.LineLength != 0 || OutputScan.LineOverflow)
        ScanChildOutputLine(&OutputScan);
    if (!GetExitCodeProcess(ProcessInformation.hProcess, &ExitCode))
        ExitCode = GetLastError();
    if (OutputScan.Unsupported)
        RunnerPrint("RPI5_GLMARK2_UNSUPPORTED detected=1\n");
    Complete = (OutputScan.SeenMask & ExpectedSceneMask) ==
                   ExpectedSceneMask &&
               OutputScan.ScoreSeen;
    RunnerPrint("RPI5_GLMARK2_RESULT size=800x600 suite=%s ideas=%lu jellyfish=%lu "
                "terrain=%lu shadow=%lu %s=%lu seen_mask=0x%lx "
                "expected_mask=0x%lx complete=%lu\n",
                FullSuite ? "full" : "subset",
                OutputScan.IdeasFps,
                OutputScan.JellyfishFps,
                OutputScan.TerrainFps,
                OutputScan.ShadowFps,
                FullSuite ? "score" : "subset_score",
                OutputScan.Score,
                OutputScan.SeenMask,
                ExpectedSceneMask,
                Complete);
    RunnerPrint("RPI5_GLMARK2_END exit=%lu runtime_ms=%lu forced=%lu\n",
                ExitCode,
                GetTickCount() - StartTick,
                Forced);

    CloseHandle(ReadPipe);
    CloseHandle(ProcessInformation.hProcess);
    return ExitCode == EXIT_SUCCESS && !Forced &&
           !OutputScan.Unsupported && Complete ? 0 : 1;
}
