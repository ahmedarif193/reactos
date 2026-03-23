/*
 * PROJECT:     ReactOS VMX Hypervisor Launcher (rosl.exe)
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Entry point, argument parsing, and VM bootstrap
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 */

#include "rosl_common.h"

/* ---- Usage -------------------------------------------------------------- */

void PrintUsage(void)
{
    RoslLog("Usage: rosl.exe [options] [bzImage] [initrd] [ram_mb] [cmdline]\n");
    RoslLog("       rosl.exe --disk <image> [options] [bzImage] [ram_mb] [cmdline]\n");
    RoslLog("       rosl.exe --attach <session-id>\n");
    RoslLog("\nOptions:\n");
    RoslLog("  --service         Start the ROSL supervisor service (default)\n");
    RoslLog("  --attach <id>     Connect as an interactive client to service session <id>\n");
    RoslLog("  --pty             Client terminal uses PTY data plane (default for --attach)\n");
    RoslLog("  --uart            Legacy direct UART mode (service supervision only)\n");
    RoslLog("  --disk <path>     Use disk image as virtio-blk root (raw .img or .vhdx)\n");
    RoslLog("  --rows <N>        Initial terminal rows (default: auto-detect)\n");
    RoslLog("  --cols <N>        Initial terminal columns (default: auto-detect)\n");
    RoslLog("  --probe-wsl2      Force-enable ROSL PTY command probe batch\n");
    RoslLog("  --no-probe-wsl2   Force-disable ROSL PTY command probe batch\n");
    RoslLog("  --net <mode>      Network backend: netio (default), netd, none\n");
    RoslLog("\nWith no arguments, uses bundled images from <exedir>\\rosv\\.\n");
    RoslLog("If a supervisor is already running, a plain `rosl.exe` auto-attaches as a client.\n");
    RoslLog("With --disk, initrd is optional. RAM defaults to %u MB (vs %u MB for initramfs).\n",
            DEFAULT_DISK_RAM_MB, DEFAULT_RAM_MB);
    RoslLog("Service mode owns serial supervision; later rosl runs auto-attach and get fresh PTYs.\n");
}

/* ---- Argument parsing --------------------------------------------------- */

/*
 * Parse --options from argv, stripping them from the positional argument list.
 * Returns the number of remaining positional arguments in posArgs[].
 */
