//
//  File: %c-combinator.c
//  Summary: "Makes Function Suitable for Use As a PARSE Keyword"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2021 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the GNU Lesser General Public License (LGPL), Version 3.0.
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.en.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The idea behind a combinator is that a function follows a standard set of
// inputs and outputs to make it fit into an ecology of parsing operations.
// At its most basic level, this function takes in a position in an input
// series and then returns an indication of how much input it consumed (the
// "remainder") as well as a synthesized value.  One of its possible return
// results is a signal of failure, which is done by synthesizing a "pure" NULL
// (as opposed to a null isotope).
//
// So one of the reasons to have a separate COMBINATOR function generator is
// to force some of those implicit function arguments and returns.
//
// But another reason is to get a hook into each time a combinator is executed.
// Without such a hook, there'd be no way to centrally know when combinators
// were being invoked (barring some more universal systemic trace facility),
// because combinators call each other without going through any intermediary
// requesting service.  This also permits being able to know things like the
// furthest point in input that was reached--even if overall the parsing
// winded up not matching.
//

#include "sys-core.h"

enum {
    IDX_COMBINATOR_BODY = 1,  // Code wrapped up
    IDX_COMBINATOR_MAX
};

// !!! These are the positions that COMBINATOR has for its known arguments in
// the generated spec.  Changes to COMBINATOR could change this.
//
enum {
    IDX_COMBINATOR_PARAM_RETURN = 1,
    IDX_COMBINATOR_PARAM_REMAINDER,
    IDX_COMBINATOR_PARAM_STATE,
    IDX_COMBINATOR_PARAM_INPUT
};

// !!! With a native UPARSE, these would come from INCLUDE_PARAMS_OF_UPARSE.
// Until that happens, this could get out of sync with the index positions of
// the usermode function.
//
enum {
    IDX_UPARSE_PARAM_RETURN = 1,
    IDX_UPARSE_PARAM_FURTHEST,
    IDX_UPARSE_PARAM_PENDING,
    IDX_UPARSE_PARAM_SERIES,
    IDX_UPARSE_PARAM_RULES,
    IDX_UPARSE_PARAM_COMBINATORS,
    IDX_UPARSE_PARAM_CASE,
    IDX_UPARSE_PARAM_FULLY,
    IDX_UPARSE_PARAM_PART,  // Note: Fake /PART at time of writing!
    IDX_UPARSE_PARAM_VERBOSE,
    IDX_UPARSE_PARAM_LOOPS
};


//
//  Combinator_Dispactcher: C
//
// The main responsibilities of the combinator dispatcher is to provide a hook
// for verbose debugging, as well as to record the furthest point reached.
// At the moment we focus on the furthest point reached.
//
Bounce Combinator_Dispatcher(Frame(*) f)
{
    Action(*) phase = FRM_PHASE(f);
    Array(*) details = ACT_DETAILS(phase);
    Cell(*) body = ARR_AT(details, IDX_DETAILS_1);  // code to run

    Bounce b;
    if (IS_ACTION(body)) {  // NATIVE-COMBINATOR
        SET_SERIES_INFO(f->varlist, HOLD);  // mandatory for natives.
        Dispatcher* dispatcher = ACT_DISPATCHER(VAL_ACTION(body));
        b = dispatcher(f);
    }
    else {  // usermode COMBINATOR
        assert(IS_BLOCK(body));
        b = Func_Dispatcher(f);
    }

    if (b == BOUNCE_THROWN)
        return b;

    REBVAL *r = Value_From_Bounce(b);

    if (r == nullptr or Is_Nulled(r))
        return r;  // did not advance, don't update furthest

    // This particular parse succeeded, but did the furthest point exceed the
    // previously measured furthest point?  This only is a question that
    // matters if there was a request to know the furthest point...
    //
    REBVAL *state = FRM_ARG(f, IDX_COMBINATOR_PARAM_STATE);
    assert(IS_FRAME(state));  // combinators *must* have this as the UPARSE.
    Frame(*) frame_ = CTX_FRAME_MAY_FAIL(VAL_CONTEXT(state));
    REBVAL *furthest_word = FRM_ARG(frame_, IDX_UPARSE_PARAM_FURTHEST);
    if (Is_Nulled(furthest_word))
        return r;

    REBVAL *furthest_var = Lookup_Mutable_Word_May_Fail(
        furthest_word,
        SPECIFIED
    );

    REBVAL *remainder_word = FRM_ARG(f, IDX_COMBINATOR_PARAM_REMAINDER);
    const REBVAL *remainder_var = Lookup_Word_May_Fail(
        remainder_word,
        SPECIFIED
    );

    assert(VAL_SERIES(remainder_var) == VAL_SERIES(furthest_var));

    if (VAL_INDEX(remainder_var) > VAL_INDEX(furthest_var))
        Copy_Cell(furthest_var, remainder_var);

    return r;
}


