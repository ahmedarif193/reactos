/*
 * PROJECT:     ReactOS Raspberry Pi 5 (BCM2712) display miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     BCM2712 CRTC / PixelValve (timing generator) access.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * BCM2712 register definitions and programming sequences are adapted from
 * Linux drm/vc4 at 95d9c0c7f20ab1b49ac88773a6138b16d2b8f061.
 *
 * For now this reads back the PixelValve the firmware enabled, records the
 * live raster timing, and can re-write the same timing as a no-op ownership
 * check.  The firmware-set HDMI mode remains the active mode.
 */

#include "rpi5vc4_crtc.h"
#include "rpi5vc4_hvs.h"
#include "rpi5vc4_mbox.h"

/* 1024x768 at 60 Hz, VESA DMT mode 16 (65 MHz, negative sync polarity). */
#define RPI5_HEADLESS_PIXEL_CLOCK_HZ    65000000u
#define RPI5_HEADLESS_HDISPLAY          1024u
#define RPI5_HEADLESS_HSYNC_START       1048u
#define RPI5_HEADLESS_HSYNC_END         1184u
#define RPI5_HEADLESS_HTOTAL            1344u
#define RPI5_HEADLESS_VDISPLAY          768u
#define RPI5_HEADLESS_VSYNC_START       771u
#define RPI5_HEADLESS_VSYNC_END         777u
#define RPI5_HEADLESS_VTOTAL            806u

#define RPI5_HDMI_FIFO_CTL              0x07c
#define RPI5_HDMI_SCHEDULER_CONTROL     0x0e8
#define RPI5_HDMI_HORZA                 0x0ec
#define RPI5_HDMI_HORZB                 0x0f0
#define RPI5_HDMI_VERTA0                0x0f4
#define RPI5_HDMI_VERTB0                0x0f8
#define RPI5_HDMI_VERTA1                0x100
#define RPI5_HDMI_VERTB1                0x104
#define RPI5_HDMI_MISC_CONTROL          0x114
#define RPI5_HDMI_DEEP_COLOR_CONFIG     0x18c
#define RPI5_HDMI_GCP_CONFIG            0x194
#define RPI5_HDMI_GCP_WORD              0x198

#define RPI5_HDMI_CLOCK_STOP            0x0bc
#define RPI5_HDMI_VEC_INTERFACE_CFG     0x0f0
#define RPI5_HDMI_VEC_INTERFACE_XBAR    0x0f4

#define RPI5_HDMI_CSC_CTL               0x000
#define RPI5_HDMI_CSC_12_11             0x004
#define RPI5_HDMI_CSC_14_13             0x008
#define RPI5_HDMI_CSC_22_21             0x00c
#define RPI5_HDMI_CSC_24_23             0x010
#define RPI5_HDMI_CSC_32_31             0x014
#define RPI5_HDMI_CSC_34_33             0x018
#define RPI5_HDMI_CSC_CHANNEL_CTL       0x02c

#define RPI5_HDMI_DVP_CTL               0x000
#define RPI5_HDMI_VIDEO_CONTROL         0x044
#define RPI5_HDMI_FRAME_COUNT           0x060

#define RPI5_HDMI0_PHY_PHYS             0x107C701D00ULL
#define RPI5_HDMI0_PHY_LENGTH           0x300
#define RPI5_HDMI0_RM_PHYS              0x107C702000ULL
#define RPI5_HDMI0_RM_LENGTH            0x80
#define RPI5_HDMI_PHY_RESET_CTL         0x000
#define RPI5_HDMI_PHY_POWERUP_CTL       0x004
#define RPI5_HDMI_PHY_CTL_0             0x008
#define RPI5_HDMI_PHY_CTL_1             0x00c
#define RPI5_HDMI_PHY_CTL_2             0x010
#define RPI5_HDMI_PHY_CTL_CK            0x014
#define RPI5_HDMI_PHY_PLL_REFCLK        0x01c
#define RPI5_HDMI_PHY_PLL_POST_KDIV     0x028
#define RPI5_HDMI_PHY_PLL_VCOCLK_DIV    0x02c
#define RPI5_HDMI_PHY_PLL_CFG           0x044
#define RPI5_HDMI_PHY_WORD_SEL          0x054
#define RPI5_HDMI_PHY_PLL_MISC_0        0x060
#define RPI5_HDMI_PHY_PLL_RESET_CTL     0x190
#define RPI5_HDMI_PHY_PLL_POWERUP_CTL   0x194
#define RPI5_HDMI_RM_OFFSET             0x018

#define RPI5_HDMI_PHY_REFCLK_CMOS       (1u << 13)
#define RPI5_HDMI_PHY_POST_KDIV_BYPASS  (1u << 4)
#define RPI5_HDMI_PHY_VCODIV_ENABLE     (1u << 10)
#define RPI5_HDMI_PHY_PLL_POWERUP       (1u << 0)
#define RPI5_HDMI_PHY_PLL_RESETB        (1u << 0)
#define RPI5_HDMI_RM_OFFSET_ONLY        (1u << 31)
#define RPI5_HDMI_PHY_POWERUP_4_LANES   0x000001cfu

