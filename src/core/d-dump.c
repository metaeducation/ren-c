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

#if RUNTIME_CHECKS

#ifdef _MSC_VER
#define snprintf _snprintf
#endif


//
//  Dump_Bytes: C
//
void Dump_Bytes(Byte *bp, REBLEN limit)
{
    const REBLEN max_lines = 120;

    REBLEN total = 0;

    Byte buf[2048];

    REBLEN l = 0;
    for (; l < max_lines; l++) {
        Byte *cp = buf;

        cp = Form_Hex_Pad(cp, i_cast(uintptr_t, bp), 8);

        *cp++ = ':';
        *cp++ = ' ';

        Byte str[40];
        Byte *tp = str;

        REBLEN n = 0;
        for (; n < 16; n++) {
            if (total++ >= limit)
                break;

            Byte c = *bp++;

            cp = Form_Hex2_UTF8(cp, c);
            if ((n & 3) == 3)
                *cp++ = ' ';
            if ((c < 32) || (c > 126))
                c = '.';
            *tp++ = c;
        }

        for (; n < 16; n++) {
            Byte c = ' ';
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
//  Dump_Flex: C
//
void Dump_Flex(Flex* s, const char *memo)
{
    printf("Dump_Flex(%s) @ %p\n", memo, cast(void*, s));
    fflush(stdout);

    if (s == nullptr)
        return;

    printf(" wide: %d\n", Flex_Wide(s));
    printf(" size: %ld\n", cast(unsigned long, Flex_Total_If_Dynamic(s)));
    if (Is_Flex_Dynamic(s))
        printf(" bias: %d\n", cast(int, Flex_Bias(s)));
    printf(" tail: %d\n", cast(int, Flex_Len(s)));
    printf(" rest: %d\n", cast(int, Flex_Rest(s)));

    // flags includes len if non-dynamic
    printf(" flags: %lx\n", cast(unsigned long, s->leader.bits));

    // info includes width
    printf(" info: %lx\n", cast(unsigned long, s->info.bits));

    fflush(stdout);

    if (Is_Flex_Array(s))
        Dump_Values(Array_Head(cast_Array(s)), Flex_Len(s));
    else
        Dump_Bytes(Flex_Data(s), (Flex_Len(s) + 1) * Flex_Wide(s));

    fflush(stdout);
}


//
//  Dump_Values: C
//
// Print values in raw hex; If memory is corrupted this still needs to work.
//
void Dump_Values(Cell* vp, REBLEN count)
{
    Byte buf[2048];
    Byte *cp;
    REBLEN l, n;
    REBLEN *bp = (REBLEN*)vp;
    const Byte *type;

    cp = buf;
    for (l = 0; l < count; l++) {
        Value* val = cast(Value*, bp);
        if (IS_END(val)) {
            break;
        }
        if (not Is_Cell_Unreadable(val) and Is_Nulled(val)) {
            bp = cast(REBLEN*, val + 1);
            continue;
        }

        cp = Form_Hex_Pad(cp, l, 8);

        *cp++ = ':';
        *cp++ = ' ';

        type = cb_cast(Symbol_Head(Get_Type_Name(val)));
        for (n = 0; n < 11; n++) {
            if (*type) *cp++ = *type++;
            else *cp++ = ' ';
        }
        *cp++ = ' ';
        for (n = 0; n < sizeof(Cell) / sizeof(REBLEN); n++) {
            cp = Form_Hex_Pad(cp, *bp++, 8);
            *cp++ = ' ';
        }
        n = 0;
        if (Is_Word(val) || Is_Get_Word(val) || Is_Set_Word(val)) {
            const char *name_utf8 = Symbol_Head(Cell_Word_Symbol(val));
            n = snprintf(
                s_cast(cp), sizeof(buf) - (cp - buf), " (%s)", name_utf8
            );
        }

        *(cp + n) = '\0';
        printf("%s\n", s_cast(buf));
        fflush(stdout);
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
    printf("    Cycles:  %ld\n", cast(unsigned long, Eval_Cycles));
    printf("    Counter: %d\n", cast(int, Eval_Count));
    printf("    Dose:    %d\n", cast(int, Eval_Dose));
    printf("    Signals: %lx\n", cast(unsigned long, Eval_Signals));
    printf("    Sigmask: %lx\n", cast(unsigned long, Eval_Sigmask));
    printf("    TOP_INDEX: %ld\n", cast(unsigned long, TOP_INDEX));

    printf("Memory/GC:\n");

    printf("    Ballast: %d\n", cast(int, GC_Ballast));
    printf("    Disable: %s\n", GC_Disabled ? "yes" : "no");
    printf("    Guarded Nodes: %d\n", cast(int, Flex_Len(GC_Guarded)));
    fflush(stdout);
}


//
//  Dump_Stack: C
//
// Prints stack counting levels from the passed in number.  Pass 0 to start.
//
void Dump_Stack(Level* L, REBLEN level)
{
    printf("\n");

    if (L == BOTTOM_LEVEL) {
        printf("*STACK[] - NO FRAMES*\n");
        fflush(stdout);
        return;
    }

    printf(
        "STACK[%d](%s) - %d\n",
        cast(int, level),
        Frame_Label_Or_Anonymous_UTF8(L),
        VAL_TYPE_RAW(L->value)
    );

    if (not Is_Action_Level(L)) {
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
    Value* arg = Level_Arg(L, 1);
    Value* param = ACT_PARAMS_HEAD(Level_Phase(L));

    for (; NOT_END(param); ++param, ++arg, ++n) {
        if (Is_Nulled(arg))
            Debug_Fmt(
                "    %s:",
                Symbol_Head(Cell_Parameter_Symbol(param))
            );
        else
            Debug_Fmt(
                "    %s: %72r",
                Symbol_Head(Cell_Parameter_Symbol(param)),
                arg
            );
    }

    if (L->prior != BOTTOM_LEVEL)
        Dump_Stack(L->prior, level + 1);
}



#endif // DUMP is picked up by scan regardless of #ifdef, must be defined


//
//  dump: native [
//
//  "Temporary debug dump"
//
//      return: [~]
//      :value [any-word!]
//  ]
//
DECLARE_NATIVE(DUMP)
{
    INCLUDE_PARAMS_OF_DUMP;

#if NO_RUNTIME_CHECKS
    UNUSED(ARG(VALUE));
    fail (Error_Debug_Only_Raw());
#else
    Value* v = ARG(VALUE);

    PROBE(v);
    printf("=> ");
    if (Is_Word(v)) {
        const Value* var = Try_Get_Opt_Var(v, SPECIFIED);
        if (not var) {
            PROBE("\\unbound\\");
        }
        else if (Is_Nulled(var)) {
            PROBE("\\null\\");
        }
        else
            PROBE(var);
    }

    return Init_Trash(OUT);
#endif
}
