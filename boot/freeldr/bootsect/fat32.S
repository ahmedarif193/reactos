/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Bootsector
 * FILE:            boot/freeldr/bootsect/fat32.S
 * PURPOSE:
 * PROGRAMMERS:     Brian Palmer
 */

/* INCLUDES ******************************************************************/

#include <asm.inc>
#include <freeldr/include/arch/pc/x86common.h>

#define BP_REL(x) [bp + x]

.code16

//ORG HEX(7c00)

start:
    jmp short main
    nop

OEMName:
    .ASCII "FrLdr1.0"
.set OEMName_off, OEMName - start
BytesPerSector:
    .word 512
.set BytesPerSector_off, BytesPerSector - start
SectsPerCluster:
    .byte 0
.set SectsPerCluster_off, SectsPerCluster - start
ReservedSectors:
    .word 32
.set ReservedSectors_off, ReservedSectors - start
NumberOfFats:
    .byte 2
.set NumberOfFats_off, NumberOfFats - start
MaxRootEntries:
    .word 0             // Always zero for FAT32 volumes
.set MaxRootEntries_off, MaxRootEntries - start
TotalSectors:
    .word 0             // Always zero for FAT32 volumes
.set TotalSectors_off, TotalSectors - start
MediaDescriptor:
    .byte HEX(0f8)
.set MediaDescriptor_off, MediaDescriptor - start
SectorsPerFat:
    .word 0             // Always zero for FAT32 volumes
.set SectorsPerFat_off, SectorsPerFat - start
SectorsPerTrack:
    .word 0
.set SectorsPerTrack_off, SectorsPerTrack - start
NumberOfHeads:
    .word 0
.set NumberOfHeads_off, NumberOfHeads - start
HiddenSectors:
    .long 0
.set HiddenSectors_off, HiddenSectors - start
TotalSectorsBig:
    .long 0
.set TotalSectorsBig_off, TotalSectorsBig - start

// FAT32 Inserted Info
SectorsPerFatBig:
    .long    0
.set SectorsPerFatBig_off, SectorsPerFatBig - start
ExtendedFlags:
    .word    0
.set ExtendedFlags_off, ExtendedFlags - start
FSVersion:
    .word    0
.set FSVersion_off, FSVersion - start
RootDirStartCluster:
    .long    0
.set RootDirStartCluster_off, RootDirStartCluster - start
FSInfoSector:
    .word    0
.set FSInfoSector_off, FSInfoSector - start
BackupBootSector:
    .word    6
.set BackupBootSector_off, BackupBootSector - start
Reserved1:
    .space 12, 0
.set Reserved1_off, Reserved1 - start
// End FAT32 Inserted Info

BootDrive:
    .byte 0
.set BootDrive_off, BootDrive - start
Reserved:
    .byte 0
.set Reserved_off, Reserved - start
ExtendSig:
    .byte HEX(29)
.set ExtendSig_off, ExtendSig - start
SerialNumber:
    .long 0
.set SerialNumber_off, SerialNumber - start
VolumeLabel:
    .ascii "NO NAME    "
.set VolumeLabel_off, VolumeLabel - start
FileSystem:
    .ascii "FAT32   "
.set FileSystem_off, FileSystem - start

main:
    xor ax,ax               // Setup segment registers
    mov ds,ax               // Make DS correct
    mov es,ax               // Make ES correct
    mov ss,ax               // Make SS correct
    mov bp, HEX(7c00)
    mov sp, HEX(7c00)       // Setup a stack

    cmp byte ptr BP_REL(BootDrive_off), HEX(0ff)    // If they have specified a boot drive then use it
    jne CheckSectorsPerFat

    mov byte ptr BP_REL(BootDrive_off), dl          // Save the boot drive

CheckSectorsPerFat:

    cmp word ptr BP_REL(SectorsPerFat_off), 0       // Check the old 16-bit value of SectorsPerFat
    jnz CheckFailed                             // If it is non-zero then exit with an error
CheckTotalSectors:                              // Check the old 16-bit value of TotalSectors & MaxRootEntries
    cmp dword ptr BP_REL(MaxRootEntries_off), 0     // by comparing the DWORD at offset MaxRootEntries to zero
    jnz CheckFailed                             // If it is non-zero then exit with an error
CheckFileSystemVersion:
    cmp word ptr BP_REL(FSVersion_off), 0           // Check the file system version word
    jna GetDriveParameters                      // It is zero, so continue
