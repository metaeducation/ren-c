//
//  file: %c-combinator.c
//  summary: "Makes Function Suitable for Use As a PARSE Keyword"
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
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
// (as opposed to a "heavy" null, that's wrapped in a block antiform).
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
    IDX_UPARSE_PARAM_PENDING,
    IDX_UPARSE_PARAM_SERIES,
    IDX_UPARSE_PARAM_RULES,
    IDX_UPARSE_PARAM_COMBINATORS,
    IDX_UPARSE_PARAM_CASE,
    IDX_UPARSE_PARAM_FULLY,
    IDX_UPARSE_PARAM_PART,  // Note: Fake :PART at time of writing!
    IDX_UPARSE_PARAM_VERBOSE,
    IDX_UPARSE_PARAM_LOOPS
};


//
//  Combinator_Dispatcher: C
//
// The main responsibilities of the combinator dispatcher is to provide a hook
// for verbose debugging, as well as to record the furthest point reached.
// At the moment we focus on the furthest point reached.
//
Bounce Combinator_Dispatcher(Level* L)
{
    Details* details = Ensure_Level_Details(L);
    Stable* body = Details_At(details, IDX_DETAILS_1);  // code to run

    Bounce b;
    if (Is_Frame(body)) {  // NATIVE-COMBINATOR
        Set_Flex_Info(Varlist_Array(L->varlist), HOLD);  // natives require
        assert(Is_Stub_Details(Frame_Phase(body)));
        Dispatcher* dispatcher = Details_Dispatcher(
            cast(Details*, Frame_Phase(body))
        );
        b = Apply_Cfunc(dispatcher, L);
    }
    else {  // usermode COMBINATOR
        assert(Is_Block(body));
        b = Func_Dispatcher(L);
    }

    if (b == BOUNCE_THROWN)
        return b;

    Value* r = Value_From_Bounce(b);

    if (r == nullptr or Is_Light_Null(r))
        return r;  // did not advance, don't update furthest

    // This particular parse succeeded, but did the furthest point exceed the
    // previously measured furthest point?  This only is a question that
    // matters if there was a request to know the furthest point...

    return r;
}

//
//  Combinator_Details_Querier: C
//
bool Combinator_Details_Querier(
    Sink(Stable) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Combinator_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_COMBINATOR);

    switch (property) {
      case SYM_RETURN_OF: {
        Stable* body = Details_At(details, IDX_DETAILS_1);  // code to run
        assert(Is_Frame(body));  // takes 1 arg (a FRAME!)

        Details* body_details = Phase_Details(Frame_Phase(body));
        DetailsQuerier* querier = Details_Querier(body_details);
        return (*querier)(out, body_details, SYM_RETURN_OF); }

      default:
        break;
    }

    return false;
}


//
//  Expanded_Combinator_Spec: C
//
// Add implicit STATE and INPUT fields to NATIVE:COMBINATOR spec.
//
// !!! INPUT is no longer implicit on combinators so you can name the input
// whatever you want.  Review.
//
Source* Expanded_Combinator_Spec(const Element* original)
{
    StackIndex base = TOP_INDEX;

    const Element* tail;
    const Element* item = List_At(&tail, original);
    Context* binding = List_Binding(original);

    assert(Is_Text(item));  // "combinator description"
    Derelativize(PUSH(), item, binding);
    assert(item != tail);
    ++item;

    assert(Is_Set_Word(item) and Word_Id(item) == SYM_RETURN);  // return:
    Derelativize(PUSH(), item, binding);
    assert(item != tail);
    ++item;

    assert(Is_Block(item));  // [return type block]
    Derelativize(PUSH(), item, binding);
    assert(item != tail);
    ++item;

    const Byte utf8[] =
        "state [frame!]\n"
        "input [any-series?]\n";

    const void* packed[2] = {utf8, rebEND};  // BEWARE: Stack, can't Trampoline!

    require (
      Feed* feed = Make_Variadic_Feed(packed, nullptr, FEED_MASK_DEFAULT)
    );
    Add_Feed_Reference(feed);
    Sync_Feed_At_Cell_Or_End_May_Panic(feed);

    while (Not_Feed_At_End(feed)) {
        Derelativize(PUSH(), At_Feed(feed), Feed_Binding(feed));
        Fetch_Next_In_Feed(feed);
    }

    Release_Feed(feed);

    // Note: We pushed unbound code, won't find FRAME! etc.

    for (; item != tail; ++item)
        Derelativize(PUSH(), item, binding);  // everything else

    return Pop_Source_From_Stack(base);
}


