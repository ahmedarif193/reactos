<!-- Copyright 2026 Ahmed Arif <arif193@gmail.com> -->

# Raspberry Pi 3 Windows 10 driver parity

`rpi3winsync/` is the unmodified
[`raspberrypi/windows-drivers`](https://github.com/raspberrypi/windows-drivers)
source snapshot from commit `88ee238c9debecce810d208cac1e5f36add3d2a1`,
tracked directly in the ReactOS repository.
That commit was upstream `master` when the snapshot was imported.

Select `Target platform -> Raspberry Pi 3` in `menuconfig` or configure with
`-DROSCONFIG_PROFILE=rpi3`. The profile enables the common Pi boot payload,
Pi 3-specific boot bindings, current native Pi 3 targets, and validates this
source snapshot.

The existing upstream Pi 3 driver projects are exposed by the
`rpi3winsync` source target, with their prospective build entries kept
disabled in `rpi3winsync.cmake`. Each disabled project has an adjacent comment
naming the ReactOS framework contract that must be implemented first.

## Sync policy

- Never edit files below `rpi3winsync/` as part of a ReactOS compatibility
  change.
- Update the directly tracked snapshot from a reviewed upstream commit and
  record that commit in `media/doc/3rd Party Files.txt`. Review a snapshot
  update separately from the ReactOS compatibility delta.
- Put reusable Windows-contract work in ReactOS WDM, KMDF, PortCls, SDPORT, or
  future class-extension layers. Keep unavoidable device-specific adaptation
  in ReactOS-owned files outside `rpi3winsync/`.
- A source listed here is a compatibility reference, not a claim that its
  Windows binary can run on ReactOS unchanged.

The upstream repository targets Windows 10 IoT Core and includes both 32-bit
and ARM64 Pi 3 configurations. Its root and per-driver license files remain
authoritative for the synced source.

## Parity map

| Pi 3 function | Windows 10 reference | ReactOS state |
| --- | --- | --- |
| Arasan SD/SDIO | `drivers/sd/bcm2836/bcm2836sdhc` (`sdport`) | Native `sdbus` BCM2847 backend; active in the profile |
| Broadcom SDHost | `drivers/sd/bcm2836/rpisdhc` (`sdport`) | Native `sdbus` BCM2855 backend; active in the profile |
| DWC2 USB host | Not published in this repository | Native `usbdwc2`; active and boot-start only in the Pi 3 profile |
| LAN951x Ethernet | No NIC miniport in this repository | Native USB `smsc95xx` behind DWC2; active in the profile |
| CYW43430 Wi-Fi | Firmware/BSP integration, not a matching driver source here | Native `cyw43455sdio` plus `cyw43455`; active in the profile |
| GPIO | `drivers/gpio/bcm2836` | Reference pending ReactOS `GpioClx` compatibility |
| I2C | `drivers/i2c/bcm2836` | Reference pending ReactOS `SpbCx` compatibility |
| SPI and AUX SPI | `drivers/spi/bcm2836`, `drivers/spi/bcmauxspi` | Reference pending ReactOS `SpbCx` compatibility |
| PL011 UART | `drivers/uart/bcm2836/serPL011` | Reference pending ReactOS `SerCx2` compatibility |
| Mini UART | `drivers/uart/bcm2836/miniUart` | Reference; KMDF/serial contract audit pending |
| PWM | `drivers/pwm/bcm2836` | Reference; KMDF/DMA contract audit pending |
| Mailbox | `drivers/mailbox/bcm2836` | Reference; KMDF and IOCTL contract audit pending |
| VCHIQ | `drivers/misc/vchiq` | Reference; KMDF and user/kernel ABI audit pending |
| WaveRT audio | `drivers/audio/bcm2836` | Reference pending PortCls/WaveRT and mailbox/VCHIQ parity |

`drivers/RpiLanPropertyChange/bcm2836` is a user-mode configuration service,
not the Raspberry Pi 3 Ethernet driver. Pi 4-only `bcm2711` sources are also
outside this profile's parity scope even though they remain in the exact
upstream snapshot.

The porting loop is contract-first: build one selected upstream driver, record
the first missing or incompatible Windows contract, improve that ReactOS
subsystem, and keep only the smallest device-specific delta needed afterward.
