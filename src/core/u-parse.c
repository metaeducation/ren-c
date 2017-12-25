//
//  File: %u-parse.c
//  Summary: "parse dialect interpreter"
//  Section: utility
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
// As a major operational difference from R3-Alpha, each recursion in Ren-C's
// PARSE runs using a "Rebol Stack Frame"--similar to how the DO evaluator
// works.  So `[print "abc"]` and `[thru "abc"]` are both seen as "code" and
// iterated using the same mechanic.  (The rules are also locked from
// modification during the course of the PARSE, as code is in Ren-C.)
//
// This leverages common services like reporting the start of the last
// "expression" that caused an error.  So merely calling `fail()` will use
// the call stack to properly indicate the start of the parse rule that caused
// a problem.  But most importantly, debuggers can break in and see the
// state at every step in the parse rule recursions.
//
// The function users see on the stack for each recursion is a native called
// SUBPARSE.  Although it is shaped similarly to typical DO code, there are
// differences.  The subparse advances the "current evaluation position" in
// the frame as it operates, so it is a variadic function...with the rules as
// the variadic parameter.  Calling it directly looks a bit unusual:
//
//     >> flags: 0
//     >> subparse "aabb" flags some "a" some "b"
//     == 4
//
// But as far as a debugging tool is concerned, the "where" of each frame
// in the call stack is what you would expect.
//
// !!! The PARSE code in R3-Alpha had gone through significant churn, and
// had a number of cautionary remarks and calls for review.  During Ren-C
// development, several edge cases emerged about interactions with the
// garbage collector or throw mechanics...regarding responsibility for
// temporary values or other issues.  The code has become more clear in many
// ways, though it is also more complex due to the frame mechanics...and is
// under ongoing cleanup as time permits.
//

#include "sys-core.h"


//
// These macros are used to address into the frame directly to get the
// current parse rule, current input series, current parse position in that
// input series, etc.  Because the bits inside the frame arguments are
// modified as the parse runs, that means users can see the effects at
// a breakpoint.
//
// (Note: when arguments to natives are viewed under the debugger, the
// debug frames are read only.  So it's not possible for the user to change
// the ANY_SERIES! of the current parse position sitting in slot 0 into
// a DECIMAL! and crash the parse, for instance.  They are able to change
// usermode authored function arguments only.)
//

#define P_RULE              (f->value + 0) // rvalue, don't change pointer
#define P_RULE_SPECIFIER    (f->specifier + 0) // rvalue, don't change pointer

#define P_INPUT_VALUE       (&f->args_head[0])
#define P_TYPE              VAL_TYPE(P_INPUT_VALUE)
#define P_INPUT             VAL_SERIES(P_INPUT_VALUE)
#define P_INPUT_SPECIFIER   VAL_SPECIFIER(P_INPUT_VALUE)
#define P_POS               VAL_INDEX(P_INPUT_VALUE)

#define P_FIND_FLAGS        VAL_INT64(&f->args_head[1])
#define P_HAS_CASE          LOGICAL(P_FIND_FLAGS & AM_FIND_CASE)

#define P_OUT (f->out)

#define P_CELL KNOWN(&f->cell)

#define FETCH_NEXT_RULE_MAYBE_END(f) \
    Fetch_Next_In_Frame(f)

#define FETCH_TO_BAR_MAYBE_END(f) \
    while (FRM_HAS_MORE(f) && !IS_BAR(P_RULE)) \
        { FETCH_NEXT_RULE_MAYBE_END(f); }


//
// See the notes on `flags` in the main parse loop for how these work.
//
enum parse_flags {
    PF_SET = 1 << 0,
    PF_COPY = 1 << 1,
    PF_NOT = 1 << 2,
    PF_NOT2 = 1 << 3,
    PF_THEN = 1 << 4,
    PF_AND = 1 << 5,
    PF_REMOVE = 1 << 6,
    PF_INSERT = 1 << 7,
    PF_CHANGE = 1 << 8,
    PF_RETURN = 1 << 9,
    PF_WHILE = 1 << 10
};


// In %words.r, the parse words are lined up in order so they can be quickly
// filtered, skipping the need for a switch statement if something is not
// a parse command.
//
// !!! This and other efficiency tricks from R3-Alpha should be reviewed to
// see if they're really the best option.
//
inline static REBSYM VAL_CMD(const RELVAL *v) {
    REBSYM sym = VAL_WORD_SYM(v);
    if (sym >= SYM_SET && sym <= SYM_END)
        return sym;
    return SYM_0;
}


// Subparse_Throws is a helper that sets up a call frame and invokes the
// SUBPARSE native--which represents one level of PARSE recursion.
//
// !!! It is the intent of Ren-C that calling functions be light and fast
// enough through Do_Va() and other mechanisms that a custom frame constructor
// like this one would not be needed.  Data should be gathered on how true
// it's possible to make that.
//
// !!! Calling subparse creates another recursion.  This recursion means
// that there are new arguments and a new frame spare cell.  Callers do not
// evaluate directly into their output slot at this time (except the top
// level parse), because most of them are framed to return other values.
//
static REBOOL Subparse_Throws(
    REBOOL *interrupted_out,
    REBVAL *out,
    RELVAL *input,
    REBSPC *input_specifier,
    const RELVAL *rules,
    REBSPC *rules_specifier,
    REBCNT find_flags
) {
    DECLARE_FRAME (f);

    SET_END(out);

    assert(ANY_ARRAY(rules));
    assert(ANY_SERIES(input));

    // Since SUBPARSE is a native that the user can call directly, and it
    // is "effectively variadic" reading its instructions inline out of the
    // `where` of execution, it has to handle the case where the frame it
    // is given is at an END.
    //
    // However, as long as this wrapper is testing for ends, rather than
    // use that test to create an END state to feed to subparse, it can
    // just return.  This is because no matter what, empty rules means a match
    // with no items advanced.
    //
    if (VAL_INDEX(rules) >= VAL_LEN_HEAD(rules)) {
        *interrupted_out = FALSE;
        Init_Integer(out, VAL_INDEX(input));
        return FALSE;
    }

    f->out = out;

    f->gotten = END;
    SET_FRAME_VALUE(f, VAL_ARRAY_AT(rules)); // not an END due to test
    f->specifier = Derive_Specifier(rules_specifier, rules);

    f->source.vaptr = NULL;
    f->source.array = VAL_ARRAY(rules);
    f->source.index = VAL_INDEX(rules) + 1;
    f->source.pending = f->value + 1;

    Init_Endlike_Header(&f->flags, 0); // implicitly terminate f->cell

    Push_Frame_Core(f); // checks for C stack overflow
    Push_Function(
        f,
        Canon(SYM_SUBPARSE),
        NAT_FUNC(subparse),
        UNBOUND
    );

    f->param = END; // informs infix lookahead
    f->arg = m_cast(REBVAL*, END);
    f->refine = NULL;
    f->special = NULL;

#if defined(NDEBUG)
    assert(FUNC_NUM_PARAMS(NAT_FUNC(subparse)) == 2);
#else
    assert(FUNC_NUM_PARAMS(NAT_FUNC(subparse)) == 3);
    Prep_Stack_Cell(&f->args_head[2]);
    Init_Void(&f->args_head[2]);
#endif

    Prep_Stack_Cell(&f->args_head[0]);
    Derelativize(&f->args_head[0], input, input_specifier);

    // We always want "case-sensitivity" on binary bytes, vs. treating as
    // case-insensitive bytes for ASCII characters.
    //
    Prep_Stack_Cell(&f->args_head[1]);
    Init_Integer(&f->args_head[1], find_flags);

    f->varlist = NULL;

    SET_END(&f->cell); // GC requires some initialization of cell

    SET_END(f->out);

    REB_R r = N_subparse(f);

    assert(NOT_END(out));

    // Can't just drop f->data.stackvars because the debugger may have
    // "reified" the frame into a FRAME!, which means it would now be using
    // the f->data.context field.
    //
    const REBOOL drop_chunks = TRUE;
    Drop_Function_Core(f, drop_chunks);

    Drop_Frame_Core(f);

    if (r == R_OUT_IS_THROWN) {
        assert(THROWN(out));

        // ACCEPT and REJECT are special cases that can happen at nested parse
        // levels and bubble up through the throw mechanism to break a looping
        // construct.
        //
        // !!! R3-Alpha didn't react to these instructions in general, only in
        // the particular case where subparsing was called inside an iterated
        // construct.  Even then, it could only break through one level of
        // depth.  Most places would treat them the same as a normal match
        // or not found.  This returns the interrupted flag which is still
        // ignored by most callers, but makes that fact more apparent.
        //
        if (IS_FUNCTION(out)) {
            if (VAL_FUNC(out) == NAT_FUNC(parse_reject)) {
                CATCH_THROWN(out, out);
                assert(IS_BLANK(out));
                *interrupted_out = TRUE;
                return FALSE;
            }

            if (VAL_FUNC(out) == NAT_FUNC(parse_accept)) {
                CATCH_THROWN(out, out);
                assert(IS_INTEGER(out));
                *interrupted_out = TRUE;
                return FALSE;
            }
        }

        return TRUE;
    }

    assert(r == R_OUT);
    *interrupted_out = FALSE;
    return FALSE;
}