#define RPI5_HDMI_PHY_EXT_CURRENT_SHIFT             28
#define RPI5_HDMI_PHY_FFE_ENABLE_SHIFT              27
#define RPI5_HDMI_PHY_SLEW_RATE_SHIFT               26
#define RPI5_HDMI_PHY_FFE_POST_TAP_SHIFT            25
#define RPI5_HDMI_PHY_LDMOS_BIAS_SHIFT              23
#define RPI5_HDMI_PHY_COMMON_MODE_LDMOS_SHIFT       22
#define RPI5_HDMI_PHY_EDGE_SELECT_SHIFT             21
#define RPI5_HDMI_PHY_EXT_CURRENT_HS_SHIFT          20
#define RPI5_HDMI_PHY_TERMINATION_SHIFT             18
#define RPI5_HDMI_PHY_EXT_CURRENT_ENABLE_SHIFT      17
#define RPI5_HDMI_PHY_INT_CURRENT_ENABLE_SHIFT      16
#define RPI5_HDMI_PHY_INT_CURRENT_SHIFT             12
#define RPI5_HDMI_PHY_INT_CURRENT_HS_SHIFT          11
#define RPI5_HDMI_PHY_MAIN_TAP_SHIFT                 8
#define RPI5_HDMI_PHY_POST_TAP_SHIFT                 5
#define RPI5_HDMI_PHY_SLOW_LOADING_SHIFT             3
#define RPI5_HDMI_PHY_SLOW_DRIVING_SHIFT             1
#define RPI5_HDMI_PHY_FFE_PRE_TAP_SHIFT              0

#define RPI5_DVP_RESET_HDMI0            (1u << 1)
#define RPI5_HDMI_CLOCK_STOP_PIXEL      (1u << 1)
#define RPI5_HDMI_FIFO_MASTER           (1u << 0)
#define RPI5_HDMI_SCHED_MANUAL          (1u << 15)
#define RPI5_HDMI_SCHED_IGNORE_PREDICT  (1u << 5)
#define RPI5_HDMI_SCHED_ACTIVE          (1u << 1)
#define RPI5_HDMI_SCHED_MODE_HDMI       (1u << 0)
#define RPI5_HDMI_GCP_ENABLE            (1u << 31)
#define RPI5_HDMI_GCP_CLEAR_AVMUTE      (1u << 4)
#define RPI5_HDMI_DEEP_INIT_PHASE_MASK  (0x7u << 8)
#define RPI5_HDMI_DEEP_COLOR_MASK       0x0fu
#define RPI5_HDMI_GCP_BYTE1_MASK        (0xffu << 8)
#define RPI5_HDMI_GCP_BYTE0_MASK        0xffu
#define RPI5_HDMI_PIXEL_REP_MASK        0x0fu
#define RPI5_HDMI_CSC_ENABLE            (1u << 2)
#define RPI5_HDMI_CSC_CUSTOM            3u
#define RPI5_HDMI_VIDEO_ENABLE          (1u << 31)
#define RPI5_HDMI_VIDEO_UNDERFLOW       (1u << 30)
#define RPI5_HDMI_VIDEO_FRAME_RESET     (1u << 29)
#define RPI5_HDMI_VIDEO_VSYNC_LOW       (1u << 28)
#define RPI5_HDMI_VIDEO_HSYNC_LOW       (1u << 27)
#define RPI5_HDMI_VIDEO_CLRRGB          (1u << 23)
#define RPI5_HDMI_VIDEO_BLANKPIX        (1u << 18)
#define RPI5_HDMI_VIDEO_BLANK_INSERT    (1u << 16)

/*
 * BCM2712 PHY values and calculations below are a direct adaptation of
 * Linux drivers/gpu/drm/vc4/vc4_hdmi_phy.c at
 * 95d9c0c7f20ab1b49ac88773a6138b16d2b8f061. This is the 0..222 MHz lane
 * profile selected by vc6_hdmi_phy_init() for this fixed 65 MHz mode.
 */
typedef struct _RPI5_VC6_PHY_LANE_SETTINGS
{
    ULONG ExtCurrent;
    ULONG FfeEnable;
    ULONG SlewRate;
    ULONG FfePostTap;
    ULONG LdmosBias;
    ULONG CommonModeLdmos;
    ULONG EdgeSelect;
    ULONG ExtCurrentHalfSwing;
    ULONG Termination;
    ULONG ExtCurrentEnable;
    ULONG IntCurrentEnable;
    ULONG IntCurrent;
    ULONG IntCurrentHalfSwing;
    ULONG MainTap;
    ULONG PostTap;
    ULONG SlowLoading;
    ULONG SlowDriving;
    ULONG FfePreTap;
} RPI5_VC6_PHY_LANE_SETTINGS;

static const RPI5_VC6_PHY_LANE_SETTINGS Rpi5Vc6LowRateLane =
{
    8, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 8, 0, 7, 0, 0, 0, 0
};

static ULONG
Rpi5Vc6PackPhyLane(
    _In_ const RPI5_VC6_PHY_LANE_SETTINGS *Settings)
{
    return (Settings->ExtCurrent << RPI5_HDMI_PHY_EXT_CURRENT_SHIFT) |
           (Settings->FfeEnable << RPI5_HDMI_PHY_FFE_ENABLE_SHIFT) |
           (Settings->SlewRate << RPI5_HDMI_PHY_SLEW_RATE_SHIFT) |
           (Settings->FfePostTap << RPI5_HDMI_PHY_FFE_POST_TAP_SHIFT) |
           (Settings->LdmosBias << RPI5_HDMI_PHY_LDMOS_BIAS_SHIFT) |
           (Settings->CommonModeLdmos << RPI5_HDMI_PHY_COMMON_MODE_LDMOS_SHIFT) |
           (Settings->EdgeSelect << RPI5_HDMI_PHY_EDGE_SELECT_SHIFT) |
           (Settings->ExtCurrentHalfSwing << RPI5_HDMI_PHY_EXT_CURRENT_HS_SHIFT) |
           (Settings->Termination << RPI5_HDMI_PHY_TERMINATION_SHIFT) |
           (Settings->ExtCurrentEnable << RPI5_HDMI_PHY_EXT_CURRENT_ENABLE_SHIFT) |
           (Settings->IntCurrentEnable << RPI5_HDMI_PHY_INT_CURRENT_ENABLE_SHIFT) |
           (Settings->IntCurrent << RPI5_HDMI_PHY_INT_CURRENT_SHIFT) |
           (Settings->IntCurrentHalfSwing << RPI5_HDMI_PHY_INT_CURRENT_HS_SHIFT) |
           (Settings->MainTap << RPI5_HDMI_PHY_MAIN_TAP_SHIFT) |
           (Settings->PostTap << RPI5_HDMI_PHY_POST_TAP_SHIFT) |
           (Settings->SlowLoading << RPI5_HDMI_PHY_SLOW_LOADING_SHIFT) |
           (Settings->SlowDriving << RPI5_HDMI_PHY_SLOW_DRIVING_SHIFT) |
           (Settings->FfePreTap << RPI5_HDMI_PHY_FFE_PRE_TAP_SHIFT);
}

