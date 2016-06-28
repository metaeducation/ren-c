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
// Copyright 2012-2016 Rebol Open Source Contributors
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

#include "sys-core.h"

#define P_RULE          (f->value + 0) // rvalue
#define P_RULE_LVALUE   (f->value) // lvalue
#define P_RULE_SPECIFIER     (f->specifier)

#define P_INPUT_VALUE       (&f->arg[0])
#define P_TYPE              VAL_TYPE(P_INPUT_VALUE)
#define P_INPUT             VAL_SERIES(P_INPUT_VALUE)
#define P_INPUT_SPECIFIER   VAL_SPECIFIER(P_INPUT_VALUE)
#define P_POS               VAL_INDEX(P_INPUT_VALUE)

#define P_FIND_FLAGS    VAL_INT64(&f->arg[1])
#define P_HAS_CASE      LOGICAL(P_FIND_FLAGS & AM_FIND_CASE)

#define P_OUT (f->out)

// The workings of PARSE don't 100% parallel the DO evaluator, because it can
// go backwards.  Figuring out exactly the points at which it needs to go
// backwards and manage it (such as by copying the data) would be needed
// for things like stream parsing.
//
#define FETCH_NEXT_RULE_MAYBE_END(f) \
    FETCH_NEXT_ONLY_MAYBE_END(f)

#define FETCH_TO_BAR_MAYBE_END(f) \
    while (NOT_END(P_RULE) && !IS_BAR(P_RULE)) \
        { FETCH_NEXT_RULE_MAYBE_END(f); }

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

// Returns SYMBOL or SYM_0 if not a command:
#define GET_CMD(n) (((n) >= SYM_SET && (n) <= SYM_END) ? (n) : SYM_0)
#define VAL_CMD(v) GET_CMD(VAL_WORD_SYM(v))


// Subparse_Throws is a helper that sets up a call frame and invokes Subparse.
//
// !!! This code creates a new frame on each recursion.  On some original
// R3-Alpha PARSE recursions, it reused the parse state and just modified some
// of its fields, then potentially changing them back.  That reduces the amount
// of debugging "transparency" in the backtrace, but may be an acceptable
// optimization if the intermediates are not interesting.  For the moment,
// creating a frame on each recursion brings about more uniformity and shows
// off the debugging.
//
static REBOOL Subparse_Throws(
    REBOOL *interrupted_out,
    REBVAL *out,
    RELVAL *input,
    REBCTX *input_specifier,
    const RELVAL *rules,
    REBCTX *rules_specifier,
    REBCNT find_flags
) {
    struct Reb_Frame frame;
    struct Reb_Frame *f = &frame;
    REB_R r;

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
        SET_INTEGER(out, VAL_LEN_HEAD(rules));
        return FALSE;
    }

    f->out = out;

    SET_FRAME_VALUE(f, VAL_ARRAY_AT(rules));
    f->specifier = IS_SPECIFIC(rules)
        ? VAL_SPECIFIER(const_KNOWN(rules))
        : rules_specifier;

    f->source.array = VAL_ARRAY(rules);
    f->index = VAL_INDEX(rules) + 1;

    f->pending = NULL;
    f->gotten = NULL;

    f->stackvars = Push_Ended_Trash_Chunk(2);
    f->varlist = NULL;

    COPY_VALUE(&f->stackvars[0], input, input_specifier);

    // We always want "case-sensitivity" on binary bytes, vs. treating as
    // case-insensitive bytes for ASCII characters.
    //
    SET_INTEGER(&f->stackvars[1], find_flags);

    f->arg = f->stackvars;
    f->label = Canon(SYM_SUBPARSE);
    f->eval_type = ET_FUNCTION;
    f->func = NAT_FUNC(subparse);
    f->flags = 0;
    f->param = END_CELL; // informs infix lookahead
    f->refine = NULL;
    f->cell.subfeed = NULL;

    PUSH_CALL(f);

    SET_TRASH_SAFE(out);
    r = N_subparse(f);
    assert(!IS_TRASH_DEBUG(out));

    // Can't just drop f->data.stackvars because the debugger may have
    // "reified" the frame into a FRAME!, which means it would now be using
    // the f->data.context field.
    //
    Drop_Function_Args_For_Frame_Core(f, TRUE);

    DROP_CALL(f);

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
    return Error(RE_PARSE_RULE);
}


// Also generic.
//
static REBCTX *Error_Parse_End() {
    return Error(RE_PARSE_END);
}


static void Print_Parse_Index(struct Reb_Frame *f) {
    REBVAL input;
    Val_Init_Series_Index_Core(
        &input,
        P_TYPE,
        P_INPUT,
        P_POS,
        Is_Array_Series(P_INPUT)
            ? P_INPUT_SPECIFIER
            : SPECIFIED
    );

    // Either the rules or the data could be positioned at the end.  The
    // data might even be past the end.
    //
    // !!! Or does PARSE adjust to ensure it never is past the end, e.g.
    // when seeking a position given in a variable or modifying?
    //
    if (IS_END(P_RULE)) {
        if (P_POS >= SER_LEN(P_INPUT))
            Debug_Fmt("[]: ** END **");
        else
            Debug_Fmt("[]: %r", &input);
    }
    else {
        if (P_POS >= SER_LEN(P_INPUT))
            Debug_Fmt("%r: ** END **", P_RULE);
        else
            Debug_Fmt("%r: %r", P_RULE, &input);
    }
}


