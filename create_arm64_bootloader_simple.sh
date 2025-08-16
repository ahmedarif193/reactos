#!/bin/bash

# Create a simple ARM64 UEFI bootloader from scratch
# This creates a minimal working bootloader without ReactOS dependencies

set -e

echo "Creating Simple ARM64 UEFI Bootloader"
echo "====================================="

BUILD_DIR="arm64-simple-bootloader"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Create a minimal UEFI bootloader in assembly
cat > bootloader.S << 'EOF'
// Simple ARM64 UEFI Bootloader
// This is a minimal bootloader that can be loaded by UEFI

.text
.align 12

// UEFI entry point
.global _start
_start:
    // Save boot parameters
    mov x19, x0  // ImageHandle
    mov x20, x1  // SystemTable
    
    // Clear BSS (if any)
    // In this simple case, we don't have BSS
    
    // Set up stack (UEFI provides one, but we can adjust if needed)
    // mov sp, #0x80000
    
    // Simple infinite loop for now
    // Later this would jump to the actual bootloader code
loop:
    wfe          // Wait for event (low power wait)
    b loop       // Branch back to loop
    
    // Should never reach here
    mov x0, #0   // Return success
    ret

// Data section
.data
.align 3
boot_message:
    .ascii "ARM64 UEFI Bootloader\0"

// PE/COFF header for UEFI
.section .text
.align 12
dos_header:
    .ascii "MZ"                     // e_magic
    .fill 58, 1, 0                  // Skip to e_lfanew
    .long pe_header - dos_header    // e_lfanew

.align 8
pe_header:
    .ascii "PE\0\0"                 // Signature
    .short 0xaa64                   // Machine (ARM64)
    .short 2                        // NumberOfSections
    .long 0                         // TimeDateStamp
    .long 0                         // PointerToSymbolTable
    .long 0                         // NumberOfSymbols
    .short section_table - optional_header // SizeOfOptionalHeader
    .short 0x206                    // Characteristics

optional_header:
    .short 0x20b                    // Magic (PE32+)
    .byte 14                        // MajorLinkerVersion
    .byte 0                         // MinorLinkerVersion
    .long _etext - _start           // SizeOfCode
    .long 0                         // SizeOfInitializedData
    .long 0                         // SizeOfUninitializedData
    .long _start - dos_header       // AddressOfEntryPoint
    .long _start - dos_header       // BaseOfCode
    .quad 0                         // ImageBase
    .long 0x1000                    // SectionAlignment
    .long 0x200                     // FileAlignment
    .short 0                        // MajorOperatingSystemVersion
    .short 0                        // MinorOperatingSystemVersion
    .short 0                        // MajorImageVersion
    .short 0                        // MinorImageVersion
    .short 10                       // MajorSubsystemVersion
    .short 0                        // MinorSubsystemVersion
    .long 0                         // Win32VersionValue
    .long _edata - dos_header       // SizeOfImage
    .long section_table - dos_header // SizeOfHeaders
    .long 0                         // CheckSum
    .short 10                       // Subsystem (EFI application)
    .short 0                        // DllCharacteristics
    .quad 0                         // SizeOfStackReserve
    .quad 0                         // SizeOfStackCommit
    .quad 0                         // SizeOfHeapReserve
    .quad 0                         // SizeOfHeapCommit
    .long 0                         // LoaderFlags
    .long 6                         // NumberOfRvaAndSizes

    // Data directories
    .quad 0                         // Export
    .quad 0                         // Import
    .quad 0                         // Resource
    .quad 0                         // Exception
    .quad 0                         // Security
    .quad 0                         // Base relocation

section_table:
    // .text section
    .ascii ".text\0\0\0"            // Name
    .long _etext - _start           // VirtualSize
    .long _start - dos_header       // VirtualAddress
    .long _etext - _start           // SizeOfRawData
    .long _start - dos_header       // PointerToRawData
    .long 0                         // PointerToRelocations
    .long 0                         // PointerToLinenumbers
    .short 0                        // NumberOfRelocations
    .short 0                        // NumberOfLinenumbers
    .long 0x60000020                // Characteristics

    // .data section
    .ascii ".data\0\0\0"            // Name
    .long _edata - boot_message     // VirtualSize
    .long boot_message - dos_header // VirtualAddress
    .long _edata - boot_message     // SizeOfRawData
    .long boot_message - dos_header // PointerToRawData
    .long 0                         // PointerToRelocations
    .long 0                         // PointerToLinenumbers
    .short 0                        // NumberOfRelocations
    .short 0                        // NumberOfLinenumbers
    .long 0xc0000040                // Characteristics

.align 12
_etext:

.align 12
_edata:
EOF

# Assemble the bootloader
echo "Assembling bootloader..."
aarch64-linux-gnu-as -o bootloader.o bootloader.S

# Link to create EFI binary
echo "Linking..."
aarch64-linux-gnu-ld -o BOOTAA64.EFI \
    -T /dev/stdin \
    --oformat binary \
    -e _start \
    bootloader.o << 'LDSCRIPT'
SECTIONS
{
    . = 0;
    .text : {
        *(.text)
    }
    .data : {
        *(.data)
    }
    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
LDSCRIPT

# Create disk image
echo "Creating disk image..."
dd if=/dev/zero of=arm64boot.img bs=1M count=32 2>/dev/null
mkfs.vfat -F 32 arm64boot.img
mmd -i arm64boot.img ::/EFI
mmd -i arm64boot.img ::/EFI/BOOT
mcopy -i arm64boot.img BOOTAA64.EFI ::/EFI/BOOT/

echo ""
echo "Build complete!"
echo "Output files:"
echo "  Bootloader: $PWD/BOOTAA64.EFI"
echo "  Disk image: $PWD/arm64boot.img"
echo ""
echo "To test with QEMU:"
echo "  qemu-system-aarch64 -M virt -cpu cortex-a72 -m 2048 \\"
echo "    -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \\"
echo "    -drive file=$PWD/arm64boot.img,format=raw,if=virtio \\"
echo "    -nographic -serial mon:stdio"