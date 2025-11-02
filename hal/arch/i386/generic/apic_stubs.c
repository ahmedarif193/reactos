/*
 * Provide weak fallbacks for APIC-related symbols so that legacy (PIC-only)
 * or non-ACPI HAL builds continue to link even when the APIC/MADT support
 * files are not part of the image.
 */

#include <hal.h>

BOOLEAN
NTAPI
HalIsIoApicPresentFallback(VOID)
{
    return FALSE;
}

#if defined(_MSC_VER)
#pragma comment(linker, "/alternatename:HalIsIoApicPresent=HalIsIoApicPresentFallback")
#else
BOOLEAN
NTAPI
HalIsIoApicPresent(VOID) __attribute__((weak, alias("HalIsIoApicPresentFallback")));
#endif

/*
 * ACPI SCI GSI number (used by APIC code to enforce level/low). On builds
 * without ACPI, provide a weak default value that indicates "unknown".
 */
#if defined(_MSC_VER)
__declspec(selectany) unsigned long HalpSciGsi = 0xFFFFFFFFul;
#else
unsigned long HalpSciGsi __attribute__((weak)) = 0xFFFFFFFFul;
#endif

/* EOF */
