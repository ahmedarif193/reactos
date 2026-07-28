/*
 * PROJECT:     ReactOS VMX Hypervisor Launcher (rosl.exe)
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Service/supervisor mode functions
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 */

#include "rosl_common.h"

/* ---- WSL probe command arrays ------------------------------------------- */

static const char *g_WslProbeCommandsBase[] = {
    "echo __ROSL_WSL2_PROBE_BEGIN__",
    "ip addr show",
    "ip route show",
    "cat /etc/os-release | head -5 || true",
    "ls /dev/vport* 2>&1 || true",
    "sudo bash -c 'setsid bash --login -i <>/dev/vport2p1 >&0 2>&1 &'",
    "echo VCON_SHELL_STARTED_ON_PORT1",
};

static const char *g_WslProbeCommandsNet[] = {
    "ping -c 4 -W 2 8.8.8.8",
    "ping -c 4 -W 2 google.com",
    "sudo -n apt-get update -qq -o APT::Color=0 -o Dpkg::Use-Pty=0 -o Acquire::Languages=none 2>&1 || true",
};

static const char *g_WslProbeCommandsTail[] = {
    "printf '\\137\\137ROSL\\137WSL2\\137PROBE\\137DONE\\137\\137\\n'"
};

#define ROSL_SELFTEST_BEGIN_MARKER "__ROSL_SELFTEST_BEGIN__"
#define ROSL_SELFTEST_READY_MARKER "__ROSL_SELFTEST_READY__"
#define ROSL_SELFTEST_SPLIT_MARKER "__ROSL_SELFTEST_SPLIT__"
#define ROSL_SELFTEST_DONE_MARKER  "__ROSL_SELFTEST_DONE__"
#define ROSL_SELFTEST_LINE_MAX     1024

/* Built at startup based on g_ProbeNetTests */
const char *g_WslProbeCommands[ROSL_WSL_PROBE_MAX_CMDS];
DWORD g_WslProbeCmdCount = 0;

void RoslBuildProbeCommandList(void)
{
    DWORD i;
    g_WslProbeCmdCount = 0;

    for (i = 0; i < (DWORD)(sizeof(g_WslProbeCommandsBase) / sizeof(g_WslProbeCommandsBase[0])); i++)
        g_WslProbeCommands[g_WslProbeCmdCount++] = g_WslProbeCommandsBase[i];

    if (g_ProbeNetTests)
    {
        for (i = 0; i < (DWORD)(sizeof(g_WslProbeCommandsNet) / sizeof(g_WslProbeCommandsNet[0])); i++)
            g_WslProbeCommands[g_WslProbeCmdCount++] = g_WslProbeCommandsNet[i];
    }

    for (i = 0; i < (DWORD)(sizeof(g_WslProbeCommandsTail) / sizeof(g_WslProbeCommandsTail[0])); i++)
        g_WslProbeCommands[g_WslProbeCmdCount++] = g_WslProbeCommandsTail[i];
}

static BOOL
RoslServicePrepareSelfTest(void)
{
    char PrepareCommand[256];
    int PrepareLength;

    PrepareLength = _snprintf(
        PrepareCommand,
        sizeof(PrepareCommand),
        "sudo -n systemctl stop "
        "landscape-client.service "
        "systemd-timesyncd.service "
        "apt-daily.timer "
        "apt-daily-upgrade.timer "
        "unattended-upgrades.service "
        "> /dev/null 2>&1 || true; sleep 1; "
        "printf '" ROSL_SELFTEST_READY_MARKER "\\n'\n");
    if (PrepareLength <= 0 || (DWORD)PrepareLength >= sizeof(PrepareCommand))
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }

    RoslLog("[INFO] Selftest: quiescing guest background services\n");
    return RoslConsoleSendInput(PrepareCommand, (DWORD)PrepareLength);
}

static BOOL
RoslServiceStartSelfTestRun(void)
{
    char TestCommand[512];
    int TestLength;

    TestLength = _snprintf(
        TestCommand,
        sizeof(TestCommand),
        "printf '" ROSL_SELFTEST_BEGIN_MARKER "\\n'; "
        "ping -c 2 -W 1 google.com 2>&1 || true; "
        "printf '" ROSL_SELFTEST_SPLIT_MARKER "\\n'; "
        "timeout 30 sudo -n apt-get update -o APT::Color=0 -o Dpkg::Use-Pty=0 -o Acquire::Languages=none 2>&1 || true; "
        "echo; printf '" ROSL_SELFTEST_DONE_MARKER "\\n'\n");
    if (TestLength <= 0 || (DWORD)TestLength >= sizeof(TestCommand))
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }

    RoslLog("[INFO] Selftest: running guest network checks\n");
    return RoslConsoleSendInput(TestCommand, (DWORD)TestLength);
}

static VOID
RoslSelfTestDbgPrintLine(
    _In_reads_(Length) const char *Line,
    _In_ DWORD Length)
{
    char Buffer[ROSL_SELFTEST_LINE_MAX + 1];

    if (Length == 0)
        return;

    if (Length > ROSL_SELFTEST_LINE_MAX)
        Length = ROSL_SELFTEST_LINE_MAX;

    memcpy(Buffer, Line, Length);
    Buffer[Length] = '\0';
    DbgPrint("rosl-selftest: %s\n", Buffer);
    RoslLog("[SELFTEST] %s\n", Buffer);
}

