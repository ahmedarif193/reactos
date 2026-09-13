/*
 * Copyright © 2019 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef IRIS_PERF_H
#define IRIS_PERF_H

#include "perf/intel_perf.h"
#include "perf/intel_perf_query.h"

void iris_perf_init_vtbl(struct intel_perf_config *cfg);

#endif /* IRIS_PERF_H */
