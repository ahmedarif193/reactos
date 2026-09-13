/*
 * Copyright © 2019 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef IRIS_MONITOR_H
#define IRIS_MONITOR_H

#include "pipe/p_screen.h"

int iris_get_monitor_info(struct pipe_screen *pscreen, unsigned index,
                          struct pipe_driver_query_info *info);
int iris_get_monitor_group_info(struct pipe_screen *pscreen,
                                unsigned index,
                                struct pipe_driver_query_group_info *info);

struct iris_context;
struct iris_screen;

struct iris_monitor_object *
iris_create_monitor_object(struct iris_context *ice,
                           unsigned num_queries,
                           unsigned *query_types);

struct pipe_query;
void iris_destroy_monitor_object(struct pipe_context *ctx,
                                 struct iris_monitor_object *monitor);

bool
iris_begin_monitor(struct pipe_context *ctx,
                   struct iris_monitor_object *monitor);
bool
iris_end_monitor(struct pipe_context *ctx,
                 struct iris_monitor_object *monitor);

bool
iris_get_monitor_result(struct pipe_context *ctx,
                        struct iris_monitor_object *monitor,
                        bool wait,
                        union pipe_numeric_type_union *result);

#endif