//
//  combinator: native [
//
//  "Make stylized code that fulfills the interface of a combinator"
//
//      return: [frame!]
//      spec [block!]
//      body [block!]
//  ]
//
DECLARE_NATIVE(COMBINATOR)
{
    INCLUDE_PARAMS_OF_COMBINATOR;

    Element* spec = Element_ARG(SPEC);
    Element* body = Element_ARG(BODY);

    // This creates the expanded spec and puts it in a block which manages it.
    // That might not be needed if Make_Paramlist_Managed() could take an
    // array and an index.
    //
    Sink(Element) expanded_spec = SCRATCH;
    Init_Block(expanded_spec, Expanded_Combinator_Spec(spec));

    require (
      ParamList* paramlist = Make_Paramlist_Managed(
        expanded_spec,
        MKF_MASK_NONE,
        SYM_RETURN  // want RETURN:
    ));

    Details* details = Make_Dispatch_Details(
        BASE_FLAG_MANAGED,
        Phase_Archetype(paramlist),
        &Combinator_Dispatcher,
        MAX_IDX_COMBINATOR  // details array capacity
    );

    // !!! As with FUNC, we copy and bind the block the user gives us.  This
    // means we will not see updates to it.  So long as we are copying it,
    // we might as well mutably bind it--there's no incentive to virtual
    // bind things that are copied.
    //
    Array* relativized = Copy_And_Bind_Relative_Deep_Managed(
        body,
        details,
        LENS_MODE_ALL_UNSEALED
    );

    Init_Relative_Block(
        Details_At(details, IDX_COMBINATOR_BODY),
        details,
        relativized
    );

    return Init_Frame(OUT, details, ANONYMOUS, UNCOUPLED);
}


//
//  Push_Parser_Sublevel: C
//
// This service routine does a faster version of something like:
//
//     Api(Stable*) result = rebStable("apply", rebQ(ARG(PARSER)), "[",
//         ":input", rebQ(ARG(INPUT)),  // quote avoids becoming const
//         ":remainder @", ARG(REMAINDER),
//     "]");
//
// But it only works on parsers that were created from specializations of
// COMBINATOR or NATIVE-COMBINATOR.  Because it expects the parameters to be
// in the right order in the frame.
//
void Push_Parser_Sublevel(
    Value* out,
    const Stable* remainder,
    const Stable* parser,
    const Stable* input
){
    assert(Any_Series(input));
    assert(Is_Frame(parser));

    ParamList* ctx = Make_Varlist_For_Action(
        parser,
        TOP_INDEX,
        nullptr,
        nullptr  // leave unspecialized slots with parameter! antiforms
    );

    const Key* remainder_key = Varlist_Key(ctx, IDX_COMBINATOR_PARAM_REMAINDER);
    const Key* input_key = Varlist_Key(ctx, IDX_COMBINATOR_PARAM_INPUT);
    if (
        Key_Id(remainder_key) != SYM_REMAINDER
        or Key_Id(input_key) != SYM_INPUT
    ){
        panic ("Push_Parser_Sublevel() only works on unadulterated combinators");
    }

    Slot* remainder_slot = Varlist_Slot(ctx, IDX_COMBINATOR_PARAM_REMAINDER);
    Slot* input_slot = Varlist_Slot(ctx, IDX_COMBINATOR_PARAM_INPUT);

    Copy_Cell(Slot_Init_Hack(remainder_slot), remainder);
    Copy_Cell(Slot_Init_Hack(input_slot), input);

    DECLARE_ELEMENT (temp);  // can't overwrite spare
    Init_Frame(temp, ctx, ANONYMOUS, UNCOUPLED);

    bool pushed = Pushed_Continuation(
        out,
        LEVEL_MASK_NONE,
        SPECIFIED,
        temp,
        nullptr  // with
    );
    assert(pushed);  // always needs to push, it's a frame
    UNUSED(pushed);
}


//
//  opt-combinator: native:combinator [
//
//  "If parser fails, succeed and return VOID without advancing the input"
//
//      return: [<void> any-stable?]
//      parser [action!]
//      {remainder}  ; !!! no longer separate output, review
//  ]
//
DECLARE_NATIVE(OPT_COMBINATOR)
{
    INCLUDE_PARAMS_OF_OPT_COMBINATOR;

    Stable* remainder = ARG(REMAINDER);  // output (combinator implicit)

    Stable* input = ARG(INPUT);  // combinator implicit
    Stable* parser = ARG(PARSER);
    UNUSED(ARG(STATE));  // combinator implicit

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

    Push_Parser_Sublevel(OUT, remainder, parser, input);

    STATE = ST_OPT_COMBINATOR_RUNNING_PARSER;
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} parser_result_in_out: {  ///////////////////////////////////////////////////

    if (not Is_Error(OUT))  // parser succeeded...
        return OUT;  // so return its result

    Copy_Cell(remainder, input);  // convey no progress made
    return Init_Nulled(OUT);  // null result
}}


