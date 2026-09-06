/*
* PROJECT:     ReactOS Device Manager
* LICENSE:     GPL - See COPYING in the top level directory
* FILE:        dll/win32/devmgr/devmgmt/ResourceNode.cpp
* PURPOSE:     Class object for
* COPYRIGHT:   Copyright 2025 Eric Kohl <ekohl@reactos.org>
*
*/

#include "precomp.h"
#include "restypes.h"
#include "devmgmt.h"
#include "ResourceNode.h"


CResourceNode::CResourceNode(
    _In_ CDeviceNode *Node,
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
    _In_ PSP_CLASSIMAGELIST_DATA ImageListData
    ) :
    CNode(ResourceNode, ImageListData),
    m_SortKey(0)
{
    WCHAR szDetail[200];
    WCHAR szDescription[100];
    ULONG ulLength;
    ULONGLONG ullStart;

    m_DeviceId = Node->GetDeviceId();
    m_ClassImage = Node->GetClassImage();

    ulLength = sizeof(szDescription);
    if (CM_Get_DevNode_Registry_PropertyW(Node->GetDeviceInst(),
                                          CM_DRP_DEVICEDESC,
                                          NULL,
                                          szDescription,
                                          &ulLength,
                                          0) != CR_SUCCESS ||
        szDescription[0] == UNICODE_NULL)
    {
        if (!LoadStringW(g_hThisInstance, IDS_UNKNOWNDEVICE, szDescription, _countof(szDescription)))
            szDescription[0] = UNICODE_NULL;
    }

    if (Descriptor->Type == CmResourceTypeInterrupt)
    {
        m_SortKey = Descriptor->u.Interrupt.Vector;
        wsprintf(szDetail, L"(%s) 0x%08X (%d) %s",
                 (InterfaceType == PCIBus) ? L"PCI" : L"ISA",
                 Descriptor->u.Interrupt.Vector, (INT)Descriptor->u.Interrupt.Vector,
                 szDescription);
        StringCchCopyW(m_DisplayName, MAX_PATH, szDetail);
    }
    else if (Descriptor->Type == CmResourceTypePort)
    {
        ullStart = Descriptor->u.Port.Start.QuadPart;
        m_SortKey = ullStart;
        wsprintf(szDetail, L"[%016I64X - %016I64X] %s",
                 ullStart, ullStart + Descriptor->u.Port.Length - 1,
                 szDescription);
        StringCchCopyW(m_DisplayName, MAX_PATH, szDetail);
    }
    else if (Descriptor->Type == CmResourceTypeMemory)
    {
        ullStart = Descriptor->u.Memory.Start.QuadPart;
        m_SortKey = ullStart;
        wsprintf(szDetail, L"[%016I64X - %016I64X] %s",
                 ullStart, ullStart + Descriptor->u.Memory.Length - 1,
                 szDescription);
        StringCchCopyW(m_DisplayName, MAX_PATH, szDetail);
    }
    else if (Descriptor->Type == CmResourceTypeDma)
    {
        m_SortKey = Descriptor->u.Dma.Channel;
        wsprintf(szDetail, L"%02lu %s", Descriptor->u.Dma.Channel, szDescription);
        StringCchCopyW(m_DisplayName, MAX_PATH, szDetail);
    }
}


CResourceNode::~CResourceNode()
{
}


bool
CResourceNode::SetupNode()
{
    return true;
}