//
//  Expanded_Combinator_Spec: C
//
// The original usermode version of this was:
//
//     compose [
//         ; Get the text description if given
//
//         ((if text? spec.1 [spec.1, elide spec: my next]))
//
//         ; Get the RETURN: definition if there is one, otherwise add one
//         ; so that we are sure that we know the position/order of the
//         ; arguments.
//
//         ((if set-word? spec.1 [
//             assert [spec.1 = 'return:]
//             assert [text? spec.2]
//             assert [block? spec.3]
//
//             reduce [spec.1 spec.2 spec.3]
//             elide spec: my skip 3
//         ] else [
//             [return: [<opt> <void> any-value!]]",
//         ]))
//
//         remainder: [<opt> any-series!]
//
//         state [frame!]
//         input [any-series!]
//
//         ((spec))  ; arguments the combinator takes, if any.
//      ]
//
// !!! Optimizing it was at first considered unnecessary because the speed
// at which combinators were created wasn't that important.  However, at the
// time of setting up native combinators there is no COMPOSE function available
// and the rebValue("...") function won't work, so it had to be hacked up as
// a handcoded routine.  Review.
//
Array(*) Expanded_Combinator_Spec(const REBVAL *original)
{
    StackIndex base = TOP_INDEX;

    Cell(const*) tail;
    Cell(const*) item = VAL_ARRAY_AT(&tail, original);
    REBSPC *specifier = VAL_SPECIFIER(original);

    if (IS_TEXT(item)) {
        Derelativize(PUSH(), item, specifier);  // {combinator description}
        if (item == tail) fail("too few combinator args");
        ++item;
    }
    Derelativize(PUSH(), item, specifier);  // return:
    if (item == tail) fail("too few combinator args");
    ++item;
    Derelativize(PUSH(), item, specifier);  // "return description"
    if (item == tail) fail("too few combinator args");
    ++item;
    Derelativize(PUSH(), item, specifier);  // [return type block]
    if (item == tail) fail("too few combinator args");
    ++item;

    const Byte utf8[] =
        "@remainder [any-series!]\n"
        "state [frame!]\n"
        "input [any-series!]\n";

    const void* packed[2] = {utf8, rebEND};  // BEWARE: Stack, can't Trampoline!

    Feed(*) feed = Make_Variadic_Feed(packed, nullptr, nullptr, FEED_MASK_DEFAULT);
    Sync_Feed_At_Cell_Or_End_May_Fail(feed);

    while (Not_Feed_At_End(feed)) {
        Derelativize(PUSH(), At_Feed(feed), FEED_SPECIFIER(feed));
        Fetch_Next_In_Feed(feed);
    }

    Free_Feed(feed);

    // Note: We pushed unbound code, won't find FRAME! etc.

    for (; item != tail; ++item)
        Derelativize(PUSH(), item, specifier);  // everything else

    // The scanned material is not bound.  The natives were bound into the
    // Lib_Context initially.  Hack around the issue by repeating that binding
    // on the product.
    //
    Array(*) expanded = Pop_Stack_Values(base);
    Bind_Values_Deep(ARR_HEAD(expanded), ARR_TAIL(expanded), Lib_Context_Value);

    return expanded;
}


