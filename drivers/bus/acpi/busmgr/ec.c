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
#define ACPI_EC_QUERY_RETRY_MS	10

struct acpi_ec {
	ACPI_HANDLE		handle;
	UINT32			gpe;
	ACPI_IO_ADDRESS		command_addr;
	ACPI_IO_ADDRESS		data_addr;
	BOOLEAN			global_lock;
	BOOLEAN			space_handler_installed;
	BOOLEAN			gpe_handler_installed;
	KMUTEX			mutex;
	KTIMER			query_retry_timer;
	KDPC			query_retry_dpc;
	KSPIN_LOCK		query_lock;
	volatile LONG		query_pending;
	volatile LONG		query_stopping;
};

static struct acpi_ec *first_ec;
static struct acpi_ec *boot_ec;

static int acpi_ec_add(struct acpi_device *device);
static int acpi_ec_remove(struct acpi_device *device, int type);
static void ACPI_SYSTEM_XFACE acpi_ec_gpe_query(void *context);

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
acpi_ec_acquire(struct acpi_ec *ec, UINT32 *glk)
{
	ACPI_STATUS status;

	KeWaitForSingleObject(&ec->mutex, Executive, KernelMode, FALSE, NULL);

	if (ec->global_lock) {
		status = AcpiAcquireGlobalLock(ACPI_EC_GLK_TIMEOUT, glk);
		if (ACPI_FAILURE(status)) {
			KeReleaseMutex(&ec->mutex, FALSE);
			return status;
		}
	}

	return AE_OK;
}

static void
acpi_ec_release(struct acpi_ec *ec, UINT32 glk)
{
	if (ec->global_lock)
		AcpiReleaseGlobalLock(glk);

	KeReleaseMutex(&ec->mutex, FALSE);
}

static void
acpi_ec_schedule_query_retry(struct acpi_ec *ec)
{
	LARGE_INTEGER due_time;
	KIRQL old_irql;

	due_time.QuadPart = -((LONGLONG)ACPI_EC_QUERY_RETRY_MS * 10000);

	/*
	 * Serialize timer arming with teardown so no callback can be armed
	 * after the timer has been cancelled.
	 */
	KeAcquireSpinLock(&ec->query_lock, &old_irql);
	if (InterlockedCompareExchange(&ec->query_stopping, 0, 0) == 0)
		KeSetTimer(&ec->query_retry_timer, due_time, &ec->query_retry_dpc);
	KeReleaseSpinLock(&ec->query_lock, old_irql);
}

static void
acpi_ec_queue_query(struct acpi_ec *ec)
{
	ACPI_STATUS status;

	if (InterlockedCompareExchange(&ec->query_stopping, 0, 0) != 0)
		return;

	if (InterlockedCompareExchange(&ec->query_pending, 1, 0) != 0)
		return;

	status = AcpiOsExecute(OSL_GPE_HANDLER, acpi_ec_gpe_query, ec);
	if (ACPI_FAILURE(status)) {
		DPRINT1("Unable to queue EC query: %s; retrying\n",
			AcpiFormatException(status));
		/*
		 * The GPE handler can run above DISPATCH_LEVEL. Hop to the
		 * preallocated DPC before arming a timer or retrying the OSL.
		 */
		KeInsertQueueDpc(&ec->query_retry_dpc, NULL, NULL);
	}
}

