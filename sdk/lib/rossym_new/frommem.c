/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            lib/rossym/frommem.c
 * PURPOSE:         Creating DWARF symbol info from an in-memory image
 *
 * PROGRAMMERS:     Ge van Geldorp (gvg@reactos.com)
 */

#include <ntifs.h>
#include <ndk/ntndk.h>
#include <reactos/rossym.h>
#include "rossympriv.h"
#include <ntimage.h>

#define NDEBUG
#include <debug.h>

#include "dwarf.h"
#include "pe.h"

#define SYMBOL_SIZE 18

/* Debug tracing for section detection - enable to diagnose issues */
#define ROSSYM_TRACE_SECTIONS 0
#if ROSSYM_TRACE_SECTIONS
#define SECTION_TRACE(fmt, ...) DbgPrint("[ROSSYM] " fmt, ##__VA_ARGS__)
#else
#define SECTION_TRACE(fmt, ...) do {} while(0)
#endif

BOOLEAN
RosSymCreateFromMem(PVOID ImageStart, ULONG_PTR ImageSize, PROSSYM_INFO *RosSymInfo)
{
	ANSI_STRING PendingName = { };
	PIMAGE_DOS_HEADER DosHeader;
	PIMAGE_NT_HEADERS NtHeaders;
	PIMAGE_SECTION_HEADER OrigSectionHeaders;
	PeSect *SectionHeaders;
	ULONG SectionIndex;
	unsigned SymbolTable, NumSymbols;

	/* Check if MZ header is valid */
	DosHeader = (PIMAGE_DOS_HEADER) ImageStart;
	if (ImageSize < sizeof(IMAGE_DOS_HEADER)
		|| ! ROSSYM_IS_VALID_DOS_HEADER(DosHeader))
    {
		return FALSE;
    }

	/* Locate NT header  */
	NtHeaders = (PIMAGE_NT_HEADERS)((char *) ImageStart + DosHeader->e_lfanew);
	if (ImageSize < DosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS)
		|| ! ROSSYM_IS_VALID_NT_HEADERS(NtHeaders))
    {
		return FALSE;
    }

	SymbolTable = NtHeaders->FileHeader.PointerToSymbolTable;
	NumSymbols = NtHeaders->FileHeader.NumberOfSymbols;

	/* Allocate PeSect array (extended section headers with name strings) */
	OrigSectionHeaders = IMAGE_FIRST_SECTION(NtHeaders);
	SectionHeaders = RosSymAllocMem(NtHeaders->FileHeader.NumberOfSections * sizeof(PeSect));
	if (!SectionHeaders)
		return FALSE;
	RtlZeroMemory(SectionHeaders, NtHeaders->FileHeader.NumberOfSections * sizeof(PeSect));

	/* Copy section headers and convert names to ANSI_STRINGs */
	for (SectionIndex = 0; SectionIndex < NtHeaders->FileHeader.NumberOfSections;
		 SectionIndex++)
	{
		PendingName.Buffer = NULL;
		PendingName.Length = 0;
		PendingName.MaximumLength = 0;

		RtlCopyMemory(&SectionHeaders[SectionIndex].hdr,
		              &OrigSectionHeaders[SectionIndex],
		              sizeof(IMAGE_SECTION_HEADER));

		if (OrigSectionHeaders[SectionIndex].Name[0] != '/') {
			PendingName.Buffer = RosSymAllocMem(IMAGE_SIZEOF_SHORT_NAME);
			if (!PendingName.Buffer) goto freeall;
			RtlCopyMemory(PendingName.Buffer, OrigSectionHeaders[SectionIndex].Name, IMAGE_SIZEOF_SHORT_NAME);
			PendingName.MaximumLength = IMAGE_SIZEOF_SHORT_NAME;
			PendingName.Length = GetStrnlen(PendingName.Buffer, IMAGE_SIZEOF_SHORT_NAME);
		} else {
			/*
			 * Long section name - the name is stored as "/NNN" where NNN is an offset
			 * into the COFF string table. The string table is NOT mapped in memory,
			 * so we need to look up the actual name from within the image.
			 */
			UNICODE_STRING intConv;
			NTSTATUS Status;
			ULONG StringOffset;
			ULONG VirtualOffset = 0;
			PCHAR ResolvedName = NULL;

			if (RtlCreateUnicodeStringFromAsciiz(&intConv, (PCSZ)OrigSectionHeaders[SectionIndex].Name + 1)) {
				Status = RtlUnicodeStringToInteger(&intConv, 10, &StringOffset);
				RtlFreeUnicodeString(&intConv);
				if (NT_SUCCESS(Status)) {
					SECTION_TRACE("Section %lu: raw name '/%lu'\n", SectionIndex, StringOffset);
					/*
					 * Use a simple linear search through original section headers.
					 * We can't use pefindrva here because the SectionHeaders array
					 * isn't fully initialized yet.
					 */
					ULONG TargetPhysical = SymbolTable + (NumSymbols * SYMBOL_SIZE) + StringOffset;
					for (ULONG k = 0; k < NtHeaders->FileHeader.NumberOfSections; k++) {
						if (TargetPhysical >= OrigSectionHeaders[k].PointerToRawData &&
							TargetPhysical < OrigSectionHeaders[k].PointerToRawData + OrigSectionHeaders[k].SizeOfRawData) {
							VirtualOffset = TargetPhysical - OrigSectionHeaders[k].PointerToRawData + OrigSectionHeaders[k].VirtualAddress;
							SECTION_TRACE("  String table in mapped section, VirtualOffset=0x%lx\n", VirtualOffset);
							break;
						}
					}

					/* If we couldn't find it in mapped sections, try to identify DWARF
					 * sections by examining their content. This is more reliable than
					 * position-based guessing because:
					 * 1. Different modules may have different DWARF sections present
					 * 2. Section order may vary between modules
					 * 3. Some modules may lack aranges, loc, ranges, etc.
					 *
					 * DWARF section identification by content:
					 * - .debug_info: Starts with unit_length (4 bytes) + version (2 bytes, typically 2-4)
					 * - .debug_abbrev: Starts with abbrev code (ULEB128) + tag (ULEB128)
					 * - .debug_line: Starts with unit_length + version (2)
					 * - .debug_str: Contains null-terminated strings
					 * - .debug_aranges: Starts with unit_length + version (2) + debug_info_offset
					 * - .debug_loc: Contains location lists with base addresses
					 * - .debug_ranges: Contains address range pairs
					 * - .debug_frame: Starts with CIE/FDE length
					 */
					if (!VirtualOffset) {
						ULONG chars = OrigSectionHeaders[SectionIndex].Characteristics;
						SECTION_TRACE("  String table NOT mapped, trying content-based detection\n");
						/*
						 * Check for typical DWARF section characteristics.
						 * DWARF sections have IMAGE_SCN_CNT_INITIALIZED_DATA and IMAGE_SCN_MEM_READ.
						 */
						if ((chars & IMAGE_SCN_CNT_INITIALIZED_DATA) &&
							(chars & IMAGE_SCN_MEM_READ)) {

							/*
							 * Try content-based identification.
							 * Read the first few bytes of section data to determine type.
							 */
							ULONG SectionVA = OrigSectionHeaders[SectionIndex].VirtualAddress;
							ULONG SectionSize = OrigSectionHeaders[SectionIndex].SizeOfRawData;
							PUCHAR SectionData = ((PUCHAR)ImageStart) + SectionVA;
							PUCHAR ImageEnd = ((PUCHAR)ImageStart) + NtHeaders->OptionalHeader.SizeOfImage;

							SECTION_TRACE("  VA=0x%lx Size=0x%lx ImageEnd=0x%p SectionData=0x%p\n",
								SectionVA, SectionSize, ImageEnd, SectionData);

							if (SectionData < ImageEnd && SectionSize >= 12) {
								SECTION_TRACE("  First 12 bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
									SectionData[0], SectionData[1], SectionData[2], SectionData[3],
									SectionData[4], SectionData[5], SectionData[6], SectionData[7],
									SectionData[8], SectionData[9], SectionData[10], SectionData[11]);
								/*
								 * Content-based DWARF section identification.
								 *
								 * DWARF section header formats (DWARF 2/3/4):
								 * - debug_info:    unit_length(4) + version(2) + abbrev_off(4) + addr_size(1)
								 * - debug_line:    unit_length(4) + version(2) + header_length(4) + ...
								 * - debug_aranges: unit_length(4) + version(2) + info_off(4) + addr_size(1) + seg_size(1)
								 * - debug_abbrev:  abbrev_code(ULEB) + tag(ULEB) + children(1) + attrs...
								 * - debug_str:     null-terminated strings (ASCII text)
								 * - debug_frame:   length(4) + CIE_id(4, 0xFFFFFFFF for CIE) + ...
								 * - debug_loc:     location lists (address pairs + expressions)
								 * - debug_ranges:  range lists (address pairs, terminated by 0,0)
								 */

								ULONG Length = *(PULONG)SectionData;
								USHORT Version = *(PUSHORT)(SectionData + 4);

								/*
								 * Check for sections with DWARF version header (debug_info, debug_line, debug_aranges).
								 * These all start with: unit_length(4) + version(2, value 2-5)
								 */
								if (Version >= 2 && Version <= 5) {
									/*
									 * Distinguish between debug_info, debug_line, and debug_aranges.
									 *
									 * debug_info header (after version):
									 *   - offset 6: debug_abbrev_offset (4 bytes)
									 *   - offset 10: address_size (1 byte, 4 or 8)
									 *
									 * debug_line header (after version):
									 *   - offset 6: header_length (4 bytes)
									 *   - offset 10: minimum_instruction_length (1 byte, usually 1-4)
									 *   - offset 11: default_is_stmt (1 byte, 0 or 1)
									 *
									 * debug_aranges header (after version):
									 *   - offset 6: debug_info_offset (4 bytes)
									 *   - offset 10: address_size (1 byte, 4 or 8)
									 *   - offset 11: segment_size (1 byte, usually 0)
									 */
									UCHAR Byte10 = *(SectionData + 10);
									UCHAR Byte11 = *(SectionData + 11);

									/*
									 * debug_info: address_size at offset 10 is 4 or 8, no byte 11 constraint
									 * debug_aranges: same as debug_info but segment_size (byte 11) is typically 0
									 * debug_line: byte 10 is min_inst_length (1-4), byte 11 is default_is_stmt (0 or 1)
									 */
									if ((Byte10 == 4 || Byte10 == 8) && Byte11 == 0) {
										/*
										 * Could be debug_info or debug_aranges.
										 * debug_aranges is typically much smaller and contains only
										 * address ranges, while debug_info is typically the largest section.
										 */
										if (SectionSize < 10000) {
											/* Small section with addr_size, likely debug_aranges */
											ResolvedName = ".debug_aranges";
										} else {
											/* Large section with addr_size, likely debug_info */
											ResolvedName = ".debug_info";
										}
									} else if (Byte10 >= 1 && Byte10 <= 4 && (Byte11 == 0 || Byte11 == 1)) {
										/* Looks like debug_line header */
										ResolvedName = ".debug_line";
									} else if (Byte10 == 4 || Byte10 == 8) {
										/* Has valid address_size but non-zero segment_size, still debug_info */
										ResolvedName = ".debug_info";
									}
								}

								/*
								 * Check for debug_frame (CIE/FDE records).
								 * CIE starts with: length(4) + CIE_id(4) where CIE_id is 0xFFFFFFFF (DWARF2/3)
								 * or 0 (DWARF4+). FDE starts with: length(4) + CIE_pointer(4, offset to CIE)
								 */
								if (!ResolvedName && SectionSize >= 8) {
									ULONG CieId = *(PULONG)(SectionData + 4);
									/* Check for CIE marker or a reasonable CIE pointer (small offset) */
									if (CieId == 0xFFFFFFFF) {
										/* DWARF2/3 CIE marker */
										ResolvedName = ".debug_frame";
									} else if (CieId == 0 && Length > 0 && Length < SectionSize) {
										/* DWARF4+ uses 0 for CIE_id */
										/* Verify by checking for augmentation string after CIE_id */
										if (SectionSize > 12 && SectionData[8] < 128) {
											/* Reasonable augmentation string start */
											ResolvedName = ".debug_frame";
										}
									}
								}

								/*
								 * Check for debug_abbrev (abbreviation table).
								 * Starts with: abbrev_code (ULEB128, > 0) + tag (ULEB128) + has_children (1 byte, 0 or 1)
								 * ULEB128 values are typically small for abbrev codes (1-255).
								 */
								if (!ResolvedName && SectionSize >= 3) {
									UCHAR AbbrevCode = SectionData[0];
									UCHAR Tag = SectionData[1];
									UCHAR HasChildren = SectionData[2];

									/* Valid abbrev entry: code > 0, tag in valid range (1-0x7F), children 0 or 1 */
									if (AbbrevCode > 0 && AbbrevCode < 128 &&
									    Tag > 0 && Tag < 128 &&
									    (HasChildren == 0 || HasChildren == 1)) {
										/* Very likely debug_abbrev */
										ResolvedName = ".debug_abbrev";
									}
								}

								/*
								 * Check for debug_str (string table).
								 * Contains only null-terminated ASCII/UTF-8 strings.
								 */
								if (!ResolvedName && SectionSize >= 4) {
									BOOLEAN LooksLikeStrings = TRUE;
									ULONG CheckLen = SectionSize > 512 ? 512 : SectionSize;
									ULONG PrintableCount = 0;
									ULONG NullCount = 0;

									for (ULONG i = 0; i < CheckLen && LooksLikeStrings; i++) {
										UCHAR c = SectionData[i];
										if (c == 0) {
											NullCount++;
										} else if (c >= 0x20 && c < 0x7F) {
											PrintableCount++;
										} else if (c == '\t' || c == '\n') {
											/* Allow tabs and newlines */
										} else if (c >= 0x80) {
											/* Could be UTF-8, allow it */
										} else {
											/* Control character - not a string table */
											LooksLikeStrings = FALSE;
										}
									}

									/* String table should have many printable chars and some nulls */
									if (LooksLikeStrings && PrintableCount > CheckLen / 3 && NullCount > 0) {
										ResolvedName = ".debug_str";
									}
								}

								/*
								 * Check for debug_loc or debug_ranges.
								 * These contain sequences of address pairs (debug_ranges) or
								 * address pairs + location expressions (debug_loc).
								 * Both terminated by (0, 0) pairs.
								 */
								if (!ResolvedName && SectionSize >= 16) {
									/*
									 * Both debug_loc and debug_ranges start with address pairs.
									 * debug_loc has location expressions between pairs.
									 * debug_ranges is simpler - just address pairs.
									 *
									 * Heuristic: Check if data looks like 8-byte aligned addresses.
									 */
									ULONGLONG FirstAddr = *(PULONGLONG)SectionData;
									ULONGLONG SecondAddr = *(PULONGLONG)(SectionData + 8);

									/* Valid addresses should be non-zero and reasonably sized */
									if (FirstAddr != 0 || SecondAddr != 0) {
										/*
										 * debug_ranges tends to be smaller than debug_loc.
										 * debug_loc contains embedded DWARF expressions.
										 */
										if (SectionSize < 5000) {
											ResolvedName = ".debug_ranges";
										} else {
											ResolvedName = ".debug_loc";
										}
									}
								}
							}

							/*
							 * Fallback: If content-based detection failed, use StringOffset
							 * to create a unique but unrecognized name. This at least allows
							 * the section to be loaded even if we can't identify it.
							 */
							if (!ResolvedName) {
								/* Create a generic name based on offset for debugging */
								/* The section will be skipped by dwarfopen if not recognized */
							}
						}
					}
				}
			}

			if (ResolvedName) {
				/* Use the resolved DWARF section name */
				SECTION_TRACE("  -> Detected as: %s\n", ResolvedName);
				PendingName.Length = strlen(ResolvedName);
				PendingName.MaximumLength = PendingName.Length + 1;
				PendingName.Buffer = RosSymAllocMem(PendingName.MaximumLength);
				if (!PendingName.Buffer) goto freeall;
				RtlCopyMemory(PendingName.Buffer, ResolvedName, PendingName.MaximumLength);
			} else if (VirtualOffset) {
				PendingName.Buffer = RosSymAllocMem(MAXIMUM_DWARF_NAME_SIZE);
				if (!PendingName.Buffer) goto freeall;
				PCHAR StringTarget = ((PCHAR)ImageStart)+VirtualOffset;
				PCHAR EndOfImage = ((PCHAR)ImageStart) + NtHeaders->OptionalHeader.SizeOfImage;
				if (StringTarget < EndOfImage) {
					ULONG PossibleStringLength = EndOfImage - StringTarget;
					if (PossibleStringLength > MAXIMUM_DWARF_NAME_SIZE)
						PossibleStringLength = MAXIMUM_DWARF_NAME_SIZE;
					RtlCopyMemory(PendingName.Buffer, StringTarget, PossibleStringLength);
					PendingName.Length = GetStrnlen(PendingName.Buffer, PossibleStringLength);
					PendingName.MaximumLength = MAXIMUM_DWARF_NAME_SIZE;
				} else {
					/* Can't resolve - use raw offset form */
					RosSymFreeMem(PendingName.Buffer);
					PendingName.Buffer = RosSymAllocMem(IMAGE_SIZEOF_SHORT_NAME);
					if (!PendingName.Buffer) goto freeall;
					RtlCopyMemory(PendingName.Buffer, OrigSectionHeaders[SectionIndex].Name, IMAGE_SIZEOF_SHORT_NAME);
					PendingName.MaximumLength = IMAGE_SIZEOF_SHORT_NAME;
					PendingName.Length = GetStrnlen(PendingName.Buffer, IMAGE_SIZEOF_SHORT_NAME);
				}
			} else {
				/* String table not in memory - use raw name bytes */
				SECTION_TRACE("  -> FAILED detection, using raw name\n");
				PendingName.Buffer = RosSymAllocMem(IMAGE_SIZEOF_SHORT_NAME);
				if (!PendingName.Buffer) goto freeall;
				RtlCopyMemory(PendingName.Buffer, OrigSectionHeaders[SectionIndex].Name, IMAGE_SIZEOF_SHORT_NAME);
				PendingName.MaximumLength = IMAGE_SIZEOF_SHORT_NAME;
				PendingName.Length = GetStrnlen(PendingName.Buffer, IMAGE_SIZEOF_SHORT_NAME);
			}
		}
		*ANSI_NAME_STRING(&SectionHeaders[SectionIndex]) = PendingName;
		PendingName.Buffer = NULL;
	}

	Pe *pe = RosSymAllocMem(sizeof(*pe));
	if (!pe) goto freeall;
	pe->fd = ImageStart;
	pe->e2 = peget2;
	pe->e4 = peget4;
	pe->e8 = peget8;
	pe->loadbase = (ULONG_PTR)ImageStart;
	pe->imagebase = NtHeaders->OptionalHeader.ImageBase;
	pe->imagesize = NtHeaders->OptionalHeader.SizeOfImage;
	pe->codestart = NtHeaders->OptionalHeader.BaseOfCode;

	/*
	 * Fix for relocated images:
	 * When a driver is loaded by the kernel loader, the PE header's ImageBase field
	 * may be updated to reflect the actual load address (after relocation).
	 * DWARF debug info uses the ORIGINAL ImageBase from the PE file, not the
	 * relocated one. If the ImageBase looks like a kernel address (high address),
	 * assume it was relocated and use a sensible default.
	 *
	 * Detection: If ImageBase >= 0xFFFF800000000000 (kernel space on x64/ARM64),
	 * it's likely been updated to the load address. Use 0x10000 as the default
	 * ImageBase for drivers (standard DLL base) or 0x400000 for EXEs.
	 */
