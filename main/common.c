#include "common.h"

#include <math.h>

/* Source: http://www.movable-type.co.uk/scripts/latlong.html */
double calc_distance (double latA, double lngA,
											double latB, double lngB) {

	const double r = 6378.137;					/* earth's radius */

	latA *= M_PI/180.;
	latB *= M_PI/180.;
	lngA *= M_PI/180.;
	lngB *= M_PI/180.;
	double dphi = (latB - latA);
	double dlam = (lngB - lngA);

	double a = sin (dphi / 2) * sin (dphi / 2) +
		cos (latA) * cos (latB) * sin (dlam/2) * sin (dlam / 2);

	double d = r * 2 * atan2 (sqrt (a), sqrt (1-a));

	return d * 1000. ;  				/* in meters */
}
