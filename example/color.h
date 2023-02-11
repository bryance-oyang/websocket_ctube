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

struct color_XYZ {
	double XYZ[3];
};

struct color_RGB {
	double RGB[3];
};

struct color_RGB_8 {
	uint8_t RGB[3];
};

struct color_physical {
	double *wavelen;
	double *radiance;
	size_t npoints;
};

static inline int color_physical_init(struct color_physical *p, size_t npoints)
{
	p->npoints = npoints;

	p->wavelen = malloc(npoints * sizeof(*p->wavelen));
	if (p->wavelen == NULL)
		goto err_nowavelen;

	p->radiance = malloc(npoints * sizeof(*p->radiance));
	if (p->wavelen == NULL)
		goto err_noradiance;


	/* interpolate wavelen 400-700 nanometers */
	double slope = (700.0 - 400.0) / (npoints - 1);
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

static inline void XYZ_normalize(struct color_XYZ *restrict c)
{
	double sum = c->XYZ[0] + c->XYZ[1] + c->XYZ[2];
	c->XYZ[0] /= sum;
	c->XYZ[1] /= sum;
	c->XYZ[2] /= sum;
}

static inline void XYZ_to_RGB(const struct color_XYZ *restrict in, struct color_RGB *restrict out)
{
	double lin[3];
	lin[0] = 3.2406 * in->XYZ[0] - 1.5372 * in->XYZ[1] - 0.4986 * in->XYZ[2];
	lin[1] = -0.9689 * in->XYZ[0] + 1.8758 * in->XYZ[1] + 0.0415 * in->XYZ[2];
	lin[2] = 0.0557 * in->XYZ[0] - 0.2040 * in->XYZ[1] + 1.0570 * in->XYZ[2];

	for (int i = 0; i < 3; i++) {
		out->RGB[i] = gamma_correct(lin[i]);
	}
}

static inline void RGB_to_uint8(const struct color_RGB *restrict in, struct color_RGB_8 *restrict out)
{
	for (int i = 0; i < 3; i++) {
		out->RGB[i] = (uint8_t)(fmin(1.0, fmax(0.0, in->RGB[i])) * 255.1);
	}
}

static inline double color_piecewise_gauss(double x, double mu, double s1, double s2)
{
	if (x < mu) {
		return expf(-(x - mu)*(x - mu) / (2*s1*s1));
	} else {
		return expf(-(x - mu)*(x - mu) / (2*s2*s2));
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

static inline void physical_to_XYZ(const struct color_physical *restrict in, struct color_XYZ *restrict out)
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
			out->XYZ[j] += dl/2 * (in->radiance[i]*xyzbar_lower[j] + in->radiance[i+1]*xyzbar_upper[j]);
		}

		tmp = xyzbar_lower;
		xyzbar_lower = xyzbar_upper;
		xyzbar_upper = tmp;
	}
}

static inline void physical_to_RGB_8(const struct color_physical *restrict in, struct color_RGB_8 *restrict out)
{
	struct color_XYZ XYZ;
	struct color_RGB RGB;

	physical_to_XYZ(in, &XYZ);
	XYZ_to_RGB(&XYZ, &RGB);
	RGB_to_uint8(&RGB, out);
}

static inline void blackbody_to_physical(const double temperature, struct color_physical *restrict out)
{
	for (size_t i = 0; i < out->npoints; i++) {
		const double l = out->wavelen[i] * 1e-9;
		const double coeff = 2 * PLANK_H * pow(LIGHT_SPEED, 2) / pow(l, 5);
		const double stat = 1.0 / (exp(PLANK_H * LIGHT_SPEED / (l * BOLTZMANN_K * temperature)) - 1);

		out->radiance[i] = coeff * stat;

		/* make order of 1 */
		out->radiance[i] = 1e-13 * coeff * stat;
	}
}

#endif /* COLOR_H */
