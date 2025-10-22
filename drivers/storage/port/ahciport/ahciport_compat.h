/* Compatibility macros to ease targeting both ScsiPort and StorPort */
#pragma once

#ifdef AHCI_USE_STORPORT

/* Expect storport.h to be included before this header */
#define ScsiPortGetUncachedExtension   StorPortGetUncachedExtension
#define ScsiPortGetPhysicalAddress     StorPortGetPhysicalAddress
#define ScsiPortStallExecution         StorPortStallExecution
#define ScsiPortNotification           StorPortNotification
#define ScsiPortGetDeviceBase          StorPortGetDeviceBase
#define ScsiPortFreeDeviceBase         StorPortFreeDeviceBase
#else

#include <srb.h>

#define PortGetUncachedExtension       ScsiPortGetUncachedExtension
#define PortGetPhysicalAddress         ScsiPortGetPhysicalAddress
#define PortStallExecution             ScsiPortStallExecution
#define PortNotification               ScsiPortNotification

#endif