// Very generic error.  Used to be parameterized with the parse rule in
// question, but now the `where` at the time of failure will indicate the
// location in the parse dialect that's the problem.
//
static REBCTX *Error_Parse_Rule() {
    return Error_Parse_Rule_Raw();
}


// Also generic.
//
static REBCTX *Error_Parse_End() {
    return Error_Parse_End_Raw();
}


static void Print_Parse_Index(REBFRM *f) {
    DECLARE_LOCAL (input);
    Init_Any_Series_At_Core(
        input,
        P_TYPE,
        P_INPUT,
        P_POS,
        GET_SER_FLAG(P_INPUT, SERIES_FLAG_ARRAY)
            ? P_INPUT_SPECIFIER
            : SPECIFIED
    );

    // Either the rules or the data could be positioned at the end.  The
    // data might even be past the end.
    //
    // !!! Or does PARSE adjust to ensure it never is past the end, e.g.
    // when seeking a position given in a variable or modifying?
    //
    if (FRM_AT_END(f)) {
        if (P_POS >= SER_LEN(P_INPUT))
            Debug_Fmt("[]: ** END **");
        else
            Debug_Fmt("[]: %r", input);
    }
    else {
        if (P_POS >= SER_LEN(P_INPUT))
            Debug_Fmt("%r: ** END **", P_RULE);
        else
            Debug_Fmt("%r: %r", P_RULE, input);
    }
}


//
//  Set_Parse_Series: C
//
// Change the series, ensuring the index is not past the end.
//
static void Set_Parse_Series(
    REBFRM *f,
    const REBVAL *any_series
) {
    Move_Value(&f->args_head[0], any_series);
    VAL_INDEX(&f->args_head[0]) =
        (VAL_INDEX(any_series) > VAL_LEN_HEAD(any_series))
            ? VAL_LEN_HEAD(any_series)
            : VAL_INDEX(any_series);

    if (IS_BINARY(any_series) || (P_FIND_FLAGS & AM_FIND_CASE))
        P_FIND_FLAGS |= AM_FIND_CASE;
    else
        P_FIND_FLAGS &= ~AM_FIND_CASE;
}


//
//  Get_Parse_Value: C
//
// Gets the value of a word (when not a command) or path.  Returns all other
// values as-is.
//
// !!! Because path evaluation does not necessarily wind up pointing to a
// variable that exists in memory, a derived value may be created.  R3-Alpha
// would push these on the stack without any corresponding drops, leading
// to leaks and overflows.  This requires you to pass in a cell of storage
// which will be good for as long as the returned pointer is used.  It may
// not be used--e.g. with a WORD! fetch.
//
static const RELVAL *Get_Parse_Value(
    REBVAL *cell,
    const RELVAL *rule,
    REBSPC *specifier
) {
    if (IS_BAR(rule))
        return rule;

    if (IS_WORD(rule)) {
        if (VAL_CMD(rule))
            return rule;

        Copy_Opt_Var_May_Fail(cell, rule, specifier);
        if (IS_VOID(cell))
            fail (Error_No_Value_Core(rule, specifier));

        return cell;
    }

    if (IS_PATH(rule)) {
        //
        // !!! REVIEW: how should GET-PATH! be handled?
        //
        // Should PATH!s be evaluating GROUP!s?  This does, but would need
        // to route potential thrown values up to do it properly.

        if (Get_Path_Throws_Core(cell, rule, specifier))
            fail (Error_No_Catch_For_Throw(cell));

        if (IS_VOID(cell))
            fail (Error_No_Value_Core(rule, specifier));

        return cell;
    }

    return rule;
}


//
//  Parse_String_One_Rule: C
//
// Match the next rule in the string ruleset.
//
// If it matches, return the index just past it.
// Otherwise return END_FLAG.
// May also return THROWN_FLAG.
//
static REBIXO Parse_String_One_Rule(REBFRM *f, const RELVAL *rule) {
    assert(IS_END(P_OUT));

    REBCNT flags = P_FIND_FLAGS | AM_FIND_MATCH | AM_FIND_TAIL;

    if (Trace_Level) {
        Trace_Value("match", rule);

        // !!! This used STR_AT (obsolete) but it's not clear that this is
        // necessarily a byte sized series.  Switched to BIN_AT, which will
        // assert if it's not BYTE_SIZE()

        Trace_String(BIN_AT(P_INPUT, P_POS), BIN_LEN(P_INPUT) - P_POS);
    }

    if (P_POS >= SER_LEN(P_INPUT))
        return END_FLAG;

    switch (VAL_TYPE(rule)) {
    case REB_BLANK:
        return P_POS; // just ignore blanks

    case REB_CHAR:
        //
        // Try matching character against current string parse position
        //
        if (P_HAS_CASE) {
            if (VAL_CHAR(rule) == GET_ANY_CHAR(P_INPUT, P_POS))
                return P_POS + 1;
        }
        else {
            if (
                UP_CASE(VAL_CHAR(rule))
                == UP_CASE(GET_ANY_CHAR(P_INPUT, P_POS))
            ) {
                return P_POS + 1;
            }
        }
        return END_FLAG;

    case REB_EMAIL:
    case REB_STRING:
    case REB_BINARY: {
        REBCNT index = Find_Str_Str(
            P_INPUT,
            0,
            P_POS,
            SER_LEN(P_INPUT),
            1,
            VAL_SERIES(rule),
            VAL_INDEX(rule),
            VAL_LEN_AT(rule),
            flags
        );
        if (index == NOT_FOUND)
            return END_FLAG;
        return index; }

    case REB_TAG:
    case REB_FILE: {
        //
        // !!! The content to be matched does not have the delimiters in the
        // actual series data.  This FORMs it, but could be more optimized.
        //
        REBSER *formed = Copy_Form_Value(rule, 0);
        REBCNT index = Find_Str_Str(
            P_INPUT,
            0,
            P_POS,
            SER_LEN(P_INPUT),
            1,
            formed,
            0,
            SER_LEN(formed),
            flags
        );
        Free_Series(formed);
        if (index == NOT_FOUND)
            return END_FLAG;
        return index; }

    case REB_BITSET:
        //
        // Check the current character against a character set, advance matches
        //
        if (Check_Bit(
            VAL_SERIES(rule), GET_ANY_CHAR(P_INPUT, P_POS), NOT(P_HAS_CASE)
        )) {
            return P_POS + 1;
        }
        return END_FLAG;

    case REB_BLOCK: {
        //
        // This parses a sub-rule block.  It may throw, and it may mutate the
        // input series.
        //
        REBOOL interrupted;
        if (Subparse_Throws(
            &interrupted,
            P_CELL,
            P_INPUT_VALUE,
            SPECIFIED,
            rule,
            P_RULE_SPECIFIER,
            P_FIND_FLAGS
        )) {
            Move_Value(P_OUT, P_CELL);
            return THROWN_FLAG;
        }

        // !!! ignore "interrupted"? (e.g. ACCEPT or REJECT ran)

        if (IS_BLANK(P_CELL))
            return END_FLAG;

        return VAL_INT32(P_CELL); }

    case REB_GROUP: {
        //
        // This runs a GROUP! as code.  It may throw, but won't influence the
        // input position...although it can change the input series.  :-/
        // If the input series is shortened to make P_POS an invalid position,
        // then truncate it to the end of series.
        //
        DECLARE_LOCAL (dummy);
        REBSPC *derived = Derive_Specifier(P_RULE_SPECIFIER, rule);
        if (Do_At_Throws(
            dummy,
            VAL_ARRAY(rule),
            VAL_INDEX(rule),
            derived
        )) {
            Move_Value(P_OUT, dummy);
            return THROWN_FLAG;
        }
        return MIN(P_POS, SER_LEN(P_INPUT)); } // !!! review truncation concept

    default:
        fail (Error_Parse_Rule());
    }
}


