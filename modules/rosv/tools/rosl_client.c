/*
 * PROJECT:     ReactOS VMX Hypervisor Launcher (rosl.exe)
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Interactive client mode functions
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 */

#include "rosl_common.h"

/*
 * VconInteractiveLoop - Interactive terminal loop using virtio-console port.
 *
 * This is a simplified interactive loop that polls the virtio-console port
 * for guest output and forwards host keyboard input to the guest.
 * No PTY, no probe, no WSL automation — just raw terminal I/O.
 */
void VconInteractiveLoop(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD stdoutMode = 0;
    DWORD ret;

    RoslLog("[INFO] Entering virtio-console mode (port %lu, Ctrl+C to exit)...\n",
            g_VconPort);
    RoslResetTerminalInputMode();

    if (hStdout != INVALID_HANDLE_VALUE &&
        GetConsoleMode(hStdout, &stdoutMode))
    {
        (void)stdoutMode;
    }

    ConsoleSetRawMode();

    while (g_Running)
    {
        BOOL ioActivity = FALSE;
        BYTE readBuf[4096];
        DWORD bytesRead = 0;

        /* 1. Poll guest output via VCON_PORT_READ */
        if (VconReadOutput(readBuf, sizeof(readBuf), &bytesRead) && bytesRead > 0)
        {
            ioActivity = TRUE;
            bytesRead = RoslFeedTerminalOutput(readBuf, bytesRead);
            if (hStdout != INVALID_HANDLE_VALUE)
            {
                DWORD written;
                if (bytesRead > 0)
                    WriteFile(hStdout, readBuf, bytesRead, &written, NULL);
            }
        }

        /* 2. Check stdin for user key input (non-blocking) */
        if (g_StdinIsConsole && g_hStdin != INVALID_HANDLE_VALUE)
        {
            DWORD numEvents = 0;

            if (GetNumberOfConsoleInputEvents(g_hStdin, &numEvents) && numEvents > 0)
            {
                INPUT_RECORD records[64];
                DWORD numRead = 0;
                DWORD i;

                if (ReadConsoleInputW(g_hStdin, records, 64, &numRead))
                {
                    for (i = 0; i < numRead; i++)
                    {
                        if (records[i].EventType == KEY_EVENT &&
                            records[i].Event.KeyEvent.bKeyDown)
                        {
                            KEY_EVENT_RECORD *keyEvent = &records[i].Event.KeyEvent;
                            UCHAR seq[8];
                            DWORD seqLen;

                            seqLen = RoslTranslateKeyEvent(keyEvent, seq, sizeof(seq));
                            if (seqLen > 0 && VconSendInput(seq, seqLen))
                            {
                                ioActivity = TRUE;
                            }
                        }
                    }
                }
            }
        }
        else if (g_hStdin != INVALID_HANDLE_VALUE)
        {
            UCHAR rawInput[256];
            DWORD rawLen = ReadRedirectedStdin(rawInput, sizeof(rawInput));
            if (rawLen > 0)
            {
                VconSendInput(rawInput, rawLen);
                ioActivity = TRUE;
            }
        }

        /* 3. State polling — check VM is still alive */
        {
            ROSV_VM_STATE_INFO stateInfo;
            if (Ioctl(ROSV_IOCTL_GET_STATE, NULL, 0,
                      &stateInfo, sizeof(stateInfo), &ret))
            {
                if (stateInfo.State == RosvVmStateStopped ||
                    stateInfo.State == RosvVmStateError ||
                    stateInfo.State == RosvVmStateCrashed)
                {
                    RoslLog("[INFO] VM stopped/crashed (state=%u), exiting vcon loop\n",
                            stateInfo.State);
                    break;
                }
            }
        }

        if (!ioActivity)
            Sleep(5);
    }

    ConsoleRestoreMode();
    RoslLog("[INFO] Exited virtio-console loop\n");
}

