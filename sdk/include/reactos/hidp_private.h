#ifndef _REACTOS_HIDP_PRIVATE_H_
#define _REACTOS_HIDP_PRIVATE_H_

#define HIDP_KDR_MAGIC_FIRST  0x50646948
#define HIDP_KDR_MAGIC_SECOND 0x52444B20
#define HIDP_REACTOS_PREPARSED_DATA_MAGIC 0x52487050

typedef struct _HIDP_REACTOS_PREPARSED_DATA
{
    ULONG Magic;
    ULONG NativeOffset;
    ULONG NativeSize;
} HIDP_REACTOS_PREPARSED_DATA, *PHIDP_REACTOS_PREPARSED_DATA;

static __inline
BOOLEAN
HidP_GetCompositePreparsedDataSize(
    IN PHIDP_PREPARSED_DATA PreparsedData,
    IN ULONG PublicSize,
    OUT PULONG TotalSize)
{
    PHIDP_REACTOS_PREPARSED_DATA Footer;

    if (!PreparsedData || !TotalSize || PublicSize < sizeof(*Footer) || *(PULONG)((PUCHAR)PreparsedData + 0) != HIDP_KDR_MAGIC_FIRST || *(PULONG)((PUCHAR)PreparsedData + sizeof(ULONG)) != HIDP_KDR_MAGIC_SECOND)
        return FALSE;

    Footer = (PHIDP_REACTOS_PREPARSED_DATA)((PUCHAR)PreparsedData + PublicSize - sizeof(*Footer));
    if (Footer->Magic != HIDP_REACTOS_PREPARSED_DATA_MAGIC || !Footer->NativeSize || Footer->NativeOffset < PublicSize || Footer->NativeOffset > MAXULONG - Footer->NativeSize)
        return FALSE;

    *TotalSize = Footer->NativeOffset + Footer->NativeSize;
    return TRUE;
}

#endif /* _REACTOS_HIDP_PRIVATE_H_ */
