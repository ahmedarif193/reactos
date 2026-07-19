/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared BPB field addressing for the FAT boot sectors
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* LLVM's Intel-syntax operand parser rejects two-symbol expressions like [bp+x-offset start], so the BPB
   fields are addressed through named constants (fine for GNU as too, evaluated lazily against the labels
   the including boot sector defines). */

#define BP_REL(x) [bp+x##Rel]
#define BP_REL_PLUS(x, n) [bp+x##Rel+n]

BytesPerSectorRel = BytesPerSector - start
SectsPerClusterRel = SectsPerCluster - start
ReservedSectorsRel = ReservedSectors - start
NumberOfFatsRel = NumberOfFats - start
MaxRootEntriesRel = MaxRootEntries - start
SectorsPerFatRel = SectorsPerFat - start
SectorsPerTrackRel = SectorsPerTrack - start
NumberOfHeadsRel = NumberOfHeads - start
HiddenSectorsRel = HiddenSectors - start
BootDriveRel = BootDrive - start
