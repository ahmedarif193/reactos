# ReactOS FEX source snapshot

This directory is tracked directly by the ReactOS repository. It does not
require a Git submodule checkout.

- Source repository: https://github.com/eotics-com/FEX.git
- Imported FEX commit: `825479e99b1dbaa56c6162068e8c48755e499798`

The source dependencies used by the ReactOS ARM64EC, WoW64, KDBG, and optional
FEX source builds are vendored at these revisions:

- Catch2: `b3fb4b9feafcd8d91c5cb510a4775143fdbef02f`
- Vulkan-Headers: `450bd2232225d6c7728a4108055ac2e37cef6475`
- drm-headers: `3e49836995c1dcb3df709440ad2f270b569c6a5f`
- fmt: `1be298e1bd68957e4cd352e1f676f00e07dcfb57`
- jemalloc_glibc: `8436195ad5e1bc347d9b39743af3d29abee59f06`
- range-v3: `ca1388fb9da8e69314dda222dc7b139ca84e092f`
- rpmalloc: `9bdc40f198c76cef6159b1b86d0a2af72f904605`
- Tracy: `650c98ece70da9e155e7ba1c2b3ee16004aae118`
- unordered_dense: `3234af2c03549bc85656bfd3a86993bf1cd8aef1`
- VIXL: `5f418449c48f6ca3ad37c47ec632f17a561c0580`
- xxHash: `e626a72bc2321cd320e953a0ccf1584cad60f363`
- Zydis: `9bfadd6a55fc92dbd37fa3ba089bf8b36622df4f`
- Zycore: `75a36c45ae1ad382b0f4e0ede0af84c11ee69928`
- cpp-optparse: `9f94388a339fcbb0bc95c17768eb786c85988f6e`

FEX's precompiled GCC, gVisor, and POSIX test-binary repositories are omitted;
the ReactOS builds disable those Linux test suites. The range-v3 documentation
branch is also omitted. Their pins in the imported FEX commit remain the
provenance record for those optional artifacts.