CheckFailed:
    jmp PrintFileSystemError                    // If it is not zero then exit with an error

GetDriveParameters:
    mov  ax, HEX(0800)
    mov  dl, byte ptr BP_REL(BootDrive_off)         // Get boot drive in dl
    int  HEX(13)                                // Request drive parameters from the bios
    jnc  CalcDriveSize                          // If the call succeeded then calculate the drive size

    // If we get here then the call to the BIOS failed
    // so just set CHS equal to the maximum addressable
    // size
    mov  cx, HEX(0ffff)
    mov  dh, cl

CalcDriveSize:
    // Now that we have the drive geometry
    // lets calculate the drive size
    mov  bl, ch         // Put the low 8-bits of the cylinder count into BL
    mov  bh, cl         // Put the high 2-bits in BH
    shr  bh, 6          // Shift them into position, now BX contains the cylinder count
    and  cl, HEX(3f)    // Mask off cylinder bits from sector count
    // CL now contains sectors per track and DH contains head count
    movzx eax, dh       // Move the heads into EAX
    movzx ebx, bx       // Move the cylinders into EBX
    movzx ecx, cl       // Move the sectors per track into ECX
    inc   eax           // Make it one based because the bios returns it zero based
    inc   ebx           // Make the cylinder count one based also
    mul   ecx           // Multiply heads with the sectors per track, result in edx:eax
    mul   ebx           // Multiply the cylinders with (heads * sectors) [stored in edx:eax already]

    // We now have the total number of sectors as reported
    // by the bios in eax, so store it in our variable
    mov dword ptr [HEX(7c00) + BiosCHSDriveSize_off], eax

LoadExtraBootCode:
    // First we have to load our extra boot code at
    // sector 14 into memory at [0000:7e00h]
    mov  eax, HEX(0e)
    add  eax, dword ptr BP_REL(HiddenSectors_off)   // Add the number of hidden sectors
    mov  cx, 1
    xor  bx, bx
    mov  es, bx                                 // Read sector to [0000:7e00h]
    mov  bx, HEX(7e00)
    call ReadSectors
    jmp  StartSearch


// Reads logical sectors into [ES:BX]
// EAX has logical sector number to read
// CX has number of sectors to read
ReadSectors:
    push es
    cmp  eax, dword ptr [HEX(7c00) + BiosCHSDriveSize_off]   // Check if they are reading a sector outside CHS range
    jae  ReadSectorsLBA                         // Yes - go to the LBA routine
                                                // If at all possible we want to use LBA routines because
                                                // They are optimized to read more than 1 sector per read

    pushad                                      // Save logical sector number & sector count

CheckInt13hExtensions:                          // Now check if this computer supports extended reads
    mov  ah, HEX(41)                            // AH = 41h
    mov  bx, HEX(55aa)                          // BX = 55AAh
    mov  dl, byte ptr BP_REL(BootDrive_off)         // DL = drive (80h-FFh)
    int  HEX(13)                                // IBM/MS INT 13 Extensions - INSTALLATION CHECK
    jc   ReadSectorsCHS                         // CF set on error (extensions not supported)
    cmp  bx, HEX(0aa55)                         // BX = AA55h if installed
    jne  ReadSectorsCHS
    test cl,1                                   // CX = API subset support bitmap
    jz   ReadSectorsCHS                         // Bit 0, extended disk access functions (AH=42h-44h,47h,48h) supported

    popad                                       // Restore sector count & logical sector number

ReadSectorsLBA:
    pushad                                      // Save logical sector number & sector count

    cmp  cx, 64                                 // Since the LBA calls only support 0x7F sectors at a time we will limit ourselves to 64
    jbe  ReadSectorsSetupDiskAddressPacket      // If we are reading less than 65 sectors then just do the read
    mov  cx, 64                                 // Otherwise read only 64 sectors on this loop iteration