//
//  combinator: native [
//
//  {Make a stylized ACTION! that fulfills the interface of a combinator}
//
//      return: [action!]
//      spec [block!]
//      body [block!]
//  ]
//
DECLARE_NATIVE(combinator)
{
    INCLUDE_PARAMS_OF_COMBINATOR;

    // This creates the expanded spec and puts it in a block which manages it.
    // That might not be needed if the Make_Paramlist_Managed() could take an
    // array and an index.
    //
    DECLARE_LOCAL (expanded_spec);
    Init_Block(expanded_spec, Expanded_Combinator_Spec(ARG(spec)));

    Context(*) meta;
    Flags flags = MKF_KEYWORDS | MKF_RETURN;
    Array(*) paramlist = Make_Paramlist_Managed_May_Fail(
        &meta,
        expanded_spec,
        &flags
    );

    Action(*) combinator = Make_Action(
        paramlist,
        nullptr,  // no partials
        &Combinator_Dispatcher,
        IDX_COMBINATOR_MAX  // details array capacity
    );

    // !!! As with FUNC, we copy and bind the block the user gives us.  This
    // means we will not see updates to it.  So long as we are copying it,
    // we might as well mutably bind it--there's no incentive to virtual
    // bind things that are copied.
    //
    Array(*) relativized = Copy_And_Bind_Relative_Deep_Managed(
        ARG(body),
        combinator,
        VAR_VISIBILITY_ALL
    );

    Init_Relative_Block(
        ARR_AT(ACT_DETAILS(combinator), IDX_COMBINATOR_BODY),
        combinator,
        relativized
    );

    return Init_Activation(OUT, combinator, ANONYMOUS, UNBOUND);
}


//
//  Call_Parser_Throws: C
//
// This service routine does a faster version of something like:
//
//     REBVAL *result = rebValue("applique @", ARG(parser), "[",
//         "input:", rebQ(ARG(input)),  // quote avoids becoming const
//         "remainder: @", ARG(remainder),
//     "]");
//
// But it only works on parsers that were created from specializations of
// COMBINATOR or NATIVE-COMBINATOR.  Because it expects the parameters to be
// in the right order in the frame.
//
void Push_Parser_Subframe(
    REBVAL *out,
    const REBVAL *remainder,
    const REBVAL *parser,
    const REBVAL *input
){
    assert(ANY_SERIES(input));
    assert(IS_ACTION(parser));

    Context(*) ctx = Make_Context_For_Action(parser, TOP_INDEX, nullptr);

    const REBKEY* remainder_key = CTX_KEY(ctx, IDX_COMBINATOR_PARAM_REMAINDER);
    const REBKEY* input_key = CTX_KEY(ctx, IDX_COMBINATOR_PARAM_INPUT);
    if (
        KEY_SYM(remainder_key) != SYM_REMAINDER
        or KEY_SYM(input_key) != SYM_INPUT
    ){
        fail ("Push_Parser_Subframe() only works on unadulterated combinators");
    }

    Copy_Cell(CTX_VAR(ctx, IDX_COMBINATOR_PARAM_REMAINDER), remainder);
    Copy_Cell(CTX_VAR(ctx, IDX_COMBINATOR_PARAM_INPUT), input);

    DECLARE_LOCAL (temp);  // can't overwrite spare
    Init_Frame(temp, ctx, ANONYMOUS);

    bool pushed = Pushed_Continuation(
        out,
        FRAME_MASK_NONE,
        SPECIFIED,
        temp,
        nullptr  // with
    );
    assert(pushed);  // always needs to push, it's a frame
    UNUSED(pushed);
}


