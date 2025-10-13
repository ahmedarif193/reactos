/* Compatibility macros to ease porting from StorPort to ScsiPort */
#pragma once

#include <srb.h>

/* Memory and DMA */
#define PortGetUncachedExtension  ScsiPortGetUncachedExtension
#define PortGetPhysicalAddress    ScsiPortGetPhysicalAddress

/* Timing */
#define PortStallExecution        ScsiPortStallExecution

/* MMIO/PIO accessors */
#define PortReadRegisterUlong     ScsiPortReadRegisterUlong
#define PortWriteRegisterUlong    ScsiPortWriteRegisterUlong

/* Notifications */
#define PortNotification          ScsiPortNotification