ReadSectorsSetupDiskAddressPacket:
    mov word ptr [HEX(7c00) + LBASectorsRead_off],cx
    push 0
    push 0
    push eax                                // Put 64-bit logical block address on stack
    push es                                 // Put transfer segment on stack
    push bx                                 // Put transfer offset on stack
    push cx                                 // Set transfer count
    push 16                                 // Set size of packet to 10h
    mov  si, sp                             // Setup disk address packet on stack

    mov  dl, byte ptr BP_REL(BootDrive_off)     // Drive number
    mov  ah, HEX(42)                        // Int 13h, AH = 42h - Extended Read
    int  HEX(13)                            // Call BIOS
    jc   PrintDiskError                     // If the read failed then abort

    add  sp, 16                             // Remove disk address packet from stack

    popad                                   // Restore sector count & logical sector number

    push bx
    mov  ebx, dword ptr [HEX(7c00) + LBASectorsRead_off]
    add  eax, ebx                           // Increment sector to read
    shl  ebx, 5
    mov  dx, es
    add  dx, bx                             // Setup read buffer for next sector
    mov  es, dx
    pop  bx

    sub  cx, word ptr [HEX(7c00) + LBASectorsRead_off]
    jnz  ReadSectorsLBA                     // Read next sector

    pop es
    ret

LBASectorsRead:
    .long    0
.set LBASectorsRead_off, LBASectorsRead - start


// Reads logical sectors into [ES:BX]
// EAX has logical sector number to read
// CX has number of sectors to read
ReadSectorsCHS:
    popad                                        // Get logical sector number & sector count off stack

ReadSectorsCHSLoop:
    pushad
    xor  edx, edx
    movzx ecx, word ptr BP_REL(SectorsPerTrack_off)
    div  ecx                                    // Divide logical by SectorsPerTrack
    inc  dl                                     // Sectors numbering starts at 1 not 0
    mov  cl, dl                                 // Sector in CL
    mov  edx, eax
    shr  edx, 16
    div  word ptr BP_REL(NumberOfHeads_off)         // Divide logical by number of heads
    mov  dh, dl                                 // Head in DH
    mov  dl, byte ptr BP_REL(BootDrive_off)         // Drive number in DL
    mov  ch, al                                 // Cylinder in CX
    ror  ah, 1                                  // Low 8 bits of cylinder in CH, high 2 bits
    ror  ah, 1                                  //  in CL shifted to bits 6 & 7
    or   cl, ah                                 // Or with sector number
    mov  ax, HEX(0201)
    int  HEX(13)    // DISK - READ SECTORS INTO MEMORY
                     // AL = number of sectors to read, CH = track, CL = sector
                     // DH = head, DL = drive, ES:BX -> buffer to fill
                     // Return: CF set on error, AH = status (see AH=01h), AL = number of sectors read

    jc   PrintDiskError                         // If the read failed then abort

    popad

    inc  eax                                    // Increment Sector to Read

    mov  dx, es
    add  dx, 32                                 // Increment read buffer for next sector
    mov  es, dx

    loop ReadSectorsCHSLoop                     // Read next sector

    pop es
    ret

// Displays a disk error message
// And reboots
PrintDiskError:
    mov  si, HEX(7c00) + msgDiskError_off       // Bad boot disk message
    call PutChars                       // Display it

    jmp  Reboot

// Displays a file system error message
// And reboots
PrintFileSystemError:
    mov  si, HEX(7c00) + msgFileSystemError_off  // FreeLdr not found message
    call PutChars                       // Display it

Reboot:
    mov  si, HEX(7c00) + msgAnyKey_off           // Press any key message
    call PutChars                       // Display it
    xor  ax, ax
    int  HEX(16)                        // Wait for a keypress
    int  HEX(19)                        // Reboot

PutChars:
    lodsb
    or   al, al
    jz   short Done
    mov  ah, HEX(0e)
    mov  bx, 7
    int  HEX(10)
    jmp  short PutChars
Done:
    ret


BiosCHSDriveSize:
    .long 0
.set BiosCHSDriveSize_off, BiosCHSDriveSize - start

msgDiskError:
    .ascii "Disk error", CR, LF, NUL
.set msgDiskError_off, msgDiskError - start
msgFileSystemError:
    .ascii "File system error", CR, LF, NUL
.set msgFileSystemError_off, msgFileSystemError - start
msgAnyKey:
    .ascii "Press key", CR, LF, NUL
.set msgAnyKey_off, msgAnyKey - start

.org 509 // Pad to 509 bytes

BootPartition:
    .byte 0
.set BootPartition_off, BootPartition - start

BootSignature:
    .word HEX(0aa55)    // BootSector signature