static ULONGLONG
Rpi5Vc6GetVcoFrequency(
    _In_ ULONGLONG TmdsRate,
    _Out_ PULONG VcoDivider)
{
    ULONG MinimumDivider = 0;
    ULONG MaximumDivider;
    ULONG Divider;

    while (TmdsRate * MinimumDivider * 10 < 8000000000ULL)
        ++MinimumDivider;

    Divider = MinimumDivider;
    while (TmdsRate * (Divider + 1) * 10 < 12000000000ULL)
        ++Divider;
    MaximumDivider = Divider;

    Divider = MinimumDivider + (MaximumDivider - MinimumDivider) / 2;
    *VcoDivider = Divider;
    return TmdsRate * Divider * 10;
}

static ULONG
Rpi5Vc6GetRateManagerOffset(
    _In_ ULONGLONG VcoFrequency)
{
    ULONGLONG Offset;

    /* Linux phy_get_rm_offset(): 9.22 fixed-point against the 54 MHz XO. */
    Offset = VcoFrequency * 2;
    Offset <<= 22;
    Offset /= 54000000u;
    Offset >>= 2;
    return (ULONG)Offset;
}

static __inline ULONG
Rpi5CrtcRead(
    _In_ PVOID Base,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + Offset));
}

static __inline VOID
Rpi5CrtcWrite(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + Offset), Value);
}

static PVOID
Rpi5CrtcMapRegisters(
    _In_ ULONGLONG Address,
    _In_ ULONG Length)
{
    PHYSICAL_ADDRESS PhysicalAddress;

    PhysicalAddress.QuadPart = Address;
    return MmMapIoSpace(PhysicalAddress, Length, MmNonCached);
}

static BOOLEAN
Rpi5CrtcInitHeadlessPhy(VOID)
{
    static const ULONG PllMisc[] =
    {
        0x810c6000, 0x00b8c451, 0x46402e31,
        0x00b8c005, 0x42410261, 0xcc021001,
        0xc8301c80, 0xb0804444, 0xf80f8000
    };
    PVOID Phy;
    PVOID RateManager;
    ULONGLONG VcoRate;
    ULONG LaneControl;
    ULONG RateManagerOffset;
    ULONG VcoDivider;
    ULONG Value;
    ULONG Index;

    Phy = Rpi5CrtcMapRegisters(RPI5_HDMI0_PHY_PHYS,
                               RPI5_HDMI0_PHY_LENGTH);
    RateManager = Rpi5CrtcMapRegisters(RPI5_HDMI0_RM_PHYS,
                                       RPI5_HDMI0_RM_LENGTH);
    if (Phy == NULL || RateManager == NULL)
    {
        if (RateManager != NULL)
            MmUnmapIoSpace(RateManager, RPI5_HDMI0_RM_LENGTH);
        if (Phy != NULL)
            MmUnmapIoSpace(Phy, RPI5_HDMI0_PHY_LENGTH);
        return FALSE;
    }

    VcoRate = Rpi5Vc6GetVcoFrequency(RPI5_HEADLESS_PIXEL_CLOCK_HZ,
                                     &VcoDivider);
    RateManagerOffset = Rpi5Vc6GetRateManagerOffset(VcoRate);
    LaneControl = Rpi5Vc6PackPhyLane(&Rpi5Vc6LowRateLane);

    Rpi5CrtcWrite(Phy, RPI5_HDMI_PHY_RESET_CTL, 0);
    Rpi5CrtcWrite(Phy, RPI5_HDMI_PHY_POWERUP_CTL, 0);
    Rpi5CrtcWrite(Phy,
                  RPI5_HDMI_PHY_PLL_POST_KDIV,
                  RPI5_HDMI_PHY_POST_KDIV_BYPASS);

    for (Index = 0; Index < RTL_NUMBER_OF(PllMisc); ++Index)
    {
        Rpi5CrtcWrite(Phy,
                      RPI5_HDMI_PHY_PLL_MISC_0 + Index * sizeof(ULONG),
                      PllMisc[Index]);
    }

    Rpi5CrtcWrite(Phy,
                  RPI5_HDMI_PHY_PLL_REFCLK,
                  RPI5_HDMI_PHY_REFCLK_CMOS | 54u);
    Rpi5CrtcWrite(Phy, RPI5_HDMI_PHY_RESET_CTL, 0x7fu);
    Rpi5CrtcWrite(RateManager,
                  RPI5_HDMI_RM_OFFSET,
                  RPI5_HDMI_RM_OFFSET_ONLY |
                      RateManagerOffset);
    Rpi5CrtcWrite(Phy,
                  RPI5_HDMI_PHY_PLL_VCOCLK_DIV,
                  RPI5_HDMI_PHY_VCODIV_ENABLE | VcoDivider);
    Rpi5CrtcWrite(Phy, RPI5_HDMI_PHY_PLL_CFG, 0);
    Rpi5CrtcWrite(Phy, RPI5_HDMI_PHY_PLL_POST_KDIV, (2u << 2) | 1u);
    Rpi5CrtcWrite(Phy, RPI5_HDMI_PHY_CTL_0, LaneControl);
    Rpi5CrtcWrite(Phy, RPI5_HDMI_PHY_CTL_1, LaneControl);
    Rpi5CrtcWrite(Phy, RPI5_HDMI_PHY_CTL_2, LaneControl);
    Rpi5CrtcWrite(Phy, RPI5_HDMI_PHY_CTL_CK, LaneControl);
    Rpi5CrtcWrite(Phy, RPI5_HDMI_PHY_WORD_SEL, 0);
    Rpi5CrtcWrite(Phy,
                  RPI5_HDMI_PHY_POWERUP_CTL,
                  RPI5_HDMI_PHY_POWERUP_4_LANES);
    Rpi5CrtcWrite(Phy,
                  RPI5_HDMI_PHY_PLL_POWERUP_CTL,
                  RPI5_HDMI_PHY_PLL_POWERUP);
    Value = Rpi5CrtcRead(Phy, RPI5_HDMI_PHY_PLL_RESET_CTL);
    Rpi5CrtcWrite(Phy,
                  RPI5_HDMI_PHY_PLL_RESET_CTL,
                  Value & ~RPI5_HDMI_PHY_PLL_RESETB);
    Value = Rpi5CrtcRead(Phy, RPI5_HDMI_PHY_PLL_RESET_CTL);
    Rpi5CrtcWrite(Phy,
                  RPI5_HDMI_PHY_PLL_RESET_CTL,
                  Value | RPI5_HDMI_PHY_PLL_RESETB);

    KeMemoryBarrier();
    MmUnmapIoSpace(RateManager, RPI5_HDMI0_RM_LENGTH);
    MmUnmapIoSpace(Phy, RPI5_HDMI0_PHY_LENGTH);
    return TRUE;
}