static VOID
NTAPI
acpi_ec_query_retry_dpc(PKDPC Dpc, PVOID DeferredContext,
			PVOID SystemArgument1, PVOID SystemArgument2)
{
	struct acpi_ec *ec = DeferredContext;
	ACPI_STATUS status;

	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);

	if (InterlockedCompareExchange(&ec->query_stopping, 0, 0) != 0)
		return;

	if (InterlockedCompareExchange(&ec->query_pending, 0, 0) == 0)
		return;

	status = AcpiOsExecute(OSL_GPE_HANDLER, acpi_ec_gpe_query, ec);
	if (ACPI_FAILURE(status))
		acpi_ec_schedule_query_retry(ec);
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

	for (;;) {
		status = acpi_ec_acquire(ec, &glk);
		if (ACPI_FAILURE(status))
			goto Retry;

		status = acpi_ec_query_unlocked(ec, &value);
		acpi_ec_release(ec, glk);

		if (ACPI_FAILURE(status))
			goto Retry;

		if (value != 0) {
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

		if (InterlockedCompareExchange(&ec->query_stopping, 0, 0) != 0 ||
		    !(acpi_ec_read_status(ec) & ACPI_EC_FLAG_SCI))
			break;

		/*
		 * Some controllers need time to deassert SCI_EVT after returning
		 * an empty query. Avoid spinning indefinitely in the worker while
		 * retaining ownership of the coalesced query request.
		 */
		if (value == 0) {
			acpi_ec_schedule_query_retry(ec);
			return;
		}
	}

	InterlockedExchange(&ec->query_pending, 0);

	/*
	 * Close the window where a new SCI edge arrived while query_pending
	 * was still set and was therefore coalesced into this worker.
	 */
	if (InterlockedCompareExchange(&ec->query_stopping, 0, 0) == 0 &&
	    (acpi_ec_read_status(ec) & ACPI_EC_FLAG_SCI))
		acpi_ec_queue_query(ec);

	return;

Retry:
	DPRINT1("EC query failed: %s\n", AcpiFormatException(status));
	if (InterlockedCompareExchange(&ec->query_stopping, 0, 0) == 0)
		acpi_ec_schedule_query_retry(ec);
	else
		InterlockedExchange(&ec->query_pending, 0);
}

