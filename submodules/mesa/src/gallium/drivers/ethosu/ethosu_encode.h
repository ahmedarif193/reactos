/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef ETHOSU_ENCODE_H
#define ETHOSU_ENCODE_H

#include "ethosu_ml.h"

#ifdef __cplusplus
extern "C" {
#endif

void
ml_reorder_encode_weights(struct ethosu_subgraph *subgraph,
                          struct ethosu_operation *operation,
                          const uint8_t *input_weights,
                          long input_weights_size,
                          uint8_t **weights,
                          long *weights_size);

#ifdef __cplusplus
}
#endif

#endif /* ETHOSU_ENCODE_H */
