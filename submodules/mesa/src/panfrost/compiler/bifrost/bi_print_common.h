/*
 * Copyright (C) 2019 Connor Abbott <cwabbott0@gmail.com>
 * Copyright (C) 2019 Lyude Paul <thatslyude@gmail.com>
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
 * Copyright (C) 2020 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __BI_PRINT_COMMON_H
#define __BI_PRINT_COMMON_H

#include <stdio.h>
#include "bifrost.h"

const char *bi_message_type_name(enum bifrost_message_type T);
const char *bi_flow_control_name(enum bifrost_flow mode);

#endif
