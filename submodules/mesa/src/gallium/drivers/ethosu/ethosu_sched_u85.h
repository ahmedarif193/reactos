/*
 * Copyright (c) 2025 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef ETHOSU_SCHED_U85_H
#define ETHOSU_SCHED_U85_H

#include "ethosu_ml.h"

struct ethosu_block_config find_block_config_u85(struct ethosu_subgraph *subgraph,
                                                 struct ethosu_operation *operation);

#endif /* ETHOSU_SCHED_U85_H */