//
//  Set_Parse_Series: C
// 
// Change the series, ensuring the index is not past the end.
//
static void Set_Parse_Series(
    struct Reb_Frame *f,
    const REBVAL *any_series
) {
    f->stackvars[0] = *any_series;
    VAL_INDEX(&f->stackvars[0]) =
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
// Get the value of a word (when not a command) or path.
// Returns all other values as-is.
// 
// !!! Because path evaluation does not necessarily wind up
// pointing to a variable that exists in memory, a derived
// value may be created during that process.  Previously
// this derived value was kept on the stack, but that
// meant every path evaluation PUSH'd without a known time
// at which a corresponding DROP would be performed.  To
// avoid the stack overflow, this requires you to pass in
// a "safe" storage value location that will be good for
// as long as the returned pointer is needed.  It *may*
// not be used in the case of a word fetch, so pay attention
// to the return value and not the contents of that variable.
// 
// !!! (Review if this can be done a better way.)
//
static const RELVAL *Get_Parse_Value(
    REBVAL *safe,
    const RELVAL *rule,
    REBCTX *specifier
) {
    if (IS_BAR(rule))
        return rule;

    if (IS_WORD(rule)) {
        const REBVAL *var;

        if (VAL_CMD(rule)) return rule;

        var = GET_OPT_VAR_MAY_FAIL(rule, specifier);

        // While NONE! is legal and represents a no-op in parse, if a
        // you write `parse "" [to undefined-value]`...and undefined-value
        // is bound...you may get a void back.  This should be an
        // error, as it is in the evaluator.  (See how this is handled
        // by REB_WORD in %c-do.c)
        //
        if (IS_VOID(var))
            fail (Error_No_Value_Core(rule, specifier));

        return var;
    }

    if (IS_PATH(rule)) {
        //
        // !!! REVIEW: how should GET-PATH! be handled?

        if (Do_Path_Throws_Core(safe, NULL, rule, specifier, NULL))
            fail (Error_No_Catch_For_Throw(safe));

        // See notes above about voids
        //
        if (IS_VOID(safe))
            fail (Error_No_Value_Core(rule, specifier));

        return safe;
    }

    return rule;
}


//
//  Parse_Next_String: C
// 
// Match the next rule in the string ruleset.
// 
// If it matches, return the index just past it.
// Otherwise return NOT_FOUND.
//
static REBCNT Parse_Next_String(
    struct Reb_Frame *f,
    REBCNT index,
    const RELVAL *rule
) {
    REBSER *ser;
    REBCNT flags = P_FIND_FLAGS | AM_FIND_MATCH | AM_FIND_TAIL;

    REBVAL save;

    if (Trace_Level) {
        Trace_Value("input", rule);

        // !!! This used STR_AT (obsolete) but it's not clear that this is
        // necessarily a byte sized series.  Switched to BIN_AT, which will
        // assert if it's not BYTE_SIZE()

        Trace_String(BIN_AT(P_INPUT, index), BIN_LEN(P_INPUT) - index);
    }

    if (IS_BLANK(rule)) return index;

    if (index >= SER_LEN(P_INPUT)) return NOT_FOUND;

    switch (VAL_TYPE(rule)) {

    // Do we match a single character?
    case REB_CHAR:
        if (P_HAS_CASE) {
            if (VAL_CHAR(rule) == GET_ANY_CHAR(P_INPUT, index))
                index = index + 1;
            else
                index = NOT_FOUND;
        }
        else {
            if (
                UP_CASE(VAL_CHAR(rule))
                == UP_CASE(GET_ANY_CHAR(P_INPUT, index))
            ) {
                index = index + 1;
            }
            else
                index = NOT_FOUND;
        }
        break;

    case REB_EMAIL:
    case REB_STRING:
    case REB_BINARY:
        index = Find_Str_Str(
            P_INPUT,
            0,
            index,
            SER_LEN(P_INPUT),
            1,
            VAL_SERIES(rule),
            VAL_INDEX(rule),
            VAL_LEN_AT(rule),
            flags
        );
        break;

    case REB_BITSET:
        if (Check_Bit(
            VAL_SERIES(rule), GET_ANY_CHAR(P_INPUT, index), NOT(P_HAS_CASE)
        )) {
            // We matched to a char set, advance.
            //
            index++;
        }
        else
            index = NOT_FOUND;
        break;
/*
    case REB_DATATYPE:  // Currently: integer!
        if (VAL_TYPE_KIND(rule) == REB_INTEGER) {
            REBCNT begin = index;
            while (IS_LEX_NUMBER(*str)) str++, index++;
            if (begin == index) index = NOT_FOUND;
        }
        break;
*/
    case REB_TAG:
    case REB_FILE:
//  case REB_ISSUE:
        // !! Can be optimized (w/o COPY)
        ser = Copy_Form_Value(rule, 0);
        index = Find_Str_Str(
            P_INPUT,
            0,
            index,
            SER_LEN(P_INPUT),
            1,
            ser,
            0,
            SER_LEN(ser),
            flags
        );
        Free_Series(ser);
        break;

    case REB_BLANK:
        break;

    // Parse a sub-rule block:
    case REB_BLOCK:
        {
        REBCNT pos_before = P_POS;
        REBOOL interrupted;

        P_POS = index; // modify input position

        if (Subparse_Throws(
            &interrupted,
            P_OUT,
            P_INPUT_VALUE, // use input value with modified position
            SPECIFIED,
            rule,
            P_RULE_SPECIFIER,
            P_FIND_FLAGS
        )) {
            assert(THROWN(P_OUT));
        }
        else {
            // !!! ignore "interrupted"? (e.g. ACCEPT or REJECT ran)
            if (IS_BLANK(P_OUT))
                index = NOT_FOUND;
            else {
                assert(IS_INTEGER(P_OUT));
                index = VAL_INT32(P_OUT);
            }
        }

        P_POS = pos_before; // restore input position
        }
        break;

    // Do an expression:
    case REB_GROUP:
        // might GC
        if (Do_At_Throws(
            &save, VAL_ARRAY(rule), VAL_INDEX(rule), P_RULE_SPECIFIER
        )) {
            *P_OUT = save;
            return THROWN_FLAG;
        }

        index = MIN(index, SER_LEN(P_INPUT)); // may affect tail
        break;

    default:
        fail (Error_Parse_Rule());
    }

    return index;
}


//
//  Parse_Next_Array: C
// 
// Used for parsing ANY-ARRAY! to match the next rule in the ruleset.
// If it matches, return the index just past it. Otherwise, return zero.
//
static REBCNT Parse_Next_Array(
    struct Reb_Frame *f,
    REBCNT index,
    const RELVAL *rule
) {
    // !!! THIS CODE NEEDS CLEANUP AND REWRITE BASED ON OTHER CHANGES
    REBARR *array = AS_ARRAY(P_INPUT);
    RELVAL *blk = ARR_AT(array, index);

    REBVAL save;

    if (Trace_Level) {
        Trace_Value("input", rule);
        if (IS_END(blk)) {
            const char *end_str = "** END **";
            Trace_String(cb_cast(end_str), strlen(end_str));
        }
        else
            Trace_Value("match", blk);
    }

    // !!! The previous code did not have a handling for this, but it fell
    // through to `no_result`.  Is that correct?
    //
    if (IS_END(blk)) goto no_result;

    switch (VAL_TYPE(rule)) {

    // Look for specific datattype:
    case REB_DATATYPE:
        index++;
        if (VAL_TYPE(blk) == VAL_TYPE_KIND(rule)) break;
        goto no_result;

    // Look for a set of datatypes:
    case REB_TYPESET:
        index++;
        if (TYPE_CHECK(rule, VAL_TYPE(blk))) break;
        goto no_result;

    // 'word
    case REB_LIT_WORD:
        index++;
        if (IS_WORD(blk) && (VAL_WORD_CANON(blk) == VAL_WORD_CANON(rule)))
            break;
        goto no_result;

    case REB_LIT_PATH:
        index++;
        if (IS_PATH(blk) && !Cmp_Array(blk, rule, FALSE)) break;
        goto no_result;

    case REB_BLANK:
        break;

    // Parse a sub-rule block:
    case REB_BLOCK:
        {
        REBCNT pos_before = P_POS;
        REBOOL interrupted;

        P_POS = index; // modify input position

        if (Subparse_Throws(
            &interrupted,
            P_OUT,
            P_INPUT_VALUE, // use input value with modified position
            SPECIFIED,
            rule,
            P_RULE_SPECIFIER,
            P_FIND_FLAGS
        )) {
            assert(THROWN(P_OUT));
        }
        else {
            // !!! ignore "interrupted"? (e.g. ACCEPT or REJECT ran)
            if (IS_BLANK(P_OUT))
                index = NOT_FOUND;
            else {
                assert(IS_INTEGER(P_OUT));
                index = VAL_INT32(P_OUT);
            }
        }

        P_POS = pos_before; // restore input position
        }
        break;

    // Do an expression:
    case REB_GROUP:
        // might GC
        if (Do_At_Throws(
            &save, VAL_ARRAY(rule), VAL_INDEX(rule), P_RULE_SPECIFIER
        )) {
            *P_OUT = save;
            return THROWN_FLAG;
        }
        // old: if (IS_ERROR(rule)) Throw_Error(VAL_CONTEXT(rule));
        index = MIN(index, ARR_LEN(array)); // may affect tail
        break;

    // Match with some other value:
    default:
        index++;
        if (Cmp_Value(blk, rule, P_HAS_CASE)) goto no_result;
    }

    return index;

no_result:
    return NOT_FOUND;
}


//
//  To_Thru: C
//
static REBCNT To_Thru(
    struct Reb_Frame *f,
    REBCNT index,
    const RELVAL *rule_block,
    REBOOL is_thru
) {
    RELVAL *blk;
    const RELVAL *rule;
    REBCNT cmd;
    REBCNT i;
    REBCNT len;

    REBVAL save;

    for (; index <= SER_LEN(P_INPUT); index++) {

        for (blk = VAL_ARRAY_HEAD(rule_block); NOT_END(blk); blk++) {

            rule = blk;

            // Deal with words and commands
            if (IS_BAR(rule)) {
                goto bad_target;
            }
            else if (IS_WORD(rule)) {
                if ((cmd = VAL_CMD(rule))) {
                    if (cmd == SYM_END) {
                        if (index >= SER_LEN(P_INPUT)) {
                            index = SER_LEN(P_INPUT);
                            goto found;
                        }
                        goto next;
                    }
                    else if (cmd == SYM_QUOTE) {
                        rule = ++blk; // next rule is the quoted value
                        if (IS_END(rule)) goto bad_target;
                        if (IS_GROUP(rule)) {
                            // might GC
                            if (Do_At_Throws(
                                &save,
                                VAL_ARRAY(rule),
                                VAL_INDEX(rule),
                                IS_SPECIFIC(rule)
                                    ? VAL_SPECIFIER(const_KNOWN(rule))
                                    : P_RULE_SPECIFIER
                            )) {
                                *P_OUT = save;
                                return THROWN_FLAG;
                            }
                            rule = &save;
                        }
                    }
                    else goto bad_target;
                }
                else {
                    // !!! Should mutability be enforced?  It might have to
                    // be if set/copy are used...
                    rule = GET_MUTABLE_VAR_MAY_FAIL(rule, P_RULE_SPECIFIER);
                }
            }
            else if (IS_PATH(rule)) {
                rule = Get_Parse_Value(&save, rule, P_RULE_SPECIFIER);
            }

            // Try to match it:
            if (P_TYPE >= REB_BLOCK) {
                if (ANY_ARRAY(rule)) goto bad_target;
                i = Parse_Next_Array(f, index, rule);
                if (THROWN(P_OUT))
                    return THROWN_FLAG;

                if (i != NOT_FOUND) {
                    if (!is_thru) i--;
                    index = i;
                    goto found;
                }
            }
            else if (P_TYPE == REB_BINARY) {
                REBYTE ch1 = *BIN_AT(P_INPUT, index);

                // Handle special string types:
                if (IS_CHAR(rule)) {
                    if (VAL_CHAR(rule) > 0xff) goto bad_target;
                    if (ch1 == VAL_CHAR(rule)) goto found1;
                }
                else if (IS_BINARY(rule)) {
                    if (ch1 == *VAL_BIN_AT(rule)) {
                        len = VAL_LEN_AT(rule);
                        if (len == 1) goto found1;
                        if (0 == Compare_Bytes(
                            BIN_AT(P_INPUT, index),
                            VAL_BIN_AT(rule),
                            len,
                            FALSE
                        )) {
                            if (is_thru) index += len;
                            goto found;
                        }
                    }
                }
                else if (IS_INTEGER(rule)) {
                    if (VAL_INT64(rule) > 0xff) goto bad_target;
                    if (ch1 == VAL_INT32(rule)) goto found1;
                }
                else goto bad_target;
            }
            else { // String
                REBCNT ch1 = GET_ANY_CHAR(P_INPUT, index);
                REBCNT ch2;

                if (!P_HAS_CASE) ch1 = UP_CASE(ch1);

                // Handle special string types:
                if (IS_CHAR(rule)) {
                    ch2 = VAL_CHAR(rule);
                    if (!P_HAS_CASE) ch2 = UP_CASE(ch2);
                    if (ch1 == ch2) goto found1;
                }
                // bitset
                else if (IS_BITSET(rule)) {
                    if (Check_Bit(VAL_SERIES(rule), ch1, NOT(P_HAS_CASE)))
                        goto found1;
                }
                else if (IS_TAG(rule)) {
                    ch2 = '<';
                    if (ch1 == ch2) {
                        //
                        // !!! This code was adapted from Parse_to, and is
                        // inefficient in the sense that it forms the tag
                        //
                        REBSER *ser = Copy_Form_Value(rule, 0);
                        REBCNT len = SER_LEN(ser);
                        i = Find_Str_Str(
                            P_INPUT,
                            0,
                            index,
                            SER_LEN(P_INPUT),
                            1,
                            ser,
                            0,
                            len,
                            AM_FIND_MATCH | P_FIND_FLAGS
                        );
                        Free_Series(ser);
                        if (i != NOT_FOUND) {
                            if (is_thru) i += len;
                            index = i;
                            goto found;
                        }
                    }
                }
                else if (ANY_STRING(rule)) {
                    ch2 = VAL_ANY_CHAR(rule);
                    if (!P_HAS_CASE) ch2 = UP_CASE(ch2);

                    if (ch1 == ch2) {
                        len = VAL_LEN_AT(rule);
                        if (len == 1) goto found1;

                        i = Find_Str_Str(
                            P_INPUT,
                            0,
                            index,
                            SER_LEN(P_INPUT),
                            1,
                            VAL_SERIES(rule),
                            VAL_INDEX(rule),
                            len,
                            AM_FIND_MATCH | P_FIND_FLAGS
                        );

                        if (i != NOT_FOUND) {
                            if (is_thru) i += len;
                            index = i;
                            goto found;
                        }
                    }
                }
                else if (IS_INTEGER(rule)) {
                    ch1 = GET_ANY_CHAR(P_INPUT, index);  // No casing!
                    if (ch1 == (REBCNT)VAL_INT32(rule)) goto found1;
                }
                else goto bad_target;
            }

        next:
            // Check for | (required if not end)
            blk++;
            if (IS_END(blk)) break;
            if (IS_GROUP(blk)) blk++;
            if (IS_END(blk)) break;
            if (!IS_BAR(blk)) {
                rule = blk;
                goto bad_target;
            }
        }
    }
    return NOT_FOUND;

found:
    if (NOT_END(blk + 1) && IS_GROUP(blk + 1)) {
        REBVAL evaluated;
        if (Do_At_Throws(
            &evaluated,
            VAL_ARRAY(blk + 1),
            VAL_INDEX(blk + 1),
            IS_SPECIFIC(rule_block)
                ? VAL_SPECIFIER(const_KNOWN(rule_block))
                : P_RULE_SPECIFIER
        )) {
            *P_OUT = evaluated;
            return THROWN_FLAG;
        }
        // !!! ignore evaluated if it's not THROWN?
    }
    return index;

found1:
    if (NOT_END(blk + 1) && IS_GROUP(blk + 1)) {
        REBVAL evaluated;
        if (Do_At_Throws(
            &evaluated,
            VAL_ARRAY(blk + 1),
            VAL_INDEX(blk + 1),
            IS_SPECIFIC(rule_block)
                ? VAL_SPECIFIER(const_KNOWN(rule_block))
                : P_RULE_SPECIFIER
        )) {
            *P_OUT = save;
            return THROWN_FLAG;
        }
        // !!! ignore evaluated if it's not THROWN?
    }
    return index + (is_thru ? 1 : 0);

bad_target:
    fail (Error_Parse_Rule());
}


//
//  Parse_To: C
// 
// Parse TO a specific:
//     1. integer - index position
//     2. END - end of input
//     3. value - according to datatype
//     4. block of values - the first one we hit
//
static REBCNT Parse_To(
    struct Reb_Frame *f,
    REBCNT index,
    const RELVAL *rule,
    REBOOL is_thru
) {
    REBCNT i;
    REBSER *ser;

    if (IS_INTEGER(rule)) {
        //
        // TO a specific index position.
        //
        // !!! This allows jumping backward to an index before the parse
        // position, while TO generally only goes forward otherwise.  Should
        // this be done by another operation?  (Like SEEK?)
        //
        // !!! Negative numbers get cast to large integers, needs error!
        // But also, should there be an option for relative addressing?
        //
        i = cast(REBCNT, Int32(const_KNOWN(rule))) - (is_thru ? 0 : 1);
        if (i > SER_LEN(P_INPUT))
            i = SER_LEN(P_INPUT);
    }
    else if (IS_WORD(rule) && VAL_WORD_SYM(rule) == SYM_END) {
        i = SER_LEN(P_INPUT);
    }
    else if (IS_BLOCK(rule)) {
        i = To_Thru(f, index, rule, is_thru);
    }
    else {
        if (Is_Array_Series(P_INPUT)) {
            REBVAL word; /// !!!Temp, but where can we put it?

            if (IS_LIT_WORD(rule)) {  // patch to search for word, not lit.
                COPY_VALUE(&word, rule, P_RULE_SPECIFIER);

                // Only set type--don't reset the header, because that could
                // make the word binding inconsistent with the bits.
                //
                VAL_SET_TYPE_BITS(&word, REB_WORD);
                rule = &word;
            }

            i = Find_In_Array(
                AS_ARRAY(P_INPUT),
                index,
                SER_LEN(P_INPUT),
                rule,
                1,
                P_HAS_CASE ? AM_FIND_CASE : 0,
                1
            );

            if (i != NOT_FOUND && is_thru) i++;
        }
        else {
            // "str"
            if (ANY_BINSTR(rule)) {
                if (!IS_STRING(rule) && !IS_BINARY(rule)) {
                    // !!! Can this be optimized not to use COPY?
                    ser = Copy_Form_Value(rule, 0);
                    i = Find_Str_Str(
                        P_INPUT,
                        0,
                        index,
                        SER_LEN(P_INPUT),
                        1,
                        ser,
                        0,
                        SER_LEN(ser),
                        (P_FIND_FLAGS & AM_FIND_CASE)
                            ? AM_FIND_CASE
                            : 0
                    );
                    if (i != NOT_FOUND && is_thru) i += SER_LEN(ser);
                    Free_Series(ser);
                }
                else {
                    i = Find_Str_Str(
                        P_INPUT,
                        0,
                        index,
                        SER_LEN(P_INPUT),
                        1,
                        VAL_SERIES(rule),
                        VAL_INDEX(rule),
                        VAL_LEN_AT(rule),
                        (P_FIND_FLAGS & AM_FIND_CASE)
                            ? AM_FIND_CASE
                            : 0
                    );
                    if (i != NOT_FOUND && is_thru) i += VAL_LEN_AT(rule);
                }
            }
            else if (IS_CHAR(rule)) {
                i = Find_Str_Char(
                    VAL_CHAR(rule),
                    P_INPUT,
                    0,
                    index,
                    SER_LEN(P_INPUT),
                    1,
                    (P_FIND_FLAGS & AM_FIND_CASE)
                        ? AM_FIND_CASE
                        : 0
                );
                if (i != NOT_FOUND && is_thru) i++;
            }
            else if (IS_BITSET(rule)) {
                i = Find_Str_Bitset(
                    P_INPUT,
                    0,
                    index,
                    SER_LEN(P_INPUT),
                    1,
                    VAL_BITSET(rule),
                    (P_FIND_FLAGS & AM_FIND_CASE)
                        ? AM_FIND_CASE
                        : 0
                );
                if (i != NOT_FOUND && is_thru) i++;
            }
            else
                fail (Error_Parse_Rule());
        }
    }

    return i;
}


//
//  Do_Eval_Rule: C
// 
// Evaluate the input as a code block. Advance input if
// rule succeeds. Return new index or failure.
// 
// Examples:
//     do skip
//     do end
//     do "abc"
//     do 'abc
//     do [...]
//     do variable
//     do datatype!
//     do quote 123
//     do into [...]
// 
// Problem: cannot write:  set var do datatype!
//
static REBCNT Do_Eval_Rule(struct Reb_Frame *f)
{
    const RELVAL *rule = P_RULE;
    REBCNT n;
    struct Reb_Frame newparse;

    REBIXO indexor = P_POS;

    REBVAL save; // REVIEW: Could this just reuse value?

    // First, check for end of input
    //
    if (P_POS >= SER_LEN(P_INPUT)) {
        if (IS_WORD(rule) && VAL_CMD(rule) == SYM_END)
            return P_POS;

        return NOT_FOUND;
    }

    // Evaluate next expression, stop processing if BREAK/RETURN/QUIT/THROW...
    //
    REBVAL value;
    indexor = DO_NEXT_MAY_THROW(
        &value, AS_ARRAY(P_INPUT), indexor, P_INPUT_SPECIFIER
    );
    if (THROWN(&value)) {
        *P_OUT = value;
        return THROWN_FLAG;
    }

    // Get variable or command:
    if (IS_WORD(rule)) {

        n = VAL_CMD(rule);

        if (n == SYM_SKIP)
            return IS_VOID(&value) ? NOT_FOUND : P_POS;

        if (n == SYM_QUOTE) {
            /* rule = rule + 1; */ // was this.
            assert(rule + 1 == P_RULE);
            rule = P_RULE;

            FETCH_NEXT_RULE_MAYBE_END(f);
            if (IS_END(P_RULE))
                fail (Error_Parse_End());

            if (IS_GROUP(rule)) {
                // might GC
                if (Do_At_Throws(
                    &save, VAL_ARRAY(rule), VAL_INDEX(rule), P_RULE_SPECIFIER
                )) {
                    *P_OUT = save;
                    return THROWN_FLAG;
                }
                rule = &save;
            }
        }
        else if (n == SYM_INTO) {
            REBOOL interrupted;

            /* rule = rule + 1; */ // was this.
            assert(rule + 1 == P_RULE);
            rule = P_RULE;

            FETCH_NEXT_RULE_MAYBE_END(f);
            if (IS_END(P_RULE))
                fail (Error_Parse_End());

            rule = Get_Parse_Value(&save, rule, P_RULE_SPECIFIER); // sub-rules

            if (!IS_BLOCK(rule))
                fail (Error_Parse_Rule());

            if (!ANY_BINSTR(&value) && !ANY_ARRAY(&value))
                return NOT_FOUND;

            if (Subparse_Throws(
                &interrupted,
                P_OUT,
                &value, // input value (source of P_INPUT, P_POS)
                SPECIFIED,
                rule,
                P_RULE_SPECIFIER,
                P_FIND_FLAGS
            )) {
                return THROWN_FLAG;
            }

            // !!! ignore interrupted?  (e.g. ACCEPT or REJECT ran)

            if (IS_BLANK(P_OUT)) return NOT_FOUND;
            assert(IS_INTEGER(P_OUT));

            if (VAL_UNT32(P_OUT) == VAL_LEN_HEAD(&value)) return P_POS;

            return NOT_FOUND;
        }
        else if (n > 0)
            fail (Error_Parse_Rule());
        else
            rule = Get_Parse_Value(&save, rule, P_RULE_SPECIFIER); // variable
    }
    else if (IS_PATH(rule)) {
        rule = Get_Parse_Value(&save, rule, P_RULE_SPECIFIER); // variable
    }
    else if (
        IS_SET_WORD(rule)
        || IS_GET_WORD(rule)
        || IS_SET_PATH(rule)
        || IS_GET_PATH(rule)
    ) {
        fail (Error_Parse_Rule());
    }

    if (IS_BLANK(rule))
        return (VAL_TYPE(&value) > REB_BLANK) ? NOT_FOUND : P_POS;

    // !!! This copies a single value into a block to use as data.  Is there
    // any way this might be avoided?
    //
    newparse.stackvars = Push_Ended_Trash_Chunk(3);
    Val_Init_Block_Index(
        &newparse.stackvars[0],
        Make_Array(1), // !!! "copy the value into its own block"
        0 // position 0
    ); // series (now a REB_BLOCK)

    Append_Value(AS_ARRAY(VAL_SERIES(&newparse.stackvars[0])), &value);
    SET_INTEGER(&newparse.stackvars[1], P_FIND_FLAGS); // find_flags
    newparse.arg = newparse.stackvars;
    newparse.out = P_OUT;

    newparse.source.array = f->source.array;
    newparse.index = f->index;
    newparse.value = rule;
    newparse.specifier = P_RULE_SPECIFIER;

    {
    PUSH_GUARD_SERIES(VAL_SERIES(&newparse.stackvars[0]));
    n = Parse_Next_Array(&newparse, P_POS, rule);
    DROP_GUARD_SERIES(VAL_SERIES(&newparse.stackvars[0]));
    }

    if (n == THROWN_FLAG)
        return THROWN_FLAG;

    if (n == NOT_FOUND)
        return NOT_FOUND;

    return P_POS;
}


//
//  subparse: native [
//
//  {Internal support function for PARSE (acts as variadic to consume rules)}
//
//      input [any-series!]
//      find-flags [integer!]
//  ]
//
REBNATIVE(subparse)
//
// Subparse is a function which is "shaped like" a native, so that it can run
// its parse logic with the necessary parsing state stored in an ordinary
// call frame.  This allows each recursion in the parse to appear as a stack
// level in the backtrace, reflected through the ordinary debugging API.
//
// Although it is shaped similarly to typical DO code, there are differences.
// The subparse advances the "current evaluation position" in the frame as
// it operates (a bit like a variadic function).  The execution point is in an
// array of parse dialect instructions, not DO functions.  Hence invoking it
// using DO will lead to unusual behavior, such as:
//
//     >> subparse "aaaa" 0 some "a"
//     == 4
//
// This is because when a filled frame is used to call the function, it then
// assumes the frame's `where` position is the place that rules should come
// from.  Here that means picking up the `some "a"` after the arguments are
// gathered, and returning the position where the match successfully ended.
// A special calling wrapper Subparse_Throws is used to fill a frame from
// arguments separate from the rule list.
//
// Rules are matched until one of these things happens:
//
// * A rule fails, and is not then picked up by a later "optional" rule.
// This returns R_OUT with the value in out as NONE!.
//
// * You run out of rules to apply without any failures or errors, and the
// position in the input series is returned.  This may be at the end of
// the input data or not--it's up to the caller to decide if that's relevant.
// This will return R_OUT with out containing an integer index.
//
// !!! The return of an integer index is based on the R3-Alpha convention,
// but needs to be rethought in light of the ability to switch series.  It
// does not seem that all callers of Subparse were prepared for
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
    struct Reb_Frame *f = frame_; // nice alias of implicit native parameter

#if !defined(NDEBUG)
    //
    // These parse state variables live in chunk-stack REBVARs, which can be
    // annoying to find to inspect in the debugger.  This makes pointers into
    // the value payloads so they can be seen more easily.
    //
    const REBCNT *pos_debug = &P_POS;

    REBUPT do_count = TG_Do_Count; // helpful to cache for visibility also
#endif

    const RELVAL *set_or_copy_word; // active word target of COPY or SET

    REBCNT start;       // recovery restart point
    REBCNT i;           // temp index point
    REBCNT begin;       // point at beginning of match
    REBINT count;       // iterated pattern counter
    REBINT mincount;    // min pattern count
    REBINT maxcount;    // max pattern count
    REBFLGS flags;
    REBCNT cmd;

    REBVAL save;

    if (C_STACK_OVERFLOWING(&flags)) Trap_Stack_Overflow();

    flags = 0;
    set_or_copy_word = NULL;
    mincount = maxcount = 1;
    start = begin = P_POS;

    while (NOT_END(P_RULE)) {
        //
        // This loop iterates across each REBVAL's worth of "rule" in the rule
        // block.  Some of these rules set flags and `continue`, so that the
        // flags will apply to the next rule item.
        //
        // !!! This flagging process--established by R3-Alpha--is efficient
        // but somewhat haphazard.  It may work for `while ["a" | "b"]` to
        // "set the PF_WHILE" flag when it sees the `while` and then iterate
        // a rule it would have otherwise processed just once.  But there are
        // a lot of edge cases like `while |` where this method isn't set up
        // to notice a "grammar error".  It could use review.

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
        ++TG_Do_Count;
        do_count = TG_Do_Count;
        cast(void, do_count); // suppress compiler warning about lack of use
    #endif

    //==////////////////////////////////////////////////////////////////==//
    //
    // GARBAGE COLLECTION AND EVENT HANDLING
    //
    //==////////////////////////////////////////////////////////////////==//

        if (--Eval_Count <= 0 || Eval_Signals) {
            //
            // !!! See notes on other invocations about the questions raised by
            // calls to Do_Signals_Throws() by places that do not have a clear
            // path up to return results from an interactive breakpoint.
            //
            REBVAL result;

            if (Do_Signals_Throws(&result))
                fail (Error_No_Catch_For_Throw(&result));

            if (IS_ANY_VALUE(&result))
                fail (Error(RE_MISC));
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
            SET_INTEGER(P_OUT, P_POS);
            return R_OUT;
        }

        // If word, set-word, or get-word, process it:
        if (VAL_TYPE(P_RULE) >= REB_WORD && VAL_TYPE(P_RULE) <= REB_GET_WORD) {
            // Is it a command word?
            if ((cmd = VAL_CMD(P_RULE))) {

                if (!IS_WORD(P_RULE))
                    fail (Error(RE_PARSE_COMMAND, P_RULE)); // no FOO: or :FOO

                if (cmd <= SYM_BREAK) { // optimization

                    switch (cmd) {
                    // Note: mincount = maxcount = 1 on entry
                    case SYM_WHILE:
                        flags |= PF_WHILE;
                    case SYM_ANY:
                        mincount = 0;
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
                    set_or_copy_pre_rule:
                        FETCH_NEXT_RULE_MAYBE_END(f);

                        if (!(IS_WORD(P_RULE) || IS_SET_WORD(P_RULE)))
                            fail (Error(RE_PARSE_VARIABLE, P_RULE));

                        if (VAL_CMD(P_RULE))
                            fail (Error(RE_PARSE_COMMAND, P_RULE));

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
                            REBVAL evaluated;
                            if (Do_At_Throws(
                                &evaluated,
                                VAL_ARRAY(P_RULE),
                                VAL_INDEX(P_RULE),
                                P_RULE_SPECIFIER
                            )) {
                                // If the group evaluation result gives a
                                // THROW, BREAK, CONTINUE, etc then we'll
                                // return that
                                *P_OUT = evaluated;
                                return R_OUT_IS_THROWN;
                            }

                            *P_OUT = *NAT_VALUE(parse);
                            CONVERT_NAME_TO_THROWN(P_OUT, &evaluated);
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
                        REBVAL thrown_arg;
                        SET_INTEGER(&thrown_arg, P_POS);
                        *P_OUT = *NAT_VALUE(parse_accept);
                        CONVERT_NAME_TO_THROWN(P_OUT, &thrown_arg);
                        return R_OUT_IS_THROWN;
                    }

                    case SYM_REJECT: {
                        //
                        // Similarly, this is a break/continue style "throw"
                        //
                        *P_OUT = *NAT_VALUE(parse_reject);
                        CONVERT_NAME_TO_THROWN(P_OUT, BLANK_VALUE);
                        return R_OUT;
                    }

                    case SYM_FAIL:
                        P_POS = NOT_FOUND;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        goto post_match_processing;

                    case SYM_IF: {
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        if (IS_END(P_RULE))
                            fail (Error_Parse_End());

                        if (!IS_GROUP(P_RULE))
                            fail (Error_Parse_Rule());

                        // might GC
                        REBVAL condition;
                        if (Do_At_Throws(
                            &condition,
                            VAL_ARRAY(P_RULE),
                            VAL_INDEX(P_RULE),
                            P_RULE_SPECIFIER
                        )) {
                            *P_OUT = save;
                            return R_OUT_IS_THROWN;
                        }

                        FETCH_NEXT_RULE_MAYBE_END(f);

                        if (IS_CONDITIONAL_TRUE(&condition))
                            continue;

                        P_POS = NOT_FOUND;
                        goto post_match_processing;
                    }

                    case SYM_LIMIT:
                        fail (Error(RE_NOT_DONE));

                    case SYM__Q_Q:
                        Print_Parse_Index(f);
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        continue;
                    }
                }
                // Any other cmd must be a match command, so proceed...
                rule = P_RULE;
            }
            else {
                // It's not a PARSE command, get or set it

                // word: - set a variable to the series at current index
                if (IS_SET_WORD(P_RULE)) {
                    *GET_MUTABLE_VAR_MAY_FAIL(P_RULE, P_RULE_SPECIFIER)
                        = *P_INPUT_VALUE;
                    FETCH_NEXT_RULE_MAYBE_END(f);
                    continue;
                }

                // :word - change the index for the series to a new position
                if (IS_GET_WORD(P_RULE)) {
                    const REBVAL *var = GET_OPT_VAR_MAY_FAIL(
                        P_RULE,
                        P_RULE_SPECIFIER
                    );
                    if (!ANY_SERIES(var)) // #1263
                        fail (Error(RE_PARSE_SERIES, P_RULE));
                    Set_Parse_Series(f, var);
                    FETCH_NEXT_RULE_MAYBE_END(f);
                    continue;
                }

                // word - some other variable
                if (IS_WORD(P_RULE)) {
                    rule = GET_OPT_VAR_MAY_FAIL(P_RULE, P_RULE_SPECIFIER);
                }
                else {
                    // rule can still be 'word or /word
                    rule = P_RULE;
                }
            }
        }
        else if (ANY_PATH(P_RULE)) {
            if (IS_PATH(P_RULE)) {
                if (Do_Path_Throws_Core(
                    &save, NULL, P_RULE, P_RULE_SPECIFIER, NULL
                )) {
                    fail (Error_No_Catch_For_Throw(&save));
                }
                rule = &save;
            }
            else if (IS_SET_PATH(P_RULE)) {
                REBVAL tmp;
                Val_Init_Series(&tmp, P_TYPE, P_INPUT);
                VAL_INDEX(&tmp) = P_POS;
                if (Do_Path_Throws_Core(
                    &save, NULL, P_RULE, P_RULE_SPECIFIER, &tmp
                )) {
                    fail (Error_No_Catch_For_Throw(&save));
                }
                rule = &save;

                // !!! code used to say `if (!rule) continue;` "for SET and
                // GET cases", but here rule isn't set to NULL...so it falls
                // through and does not continue.  Investigate.
            }
            else {
                assert(IS_GET_PATH(P_RULE));

                if (Do_Path_Throws_Core(
                    &save, NULL, P_RULE, P_RULE_SPECIFIER, NULL
                )) {
                    fail (Error_No_Catch_For_Throw(&save));
                }

                // !!! This allows the series to be changed, as per #1263,
                // but note the positions being returned and checked aren't
                // prepared for this, they only exchange numbers ATM (!!!)
                //
                if (!ANY_SERIES(&save))
                    fail (Error(RE_PARSE_SERIES, &save));

                Set_Parse_Series(f, &save);
                FETCH_NEXT_RULE_MAYBE_END(f);
                continue;
            }

            if (P_POS > SER_LEN(P_INPUT))
                P_POS = SER_LEN(P_INPUT);
        }
        else {
            rule = P_RULE;
        }

        // All cases should have either set `rule` by this point or continued
        //
        assert(rule != NULL);

        if (IS_GROUP(rule)) {
            REBVAL evaluated;
            if (Do_At_Throws( // might GC
                &evaluated, VAL_ARRAY(rule), VAL_INDEX(rule), P_RULE_SPECIFIER
            )) {
                *P_OUT = evaluated;
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
            if (IS_END(P_RULE))
                fail (Error_Parse_End());

            rule = Get_Parse_Value(&save, P_RULE, P_RULE_SPECIFIER);

            if (IS_INTEGER(rule)) {
                maxcount = Int32s(const_KNOWN(rule), 0);

                FETCH_NEXT_RULE_MAYBE_END(f);
                if (IS_END(P_RULE))
                    fail (Error_Parse_End());

                rule = Get_Parse_Value(&save, P_RULE, P_RULE_SPECIFIER);
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

        FETCH_NEXT_RULE_MAYBE_END(f); // pushed down?

        if (VAL_TYPE(rule) <= REB_0 || VAL_TYPE(rule) >= REB_FUNCTION)
            fail (Error_Parse_Rule());

        begin = P_POS;       // input at beginning of match section

        //note: rules var already advanced

        for (count = 0; count < maxcount;) {

            if (IS_BAR(rule)) {
                fail (Error_Parse_Rule()); // !!! Is this possible?
            }
            if (IS_WORD(rule)) {

                switch (cmd = VAL_WORD_SYM(rule)) {

                case SYM_SKIP:
                    i = (P_POS < SER_LEN(P_INPUT))
                        ? P_POS + 1
                        : NOT_FOUND;
                    break;

                case SYM_END:
                    i = (P_POS < SER_LEN(P_INPUT))
                        ? NOT_FOUND
                        : SER_LEN(P_INPUT);
                    break;

                case SYM_TO:
                case SYM_THRU:
                    if (IS_END(P_RULE))
                        fail (Error_Parse_End());

                    if (!subrule) { // capture only on iteration #1
                        subrule = Get_Parse_Value(
                            &save, P_RULE, P_RULE_SPECIFIER
                        );
                        FETCH_NEXT_RULE_MAYBE_END(f);
                    }

                    i = Parse_To(f, P_POS, subrule, LOGICAL(cmd == SYM_THRU));
                    break;

                case SYM_QUOTE: {
                    const RELVAL *quoted;

                    //
                    // !!! Disallow QUOTE on string series, see #2253
                    //
                    if (!Is_Array_Series(P_INPUT))
                        fail (Error_Parse_Rule());

                    if (IS_END(P_RULE))
                        fail (Error_Parse_End());

                    if (!subrule) { // capture only on iteration #1
                        subrule = P_RULE;
                        FETCH_NEXT_RULE_MAYBE_END(f);
                    }

                    if (IS_GROUP(subrule)) {
                        // might GC
                        if (Do_At_Throws(
                            &save,
                            VAL_ARRAY(subrule),
                            VAL_INDEX(subrule),
                            P_RULE_SPECIFIER
                        )) {
                            *P_OUT = save;
                            return R_OUT_IS_THROWN;
                        }
                        quoted = &save;
                    }
                    else quoted = subrule;

                    if (0 == Cmp_Value(
                        ARR_AT(AS_ARRAY(P_INPUT), P_POS),
                        quoted,
                        P_HAS_CASE
                    )) {
                        i = P_POS + 1;
                    }
                    else {
                        i = NOT_FOUND;
                    }
                    break;
                }

                case SYM_INTO: {
                    RELVAL *val;
                    REBOOL interrupted;

                    if (IS_END(P_RULE))
                        fail (Error_Parse_End());

                    if (!subrule) {
                        subrule = Get_Parse_Value(
                            &save, P_RULE, P_RULE_SPECIFIER
                        );
                        FETCH_NEXT_RULE_MAYBE_END(f);
                    }

                    if (!IS_BLOCK(subrule))
                        fail (Error_Parse_Rule());

                    val = ARR_AT(AS_ARRAY(P_INPUT), P_POS);

                    if (IS_END(val) || (!ANY_BINSTR(val) && !ANY_ARRAY(val))) {
                        i = NOT_FOUND;
                        break;
                    }

                    if (Subparse_Throws(
                        &interrupted,
                        P_OUT,
                        val,
                        P_INPUT_SPECIFIER, // val was taken from P_INPUT
                        subrule,
                        P_RULE_SPECIFIER,
                        P_FIND_FLAGS
                    )) {
                        return R_OUT_IS_THROWN;
                    }

                    // !!! ignore interrupted? (e.g. ACCEPT or REJECT ran)

                    if (IS_BLANK(P_OUT)) {
                        i = NOT_FOUND;
                        break;
                    }

                    assert(IS_INTEGER(P_OUT));

                    if (VAL_UNT32(P_OUT) != VAL_LEN_HEAD(val)) {
                        i = NOT_FOUND;
                        break;
                    }

                    i = P_POS + 1;
                    break;
                }

                case SYM_DO: {
                    if (!Is_Array_Series(P_INPUT))
                        fail (Error_Parse_Rule());

                    if (subrule != NULL) {
                        //
                        // Not currently set up for iterating DO rules
                        // since the Do_Eval_Rule routine expects to be
                        // able to arbitrarily update P_NEXT_RULE
                        //
                        fail (Error(RE_MISC));
                    }

                    subrule = BLANK_VALUE; // cause an error if iterating

                    {
                    REBCNT pos_before = P_POS;

                    i = Do_Eval_Rule(f); // changes P_RULE (should)

                    P_POS = pos_before; // !!! Simulate restore (needed?)
                    }

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
                    P_OUT,
                    P_INPUT_VALUE,
                    SPECIFIED,
                    rule,
                    P_RULE_SPECIFIER,
                    P_FIND_FLAGS
                )) {
                    return R_OUT_IS_THROWN;
                }

                // Non-breaking out of loop instances of match or not.

                if (IS_BLANK(P_OUT))
                    i = NOT_FOUND;
                else {
                    assert(IS_INTEGER(P_OUT));
                    i = VAL_INT32(P_OUT);
                }

                SET_TRASH_SAFE(P_OUT);

                if (interrupted) { // ACCEPT or REJECT ran
                    P_POS = i;
                    break;
                }
            }
            else {
                // Parse according to datatype
                REBCNT pos_before = P_POS;

                if (Is_Array_Series(P_INPUT))
                    i = Parse_Next_Array(f, P_POS, rule);
                else
                    i = Parse_Next_String(f, P_POS, rule);

                assert(P_POS == pos_before);
                /*P_POS = pos_before; // !!! Simulate restoration (needed?)*/

                // i may be THROWN_FLAG
            }

            if (i == THROWN_FLAG) return THROWN_FLAG;

            // Necessary for special cases like: some [to end]
            // i: indicates new index or failure of the match, but
            // that does not mean failure of the rule, because optional
            // matches can still succeed, if if the last match failed.
            //
            if (i != NOT_FOUND) {
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
                else if (i != NOT_FOUND) {
                    P_POS = i;
                }
                else {
                    // just keep index as is.
                }
                break;
            }
            P_POS = i;

            // A BREAK word stopped us:
            //if (P_OUT) {P_OUT = 0; break;}
        }

        if (P_POS > SER_LEN(P_INPUT)) P_POS = NOT_FOUND;

    //==////////////////////////////////////////////////////////////////==//
    //
    // "POST-MATCH PROCESSING"
    //
    //==////////////////////////////////////////////////////////////////==//

    post_match_processing:
        // Process special flags:
        if (flags) {
            // NOT before all others:
            if (flags & PF_NOT) {
                if ((flags & PF_NOT2) && P_POS != NOT_FOUND)
                    P_POS = NOT_FOUND;
                else P_POS = begin;
            }
            if (P_POS == NOT_FOUND) { // Failure actions:
                // !!! if word isn't NULL should we set its var to NONE! ...?
                if (flags & PF_THEN) {
                    FETCH_TO_BAR_MAYBE_END(f);
                    if (NOT_END(P_RULE))
                        FETCH_NEXT_RULE_MAYBE_END(f);
                }
            }
            else {
                //
                // Success actions.  Set count to how much input was advanced
                //
                count = (begin > P_POS) ? 0 : P_POS - begin;

                if (flags & PF_COPY) {
                    REBVAL temp;
                    Val_Init_Series(
                        &temp,
                        P_TYPE,
                        Is_Array_Series(P_INPUT)
                            ? ARR_SERIES(Copy_Array_At_Max_Shallow(
                                AS_ARRAY(P_INPUT),
                                begin,
                                P_INPUT_SPECIFIER,
                                count
                            ))
                            : Copy_String_Slimming(P_INPUT, begin, count)
                    );

                    *GET_MUTABLE_VAR_MAY_FAIL(
                        set_or_copy_word, P_RULE_SPECIFIER
                    ) = temp;
                }
                else if (flags & PF_SET) {
                    REBVAL *var = GET_MUTABLE_VAR_MAY_FAIL(
                        set_or_copy_word, P_RULE_SPECIFIER
                    );

                    if (Is_Array_Series(P_INPUT)) {
                        if (count == 0)
                            SET_BLANK(var);
                        else {
                            COPY_VALUE(
                                var,
                                ARR_AT(AS_ARRAY(P_INPUT), begin),
                                P_INPUT_SPECIFIER
                            );
                        }
                    }
                    else {
                        if (count == 0) SET_BLANK(var);
                        else {
                            i = GET_ANY_CHAR(P_INPUT, begin);
                            if (P_TYPE == REB_BINARY) {
                                SET_INTEGER(var, i);
                            } else {
                                SET_CHAR(var, i);
                            }
                        }
                    }
                }

                if (flags & PF_RETURN) {
                    // See notes on PARSE's return in handling of SYM_RETURN

                    REBVAL captured;
                    Val_Init_Series(
                        &captured,
                        P_TYPE,
                        Is_Array_Series(P_INPUT)
                            ? ARR_SERIES(Copy_Array_At_Max_Shallow(
                                AS_ARRAY(P_INPUT),
                                begin,
                                P_INPUT_SPECIFIER,
                                count
                            ))
                            : Copy_String_Slimming(P_INPUT, begin, count)
                    );

                    *P_OUT = *NAT_VALUE(parse);
                    CONVERT_NAME_TO_THROWN(P_OUT, &captured);
                    return R_OUT_IS_THROWN;
                }

                if (flags & PF_REMOVE) {
                    if (count) Remove_Series(P_INPUT, begin, count);
                    P_POS = begin;
                }

                if (flags & (PF_INSERT | PF_CHANGE)) {
                    count = (flags & PF_INSERT) ? 0 : count;
                    cmd = (flags & PF_INSERT) ? 0 : (1<<AN_PART);

                    if (IS_END(P_RULE))
                        fail (Error_Parse_End());

                    // Check for ONLY flag:
                    if (IS_WORD(P_RULE) && (cmd = VAL_CMD(P_RULE))) {
                        if (cmd != SYM_ONLY)
                            fail (Error_Parse_Rule());

                        cmd |= (1<<AN_ONLY);
                        FETCH_NEXT_RULE_MAYBE_END(f);
                        if (IS_END(P_RULE))
                            fail (Error_Parse_End());
                    }
                    // new value...comment said "CHECK FOR QUOTE!!"
                    rule = Get_Parse_Value(&save, P_RULE, P_RULE_SPECIFIER);
                    FETCH_NEXT_RULE_MAYBE_END(f);

                    if (Is_Array_Series(P_INPUT)) {
                        REBVAL specified;
                        COPY_VALUE(&specified, rule, P_RULE_SPECIFIER);

                        P_POS = Modify_Array(
                            (flags & PF_CHANGE) ? SYM_CHANGE : SYM_INSERT,
                            AS_ARRAY(P_INPUT),
                            begin,
                            &specified,
                            cmd,
                            count,
                            1
                        );

                        if (IS_LIT_WORD(rule))
                            //
                            // Only set the type, not the whole header (in
                            // order to keep binding information)
                            //
                            VAL_SET_TYPE_BITS(
                                ARR_AT(AS_ARRAY(P_INPUT), P_POS - 1),
                                REB_WORD
                            );
                    }
                    else {
                        REBVAL specified;
                        COPY_VALUE(&specified, rule, P_RULE_SPECIFIER);

                        if (P_TYPE == REB_BINARY)
                            cmd |= (1 << AN_SERIES); // special flag

                        P_POS = Modify_String(
                            (flags & PF_CHANGE) ? SYM_CHANGE : SYM_INSERT,
                            P_INPUT,
                            begin,
                            &specified,
                            cmd,
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
            if (IS_END(P_RULE)) { // no alternate rule
                SET_BLANK(P_OUT);
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

    SET_INTEGER(P_OUT, P_POS); // !!! return switched input series??
    return R_OUT;
}


//
// Shared implementation routine for PARSE? and PARSE.  The difference is that
// PARSE? only returns whether or not a set of rules completed to the end.
// PARSE is more general purpose in terms of the result it provides, and
// it defaults to returning the input.
//
static REB_R Parse_Core(struct Reb_Frame *frame_, REBOOL logic)
{
    PARAM(1, input);
    PARAM(2, rules);
    REFINE(3, case);
    REFINE(4, all);

    REBVAL *rules = ARG(rules);
    REBCNT index;
    REBOOL interrupted;

    if (IS_BLANK(rules) || IS_STRING(rules)) {
        //
        // !!! R3-Alpha supported "simple parse", which was cued by the rules
        // being either NONE! or a STRING!.  Though this functionality does
        // not exist in Ren-C, it's more informative to give an error telling
        // where to look for the functionality than a generic "parse doesn't
        // take that type" error.
        //
        fail (Error(RE_USE_SPLIT_SIMPLE));
    }

    // The native dispatcher should have pre-filled the output slot with a
    // trash value in the debug build.  We double-check the expectation of
    // whether the parse loop overwites this slot with a result or not.
    //
    assert(IS_END(D_OUT));

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

            // In the logic case, we are only concerned with matching.  If
            // a construct that can return arbitrary values is used, then
            // failure is triggered with a specific error, saying PARSE must
            // be used instead of PARSE?.
            //
            // !!! Review if this is the best semantics for a parsing variant
            // that is committed to only returning logic TRUE or FALSE, in
            // spite of existence of rules that allow the general PARSE to
            // do otherwise.
            //
            if (logic && !IS_LOGIC(D_OUT))
                fail (Error(RE_PARSE_NON_LOGIC, D_OUT));

            return R_OUT;
        }

        // All other throws should just bubble up uncaught.
        //
        return R_OUT_IS_THROWN;
    }

    // Parse can fail if the match rule state can't process pending input.
    //
    if (IS_BLANK(D_OUT))
        return logic ? R_FALSE : R_BLANK;

    assert(IS_INTEGER(D_OUT));

    // If the match rules all completed, but the parse position didn't end
    // at (or beyond) the tail of the input series, the parse also failed.
    //
    if (VAL_UNT32(D_OUT) < VAL_LEN_HEAD(ARG(input)))
        return logic ? R_FALSE : R_BLANK;

    // The end was reached...if doing a logic-based PARSE? then return TRUE.
    //
    if (logic) return R_TRUE;

    // Otherwise it's PARSE so return the input (a series, hence conditionally
    // true, yet more informative for chaining.)  See #2165.
    //
    *D_OUT = *ARG(input);
    return R_OUT;
}


//
//  parse?: native [
//
//  ; NOTE: If changing this, also update PARSE
//
//  "Determines if a series matches the given grammar rules or not."
//
//      input [any-series!]
//          "Input series to parse"
//      rules [block! string! blank!]
//          "Rules to parse by (STRING! and BLANK!/none! are deprecated)"
//      /case
//          "Uses case-sensitive comparison"
//      /all
//          "(ignored refinement left for Rebol2 transitioning)"
//  ]
//
REBNATIVE(parse_q)
{
    return Parse_Core(frame_, TRUE);
}


//
//  parse: native [
//
//  ; NOTE: If changing this, also update PARSE?
//
//  "Parses a series according to grammar rules and returns a result."
//
//      input [any-series!]
//          "Input series to parse (default result for successful match)"
//      rules [block! string! blank!]
//          "Rules to parse by (STRING! and NONE! are deprecated)"
//      /case
//          "Uses case-sensitive comparison"
//      /all
//          "(ignored refinement left for Rebol2 transitioning)"
//  ]
//
REBNATIVE(parse)
{
    return Parse_Core(frame_, FALSE);
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
    fail (Error(RE_MISC));
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
    fail (Error(RE_MISC));
}
