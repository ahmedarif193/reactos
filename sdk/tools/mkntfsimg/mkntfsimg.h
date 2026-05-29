#ifndef _MKNTFSIMG_H_
#define _MKNTFSIMG_H_

#include "compat.h"
#include "ntfs_layout.h"

typedef struct _NTFSIMG_IO
{
    int fd;
    uint32_t sector_size;
} NTFSIMG_IO;

NTSTATUS ntfsimg_io_open(NTFSIMG_IO *io, const char *path);
void     ntfsimg_io_close(NTFSIMG_IO *io);
NTSTATUS ntfsimg_io_read(NTFSIMG_IO *io, uint64_t byte_offset, uint32_t length, void *buf);
NTSTATUS ntfsimg_io_write(NTFSIMG_IO *io, uint64_t byte_offset, uint32_t length, const void *buf);
NTSTATUS ntfsimg_io_flush(NTFSIMG_IO *io);

typedef struct _NTFSIMG_VOLUME
{
    NTFSIMG_IO io;

    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t bytes_per_mft_record;
    uint32_t bytes_per_index_record;
    uint64_t total_sectors;
    uint64_t total_clusters;
    uint64_t mft_lcn;
    uint64_t mft_mirr_lcn;
    uint64_t serial_number;

    NTFSLX_RUNLIST_ELEMENT *mft_runlist;
    uint32_t mft_runlist_count;
    NTFSLX_RUNLIST_ELEMENT *bitmap_runlist;
    uint32_t bitmap_runlist_count;
    uint64_t mft_data_size;
    uint64_t bitmap_data_size;
} NTFSIMG_VOLUME;

NTSTATUS ntfsimg_volume_open(NTFSIMG_VOLUME *vol, const char *path);
void     ntfsimg_volume_close(NTFSIMG_VOLUME *vol);

NTSTATUS ntfsimg_runlist_vcn_to_lcn(const NTFSLX_RUNLIST_ELEMENT *rl, uint32_t rl_count,
                                    int64_t vcn, int64_t *out_lcn);
NTSTATUS ntfsimg_read_mapped(NTFSIMG_VOLUME *vol, const NTFSLX_RUNLIST_ELEMENT *rl, uint32_t rl_count,
                             uint64_t byte_offset, uint32_t length, void *buf);
NTSTATUS ntfsimg_write_mapped(NTFSIMG_VOLUME *vol, const NTFSLX_RUNLIST_ELEMENT *rl, uint32_t rl_count,
                              uint64_t byte_offset, uint32_t length, const void *buf);
NTSTATUS ntfsimg_read_mft_record(NTFSIMG_VOLUME *vol, uint64_t record_number,
                                 NTFSLX_MFT_RECORD *out_record);
NTSTATUS ntfsimg_write_mft_record(NTFSIMG_VOLUME *vol, uint64_t record_number,
                                  NTFSLX_MFT_RECORD *record);

PNTFSLX_ATTR_RECORD ntfsimg_find_attr(NTFSLX_MFT_RECORD *rec, uint32_t rec_size, uint32_t type);
PNTFSLX_ATTR_RECORD ntfsimg_find_attr_named(NTFSLX_MFT_RECORD *rec, uint32_t rec_size,
                                            uint32_t type, const WCHAR *name, uint32_t name_len);
uint64_t            ntfsimg_current_ntfs_time(void);
int                 ntfsimg_utf8_to_wchar(const char *utf8, uint32_t utf8_bytes,
                                          WCHAR *out_wchar, uint32_t out_capacity, uint32_t *out_len);

NTSTATUS ntfsimg_mft_bitmap_load(NTFSIMG_VOLUME *vol, UCHAR **out_bitmap, uint32_t *out_bytes);
NTSTATUS ntfsimg_mft_bitmap_save(NTFSIMG_VOLUME *vol, const UCHAR *bitmap, uint32_t bytes);
NTSTATUS ntfsimg_mft_alloc_record(const UCHAR *bitmap, uint32_t bitmap_bytes,
                                  uint32_t total_records, uint64_t *out_index);

NTSTATUS ntfsimg_build_file_record(UCHAR *buf, uint32_t rec_size, uint64_t record_number,
                                   USHORT sequence_number, int is_directory);