//
//  Parse_Array_One_Rule_Core: C
//
// Used for parsing ANY-ARRAY! to match the next rule in the ruleset.  If it
// matches, return the index just past it. Otherwise, return zero.
//
// This function is called by To_Thru, and as a result it may need to
// process elements other than the current one in the frame.  Hence it
// is parameterized by an arbitrary `pos` instead of assuming the P_POS
// that is held by the frame.
//
// The return result is either an integer, END_FLAG, or THROWN_FLAG
// Only in the case of THROWN_FLAG will f->out (aka P_OUT) be affected.
// Otherwise, it should exit the routine as an END marker (as it started);
//
static REBIXO Parse_Array_One_Rule_Core(
    REBFRM *f,
    REBCNT pos,
    const RELVAL *rule
) {
    assert(IS_END(P_OUT));

    REBARR *array = ARR(P_INPUT);
    RELVAL *item = ARR_AT(array, pos);

    if (Trace_Level) {
        Trace_Value("input", rule);
        if (IS_END(item)) {
            const char *end_str = "** END **";
            Trace_String(cb_cast(end_str), strlen(end_str));
        }
        else
            Trace_Value("match", item);
    }

    if (IS_END(item)) {
        //
        // Only the BLANK and BLOCK rules can potentially handle an END input
        // For instance, `parse [] [[[_ _ _]]]` should be able to match.
        // The other cases would assert if fed an END marker as item.
        //
        if (!IS_BLANK(rule) && !IS_BLOCK(rule))
            return END_FLAG;
    }

    switch (VAL_TYPE(rule)) {
    case REB_BLANK:
        return pos; // blank rules "match" but don't affect the parse position

    case REB_DATATYPE:
        if (VAL_TYPE(item) == VAL_TYPE_KIND(rule)) // specific datatype match
            return pos + 1;
        return END_FLAG;

    case REB_TYPESET:
        if (TYPE_CHECK(rule, VAL_TYPE(item))) // type was found in the typeset
            return pos + 1;
        return END_FLAG;

    case REB_LIT_WORD:
        if (IS_WORD(item) && (VAL_WORD_CANON(item) == VAL_WORD_CANON(rule)))
            return pos + 1;
        return END_FLAG;

    case REB_LIT_PATH:
        if (IS_PATH(item) && Cmp_Array(item, rule, FALSE) == 0)
            return pos + 1;
        return END_FLAG;

    case REB_GROUP: {
        //
        // If a GROUP! is hit then it is treated as a match and assumed that
        // it should execute.  Although the rules series is protected from
        // modification during the parse, the input series is not...so the
        // index may have to be adjusted to keep it in the array bounds.
        //
        REBSPC *derived = Derive_Specifier(P_RULE_SPECIFIER, rule);
        DECLARE_LOCAL (dummy);
        if (Do_At_Throws(
            dummy,
            VAL_ARRAY(rule),
            VAL_INDEX(rule),
            derived
        )) {
            Move_Value(P_OUT, dummy);
            return THROWN_FLAG;
        }
        return MIN(pos, ARR_LEN(array)); } // may affect tail

    case REB_BLOCK: {
        //
        // Process a subrule.  The subrule will run in its own frame, so it
        // will not change P_POS directly (it will have its own P_INPUT_VALUE)
        // Hence the return value regarding whether a match occurred or not
        // has to be based on the result that comes back in P_OUT.
        //
        REBCNT pos_before = P_POS;
        REBOOL interrupted;

        P_POS = pos; // modify input position

        if (Subparse_Throws(
            &interrupted,
            P_CELL,
            P_INPUT_VALUE, // use input value with modified position
            SPECIFIED,
            rule,
            P_RULE_SPECIFIER,
            P_FIND_FLAGS
        )) {
            Move_Value(P_OUT, P_CELL);
            return THROWN_FLAG;
        }

        // !!! ignore "interrupted"? (e.g. ACCEPT or REJECT ran)

        P_POS = pos_before; // restore input position

        if (IS_BLANK(P_CELL))
            return END_FLAG;

        assert(IS_INTEGER(P_CELL));
        return VAL_INT32(P_CELL); }

    default:
        break;
    }

    // !!! R3-Alpha said "Match with some other value"... is this a good
    // default?!
    //
    if (Cmp_Value(item, rule, P_HAS_CASE) == 0)
        return pos + 1;

    return END_FLAG;
}


//
// To make clear that the frame's P_POS is usually enough to know the state
// of the parse, this is the version used in the main loop.  To_Thru uses
// the random access variation.
//
inline static REBIXO Parse_Array_One_Rule(REBFRM *f, const RELVAL *rule) {
    return Parse_Array_One_Rule_Core(f, P_POS, rule);
}


//
//  To_Thru_Block_Rule: C
//
// The TO and THRU keywords in PARSE do not necessarily match the direct next
// item, but scan ahead in the series.  This scan may be successful or not,
// and how much the match consumes can vary depending on how much THRU
// content was expressed in the rule.
//
// !!! This routine from R3-Alpha is fairly circuitous.  As with the rest of
// the code, it gets clarified in small steps.
//
static REBIXO To_Thru_Block_Rule(
    REBFRM *f,
    const RELVAL *rule_block,
    REBOOL is_thru
) {
    DECLARE_LOCAL (cell); // holds evaluated rules (use frame cell instead?)

    RELVAL *blk;

    REBCNT pos = P_POS;
    for (; pos <= SER_LEN(P_INPUT); ++pos) {
        blk = VAL_ARRAY_HEAD(rule_block);
        for (; NOT_END(blk); blk++) {
            const RELVAL *rule = blk;

            if (IS_BAR(rule))
                fail (Error_Parse_Rule()); // !!! Shouldn't `TO [|]` succed?

            if (IS_WORD(rule)) {
                REBSYM cmd = VAL_CMD(rule);

                if (cmd != SYM_0) {
                    if (cmd == SYM_END) {
                        if (pos >= SER_LEN(P_INPUT)) {
                            pos = SER_LEN(P_INPUT);
                            goto found;
                        }
                        goto next_alternate_rule;
                    }
                    else if (cmd == SYM_QUOTE) {
                        rule = ++blk; // next rule is the quoted value
                        if (IS_END(rule))
                            fail (Error_Parse_Rule());

                        if (IS_GROUP(rule)) {
                            REBSPC *derived = Derive_Specifier(
                                P_RULE_SPECIFIER,
                                rule
                            );
                            if (Do_At_Throws( // might GC
                                cell,
                                VAL_ARRAY(rule),
                                VAL_INDEX(rule),
                                derived
                            )) {
                                Move_Value(P_OUT, cell);
                                return THROWN_FLAG;
                            }
                            rule = cell;
                        }
                    }
                    else
                        fail (Error_Parse_Rule());
                }
                else {
                    Copy_Opt_Var_May_Fail(cell, rule, P_RULE_SPECIFIER);
                    rule = cell;
                }
            }
            else if (IS_PATH(rule))
                rule = Get_Parse_Value(cell, rule, P_RULE_SPECIFIER);

            // Try to match it:
            if (ANY_ARRAY_KIND(P_TYPE)) {
                if (ANY_ARRAY(rule))
                    fail (Error_Parse_Rule());

                REBIXO i = Parse_Array_One_Rule_Core(f, pos, rule);
                if (i == THROWN_FLAG) {
                    assert(THROWN(P_OUT));
                    return THROWN_FLAG;
                }

                if (i != END_FLAG) {
                    pos = cast(REBCNT, i);
                    if (!is_thru) pos--; // passed it, so back up if only TO...
                    goto found;
                }
            }
            else if (P_TYPE == REB_BINARY) {
                REBYTE ch1 = *BIN_AT(P_INPUT, pos);

                // Handle special string types:
                if (IS_CHAR(rule)) {
                    if (VAL_CHAR(rule) > 0xff)
                        fail (Error_Parse_Rule());

                    if (ch1 == VAL_CHAR(rule)) {
                        if (is_thru) ++pos;
                        goto found;
                    }
                }
                else if (IS_BINARY(rule)) {
                    if (ch1 == *VAL_BIN_AT(rule)) {
                        REBCNT len = VAL_LEN_AT(rule);
                        if (len == 1) {
                            if (is_thru) ++pos;
                            goto found;
                        }

                        if (0 == Compare_Bytes(
                            BIN_AT(P_INPUT, pos),
                            VAL_BIN_AT(rule),
                            len,
                            FALSE
                        )) {
                            if (is_thru) pos += len;
                            goto found;
                        }
                    }
                }
                else if (IS_INTEGER(rule)) {
                    if (VAL_INT64(rule) > 0xff)
                        fail (Error_Parse_Rule());

                    if (ch1 == VAL_INT32(rule)) {
                        if (is_thru) ++pos;
                        goto found;
                    }
                }
                else
                    fail (Error_Parse_Rule());
            }
            else { // String
                REBUNI ch_unadjusted = GET_ANY_CHAR(P_INPUT, pos);
                REBUNI ch;
                if (!P_HAS_CASE)
                    ch = UP_CASE(ch_unadjusted);
                else
                    ch = ch_unadjusted;

                // Handle special string types:
                if (IS_CHAR(rule)) {
                    REBUNI ch2 = VAL_CHAR(rule);
                    if (!P_HAS_CASE)
                        ch2 = UP_CASE(ch2);
                    if (ch == ch2) {
                        if (is_thru) ++pos;
                        goto found;
                    }
                }
                // bitset
                else if (IS_BITSET(rule)) {
                    if (Check_Bit(VAL_SERIES(rule), ch, NOT(P_HAS_CASE))) {
                        if (is_thru) ++pos;
                        goto found;
                    }
                }
                else if (IS_TAG(rule)) {
                    if (ch == '<') {
                        //
                        // !!! This code was adapted from Parse_to, and is
                        // inefficient in the sense that it forms the tag
                        //
                        REBSER *formed = Copy_Form_Value(rule, 0);
                        REBCNT len = SER_LEN(formed);
                        REBCNT i = Find_Str_Str(
                            P_INPUT,
                            0,
                            pos,
                            SER_LEN(P_INPUT),
                            1,
                            formed,
                            0,
                            len,
                            AM_FIND_MATCH | P_FIND_FLAGS
                        );
                        Free_Series(formed);
                        if (i != NOT_FOUND) {
                            pos = i;
                            if (is_thru) pos += len;
                            goto found;
                        }
                    }
                }
                else if (ANY_STRING(rule)) {
                    REBUNI ch2 = VAL_ANY_CHAR(rule);
                    if (!P_HAS_CASE) ch2 = UP_CASE(ch2);

                    if (ch == ch2) {
                        REBCNT len = VAL_LEN_AT(rule);
                        if (len == 1) {
                            if (is_thru) ++pos;
                            goto found;
                        }

                        REBCNT i = Find_Str_Str(
                            P_INPUT,
                            0,
                            pos,
                            SER_LEN(P_INPUT),
                            1,
                            VAL_SERIES(rule),
                            VAL_INDEX(rule),
                            len,
                            AM_FIND_MATCH | P_FIND_FLAGS
                        );

                        if (i != NOT_FOUND) {
                            pos = i;
                            if (is_thru) pos += len;
                            goto found;
                        }
                    }
                }
                else if (IS_INTEGER(rule)) {
                    if (ch_unadjusted == cast(REBUNI, VAL_INT32(rule))) {
                        if (is_thru) ++pos;
                        goto found;
                    }
                }
                else
                    fail (Error_Parse_Rule());
            }

        next_alternate_rule:; // alternates are BAR! separated `[a | b | c]`
            blk++;
            if (IS_END(blk))
                break;

            if (IS_GROUP(blk)) // don't run GROUP!s in the failing rule
                blk++;

            if (IS_END(blk))
                break;

            if (!IS_BAR(blk))
                fail (Error_Parse_Rule());
        }
    }
    return END_FLAG;

found:
    if (NOT_END(blk + 1) && IS_GROUP(blk + 1)) {
        DECLARE_LOCAL (dummy);
        REBSPC *derived = Derive_Specifier(P_RULE_SPECIFIER, rule_block);
        if (Do_At_Throws(
            dummy,
            VAL_ARRAY(blk + 1),
            VAL_INDEX(blk + 1),
            derived
        )) {
            Move_Value(P_OUT, dummy);
            return THROWN_FLAG;
        }
    }
    return pos;
}


