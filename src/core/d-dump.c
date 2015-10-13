/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
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
**  Module:  d-dump.c
**  Summary: various debug output functions
**  Section: debug
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include <stdio.h>
#include "sys-core.h"

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

/***********************************************************************
**
*/	void Dump_Series(REBSER *series, const char *memo)
/*
***********************************************************************/
{
	if (!series) return;
	Debug_Fmt(
		Str_Dump, //"%s Series %x %s: Wide: %2d Size: %6d - Bias: %d Tail: %d Rest: %d Flags: %x"
		memo,
		series,
		(SERIES_LABEL(series) ? SERIES_LABEL(series) : "-"),
		SERIES_WIDE(series),
		SERIES_TOTAL(series),
		SERIES_BIAS(series),
		SERIES_TAIL(series),
		SERIES_REST(series),
		SERIES_FLAGS(series)
	);
	if (Is_Array_Series(series)) {
		Dump_Values(BLK_HEAD(series), SERIES_TAIL(series));
	} else
		Dump_Bytes(series->data, (SERIES_TAIL(series)+1) * SERIES_WIDE(series));
}

/***********************************************************************
**
*/	void Dump_Bytes(REBYTE *bp, REBCNT limit)
/*
***********************************************************************/
{
	const REBCNT max_lines = 120;
	REBYTE buf[2048];
	REBYTE str[40];
	REBYTE *cp, *tp;
	REBYTE c;
	REBCNT l, n;
	REBCNT cnt = 0;

	cp = buf;
	for (l = 0; l < max_lines; l++) {
		cp = Form_Hex_Pad(cp, (REBUPT) bp, 8);

		*cp++ = ':';
		*cp++ = ' ';
		tp = str;

		for (n = 0; n < 16; n++) {
			if (cnt++ >= limit) break;
			c = *bp++;
			cp = Form_Hex2(cp, c);
			if ((n & 3) == 3) *cp++ = ' ';
			if ((c < 32) || (c > 126)) c = '.';
			*tp++ = c;
		}

		for (; n < 16; n++) {
			c = ' ';
			*cp++ = c;
			*cp++ = c;
			if ((n & 3) == 3) *cp++ = ' ';
			if ((c < 32) || (c > 126)) c = '.';
			*tp++ = c;
		}
		*tp++ = 0;

		for (tp = str; *tp;) *cp++ = *tp++;

		*cp = 0;
		Debug_Str(s_cast(buf));
		if (cnt >= limit) break;
		cp = buf;
	}
}

/***********************************************************************
**
*/	void Dump_Values(REBVAL *vp, REBCNT count)
/*
**		Print out values in raw hex; If memory is corrupted
**		this function still needs to work.
**
***********************************************************************/
{
	REBYTE buf[2048];
	REBYTE *cp;
	REBCNT l, n;
	REBCNT *bp = (REBCNT*)vp;
	const REBYTE *type;

	cp = buf;
	for (l = 0; l < count; l++) {
		REBVAL *val = (REBVAL*)bp;
		cp = Form_Hex_Pad(cp, l, 8);

		*cp++ = ':';
		*cp++ = ' ';

		type = Get_Type_Name((REBVAL*)bp);
		for (n = 0; n < 11; n++) {
			if (*type) *cp++ = *type++;
			else *cp++ = ' ';
		}
		*cp++ = ' ';
		for (n = 0; n < sizeof(REBVAL) / sizeof(REBCNT); n++) {
			cp = Form_Hex_Pad(cp, *bp++, 8);
			*cp++ = ' ';
		}
		n = 0;
		if (IS_WORD((REBVAL*)val) || IS_GET_WORD((REBVAL*)val) || IS_SET_WORD((REBVAL*)val)) {
			const char * name = cs_cast(Get_Word_Name((REBVAL*)val));
			n = snprintf(s_cast(cp), sizeof(buf) - (cp - buf), " (%s)", name);
		}

		*(cp + n) = 0;
		Debug_Str(s_cast(buf));
		cp = buf;
	}
}


/***********************************************************************
**
*/  void Dump_Info(void)
/*
***********************************************************************/
{
	REBINT n;

	// Must be compile-time const for '= {...}' style init (-Wc99-extensions)
	REBINT nums[14];

	nums[0] = 0;
	nums[1] = 0,
	nums[2] = cast(REBINT, Eval_Cycles);
	nums[3] = Eval_Count;
	nums[4] = Eval_Dose;
	nums[5]	= Eval_Signals;
	nums[6] = Eval_Sigmask;
	nums[7] = DSP;
	nums[8] = Stack_Depth(); // DSF
	nums[9] = 0;
	nums[10] = GC_Ballast;
	nums[11] = GC_Disabled;
	nums[12] = SERIES_TAIL(GC_Series_Guard);
	nums[13] = SERIES_TAIL(GC_Value_Guard);

	for (n = 0; n < 14; n++) Debug_Fmt(cs_cast(BOOT_STR(RS_DUMP, n)), nums[n]);
}


/***********************************************************************
**
*/  void Dump_Stack(struct Reb_Call *call, REBINT dsp)
/*
***********************************************************************/
{
	REBINT n;
	REBINT m;
	REBVAL *args;

	if (!call) {
		call = DSF;
		dsp = DSP;
	}

	m = 0; // !!! dsp - dsf - DSF_SIZE
	Debug_Fmt(
		cs_cast(BOOT_STR(RS_STACK, 1)),
		dsp,
		Get_Word_Name(DSF_LABEL(call)),
		m,
		Get_Type_Name(DSF_FUNC(call))
	);

	if (call) {
		if (ANY_FUNC(DSF_FUNC(call))) {
			args = BLK_HEAD(VAL_FUNC_PARAMLIST(DSF_FUNC(call)));
			m = SERIES_TAIL(VAL_FUNC_PARAMLIST(DSF_FUNC(call)));
			for (n = 1; n < m; n++)
				Debug_Fmt("\t%s: %72r", Get_Word_Name(args+n), DSF_ARG(call, n));
		}
		//Debug_Fmt(Str_Stack[2], PRIOR_DSF(dsf));
		if (PRIOR_DSF(call)) Dump_Stack(PRIOR_DSF(call), dsp);
	}

	//for (n = 1; n <= 2; n++) {
	//	Debug_Fmt("  ARG%d: %s %r", n, Get_Type_Name(DSF_ARG(dsf, n)), DSF_ARG(dsf, n));
	//}
}

#ifdef TEST_PRINT
	// Simple low-level tests:
	Print("%%d %d", 1234);
	Print("%%d %d", -1234);
	Print("%%d %d", 12345678);
	Print("%%d %d", 0);
	Print("%%6d %6d", 1234);
	Print("%%10d %10d", 123456789);
	Print("%%x %x", 0x1234ABCD);
	Print("%%x %x", -1);
	Print("%%4x %x", 0x1234);
	Print("%%s %s", "test");
	Print("%%s %s", 0);
	Print("%%c %c", (REBINT)'X');
	Print("%s %d %x", "test", 1234, 1234);
	getchar();
#endif
