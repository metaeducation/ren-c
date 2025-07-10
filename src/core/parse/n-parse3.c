//
//  file: %n-parse3.c
//  summary: "parse dialect interpreter"
//  section: utility
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2021 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=//// !!! NOTICE !!! /////////////////////////////////////////////////////=//
//
// The PARSE code in R3-Alpha was a fairly organic codebase, and was largely
// concerned with being performant (to make it a viable competitor to things
// like RegEx).  Since it did flag-fiddling in lieu of enforcing a generalized
// architecture, there were significant irregularities...and compositions of
// rules that seemed like they should be legal wouldn't work.  Many situations
// that should have been errors would be ignored or have strange behaviors.
//
// The code was patched to make its workings clearer over time in Ren-C, and
// to try and eliminate mechanical bugs (such as bad interactions with the GC).
// But the basic method was not attacked from the ground up.  Recursions of
// the parser were unified with the level model of recursing the evaluator...
// but that was the only true big change.
//
// However, a full redesign has been started with %src/mezz/uparse.r.  This
// is in the spirit of "parser combinators" as defined in many other languages,
// but brings in the PARSE dialect's succinct symbolic nature.  That design is
// extremely slow, however--and will need to be merged in with some of the
// ideas in this file.
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// As a major operational difference from R3-Alpha, each recursion in Ren-C's
// PARSE runs using a "Rebol Stack Level"--similar to how the EVAL evaluator
// works.  So `[print "abc"]` and `[thru "abc"]` are both seen as "code" and
// iterated using the same mechanic.  (The rules are also locked from
// modification during the course of the PARSE, as code is in Ren-C.)
//
// This leverages common services like reporting the start of the last
// "expression" that caused an error.  So merely calling `panic()` will use
// the call stack to properly indicate the start of the parse rule that caused
// a problem.  But most importantly, debuggers can break in and see the
// state at every step in the parse rule recursions.
//
// The function users see on the stack for each recursion is a native called
// SUBPARSE.  Although it is shaped similarly to typical EVAL code, there are
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

#include "sys-core.h"


// !!! R3-Alpha would frequently conflate indexes and flags, which could be
// confusing in the evaluator and led to many THROWN values being overlooked.
// To deal with this, a REBIXO datatype (Index-OR-a-flag) was introduced.  It
// helped transition the system to its current mechanism where there is no
// THROWN type indicator--rather a _Throws() boolean-return convention that
// chains through the stack.  PARSE is left as the only user of the datatype,
// and should also be converted to the cleaner convention.
//
#define REBIXO REBLEN
#define THROWN_FLAG ((REBLEN)(-1))
#define END_FLAG ((REBLEN)(-2))


//
// These macros are used to address into the frame directly to get the
// current parse rule, current input series, current parse position in that
// input series, etc.  Because the cell bits of the frame arguments are
// modified as the parse runs, that means users can see the effects at
// a breakpoint.
//
// (Note: when arguments to natives are viewed under the debugger, the
// debug frames are read only.  So it's not possible for the user to change
// the ANY-SERIES? of the current parse position sitting in slot 0 into
// a DECIMAL! and crash the parse, for instance.  They are able to change
// usermode authored function arguments only.)
//

// The compiler typically warns us about not using all the arguments to
// a native at some point.  Service routines may use only some of the values
// in the parse frame, so defeat that check.
//
#define USE_PARAMS_OF_SUBPARSE \
    INCLUDE_PARAMS_OF_SUBPARSE; \
    USED(ARG(INPUT)); \
    USED(ARG(FLAGS)); \
    USED(ARG(NUM_QUOTES)); \
    USED(ARG(POSITION)); \
    USED(ARG(SAVE)); \
    USED(ARG(LOOKBACK))

#define P_AT_END            Is_Level_At_End(level_)
#define P_RULE              At_Level(level_)  // rvalue
#define P_RULE_BINDING      Level_Binding(level_)

#define P_HEART             Heart_Of_Builtin_Fundamental(Element_ARG(INPUT))
#define P_INPUT             Cell_Flex(ARG(INPUT))
#define P_INPUT_BINARY      Cell_Binary(ARG(INPUT))
#define P_INPUT_STRING      Cell_Strand(ARG(INPUT))
#define P_INPUT_ARRAY       Cell_Array(ARG(INPUT))
#define P_INPUT_SPECIFIER   List_Binding(Element_ARG(INPUT))
#define P_INPUT_IDX         SERIES_INDEX_UNBOUNDED(Element_ARG(INPUT))
#define P_INPUT_LEN         Series_Len_Head(Element_ARG(INPUT))

#define P_FLAGS             mutable_VAL_INT64(ARG(FLAGS))

#define P_NUM_QUOTES        VAL_INT32(ARG(NUM_QUOTES))

#define P_POS               SERIES_INDEX_UNBOUNDED(ARG(POSITION))

// !!! The way that PARSE works, it will sometimes run the thing it finds
// in the list...but if it's a WORD! or PATH! it will look it up and run
// the result.  When it's in the list, the binding for that list needs
// to be applied to it.  But when the value has been fetched, that binding
// shouldn't be used again...because virtual binding isn't supposed to
// carry through references.  The hack to get virtual binding running is to
// always put the fetched rule in the same place...and then the binding
// is only used when the rule *isn't* in that cell.
//
#define P_SAVE              u_cast(Element*, ARG(SAVE))
#define rule_binding() \
    (rule == ARG(SAVE) ? SPECIFIED : P_RULE_BINDING)


#define FETCH_NEXT_RULE(L) \
    Fetch_Next_In_Feed(L->feed)


#define FETCH_TO_BAR_OR_END(L) \
    while (not P_AT_END and not ( \
        Type_Of_Unchecked(P_RULE) == TYPE_WORD \
        and Word_Symbol(P_RULE) == CANON(BAR_1) \
    )){ \
        FETCH_NEXT_RULE(L); \
    }


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
    PF_0 = 1 << 0,
    PF_FIND_CASE = 1 << 1,
    PF_FIND_MATCH = 1 << 2,

    PF_SET = 1 << 3,
    PF_ACROSS = 1 << 4,
    PF_NOT = 1 << 5,
    PF_NOT2 = 1 << 6,  // #1246
    PF_7 = 1 << 7,
    PF_AHEAD = 1 << 8,
    PF_REMOVE = 1 << 9,
    PF_INSERT = 1 << 10,
    PF_CHANGE = 1 << 11,
    PF_LOOPING = 1 << 12,
    PF_FURTHER = 1 << 13,  // must advance parse input to count as a match
    PF_OPTIONAL = 1 << 14,  // want VOID (not no-op) if no matches
    PF_TRY = 1 << 15,  // want NULL (not no-op) if no matches

    PF_ONE_RULE = 1 << 16,  // signal to only run one step of the parse

    PF_MAX = PF_ONE_RULE
};

STATIC_ASSERT(PF_MAX <= INT32_MAX);  // needs to fit in VAL_INTEGER()

// Note: clang complains if `cast(int, ...)` used here, though gcc doesn't
STATIC_ASSERT((int)AM_FIND_CASE == (int)PF_FIND_CASE);
STATIC_ASSERT((int)AM_FIND_MATCH == (int)PF_FIND_MATCH);

#define PF_FIND_MASK \
    (PF_FIND_CASE | PF_FIND_MATCH)

#define PF_STATE_MASK (~PF_FIND_MASK & ~PF_ONE_RULE)


// In %words.r, the parse words are lined up in order so they can be quickly
// filtered, skipping the need for a switch statement if something is not
// a parse command.
//
// !!! This and other efficiency tricks from R3-Alpha should be reviewed to
// see if they're really the best option.
//
INLINE Option(SymId) VAL_CMD(const Cell* v) {
    Option(SymId) sym = Word_Id(v);
    if (
        cast(int, sym) >= MIN_SYM_PARSE3
        and cast(int, sym) <= MAX_SYM_PARSE3
    ){
        return sym;
    }
    return SYM_0;
}


// Subparse_Throws() is a helper that sets up a call frame and invokes the
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
    Sink(Atom) out,
    const Element* input,
    Context* input_binding,
    Level* const L,
    Flags flags
){
    assert(Any_Series_Type(Heart_Of(input)));

    Push_Level_Erase_Out_If_State_0(out, L);

    required (Push_Action(L, LIB(SUBPARSE), PREFIX_0));

    // This needs to be set before INCLUDE_PARAMS_OF_SUBPARSE; it is what
    // ensures that usermode accesses to the frame won't be able to fiddle
    // the frame values to bit patterns the native might crash on.
    //
    Set_Flex_Info(Varlist_Array(L->varlist), HOLD);

    USE_LEVEL_SHORTHANDS (L);
    INCLUDE_PARAMS_OF_SUBPARSE;

    Derelativize(
        Erase_ARG(INPUT),
        cast(Element*, input),
        input_binding
    );

    assert((flags & PF_STATE_MASK) == 0);  // no "parse state" flags allowed
    Init_Integer(Erase_ARG(FLAGS), flags);

    // Locals in frame would be unset on entry if called by action dispatch.
    //
    Init_Tripwire(Erase_ARG(NUM_QUOTES));
    Init_Tripwire(Erase_ARG(POSITION));
    Init_Tripwire(Erase_ARG(SAVE));
    Init_Tripwire(Erase_ARG(LOOKBACK));

    // !!! By calling the subparse native here directly from its C function
    // vs. going through the evaluator, we don't get the opportunity to do
    // things like HIJACK it.  Consider APPLY-ing it.
    //
    Set_Executor_Flag(ACTION, L, IN_DISPATCH);

    Bounce b = NATIVE_CFUNC(SUBPARSE)(L);

    Drop_Action(L);

    if (b == BOUNCE_THROWN) {
        Drop_Level(L);

        // ACCEPT and REJECT are special cases that can happen at nested parse
        // levels and bubble up through the throw mechanism to break a looping
        // construct.
        //
        // !!! R3-Alpha didn't react to these instructions in general, only in
        // the particular case where subparsing was called in an iterated
        // construct.  Even then, it could only break through one level of
        // depth.  Most places would treat them the same as a normal match
        // or not found.  This returns the interrupted flag which is still
        // ignored by most callers, but makes that fact more apparent.
        //
        const Value* label = VAL_THROWN_LABEL(LEVEL);
        if (Is_Frame(label)) {
            if (Frame_Phase(label) == Frame_Phase(LIB(PARSE_REJECT))) {
                CATCH_THROWN(out, LEVEL);
                *interrupted_out = true;
                return false;
            }

            if (Frame_Phase(label) == Frame_Phase(LIB(PARSE_BREAK))) {
                CATCH_THROWN(out, LEVEL);
                assert(Is_Integer(Known_Element(out)));
                *interrupted_out = true;
                return false;
            }
        }

        return true;
    }

    b = maybe Irreducible_Bounce(L, b);

    Drop_Level(L);

    if (b) {
        assert(b == BOUNCE_THROWN);
        assert(Is_Throwing_Panic(TOP_LEVEL));
        return true;
    }

    *interrupted_out = false;
    return false;
}