#ifdef _WIN64
	if (pe->imagebase >= 0xFFFF800000000000ULL) {
		/* Check if this looks like a DLL/SYS (has exports) or EXE */
		ULONG Characteristics = NtHeaders->FileHeader.Characteristics;
		if (Characteristics & IMAGE_FILE_DLL) {
			pe->imagebase = 0x10000;  /* Standard DLL base */
		} else {
			pe->imagebase = 0x400000;  /* Standard EXE base */
		}
	}
#endif
#ifdef _WIN64
	pe->datastart = 0;
#else
	pe->datastart = NtHeaders->OptionalHeader.BaseOfData;
#endif
	pe->nsections = NtHeaders->FileHeader.NumberOfSections;
	pe->sect = SectionHeaders;
	pe->loadsection = loadmemsection;

	*RosSymInfo = dwarfopen(pe);

	return !!*RosSymInfo;

freeall:
	if (PendingName.Buffer) RosSymFreeMem(PendingName.Buffer);
	for (SectionIndex = 0; SectionIndex < NtHeaders->FileHeader.NumberOfSections;
		 SectionIndex++)
		RtlFreeAnsiString(ANSI_NAME_STRING(&SectionHeaders[SectionIndex]));
	RosSymFreeMem(SectionHeaders);

	return FALSE;
}

/* EOF */
