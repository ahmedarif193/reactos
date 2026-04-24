/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the ReactOS PSDK package.
 * No warranty is given; refer to the file DISCLAIMER within this package.
 */

#pragma once

#ifndef __NTDDSD_H__
#define __NTDDSD_H__

#include <sddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Interface version */
#define SDBUS_INTERFACE_VERSION     0x101

/* GUIDs */
DEFINE_GUID(GUID_SDBUS_INTERFACE_STANDARD,
    0x6BB24D81, 0xE924, 0x4825, 0xAF, 0x49, 0x3A, 0xCD, 0x33, 0xC1, 0xD8, 0x20);

DEFINE_GUID(GUID_SD_HOST_CONTROLLER,
    0x79626149, 0x04A0, 0x4353, 0xBE, 0x16, 0x4B, 0x34, 0x1B, 0x11, 0x07, 0xA9);

#define SDBUS_INTTYPE_DEVICE 0

#define SDIO_FLAG_DO_NOT_MANAGE_IO_ENABLE 0x1
#define SDIO_FLAG_SDIO_ENABLE_POLLING     0x2

/* SD_REQUEST_FUNCTION - Type of request sent to the bus driver */
typedef enum {
    SDRF_GET_PROPERTY = 0,
    SDRF_SET_PROPERTY,
    SDRF_DEVICE_COMMAND,
    SDRF_ERASE_COMMAND,
    SDRF_MMC_SOFT_RESET,
    SDRF_MMC_HPI,
    SDRF_IO_RW_DIRECT = 0x100,
    SDRF_IO_RW_EXTENDED,
    SDRF_EMMC_SWITCH,
    SDRF_EMMC_SELECT_PARTITION,
    SDRF_EMMC_SANITIZE
} SD_REQUEST_FUNCTION;

/* SDBUS_PROPERTY - Properties that can be get/set on an SD device */
typedef enum {
    SDP_MEDIA_CHANGECOUNT = 0,
    SDP_MEDIA_STATE,
    SDP_WRITE_PROTECTED,
    SDP_FUNCTION_NUMBER,
    SDP_FUNCTION_TYPE,
    SDP_BUS_DRIVER_VERSION,
    SDP_BUS_WIDTH,
    SDP_BUS_CLOCK,
    SDP_BUS_INTERFACE_CONTROL,
    SDP_HOST_BLOCK_LENGTH,
    SDP_FUNCTION_BLOCK_LENGTH,
    SDP_FN0_BLOCK_LENGTH,
    SDP_FUNCTION_INT_ENABLE,
    SDP_SET_CARD_INTERRUPT_FORWARD,
    SDP_SET_WAKE_INTERRUPT_FORWARD,
    SDP_HIGH_CAPACITY_SUPPORTED,
    SDP_CHAINED_MDL_SUPPORTED,
    SDP_HPI_SUPPORTED,

    /* ReactOS extension: total sector count exposed by the bus driver */
    SDP_TOTAL_SECTORS = 0x100,
    SDP_ROS_CARD_TYPE
} SDBUS_PROPERTY;

/* Bus driver versions */
#define SDBUS_DRIVER_VERSION_1 0x100
#define SDBUS_DRIVER_VERSION_2 0x200
#define SDBUS_DRIVER_VERSION_3 0x300
#define SDBUS_DRIVER_VERSION_4 0x400

/* SDBUS request packet flags */
#define SDRP_FLAG_APPEND_CMD_SEQ 0x0001
#define SDRP_FLAG_WAIT_FOR_BUSY  0x0002

/* SDPROP_MEDIA_STATE - Media state values */
typedef enum {
    SDPMS_NO_MEDIA = 0,
    SDPMS_MEDIA_INSERTED
} SDPROP_MEDIA_STATE;

/* SDBUS_ERASE_TYPE - Erase command types */
typedef enum {
    SDBUS_ERASE_TYPE_ERASE = 0,
    SDBUS_ERASE_TYPE_TRIM = 1,
    SDBUS_ERASE_TYPE_DISCARD = 3,
    SDBUS_ERASE_TYPE_SEC_ERASE = 0x80000000,
    SDBUS_ERASE_TYPE_SEC_TRIM_1 = 0x80000001,
    SDBUS_ERASE_TYPE_SEC_TRIM_2 = 0x80008000
} SDBUS_ERASE_TYPE;

