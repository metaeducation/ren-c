//
//  File: %d-dump.c
//  Summary: "various debug output functions"
//  Section: debug
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Most of these low-level debug routines were leftovers from R3-Alpha, which
// had no DEBUG build (and was perhaps frequently debugged without an IDE
// debugger).  After the open source release, Ren-C's reliance is on a
// more heavily checked debug build...so these routines were not used.
//
// They're being brought up to date to be included in the debug build only
// version of panic().  That should keep them in working shape.
//
// Note: These routines use `printf()`, which is only linked in DEBUG builds.
// Higher-level Rebol formatting should ultimately be using BLOCK! dialects,
// as opposed to strings with %s and %d.  Bear in mind the "z" modifier in
// printf is unavailable in C89, so if something might be 32-bit or 64-bit
// depending, it must be cast to unsigned long:
//
// http://stackoverflow.com/q/2125845
//

#include "sys-core.h"
#include "mem-series.h" // low-level series memory access

#if !defined(NDEBUG)

#ifdef _MSC_VER
#define snprintf _snprintf
#endif


//
//  Dump_Bytes: C
//
void Dump_Bytes(REBYTE *bp, REBCNT limit)
{
    const REBCNT max_lines = 120;

    REBCNT total = 0;

    REBYTE buf[2048];

    REBCNT l = 0;
    for (; l < max_lines; l++) {
        REBYTE *cp = buf;

        cp = Form_Hex_Pad(cp, cast(REBUPT, bp), 8);

        *cp++ = ':';
        *cp++ = ' ';

        REBYTE str[40];
        REBYTE *tp = str;

        REBCNT n = 0;
        for (; n < 16; n++) {
            if (total++ >= limit)
                break;

            REBYTE c = *bp++;
            cp = Form_Hex2(cp, c);
            if ((n & 3) == 3)
                *cp++ = ' ';
            if ((c < 32) || (c > 126))
                c = '.';
            *tp++ = c;
        }

        for (; n < 16; n++) {
            REBYTE c = ' ';
            *cp++ = c;
            *cp++ = c;
            if ((n & 3) == 3)
                *cp++ = ' ';
            if ((c < 32) || (c > 126))
                c = '.';
            *tp++ = c;
        }

        *tp++ = 0;

        for (tp = str; *tp;)
            *cp++ = *tp++;

        *cp = 0;
        printf("%s\n", s_cast(buf));
        fflush(stdout);

        if (total >= limit)
            break;
    }
}


//
//  Dump_Series: C
//
void Dump_Series(REBSER *s, const char *memo)
{
    printf("Dump_Series(%s) @ %p\n", memo, cast(void*, s));
    fflush(stdout);

    if (s == NULL)
        return;

    printf(" wide: %d\n", SER_WIDE(s));
    printf(" size: %ld\n", cast(unsigned long, SER_TOTAL_IF_DYNAMIC(s)));
    if (GET_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC))
        printf(" bias: %d\n", cast(int, SER_BIAS(s)));
    printf(" tail: %d\n", cast(int, SER_LEN(s)));
    printf(" rest: %d\n", cast(int, SER_REST(s)));

    // flags includes len if non-dynamic
    printf(" flags: %lx\n", cast(unsigned long, s->header.bits));

    // info includes width
    printf(" info: %lx\n", cast(unsigned long, s->info.bits));

    fflush(stdout);

    if (Is_Array_Series(s))
        Dump_Values(ARR_HEAD(AS_ARRAY(s)), SER_LEN(s));
    else
        Dump_Bytes(SER_DATA_RAW(s), (SER_LEN(s) + 1) * SER_WIDE(s));

    fflush(stdout);
}


