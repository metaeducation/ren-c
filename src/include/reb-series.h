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
**  Summary: REBOL series structure
**  Module:  reb-series.h
**  Author:  Carl Sassenrath
**  Notes:
**      WARNING: struct size may change -- do not malloc()
**
***********************************************************************/

struct rebol_series {
    REBYTE *data;
    REBCNT tail;
    REBCNT rest;
    REBINT info;
#if defined(__LP64__) || defined(__LLP64__)
    REBCNT padding; /* make the size multiple of sizeof(pointer) */
#endif
    REBCNT size; // Temp - size of image w/h
    // OPTIONAL Extensions
};

// !!! Getting/setting the length or data pointer of a series is now complex.
// Based on bitflags in the series, the data may not be allocated at all,
// but live directly in the series node itself!
//
// Hence client code must go through an RL_API, repeat the complexity of the
// macros internal to Rebol, or become a formal Ren-C client and use the
// same definitions that the core does.
//
// #define SER_LEN(s) ((s)->tail)
// #define SER_DATA(s) ((s)->data)

#define ARR_HEAD(s) ((REBVAL *)SER_DATA(s))
#define STR_HEAD(s) ((REBYTE *)SER_DATA(s))

#define IMG_SIZE(s) ((s)->size)
#define IMG_WIDE(s) ((s)->size & 0xffff)
#define IMG_HIGH(s) ((s)->size >> 16)
#define IMG_DATA(s) ((REBYTE *)((s)->data))