/* Map the selected PixelValve register block once and cache it. */
static PVOID
Rpi5CrtcMapPv(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->PixelValveBase == NULL && DeviceExtension->PixelValveValid && DeviceExtension->PixelValvePhysical.QuadPart != 0)
        DeviceExtension->PixelValveBase = MmMapIoSpace(DeviceExtension->PixelValvePhysical, RPI5_PV_LENGTH, MmNonCached);

    return DeviceExtension->PixelValveBase;
}

static BOOLEAN
Rpi5CrtcProbeVfpLatch(
    _In_ PVOID Base);

BOOLEAN
Rpi5CrtcColdStartHeadless(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID GlobalDvp = NULL;
    PVOID HdmiCore = NULL;
    PVOID HdmiDvp = NULL;
    PVOID HdmiCsc = NULL;
    PVOID Hd = NULL;
    PVOID Pv = NULL;
    ULONG HdmiRate = 0;
    ULONG BvbRate = 0;
    ULONG DisplayState = 0;
    ULONG Reset;
    ULONG Control;
    ULONG VControl;
    ULONG Scheduler;
    ULONG HvsStatus;
    ULONG FrameBefore;
    ULONG FrameAfter;
    ULONG HorizontalBackPorch;
    ULONG HorizontalSync;
    ULONG HorizontalFrontPorch;
    ULONG VerticalBackPorch;
    ULONG VerticalSync;
    ULONG VerticalFrontPorch;
    BOOLEAN Result = FALSE;

    if (!DeviceExtension->Headless ||
        DeviceExtension->ScreenWidth != RPI5_HEADLESS_HDISPLAY ||
        DeviceExtension->ScreenHeight != RPI5_HEADLESS_VDISPLAY ||
        DeviceExtension->MboxBase == NULL)
    {
        return FALSE;
    }

    if (!Rpi5MboxSetClockRate(DeviceExtension,
                              RPI5_MBOX_CLOCK_HDMI,
                              120000000u) ||
        !Rpi5MboxSetClockState(DeviceExtension,
                               RPI5_MBOX_CLOCK_HDMI,
                               TRUE) ||
        !Rpi5MboxSetClockRate(DeviceExtension,
                              RPI5_MBOX_CLOCK_PIXEL_BVB,
                              75000000u) ||
        !Rpi5MboxSetClockState(DeviceExtension,
                               RPI5_MBOX_CLOCK_PIXEL_BVB,
                               TRUE) ||
        !Rpi5MboxSetClockState(DeviceExtension,
                               RPI5_MBOX_CLOCK_DISPLAY,
                               TRUE))
    {
        return FALSE;
    }

    if (!Rpi5MboxGetClockRate(DeviceExtension,
                              RPI5_MBOX_CLOCK_HDMI,
                              &HdmiRate) ||
        !Rpi5MboxGetClockRate(DeviceExtension,
                              RPI5_MBOX_CLOCK_PIXEL_BVB,
                              &BvbRate) ||
        !Rpi5MboxGetClockState(DeviceExtension,
                               RPI5_MBOX_CLOCK_DISPLAY,
                               &DisplayState) ||
        HdmiRate == 0 || BvbRate == 0 || !(DisplayState & 1u))
    {
        return FALSE;
    }

    GlobalDvp = Rpi5CrtcMapRegisters(RPI5_DVP_PHYS, RPI5_DVP_LENGTH);
    HdmiCore = Rpi5CrtcMapRegisters(RPI5_HDMI0_CORE_PHYS,
                                    RPI5_HDMI0_CORE_LENGTH);
    HdmiDvp = Rpi5CrtcMapRegisters(RPI5_HDMI0_DVP_PHYS,
                                   RPI5_HDMI0_DVP_LENGTH);
    HdmiCsc = Rpi5CrtcMapRegisters(RPI5_HDMI0_CSC_PHYS,
                                   RPI5_HDMI0_CSC_LENGTH);
    Hd = Rpi5CrtcMapRegisters(RPI5_HDMI_HD_PHYS, RPI5_HDMI_HD_LENGTH);
    Pv = Rpi5CrtcMapRegisters(RPI5_PV0_PHYS, RPI5_PV_LENGTH);
    if (GlobalDvp == NULL || HdmiCore == NULL || HdmiDvp == NULL ||
        HdmiCsc == NULL || Hd == NULL || Pv == NULL)
    {
        goto Exit;
    }

    Reset = Rpi5CrtcRead(GlobalDvp, RPI5_DVP_SW_INIT);
    Rpi5CrtcWrite(GlobalDvp, RPI5_DVP_SW_INIT,
                  Reset | RPI5_DVP_RESET_HDMI0);
    KeStallExecutionProcessor(1);
    Rpi5CrtcWrite(GlobalDvp, RPI5_DVP_SW_INIT,
                  Reset & ~RPI5_DVP_RESET_HDMI0);

    if (!Rpi5CrtcInitHeadlessPhy())
    {
        goto Exit;
    }

    Rpi5CrtcWrite(Hd, RPI5_HDMI_DVP_CTL, 0);
    Rpi5CrtcWrite(HdmiDvp,
                  RPI5_HDMI_CLOCK_STOP,
                  Rpi5CrtcRead(HdmiDvp, RPI5_HDMI_CLOCK_STOP) |
                      RPI5_HDMI_CLOCK_STOP_PIXEL);

    HorizontalBackPorch = RPI5_HEADLESS_HTOTAL - RPI5_HEADLESS_HSYNC_END;
    HorizontalSync = RPI5_HEADLESS_HSYNC_END - RPI5_HEADLESS_HSYNC_START;
    HorizontalFrontPorch = RPI5_HEADLESS_HSYNC_START - RPI5_HEADLESS_HDISPLAY;
    VerticalBackPorch = RPI5_HEADLESS_VTOTAL - RPI5_HEADLESS_VSYNC_END;
    VerticalSync = RPI5_HEADLESS_VSYNC_END - RPI5_HEADLESS_VSYNC_START;
    VerticalFrontPorch = RPI5_HEADLESS_VSYNC_START - RPI5_HEADLESS_VDISPLAY;

    Scheduler = Rpi5CrtcRead(HdmiCore, RPI5_HDMI_SCHEDULER_CONTROL);
    Rpi5CrtcWrite(HdmiCore,
                  RPI5_HDMI_SCHEDULER_CONTROL,
                  Scheduler | RPI5_HDMI_SCHED_MANUAL |
                      RPI5_HDMI_SCHED_IGNORE_PREDICT);
    Rpi5CrtcWrite(HdmiCore,
                  RPI5_HDMI_HORZA,
                  (HorizontalFrontPorch << 16) |
                      RPI5_HEADLESS_HDISPLAY);
    Rpi5CrtcWrite(HdmiCore,
                  RPI5_HDMI_HORZB,
                  (HorizontalBackPorch << 16) | HorizontalSync);
    Rpi5CrtcWrite(HdmiCore,
                  RPI5_HDMI_VERTA0,
                  (VerticalSync << 24) |
                      (VerticalFrontPorch << 16) |
                      RPI5_HEADLESS_VDISPLAY);
    Rpi5CrtcWrite(HdmiCore,
                  RPI5_HDMI_VERTA1,
                  (VerticalSync << 24) |
                      (VerticalFrontPorch << 16) |
                      RPI5_HEADLESS_VDISPLAY);
    Rpi5CrtcWrite(HdmiCore, RPI5_HDMI_VERTB0, VerticalBackPorch);
    Rpi5CrtcWrite(HdmiCore,
                  RPI5_HDMI_VERTB1,
                  ((RPI5_HEADLESS_HTOTAL >> 1) << 16) |
                      VerticalBackPorch);
    Control = Rpi5CrtcRead(HdmiCore, RPI5_HDMI_DEEP_COLOR_CONFIG);
    Control &= ~(RPI5_HDMI_DEEP_INIT_PHASE_MASK |
                 RPI5_HDMI_DEEP_COLOR_MASK);
    Control |= 2u << 8;
    Rpi5CrtcWrite(HdmiCore, RPI5_HDMI_DEEP_COLOR_CONFIG, Control);

    Control = Rpi5CrtcRead(HdmiCore, RPI5_HDMI_GCP_WORD);
    Control &= ~(RPI5_HDMI_GCP_BYTE1_MASK | RPI5_HDMI_GCP_BYTE0_MASK);
    Control |= RPI5_HDMI_GCP_CLEAR_AVMUTE;
    Rpi5CrtcWrite(HdmiCore, RPI5_HDMI_GCP_WORD, Control);

    Control = Rpi5CrtcRead(HdmiCore, RPI5_HDMI_GCP_CONFIG);
    Rpi5CrtcWrite(HdmiCore,
                  RPI5_HDMI_GCP_CONFIG,
                  Control | RPI5_HDMI_GCP_ENABLE);

    Control = Rpi5CrtcRead(HdmiCore, RPI5_HDMI_MISC_CONTROL);
    Control &= ~RPI5_HDMI_PIXEL_REP_MASK;
    Rpi5CrtcWrite(HdmiCore, RPI5_HDMI_MISC_CONTROL, Control);
    Rpi5CrtcWrite(HdmiDvp, RPI5_HDMI_CLOCK_STOP, 0);

    if (!Rpi5HvsColdStartChannel(DeviceExtension,
                                 RPI5_HEADLESS_HDISPLAY,
                                 RPI5_HEADLESS_VDISPLAY))
    {
        goto Exit;
    }

    Control = Rpi5CrtcRead(Pv, RPI5_PV_CONTROL);
    Rpi5CrtcWrite(Pv, RPI5_PV_CONTROL,
                  Control & ~RPI5_PV_CONTROL_EN);
    Rpi5CrtcWrite(Pv, RPI5_PV_CONTROL,
                  (Control & ~RPI5_PV_CONTROL_EN) |
                      RPI5_PV_CONTROL_FIFO_CLR);
    Rpi5CrtcWrite(Pv, RPI5_PV_HORZA,
                  (HorizontalBackPorch << 16) | HorizontalSync);
    Rpi5CrtcWrite(Pv, RPI5_PV_HORZB,
                  (HorizontalFrontPorch << 16) |
                      RPI5_HEADLESS_HDISPLAY);
    Rpi5CrtcWrite(Pv, RPI5_PV_VERTA,
                  (VerticalBackPorch << 16) | VerticalSync);
    Rpi5CrtcWrite(Pv, RPI5_PV_VERTB,
                  (VerticalFrontPorch << 16) |
                      RPI5_HEADLESS_VDISPLAY);
    Rpi5CrtcWrite(Pv, RPI5_PV_VSYNCD_EVEN, 0);
    VControl = RPI5_PV_VCONTROL_CONTINUOUS |
               RPI5_PV_VCONTROL_ODD_TIMING;
    Rpi5CrtcWrite(Pv, RPI5_PV_V_CONTROL, VControl);
    Rpi5CrtcWrite(Pv, RPI5_PV_MUX_CFG, 8u << 2);
    Rpi5CrtcWrite(Pv, RPI5_PV_PIPE_INIT_CTRL,
                  (1u << 8) | (1u << 4) | 1u);

    Control = (46u << RPI5_PV_CONTROL_FIFO_SHIFT) |
              RPI5_PV_CONTROL_CLR_AT_START |
              RPI5_PV_CONTROL_TRIGGER_UFLOW |
              RPI5_PV_CONTROL_WAIT_HSTART |
              RPI5_PV_CONTROL_FIFO_CLR;
    Rpi5CrtcWrite(Pv, RPI5_PV_CONTROL, Control);
    Rpi5CrtcWrite(Pv, RPI5_PV_CONTROL, Control | RPI5_PV_CONTROL_EN);

    Rpi5CrtcWrite(HdmiDvp, RPI5_HDMI_VEC_INTERFACE_CFG, 0);
    Rpi5CrtcWrite(HdmiDvp, RPI5_HDMI_VEC_INTERFACE_XBAR, 0x354021);
    Rpi5CrtcWrite(HdmiCsc, RPI5_HDMI_CSC_12_11, 0x00002000);
    Rpi5CrtcWrite(HdmiCsc, RPI5_HDMI_CSC_14_13, 0x00000000);
    Rpi5CrtcWrite(HdmiCsc, RPI5_HDMI_CSC_22_21, 0x20000000);
    Rpi5CrtcWrite(HdmiCsc, RPI5_HDMI_CSC_24_23, 0x00000000);
    Rpi5CrtcWrite(HdmiCsc, RPI5_HDMI_CSC_32_31, 0x00000000);
    Rpi5CrtcWrite(HdmiCsc, RPI5_HDMI_CSC_34_33, 0x00002000);
    Rpi5CrtcWrite(HdmiCsc, RPI5_HDMI_CSC_CHANNEL_CTL, 0);
    Rpi5CrtcWrite(HdmiCsc,
                  RPI5_HDMI_CSC_CTL,
                  RPI5_HDMI_CSC_ENABLE | RPI5_HDMI_CSC_CUSTOM);
    Rpi5CrtcWrite(HdmiCore, RPI5_HDMI_FIFO_CTL, RPI5_HDMI_FIFO_MASTER);

    Rpi5CrtcWrite(Pv,
                  RPI5_PV_V_CONTROL,
                  VControl | RPI5_PV_VCONTROL_VIDEN);
    Control = RPI5_HDMI_VIDEO_ENABLE |
              RPI5_HDMI_VIDEO_UNDERFLOW |
              RPI5_HDMI_VIDEO_FRAME_RESET |
              RPI5_HDMI_VIDEO_VSYNC_LOW |
              RPI5_HDMI_VIDEO_HSYNC_LOW |
              RPI5_HDMI_VIDEO_CLRRGB |
              RPI5_HDMI_VIDEO_BLANK_INSERT;
    Rpi5CrtcWrite(Hd, RPI5_HDMI_VIDEO_CONTROL, Control);
    Rpi5CrtcWrite(Hd,
                  RPI5_HDMI_VIDEO_CONTROL,
                  Rpi5CrtcRead(Hd, RPI5_HDMI_VIDEO_CONTROL) &
                      ~RPI5_HDMI_VIDEO_BLANKPIX);
    Rpi5CrtcWrite(HdmiCore,
                  RPI5_HDMI_SCHEDULER_CONTROL,
                  Rpi5CrtcRead(HdmiCore, RPI5_HDMI_SCHEDULER_CONTROL) |
                      RPI5_HDMI_SCHED_MODE_HDMI);

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    FrameBefore = Rpi5CrtcRead(Hd, RPI5_HDMI_FRAME_COUNT);
    KeStallExecutionProcessor(20000);
    FrameAfter = Rpi5CrtcRead(Hd, RPI5_HDMI_FRAME_COUNT);
    Scheduler = Rpi5CrtcRead(HdmiCore, RPI5_HDMI_SCHEDULER_CONTROL);
    HvsStatus = Rpi5CrtcRead(DeviceExtension->HvsBase,
                             RPI5_HVS_D0_STATUS);
    Control = Rpi5CrtcRead(Pv, RPI5_PV_CONTROL);
    VControl = Rpi5CrtcRead(Pv, RPI5_PV_V_CONTROL);

    if ((HvsStatus & RPI5_HVS_D0_STATUS_MODE_MASK) ==
            RPI5_HVS_D0_STATUS_MODE_RUN &&
        (Scheduler & RPI5_HDMI_SCHED_ACTIVE) &&
        FrameBefore != FrameAfter)
    {
        DeviceExtension->PixelValveBase = Pv;
        DeviceExtension->PixelValveValid = TRUE;
        DeviceExtension->PixelValveVBlankLive = TRUE;
        DeviceExtension->PixelValveIndex = 0;
        DeviceExtension->PixelValvePhysical.QuadPart = RPI5_PV0_PHYS;
        DeviceExtension->PixelValveControl = Control;
        DeviceExtension->PixelValveVControl = VControl;
        DeviceExtension->PixelValveVsyncEven = 0;
        DeviceExtension->PixelValveHorzA =
            (HorizontalBackPorch << 16) | HorizontalSync;
        DeviceExtension->PixelValveHorzB =
            (HorizontalFrontPorch << 16) | RPI5_HEADLESS_HDISPLAY;
        DeviceExtension->PixelValveVertA =
            (VerticalBackPorch << 16) | VerticalSync;
        DeviceExtension->PixelValveVertB =
            (VerticalFrontPorch << 16) | RPI5_HEADLESS_VDISPLAY;
        DeviceExtension->PixelValveHactAct = 0;
        DeviceExtension->PvVBlankBroken = FALSE;
        Pv = NULL;
        Result = TRUE;
    }

Exit:
    if (Pv != NULL)
        MmUnmapIoSpace(Pv, RPI5_PV_LENGTH);
    if (Hd != NULL)
        MmUnmapIoSpace(Hd, RPI5_HDMI_HD_LENGTH);
    if (HdmiCsc != NULL)
        MmUnmapIoSpace(HdmiCsc, RPI5_HDMI0_CSC_LENGTH);
    if (HdmiDvp != NULL)
        MmUnmapIoSpace(HdmiDvp, RPI5_HDMI0_DVP_LENGTH);
    if (HdmiCore != NULL)
        MmUnmapIoSpace(HdmiCore, RPI5_HDMI0_CORE_LENGTH);
    if (GlobalDvp != NULL)
        MmUnmapIoSpace(GlobalDvp, RPI5_DVP_LENGTH);
    return (BOOLEAN)Result;
}