// Very generic errors.  Used to be parameterized with the parse rule in
// question, but now the `where` at the time of failure will indicate the
// location in the parse dialect that's the problem.

INLINE Error* Error_Parse3_Rule(void) {
    return Error_Parse3_Rule_Raw();
}

INLINE Error* Error_Parse3_End(void) {
    return Error_Parse3_End_Raw();
}

INLINE Error* Error_Parse3_Command(Level* level_) {
    return Error_Parse3_Command_Raw(P_RULE);
}

INLINE Error* Error_Parse3_Variable(Level* level_) {
    return Error_Parse3_Variable_Raw(P_RULE);
}


//
//  Get_Parse_Value: C
//
// Gets the value of a word (when not a command) or path.  Returns all other
// values as-is.
//
// If the fetched value is an antiform logic or splice, it is returned as
// a quasiform.  Fetched quasiforms are errors.
//
// 1. The caller expects an Element, but we transiently write non-Elements
//    into the `out` cell when doing a variable fetch.  To defuse the Sink's
//    overwriting property, we alias the out to `~`.  If antiform bits are
//    written to the aliased location, we fix them up or return an error.
//
static Result(Option(SymId)) Get_Parse_Value(
    Sink(Element) out,  // storage for fetched values; must be GC protected
    const Element* rule,
    Context* context
){
    assert(out != rule);

    Value* out_value = u_cast(Value*, out);  // defuses Sink() behavior [1]

    if (Is_Word(rule)) {
        Option(SymId) id = Word_Id(rule);
        if (
            cast(int, id) >= MIN_SYM_PARSE3
            and cast(int, id) <= MAX_SYM_PARSE3
        ){
            return id;
        }

        required (Get_Var(out_value, NO_STEPS, rule, context));

        // fallthrough to fix up antiforms in aliased out
    }
    else if (Is_Tuple(rule) or Is_Path(rule)) {
        required (Get_Var(out_value, NO_STEPS, rule, context));

        // fallthrough to fix up antiforms in aliased out
    }
    else {
        Copy_Cell(out, rule);
        return SYM_0;
    }

    if (Is_Quasiform(out_value))
        return fail ("RULE should not look up to quasiforms");

    if (Is_Antiform(out_value)) {
        if (Is_Nulled(out_value))
            return fail (Error_Bad_Null(rule));

        if (Is_Trash(out_value))
            return fail (Error_Bad_Word_Get(rule, out));

        if (Is_Logic(out_value) or Is_Splice(out_value))
            Quasify_Antiform(out_value);
        else if (Is_Datatype(out)) {  // convert to functions for now
            DECLARE_VALUE (checker);
            required (Init_Typechecker(checker, out));
            assert(Heart_Of(checker) == TYPE_FRAME);
            Copy_Cell(out_value, checker);
            LIFT_BYTE(out_value) = NOQUOTE_2;
        }
        else {
            return fail (Error_Bad_Antiform(out));
        }
    }
    else {
        if (Is_Integer(out))
            return fail (
                "Use REPEAT on integers https://forum.rebol.info/t/1578/6"
            );
    }

    return SYM_0;
}


//
//  Process_Group_For_Parse_Throws: C
//
// Ren-C adds the ability of a GROUP! evaluation to produce a VETO error
// antiform, to signal that the group should count as a failed match.
//
// Other antiform errors are elevated to panics, while all other results are
// simply ignored.
//
// The INLINE combinator is used in cases where a GROUP! wants to generate a
// rule that is retriggered.  (Attempts to come up with a good decoration on
// a group to mean this didn't come up with anything great, and it's rare
// enough that it doesn't seem so bad to have to type INLINE to get it.)
//
bool Process_Group_For_Parse_Throws(
    Sink(bool) veto,
    Level* level_,
    const Element* group  // can't be same as out
){
    USE_PARAMS_OF_SUBPARSE;

    Context* derived = (group == P_SAVE)
        ? SPECIFIED
        : Derive_Binding(P_RULE_BINDING, group);

    DECLARE_ATOM (eval);
    Flags flags = LEVEL_MASK_NONE;
    if (Eval_Element_Core_Throws(eval, flags, group, derived))
        return true;

    if (Is_Error(eval)) {
        if (Is_Error_Veto_Signal(Cell_Error(eval))) {
            *veto = true;
            return false;
        }
        panic (Cell_Error(eval));
    }

    if (Is_Ghost_Or_Void(eval)) {
        // allow it (can't decay)
    }
    else {
        required (Decay_If_Unstable(eval));
    }

    // !!! The input is not locked from modification by agents other than the
    // PARSE's own REMOVE etc.  This is a sketchy idea, but as long as it's
    // allowed, each time arbitrary user code runs, rules have to be adjusted
    //
    if (P_POS > P_INPUT_LEN)
        P_POS = P_INPUT_LEN;

    *veto = false;
    return false;
}


