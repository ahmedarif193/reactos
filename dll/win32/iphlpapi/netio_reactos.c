/* Native NETIO table helpers for ReactOS. */

#include "iphlpapi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(iphlpapi);

static NET_LUID make_interface_luid(DWORD index)
{
    BYTE address[MAX_INTERFACE_PHYSADDR];
    DWORD length = sizeof(address), type = MIB_IF_TYPE_OTHER;
    NET_LUID luid;

    getInterfacePhysicalByIndex(index, &length, address, &type);
    luid.Value = 0;
    luid.Info.NetLuidIndex = index;
    luid.Info.IfType = type;
    return luid;
}

void WINAPI FreeMibTable(void *table)
{
    TRACE("%p\n", table);
    HeapFree(GetProcessHeap(), 0, table);
}

DWORD WINAPI GetIpInterfaceTable(ADDRESS_FAMILY family, MIB_IPINTERFACE_TABLE **table)
{
    InterfaceIndexTable *indexes;
    MIB_IPINTERFACE_TABLE *result;
    DWORD count, i;

    TRACE("%u, %p\n", family, table);
    if (!table || (family != AF_UNSPEC && family != AF_INET && family != AF_INET6)) return ERROR_INVALID_PARAMETER;
    *table = NULL;
    indexes = family == AF_INET6 ? NULL : getInterfaceIndexTable();
    count = indexes ? indexes->numIndexes : 0;
    result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, FIELD_OFFSET(MIB_IPINTERFACE_TABLE, Table) + count * sizeof(result->Table[0]));
    if (!result)
    {
        free(indexes);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    result->NumEntries = count;
    for (i = 0; i < count; i++)
    {
        MIB_IPINTERFACE_ROW *row = result->Table + i;
        DWORD status = 0;

        row->Family = AF_INET;
        row->InterfaceIndex = indexes->indexes[i];
        row->InterfaceLuid = make_interface_luid(row->InterfaceIndex);
        row->MaxReassemblySize = 65535;
        row->MinRouterAdvertisementInterval = 200;
        row->MaxRouterAdvertisementInterval = 600;
        row->UseAutomaticMetric = TRUE;
        row->UseNeighborUnreachabilityDetection = TRUE;
        row->RouterDiscoveryBehavior = RouterDiscoveryDhcp;
        row->DadTransmits = 1;
        row->BaseReachableTime = 30000;
        row->RetransmitTime = 1000;
        row->PathMtuDiscoveryTimeout = 600000;
        row->SupportsNeighborDiscovery = TRUE;
        row->SupportsRouterDiscovery = TRUE;
        getInterfaceMtuByIndex(row->InterfaceIndex, &row->NlMtu);
        getInterfaceStatusByIndex(row->InterfaceIndex, &status);
        row->Connected = status == MIB_IF_OPER_STATUS_OPERATIONAL || status == MIB_IF_OPER_STATUS_CONNECTED;
        row->ReachableTime = row->BaseReachableTime;
    }
    free(indexes);
    *table = result;
    return ERROR_SUCCESS;
}