static VOID
RoslSelfTestFeedOutput(
    _Inout_ PBOOL CaptureActive,
    _Inout_ PBOOL Completed,
    _Inout_opt_ PBOOL ReadyToRun,
    _Inout_updates_(ROSL_SELFTEST_LINE_MAX + 1) char *LineBuffer,
    _Inout_ PDWORD LineLength,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ DWORD Length)
{
    DWORD i;

    if (Completed != NULL && *Completed)
        return;

    for (i = 0; i < Length; i++)
    {
        UCHAR Byte = Data[i];

        if (Byte == '\r')
            continue;

        if (Byte != '\n')
        {
            if (*LineLength < ROSL_SELFTEST_LINE_MAX)
            {
                LineBuffer[*LineLength] = (char)Byte;
                (*LineLength)++;
            }
            continue;
        }

        LineBuffer[*LineLength] = '\0';

        if (strcmp(LineBuffer, ROSL_SELFTEST_BEGIN_MARKER) == 0)
        {
            *CaptureActive = TRUE;
            RoslLog("[INFO] Selftest: ping google.com starting\n");
        }
        else if (strcmp(LineBuffer, ROSL_SELFTEST_READY_MARKER) == 0)
        {
            if (ReadyToRun != NULL)
                *ReadyToRun = TRUE;
            RoslLog("[INFO] Selftest: guest background services stopped\n");
        }
        else if (strcmp(LineBuffer, ROSL_SELFTEST_SPLIT_MARKER) == 0)
        {
            if (*CaptureActive)
                RoslLog("[INFO] Selftest: apt-get update starting\n");
        }
        else if (strcmp(LineBuffer, ROSL_SELFTEST_DONE_MARKER) == 0)
        {
            if (*CaptureActive)
                RoslLog("[OK]   Selftest completed\n");
            *CaptureActive = FALSE;
            if (Completed != NULL)
                *Completed = TRUE;
        }
        else if (*CaptureActive)
        {
            RoslSelfTestDbgPrintLine(LineBuffer, *LineLength);
        }

        *LineLength = 0;
        LineBuffer[0] = '\0';
    }
}

/* ---- Vcon shell launch -------------------------------------------------- */

BOOL
RoslServiceStartVconShell(
    _In_ ULONG PortIndex)
{
    char Command[512];
    int Length;

    /*
     * Launch a shell on /dev/vport2pN with a guest-side PTY.
     * `script -qc bash /dev/null` allocates a real pseudo-terminal inside
     * the guest, giving bash isatty()=true. This enables:
     *   - Ctrl+C (0x03) -> SIGINT via the guest tty layer
     *   - Ctrl+Z (0x1A) -> SIGTSTP
     *   - Ctrl+D (0x04) -> EOF
     *   - readline line editing, tab completion
     *   - proper job control
     * Without this, /dev/vportNpM is a raw char device (not a tty)
     * and bash runs in degraded mode with no signal handling.
     */
    Length = _snprintf(
        Command,
        sizeof(Command),
        "(sudo -n bash -c 'setsid script -qc \"bash --login\" /dev/null <>/dev/vport2p%lu >&0 2>&1 &' "
        "|| setsid script -qc 'bash --login' /dev/null <>/dev/vport2p%lu >&0 2>&1 &) "
        "&& echo __ROSL_VCON_PORT_%lu__\n",
        PortIndex,
        PortIndex,
        PortIndex);
    if (Length <= 0 || (DWORD)Length >= sizeof(Command))
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }

    RoslLog("[INFO] Starting guest shell (script+pty) on /dev/vport2p%lu via serial bootstrap\n",
            PortIndex);
    return RoslConsoleSendInput(Command, (DWORD)Length);
}

/* Vcon port allocation is now handled by the driver via PTY_CREATE.
 * The driver allocates both a PTY and a bound vcon port atomically.
 * PTY_DESTROY releases the vcon port. */

