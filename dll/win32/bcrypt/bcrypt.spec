@ stub BCryptAddContextFunction
@ stub BCryptAddContextFunctionProvider
@ stdcall BCryptCloseAlgorithmProvider(ptr long)
@ stub BCryptConfigureContext
@ stub BCryptConfigureContextFunction
@ stub BCryptCreateContext
@ stdcall BCryptCreateHash(ptr ptr ptr long ptr long long)
@ stub BCryptDecrypt
@ stub BCryptDeleteContext
@ stub BCryptDeriveKey
@ stdcall BCryptDestroyHash(ptr)
@ stdcall -stub BCryptDestroyKey(ptr)
@ stub BCryptDestroySecret
@ stub BCryptDuplicateHash
@ stub BCryptDuplicateKey
@ stub BCryptEncrypt
@ stdcall BCryptEnumAlgorithms(long ptr ptr long)
@ stub BCryptEnumContextFunctionProviders
@ stub BCryptEnumContextFunctions
@ stub BCryptEnumContexts
@ stub BCryptEnumProviders
@ stub BCryptEnumRegisteredProviders
@ stub BCryptExportKey
@ stub BCryptFinalizeKeyPair
@ stdcall BCryptFinishHash(ptr ptr long long)
@ stub BCryptFreeBuffer
@ stdcall BCryptGenRandom(ptr ptr long long)
@ stub BCryptGenerateKeyPair
@ stub BCryptGenerateSymmetricKey
@ stdcall BCryptGetFipsAlgorithmMode(ptr)
@ stdcall BCryptGetProperty(ptr wstr ptr long ptr long)
@ stdcall BCryptHash(ptr ptr long ptr long ptr long)
@ stdcall BCryptHashData(ptr ptr long long)
@ stub BCryptImportKey
@ stdcall -stub BCryptImportKeyPair(ptr ptr wstr ptr ptr long long)
@ stdcall BCryptOpenAlgorithmProvider(ptr wstr wstr long)
@ stub BCryptQueryContextConfiguration
@ stub BCryptQueryContextFunctionConfiguration
@ stub BCryptQueryContextFunctionProperty
@ stub BCryptQueryProviderRegistration
@ stub BCryptRegisterConfigChangeNotify
@ stub BCryptRegisterProvider
@ stub BCryptRemoveContextFunction
@ stub BCryptRemoveContextFunctionProvider
@ stub BCryptResolveProviders
@ stub BCryptSecretAgreement
@ stub BCryptSetAuditingInterface
@ stub BCryptSetContextFunctionProperty
@ stub BCryptSetProperty
@ stub BCryptSignHash
@ stub BCryptUnregisterConfigChangeNotify
@ stub BCryptUnregisterProvider
@ stdcall -stub BCryptVerifySignature(ptr ptr ptr long ptr long long)
@ stub GetAsymmetricEncryptionInterface
@ stub GetCipherInterface
@ stub GetHashInterface
@ stub GetRngInterface
@ stub GetSecretAgreementInterface
@ stub GetSignatureInterface

# Windows 10 BCrypt/CNG Enhanced Cryptographic Support
@ stdcall -stub -version=0x600+ BCryptCreateMultiHash(ptr ptr ptr long ptr long long)
@ stdcall -stub -version=0x600+ BCryptProcessMultiOperations(ptr long ptr long long)
@ stdcall -stub -version=0x600+ BCryptKeyDerivation(ptr ptr ptr long ptr long long)
@ stdcall -stub -version=0x600+ BCryptDeriveKeyCapi(ptr ptr ptr long long)
@ stdcall -stub -version=0x600+ BCryptDeriveKeyPBKDF2(ptr ptr long ptr long int64 ptr long long)

# Enhanced Elliptic Curve Support
@ stdcall -stub -version=0x600+ BCryptCreateECKey(ptr ptr ptr long ptr long long)
@ stdcall -stub -version=0x600+ BCryptImportECCKey(ptr ptr wstr ptr ptr long long)
@ stdcall -stub -version=0x600+ BCryptExportECCKey(ptr ptr wstr ptr long ptr long)
@ stdcall -stub -version=0x600+ BCryptECDHSecretAgreement(ptr ptr ptr long)
@ stdcall -stub -version=0x600+ BCryptECDSASignHash(ptr ptr ptr long ptr long ptr long)
@ stdcall -stub -version=0x600+ BCryptECDSAVerifySignature(ptr ptr ptr long ptr long long)

# Advanced Key Management
@ stdcall -stub -version=0x600+ BCryptGenerateKeyFromPassword(ptr ptr wstr long ptr long ptr long long)
@ stdcall -stub -version=0x600+ BCryptDeriveKeyFromPassword(ptr ptr wstr long ptr long ptr long long long)
@ stdcall -stub -version=0x600+ BCryptCreateKeyFromSecret(ptr ptr ptr long ptr long long)
@ stdcall -stub -version=0x600+ BCryptWrapKey(ptr ptr ptr long ptr long ptr long ptr long)
@ stdcall -stub -version=0x600+ BCryptUnwrapKey(ptr ptr ptr long ptr long ptr long ptr long long)

