/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/arch/common/acpi/hpet.c
 * PURPOSE:         HPET (High Precision Event Timer) support for HAL
 * PROGRAMMERS:     ReactOS Portable Systems Group
 *
 * DESCRIPTION:     This file implements HPET support for high-resolution
 *                  timing in the HAL. HPET provides a more accurate clock
 *                  source than the legacy 8254 PIT and can be used for:
 *                  - High-resolution performance counter
 *                  - TSC calibration verification
 *                  - Alternative clock interrupt source
 */

#include <hal.h>
#include <halacpi.h>
#include <ntifs.h>
#include <reactos/drivers/acpi/acpi.h>
#define NDEBUG
#include <debug.h>

/* HPET global state */
BOOLEAN HalpHpetInitialized = FALSE;
PVOID HalpHpetBase = NULL;
ULONGLONG HalpHpetPeriod = 0;       /* Period in femtoseconds */
ULONGLONG HalpHpetFrequency = 0;    /* Frequency in Hz */

/* Cached HPET table */
static PHPET_DESCRIPTION_TABLE HalpHpetTable = NULL;

/* PRIVATE FUNCTIONS *********************************************************/

/**
 * @brief Read a 32-bit HPET register
 */
FORCEINLINE
ULONG
HalpHpetRead32(
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)HalpHpetBase + Offset));
}

/**
 * @brief Read a 64-bit HPET register
 */
FORCEINLINE
ULONGLONG
HalpHpetRead64(
    _In_ ULONG Offset)
{
    /* Read as two 32-bit values for compatibility with 32-bit systems */
    ULONGLONG Value;
    PULONG Reg = (PULONG)((PUCHAR)HalpHpetBase + Offset);

    /* For 64-bit counter, we need to handle wrap-around correctly:
     * Read high, low, high again. If high changed, re-read. */
    ULONG High1, High2, Low;

    do
    {
        High1 = READ_REGISTER_ULONG(Reg + 1);
        Low = READ_REGISTER_ULONG(Reg);
        High2 = READ_REGISTER_ULONG(Reg + 1);
    } while (High1 != High2);

    Value = ((ULONGLONG)High2 << 32) | Low;
    return Value;
}

/**
 * @brief Write a 32-bit HPET register
 */
FORCEINLINE
VOID
HalpHpetWrite32(
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)HalpHpetBase + Offset), Value);
}

/**
 * @brief Write a 64-bit HPET register
 */
FORCEINLINE
VOID
HalpHpetWrite64(
    _In_ ULONG Offset,
    _In_ ULONGLONG Value)
{
    PULONG Reg = (PULONG)((PUCHAR)HalpHpetBase + Offset);

    /* Write low first, then high */
    WRITE_REGISTER_ULONG(Reg, (ULONG)Value);
    WRITE_REGISTER_ULONG(Reg + 1, (ULONG)(Value >> 32));
}

/* PUBLIC FUNCTIONS **********************************************************/

/**
 * @brief Discover and cache the HPET ACPI table
 *
 * This function is called early during HAL initialization to locate
 * the HPET table in the ACPI tables. The actual initialization of
 * HPET hardware is done later in HalpHpetInitialize().
 */
CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiDiscoverHpetTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PDESCRIPTION_HEADER Header;

    Header = HalAcpiGetTable(LoaderBlock, HPET_SIGNATURE);
    if (!Header)
    {
        DPRINT("HAL: No HPET ACPI table found\n");
        return;
    }

    /* Validate table length */
    if (Header->Length < sizeof(HPET_DESCRIPTION_TABLE))
    {
        DPRINT1("HAL: HPET table too small (length %lu, expected %lu)\n",
                Header->Length, (ULONG)sizeof(HPET_DESCRIPTION_TABLE));
        return;
    }

    HalpHpetTable = (PHPET_DESCRIPTION_TABLE)Header;

    DPRINT("HAL: ACPI HPET table discovered (len %lu, HwId 0x%08lX, addr 0x%llX)\n",
           Header->Length,
           HalpHpetTable->HardwareId,
           HalpHpetTable->Address.Address.QuadPart);
}

