// BFME: E1-49. core_wwmath with SAGE_USE_DETERMINISTIC_MATH=ON compiles this
// header. GameMath.h must be included outside class WWMath; a namespace
// inside the class is "expected unqualified-id before 'namespace'".

#define USE_DETERMINISTIC_MATH 1
#include "wwmath.h"

float bfme_wwmath_on_anchor(float x)
{
	return WWMath::SinTrig(x);
}