//
//  opt-combinator: native/combinator [
//
//  {If supplied parser fails, succeed anyway without advancing the input}
//
//      return: "PARSER's result if it succeeds, otherwise ~_~ isotope"
//          [any-value!]
//      parser [action!]
//  ]
//
DECLARE_NATIVE(opt_combinator)
{
    INCLUDE_PARAMS_OF_OPT_COMBINATOR;

    Value(*) remainder = ARG(remainder);  // output (combinator implicit)

    Value(*) input = ARG(input);  // combinator implicit
    Value(*) parser = ARG(parser);
    UNUSED(ARG(state));  // combinator implicit

    enum {
        ST_OPT_COMBINATOR_INITIAL_ENTRY = STATE_0,
        ST_OPT_COMBINATOR_RUNNING_PARSER
    };

    switch (STATE) {
      case ST_OPT_COMBINATOR_INITIAL_ENTRY :
        goto initial_entry;

      case ST_OPT_COMBINATOR_RUNNING_PARSER :
        goto parser_result_in_out;

      default : assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Push_Parser_Subframe(OUT, remainder, parser, input);

    STATE = ST_OPT_COMBINATOR_RUNNING_PARSER;
    return CATCH_CONTINUE_SUBFRAME(SUBFRAME);

} parser_result_in_out: {  ///////////////////////////////////////////////////

    if (not Is_Nulled(OUT))  // parser succeeded...
        return OUT;  // so return its result (note: may be null *isotope*)

    Set_Var_May_Fail(remainder, SPECIFIED, input);  // convey no progress made
    return Init_Heavy_Null(OUT);  // ...but still, non-NULL for success
}}


//
//  text!-combinator: native/combinator [
//
//  {Match a TEXT! value as an array item or at current position of bin/string}
//
//      return: "The rule series matched against (not input value)"
//          [<opt> text!]
//      value [text!]
//  ]
//
DECLARE_NATIVE(text_x_combinator)
{
    INCLUDE_PARAMS_OF_TEXT_X_COMBINATOR;

    Context(*) state = VAL_CONTEXT(ARG(state));
    bool cased = Is_Truthy(CTX_VAR(state, IDX_UPARSE_PARAM_CASE));

    Value(*) v = ARG(value);
    Value(*) input = ARG(input);

    if (ANY_ARRAY(input)) {
        Cell(const*) tail;
        Cell(const*) at = VAL_ARRAY_AT(&tail, input);
        if (at == tail)  // no item to match against
            return nullptr;
        if (Cmp_Value(at, v, true) != 0)  // not case-insensitive equal
            return nullptr;

        ++VAL_INDEX_UNBOUNDED(input);
        Set_Var_May_Fail(ARG(remainder), SPECIFIED, input);

        Derelativize(OUT, at, VAL_SPECIFIER(input));
        return OUT;  // Note: returns item in array, not rule, when an array!
    }

    assert(ANY_STRING(input) or IS_BINARY(input));

    REBLEN len;
    REBINT index = Find_Value_In_Binstr(
        &len,
        input,
        VAL_LEN_HEAD(input),
        v,
        AM_FIND_MATCH | (cased ? AM_FIND_CASE : 0),
        1  // skip
    );
    if (index == NOT_FOUND)
        return nullptr;

    assert(cast(REBLEN, index) == VAL_INDEX(input));  // asked for AM_FIND_MATCH
    VAL_INDEX_UNBOUNDED(input) += len;
    Set_Var_May_Fail(ARG(remainder), SPECIFIED, input);

    // If not an array, we have return the rule on match since there's
    // no isolated value to capture.

    return COPY(v);
}


