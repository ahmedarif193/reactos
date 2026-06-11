
# ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
#
# Self-contained static library: crypto primitives and the IEEE 802.11i
# 4-Way Handshake state machine, no NDIS/driver/socket dependency.
# test_rsna.c is a host-only test harness and is not part of this build.

list(APPEND SOURCE
    rsna_sha1.c
    rsna_md5.c
    rsna_hmac.c
    rsna_kdf.c
    rsna_aes.c
    rsna_keywrap.c
    rsna_rc4.c
    rsna_supplicant.c)

add_library(rsna_supplicant ${SOURCE})

add_dependencies(rsna_supplicant xdk)
