/*
 * Copyright (c) 2023 Bryance Oyang
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file
 * @brief color space operations
 *
 * References
 * https://en.wikipedia.org/wiki/CIE_1931_color_space
 * https://en.wikipedia.org/wiki/SRGB
 */

#ifndef COLOR_H
#define COLOR_H

#include <stddef.h>
#include <stdint.h>
#include <math.h>

#define LIGHT_SPEED 299792458.0
#define PLANK_H 6.626e-34
#define BOLTZMANN_K 1.38e-23

/** CIE 1931 XYZ color space */
struct color_XYZ {
	double XYZ[3];
};

/** sRGB floating point */
struct color_RGB {
	double RGB[3];
};

/** sRGB (0-255) */
struct color_RGB_8 {
	uint8_t RGB[3];
};

/** wavelength to physical radiance */
struct color_physical {
	double *wavelen;
	double *radiance;
	size_t npoints;
};

/** precomputed sRGB colors for blackbody */
struct blackbody_RGB_8_table {
	int *temperatures;
	struct color_RGB_8 *colors;
	size_t npoints;
};

/** npoints of wavelength from 400-700nm */
static inline int color_physical_init(struct color_physical *p, size_t npoints)
{
	double slope;

	p->npoints = npoints;

	p->wavelen = (typeof(p->wavelen))malloc(npoints * sizeof(*p->wavelen));
	if (p->wavelen == NULL)
		goto err_nowavelen;

	p->radiance = (typeof(p->radiance))malloc(npoints * sizeof(*p->radiance));
	if (p->wavelen == NULL)
		goto err_noradiance;

	/* interpolate wavelen 400-700 nanometers */
	slope = (700.0 - 400.0) / (npoints - 1);
	for (size_t i = 0; i < npoints; i++) {
		p->wavelen[i] = slope * i + 400.0;
		p->radiance[i] = 0;
	}

	return 0;

err_noradiance:
	free(p->wavelen);
err_nowavelen:
	return -1;
}

static inline void color_physical_destroy(struct color_physical *p)
{
	p->npoints = 0;
	free(p->wavelen);
	free(p->radiance);
}

static inline double gamma_correct(double rgb_lin)
{
	if (rgb_lin <= 0.0031308) {
		return 12.92 * rgb_lin;
	} else {
		return 1.055 * pow(rgb_lin, 1.0/2.4) - 0.055;
	}
}

static inline void XYZ_normalize(struct color_XYZ *c)
{
	double sum = c->XYZ[0] + c->XYZ[1] + c->XYZ[2];
	c->XYZ[0] /= sum;
	c->XYZ[1] /= sum;
	c->XYZ[2] /= sum;
}

static inline void XYZ_to_RGB(const struct color_XYZ *in, struct color_RGB *out)
{
	double lin[3];
	lin[0] = 3.2406 * in->XYZ[0] - 1.5372 * in->XYZ[1] - 0.4986 * in->XYZ[2];
	lin[1] = -0.9689 * in->XYZ[0] + 1.8758 * in->XYZ[1] + 0.0415 * in->XYZ[2];
	lin[2] = 0.0557 * in->XYZ[0] - 0.2040 * in->XYZ[1] + 1.0570 * in->XYZ[2];

	for (int i = 0; i < 3; i++) {
		out->RGB[i] = gamma_correct(lin[i]);
	}
}

static inline void RGB_to_uint8(const struct color_RGB *in, struct color_RGB_8 *out)
{
	for (int i = 0; i < 3; i++) {
		out->RGB[i] = (uint8_t)(fmin(1.0, fmax(0.0, in->RGB[i])) * 255.1);
	}
}

static inline double color_piecewise_gauss(double x, double mu, double s1, double s2)
{
	if (x < mu) {
		return exp(-(x - mu)*(x - mu) / (2*s1*s1));
	} else {
		return exp(-(x - mu)*(x - mu) / (2*s2*s2));
	}
}