//
//  To_Thru_Non_Block_Rule: C
//
static REBIXO To_Thru_Non_Block_Rule(
    REBFRM *f,
    const RELVAL *rule,
    REBOOL is_thru
) {
    assert(!IS_BLOCK(rule));

    if (IS_BLANK(rule))
        return P_POS; // make it a no-op

    if (IS_INTEGER(rule)) {
        //
        // `TO/THRU (INTEGER!)` JUMPS TO SPECIFIC INDEX POSITION
        //
        // !!! This allows jumping backward to an index before the parse
        // position, while TO generally only goes forward otherwise.  Should
        // this be done by another operation?  (Like SEEK?)
        //
        // !!! Negative numbers get cast to large integers, needs error!
        // But also, should there be an option for relative addressing?
        //
        REBCNT i = cast(REBCNT, Int32(const_KNOWN(rule))) - (is_thru ? 0 : 1);
        if (i > SER_LEN(P_INPUT))
            return SER_LEN(P_INPUT);
        return i;
    }

    if (IS_WORD(rule) && VAL_WORD_SYM(rule) == SYM_END) {
        //
        // `TO/THRU END` JUMPS TO END INPUT SERIES (ANY SERIES TYPE)
        //
        return SER_LEN(P_INPUT);
    }

    if (GET_SER_FLAG(P_INPUT, SERIES_FLAG_ARRAY)) {
        //
        // FOR ARRAY INPUT WITH NON-BLOCK RULES, USE Find_In_Array()
        //
        // !!! This adjusts it to search for non-literal words, but are there
        // other considerations for how non-block rules act with array input?
        //
        DECLARE_LOCAL (word);
        if (IS_LIT_WORD(rule)) {
            Derelativize(word, rule, P_RULE_SPECIFIER);
            VAL_SET_TYPE_BITS(word, REB_WORD);
            rule = word;
        }

        REBCNT i = Find_In_Array(
            ARR(P_INPUT),
            P_POS,
            SER_LEN(P_INPUT),
            rule,
            1,
            P_HAS_CASE ? AM_FIND_CASE : 0,
            1
        );

        if (i == NOT_FOUND)
            return END_FLAG;

        if (is_thru)
            return i + 1;

        return i;
    }

    //=//// PARSE INPUT IS A STRING OR BINARY, USE A FIND ROUTINE /////////=//

    if (ANY_BINSTR(rule)) {
        if (!IS_STRING(rule) && !IS_BINARY(rule)) {
            // !!! Can this be optimized not to use COPY?
            REBSER *formed = Copy_Form_Value(rule, 0);
            REBCNT form_len = SER_LEN(formed);
            REBCNT i = Find_Str_Str(
                P_INPUT,
                0,
                P_POS,
                SER_LEN(P_INPUT),
                1,
                formed,
                0,
                form_len,
                (P_FIND_FLAGS & AM_FIND_CASE)
                    ? AM_FIND_CASE
                    : 0
            );
            Free_Series(formed);

            if (i == NOT_FOUND)
                return END_FLAG;

            if (is_thru)
                return i + form_len;

            return i;
        }

        REBCNT i = Find_Str_Str(
            P_INPUT,
            0,
            P_POS,
            SER_LEN(P_INPUT),
            1,
            VAL_SERIES(rule),
            VAL_INDEX(rule),
            VAL_LEN_AT(rule),
            (P_FIND_FLAGS & AM_FIND_CASE)
                ? AM_FIND_CASE
                : 0
        );

        if (i == NOT_FOUND)
            return END_FLAG;

        if (is_thru)
            return i + VAL_LEN_AT(rule);

        return i;
    }

    if (IS_CHAR(rule)) {
        REBCNT i = Find_Str_Char(
            VAL_CHAR(rule),
            P_INPUT,
            0,
            P_POS,
            SER_LEN(P_INPUT),
            1,
            (P_FIND_FLAGS & AM_FIND_CASE)
                ? AM_FIND_CASE
                : 0
        );

        if (i == NOT_FOUND)
            return END_FLAG;

        if (is_thru)
            return i + 1;

        return i;
    }

    if (IS_BITSET(rule)) {
        REBCNT i = Find_Str_Bitset(
            P_INPUT,
            0,
            P_POS,
            SER_LEN(P_INPUT),
            1,
            VAL_BITSET(rule),
            (P_FIND_FLAGS & AM_FIND_CASE)
                ? AM_FIND_CASE
                : 0
        );

        if (i == NOT_FOUND)
            return END_FLAG;

        if (is_thru)
            return i + 1;

        return i;
    }

    fail (Error_Parse_Rule());
}


//
//  Do_Eval_Rule: C
//
// Perform a DO/NEXT on the *input* as a code block, and match the following
// rule against the evaluative result.
//
//     parse [1 + 2] [do [quote 3]] => true
//
// The rule may be in a block or inline.
//
//     parse [reverse copy "abc"] [do "cba"]
//     parse [reverse copy "abc"] [do ["cba"]]
//
// !!! Due to failures in the mechanics of "Parse_One_Rule", a block must
// be used on rules that are more than one item in length.
//
// This feature was added to make it easier to do dialect processing where the
// dialect had code inline.  It can be a little hard to get one's head around,
// because it says `do [...]` and yet the `...` is a parse rule and not the
// code to be executed.  But this is somewhat in the spirit of operations
// like COPY which are not operating on their arguments, but implicitly taking
// the series itself as an argument.
//
// !!! The way this feature was expressed in R3-Alpha isolates it from
// participating in iteration or as the target of an outer rule, e.g.
//
//     parse [1 + 2] [set var do [quote 3]] ;-- var gets 1, not 3
//
// Other problems arise since the caller doesn't know about the trickiness
// of this evaluation, e.g. this won't work either:
//
//     parse [1 + 2] [thru do integer!]
//
static REBIXO Do_Eval_Rule(REBFRM *f)
{
    if (NOT_SER_FLAG(P_INPUT, SERIES_FLAG_ARRAY)) // can't be an ANY-STRING!
        fail (Error_Parse_Rule());

    if (IS_END(P_RULE))
        fail (Error_Parse_End());

    // The DO'ing of the input series will generate a single REBVAL.  But
    // for a parse to run on some input, that input has to be in a series...
    // so the single item is put into a block holder.  If the item was already
    // a block, then the user will have to use INTO to parse into it.
    //
    // Note: Implicitly handling a block evaluative result as an array would
    // make it impossible to tell whether the evaluation produced [1] or 1.
    //
    REBARR *holder;

    REBIXO indexor;
    if (P_POS >= SER_LEN(P_INPUT)) {
        //
        // We could short circuit and notice if the rule was END or not, but
        // that leaves out other potential matches like `[(print "Hi") end]`
        // as a rule.  Keep it generalized and pass an empty block in as
        // the series to process.
        //
        holder = EMPTY_ARRAY; // read-only
        indexor = END_FLAG;
    }
    else {
        // Evaluate next expression from the *input* series (not the rules)
        //
        indexor = DO_NEXT_MAY_THROW(
            P_CELL, ARR(P_INPUT), P_POS, P_INPUT_SPECIFIER
        );
        if (indexor == THROWN_FLAG) { // BREAK/RETURN/QUIT/THROW...
            Move_Value(P_OUT, P_CELL);
            return THROWN_FLAG;
        }

        // !!! This copies a single value into a block to use as data, because
        // parse input is matched as a series.  Can this be avoided?
        //
        holder = Alloc_Singular_Array();
        Move_Value(ARR_HEAD(holder), P_CELL);
        Deep_Freeze_Array(holder); // don't allow modification of temporary
    }

    // We want to reuse the same frame we're in, because if you say
    // something like `parse [1 + 2] [do [quote 3]]`, the`[quote 3]` rule
    // should be consumed.  We also want to be able to use a nested rule
    // inline, such as `do skip` not only allow `do [skip]`.
    //
    // So the rules should be processed normally, it's just that for the
    // duration of the next rule the *input* is the temporary evaluative
    // result.
    //
    DECLARE_LOCAL (saved_input);
    Move_Value(saved_input, P_INPUT_VALUE); // series and P_POS position
    PUSH_GUARD_VALUE(saved_input);
    Init_Block(P_INPUT_VALUE, holder);

    // !!! There is not a generic form of SUBPARSE/NEXT, but there should be.
    // The particular factoring of the one-rule form of parsing makes us
    // redo work like fetching words/paths, which should not be needed.
    //
    DECLARE_LOCAL (cell);
    const RELVAL *rule = Get_Parse_Value(cell, P_RULE, P_RULE_SPECIFIER);

    // !!! The actual mechanic here does not permit you to say `do thru x`
    // or other multi-argument things.  A lot of R3-Alpha's PARSE design was
    // rather ad-hoc and hard to adapt.  The one rule parsing does not
    // advance the position, but it should.
    //
    REBIXO n = Parse_Array_One_Rule(f, rule);
    FETCH_NEXT_RULE_MAYBE_END(f);

    // Restore the input series to what it was before parsing the temporary
    // (this restores P_POS, since it's just an alias for the input's index)
    //
    Move_Value(P_INPUT_VALUE, saved_input);
    DROP_GUARD_VALUE(saved_input);

    if (n == THROWN_FLAG) {
        assert(THROWN(P_OUT));
        return THROWN_FLAG;
    }

    if (n == ARR_LEN(holder)) {
        //
        // Eval result reaching end means success, so return index advanced
        // past the evaluation.
        //
        // !!! Although DO_NEXT_MAY_THROW uses an END_FLAG-based
        // convention when it reaches the end, these parse routines always
        // return an array index.
        //
        return indexor == END_FLAG ? SER_LEN(P_INPUT) : indexor;
    }

    return P_POS; // as failure, hand back original position--no advancement
}