/**
 * @brief Initialize the HPET hardware
 *
 * This function maps the HPET registers, reads the capabilities,
 * and enables the main counter. It must be called after memory
 * management is initialized (Phase 1 init or later).
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpHpetInitialize(VOID)
{
    PHYSICAL_ADDRESS HpetPhysAddr;
    ULONGLONG Capabilities;
    ULONGLONG Config;
    ULONG NumTimers;
    ULONG VendorId;

    /* Check if HPET table was found */
    if (HalpHpetTable == NULL)
    {
        DPRINT("HAL: HPET not available (no ACPI table)\n");
        return STATUS_NOT_FOUND;
    }

    /* Validate address space - HPET must be memory-mapped */
    if (HalpHpetTable->Address.AddressSpaceID != ACPI_GAS_SYSTEM_MEMORY)
    {
        DPRINT1("HAL: HPET address space is not memory (got %u)\n",
                HalpHpetTable->Address.AddressSpaceID);
        return STATUS_NOT_SUPPORTED;
    }

    /* Get physical address */
    HpetPhysAddr = HalpHpetTable->Address.Address;
    if (HpetPhysAddr.QuadPart == 0)
    {
        DPRINT1("HAL: HPET base address is zero\n");
        return STATUS_INVALID_PARAMETER;
    }

    /* Map HPET registers - HPET uses 1KB of address space */
    HalpHpetBase = MmMapIoSpace(HpetPhysAddr, PAGE_SIZE, MmNonCached);
    if (HalpHpetBase == NULL)
    {
        DPRINT1("HAL: Failed to map HPET registers at 0x%llX\n",
                HpetPhysAddr.QuadPart);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DPRINT("HAL: HPET registers mapped at VA %p (PA 0x%llX)\n",
           HalpHpetBase, HpetPhysAddr.QuadPart);

    /* Read capabilities register */
    Capabilities = HalpHpetRead64(HPET_REG_CAPABILITIES);

    /* Extract period (in femtoseconds) from high 32 bits */
    HalpHpetPeriod = Capabilities >> HPET_CAP_PERIOD_SHIFT;
    if (HalpHpetPeriod == 0 || HalpHpetPeriod > 100000000ULL)
    {
        /* Invalid period - HPET spec says max is 100ns = 100,000,000 fs */
        DPRINT1("HAL: HPET has invalid period %llu fs\n", HalpHpetPeriod);
        MmUnmapIoSpace(HalpHpetBase, PAGE_SIZE);
        HalpHpetBase = NULL;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* Calculate frequency: freq = 10^15 / period_fs */
    HalpHpetFrequency = 1000000000000000ULL / HalpHpetPeriod;

    /* Extract other capabilities */
    NumTimers = ((Capabilities & HPET_CAP_NUM_TIMERS_MASK) >> HPET_CAP_NUM_TIMERS_SHIFT) + 1;
    VendorId = (ULONG)((Capabilities & HPET_CAP_VENDOR_ID_MASK) >> HPET_CAP_VENDOR_ID_SHIFT);

    DPRINT("HAL: HPET capabilities: period=%llu fs, freq=%llu Hz, %u timers, vendor=0x%04X, %s counter\n",
           HalpHpetPeriod,
           HalpHpetFrequency,
           NumTimers,
           VendorId,
           (Capabilities & HPET_CAP_COUNT_SIZE_64) ? "64-bit" : "32-bit");

    /* Stop the counter before configuration */
    Config = HalpHpetRead64(HPET_REG_CONFIGURATION);
    Config &= ~(HPET_CFG_ENABLE | HPET_CFG_LEGACY_ROUTE);
    HalpHpetWrite64(HPET_REG_CONFIGURATION, Config);

    /* Reset main counter to 0 */
    HalpHpetWrite64(HPET_REG_MAIN_COUNTER, 0);

    /* Enable the main counter (but not legacy routing - we use APIC) */
    Config = HalpHpetRead64(HPET_REG_CONFIGURATION);
    Config |= HPET_CFG_ENABLE;
    HalpHpetWrite64(HPET_REG_CONFIGURATION, Config);

    /* Verify counter is running */
    {
        ULONGLONG Counter1, Counter2;
        ULONG Attempts;

        Counter1 = HalpHpetRead64(HPET_REG_MAIN_COUNTER);

        /* Wait a bit and verify counter incremented */
        for (Attempts = 0; Attempts < 1000; Attempts++)
        {
            YieldProcessor();
        }

        Counter2 = HalpHpetRead64(HPET_REG_MAIN_COUNTER);

        if (Counter2 <= Counter1)
        {
            DPRINT1("HAL: HPET counter not incrementing (c1=%llu, c2=%llu)\n",
                    Counter1, Counter2);
            /* Disable and unmap */
            Config &= ~HPET_CFG_ENABLE;
            HalpHpetWrite64(HPET_REG_CONFIGURATION, Config);
            MmUnmapIoSpace(HalpHpetBase, PAGE_SIZE);
            HalpHpetBase = NULL;
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }

        DPRINT("HAL: HPET counter verified running (delta=%llu ticks)\n",
               Counter2 - Counter1);
    }

    HalpHpetInitialized = TRUE;

    DPRINT("HAL: HPET initialized successfully at %llu Hz\n", HalpHpetFrequency);

    return STATUS_SUCCESS;
}

/**
 * @brief Query the HPET main counter value
 *
 * This function returns the current value of the HPET main counter.
 * It can be used for high-resolution timing measurements.
 *
 * @return Current counter value, or 0 if HPET is not initialized
 */
ULONGLONG
NTAPI
HalpHpetQueryCounter(VOID)
{
    if (!HalpHpetInitialized || HalpHpetBase == NULL)
    {
        return 0;
    }

    return HalpHpetRead64(HPET_REG_MAIN_COUNTER);
}