// End of bootsector
//
// Now starts the extra boot code that we will store
// at sector 14 on a FAT32 volume
//
// To remain multi-boot compatible with other operating
// systems we must not overwrite anything other than
// the bootsector which means we will have to use
// a different sector like 14 to store our extra boot code



StartSearch:

    // Now we must get the first cluster of the root directory
    mov  eax, dword ptr BP_REL(RootDirStartCluster_off)
    cmp  eax, HEX(0ffffff8)     // Check to see if this is the last cluster in the chain
    jb   ContinueSearch         // If not continue, if so then we didn't find freeldr.sys
    jmp  PrintFileNotFound

ContinueSearch:
    mov  bx, HEX(2000)
    mov  es, bx             // Read cluster to [2000:0000h]
    call ReadCluster        // Read the cluster

    // Now we have to find our way through the root directory to
    // The FREELDR.SYS file
    xor  bx,bx
    mov  bl, byte ptr BP_REL(SectsPerCluster_off)
    shl  bx, 4              // BX = BX * 512 / 32
    mov  ax, HEX(2000)      // We loaded at 2000:0000
    mov  es, ax
    xor  di, di
    mov  si, HEX(7c00) + filename_off
    mov  cx, 11
    repe cmpsb              // Compare filenames
    jz   FoundFile          // If same we found it
    dec  bx
    jnz  FindFile
    jmp  PrintFileNotFound

FindFile:
    mov  ax, es             // We didn't find it in the previous dir entry
    add  ax, 2              // So lets move to the next one
    mov  es, ax             // And search again
    xor  di, di
    mov  si, HEX(7c00) + filename_off
    mov  cx, 11
    repe cmpsb              // Compare filenames
    jz   FoundFile          // If same we found it
    dec  bx                 // Keep searching till we run out of dir entries
    jnz  FindFile           // Last entry?

    // Get the next root dir cluster and try again until we run out of clusters
    mov  eax, dword ptr BP_REL(RootDirStartCluster_off)
    call GetFatEntry
    mov dword ptr BP_REL(RootDirStartCluster_off), eax
    jmp  StartSearch

FoundFile:
                                    // Display "Loading FreeLoader..." message
    mov  si, HEX(7c00) + msgLoading_off      // Loading message
    call PutChars                   // Display it

    xor  di, di                     // ES:DI has dir entry
    xor  dx, dx
    mov  ax, word ptr es:[di+20]    // Get start cluster high word
    shl  eax, 16
    mov  ax, word ptr es:[di+26]    // Get start cluster low word

CheckStartCluster:
    cmp  eax, 2                     // Check and see if the start cluster starts at cluster 2 or above
    jnb  CheckEndCluster            // If so then continue
    jmp  PrintFileSystemError       // If not exit with error
CheckEndCluster:
    cmp  eax, HEX(0ffffff8)         // Check and see if the start cluster is and end of cluster chain indicator
    jb   InitializeLoadSegment      // If not then continue
    jmp  PrintFileSystemError       // If so exit with error

InitializeLoadSegment:
    mov  bx, FREELDR_BASE / 16
    mov  es, bx

LoadFile:
    cmp  eax, HEX(0ffffff8)     // Check to see if this is the last cluster in the chain
    jae  LoadFileDone           // If so continue, if not then read the next one
    push eax
    xor  bx, bx                 // Load ROSLDR starting at 0000:F800h
    push es
    call ReadCluster
    pop  es

    xor  bx, bx
    mov  bl, byte ptr BP_REL(SectsPerCluster_off)
    shl  bx, 5                  // BX = BX * 512 / 16
    mov  ax, es                 // Increment the load address by
    add  ax, bx                 // The size of a cluster
    mov  es, ax

    pop  eax
    push es
    call GetFatEntry            // Get the next entry
    pop  es

    jmp  LoadFile               // Load the next cluster (if any)

LoadFileDone:
    mov  dl, byte ptr BP_REL(BootDrive_off)     // Load boot drive into DL
    mov  dh, byte ptr [HEX(7c00) + BootPartition_off]    // Load boot partition into DH

    /* Transfer execution to the bootloader */
    ljmp16 0, FREELDR_BASE