//
//  subparse: native [
//
//  {Internal support function for PARSE (acts as variadic to consume rules)}
//
//      return: [integer! blank!]
//      input [any-series!]
//      find-flags [integer!]
//  ]
//
REBNATIVE(subparse)
//
// Rules are matched until one of these things happens:
//
// * A rule fails, and is not then picked up by a later "optional" rule.
// This returns R_OUT with the value in out as BLANK!.
//
// * You run out of rules to apply without any failures or errors, and the
// position in the input series is returned.  This may be at the end of
// the input data or not--it's up to the caller to decide if that's relevant.
// This will return R_OUT with out containing an integer index.
//
// !!! The return of an integer index is based on the R3-Alpha convention,
// but needs to be rethought in light of the ability to switch series.  It
// does not seem that all callers of Subparse's predecessor were prepared for
// the semantics of switching the series.
//
// * A `fail()`, in which case the function won't return--it will longjmp
// up to the most recently pushed handler.  This can happen due to an invalid
// rule pattern, or if there's an error in code that is run in parentheses.
//
// * A throw-style result caused by DO code run in parentheses (e.g. a
// THROW, RETURN, BREAK, CONTINUE).  This returns R_OUT_IS_THROWN.
//
// * A special throw to indicate a return out of the PARSE itself, triggered
// by the RETURN instruction.  This also returns R_OUT_IS_THROWN, but will
// be caught by PARSE before returning.
//
{
    REBFRM *f = frame_; // nice alias of implicit native parameter

    assert(IS_END(P_OUT)); // invariant provided by evaluator

#if !defined(NDEBUG)
    //
    // These parse state variables live in chunk-stack REBVARs, which can be
    // annoying to find to inspect in the debugger.  This makes pointers into
    // the value payloads so they can be seen more easily.
    //
    const REBCNT *pos_debug = &P_POS;
    (void)pos_debug; // UNUSED() forces corruption in C++11 debug builds

    REBUPT tick = TG_Tick; // helpful to cache for visibility also
#endif

    DECLARE_LOCAL (save);

    REBCNT start = P_POS; // recovery restart point
    REBCNT begin = P_POS; // point at beginning of match

    // The loop iterates across each REBVAL's worth of "rule" in the rule
    // block.  Some of these rules just set `flags` and `continue`, so that
    // the flags will apply to the next rule item.  If the flag is PF_SET
    // or PF_COPY, then the `set_or_copy_word` pointers will be assigned
    // at the same time as the active target of the COPY or SET.
    //
    // !!! This flagging process--established by R3-Alpha--is efficient
    // but somewhat haphazard.  It may work for `while ["a" | "b"]` to
    // "set the PF_WHILE" flag when it sees the `while` and then iterate
    // a rule it would have otherwise processed just once.  But there are
    // a lot of edge cases like `while |` where this method isn't set up
    // to notice a "grammar error".  It could use review.
    //
    REBFLGS flags = 0;
    const RELVAL *set_or_copy_word = NULL;

    REBINT mincount = 1; // min pattern count
    REBINT maxcount = 1; // max pattern count

    while (FRM_HAS_MORE(f)) {
        //
        // The rule in the block of rules can be literal, while the "real
        // rule" we want to process is the result of a variable fetched from
        // that item.  If the code makes it to the iterated rule matching
        // section, then rule should be set to something non-NULL by then...
        //
        const RELVAL *rule = NULL;

        // Some rules that make it to the iterated rule section have a
        // parameter.  For instance `3 into [some "a"]` will actually run
        // the INTO `rule` 3 times with the `subrule` of `[some "a"]`.
        // Because it is iterated it is only captured the first time through,
        // so setting it to NULL indicates for such instructions that it
        // has not been captured yet.
        //
        const RELVAL *subrule = NULL;

        /* Print_Parse_Index(f); */
        UPDATE_EXPRESSION_START(f);

    #if !defined(NDEBUG)
        ++TG_Tick;
        tick = TG_Tick;
        cast(void, tick); // suppress compiler warning about lack of use
    #endif

    //==////////////////////////////////////////////////////////////////==//
    //
    // GARBAGE COLLECTION AND EVENT HANDLING
    //
    //==////////////////////////////////////////////////////////////////==//

        assert(Eval_Count >= 0);
        if (--Eval_Count == 0) {
            SET_END(P_CELL);

            if (Do_Signals_Throws(P_CELL))
                fail (Error_No_Catch_For_Throw(P_CELL));

            assert(IS_END(P_CELL));
        }

    //==////////////////////////////////////////////////////////////////==//
    //
    // PRE-RULE PROCESSING SECTION
    //
    //==////////////////////////////////////////////////////////////////==//

        // For non-iterated rules, including setup for iterated rules.
        // The input index is not advanced here, but may be changed by
        // a GET-WORD variable.

        if (IS_BAR(P_RULE)) {
            //
            // If a BAR! is hit while processing any rules in the rules
            // block, then that means the current option didn't fail out
            // first...so it's a success for the rule.  Stop processing and
            // return the current input position.
            //
            // (Note this means `[| ...anything...]` is a "no-op" match)
            //
            Init_Integer(P_OUT, P_POS);
            return R_OUT;
        }

        // If word, set-word, or get-word, process it:
        if (VAL_TYPE(P_RULE) >= REB_WORD && VAL_TYPE(P_RULE) <= REB_GET_WORD) {

            REBSYM cmd = VAL_CMD(P_RULE);
            if (cmd != SYM_0) {
                if (NOT(IS_WORD(P_RULE))) { // COPY: :THRU ...
                    DECLARE_LOCAL (non_word);
                    Derelativize(non_word, P_RULE, P_RULE_SPECIFIER);
                    fail (Error_Parse_Command_Raw(non_word));
                }

                if (cmd <= SYM_BREAK) { // optimization

                    switch (cmd) {
                    // Note: mincount = maxcount = 1 on entry
                    case SYM_WHILE:
                        flags |= PF_WHILE;
                        // falls through
                    case SYM_ANY:
                        mincount = 0;
                        // falls through
                    case SYM_SOME:
                        maxcount = MAX_I32;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        continue;

                    case SYM_OPT:
                        mincount = 0;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        continue;

                    case SYM_COPY:
                        flags |= PF_COPY;
                        goto set_or_copy_pre_rule;
                    case SYM_SET:
                        flags |= PF_SET;
                        // falls through
                    set_or_copy_pre_rule:
                        FETCH_NEXT_RULE_MAYBE_END(f);

                        if (NOT(IS_WORD(P_RULE) || IS_SET_WORD(P_RULE))) {
                            DECLARE_LOCAL (bad_var);
                            Derelativize(bad_var, P_RULE, P_RULE_SPECIFIER);
                            fail (Error_Parse_Variable_Raw(bad_var));
                        }

                        if (VAL_CMD(P_RULE)) { // set set [...]
                            DECLARE_LOCAL (keyword);
                            Derelativize(keyword, P_RULE, P_RULE_SPECIFIER);
                            fail (Error_Parse_Command_Raw(keyword));
                        }

                        set_or_copy_word = P_RULE;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        continue;

                    case SYM_NOT:
                        flags |= PF_NOT;
                        flags ^= PF_NOT2;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        continue;

                    case SYM_AND:
                        flags |= PF_AND;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        continue;

                    case SYM_THEN:
                        flags |= PF_THEN;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        continue;

                    case SYM_REMOVE:
                        flags |= PF_REMOVE;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        continue;

                    case SYM_INSERT:
                        flags |= PF_INSERT;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        goto post_match_processing;

                    case SYM_CHANGE:
                        flags |= PF_CHANGE;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        continue;

                    // There are two RETURNs: one is a matching form, so with
                    // 'parse data [return "abc"]' you are not asking to
                    // return the literal string "abc" independent of input.
                    // it will only return if "abc" matches.  This works for
                    // a rule reference as well, such as 'return rule'.
                    //
                    // The second option is if you put the value in parens,
                    // in which case it will just return whatever that value
                    // happens to be, e.g. 'parse data [return ("abc")]'

                    case SYM_RETURN:
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        if (IS_GROUP(P_RULE)) {
                            DECLARE_LOCAL (evaluated);
                            if (Do_At_Throws(
                                evaluated,
                                VAL_ARRAY(P_RULE),
                                VAL_INDEX(P_RULE),
                                P_RULE_SPECIFIER
                            )) {
                                // If the group evaluation result gives a
                                // THROW, BREAK, CONTINUE, etc then we'll
                                // return that
                                Move_Value(P_OUT, evaluated);
                                return R_OUT_IS_THROWN;
                            }

                            Move_Value(P_OUT, NAT_VALUE(parse));
                            CONVERT_NAME_TO_THROWN(P_OUT, evaluated);
                            return R_OUT_IS_THROWN;
                        }
                        flags |= PF_RETURN;
                        continue;

                    case SYM_ACCEPT:
                    case SYM_BREAK: {
                        //
                        // This has to be throw-style, because it's not enough
                        // to just say the current rule succeeded...it climbs
                        // up and affects an enclosing parse loop.
                        //
                        DECLARE_LOCAL (thrown_arg);
                        Init_Integer(thrown_arg, P_POS);
                        Move_Value(P_OUT, NAT_VALUE(parse_accept));

                        // Unfortunately, when the warnings are set all the
                        // way high for uninitialized variable use, the
                        // compiler may think this integer's binding will
                        // be used by the Move_Value() inlined here.  Get
                        // past that by initializing it.
                        //
                        thrown_arg->extra.binding = UNBOUND; // unused by ints

                        CONVERT_NAME_TO_THROWN(P_OUT, thrown_arg);
                        return R_OUT_IS_THROWN;
                    }

                    case SYM_REJECT: {
                        //
                        // Similarly, this is a break/continue style "throw"
                        //
                        Move_Value(P_OUT, NAT_VALUE(parse_reject));
                        CONVERT_NAME_TO_THROWN(P_OUT, BLANK_VALUE);
                        return R_OUT;
                    }

                    case SYM_FAIL:
                        P_POS = NOT_FOUND;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        goto post_match_processing;

                    case SYM_IF: {
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        if (FRM_AT_END(f))
                            fail (Error_Parse_End());

                        if (!IS_GROUP(P_RULE))
                            fail (Error_Parse_Rule());

                        // might GC
                        DECLARE_LOCAL (condition);
                        if (Do_At_Throws(
                            condition,
                            VAL_ARRAY(P_RULE),
                            VAL_INDEX(P_RULE),
                            P_RULE_SPECIFIER
                        )) {
                            Move_Value(P_OUT, condition);
                            return R_OUT_IS_THROWN;
                        }

                        FETCH_NEXT_RULE_MAYBE_END(f);

                        if (IS_TRUTHY(condition))
                            continue;

                        P_POS = NOT_FOUND;
                        goto post_match_processing;
                    }

                    case SYM_LIMIT:
                        fail (Error_Not_Done_Raw());

                    case SYM__Q_Q:
                        Print_Parse_Index(f);
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        continue;

                    default: //the list above should be exhaustive
                        assert(FALSE);
                    }
                }
                // Any other cmd must be a match command, so proceed...
                rule = P_RULE;
            }
            else {
                // It's not a PARSE command, get or set it

                // word: - set a variable to the series at current index
                if (IS_SET_WORD(P_RULE)) {
                    //
                    // !!! Review meaning of marking the parse in a slot that
                    // is a target of a rule, e.g. `thru pos: xxx` #
                    //
                    // https://github.com/rebol/rebol-issues/issues/2269
                    //
                    // if (flags != 0) fail (Error_Parse_Rule());

                    Move_Value(
                        Sink_Var_May_Fail(P_RULE, P_RULE_SPECIFIER),
                        P_INPUT_VALUE
                    );
                    FETCH_NEXT_RULE_MAYBE_END(f);
                    continue;
                }

                // :word - change the index for the series to a new position
                if (IS_GET_WORD(P_RULE)) {
                    DECLARE_LOCAL (temp);
                    Copy_Opt_Var_May_Fail(temp, P_RULE, P_RULE_SPECIFIER);
                    if (!ANY_SERIES(temp)) { // #1263
                        DECLARE_LOCAL (non_series);
                        Derelativize(non_series, P_RULE, P_RULE_SPECIFIER);
                        fail (Error_Parse_Series_Raw(non_series));
                    }
                    Set_Parse_Series(f, temp);

                    // !!! `continue` is used here without any post-"match"
                    // processing, so the only way `begin` will get set for
                    // the next rule is if it's set here, else commands like
                    // INSERT that follow will insert at the old location.
                    //
                    // https://github.com/rebol/rebol-issues/issues/2269
                    //
                    // Without known resolution on #2269, it isn't clear if
                    // there is legitimate meaning to seeking a parse in mid
                    // rule or not.  So only reset the begin position if the
                    // seek appears to be a "separate rule" in its own right.
                    //
                    if (flags == 0)
                        begin = P_POS;

                    FETCH_NEXT_RULE_MAYBE_END(f);
                    continue;
                }

                // word - some other variable
                if (IS_WORD(P_RULE)) {
                    Copy_Opt_Var_May_Fail(save, P_RULE, P_RULE_SPECIFIER);
                    rule = save;
                    if (IS_VOID(rule))
                        fail (Error_No_Value_Core(P_RULE, P_RULE_SPECIFIER));
                }
                else {
                    // rule can still be 'word or /word
                    rule = P_RULE;
                }
            }
        }
        else if (ANY_PATH(P_RULE)) {
            if (IS_PATH(P_RULE)) {
                //
                // !!! This evaluates GROUP!s.  Should it?
                //
                if (Get_Path_Throws_Core(save, P_RULE, P_RULE_SPECIFIER))
                    fail (Error_No_Catch_For_Throw(save));

                rule = save;
            }
            else if (IS_SET_PATH(P_RULE)) {
                //
                // !!! This evaluates GROUP!s.  Should it?
                //
                if (Set_Path_Throws_Core(
                    save, P_RULE, P_RULE_SPECIFIER, P_INPUT_VALUE
                )){
                    fail (Error_No_Catch_For_Throw(save));
                }

                // Nothing left to do after storing the parse position in the
                // path location...continue.
                //
                FETCH_NEXT_RULE_MAYBE_END(f);
                continue;
            }
            else if (IS_GET_PATH(P_RULE)) {
                //
                // !!! This evaluates GROUP!s.  Should it?
                //
                if (Get_Path_Throws_Core(save, P_RULE, P_RULE_SPECIFIER))
                    fail (Error_No_Catch_For_Throw(save));

                // !!! This allows the series to be changed, as per #1263,
                // but note the positions being returned and checked aren't
                // prepared for this, they only exchange numbers ATM (!!!)
                //
                if (NOT(ANY_SERIES(save)))
                    fail (Error_Parse_Series_Raw(save));

                Set_Parse_Series(f, save);
                FETCH_NEXT_RULE_MAYBE_END(f);
                continue;
            }
            else {
                assert(IS_LIT_PATH(P_RULE));
                rule = P_RULE;
            }

            if (P_POS > SER_LEN(P_INPUT))
                P_POS = SER_LEN(P_INPUT);
        }
        else {
            rule = P_RULE;
        }

        // All cases should have either set `rule` by this point or continued
        //
        assert(rule != NULL && !IS_VOID(rule));

        if (IS_GROUP(rule)) {
            DECLARE_LOCAL (evaluated);
            REBSPC *derived = Derive_Specifier(P_RULE_SPECIFIER, rule);
            if (Do_At_Throws( // might GC
                evaluated,
                VAL_ARRAY(rule),
                VAL_INDEX(rule),
                derived
            )) {
                Move_Value(P_OUT, evaluated);
                return R_OUT_IS_THROWN;
            }
            // ignore evaluated if it's not THROWN?

            if (P_POS > SER_LEN(P_INPUT)) P_POS = SER_LEN(P_INPUT);
            FETCH_NEXT_RULE_MAYBE_END(f);
            continue;
        }

        // Counter? 123
        if (IS_INTEGER(rule)) { // Specify count or range count
            flags |= PF_WHILE;
            mincount = maxcount = Int32s(const_KNOWN(rule), 0);

            FETCH_NEXT_RULE_MAYBE_END(f);
            if (FRM_AT_END(f))
                fail (Error_Parse_End());

            rule = Get_Parse_Value(save, P_RULE, P_RULE_SPECIFIER);

            if (IS_INTEGER(rule)) {
                maxcount = Int32s(const_KNOWN(rule), 0);

                FETCH_NEXT_RULE_MAYBE_END(f);
                if (FRM_AT_END(f))
                    fail (Error_Parse_End());

                rule = Get_Parse_Value(save, P_RULE, P_RULE_SPECIFIER);
            }
        }
        // else fall through on other values and words

    //==////////////////////////////////////////////////////////////////==//
    //
    // ITERATED RULE PROCESSING SECTION
    //
    //==////////////////////////////////////////////////////////////////==//

        // Repeats the same rule N times or until the rule fails.
        // The index is advanced and stored in a temp variable i until
        // the entire rule has been satisfied.

        FETCH_NEXT_RULE_MAYBE_END(f);

        begin = P_POS;// input at beginning of match section

        REBINT count; // gotos would cross initialization
        count = 0;
        while (count < maxcount) {
            if (IS_BLANK(rule)) // these type tests should be in a switch
                break;

            if (IS_BAR(rule))
                fail (Error_Parse_Rule()); // !!! Is this possible?

            REBIXO i; // temp index point

            if (IS_WORD(rule)) {
                REBSYM cmd = VAL_CMD(rule);

                switch (cmd) {
                case SYM_SKIP:
                    i = (P_POS < SER_LEN(P_INPUT))
                        ? P_POS + 1
                        : END_FLAG;
                    break;

                case SYM_END:
                    i = (P_POS < SER_LEN(P_INPUT))
                        ? END_FLAG
                        : SER_LEN(P_INPUT);
                    break;

                case SYM_TO:
                case SYM_THRU: {
                    if (FRM_AT_END(f))
                        fail (Error_Parse_End());

                    if (!subrule) { // capture only on iteration #1
                        subrule = Get_Parse_Value(
                            save, P_RULE, P_RULE_SPECIFIER
                        );
                        FETCH_NEXT_RULE_MAYBE_END(f);
                    }

                    REBOOL is_thru = LOGICAL(cmd == SYM_THRU);

                    if (IS_BLOCK(subrule))
                        i = To_Thru_Block_Rule(f, subrule, is_thru);
                    else
                        i = To_Thru_Non_Block_Rule(f, subrule, is_thru);
                    break; }

                case SYM_QUOTE: {
                    if (NOT_SER_FLAG(P_INPUT, SERIES_FLAG_ARRAY))
                        fail (Error_Parse_Rule()); // see #2253

                    if (FRM_AT_END(f))
                        fail (Error_Parse_End());

                    if (!subrule) { // capture only on iteration #1
                        subrule = P_RULE;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                    }

                    RELVAL *cmp = ARR_AT(ARR(P_INPUT), P_POS);

                    if (IS_END(cmp))
                        i = END_FLAG;
                    else if (0 == Cmp_Value(cmp, subrule, P_HAS_CASE))
                        i = P_POS + 1;
                    else
                        i = END_FLAG;
                    break;
                }

                case SYM_INTO: {
                    if (FRM_AT_END(f))
                        fail (Error_Parse_End());

                    if (!subrule) {
                        subrule = Get_Parse_Value(
                            save, P_RULE, P_RULE_SPECIFIER
                        );
                        FETCH_NEXT_RULE_MAYBE_END(f);
                    }

                    if (!IS_BLOCK(subrule))
                        fail (Error_Parse_Rule());

                    RELVAL *into = ARR_AT(ARR(P_INPUT), P_POS);

                    if (
                        IS_END(into)
                        || (!ANY_BINSTR(into) && !ANY_ARRAY(into))
                    ){
                        i = END_FLAG;
                        break;
                    }

                    REBOOL interrupted;
                    if (Subparse_Throws(
                        &interrupted,
                        P_CELL,
                        into,
                        P_INPUT_SPECIFIER, // val was taken from P_INPUT
                        subrule,
                        P_RULE_SPECIFIER,
                        P_FIND_FLAGS
                    )) {
                        Move_Value(P_OUT, P_CELL);
                        return R_OUT_IS_THROWN;
                    }

                    // !!! ignore interrupted? (e.g. ACCEPT or REJECT ran)

                    if (IS_BLANK(P_CELL)) {
                        i = END_FLAG;
                    }
                    else {
                        assert(IS_INTEGER(P_CELL));
                        if (VAL_UNT32(P_CELL) != VAL_LEN_HEAD(into))
                            i = END_FLAG;
                        else
                            i = P_POS + 1;
                    }
                    break;
                }

                case SYM_DO: {
                    if (subrule != NULL) {
                        //
                        // Not currently set up for iterating DO rules
                        // since the Do_Eval_Rule routine expects to be
                        // able to arbitrarily update P_NEXT_RULE
                        //
                        fail ("DO rules currently cannot be iterated");
                    }

                    subrule = BLANK_VALUE; // cause an error if iterating

                    i = Do_Eval_Rule(f); // changes P_RULE (should)

                    if (i == THROWN_FLAG) return R_OUT_IS_THROWN;

                    break;
                }

                default:
                    fail (Error_Parse_Rule());
                }
            }
            else if (IS_BLOCK(rule)) {
                REBOOL interrupted;
                if (Subparse_Throws(
                    &interrupted,
                    P_CELL,
                    P_INPUT_VALUE,
                    SPECIFIED,
                    rule,
                    P_RULE_SPECIFIER,
                    P_FIND_FLAGS
                )) {
                    Move_Value(P_OUT, P_CELL);
                    return R_OUT_IS_THROWN;
                }

                // Non-breaking out of loop instances of match or not.

                if (IS_BLANK(P_CELL))
                    i = END_FLAG;
                else {
                    assert(IS_INTEGER(P_CELL));
                    i = VAL_INT32(P_CELL);
                }

                if (interrupted) { // ACCEPT or REJECT ran
                    assert(i != THROWN_FLAG);
                    if (i == END_FLAG)
                        P_POS = NOT_FOUND;
                    else
                        P_POS = cast(REBCNT, i);
                    break;
                }
            }
            else {
                // Parse according to datatype

                if (GET_SER_FLAG(P_INPUT, SERIES_FLAG_ARRAY))
                    i = Parse_Array_One_Rule(f, rule);
                else
                    i = Parse_String_One_Rule(f, rule);

                // i may be THROWN_FLAG
            }

            if (i == THROWN_FLAG)
                return R_OUT_IS_THROWN;

            // Necessary for special cases like: some [to end]
            // i: indicates new index or failure of the match, but
            // that does not mean failure of the rule, because optional
            // matches can still succeed, if if the last match failed.
            //
            if (i != END_FLAG) {
                count++; // may overflow to negative

                if (count < 0)
                    count = MAX_I32; // the forever case

                if (i == P_POS && NOT(flags & PF_WHILE)) {
                    //
                    // input did not advance

                    if (count < mincount) {
                        P_POS = NOT_FOUND; // was not enough
                    }
                    break;
                }
            }
            else {
                if (count < mincount) {
                    P_POS = NOT_FOUND; // was not enough
                }
                else if (i != END_FLAG) {
                    P_POS = cast(REBCNT, i);
                }
                else {
                    // just keep index as is.
                }
                break;
            }
            P_POS = cast(REBCNT, i);
        }

        if (P_POS > SER_LEN(P_INPUT))
            P_POS = NOT_FOUND;

    //==////////////////////////////////////////////////////////////////==//
    //
    // "POST-MATCH PROCESSING"
    //
    //==////////////////////////////////////////////////////////////////==//

        // The comment here says "post match processing", but it may be a
        // failure signal.  Or it may have been a success and there could be
        // a NOT to apply.  Note that failure here doesn't mean returning
        // from SUBPARSE, as there still may be alternate rules to apply
        // with bar e.g. `[a | b | c]`.

    post_match_processing:
        if (flags) {
            if (flags & PF_NOT) {
                if ((flags & PF_NOT2) && P_POS != NOT_FOUND)
                    P_POS = NOT_FOUND;
                else
                    P_POS = begin;
            }

            if (P_POS == NOT_FOUND) {
                if (flags & PF_THEN) {
                    FETCH_TO_BAR_MAYBE_END(f);
                    if (FRM_HAS_MORE(f))
                        FETCH_NEXT_RULE_MAYBE_END(f);
                }
            }
            else {
                // Set count to how much input was advanced
                //
                count = (begin > P_POS) ? 0 : P_POS - begin;

                if (flags & PF_COPY) {
                    DECLARE_LOCAL (temp);
                    if (ANY_ARRAY(P_INPUT_VALUE)) {
                        Init_Any_Array(
                            temp,
                            P_TYPE,
                            Copy_Array_At_Max_Shallow(
                                ARR(P_INPUT),
                                begin,
                                P_INPUT_SPECIFIER,
                                count
                            )
                        );
                    }
                    else if (IS_BINARY(P_INPUT_VALUE)) {
                        Init_Binary(
                            temp,
                            Copy_Sequence_At_Len(P_INPUT, begin, count)
                        );
                    }
                    else {
                        assert(ANY_STRING(P_INPUT_VALUE));

                        DECLARE_LOCAL (begin_val);
                        Init_Any_Series_At(begin_val, P_TYPE, P_INPUT, begin);

                        Init_Any_Series(
                            temp,
                            P_TYPE,
                            Copy_String_At_Len(begin_val, count)
                        );
                    }

                    Move_Value(
                        Sink_Var_May_Fail(set_or_copy_word, P_RULE_SPECIFIER),
                        temp
                    );
                }
                else if (flags & PF_SET) {
                    if (GET_SER_FLAG(P_INPUT, SERIES_FLAG_ARRAY)) {
                        if (count != 0)
                            Derelativize(
                                Sink_Var_May_Fail(
                                    set_or_copy_word, P_RULE_SPECIFIER
                                ),
                                ARR_AT(ARR(P_INPUT), begin),
                                P_INPUT_SPECIFIER
                            );
                        else
                            NOOP; // !!! leave as-is on 0 count?
                    }
                    else {
                        if (count != 0) {
                            REBVAL *var = Sink_Var_May_Fail(
                                set_or_copy_word, P_RULE_SPECIFIER
                            );
                            REBUNI ch = GET_ANY_CHAR(P_INPUT, begin);
                            if (P_TYPE == REB_BINARY)
                                Init_Integer(var, ch);
                            else
                                Init_Char(var, ch);
                        }
                        else
                            NOOP; // !!! leave as-is on 0 count?
                    }
                }

                if (flags & PF_RETURN) {
                    //
                    // See notes in PARSE native on handling of SYM_RETURN
                    //
                    DECLARE_LOCAL (captured);
                    if (GET_SER_FLAG(P_INPUT, SERIES_FLAG_ARRAY)) {
                        Init_Any_Array(
                            captured,
                            P_TYPE,
                            Copy_Array_At_Max_Shallow(
                                ARR(P_INPUT),
                                begin,
                                P_INPUT_SPECIFIER,
                                count
                            )
                        );
                    }
                    else {
                        DECLARE_LOCAL (begin_val);
                        Init_Any_Series_At(begin_val, P_TYPE, P_INPUT, begin);
                        Init_Any_Series(
                            captured,
                            P_TYPE,
                            Copy_String_At_Len(begin_val, count)
                        );
                    }

                    Move_Value(P_OUT, NAT_VALUE(parse));
                    CONVERT_NAME_TO_THROWN(P_OUT, captured);
                    return R_OUT_IS_THROWN;
                }

                if (flags & PF_REMOVE) {
                    if (count) Remove_Series(P_INPUT, begin, count);
                    P_POS = begin;
                }

                if (flags & (PF_INSERT | PF_CHANGE)) {
                    count = (flags & PF_INSERT) ? 0 : count;
                    REBCNT mod_flags = (flags & PF_INSERT) ? 0 : AM_PART;

                    if (FRM_AT_END(f))
                        fail (Error_Parse_End());

                    if (IS_WORD(P_RULE)) { // check for ONLY flag
                        REBSYM cmd = VAL_CMD(P_RULE);
                        switch (cmd) {
                        case SYM_ONLY:
                            mod_flags |= AM_ONLY;
                            FETCH_NEXT_RULE_MAYBE_END(f);
                            if (FRM_AT_END(f))
                                fail (Error_Parse_End());
                            break;

                        case SYM_0: // not a "parse command" word, keep going
                            break;

                        default: // other commands invalid after INSERT/CHANGE
                            fail (Error_Parse_Rule());
                        }
                    }

                    // new value...comment said "CHECK FOR QUOTE!!"
                    rule = Get_Parse_Value(save, P_RULE, P_RULE_SPECIFIER);
                    FETCH_NEXT_RULE_MAYBE_END(f);

                    // If a GROUP!, then execute it first.  See #1279
                    //
                    DECLARE_LOCAL (evaluated);
                    if (IS_GROUP(rule)) {
                        REBSPC *derived = Derive_Specifier(
                            P_RULE_SPECIFIER,
                            rule
                        );
                        if (Do_At_Throws(
                            evaluated,
                            VAL_ARRAY(rule),
                            VAL_INDEX(rule),
                            derived
                        )) {
                            Move_Value(P_OUT, evaluated);
                            return R_OUT_IS_THROWN;
                        }

                        rule = evaluated;
                    }

                    if (GET_SER_FLAG(P_INPUT, SERIES_FLAG_ARRAY)) {
                        DECLARE_LOCAL (specified);
                        Derelativize(specified, rule, P_RULE_SPECIFIER);

                        P_POS = Modify_Array(
                            (flags & PF_CHANGE) ? SYM_CHANGE : SYM_INSERT,
                            ARR(P_INPUT),
                            begin,
                            specified,
                            mod_flags,
                            count,
                            1
                        );

                        if (IS_LIT_WORD(rule))
                            VAL_SET_TYPE_BITS( // keeps binding flags
                                ARR_AT(ARR(P_INPUT), P_POS - 1),
                                REB_WORD
                            );
                    }
                    else {
                        DECLARE_LOCAL (specified);
                        Derelativize(specified, rule, P_RULE_SPECIFIER);

                        P_POS = begin;
                        if (P_TYPE == REB_BINARY)
                            P_POS = Modify_Binary(
                                P_INPUT_VALUE,
                                (flags & PF_CHANGE) ? SYM_CHANGE : SYM_INSERT,
                                specified,
                                mod_flags,
                                count,
                                1
                            );
                        else
                            P_POS = Modify_String(
                                P_INPUT_VALUE,
                                (flags & PF_CHANGE) ? SYM_CHANGE : SYM_INSERT,
                                specified,
                                mod_flags,
                                count,
                                1
                            );

                    }
                }

                if (flags & PF_AND) P_POS = begin;
            }

            flags = 0;
            set_or_copy_word = NULL;
        }

        if (P_POS == NOT_FOUND) {
            //
            // If a rule fails but "falls through", there may still be other
            // options later in the block to consider separated by |.

            FETCH_TO_BAR_MAYBE_END(f);
            if (FRM_AT_END(f)) { // no alternate rule
                Init_Blank(P_OUT);
                return R_OUT;
            }

            // Jump to the alternate rule and reset input
            //
            FETCH_NEXT_RULE_MAYBE_END(f);
            P_POS = begin = start;
        }

        begin = P_POS;
        mincount = maxcount = 1;
    }

    Init_Integer(P_OUT, P_POS); // !!! return switched input series??
    return R_OUT;
}


