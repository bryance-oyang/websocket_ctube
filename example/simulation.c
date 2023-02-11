/**
 * @file
 * @brief random simulation to generator pretty data for example
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <float.h>

#include "simulation.h"

static int t;
static float *grid;
static uint8_t *img_data;

static void get_minmax_cell(float *min, float *max);
static void mkimg();

int simulation_init()
{
	grid = malloc(GRID_SIDE*GRID_SIDE*sizeof(*grid));
	if (grid == NULL)
		goto err_nogrid;

	img_data = malloc(GRID_SIDE*GRID_SIDE*sizeof(*img_data));
	if (img_data == NULL)
		goto err_noimgdata;

	return 0;

err_noimgdata:
	free(grid);
err_nogrid:
	return -1;
}

void simulation_destroy()
{
	free(grid);
	free(img_data);
}

void *simulation_step()
{
	for (int i = 1; i < GRID_SIDE - 1; i++) {
		for (int j = 1; j < GRID_SIDE - 1; j++) {
			grid[GRID_SIDE*i + j] = (t + j) % GRID_SIDE;
		}
	}

	mkimg();
	t += 1;
	sleep(1);
	return (void *)img_data;
}

static void get_minmax_cell(float *min, float *max)
{
	*min = FLT_MAX;
	*max = -FLT_MAX;
	for (int i = 0; i < GRID_SIDE*GRID_SIDE; i++) {
		if (grid[i] < *min) {
			*min = grid[i];
		}
		if (grid[i] > *max) {
			*max = grid[i];
		}
	}
}

/** map cell data to 0-255 */
static void mkimg()
{
	float min, max;
	get_minmax_cell(&min, &max);

	for (int i = 0; i < GRID_SIDE*GRID_SIDE; i++) {
		img_data[i] = (uint8_t)((float)255 * ((grid[i] - min) / (max - min)));
	}
}
