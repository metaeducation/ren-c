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

#define P_INPUT_VALUE       (f->rootvar + 1)
#define P_TYPE              VAL_TYPE(P_INPUT_VALUE)
#define P_INPUT             VAL_SERIES(P_INPUT_VALUE)
#define P_INPUT_SPECIFIER   VAL_SPECIFIER(P_INPUT_VALUE)
#define P_POS               VAL_INDEX(P_INPUT_VALUE)

#define P_FIND_FLAGS        VAL_INT64(f->rootvar + 2)
#define P_HAS_CASE          (did (P_FIND_FLAGS & AM_FIND_CASE))

#define P_NUM_QUOTES_VALUE  (f->rootvar + 3)
#define P_NUM_QUOTES        VAL_INT32(P_NUM_QUOTES_VALUE)


#define P_OUT (f->out)

#define P_CELL FRM_CELL(f)

// !!! R3-Alpha's PARSE code long predated frames, and was retrofitted to use
// them as an experiment in Ren-C.  If it followed the rules of frames, then
// what is seen in a lookback is only good for *one* unit of time and may be
// invalid after that.  It takes several observations and goes back expecting
// a word to be in the same condition, so it can't use opt_lookback yet.
//
#define FETCH_NEXT_RULE_KEEP_LAST(opt_lookback,f) \
    *opt_lookback = P_RULE; \
    Fetch_Next_In_Frame(nullptr, (f))

#define FETCH_NEXT_RULE(f) \
    Fetch_Next_In_Frame(nullptr, (f))

inline static bool IS_BAR(const RELVAL *v)
    { return IS_WORD(v) and VAL_WORD_SYM(v) == SYM_BAR; }

#define FETCH_TO_BAR_OR_END(f) \
    while (NOT_END(f->value) and not IS_BAR(P_RULE)) \
        { FETCH_NEXT_RULE(f); }


//
// See the notes on `flags` in the main parse loop for how these work.
//
enum parse_flags {
    PF_SET = 1 << 0,
    PF_COPY = 1 << 1,
    PF_NOT = 1 << 2,
    PF_NOT2 = 1 << 3,
    PF_THEN = 1 << 4,
    PF_AHEAD = 1 << 5,
    PF_REMOVE = 1 << 6,
    PF_INSERT = 1 << 7,
    PF_CHANGE = 1 << 8,
    PF_WHILE = 1 << 9
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
    if (sym >= SYM_SET and sym <= SYM_END)
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
static bool Subparse_Throws(
    bool *interrupted_out,
    REBVAL *out,
    RELVAL *input,
    REBSPC *input_specifier,
    const RELVAL *rules,
    REBSPC *rules_specifier,
    REBCNT find_flags
){
    assert(IS_BLOCK(rules));
    assert(ANY_SERIES_OR_PATH_KIND(CELL_KIND(VAL_UNESCAPED(input))));

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
        Init_Integer(out, VAL_INDEX(VAL_UNESCAPED(input)));
        return false;
    }

    DECLARE_FRAME (f);

    SET_END(out);
    f->out = out;

    f->gotten = nullptr;
    SET_FRAME_VALUE(f, VAL_ARRAY_AT(rules)); // not an END due to test above
    f->specifier = Derive_Specifier(rules_specifier, rules);

    f->feed->vaptr = nullptr;
    f->feed->array = VAL_ARRAY(rules);
    f->feed->flags.bits = FEED_MASK_DEFAULT;
    f->feed->index = VAL_INDEX(rules) + 1;
    f->feed->pending = f->value + 1;

    f->flags.bits = DO_MASK_DEFAULT; // terminates f->cell

    Push_Frame_Core(f); // checks for C stack overflow
    Reuse_Varlist_If_Available(f);
    Push_Action(f, NAT_ACTION(subparse), UNBOUND);

    Begin_Action(f, Canon(SYM_SUBPARSE));

    f->param = END_NODE; // informs infix lookahead
    f->arg = m_cast(REBVAL*, END_NODE);
    assert(f->refine == ORDINARY_ARG); // Begin_Action() sets
    f->special = END_NODE;

    Derelativize(Prep_Stack_Cell(f->rootvar + 1), input, input_specifier);

    // We always want "case-sensitivity" on binary bytes, vs. treating as
    // case-insensitive bytes for ASCII characters.
    //
    Init_Integer(Prep_Stack_Cell(f->rootvar + 2), find_flags);

    // Need to track NUM-QUOTES somewhere that it can be read from the frame
    //
    Init_Nulled(Prep_Stack_Cell(f->rootvar + 3));

  #if defined(NDEBUG)
    assert(ACT_NUM_PARAMS(NAT_ACTION(subparse)) == 3); // elides RETURN:
  #else
    assert(ACT_NUM_PARAMS(NAT_ACTION(subparse)) == 4); // checks RETURN:
    Init_Nulled(Prep_Stack_Cell(f->rootvar + 4));
  #endif

    // !!! By calling the subparse native here directly from its C function
    // vs. going through the evaluator, we don't get the opportunity to do
    // things like HIJACK it.  Consider APPLY-ing it.
    //
    const REBVAL *r = N_subparse(f);
    assert(NOT_END(out));

    Drop_Action(f);
    Drop_Frame(f);