BOOL
RoslServiceWaitForSerialPrompt(void)
{
    if (g_ServiceSerialPromptSeen)
        return TRUE;

    if (g_ServicePromptEvent == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (WaitForSingleObject(g_ServicePromptEvent, ROSL_VCON_PROMPT_WAIT_MS) == WAIT_OBJECT_0)
        return TRUE;

    SetLastError(ERROR_TIMEOUT);
    return FALSE;
}

void
RoslServiceNoteSerialPrompt(void)
{
    if (g_ServiceSerialPromptSeen)
        return;

    g_ServiceSerialPromptSeen = TRUE;
    RoslLog("[INFO] Service supervisor: serial prompt detected\n");
    if (g_ServicePromptEvent != NULL)
    {
        SetEvent(g_ServicePromptEvent);
    }
}

static BOOL
RoslServiceTailHasPrompt(
    _In_opt_z_ const char *Tail)
{
    if (Tail == NULL || Tail[0] == '\0')
        return FALSE;

    return strstr(Tail, ":~$ ") != NULL ||
           strstr(Tail, ":/$ ") != NULL ||
           strstr(Tail, ":~# ") != NULL ||
           strstr(Tail, ":/# ") != NULL ||
           strstr(Tail, "wsluser@") != NULL ||
           strstr(Tail, "root@") != NULL;
}

static BOOL
RoslServiceTailHasAutologinReadyMarker(
    _In_opt_z_ const char *Tail)
{
    if (Tail == NULL || Tail[0] == '\0')
        return FALSE;

    return strstr(Tail, "serial-getty@ttyS0.service") != NULL ||
           strstr(Tail, "Reached target multi-user.target") != NULL ||
           strstr(Tail, "Reached target graphical.target") != NULL;
}

/* ---- WSL probe functions ------------------------------------------------ */

void
RoslWslProbeInit(
    _Out_ ROSL_WSL_PROBE_STATE *Probe,
    _In_ BOOL Enable,
    _In_opt_ ROSL_SEND_INPUT_ROUTINE SendInput,
    _In_opt_z_ const char *ChannelName)
{
    memset(Probe, 0, sizeof(*Probe));
    Probe->Enabled = Enable;
    Probe->LoopStartTick = GetTickCount();
    Probe->LastSendTick = Probe->LoopStartTick;
    Probe->SendInput = SendInput;
    Probe->ChannelName = (ChannelName != NULL) ? ChannelName : "control";
}

void RoslWslProbeAppendTail(
    _Inout_ ROSL_WSL_PROBE_STATE *Probe,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ DWORD Length)
{
    DWORD keep;

    if (Length == 0)
        return;

    if (Length >= (ROSL_WSL_PROBE_TAIL_MAX - 1))
    {
        keep = ROSL_WSL_PROBE_TAIL_MAX - 1;
        memcpy(Probe->Tail, Data + (Length - keep), keep);
        Probe->TailLength = keep;
        Probe->Tail[Probe->TailLength] = '\0';
        return;
    }

    if (Probe->TailLength + Length >= (ROSL_WSL_PROBE_TAIL_MAX - 1))
    {
        DWORD overflow = (Probe->TailLength + Length) - (ROSL_WSL_PROBE_TAIL_MAX - 1);
        memmove(Probe->Tail, Probe->Tail + overflow, Probe->TailLength - overflow);
        Probe->TailLength -= overflow;
    }

    memcpy(Probe->Tail + Probe->TailLength, Data, Length);
    Probe->TailLength += Length;
    Probe->Tail[Probe->TailLength] = '\0';
}

BOOL RoslWslProbePromptDetected(_In_ const ROSL_WSL_PROBE_STATE *Probe)
{
    return strstr(Probe->Tail, ":~$ ") != NULL ||
           strstr(Probe->Tail, ":/$ ") != NULL ||
           strstr(Probe->Tail, ":~# ") != NULL ||
           strstr(Probe->Tail, ":/# ") != NULL ||
           strstr(Probe->Tail, "wsluser@") != NULL;
}

BOOL RoslWslProbePromptAtTail(_In_ const ROSL_WSL_PROBE_STATE *Probe)
{
    static const char *const prompts[] = { ":~$ ", ":/$ ", ":~# ", ":/# " };
    size_t i;

    if (Probe->TailLength < 4)
        return FALSE;

    /*
     * Search the last 256 bytes of the tail for a prompt pattern.
     * The prompt may not be at the exact end due to trailing ANSI
     * escape sequences (e.g., bracket paste mode \x1b[?2004h) that
     * bash emits after the prompt.
     */
    for (i = 0; i < (sizeof(prompts) / sizeof(prompts[0])); i++)
    {
        const char *found;
        size_t searchStart = (Probe->TailLength > 256) ? (Probe->TailLength - 256) : 0;

        found = strstr(Probe->Tail + searchStart, prompts[i]);
        if (found != NULL)
        {
            return TRUE;
        }
    }

    return FALSE;
}

void RoslWslProbeFeedOutput(
    _Inout_ ROSL_WSL_PROBE_STATE *Probe,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ DWORD Length)
{
    if (!Probe->Enabled || Probe->Done || Length == 0)
        return;

    RoslWslProbeAppendTail(Probe, Data, Length);

    if (!Probe->PromptSeen && RoslWslProbePromptDetected(Probe))
    {
        Probe->PromptSeen = TRUE;
        RoslLog("[INFO] WSL2 probe: login/prompt detected\n");
    }

    /* Check for DONE marker BEFORE clearing tail on prompt detection,
     * otherwise the prompt-detected tail clear races with DONE detection. */
    if (strstr(Probe->Tail, "__ROSL_WSL2_PROBE_DONE__") != NULL)
    {
        Probe->Done = TRUE;
        RoslLog("[OK]   WSL2 probe: command batch completed\n");
        return;
    }

    if (Probe->WaitingForCommandPrompt)
    {
        if (RoslWslProbePromptAtTail(Probe))
        {
            RoslLog("[INFO] WSL2 probe: prompt detected in tail, proceeding\n");
            Probe->WaitingForCommandPrompt = FALSE;
            Probe->TailLength = 0;
            Probe->Tail[0] = '\0';
        }
    }
}

void RoslWslProbeTick(_Inout_ ROSL_WSL_PROBE_STATE *Probe)
{
    DWORD now;
    char line[256];
    int lineLen;

    if (!Probe->Enabled)
        return;

    now = GetTickCount();

    /*
     * After the probe batch has completed, only run the periodic heartbeat.
     * Skip the initial probe logic.
     */
    if (Probe->Done)
        goto heartbeat;

    if (!Probe->Started)
    {
        if (!Probe->PromptSeen &&
            (now - Probe->LoopStartTick) < ROSL_WSL_PROBE_PROMPT_TIMEOUT_MS)
        {
            return;
        }

        Probe->Started = TRUE;
        Probe->ProbeStartTick = now;
        Probe->LastSendTick = 0;
        Probe->NextCommand = 0;

        if (!Probe->PromptSeen)
        {
            RoslLog("[WARN] WSL2 probe: prompt not detected within %lu ms, sending commands anyway\n",
                    (unsigned long)ROSL_WSL_PROBE_PROMPT_TIMEOUT_MS);
        }

        RoslLog("[INFO] WSL2 probe: sending %lu commands via %s\n",
                (unsigned long)ROSL_WSL_PROBE_CMD_COUNT,
                Probe->ChannelName);
    }

    if (Probe->NextCommand < ROSL_WSL_PROBE_CMD_COUNT)
    {
        BOOL sent;
        BOOL waitForPrompt;

        if (Probe->WaitingForCommandPrompt)
        {
            /* Safety valve: if we've waited >5s for a prompt, force-proceed */
            if ((now - Probe->WaitStartTick) > 5000)
            {
                RoslLog("[WARN] WSL2 probe: prompt wait timeout after 5s, forcing next cmd\n");
                Probe->WaitingForCommandPrompt = FALSE;
            }
            else
            {
                return;
            }
        }

        if ((now - Probe->LastSendTick) < ROSL_WSL_PROBE_COMMAND_INTERVAL_MS)
            return;

        sent = FALSE;
        lineLen = _snprintf(line, sizeof(line), "%s\n", g_WslProbeCommands[Probe->NextCommand]);
        if (lineLen > 0 && (DWORD)lineLen < sizeof(line))
        {
            if (Probe->SendInput == NULL ||
                !Probe->SendInput(line, (DWORD)lineLen))
            {
                RoslLog("[WARN] WSL2 probe: failed to send cmd[%lu]\n",
                        (unsigned long)(Probe->NextCommand + 1));
            }
            else
            {
                sent = TRUE;
                RoslLog("[INFO] WSL2 probe cmd[%lu/%lu]: %s\n",
                        (unsigned long)(Probe->NextCommand + 1),
                        (unsigned long)ROSL_WSL_PROBE_CMD_COUNT,
                        g_WslProbeCommands[Probe->NextCommand]);
            }
        }
        else
        {
            RoslLog("[WARN] WSL2 probe: command[%lu] too long, skipping\n",
                    (unsigned long)(Probe->NextCommand + 1));
        }

        waitForPrompt = (sent &&
                         (Probe->NextCommand + 1) < ROSL_WSL_PROBE_CMD_COUNT);
        Probe->NextCommand++;
        Probe->LastSendTick = now;
        Probe->WaitingForCommandPrompt = waitForPrompt;
        if (waitForPrompt)
        {
            Probe->WaitStartTick = now;
            /*
             * Do NOT clear the tail here — the prompt from the command we just
             * sent may arrive in the same data chunk and already be in the tail
             * by the time FeedOutput runs next.  Clearing would lose it, causing
             * a deadlock where WaitingForCommandPrompt is never cleared.
             *
             * Also check if the tail already ends with a prompt (from the
             * previous command's output that FeedOutput saw before Tick ran).
             */
            if (RoslWslProbePromptAtTail(Probe))
            {
                RoslLog("[INFO] WSL2 probe: prompt already in tail, proceeding immediately\n");
                Probe->WaitingForCommandPrompt = FALSE;
            }
        }
        return;
    }

heartbeat:
    (void)0;
}

/* ---- Service supervisor loop -------------------------------------------- */

void ServiceSupervisorLoop(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    BYTE readBuf[CONSOLE_BUF_SIZE];
    ROSV_VM_STATE_INFO stateInfo;
    ROSV_VM_STATE lastState = (ROSV_VM_STATE)-1;
    ROSL_WSL_PROBE_STATE wslProbe;
    ROSV_VM_STATS vmStats;
    DWORD ret;
    DWORD stateCounter = 0;
    DWORD stateHeartbeatCounter = 0;
    DWORD lastStatsTick;
    BOOL bootCmdSent = FALSE;
    BOOL serialPromptNudgeWarned = FALSE;
    BOOL serialShellReady = FALSE;
    BOOL selfTestCommandSent = FALSE;
    BOOL selfTestReadyToRun = FALSE;
    BOOL selfTestTriggered = FALSE;
    BOOL selfTestCaptureActive = FALSE;
    BOOL selfTestCompleted = FALSE;
    char bootCmdTail[512];
    char selfTestLine[ROSL_SELFTEST_LINE_MAX + 1];
    DWORD bootCmdTailLen = 0;
    DWORD serialPromptNudgeCount = 0;
    DWORD selfTestLineLen = 0;
    DWORD selfTestCommandSentTick = 0;
    DWORD lastSerialPromptNudgeTick = 0;
    BOOL probeEnabled;
    ULONGLONG lastStatsExitCount = 0;
    ULONGLONG lastStatsTotalTicks = 0;
    ULONGLONG lastStatsHltTicks = 0;

    memset(bootCmdTail, 0, sizeof(bootCmdTail));
    memset(selfTestLine, 0, sizeof(selfTestLine));
    probeEnabled = (g_WslProbeMode == RoslWslProbeEnabled);
    lastStatsTick = GetTickCount();

    RoslBuildProbeCommandList();
    RoslWslProbeInit(&wslProbe, probeEnabled, RoslConsoleSendInput, "serial");
    RoslResetTerminalInputMode();
    ConsoleEnableVtOutput();

    RoslLog("[INFO] Service supervisor active on serial console\n");
    if (probeEnabled)
    {
        RoslLog("[INFO] WSL2 probe: enabled (serial)\n");
    }

    while (g_Running)
    {
        BOOL ioActivity = FALSE;
        DWORD nowTick;

        memset(readBuf, 0, sizeof(readBuf));
        if (Ioctl(ROSV_IOCTL_CONSOLE_READ, NULL, 0, readBuf, sizeof(readBuf) - 1, &ret))
        {
            if (ret > 0)
            {
                DWORD copyLen;

                ioActivity = TRUE;
                copyLen = RoslFeedTerminalOutput(readBuf, ret);
                readBuf[copyLen] = '\0';
                if (hStdout != INVALID_HANDLE_VALUE)
                {
                    DWORD written;
                    if (copyLen > 0)
                        WriteFile(hStdout, readBuf, copyLen, &written, NULL);
                }

                if (copyLen > 0)
                    RoslWslProbeFeedOutput(&wslProbe, readBuf, copyLen);

                if (copyLen > 0 && g_SelfTestMode)
                {
                    RoslSelfTestFeedOutput(&selfTestCaptureActive,
                                           &selfTestCompleted,
                                           &selfTestReadyToRun,
                                           selfTestLine,
                                           &selfTestLineLen,
                                           readBuf,
                                           copyLen);
                }

                /* Keep a sliding window of the last sizeof(bootCmdTail)-1 bytes
                 * of serial output so we can detect shell prompts. */
                {
                    const BYTE *src = readBuf;
                    DWORD srcLen = copyLen;
                    DWORD maxTail = sizeof(bootCmdTail) - 1;

                    if (srcLen >= maxTail)
                    {
                        /* Incoming chunk alone fills the window — just take the tail */
                        memcpy(bootCmdTail, src + srcLen - maxTail, maxTail);
                        bootCmdTailLen = maxTail;
                    }
                    else if (bootCmdTailLen + srcLen > maxTail)
                    {
                        /* Shift existing data to make room */
                        DWORD keep = maxTail - srcLen;
                        memmove(bootCmdTail, bootCmdTail + bootCmdTailLen - keep, keep);
                        memcpy(bootCmdTail + keep, src, srcLen);
                        bootCmdTailLen = maxTail;
                    }
                    else
                    {
                        memcpy(bootCmdTail + bootCmdTailLen, src, srcLen);
                        bootCmdTailLen += srcLen;
                    }
                    bootCmdTail[bootCmdTailLen] = '\0';
                }

                if (!serialShellReady &&
                    RoslServiceTailHasAutologinReadyMarker(bootCmdTail))
                {
                    serialShellReady = TRUE;
                    RoslLog("[INFO] Service supervisor: serial autologin shell is expected now\n");
                }

                if (!g_ServiceSerialPromptSeen &&
                    RoslServiceTailHasPrompt(bootCmdTail))
                {
                    RoslServiceNoteSerialPrompt();
                }

                if (g_BootCmd != NULL && !bootCmdSent && g_ServiceSerialPromptSeen)
                {
                    char cmdBuf[512];
                    int cmdLen = _snprintf(cmdBuf, sizeof(cmdBuf), "%s\n", g_BootCmd);
                    if (cmdLen > 0 && (DWORD)cmdLen < sizeof(cmdBuf))
                    {
                        RoslLog("[INFO] Boot command: injecting '%s' via serial\n", g_BootCmd);
                        RoslConsoleSendInput(cmdBuf, (DWORD)cmdLen);
                    }
                    bootCmdSent = TRUE;
                }

                if (g_SelfTestMode && !selfTestTriggered && g_ServiceSerialPromptSeen)
                {
                    if (!RoslServicePrepareSelfTest())
                    {
                        RoslLog("[FAIL] Selftest preparation failed (err=%lu)\n", GetLastError());
                    }
                    selfTestTriggered = TRUE;
                }

                if (g_SelfTestMode &&
                    selfTestTriggered &&
                    selfTestReadyToRun &&
                    !selfTestCommandSent)
                {
                    if (!RoslServiceStartSelfTestRun())
                    {
                        RoslLog("[FAIL] Selftest injection failed (err=%lu)\n", GetLastError());
                    }
                    selfTestCommandSent = TRUE;
                    selfTestCommandSentTick = GetTickCount();
                }
            }
        }

        RoslWslProbeTick(&wslProbe);

        /* Selftest exit: success or 180s timeout */
        if (g_SelfTestMode)
        {
            if (selfTestCompleted)
            {
                RoslLog("[OK]   Selftest: all markers received, exiting\n");
                g_Running = FALSE;
                break;
            }
            if (selfTestCommandSent &&
                (GetTickCount() - selfTestCommandSentTick) >= 300000)
            {
                RoslLog("[FAIL] Selftest: timed out after 300 seconds\n");
                g_Running = FALSE;
                break;
            }
        }

        nowTick = GetTickCount();
        if (!g_ServiceSerialPromptSeen &&
            serialShellReady &&
            serialPromptNudgeCount < 8 &&
            (serialPromptNudgeCount == 0 ||
             (nowTick - lastSerialPromptNudgeTick) >= 1000))
        {
            if (RoslConsoleSendInput("\n", 1))
            {
                serialPromptNudgeCount++;
                lastSerialPromptNudgeTick = nowTick;
                RoslLog("[INFO] Service supervisor: nudging ttyS0 autologin shell (%lu/8)\n",
                        (unsigned long)serialPromptNudgeCount);
            }
            else
            {
                serialPromptNudgeCount++;
                lastSerialPromptNudgeTick = nowTick;
                RoslLog("[WARN] Service supervisor: ttyS0 nudge failed (err=%lu)\n",
                        GetLastError());
            }
        }
        else if (!g_ServiceSerialPromptSeen &&
                 serialShellReady &&
                 serialPromptNudgeCount >= 8 &&
                 !serialPromptNudgeWarned)
        {
            serialPromptNudgeWarned = TRUE;
            RoslLog("[WARN] Service supervisor: no serial shell prompt after 8 nudges\n");
        }

        if ((nowTick - lastStatsTick) >= 1000)
        {
            lastStatsTick = nowTick;
            if (RoslQueryVmStats(&vmStats))
            {
                RoslLogVmCpuUsage("service",
                                  &vmStats,
                                  &lastStatsExitCount,
                                  &lastStatsTotalTicks,
                                  &lastStatsHltTicks);
            }
        }

        stateCounter++;
        if (stateCounter >= 10)
        {
            stateCounter = 0;
            memset(&stateInfo, 0, sizeof(stateInfo));
            if (Ioctl(ROSV_IOCTL_GET_STATE, NULL, 0, &stateInfo, sizeof(stateInfo), &ret))
            {
                if (stateInfo.State != lastState)
                {
                    RoslLog("[STATE] %s | exits=%llu last_exit=0x%lX checkpoint=%s\n",
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
                    RoslLog("[INFO] Service supervisor exiting because VM state=%s\n",
                            StateName(stateInfo.State));
                    break;
                }
            }
        }

        if (!ioActivity)
            Sleep(POLL_INTERVAL_MS);
        else
            Sleep(0);
    }

    ConsoleRestoreMode();
}

/* ---- VM creation with retry --------------------------------------------- */

BOOL
CreateAndConfigureVm(
    _In_ ULONG ramMb,
    _In_ ULONG minRamMb,
    _Out_ ROSV_VM_CREATE_RESULT *createRes,
    _Out_ ULONG *actualRamMb,
    _Out_opt_ DWORD *lastErrorOut)
{
    ROSV_VM_CONFIG cfg;
    DWORD ret;
    ROSV_VM_CREATE_RESULT localCreateRes;
    DWORD err;
    ULONG tryMb = ramMb;
    BOOL vmCreated = FALSE;

    if (minRamMb < RAM_MIN_FLOOR_MB)
        minRamMb = RAM_MIN_FLOOR_MB;

    /* Create VM once with initial (max) RAM config */
    cfg.RamSizeMB = tryMb;
    cfg.NetBackendType = g_NetBackendType;
    RoslLog("[INFO] ROSV_IOCTL_CREATE_VM (RAM=%lu MB, net=%s)\n", tryMb,
            g_NetBackendType == 2 ? "netio" : g_NetBackendType == 1 ? "netd" : "none");
    memset(&localCreateRes, 0, sizeof(localCreateRes));
    if (!Ioctl(ROSV_IOCTL_CREATE_VM,
               &cfg, sizeof(cfg),
               &localCreateRes, sizeof(localCreateRes),
               &ret))
    {
        err = GetLastError();
        RoslLog("[FAIL] IOCTL_CREATE_VM failed (err=%lu)\n", err);
        if (lastErrorOut) *lastErrorOut = err;
        return FALSE;
    }
    vmCreated = TRUE;
    RoslLog("[OK]   VM created: VmId=%lu Status=0x%08lX\n",
            localCreateRes.VmId, (ULONG)localCreateRes.Status);

    /* Try SET_MEMORY, retrying with less RAM on failure */
    while (tryMb >= minRamMb)
    {
        cfg.RamSizeMB = tryMb;
        RoslLog("[INFO] ROSV_IOCTL_SET_MEMORY (RAM=%lu MB)\n", tryMb);
        if (Ioctl(ROSV_IOCTL_SET_MEMORY, &cfg, sizeof(cfg), NULL, 0, &ret))
        {
            RoslLog("[OK]   Memory configured: %lu MB\n", tryMb);
            if (createRes != NULL)
                *createRes = localCreateRes;
            if (actualRamMb != NULL)
                *actualRamMb = tryMb;
            if (lastErrorOut) *lastErrorOut = ERROR_SUCCESS;
            return TRUE;
        }

        err = GetLastError();
        RoslLog("[WARN] IOCTL_SET_MEMORY failed for %lu MB (err=%lu)\n", tryMb, err);

        /* Reduce and retry */
        if (tryMb <= minRamMb)
            break;

        tryMb -= RAM_RETRY_STEP_MB;
        if (tryMb < minRamMb)
            tryMb = minRamMb;

        RoslLog("[INFO] Retrying with %lu MB (min=%lu MB)\n", tryMb, minRamMb);
    }

    /* All retries exhausted */
    err = GetLastError();
    RoslLog("[FAIL] Cannot allocate VM memory (tried %lu-%lu MB)\n", ramMb, tryMb);
    if (lastErrorOut) *lastErrorOut = err;

    /* Destroy the VM we created */
    if (vmCreated)
    {
        RoslLog("[INFO] Destroying VM after memory allocation failure\n");
        Ioctl(ROSV_IOCTL_DESTROY_VM, NULL, 0, NULL, 0, &ret);
    }

    SetLastError(err);
    return FALSE;
}

/* ---- Control plane ------------------------------------------------------ */

DWORD WINAPI
RoslControlClientThread(
    _In_ LPVOID Parameter)
{
    HANDLE Pipe = (HANDLE)Parameter;
    ROSL_CONTROL_REQUEST Request;
    ROSL_CONTROL_RESPONSE Response;
    ULONG AllocatedPtyIndex = (ULONG)-1;
    ULONG AllocatedVconPort = (ULONG)-1;
    BOOL ClientAttached = FALSE;

    memset(&Response, 0, sizeof(Response));
    Response.Version = ROSL_CONTROL_VERSION;
    Response.SessionId = g_ServiceSessionId;

    if (!RoslPipeReadExact(Pipe, &Request, sizeof(Request)))
        goto cleanup;

    if (Request.Version != ROSL_CONTROL_VERSION ||
        Request.SessionId != g_ServiceSessionId ||
        Request.Op != RoslCtlOpAllocateInteractive)
    {
        RoslLog("[FAIL] Control client: bad request (ver=%lu sess=%lu op=%lu)\n",
                Request.Version, Request.SessionId, Request.Op);
        Response.Status = ERROR_INVALID_PARAMETER;
        RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
        goto cleanup;
    }

    /* Create a PTY via IOCTL — driver allocates PTY + binds a vcon port */
    {
        ROSV_PTY_CREATE_REQUEST PtyReq;
        ROSV_PTY_CREATE_RESULT PtyRes;
        DWORD Ret;

        memset(&PtyReq, 0, sizeof(PtyReq));
        PtyReq.InitialRows = Request.Rows;
        PtyReq.InitialCols = Request.Cols;
        PtyReq.Flags = 0;

        memset(&PtyRes, 0, sizeof(PtyRes));

        if (!Ioctl(ROSV_IOCTL_PTY_CREATE, &PtyReq, sizeof(PtyReq),
                   &PtyRes, sizeof(PtyRes), &Ret) ||
            PtyRes.Status != 0)
        {
            RoslLog("[FAIL] Control client: PTY_CREATE failed (Status=0x%08lX)\n",
                    (ULONG)PtyRes.Status);
            Response.Status = ERROR_GEN_FAILURE;
            RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
            goto cleanup;
        }

        AllocatedPtyIndex = PtyRes.PtyIndex;
        AllocatedVconPort = PtyRes.VconPort;
        ClientAttached = TRUE;

        RoslLog("[OK]   PTY_CREATE: pty=%lu reader=%lu vcon_port=%lu\n",
                PtyRes.PtyIndex, PtyRes.ReaderIndex, PtyRes.VconPort);
    }

    /* Wait for guest serial prompt before launching shell */
    if (!RoslServiceWaitForSerialPrompt())
    {
        RoslLog("[FAIL] Control client: timed out waiting for serial prompt\n");
        Response.Status = GetLastError();
        RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
        goto cleanup;
    }

    /* Start a guest shell on the vcon port bound to this PTY */
    EnterCriticalSection(&g_ControlStateLock);
    if (!RoslServiceStartVconShell(AllocatedVconPort))
    {
        RoslLog("[FAIL] Control client: failed to start shell on vcon port %lu\n",
                AllocatedVconPort);
        Response.Status = GetLastError();
        if (Response.Status == ERROR_SUCCESS)
            Response.Status = ERROR_GEN_FAILURE;
        LeaveCriticalSection(&g_ControlStateLock);
        RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
        goto cleanup;
    }
    LeaveCriticalSection(&g_ControlStateLock);

    /* Return PTY credentials to client (NOT vcon — client uses PTY IOCTLs) */
    Response.Status = ERROR_SUCCESS;
    Response.PtyIndex = AllocatedPtyIndex;
    Response.ReaderIndex = 0;
    Response.Flags = 0;  /* NO ROSL_CONTROL_FLAG_USE_VCON — client uses PTY path */
    if (!RoslPipeWriteExact(Pipe, &Response, sizeof(Response)))
        goto cleanup;

    RoslLog("[INFO] Control plane: client attached (pty=%lu vcon=%lu session=%lu)\n",
            AllocatedPtyIndex, AllocatedVconPort, g_ServiceSessionId);

    /* Hold connection open until client releases or disconnects */
    while (g_Running && RoslPipeReadExact(Pipe, &Request, sizeof(Request)))
    {
        memset(&Response, 0, sizeof(Response));
        Response.Version = ROSL_CONTROL_VERSION;
        Response.SessionId = g_ServiceSessionId;
        Response.PtyIndex = AllocatedPtyIndex;
        Response.Flags = 0;

        if (Request.Version != ROSL_CONTROL_VERSION ||
            Request.SessionId != g_ServiceSessionId ||
            Request.PtyIndex != AllocatedPtyIndex)
        {
            Response.Status = ERROR_INVALID_PARAMETER;
            RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
            continue;
        }

        if (Request.Op == RoslCtlOpReleaseInteractive)
        {
            Response.Status = ERROR_SUCCESS;
            RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
            goto cleanup;
        }

        Response.Status = ERROR_INVALID_FUNCTION;
        RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
    }

cleanup:
    /* Destroy the PTY (driver releases the bound vcon port internally) */
    if (ClientAttached && AllocatedPtyIndex != (ULONG)-1)
    {
        DWORD Ret;
        RoslLog("[INFO] Control plane: destroying PTY %lu (vcon=%lu)\n",
                AllocatedPtyIndex, AllocatedVconPort);
        Ioctl(ROSV_IOCTL_PTY_DESTROY, &AllocatedPtyIndex,
              sizeof(AllocatedPtyIndex), NULL, 0, &Ret);
    }

    FlushFileBuffers(Pipe);
    DisconnectNamedPipe(Pipe);
    CloseHandle(Pipe);
    return 0;
}

DWORD WINAPI
RoslControlListenerThread(
    _In_ LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);

    while (WaitForSingleObject(g_ControlStopEvent, 0) == WAIT_TIMEOUT)
    {
        HANDLE Pipe;
        HANDLE ThreadHandle;
        BOOL Connected;
        DWORD Error;

        Pipe = CreateNamedPipeA(g_ControlPipeName,
                                PIPE_ACCESS_DUPLEX,
                                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                PIPE_UNLIMITED_INSTANCES,
                                sizeof(ROSL_CONTROL_RESPONSE),
                                sizeof(ROSL_CONTROL_REQUEST),
                                1000,
                                NULL);
        if (Pipe == INVALID_HANDLE_VALUE)
        {
            RoslLog("[FAIL] CreateNamedPipe failed (err=%lu)\n", GetLastError());
            return 1;
        }

        Connected = ConnectNamedPipe(Pipe, NULL);
        if (!Connected)
        {
            Error = GetLastError();
            if (Error != ERROR_PIPE_CONNECTED)
            {
                CloseHandle(Pipe);
                if (WaitForSingleObject(g_ControlStopEvent, 0) == WAIT_OBJECT_0)
                    break;
                continue;
            }
        }

        ThreadHandle = CreateThread(NULL,
                                    0,
                                    RoslControlClientThread,
                                    Pipe,
                                    0,
                                    NULL);
        if (ThreadHandle == NULL)
        {
            RoslLog("[WARN] CreateThread(control client) failed (err=%lu)\n", GetLastError());
            CloseHandle(Pipe);
            continue;
        }

        CloseHandle(ThreadHandle);
    }

    return 0;
}

void
RoslWakeControlListener(void)
{
    HANDLE Pipe;

    if (g_ControlPipeName[0] == '\0')
        return;

    Pipe = CreateFileA(g_ControlPipeName,
                       GENERIC_READ | GENERIC_WRITE,
                       0,
                       NULL,
                       OPEN_EXISTING,
                       0,
                       NULL);
    if (Pipe != INVALID_HANDLE_VALUE)
        CloseHandle(Pipe);
}

BOOL
RoslStartControlServer(
    _In_ ULONG SessionId)
{
    RoslInitControlState();
    g_ServiceSessionId = SessionId;

    g_ServiceSerialPromptSeen = FALSE;
    RoslBuildControlPipeName(SessionId, g_ControlPipeName, sizeof(g_ControlPipeName));

    g_ControlStopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (g_ControlStopEvent == NULL)
    {
        RoslLog("[FAIL] CreateEvent(control stop) failed (err=%lu)\n", GetLastError());
        return FALSE;
    }

    g_ControlListenerThread = CreateThread(NULL,
                                           0,
                                           RoslControlListenerThread,
                                           NULL,
                                           0,
                                           NULL);
    if (g_ControlListenerThread == NULL)
    {
        RoslLog("[FAIL] CreateThread(control listener) failed (err=%lu)\n", GetLastError());
        CloseHandle(g_ControlStopEvent);
        g_ControlStopEvent = NULL;
        return FALSE;
    }

    g_ServicePromptEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (g_ServicePromptEvent == NULL)
    {
        RoslLog("[FAIL] CreateEvent(service prompt) failed (err=%lu)\n", GetLastError());
        CloseHandle(g_ControlListenerThread);
        g_ControlListenerThread = NULL;
        CloseHandle(g_ControlStopEvent);
        g_ControlStopEvent = NULL;
        return FALSE;
    }

    if (!RoslPublishServiceDiscovery(SessionId))
    {
        RoslLog("[FAIL] Failed to publish service discovery (err=%lu)\n", GetLastError());
        CloseHandle(g_ControlListenerThread);
        g_ControlListenerThread = NULL;
        CloseHandle(g_ServicePromptEvent);
        g_ServicePromptEvent = NULL;
        CloseHandle(g_ControlStopEvent);
        g_ControlStopEvent = NULL;
        return FALSE;
    }

    RoslLog("[INFO] Control plane listening on %s\n", g_ControlPipeName);
    return TRUE;
}

void
RoslStopControlServer(void)
{
    if (g_ControlStopEvent != NULL)
        SetEvent(g_ControlStopEvent);

    RoslWakeControlListener();

    if (g_ControlListenerThread != NULL)
    {
        WaitForSingleObject(g_ControlListenerThread, 5000);
        CloseHandle(g_ControlListenerThread);
        g_ControlListenerThread = NULL;
    }

    if (g_ControlStopEvent != NULL)
    {
        CloseHandle(g_ControlStopEvent);
        g_ControlStopEvent = NULL;
    }

    if (g_ServicePromptEvent != NULL)
    {
        CloseHandle(g_ServicePromptEvent);
        g_ServicePromptEvent = NULL;
    }


    g_ServiceSerialPromptSeen = FALSE;

    RoslUnpublishServiceDiscovery();
    g_ControlPipeName[0] = '\0';
}

/* ---- Service discovery -------------------------------------------------- */

BOOL
RoslPublishServiceDiscovery(
    _In_ ULONG SessionId)
{
    g_DiscoveryMapping = CreateFileMappingA(INVALID_HANDLE_VALUE,
                                            NULL,
                                            PAGE_READWRITE,
                                            0,
                                            sizeof(ROSL_DISCOVERY_SHARED),
                                            ROSL_DISCOVERY_MAPPING_NAME);
    if (g_DiscoveryMapping == NULL)
        return FALSE;

    g_DiscoveryView = (ROSL_DISCOVERY_SHARED *)MapViewOfFile(g_DiscoveryMapping,
                                                             FILE_MAP_ALL_ACCESS,
                                                             0,
                                                             0,
                                                             sizeof(ROSL_DISCOVERY_SHARED));
    if (g_DiscoveryView == NULL)
    {
        CloseHandle(g_DiscoveryMapping);
        g_DiscoveryMapping = NULL;
        return FALSE;
    }

    memset(g_DiscoveryView, 0, sizeof(*g_DiscoveryView));
    g_DiscoveryView->Version = ROSL_CONTROL_VERSION;
    g_DiscoveryView->SessionId = SessionId;
    g_DiscoveryView->ServicePid = GetCurrentProcessId();
    g_DiscoveryView->Flags = 1;
    FlushViewOfFile(g_DiscoveryView, sizeof(*g_DiscoveryView));
    return TRUE;
}

void
RoslUnpublishServiceDiscovery(void)
{
    if (g_DiscoveryView != NULL)
    {
        memset(g_DiscoveryView, 0, sizeof(*g_DiscoveryView));
        FlushViewOfFile(g_DiscoveryView, sizeof(*g_DiscoveryView));
        UnmapViewOfFile(g_DiscoveryView);
        g_DiscoveryView = NULL;
    }

    if (g_DiscoveryMapping != NULL)
    {
        CloseHandle(g_DiscoveryMapping);
        g_DiscoveryMapping = NULL;
    }
}
