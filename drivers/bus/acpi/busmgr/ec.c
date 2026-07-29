/*
 * PROJECT:     ReactOS ACPI bus driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ACPI Embedded Controller driver
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include <precomp.h>

#define NDEBUG
#include <debug.h>

#define _COMPONENT		ACPI_EC_COMPONENT
ACPI_MODULE_NAME		("acpi_ec")

#define ACPI_EC_FLAG_OBF	0x01
#define ACPI_EC_FLAG_IBF	0x02
#define ACPI_EC_FLAG_BURST	0x10
#define ACPI_EC_FLAG_SCI	0x20

#define ACPI_EC_COMMAND_READ	0x80
#define ACPI_EC_COMMAND_WRITE	0x81
#define ACPI_EC_BURST_ENABLE	0x82
#define ACPI_EC_BURST_DISABLE	0x83
#define ACPI_EC_COMMAND_QUERY	0x84

#define ACPI_EC_UDELAY		550
#define ACPI_EC_SPIN_COUNT	20
#define ACPI_EC_DELAY_MS	500
#define ACPI_EC_GLK_TIMEOUT	1000

struct acpi_ec {
	ACPI_HANDLE		handle;
	UINT32			gpe;
	ACPI_IO_ADDRESS		command_addr;
	ACPI_IO_ADDRESS		data_addr;
	BOOLEAN			global_lock;
	BOOLEAN			space_handler_installed;
	BOOLEAN			gpe_handler_installed;
	KMUTEX			mutex;
};

static struct acpi_ec *first_ec;
static struct acpi_ec *boot_ec;

static int acpi_ec_add(struct acpi_device *device);
static int acpi_ec_remove(struct acpi_device *device, int type);

static struct acpi_driver acpi_ec_driver = {
	{0,0},
	ACPI_EC_DRIVER_NAME,
	ACPI_EC_CLASS,
	0,
	0,
	ACPI_EC_HID,
	{acpi_ec_add,acpi_ec_remove}
};

static UINT8
acpi_ec_read_status(struct acpi_ec *ec)
{
	UINT32 value = 0;

	AcpiOsReadPort(ec->command_addr, &value, 8);
	return (UINT8)value;
}

static UINT8
acpi_ec_read_data(struct acpi_ec *ec)
{
	UINT32 value = 0;

	AcpiOsReadPort(ec->data_addr, &value, 8);
	return (UINT8)value;
}

static void
acpi_ec_write_cmd(struct acpi_ec *ec, UINT8 command)
{
	AcpiOsWritePort(ec->command_addr, command, 8);
}

static void
acpi_ec_write_data(struct acpi_ec *ec, UINT8 data)
{
	AcpiOsWritePort(ec->data_addr, data, 8);
}

static ACPI_STATUS
acpi_ec_wait(struct acpi_ec *ec, UINT8 mask, UINT8 desired)
{
	UINT32 i;

	for (i = 0; i < ACPI_EC_SPIN_COUNT; i++) {
		if ((acpi_ec_read_status(ec) & mask) == desired)
			return AE_OK;
		AcpiOsStall(ACPI_EC_UDELAY);
	}

	for (i = 0; i < ACPI_EC_DELAY_MS; i++) {
		if ((acpi_ec_read_status(ec) & mask) == desired)
			return AE_OK;
		AcpiOsSleep(1);
	}

	return AE_TIME;
}

static ACPI_STATUS
acpi_ec_read_unlocked(struct acpi_ec *ec, UINT8 address, UINT8 *data)
{
	ACPI_STATUS status;

	status = acpi_ec_wait(ec, ACPI_EC_FLAG_IBF, 0);
	if (ACPI_FAILURE(status))
		return status;

	acpi_ec_write_cmd(ec, ACPI_EC_COMMAND_READ);

	status = acpi_ec_wait(ec, ACPI_EC_FLAG_IBF, 0);
	if (ACPI_FAILURE(status))
		return status;

	acpi_ec_write_data(ec, address);

	status = acpi_ec_wait(ec, ACPI_EC_FLAG_OBF, ACPI_EC_FLAG_OBF);
	if (ACPI_FAILURE(status))
		return status;

	*data = acpi_ec_read_data(ec);
	return AE_OK;
}

static ACPI_STATUS
acpi_ec_write_unlocked(struct acpi_ec *ec, UINT8 address, UINT8 data)
{
	ACPI_STATUS status;

	status = acpi_ec_wait(ec, ACPI_EC_FLAG_IBF, 0);
	if (ACPI_FAILURE(status))
		return status;

	acpi_ec_write_cmd(ec, ACPI_EC_COMMAND_WRITE);

	status = acpi_ec_wait(ec, ACPI_EC_FLAG_IBF, 0);
	if (ACPI_FAILURE(status))
		return status;

	acpi_ec_write_data(ec, address);

	status = acpi_ec_wait(ec, ACPI_EC_FLAG_IBF, 0);
	if (ACPI_FAILURE(status))
		return status;

	acpi_ec_write_data(ec, data);

	return acpi_ec_wait(ec, ACPI_EC_FLAG_IBF, 0);
}

static ACPI_STATUS
acpi_ec_query_unlocked(struct acpi_ec *ec, UINT8 *data)
{
	ACPI_STATUS status;

	status = acpi_ec_wait(ec, ACPI_EC_FLAG_IBF, 0);
	if (ACPI_FAILURE(status))
		return status;

	acpi_ec_write_cmd(ec, ACPI_EC_COMMAND_QUERY);

	status = acpi_ec_wait(ec, ACPI_EC_FLAG_OBF, ACPI_EC_FLAG_OBF);
	if (ACPI_FAILURE(status))
		return status;

	*data = acpi_ec_read_data(ec);
	return AE_OK;
}

static ACPI_STATUS
acpi_ec_transaction(struct acpi_ec *ec, UINT8 address, UINT8 *data, BOOLEAN write)
{
	ACPI_STATUS status;
	UINT32 glk = 0;

	KeWaitForSingleObject(&ec->mutex, Executive, KernelMode, FALSE, NULL);

	if (ec->global_lock) {
		status = AcpiAcquireGlobalLock(ACPI_EC_GLK_TIMEOUT, &glk);
		if (ACPI_FAILURE(status)) {
			KeReleaseMutex(&ec->mutex, FALSE);
			return status;
		}
	}

	if (write)
		status = acpi_ec_write_unlocked(ec, address, *data);
	else
		status = acpi_ec_read_unlocked(ec, address, data);

	if (ec->global_lock)
		AcpiReleaseGlobalLock(glk);

	KeReleaseMutex(&ec->mutex, FALSE);

	if (ACPI_FAILURE(status))
		DPRINT1("EC transaction failed at address 0x%02x: %s\n",
			address, AcpiFormatException(status));

	return status;
}

static void ACPI_SYSTEM_XFACE
acpi_ec_gpe_query(void *context)
{
	struct acpi_ec *ec = context;
	ACPI_STATUS status;
	UINT8 value = 0;
	UINT32 glk = 0;
	char method[5];

	if (!ec)
		return;

	KeWaitForSingleObject(&ec->mutex, Executive, KernelMode, FALSE, NULL);

	if (ec->global_lock) {
		status = AcpiAcquireGlobalLock(ACPI_EC_GLK_TIMEOUT, &glk);
		if (ACPI_FAILURE(status)) {
			KeReleaseMutex(&ec->mutex, FALSE);
			return;
		}
	}

	status = acpi_ec_query_unlocked(ec, &value);

	if (ec->global_lock)
		AcpiReleaseGlobalLock(glk);

	KeReleaseMutex(&ec->mutex, FALSE);

	if (ACPI_FAILURE(status) || value == 0)
		return;

	method[0] = '_';
	method[1] = 'Q';
	method[2] = "0123456789ABCDEF"[(value >> 4) & 0x0F];
	method[3] = "0123456789ABCDEF"[value & 0x0F];
	method[4] = '\0';

	DPRINT("EC query event 0x%02x, evaluating %s\n", value, method);

	status = AcpiEvaluateObject(ec->handle, method, NULL, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND)
		DPRINT1("Failed to evaluate EC query method %s: %s\n",
			method, AcpiFormatException(status));
}

static UINT32
acpi_ec_gpe_handler(ACPI_HANDLE GpeDevice, UINT32 GpeNumber, void *context)
{
	struct acpi_ec *ec = context;

	UNREFERENCED_PARAMETER(GpeDevice);
	UNREFERENCED_PARAMETER(GpeNumber);

	if (ec && (acpi_ec_read_status(ec) & ACPI_EC_FLAG_SCI))
		AcpiOsExecute(OSL_GPE_HANDLER, acpi_ec_gpe_query, ec);

	return ACPI_INTERRUPT_HANDLED | ACPI_REENABLE_GPE;
}

static ACPI_STATUS
acpi_ec_space_handler(UINT32 function,
		      ACPI_PHYSICAL_ADDRESS address,
		      UINT32 bit_width,
		      UINT64 *value,
		      void *handler_context,
		      void *region_context)
{
	struct acpi_ec *ec = handler_context;
	ACPI_STATUS status = AE_OK;
	UINT32 bytes;
	UINT32 i;
	UINT8 data;

	UNREFERENCED_PARAMETER(region_context);

	if (!ec || !value)
		return AE_BAD_PARAMETER;

	if (function != ACPI_READ && function != ACPI_WRITE)
		return AE_BAD_PARAMETER;

	if (bit_width == 0)
		return AE_BAD_PARAMETER;

	if ((address + ((bit_width + 7) / 8)) > 0x100)
		return AE_BAD_ADDRESS;

	bytes = (bit_width + 7) / 8;

	if (function == ACPI_READ)
		*value = 0;

	for (i = 0; i < bytes; i++) {
		if (function == ACPI_READ) {
			status = acpi_ec_transaction(ec, (UINT8)(address + i),
						     &data, FALSE);
			if (ACPI_FAILURE(status))
				return status;

			*value |= ((UINT64)data) << (i * 8);
		} else {
			data = (UINT8)((*value >> (i * 8)) & 0xFF);
			status = acpi_ec_transaction(ec, (UINT8)(address + i),
						     &data, TRUE);
			if (ACPI_FAILURE(status))
				return status;
		}
	}

	return AE_OK;
}

static ACPI_STATUS
acpi_ec_io_ports(ACPI_RESOURCE *resource, void *context)
{
	struct acpi_ec *ec = context;

	if (resource->Type != ACPI_RESOURCE_TYPE_IO)
		return AE_OK;

	if (ec->data_addr == 0)
		ec->data_addr = resource->Data.Io.Minimum;
	else if (ec->command_addr == 0)
		ec->command_addr = resource->Data.Io.Minimum;
	else
		return AE_CTRL_TERMINATE;

	return AE_OK;
}

static ACPI_STATUS
acpi_ec_install_handlers(struct acpi_ec *ec, ACPI_HANDLE region_scope)
{
	ACPI_STATUS status;

	status = AcpiInstallAddressSpaceHandler(region_scope,
						ACPI_ADR_SPACE_EC,
						acpi_ec_space_handler,
						NULL,
						ec);
	if (ACPI_FAILURE(status)) {
		DPRINT1("Failed to install EC address space handler: %s\n",
			AcpiFormatException(status));
		return status;
	}

	ec->space_handler_installed = TRUE;

	status = AcpiInstallGpeHandler(NULL, ec->gpe,
				       ACPI_GPE_EDGE_TRIGGERED,
				       acpi_ec_gpe_handler,
				       ec);
	if (ACPI_FAILURE(status)) {
		DPRINT1("Failed to install EC GPE handler: %s\n",
			AcpiFormatException(status));
		return status;
	}

	ec->gpe_handler_installed = TRUE;

	status = AcpiEnableGpe(NULL, ec->gpe);
	if (ACPI_FAILURE(status))
		DPRINT1("Failed to enable EC GPE 0x%02x: %s\n",
			ec->gpe, AcpiFormatException(status));

	return AE_OK;
}

static void
acpi_ec_remove_handlers(struct acpi_ec *ec, ACPI_HANDLE region_scope)
{
	if (ec->gpe_handler_installed) {
		AcpiDisableGpe(NULL, ec->gpe);
		AcpiRemoveGpeHandler(NULL, ec->gpe, acpi_ec_gpe_handler);
		ec->gpe_handler_installed = FALSE;
	}

	if (ec->space_handler_installed) {
		AcpiRemoveAddressSpaceHandler(region_scope,
					      ACPI_ADR_SPACE_EC,
					      acpi_ec_space_handler);
		ec->space_handler_installed = FALSE;
	}
}

static int
acpi_ec_add(struct acpi_device *device)
{
	struct acpi_ec *ec;
	ACPI_STATUS status;
	ACPI_INTEGER gpe = 0;
	ACPI_INTEGER glk = 0;

	if (!device)
		return AE_BAD_PARAMETER;

	if (boot_ec && boot_ec->handle == device->handle) {
		device->driver_data = boot_ec;
		first_ec = boot_ec;
		DPRINT1("EC already configured from ECDT\n");
		return AE_OK;
	}

	ec = ExAllocatePoolWithTag(NonPagedPool, sizeof(*ec), 'CEpA');
	if (!ec)
		return AE_NO_MEMORY;

	RtlZeroMemory(ec, sizeof(*ec));
	ec->handle = device->handle;
	KeInitializeMutex(&ec->mutex, 0);

	status = AcpiWalkResources(ec->handle, METHOD_NAME__CRS,
				   acpi_ec_io_ports, ec);
	if (ACPI_FAILURE(status) || ec->command_addr == 0 || ec->data_addr == 0) {
		DPRINT1("Failed to get EC port resources: %s\n",
			AcpiFormatException(status));
		ExFreePoolWithTag(ec, 'CEpA');
		return AE_NOT_FOUND;
	}

	status = acpi_evaluate_integer(ec->handle, "_GPE", NULL, &gpe);
	if (ACPI_FAILURE(status)) {
		DPRINT1("Failed to evaluate EC _GPE: %s\n",
			AcpiFormatException(status));
		ExFreePoolWithTag(ec, 'CEpA');
		return AE_NOT_FOUND;
	}

	ec->gpe = (UINT32)gpe;

	status = acpi_evaluate_integer(ec->handle, "_GLK", NULL, &glk);
	ec->global_lock = ACPI_SUCCESS(status) && glk != 0;

	DPRINT1("EC: command 0x%x data 0x%x GPE 0x%02x global lock %d\n",
		(UINT32)ec->command_addr, (UINT32)ec->data_addr,
		ec->gpe, ec->global_lock);

	status = acpi_ec_install_handlers(ec, ec->handle);
	if (ACPI_FAILURE(status)) {
		acpi_ec_remove_handlers(ec, ec->handle);
		ExFreePoolWithTag(ec, 'CEpA');
		return status;
	}

	device->driver_data = ec;

	if (!first_ec)
		first_ec = ec;

	return AE_OK;
}

static int
acpi_ec_remove(struct acpi_device *device, int type)
{
	struct acpi_ec *ec;

	UNREFERENCED_PARAMETER(type);

	if (!device)
		return AE_BAD_PARAMETER;

	ec = acpi_driver_data(device);
	if (!ec)
		return AE_OK;

	device->driver_data = NULL;

	if (ec == boot_ec)
		return AE_OK;

	acpi_ec_remove_handlers(ec, ec->handle);

	if (first_ec == ec)
		first_ec = NULL;

	ExFreePoolWithTag(ec, 'CEpA');

	return AE_OK;
}

int
acpi_ec_ecdt_probe(void)
{
	ACPI_TABLE_ECDT *ecdt;
	ACPI_STATUS status;
	struct acpi_ec *ec;
	ACPI_HANDLE handle;

	status = AcpiGetTable(ACPI_SIG_ECDT, 1, (ACPI_TABLE_HEADER **)&ecdt);
	if (ACPI_FAILURE(status))
		return AE_NOT_FOUND;

	if (ecdt->Control.Address == 0 || ecdt->Data.Address == 0)
		return AE_NOT_FOUND;

	status = AcpiGetHandle(NULL, (char *)ecdt->Id, &handle);
	if (ACPI_FAILURE(status)) {
		DPRINT1("ECDT names a device that is not in the namespace\n");
		return AE_NOT_FOUND;
	}

	ec = ExAllocatePoolWithTag(NonPagedPool, sizeof(*ec), 'CEpA');
	if (!ec)
		return AE_NO_MEMORY;

	RtlZeroMemory(ec, sizeof(*ec));
	ec->handle = handle;
	ec->command_addr = (ACPI_IO_ADDRESS)ecdt->Control.Address;
	ec->data_addr = (ACPI_IO_ADDRESS)ecdt->Data.Address;
	ec->gpe = ecdt->Gpe;
	KeInitializeMutex(&ec->mutex, 0);

	DPRINT1("EC from ECDT: command 0x%x data 0x%x GPE 0x%02x\n",
		(UINT32)ec->command_addr, (UINT32)ec->data_addr, ec->gpe);

	status = acpi_ec_install_handlers(ec, ACPI_ROOT_OBJECT);
	if (ACPI_FAILURE(status)) {
		acpi_ec_remove_handlers(ec, ACPI_ROOT_OBJECT);
		ExFreePoolWithTag(ec, 'CEpA');
		return status;
	}

	boot_ec = ec;
	first_ec = ec;

	return AE_OK;
}

int
acpi_ec_init(void)
{
	int result;

	result = acpi_bus_register_driver(&acpi_ec_driver);
	if (result < 0)
		return result;

	return 0;
}

void
acpi_ec_exit(void)
{
	acpi_bus_unregister_driver(&acpi_ec_driver);

	if (boot_ec) {
		acpi_ec_remove_handlers(boot_ec, ACPI_ROOT_OBJECT);
		ExFreePoolWithTag(boot_ec, 'CEpA');
		boot_ec = NULL;
	}

	first_ec = NULL;
}
