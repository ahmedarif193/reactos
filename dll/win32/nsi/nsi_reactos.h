#pragma once

#include "wine/nsi.h"

BOOL nsi_reactos_init(void);
void nsi_reactos_cleanup(void);
DWORD nsi_reactos_cancel_change_notification(OVERLAPPED *overlapped);
DWORD nsi_reactos_enumerate_all(struct nsi_enumerate_all_ex *params);
DWORD nsi_reactos_get_all(struct nsi_get_all_parameters_ex *params);
DWORD nsi_reactos_get_parameter(struct nsi_get_parameter_ex *params);
DWORD nsi_reactos_request_change_notification(struct nsi_request_change_notification_ex *params);