static BOOLEAN
Rpi5CrtcReportPv(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG PvPhys,
    _In_ ULONG Index)
{
    PHYSICAL_ADDRESS Phys;
    PVOID Base;
    ULONG Control, VControl, HorzA, HorzB, VertA, VertB;
    BOOLEAN Enabled, VBlankLive;

    Phys.QuadPart = PvPhys;
    Base = MmMapIoSpace(Phys, RPI5_PV_LENGTH, MmNonCached);
    if (Base == NULL)
    {
        DbgPrint("RPI5VC4: PV%lu map failed\n", Index);
        return FALSE;
    }

    Control  = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_CONTROL));
    VControl = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_V_CONTROL));
    HorzA    = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HORZA));
    HorzB    = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HORZB));
    VertA    = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VERTA));
    VertB    = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VERTB));
    Enabled  = (Control & RPI5_PV_CONTROL_EN) && (VControl & RPI5_PV_VCONTROL_VIDEN);

    if (Enabled)
    {
        BOOLEAN MatchesMode;
        BOOLEAN CurrentMatchesMode;
        BOOLEAN PreferThisValve =
            !DeviceExtension->PixelValveValid;

        VBlankLive = Rpi5CrtcProbeVfpLatch(Base);
        MatchesMode = RPI5_PV_LO16(VertB) == DeviceExtension->ScreenHeight;
        CurrentMatchesMode =
            DeviceExtension->PixelValveValid &&
            RPI5_PV_LO16(DeviceExtension->PixelValveVertB) ==
                DeviceExtension->ScreenHeight;
        if (!PreferThisValve &&
            ((VBlankLive && !DeviceExtension->PixelValveVBlankLive) ||
             (VBlankLive == DeviceExtension->PixelValveVBlankLive &&
              MatchesMode && !CurrentMatchesMode)))
        {
            PreferThisValve = TRUE;
        }

        if (PreferThisValve)
        {
            DeviceExtension->PixelValveValid = TRUE;
            DeviceExtension->PixelValveVBlankLive = VBlankLive;
            DeviceExtension->PixelValveIndex = Index;
            DeviceExtension->PixelValvePhysical.QuadPart = PvPhys;
            DeviceExtension->PixelValveControl = Control;
            DeviceExtension->PixelValveVControl = VControl;
            DeviceExtension->PixelValveVsyncEven =
                READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VSYNCD_EVEN));
            DeviceExtension->PixelValveHorzA = HorzA;
            DeviceExtension->PixelValveHorzB = HorzB;
            DeviceExtension->PixelValveVertA = VertA;
            DeviceExtension->PixelValveVertB = VertB;
            DeviceExtension->PixelValveHactAct =
                READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HACT_ACT));
        }
    }

    MmUnmapIoSpace(Base, RPI5_PV_LENGTH);
    return Enabled;
}