int ParseOptions(int argc, char *argv[], char *posArgs[], int maxPos)
{
    int i, posCount = 0;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--pty") == 0)
        {
            g_IoMode = RoslModePty;
        }
        else if (strcmp(argv[i], "--uart") == 0)
        {
            g_IoMode = RoslModeUart;
        }
        else if (strcmp(argv[i], "--attach") == 0)
        {
            if (i + 1 >= argc)
            {
                RoslLog("[FAIL] --attach requires a session ID\n");
                return -1;
            }
            g_AttachSessionId = (ULONG)atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--service") == 0)
        {
            g_ForceService = TRUE;
        }
        else if (strcmp(argv[i], "--rows") == 0)
        {
            if (i + 1 >= argc)
            {
                RoslLog("[FAIL] --rows requires a value\n");
                return -1;
            }
            g_InitialRows = (USHORT)atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--cols") == 0)
        {
            if (i + 1 >= argc)
            {
                RoslLog("[FAIL] --cols requires a value\n");
                return -1;
            }
            g_InitialCols = (USHORT)atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--disk") == 0)
        {
            if (i + 1 >= argc)
            {
                RoslLog("[FAIL] --disk requires a file path\n");
                return -1;
            }
            g_DiskImagePath = argv[++i];
        }
        else if (strcmp(argv[i], "--cmd") == 0)
        {
            if (i + 1 >= argc)
            {
                RoslLog("[FAIL] --cmd requires a command string\n");
                return -1;
            }
            g_BootCmd = argv[++i];
        }
        else if (strcmp(argv[i], "--probe-wsl2") == 0)
        {
            g_WslProbeMode = RoslWslProbeEnabled;
        }
        else if (strcmp(argv[i], "--no-probe-wsl2") == 0)
        {
            g_WslProbeMode = RoslWslProbeDisabled;
        }
        else if (strcmp(argv[i], "--net") == 0)
        {
            if (i + 1 >= argc)
            {
                RoslLog("[FAIL] --net requires: netio|netd|none\n");
                return -1;
            }
            i++;
            if (strcmp(argv[i], "netio") == 0)
                g_NetBackendType = 2;
            else if (strcmp(argv[i], "netd") == 0)
                g_NetBackendType = 1;
            else if (strcmp(argv[i], "none") == 0)
                g_NetBackendType = 0;
            else
            {
                RoslLog("[FAIL] --net: unknown backend '%s' (use netio|netd|none)\n", argv[i]);
                return -1;
            }
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            PrintUsage();
            return -1;
        }
        else if (argv[i][0] == '-' && argv[i][1] == '-')
        {
            RoslLog("[FAIL] Unknown option: %s\n", argv[i]);
            return -1;
        }
        else
        {
            /* Positional argument */
            if (posCount < maxPos)
                posArgs[posCount] = argv[i];
            posCount++;
        }
    }

    return posCount;
}

/*
 * Resolve kernel path for disk-backed boots.
 *
 * Preferred order:
 *   1) Bundled kernel path (e.g. X:\reactos\system32\rosv\vmlinuz)
 *   2) Installed alongside VHDX: <disk_drive>:\reactos\system32\rosv\vmlinuz
 *      (e.g. C:\reactos\system32\rosv\vmlinuz when C:\...\ubuntu24.vhdx found)
 *   3) Root-level fallback: <disk_drive>:\vmlinuz
 */
const char *ResolveDiskBootKernelPath(const char *defaultKernelPath,
                                             const char *diskImagePath,
                                             char *fallbackBuf,
                                             DWORD fallbackBufSize)
{
    if (defaultKernelPath != NULL &&
        GetFileAttributesA(defaultKernelPath) != INVALID_FILE_ATTRIBUTES)
    {
        return defaultKernelPath;
    }

    if (fallbackBuf != NULL && fallbackBufSize > 0 &&
        diskImagePath != NULL &&
        diskImagePath[0] >= 'A' && diskImagePath[0] <= 'Z' &&
        diskImagePath[1] == ':')
    {
        /* Probe 2: standard ReactOS installation path on the data drive */
        _snprintf(fallbackBuf, fallbackBufSize,
                  "%c:\\reactos\\system32\\rosv\\vmlinuz", diskImagePath[0]);
        fallbackBuf[fallbackBufSize - 1] = '\0';
        if (GetFileAttributesA(fallbackBuf) != INVALID_FILE_ATTRIBUTES)
            return fallbackBuf;

        /* Probe 3: root-level vmlinuz on the data drive */
        _snprintf(fallbackBuf, fallbackBufSize, "%c:\\vmlinuz", diskImagePath[0]);
        fallbackBuf[fallbackBufSize - 1] = '\0';
        if (GetFileAttributesA(fallbackBuf) != INVALID_FILE_ATTRIBUTES)
            return fallbackBuf;
    }

    return defaultKernelPath;
}

/* ---- Main --------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    ROSV_VM_CREATE_RESULT tmpCreateRes;
    DWORD ret;
    BYTE *kernelBuf = NULL;
    DWORD kernelSz = 0, initrdSz = 0;
    ULONG ramMB;
    ULONG minRamMB;
    ULONG actualRamMB = 0;
    DWORD vmConfigErr = ERROR_SUCCESS;
    const char *cmdline;
    const char *kernelPath;
    const char *initrdPath;
    char exeDir[MAX_PATH];
    char defaultKernel[MAX_PATH];
    char defaultInitrd[MAX_PATH];
    HANDLE hMutex;
    char *posArgs[8];
    int posCount;
    ROSL_INITRD_INFO initrdInfo;

    RoslLog("=== rosl - ReactOS VMX Hypervisor Launcher ===\n\n");

    if (0)
    {
        VconInteractiveLoop();
        UartInteractiveLoop();
    }

    /* Multiple rosl instances can connect to the same running VM.
     * No single-instance enforcement needed. */
    hMutex = NULL;

    /* ---- Parse options -------------------------------------------------- */
    memset(posArgs, 0, sizeof(posArgs));
    posCount = ParseOptions(argc, argv, posArgs, 8);
    if (posCount < 0)
    {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    /* Reserved command names fail closed until implemented. */
    if (posCount >= 1 &&
        (strcmp(posArgs[0], "download") == 0 ||
         strcmp(posArgs[0], "manage") == 0))
    {
        RoslLog("[FAIL] Subcommand '%s' is unavailable in this build.\n", posArgs[0]);
        PrintUsage();
        if (hMutex) CloseHandle(hMutex);
        return 2;
    }

    /* ---- Handle --attach mode ------------------------------------------- */
    if (g_AttachSessionId != (ULONG)-1)
        return RoslRunInteractiveClient(hMutex, FALSE);

    /*
     * Auto-client fast path: if a supervisor is already running, become a
     * client before doing any image/path resolution work.
     */
    if (!g_ForceService)
    {
        ROSV_VM_STATE_INFO ExistingState;
        ULONG AutoSessionId = 0;

        if (!EnsureDriverLoaded())
            Die("Failed to load rosv.sys driver");

        g_hDev = OpenRosvDeviceWithRetry(50, 100);
        if (g_hDev == INVALID_HANDLE_VALUE)
            Die("Cannot open \\\\.\\RosvHypervisor - is rosv.sys loaded?");

        memset(&ExistingState, 0, sizeof(ExistingState));
        if (Ioctl(ROSV_IOCTL_GET_STATE, NULL, 0, &ExistingState, sizeof(ExistingState), &ret) &&
            ExistingState.State == RosvVmStateRunning)
        {
            DWORD WaitDeadline = GetTickCount() + 5000;

            do
            {
                if (RoslDiscoverServiceSession(&AutoSessionId))
                    break;

                Sleep(100);
            } while (GetTickCount() < WaitDeadline);

            if (AutoSessionId != 0)
            {
                g_AttachSessionId = AutoSessionId;
                RoslLog("[INFO] Detected active supervisor session %lu; switching to client mode\n",
                        g_AttachSessionId);
                return RoslRunInteractiveClient(hMutex, TRUE);
            }
        }

        CloseHandle(g_hDev);
        g_hDev = INVALID_HANDLE_VALUE;
    }

    /* ---- Resolve kernel/initrd paths ------------------------------------ */
    if (g_DiskImagePath != NULL)
    {
        /* Disk mode: initrd is optional.
         * posArgs[0] = bzImage (optional), posArgs[1] = initrd (optional) */
        static char diskKernelFallback[MAX_PATH];

        if (posCount >= 1)
        {
            kernelPath = posArgs[0];
        }
        else
        {
            if (!GetExeDir(exeDir, sizeof(exeDir)))
            {
                if (hMutex) CloseHandle(hMutex);
                return 1;
            }
            _snprintf(defaultKernel, sizeof(defaultKernel), "%srosv\\vmlinuz", exeDir);
            defaultKernel[sizeof(defaultKernel) - 1] = '\0';
            kernelPath = defaultKernel;
        }

        if (posCount < 1)
        {
            const char *resolvedKernel = ResolveDiskBootKernelPath(kernelPath,
                                                                    g_DiskImagePath,
                                                                    diskKernelFallback,
                                                                    sizeof(diskKernelFallback));
            if (resolvedKernel != kernelPath)
            {
                RoslLog("[INFO] Disk mode: bundled kernel missing, using %s\n",
                        resolvedKernel);
            }
            kernelPath = resolvedKernel;
        }

        /* In disk mode, initrd is optional (for small boot initrd) */
        initrdPath = (posCount >= 2) ? posArgs[1] : NULL;

        RoslLog("[INFO] Disk mode: image=%s\n", g_DiskImagePath);
        RoslLog("[INFO]   Kernel: %s\n", kernelPath);
        if (initrdPath)
            RoslLog("[INFO]   Initrd: %s\n", initrdPath);
        else
            RoslLog("[INFO]   Initrd: none (disk-backed root)\n");
    }
    else if (posCount >= 2)
    {
        /* Explicit paths provided */
        kernelPath = posArgs[0];
        initrdPath = posArgs[1];
    }
    else if (posCount == 0)
    {
        /* No args: look for bundled images in <exedir>\rosv\ */
        if (!GetExeDir(exeDir, sizeof(exeDir)))
        {
            if (hMutex) CloseHandle(hMutex);
            return 1;
        }

        _snprintf(defaultKernel, sizeof(defaultKernel), "%srosv\\vmlinuz", exeDir);
        defaultKernel[sizeof(defaultKernel) - 1] = '\0';

        /* Auto-detect VHDX:
         *  1) Prefer non-boot, non-CD volumes (wait briefly for late mounts).
         *  2) Fall back to bundled image next to the kernel.
         *  3) Last resort: scan all volumes, including CD media.
         *
         * Keep fixed/removable media ahead of optical media to reduce boot-time
         * variability during large random-read phases. */
        {
            static char defaultDisk[MAX_PATH];
            static char scannedDisk[MAX_PATH];
            static const char *Suffixes[] = {
                "\\ubuntu24.vhdx",
                "\\reactos\\system32\\rosv\\ubuntu24.vhdx"
            };
            char fallbackDrive = 0;
            DWORD startTick = GetTickCount();
            const DWORD waitMs = 15000;
            int loggedWait = 0;
            size_t si;
            char drive;

            _snprintf(defaultDisk, sizeof(defaultDisk), "%srosv\\ubuntu24.vhdx", exeDir);
            defaultDisk[sizeof(defaultDisk) - 1] = '\0';

            /* Bundled VHDX next to the kernel (e.g. X:\reactos\system32\rosv\).
             * If present, use it immediately — no need to poll data drives. */
            if (GetFileAttributesA(defaultDisk) != INVALID_FILE_ATTRIBUTES)
            {
                g_DiskImagePath = defaultDisk;
                RoslLog("[INFO] Found bundled VHDX: %s\n", defaultDisk);
            }

            /* Pass 1: poll non-boot non-CD volumes briefly for late mounts. */
            while (g_DiskImagePath == NULL &&
                   (GetTickCount() - startTick) < waitMs)
            {
                DWORD logicalDrives = GetLogicalDrives();

                for (si = 0; si < sizeof(Suffixes) / sizeof(Suffixes[0]) && g_DiskImagePath == NULL; si++)
                {
                    for (drive = 'C'; drive <= 'Z'; drive++)
                    {
                        char rootPath[4] = "C:\\";
                        UINT driveType;

                        if ((logicalDrives & (1UL << (drive - 'A'))) == 0)
                            continue;

                        rootPath[0] = drive;
                        driveType = GetDriveTypeA(rootPath);
                        if (driveType == DRIVE_UNKNOWN || driveType == DRIVE_NO_ROOT_DIR)
                            continue;
                        if (driveType == DRIVE_CDROM)
                            continue;
                        if (fallbackDrive != 0 && drive == fallbackDrive)
                            continue;

                        _snprintf(scannedDisk, sizeof(scannedDisk), "%c:%s", drive, Suffixes[si]);
                        scannedDisk[sizeof(scannedDisk) - 1] = '\0';
                        if (GetFileAttributesA(scannedDisk) != INVALID_FILE_ATTRIBUTES)
                        {
                            g_DiskImagePath = scannedDisk;
                            break;
                        }
                    }
                }

                if (g_DiskImagePath != NULL)
                    break;

                if (!loggedWait && fallbackDrive != 0)
                {
                    RoslLog("[INFO] Waiting up to %lu ms for non-CD VHDX volume...\n",
                            (unsigned long)waitMs);
                    loggedWait = 1;
                }
                Sleep(250);
            }

            /* Pass 2: bundled fallback next to kernel image. */
            if (g_DiskImagePath == NULL &&
                GetFileAttributesA(defaultDisk) != INVALID_FILE_ATTRIBUTES)
            {
                g_DiskImagePath = defaultDisk;
            }

            /* Pass 3: final broad scan including CD fallback.
             * A cleaner long-term direction is volume-GUID enumeration once
             * those APIs are consistently available on ReactOS. */
            if (g_DiskImagePath == NULL)
            {
                DWORD logicalDrives = GetLogicalDrives();
                int pass;

                for (pass = 0; pass < 2 && g_DiskImagePath == NULL; pass++)
                {
                    for (si = 0; si < sizeof(Suffixes) / sizeof(Suffixes[0]) && g_DiskImagePath == NULL; si++)
                    {
                        for (drive = 'C'; drive <= 'Z'; drive++)
                        {
                            char rootPath[4] = "C:\\";
                            UINT driveType;

                            if ((logicalDrives & (1UL << (drive - 'A'))) == 0)
                                continue;

                            rootPath[0] = drive;
                            driveType = GetDriveTypeA(rootPath);
                            if (driveType == DRIVE_UNKNOWN || driveType == DRIVE_NO_ROOT_DIR)
                                continue;

                            if (pass == 0)
                            {
                                if (driveType == DRIVE_CDROM)
                                    continue;
                            }
                            else
                            {
                                if (driveType != DRIVE_CDROM)
                                    continue;
                            }

                            _snprintf(scannedDisk, sizeof(scannedDisk), "%c:%s", drive, Suffixes[si]);
                            scannedDisk[sizeof(scannedDisk) - 1] = '\0';
                            if (GetFileAttributesA(scannedDisk) != INVALID_FILE_ATTRIBUTES)
                            {
                                g_DiskImagePath = scannedDisk;
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (g_DiskImagePath != NULL)
        {
            static char autoKernelFallback[MAX_PATH];
            kernelPath = ResolveDiskBootKernelPath(defaultKernel,
                                                   g_DiskImagePath,
                                                   autoKernelFallback,
                                                   sizeof(autoKernelFallback));
            initrdPath = NULL;  /* no initrd needed for disk boot */
            RoslLog("[INFO] Auto-detected VHDX: %s\n", g_DiskImagePath);
            RoslLog("[INFO]   Kernel: %s\n", kernelPath);
            RoslLog("[INFO]   Initrd: none (disk-backed root)\n");
        }
        else
        {
            /* Fallback to initrd mode */
            _snprintf(defaultInitrd, sizeof(defaultInitrd), "%srosv\\initrd.img", exeDir);
            defaultInitrd[sizeof(defaultInitrd) - 1] = '\0';

            RoslLog("[INFO] No arguments given, looking for bundled images:\n");
            RoslLog("[INFO]   Kernel: %s\n", defaultKernel);
            RoslLog("[INFO]   Initrd: %s\n", defaultInitrd);

            kernelPath = defaultKernel;
            initrdPath = defaultInitrd;
        }
    }
    else
    {
        PrintUsage();
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    ramMB = (posCount >= 3) ? (ULONG)atoi(posArgs[2])
                            : (g_DiskImagePath ? DEFAULT_DISK_RAM_MB : DEFAULT_RAM_MB);

    if (posCount >= 4)
    {
        /* Explicit cmdline from user - use as-is */
        cmdline = posArgs[3];
    }
    else if (g_DiskImagePath)
    {
        static char tscArg[64];
        /* Disk boot: use disk cmdline with virtio_mmio.device= parameters.
         * Both virtio-blk (disk) and virtio-net (network) MMIO devices. */
        static char diskCmdlineBuf[1152];
        ULONG tscEarlyKHz = RoslDetectTscEarlyKHz();
        const char *tscExtra = "";

        if (tscEarlyKHz != 0)
        {
            _snprintf(tscArg, sizeof(tscArg), " tsc_early_khz=%lu clocksource=tsc",
                      (unsigned long)tscEarlyKHz);
            tscArg[sizeof(tscArg) - 1] = '\0';
            tscExtra = tscArg;
            RoslLog("[INFO] TSC hint: tsc_early_khz=%lu\n", (unsigned long)tscEarlyKHz);
        }

        _snprintf(diskCmdlineBuf, sizeof(diskCmdlineBuf),
                  "%s%s %s %s %s %s",
                  DEFAULT_CMDLINE_COMMON,
                  tscExtra,
                  DEFAULT_DISK_ROOT_ARGS,
                  VIRTIO_MMIO_BLK_PARAM,
                  VIRTIO_MMIO_NET_PARAM,
                  VIRTIO_MMIO_CON_PARAM);
        diskCmdlineBuf[sizeof(diskCmdlineBuf) - 1] = '\0';
        cmdline = diskCmdlineBuf;
        RoslLog("[INFO] Disk boot mode: root=/dev/vda via virtio-blk (%s)\n",
                VIRTIO_MMIO_BLK_PARAM);
        RoslLog("[INFO] Network: virtio-net at %s\n",
                VIRTIO_MMIO_NET_PARAM);
    }
    else
    {
        static char tscArg[64];
        /* Ramdisk boot: use standard initramfs root args + virtio-net. */
        static char initrdCmdlineBuf[1152];
        ULONG tscEarlyKHz = RoslDetectTscEarlyKHz();
        const char *tscExtra = "";

        if (tscEarlyKHz != 0)
        {
            _snprintf(tscArg, sizeof(tscArg), " tsc_early_khz=%lu clocksource=tsc",
                      (unsigned long)tscEarlyKHz);
            tscArg[sizeof(tscArg) - 1] = '\0';
            tscExtra = tscArg;
            RoslLog("[INFO] TSC hint: tsc_early_khz=%lu\n", (unsigned long)tscEarlyKHz);
        }

        _snprintf(initrdCmdlineBuf, sizeof(initrdCmdlineBuf),
                  "%s%s %s %s %s",
                  DEFAULT_CMDLINE_COMMON,
                  tscExtra,
                  DEFAULT_INITRD_ROOT_ARGS,
                  VIRTIO_MMIO_NET_PARAM,
                  VIRTIO_MMIO_CON_PARAM);
        initrdCmdlineBuf[sizeof(initrdCmdlineBuf) - 1] = '\0';
        cmdline = initrdCmdlineBuf;
        RoslLog("[INFO] Network: virtio-net at %s\n",
                VIRTIO_MMIO_NET_PARAM);
    }

    if (ramMB == 0 || ramMB > 4096)
    {
        RoslLog("[FAIL] Invalid RAM size %lu MB (must be 1-4096)\n", ramMB);
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    /* Detect initrd format and auto-size RAM if needed.
     * In disk mode with no initrd, skip this entirely. */
    memset(&initrdInfo, 0, sizeof(initrdInfo));
    if (initrdPath != NULL && GetInitrdInfo(initrdPath, &initrdInfo))
    {
        ULONG compMB = (initrdInfo.CompressedSize + (1024 * 1024 - 1)) / (1024 * 1024);
        ULONG uncompMB = (initrdInfo.UncompressedSize + (1024 * 1024 - 1)) / (1024 * 1024);

        if (initrdInfo.IsGzip && initrdInfo.UncompressedSize > 0)
        {
            ULONG neededMB;
            RoslLog("[INFO] Initrd image: gzip compressed=%lu MB, uncompressed=%lu MB\n",
                    compMB, uncompMB);

            /* During initramfs extraction, Linux needs free pages proportional
             * to total RAM (hash tables, page structs, etc.).  Empirically,
             * uncomp * 2 + 256 is the minimum that works reliably. */
            neededMB = ((uncompMB * 2 + 576) + 63) & ~63UL;
            if (neededMB < 512)
                neededMB = 512;
            if (neededMB > 3584)
                neededMB = 3584; /* Cap: leave room for host OS */

            if (posCount < 3 && neededMB > ramMB)
            {
                RoslLog("[INFO] Auto-sizing VM RAM from %lu MB to %lu MB for initrd unpack\n",
                        ramMB, neededMB);
                ramMB = neededMB;
            }

            /* Minimum RAM: must fit compressed initrd + 128 MB for kernel/overhead */
            minRamMB = compMB + 128;
        }
        else
        {
            RoslLog("[INFO] Initrd image: %lu MB (not gzip compressed)\n", compMB);
            minRamMB = compMB + 128;
        }
    }
    else
    {
        minRamMB = RAM_MIN_FLOOR_MB;
    }

    /* Round minRamMB up to 64 MB boundary */
    minRamMB = (minRamMB + 63) & ~63UL;
    if (minRamMB < RAM_MIN_FLOOR_MB)
        minRamMB = RAM_MIN_FLOOR_MB;

    RoslLog("[INFO] Role: %s\n",
            (g_AttachSessionId != (ULONG)-1) ? "client" : "service");
    RoslLog("[INFO] I/O mode: %s\n",
            g_IoMode == RoslModeVcon ? "VCON (virtio-console)" :
            g_IoMode == RoslModePty ? "PTY" : "UART (legacy)");
    if (g_AttachSessionId == (ULONG)-1 && g_IoMode == RoslModePty)
    {
        RoslLog("[INFO] Service keeps serial supervision; interactive PTYs are allocated only for clients\n");
    }

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    /* Step 0: Load driver if not already running */
    if (!EnsureDriverLoaded())
        Die("Failed to load rosv.sys driver");

    /* Step 1: Open device */
    RoslLog("[INFO] Opening \\\\.\\RosvHypervisor\n");
    g_hDev = OpenRosvDeviceWithRetry(50, 100);
    if (g_hDev == INVALID_HANDLE_VALUE)
        Die("Cannot open \\\\.\\RosvHypervisor - is rosv.sys loaded?");
    RoslLog("[OK]   Device opened\n");

    /* A running VM must be managed through the service control plane. */
    {
        ROSV_VM_STATE_INFO existingState;
        ULONG AutoSessionId = 0;
        memset(&existingState, 0, sizeof(existingState));
        if (Ioctl(ROSV_IOCTL_GET_STATE, NULL, 0, &existingState, sizeof(existingState), &ret) &&
            existingState.State == RosvVmStateRunning)
        {
            if (!g_ForceService && RoslDiscoverServiceSession(&AutoSessionId))
            {
                g_AttachSessionId = AutoSessionId;
                RoslLog("[INFO] Detected active supervisor session %lu; switching to client mode\n",
                        g_AttachSessionId);
                return RoslRunInteractiveClient(hMutex, TRUE);
            }

            RoslLog("[FAIL] A ROSL VM is already running. Re-run without --service to auto-attach as a client.\n");
            CloseHandle(g_hDev);
            g_hDev = INVALID_HANDLE_VALUE;
            if (hMutex) CloseHandle(hMutex);
            return 2;
        }
    }

    /* Steps 2+3: Create VM and configure memory (with retry on low memory). */
    if (!CreateAndConfigureVm(ramMB, minRamMB, &tmpCreateRes, &actualRamMB, &vmConfigErr))
    {
        SetLastError(vmConfigErr);
        Die("Failed to create VM and configure memory");
    }

    if (actualRamMB != ramMB)
    {
        RoslLog("[INFO] VM running with %lu MB (requested %lu MB, host memory limited)\n",
                actualRamMB, ramMB);
    }

    /* Step 4: Load kernel (read on demand to reduce peak host memory) */
    kernelBuf = ReadWholeFile(kernelPath, &kernelSz);
    if (!kernelBuf)
        Die("Failed to read kernel image");

    RoslLog("[INFO] ROSV_IOCTL_LOAD_KERNEL (%lu bytes)\n", kernelSz);
    if (!Ioctl(ROSV_IOCTL_LOAD_KERNEL, kernelBuf, kernelSz, NULL, 0, &ret))
        Die("IOCTL_LOAD_KERNEL failed");
    RoslLog("[OK]   Kernel loaded\n");
    HeapFree(GetProcessHeap(), 0, kernelBuf);
    kernelBuf = NULL;

    /* Step 5: Load initrd (optional in disk mode) */
    if (initrdPath != NULL)
    {
        if (!StreamInitrdFile(initrdPath, &initrdSz))
            Die("Failed to stream initrd image");
    }
    else
    {
        RoslLog("[INFO] No initrd - booting with disk-backed root only\n");
    }

    /* Step 5.5: Attach disk image (disk mode only) */
    if (g_DiskImagePath != NULL)
    {
        ROSV_DISK_ATTACH_REQUEST diskReq;
        ROSV_DISK_ATTACH_RESULT diskRes;
        DWORD pathLen = (DWORD)strlen(g_DiskImagePath);
        int wideLen;

        if (pathLen >= ROSV_DISK_PATH_MAX)
        {
            RoslLog("[FAIL] Disk image path too long (%lu chars, max %d)\n",
                    pathLen, ROSV_DISK_PATH_MAX - 1);
            Die("Disk image path exceeds maximum length");
        }

        memset(&diskReq, 0, sizeof(diskReq));
        diskReq.DiskSizeBytes = 0; /* auto-detect from file */
        diskReq.Flags = 0;

        /* Convert narrow path to wide chars for the kernel driver */
        wideLen = MultiByteToWideChar(CP_ACP, 0, g_DiskImagePath, (int)pathLen,
                                      diskReq.Path, ROSV_DISK_PATH_MAX - 1);
        if (wideLen <= 0)
        {
            RoslLog("[FAIL] Failed to convert disk path to wide chars (err=%lu)\n",
                    GetLastError());
            Die("Disk path conversion failed");
        }
        diskReq.PathLength = (ULONG)wideLen;
        diskReq.Path[wideLen] = L'\0';

        memset(&diskRes, 0, sizeof(diskRes));
        RoslLog("[INFO] ROSV_IOCTL_ATTACH_DISK: \"%s\"\n", g_DiskImagePath);
        if (!Ioctl(ROSV_IOCTL_ATTACH_DISK,
                   &diskReq, sizeof(diskReq),
                   &diskRes, sizeof(diskRes), &ret))
        {
            RoslLog("[FAIL] IOCTL_ATTACH_DISK failed (err=%lu)\n", GetLastError());
            Die("Failed to attach disk image");
        }
        {
            const char *modeName = (diskRes.DiskMode == 1) ? "demand-paged" : "ramdisk";
            const char *backendName = (diskRes.BackendType == 1) ? "VHDX" : "RAW";
            RoslLog("[OK]   Disk attached (%s, %s): index=%lu, size=%llu bytes (%llu MB)\n",
                    modeName, backendName,
                    diskRes.DiskIndex,
                    (unsigned long long)diskRes.DiskSizeBytes,
                    (unsigned long long)(diskRes.DiskSizeBytes / (1024 * 1024)));
        }
    }

    /* Step 6: Set command line */
    RoslLog("[INFO] ROSV_IOCTL_SET_CMDLINE: \"%s\"\n", cmdline);
    if (!Ioctl(ROSV_IOCTL_SET_CMDLINE, (void *)cmdline, (DWORD)(strlen(cmdline) + 1), NULL, 0, &ret))
        Die("IOCTL_SET_CMDLINE failed");
    RoslLog("[OK]   Command line set\n");

    /* Step 7: Start VM */
    RoslLog("[INFO] ROSV_IOCTL_START_VM\n");
    if (!Ioctl(ROSV_IOCTL_START_VM, NULL, 0, NULL, 0, &ret))
        Die("IOCTL_START_VM failed");
    RoslLog("[OK]   VM started!\n\n");

    RoslLog("[INFO] Network backend: %s\n",
            g_NetBackendType == 2 ? "in-kernel netio" :
            g_NetBackendType == 1 ? "external netd/slirp (launch rosl_netd)" : "none");

    /* Step 8b: Launch rosl_netd only for the legacy external netd backend */
    if (g_NetBackendType == 1)
    {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        char netdCmd[] = "rosl_netd.exe --backend=slirp";

        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        memset(&pi, 0, sizeof(pi));

        RoslLog("[INFO] Launching: %s\n", netdCmd);
        if (CreateProcessA(NULL, netdCmd, NULL, NULL, FALSE,
                           CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
        {
            RoslLog("[OK]   rosl_netd started (PID=%lu)\n", pi.dwProcessId);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        else
        {
            RoslLog("[WARN] Failed to launch rosl_netd (err=%lu)\n", GetLastError());
        }
    }

    if (!RoslStartControlServer(tmpCreateRes.VmId))
    {
        RoslLog("[FAIL] Failed to start service control plane\n");
        Ioctl(ROSV_IOCTL_DESTROY_VM, NULL, 0, NULL, 0, &ret);
        CloseHandle(g_hDev);
        g_hDev = INVALID_HANDLE_VALUE;
        RoslDeleteControlState();
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    RoslLog("[INFO] Service session ID: %lu\n", g_ServiceSessionId);
    RoslLog("[INFO] Attach interactive clients with: rosl.exe --attach %lu\n",
            g_ServiceSessionId);

    /* Step 8: Run the serial supervisor loop */
    ServiceSupervisorLoop();

    RoslStopControlServer();
    RoslLog("[INFO] ROSV_IOCTL_DESTROY_VM\n");
    if (!Ioctl(ROSV_IOCTL_DESTROY_VM, NULL, 0, NULL, 0, &ret))
    {
        RoslLog("[WARN] IOCTL_DESTROY_VM failed (err=%lu)\n", GetLastError());
    }
    else
    {
        RoslLog("[OK]   VM destroyed\n");
    }

    CloseHandle(g_hDev);
    g_hDev = INVALID_HANDLE_VALUE;
    RoslDeleteControlState();
    if (hMutex) CloseHandle(hMutex);
    RoslLog("[DONE] Service stopped.\n");
    return 0;
}
