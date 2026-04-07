add_subdirectory(usbccgp)
add_subdirectory(usbd)
add_subdirectory(usbehci)
add_subdirectory(usbhub)
add_subdirectory(usbohci)
add_subdirectory(usbport)
# usbrndis requires NDIS 6 (NET_BUFFER_LIST datapath, miniport driver model
# from drivers/network/ndis/ndis/60stubs.c). It is only built when the
# target NT level is Vista (0x600) or higher.
math(EXPR _USBRNDIS_NT_NUM "${REACTOS_TARGET_NT} + 0")
if(_USBRNDIS_NT_NUM GREATER_EQUAL 1536)
    # 1536 == 0x600 == Vista
    add_subdirectory(usbrndis)
endif()
add_subdirectory(usbstor)
#add_subdirectory(usbstor_new)
add_subdirectory(usbuhci)
