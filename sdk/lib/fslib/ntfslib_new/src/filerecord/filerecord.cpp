/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

FileRecord::FileRecord(_In_ PVolume DiskVolume,
                       _In_ ULONG FileRecordSize)
{
    // Save Volume pointer.
    this->DiskVolume = DiskVolume;
    RecordBufferSize = FileRecordSize;

    // Initialize data buffer and header pointer.
    Data = new(PagedPool, TAG_FILE_RECORD) UCHAR[FileRecordSize];
    Header = (PFileRecordHeader)Data;
}

FileRecord::FileRecord(_In_ PVolume DiskVolume) : FileRecord(DiskVolume, DiskVolume->MFT->FileRecordSize) { }

FileRecord::~FileRecord()
{
    ClearDataRunCache();
    ClearExtentCache();
    if (AttributeListData)
        delete[] AttributeListData;
    if (Data)
        delete[] Data;
}