void PtyInteractiveLoop(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD stdoutMode = 0;
    DWORD ret;
    ROSV_VM_STATE_INFO stateInfo;
    ROSV_VM_STATE lastState = (ROSV_VM_STATE)-1;
    DWORD stateCounter = 0;
    /* PTY_READ uses: input = PTY_READ_REQUEST, output = IO_RESULT + data */
    ROSV_PTY_READ_REQUEST ptyReadReq;
    BYTE readResBuf[4096 + 8];
    ROSV_PTY_IO_RESULT *readRes = (ROSV_PTY_IO_RESULT *)readResBuf;
    DWORD stateHeartbeatCounter = 0;

    RoslLog("[INFO] Entering PTY client mode (session=%lu pty=%lu reader=%lu)\n",
            g_AttachSessionId,
            g_PtyIndex,
            g_ReaderIndex);
    RoslResetTerminalInputMode();

    if (hStdout != INVALID_HANDLE_VALUE &&
        GetConsoleMode(hStdout, &stdoutMode))
    {
        (void)stdoutMode;
    }

    ConsoleSetRawMode();

    while (g_Running)
    {
        BOOL ioActivity = FALSE;

        /* 1. Poll PTY output only. */
        memset(readResBuf, 0, sizeof(readResBuf));
        ptyReadReq.PtyIndex = g_PtyIndex;
        ptyReadReq.ReaderIndex = g_ReaderIndex;
        if (Ioctl(ROSV_IOCTL_PTY_READ, &ptyReadReq, sizeof(ptyReadReq),
                  readResBuf, sizeof(readResBuf), &ret))
        {
            if (readRes->BytesTransferred > 0)
            {
                DWORD bytesRead = readRes->BytesTransferred;

                ioActivity = TRUE;
                bytesRead = RoslFeedTerminalOutput(readRes->Data, bytesRead);
                if (hStdout != INVALID_HANDLE_VALUE)
                {
                    DWORD written;
                    if (bytesRead > 0)
                        WriteFile(hStdout, readRes->Data, bytesRead, &written, NULL);
                }
            }
        }

        /* 2. Check stdin for user key input (non-blocking) */
        if (g_StdinIsConsole && g_hStdin != INVALID_HANDLE_VALUE)
        {
            DWORD numEvents = 0;

            if (GetNumberOfConsoleInputEvents(g_hStdin, &numEvents) && numEvents > 0)
            {
                INPUT_RECORD records[64];
                DWORD numRead = 0;
                DWORD i;

                if (ReadConsoleInputW(g_hStdin, records, 64, &numRead))
                {
                    for (i = 0; i < numRead; i++)
                    {
                        if (records[i].EventType == KEY_EVENT &&
                            records[i].Event.KeyEvent.bKeyDown)
                        {
                            KEY_EVENT_RECORD *keyEvent = &records[i].Event.KeyEvent;
                            UCHAR seq[8];
                            DWORD seqLen;

                            seqLen = RoslTranslateKeyEvent(keyEvent, seq, sizeof(seq));
                            if (seqLen > 0 &&
                                PtySendInput(seq, seqLen))
                            {
                                ioActivity = TRUE;
                            }
                        }
                        else if (records[i].EventType == WINDOW_BUFFER_SIZE_EVENT)
                        {
                            COORD newSize = records[i].Event.WindowBufferSizeEvent.dwSize;

                            if (PtyResize((USHORT)newSize.Y, (USHORT)newSize.X))
                                ioActivity = TRUE;
                        }
                    }
                }
            }
        }
        else if (g_hStdin != INVALID_HANDLE_VALUE)
        {
            UCHAR rawInput[256];
            DWORD rawLen = ReadRedirectedStdin(rawInput, sizeof(rawInput));
            if (rawLen > 0)
            {
                PtySendInput(rawInput, rawLen);
                ioActivity = TRUE;
            }
        }

        /* 3. Periodically poll VM state (~every 500ms = 10 iterations at 50ms) */
        stateCounter++;
        if (stateCounter >= 10)
        {
            stateCounter = 0;
            memset(&stateInfo, 0, sizeof(stateInfo));
            if (Ioctl(ROSV_IOCTL_GET_STATE, NULL, 0, &stateInfo, sizeof(stateInfo), &ret))
            {
                if (stateInfo.State != lastState)
                {
                    lastState = stateInfo.State;
                    stateHeartbeatCounter = 0;
                }
                else
                {
                    stateHeartbeatCounter++;
                    if (stateInfo.State == RosvVmStateRunning &&
                        stateHeartbeatCounter >= 10)
                    {
                            stateHeartbeatCounter = 0;
                    }
                }

                if (stateInfo.State == RosvVmStateStopped ||
                    stateInfo.State == RosvVmStateError ||
                    stateInfo.State == RosvVmStateCrashed)
                {
                    break;
                }
            }
        }

        if (!ioActivity)
        {
            if (g_StdinIsConsole && g_hStdin != INVALID_HANDLE_VALUE)
            {
                DWORD wait = WaitForSingleObject(g_hStdin, POLL_INTERVAL_MS);
                if (wait == WAIT_FAILED)
                    Sleep(POLL_INTERVAL_MS);
            }
            else
            {
                Sleep(POLL_INTERVAL_MS);
            }
        }
        else
        {
            Sleep(0);
        }
    }

    ConsoleRestoreMode();
    RoslLog("[INFO] PTY client loop exited\n");
}

