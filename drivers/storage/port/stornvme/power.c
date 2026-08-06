/*
 * PROJECT:     ReactOS Storport NVMe miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Power states, autonomous transitions, thermal management
 *              and asynchronous event handling
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "stornvme.h"

VOID
NvmeParsePowerCapabilities(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PNVME_IDENTIFY_CONTROLLER Identify)
{
    ULONG State;

    Device->Npss = Identify->NPSS;
    Device->Apsta = Identify->APSTA;
    Device->Wctemp = Identify->WCTEMP;
    Device->Cctemp = Identify->CCTEMP;
    Device->Hctma = Identify->HCTMA;
    Device->Mntmt = Identify->MNTMT;
    Device->Mxtmt = Identify->MXTMT;
    RtlCopyMemory(Device->Psd, Identify->PSD, sizeof(Device->Psd));

    Device->DeepestNonOpState = 0;
    for (State = Device->Npss; State >= 1; State--)
    {
        PNVME_POWER_STATE_DESC Psd = &Device->Psd[State];

        if ((Psd->Flags & NVME_PSD_FLAG_NOPS) == 0)
            continue;
        if ((ULONGLONG)Psd->ENLAT + Psd->EXLAT > NVME_APST_MAX_LATENCY_US)
            continue;
        Device->DeepestNonOpState = (UCHAR)State;
        break;
    }
}

/*
 * Autonomous power state transitions: every operational state idles into
 * the deepest acceptable non-operational state, with a transition time
 * scaled to the state's combined entry and exit latency.
 */
static
VOID
NvmeConfigureApst(_In_ PNVME_DEVICE_EXTENSION Device)
{
    PNVME_POWER_STATE_DESC Psd;
    PULONGLONG Table = (PULONGLONG)Device->IdentifyBuffer;
    NVME_COMMAND Command;
    ULONGLONG IdleMilliseconds;
    ULONG State;

    Device->ApstEnabled = FALSE;
    if ((Device->Apsta & 1) == 0 || Device->DeepestNonOpState == 0)
        return;

    Psd = &Device->Psd[Device->DeepestNonOpState];
    IdleMilliseconds = (((ULONGLONG)Psd->ENLAT + Psd->EXLAT) / 1000) * 50;
    if (IdleMilliseconds < NVME_APST_MIN_ITPT_MS)
        IdleMilliseconds = NVME_APST_MIN_ITPT_MS;
    if (IdleMilliseconds > NVME_APST_MAX_ITPT_MS)
        IdleMilliseconds = NVME_APST_MAX_ITPT_MS;

    RtlZeroMemory(Table, NVME_APST_ENTRIES * sizeof(ULONGLONG));
    for (State = 0; State < Device->DeepestNonOpState; State++)
        Table[State] = NVME_APST_ENTRY(Device->DeepestNonOpState, IdleMilliseconds);

    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_SET_FEATURES, 0);
    Command.PRP1 = Device->IdentifyPhysical;
    Command.CDW10 = NVME_FEATURE_APST;
    Command.CDW11 = 1;
    if (NvmeAdminCommandSync(Device, &Command, NULL))
    {
        Device->ApstEnabled = TRUE;
        DPRINT1("stornvme: APST enabled, idle to PS%u after %I64u ms\n",
                Device->DeepestNonOpState, IdleMilliseconds);
    }
}

/*
 * Host controlled thermal management: light throttling at the warning
 * temperature, heavy throttling at the critical temperature.
 */
static
VOID
NvmeConfigureThermal(_In_ PNVME_DEVICE_EXTENSION Device)
{
    NVME_COMMAND Command;
    ULONG Tmt1;
    ULONG Tmt2;

    if (Device->Wctemp != 0)
    {
        RtlZeroMemory(&Command, sizeof(Command));
        NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_SET_FEATURES, 0);
        Command.CDW10 = NVME_FEATURE_TEMP_THRESHOLD;
        Command.CDW11 = Device->Wctemp;
        NvmeAdminCommandSync(Device, &Command, NULL);
    }

    if ((Device->Hctma & 1) == 0 || Device->Mxtmt == 0 || Device->Mxtmt < Device->Mntmt)
        return;
    Tmt2 = Device->Cctemp != 0 ? Device->Cctemp : Device->Mxtmt;
    Tmt2 = min(Tmt2, Device->Mxtmt);
    if (Tmt2 <= Device->Mntmt)
        return;
    Tmt1 = Device->Wctemp != 0 ? Device->Wctemp : Tmt2 - 1;
    Tmt1 = min(Tmt1, Tmt2 - 1);
    if (Tmt1 < Device->Mntmt)
        Tmt1 = Device->Mntmt;

    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_SET_FEATURES, 0);
    Command.CDW10 = NVME_FEATURE_HCTM;
    Command.CDW11 = (Tmt1 << 16) | Tmt2;
    if (NvmeAdminCommandSync(Device, &Command, NULL))
        DPRINT1("stornvme: thermal management thresholds %luK/%luK\n", Tmt1, Tmt2);
}

