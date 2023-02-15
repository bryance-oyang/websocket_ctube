/**
 * @file
 * @brief random simulation to make pretty data for demo
 *
 * Barebones heat equation solver: blowtorch adds heat to metal plate which is
 * also actively cooled
 */

#include <stdlib.h>
#include <stdint.h>
#include <float.h>
#include <time.h>
#include <math.h>

#include "simulation.h"
#include "color.h"

#define SQR(x) ((x)*(x))

static float t; // time
static float *grid, *prev_grid; // simulation grid
static pthread_mutex_t img_mutex; // protects img_data
static uint8_t *img_data; // simulation grid mapped to image rgb [0,255]

const int low_temperature = 600; // for color mapping
const int high_temperature = 3000; // for color mapping
struct blackbody_RGB_8_table blackbody_color_table; // physically computed blackbody sRGB

int simulation_init(pthread_mutex_t **data_mutex)
{
	grid = (typeof(grid))malloc(GRID_SIDE*GRID_SIDE*sizeof(*grid));
	if (grid == NULL)
		goto err_nogrid;

	prev_grid = (typeof(prev_grid))malloc(GRID_SIDE*GRID_SIDE*sizeof(*prev_grid));
	if (prev_grid == NULL)
		goto err_noprevgrid;

	img_data = (typeof(img_data))malloc(3*GRID_SIDE*GRID_SIDE*sizeof(*img_data));
	if (img_data == NULL)
		goto err_noimgdata;

	if (blackbody_RGB_8_table_init(&blackbody_color_table, low_temperature, high_temperature) != 0)
		goto err_nocolor;

	pthread_mutex_init(&img_mutex, NULL);
	*data_mutex = &img_mutex;

	/* simulation var init */
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
	blackbody_RGB_8_table_destroy(&blackbody_color_table);
	pthread_mutex_destroy(&img_mutex);
}

static float heat_src(float t, int i, int j)
{
	float icenter = GRID_SIDE * (0.5 + 0.3*cosf(1.9*t / GRID_SIDE));
	float jcenter = GRID_SIDE * (0.5 + 0.3*sinf(1.5*t / GRID_SIDE));
	return (cosf(1.2*t / GRID_SIDE) + 1)
		* expf(-(SQR(i - icenter) + SQR(j - jcenter)) / (2 * SQR(GRID_SIDE/25)));
}

static void mkimg();

void simulation_step(void **data, size_t *data_bytes)
{
	/* heat diffusion */
	for (int i = 1; i < GRID_SIDE - 1; i++) {
		for (int j = 1; j < GRID_SIDE - 1; j++) {
			grid[GRID_SIDE*i + j] = 0.25 * (
				prev_grid[GRID_SIDE*(i+1) + (j+0)] +
				prev_grid[GRID_SIDE*(i-1) + (j+0)] +
				prev_grid[GRID_SIDE*(i+0) + (j+1)] +
				prev_grid[GRID_SIDE*(i+0) + (j-1)]);
		}
	}

	/* heating/cooling */
	for (int i = 1; i < GRID_SIDE - 1; i++) {
		for (int j = 1; j < GRID_SIDE - 1; j++) {
			grid[GRID_SIDE*i + j] += heat_src(t, i, j);
			grid[GRID_SIDE*i + j] *= 0.999;
		}
	}

	mkimg();

	/* swap grids */
	float *tmp = grid;
	grid = prev_grid;
	prev_grid = tmp;

	t += 1;

	/* make simulation slow xD */
	struct timespec sleep_time;
	sleep_time.tv_sec = 0;
	sleep_time.tv_nsec = 2000000;
	nanosleep(&sleep_time, NULL);

	*data = (void *)img_data;
	*data_bytes = 3*GRID_SIDE*GRID_SIDE;
}

static int clip(int x, int min, int max)
{
	if (x < min)
		return min;
	if (x > max)
		return max;
	return x;
}

/** map cell data to physically computed sRGB blackbody color */
static void mkimg()
{
	struct color_RGB_8 *srgb;
	const float min = 0;
	const float max = GRID_SIDE / 4;

	pthread_mutex_lock(&img_mutex);
	for (int i = 0; i < GRID_SIDE*GRID_SIDE; i++) {
		/* linearly map simulation grid data to a temperature */
		int temperature = (grid[i] - min) * (high_temperature - low_temperature) / (max - min) + low_temperature;
		temperature = clip(temperature, low_temperature, high_temperature);

		/* img_data to blackbody color corresponding to temperature */
		srgb = &blackbody_color_table.colors[temperature - low_temperature];
		img_data[3*i + 0] = srgb->RGB[0];
		img_data[3*i + 1] = srgb->RGB[1];
		img_data[3*i + 2] = srgb->RGB[2];
	}
	pthread_mutex_unlock(&img_mutex);
}