# Modern Symmetric Algorithms
@ stdcall -stub -version=0x600+ BCryptEncryptEx(ptr ptr long ptr ptr long ptr long ptr long long)
@ stdcall -stub -version=0x600+ BCryptDecryptEx(ptr ptr long ptr ptr long ptr long ptr long long)
@ stdcall -stub -version=0x600+ BCryptCreateCipherWithMode(ptr ptr wstr ptr long long)
@ stdcall -stub -version=0x600+ BCryptSetCipherMode(ptr wstr)
@ stdcall -stub -version=0x600+ BCryptGetCipherMode(ptr ptr long ptr)

# Advanced Hash Functions
@ stdcall -stub -version=0x600+ BCryptCreateHMACHash(ptr ptr ptr long ptr long long)
@ stdcall -stub -version=0x600+ BCryptComputeHMAC(ptr ptr long ptr long ptr long)
@ stdcall -stub -version=0x600+ BCryptCreateDigest(ptr ptr wstr ptr long long)
@ stdcall -stub -version=0x600+ BCryptUpdateDigest(ptr ptr long long)
@ stdcall -stub -version=0x600+ BCryptFinalizeDigest(ptr ptr long long)

# Digital Signature Enhancements
@ stdcall -stub -version=0x600+ BCryptSignHashEx(ptr ptr ptr long ptr long ptr long long)
@ stdcall -stub -version=0x600+ BCryptVerifySignatureEx(ptr ptr ptr long ptr long long long)
@ stdcall -stub -version=0x600+ BCryptCreateSignature(ptr ptr ptr long ptr long ptr long long)
@ stdcall -stub -version=0x600+ BCryptVerifySignatureWithCert(ptr ptr ptr long ptr long long)

# Random Number Generation Enhancements
@ stdcall -stub -version=0x600+ BCryptGenRandomSeed(ptr long)
@ stdcall -stub -version=0x600+ BCryptCreateRNGProvider(ptr wstr long)
@ stdcall -stub -version=0x600+ BCryptRNGInitialize(ptr ptr long long)
@ stdcall -stub -version=0x600+ BCryptRNGReseed(ptr ptr long)

# Algorithm Provider Management
@ stdcall -stub -version=0x600+ BCryptEnumAlgorithmsEx(long ptr ptr long long)
@ stdcall -stub -version=0x600+ BCryptQueryAlgorithmProperty(ptr wstr ptr long ptr long)
@ stdcall -stub -version=0x600+ BCryptSetAlgorithmProperty(ptr wstr ptr long long)
@ stdcall -stub -version=0x600+ BCryptGetDefaultProvider(wstr ptr long)
@ stdcall -stub -version=0x600+ BCryptSetDefaultProvider(wstr ptr long)

# Certificate and PKI Support
@ stdcall -stub -version=0x600+ BCryptImportCertificateKey(ptr ptr ptr long ptr long long)
@ stdcall -stub -version=0x600+ BCryptExportCertificateKey(ptr ptr ptr long ptr long)
@ stdcall -stub -version=0x600+ BCryptValidateCertificateChain(ptr ptr long ptr)
@ stdcall -stub -version=0x600+ BCryptCreateCertificateContext(ptr long ptr long ptr)

# Performance and Hardware Acceleration
@ stdcall -stub -version=0x600+ BCryptQueryHardwareCapabilities(ptr ptr long)
@ stdcall -stub -version=0x600+ BCryptEnableHardwareAcceleration(ptr long)
@ stdcall -stub -version=0x600+ BCryptGetPerformanceCounters(ptr ptr long)
@ stdcall -stub -version=0x600+ BCryptOptimizeOperation(ptr long ptr long)

# Memory and Resource Management
@ stdcall -stub -version=0x600+ BCryptAllocateMemory(ptr long long)
@ stdcall -stub -version=0x600+ BCryptReallocateMemory(ptr ptr long long)
@ stdcall -stub -version=0x600+ BCryptFreeMemory(ptr long)
@ stdcall -stub -version=0x600+ BCryptSecureZeroMemory(ptr long)

# Context and Session Management
@ stdcall -stub -version=0x600+ BCryptCreateCryptoContext(ptr wstr ptr long)
@ stdcall -stub -version=0x600+ BCryptDestroyCryptoContext(ptr)
@ stdcall -stub -version=0x600+ BCryptSaveContextState(ptr ptr long ptr)
@ stdcall -stub -version=0x600+ BCryptRestoreContextState(ptr ptr long)

# TLS/SSL Cryptographic Primitives
@ stdcall -stub -version=0x600+ BCryptTlsPrf(ptr ptr long ptr long ptr long ptr long)
@ stdcall -stub -version=0x600+ BCryptSslKeyDerivation(ptr ptr long ptr long ptr long)
@ stdcall -stub -version=0x600+ BCryptSslComputeFinishedHash(ptr ptr long ptr long ptr long)
@ stdcall -stub -version=0x600+ BCryptSslComputeSessionKeys(ptr ptr long ptr)
