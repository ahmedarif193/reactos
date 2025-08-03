@ stdcall -private DllCanUnloadNow()
@ stdcall -private DllGetClassObject(ptr ptr ptr)
@ stdcall -private DllRegisterServer()
@ stdcall -private DllUnregisterServer()
@ stdcall WinHttpAddRequestHeaders(ptr wstr long long)
@ stdcall WinHttpCheckPlatform()
@ stdcall WinHttpCloseHandle(ptr)
@ stdcall WinHttpConnect(ptr wstr long long)
@ stdcall WinHttpCrackUrl(wstr long long ptr)
@ stdcall WinHttpCreateUrl(ptr long ptr ptr)
@ stdcall WinHttpDetectAutoProxyConfigUrl(long ptr)
@ stdcall WinHttpGetDefaultProxyConfiguration(ptr)
@ stdcall WinHttpGetIEProxyConfigForCurrentUser(ptr)
@ stdcall WinHttpGetProxyForUrl(ptr wstr ptr ptr)
@ stdcall WinHttpOpen(wstr long wstr wstr long)
@ stdcall WinHttpOpenRequest(ptr wstr wstr wstr wstr ptr long)
@ stdcall WinHttpQueryAuthSchemes(ptr ptr ptr ptr)
@ stdcall WinHttpQueryDataAvailable(ptr ptr)
@ stdcall WinHttpQueryHeaders(ptr long wstr ptr ptr ptr)
@ stdcall WinHttpQueryOption(ptr long ptr ptr)
@ stdcall WinHttpReadData(ptr ptr long ptr)
@ stdcall WinHttpReceiveResponse(ptr ptr)
@ stdcall WinHttpSendRequest(ptr wstr long ptr long long long)
@ stdcall WinHttpSetCredentials(ptr long long wstr ptr ptr)
@ stdcall WinHttpSetDefaultProxyConfiguration(ptr)
@ stdcall WinHttpSetOption(ptr long ptr long)
@ stdcall WinHttpSetStatusCallback(ptr ptr long long)
@ stdcall WinHttpSetTimeouts(ptr long long long long)
@ stdcall WinHttpTimeFromSystemTime(ptr ptr)
@ stdcall WinHttpTimeToSystemTime(wstr ptr)
@ stdcall WinHttpWriteData(ptr ptr long ptr)

# Windows 10 WinHTTP Advanced Features
@ stdcall -stub -version=0x600+ WinHttpWebSocketCompleteUpgrade(ptr long)
@ stdcall -stub -version=0x600+ WinHttpWebSocketSend(ptr long ptr long)
@ stdcall -stub -version=0x600+ WinHttpWebSocketReceive(ptr ptr long ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpWebSocketShutdown(ptr long ptr long)
@ stdcall -stub -version=0x600+ WinHttpWebSocketClose(ptr long ptr long)
@ stdcall -stub -version=0x600+ WinHttpWebSocketQueryCloseStatus(ptr ptr ptr long ptr)

# HTTP/2 and Modern Protocol Support
@ stdcall -stub -version=0x600+ WinHttpQueryConnectionGroup(ptr ptr ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpSetConnectionGroup(ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpQueryHeadersEx(ptr long wstr ptr ptr ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpAddRequestHeadersEx(ptr long wstr ptr)

# Enhanced SSL/TLS and Security
@ stdcall -stub -version=0x600+ WinHttpSetSecurityInfo(ptr long ptr)
@ stdcall -stub -version=0x600+ WinHttpQuerySecurityInfo(ptr long ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpSetClientCertificate(ptr ptr long)
@ stdcall -stub -version=0x600+ WinHttpQueryClientCertificate(ptr ptr ptr)

# Advanced Proxy and Authentication
@ stdcall -stub -version=0x600+ WinHttpGetProxyForUrlEx(ptr wstr ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpGetProxyForUrlEx2(ptr wstr ptr ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpCreateProxyResolver(ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpGetProxyResult(ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpGetProxyResultEx(ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpResetAutoProxy(ptr long)
@ stdcall -stub -version=0x600+ WinHttpGetTunnelSocket(ptr ptr)

# Stream and Data Transfer Enhancements
@ stdcall -stub -version=0x600+ WinHttpReadDataEx(ptr ptr long ptr long ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpWriteDataEx(ptr ptr long ptr long ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpStreamedUpload(ptr ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpStreamedDownload(ptr ptr ptr)

# Connection Management
@ stdcall -stub -version=0x600+ WinHttpSetSessionProperty(ptr long ptr long)
@ stdcall -stub -version=0x600+ WinHttpQuerySessionProperty(ptr long ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpSetRequestProperty(ptr long ptr long)
@ stdcall -stub -version=0x600+ WinHttpQueryRequestProperty(ptr long ptr ptr)

# Enhanced Cookie and State Management
@ stdcall -stub -version=0x600+ WinHttpQueryCookies(ptr wstr ptr ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpAddCookies(ptr wstr long)
@ stdcall -stub -version=0x600+ WinHttpSetCookieJar(ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpGetCookieJar(ptr ptr ptr)

# Performance and Diagnostics
@ stdcall -stub -version=0x600+ WinHttpQueryPerformanceCounter(ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpQueryStatistics(ptr long ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpSetTracing(ptr long ptr)
@ stdcall -stub -version=0x600+ WinHttpGetTracing(ptr ptr ptr ptr)

# Modern Content Encoding
@ stdcall -stub -version=0x600+ WinHttpSetDecompressionLevel(ptr long)
@ stdcall -stub -version=0x600+ WinHttpQueryCompressionLevel(ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpSetContentEncoding(ptr wstr)
@ stdcall -stub -version=0x600+ WinHttpQueryContentEncoding(ptr ptr ptr)

# IPv6 and Network Stack Integration
@ stdcall -stub -version=0x600+ WinHttpSetNetworkInterface(ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpQueryNetworkInterface(ptr ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpSetAddressFamily(ptr long)
@ stdcall -stub -version=0x600+ WinHttpQueryAddressFamily(ptr ptr)

# Extended Error and Status Information
@ stdcall -stub -version=0x600+ WinHttpQueryExtendedError(ptr ptr ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpSetExtendedErrorCallback(ptr ptr long)
@ stdcall -stub -version=0x600+ WinHttpQueryRequestTiming(ptr ptr ptr)
@ stdcall -stub -version=0x600+ WinHttpQueryConnectionTiming(ptr ptr ptr)