//
//  text!-combinator: native:combinator [
//
//  "Match a TEXT! value as a list item or at current position of bin/string"
//
//      return: [text!]
//      value [text!]
//      {remainder}  ; !!! no longer separate output, review
//  ]
//
DECLARE_NATIVE(TEXT_X_COMBINATOR)
{
    INCLUDE_PARAMS_OF_TEXT_X_COMBINATOR;

    VarList* state = Cell_Varlist(ARG(STATE));

    require (
      bool cased = Test_Conditional(  // or trust it's a LOGIC ?
        Slot_Hack(Varlist_Slot(state, IDX_UPARSE_PARAM_CASE))
    ));

    Element* v = Element_ARG(VALUE);
    Element* input = Element_ARG(INPUT);

    if (Any_List(input)) {
        const Element* tail;
        const Element* at = List_At(&tail, input);
        if (at == tail)  // no item to match against
            return NULLED;
        require (
          bool equal = Equal_Values(at, v, true)
        );
        if (not equal)  // not case-insensitive equal
            return NULLED;

        ++SERIES_INDEX_UNBOUNDED(input);
        Copy_Cell(ARG(REMAINDER), input);

        Derelativize(OUT, at, List_Binding(input));
        return OUT;  // Note: returns item in array, not rule, when an array!
    }

    assert(Any_String(input) or Is_Blob(input));

    REBLEN len;
    REBINT index = Find_Value_In_Binstr(
        &len,
        input,
        Series_Len_Head(input),
        v,
        AM_FIND_MATCH | (cased ? AM_FIND_CASE : 0),
        1  // skip
    );
    if (index == NOT_FOUND)
        return NULLED;

    assert(index == Series_Index(input));  // asked for AM_FIND_MATCH
    SERIES_INDEX_UNBOUNDED(input) += len;
    Copy_Cell(ARG(REMAINDER), input);

    // If not a list, we have return the rule on match since there's
    // no isolated value to capture.

    return COPY(v);
}


//
//  some-combinator: native:combinator [
//
//  "Must run at least one match, return result of last parser call"
//
//      return: [any-stable?]
//      parser [action!]
//      {remainder}  ; !!! no longer separate output, review
//  ]
//
DECLARE_NATIVE(SOME_COMBINATOR)
{
    INCLUDE_PARAMS_OF_SOME_COMBINATOR;

    Stable* remainder = ARG(REMAINDER);
    Stable* parser = ARG(PARSER);
    Stable* input = ARG(INPUT);

    Stable* state = ARG(STATE);
    Source* loops = Cell_Array_Ensure_Mutable(
        Slot_Hack(Varlist_Slot(Cell_Varlist(state), IDX_UPARSE_PARAM_LOOPS))
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

    // 1. Currently the usermode parser has no support for intercepting throws
    //    removing frames from the loops list in usermode.  Mirror that
    //    limitation here in the native implementation for now.

    require (
      Sink(Element) loop_last = Alloc_Tail_Array(loops)
    );
    Init_Frame(loop_last, Level_Varlist(level_), CANON(SOME), UNCOUPLED);

    Push_Parser_Sublevel(OUT, remainder, parser, input);

    STATE = ST_SOME_COMBINATOR_FIRST_PARSER_RUN;
    return CONTINUE_SUBLEVEL(SUBLEVEL);  // mirror usermode [1]

} first_parse_result_in_out: {  //////////////////////////////////////////////

    if (Is_Light_Null(OUT)) {  // didn't match even once, not enough, drop loop
        Remove_Flex_Units_And_Update_Used(loops, Array_Len(loops) - 1, 1);
        return NULLED;
    }

} call_parser_again: {  //////////////////////////////////////////////////////

    Copy_Cell(remainder, input);  // remainder from previous call is new input

    Push_Parser_Sublevel(SPARE, remainder, parser, input);

    STATE = ST_SOME_COMBINATOR_LATER_PARSER_RUN;
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} later_parse_result_in_spare: {  ////////////////////////////////////////////

    if (Is_Light_Null(SPARE)) {  // first still succeeded, so we're okay.
        Copy_Cell(remainder, input);  // put back [3] and drop loop
        Remove_Flex_Units_And_Update_Used(loops, Array_Len(loops) - 1, 1);
        return OUT;  // return previous successful parser result
    }

    Move_Value(OUT, SPARE);  // update last successful result
    goto call_parser_again;
}}


