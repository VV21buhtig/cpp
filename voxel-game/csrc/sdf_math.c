#include "sdf_math.h"
#include <math.h>

float v3_len(Vec3 a) { return sqrtf(a.x*a.x + a.y*a.y + a.z*a.z); }