// Returns the FAT entry for a given cluster number
// On entry EAX has cluster number
// On return EAX has FAT entry for that cluster
GetFatEntry:

    shl   eax, 2                                // EAX = EAX * 4 (since FAT32 entries are 4 bytes)
    mov   ecx, eax                              // Save this for later in ECX
    xor   edx, edx
    movzx ebx, word ptr BP_REL(BytesPerSector_off)
    push  ebx
    div   ebx                                   // FAT Sector Number = EAX / BytesPerSector
    movzx ebx, word ptr BP_REL(ReservedSectors_off)
    add   eax, ebx                              // FAT Sector Number += ReservedSectors
    mov   ebx, dword ptr BP_REL(HiddenSectors_off)
    add   eax, ebx                              // FAT Sector Number += HiddenSectors
    pop   ebx
    dec   ebx
    and   ecx,ebx                               // FAT Offset Within Sector = ECX % BytesPerSector
                                                // EAX holds logical FAT sector number
                                                // ECX holds FAT entry offset

                                                // Now we have to check the extended flags
                                                // to see which FAT is the active one
                                                // and use it, or if they are mirrored then
                                                // no worries
    movzx ebx, word ptr BP_REL(ExtendedFlags_off)   // Get extended flags and put into ebx
    and   bx, HEX(0f)                           // Mask off upper 8 bits, now we have active fat in bl
    jz    LoadFatSector                         // If fat is mirrored then skip fat calcs
    cmp   bl, byte ptr BP_REL(NumberOfFats_off)     // Compare bl to number of fats
    jb    GetActiveFatOffset
    jmp   PrintFileSystemError                  // If bl is bigger than numfats exit with error
GetActiveFatOffset:
    push  eax                                   // Save logical FAT sector number
    mov   eax, dword ptr BP_REL(SectorsPerFatBig_off)   // Get the number of sectors occupied by one fat in eax
    mul   ebx                                   // Multiplied by the active FAT index we have in ebx
    pop   edx                                   // Get logical FAT sector number
    add   eax, edx                              // Add the current FAT sector offset

LoadFatSector:
    push  ecx

    mov   bx, HEX(9000)                         // We will load it to [9000:0000h]
    mov   es, bx

    // EAX holds logical FAT sector number
    // Check if we have already loaded it
    cmp   eax, dword ptr [HEX(7c00) + FatSectorInCache_off]
    je    LoadFatSectorAlreadyLoaded

    mov   dword ptr [HEX(7c00) + FatSectorInCache_off], eax
    xor   bx, bx
    mov   cx, 1
    call  ReadSectors

LoadFatSectorAlreadyLoaded:
    pop   ecx
    mov   eax, dword ptr es:[ecx]               // Get FAT entry
    and   eax, HEX(0fffffff)                    // Mask off reserved bits

    ret

FatSectorInCache:                               // This variable tells us which sector we currently have in memory
    .long    HEX(0ffffffff)                     // There is no need to re-read the same sector if we don't have to
.set FatSectorInCache_off, FatSectorInCache - start


// Reads cluster number in EAX into [ES:0000]
ReadCluster:
    // StartSector = ((Cluster - 2) * SectorsPerCluster) + ReservedSectors + HiddenSectors;

    dec   eax
    dec   eax
    xor   edx, edx
    movzx ebx, byte ptr BP_REL(SectsPerCluster_off)
    mul   ebx
    push  eax
    xor   edx, edx
    movzx eax, byte ptr BP_REL(NumberOfFats_off)
    mul   dword ptr BP_REL(SectorsPerFatBig_off)
    movzx ebx, word ptr BP_REL(ReservedSectors_off)
    add   eax, ebx
    add   eax, dword ptr BP_REL(HiddenSectors_off)
    pop   ebx
    add   eax, ebx              // EAX now contains the logical sector number of the cluster
    xor   bx, bx                // We will load it to [ES:0000], ES loaded before function call
    movzx cx, byte ptr BP_REL(SectsPerCluster_off)
    call  ReadSectors
    ret

// Displays a file not found error message
// And reboots
PrintFileNotFound:
    mov  si, HEX(7c00) + msgFreeLdr_off  // FreeLdr not found message
    call PutChars               // Display it

    jmp  Reboot

msgFreeLdr:
    .ascii "Ldr not found", CR, LF, NUL
.set msgFreeLdr_off, msgFreeLdr - start
filename:
    .ascii "FREELDR SYS"
.set filename_off, filename - start
msgLoading:
    .ascii "Loading...", CR, LF, NUL
.set msgLoading_off, msgLoading - start

.org 1022   // Pad to 1022 bytes

    .word HEX(0aa55)       // BootSector signature

.endcode16

END