/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later
 * PURPOSE:         Power-setting callback and update tests
 */

#include <kmt_test.h>

typedef struct _POWER_SETTING_ULONG_BUFFER
{
    ULONG Version;
    GUID Guid;
    SYSTEM_POWER_CONDITION PowerCondition;
    ULONG DataLength;
    ULONG Value;
} POWER_SETTING_ULONG_BUFFER, *PPOWER_SETTING_ULONG_BUFFER;

typedef struct _POWER_SETTING_OVERSIZED_BUFFER
{
    ULONG Version;
    GUID Guid;
    SYSTEM_POWER_CONDITION PowerCondition;
    ULONG DataLength;
    UCHAR Value[1025];
} POWER_SETTING_OVERSIZED_BUFFER, *PPOWER_SETTING_OVERSIZED_BUFFER;

typedef struct _POWER_SETTING_TEST_CONTEXT
{
    volatile LONG CallbackCount;
    ULONG LastValue;
    ULONG LastValueLength;
    KIRQL ExpectedIrql;
    BOOLEAN UnregisterOnCallback;
    PVOID RegistrationHandle;
    NTSTATUS UnregisterStatus;
} POWER_SETTING_TEST_CONTEXT, *PPOWER_SETTING_TEST_CONTEXT;

C_ASSERT(FIELD_OFFSET(POWER_SETTING_ULONG_BUFFER, Value) == FIELD_OFFSET(SET_POWER_SETTING_VALUE, Data));

