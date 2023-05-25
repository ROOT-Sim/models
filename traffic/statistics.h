#pragma once

#include <ROOT-Sim/random.h>

extern double Gaussian(struct rng_t *seed, double m, double s);
extern double contour_cdf(double min, double max, double mean, double var);