/* ---- UART interactive console loop (legacy) ----------------------------- */

void UartInteractiveLoop(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    BYTE readBuf[CONSOLE_BUF_SIZE];
    ROSV_VM_STATE_INFO stateInfo;
    ROSV_VM_STATE lastState = (ROSV_VM_STATE)-1;
    DWORD ret;
    DWORD stateCounter = 0;
    DWORD stateHeartbeatCounter = 0;

    RoslLog("[INFO] Entering interactive console (Ctrl+C to exit)...\n");
    RoslResetTerminalInputMode();

    ConsoleSetRawMode();

    while (g_Running)
    {
        BOOL ioActivity = FALSE;

        /* 1. Poll CONSOLE_READ for VM serial output */
        memset(readBuf, 0, sizeof(readBuf));
        if (Ioctl(ROSV_IOCTL_CONSOLE_READ, NULL, 0, readBuf, sizeof(readBuf) - 1, &ret))
        {
            if (ret > 0)
            {
                DWORD written;
                ioActivity = TRUE;
                ret = RoslFeedTerminalOutput(readBuf, ret);
                readBuf[ret] = '\0';
                if (ret > 0)
                    WriteFile(hStdout, readBuf, ret, &written, NULL);
            }
        }

        /* 2. Check stdin for user key input (non-blocking) */
        if (g_hStdin != INVALID_HANDLE_VALUE)
        {
            if (g_StdinIsConsole)
            {
                DWORD numEvents = 0;

                if (GetNumberOfConsoleInputEvents(g_hStdin, &numEvents) && numEvents > 0)
                {
                    INPUT_RECORD records[64];
                    DWORD numRead = 0;
                    DWORD i;

                    if (ReadConsoleInputW(g_hStdin, records, 64, &numRead))
                    {
                        for (i = 0; i < numRead; i++)
                        {
                            if (records[i].EventType == KEY_EVENT &&
                                records[i].Event.KeyEvent.bKeyDown)
                            {
                                UCHAR seq[8];
                                DWORD seqLen = RoslTranslateKeyEvent(&records[i].Event.KeyEvent,
                                                                     seq,
                                                                     sizeof(seq));

                                if (seqLen > 0 &&
                                    Ioctl(ROSV_IOCTL_CONSOLE_WRITE,
                                          seq,
                                          seqLen,
                                          NULL,
                                          0,
                                          &ret))
                                {
                                    ioActivity = TRUE;
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                UCHAR rawInput[256];
                DWORD rawLen = ReadRedirectedStdin(rawInput, sizeof(rawInput));
                if (rawLen > 0)
                {
                    Ioctl(ROSV_IOCTL_CONSOLE_WRITE, rawInput, rawLen, NULL, 0, &ret);
                    ioActivity = TRUE;
                }
            }
        }

        /* 3. Periodically poll VM state (~every 500ms = 10 iterations at 50ms) */
        stateCounter++;
        if (stateCounter >= 10)
        {
            stateCounter = 0;
            memset(&stateInfo, 0, sizeof(stateInfo));
            if (Ioctl(ROSV_IOCTL_GET_STATE, NULL, 0, &stateInfo, sizeof(stateInfo), &ret))
            {
                if (stateInfo.State != lastState)
                {
                    /* State transition: debug serial only */
                    DbgPrint("rosl: [STATE] %s | exits=%llu last_exit=0x%lX checkpoint=%s\n",
                           StateName(stateInfo.State),
                           (unsigned long long)stateInfo.ExitCount,
                           (unsigned long)stateInfo.LastExitReason,
                           CheckpointName(stateInfo.LastCheckpoint));
                    lastState = stateInfo.State;
                    stateHeartbeatCounter = 0;
                }
                else
                {
                    stateHeartbeatCounter++;
                    if (stateInfo.State == RosvVmStateRunning &&
                        stateHeartbeatCounter >= 10)
                    {
                        stateHeartbeatCounter = 0;
                    }
                }

                if (stateInfo.State == RosvVmStateStopped ||
                    stateInfo.State == RosvVmStateError ||
                    stateInfo.State == RosvVmStateCrashed)
                {
                    RoslLog("\r\n[INFO] VM is no longer running (state=%s)\r\n",
                           StateName(stateInfo.State));
                    break;
                }
            }
        }

        /*
         * Sleep strategy:
         *  - Idle: wait on console input handle so a keypress wakes immediately.
         *  - Active: Sleep(0) yields without timer-granularity latency.
         */
        if (!ioActivity)
        {
            if (g_hStdin != INVALID_HANDLE_VALUE && g_StdinIsConsole)
            {
                DWORD wait = WaitForSingleObject(g_hStdin, POLL_INTERVAL_MS);
                if (wait == WAIT_FAILED)
                    Sleep(POLL_INTERVAL_MS);
            }
            else
            {
                Sleep(POLL_INTERVAL_MS);
            }
        }
        else
        {
            Sleep(0);
        }
    }

    ConsoleRestoreMode();
}

/* ---- Client control plane ----------------------------------------------- */

HANDLE
RoslClientConnectControl(
    _In_ ULONG SessionId)
{
    HANDLE Pipe;
    DWORD Mode = PIPE_READMODE_MESSAGE;
    char PipeName[128];

    RoslBuildControlPipeName(SessionId, PipeName, sizeof(PipeName));

    if (!WaitNamedPipeA(PipeName, 5000))
        return INVALID_HANDLE_VALUE;

    Pipe = CreateFileA(PipeName,
                       GENERIC_READ | GENERIC_WRITE,
                       0,
                       NULL,
                       OPEN_EXISTING,
                       0,
                       NULL);
    if (Pipe == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    SetNamedPipeHandleState(Pipe, &Mode, NULL, NULL);
    return Pipe;
}

BOOL
RoslClientAllocatePty(
    _In_ HANDLE Pipe,
    _In_ ULONG SessionId,
    _In_ USHORT Rows,
    _In_ USHORT Cols,
    _Out_ PULONG PtyIndex,
    _Out_ PULONG ReaderIndex,
    _Out_opt_ PULONG Flags)
{
    ROSL_CONTROL_REQUEST Request;
    ROSL_CONTROL_RESPONSE Response;

    if (PtyIndex == NULL || ReaderIndex == NULL)
        return FALSE;

    memset(&Request, 0, sizeof(Request));
    memset(&Response, 0, sizeof(Response));
    Request.Version = ROSL_CONTROL_VERSION;
    Request.Op = RoslCtlOpAllocateInteractive;
    Request.SessionId = SessionId;
    Request.Rows = Rows;
    Request.Cols = Cols;

    if (!RoslPipeWriteExact(Pipe, &Request, sizeof(Request)) ||
        !RoslPipeReadExact(Pipe, &Response, sizeof(Response)))
    {
        return FALSE;
    }

    if (Response.Status != ERROR_SUCCESS)
    {
        SetLastError((DWORD)Response.Status);
        return FALSE;
    }

    *PtyIndex = Response.PtyIndex;
    *ReaderIndex = Response.ReaderIndex;
    if (Flags != NULL)
        *Flags = Response.Flags;
    return TRUE;
}

BOOL
RoslDiscoverServiceSession(
    _Out_ PULONG SessionId)
{
    HANDLE Mapping;
    ROSL_DISCOVERY_SHARED *View;
    BOOL Found = FALSE;

    if (SessionId == NULL)
        return FALSE;

    *SessionId = 0;

    Mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, ROSL_DISCOVERY_MAPPING_NAME);
    if (Mapping == NULL)
        return FALSE;

    View = (ROSL_DISCOVERY_SHARED *)MapViewOfFile(Mapping,
                                                  FILE_MAP_READ,
                                                  0,
                                                  0,
                                                  sizeof(ROSL_DISCOVERY_SHARED));
    if (View != NULL)
    {
        if (View->Version == ROSL_CONTROL_VERSION &&
            View->SessionId != 0 &&
            (View->Flags & 1) != 0)
        {
            *SessionId = View->SessionId;
            Found = TRUE;
        }

        UnmapViewOfFile(View);
    }

    CloseHandle(Mapping);
    return Found;
}

void
RoslClientReleasePty(
    _In_ HANDLE Pipe,
    _In_ ULONG SessionId,
    _In_ ULONG PtyIndex,
    _In_ ULONG ReaderIndex)
{
    ROSL_CONTROL_REQUEST Request;
    ROSL_CONTROL_RESPONSE Response;

    memset(&Request, 0, sizeof(Request));
    memset(&Response, 0, sizeof(Response));
    Request.Version = ROSL_CONTROL_VERSION;
    Request.Op = RoslCtlOpReleaseInteractive;
    Request.SessionId = SessionId;
    Request.PtyIndex = PtyIndex;
    Request.ReaderIndex = ReaderIndex;

    RoslPipeWriteExact(Pipe, &Request, sizeof(Request));
    RoslPipeReadExact(Pipe, &Response, sizeof(Response));
}

/* ---- Interactive client entry point ------------------------------------- */

int
RoslRunInteractiveClient(
    _In_opt_ HANDLE hMutex,
    _In_ BOOL DeviceAlreadyOpen)
{
    HANDLE ControlPipe;
    USHORT DetectedRows = 0;
    USHORT DetectedCols = 0;
    ULONG ControlFlags = 0;

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    if (g_IoMode == RoslModeUart)
    {
        RoslLog("[FAIL] Client mode requires PTY; the service owns the serial path\n");
        if (g_hDev != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_hDev);
            g_hDev = INVALID_HANDLE_VALUE;
        }
        if (hMutex) CloseHandle(hMutex);
        return 2;
    }

    if (!DeviceAlreadyOpen)
    {
        if (!EnsureDriverLoaded())
            Die("Failed to load rosv.sys driver");

        RoslLog("[INFO] Opening \\\\.\\RosvHypervisor\n");
        g_hDev = OpenRosvDeviceWithRetry(50, 100);
        if (g_hDev == INVALID_HANDLE_VALUE)
            Die("Cannot open \\\\.\\RosvHypervisor - is rosv.sys loaded?");
        RoslLog("[OK]   Device opened\n");
    }

    if (g_InitialRows == 0 || g_InitialCols == 0)
    {
        TerminalGetSize(&DetectedRows, &DetectedCols);
        if (g_InitialRows == 0)
            g_InitialRows = DetectedRows;
        if (g_InitialCols == 0)
            g_InitialCols = DetectedCols;
    }
    if (g_InitialRows == 0)
        g_InitialRows = 24;
    if (g_InitialCols == 0)
        g_InitialCols = 80;

    RoslLog("[INFO] Connecting to service session %lu\n", g_AttachSessionId);
    ControlPipe = RoslClientConnectControl(g_AttachSessionId);
    if (ControlPipe == INVALID_HANDLE_VALUE)
        Die("Failed to connect to ROSL control plane");

    if (!RoslClientAllocatePty(ControlPipe,
                               g_AttachSessionId,
                               g_InitialRows,
                               g_InitialCols,
                               &g_PtyIndex,
                               &g_ReaderIndex,
                               &ControlFlags))
    {
        CloseHandle(ControlPipe);
        Die("Service refused interactive PTY allocation");
    }

    RoslLog("[OK]   Service allocated PTY %lu reader %lu (%ux%u)\n",
            g_PtyIndex, g_ReaderIndex, g_InitialCols, g_InitialRows);

    g_IoMode = RoslModePty;
    ConfigurePtyLowLatencyMode();
    PtyInteractiveLoop();

    RoslClientReleasePty(ControlPipe, g_AttachSessionId, g_PtyIndex, g_ReaderIndex);
    CloseHandle(ControlPipe);

    if (g_hDev != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hDev);
        g_hDev = INVALID_HANDLE_VALUE;
    }

    if (hMutex) CloseHandle(hMutex);
    RoslLog("[DONE] Detached.\n");
    return 0;
}