NTSTATUS ntfsimg_append_std_info(UCHAR *buf, uint32_t rec_size, uint64_t ntfs_time,
                                 uint32_t file_attributes);
NTSTATUS ntfsimg_append_file_name(UCHAR *buf, uint32_t rec_size, uint64_t parent_ref,
                                  uint64_t ntfs_time, uint32_t file_attributes,
                                  uint64_t data_size, const WCHAR *name, uint32_t name_len,
                                  UCHAR name_type);
NTSTATUS ntfsimg_append_data_resident(UCHAR *buf, uint32_t rec_size,
                                      const void *data, uint32_t data_len);
NTSTATUS ntfsimg_finalize_record(UCHAR *buf, uint32_t rec_size);

NTSTATUS ntfsimg_root_add_entry(NTFSIMG_VOLUME *vol, uint64_t file_mft_ref,
                                const WCHAR *name, uint32_t name_len, UCHAR name_type,
                                uint64_t ntfs_time, uint32_t file_attributes,
                                uint64_t data_size);

typedef struct _NTFSIMG_CLUSTER_ALLOCATOR
{
    UCHAR *bitmap;
    uint32_t bitmap_bytes;
    uint64_t total_clusters;
    uint64_t next_search;
    int dirty;
} NTFSIMG_CLUSTER_ALLOCATOR;

NTSTATUS ntfsimg_cluster_alloc_init(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca);
void     ntfsimg_cluster_alloc_free(NTFSIMG_CLUSTER_ALLOCATOR *ca);
NTSTATUS ntfsimg_cluster_alloc(NTFSIMG_CLUSTER_ALLOCATOR *ca, uint64_t needed_clusters,
                               NTFSLX_RUNLIST_ELEMENT **out_runlist, uint32_t *out_count);
NTSTATUS ntfsimg_cluster_alloc_flush(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca);

NTSTATUS ntfsimg_mft_extend(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                            uint32_t extra_records);
NTSTATUS ntfsimg_mft_alloc_or_extend(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                                     uint64_t *out_index);

NTSTATUS ntfsimg_append_data_nonresident(UCHAR *buf, uint32_t rec_size,
                                         NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                                         const char *host_path, uint64_t host_size);

typedef struct _NTFSIMG_INDX
{
    UCHAR *bytes;
    uint32_t block_size;
    uint64_t vcn;
    int dirty;
} NTFSIMG_INDX;

NTSTATUS ntfsimg_indx_read(NTFSIMG_VOLUME *vol, const NTFSLX_RUNLIST_ELEMENT *rl,
                           uint32_t rl_count, uint32_t block_size, uint64_t vcn,
                           NTFSIMG_INDX *out_block);
NTSTATUS ntfsimg_indx_write(NTFSIMG_VOLUME *vol, const NTFSLX_RUNLIST_ELEMENT *rl,
                            uint32_t rl_count, NTFSIMG_INDX *block);
void     ntfsimg_indx_free(NTFSIMG_INDX *block);

NTSTATUS ntfsimg_index_alloc_insert(NTFSIMG_VOLUME *vol, uint64_t dir_mft_index,
                                    const UCHAR *new_entry, uint32_t new_entry_len,
                                    const WCHAR *key_name, uint32_t key_name_len);

NTSTATUS ntfsimg_index_resident_insert(NTFSIMG_VOLUME *vol, uint64_t dir_mft_index,
                                       const UCHAR *new_entry, uint32_t new_entry_len,
                                       const WCHAR *key_name, uint32_t key_name_len);

NTSTATUS ntfsimg_dir_insert(NTFSIMG_VOLUME *vol, uint64_t dir_mft_index,
                            const UCHAR *new_entry, uint32_t new_entry_len,
                            const WCHAR *key_name, uint32_t key_name_len);

NTSTATUS ntfsimg_dir_insert_ex(NTFSIMG_VOLUME *vol,
                               struct _NTFSIMG_CLUSTER_ALLOCATOR *ca,
                               uint64_t dir_mft_index,
                               const UCHAR *new_entry, uint32_t new_entry_len,
                               const WCHAR *key_name, uint32_t key_name_len);

