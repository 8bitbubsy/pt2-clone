/* Approximation routines for sin/cos/sqrt/tan.
** These are not really fast, and shouldn't be used when speed is important.
*/

#include <stdint.h>
#include <math.h> // fmod()
#include "pt2_math.h"

static const double pi = PT2_PI;
static const double twopi = PT2_TWO_PI;
static const double four_over_pi = 4.0 / PT2_PI;
static const double threehalfpi = (3.0 * PT2_PI) / 2.0;
static const double halfpi = PT2_PI / 2.0;

static double cosTaylorSeries(double x)
{
#define ITERATIONS 32 /* good enough... */

	x = fmod(x, twopi);
	if (x < 0.0)
		x = -x;

	double tmp = 1.0;
	double sum = 1.0;

	for (double i = 2.0; i <= ITERATIONS*2.0; i += 2.0)
	{
		tmp *= -(x*x) / (i * (i-1.0));
		sum += tmp;
	}

	return sum;
}

static double tan_14s(double x)
{
	const double c1 = -34287.4662577359568109624;
	const double c2 =   2566.7175462315050423295;
	const double c3 =    -26.5366371951731325438;
	const double c4 = -43656.1579281292375769579;
	const double c5 =  12244.4839556747426927793;
	const double c6 =   -336.611376245464339493;

	double x2 = x * x;
	return x*(c1 + x2*(c2 + x2*c3))/(c4 + x2*(c5 + x2*(c6 + x2)));
}

double pt2_tan(double x)
{
	x = fmod(x, twopi);

	const int32_t octant = (int32_t)(x * four_over_pi);
	switch (octant)
	{
		default:
		case 0: return      tan_14s(x               * four_over_pi);
		case 1: return  1.0/tan_14s((halfpi-x)      * four_over_pi);
		case 2: return -1.0/tan_14s((x-halfpi)      * four_over_pi);
		case 3: return     -tan_14s((pi-x)          * four_over_pi);
		case 4: return      tan_14s((x-pi)          * four_over_pi);
		case 5: return  1.0/tan_14s((threehalfpi-x) * four_over_pi);
		case 6: return -1.0/tan_14s((x-threehalfpi) * four_over_pi);
		case 7: return     -tan_14s((twopi-x)       * four_over_pi);
	}
}

double pt2_sqrt(double x)
{
	double number = x;
	double s = number / 2.5;

	double old = 0.0;
	while (s != old)
	{
		old = s;
		s = (number / old + old) / 2.0;
	}
 
	return s;
}

double pt2_cos(double x)
{
	return cosTaylorSeries(x);
}

double pt2_sin(double x)
{
	return cosTaylorSeries(halfpi-x);
}