//
//  some-combinator: native/combinator [
//
//  {Must run at least one match}
//
//      return: "Result of last successful match"
//          [<opt> any-value!]
//      parser [action!]
//  ]
//
DECLARE_NATIVE(some_combinator)
//
// 1. If we don't put a phase on this, then it will pay attention to the
//    FRAME_HAS_BEEN_INVOKED flag and prohibit things like STOP from advancing
//    the input because `f.input` assignment will raise an error.  Review.
//
// 2. Currently the usermode parser has no support for intercepting throws
//    removing frames from the loops list in usermode.  Mirror that limitation
//    here in the native implementation for now.
//
// 3. There's no guarantee that a parser that fails leaves the remainder as-is
//    (in fact multi-returns have historically unset variables to hide their
//    previous values from acting as input).  So we have to put the remainder
//    back to the input we just tried but didn't work.
{
    INCLUDE_PARAMS_OF_SOME_COMBINATOR;

    Value(*) remainder = ARG(remainder);
    Value(*) parser = ARG(parser);
    Value(*) input = ARG(input);

    Value(*) state = ARG(state);
    Array(*) loops = VAL_ARRAY_ENSURE_MUTABLE(
        CTX_VAR(VAL_CONTEXT(state), IDX_UPARSE_PARAM_LOOPS)
    );

    enum {
        ST_SOME_COMBINATOR_INITIAL_ENTRY = STATE_0,
        ST_SOME_COMBINATOR_FIRST_PARSER_RUN,
        ST_SOME_COMBINATOR_LATER_PARSER_RUN
    };

    switch (STATE) {
      case ST_SOME_COMBINATOR_INITIAL_ENTRY :
        goto initial_entry;

      case ST_SOME_COMBINATOR_FIRST_PARSER_RUN :
        goto first_parse_result_in_out;

      case ST_SOME_COMBINATOR_LATER_PARSER_RUN :
        goto later_parse_result_in_spare;

      default : assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Cell(*) loop_last = Alloc_Tail_Array(loops);
    Init_Frame(loop_last, CTX(frame_->varlist), Canon(SOME));
    INIT_VAL_FRAME_PHASE(loop_last, FRM_PHASE(frame_));  // need phase, see [1]

    Push_Parser_Subframe(OUT, remainder, parser, input);

    STATE = ST_SOME_COMBINATOR_FIRST_PARSER_RUN;
    return CONTINUE_SUBFRAME(SUBFRAME);  // mirror usermode, see [2]

} first_parse_result_in_out: {  //////////////////////////////////////////////

    if (Is_Nulled(OUT)) {  // didn't match even once, so not enough
        Remove_Series_Units(loops, ARR_LEN(loops) - 1, 1);  // drop loop
        return nullptr;
    }

} call_parser_again: {  //////////////////////////////////////////////////////

    Get_Var_May_Fail(
        input,
        remainder, // remainder from previous call becomes new input
        SPECIFIED,
        true
    );

    Push_Parser_Subframe(SPARE, remainder, parser, input);

    STATE = ST_SOME_COMBINATOR_LATER_PARSER_RUN;
    return CONTINUE_SUBFRAME(SUBFRAME);

} later_parse_result_in_spare: {  ////////////////////////////////////////////

    if (Is_Nulled(SPARE)) {  // first still succeeded, so we're okay.
        Set_Var_May_Fail(remainder, SPECIFIED, input);  // put back, see [3]
        Remove_Series_Units(loops, ARR_LEN(loops) - 1, 1);  // drop loop
        return OUT;  // return previous successful parser result
    }

    Move_Cell(OUT, SPARE);  // update last successful result
    goto call_parser_again;
}}


//
//  further-combinator: native/combinator [
//
//  {Pass through the result only if the input was advanced by the rule}
//
//      return: "parser result if it succeeded and advanced input, else NULL"
//          [<opt> any-value!]
//      parser [action!]
//  ]
//
DECLARE_NATIVE(further_combinator)
{
    INCLUDE_PARAMS_OF_FURTHER_COMBINATOR;

    REBVAL *remainder = ARG(remainder);
    REBVAL *input = ARG(input);
    REBVAL *parser = ARG(parser);
    UNUSED(ARG(state));

    enum {
        ST_FURTHER_COMBINATOR_INITIAL_ENTRY = STATE_0,
        ST_FURTHER_COMBINATOR_RUNNING_PARSER
    };

    switch (STATE) {
      case ST_FURTHER_COMBINATOR_INITIAL_ENTRY :
        goto initial_entry;

      case ST_FURTHER_COMBINATOR_RUNNING_PARSER :
        goto parser_result_in_out;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Push_Parser_Subframe(OUT, remainder, parser, input);

    STATE = ST_FURTHER_COMBINATOR_RUNNING_PARSER;
    return CATCH_CONTINUE_SUBFRAME(SUBFRAME);

} parser_result_in_out: {  ///////////////////////////////////////////////////

    if (Is_Nulled(OUT))
        return nullptr;  // the parse rule did not match

    Get_Var_May_Fail(SPARE, remainder, SPECIFIED, true);

    if (VAL_INDEX(SPARE) <= VAL_INDEX(input))
        return nullptr;  // the rule matched but did not advance the input

    return OUT;
}}


