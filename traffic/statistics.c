#include "statistics.h"

#include <ROOT-Sim/random.h>
#include <assert.h>
#include <float.h>
#include <math.h>

#define SQRT_2 1.41421356237309504880168872421      /* \sqrt(2) */
#define _1_SQRT_2PI 0.39894228040143267793994605993 /* 1/\sqrt(2\pi) */

double Gaussian(struct rng_t *seed, double m, double s)
/* ========================================================================
 * Returns a normal (Gaussian) distributed real number.
 * NOTE: use s > 0.0
 *
 * Uses a very accurate approximation of the normal idf due to Odeh & Evans,
 * J. Applied Statistics, 1974, vol 23, pp 96-97.
 * ========================================================================
 * copied from http://www.cs.wm.edu/~va/software/park/
 */
{
	const double p0 = 0.322232431088;
	const double q0 = 0.099348462606;
	const double p1 = 1.0;
	const double q1 = 0.588581570495;
	const double p2 = 0.342242088547;
	const double q2 = 0.531103462366;
	const double p3 = 0.204231210245e-1;
	const double q3 = 0.103537752850;
	const double p4 = 0.453642210148e-4;
	const double q4 = 0.385607006340e-2;
	double u, t, p, q, z;

	u = Random(seed);
	if(u < 0.5)
		t = sqrt(-2.0 * log(u));
	else
		t = sqrt(-2.0 * log(1.0 - u));
	p = p0 + t * (p1 + t * (p2 + t * (p3 + t * p4)));
	q = q0 + t * (q1 + t * (q2 + t * (q3 + t * q4)));

	if(u < 0.5)
		z = (p / q) - t;
	else
		z = t - (p / q);

	return (m + s * z);
}


/*----------------------------------------------------------------------
References: - W.J. Cody.
              Rational Chebyshev Approximations for the Error Function.
              Mathematics of Computation, 23(107):631-637, 1969
            - W. Fraser and J.F Hart.
              On the Computation of Rational Approximations
              to Continuous Functions.
              Communications of the ACM 5, 1962
            - W.J. Kennedy Jr. and J.E. Gentle.
              Statistical Computing.
              Marcel Dekker, 1980
Implemented by: Christian Borgelt
----------------------------------------------------------------------*/
static double unitcdf(double x)
{
	double y, z, u; /* square, absolute value of x */

	if(x > 8.572)
		return 1.0; /* if outside proper interval, */
	if(x < -37.519)
		return 0.0; /* return the limiting values */
	z = fabs(x);        /* exploit the symmetry */
	if(z < 0.5 * 2.2204460492503131e-16)
		return 0.5; /* treat 0 as a special case */
	if(z < 0.66291) {   /* if fairly close to zero */
		y = x * x;  /* compute the square of x */
		z = ((((0.065682337918207449113 * y + 2.2352520354606839287) * y + 161.02823106855587881) * y +
			 1067.6894854603709582) *
			    y +
			18154.981253343561249) /
		    ((((y + 47.20258190468824187) * y + 976.09855173777669322) * y + 10260.932208618978205) * y +
			45507.789335026729956);
		return 0.5 + x * z; /* compute with rational function */
	}
	if(z < 4 * SQRT_2) {        /* if medium value */
		u = ((((((((1.0765576773720192317e-8 * z + 0.39894151208813466764) * z + 8.8831497943883759412) * z +
			     93.506656132177855979) *
				z +
			    597.27027639480026226) *
			       z +
			   2494.5375852903726711) *
			      z +
			  6848.1904505362823326) *
			     z +
			 11602.651437647350124) *
			    z +
			9842.7148383839780218) /
		    ((((((((z + 22.266688044328115691) * z + 235.38790178262499861) * z + 1519.377599407554805) * z +
			    6485.558298266760755) *
			       z +
			   18615.571640885098091) *
			      z +
			  34900.952721145977266) *
			     z +
			 38912.003286093271411) *
			    z +
			19685.429676859990727);
		z = u * exp(-0.5 * x * x);
	}                        /* compute with rational function */
	else {                   /* if tail value */
		y = 1 / (x * x); /* compute the inverse square of x */
		u = (((((0.02307344176494017303 * y + 0.21589853405795699) * y + 0.1274011611602473639) * y +
			  0.022235277870649807) *
			     y +
			 0.001421619193227893466) *
			    y +
			2.9112874951168792e-5) /
		    (((((y + 1.28426009614491121) * y + 0.468238212480865118) * y + 0.0659881378689285515) * y +
			 0.00378239633202758244) *
			    y +
			7.29751555083966205e-5);
		z = (((_1_SQRT_2PI)-y * u) / z) * exp(-0.5 * x * x);
	}                           /* compute with rational function */
	return (x > 0) ? 1 - z : z; /* exploit the symmetry */
}

// Transform the distribution from the one given by the requested parameters into the
// equivalent one centered in 0, having variance = 1
static double normcdf(double x, double mean, double var)
{
	assert(var >= 0); /* check the function arguments */
	if(var <= 0.0) {
		return (x >= mean) ? 1.0 : 0.0;
	}
	if(fabs(var - 1.0) < DBL_EPSILON) {
		return unitcdf(x - mean);
	}
	return unitcdf((x - mean) / sqrt(var));
}

double contour_cdf(double min, double max, double mean, double var)
{
	double minCDF, maxCDF;

	minCDF = normcdf(min, mean, var);
	maxCDF = normcdf(max, mean, var);

	return (maxCDF - minCDF);
}
