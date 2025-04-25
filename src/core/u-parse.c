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
// the Any_Series! of the current parse position sitting in slot 0 into
// a DECIMAL! and crash the parse, for instance.  They are able to change
// usermode authored function arguments only.)
//

#define P_RULE              (L->value + 0) // rvalue, don't change pointer
#define P_RULE_SPECIFIER    (L->specifier + 0) // rvalue, don't change pointer

#define P_INPUT_VALUE       (Level_Args_Head(L) + 0)
#define P_TYPE              Type_Of(P_INPUT_VALUE)
#define P_INPUT             Cell_Flex(P_INPUT_VALUE)
#define P_INPUT_SPECIFIER   VAL_SPECIFIER(P_INPUT_VALUE)
#define P_POS               VAL_INDEX(P_INPUT_VALUE)

#define P_FLAGS             VAL_INT64(Level_Args_Head(L) + 1)
#define P_HAS_CASE          (did (P_FLAGS & AM_FIND_CASE))

#define P_NUM_QUOTES_VALUE  (L->rootvar + 3)
#define P_NUM_QUOTES        VAL_INT32(P_NUM_QUOTES_VALUE)

#define P_OUT (L->out)

#define P_CELL Level_Spare(L)

#define FETCH_NEXT_RULE(L) \
    Fetch_Next_In_Level(nullptr, (L))

#define FETCH_TO_BAR_OR_END(L) \
    while (NOT_END(L->value) and not Is_Bar(P_RULE)) \
        { FETCH_NEXT_RULE(L); }


// See the notes on `flags` in the main parse loop for how these work.
//
enum parse_flags {
    //
    // In R3-Alpha, the "parse->flags" (persistent across an iteration) were
    // distinct from the "flags" (per recursion, zeroed on each loop).  The
    // former had undocumented overlap with the values of AM_FIND_XXX flags.
    //
    // They are unified in Ren-C, with the overlap asserted.
    //
    PF_FIND_ONLY = 1 << 0,
    PF_FIND_CASE = 1 << 1,
    PF_FIND_LAST = 1 << 2,
    PF_FIND_REVERSE = 1 << 3,
    PF_FIND_TAIL = 1 << 4,
    PF_FIND_MATCH = 1 << 5,

    PF_SET = 1 << 6,
    PF_COPY = 1 << 7,
    PF_NOT = 1 << 8,
    PF_NOT2 = 1 << 9,
    PF_THEN = 1 << 10,
    PF_AHEAD = 1 << 11,
    PF_REMOVE = 1 << 12,
    PF_INSERT = 1 << 13,
    PF_CHANGE = 1 << 14,
    PF_WHILE = 1 << 15,

    PF_REDBOL = 1 << 16  // use Rebol2/Red-style rules
};

// Note: clang complains if `cast(int, ...)` used here, though gcc doesn't
STATIC_ASSERT((int)AM_FIND_CASE == (int)PF_FIND_CASE);
STATIC_ASSERT((int)AM_FIND_MATCH == (int)PF_FIND_MATCH);

#define PF_FIND_MASK \
    (PF_FIND_ONLY | PF_FIND_CASE | PF_FIND_LAST | PF_FIND_REVERSE \
        | PF_FIND_TAIL | PF_FIND_MATCH)

#define PF_STATE_MASK (~PF_FIND_MASK & ~PF_REDBOL)


// In %words.r, the parse words are lined up in order so they can be quickly
// filtered, skipping the need for a switch statement if something is not
// a parse command.
//
// !!! This and other efficiency tricks from R3-Alpha should be reviewed to
// see if they're really the best option.
//
INLINE Option(SymId) VAL_CMD(const Cell* v) {
    Option(SymId) sym = Cell_Word_Id(v);
    if (sym >= SYM_SET and sym <= SYM_END)
        return sym;
    return SYM_0;
}


// Throws is a helper that sets up a call frame and invokes the
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
static bool Subparse_Throws(
    bool *interrupted_out,
    Value* out,
    Cell* input,
    Specifier* input_specifier,
    const Cell* rules,
    Specifier* rules_specifier,
    Flags flags
){
    assert(Any_List(rules));
    assert(Any_Series(input));

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
        *interrupted_out = false;
        Init_Integer(out, VAL_INDEX(input));
        return false;
    }

    DECLARE_LEVEL (L);

    SET_END(out);
    L->out = out;

    L->gotten = nullptr;
    SET_FRAME_VALUE(L, Cell_List_At(rules)); // not an END due to test above
    L->specifier = Derive_Specifier(rules_specifier, rules);

    L->source->vaptr = nullptr;
    L->source->array = Cell_Array(rules);
    L->source->index = VAL_INDEX(rules) + 1;
    L->source->pending = L->value + 1;

    L->flags = Endlike_Header(DO_FLAG_PARSE_FRAME); // terminates L->spare

    Push_Level_Core(L); // checks for C stack overflow
    Reuse_Varlist_If_Available(L);
    Push_Action(L, NAT_ACTION(SUBPARSE), UNBOUND);

    Begin_Action(L, Canon(SYM_SUBPARSE), m_cast(Value*, END_NODE));

    L->param = END_NODE; // informs infix lookahead
    L->arg = m_cast(Value*, END_NODE);
    assert(L->refine == END_NODE); // passed to Begin_Action()
    L->special = END_NODE;

    Erase_Cell(Level_Args_Head(L) + 0);
    Derelativize(Level_Args_Head(L) + 0, input, input_specifier);

    // We always want "case-sensitivity" on binary bytes, vs. treating as
    // case-insensitive bytes for ASCII characters.
    //
    Erase_Cell(Level_Args_Head(L) + 1);
    assert((flags & PF_STATE_MASK) == 0);  // no "parse state" flags allowed
    Init_Integer(Level_Args_Head(L) + 1, flags);

    // Need to track NUM-QUOTES somewhere that it can be read from the frame
    //
    Init_Nulled(Erase_Cell(Level_Args_Head(L) + 2));

    assert(ACT_NUM_PARAMS(NAT_ACTION(SUBPARSE)) == 4); // checks RETURN:
    Init_Nulled(Erase_Cell(Level_Args_Head(L) + 3));

    // !!! By calling the subparse native here directly from its C function
    // vs. going through the evaluator, we don't get the opportunity to do
    // things like HIJACK it.  Consider APPLY-ing it.
    //
    const Value* r = (NATIVE_CFUNC(SUBPARSE))(L);
    assert(NOT_END(out));

    Drop_Action(L);
    Drop_Level(L);

    if (r == BOUNCE_THROWN) {
        //
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
        if (Is_Action(out)) {
            if (VAL_ACTION(out) == NAT_ACTION(PARSE_REJECT)) {
                CATCH_THROWN(out, out);
                assert(Is_Nulled(out));
                *interrupted_out = true;
                return false;
            }

            if (VAL_ACTION(out) == NAT_ACTION(PARSE_BREAK)) {
                CATCH_THROWN(out, out);
                assert(Is_Integer(out));
                *interrupted_out = true;
                return false;
            }
        }

        return true;
    }

    assert(r == out);

    *interrupted_out = false;
    return false;
}


// Very generic errors.  Used to be parameterized with the parse rule in
// question, but now the `where` at the time of failure will indicate the
// location in the parse dialect that's the problem.

INLINE Error* Error_Parse_Rule(void) {
    return Error_Parse_Rule_Raw();
}

INLINE Error* Error_Parse_End(void) {
    return Error_Parse_End_Raw();
}

INLINE Error* Error_Parse_Command(Level* L) {
    DECLARE_VALUE (command);
    Derelativize(command, P_RULE, P_RULE_SPECIFIER);
    return Error_Parse_Command_Raw(command);
}

INLINE Error* Error_Parse_Variable(Level* L) {
    DECLARE_VALUE (variable);
    Derelativize(variable, P_RULE, P_RULE_SPECIFIER);
    return Error_Parse_Variable_Raw(variable);
}


