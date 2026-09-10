The process image-signing policy
================================

`SetProcessMitigationPolicy(ProcessSignaturePolicy)` enables verification when
the kernel creates a `SEC_IMAGE` section. Microsoft-only enforcement is permanent
for the process. A section created before enabling the policy remains usable;
data-file mappings are unaffected. The Win32 error for a rejected image is
`ERROR_INVALID_IMAGE_HASH` (577), corresponding to `STATUS_INVALID_IMAGE_HASH`.

Trust sources
-------------

* The kernel embeds SHA-256 hashes of the system DLLs produced by the same build.
  `catalog.py` regenerates this catalog when a system image changes. Matching is
  by complete file content, not by filename, directory, version resource or
  application identity. This supplies operating-system image trust for ReactOS's
  unsigned development builds.
* An external image needs a valid embedded Authenticode signature and a code
  signing certificate identifying Microsoft Corporation, with a chain to one of
  the Microsoft roots below. The PE digest, authenticated attributes, RSA
  signature, certificate chain, key usage and validity interval are checked.
* An RFC 3161 timestamp can establish that the code signature was made during
  its certificate's validity interval. Its message imprint is bound to the code
  signature, and its own signer and chain are verified for timestamping usage.

The existing Mbed TLS sources provide RSA, digest and X.509 operations. The
kernel configuration uses paged-pool allocation and no user-mode DLL imports.
The PE and SignedData readers are independently authored from the public
[PE format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format),
[Authenticode description](https://learn.microsoft.com/en-us/windows/win32/secbp/understanding-pe-signatures),
[RFC 5652](https://www.rfc-editor.org/rfc/rfc5652) and
[RFC 3161](https://www.rfc-editor.org/rfc/rfc3161).

Root certificates
-----------------

The DER files and `roots.h` contain public certificates, not signing keys:

* [Microsoft Root Certificate Authority 2010](https://www.microsoft.com/pki/certs/MicRooCerAut_2010-06-23.crt):
  SHA-256 `df545bf919a2439c36983b54cdfc903dfa4f37d3996d8d84b4c31eec6f3c163e`.
* [Microsoft Root Certificate Authority 2011](https://www.microsoft.com/pki/certs/MicRooCerAut2011_2011_03_22.crt):
  SHA-256 `847df6a78497943f27fc72eb93f9a637320a02b561d0a91b09e87a7807ed7c61`.

Current boundary
----------------

Supported embedded signatures use one PKCS#7 WIN_CERTIFICATE, one signer,
RSA PKCS#1 v1.5, and SHA-256, SHA-384 or SHA-512. Certificate data is bounded to
1 MiB and 16 public-key certificates. Unsupported formats are rejected.
Online revocation, external Microsoft catalogs, legacy countersignatures,
Store signing levels and creation-time signing policies are not implemented.
Store-only audit returns `STATUS_NOT_SUPPORTED`. The process creation mitigation
options mask therefore does not advertise signature-policy creation support.

`modules/rostests/tests/imagesigning` exercises the runtime policy and its
kernel enforcement with the same binary and fixtures on Windows and ReactOS.
It requires an unsigned DLL, a Microsoft-signed DLL, and a copy of that signed
DLL with an executable-section byte changed. Fixture architecture must match
the test executable.
