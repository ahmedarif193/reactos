<!-- Copyright 2026 Ahmed Arif <arif193@gmail.com> -->

# Raspberry Pi 3 Windows 10 driver parity

`rpi3winsync/` preserves the files and directory layout from the
[`raspberrypi/windows-drivers`](https://github.com/raspberrypi/windows-drivers)
source snapshot from commit `88ee238c9debecce810d208cac1e5f36add3d2a1`,
tracked directly in the ReactOS repository.
That commit was upstream `master` when the snapshot was imported.
ReactOS adds adjacent `CMakeLists.txt` files that compile those synchronized
sources in place and headers below `reactos-compat/`. Necessary source-level
ABI adaptations must preserve the native path and use `__REACTOS__` guards.

Select `Target platform -> Raspberry Pi 3` in `menuconfig` or configure with
`-DROSCONFIG_PROFILE=rpi3`. The profile enables the common Pi boot payload,
Pi 3-specific boot bindings, current native Pi 3 targets, and validates this
source snapshot.

Every upstream Pi 3 driver project now has an adjacent ReactOS `CMakeLists.txt`.
The KMDF class extensions they depend on (`msgpioclx`, `spbcx`, `sercx2`) live
in `sdk/lib/drivers/wdf/cx` and are loaded on demand by `wdfldr` through the
`Control\Wdf\Kmdf\<Class>\Versions` registry routing.

## Sync policy

- Keep synchronized source edits minimal. Put ReactOS-only additions below
  `#ifdef __REACTOS__` and retain replaced native code below
  `#ifndef __REACTOS__`.
- Update the directly tracked snapshot from a reviewed upstream commit and
  record that commit in `media/doc/3rd Party Files.txt`. Review a snapshot
  update separately from the ReactOS compatibility delta.
- Put reusable Windows-contract work in ReactOS WDM, KMDF, PortCls, SDPORT, or
  future class-extension layers. Keep WPP generated-header substitutes and INF
  build metadata in the matching `reactos-compat/` subdirectory.
- A source listed here is a compatibility reference, not a claim that its
  Windows binary can run on ReactOS unchanged.

The upstream repository targets Windows 10 IoT Core and includes both 32-bit
and ARM64 Pi 3 configurations. Its root and per-driver license files remain
authoritative for the synced source.

## Parity map

| Pi 3 function | Windows 10 reference | ReactOS state |
| --- | --- | --- |
| Arasan SD/SDIO | `drivers/sd/bcm2836/bcm2836sdhc` (`sdport`) | Native `sdbus` BCM2847 backend; active in the profile. Miniport `bcm2836sdhc` runs on the Windows `sdport.h` miniport contract (ReactOS `sdport` speaks the Windows two-phase request protocol to it); `sdbus` keeps the boot controller by DriverVer ranking |
| Broadcom SDHost | `drivers/sd/bcm2836/rpisdhc` (`sdport`) | Native `sdbus` BCM2855 backend; active in the profile. Miniport `rpisdhc` runs on the same Windows `sdport` contract |
| DWC2 USB host | Not published in this repository | Native `usbdwc2`; active and boot-start only in the Pi 3 profile |
| LAN951x Ethernet | No NIC miniport in this repository | Native USB `smsc95xx` behind DWC2; active in the profile |
| CYW43430 Wi-Fi | Firmware/BSP integration, not a matching driver source here | Native `cyw43455sdio` plus `cyw43455`; active in the profile |
| GPIO | `drivers/gpio/bcm2836` | Built as `bcmgpio` on the ReactOS `msgpioclx` class extension (controller PnP/power, bank interrupts, IO-pin connections); GpioInt consumers get virtual GSIVs (`HalAllocateSecondaryInterrupt`, INTID 4096+) that `msgpioclx` registers with the HAL and dispatches from the bank ISR |
| I2C | `drivers/i2c/bcm2836` | Built as `bcmi2c` on the ReactOS `spbcx` class extension; hardware validation pending |
| SPI and AUX SPI | `drivers/spi/bcm2836`, `drivers/spi/bcmauxspi` | Built as `bcmspi` and `bcmauxspi` on the ReactOS `spbcx` class extension; hardware validation pending |
| PL011 UART | `drivers/uart/bcm2836/serPL011` | Built as `SerPL011` on the ReactOS `sercx2` class extension (PIO transmit/receive, wait mask, timeouts, purge); hardware validation pending |
| Mini UART | `drivers/uart/bcm2836/miniUart` | Built as `pi_miniuart` with ReactOS source guards; hardware validation pending |
| PWM | `drivers/pwm/bcm2836` | Built as `bcm2836pwm`; DMA/interrupt hardware validation pending |
| Mailbox | `drivers/mailbox/bcm2836` | Built unchanged as `rpiq`; hardware/runtime validation pending |
| VCHIQ | `drivers/misc/vchiq` | Build adapters present for the driver and kernel DLLs; firmware/runtime validation pending |
| WaveRT audio | `drivers/audio/bcm2836` | Reference pending PortCls/WaveRT and mailbox/VCHIQ parity |

`drivers/RpiLanPropertyChange/bcm2836` is a user-mode configuration service,
not the Raspberry Pi 3 Ethernet driver. Pi 4-only `bcm2711` sources are also
outside this profile's parity scope even though they remain in the exact
upstream snapshot.

The porting loop is contract-first: build one selected upstream driver, record
the first missing or incompatible Windows contract, improve that ReactOS
subsystem, and keep only the smallest device-specific delta needed afterward.
