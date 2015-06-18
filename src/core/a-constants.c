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
**  Module:  a-constants.c
**  Summary: special global constants and strings
**  Section: environment
**  Author:  Carl Sassenrath
**  Notes:
**     Very few strings should be located here. Most strings are
**     put in the compressed embedded boot image. That saves space,
**     reduces tampering, and allows UTF8 encoding. See ../boot dir.
**
***********************************************************************/

#include "sys-core.h"

const char Str_Banner[] = "Rebol 3 %d.%d.%d.%d.%d";

const char Str_Stack_Misaligned[] = "!! Stack misaligned: %d";

const char Str_REBOL[] = "REBOL";

const char * const Crash_Msgs[] = {
	"REBOL System Error",
	"boot failure",
	"internal problem",
	"invalid datatype %d",
	"unspecific",
	"\n\nProgram terminated abnormally."
		"\nThis should never happen."
		"\nPlease file a bug report with details."
};

const char Str_Dump[] =
	"%s Series %x \"%s\":"
		" wide: %2d"
		" size: %6d"
		" bias: %d"
		" tail: %d"
		" rest: %d"
		" flags: %x";
		
const char * Hex_Digits = "0123456789ABCDEF";

const char * const Esc_Names[] = {
	// Must match enum REBOL_Esc_Codes!
	"line",
	"tab",
	"page",
	"escape",
	"esc",
	"back",
	"del",
	"null"
};

const REBYTE Esc_Codes[] = {
	// Must match enum REBOL_Esc_Codes!
	10,
	9,
	12,
	27,
	27,
	8,
	127,
	0
};

const REBYTE Month_Lengths_USUALLY[12] = {
	31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

const char * const Month_Names[12] = {
	"January",
	"February",
	"March",
	"April",
	"May",
	"June",
	"July",
	"August",
	"September",
	"October",
	"November",
	"December"
};

