/**
 * Copyright (c) 2023 Bryance Oyang
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef SIMULATION_H
#define SIMULATION_H

#include <pthread.h>

#define GRID_SIDE 100

int simulation_init(pthread_mutex_t **data_mutex);
void simulation_destroy();
void simulation_step(void **data, size_t *data_bytes);

#endif /* SIMULATION_H */