struct Combinator_Param_State {
    Context(*) ctx;
    Frame(*) frame_;
};

static bool Combinator_Param_Hook(
    const REBKEY *key,
    const REBPAR *param,
    Flags flags,
    void *opaque
){
    UNUSED(flags);

    struct Combinator_Param_State *s
        = cast(struct Combinator_Param_State*, opaque);

    Frame(*) frame_ = s->frame_;
    INCLUDE_PARAMS_OF_COMBINATORIZE;

    UNUSED(REF(path));  // used by caller of hook
    UNUSED(ARG(advanced));  // used by caller of hook

    option(SymId) symid = KEY_SYM(key);

    if (symid == SYM_INPUT or symid == SYM_REMAINDER) {
        //
        // The idea is that INPUT is always left unspecialized (a completed
        // parser produced from a combinator takes it as the only parameter).
        // And the REMAINDER is an output, so it's the callers duty to fill.
        //
        return true;  // keep iterating the parameters.
    }

    if (GET_PARAM_FLAG(param, REFINEMENT)) {
        //
        // !!! Behavior of refinements is a bit up in the air, the idea is
        // that refinements that don't take arguments can be supported...
        // examples would be things like KEEP/ONLY.  But refinements that
        // take arguments...e.g. additional rules...is open to discussion.
        //
        return true;  // just leave unspecialized for now
    }

    // we need to calculate what variable slot this lines up with.  Can be
    // done based on the offset of the param from the head.

    REBLEN offset = param - ACT_PARAMS_HEAD(VAL_ACTION(ARG(c)));
    REBVAR *var = CTX_VARS_HEAD(s->ctx) + offset;

    if (symid == SYM_STATE) {  // the "state" is currently the UPARSE frame
        Copy_Cell(var, ARG(state));
    }
    else if (symid == SYM_VALUE and REF(value)) {
        //
        // The "value" parameter only has special meaning for datatype
        // combinators, e.g. TEXT!.  Otherwise a combinator can have an
        // argument named value for other purposes.
        //
        Copy_Cell(var, ARG(value));
    }
    else switch (VAL_PARAM_CLASS(param)) {
      case PARAM_CLASS_HARD: {
        //
        // Quoted parameters represent a literal element captured from rules.
        //
        // !!! <skip>-able parameters would be useful as well.
        //
        Cell(const*) tail;
        Cell(const*) item = VAL_ARRAY_AT(&tail, ARG(rules));

        if (
            item == tail
            or (IS_COMMA(item) or IS_BAR(item) or IS_BAR_BAR(item))
        ){
            if (NOT_PARAM_FLAG(param, ENDABLE))
                fail ("Too few parameters for combinator");  // !!! Error_No_Arg
            Init_Nulled(var);
        }
        else {
            if (
                GET_PARAM_FLAG(param, SKIPPABLE)
                and not TYPE_CHECK(param, VAL_TYPE(item))
            ){
                Init_Nulled(var);
            }
            else {
                Derelativize(var, item, VAL_SPECIFIER(ARG(rules)));
                ++VAL_INDEX_UNBOUNDED(ARG(rules));
            }
        }
        break; }

      case PARAM_CLASS_NORMAL: {
        //
        // Need to make PARSIFY a native!  Work around it for now...
        //
        Cell(const*) tail;
        Cell(const*) item = VAL_ARRAY_AT(&tail, ARG(rules));
        if (
            item == tail
            or (IS_COMMA(item) or IS_BAR(item) or IS_BAR_BAR(item))
        ){
            if (NOT_PARAM_FLAG(param, ENDABLE))
                fail ("Too few parameters for combinator");  // !!! Error_No_Arg
            Init_Nulled(var);
        }
        else {
            // !!! Getting more than one value back from a libRebol API is not
            // currently supported.  Usermode code is not allowed to directly
            // write to native frame variables, so hack in a temporary here.
            // (could be done much more efficiently another way!)

            if (rebRunThrows(SPARE, "let temp"))
                assert(!"LET failed");
            REBVAL *parser = rebValue(
                "[@", SPARE, "]: parsify", ARG(state), ARG(rules)
            );
            bool any = false;
            Get_Var_May_Fail(ARG(rules), SPARE, SPECIFIED, any);
            Copy_Cell(var, parser);
            rebRelease(parser);
        }
        break; }

      default:
        fail ("COMBINATOR parameters must be normal or quoted at this time");
    }

    return true;  // want to see all parameters
}