//
//  Parse_One_Rule: C
//
// Used for parsing ANY-SERIES? to match the next rule in the ruleset.  If it
// matches, return the index just past it.
//
// This function is also called by To_Thru, consequently it may need to
// process elements other than the current one in the frame.  Hence it
// is parameterized by an arbitrary `pos` instead of assuming the P_POS
// that is held by the frame.
//
// The return result is either an int position, END_FLAG, or THROWN_FLAG
// Only in the case of THROWN_FLAG will L->out (aka OUT) be affected.
// Otherwise, it should exit the routine as an END marker (as it started);
//
static Result(REBIXO) Parse_One_Rule(
    Level* level_,
    REBLEN pos,
    const Element* rule
){
    USE_PARAMS_OF_SUBPARSE;

    if (Is_Group(rule)) {
        bool veto;
        if (Process_Group_For_Parse_Throws(&veto, level_, rule))
            return THROWN_FLAG;

        if (veto)
            return END_FLAG;

        assert(pos <= P_INPUT_LEN);  // !!! Process_Group ensures
        return pos;
    }

    if (pos == P_INPUT_LEN) {  // at end of input
        if (Is_Quasiform(rule) or Is_Block(rule)) {
            //
            // Only these types can *potentially* handle an END input.
        }
        else if (
            (Is_Text(rule) or Is_Blob(rule))
            and (Series_Len_At(rule) == 0)
            and (Any_String_Type(P_HEART) or P_HEART == TYPE_BLOB)
        ){
            // !!! The way this old R3-Alpha code was structured is now very
            // archaic (compared to UPARSE).  But while that design stabilizes,
            // this patch handles the explicit case of wanting to match
            // something like:
            //
            //     >> did parse3 "ab" [thru ["ab"] ""]
            //     == ~true~  ; anti
            //
            // Just to show what should happen in the new model (R3-Alpha did
            // not have that working for multiple reasons...lack of making
            // progress in the "" rule, for one.)
            //
            return pos;
        }
        else
            return END_FLAG;  // Other cases below assert if item is END
    }

    if (Is_Quasi_Word(rule)) {
        if (
            Is_Quasi_Word_With_Id(rule, SYM_VOID)
            or Is_Quasi_Word_With_Id(rule, SYM_OKAY)
        ){
            return pos;  // just skip ahead
        }
        panic ("PARSE3 only supports ~void~ and ~okay~ quasiforms/antiforms");
    }
    else switch (maybe Type_Of(rule)) {  // same behaviors for all P_INPUT
      case TYPE_INTEGER:
        panic ("Non-rule-count INTEGER! in PARSE must be literal, use QUOTE");

      case TYPE_BLOCK: {
        //
        // Process subrule in its own frame.  It will not change P_POS
        // directly (it will have its own P_POSITION_VALUE).  Hence the return
        // value regarding whether a match occurred or not has to be based on
        // the result that comes back in OUT.

        REBLEN pos_before = P_POS;
        P_POS = pos;  // modify input position

        Level* sub = require (Make_Level_At_Inherit_Const(
            &Action_Executor,  // !!! Parser_Executor?
            rule, rule_binding(),
            LEVEL_MASK_NONE
        ));

        DECLARE_ATOM (subresult);
        bool interrupted;
        if (Subparse_Throws(
            &interrupted,
            subresult,
            Element_ARG(POSITION),  // affected by P_POS assignment above
            SPECIFIED,
            sub,
            (P_FLAGS & PF_FIND_MASK)
        )){
            return THROWN_FLAG;
        }

        UNUSED(interrupted);  // !!! ignore "interrupted" (ACCEPT or REJECT?)

        P_POS = pos_before;  // restore input position

        if (Is_Light_Null(subresult))
            return END_FLAG;

        REBINT index = VAL_INT32(Known_Element(subresult));
        assert(index >= 0);
        return index; }

      default:;
        // Other cases handled distinctly between blocks/strings/binaries...
    }

    if (Stub_Holds_Cells(P_INPUT)) {
        const Element* item = Array_At(P_INPUT_ARRAY, pos);

        if (Is_Quoted(rule)) {  // fall through to direct match
            rule = Unquotify(Copy_Cell(SPARE, rule));
        }
        else if (Is_Pinned_Form_Of(WORD, rule)) {
            required (Get_Var(SPARE, NO_STEPS, rule, P_RULE_BINDING));
            if (Is_Antiform(SPARE))
                panic (Error_Bad_Antiform(SPARE));

            rule = Known_Element(SPARE);  // fall through to direct match
        }
        else switch (maybe Type_Of(rule)) {
          case TYPE_FRAME: {  // want to run a type constraint...
            Copy_Cell(SPARE, item);
            if (Typecheck_Spare_With_Predicate_Uses_Scratch(
                level_, rule, Cell_Frame_Label(rule)
            )){
                return pos + 1;
            }
            return END_FLAG; }

          case TYPE_PARAMETER: {
            assert(rule != SPARE);
            Copy_Cell(SPARE, item);
            if (Typecheck_Atom_In_Spare_Uses_Scratch(
                LEVEL, rule, P_RULE_BINDING
            )){
                return pos + 1;  // type was in typeset
            }
            return END_FLAG; }

          case TYPE_TEXT:
          case TYPE_RUNE:
            break;  // all interpreted literally

          default:
            panic ("Unknown value type for match in ANY-ARRAY!");
        }

        // !!! R3-Alpha said "Match with some other value"... is this a good
        // default?!
        //
        bool equal = require (
            Equal_Values(item, rule, did (P_FLAGS & AM_FIND_CASE))
        );
        if (equal)
            return pos + 1;

        return END_FLAG;
    }
    else {
        assert(Any_String_Type(P_HEART) or P_HEART == TYPE_BLOB);

        if (Is_Pinned_Form_Of(WORD, rule)) {
            required (Get_Var(SPARE, NO_STEPS, rule, P_RULE_BINDING));
            if (Is_Antiform(SPARE))
                panic (Error_Bad_Antiform(SPARE));
            rule = Known_Element(SPARE);
        }

        // Build upon FIND's behavior to mold quoted items, e.g.:
        //
        //     >> parse "ab<c>10" ['ab '<c> '10]
        //     == 10
        //
        // It can be less visually noisy than:
        //
        //     >> parse "ab<c>10" ["ab" "<c>" "10"]
        //     == "10"
        //
        // The return value may also be more useful.
        //
        Option(Heart) rule_heart = Heart_Of(rule);
        if (
            Quotes_Of(rule) == 1  // '<a> will mold to "<a>"
            or (Quotes_Of(rule) == 0 and (
                rule_heart == TYPE_TEXT
                or rule_heart == TYPE_RUNE
                or rule_heart == TYPE_BLOB
            ))
        ){
            REBLEN len;
            REBINT index = Find_Value_In_Binstr(
                &len,
                Element_ARG(POSITION),
                Series_Len_Head(ARG(POSITION)),
                rule,
                (P_FLAGS & PF_FIND_MASK) | AM_FIND_MATCH
                    | (Is_Rune(rule) ? AM_FIND_CASE : 0),
                1  // skip
            );
            if (index == NOT_FOUND)
                return END_FLAG;
            return index + len;
        }
        else switch (maybe Type_Of(rule)) {
          case TYPE_BITSET: {
            //
            // Check current char/byte against character set, advance matches
            //
            bool uncased;
            Codepoint uni;
            if (P_HEART == TYPE_BLOB) {
                uni = *Binary_At(P_INPUT_BINARY, P_POS);
                uncased = false;
            }
            else {
                uni = Get_Strand_Char_At(cast(Strand*, P_INPUT), P_POS);
                uncased = not (P_FLAGS & AM_FIND_CASE);
            }

            if (Check_Bit(VAL_BITSET(rule), uni, uncased))
                return P_POS + 1;

            return END_FLAG; }

          default:
            panic (Error_Parse3_Rule());
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
static Result(REBIXO) To_Thru_Block_Rule(
    Level* level_,
    const Element* rule_block,
    bool is_thru
){
    USE_PARAMS_OF_SUBPARSE;

    DECLARE_ELEMENT (cell);  // holds evaluated rules (use frame cell instead?)

    // Note: This enumeration goes through <= P_INPUT_LEN, because the
    // block rule might be something like `to [{a} | <end>]`.  e.g. being
    // positioned on the end cell or null terminator of a string may match.
    //
    DECLARE_ELEMENT (iter);
    Copy_Cell(iter, Element_ARG(POSITION));  // need to slide pos
    for (
        ;
        SERIES_INDEX_UNBOUNDED(iter) <= P_INPUT_LEN;
        ++SERIES_INDEX_UNBOUNDED(iter)
    ){  // see note
        const Element* blk_tail = Array_Tail(Cell_Array(rule_block));
        const Element* blk = Array_Head(Cell_Array(rule_block));
        for (; blk != blk_tail; blk++) {
            if (Is_Bar(blk))
                panic (Error_Parse3_Rule());  // !!! Shouldn't `TO [|]` succeed?

            if (Is_Group(blk)) {
                bool veto;
                if (Process_Group_For_Parse_Throws(&veto, level_, blk))
                    return THROWN_FLAG;

                if (veto)
                    return END_FLAG;

                continue;
            }

            const Element* rule = blk;

            if (Is_Word(rule)) {
                Option(SymId) cmd = VAL_CMD(rule);

                if (cmd) {
                    if (cmd == SYM_END)
                        panic ("Use <end> instead of END in PARSE3");

                    if (cmd == SYM_QUOTE)
                        panic ("Use THE instead of QUOTE in PARSE3");

                    if (cmd == SYM_THE) {
                        rule = ++blk;  // next rule is the literal value
                        if (rule == blk_tail)
                            panic (Error_Parse3_Rule());
                    }
                    else
                        panic (Error_Parse3_Rule());
                }
                else {
                    required (Get_Parse_Value(cell, rule, P_RULE_BINDING));

                    rule = cell;
                }
            }
            else if (Is_Tag(rule)) {
                bool strict = true;
                if (0 == CT_Utf8(rule, g_tag_end, strict)) {
                    if (Series_Index(iter) >= P_INPUT_LEN)
                        return P_INPUT_LEN;
                    goto next_alternate_rule;
                }
                else if (0 == CT_Utf8(rule, g_tag_here, strict)) {
                    // ignore for now
                }
                else
                    panic ("TAG! combinator must be <here> or <end> ATM");
            }
            else if (Is_Tuple(rule) or Is_Path(rule)) {
                required (Get_Parse_Value(cell, rule, P_RULE_BINDING));

                rule = cell;
            }
            else {
                // fallthrough to literal match of rule (text, bitset, etc)
            }

            // Try to match it:
            if (Any_List_Type(P_HEART) or Any_Sequence_Type(P_HEART)) {
                if (Any_List(rule))
                    panic (Error_Parse3_Rule());

                REBIXO ixo = require (
                    Parse_One_Rule(level_, Series_Index(iter), rule)
                );
                if (ixo == THROWN_FLAG)
                    return THROWN_FLAG;

                if (ixo == END_FLAG) {
                    // fall through, keep looking
                }
                else {  // ixo is pos we matched past, so back up if only TO
                    SERIES_INDEX_UNBOUNDED(iter) = ixo;
                    if (is_thru)
                        return Series_Index(iter);  // don't back up
                    return Series_Index(iter) - 1;  // back up
                }
            }
            else if (P_HEART == TYPE_BLOB) {
                Byte ch1 = *Blob_At(iter);

                if (Series_Index(iter) == P_INPUT_LEN) {
                    //
                    // If we weren't matching END, then the only other thing
                    // we'll match at the BLOB! end is an empty BLOB!.
                    // Not a "NUL codepoint", because the internal BLOB!
                    // terminator is implementation detail.
                    //
                    assert(ch1 == '\0');  // internal BLOB! terminator
                    if (Is_Blob(rule) and Series_Len_At(rule) == 0)
                        return Series_Index(iter);
                }
                else if (Is_Rune_And_Is_Char(rule)) {
                    if (Rune_Known_Single_Codepoint(rule) > 0xff)
                        panic (Error_Parse3_Rule());

                    if (ch1 == Rune_Known_Single_Codepoint(rule)) {
                        if (is_thru)
                            return Series_Index(iter) + 1;
                        return Series_Index(iter);
                    }
                }
                else if (Is_Blob(rule)) {
                    Size rule_size;
                    const Byte* rule_data = Blob_Size_At(
                        &rule_size,
                        rule
                    );

                    Size iter_size;
                    const Byte* iter_data = Blob_Size_At(
                        &iter_size,
                        iter
                    );

                    if (
                        iter_size == rule_size
                        and (0 == memcmp(iter_data, rule_data, iter_size))
                    ){
                        if (is_thru)  // ^-- VAL_XXX_AT checked Series_Index()
                            return SERIES_INDEX_UNBOUNDED(iter) + 1;
                        return SERIES_INDEX_UNBOUNDED(iter);
                    }
                }
                else if (Is_Integer(rule)) {
                    if (VAL_INT64(rule) > 0xff)
                        panic (Error_Parse3_Rule());

                    if (ch1 == VAL_INT32(rule)) {
                        if (is_thru)
                            return Series_Index(iter) + 1;
                        return Series_Index(iter);
                    }
                }
                else
                    panic (Error_Parse3_Rule());
            }
            else {
                assert(Any_String_Type(P_HEART));

                Codepoint unadjusted = Get_Strand_Char_At(
                    P_INPUT_STRING,
                    Series_Index(iter)
                );
                if (unadjusted == '\0') {  // cannot be passed to UP_CASE()
                    assert(Series_Index(iter) == P_INPUT_LEN);

                    if (Is_Text(rule) and Series_Len_At(rule) == 0)
                        return Series_Index(iter);  // empty string can match end

                    goto next_alternate_rule;  // other match is END (above)
                }

                Codepoint ch;
                if (P_FLAGS & AM_FIND_CASE)
                    ch = unadjusted;
                else
                    ch = UP_CASE(unadjusted);

                if (Is_Rune_And_Is_Char(rule)) {
                    Codepoint ch2 = Rune_Known_Single_Codepoint(rule);
                    if (ch2 == 0)
                        goto next_alternate_rule;  // no 0 char in ANY-STRING?

                    if (not (P_FLAGS & AM_FIND_CASE))
                        ch2 = UP_CASE(ch2);
                    if (ch == ch2) {
                        if (is_thru)
                            return Series_Index(iter) + 1;
                        return Series_Index(iter);
                    }
                }
                else if (Is_Bitset(rule)) {
                    bool uncased = not (P_FLAGS & AM_FIND_CASE);
                    if (Check_Bit(VAL_BITSET(rule), ch, uncased)) {
                        if (is_thru)
                            return Series_Index(iter) + 1;
                        return Series_Index(iter);
                    }
                }
                else if (Any_String(rule)) {
                    REBLEN len = Series_Len_At(rule);
                    REBINT i = Find_Value_In_Binstr(
                        &len,
                        iter,
                        Series_Len_Head(iter),
                        rule,
                        (P_FLAGS & PF_FIND_MASK) | AM_FIND_MATCH,
                        1  // skip
                    );

                    if (i != NOT_FOUND) {
                        if (is_thru)
                            return i + len;
                        return i;
                    }
                }
                else if (Is_Integer(rule)) {
                    if (unadjusted == cast(Codepoint, VAL_INT32(rule))) {
                        if (is_thru)
                            return Series_Index(iter) + 1;
                        return Series_Index(iter);
                    }
                }
                else
                    panic (Error_Parse3_Rule());
            }

          next_alternate_rule:  // alternates are BAR! separated `[a | b | c]`

            do {
                ++blk;
                if (blk == blk_tail)
                    goto next_input_position;
            } while (not Is_Bar(blk));
        }

      next_input_position:;  // not matched yet, keep trying to go THRU or TO
    }
    return END_FLAG;
}


//
//  To_Thru_Non_Block_Rule: C
//
// There's a high-level split between block and non-block rule processing,
// as blocks are the common case.
//
static Result(REBIXO) To_Thru_Non_Block_Rule(
    Level* level_,
    const Element* rule,
    bool is_thru
){
    USE_PARAMS_OF_SUBPARSE;

    if (Is_Quasiform(rule)) {
        if (
            Is_Quasi_Word_With_Id(rule, SYM_VOID)
            or Is_Quasi_Word_With_Id(rule, SYM_OKAY)
        ){
            return P_POS;  // no-op
        }
        if (not Is_Lifted_Datatype(rule))
            panic ("PARSE3 supports ~void~, ~okay~, and datatype antiforms");
    }

    Option(Type) t = Type_Of(rule);
    assert(t != TYPE_BLOCK);

    if (t == TYPE_WORD and Word_Id(rule) == SYM_END)
        panic ("Use <end> instead of END in PARSE3");

    if (t == TYPE_TAG) {
        bool strict = true;
        if (0 == CT_Utf8(rule, g_tag_end, strict)) {
            return P_INPUT_LEN;
        }
        else if (0 == CT_Utf8(rule, g_tag_here, strict)) {
            panic ("TO/THRU <here> isn't supported in PARSE3");
        }
        else
            panic ("TAG! combinator must be <here> or <end> ATM");
    }

    if (Stub_Holds_Cells(P_INPUT)) {
        //
        // FOR ARRAY INPUT WITH NON-BLOCK RULES, USE Find_In_Array()
        //
        // !!! This adjusts it to search for non-literal words, but are there
        // other considerations for how non-block rules act with array input?
        //
        Flags find_flags = (P_FLAGS & AM_FIND_CASE);
        DECLARE_VALUE (temp);
        if (Is_Quoted(rule)) {  // make `'[foo bar]` match `[foo bar]`
            Unquotify(Derelativize(temp, rule, P_RULE_BINDING));
        }
        else if (Is_Pinned_Form_Of(WORD, rule)) {
            required (Get_Var(temp, NO_STEPS, rule, P_RULE_BINDING));
            if (Is_Antiform(temp))
                panic (Error_Bad_Antiform(temp));
            rule = Known_Element(temp);  // fall through to direct match
        }
        else if (Is_Lifted_Datatype(rule)) {
            DECLARE_ELEMENT (rule_value);
            Copy_Cell(rule_value, rule);
            Quasify_Isotopic_Fundamental(rule_value);
            required (Init_Typechecker(temp, rule_value));
        }
        else {
            Copy_Cell(temp, rule);
        }

        Length len;
        REBINT i = Find_In_Array(
            &len,
            P_INPUT_ARRAY,
            P_POS,
            Array_Len(P_INPUT_ARRAY),
            temp,
            find_flags,
            1
        );
        assert(len == 1);

        if (i == NOT_FOUND)
            return END_FLAG;

        if (is_thru)
            return i + len;

        return i;
    }
    else {
        if (Is_Pinned_Form_Of(WORD, rule)) {
            required (Get_Var(SPARE, NO_STEPS, rule, P_RULE_BINDING));
            if (Is_Antiform(SPARE))
                panic (Error_Bad_Antiform(SPARE));
            rule = Known_Element(SPARE);
        }
    }

    //=//// PARSE INPUT IS A STRING OR BINARY, USE A FIND ROUTINE /////////=//

    REBLEN len;  // e.g. if a TAG!, match length includes < and >
    REBINT i = Find_Value_In_Binstr(
        &len,
        Element_ARG(POSITION),
        Series_Len_Head(ARG(POSITION)),
        rule,
        (P_FLAGS & PF_FIND_MASK),
        1  // skip
    );

    if (i == NOT_FOUND)
        return END_FLAG;

    if (is_thru)
        return i + len;

    return i;
}


// This handles marking positions, either as plain `pos:` the SET-WORD! rule,
// or the newer `mark pos` rule.  Handles WORD! and PATH!.
//
static Result(Zero) Handle_Mark_Rule(
    Level* level_,
    const Element* quoted_set_or_copy_word  // bound
){
    USE_PARAMS_OF_SUBPARSE;

    // !!! Experiment: Put the quote level of the original series back on when
    // setting positions (then remove)
    //
    //     parse just '''{abc} ["a" mark x:]` => '''{bc}

    Quotify_Depth(Element_ARG(POSITION), P_NUM_QUOTES);

    // !!! Assume we might not be able to corrupt SPARE (rule may be
    // in SPARE?)
    //
    Sink(Value) out = OUT;
    DECLARE_ATOM (temp);
    if (rebRunThrows(
        out,  // <-- output cell
        CANON(SET), quoted_set_or_copy_word, ARG(POSITION)
    )){
        panic (Error_No_Catch_For_Throw(LEVEL));
    }
    Erase_Cell(OUT);

    Dequotify(Element_ARG(POSITION));  // go back to 0 quote level

    return zero;
}


static Result(Zero) Handle_Seek_Rule_Dont_Update_Begin(
    Level* level_,
    const Element* rule,
    Context* context
){
    USE_PARAMS_OF_SUBPARSE;

    Option(Type) t = Type_Of(rule);
    if (t == TYPE_WORD or t == TYPE_TUPLE) {
        required (Get_Var(SPARE, NO_STEPS, rule, context));
        if (Is_Antiform(SPARE))
            panic (Error_Bad_Antiform(SPARE));
        rule = Known_Element(SPARE);
        t = Type_Of(rule);
    }

    REBINT index;
    if (t == TYPE_INTEGER) {
        index = VAL_INT32(rule);
        if (index < 1)
            panic ("Cannot SEEK a negative integer position");
        --index;  // Rebol is 1-based, C is 0 based...
    }
    else if (Any_Series_Type(t)) {
        if (Cell_Flex(rule) != P_INPUT)
            panic ("Switching PARSE series is not allowed");
        index = Series_Index(rule);
    }
    else  // #1263
        panic (Error_Parse3_Series_Raw(rule));

    if (index > P_INPUT_LEN)
        P_POS = P_INPUT_LEN;
    else
        P_POS = index;

    return zero;
}

// !!! Note callers will `continue` without any post-"match" processing, so
// the only way `begin` will get set for the next rule is if they set it,
// else commands like INSERT that follow will insert at the old location.
//
// https://github.com/rebol/rebol-issues/issues/2269
//
// Without known resolution on #2269, it isn't clear if there is legitimate
// meaning to seeking a parse in mid rule or not.  So only reset the begin
// position if the seek appears to be a "separate rule" in its own right.
//
#define HANDLE_SEEK_RULE_UPDATE_BEGIN(f,rule,context) \
    required (Handle_Seek_Rule_Dont_Update_Begin((L), (rule), (context))); \
    if (not (P_FLAGS & PF_STATE_MASK)) \
        begin = P_POS;


//
//  subparse: native [
//
//  "Internal support function for PARSE (acts as variadic to consume rules)"
//
//      return: [null? integer!]
//      input [any-series? any-list? quoted!]
//      flags [integer!]
//      <local> position num-quotes save lookback
//  ]
//
DECLARE_NATIVE(SUBPARSE)
//
// Rules are matched until one of these things happens:
//
// * A rule fails, and is not then picked up by a later "optional" rule.
// This returns NULL.
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
// * A `panic()`, in which case the function won't return--it will longjmp
// up to the most recently pushed handler.  This can happen due to an invalid
// rule pattern, or if there's an error in code that is run in parentheses.
//
// * A throw-style result caused by EVAL code run in parentheses (e.g. a
// THROW, RETURN, BREAK, CONTINUE).  This returns a thrown value.
//
// * A special throw to indicate a return out of the PARSE itself, triggered
// by the RETURN instruction.  This also returns a thrown value, but will
// be caught by PARSE before returning.
//
{
    INCLUDE_PARAMS_OF_SUBPARSE;

    UNUSED(ARG(FLAGS));  // used via P_FLAGS

    Level* L = level_;  // nice alias of implicit native parameter

    // If the input is quoted, e.g. `parse just ''''[...] [rules]`, we dequote
    // it while we are processing the ARG().  This is because we are trying
    // to update and maintain the value as we work in a way that can be shown
    // in the debug stack frame.
    //
    // But we save the number of quotes in a local variable.  This way we can
    // put the quotes back on whenever doing a COPY etc.
    //
    assert(Is_Trash(ARG(NUM_QUOTES)));
    Init_Integer(ARG(NUM_QUOTES), Quotes_Of(Element_ARG(INPUT)));
    Dequotify(Element_ARG(INPUT));

    // Make sure index position is not past END
    if (SERIES_INDEX_UNBOUNDED(ARG(INPUT)) > Series_Len_Head(ARG(INPUT)))
        SERIES_INDEX_UNBOUNDED(ARG(INPUT)) = Series_Len_Head(ARG(INPUT));

    assert(Is_Trash(ARG(POSITION)));
    Copy_Cell(ARG(POSITION), ARG(INPUT));

  #if RUNTIME_CHECKS
    //
    // These parse state variables live in frame varlists, which can be
    // annoying to find to inspect in the debugger.  This makes pointers into
    // the value payloads so they can be seen more easily.
    //
    const REBIDX *pos_debug = &P_POS;
    USED(pos_debug);
  #endif

    REBIDX begin = P_POS;  // point at beginning of match

    // The loop iterates across each Element's worth of "rule" in the rule
    // block.  Some of these rules just set `flags` and `continue`, so that
    // the flags will apply to the next rule item.  If the flag is PF_SET
    // or PF_ACROSS, then the `set_or_copy_word` pointers will be assigned
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

    const Element* quoted_set_or_copy_word = nullptr;

    REBINT mincount = 1;  // min pattern count
    REBINT maxcount = 1;  // max pattern count


    //==////////////////////////////////////////////////////////////////==//
    //
    // PRE-RULE PROCESSING SECTION
    //
    //==////////////////////////////////////////////////////////////////==//

    // For non-iterated rules, including setup for iterated rules.
    // The input index is not advanced here, but may be changed by
    // a GET-WORD variable.

  pre_rule: {

    const Element* rule = P_AT_END ? nullptr : P_RULE;

    /*Update_Expression_Start(L);*/  // only valid for evaluator levels now

    //=//// FIRST THINGS FIRST: CHECK FOR END /////////////////////////////=//

    if (rule == nullptr)
        goto do_signals;

    //=//// HANDLE BAR! (BEFORE GROUP!) ///////////////////////////////////=//

    // BAR!s cannot be abstracted.  If they could be, then you'd have to
    // run all GET-GROUP! `:(...)` to find them in alternates lists.
    //
    // Note: First test, so `[| ...anything...]` is a "no-op" match

    if (Is_Bar(rule))  // reached BAR! without a match failure, good!
        return Init_Integer(OUT, P_POS);  // indicate match @ current pos

    //=//// HANDLE COMMA! (BEFORE GROUP...?) //////////////////////////////=//

    // The R3-Alpha PARSE design wasn't based on any particular notion of
    // "instruction format"; it fiddled a lot of flags like PF_WHILE to know
    // what construct you were in.  So things like `parse "a" [some]` were not
    // set up to deliver errors in a sense of "keywords that take arguments".
    //
    // Hence in this formulation, an expression barrier is a little hard to
    // make.  PARSE should be rewritten in a better way, but until it is
    // the we have to intuit the rule situation.
    //
    // !!! For now we assume that a GROUP! evaluation to produce a comma
    // will just error, vs. be okay in interstitial positions.  But unlike
    // BAR! there's no scan skipping that *requires* commas to be at source
    // level, so this could be relaxed if there was a good reason to.

    if (Is_Comma(rule)) {
        if (mincount != 1 or maxcount != 1 or (P_FLAGS & PF_STATE_MASK))
            panic (Error_Expression_Barrier_Raw());
        FETCH_NEXT_RULE(L);
        goto pre_rule;
    }

    //=//// (GROUP!) PROCESSING /////////////////////////=//

  reparse_rule:

    if (Is_Group(rule)) {

        // Code below may jump here to re-process groups, consider:
        //
        //    rule: just (print "Hi")
        //    parse "a" [inline ($rule) "a"]
        //
        // First it processes the group to get RULE, then it looks that
        // up and gets another group.  In theory this could continue
        // indefinitely, but for now a GET-GROUP! can't return another.

      process_group: {

        bool veto;
        if (Process_Group_For_Parse_Throws(&veto, L, rule))
            return THROWN;

        if (veto) {
            Init_Nulled(ARG(POSITION));  // treat as mismatch
            goto post_match_processing;
        }

        FETCH_NEXT_RULE(L);  // ignore result and go on to next rule
        goto pre_rule;
    }}
    else {
        // If we ran the GROUP! then that invokes the evaluator, and so
        // we already gave the GC and cancellation a chance to run.  But
        // if not, we might want to do it here... (?)

      do_signals:

        Update_Tick_If_Enabled();

        if (--g_ts.eval_countdown <= 0) {
            if (Do_Signals_Throws(LEVEL))
                return THROWN;
        }

        Maybe_Trampoline_Break_On_Tick(LEVEL);
    }


    if (rule == nullptr)  // means at end
        goto return_position;  // done all needed to do for end position

    //=//// ANY-WORD?/ANY-PATH? PROCESSING ////////////////////////////////=//

    if (Is_Word(rule) or Is_Get_Word(rule) or Is_Set_Word(rule)) {
        Option(SymId) cmd = VAL_CMD(rule);
        if (cmd) {
            if (not Is_Word(rule)) {
                //
                // Command but not WORD! (COPY:, :THRU)
                //
                panic (Error_Parse3_Command(L));
            }

            assert(
                cast(int, cmd) >= MIN_SYM_PARSE3
                and cast(int, cmd) <= MAX_SYM_PARSE3
            );
            if (cast(int, cmd) >= MIN_SYM_PARSE3_MATCH)
                goto skip_pre_rule;

            switch (maybe cmd) {
              case SYM_SOME:
                assert(
                    (mincount == 1 or mincount == 0)  // could be OPT SOME
                    and maxcount == 1
                );  // true on entry
                P_FLAGS |= PF_LOOPING;
                maxcount = INT32_MAX;
                FETCH_NEXT_RULE(L);
                goto pre_rule;

              case SYM_OPT:
              case SYM_OPTIONAL:
                P_FLAGS |= PF_OPTIONAL;
                mincount = 0;
                FETCH_NEXT_RULE(L);
                goto pre_rule;

              case SYM_TRY:
                P_FLAGS |= PF_TRY;
                mincount = 0;
                FETCH_NEXT_RULE(L);
                goto pre_rule;

              case SYM_REPEAT: {
                //
                // !!! OPT REPEAT (N) RULE can't work because OPT is done by
                // making the minimum number of match counts zero.  But
                // unfortunately if that rule isn't in a BLOCK! then the
                // 0 repeat rule transfers onto the rule... making it act like
                // `REPEAT (N) OPT RULE` which is not the same.
                //

                if (mincount != 1 or maxcount != 1)
                    panic (
                        "Old PARSE REPEAT does not mix with ranges or OPT"
                        " so put a block around the REPEAT or use UPARSE!"
                    );

                FETCH_NEXT_RULE(L);
                if (Is_Group(P_RULE)) {
                    if (Eval_Value_Throws(OUT, P_RULE, P_RULE_BINDING))
                        goto return_thrown;
                } else {
                    Derelativize(OUT, P_RULE, P_RULE_BINDING);
                }

                Value* out = require (Decay_If_Unstable(OUT));
                if (Is_Integer(out)) {
                    mincount = Int32s(out, 0);
                    maxcount = Int32s(out, 0);
                } else {
                    if (
                        not Is_Block(out)
                        or not (
                            Series_Len_At(out) == 2
                            and Is_Integer(List_Item_At(out))
                            and Is_Integer(List_Item_At(out) + 1)
                        )
                    ){
                        panic ("REPEAT takes INTEGER! or length 2 BLOCK! range");
                    }

                    mincount = Int32s(List_Item_At(out), 0);
                    maxcount = Int32s(List_Item_At(out) + 1, 0);

                    if (maxcount < mincount)
                        panic ("REPEAT range can't have lower max than minimum");
                }

                Erase_Cell(OUT);

                FETCH_NEXT_RULE(L);
                goto pre_rule; }

              case SYM_FURTHER:  // require advancement
                P_FLAGS |= PF_FURTHER;
                FETCH_NEXT_RULE(L);
                goto pre_rule;

              case SYM_LET:
                FETCH_NEXT_RULE(L);

                if (not (Is_Word(P_RULE) or Is_Set_Word(P_RULE)))
                    panic (Error_Parse3_Variable(L));

                if (VAL_CMD(P_RULE))  // set set [...]
                    panic (Error_Parse3_Command(L));

                // We need to add a new binding before we derelativize w.r.t.
                // the in-effect binding.
                //
                if (cmd == SYM_LET) {
                    Tweak_Cell_Binding(Feed_Data(L->feed), Make_Let_Variable(
                        Word_Symbol(P_RULE),
                        P_RULE_BINDING
                    ));
                    if (Is_Word(P_RULE)) {  // no further action
                        FETCH_NEXT_RULE(L);
                        goto pre_rule;
                    }
                    rule = P_RULE;
                    goto handle_set;
                }

                quoted_set_or_copy_word = Quotify(Derelativize(
                    LOCAL(LOOKBACK), P_RULE, P_RULE_BINDING
                ));
                FETCH_NEXT_RULE(L);
                goto pre_rule;

              case SYM_NOT_1: {  // see TO-C-NAME
                P_FLAGS |= PF_NOT;
                P_FLAGS ^= PF_NOT2;
                FETCH_NEXT_RULE(L);
                bool strict = false;
                if (not (
                    Is_Word(P_RULE)
                    and Word_Id(P_RULE) == SYM_AHEAD
                ) and not (
                    Is_Tag(P_RULE)
                    and 0 == CT_Utf8(
                        P_RULE,
                        g_tag_end,
                        strict
                    )
                )){
                    panic ("NOT must be NOT AHEAD or NOT <end> in PARSE3");
                }
                goto pre_rule; }

              case SYM_AHEAD:
                P_FLAGS |= PF_AHEAD;
                FETCH_NEXT_RULE(L);
                goto pre_rule;

              case SYM_REMOVE:
                P_FLAGS |= PF_REMOVE;
                FETCH_NEXT_RULE(L);
                goto pre_rule;

              case SYM_INSERT:
                P_FLAGS |= PF_INSERT;
                FETCH_NEXT_RULE(L);
                goto post_match_processing;

              case SYM_CHANGE:
                P_FLAGS |= PF_CHANGE;
                FETCH_NEXT_RULE(L);
                goto pre_rule;

              case SYM_INLINE: {
                FETCH_NEXT_RULE(L);
                if (P_AT_END)
                    panic (Error_Parse3_End());

                if (not Is_Group(P_RULE))
                    panic (Error_Parse3_Rule());

                DECLARE_ATOM (eval);
                Flags flags = LEVEL_MASK_NONE;
                if (Eval_Any_List_At_Core_Throws(  // note: might GC
                    eval,
                    flags,
                    P_RULE,
                    P_RULE_BINDING
                )) {
                    goto return_thrown;
                }

                if (Is_Void(eval) or Is_Ghost(eval))
                    goto pre_rule;

                if (Is_Error(eval)) {
                    if (Is_Error_Veto_Signal(Cell_Error(eval))) {
                        Init_Nulled(ARG(POSITION));  // treat as mismatch
                        goto post_match_processing;
                    }
                    panic (Cell_Error(eval));
                }

                required (Decay_If_Unstable(eval));
                if (Is_Antiform(eval))
                    panic (Error_Bad_Antiform(eval));

                rule = Copy_Cell(P_SAVE, Known_Element(eval));

                goto reparse_rule; }

              case SYM_COND: {
                FETCH_NEXT_RULE(L);
                if (P_AT_END)
                    panic (Error_Parse3_End());

                if (not Is_Group(P_RULE))
                    panic (Error_Parse3_Rule());

                DECLARE_ATOM (eval);
                if (Eval_Any_List_At_Throws(  // note: might GC
                    eval,
                    P_RULE,
                    P_RULE_BINDING
                )) {
                    goto return_thrown;
                }

                FETCH_NEXT_RULE(L);

                Value* condition = require (Decay_If_Unstable(eval));

                bool cond = require (Test_Conditional(condition));
                if (cond)
                    goto pre_rule;

                Init_Nulled(ARG(POSITION));  // not found
                goto post_match_processing; }

              case SYM_ACCEPT: {
                //
                // ACCEPT means different things in Rebol2/Red (synonym for
                // BREAK) where in UPARSE it means RETURN.
                //
                FETCH_NEXT_RULE(L);

                DECLARE_ATOM (thrown_arg);
                if (Is_Tag(P_RULE)) {
                    if (rebUnboxLogic(P_RULE, "= <here>"))
                        Copy_Cell(thrown_arg, ARG(POSITION));
                    else
                        panic ("PARSE3 ACCEPT TAG! only works with <here>");
                }
                else if (Is_Group(P_RULE)) {
                    if (Eval_Value_Throws(thrown_arg, P_RULE, P_RULE_BINDING))
                        goto return_thrown;
                }
                else
                    panic ("PARSE3 ACCEPT only works with GROUP! and <here>");

                Init_Thrown_With_Label(LEVEL, thrown_arg, LIB(PARSE_ACCEPT));
                goto return_thrown; }

              case SYM_BREAK: {
                //
                // This has to be throw-style, because it's not enough
                // to just say the current rule succeeded...it climbs
                // up and affects an enclosing parse loop.
                //
                DECLARE_ATOM (thrown_arg);
                Init_Integer(thrown_arg, P_POS);

                Init_Thrown_With_Label(LEVEL, thrown_arg, LIB(PARSE_BREAK));
                goto return_thrown; }

              case SYM_REJECT: {
                //
                // Similarly, this is a break/continue style "throw"
                //
                Init_Thrown_With_Label(LEVEL, LIB(NULL), LIB(PARSE_REJECT));
                goto return_thrown; }

              case SYM_VETO:  // skip to next alternate
                Init_Nulled(ARG(POSITION));  // not found
                FETCH_NEXT_RULE(L);
                goto post_match_processing;

              case SYM_SEEK: {
                FETCH_NEXT_RULE(L);  // skip the SEEK word
                // !!! what about `seek ^(first x)` ?
                HANDLE_SEEK_RULE_UPDATE_BEGIN(L, P_RULE, P_RULE_BINDING);
                FETCH_NEXT_RULE(L);  // e.g. skip the `x` in `seek x`
                goto pre_rule; }

              case SYM_AND_1:  // see TO-C-NAME
                panic ("Please replace PARSE3's AND with AHEAD");

              case SYM_WHILE:
                panic (
                    "Please replace PARSE3's WHILE with OPT SOME -or-"
                    " OPT FURTHER SOME--it's being reclaimed as arity-2."
                    " https://forum.rebol.info/t/1540/12"
                );

              case SYM_ANY:
                panic (
                    "Please replace PARSE3's ANY with OPT SOME"
                    " -- it's being reclaimed for a new construct"
                    " https://forum.rebol.info/t/1540/12"
                );

              case SYM_COPY:
                panic ("COPY not supported in PARSE3 (use SET-WORD!+ACROSS)");

              case SYM_SET:
                panic ("SET not supported in PARSE3 (use SET-WORD!)");

              case SYM_LIMIT:
                panic ("LIMIT not implemented");

              case SYM_RETURN:
                panic ("RETURN keyword switched to ACCEPT in PARSE3/UPARSE");

              default:  // the list above should be exhaustive
                assert(false);
            }

          skip_pre_rule:;

            // Any other WORD! with VAL_CMD() is a parse keyword, but is
            // a "match command", so proceed...
        }
        else {
            // It's not a PARSE command, get or set it

            // Historically SET-WORD! was used to capture the parse position.
            // However it is being repurposed as the tool for any form of
            // assignment...a new generalized SET.
            //
            // UPARSE2 should be used with code that wants the old semantics.
            // The performance on that should increase with time.
            //
            if (Is_Set_Word(rule)) {
                //
                // !!! Review meaning of marking the parse in a slot that
                // is a target of a rule, e.g. `thru pos: xxx`
                //
                // https://github.com/rebol/rebol-issues/issues/2269

                goto handle_set;
            }
            else if (Is_Get_Word(rule)) {
                panic ("GET-WORD! in modern PARSE is reserved (use SEEK)");
            }
            else {
                assert(Is_Word(rule));  // word - some other variable

                if (rule != P_SAVE) {
                    required (Get_Parse_Value(P_SAVE, rule, P_RULE_BINDING));

                    rule = P_SAVE;
                }
            }
        }
    }
    else if (Is_Tuple(rule)) {
        Value* spare = require (Get_Var(SPARE, NO_STEPS, rule, P_RULE_BINDING));

        if (Is_Datatype(spare)) {
            required (Init_Typechecker(u_cast(Value*, P_SAVE), spare));
            LIFT_BYTE(spare) = NOQUOTE_2;
            assert(Is_Frame(spare));
            rule = Known_Element(spare);
        }
        else if (Is_Antiform(spare))
            panic (Error_Bad_Antiform(spare));
        else
            rule = Copy_Cell(P_SAVE, Known_Element(spare));
    }
    else if (Is_Path(rule)) {
        Value* spare = require (
            Get_Var(SPARE, NO_STEPS, rule, P_RULE_BINDING)
        );

        if (not Is_Action(spare))
            panic ("PATH! in PARSE3 must be an ACTION!");

        LIFT_BYTE(spare) = NOQUOTE_2;
        rule = Copy_Cell(P_SAVE, Known_Element(spare));
    }
    else if (Is_Set_Tuple(rule)) {
      handle_set:
        quoted_set_or_copy_word = Quotify(
            Derelativize(LOCAL(LOOKBACK), rule, P_RULE_BINDING)
        );
        FETCH_NEXT_RULE(L);

        if (Is_Word(P_RULE) and Word_Id(P_RULE) == SYM_ACROSS) {
            FETCH_NEXT_RULE(L);
            P_FLAGS |= PF_ACROSS;
            goto pre_rule;
        }

        // Permit `pos: <here>` to act as setting the position
        //
        if (Is_Tag(P_RULE)) {
            bool strict = true;
            if (0 == CT_Utf8(P_RULE, g_tag_here, strict))
                FETCH_NEXT_RULE(L);
            else
                panic ("SET-WORD! works with <HERE> tag in PARSE3");

            required (Handle_Mark_Rule(L, quoted_set_or_copy_word));
            goto pre_rule;
        }

        P_FLAGS |= PF_SET;
        goto pre_rule;
    }

    if (Is_Bar(rule))
        panic ("BAR! must be source level (else PARSE can't skip it)");

    if (Is_Group(rule))
        goto process_group;  // GROUP! can make WORD! that fetches GROUP!


    //==////////////////////////////////////////////////////////////////==//
    //
    // ITERATED RULE PROCESSING SECTION
    //
    //==////////////////////////////////////////////////////////////////==//

  iterated_rule_processing: {

    // Repeats the same rule N times or until the rule fails.
    // The index is advanced and stored in a temp variable i until
    // the entire rule has been satisfied.

    FETCH_NEXT_RULE(L);

    begin = P_POS;  // input at beginning of match section

    // Some iterated rules have a parameter.  `3 into [some "a"]` will
    // actually run the INTO `rule` 3 times with the `subrule` of
    // `[some "a"]`.  Because it is iterated it is only captured the first
    // time through, nullptr indicates it's not been captured yet.
    //
    const Element* subrule = nullptr;

    REBINT count = 0;

    if (Is_Quasiform(rule)) {
        if (Is_Quasi_Word_With_Id(rule, SYM_OKAY))
            goto pre_rule;

        panic ("PARSE3 only supports ~okay~ quasiforms/antiforms");
    }
    else switch (maybe Type_Of(rule)) {
      case TYPE_INTEGER:  // Specify repeat count
        panic (
            "[1 2 rule] now illegal https://forum.rebol.info/t/1578/6"
            " (use REPEAT)"
        );

      case TYPE_TAG: {  // tag combinator in UPARSE, matches in UPARSE2
        bool strict = true;
        if (0 == CT_Utf8(rule, g_tag_here, strict)) {
            goto pre_rule;
        }
        if (0 == CT_Utf8(rule, g_tag_end, strict)) {
            goto handle_end;
        }
        panic ("Only TAG! combinators PARSE3 supports are <here> and <end>"); }

      default:
        break;  // fall through
    }

    while (count < maxcount) {
        assert(
            not Is_Bar(rule)
            and not Is_Integer(rule)
            and not Is_Group(rule)
        );  // these should all have been handled before iterated section

        REBIXO i;  // temp index point

        if (Is_Word(rule)) {
            Option(SymId) cmd = VAL_CMD(rule);

            switch (maybe cmd) {
              case SYM_SKIP:
                panic ("Use ONE instead of SKIP in PARSE3");

              case SYM_ONE:
                i = (P_POS < P_INPUT_LEN)
                    ? P_POS + 1
                    : END_FLAG;
                break;

              case SYM_TO:
              case SYM_THRU: {
                if (P_AT_END)
                    panic (Error_Parse3_End());

                if (subrule == nullptr) {  // capture only on iteration #1
                    required (Get_Parse_Value(P_SAVE, P_RULE, P_RULE_BINDING));

                    subrule = P_SAVE;
                    FETCH_NEXT_RULE(L);
                }

                bool is_thru = (cmd == SYM_THRU);

                if (Is_Block(subrule)) {
                    i = require (To_Thru_Block_Rule(L, subrule, is_thru));
                }
                else {
                    i = require (To_Thru_Non_Block_Rule(L, subrule, is_thru));
                }
                break; }

              case SYM_THE: {
                if (not Stub_Holds_Cells(P_INPUT))
                    panic (Error_Parse3_Rule());  // see #2253

                if (P_AT_END)
                    panic (Error_Parse3_End());

                if (not subrule) {  // capture only on iteration #1
                    subrule = Copy_Cell(LOCAL(LOOKBACK), P_RULE);
                    FETCH_NEXT_RULE(L);
                }

                const Element* input_tail = Array_Tail(P_INPUT_ARRAY);
                const Element* cmp = Array_At(P_INPUT_ARRAY, P_POS);

                if (cmp == input_tail)
                    i = END_FLAG;
                else {
                    bool equal = require (
                        Equal_Values(cmp, subrule, did (P_FLAGS & AM_FIND_CASE))
                    );
                    if (equal)
                        i = P_POS + 1;
                    else
                        i = END_FLAG;
                }
                break; }

              case SYM_INTO: {
                if (P_AT_END)
                    panic (Error_Parse3_End());

                if (subrule == nullptr) {  // capture only on iteration #1
                    required (Get_Parse_Value(P_SAVE, P_RULE, P_RULE_BINDING));

                    subrule = P_SAVE;
                    FETCH_NEXT_RULE(L);
                }

                if (not Is_Block(subrule))
                    panic (Error_Parse3_Rule());

                // parse ["aa"] [into ["a" "a"]] ; is legal
                // parse "aa" [into ["a" "a"]] ; is not...already "into"
                //
                if (not Stub_Holds_Cells(P_INPUT))
                    panic (Error_Parse3_Rule());

                const Element* input_tail = Array_Tail(P_INPUT_ARRAY);
                const Element* into = Array_At(P_INPUT_ARRAY, P_POS);
                if (into == input_tail) {
                    i = END_FLAG;  // `parse [] [into [...]]`, rejects
                    break;
                }
                else if (Any_Sequence(into)) {  // need position, alias BLOCK!
                    Derelativize(SPARE, into, P_INPUT_SPECIFIER);

                    into = Blockify_Any_Sequence(cast(Element*, SPARE));
                }
                else if (not Any_Series(into)) {
                    i = END_FLAG;  // `parse [1] [into [...]`, rejects
                    break;
                }

                Level* sub = require (Make_Level_At_Inherit_Const(
                    &Action_Executor,  // !!! Parser_Executor?
                    subrule, P_RULE_BINDING,
                    LEVEL_MASK_NONE
                ));

                bool interrupted;
                if (Subparse_Throws(
                    &interrupted,
                    OUT,
                    into,
                    P_INPUT_SPECIFIER,  // harmless if specified API value
                    sub,
                    (P_FLAGS & PF_FIND_MASK)  // PF_ONE_RULE?
                )){
                    goto return_thrown;
                }

                // !!! ignore interrupted? (e.g. ACCEPT or REJECT ran)

                if (Is_Light_Null(OUT)) {
                    i = END_FLAG;
                }
                else {
                    Element* out = Known_Element(OUT);
                    if (VAL_INT32(out) != Series_Len_Head(into))
                        i = END_FLAG;
                    else
                        i = P_POS + 1;
                }

                if (Is_Api_Value(into))
                    rebRelease(m_cast(Value*, into));  // !!! or use SPARE?

                Erase_Cell(OUT);  // restore invariant
                break; }

              case SYM_QUOTE:
                panic ("Use THE instead of QUOTE in PARSE3 for literal match");

              case SYM_END:
                panic ("Use <end> instead of END in PARSE3");

              default:
                panic (Error_Parse3_Rule());
            }
        }
        else if (Is_Block(rule)) {  // word fetched block, or inline block

            Level* sub = require (Make_Level_At_Core(
                &Action_Executor,  // !!! Parser_Executor?
                rule, rule_binding(),
                LEVEL_MASK_NONE
            ));

            bool interrupted;
            if (Subparse_Throws(
                &interrupted,
                SPARE,
                Element_ARG(POSITION),
                SPECIFIED,
                sub,
                (P_FLAGS & PF_FIND_MASK)  // no PF_ONE_RULE
            )){
                return THROWN;
            }

            // Non-breaking out of loop instances of match or not.

            if (Is_Light_Null(SPARE))
                i = END_FLAG;
            else {
                Value* spare = Known_Element(SPARE);
                assert(Is_Integer(spare));
                i = VAL_INT32(spare);
            }

            if (interrupted) {  // ACCEPT or REJECT ran
                assert(i != THROWN_FLAG);
                if (i == END_FLAG)
                    Init_Nulled(ARG(POSITION));
                else
                    P_POS = i;
                break;
            }
        }
        else if (false) {
          handle_end:
            count = 0;
            i = (P_POS < P_INPUT_LEN)
                ? END_FLAG
                : P_INPUT_LEN;
        }
        else {
            // Parse according to datatype

            i = require (Parse_One_Rule(L, P_POS, rule));
            if (i == THROWN_FLAG)
                return THROWN;
        }

        assert(i != THROWN_FLAG);

        // i: indicates new index or failure of the *match*, but
        // that does not mean failure of the *rule*, because optional
        // matches can still succeed when the last match failed.
        //
        if (i == END_FLAG) {  // this match failed
            if (count < mincount) {
                Init_Nulled(ARG(POSITION));  // num matches not enough
            }
            else {
                // just keep index as is.
            }
            break;
        }

        count++;  // may overflow to negative
        if (count < 0)
            count = INT32_MAX;  // the forever case

        // If FURTHER was used then the parse must advance the input; it can't
        // be at the saem position.
        //
        if (P_POS == i and (P_FLAGS & PF_FURTHER)) {
            if (not (P_FLAGS & PF_LOOPING))
                Init_Nulled(ARG(POSITION));  // fail the rule, not loop
            break;
        }

        P_POS = i;
    }

    // !!! This out of bounds check is necessary because GROUP!s execute
    // code that could change the size of the input.  The idea of locking
    // the input and only allowing mutations through PARSE rules has come
    // up...but at the very least, such checks should only be needed right
    // after potential group executions (which includes subrules).
    //
    if (not Is_Nulled(ARG(POSITION)))
        if (P_POS > P_INPUT_LEN)
            Init_Nulled(ARG(POSITION));  // not found


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

} post_match_processing: {

    if (P_FLAGS & PF_STATE_MASK) {
        if (P_FLAGS & PF_NOT) {
            if ((P_FLAGS & PF_NOT2) and not Is_Nulled(ARG(POSITION)))
                Init_Nulled(ARG(POSITION));  // not found
            else {
                Copy_Cell(ARG(POSITION), ARG(INPUT));
                P_POS = begin;
            }
        }

        if (not Is_Nulled(ARG(POSITION))) {
            //
            // Set count to how much input was advanced
            //
            REBINT count = (begin > P_POS) ? 0 : P_POS - begin;

            if (P_FLAGS & PF_ACROSS) {
                if (Any_List_Type(P_HEART)) {
                    //
                    // Act like R3-Alpha in preserving GROUP! vs. BLOCK!
                    // distinction (which Rebol2 did not).  But don't keep
                    // SET-XXX! or GET-XXX! (like how quoting is not kept)
                    //
                    Init_Any_List(
                        OUT,
                        P_HEART == TYPE_GROUP ? TYPE_GROUP : TYPE_BLOCK,
                        Copy_Source_At_Max_Shallow(
                            P_INPUT_ARRAY,
                            begin,
                            count
                        )
                    );
                }
                else if (P_HEART == TYPE_BLOB) {
                    Binary* bin = require (
                        Copy_Binary_At_Len(P_INPUT_BINARY, begin, count)
                    );

                    Init_Blob(OUT, bin);
                }
                else {
                    assert(Any_String_Type(P_HEART));

                    DECLARE_ATOM (begin_val);
                    Init_Series_At(begin_val, P_HEART, P_INPUT, begin);

                    // Rebol2 behavior of always "netural" TEXT!.  Avoids
                    // creation of things like URL!-typed fragments that
                    // have no scheme:// at their head, or getting <bc>
                    // out of <abcd> as if `<b` or `c>` had been found.
                    //
                    Strand* copy = require (
                        Copy_String_At_Limit(begin_val, &count)
                    );
                    Init_Text(OUT, copy);
                }

                // !!! As we are losing the datatype here, it doesn't make
                // sense to carry forward the quoting on the input.  It is not
                // obvious what marking a position should do.

                rebElide(CANON(SET), quoted_set_or_copy_word, Liftify(OUT));
                Erase_Cell(OUT);
            }
            else if (P_FLAGS & PF_SET) {
                if (count > 1)
                    panic (Error_Parse3_Multi_Set_Raw());

                if (count == 0) {
                    //
                    // !!! Right now, a rule like `set x group!` will leave x
                    // alone if you don't match.  (This is the same as
                    // `opt set x group!`).  Instead of being a synonym, the
                    // behavior of unsetting x has been considered, and to
                    // require saying `opt set x group!` to get the no-op.
                    // But `opt x: group!` will set x to null on no match.
                    //
                    // Note: It should be `x: try group!` but R3-Alpha parse
                    // is hard to get composability on such things.
                    //
                    if (P_FLAGS & PF_TRY)  // don't just leave alone
                        Init_Nulled(OUT);
                    else if (P_FLAGS & PF_OPTIONAL)
                        panic ("Cannot assign OPT VOID to variable in PARSE3");
                }
                else if (Stub_Holds_Cells(P_INPUT)) {
                    assert(count == 1);  // check for > 1 would have errored

                    Copy_Cell(OUT, Array_At(P_INPUT_ARRAY, begin));
                }
                else {
                    assert(count == 1);  // check for > 1 would have errored

                    if (P_HEART == TYPE_BLOB)
                        Init_Integer(OUT, *Binary_At(P_INPUT_BINARY, begin));
                    else
                        Init_Char_Unchecked(
                            OUT,
                            Get_Strand_Char_At(P_INPUT_STRING, begin)
                        );
                }

                rebElide(CANON(SET), quoted_set_or_copy_word, Liftify(OUT));
                Erase_Cell(OUT);
            }

            if (P_FLAGS & PF_REMOVE) {
                Ensure_Mutable(ARG(POSITION));
                if (count)
                    Remove_Any_Series_Len(Element_ARG(POSITION), begin, count);
                P_POS = begin;
            }

            if (P_FLAGS & (PF_INSERT | PF_CHANGE)) {
                count = (P_FLAGS & PF_INSERT) ? 0 : count;
                if (P_AT_END)
                    panic (Error_Parse3_End());

                // new value...comment said "CHECK FOR QUOTE!!"

                required (Get_Parse_Value(P_SAVE, P_RULE, P_RULE_BINDING));

                rule = P_SAVE;
                FETCH_NEXT_RULE(L);

                if (not Is_Group(rule))
                    panic ("Splicing (...) only in PARSE3's CHANGE or INSERT");

                DECLARE_VALUE (evaluated);
                Context* derived = Derive_Binding(
                    P_RULE_BINDING,
                    rule
                );

              perform_evaluation: {

                Atom* atom_evaluated = evaluated;
                if (Eval_Any_List_At_Throws(
                    atom_evaluated,
                    rule,
                    derived
                )){
                    goto return_thrown;
                }
                required (Decay_If_Unstable(atom_evaluated));

            } handle_result: {

                if (Stub_Holds_Cells(P_INPUT)) {
                    REBLEN mod_flags = (P_FLAGS & PF_INSERT) ? 0 : AM_PART;
                    if (Any_List(evaluated))  // bootstrap r3 has no SPREAD
                        Splicify(evaluated);

                    // Note: We could check for mutability at the start
                    // of the operation -but- by checking right at the
                    // last minute that allows protects or unprotects
                    // to happen in rule processing if GROUP!s execute.
                    //
                    Source* a = Cell_Array_Ensure_Mutable(ARG(POSITION));
                    P_POS = require (Modify_Array(
                        a,
                        begin,
                        (P_FLAGS & PF_CHANGE)
                            ? SYM_CHANGE
                            : SYM_INSERT,
                        evaluated,
                        mod_flags,
                        count,
                        1
                    ));
                }
                else {
                    P_POS = begin;

                    REBLEN mod_flags = (P_FLAGS & PF_INSERT) ? 0 : AM_PART;

                    P_POS = require (Modify_String_Or_Blob(  // checks readonly
                        ARG(POSITION),
                        (P_FLAGS & PF_CHANGE)
                            ? SYM_CHANGE
                            : SYM_INSERT,
                        evaluated,
                        mod_flags,
                        count,
                        1
                    ));
                }
            }}

            if (P_FLAGS & PF_AHEAD)
                P_POS = begin;
        }

        P_FLAGS &= ~PF_STATE_MASK;  // reset any state-oriented flags
        quoted_set_or_copy_word = nullptr;
    }

    if (Is_Nulled(ARG(POSITION))) {
        if (P_FLAGS & PF_ONE_RULE)
            goto return_null;

        FETCH_TO_BAR_OR_END(L);
        if (P_AT_END)  // no alternate rule
            goto return_null;

        // Jump to the alternate rule and reset input
        //
        FETCH_NEXT_RULE(L);
        Copy_Cell(ARG(POSITION), ARG(INPUT));  // P_POS may be null
        begin = P_INPUT_IDX;
    }

    if (P_FLAGS & PF_ONE_RULE)  // don't loop
        goto return_position;

    assert((P_FLAGS & PF_STATE_MASK) == 0);

    begin = P_POS;
    mincount = maxcount = 1;
    goto pre_rule;

}} return_position: {

    return Init_Integer(OUT, P_POS);  // !!! return switched input series??

} return_null: {

    return Init_Nulled(OUT);

} return_thrown: {

    return THROWN;
}}


//
//  parse3: native [
//
//  "Parse series according to grammar rules"
//
//      return: "Parse product (return value may be what's passed to ACCEPT)"
//          [any-value?]
//
//      input "Input series to parse"
//          [<opt-out> any-series? any-sequence? any-utf8?]
//      rules "Rules to parse by"
//          [<opt-out> block!]
//      :case "Uses case-sensitive comparison"
//      :match "Return PARSE input instead of synthesized result"
//      :relax "Don't require reaching the tail of the input for success"
//  ]
//
DECLARE_NATIVE(PARSE3)
//
// https://forum.rebol.info/t/1084
//
// 1. The mechanics of PARSE actually require the input to be a series, since
//    it stores the "current" parse position as the index in that series cell.
//    But it's nice to be able to say (parse #aaabbb [some "a" some "b"])
//    instead of (parse as text! #aaabbb [some "a" some "b"]), or to be
//    able to parse sequences.  So we implicitly alias non-series types as
//    series in order to make the input more flexible.
{
    INCLUDE_PARAMS_OF_PARSE3;

    Element* input = Element_ARG(INPUT);
    Element* rules = Element_ARG(RULES);

    if (Any_Sequence(input)) {  // needs index [1]
        Blockify_Any_Sequence(input);
    }
    else if (Any_Utf8(input) and not Any_Series(input)) {  // needs index [1]
        Textify_Any_Utf8(input);  // <input> won't preserve input type :-/
    }

    assert(Any_Series(input));

    Level* sub = require (Make_Level_At(
        &Action_Executor,  // !!! Parser_Executor?
        rules,
        LEVEL_MASK_NONE
    ));

    bool interrupted;
    if (Subparse_Throws(
        &interrupted,
        OUT,
        input, SPECIFIED,
        sub,
        (Bool_ARG(CASE) ? AM_FIND_CASE : 0)
        //
        // We always want "case-sensitivity" on binary bytes, vs. treating
        // as case-insensitive bytes for ASCII characters.
    )){
        // Any PARSE-specific THROWs (where a PARSE directive jumped the
        // stack) should be handled here.  ACCEPT is one example.

        const Value* label = VAL_THROWN_LABEL(LEVEL);
        if (Is_Frame(label)) {
            if (Frame_Phase(label) == Frame_Phase(LIB(PARSE_ACCEPT))) {
                CATCH_THROWN(OUT, LEVEL);
                return OUT;
            }
        }

        return THROWN;
    }

    if (Is_Light_Null(OUT)) {  // a match failed (but may be at end of input)
        if (Bool_ARG(MATCH))
            return NULLED;
        return fail (Error_Parse3_Incomplete_Raw());
    }

    REBLEN index = VAL_UINT32(Known_Element(OUT));
    assert(index <= Series_Len_Head(input));

    if (index != Series_Len_Head(input)) {  // didn't reach end of input
        if (Bool_ARG(MATCH))
            return NULLED;
        if (not Bool_ARG(RELAX))
            return fail (Error_Parse3_Incomplete_Raw());
    }

    if (Bool_ARG(MATCH))
        return COPY(ARG(INPUT));

    return TRIPWIRE;  // no synthesized result in PARSE3 unless ACCEPT
}


//
//  parse-accept: native [
//
//  "Accept argument as parse result (Internal Implementation Detail ATM)"
//
//      return: [<divergent>]
//  ]
//
DECLARE_NATIVE(PARSE_ACCEPT)
//
// !!! This was not created for user usage, but rather as a label for the
// internal throw used to indicate "accept".
{
    INCLUDE_PARAMS_OF_PARSE_ACCEPT;

    panic ("PARSE-ACCEPT is for internal PARSE use only");
}


//
//  parse-break: native [
//
//  "Break the current parse rule (Internal Implementation Detail ATM)"
//
//      return: [<divergent>]
//  ]
//
DECLARE_NATIVE(PARSE_BREAK)
//
// !!! This was not created for user usage, but rather as a label for the
// internal throw used to indicate "break".
{
    INCLUDE_PARAMS_OF_PARSE_BREAK;

    panic ("PARSE-BREAK is for internal PARSE use only");
}


//
//  parse-reject: native [
//
//  "Reject the current parse rule (Internal Implementation Detail ATM)"
//
//      return: [<divergent>]
//  ]
//
DECLARE_NATIVE(PARSE_REJECT)
//
// !!! This was not created for user usage, but rather as a label for the
// internal throw used to indicate "reject".
{
    INCLUDE_PARAMS_OF_PARSE_REJECT;

    panic ("PARSE-REJECT is for internal PARSE use only");
}
