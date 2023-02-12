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
#include "color.h"

#define SQR(x) ((x)*(x))

static float t; // time
static float *grid, *prev_grid; // simulation grid
static uint8_t *img_data; // simulation grid mapped to image rgb [0,255]
struct color_physical pcolor;

static inline void mkimg();

int simulation_init()
{
	grid = malloc(GRID_SIDE*GRID_SIDE*sizeof(*grid));
	if (grid == NULL)
		goto err_nogrid;

	prev_grid = malloc(GRID_SIDE*GRID_SIDE*sizeof(*prev_grid));
	if (prev_grid == NULL)
		goto err_noprevgrid;

	img_data = malloc(3*GRID_SIDE*GRID_SIDE*sizeof(*img_data));
	if (img_data == NULL)
		goto err_noimgdata;

	if (color_physical_init(&pcolor, 32) != 0)
		goto err_nocolor;

	t = 0;
	for (int i = 0; i < GRID_SIDE*GRID_SIDE; i++) {
		prev_grid[i] = 0;
		grid[i] = 0;
	}

	return 0;

err_nocolor:
	free(img_data);
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
	color_physical_destroy(&pcolor);
}

static inline float heat_src(float t, int i, int j)
{
	float icenter = GRID_SIDE * (0.5 + 0.3*cosf(1.9*t / GRID_SIDE));
	float jcenter = GRID_SIDE * (0.5 + 0.3*sinf(1.5*t / GRID_SIDE));
	return (cosf(1.2*t / GRID_SIDE) + 1) * expf(-(SQR(i - icenter) + SQR(j - jcenter)) / (2 * SQR(GRID_SIDE/20)));
}

void *simulation_step(size_t *data_bytes)
{
	*data_bytes = 3*GRID_SIDE*GRID_SIDE;

	for (int i = 1; i < GRID_SIDE - 1; i++) {
		for (int j = 1; j < GRID_SIDE - 1; j++) {
			grid[GRID_SIDE*i + j] = 0.25 * (
				prev_grid[GRID_SIDE*(i+1) + (j+0)] +
				prev_grid[GRID_SIDE*(i-1) + (j+0)] +
				prev_grid[GRID_SIDE*(i+0) + (j+1)] +
				prev_grid[GRID_SIDE*(i+0) + (j-1)]) +
				heat_src(t, i, j);
		}
	}
	for (int i = 1; i < GRID_SIDE - 1; i++) {
		for (int j = 1; j < GRID_SIDE - 1; j++) {
			grid[GRID_SIDE*i + j] *= 0.999;
		}
	}

	mkimg();

	float *tmp = grid;
	grid = prev_grid;
	prev_grid = tmp;

	t += 1;
	usleep(1000);
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

static inline float clipf(float x, float min, float max)
{
	if (x < min)
		return min;
	if (x > max)
		return max;
	return x;
}

/** map cell data to 0-255 */
static inline void mkimg()
{
	struct color_RGB_8 srgb;
	const float min = 0;
	const float max = GRID_SIDE / 4;
	const float Tmin = 600;
	const float Tmax = 4300;

	for (int i = 0; i < GRID_SIDE*GRID_SIDE; i++) {
		/* linearly map simulation grid data to a temperature */
		float temperature = (clipf(grid[i], min, max) - min) * (Tmax - Tmin) / (max - min) + Tmin;

		/* img_data to blackbody color corresponding to temperature */
		blackbody_to_physical(temperature, &pcolor);
		physical_to_RGB_8(&pcolor, &srgb);
		img_data[3*i + 0] = srgb.RGB[0];
		img_data[3*i + 1] = srgb.RGB[1];
		img_data[3*i + 2] = srgb.RGB[2];
	}
}