BOOLEAN
Rpi5CrtcReportTiming(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    BOOLEAN Found;

    if (DeviceExtension->Headless)
        return FALSE;

    DeviceExtension->PixelValveValid = FALSE;
    DeviceExtension->PixelValveVBlankLive = FALSE;

    Found  = Rpi5CrtcReportPv(DeviceExtension, RPI5_PV0_PHYS, 0);
    Found |= Rpi5CrtcReportPv(DeviceExtension, RPI5_PV1_PHYS, 1);
    DeviceExtension->PvVBlankBroken =
        !DeviceExtension->PixelValveVBlankLive;
    if (!Found)
        DbgPrint("RPI5VC4: no enabled PixelValve found (firmware HDMI off?)\n");
    return Found;
}

BOOLEAN
Rpi5CrtcProgramCurrentTiming(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID Base;

    if (DeviceExtension->Headless)
        return FALSE;
    ULONG Control, VControl;

    Base = Rpi5CrtcMapPv(DeviceExtension);
    if (Base == NULL)
        return FALSE;

    Control = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_CONTROL));
    VControl = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_V_CONTROL));
    if (!(Control & RPI5_PV_CONTROL_EN) ||
        !(VControl & RPI5_PV_VCONTROL_VIDEN))
    {
        return FALSE;
    }

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VSYNCD_EVEN),
                         DeviceExtension->PixelValveVsyncEven);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HORZA),
                         DeviceExtension->PixelValveHorzA);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HORZB),
                         DeviceExtension->PixelValveHorzB);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VERTA),
                         DeviceExtension->PixelValveVertA);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VERTB),
                         DeviceExtension->PixelValveVertB);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HACT_ACT),
                         DeviceExtension->PixelValveHactAct);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_V_CONTROL),
                         DeviceExtension->PixelValveVControl);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_CONTROL),
                         DeviceExtension->PixelValveControl);

