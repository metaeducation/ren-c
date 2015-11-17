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
**  Summary: Miscellaneous structures and definitions
**  Module:  reb-defs.h
**  Author:  Carl Sassenrath
**  Notes:
**      This file is used by internal and external C code. It
**      should not depend on many other header files prior to it.
**
***********************************************************************/

#ifndef REB_DEFS_H  // due to sequences within the lib build itself
#define REB_DEFS_H

#ifndef REB_DEF
typedef void *REBSER;
typedef void *REBOBJ;
#endif

#pragma pack(4)

// X/Y coordinate pair as floats:
struct Reb_Pair {
    float x;
    float y;
};

// !!! Use this instead of struct Reb_Pair when all integer pairs are gone?
// (Apparently PAIR went through an int-to-float transition at some point)
/* typedef struct Reb_Pair REBPAR; */

// !!! Temporary name for Reb_Pair "X and Y as floats"
typedef struct Reb_Pair REBXYF;

// X/Y coordinate pair as integers:
typedef struct rebol_xy_int {
    int x;
    int y;
} REBXYI;

// Standard date and time:
typedef struct rebol_dat {
    int year;
    int month;
    int day;
    int time;
    int nano;
    int zone;
} REBOL_DAT;  // not same as REBDAT

#pragma pack()

#endif