static
NTSTATUS
NTAPI
PowerSettingCallback(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _Inout_opt_ PVOID Context)
{
    PPOWER_SETTING_TEST_CONTEXT TestContext = Context;

    ok(TestContext != NULL, "Context is NULL\n");
    if (!TestContext)
        return STATUS_INVALID_PARAMETER;
    ok_irql(TestContext->ExpectedIrql);
    ok(RtlCompareMemory(SettingGuid, &GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE, sizeof(*SettingGuid)) == sizeof(*SettingGuid), "Unexpected setting GUID\n");

    TestContext->LastValueLength = ValueLength;
    if (Value && ValueLength == sizeof(ULONG))
        TestContext->LastValue = *(PULONG)Value;
    InterlockedIncrement(&TestContext->CallbackCount);
    if (TestContext->UnregisterOnCallback)
    {
        TestContext->UnregisterOnCallback = FALSE;
        TestContext->UnregisterStatus = PoUnregisterPowerSettingCallback(TestContext->RegistrationHandle);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
SetEnergyPreferenceBuffer(
    _In_ ULONG Version,
    _In_ SYSTEM_POWER_CONDITION PowerCondition,
    _In_ ULONG DataLength,
    _In_ ULONG Value,
    _In_ ULONG InputLength)
{
    POWER_SETTING_ULONG_BUFFER Setting;

    RtlZeroMemory(&Setting, sizeof(Setting));
    Setting.Version = Version;
    Setting.Guid = GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE;
    Setting.PowerCondition = PowerCondition;
    Setting.DataLength = DataLength;
    Setting.Value = Value;
    return ZwPowerInformation(SetPowerSettingValue, &Setting, InputLength, NULL, 0);
}

static
NTSTATUS
SetEnergyPreference(
    _In_ SYSTEM_POWER_CONDITION PowerCondition,
    _In_ ULONG Value)
{
    return SetEnergyPreferenceBuffer(POWER_SETTING_VALUE_VERSION, PowerCondition, sizeof(ULONG), Value, sizeof(POWER_SETTING_ULONG_BUFFER));
}

static
NTSTATUS
SetPowerSource(
    _In_ SYSTEM_POWER_CONDITION PowerCondition)
{
    POWER_SETTING_ULONG_BUFFER Setting;

    RtlZeroMemory(&Setting, sizeof(Setting));
    Setting.Version = POWER_SETTING_VALUE_VERSION;
    Setting.Guid = GUID_ACDC_POWER_SOURCE;
    Setting.PowerCondition = PowerCondition;
    Setting.DataLength = sizeof(Setting.Value);
    Setting.Value = PowerCondition;
    return ZwPowerInformation(SetPowerSettingValue, &Setting, sizeof(Setting), NULL, 0);
}

START_TEST(PoPowerSettings)
{
    POWER_SETTING_TEST_CONTEXT Context;
    POWER_SETTING_TEST_CONTEXT ApcContext;
    POWER_SETTING_TEST_CONTEXT SelfContext;
    POWER_SETTING_OVERSIZED_BUFFER OversizedSetting;
    SYSTEM_BATTERY_STATE BatteryState;
    SYSTEM_POWER_CONDITION CurrentCondition;
    SYSTEM_POWER_CONDITION OtherCondition;
    PVOID Handle = NULL;
    PVOID ApcHandle = NULL;
    PVOID SelfHandle = NULL;
    ULONG OriginalValue;
    ULONG OtherOriginalValue;
    ULONG OtherTestValue;
    ULONG TestValue;
    LONG CallbackCount;
    KIRQL OldIrql;
    NTSTATUS ApcUnregisterStatus = STATUS_UNSUCCESSFUL;
    NTSTATUS Status;

    Status = ZwPowerInformation(SystemBatteryState, NULL, 0, &BatteryState, sizeof(BatteryState));
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    CurrentCondition = BatteryState.AcOnLine ? PoAc : PoDc;
    OtherCondition = CurrentCondition == PoAc ? PoDc : PoAc;

    RtlZeroMemory(&OversizedSetting, sizeof(OversizedSetting));
    OversizedSetting.Version = POWER_SETTING_VALUE_VERSION;
    OversizedSetting.Guid = GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE;
    OversizedSetting.PowerCondition = CurrentCondition;
    OversizedSetting.DataLength = sizeof(OversizedSetting.Value);
    Status = ZwPowerInformation(SetPowerSettingValue, &OversizedSetting, sizeof(OversizedSetting), NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_BUFFER_SIZE);

    RtlZeroMemory(&Context, sizeof(Context));
    Status = PoRegisterPowerSettingCallback(NULL, &GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE, PowerSettingCallback, &Context, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Handle != NULL, "Registration handle is NULL\n");
    if (!NT_SUCCESS(Status) || !Handle)
        return;

    ok_eq_long(Context.CallbackCount, 1);
    ok_eq_ulong(Context.LastValueLength, sizeof(ULONG));
    ok(Context.LastValue <= 100, "Initial EPP value %lu exceeds 100\n", Context.LastValue);
    OriginalValue = Context.LastValue;
    TestValue = OriginalValue == 67 ? 68 : 67;

    Status = SetPowerSource(OtherCondition);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_long(Context.CallbackCount, 2);
        OtherOriginalValue = Context.LastValue;
        OtherTestValue = OtherOriginalValue == 72 ? 73 : 72;

        Status = SetPowerSource(CurrentCondition);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_long(Context.CallbackCount, 3);
        ok_eq_ulong(Context.LastValue, OriginalValue);

        CallbackCount = Context.CallbackCount;
        Status = SetEnergyPreference(OtherCondition, OtherTestValue);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_long(Context.CallbackCount, CallbackCount);
        Status = SetPowerSource(OtherCondition);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_long(Context.CallbackCount, CallbackCount + 1);
        ok_eq_ulong(Context.LastValue, OtherTestValue);
        Status = SetPowerSource(CurrentCondition);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_long(Context.CallbackCount, CallbackCount + 2);
        ok_eq_ulong(Context.LastValue, OriginalValue);
        Status = SetEnergyPreference(OtherCondition, OtherOriginalValue);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_long(Context.CallbackCount, CallbackCount + 2);
    }
    else
    {
        (void)SetPowerSource(CurrentCondition);
    }

    CallbackCount = Context.CallbackCount;

    Status = SetEnergyPreference(CurrentCondition, TestValue);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Context.CallbackCount, CallbackCount + 1);
    ok_eq_ulong(Context.LastValueLength, sizeof(ULONG));
    ok_eq_ulong(Context.LastValue, TestValue);

    Status = SetEnergyPreference(CurrentCondition, 101);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_long(Context.CallbackCount, CallbackCount + 1);
    Status = SetEnergyPreferenceBuffer(POWER_SETTING_VALUE_VERSION + 1, CurrentCondition, sizeof(ULONG), TestValue, sizeof(POWER_SETTING_ULONG_BUFFER));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_long(Context.CallbackCount, CallbackCount + 1);
    Status = SetEnergyPreferenceBuffer(POWER_SETTING_VALUE_VERSION, PoConditionMaximum, sizeof(ULONG), TestValue, sizeof(POWER_SETTING_ULONG_BUFFER));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_long(Context.CallbackCount, CallbackCount + 1);
    Status = SetEnergyPreferenceBuffer(POWER_SETTING_VALUE_VERSION, CurrentCondition, sizeof(ULONG) + 1, TestValue, sizeof(POWER_SETTING_ULONG_BUFFER));
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok_eq_long(Context.CallbackCount, CallbackCount + 1);
    Status = SetEnergyPreferenceBuffer(POWER_SETTING_VALUE_VERSION, CurrentCondition, 0, TestValue, FIELD_OFFSET(POWER_SETTING_ULONG_BUFFER, Value));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_long(Context.CallbackCount, CallbackCount + 1);

    Status = PoUnregisterPowerSettingCallback(Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = PoUnregisterPowerSettingCallback(Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = SetEnergyPreference(CurrentCondition, OriginalValue);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Context.CallbackCount, CallbackCount + 1);

    RtlZeroMemory(&ApcContext, sizeof(ApcContext));
    ApcContext.ExpectedIrql = APC_LEVEL;
    KeRaiseIrql(APC_LEVEL, &OldIrql);
    Status = PoRegisterPowerSettingCallback(NULL, &GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE, PowerSettingCallback, &ApcContext, &ApcHandle);
    if (NT_SUCCESS(Status) && ApcHandle)
        ApcUnregisterStatus = PoUnregisterPowerSettingCallback(ApcHandle);
    KeLowerIrql(OldIrql);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(ApcHandle != NULL, "APC-level registration handle is NULL\n");
    ok_eq_long(ApcContext.CallbackCount, 1);
    ok_eq_hex(ApcUnregisterStatus, STATUS_SUCCESS);

    RtlZeroMemory(&SelfContext, sizeof(SelfContext));
    SelfContext.UnregisterStatus = STATUS_UNSUCCESSFUL;
    Status = PoRegisterPowerSettingCallback(NULL, &GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE, PowerSettingCallback, &SelfContext, &SelfHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(SelfHandle != NULL, "Self-unregister registration handle is NULL\n");
    if (!NT_SUCCESS(Status) || !SelfHandle)
        return;
    ok_eq_long(SelfContext.CallbackCount, 1);
    SelfContext.RegistrationHandle = SelfHandle;
    SelfContext.UnregisterOnCallback = TRUE;
    Status = SetEnergyPreference(CurrentCondition, TestValue);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(SelfContext.CallbackCount, 2);
    ok_eq_hex(SelfContext.UnregisterStatus, STATUS_SUCCESS);
    Status = SetEnergyPreference(CurrentCondition, OriginalValue);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(SelfContext.CallbackCount, 2);
    Status = PoUnregisterPowerSettingCallback(SelfHandle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}