//
//  further-combinator: native:combinator [
//
//  "Pass through the result only if the input was advanced by the rule"
//
//      return: [any-stable?]
//      parser [action!]
//      {remainder}  ; !!! no longer separate output, review
//  ]
//
DECLARE_NATIVE(FURTHER_COMBINATOR)
{
    INCLUDE_PARAMS_OF_FURTHER_COMBINATOR;

    Stable* remainder = ARG(REMAINDER);
    Stable* input = ARG(INPUT);
    Stable* parser = ARG(PARSER);
    UNUSED(ARG(STATE));

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

    Push_Parser_Sublevel(OUT, remainder, parser, input);

    STATE = ST_FURTHER_COMBINATOR_RUNNING_PARSER;
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} parser_result_in_out: {  ///////////////////////////////////////////////////

    if (Is_Light_Null(OUT))
        return NULLED;  // the parse rule did not match

    Copy_Cell(SPARE, remainder);

    if (Series_Index(SPARE) <= Series_Index(input))
        return NULLED;  // the rule matched but did not advance the input

    return OUT;
}}


struct CombinatorParamStateStruct {
    VarList* ctx;
    Level* level_;
    Stable* rule_end;
};
typedef struct CombinatorParamStateStruct CombinatorParamState;

static bool Combinator_Param_Hook(
    const Key* key,
    const Param* param,
    CombinatorParamState* s
){
    Level* level_ = s->level_;
    INCLUDE_PARAMS_OF_COMBINATORIZE;

    Element* rules = Element_ARG(RULES);

    UNUSED(Bool_ARG(PATH));  // used by caller of hook

    Option(SymId) symid = Key_Id(key);

    // we need to calculate what variable slot this lines up with.  Can be
    // done based on the offset of the param from the head.

    REBLEN offset = param - Phase_Params_Head(
        Frame_Phase(ARG(COMBINATOR))
    );

    if (offset == 2) {  // [RETURN STATE INPUT ...]
        //
        // The idea is that INPUT is always left unspecialized (a completed
        // parser produced from a combinator takes it as the only parameter).
        // We have to use the index to determine which argument is INPUT,
        // because COMBINATOR allows people to use arbitrary names for it.
        // It's the second parameter in the spec, after STATE, but we also
        // have to account for the RETURN slot.
        //
        return true;  // keep iterating the parameters.
    }

    Stable* var = Slot_Hack(Varlist_Slots_Head(s->ctx) + offset);

    if (symid == SYM_STATE) {  // the "state" is currently the UPARSE frame
        Copy_Cell(var, ARG(STATE));
    }
    else if (symid == SYM_VALUE and Bool_ARG(VALUE)) {
        //
        // The "value" parameter only has special meaning for datatype
        // combinators, e.g. TEXT!.  Otherwise a combinator can have an
        // argument named value for other purposes.
        //
        Copy_Cell(var, ARG(VALUE));
    }
    else if (symid == SYM_RULE_START) {
        Copy_Cell(var, ARG(RULE_START));
    }
    else if (symid == SYM_RULE_END) {
        s->rule_end = var;  // can't set until rules consumed, let caller do it
        UNUSED(LOCAL(RULE_END));  // only exists in spec to make SYM_RULE_END
    }
    else if (Get_Parameter_Flag(param, REFINEMENT)) {
        //
        // !!! Behavior of refinements is a bit up in the air, the idea is
        // that refinements that don't take arguments can be supported...
        // examples would be things like KEEP/ONLY.  But refinements that
        // take arguments...e.g. additional rules...is open to discussion.
        //
        // BLOCK! combinator has a /LIMIT refinement it uses internally ATM.
        //
        return true;  // just leave unspecialized for now
    }
    else switch (Parameter_Class(param)) {
      case PARAMCLASS_JUST:
      case PARAMCLASS_THE: {
        //
        // Quoted parameters represent a literal element captured from rules.
        //
        const Element* tail;
        const Element* item = List_At(&tail, rules);

        if (
            item == tail
            or (Is_Comma(item) or Is_Bar(item) or Is_Bar_Bar(item))
        ){
            if (Not_Parameter_Flag(param, ENDABLE))
                panic ("Too few parameters for combinator");  // !!! Error_No_Arg
            Init_Unset_Due_To_End(u_cast(Value*, var));
        }
        else {
            if (Parameter_Class(param) == PARAMCLASS_THE)
                Derelativize(var, item, List_Binding(rules));
            else {
                assert(Parameter_Class(param) == PARAMCLASS_JUST);
                Copy_Cell(var, item);
            }
            ++SERIES_INDEX_UNBOUNDED(rules);
        }
        break; }

      case PARAMCLASS_NORMAL: {
        //
        // Need to make PARSIFY a native!  Work around it for now...
        //
        const Element* tail;
        const Element* item = List_At(&tail, rules);
        if (
            item == tail
            or (Is_Comma(item) or Is_Bar(item) or Is_Bar_Bar(item))
        ){
            if (Not_Parameter_Flag(param, ENDABLE))
                panic ("Too few parameters for combinator");  // !!! Error_No_Arg
            Init_Unset_Due_To_End(u_cast(Value*, var));
        }
        else {
            // !!! Getting more than one value back from a libRebol API is not
            // currently supported.  Usermode code is not allowed to directly
            // write to native frame variables, so hack in a temporary here.
            // (could be done much more efficiently another way!)

            if (rebRunThrows(u_cast(Sink(Stable), SPARE), "let temp"))
                assert(!"LET failed");
            Element* temp = cast(Element*, SPARE);
            Api(Stable*) parser = rebStable(
                "[_", temp, "]: parsify", rebQ(ARG(STATE)), ARG(RULES)
            );
            require (
              Get_Var(
                ARG(RULES), NO_STEPS, temp, SPECIFIED
            ));
            Copy_Cell(var, parser);
            rebRelease(parser);
        }
        break; }

      default:
        panic ("COMBINATOR parameters must be normal or quoted at this time");
    }

    return true;  // want to see all parameters
}


