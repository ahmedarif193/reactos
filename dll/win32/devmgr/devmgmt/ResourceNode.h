#pragma once
#include "Node.h"
#include "DeviceNode.h"
#include "restypes.h"

class CResourceNode : public CNode
{
public:

    CResourceNode(
        _In_ CDeviceNode *Node,
        _In_ INTERFACE_TYPE InterfaceType,
        _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
        _In_ PSP_CLASSIMAGELIST_DATA ImageListData
        );

    ~CResourceNode();

    virtual bool SetupNode();

    ULONGLONG GetSortKey() const { return m_SortKey; }

private:
    ULONGLONG m_SortKey;
};
