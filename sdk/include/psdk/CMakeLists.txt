
add_definitions(-D_MIDL_USE_GUIDDEF_)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

list(APPEND SOURCE
#   access.idl
    activation.idl
    activaut.idl
    activdbg.idl
    activscp.idl
    asyncinfo.idl
    asptlb.idl
#   asynot.idl
#   asysta.idl
    atliface.idl
    audioclient.idl
    audiopolicy.idl
    bdaiface.idl
#   binres.idl
    bits.idl
    bits1_5.idl
    bits2_0.idl
    bits2_5.idl
    bits3_0.idl
    bits5_0.idl
#   cmdbas.idl
#   cmdtxt.idl
    comcat.idl
    comsvcs.idl
    commoncontrols.idl
    control.idl
    cor.idl
    cordebug.idl
#   crtrow.idl
    ctffunc.idl
    ctxtcall.idl
#   dbccmd.idl
#   dbcses.idl
#   dbdsad.idl
#   dbinit.idl
#   dbprop.idl
#   dbs.idl
    devenum.idl
    devicetopology.idl
    dimm.idl
    dispex.idl
    docobj.idl
    docobjectservice.idl
    downloadmgr.idl
    drmexternals.idl
    d2d1.idl
    d2d1_1.idl
    d2d1_2.idl
    d2d1_3.idl
    d2d1effectauthor.idl
    d2d1effects.idl
    d2d1effects_1.idl
    d2d1effects_2.idl
    dwrite.idl
    dwrite_1.idl
    dwrite_2.idl
    dwrite_3.idl
    dvdif.idl
    dxgiformat.idl
#   dyngraph.idl
    emptyvc.idl
    endpointvolume.idl
    evr.idl
    eventtoken.idl
    exdisp.idl
    fusion.idl
    gameinput.idl
    hlink.idl
    hstring.idl
    htiface.idl
    htiframe.idl
    httprequest.idl
    iads.idl
    icftypes.idl
    ieautomation.idl
    imnact.idl
    imnxport.idl
    indexsrv.idl
    inputscope.idl
    inseng.idl
    inspectable.idl
    ivectorchangedeventargs.idl
    mediaobj.idl
    metahost.idl
    mfidl.idl
    mfobjects.idl
    mftransform.idl
    mimeinfo.idl
    mimeole.idl
    mlang.idl
    mmdeviceapi.idl
    msaatext.idl
    mscoree.idl
    msctf.idl
    msdadc.idl
    mshtmhst.idl
    mshtml.idl
    msinkaut.idl
    mstask.idl
    msxml.idl
    msxml2.idl
    msxml6.idl
    natupnp.idl
    netcon.idl
    netlistmgr.idl
    netfw.idl
    oaidl.idl
    objectarray.idl
    objidl.idl
    objidlbase.idl
    objsafe.idl
    ocidl.idl
    ocmm.idl
    oleacc.idl
    oledb.idl
    oleidl.idl
#   opnrst.idl
    optary.idl
    perhist.idl
    propidl.idl
    propsys.idl
    pstore.idl
    restrictederrorinfo.idl
    richole.idl
    roparameterizediid.idl
#   row.idl
#   rowchg.idl
#   rstbas.idl
#   rstinf.idl
#   rstloc.idl
    sensevts.idl
    servprov.idl
#   sesprp.idl
    shdeprecated.idl
    shimgdata.idl
    shldisp.idl
    shobjidl.idl
    shtypes.idl
    spatialaudioclient.idl
#   stdole2.idl
    strmif.idl
    structuredquerycondition.idl
    taskschd.idl
    textstor.idl
    tlogstg.idl
    tom.idl
    transact.idl
    tuner.idl
    txcoord.idl
    txdtc.idl
    uiautomationclient.idl
    uiautomationcore.idl
    unknwn.idl
    urlhist.idl
    urlmon.idl
    vmr9.idl
#   vmrender.idl
    vsbackup.idl
    vss.idl
    vswriter.idl
    wbemcli.idl
    wbemdisp.idl
    wbemprov.idl
    weakreference.idl
    wia_lh.idl
    wia_xp.idl
    windows.devices.geolocation.idl
    windows.devices.haptics.idl
    windows.devices.power.idl
    windows.foundation.collections.idl
    windows.foundation.idl
    windows.foundation.numerics.idl
    windows.gaming.input.custom.idl
    windows.gaming.input.forcefeedback.idl
    windows.gaming.input.idl
    windows.storage.fileproperties.idl
    windows.storage.idl
    windows.storage.search.idl
    windows.storage.streams.idl
    windows.system.idl
    windows.system.power.idl
    windowscontracts.idl
    wincodec.idl
    wincodecsdk.idl
    winsxs.idl
    wmdrmsdk.idl
    wmiutils.idl
    wmsbuffer.idl
    wmsdkidl.idl
    wtypes.idl
    wuapi.idl
    xmldom.idl
    xmldso.idl
    ctfutb.idl
    xmllite.idl)

add_idl_headers(psdk ${SOURCE})
add_dependencies(psdk xdk)
add_typelib(stdole2.idl)
add_custom_target(stdole2 DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/stdole2.tlb)

add_idl_headers(d3d_idl_headers
    dcommon.idl
    d3dcommon.idl
    d3d9on12.idl
    d3d10.idl
    d3d10_1.idl
    d3d10effect.idl
    d3d10sdklayers.idl
    d3d10shader.idl
    d3d11.idl
    d3d11_1.idl
    d3d11_2.idl
    d3d11_3.idl
    d3d11_4.idl
    d3d11on12.idl
    d3d11sdklayers.idl
    d3d12.idl
    d3d12sdklayers.idl
    d3d12shader.idl
    dxgi.idl
    dxgi1_2.idl
    dxgi1_3.idl
    dxgi1_4.idl
    dxgi1_5.idl
    dxgi1_6.idl
    dxgicommon.idl
    dxgidebug.idl
    dxgitype.idl)
add_dependencies(psdk d3d_idl_headers)

add_iid_library(adsiid iads.idl)
add_iid_library(wuguid wuapi.idl)
add_iid_library(xml_uuids msxml2.idl)

generate_idl_iids(wincodec.idl)