    if (r == R_THROWN) {
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
        const REBVAL *label = VAL_THROWN_LABEL(out);
        if (IS_ACTION(label)) {
            if (VAL_ACTION(label) == NAT_ACTION(parse_reject)) {
                CATCH_THROWN(out, out);
                assert(IS_NULLED(out));
                *interrupted_out = true;
                return false;
            }

            if (VAL_ACTION(label) == NAT_ACTION(parse_accept)) {
                CATCH_THROWN(out, out);
                assert(IS_INTEGER(out));
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


// Very generic error.  Used to be parameterized with the parse rule in
// question, but now the `where` at the time of failure will indicate the
// location in the parse dialect that's the problem.
//
static REBCTX *Error_Parse_Rule(void) {
    return Error_Parse_Rule_Raw();
}


// Also generic.
//
static REBCTX *Error_Parse_End(void) {
    return Error_Parse_End_Raw();
}


static void Print_Parse_Index(REBFRM *f) {
    DECLARE_LOCAL (input);
    Init_Any_Series_At_Core(
        input,
        P_TYPE,
        P_INPUT,
        P_POS,
        IS_SER_ARRAY(P_INPUT)
            ? P_INPUT_SPECIFIER
            : SPECIFIED
    );

    // Either the rules or the data could be positioned at the end.  The
    // data might even be past the end.
    //
    // !!! Or does PARSE adjust to ensure it never is past the end, e.g.
    // when seeking a position given in a variable or modifying?
    //
    if (IS_END(f->value)) {
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
    const RELVAL *any_series,
    REBSPC *specifier
){
    if (any_series != P_INPUT_VALUE) // we may just be checking, not setting
        Derelativize(P_INPUT_VALUE, any_series, specifier);

    // If the input is quoted, e.g. `parse lit ''''[...] [rules]`, we dequote
    // it while we are processing the ARG().  This is because we are trying
    // to update and maintain the value as we work in a way that can be shown
    // in the debug stack frame.  Calling VAL_UNESCAPED() constantly would be
    // slower, and also gives back a const value which may be shared with
    // other quoted instances, so we couldn't update the VAL_INDEX() directly.
    //
    // But we save the number of quotes in a local variable.  This way we can
    // put the quotes back on whenever doing a COPY etc.
    //
    Init_Integer(P_NUM_QUOTES_VALUE, VAL_NUM_QUOTES(P_INPUT_VALUE));
    Dequotify(P_INPUT_VALUE);
    if (not ANY_SERIES_OR_PATH(P_INPUT_VALUE)) // #1263
        fail (Error_Parse_Series_Raw(P_INPUT_VALUE));

    if (VAL_INDEX(P_INPUT_VALUE) > VAL_LEN_HEAD(P_INPUT_VALUE))
        VAL_INDEX(P_INPUT_VALUE) = VAL_LEN_HEAD(P_INPUT_VALUE);

    if (IS_BINARY(P_INPUT_VALUE) || (P_FIND_FLAGS & AM_FIND_CASE))
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
){
    if (IS_BAR(rule))
        return rule;

    if (IS_WORD(rule)) {
        if (VAL_CMD(rule))
            return rule;

        Move_Opt_Var_May_Fail(cell, rule, specifier);
        if (IS_NULLED(cell))
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

        if (IS_NULLED(cell))
            fail (Error_No_Value_Core(rule, specifier));

        return cell;
    }

    return rule;
}


//
//  Process_Group_For_Parse: C
//
// Historically a single group in PARSE ran code, discarding the value (with
// a few exceptions when appearing in an argument position to a rule).  Ren-C
// adds another behavior for GET-GROUP!, e.g. :(...).  This makes them act
// like a COMPOSE/ONLY that runs each time they are visited.
//
REB_R Process_Group_For_Parse(
    REBFRM *f,
    REBVAL *cell,
    const RELVAL *group
){
  #if 0
    // !!! Unfortunately, the bootstrap process still loads source and molds
    // it out.  This dependency needs to go away.  Until it does, GET-GROUP!
    // can't be used in mezzanine code...so doubled groups remain as an
    // alternative expression.
    //
    if (Is_Doubled_Group(group)) {
      #if !defined(NDEBUG)
        PROBE(group);
      #endif
        fail ("Doubled group behavior is now performed by GET-GROUP!");
    }
  #endif

    assert(IS_GROUP(group) or IS_GET_GROUP(group));
    REBSPC *derived = Derive_Specifier(P_RULE_SPECIFIER, group);

    // Evaluator should optimize execution of a GROUP! with only one element.
    //
    if (Do_At_Throws(cell, VAL_ARRAY(group), VAL_INDEX(group), derived))
        return R_THROWN;

    // !!! The input is not locked from modification by agents other than the
    // PARSE's own REMOVE/etc.  This is a sketchy idea, but as long as it's
    // allowed, each time arbitrary user code runs, rules have to be adjusted
    //
    if (P_POS > SER_LEN(P_INPUT))
        P_POS = SER_LEN(P_INPUT);

    if (
        IS_NULLED(cell) // even for GET-GROUP!, null evals are discarded
        or not (
            IS_GET_GROUP(group) // plain groups always discard
            or Is_Doubled_Group(group) // !!! Temp hack, see above
        )
    ){
        return R_INVISIBLE;
    }

    return cell;
}


//
//  Parse_One_Rule: C
//
// Used for parsing ANY-SERIES! to match the next rule in the ruleset.  If it
// matches, return the index just past it.
//
// This function is also called by To_Thru, consequently it may need to
// process elements other than the current one in the frame.  Hence it
// is parameterized by an arbitrary `pos` instead of assuming the P_POS
// that is held by the frame.
//
// The return result is either an int position, END_FLAG, or THROWN_FLAG
// Only in the case of THROWN_FLAG will f->out (aka P_OUT) be affected.
// Otherwise, it should exit the routine as an END marker (as it started);
//
static REBIXO Parse_One_Rule(
    REBFRM *f,
    REBCNT pos,
    const RELVAL *rule
){
    assert(IS_END(P_OUT));

    if (IS_GROUP(rule) or IS_GET_GROUP(rule)) {
        rule = Process_Group_For_Parse(f, P_CELL, rule);
        if (rule == R_THROWN) {
            Move_Value(P_OUT, P_CELL);
            return THROWN_FLAG;
        }
        if (rule == R_INVISIBLE) { // !!! Should this be legal?
            assert(pos <= SER_LEN(P_INPUT)); // !!! Process_Group ensures
            return pos;
        }
        // was a GET-GROUP! :(...), use result as rule
    }

    if (P_POS == SER_LEN(P_INPUT)) { // at end of input
        if (IS_BLANK(rule) or IS_LOGIC(rule) or IS_BLOCK(rule)) {
            //
            // Only these types can *potentially* handle an END input.
            // For instance, `parse [] [[[_ _ _]]]` should be able to match,
            // but we have to process the block to know for sure.
        }
        else
            return END_FLAG; // Other cases below can assert if item is END
    }

    switch (KIND_BYTE(rule)) { // handle rules w/same behavior for all P_INPUT
      case REB_BLANK:
        return pos; // blank rules "match" but don't affect the parse position

      case REB_LOGIC: // true matches always, false matches never
        return VAL_LOGIC(rule) ? pos : END_FLAG;

      case REB_INTEGER:
        fail ("Non-rule-count INTEGER! in PARSE must be literal, use QUOTE");

      case REB_BLOCK: {
        //
        // Process a subrule.  The subrule will run in its own frame, so it
        // will not change P_POS directly (it will have its own P_INPUT_VALUE)
        // Hence the return value regarding whether a match occurred or not
        // has to be based on the result that comes back in P_OUT.

        REBCNT pos_before = P_POS;
        P_POS = pos; // modify input position

        DECLARE_LOCAL (subresult);
        bool interrupted;
        if (Subparse_Throws(
            &interrupted,
            subresult,
            P_INPUT_VALUE, // affected by P_POS assignment above
            SPECIFIED,
            rule,
            P_RULE_SPECIFIER,
            P_FIND_FLAGS
        )){
            Move_Value(P_OUT, subresult);
            return THROWN_FLAG;
        }

        UNUSED(interrupted); // !!! ignore "interrupted" (ACCEPT or REJECT?)

        P_POS = pos_before; // restore input position

        if (IS_NULLED(subresult))
            return END_FLAG;

        REBINT index = VAL_INT32(subresult);
        assert(index >= 0);
        return cast(REBCNT, index); }

      default:;
        // Other cases handled distinctly between blocks/strings/binaries...
    }

    if (IS_SER_ARRAY(P_INPUT)) {
        REBARR *arr = ARR(P_INPUT);
        RELVAL *item = ARR_AT(arr, pos);

        if (Trace_Level) {
            Trace_Value("input", rule);
            if (IS_END(item)) {
                const char *end_str = "** END **";
                Trace_String(cb_cast(end_str), strlen(end_str));
            }
            else
                Trace_Value("match", item);
        }

        switch (VAL_TYPE(rule)) {
          case REB_QUOTED:
            Derelativize(P_CELL, rule, P_RULE_SPECIFIER);
            rule = Unquotify(P_CELL, 1);
            break; // fall through to direct match

          case REB_DATATYPE:
            if (VAL_TYPE(item) == VAL_TYPE_KIND(rule))
                return pos + 1; // specific datatype match
            return END_FLAG;

          case REB_TYPESET:
            if (TYPE_CHECK(rule, VAL_TYPE(item)))
                return pos + 1; // type was found in the typeset
            return END_FLAG;

          case REB_WORD:
            if (VAL_WORD_SYM(rule) == SYM_LIT_WORD_X) { // hack for lit-word!
                if (IS_QUOTED_WORD(item))
                    return pos + 1;
                return END_FLAG;
            }
            if (VAL_WORD_SYM(rule) == SYM_LIT_PATH_X) { // hack for lit-path!
                if (IS_QUOTED_PATH(item))
                    return pos + 1;
                return END_FLAG;
            }
            if (VAL_WORD_SYM(rule) == SYM_REFINEMENT_X) { // another hack...
                if (IS_REFINEMENT(item))
                    return pos + 1;
                return END_FLAG;
            }
            fail (Error_Parse_Rule());

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
    else {
        REBCNT flags = P_FIND_FLAGS | AM_FIND_MATCH | AM_FIND_TAIL;

        if (Trace_Level) {
            Trace_Value("match", rule);

            // !!! This used STR_AT (obsolete) but it's not clear that this is
            // necessarily a byte sized series.  Switched to BIN_AT, which
            // will assert if it's not BYTE_SIZE()

            Trace_String(BIN_AT(P_INPUT, pos), BIN_LEN(P_INPUT) - pos);
        }

        switch (VAL_TYPE(rule)) {
          case REB_CHAR:
            //
            // Try matching character against current string parse position
            //
            if (P_HAS_CASE) {
                if (VAL_CHAR(rule) == GET_ANY_CHAR(P_INPUT, pos))
                    return pos + 1;
            }
            else {
                if (
                    UP_CASE(VAL_CHAR(rule))
                    == UP_CASE(GET_ANY_CHAR(P_INPUT, pos))
                ) {
                    return pos + 1;
                }
            }
            return END_FLAG;

          case REB_EMAIL:
          case REB_TEXT:
          case REB_BINARY: {
            REBCNT index = Find_Str_Str(
                P_INPUT,
                0,
                pos,
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
                pos,
                SER_LEN(P_INPUT),
                1,
                formed,
                0,
                SER_LEN(formed),
                flags
            );
            Free_Unmanaged_Series(formed);
            if (index == NOT_FOUND)
                return END_FLAG;
            return index; }

          case REB_BITSET:
            //
            // Check the current character against a character set, advance matches
            //
            if (Check_Bit(
                VAL_SERIES(rule), GET_ANY_CHAR(P_INPUT, pos), not P_HAS_CASE
            )){
                return pos + 1;
            }
            return END_FLAG;

          default:
            fail (Error_Parse_Rule());
        }
    }
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
    bool is_thru
) {
    DECLARE_LOCAL (cell); // holds evaluated rules (use frame cell instead?)

    REBCNT pos = P_POS;
    for (; pos <= SER_LEN(P_INPUT); ++pos) {
        const RELVAL *blk = VAL_ARRAY_HEAD(rule_block);
        for (; NOT_END(blk); blk++) {
            if (IS_BAR(blk))
                fail (Error_Parse_Rule()); // !!! Shouldn't `TO [|]` succeed?

            const RELVAL *rule;
            if (not (IS_GROUP(blk) or IS_GET_GROUP(blk)))
                rule = blk;
            else {
                rule = Process_Group_For_Parse(f, cell, blk);
                if (rule == R_THROWN) {
                    Move_Value(P_OUT, cell);
                    return THROWN_FLAG;
                }
                if (rule == R_INVISIBLE)
                    continue;
            }

            if (IS_WORD(rule)) {
                REBSYM cmd = VAL_CMD(rule);

                if (cmd != SYM_0) {
                    if (cmd == SYM_END) {
                        if (pos >= SER_LEN(P_INPUT))
                            return SER_LEN(P_INPUT);
                        goto next_alternate_rule;
                    }
                    else if (
                        cmd == SYM_LIT or cmd == SYM_LITERAL
                        or cmd == SYM_QUOTE // temporarily same for bootstrap
                    ){
                        rule = ++blk; // next rule is the literal value
                        if (IS_END(rule))
                            fail (Error_Parse_Rule());
                    }
                    else
                        fail (Error_Parse_Rule());
                }
                else {
                    Move_Opt_Var_May_Fail(cell, rule, P_RULE_SPECIFIER);
                    rule = cell;
                }
            }
            else if (IS_PATH(rule))
                rule = Get_Parse_Value(cell, rule, P_RULE_SPECIFIER);

            // Try to match it:
            if (ANY_ARRAY_OR_PATH_KIND(P_TYPE)) {
                if (ANY_ARRAY(rule))
                    fail (Error_Parse_Rule());

                REBIXO i = Parse_One_Rule(f, pos, rule);
                if (i == THROWN_FLAG)
                    return THROWN_FLAG;

                if (i != END_FLAG) {
                    pos = cast(REBCNT, i); // passed it, so back up if only TO
                    if (is_thru)
                        return pos; // don't back up
                    return pos - 1; // back up
                }
            }
            else if (P_TYPE == REB_BINARY) {
                REBYTE ch1 = *BIN_AT(P_INPUT, pos);

                // Handle special string types:
                if (IS_CHAR(rule)) {
                    if (VAL_CHAR(rule) > 0xff)
                        fail (Error_Parse_Rule());

                    if (ch1 == VAL_CHAR(rule)) {
                        if (is_thru)
                            return pos + 1;
                        return pos;
                    }
                }
                else if (IS_BINARY(rule)) {
                    if (ch1 == *VAL_BIN_AT(rule)) {
                        REBCNT len = VAL_LEN_AT(rule);
                        if (len == 1) {
                            if (is_thru)
                                return pos + 1;
                            return pos;
                        }

                        if (0 == Compare_Bytes(
                            BIN_AT(P_INPUT, pos),
                            VAL_BIN_AT(rule),
                            len,
                            false
                        )) {
                            if (is_thru)
                                return pos + 1;
                            return pos;
                        }
                    }
                }
                else if (IS_INTEGER(rule)) {
                    if (VAL_INT64(rule) > 0xff)
                        fail (Error_Parse_Rule());

                    if (ch1 == VAL_INT32(rule)) {
                        if (is_thru)
                            return pos + 1;
                        return pos;
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
                        if (is_thru)
                            return pos + 1;
                        return pos;
                    }
                }
                // bitset
                else if (IS_BITSET(rule)) {
                    if (Check_Bit(VAL_SERIES(rule), ch, not P_HAS_CASE)) {
                        if (is_thru)
                            return pos + 1;
                        return pos;
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
                        Free_Unmanaged_Series(formed);
                        if (i != NOT_FOUND) {
                            if (is_thru)
                                return pos + len;
                            return pos;
                        }
                    }
                }
                else if (ANY_STRING(rule)) {
                    REBUNI ch2 = VAL_ANY_CHAR(rule);
                    if (!P_HAS_CASE) ch2 = UP_CASE(ch2);

                    if (ch == ch2) {
                        REBCNT len = VAL_LEN_AT(rule);
                        if (len == 1) {
                            if (is_thru)
                                return pos + 1;
                            return pos;
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
                            if (is_thru)
                                return i + len;
                            return i;
                        }
                    }
                }
                else if (IS_INTEGER(rule)) {
                    if (ch_unadjusted == cast(REBUNI, VAL_INT32(rule))) {
                        if (is_thru)
                            return pos + 1;
                        return pos;
                    }
                }
                else
                    fail (Error_Parse_Rule());
            }

          next_alternate_rule:; // alternates are BAR! separated `[a | b | c]`

            do {
                ++blk;
                if (IS_END(blk))
                    goto next_input_position;
            } while (not IS_BAR(blk));
        }

      next_input_position:; // not matched yet, so keep trying to go THRU or TO
    }
    return END_FLAG;
}


//
//  To_Thru_Non_Block_Rule: C
//
// There's a high-level split between block and non-block rule processing,
// as blocks are the common case.
//
static REBIXO To_Thru_Non_Block_Rule(
    REBFRM *f,
    const RELVAL *rule,
    bool is_thru
) {
    assert(not IS_BLOCK(rule));

    if (IS_BLANK(rule))
        return P_POS; // make it a no-op

    if (IS_LOGIC(rule)) // no-op if true, match failure if false
        return VAL_LOGIC(rule) ? P_POS : END_FLAG;

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
        REBCNT i = cast(REBCNT, Int32(rule)) - (is_thru ? 0 : 1);
        if (i > SER_LEN(P_INPUT))
            return SER_LEN(P_INPUT);
        return i;
    }

    if (IS_WORD(rule) and VAL_WORD_SYM(rule) == SYM_END) {
        //
        // `TO/THRU END` JUMPS TO END INPUT SERIES (ANY SERIES TYPE)
        //
        return SER_LEN(P_INPUT);
    }

    if (IS_SER_ARRAY(P_INPUT)) {
        //
        // FOR ARRAY INPUT WITH NON-BLOCK RULES, USE Find_In_Array()
        //
        // !!! This adjusts it to search for non-literal words, but are there
        // other considerations for how non-block rules act with array input?
        //
        REBFLGS flags = P_HAS_CASE ? AM_FIND_CASE : 0;
        DECLARE_LOCAL (temp);
        if (IS_QUOTED(rule)) { // make `'[foo bar]` match `[foo bar]`
            Derelativize(temp, rule, P_RULE_SPECIFIER);
            rule = Unquotify(temp, 1);
            flags |= AM_FIND_ONLY; // !!! Is this implied?
        }

        REBCNT i = Find_In_Array(
            ARR(P_INPUT),
            P_POS,
            SER_LEN(P_INPUT),
            rule,
            1,
            flags,
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
        if (not IS_TEXT(rule) and not IS_BINARY(rule)) {
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
            Free_Unmanaged_Series(formed);

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
// Perform an EVALAUTE on the *input* as a code block, and match the following
// rule against the evaluative result.
//
//     parse [1 + 2] [do [lit 3]] => true
//
// The rule may be in a block or inline.
//
//     parse [reverse copy "abc"] [do "cba"]
//     parse [reverse copy "abc"] [do ["cba"]]
//
// !!! Since this only does one step, it no longer corresponds to DO as a
// name, and corresponds to EVALUATE.
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
//     parse [1 + 2] [set var do [lit 3]] ;-- var gets 1, not 3
//
// Other problems arise since the caller doesn't know about the trickiness
// of this evaluation, e.g. this won't work either:
//
//     parse [1 + 2] [thru do integer!]
//
static REBIXO Do_Eval_Rule(REBFRM *f)
{
    if (not IS_SER_ARRAY(P_INPUT)) // can't be an ANY-STRING!
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
        indexor = Eval_Array_At_Core(
            P_CELL,
            nullptr, // opt_first (null indicates nothing)
            ARR(P_INPUT),
            P_POS,
            P_INPUT_SPECIFIER,
            DO_MASK_DEFAULT
        );
        if (indexor == THROWN_FLAG) { // BREAK/RETURN/QUIT/THROW...
            Move_Value(P_OUT, P_CELL);
            return THROWN_FLAG;
        }

        if (indexor != END_FLAG)
            indexor = cast(REBCNT, indexor) - 1; // 1 past

        // !!! This copies a single value into a block to use as data, because
        // parse input is matched as a series.  Can this be avoided?
        //
        holder = Alloc_Singular(SERIES_FLAGS_NONE);
        Move_Value(ARR_SINGLE(holder), P_CELL);
        Deep_Freeze_Array(holder); // don't allow modification of temporary
    }

    // We want to reuse the same frame we're in, because if you say
    // something like `parse [1 + 2] [do [lit 3]]`, the `[lit 3]` rule
    // should be consumed.  We also want to be able to use a nested rule
    // inline, such as `do skip` not only allow `do [skip]`.
    //
    // So the rules should be processed normally, it's just that for the
    // duration of the next rule the *input* is the temporary evaluative
    // result.
    //
    DECLARE_LOCAL (saved_input);
    Move_Value(saved_input, P_INPUT_VALUE); // series and P_POS position
    PUSH_GC_GUARD(saved_input);
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
    REBIXO n = Parse_One_Rule(f, P_POS, rule);
    FETCH_NEXT_RULE(f);

    // Restore the input series to what it was before parsing the temporary
    // (this restores P_POS, since it's just an alias for the input's index)
    //
    Move_Value(P_INPUT_VALUE, saved_input);
    DROP_GC_GUARD(saved_input);

    if (n == THROWN_FLAG)
        return THROWN_FLAG;

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
//      return: [<opt> integer!]
//      input [any-series! any-array! quoted!]
//      find-flags [integer!]
//      <local> num-quotes
//  ]
//
REBNATIVE(subparse)
//
// Rules are matched until one of these things happens:
//
// * A rule fails, and is not then picked up by a later "optional" rule.
// This returns NULL.
//
// * You run out of rules to apply without any failures or errors, and the
// position in the input series is returned.  This may be at the end of
// the input data or not--it's up to the caller to decide if that's relevant.
// This will return D_OUT with out containing an integer index.
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

    UNUSED(ARG(find_flags)); // used via P_FIND_FLAGS

    REBFRM *f = frame_; // nice alias of implicit native parameter

    Set_Parse_Series(f, ARG(input), SPECIFIED); // doesn't reset, just checks
    UNUSED(ARG(num_quotes)); // Set_Parse_Series sets this

    assert(IS_END(P_OUT)); // invariant provided by evaluator

  #if !defined(NDEBUG)
    //
    // These parse state variables live in chunk-stack REBVARs, which can be
    // annoying to find to inspect in the debugger.  This makes pointers into
    // the value payloads so they can be seen more easily.
    //
    const REBCNT *pos_debug = &P_POS;
    (void)pos_debug; // UNUSED() forces corruption in C++11 debug builds
  #endif

  #if defined(DEBUG_COUNT_TICKS)
    REBTCK tick = TG_Tick; // helpful to cache for visibility also
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

    while (NOT_END(f->value)) {

        /* Print_Parse_Index(f); */
        UPDATE_EXPRESSION_START(f);

      #if defined(DEBUG_COUNT_TICKS)
        ++TG_Tick;
        tick = TG_Tick;
        cast(void, tick); // suppress unused warning (but UNUSED() corrupts)
      #endif

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
        // run all GET-GROUP! `:(...)` to find them in alternates lists.

        if (IS_BAR(P_RULE)) { // reached BAR! without a match failure, good!
            //
            // Note: First test, so `[| ...anything...]` is a "no-op" match
            //
            return Init_Integer(P_OUT, P_POS); // indicate match @ current pos
        }

        const RELVAL *rule = P_RULE; // start w/rule in block, may eval/fetch

    //=//// (GROUP!) AND :(GET-GROUP!) PROCESSING /////////////////////////=//

        if (IS_GROUP(rule) or IS_GET_GROUP(rule)) {
            //
            // Code below may jump here to re-process groups, consider:
            //
            //    rule: lit (print "Hi")
            //    parse "a" [:('rule) "a"]
            //
            // First it processes the group to get RULE, then it looks that
            // up and gets another group.  In theory this could continue
            // indefinitely, but for now a GET-GROUP! can't return another.

          process_group:;

            rule = Process_Group_For_Parse(f, save, P_RULE);
            if (rule == R_THROWN) {
                Move_Value(P_OUT, save);
                return R_THROWN;
            }
            if (rule == R_INVISIBLE) { // was a (...), or null-bearing :(...)
                FETCH_NEXT_RULE(f); // ignore result and go on to next rule
                continue;
            }
            // was a GET-GROUP!, e.g. :(...), fall through so its result will
            // act as a rule in its own right.
            //
            assert(IS_SPECIFIC(rule)); // can use w/P_RULE_SPECIFIER, harmless
        }
        else {
            // If we ran the GROUP! then that invokes the evaluator, and so
            // we already gave the GC and cancellation a chance to run.  But
            // if not, we might want to do it here... (?)

            assert(Eval_Count >= 0);
            if (--Eval_Count == 0) {
                SET_END(P_CELL);

                if (Do_Signals_Throws(P_CELL)) {
                    Move_Value(P_OUT, P_CELL);
                    return R_THROWN;
                }

                assert(IS_END(P_CELL));
            }
        }

    //=//// ANY-WORD!/ANY-PATH! PROCESSING ////////////////////////////////=//

        // Some iterated rules have a parameter.  `3 into [some "a"]` will
        // actually run the INTO `rule` 3 times with the `subrule` of
        // `[some "a"]`.  Because it is iterated it is only captured the first
        // time through, nullptr indicates it's not been captured yet.
        //
        const RELVAL *subrule = nullptr;

        if (ANY_PLAIN_GET_SET_WORD(rule)) { // word!, set-word!, or get-word!

            REBSYM cmd = VAL_CMD(rule);
            if (cmd != SYM_0) {
                if (not IS_WORD(rule)) { // COPY: :THRU ...
                    DECLARE_LOCAL (non_word);
                    Derelativize(non_word, rule, P_RULE_SPECIFIER);
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
                        maxcount = INT32_MAX;
                        FETCH_NEXT_RULE(f);
                        continue;

                    case SYM_OPT:
                        mincount = 0;
                        FETCH_NEXT_RULE(f);
                        continue;

                    case SYM_COPY:
                        flags |= PF_COPY;
                        goto set_or_copy_pre_rule;
                    case SYM_SET:
                        flags |= PF_SET;
                        // falls through
                    set_or_copy_pre_rule:
                        FETCH_NEXT_RULE(f);

                        if (not (IS_WORD(P_RULE) or IS_SET_WORD(P_RULE))) {
                            DECLARE_LOCAL (bad_var);
                            Derelativize(bad_var, P_RULE, P_RULE_SPECIFIER);
                            fail (Error_Parse_Variable_Raw(bad_var));
                        }

                        if (VAL_CMD(P_RULE)) { // set set [...]
                            DECLARE_LOCAL (keyword);
                            Derelativize(keyword, P_RULE, P_RULE_SPECIFIER);
                            fail (Error_Parse_Command_Raw(keyword));
                        }

                        FETCH_NEXT_RULE_KEEP_LAST(&set_or_copy_word, f);
                        continue;

                    case SYM_NOT:
                        flags |= PF_NOT;
                        flags ^= PF_NOT2;
                        FETCH_NEXT_RULE(f);
                        continue;

                    case SYM_AND:
                    case SYM_AHEAD:
                        flags |= PF_AHEAD;
                        FETCH_NEXT_RULE(f);
                        continue;

                    case SYM_THEN:
                        flags |= PF_THEN;
                        FETCH_NEXT_RULE(f);
                        continue;

                    case SYM_REMOVE:
                        flags |= PF_REMOVE;
                        FETCH_NEXT_RULE(f);
                        continue;

                    case SYM_INSERT:
                        flags |= PF_INSERT;
                        FETCH_NEXT_RULE(f);
                        goto post_match_processing;

                    case SYM_CHANGE:
                        flags |= PF_CHANGE;
                        FETCH_NEXT_RULE(f);
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
                        thrown_arg->extra.trash = thrown_arg; // see notes

                        return Init_Thrown_With_Label(
                            P_OUT,
                            thrown_arg,
                            NAT_VALUE(parse_accept)
                        ); }

                    case SYM_REJECT: {
                        //
                        // Similarly, this is a break/continue style "throw"
                        //
                        return Init_Thrown_With_Label(
                            P_OUT,
                            NULLED_CELL,
                            NAT_VALUE(parse_reject)
                        ); }

                    case SYM_FAIL: // deprecated... use LOGIC! false instead
                        P_POS = NOT_FOUND;
                        FETCH_NEXT_RULE(f);
                        goto post_match_processing;

                    case SYM_LIMIT:
                        fail (Error_Not_Done_Raw());

                    case SYM__Q_Q:
                        Print_Parse_Index(f);
                        FETCH_NEXT_RULE(f);
                        continue;

                    case SYM_IF:
                        fail ("IF removed from PARSE, use LOGIC!");

                    case SYM_RETURN:
                        fail ("RETURN removed from PARSE, use ((THROW ...))");

                    default: //the list above should be exhaustive
                        assert(false);
                    }
                }
                // Any other cmd must be a match command, so proceed...
            }
            else {
                // It's not a PARSE command, get or set it

                // word: - set a variable to the series at current index
                if (IS_SET_WORD(rule)) {
                    //
                    // !!! Review meaning of marking the parse in a slot that
                    // is a target of a rule, e.g. `thru pos: xxx` #
                    //
                    // https://github.com/rebol/rebol-issues/issues/2269
                    //
                    // if (flags != 0) fail (Error_Parse_Rule());

                    Quotify(
                        Move_Value(
                            Sink_Var_May_Fail(rule, P_RULE_SPECIFIER),
                            P_INPUT_VALUE
                        ),
                        P_NUM_QUOTES
                    );
                    FETCH_NEXT_RULE(f);
                    continue;
                }

                // :word - change the index for the series to a new position
                if (IS_GET_WORD(rule)) {
                    Set_Parse_Series(
                        f,
                        Get_Opt_Var_May_Fail(rule, P_RULE_SPECIFIER),
                        SPECIFIED
                    );

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

                    FETCH_NEXT_RULE(f);
                    continue;
                }

                // word - some other variable
                if (IS_WORD(rule)) {
                    if (rule != save) {
                        Move_Opt_Var_May_Fail(save, rule, P_RULE_SPECIFIER);
                        rule = save;
                    }
                    if (IS_NULLED(rule))
                        fail (Error_No_Value_Core(rule, P_RULE_SPECIFIER));
                }
                else {
                    // rule can still be 'word or /word
                }
            }
        }
        else if (ANY_PATH(rule)) {
            if (IS_PATH(rule)) {
                if (Get_Path_Throws_Core(save, rule, P_RULE_SPECIFIER)) {
                    Move_Value(P_OUT, save);
                    return R_THROWN;
                }

                rule = save;
            }
            else if (IS_SET_PATH(rule)) {
                if (Set_Path_Throws_Core(
                    save, rule, P_RULE_SPECIFIER, P_INPUT_VALUE
                )){
                    Move_Value(P_OUT, save);
                    return R_THROWN;
                }

                // Nothing left to do after storing the parse position in the
                // path location...continue.
                //
                FETCH_NEXT_RULE(f);
                continue;
            }
            else if (IS_GET_PATH(rule)) {
                if (Get_Path_Throws_Core(save, rule, P_RULE_SPECIFIER)) {
                    Move_Value(P_OUT, save);
                    return R_THROWN;
                }

                Set_Parse_Series(f, save, SPECIFIED);
                FETCH_NEXT_RULE(f);
                continue;
            }

            if (P_POS > SER_LEN(P_INPUT))
                P_POS = SER_LEN(P_INPUT);
        }
        else if (IS_SET_GROUP(rule)) {
            //
            // Don't run the group yet, just hold onto it...will run and set
            // the contents (or pass found value to function as parameter)
            // only if a match happens.
            //
            FETCH_NEXT_RULE_KEEP_LAST(&set_or_copy_word, f);
            flags |= PF_SET;
            continue;
        }

        assert(not IS_NULLED(rule));

        if (IS_BAR(rule))
            fail ("BAR! must be source level (else PARSE can't skip it)");

        switch (VAL_TYPE(rule)) {
          case REB_GROUP:
            goto process_group; // GROUP! can make WORD! that fetches GROUP!

          case REB_BLANK: // no-op
            FETCH_NEXT_RULE(f);
            continue;

          case REB_LOGIC: // true is a no-op, false causes match failure
            if (VAL_LOGIC(rule)) {
                FETCH_NEXT_RULE(f);
                continue;
            }
            FETCH_NEXT_RULE(f);
            P_POS = NOT_FOUND;
            goto post_match_processing;

          case REB_INTEGER: // Specify count or range count, 1 or 2 integers
            flags |= PF_WHILE;
            mincount = maxcount = Int32s(rule, 0);

            FETCH_NEXT_RULE(f);
            if (IS_END(P_RULE))
                fail (Error_Parse_End());

            rule = Get_Parse_Value(save, P_RULE, P_RULE_SPECIFIER);

            if (IS_INTEGER(rule)) {
                maxcount = Int32s(rule, 0);

                FETCH_NEXT_RULE(f);
                if (IS_END(f->value))
                    fail (Error_Parse_End());

                rule = Get_Parse_Value(save, P_RULE, P_RULE_SPECIFIER);
            }

            if (IS_INTEGER(rule)) {
                //
                // `parse [1 1] [1 3 1]` must be `parse [1 1] [1 3 lit 1]`
                //
                fail ("For matching, INTEGER!s must be literal with QUOTE");
            }
            break;

          default:;
            // Fall through to next section
        }

    //==////////////////////////////////////////////////////////////////==//
    //
    // ITERATED RULE PROCESSING SECTION
    //
    //==////////////////////////////////////////////////////////////////==//

        // Repeats the same rule N times or until the rule fails.
        // The index is advanced and stored in a temp variable i until
        // the entire rule has been satisfied.

        FETCH_NEXT_RULE(f);

        begin = P_POS;// input at beginning of match section

        REBINT count; // gotos would cross initialization
        count = 0;
        while (count < maxcount) {
            assert(
                not IS_BAR(rule)
                and not IS_BLANK(rule)
                and not IS_LOGIC(rule)
                and not IS_INTEGER(rule)
                and not IS_GROUP(rule)
            ); // these should all have been handled before iterated section

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
                    if (IS_END(f->value))
                        fail (Error_Parse_End());

                    if (!subrule) { // capture only on iteration #1
                        subrule = Get_Parse_Value(
                            save, P_RULE, P_RULE_SPECIFIER
                        );
                        FETCH_NEXT_RULE(f);
                    }

                    bool is_thru = (cmd == SYM_THRU);

                    if (IS_BLOCK(subrule))
                        i = To_Thru_Block_Rule(f, subrule, is_thru);
                    else
                        i = To_Thru_Non_Block_Rule(f, subrule, is_thru);
                    break; }

                case SYM_QUOTE: // temporarily behaving like LIT for bootstrap
                case SYM_LITERAL:
                case SYM_LIT: {
                    if (not IS_SER_ARRAY(P_INPUT))
                        fail (Error_Parse_Rule()); // see #2253

                    if (IS_END(f->value))
                        fail (Error_Parse_End());

                    if (not subrule) // capture only on iteration #1
                        FETCH_NEXT_RULE_KEEP_LAST(&subrule, f);

                    RELVAL *cmp = ARR_AT(ARR(P_INPUT), P_POS);

                    if (IS_END(cmp))
                        i = END_FLAG;
                    else if (0 == Cmp_Value(cmp, subrule, P_HAS_CASE))
                        i = P_POS + 1;
                    else
                        i = END_FLAG;
                    break;
                }

                // !!! This is a hack to try and get some semblance of
                // compatibility in a world where 'X and 'X/Y/Z don't have
                // unique datatype "kinds", but are both QUOTED! (versions of
                // WORD! and PATH! respectively).  By making a LIT-WORD! and
                // LIT-PATH! parse rule keyword, situations can be worked
                // around, but MATCH should be used in the general case.
                //
                case SYM_LIT_WORD_X: // lit-word!
                case SYM_LIT_PATH_X: // lit-path!
                case SYM_REFINEMENT_X: // refinement!
                    i = Parse_One_Rule(f, P_POS, rule);
                    break;

                // Because there are no LIT-XXX! datatypes, a special rule
                // must be used if you want to match quoted types.  MATCH is
                // brought in to do this duty, bringing along with it the
                // features of the native.
                //
                case SYM_MATCH: {
                    if (not IS_SER_ARRAY(P_INPUT))
                        fail (Error_Parse_Rule()); // see #2253

                    if (IS_END(f->value))
                        fail (Error_Parse_End());

                    if (not subrule) // capture only on iteration #1
                        FETCH_NEXT_RULE_KEEP_LAST(&subrule, f);

                    RELVAL *cmp = ARR_AT(ARR(P_INPUT), P_POS);

                    if (IS_END(cmp))
                        i = END_FLAG;
                    else {
                        DECLARE_LOCAL (temp);
                        if (Match_Core_Throws(
                            temp,
                            subrule, P_RULE_SPECIFIER,
                            cmp, P_INPUT_SPECIFIER
                        )){
                            Move_Value(P_OUT, temp);
                            return R_THROWN;
                        }

                        if (VAL_LOGIC(temp))
                            i = P_POS + 1;
                        else
                            i = END_FLAG;
                    }
                    break; }

                case SYM_INTO: {
                    if (IS_END(f->value))
                        fail (Error_Parse_End());

                    if (!subrule) {
                        subrule = Get_Parse_Value(
                            save, P_RULE, P_RULE_SPECIFIER
                        );
                        FETCH_NEXT_RULE(f);
                    }

                    if (not IS_BLOCK(subrule))
                        fail (Error_Parse_Rule());

                    // parse ["aa"] [into ["a" "a"]] ; is legal
                    // parse "aa" [into ["a" "a"]] ; is not...already "into"
                    //
                    if (not IS_SER_ARRAY(P_INPUT))
                        fail (Error_Parse_Rule());

                    RELVAL *into = ARR_AT(ARR(P_INPUT), P_POS);

                    if (
                        IS_END(into)
                        or (
                            not ANY_BINSTR(into)
                            and not ANY_ARRAY_OR_PATH(into)
                        )
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
                        P_FIND_FLAGS
                    )) {
                        Move_Value(P_OUT, P_CELL);
                        return R_THROWN;
                    }

                    // !!! ignore interrupted? (e.g. ACCEPT or REJECT ran)

                    if (IS_NULLED(P_CELL)) {
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

                case SYM_DO: {
                    if (subrule) {
                        //
                        // Not currently set up for iterating DO rules
                        // since the Do_Eval_Rule routine expects to be
                        // able to arbitrarily update P_NEXT_RULE
                        //
                        fail ("DO rules currently cannot be iterated");
                    }

                    subrule = VOID_VALUE; // cause an error if iterating

                    i = Do_Eval_Rule(f); // changes P_RULE (should)

                    if (i == THROWN_FLAG)
                        return R_THROWN;

                    break;
                }

                default:
                    fail (Error_Parse_Rule());
                }
            }
            else if (IS_BLOCK(rule)) {
                bool interrupted;
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
                    return R_THROWN;
                }

                // Non-breaking out of loop instances of match or not.

                if (IS_NULLED(P_CELL))
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

                i = Parse_One_Rule(f, P_POS, rule);

                // i may be THROWN_FLAG
            }

            if (i == THROWN_FLAG)
                return R_THROWN;

            // Necessary for special cases like: some [to end]
            // i: indicates new index or failure of the match, but
            // that does not mean failure of the rule, because optional
            // matches can still succeed, if if the last match failed.
            //
            if (i != END_FLAG) {
                count++; // may overflow to negative

                if (count < 0)
                    count = INT32_MAX; // the forever case

                if (i == P_POS and not (flags & PF_WHILE)) {
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

      post_match_processing:;

        if (flags) {
            if (flags & PF_NOT) {
                if ((flags & PF_NOT2) and P_POS != NOT_FOUND)
                    P_POS = NOT_FOUND;
                else
                    P_POS = begin;
            }

            if (P_POS == NOT_FOUND) {
                if (flags & PF_THEN) {
                    FETCH_TO_BAR_OR_END(f);
                    if (NOT_END(P_RULE))
                        FETCH_NEXT_RULE(f);
                }
            }
            else {
                // Set count to how much input was advanced
                //
                count = (begin > P_POS) ? 0 : P_POS - begin;

                if (flags & PF_COPY) {
                    REBVAL *sink = Sink_Var_May_Fail(
                        set_or_copy_word,
                        P_RULE_SPECIFIER
                    );
                    if (ANY_ARRAY_OR_PATH(P_INPUT_VALUE)) {
                        Init_Any_Array(
                            sink,
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
                            sink,
                            Copy_Sequence_At_Len(P_INPUT, begin, count)
                        );
                    }
                    else {
                        assert(ANY_STRING(P_INPUT_VALUE));

                        DECLARE_LOCAL (begin_val);
                        Init_Any_Series_At(begin_val, P_TYPE, P_INPUT, begin);

                        Init_Any_Series(
                            sink,
                            P_TYPE,
                            Copy_String_At_Len(begin_val, count)
                        );
                    }

                    Quotify(sink, P_NUM_QUOTES);
                }
                else if ((flags & PF_SET) and (count != 0)) { // 0-leave alone
                    //
                    // We waited to eval the SET-GROUP! until we knew we had
                    // something we wanted to set.  Do so, and then go through
                    // a normal setting procedure.
                    //
                    if (IS_SET_GROUP(set_or_copy_word)) {
                        if (Do_At_Throws(
                            P_CELL,
                            VAL_ARRAY(set_or_copy_word),
                            VAL_INDEX(set_or_copy_word),
                            f->specifier
                        )){
                            Move_Value(P_OUT, P_CELL);
                            return R_THROWN;
                        }

                        // !!! What SET-GROUP! can do in PARSE is more
                        // ambitious than just an indirection for naming
                        // variables or paths...but for starters it does
                        // that just to show where more work could be done.

                        if (not (IS_WORD(P_CELL) or IS_SET_WORD(P_CELL)))
                            fail (Error_Parse_Variable_Raw(P_CELL));

                        set_or_copy_word = P_CELL;
                    }

                    if (IS_SER_ARRAY(P_INPUT)) {
                        Derelativize(
                            Sink_Var_May_Fail(
                                set_or_copy_word, P_RULE_SPECIFIER
                            ),
                            ARR_AT(ARR(P_INPUT), begin),
                            P_INPUT_SPECIFIER
                        );
                    }
                    else {
                        REBVAL *var = Sink_Var_May_Fail(
                            set_or_copy_word, P_RULE_SPECIFIER
                        );
                        REBUNI ch = GET_ANY_CHAR(P_INPUT, begin);
                        if (P_TYPE == REB_BINARY)
                            Init_Integer(var, ch);
                        else
                            Init_Char(var, ch);
                    }
                }

                if (flags & PF_REMOVE) {
                    FAIL_IF_READ_ONLY_SERIES(P_INPUT_VALUE);
                    if (count) Remove_Series(P_INPUT, begin, count);
                    P_POS = begin;
                }

                if (flags & (PF_INSERT | PF_CHANGE)) {
                    FAIL_IF_READ_ONLY_SERIES(P_INPUT_VALUE);
                    count = (flags & PF_INSERT) ? 0 : count;
                    bool only = false;

                    if (IS_END(f->value))
                        fail (Error_Parse_End());

                    if (IS_WORD(P_RULE)) { // check for ONLY flag
                        REBSYM cmd = VAL_CMD(P_RULE);
                        switch (cmd) {
                        case SYM_ONLY:
                            only = true;
                            FETCH_NEXT_RULE(f);
                            if (IS_END(P_RULE))
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
                    FETCH_NEXT_RULE(f);

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
                            return R_THROWN;
                        }

                        rule = evaluated;
                    }

                    if (IS_SER_ARRAY(P_INPUT)) {
                        DECLARE_LOCAL (specified);
                        Derelativize(specified, rule, P_RULE_SPECIFIER);

                        REBCNT mod_flags = (flags & PF_INSERT) ? 0 : AM_PART;
                        if (
                            not only and
                            Splices_Into_Type_Without_Only(P_TYPE, specified)
                        ){
                            mod_flags |= AM_SPLICE;
                        }
                        P_POS = Modify_Array(
                            (flags & PF_CHANGE)
                                ? Canon(SYM_CHANGE)
                                : Canon(SYM_INSERT),
                            ARR(P_INPUT),
                            begin,
                            specified,
                            mod_flags,
                            count,
                            1
                        );

                        if (IS_QUOTED(rule))
                            Unquotify(ARR_AT(ARR(P_INPUT), P_POS - 1), 1);
                    }
                    else {
                        DECLARE_LOCAL (specified);
                        Derelativize(specified, rule, P_RULE_SPECIFIER);

                        P_POS = begin;

                        REBCNT mod_flags = (flags & PF_INSERT) ? 0 : AM_PART;

                        if (P_TYPE == REB_BINARY)
                            P_POS = Modify_Binary(
                                P_INPUT_VALUE,
                                (flags & PF_CHANGE)
                                    ? Canon(SYM_CHANGE)
                                    : Canon(SYM_INSERT),
                                specified,
                                mod_flags,
                                count,
                                1
                            );
                        else {
                            P_POS = Modify_String(
                                P_INPUT_VALUE,
                                (flags & PF_CHANGE)
                                    ? Canon(SYM_CHANGE)
                                    : Canon(SYM_INSERT),
                                specified,
                                mod_flags,
                                count,
                                1
                            );
                        }
                    }
                }

                if (flags & PF_AHEAD)
                    P_POS = begin;
            }

            flags = 0;
            set_or_copy_word = NULL;
        }

        if (P_POS == NOT_FOUND) {
            FETCH_TO_BAR_OR_END(f);
            if (IS_END(P_RULE)) // no alternate rule
                return Init_Nulled(D_OUT);

            // Jump to the alternate rule and reset input
            //
            FETCH_NEXT_RULE(f);
            P_POS = begin = start;
        }

        begin = P_POS;
        mincount = maxcount = 1;
    }

    return Init_Integer(D_OUT, P_POS); // !!! return switched input series??
}


//
//  parse: native [
//
//  "Parse series according to grammar rules, return last match position"
//
//      return: "null if rules failed, else terminal position of match"
//          [<opt> any-series! quoted!]
//      input "Input series to parse"
//          [<blank> any-series! any-path! quoted!]
//      rules "Rules to parse by"
//          [<blank> block!]
//      /case "Uses case-sensitive comparison"
//  ]
//
REBNATIVE(parse)
{
    INCLUDE_PARAMS_OF_PARSE;

    REBVAL *rules = ARG(rules);

    bool interrupted;
    if (Subparse_Throws(
        &interrupted,
        D_OUT,
        ARG(input),
        SPECIFIED, // input is a non-relative REBVAL
        rules,
        SPECIFIED, // rules is a non-relative REBVAL
        REF(case) or IS_BINARY(ARG(input)) ? AM_FIND_CASE : 0
        //
        // We always want "case-sensitivity" on binary bytes, vs. treating
        // as case-insensitive bytes for ASCII characters.
    )){
        // Any PARSE-specific THROWs (where a PARSE directive jumped the
        // stack) should be handled here.  However, RETURN was eliminated,
        // in favor of enforcing a more clear return value protocol for PARSE

        return R_THROWN;
    }

    if (IS_NULLED(D_OUT))
        return nullptr;

    REBCNT progress = VAL_UINT32(D_OUT);
    assert(progress <= VAL_LEN_HEAD(ARG(input)));
    Move_Value(D_OUT, ARG(input));
    VAL_INDEX(D_OUT) = progress;
    return D_OUT;
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