//
//  parse: native [
//
//  "Parses a series according to grammar rules and returns a result."
//
//      input [any-series!]
//          "Input series to parse (default result for successful match)"
//      rules [block! string! blank!]
//          "Rules to parse by (STRING! and BLANK!/none! are deprecated)"
//      /case
//          "Uses case-sensitive comparison"
//  ]
//
REBNATIVE(parse)
{
    INCLUDE_PARAMS_OF_PARSE;

    REBVAL *rules = ARG(rules);

    if (IS_BLANK(rules) || IS_STRING(rules)) {
        //
        // !!! R3-Alpha supported "simple parse", which was cued by the rules
        // being either NONE! or a STRING!.  Though this functionality does
        // not exist in Ren-C, it's more informative to give an error telling
        // where to look for the functionality than a generic "parse doesn't
        // take that type" error.
        //
        fail (Error_Use_Split_Simple_Raw());
    }

    REBOOL interrupted;
    if (Subparse_Throws(
        &interrupted,
        D_OUT,
        ARG(input),
        SPECIFIED, // input is a non-relative REBVAL
        rules,
        SPECIFIED, // rules is a non-relative REBVAL
        REF(case) || IS_BINARY(ARG(input)) ? AM_FIND_CASE : 0
        //
        // We always want "case-sensitivity" on binary bytes, vs. treating
        // as case-insensitive bytes for ASCII characters.
    )) {
        if (
            IS_FUNCTION(D_OUT)
            && NAT_FUNC(parse) == VAL_FUNC(D_OUT)
        ) {
            // Note the difference:
            //
            //     parse "1020" [(return true) not-seen]
            //     parse "0304" [return [some ["0" skip]]] not-seen]
            //
            // In the first, a parenthesized evaluation ran a `return`, which
            // is aiming to return from a function using a THROWN().  In
            // the second case parse interrupted *itself* with a THROWN_FLAG
            // to evaluate the expression to the result "0304" from the
            // matched pattern.
            //
            // When parse interrupts itself by throwing, it indicates so
            // by using the throw name of its own REB_NATIVE-valued function.
            // This handles that branch and catches the result value.
            //
            CATCH_THROWN(D_OUT, D_OUT);
            return R_OUT;
        }

        // All other throws should just bubble up uncaught.
        //
        return R_OUT_IS_THROWN;
    }

    // Parse can fail if the match rule state can't process pending input.
    //
    if (IS_BLANK(D_OUT))
        return R_FALSE;

    assert(IS_INTEGER(D_OUT));

    // If the match rules all completed, but the parse position didn't end
    // at (or beyond) the tail of the input series, the parse also failed.
    //
    if (VAL_UNT32(D_OUT) < VAL_LEN_HEAD(ARG(input)))
        return R_FALSE;

    // The end was reached.  Return TRUE.  (Alternate thoughts, see #2165)
    //
    return R_TRUE;
}


//
//  parse-accept: native [
//
//  "Accept the current parse rule (Internal Implementation Detail ATM)."
//
//  ]
//
REBNATIVE(parse_accept)
//
// !!! This was not created for user usage, but rather as a label for the
// internal throw used to indicate "accept".
{
    UNUSED(frame_);
    fail ("PARSE-ACCEPT is for internal PARSE use only");
}


//
//  parse-reject: native [
//
//  "Reject the current parse rule (Internal Implementation Detail ATM)."
//
//  ]
//
REBNATIVE(parse_reject)
//
// !!! This was not created for user usage, but rather as a label for the
// internal throw used to indicate "reject".
{
    UNUSED(frame_);
    fail ("PARSE-REJECT is for internal PARSE use only");
}
