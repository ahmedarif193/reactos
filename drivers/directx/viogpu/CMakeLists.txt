# Genuine Red Hat VirtIO GPU WDDM Display-Only Driver (viogpudo.sys), shipped
# AS-IS from virtio-win (NOT recompiled). The prebuilt ARM64 binary + its inf
# are placed verbatim onto the boot media; PnP loads it for the QEMU virtio-gpu
# PCI device via viogpudo_reg.inf (CriticalDeviceDatabase). The goal is to run a
# real Windows WDDM driver on ReactOS' dxgkrnl unchanged.
#
# Source: virtio-win.iso viogpudo/w11(==2k25)/ARM64/, md5 de615259c5a259be6089490cd6b2fc75

add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/viogpudo.sys
            DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/viogpudo.inf
            DESTINATION reactos/inf NO_CAB FOR all)

add_registry_inf(viogpudo_reg.inf)