//
//  Dump_Values: C
//
// Print values in raw hex; If memory is corrupted this still needs to work.
//
void Dump_Values(RELVAL *vp, REBCNT count)
{
    REBYTE buf[2048];
    REBYTE *cp;
    REBCNT l, n;
    REBCNT *bp = (REBCNT*)vp;
    const REBYTE *type;

    cp = buf;
    for (l = 0; l < count; l++) {
        REBVAL *val = cast(REBVAL*, bp);
        if (IS_END(val)) {
            break;
        }
        if (IS_BLANK_RAW(val) || IS_VOID(val)) {
            bp = cast(REBCNT*, val + 1);
            continue;
        }

        cp = Form_Hex_Pad(cp, l, 8);

        *cp++ = ':';
        *cp++ = ' ';

        type = Get_Type_Name(val);
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
        if (IS_WORD(val) || IS_GET_WORD(val) || IS_SET_WORD(val)) {
            const REBYTE *name = STR_HEAD(VAL_WORD_SPELLING(val));
            n = snprintf(
                s_cast(cp), sizeof(buf) - (cp - buf), " (%s)", cs_cast(name)
            );
        }

        *(cp + n) = 0;
        Debug_Str(s_cast(buf));
        cp = buf;
    }
}


//
//  Dump_Info: C
//
void Dump_Info(void)
{
    printf("^/--REBOL Kernel Dump--\n");

    printf("Evaluator:\n");
    printf("    Cycles:  %ld\n", Eval_Cycles);
    printf("    Counter: %d\n", cast(int, Eval_Count));
    printf("    Dose:    %d\n", cast(int, Eval_Dose));
    printf("    Signals: %lx\n", cast(unsigned long, Eval_Signals));
    printf("    Sigmask: %lx\n", cast(unsigned long, Eval_Sigmask));
    printf("    DSP:     %d\n", DSP);

    printf("Memory/GC:\n");

    printf("    Ballast: %d\n", cast(int, GC_Ballast));
    printf("    Disable: %s\n", GC_Disabled ? "yes" : "no");
    printf("    Guarded Nodes: %d\n", cast(int, SER_LEN(GC_Guarded)));
    fflush(stdout);
}


//
//  Dump_Stack: C
//
// Prints stack counting levels from the passed in number.  Pass 0 to start.
//
void Dump_Stack(REBFRM *f, REBCNT level)
{
    printf("\n");

    if (f == NULL)
        f = FS_TOP;

    if (f == NULL) {
        printf("*STACK[] - NO FRAMES*\n");
        fflush(stdout);
        return;
    }

   printf(
        "STACK[%d](%s) - %d\n",
        cast(int, level),
        STR_HEAD(FRM_LABEL(f)),
        f->eval_type // note: this is now an ordinary Reb_Kind, stringify it
    );

    if (NOT(Is_Any_Function_Frame(f))) {
        printf("(no function call pending or in progress)\n");
        fflush(stdout);
        return;
    }

    // !!! This is supposed to be a low-level debug routine, but it is
    // effectively molding arguments.  If the stack is known to be in "good
    // shape" enough for that, it should be dumped by routines using the
    // Rebol backtrace API.

    fflush(stdout);

    REBINT n = 1;
    REBVAL *arg = FRM_ARG(f, 1);
    REBVAL *param = FUNC_PARAMS_HEAD(f->func);

    for (; NOT_END(param); ++param, ++arg, ++n) {
        Debug_Fmt(
            "    %s: %72r",
            STR_HEAD(VAL_PARAM_SPELLING(param)),
            arg
        );
    }

    if (f->prior)
        Dump_Stack(f->prior, level + 1);
}



#endif // DUMP is picked up by scan regardless of #ifdef, must be defined


//
//  dump: native [
//
//  "Temporary debug dump"
//
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(dump)
{
    INCLUDE_PARAMS_OF_DUMP;

#ifdef NDEBUG
    UNUSED(ARG(value));
    fail (Error_Debug_Only_Raw());
#else
    REBVAL *value = ARG(value);

    Dump_Stack(frame_, 0);

    if (ANY_SERIES(value))
        Dump_Series(VAL_SERIES(value), "=>");
    else
        Dump_Values(value, 1);

    Move_Value(D_OUT, value);
    return R_OUT;
#endif
}
