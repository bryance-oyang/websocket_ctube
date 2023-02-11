/**
 * @file
 * @brief random simulation to generator pretty data for example
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <float.h>
#include <math.h>

#include "simulation.h"

#define SQR(x) ((x)*(x))

static float t;
static float *grid, *prev_grid;
static uint8_t *img_data;

static inline void mkimg();

int simulation_init()
{
	grid = malloc(GRID_SIDE*GRID_SIDE*sizeof(*grid));
	if (grid == NULL)
		goto err_nogrid;

	prev_grid = malloc(GRID_SIDE*GRID_SIDE*sizeof(*prev_grid));
	if (prev_grid == NULL)
		goto err_noprevgrid;

	img_data = malloc(GRID_SIDE*GRID_SIDE*sizeof(*img_data));
	if (img_data == NULL)
		goto err_noimgdata;

	t = 0;
	for (int i = 0; i < GRID_SIDE*GRID_SIDE; i++) {
		prev_grid[i] = 0;
		grid[i] = 0;
	}

	return 0;

err_noimgdata:
	free(prev_grid);
err_noprevgrid:
	free(grid);
err_nogrid:
	return -1;
}

void simulation_destroy()
{
	free(grid);
	free(prev_grid);
	free(img_data);
}

static inline float heat_src(float t, int i, int j)
{
	float icenter = GRID_SIDE * (0.5 + 0.3*cos(0.7*t / GRID_SIDE));
	float jcenter = GRID_SIDE * (0.5 + 0.3*sin(0.5*t / GRID_SIDE));
	return cosf(0.3*t / GRID_SIDE) * expf(-(SQR(i - icenter) + SQR(j - jcenter)) / (2 * SQR(GRID_SIDE/20)));
}

void *simulation_step()
{
	for (int i = 1; i < GRID_SIDE - 1; i++) {
		for (int j = 1; j < GRID_SIDE - 1; j++) {
			grid[GRID_SIDE*i + j] = (
				grid[GRID_SIDE*(i+1) + (j+0)] +
				grid[GRID_SIDE*(i-1) + (j+0)] +
				grid[GRID_SIDE*(i+0) + (j+1)] +
				grid[GRID_SIDE*(i+0) + (j-1)]) / 4 +
				heat_src(t, i, j);
		}
	}

	mkimg();

	float *tmp = grid;
	grid = prev_grid;
	prev_grid = tmp;

	t += 1;
	usleep(3000);
	return (void *)img_data;
}

static inline void get_minmax_cell(float *min, float *max)
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

static inline uint8_t clip(float x)
{
	if (x < 0)
		return 0;
	if (x > 255)
		return 255;
	return x;
}

/** map cell data to 0-255 */
static inline void mkimg()
{
	float min, max;
	//get_minmax_cell(&min, &max);
	min = -GRID_SIDE / 4;
	max = GRID_SIDE / 4;

	for (int i = 0; i < GRID_SIDE*GRID_SIDE; i++) {
		img_data[i] = clip(255 * ((grid[i] - min) / (max - min)));
	}
}
