
# dev-nt6-1 branch test drivers for the new NDIS 6 / WFP / WSK stubs.
# Build as regular kernel drivers; they link-test the new surfaces but
# don't actually process packets.

add_subdirectory(ndis6miniport)
add_subdirectory(wfpcallout)
add_subdirectory(wskclient)
