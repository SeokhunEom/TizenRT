/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/****************************************************************************
 * libc/math/lib_truncl.c
 *
 * This implementation is derived from the musl library under the MIT License
 *
 * Copyright © 2005-2014 Rich Felker, et al.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 ****************************************************************************/

/************************************************************************
 * Included Files
 ************************************************************************/

#include <tinyara/compiler.h>

#include <math.h>

#include "libm.h"

/************************************************************************
 * Public Functions
 ************************************************************************/

#ifdef CONFIG_HAVE_LONG_DOUBLE
#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double truncl(long double x)
{
	return (long double)trunc((double)x);
}
#elif (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
static const long double toint = 1 / LDBL_EPSILON;

long double truncl(long double x)
{
	union ldshape u = { .f = x };
	int e = u.i.se & 0x7fff;
	int s = u.i.se >> 15;
	long double y;
	volatile long double __x;

	if (e >= 0x3fff + LDBL_MANT_DIG - 1) {
		return x;
	}

	if (e <= 0x3fff - 1) {
		/* Force Eval */

		__x = (x + 0x1p120f);
		(void)__x;
		return x * 0;
	}

	/* y = int(|x|) - |x|, where int(|x|) is an integer neighbor of |x| */

	if (s) {
		x = -x;
	}

	y = x + toint - toint - x;
	if (y > 0) {
		y -= 1;
	}

	x += y;
	return s ? -x : x;
}
#else
#error Unsupported long double representation
#endif
#endif