//
//  combinatorize: native [
//
//  {Analyze combinator parameters in rules to produce a specialized "parser"}
//
//      return: "Parser function taking only input, returning value + remainder"
//          [action!]
//      @advanced [block!]
//      c "Parser combinator taking input, but also other parameters"
//          [action!]
//      rules [block!]
//      state "Parse State" [frame!]
//      /value "Initiating value (if datatype)" [any-value!]
//      /path "Invoking Path" [path!]
//  ]
//
DECLARE_NATIVE(combinatorize)
//
// While *parsers* take one argument (the input), *parser combinators* may take
// more.  If the arguments are quoted, then they are taken literally from the
// rules feed.  If they are not quoted, they will be another "parser" generated
// from the rules...that comes from UPARSE orchestrating the specialization of
// other "parser combinators".
//
// For instance: the old form of CHANGE took two arguments.  The first would
// still be a parser and has to be constructed with PARSIFY from the rules.
// But the replacement would be a literal value, e.g.
//
//      rebol2>> data: "aaabbb"
//      rebol2>> parse data [change some "a" "literal" some "b"]
//      == "literalbbb"
//
// So we see that CHANGE got SOME "A" turned into a parser action, but it
// received "literal" literally.  The definition of the combinator is used
// to determine the arguments and which kind they are.
{
    INCLUDE_PARAMS_OF_COMBINATORIZE;

    Action(*) act = VAL_ACTION(ARG(c));

    // !!! The hack for PATH! handling was added to make /ONLY work; it only
    // works for refinements with no arguments by looking at what's in the path
    // when it doesn't end in /.  Now /ONLY is not used.  Review general
    // mechanisms for refinements on combinators.
    //
    if (REF(path))
        fail ("PATH! mechanics in COMBINATORIZE not supported ATM");

    struct Combinator_Param_State s;
    s.ctx = Make_Context_For_Action(ARG(c), TOP_INDEX, nullptr);
    s.frame_ = frame_;

    PUSH_GC_GUARD(s.ctx);  // Combinator_Param_Hook may call evaluator

    USED(REF(state));
    USED(REF(value));
    For_Each_Unspecialized_Param(act, &Combinator_Param_Hook, &s);

    // Set the advanced parameter to how many rules were consumed (the hook
    // steps through ARG(rules), updating its index)
    //
    Copy_Cell(ARG(advanced), ARG(rules));

    Action(*) parser = Make_Action_From_Exemplar(s.ctx);
    DROP_GC_GUARD(s.ctx);

    Activatify(Init_Action(  // note: MAKE ACTION! copies the context, this won't
        OUT,
        parser,
        VAL_ACTION_LABEL(ARG(c)),
        VAL_ACTION_BINDING(ARG(c))
    ));

    return Proxy_Multi_Returns(frame_);
}