static void Print_Parse_Index(Level* L) {
    DECLARE_VALUE (input);
    Init_Any_Series_At_Core(
        input,
        P_TYPE,
        P_INPUT,
        P_POS,
        Is_Flex_Array(P_INPUT)
            ? P_INPUT_SPECIFIER
            : SPECIFIED
    );

    // Either the rules or the data could be positioned at the end.  The
    // data might even be past the end.
    //
    // !!! Or does PARSE adjust to ensure it never is past the end, e.g.
    // when seeking a position given in a variable or modifying?
    //
    if (IS_END(L->value)) {
        if (P_POS >= Flex_Len(P_INPUT))
            Debug_Fmt("[]: ** END **");
        else
            Debug_Fmt("[]: %r", input);
    }
    else {
        if (P_POS >= Flex_Len(P_INPUT))
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
    Level* L,
    const Value* any_series
) {
    if (any_series != Level_Args_Head(L) + 0)
        Copy_Cell(Level_Args_Head(L) + 0, any_series);

    VAL_INDEX(Level_Args_Head(L) + 0) =
        (VAL_INDEX(any_series) > VAL_LEN_HEAD(any_series))
            ? VAL_LEN_HEAD(any_series)
            : VAL_INDEX(any_series);

    if (Is_Binary(any_series) || (P_FLAGS & AM_FIND_CASE))
        P_FLAGS |= AM_FIND_CASE;
    else
        P_FLAGS &= ~AM_FIND_CASE;
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
static const Value* Get_Parse_Value(
    Value* out,
    const Cell* rule,
    Specifier* specifier
){
    if (Is_Word(rule)) {
        if (VAL_CMD(rule))  // includes Is_Bar()...also a "command"
            return Init_Word(out, Cell_Word_Symbol(rule));

        Move_Opt_Var_May_Fail(out, rule, specifier);

        if (Is_Trash(out))
            fail (Error_No_Value_Core(rule, specifier));

        if (Is_Nulled(out))
            fail (Error_No_Value_Core(rule, specifier));

        return out;
    }

    if (Is_Path(rule)) {
        //
        // !!! REVIEW: how should GET-PATH! be handled?
        //
        // Should PATH!s be evaluating GROUP!s?  This does, but would need
        // to route potential thrown values up to do it properly.

        if (Get_Path_Throws_Core(out, rule, specifier))
            fail (Error_No_Catch_For_Throw(out));

        if (Is_Trash(out))
            fail (Error_No_Value_Core(rule, specifier));

        if (Is_Nulled(out))
            fail (Error_No_Value_Core(rule, specifier));

        return out;
    }

    return Derelativize(out, rule, specifier);
}


//
//  Process_Group_For_Parse: C
//
// Historically a single group in PARSE ran code, discarding the value (with
// a few exceptions when appearing in an argument position to a rule).  Ren-C
// adds another behavior for when groups are "doubled", e.g. ((...)).  This
// makes them act like a COMPOSE/ONLY that runs each time they are visited.
//
Bounce Process_Group_For_Parse(
    Level* L,
    Value* cell,
    const Cell* group
){
    assert(Is_Group(group));
    Specifier* derived = Derive_Specifier(P_RULE_SPECIFIER, group);

    if (Eval_Array_At_Throws(cell, Cell_Array(group), VAL_INDEX(group), derived))
        return BOUNCE_THROWN;

    // !!! The input is not locked from modification by agents other than the
    // PARSE's own REMOVE/etc.  This is a sketchy idea, but as long as it's
    // allowed, each time arbitrary user code runs, rules have to be adjusted
    //
    if (P_POS > Flex_Len(P_INPUT))
        P_POS = Flex_Len(P_INPUT);

    if (not Is_Doubled_Group(group))  // non-doubled groups always discard
        return BOUNCE_INVISIBLE;

    if (Is_Void(cell))  // even for doubled groups, void evals are discarded
        return BOUNCE_INVISIBLE;

    if (Is_Trash(cell))
        fail ("Doubled GROUP! eval returned TRASH!");

    if (Is_Nulled(cell))
        fail ("Doubled GROUP! eval returned NULL!");

    if (Is_Group(cell))
        fail ("Doubled GROUP! eval returned GROUP!, re-evaluation disabled.");

    if (Is_Bar(cell))
        fail ("Doubled GROUP! eval returned BAR!...cannot be abstracted.");

    return cell;
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
static REBIXO Parse_String_One_Rule(Level* L, const Cell* rule) {
    assert(IS_END(P_OUT));

    if (Is_Void(rule))
        return P_POS;

    if (P_POS >= Flex_Len(P_INPUT)) {
        //
        // Only the VOID and BLOCK rules can potentially handle an END input
        // For instance, `parse "" [[[~void~ ~void~ ~void~]]]` should match.
        // The other cases would assert if fed an END marker as item.
        //
        if (not Is_Block(rule))
            return END_FLAG;
    }

    REBLEN flags = (P_FLAGS & PF_FIND_MASK) | AM_FIND_MATCH | AM_FIND_TAIL;

    if (Is_Group(rule)) {
        rule = Process_Group_For_Parse(L, P_CELL, rule);
        if (rule == BOUNCE_THROWN) {
            Copy_Cell(P_OUT, P_CELL);
            return THROWN_FLAG;
        }
        if (rule == BOUNCE_INVISIBLE) {
            assert(P_POS <= Flex_Len(P_INPUT)); // !!! Process_Group ensures
            return P_POS;
        }
        // was a doubled group ((...)), use result as rule
    }

    switch (Type_Of(rule)) {
    case TYPE_BLANK:
        if (GET_ANY_CHAR(P_INPUT, P_POS) == ' ')  // treat as space
            return P_POS + 1;
        return END_FLAG;

    case TYPE_CHAR:
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

    case TYPE_EMAIL:
    case TYPE_TEXT:
    case TYPE_BINARY: {
        REBLEN index = Find_Str_Str(
            P_INPUT,
            0,
            P_POS,
            Flex_Len(P_INPUT),
            1,
            Cell_Flex(rule),
            VAL_INDEX(rule),
            Cell_Series_Len_At(rule),
            flags
        );
        if (index == NOT_FOUND)
            return END_FLAG;
        return index; }

    case TYPE_FILE: {
        //
        // !!! The content to be matched does not have the delimiters in the
        // actual series data.  This FORMs it, but could be more optimized.
        //
        Flex* formed = Copy_Form_Value(rule, 0);
        REBLEN index = Find_Str_Str(
            P_INPUT,
            0,
            P_POS,
            Flex_Len(P_INPUT),
            1,
            formed,
            0,
            Flex_Len(formed),
            flags
        );
        Free_Unmanaged_Flex(formed);
        if (index == NOT_FOUND)
            return END_FLAG;
        return index; }

    case TYPE_BITSET:
        //
        // Check the current character against a character set, advance matches
        //
        if (Check_Bit(
            Cell_Bitset(rule), GET_ANY_CHAR(P_INPUT, P_POS), not P_HAS_CASE
        )) {
            return P_POS + 1;
        }
        return END_FLAG;

    case TYPE_BLOCK: {
        //
        // This parses a sub-rule block.  It may throw, and it may mutate the
        // input series.
        //
        DECLARE_VALUE (subresult);
        bool interrupted;
        if (Subparse_Throws(
            &interrupted,
            subresult,
            P_INPUT_VALUE,
            SPECIFIED,
            rule,
            P_RULE_SPECIFIER,
            (P_FLAGS & PF_FIND_MASK) | (P_FLAGS & PF_REDBOL)
        )) {
            Copy_Cell(P_OUT, subresult);
            return THROWN_FLAG;
        }

        // !!! ignore "interrupted"? (e.g. ACCEPT or REJECT ran)

        if (Is_Nulled(subresult))
            return END_FLAG;

        REBINT index = VAL_INT32(subresult);
        assert(index >= 0);
        return cast(REBLEN, index); }

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
// Only in the case of THROWN_FLAG will L->out (aka P_OUT) be affected.
// Otherwise, it should exit the routine as an END marker (as it started);
//
static REBIXO Parse_Array_One_Rule_Core(
    Level* L,
    REBLEN pos,
    const Cell* rule
) {
    assert(IS_END(P_OUT));

    if (Is_Void(rule))
        return pos;

    Array* array = cast_Array(P_INPUT);
    Cell* item = Array_At(array, pos);

    if (IS_END(item)) {
        //
        // Only the VOID and BLOCK rules can potentially handle an END input
        // For instance, `parse [] [[[~void~ ~void~ ~void~]]]` should match.
        // The other cases would assert if fed an END marker as item.
        //
        if (not Is_Block(rule))
            return END_FLAG;
    }

    if (Is_Group(rule)) {
        rule = Process_Group_For_Parse(L, P_CELL, rule);
        if (rule == BOUNCE_THROWN) {
            Copy_Cell(P_OUT, P_CELL);
            return THROWN_FLAG;
        }
        if (rule == BOUNCE_INVISIBLE) {
            assert(pos <= Array_Len(array)); // !!! Process_Group ensures
            return pos;
        }
        // was a doubled group ((...)), use result as rule
    }

    switch (Type_Of(rule)) {
    case TYPE_BLANK:
        if (Type_Of(item) == TYPE_BLANK)
            return pos + 1;
        return END_FLAG;

    case TYPE_DATATYPE:
        if (Type_Of(item) == CELL_DATATYPE_TYPE(rule)) // specific datatype match
            return pos + 1;
        return END_FLAG;

    case TYPE_TYPESET:
        if (Typeset_Check(rule, Type_Of(item))) // type was found in the typeset
            return pos + 1;
        return END_FLAG;

    case TYPE_LIT_WORD:
        if (Is_Word(item) and VAL_WORD_CANON(item) == VAL_WORD_CANON(rule))
            return pos + 1;
        return END_FLAG;

    case TYPE_LIT_PATH:
        if (Is_Path(item) and Cmp_Array(item, rule, false) == 0)
            return pos + 1;
        return END_FLAG;

    case TYPE_BLOCK: {
        //
        // Process a subrule.  The subrule will run in its own frame, so it
        // will not change P_POS directly (it will have its own P_INPUT_VALUE)
        // Hence the return value regarding whether a match occurred or not
        // has to be based on the result that comes back in P_OUT.
        //
        REBLEN pos_before = P_POS;
        bool interrupted;

        P_POS = pos; // modify input position

        DECLARE_VALUE (subresult);
        if (Subparse_Throws(
            &interrupted,
            subresult,
            P_INPUT_VALUE, // use input value with modified position
            SPECIFIED,
            rule,
            P_RULE_SPECIFIER,
            (P_FLAGS & PF_FIND_MASK) | (P_FLAGS & PF_REDBOL)
        )) {
            Copy_Cell(P_OUT, subresult);
            return THROWN_FLAG;
        }

        // !!! ignore "interrupted"? (e.g. ACCEPT or REJECT ran)

        P_POS = pos_before; // restore input position

        if (Is_Nulled(subresult))
            return END_FLAG;

        REBINT index = VAL_INT32(subresult);
        assert(index >= 0);
        return cast(REBLEN, index); }

    case TYPE_TEXT:
    case TYPE_ISSUE:
        break;

    default:
        fail ("Unknown value type for match in ANY-ARRAY!");
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
INLINE REBIXO Parse_Array_One_Rule(Level* L, const Cell* rule) {
    return Parse_Array_One_Rule_Core(L, P_POS, rule);
}


//
//  To_Thru_Non_Block_Rule: C
//
static REBIXO To_Thru_Non_Block_Rule(
    Level* L,
    const Cell* rule,
    bool is_thru
) {
    assert(not Is_Block(rule));

    if (Is_Void(rule))
        return P_POS;

    if (Is_Integer(rule)) {
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
        REBLEN i = cast(REBLEN, Int32(rule)) - (is_thru ? 0 : 1);
        if (i > Flex_Len(P_INPUT))
            return Flex_Len(P_INPUT);
        return i;
    }

    if (Is_Word(rule) and Cell_Word_Id(rule) == SYM_END) {
        if (not (P_FLAGS & PF_REDBOL))
            fail ("Use <end> instead of END outside PARSE2");

        // `TO/THRU END` JUMPS TO END INPUT SERIES (ANY SERIES TYPE)
        //
        return Flex_Len(P_INPUT);
    }

    if (Is_Tag(rule)) {
        bool strict = true;
        if (0 == Compare_String_Vals(rule, Root_End_Tag, strict)) {
            return Flex_Len(P_INPUT);
        }
        else if (0 == Compare_String_Vals(rule, Root_Here_Tag, strict)) {
            fail ("TO/THRU <here> isn't supported in PARSE3");
        }
        else
            fail ("TAG! combinator must be <here> or <end> ATM");
    }

    if (Is_Flex_Array(P_INPUT)) {
        //
        // FOR ARRAY INPUT WITH NON-BLOCK RULES, USE Find_In_Array()
        //
        // !!! This adjusts it to search for non-literal words, but are there
        // other considerations for how non-block rules act with array input?
        //
        DECLARE_VALUE (word);
        if (Is_Lit_Word(rule)) {
            Derelativize(word, rule, P_RULE_SPECIFIER);
            CHANGE_VAL_TYPE_BITS(word, TYPE_WORD);
            rule = word;
        }

        REBLEN i = Find_In_Array(
            cast_Array(P_INPUT),
            P_POS,
            Flex_Len(P_INPUT),
            rule,
            1,
            (P_FLAGS & AM_FIND_CASE),
            1
        );

        if (i == NOT_FOUND)
            return END_FLAG;

        if (is_thru)
            return i + 1;

        return i;
    }

    //=//// PARSE INPUT IS A STRING OR BINARY, USE A FIND ROUTINE /////////=//

    if (Is_Binary(rule) or Any_String(rule)) {
        if (not Is_Text(rule) and not Is_Binary(rule)) {
            // !!! Can this be optimized not to use COPY?
            Flex* formed = Copy_Form_Value(rule, 0);
            REBLEN form_len = Flex_Len(formed);
            REBLEN i = Find_Str_Str(
                P_INPUT,
                0,
                P_POS,
                Flex_Len(P_INPUT),
                1,
                formed,
                0,
                form_len,
                (P_FLAGS & AM_FIND_CASE)
            );
            Free_Unmanaged_Flex(formed);

            if (i == NOT_FOUND)
                return END_FLAG;

            if (is_thru)
                return i + form_len;

            return i;
        }

        REBLEN i = Find_Str_Str(
            P_INPUT,
            0,
            P_POS,
            Flex_Len(P_INPUT),
            1,
            Cell_Flex(rule),
            VAL_INDEX(rule),
            Cell_Series_Len_At(rule),
            (P_FLAGS & AM_FIND_CASE)
        );

        if (i == NOT_FOUND)
            return END_FLAG;

        if (is_thru)
            return i + Cell_Series_Len_At(rule);

        return i;
    }

    if (Is_Char(rule)) {
        REBLEN i = Find_Str_Char(
            VAL_CHAR(rule),
            P_INPUT,
            0,
            P_POS,
            Flex_Len(P_INPUT),
            1,
            (P_FLAGS & AM_FIND_CASE)
        );

        if (i == NOT_FOUND)
            return END_FLAG;

        if (is_thru)
            return i + 1;

        return i;
    }

    if (Is_Bitset(rule)) {
        REBLEN i = Find_Str_Bitset(
            P_INPUT,
            0,
            P_POS,
            Flex_Len(P_INPUT),
            1,
            Cell_Bitset(rule),
            (P_FLAGS & AM_FIND_CASE)
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
    Level* L,
    const Value* rule_block,
    bool is_thru
) {
    DECLARE_VALUE (cell); // holds evaluated rules (use frame cell instead?)

    if (not SPORADICALLY(20)) {  // should be equivalent to recursing, test it
        if (Cell_Series_Len_At(rule_block) == 0)
            return P_POS;  // `to []` or `thru []` succeed quickly

        if (Cell_Series_Len_At(rule_block) == 1) {
            const Cell* at = Cell_List_At(rule_block);
            if (Is_Word(at) and Cell_Word_Id(at) == SYM__TVOID_T)
                return P_POS;  // make `to [~void~]` about as fast as `to []`
        }
    }

    // R3-Alpha was squeamish about recursing on BLOCK! rules, and handled a
    // limited set of possibilities.  We optimize for empty blocks but don't
    // fear the recursion, vs. writing a bunch of error prone hacks.
    //
    // (Nevertheless this isn't quite wired up correctly.)

    DECLARE_VALUE(input_at);
    Copy_Cell(input_at, P_INPUT_VALUE);

    for (; VAL_INDEX(input_at) <= Flex_Len(P_INPUT); ++VAL_INDEX(input_at)) {
        bool interrupted;
        DECLARE_VALUE (subresult);
        if (Subparse_Throws(
            &interrupted,
            subresult,
            input_at,
            Any_List(input_at) ?P_INPUT_SPECIFIER : SPECIFIED,
            rule_block,
            SPECIFIED,
            (P_FLAGS & PF_FIND_MASK) | (P_FLAGS & PF_REDBOL)
        )){
            fail ("Throws not currently supported in TO/THRU BLOCK!");
        }

        if (interrupted)
            fail ("Interruptions not currently supported in TO/THRU BLOCK!");

        if (Is_Nulled(subresult))
            continue;  // try matching at next position

        REBINT index = VAL_INT32(subresult);
        assert(index >= 0);
        if (cast(REBLEN, index) == P_POS)  // trivial match [~void~] etc.
            return cast(REBLEN, index);
        if (is_thru)
            return cast(REBLEN, index);
        return cast(REBLEN, index - 1);
    }

    return END_FLAG;
}


//
//  subparse: native [
//
//  {Internal support function for PARSE (acts as variadic to consume rules)}
//
//      return: [~null~ integer!]
//      input [any-series!]
//      find-flags [integer!]
//      <local> num-quotes
//  ]
//
DECLARE_NATIVE(SUBPARSE)
//
// Rules are matched until one of these things happens:
//
// * A rule fails, and is not then picked up by a later "optional" rule.
// This returns OUT with the value in out as BLANK!.
//
// * You run out of rules to apply without any failures or errors, and the
// position in the input series is returned.  This may be at the end of
// the input data or not--it's up to the caller to decide if that's relevant.
// This will return OUT with out containing an integer index.
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
// THROW, RETURN, BREAK, CONTINUE).  This returns a thrown value.
//
// * A special throw to indicate a return out of the PARSE itself, triggered
// by the RETURN instruction.  This also returns a thrown value, but will
// be caught by PARSE before returning.
//
{
    INCLUDE_PARAMS_OF_SUBPARSE;
    UNUSED(ARG(FIND_FLAGS));  // !!! access via macro

    Level* L = level_; // nice alias of implicit native parameter

    Set_Parse_Series(L, ARG(INPUT));  // doesn't reset, just checks
    UNUSED(ARG(NUM_QUOTES));  // Set_Parse_Series sets this

    assert(IS_END(P_OUT)); // invariant provided by evaluator

  #if RUNTIME_CHECKS
    //
    // These parse state variables live in chunk-stack REBVARs, which can be
    // annoying to find to inspect in the debugger.  This makes pointers into
    // the value payloads so they can be seen more easily.
    //
    const REBLEN *pos_debug = &P_POS;
    (void)pos_debug; // UNUSED() forces corruption in C++11 debug builds
  #endif

  #if DEBUG_COUNT_TICKS
    Tick tick = TG_Tick; // helpful to cache for visibility also
  #endif

    DECLARE_VALUE (save);

    REBLEN start = P_POS; // recovery restart point
    REBLEN begin = P_POS; // point at beginning of match

    // The loop iterates across each cell's worth of "rule" in the rule
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
    assert((P_FLAGS & PF_STATE_MASK) == 0);

    const Cell* set_or_copy_word = nullptr;

    REBINT mincount = 1; // min pattern count
    REBINT maxcount = 1; // max pattern count

    while (NOT_END(L->value)) {

        /* Print_Parse_Index(L); */
        UPDATE_EXPRESSION_START(L);

      #if DEBUG_COUNT_TICKS
        ++TG_Tick;
        tick = TG_Tick;
        cast(void, tick); // suppress unused warning (but UNUSED() corrupts)
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

    //=//// HANDLE BAR! FIRST... BEFORE GROUP! ////////////////////////////=//

        // BAR!s cannot be abstracted.  If they could be, then you'd have to
        // run all doubled groups `((...))` to find them in alternates lists.

        if (Is_Bar(P_RULE)) {
            //
            // If a BAR! is hit while processing any rules in the rules
            // block, then that means the current option didn't fail out
            // first...so it's a success for the rule.  Stop processing and
            // return the current input position.
            //
            // (Note this means `[| ...anything...]` is a "no-op" match)
            //
            return Init_Integer(P_OUT, P_POS);
        }

        // The rule in the block of rules can be literal, while the "real
        // rule" we want to process is the result of a variable fetched from
        // that item.  If the code makes it to the iterated rule matching
        // section, rule should be set to something non-nullptr by then...
        //
        const Value* rule;
        if (not Is_Group(P_RULE))
            rule = Derelativize(save, P_RULE, P_RULE_SPECIFIER);
        else {
            rule = Process_Group_For_Parse(L, save, P_RULE);
            if (rule == BOUNCE_THROWN) {
                Copy_Cell(P_OUT, save);
                return BOUNCE_THROWN;
            }
            if (rule == BOUNCE_INVISIBLE) { // was a (...), or null-bearing ((...))
                FETCH_NEXT_RULE(L); // ignore result and go on to next rule
                continue;
            }
        }

        // Some iterated rules have a parameter.  `3 into [some "a"]` will
        // actually run the INTO `rule` 3 times with the `subrule` of
        // `[some "a"]`.  Because it is iterated it is only captured the first
        // time through, nullptr indicates it's not been captured yet.
        //
        const Value* subrule = nullptr;

        // If word, set-word, or get-word, process it:
        if (Type_Of(rule) >= TYPE_WORD and Type_Of(rule) <= TYPE_GET_WORD) {

            Option(SymId) cmd = VAL_CMD(rule);
            if (cmd) {
                if (not Is_Word(rule)) // Command but not WORD! (COPY:, :THRU)
                    fail (Error_Parse_Command(L));

                if (cmd <= SYM_BREAK) { // optimization

                    switch (cmd) {

                    case SYM_SEEK:  // modern variant on GET-WORD! in PARSE
                        FETCH_NEXT_RULE(L);
                        rule = Derelativize(save, P_RULE, P_RULE_SPECIFIER);
                        goto seek_rule;

                    // Note: mincount = maxcount = 1 on entry
                    case SYM_WHILE:
                        if (not (P_FLAGS & PF_REDBOL))
                            fail (
                                "Please replace PARSE3's WHILE with OPT SOME (no"
                                " progress requirement any longer).  FURTHER can"
                                " be added to PARSE3 if it's really needed."
                                " https://forum.rebol.info/t/1540/12"
                            );

                        P_FLAGS |= PF_WHILE;
                        mincount = 0;
                        goto handle_loop;

                    case SYM_ANY:
                        if (not (P_FLAGS & PF_REDBOL))
                            fail (
                                "Please replace PARSE's ANY with OPT SOME"
                                " -- it's being reclaimed for a new construct"
                                " https://forum.rebol.info/t/1540/12 (or use PARSE2)"
                            );
                        mincount = 0;
                        goto handle_loop;

                    case SYM_REPEAT:
                        if (P_FLAGS & PF_REDBOL)
                            fail ("REPEAT not available in /REDBOL PARSE mode");

                        // !!! OPT REPEAT (N) RULE can't work because OPT is
                        // done by making the minimum number of match counts
                        // zero.  But unfortunately if that rule isn't in a
                        // BLOCK! then the 0 repeat rule transfers onto the
                        // rule... making it act like `REPEAT (N) OPT RULE`
                        // which is not the same.
                        //

                        if (mincount != 1 or maxcount != 1)
                            fail (
                                "Old PARSE REPEAT does not mix with ranges or OPT"
                                " so put a block around the REPEAT!"
                            );

                        FETCH_NEXT_RULE(L);
                        if (Is_Group(P_RULE)) {
                            if (Eval_Value_Core_Throws(OUT, P_RULE, P_RULE_SPECIFIER))
                                return BOUNCE_THROWN;
                        }
                        else {
                            Derelativize(OUT, P_RULE, P_RULE_SPECIFIER);
                        }

                        if (Is_Integer(OUT)) {
                            mincount = Int32s(OUT, 0);
                            maxcount = Int32s(OUT, 0);
                        } else {
                            if (
                                not Is_Block(OUT)
                                or not (
                                    Cell_Series_Len_At(OUT) == 2
                                    and Is_Integer(Cell_List_At(OUT))
                                    and Is_Integer(Cell_List_At(OUT) + 1)
                                )
                            ){
                                fail ("REPEAT takes INTEGER! or length 2 BLOCK! range");
                            }

                            mincount = Int32s(Cell_List_At(OUT), 0);
                            maxcount = Int32s(Cell_List_At(OUT) + 1, 0);

                            if (maxcount < mincount)
                                fail ("REPEAT range can't have lower max than minimum");
                        }

                        SET_END(OUT);

                        FETCH_NEXT_RULE(L);
                        continue;

                    case SYM_SOME:
                    handle_loop:
                        maxcount = INT32_MAX;
                        FETCH_NEXT_RULE(L);
                        continue;

                    case SYM_OPT:
                        mincount = 0;
                        FETCH_NEXT_RULE(L);
                        continue;

                    case SYM_COPY:
                        if (not (P_FLAGS & PF_REDBOL))
                            fail ("Use ACROSS instead of COPY unless /REDBOL");

                        P_FLAGS |= PF_COPY;
                        goto set_or_copy_pre_rule;

                    case SYM_SET:
                        if (not (P_FLAGS & PF_REDBOL))
                            fail ("Use SET-WORD! vs. SET unless /REDBOL");

                        P_FLAGS |= PF_SET;
                        goto set_or_copy_pre_rule;

                    set_or_copy_pre_rule:
                        FETCH_NEXT_RULE(L);

                        if (not (Is_Word(P_RULE) or Is_Set_Word(P_RULE)))
                            fail (Error_Parse_Variable(L));

                        if (VAL_CMD(P_RULE))  // set set [...]
                            fail (Error_Parse_Command(L));

                        set_or_copy_word = P_RULE;
                        FETCH_NEXT_RULE(L);

                        if (not (P_FLAGS & PF_SET))
                            continue;

                        // !!! Bootstrap handling of SET and SET-WORD! is much
                        // worse than UPARSE, because R3-Alpha's conception of
                        // SET wasn't based on synthesized combinator products.
                        //
                        // Compromise with a really weak handling of special
                        // cases, but other than that you just get one unit
                        // of series content matched.
                        //
                    handle_set:
                        if (Is_Group(P_RULE)) {
                            if (Eval_Value_Core_Throws(
                                P_OUT,
                                P_RULE,
                                P_RULE_SPECIFIER
                            )){
                                return BOUNCE_THROWN;
                            }
                            Value* var = Sink_Var_May_Fail(
                                set_or_copy_word, P_RULE_SPECIFIER
                            );
                            Copy_Cell(var, P_OUT);
                            FETCH_NEXT_RULE(L);
                            P_FLAGS &= ~PF_SET;
                            continue;
                        }

                        if (
                            Is_Word(P_RULE)
                            and Cell_Word_Id(P_RULE) == SYM_ACROSS
                        ){
                            FETCH_NEXT_RULE(L);
                            P_FLAGS &= ~PF_SET;
                            P_FLAGS |= PF_COPY;
                            continue;
                        }

                        continue;  // use old weak interpretation of SET

                    case SYM_NOT: {
                        P_FLAGS |= PF_NOT;
                        P_FLAGS ^= PF_NOT2;
                        FETCH_NEXT_RULE(L);
                        bool strict = true;
                        if (not (P_FLAGS & PF_REDBOL)) {
                            if (not (
                                Is_Word(P_RULE)
                                and Cell_Word_Id(P_RULE) == SYM_AHEAD
                            ) and not (
                                Is_Tag(P_RULE)
                                and 0 == Compare_String_Vals(
                                    P_RULE,
                                    Root_End_Tag,
                                    strict
                                )
                            )){
                                fail ("NOT must be NOT AHEAD/<end> in PARSE3");
                            }
                        }
                        continue; }

                    case SYM_AND:
                    case SYM_AHEAD:
                        P_FLAGS |= PF_AHEAD;
                        FETCH_NEXT_RULE(L);
                        continue;

                    case SYM_THEN:
                        P_FLAGS |= PF_THEN;
                        FETCH_NEXT_RULE(L);
                        continue;

                    case SYM_REMOVE:
                        P_FLAGS |= PF_REMOVE;
                        FETCH_NEXT_RULE(L);
                        continue;

                    case SYM_INSERT:
                        P_FLAGS |= PF_INSERT;
                        FETCH_NEXT_RULE(L);
                        goto post_match_processing;

                    case SYM_CHANGE:
                        P_FLAGS |= PF_CHANGE;
                        FETCH_NEXT_RULE(L);
                        continue;

                    case SYM_ACCEPT: {
                        //
                        // ACCEPT means different things in Rebol2/Red (synonym
                        // for BREAK) where in UPARSE it means RETURN.
                        //
                        FETCH_NEXT_RULE(L);
                        if (IS_END(P_RULE))
                            fail ("PARSE3 ACCEPT requires argument");

                        DECLARE_VALUE (thrown_arg);
                        if (Is_Tag(P_RULE)) {
                            bool strict = true;
                            if (0 == Compare_String_Vals(
                                P_RULE,
                                Root_Here_Tag,
                                strict
                            )){
                                Copy_Cell(thrown_arg, P_INPUT_VALUE);
                            }
                            else
                                fail ("PARSE3 ACCEPT TAG! only works with <here>");
                        }
                        else if (Is_Group(P_RULE)) {
                            if (Eval_Value_Core_Throws(
                                thrown_arg,
                                P_RULE,
                                P_RULE_SPECIFIER
                            )){
                                Copy_Cell(P_OUT, thrown_arg);
                                return BOUNCE_THROWN;
                            }
                        }
                        else
                            fail ("PARSE3 ACCEPT only works with GROUP! and <here>");

                        Copy_Cell(P_OUT, NAT_VALUE(PARSE_ACCEPT));
                        CONVERT_NAME_TO_THROWN(P_OUT, thrown_arg);
                        return BOUNCE_THROWN; }

                    case SYM_BREAK: {
                        //
                        // This has to be throw-style, because it's not enough
                        // to just say the current rule succeeded...it climbs
                        // up and affects an enclosing parse loop.
                        //
                        DECLARE_VALUE (thrown_arg);
                        Init_Integer(thrown_arg, P_POS);
                        Copy_Cell(P_OUT, NAT_VALUE(PARSE_BREAK));

                        // Unfortunately, when the warnings are set all the
                        // way high for uninitialized variable use, the
                        // compiler may think this integer's binding will
                        // be used by the Copy_Cell() inlined here.  Get
                        // past that by initializing it.
                        //
                        thrown_arg->extra.corrupt = thrown_arg;  // local junk

                        CONVERT_NAME_TO_THROWN(P_OUT, thrown_arg);
                        return BOUNCE_THROWN;
                    }

                    case SYM_REJECT: {
                        //
                        // Similarly, this is a break/continue style "throw"
                        //
                        Copy_Cell(P_OUT, NAT_VALUE(PARSE_REJECT));
                        CONVERT_NAME_TO_THROWN(P_OUT, NULLED_CELL);
                        return BOUNCE_THROWN;
                    }

                    case SYM_FAIL:
                        if (not (P_FLAGS & PF_REDBOL))
                            fail ("Use BYPASS for next alternate in PARSE");
                        goto handle_bypass;

                    handle_bypass:
                    case SYM_BYPASS:
                        P_POS = NOT_FOUND;
                        FETCH_NEXT_RULE(L);
                        goto post_match_processing;

                    case SYM_IF:
                        if (not (P_FLAGS & PF_REDBOL))
                            fail ("Use WHEN for arity-1 IF in PARSE");
                        goto handle_when;

                    handle_when:
                    case SYM_WHEN: {
                        FETCH_NEXT_RULE(L);
                        if (IS_END(P_RULE))
                            fail (Error_Parse_End());

                        if (not Is_Group(P_RULE))
                            fail (Error_Parse_Rule());

                        // might GC
                        DECLARE_VALUE (condition);
                        if (Eval_Array_At_Throws(
                            condition,
                            Cell_Array(P_RULE),
                            VAL_INDEX(P_RULE),
                            P_RULE_SPECIFIER
                        )) {
                            Copy_Cell(P_OUT, condition);
                            return BOUNCE_THROWN;
                        }

                        FETCH_NEXT_RULE(L);

                        if (IS_TRUTHY(condition))
                            continue;

                        P_POS = NOT_FOUND;
                        goto post_match_processing;
                    }

                    case SYM_LIMIT:
                        fail (Error_Not_Done_Raw());

                    case SYM__Q_Q:
                        Print_Parse_Index(L);
                        FETCH_NEXT_RULE(L);
                        continue;

                    default: //the list above should be exhaustive
                        assert(false);
                    }
                }
                // Any other cmd must be a match command, so proceed...
            }
            else {
                // It's not a PARSE command, get or set it

                // word: - set a variable to the series at current index
                if (Is_Set_Word(rule)) {
                    //
                    // Marking the parse in a slot that is a target of a
                    // rule, e.g. `thru pos: xxx`, handled by UPARSE so go
                    // ahead and allow it here.
                    //
                    // https://github.com/rebol/rebol-issues/issues/2269
                    //
                    /* if (P_FLAGS & PF_STATE_MASK != 0)
                        fail (Error_Parse_Rule()); */

                    set_or_copy_word = P_RULE;

                    FETCH_NEXT_RULE(L);

                    bool strict = true;
                    if (
                        NOT_END(P_RULE)
                        and Is_Tag(P_RULE)
                        and (0 == Compare_String_Vals(
                            P_RULE,
                            Root_Here_Tag,
                            strict
                        ))
                    ){  // tolerate POS: <HERE> whether in Redbol or not
                        Copy_Cell(
                            Sink_Var_May_Fail(rule, P_RULE_SPECIFIER),
                            P_INPUT_VALUE
                        );
                        FETCH_NEXT_RULE(L);
                        continue;
                    }

                    if (P_FLAGS & PF_REDBOL) {  // POS: captures input position
                        Copy_Cell(
                            Sink_Var_May_Fail(rule, P_RULE_SPECIFIER),
                            P_INPUT_VALUE
                        );
                        continue;
                    }

                    P_FLAGS |= PF_SET;
                    goto handle_set;
                }

                // :word - change the index for the series to a new position
                if (Is_Get_Word(rule)) {
                    if (not (P_FLAGS & PF_REDBOL))
                        fail ("Use SEEK vs. GET-WORD! unless PARSE/REDBOL");

                  seek_rule: ;
                    DECLARE_VALUE (temp);
                    Move_Opt_Var_May_Fail(temp, rule, P_RULE_SPECIFIER);
                    if (not Any_Series(temp)) { // #1263
                        DECLARE_VALUE (non_series);
                        Derelativize(non_series, P_RULE, P_RULE_SPECIFIER);
                        fail (Error_Parse_Series_Raw(non_series));
                    }
                    Set_Parse_Series(L, temp);

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
                    if ((P_FLAGS & PF_STATE_MASK) == 0)
                        begin = P_POS;

                    FETCH_NEXT_RULE(L);
                    continue;
                }

                // word - some other variable
                if (Is_Word(rule)) {
                    assert(rule == save);  // need to be careful...
                    rule = Copy_Cell(
                        save,  // safe because we get, then copy
                        Get_Opt_Var_May_Fail(rule, SPECIFIED)
                    );

                    if (Is_Nulled(rule) or Is_Trash(rule))
                        fail (Error_No_Value_Core(rule, P_RULE_SPECIFIER));
                }
                else {
                    // rule can still be 'word or /word
                }
            }
        }
        else if (Any_Path(rule)) {
            if (Is_Path(rule)) {
                if (Get_Path_Throws_Core(save, rule, P_RULE_SPECIFIER)) {
                    Copy_Cell(P_OUT, save);
                    return BOUNCE_THROWN;
                }

                rule = save;
            }
            else if (Is_Set_Path(rule)) {
                if (Set_Path_Throws_Core(
                    save, rule, P_RULE_SPECIFIER, P_INPUT_VALUE
                )){
                    Copy_Cell(P_OUT, save);
                    return BOUNCE_THROWN;
                }

                // Nothing left to do after storing the parse position in the
                // path location...continue.
                //
                FETCH_NEXT_RULE(L);
                continue;
            }
            else if (Is_Get_Path(rule)) {
                if (Get_Path_Throws_Core(save, rule, P_RULE_SPECIFIER)) {
                    Copy_Cell(P_OUT, save);
                    return BOUNCE_THROWN;
                }

                // !!! This allows the series to be changed, as per #1263,
                // but note the positions being returned and checked aren't
                // prepared for this, they only exchange numbers ATM (!!!)
                //
                if (not Any_Series(save))
                    fail (Error_Parse_Series_Raw(save));

                Set_Parse_Series(L, save);
                FETCH_NEXT_RULE(L);
                continue;
            }
            else
                assert(Is_Lit_Path(rule));

            if (P_POS > Flex_Len(P_INPUT))
                P_POS = Flex_Len(P_INPUT);
        }

        // All cases should have either set `rule` by this point or continued
        //
        assert(rule and not Is_Nulled(rule));

        // Counter? 123
        if (Is_Integer(rule)) { // Specify count or range count
            if (not (P_FLAGS & PF_REDBOL))
                fail (
                    "[1 2 rule] now illegal https://forum.rebol.info/t/1578/6"
                    " (or use PARSE/REDBOL)"
                );

            P_FLAGS |= PF_WHILE;
            mincount = maxcount = Int32s(rule, 0);

            FETCH_NEXT_RULE(L);
            if (IS_END(P_RULE))
                fail (Error_Parse_End());

            rule = Get_Parse_Value(save, P_RULE, P_RULE_SPECIFIER);

            if (Is_Integer(rule)) {
                maxcount = Int32s(rule, 0);

                FETCH_NEXT_RULE(L);
                if (IS_END(L->value))
                    fail (Error_Parse_End());

                rule = Get_Parse_Value(save, P_RULE, P_RULE_SPECIFIER);
            }
        }
        else if (Is_Tag(rule)) {
            bool strict = true;
            if (0 == Compare_String_Vals(rule, Root_End_Tag, strict)) {
                FETCH_NEXT_RULE(L);
                begin = P_POS;
                goto handle_end;
            }
            if (0 == Compare_String_Vals(rule, Root_Here_Tag, strict)) {
                fail ("<here> tag would be no-op in this position");
            }
            fail ("Only TAG! combinators PARSE3 supports are <here> and <end>");
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

        FETCH_NEXT_RULE(L);

        begin = P_POS;// input at beginning of match section

        REBINT count; // gotos would cross initialization
        count = 0;
        while (count < maxcount) {
            assert(not Is_Bar(rule));

            REBIXO i; // temp index point

            if (Is_Word(rule)) {
                Option(SymId) cmd = VAL_CMD(rule);

                switch (cmd) {
                case SYM_SKIP:
                    if (not (P_FLAGS & PF_REDBOL))
                        fail ("Use ONE instead of SKIP outside PARSE2");
                    goto handle_one;

                case SYM_ONE:
                handle_one:
                    i = (P_POS < Flex_Len(P_INPUT))
                        ? P_POS + 1
                        : END_FLAG;
                    break;

                case SYM_END:
                    if (not (P_FLAGS & PF_REDBOL))
                        fail ("Use <end> instead of END outside PARSE2");
                    goto handle_end;

                case SYM_TO:
                case SYM_THRU: {
                    if (IS_END(L->value))
                        fail (Error_Parse_End());

                    if (!subrule) { // capture only on iteration #1
                        subrule = Get_Parse_Value(
                            save, P_RULE, P_RULE_SPECIFIER
                        );
                        FETCH_NEXT_RULE(L);
                    }

                    bool is_thru = (cmd == SYM_THRU);

                    if (Is_Block(subrule))
                        i = To_Thru_Block_Rule(L, subrule, is_thru);
                    else
                        i = To_Thru_Non_Block_Rule(L, subrule, is_thru);
                    break; }

                case SYM_QUOTE:
                    if (not (P_FLAGS & PF_REDBOL))
                        fail ("THE and not QUOTE unless using PARSE/REDBOL");
                    goto literal_match;

                case SYM_THE:
                    goto literal_match;

                literal_match: {
                    if (not Is_Flex_Array(P_INPUT))
                        fail (Error_Parse_Rule()); // see #2253

                    if (IS_END(L->value))
                        fail (Error_Parse_End());

                    if (not subrule) { // capture only on iteration #1
                        subrule = Derelativize(save, P_RULE, P_RULE_SPECIFIER);
                        FETCH_NEXT_RULE(L);
                    }

                    Cell* cmp = Array_At(cast_Array(P_INPUT), P_POS);

                    if (IS_END(cmp))
                        i = END_FLAG;
                    else if (0 == Cmp_Value(cmp, subrule, P_HAS_CASE))
                        i = P_POS + 1;
                    else
                        i = END_FLAG;
                    break;
                }

                case SYM_INTO: {
                    if (IS_END(L->value))
                        fail (Error_Parse_End());

                    if (!subrule) {
                        subrule = Get_Parse_Value(
                            save, P_RULE, P_RULE_SPECIFIER
                        );
                        FETCH_NEXT_RULE(L);
                    }

                    if (not Is_Block(subrule))
                        fail (Error_Parse_Rule());

                    // parse ["aa"] [into ["a" "a"]] ; is legal
                    // parse "aa" [into ["a" "a"]] ; is not...already "into"
                    //
                    if (not Is_Flex_Array(P_INPUT))
                        fail (Error_Parse_Rule());

                    Cell* into = Array_At(cast_Array(P_INPUT), P_POS);

                    if (
                        IS_END(into)
                        or (not (
                            Is_Binary(into)
                            or Any_String(into)
                        )
                        and not Any_List(into))
                    ){
                        i = END_FLAG;
                        break;
                    }

                    bool interrupted;
                    if (Subparse_Throws(
                        &interrupted,
                        P_CELL,
                        into,
                        P_INPUT_SPECIFIER, // val was taken from P_INPUT
                        subrule,
                        P_RULE_SPECIFIER,
                        (P_FLAGS & PF_FIND_MASK) | (P_FLAGS & PF_REDBOL)
                    )) {
                        Copy_Cell(P_OUT, P_CELL);
                        return BOUNCE_THROWN;
                    }

                    // !!! ignore interrupted? (e.g. ACCEPT or REJECT ran)

                    if (Is_Nulled(P_CELL)) {
                        i = END_FLAG;
                    }
                    else {
                        if (VAL_UINT32(P_CELL) != VAL_LEN_HEAD(into))
                            i = END_FLAG;
                        else
                            i = P_POS + 1;
                    }
                    break;
                }

                default:
                    fail (Error_Parse_Rule());
                }
            }
            else if (Is_Block(rule)) {
                bool interrupted;
                if (Subparse_Throws(
                    &interrupted,
                    P_CELL,
                    P_INPUT_VALUE,
                    SPECIFIED,
                    rule,
                    P_RULE_SPECIFIER,
                    (P_FLAGS & PF_FIND_MASK) | (P_FLAGS & PF_REDBOL)
                )) {
                    Copy_Cell(P_OUT, P_CELL);
                    return BOUNCE_THROWN;
                }

                // Non-breaking out of loop instances of match or not.

                if (Is_Nulled(P_CELL))
                    i = END_FLAG;
                else {
                    assert(Is_Integer(P_CELL));
                    i = VAL_INT32(P_CELL);
                }

                if (interrupted) { // ACCEPT or REJECT ran
                    assert(i != THROWN_FLAG);
                    if (i == END_FLAG)
                        P_POS = NOT_FOUND;
                    else
                        P_POS = cast(REBLEN, i);
                    break;
                }
            }
            else if (false) {
              handle_end:
                count = 0;
                i = (P_POS < Flex_Len(P_INPUT))
                    ? END_FLAG
                    : Flex_Len(P_INPUT);
            }
            else {
                // Parse according to datatype

                if (Is_Flex_Array(P_INPUT))
                    i = Parse_Array_One_Rule(L, rule);
                else
                    i = Parse_String_One_Rule(L, rule);

                // i may be THROWN_FLAG
            }

            if (i == THROWN_FLAG)
                return BOUNCE_THROWN;

            // Necessary for special cases like: some [to <end>]
            // i: indicates new index or failure of the match, but
            // that does not mean failure of the rule, because optional
            // matches can still succeed, if if the last match failed.
            //
            if (i != END_FLAG) {
                count++; // may overflow to negative

                if (count < 0)
                    count = INT32_MAX; // the forever case

                // !!! This code pertained to a characteristic of R3-Alpha's
                // SOME and ANY which required advancement.  WHILE was needed
                // for a rule to succeed even if it did not advance, which
                // meant it had to be used with things like REMOVE.  Modern
                // UPARSE does not have an implicit advancement rule, it is
                // done explicitly with FURTHER.  Patching bootstrap PARSE3
                // to support FURTHER wouldn't be hard, but it does not seem
                // to be necessary...instead just removing the progress
                // requirement.  Review the question.
                //
                if (P_FLAGS & PF_REDBOL) {
                    if (i == P_POS and not (P_FLAGS & PF_WHILE)) {
                        if (count < mincount) {
                            P_POS = NOT_FOUND; // was not enough
                        }
                        break;
                    }
                }
            }
            else {
                if (count < mincount) {
                    P_POS = NOT_FOUND; // was not enough
                }
                else if (i != END_FLAG) {
                    P_POS = cast(REBLEN, i);
                }
                else {
                    // just keep index as is.
                }
                break;
            }
            P_POS = cast(REBLEN, i);
        }

        if (P_POS > Flex_Len(P_INPUT))
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
        if (P_FLAGS & PF_STATE_MASK) {
            if (P_FLAGS & PF_NOT) {
                if ((P_FLAGS & PF_NOT2) and P_POS != NOT_FOUND)
                    P_POS = NOT_FOUND;
                else
                    P_POS = begin;
            }

            if (P_POS == NOT_FOUND) {
                if (P_FLAGS & PF_THEN) {
                    FETCH_TO_BAR_OR_END(L);
                    if (NOT_END(P_RULE))
                        FETCH_NEXT_RULE(L);
                }
            }
            else {
                // Set count to how much input was advanced
                //
                count = (begin > P_POS) ? 0 : P_POS - begin;

                if (P_FLAGS & PF_COPY) {
                    DECLARE_VALUE (temp);
                    if (Any_List(P_INPUT_VALUE)) {
                        Init_Any_List(
                            temp,
                            P_TYPE,
                            Copy_Array_At_Max_Shallow(
                                cast_Array(P_INPUT),
                                begin,
                                P_INPUT_SPECIFIER,
                                count
                            )
                        );
                    }
                    else if (Is_Binary(P_INPUT_VALUE)) {
                        Init_Blob(
                            temp,
                            Copy_Sequence_At_Len(P_INPUT, begin, count)
                        );
                    }
                    else {
                        assert(Any_String(P_INPUT_VALUE));

                        DECLARE_VALUE (begin_val);
                        Init_Any_Series_At(begin_val, P_TYPE, P_INPUT, begin);

                        Init_Any_Series(
                            temp,
                            P_TYPE,
                            Copy_String_At_Len(begin_val, count)
                        );
                    }

                    Copy_Cell(
                        Sink_Var_May_Fail(set_or_copy_word, P_RULE_SPECIFIER),
                        temp
                    );
                }
                else if (P_FLAGS & PF_SET) {
                    if (Is_Flex_Array(P_INPUT)) {
                        if (count != 0)
                            Derelativize(
                                Sink_Var_May_Fail(
                                    set_or_copy_word, P_RULE_SPECIFIER
                                ),
                                Array_At(cast_Array(P_INPUT), begin),
                                P_INPUT_SPECIFIER
                            );
                        else
                            NOOP; // !!! leave as-is on 0 count?
                    }
                    else {
                        if (count != 0) {
                            Value* var = Sink_Var_May_Fail(
                                set_or_copy_word, P_RULE_SPECIFIER
                            );
                            Ucs2Unit ch = GET_ANY_CHAR(P_INPUT, begin);
                            if (P_TYPE == TYPE_BINARY)
                                Init_Integer(var, ch);
                            else
                                Init_Char(var, ch);
                        }
                        else
                            NOOP; // !!! leave as-is on 0 count?
                    }
                }

                if (P_FLAGS & PF_REMOVE) {
                    Fail_If_Read_Only_Flex(P_INPUT);
                    if (count) Remove_Flex(P_INPUT, begin, count);
                    P_POS = begin;
                }

                if (P_FLAGS & (PF_INSERT | PF_CHANGE)) {
                    Fail_If_Read_Only_Flex(P_INPUT);
                    count = (P_FLAGS & PF_INSERT) ? 0 : count;
                    bool only = false;

                    if (IS_END(L->value))
                        fail (Error_Parse_End());

                    if (Is_Word(P_RULE)) { // check for ONLY flag
                        Option(SymId) cmd = VAL_CMD(P_RULE);
                        if (cmd) {
                            switch (cmd) {
                              case SYM_ONLY:
                                only = true;
                                FETCH_NEXT_RULE(L);
                                if (IS_END(P_RULE))
                                    fail (Error_Parse_End());
                                break;

                              default: // other cmds invalid after INSERT/CHANGE
                                fail (Error_Parse_Rule());
                            }
                        }
                    }

                    // new value...comment said "CHECK FOR QUOTE!!"
                    rule = Get_Parse_Value(save, P_RULE, P_RULE_SPECIFIER);
                    FETCH_NEXT_RULE(L);

                    // If a GROUP!, then execute it first.  See #1279
                    //
                    DECLARE_VALUE (evaluated);
                    if (Is_Group(rule)) {
                        Specifier* derived = Derive_Specifier(
                            P_RULE_SPECIFIER,
                            rule
                        );
                        if (Eval_Array_At_Throws(
                            evaluated,
                            Cell_Array(rule),
                            VAL_INDEX(rule),
                            derived
                        )) {
                            Copy_Cell(P_OUT, evaluated);
                            return BOUNCE_THROWN;
                        }

                        rule = evaluated;
                    }

                    if (Is_Flex_Array(P_INPUT)) {
                        REBLEN mod_flags = (P_FLAGS & PF_INSERT) ? 0 : AM_PART;
                        if (
                            not only and
                            Splices_Into_Type_Without_Only(P_TYPE, rule)
                        ){
                            mod_flags |= AM_SPLICE;
                        }
                        P_POS = Modify_Array(
                            (P_FLAGS & PF_CHANGE)
                                ? SYM_CHANGE
                                : SYM_INSERT,
                            cast_Array(P_INPUT),
                            begin,
                            rule,
                            mod_flags,
                            count,
                            1
                        );

                        if (Is_Lit_Word(rule))
                            CHANGE_VAL_TYPE_BITS( // keeps binding flags
                                Array_At(cast_Array(P_INPUT), P_POS - 1),
                                TYPE_WORD
                            );
                    }
                    else {
                        P_POS = begin;

                        REBLEN mod_flags = (P_FLAGS & PF_INSERT) ? 0 : AM_PART;

                        if (P_TYPE == TYPE_BINARY)
                            P_POS = Modify_Binary(
                                P_INPUT_VALUE,
                                (P_FLAGS & PF_CHANGE)
                                    ? SYM_CHANGE
                                    : SYM_INSERT,
                                rule,
                                mod_flags,
                                count,
                                1
                            );
                        else {
                            P_POS = Modify_String(
                                P_INPUT_VALUE,
                                (P_FLAGS & PF_CHANGE)
                                    ? SYM_CHANGE
                                    : SYM_INSERT,
                                rule,
                                mod_flags,
                                count,
                                1
                            );
                        }
                    }
                }

                if (P_FLAGS & PF_AHEAD)
                    P_POS = begin;
            }

            P_FLAGS &= ~(PF_STATE_MASK);
            set_or_copy_word = nullptr;
        }

        if (P_POS == NOT_FOUND) {
            //
            // If a rule fails but "falls through", there may still be other
            // options later in the block to consider separated by |.

            FETCH_TO_BAR_OR_END(L);
            if (IS_END(P_RULE)) // no alternate rule
                return Init_Nulled(OUT);

            // Jump to the alternate rule and reset input
            //
            FETCH_NEXT_RULE(L);
            P_POS = begin = start;
        }

        begin = P_POS;
        mincount = maxcount = 1;
    }

    return Init_Integer(OUT, P_POS); // !!! return switched input series??
}


//
//  parse: native [
//
//  "Parse series according to grammar rules"
//
//      return: "Input series if /MATCH, otherwise synthesized result"  ; [1]
//          [any-value!]
//      input "Input series to parse"
//          [<maybe> any-series!]
//      rules "Rules to parse by"
//          [<maybe> block!]
//      /case "Uses case-sensitive comparison"
//      /match "Return PARSE input instead of synthesized result"
//      /redbol "Use Rebol2/Red-style rules vs. UPARSE-style rules"
//  ]
//
DECLARE_NATIVE(PARSE)
//
// 1. In modern Ren-C beyond this bootstrap branch, PARSE is designed to
//    extract and synthesize results, e.g.:
//
//        >> parse "bbb" [some "a" (1) | some "b" (2)]
//        == 2
//
//    This isn't supported by the R3-Alpha parse design, and it won't be
//    retrofitted to get it.  But to be interface-compatible, it returns a
//    trash value or it raises an error.
//
//    Shifting it into /MATCH mode will return the input or null.
{
    INCLUDE_PARAMS_OF_PARSE;

    Value* input = ARG(INPUT);
    Value* rules = ARG(RULES);

    bool interrupted;
    if (Subparse_Throws(
        &interrupted,
        OUT,
        ARG(INPUT),
        SPECIFIED, // input is a non-relative Value
        rules,
        SPECIFIED, // rules is a non-relative Value
        (Bool_ARG(CASE) or Is_Binary(ARG(INPUT)) ? AM_FIND_CASE : 0)
            | (Bool_ARG(REDBOL) ? PF_REDBOL : 0)
        //
        // We always want "case-sensitivity" on binary bytes, vs. treating
        // as case-insensitive bytes for ASCII characters.
    )){
        // Any PARSE-specific THROWs (where a PARSE directive jumped the
        // stack) should be handled here.  ACCEPT is one example.

        assert(THROWN(OUT));
        if (Is_Action(OUT)) {
            if (
                VAL_ACTION(OUT) == NAT_ACTION(PARSE_ACCEPT)
                // !!! no binding check that it's this parse, not definitional
                // (but it's an outdated PARSE so not worth worrying about)
            ){
                CATCH_THROWN(OUT, OUT);
                return OUT;
            }
        }

        return BOUNCE_THROWN;
    }

    if (Is_Nulled(OUT)) {
        if (Bool_ARG(MATCH))
            return nullptr;
        fail (Error_Parse_Mismatch_Raw(rules));
    }

    REBLEN progress = VAL_UINT32(OUT);
    assert(progress <= VAL_LEN_HEAD(input));
    if (progress < VAL_LEN_HEAD(input)) {
        if (Bool_ARG(MATCH))
            return nullptr;
        fail (Error_Parse_Incomplete_Raw(rules));
    }

    if (Bool_ARG(MATCH))
        return Copy_Cell(OUT, input);

    return Init_Trash(OUT);  // should be synthesized value, see [1]
}


//
//  parse-accept: native [
//
//  "Accept a value as parse product (Internal Implementation Detail ATM)."
//
//  ]
//
DECLARE_NATIVE(PARSE_ACCEPT)
//
// !!! This was not created for user usage, but rather as a label for the
// internal throw used to indicate "accept".
{
    UNUSED(level_);
    fail ("PARSE-ACCEPT is for internal PARSE use only");
}


//
//  parse-reject: native [
//
//  "Reject the current parse rule (Internal Implementation Detail ATM)."
//
//  ]
//
DECLARE_NATIVE(PARSE_REJECT)
//
// !!! This was not created for user usage, but rather as a label for the
// internal throw used to indicate "reject".
{
    UNUSED(level_);
    fail ("PARSE-REJECT is for internal PARSE use only");
}


//
//  parse-break: native [
//
//  "Break the current parse loop (Internal Implementation Detail ATM)."
//
//  ]
//
DECLARE_NATIVE(PARSE_BREAK)
//
// !!! This was not created for user usage, but rather as a label for the
// internal throw used to indicate "break".
{
    UNUSED(level_);
    fail ("PARSE-BREAK is for internal PARSE use only");
}
