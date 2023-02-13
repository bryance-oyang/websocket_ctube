#ifndef SIMULATION_H
#define SIMULATION_H

#include <pthread.h>

#define GRID_SIDE 100

int simulation_init(pthread_mutex_t **data_mutex);
void simulation_destroy();
void simulation_step(void **data, size_t *data_bytes);

#endif /* SIMULATION_H */