#define SDBUS_ERASE_NORMAL       SDBUS_ERASE_TYPE_ERASE
#define SDBUS_ERASE_TRIM         SDBUS_ERASE_TYPE_TRIM
#define SDBUS_ERASE_DISCARD      SDBUS_ERASE_TYPE_DISCARD
#define SDBUS_ERASE_SECURE_TRIM  SDBUS_ERASE_TYPE_SEC_TRIM_1
#define SDBUS_ERASE_SECURE_PURGE SDBUS_ERASE_TYPE_SEC_TRIM_2

/* SDBUS_FUNCTION_TYPE - SD device function types */
typedef enum {
    SDBUS_FUNCTION_TYPE_UNKNOWN = 0,
    SDBUS_FUNCTION_TYPE_SDIO,
    SDBUS_FUNCTION_TYPE_SD_MEMORY,
    SDBUS_FUNCTION_TYPE_MMC_MEMORY
} SDBUS_FUNCTION_TYPE;

#define IsTypeMemory(type) ((type == SDBUS_FUNCTION_TYPE_SD_MEMORY) || \
                            (type == SDBUS_FUNCTION_TYPE_MMC_MEMORY))
#define IsTypeIo(type) (type == SDBUS_FUNCTION_TYPE_SDIO)

/*
 * Callback routine prototypes
 */

/* SDBUS_CALLBACK_ROUTINE - Called by bus driver to report device interrupts */
typedef
VOID
(NTAPI SDBUS_CALLBACK_ROUTINE)(
    _In_ PVOID CallbackRoutineContext,
    _In_ ULONG InterruptType);

typedef SDBUS_CALLBACK_ROUTINE *PSDBUS_CALLBACK_ROUTINE;

/*
 * SDBUS_INTERFACE_PARAMETERS - Parameters for interface initialization
 */
typedef struct _SDBUS_INTERFACE_PARAMETERS {
    USHORT Size;
    USHORT SdioFlags;
    PDEVICE_OBJECT TargetObject;
    BOOLEAN DeviceGeneratesInterrupts;
    BOOLEAN CallbackAtDpcLevel;
    PSDBUS_CALLBACK_ROUTINE CallbackRoutine;
    PVOID CallbackRoutineContext;
} SDBUS_INTERFACE_PARAMETERS, *PSDBUS_INTERFACE_PARAMETERS;

/* PSDBUS_INITIALIZE_INTERFACE_ROUTINE - Sets initialization parameters */
typedef
NTSTATUS
(NTAPI *PSDBUS_INITIALIZE_INTERFACE_ROUTINE)(
    _In_ PVOID Context,
    _In_ PSDBUS_INTERFACE_PARAMETERS InterfaceParameters);

/* PSDBUS_ACKNOWLEDGE_INT_ROUTINE - Acknowledges interrupt processing complete */
typedef
NTSTATUS
(NTAPI *PSDBUS_ACKNOWLEDGE_INT_ROUTINE)(
    _In_ PVOID Context);

/*
 * SDBUS_INTERFACE_STANDARD - Contains method routine pointers for the SD bus interface
 *
 * This follows the standard INTERFACE pattern from wdm.h
 */
typedef struct _SDBUS_INTERFACE_STANDARD {
    /* Standard interface header */
    USHORT                                 Size;
    USHORT                                 Version;
    PVOID                                  Context;
    PINTERFACE_REFERENCE                   InterfaceReference;
    PINTERFACE_DEREFERENCE                 InterfaceDereference;

    /* SD bus specific routines */
    PSDBUS_INITIALIZE_INTERFACE_ROUTINE    InitializeInterface;
    PSDBUS_ACKNOWLEDGE_INT_ROUTINE         AcknowledgeInterrupt;
} SDBUS_INTERFACE_STANDARD, *PSDBUS_INTERFACE_STANDARD;

/*
 * SDBUS_REQUEST_PACKET - Specifies parameters for SD bus requests
 */