static UINT32
acpi_ec_gpe_handler(ACPI_HANDLE GpeDevice, UINT32 GpeNumber, void *context)
{
	struct acpi_ec *ec = context;

	UNREFERENCED_PARAMETER(GpeDevice);
	UNREFERENCED_PARAMETER(GpeNumber);

	if (ec && (acpi_ec_read_status(ec) & ACPI_EC_FLAG_SCI))
		acpi_ec_queue_query(ec);

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
	UINT8 *buffer = (UINT8 *)value;
	UINT32 bytes;
	UINT32 i;
	UINT32 glk = 0;

	UNREFERENCED_PARAMETER(region_context);

	if (!ec || !value)
		return AE_BAD_PARAMETER;

	if (function != ACPI_READ && function != ACPI_WRITE)
		return AE_BAD_PARAMETER;

	if (bit_width == 0)
		return AE_BAD_PARAMETER;

	bytes = (bit_width / 8) + ((bit_width % 8) != 0);

	/*
	 * ACPICA splits field access into datum transfers of at most 64 bits,
	 * so value points at a single UINT64. Never write past it.
	 */
	if (bytes > sizeof(*value))
		return AE_BAD_PARAMETER;

	if (address > 0xFF || bytes > (0x100 - (UINT32)address))
		return AE_BAD_ADDRESS;

	status = acpi_ec_acquire(ec, &glk);
	if (ACPI_FAILURE(status))
		return status;

	for (i = 0; i < bytes; i++) {
		if (function == ACPI_READ)
			status = acpi_ec_read_unlocked(ec, (UINT8)(address + i),
						       &buffer[i]);
		else
			status = acpi_ec_write_unlocked(ec, (UINT8)(address + i),
							buffer[i]);

		if (ACPI_FAILURE(status))
			break;
	}

	acpi_ec_release(ec, glk);

	if (ACPI_FAILURE(status))
		DPRINT1("EC field transaction failed at address 0x%02x: %s\n",
			(UINT32)(address + i), AcpiFormatException(status));

	return status;
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

static void
acpi_ec_initialize(struct acpi_ec *ec)
{
	KeInitializeMutex(&ec->mutex, 0);
	KeInitializeTimer(&ec->query_retry_timer);
	KeInitializeDpc(&ec->query_retry_dpc, acpi_ec_query_retry_dpc, ec);
	KeInitializeSpinLock(&ec->query_lock);
}

static void
acpi_ec_update_global_lock(struct acpi_ec *ec)
{
	ACPI_INTEGER glk = 0;
	ACPI_STATUS status;
	BOOLEAN global_lock;

	status = acpi_evaluate_integer(ec->handle, "_GLK", NULL, &glk);
	global_lock = ACPI_SUCCESS(status) && glk != 0;

	KeWaitForSingleObject(&ec->mutex, Executive, KernelMode, FALSE, NULL);
	ec->global_lock = global_lock;
	KeReleaseMutex(&ec->mutex, FALSE);
}

static ACPI_STATUS
acpi_ec_install_handlers(struct acpi_ec *ec, ACPI_HANDLE region_scope)
{
	ACPI_STATUS status;

	InterlockedExchange(&ec->query_stopping, 0);

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
	BOOLEAN had_gpe_handler = ec->gpe_handler_installed;
	KIRQL old_irql;

	InterlockedExchange(&ec->query_stopping, 1);

	if (ec->gpe_handler_installed) {
		AcpiDisableGpe(NULL, ec->gpe);
		AcpiRemoveGpeHandler(NULL, ec->gpe, acpi_ec_gpe_handler);
		ec->gpe_handler_installed = FALSE;
	}

	KeAcquireSpinLock(&ec->query_lock, &old_irql);
	KeCancelTimer(&ec->query_retry_timer);
	KeRemoveQueueDpc(&ec->query_retry_dpc);
	KeReleaseSpinLock(&ec->query_lock, old_irql);

	if (had_gpe_handler ||
	    InterlockedCompareExchange(&ec->query_pending, 0, 0) != 0) {
		/*
		 * A retry DPC may be handing the request to the OSL queue.
		 * Drain it before waiting for any queued/running query callback.
		 */
		KeFlushQueuedDpcs();
		AcpiOsWaitEventsComplete();
		InterlockedExchange(&ec->query_pending, 0);
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

	if (!device)
		return AE_BAD_PARAMETER;

	if (boot_ec && boot_ec->handle == device->handle) {
		acpi_ec_update_global_lock(boot_ec);
		device->driver_data = boot_ec;
		first_ec = boot_ec;
		DPRINT1("EC already configured from ECDT, global lock %d\n",
			boot_ec->global_lock);
		return AE_OK;
	}

	ec = ExAllocatePoolWithTag(NonPagedPool, sizeof(*ec), 'CEpA');
	if (!ec)
		return AE_NO_MEMORY;

	RtlZeroMemory(ec, sizeof(*ec));
	ec->handle = device->handle;
	acpi_ec_initialize(ec);

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

	acpi_ec_update_global_lock(ec);

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
	ACPI_IO_ADDRESS command_addr;
	ACPI_IO_ADDRESS data_addr;
	UINT32 gpe;

	status = AcpiGetTable(ACPI_SIG_ECDT, 1, (ACPI_TABLE_HEADER **)&ecdt);
	if (ACPI_FAILURE(status))
		return AE_NOT_FOUND;

	if (ecdt->Control.Address == 0 || ecdt->Data.Address == 0) {
		AcpiPutTable((ACPI_TABLE_HEADER *)ecdt);
		return AE_NOT_FOUND;
	}

	status = AcpiGetHandle(NULL, (char *)ecdt->Id, &handle);
	if (ACPI_FAILURE(status)) {
		DPRINT1("ECDT names a device that is not in the namespace\n");
		AcpiPutTable((ACPI_TABLE_HEADER *)ecdt);
		return AE_NOT_FOUND;
	}

	command_addr = (ACPI_IO_ADDRESS)ecdt->Control.Address;
	data_addr = (ACPI_IO_ADDRESS)ecdt->Data.Address;
	gpe = ecdt->Gpe;
	AcpiPutTable((ACPI_TABLE_HEADER *)ecdt);

	ec = ExAllocatePoolWithTag(NonPagedPool, sizeof(*ec), 'CEpA');
	if (!ec)
		return AE_NO_MEMORY;

	RtlZeroMemory(ec, sizeof(*ec));
	ec->handle = handle;
	ec->command_addr = command_addr;
	ec->data_addr = data_addr;
	ec->gpe = gpe;
	acpi_ec_initialize(ec);

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