//
//  combinatorize: native [
//
//  "Analyze combinator parameters in rules to produce a specialized parser"
//
//      return: [
//          ~[action! block!]~ "Parser function and advanced position in rules"
//      ]
//      combinator "Parser combinator taking input, but also other parameters"
//          [frame!]
//      rules [block!]
//      state "Parse State" [frame!]
//      :value "Initiating value (if datatype)" [element?]
//      :path "Invoking Path" [path!]
//      {rule-start rule-end}  ; RULE-END needed to make SYM_RULE_END
//  ]
//
DECLARE_NATIVE(COMBINATORIZE)
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

    Stable* combinator = ARG(COMBINATOR);

    Phase* phase = Frame_Phase(combinator);
    Option(const Symbol*) label = Frame_Label_Deep(combinator);
    Option(VarList*) coupling = Frame_Coupling(combinator);

    Stable* rule_start = Copy_Cell(LOCAL(RULE_START), ARG(RULES));
    if (Series_Index(rule_start) > 0)
        SERIES_INDEX_UNBOUNDED(rule_start) -= 1;

    UNUSED(LOCAL(RULE_END));  // only exists in spec to make SYM_RULE_END

    // !!! The hack for PATH! handling was added to make /ONLY work; it only
    // works for refinements with no arguments by looking at what's in the path
    // when it doesn't end in /.  Now /ONLY is not used.  Review general
    // mechanisms for refinements on combinators.
    //
    if (Bool_ARG(PATH))
        panic ("PATH! mechanics in COMBINATORIZE not supported ATM");

    ParamList* paramlist = Make_Varlist_For_Action(
        combinator,
        TOP_INDEX,
        nullptr,
        nullptr  // leave unspecialized slots with parameter! antiforms
    );
    CombinatorParamState s;
    s.ctx = paramlist;
    s.level_ = level_;
    s.rule_end = nullptr;  // argument found by param hook

    Push_Lifeguard(s.ctx);  // Combinator_Param_Hook may call evaluator

    USED(Bool_ARG(STATE));
    USED(Bool_ARG(VALUE));

    const Key* key_tail;
    const Key* key = Phase_Keys(&key_tail, phase);
    Param* param = Phase_Params_Head(phase);
    for (; key != key_tail; ++key, ++param) {
        if (Is_Specialized(param))
            continue;
        Combinator_Param_Hook(key, param, &s);
    }

    Drop_Lifeguard(s.ctx);

    // For debug and tracing, combinators are told where their rule end is
    //
    Copy_Cell(s.rule_end, ARG(RULES));

    Source* pack = Make_Source_Managed(2);
    Set_Flex_Len(pack, 2);

    Element* frame = Init_Frame(Array_At(pack, 0), paramlist, label, coupling);
    Copy_Ghostability(frame, combinator);
    Quasify_Isotopic_Fundamental(frame);

    Copy_Lifted_Cell(Array_At(pack, 1), ARG(RULES));  // advanced by param hook

    return Init_Pack(OUT, pack);
}