#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#endif
    KeMemoryBarrier();

    if (READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HORZA)) !=
            DeviceExtension->PixelValveHorzA ||
        READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_HORZB)) !=
            DeviceExtension->PixelValveHorzB ||
        READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VERTA)) !=
            DeviceExtension->PixelValveVertA ||
        READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_VERTB)) !=
            DeviceExtension->PixelValveVertB ||
        READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_V_CONTROL)) !=
            DeviceExtension->PixelValveVControl ||
        READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_CONTROL)) !=
            DeviceExtension->PixelValveControl)
    {
        return FALSE;
    }

    return TRUE;
}

/*
 * INTSTAT only latches interrupt sources enabled in INTEN (the GIC side of
 * the PV line stays disabled — this is a pure status latch for polling, the
 * same arming Linux vc4_crtc does before it reads VFP_START).
 */
static VOID
Rpi5CrtcArmVfpLatch(
    _In_ PVOID Base)
{
    ULONG IntEn = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTEN));

    if (!(IntEn & RPI5_PV_INT_VFP_START))
    {
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTEN),
                             IntEn | RPI5_PV_INT_VFP_START);
    }
}

static BOOLEAN
Rpi5CrtcProbeVfpLatch(
    _In_ PVOID Base)
{
    ULONG ElapsedUs;

    Rpi5CrtcArmVfpLatch(Base);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTSTAT),
                         RPI5_PV_INT_VFP_START);

    for (ElapsedUs = 0;
         ElapsedUs < RPI5_PV_VBLANK_TIMEOUT_US;
         ElapsedUs += RPI5_PV_VBLANK_POLL_US)
    {
        if (READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTSTAT)) &
            RPI5_PV_INT_VFP_START)
        {
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTSTAT),
                                 RPI5_PV_INT_VFP_START);
            return TRUE;
        }

        KeStallExecutionProcessor(RPI5_PV_VBLANK_POLL_US);
    }

    return FALSE;
}