typedef struct _SDBUS_REQUEST_PACKET {
    SD_REQUEST_FUNCTION  RequestFunction;
    PVOID                UserContext[3];
    ULONG_PTR            Information;

    /* Response data from the card */
    union {
        UCHAR            AsUCHAR[16];
        ULONG            AsULONG[4];
        SDRESP_TYPE3     Type3;
    } ResponseData;

    UCHAR                ResponseLength;
    UCHAR                Reserved;
    USHORT               Flags;

    /* Request-specific parameters */
    union {
        /* SDRF_GET_PROPERTY / SDRF_SET_PROPERTY */
        struct {
            SDBUS_PROPERTY  Property;
            PVOID           Buffer;
            ULONG           Length;
        } GetSetProperty;

        /* SDRF_DEVICE_COMMAND */
        struct {
            SDCMD_DESCRIPTOR CmdDesc;
            ULONG            Argument;
            PMDL             Mdl;
            ULONG            Length;
        } DeviceCommand;

        /* SDRF_ERASE_COMMAND */
        struct {
            SDBUS_ERASE_TYPE EraseType;
            ULONG            StartBlock;
            ULONG            EndBlock;
        } EraseCommand;

        /* SDRF_MMC_SOFT_RESET */
        struct {
            ULONG Frequency;
        } MmcSoftReset;

        /* SDRF_MMC_HPI */
        struct {
            PIRP IrpToHpi;
        } MmcHpi;

        struct {
            UCHAR   Function;
            BOOLEAN Write;
            BOOLEAN RawMode;
            ULONG   Address;
            UCHAR   DataIn;
            UCHAR   DataOut;
        } IoDirect;

        struct {
            UCHAR   Function;
            BOOLEAN Write;
            BOOLEAN BlockMode;
            BOOLEAN Increment;
            ULONG   Address;
            ULONG   BlockCount;
            ULONG   BlockSize;
            PMDL    Mdl;
        } IoExtended;

        struct {
            UCHAR Access;
            UCHAR Index;
            UCHAR Value;
            UCHAR CmdSet;
        } EmmcSwitch;

        struct {
            UCHAR PartitionId;
        } EmmcSelectPartition;
    } Parameters;
} SDBUS_REQUEST_PACKET, *PSDBUS_REQUEST_PACKET;

/*
 * Function prototypes
 */

NTSTATUS
NTAPI
SdBusOpenInterface(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_ PSDBUS_INTERFACE_STANDARD InterfaceStandard,
    _In_ USHORT Size,
    _In_ USHORT Version);

NTSTATUS
NTAPI
SdBusSubmitRequest(
    _In_ PVOID InterfaceContext,
    _In_ PSDBUS_REQUEST_PACKET Packet);

NTSTATUS
NTAPI
SdBusSubmitRequestAsync(
    _In_ PVOID InterfaceContext,
    _In_ PSDBUS_REQUEST_PACKET Packet,
    _In_ PIRP Irp,
    _In_ PIO_COMPLETION_ROUTINE CompletionRoutine,
    _In_ PVOID UserContext);

/*
 * Helper macros for initializing request packets
 */
#define SD_INIT_REQUEST_PACKET(Packet, Function) \
    do { \
        RtlZeroMemory((Packet), sizeof(SDBUS_REQUEST_PACKET)); \
        (Packet)->RequestFunction = (Function); \
    } while (0)

#define SD_INIT_GET_PROPERTY(Packet, Prop, Buf, Len) \
    do { \
        SD_INIT_REQUEST_PACKET(Packet, SDRF_GET_PROPERTY); \
        (Packet)->Parameters.GetSetProperty.Property = (Prop); \
        (Packet)->Parameters.GetSetProperty.Buffer = (Buf); \
        (Packet)->Parameters.GetSetProperty.Length = (Len); \
    } while (0)

#define SD_INIT_SET_PROPERTY(Packet, Prop, Buf, Len) \
    do { \
        SD_INIT_REQUEST_PACKET(Packet, SDRF_SET_PROPERTY); \
        (Packet)->Parameters.GetSetProperty.Property = (Prop); \
        (Packet)->Parameters.GetSetProperty.Buffer = (Buf); \
        (Packet)->Parameters.GetSetProperty.Length = (Len); \
    } while (0)

#define IOCTL_SDBUS_BASE                 FILE_DEVICE_CONTROLLER
#define IOCTL_SD_SUBMIT_REQUEST          CTL_CODE(IOCTL_SDBUS_BASE, 3100, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_SD_GET_SOCKET_COUNT        CTL_CODE(IOCTL_SDBUS_BASE, 3101, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SD_GET_ADAPTER_SOCKET_INFO CTL_CODE(IOCTL_SDBUS_BASE, 3102, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SD_GET_DEVICE_INFO         CTL_CODE(IOCTL_SDBUS_BASE, 3103, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SD_SET_CONTROLLER_SPEED    CTL_CODE(IOCTL_SDBUS_BASE, 3104, METHOD_BUFFERED, FILE_ANY_ACCESS)

#ifdef __cplusplus
}
#endif

#endif /* __NTDDSD_H__ */
