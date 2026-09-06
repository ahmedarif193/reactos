# SSPI client forwarders to secur32

@ stdcall AcceptSecurityContext(ptr ptr ptr long long ptr ptr ptr ptr) secur32.AcceptSecurityContext
@ stdcall AcquireCredentialsHandleA(str str long ptr ptr ptr ptr ptr ptr) secur32.AcquireCredentialsHandleA
@ stdcall AcquireCredentialsHandleW(wstr wstr long ptr ptr ptr ptr ptr ptr) secur32.AcquireCredentialsHandleW
@ stdcall AddCredentialsA(ptr str str long ptr ptr ptr ptr) secur32.AddCredentialsA
@ stdcall AddCredentialsW(ptr wstr wstr long ptr ptr ptr ptr) secur32.AddCredentialsW
@ stdcall AddSecurityPackageA(str ptr) secur32.AddSecurityPackageA
@ stdcall AddSecurityPackageW(wstr ptr) secur32.AddSecurityPackageW
@ stdcall ApplyControlToken(ptr ptr) secur32.ApplyControlToken
@ stdcall CompleteAuthToken(ptr ptr) secur32.CompleteAuthToken
@ stdcall DecryptMessage(ptr ptr long ptr) secur32.DecryptMessage
@ stdcall DeleteSecurityContext(ptr) secur32.DeleteSecurityContext
@ stdcall DeleteSecurityPackageA(str) secur32.DeleteSecurityPackageA
@ stdcall DeleteSecurityPackageW(wstr) secur32.DeleteSecurityPackageW
@ stdcall EncryptMessage(ptr long ptr long) secur32.EncryptMessage
@ stdcall EnumerateSecurityPackagesA(ptr ptr) secur32.EnumerateSecurityPackagesA
@ stdcall EnumerateSecurityPackagesW(ptr ptr) secur32.EnumerateSecurityPackagesW
@ stdcall ExportSecurityContext(ptr long ptr ptr) secur32.ExportSecurityContext
@ stdcall FreeContextBuffer(ptr) secur32.FreeContextBuffer
@ stdcall FreeCredentialsHandle(ptr) secur32.FreeCredentialsHandle
@ stdcall GetComputerObjectNameA(long ptr ptr) secur32.GetComputerObjectNameA
@ stdcall GetComputerObjectNameW(long ptr ptr) secur32.GetComputerObjectNameW
@ stdcall GetSecurityUserInfo(ptr long ptr) secur32.GetSecurityUserInfo
@ stdcall GetUserNameExA(long ptr ptr) secur32.GetUserNameExA
@ stdcall GetUserNameExW(long ptr ptr) secur32.GetUserNameExW
@ stdcall ImpersonateSecurityContext(ptr) secur32.ImpersonateSecurityContext
@ stdcall ImportSecurityContextA(str ptr ptr ptr) secur32.ImportSecurityContextA
@ stdcall ImportSecurityContextW(wstr ptr ptr ptr) secur32.ImportSecurityContextW
@ stdcall InitSecurityInterfaceA() secur32.InitSecurityInterfaceA
@ stdcall InitSecurityInterfaceW() secur32.InitSecurityInterfaceW
@ stdcall InitializeSecurityContextA(ptr ptr str long long long ptr long ptr ptr ptr ptr) secur32.InitializeSecurityContextA
@ stdcall InitializeSecurityContextW(ptr ptr wstr long long long ptr long ptr ptr ptr ptr) secur32.InitializeSecurityContextW
@ stdcall LsaCallAuthenticationPackage(long long ptr long ptr ptr ptr) secur32.LsaCallAuthenticationPackage
@ stdcall LsaConnectUntrusted(ptr) secur32.LsaConnectUntrusted
@ stdcall LsaDeregisterLogonProcess(long) secur32.LsaDeregisterLogonProcess
@ stdcall LsaEnumerateLogonSessions(ptr ptr) secur32.LsaEnumerateLogonSessions
@ stdcall LsaFreeReturnBuffer(ptr) secur32.LsaFreeReturnBuffer
@ stdcall LsaGetLogonSessionData(ptr ptr) secur32.LsaGetLogonSessionData
@ stdcall LsaLogonUser(long ptr long long ptr long ptr ptr ptr ptr ptr ptr ptr ptr) secur32.LsaLogonUser
@ stdcall LsaLookupAuthenticationPackage(long ptr ptr) secur32.LsaLookupAuthenticationPackage
@ stdcall LsaRegisterLogonProcess(ptr ptr ptr) secur32.LsaRegisterLogonProcess
@ stdcall LsaRegisterPolicyChangeNotification(long ptr) secur32.LsaRegisterPolicyChangeNotification
@ stdcall LsaUnregisterPolicyChangeNotification(long ptr) secur32.LsaUnregisterPolicyChangeNotification
@ stdcall MakeSignature(ptr long ptr long) secur32.MakeSignature
@ stdcall QueryContextAttributesA(ptr long ptr) secur32.QueryContextAttributesA
@ stdcall QueryContextAttributesW(ptr long ptr) secur32.QueryContextAttributesW
@ stdcall QueryCredentialsAttributesA(ptr long ptr) secur32.QueryCredentialsAttributesA
@ stdcall QueryCredentialsAttributesW(ptr long ptr) secur32.QueryCredentialsAttributesW
@ stdcall QuerySecurityContextToken(ptr ptr) secur32.QuerySecurityContextToken
@ stdcall QuerySecurityPackageInfoA(str ptr) secur32.QuerySecurityPackageInfoA
@ stdcall QuerySecurityPackageInfoW(wstr ptr) secur32.QuerySecurityPackageInfoW
@ stdcall RevertSecurityContext(ptr) secur32.RevertSecurityContext
@ stdcall SealMessage(ptr long ptr long) secur32.SealMessage
@ stdcall SetContextAttributesA(ptr long ptr long) secur32.SetContextAttributesA
@ stdcall SetContextAttributesW(ptr long ptr long) secur32.SetContextAttributesW
@ stdcall TranslateNameA(str long long ptr ptr) secur32.TranslateNameA
@ stdcall TranslateNameW(wstr long long ptr ptr) secur32.TranslateNameW
@ stdcall UnsealMessage(ptr ptr long ptr) secur32.UnsealMessage
@ stdcall VerifySignature(ptr ptr long ptr) secur32.VerifySignature
@ stdcall -private DllInitialize(long long ptr) DllMain