VOID
NvmeConfigurePowerAndEvents(_In_ PNVME_DEVICE_EXTENSION Device)
{
    NVME_COMMAND Command;

    NvmeConfigureApst(Device);
    NvmeConfigureThermal(Device);

    /* All SMART warnings; namespace notices too when the controller has them. */
    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_SET_FEATURES, 0);
    Command.CDW10 = NVME_FEATURE_ASYNC_EVENT_CONFIG;
    Command.CDW11 = 0xFF | (Device->Oaes & NVME_OAES_NS_ATTRIBUTE);
    NvmeAdminCommandSync(Device, &Command, NULL);
}

/* Caller holds the admin domain. */
VOID
NvmeArmAerLocked(_In_ PNVME_DEVICE_EXTENSION Device)
{
    NVME_COMMAND Command;

    if (Device->AerOutstanding)
        return;
    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_ASYNC_EVENT, 0);
    if (NvmeAdminSubmitLocked(Device, &Command, NVME_SLOT_AER, NULL) != MAXULONG)
        Device->AerOutstanding = TRUE;
}

/* Caller holds the admin domain. */
VOID
NvmeKickSmartLocked(_In_ PNVME_DEVICE_EXTENSION Device)
{
    NVME_COMMAND Command;

    if (Device->SmartInFlight)
        return;
    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_GET_LOG_PAGE, 0);
    Command.NSID = 0xFFFFFFFF;
    Command.PRP1 = Device->SmartLogPhysical;
    Command.CDW10 = NVME_LOG_SMART | (((sizeof(NVME_SMART_LOG) / 4) - 1) << 16);
    if (NvmeAdminSubmitLocked(Device, &Command, NVME_SLOT_SMART, NULL) != MAXULONG)
        Device->SmartInFlight = TRUE;
}

/* Caller holds the admin domain. */
VOID
NvmeHandleAerLocked(_In_ PNVME_DEVICE_EXTENSION Device, _In_ ULONG Dw0, _In_ USHORT Status)
{
    Device->AerOutstanding = FALSE;
    if (Status != 0)
        return;

    DPRINT1("stornvme: async event type %lu info 0x%02lx log page 0x%02lx\n",
            NVME_AEN_TYPE(Dw0), NVME_AEN_INFO(Dw0), NVME_AEN_LOG_PAGE(Dw0));
    NvmeArmAerLocked(Device);

    switch (NVME_AEN_TYPE(Dw0))
    {
        case NVME_AEN_TYPE_SMART:
        case NVME_AEN_TYPE_ERROR:
            NvmeKickSmartLocked(Device);
            break;

        case NVME_AEN_TYPE_NOTICE:
            /* Namespace attributes changed; let the port rescan the bus. */
            StorPortNotification(BusChangeDetected, Device, 0);
            break;

        default:
            break;
    }
}

/* Caller holds the admin domain. */
VOID
NvmeHandleSmartLocked(_In_ PNVME_DEVICE_EXTENSION Device, _In_ USHORT Status)
{
    UCHAR PreviousWarning = Device->CriticalWarning;

    Device->SmartInFlight = FALSE;
    if (Status != 0)
        return;

    Device->CompositeTemperature = Device->SmartLog->CompositeTemperature;
    Device->CriticalWarning = Device->SmartLog->CriticalWarning;
    Device->AvailableSpare = Device->SmartLog->AvailableSpare;
    Device->PercentageUsed = Device->SmartLog->PercentageUsed;

    if (Device->CriticalWarning != PreviousWarning)
    {
        DPRINT1("stornvme: health change: warning 0x%02x, %uK, spare %u%%, used %u%%\n",
                Device->CriticalWarning,
                Device->CompositeTemperature,
                Device->AvailableSpare,
                Device->PercentageUsed);
    }
}

/*
 * START STOP UNIT power plumbing: an explicit stop or idle request parks
 * the controller in the deepest usable non-operational power state, a
 * start request returns it to full power.
 */
BOOLEAN
NvmeSubmitPowerStateSrb(_In_ PNVME_DEVICE_EXTENSION Device,
                        _In_ PSCSI_REQUEST_BLOCK Srb,
                        _In_ BOOLEAN Operational)
{
    PNVME_SRB_EXTENSION SrbExtension = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    NVME_COMMAND Command;
    NVME_LOCK Lock;
    ULONG Slot;

    if (!Operational && Device->DeepestNonOpState == 0)
    {
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
        return TRUE;
    }
    if (SrbExtension == NULL)
    {
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_ERROR);
        return TRUE;
    }
    SrbExtension->FeatureIsWce = FALSE;

    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_SET_FEATURES, 0);
    Command.CDW10 = NVME_FEATURE_POWER_MANAGEMENT;
    Command.CDW11 = Operational ? 0 : Device->DeepestNonOpState;

    NvmeAcquireLock(Device, 0, &Lock);
    Slot = NvmeAdminSubmitLocked(Device, &Command, NVME_SLOT_SRB_FEATURE, Srb);
    NvmeReleaseLock(Device, &Lock);
    if (Slot == MAXULONG)
    {
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_BUSY);
        return TRUE;
    }
    return TRUE;
}
