/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2014 Atronix Engineering, Inc
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  f-int.c
**  Summary: integer arithmetic functions
**  Section: functional
**  Author:  Shixin Zeng
**  Notes: Based on original code in t-integer.c
**
***********************************************************************/

#include "reb-c.h"

REBOOL reb_i32_add_overflow(i32 x, i32 y, i32 *sum)
{
	*sum = (i64)x + (i64)y;
	if (*sum > MAX_I32 || *sum < MIN_I32) return TRUE;
	return FALSE;
}

REBOOL reb_i64_add_overflow(i64 x, i64 y, i64 *sum)
{
	*sum = (REBU64)x + (REBU64)y; /* never overflow with unsigned integers*/
	if (((x < 0) == (y < 0))
		&& ((x < 0) != (*sum < 0))) return TRUE;
	return FALSE;
}

REBOOL reb_i32_sub_overflow(i32 x, i32 y, i32 *diff)
{
	*diff = (i64)x - (i64)y;
	if (((x < 0) != (y < 0)) && ((x < 0) != (*diff < 0))) return TRUE;

	return FALSE;
}

REBOOL reb_i64_sub_overflow(i64 x, i64 y, i64 *diff)
{
	*diff = (REBU64)x - (REBU64)y;
	if (((x < 0) != (y < 0)) && ((x < 0) != (*diff < 0))) return TRUE;

	return FALSE;
}

REBOOL reb_i32_mul_overflow(i32 x, i32 y, i32 *prod)
{
	i64 p = (i64)x * (i64)y;
	if (p > MAX_I32 || p < MIN_I32) return TRUE;
	*prod = p;
	return FALSE;
}

REBOOL reb_i64_mul_overflow(i64 x, i64 y, i64 *prod)
{
	REBCNT x1, x0, y1, y0;
	REBFLG sgn;
	i64 p = 0;

	sgn = (x < 0);
	if (sgn) x = -x;
	if (y < 0) {
		sgn = !sgn;
		y = -y;
	}
	p = x * y;
	x1 = x >> 32;
	x0 = x;
	y1 = y >> 32;
	y0 = y;
	if ((x1 && y1)
		|| ((REBU64)x0 * y1 + (REBU64)x1 * y0 > p >> 32)
		|| ((p > (REBU64)MAX_I64) && (!sgn || (p > -(REBU64)MIN_I64)))) return TRUE;

	*prod = sgn? -p : p;
	return FALSE;
}
