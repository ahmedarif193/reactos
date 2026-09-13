/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef ETHOSU_PUBLIC_H
#define ETHOSU_PUBLIC_H

struct pipe_ml_device;

struct pipe_ml_device *ethosu_ml_device_create(const char *spec);

#endif /* ETHOSU_PUBLIC_H */