static inline void color_xyzbar(double wavelen, double *xyzbar)
{
	xyzbar[0] = 1.056 * color_piecewise_gauss(wavelen, 599.8, 37.9, 31.0)
		+ 0.362 * color_piecewise_gauss(wavelen, 442.0, 16.0, 26.7)
		- 0.065 * color_piecewise_gauss(wavelen, 501.1, 20.4, 26.2);

	xyzbar[1] = 0.821 * color_piecewise_gauss(wavelen, 568.8, 46.9, 40.5)
		+ 0.286 * color_piecewise_gauss(wavelen, 530.9, 16.3, 31.1);

	xyzbar[2] = 1.217 * color_piecewise_gauss(wavelen, 437.0, 11.8, 36.0)
		+ 0.681 * color_piecewise_gauss(wavelen, 459.0, 26.0, 13.8);
}

static inline void physical_to_XYZ(const struct color_physical *in, struct color_XYZ *out)
{
	double dl;
	double xyzbar1[3];
	double xyzbar2[3];
	double *xyzbar_lower, *xyzbar_upper, *tmp;

	for (int j = 0; j < 3; j++) {
		out->XYZ[j] = 0;
	}

	/* trapezoid integral of (radiance * xyzbar) * dwavelen */
	xyzbar_lower = xyzbar1;
	xyzbar_upper = xyzbar2;
	color_xyzbar(in->wavelen[0], xyzbar_lower);
	for (size_t i = 0; i < in->npoints - 1; i++) {
		color_xyzbar(in->wavelen[i+1], xyzbar_upper);
		dl = in->wavelen[i+1] - in->wavelen[i];

		for (int j = 0; j < 3; j++) {
			out->XYZ[j] += dl/2 * (in->radiance[i]*xyzbar_lower[j]
				+ in->radiance[i+1]*xyzbar_upper[j]);
		}

		tmp = xyzbar_lower;
		xyzbar_lower = xyzbar_upper;
		xyzbar_upper = tmp;
	}
}

static inline void physical_to_RGB_8(const struct color_physical *in, struct color_RGB_8 *out)
{
	struct color_XYZ XYZ;
	struct color_RGB RGB;

	physical_to_XYZ(in, &XYZ);
	XYZ_to_RGB(&XYZ, &RGB);
	RGB_to_uint8(&RGB, out);
}

static inline void blackbody_to_physical(const double temperature, struct color_physical *out)
{
	for (size_t i = 0; i < out->npoints; i++) {
		const double l = out->wavelen[i] * 1e-9;
		const double coeff = 2 * PLANK_H * pow(LIGHT_SPEED, 2) / pow(l, 5);
		const double stat = 1.0 / (exp(PLANK_H * LIGHT_SPEED / (l * BOLTZMANN_K * temperature)) - 1);

		out->radiance[i] = coeff * stat;

		/* make less big */
		out->radiance[i] *= 1e-12;
	}
}

/** make table of blackbody colors at integer increments of temperature */
static inline int blackbody_RGB_8_table_init(struct blackbody_RGB_8_table *table, int low_temperature, int high_temperature)
{
	if (high_temperature < low_temperature) {
		return -1;
	}

	table->npoints = high_temperature - low_temperature + 1;

	table->temperatures = (typeof(table->temperatures))malloc(table->npoints * sizeof(*table->temperatures));
	if (table->temperatures == NULL) {
		goto err_notemp;
	}

	table->colors = (typeof(table->colors))malloc(table->npoints * sizeof(*table->colors));
	if (table->colors == NULL) {
		goto err_nocolors;
	}

	struct color_physical physical;
	if (color_physical_init(&physical, 1024) != 0) {
		goto err_nophys;
	}

	/* compute sRGB colors for blackbody temperatures */
	for (size_t i = 0; i < table->npoints; i++) {
		int temperature = low_temperature + i;
		table->temperatures[i] = temperature;

		blackbody_to_physical(temperature, &physical);
		physical_to_RGB_8(&physical, &table->colors[i]);
	}

	color_physical_destroy(&physical);
	return 0;

err_nophys:
	free(table->colors);
err_nocolors:
	free(table->temperatures);
err_notemp:
	return -1;
}

void blackbody_RGB_8_table_destroy(struct blackbody_RGB_8_table *table)
{
	free(table->temperatures);
	free(table->colors);
	table->npoints = 0;
}

#endif /* COLOR_H */