BOOLEAN
Rpi5CrtcVBlankSeen(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID Base;

    if (DeviceExtension->PvVBlankBroken)
        return TRUE;

    Base = Rpi5CrtcMapPv(DeviceExtension);
    if (Base == NULL)
        return TRUE;

    Rpi5CrtcArmVfpLatch(Base);

    if (READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTSTAT)) &
        RPI5_PV_INT_VFP_START)
    {
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTSTAT),
                             RPI5_PV_INT_VFP_START);
        return TRUE;
    }

    return FALSE;
}

static BOOLEAN
Rpi5CrtcWaitForVBlankLocked(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    PVOID Base;
    ULONG Control, VControl;
    ULONG ElapsedUs;

    if (DeviceExtension->PvVBlankBroken)
        return FALSE;

    Base = Rpi5CrtcMapPv(DeviceExtension);
    if (Base == NULL)
        return FALSE;

    Control = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_CONTROL));
    VControl = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_V_CONTROL));
    if (!(Control & RPI5_PV_CONTROL_EN) ||
        !(VControl & RPI5_PV_VCONTROL_VIDEN))
    {
        return FALSE;
    }

    Rpi5CrtcArmVfpLatch(Base);

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTSTAT),
                         RPI5_PV_INT_VFP_START);

    for (ElapsedUs = 0;
         ElapsedUs < RPI5_PV_VBLANK_TIMEOUT_US;
         ElapsedUs += RPI5_PV_VBLANK_POLL_US)
    {
        if (READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTSTAT)) &
            RPI5_PV_INT_VFP_START)
        {
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + RPI5_PV_INTSTAT),
                                 RPI5_PV_INT_VFP_START);
            return TRUE;
        }

        KeStallExecutionProcessor(RPI5_PV_VBLANK_POLL_US);
    }

    /* VFP latch never fired. Keep future flips from repeating the timeout;
     * tear avoidance is optional and presentation still proceeds. */
    DeviceExtension->PvVBlankBroken = TRUE;
    return FALSE;
}

BOOLEAN
Rpi5CrtcWaitForVBlank(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    BOOLEAN Result;

    /* Waiters share the VFP latch without blocking cursor updates. */
    ExAcquireFastMutex(&DeviceExtension->VBlankMutex);
    Result = Rpi5CrtcWaitForVBlankLocked(DeviceExtension);
    ExReleaseFastMutex(&DeviceExtension->VBlankMutex);
    return Result;
}