NTSTATUS ntfsimg_spill_root_to_alloc(NTFSIMG_VOLUME *vol,
                                     struct _NTFSIMG_CLUSTER_ALLOCATOR *ca,
                                     uint64_t dir_mft_index);

NTSTATUS ntfsimg_index_alloc_extend(NTFSIMG_VOLUME *vol,
                                    struct _NTFSIMG_CLUSTER_ALLOCATOR *ca,
                                    uint64_t dir_mft_index,
                                    uint32_t extra_blocks);

NTSTATUS ntfsimg_mkdir(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                       uint64_t parent_mft_index, const WCHAR *name, uint32_t name_len,
                       uint64_t *out_mft_index);
NTSTATUS ntfsimg_lookup_child(NTFSIMG_VOLUME *vol, uint64_t parent_mft_index,
                              const WCHAR *name, uint32_t name_len,
                              uint64_t *out_mft_index);
NTSTATUS ntfsimg_resolve_path(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                              const char *image_path, int create_intermediate,
                              uint64_t *out_parent_mft, char **out_leaf_name);

NTSTATUS ntfsimg_add_file(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                          const char *image_path, const char *host_path);
NTSTATUS ntfsimg_add_dir(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                         const char *image_path);
NTSTATUS ntfsimg_addfiles_list(NTFSIMG_VOLUME *vol, NTFSIMG_CLUSTER_ALLOCATOR *ca,
                               const char *list_path);

NTSTATUS NtfslxPreWriteMstFixup(_Inout_ PNTFSLX_RECORD_HEADER Record, _In_ ULONG Size);
VOID     NtfslxPostWriteMstFixup(_Inout_ PNTFSLX_RECORD_HEADER Record);

NTSTATUS NtfslxBitmapSetBitsInRun(_Inout_updates_(BitmapLength) PUCHAR Bitmap,
                                  _In_ ULONG BitmapLength,
                                  _In_ ULONGLONG StartBit,
                                  _In_ ULONGLONG Count,
                                  _In_ UCHAR Value);
BOOLEAN  NtfslxBitmapTestBit(_In_reads_(BitmapLength) const UCHAR *Bitmap,
                             _In_ ULONG BitmapLength,
                             _In_ ULONGLONG Bit);
ULONGLONG NtfslxBitmapCountFreeBits(_In_reads_(BitmapLength) const UCHAR *Bitmap,
                                    _In_ ULONG BitmapLength);

NTSTATUS NtfslxRunlistsMerge(_In_reads_(DestCount) PNTFSLX_RUNLIST_ELEMENT DestRunlist,
                             _In_ ULONG DestCount,
                             _In_reads_(SrcCount) const NTFSLX_RUNLIST_ELEMENT *SrcRunlist,
                             _In_ ULONG SrcCount,
                             _Outptr_ PNTFSLX_RUNLIST_ELEMENT *MergedRunlist,
                             _Out_ PULONG MergedCount);
NTSTATUS NtfslxRunlistTruncate(_Inout_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
                               _Inout_ PULONG RunlistCount,
                               _In_ LONGLONG NewLength);
LONG     NtfslxGetSizeForMappingPairs(_In_reads_(RunlistCount) const NTFSLX_RUNLIST_ELEMENT *Runlist,
                                      _In_ ULONG RunlistCount,
                                      _In_ LONGLONG FirstVcn,
                                      _In_ LONGLONG LastVcn);
NTSTATUS NtfslxMappingPairsBuild(_Out_writes_bytes_(DstLength) PUCHAR Dst,
                                 _In_ LONG DstLength,
                                 _In_reads_(RunlistCount) const NTFSLX_RUNLIST_ELEMENT *Runlist,
                                 _In_ ULONG RunlistCount,
                                 _In_ LONGLONG FirstVcn,
                                 _In_ LONGLONG LastVcn,
                                 _Out_opt_ PLONGLONG StopVcn);
BOOLEAN  NtfslxRunlistIsSparse(_In_reads_(RunlistCount) const NTFSLX_RUNLIST_ELEMENT *Runlist,
                               _In_ ULONG RunlistCount);

#endif
