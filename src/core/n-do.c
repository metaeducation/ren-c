//
//  File: %n-do.c
//  Summary: "native functions for DO, EVAL, APPLY"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
//=////////////////////////////////////////////////////////////////////////=//
//
// Ren-C's philosophy of DO is that the argument to it represents a place to
// find source code.  Hence `DO 3` does not evaluate to the number 3, any
// more than `DO "print hello"` would evaluate to `"print hello"`.  If a
// generalized evaluator is needed, use the special-purpose REEVAL(UATE).
//
// Note that although the code for running blocks and frames is implemented
// here as C, the handler for processing STRING!, FILE!, TAG!, URL!, etc. is
// dispatched out to some Rebol code.  See `system/intrinsic/do*`.
//

#include "sys-core.h"


//
//  reeval: native [
//
//  "Process an evaluated argument *inline* as an evaluator step would"
//
//      return: [any-atom?]
//      value "BLOCK! passes-thru, ACTION! runs, SET-WORD! assigns..."
//          [element?]
//      expressions "Depending on VALUE, more expressions may be consumed"
//          [~null~ element? <variadic>]
//  ]
//
DECLARE_NATIVE(reeval)
{
    INCLUDE_PARAMS_OF_REEVAL;

    // REEVAL only *acts* variadic, but uses ST_STEPPER_REEVALUATING
    //
    UNUSED(ARG(expressions));

    Element* v = cast(Element*, ARG(value));

    bool enfix =
        Is_Quasiform(v)
        and HEART_BYTE(v) == REB_FRAME
        and Is_Enfixed(v);

    Flags flags = LEVEL_MASK_NONE;

    if (Reevaluate_In_Sublevel_Throws(
        OUT,  // reeval :comment "this should leave old input"
        level_,
        v,
        flags,
        enfix
    )){
        return THROWN;
    }

    return OUT;
}


//
//  shove: native [
//
//  "Shove a parameter into an ACTION! as its first argument"
//
//      return: [any-value?]
//          "REVIEW: How might this handle shoving enfix invisibles?"
//      :left [any-value?]
//          "Requests parameter convention based on enfixee's first argument"
//      'right [<variadic> <end> element?]
//          "(uses magic -- SHOVE can't be written easily in usermode yet)"
//      /prefix "Force either prefix or enfix behavior (vs. acting as is)"
//          [logic?]
//  ]
//
DECLARE_NATIVE(shove)
//
// PATH!s do not do infix lookup in Rebol, and there are good reasons for this
// in terms of both performance and semantics.  However, it is sometimes
// needed to dispatch via a path--for instance to call an enfix function that
// lives in a context, or even to call one that has refinements.
//
// The SHOVE operation is used to push values from the left to act as the
// first argument of an operation, e.g.:
//
//      >> 10 >- lib.(print "Hi!" first [multiply]) 20
//      Hi!
//      200
//
// It's becoming more possible to write something like this in usermode, but
// it would be inefficient, and there are binding problems to worry about
// in macro-like code.
{
    INCLUDE_PARAMS_OF_SHOVE;

    Level* L;
    if (not Is_Level_Style_Varargs_May_Fail(&L, ARG(right)))
        fail ("SHOVE (>-) not implemented for MAKE VARARGS! [...] yet");

    Value* left = ARG(left);

    if (Is_Level_At_End(L))  // shouldn't be for WORD!/PATH! unless APPLY
        return COPY(ARG(left));  // ...because evaluator wants `help <-` to work

    // It's best for SHOVE to do type checking here, as opposed to setting
    // some kind of LEVEL_FLAG_SHOVING and passing that into the evaluator, then
    // expecting it to notice if you shoved into an INTEGER! or something.
    //
    // !!! To get the feature working as a first cut, this doesn't try get too
    // fancy with apply-like mechanics and slipstream refinements on the
    // stack to enfix functions with refinements.  It specializes the ACTION!.
    // We can do better, but seeing as how you couldn't call enfix actions
    // with refinements *at all* before, this is a step up.

    Value* shovee = ARG(right); // reuse arg cell for the shoved-into
    Option(const Symbol*) label = nullptr;

    if (Is_Word(At_Level(L)) or Is_Path(At_Level(L)) or Is_Tuple(At_Level(L))) {
        //
        // !!! should get label from word
        //
        Get_Var_May_Fail(
            OUT, // can't eval directly into arg slot
            At_Level(L),
            Level_Specifier(L),
            false
        );
        Move_Cell(shovee, cast(Value*, OUT));
    }
    else if (Is_Group(At_Level(L))) {
        if (Do_Any_List_At_Throws(OUT, At_Level(L), Level_Specifier(L)))
            return THROWN;

        Move_Cell(shovee, Decay_If_Unstable(OUT));
    }
    else
        Copy_Cell(shovee, At_Level(L));

    Deactivate_If_Action(shovee);  // allow ACTION! to be run

    if (not Is_Frame(shovee))
        fail ("SHOVE's immediate right must be ACTION! or SET-XXX! type");

    if (not label)
        label = VAL_FRAME_LABEL(shovee);

    // Basic operator `>-` will use the enfix status of the shovee.
    // `->-` will force enfix evaluator behavior even if shovee is prefix.
    // `>--` will force prefix evaluator behavior even if shovee is enfix.
    //
    bool enfix;
    if (REF(prefix))
        enfix = not Cell_Logic(ARG(prefix));
    else if (Is_Frame(shovee))
        enfix = Is_Enfixed(shovee);
    else
        enfix = false;

    Fetch_Next_In_Feed(L->feed);

    // Since we're simulating enfix dispatch, we need to move the first arg
    // where enfix gets it from...the frame output slot.
    //
    // We quoted the argument on the left, but the ACTION! we are feeding
    // into may want it evaluative.  (Enfix handling itself does soft quoting)
    //
    if (
        Not_Subclass_Flag(
            VARLIST,
            ACT_PARAMLIST(VAL_ACTION(shovee)),
            PARAMLIST_QUOTES_FIRST
        )
    ){
        if (Eval_Value_Throws(OUT, Ensure_Element(left), SPECIFIED))
            return THROWN;
    }
    else {
        Copy_Cell(OUT, left);
    }

    Flags flags = FLAG_STATE_BYTE(ST_ACTION_INITIAL_ENTRY_ENFIX);

    Level* sub = Make_Level(&Action_Executor, level_->feed, flags);
    Push_Action(sub, VAL_ACTION(shovee), VAL_FRAME_COUPLING(shovee));
    Begin_Action_Core(sub, label, enfix);

    Push_Level(OUT, sub);
    return DELEGATE_SUBLEVEL(sub);
}


//
//  do: native [
//
//  "Evaluates source (see also EVAL for stepwise or invisible evaluation)"
//
//      return: [any-value?]
//      source "Block of code, or indirect specification to find/make it" [
//          <maybe>  ; opts out of the DO, returns null
//          ; block!  ; will be "DO spec", use EVAL for "classic block DO" [1]
//          text!  ; source code in text form
//          binary!  ; treated as UTF-8
//          url!  ; load code from URL via protocol
//          file!  ; load code from file on local disk
//          tag!  ; load relative to system.script.name
//          the-word!  ; module name (URL! looked up from table)
//          error!  ; should use FAIL instead
//          frame!  ; acts like APPLY (voids are optionals, not unspecialized)
//          action?  ; will only run arity 0 actions (avoids DO variadic)
//          varargs!  ; simulates as if frame! or block! is being executed
//      ]
//      /args "Sets system.script.args if doing a script (usually a TEXT!)"
//          [element?]
//      /only "Don't catch QUIT (default behavior for BLOCK!)"
//  ]
//
DECLARE_NATIVE(do)
//
// 1. DO is aiming to be polymorphic, to run things like JavaScript.  It
//    always requires a header (or an implied header from filename/location).
//    It will support blocks at one point, but not in the historical way.
//
//    https://forum.rebol.info/t/polyglot-polymorphic-do/1846
//
//    Modern calls for evaluation services on BLOCK! etc. should use EVAL.
//
{
    INCLUDE_PARAMS_OF_DO;

    Value* source = ARG(source);

    Tweak_Non_Const_To_Explicitly_Mutable(source);

  #if !defined(NDEBUG)
    Set_Cell_Flag(source, PROTECTED);  // maybe only GC reference, keep!
  #endif

    Deactivate_If_Action(source);

    switch (VAL_TYPE(source)) {
      case REB_BLOCK :
        fail ("BLOCK! reserved for new meaning in DO, use EVAL to evaluate");

      case REB_THE_WORD : goto do_helper;
      case REB_BINARY : goto do_helper;
      case REB_TEXT : goto do_helper;
      case REB_URL : goto do_helper;
      case REB_FILE : goto do_helper;
      case REB_TAG : goto do_helper;

      do_helper : {
        UNUSED(REF(args)); // detected via `value? :arg`

        rebPushContinuation(
            cast(Value*, OUT),  // <-- output cell
            LEVEL_MASK_NONE,
            rebRUN(SysUtil(DO_P)),
                source,
                rebQ(ARG(args)),
                REF(only) ? rebQ(Lib(TRUE)) : rebQ(Lib(FALSE))
        );
        return BOUNCE_DELEGATE; }

      case REB_ERROR :
        fail ("Use EVAL (not DO) to evaluate ERROR!");

      case REB_VARARGS :
        fail ("Use EVAL (not DO) to evaluate VARARGS!");

      case REB_FRAME :
        fail ("Use EVAL to evaluate FRAME!");

      default :
        break;
    }

    fail (Error_Do_Arity_Non_Zero_Raw());  // https://trello.com/c/YMAb89dv
}


//
//  eval: native [
//
//  "Perform a single evaluator step, returning the next source position"
//
//      return: "Evaluation product, or ~[position product]~ pack if /NEXT"
//          [any-atom?]  ; /NEXT changes primary return product [1]
//      source [
//          <maybe>  ; useful for `evaluate maybe ...` scenarios
//          any-list?  ; source code
//          <unrun> frame!  ; invoke the frame (no arguments, see RUN)
//          error!  ; raise the error
//          varargs!  ; simulates as if frame! or block! is being executed
//      ]
//      /next "Do one step of evaluation (return null position if at tail)"
//      /undecayed "Don't convert NIHIL or COMMA! antiforms to VOID"
//  ]
//
DECLARE_NATIVE(eval)  // synonym as EVALUATE in mezzanine
//
// 1. When operating stepwise, the primary result shifts to be the position,
//    to be more useful for knowing if there are more steps to take.  It also
//    helps prevent misunderstandings if the first value of a multi-return
//    cannot itself be a multi-return pack:
//
//      https://forum.rebol.info/t/re-imagining-eval-next/767
//
// 2. This may be the only GC reference holding the array, don't lose it!
//
// 3. It might seem that since EVAL [] is VOID, that EVAL/NEXT [] should
//    produce a VOID.  But in practice, there's a dummy step at the end
//    of every enumeration, e.g. EVAL [1 + 2 10 + 20] goes through three
//    steps, where the third step is []... and if we were to say that "step"
//    produced anything, it would be NIHIL...because that step does not
//    contribute to the output (the result is 30).  But we actually don't
//    produce anything--because we don't return a pack of values when nothing
//    is synthesized, we just return NULL.
//
// 4. We want EVALUATE to treat all ANY-LIST? the same.  (e.g. a ^[1 + 2] just
//    does the same thing as [1 + 2] and gives 3, not '3)  Rather than mutate
//    the cell to plain BLOCK! and pass it to CONTINUE_CORE(), we initialize
//    a feed from the array directly.
//
// 6. There may have been a LET statement in the code.  If there was, we have
//    to incorporate the binding it added into the reported state *somehow*.
//    Right now we add it to the block we give back...this gives rise to
//    questionable properties, such as if the user goes backward in the block
//    and were to evaluate it again:
//
//      https://forum.rebol.info/t/1496
//
//    Right now we can politely ask "don't do that", but better would probably
//    be to make EVALUATE return something with more limited privileges... more
//    like a FRAME!/VARARGS!.
//
// 7. FAIL is the preferred operation for triggering errors, as it has a
//    natural behavior for blocks passed to construct readable messages and
//    "FAIL X" more clearly communicates a failure than "EVAL X".  But EVAL of
//    an ERROR! would have to raise an error anyway, so it might as well use
//    the one it is given.
{
    INCLUDE_PARAMS_OF_EVAL;

    Element* source = Ensure_Element(ARG(source));  // hold for GC [2]

    enum {
        ST_EVALUATE_INITIAL_ENTRY = STATE_0,
        ST_EVALUATE_SINGLE_STEPPING,
        ST_EVALUATE_RUNNING_TO_END
    };

    switch (STATE) {
      case ST_EVALUATE_INITIAL_ENTRY :
        goto initial_entry;

      case ST_EVALUATE_SINGLE_STEPPING :
        goto single_step_result_in_out;

      case ST_EVALUATE_RUNNING_TO_END :
        goto result_in_out;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Tweak_Non_Const_To_Explicitly_Mutable(source);

  #if !defined(NDEBUG)
    Set_Cell_Flag(ARG(source), PROTECTED);
  #endif

    if (Any_List(source)) {
        if (Cell_Series_Len_At(source) == 0) {
            if (REF(next))  // `eval/next []` doesn't "count" [3]
                return nullptr;  // need pure null for THEN/ELSE to work right

            if (REF(undecayed))
                Init_Nihil(OUT);  // undecayed allows vanishing
            else
                Init_Void(OUT);  // `eval []` is ~void~

            return OUT;
        }

        Feed* feed = Make_At_Feed_Core(  // use feed, ignore type [4]
            source,
            SPECIFIED
        );
        assert(Not_Feed_At_End(feed));

        Flags flags = LEVEL_FLAG_RAISED_RESULT_OK;

        if (not REF(next))
            Init_Nihil(Alloc_Evaluator_Primed_Result());

        Level* sub = Make_Level(
            REF(next) ? &Stepper_Executor : &Evaluator_Executor,
            feed,
            flags
        );
        Push_Level(OUT, sub);

        if (not REF(next)) {  // plain evaluation to end, maybe invisible
            if (REF(undecayed))
                return DELEGATE_SUBLEVEL(sub);

            STATE = ST_EVALUATE_RUNNING_TO_END;
            return CONTINUE_SUBLEVEL(sub);  // need callback to decay
        }

        STATE = ST_EVALUATE_SINGLE_STEPPING;

        Set_Level_Flag(sub, TRAMPOLINE_KEEPALIVE);  // to ask how far it got

        return CONTINUE_SUBLEVEL(sub);
    }
    else switch (VAL_TYPE(source)) {

      case REB_FRAME : {
        //
        // !!! It is likely that the return result for the NEXT: will actually
        // be a FRAME! when the input to EVALUATE is a BLOCK!, so that the
        // LET bindings can be preserved.  Binding is still a mess when it
        // comes to questions like backtracking in blocks, so review.
        //
        if (REF(next))
            fail ("/NEXT Behavior not implemented for FRAME! in EVALUATE");

        if (Is_Frame_Details(source))
            if (First_Unspecialized_Param(nullptr, VAL_ACTION(source)))
                fail (Error_Do_Arity_Non_Zero_Raw());  // see notes in DO on error

        return DELEGATE(OUT, source); }

      case REB_VARARGS : {
        Element* position;
        if (Is_Block_Style_Varargs(&position, source)) {
            //
            // We can execute the array, but we must "consume" elements out
            // of it (e.g. advance the index shared across all instances)
            //
            // !!! If any VARARGS! op does not honor the "locked" flag on the
            // array during execution, there will be problems if it is TAKE'n
            // or DO'd while this operation is in progress.
            //
            if (Do_Any_List_At_Throws(OUT, position, SPECIFIED)) {
                //
                // !!! A BLOCK! varargs doesn't technically need to "go bad"
                // on a throw, since the block is still around.  But a FRAME!
                // varargs does.  This will cause an assert if reused, and
                // having BLANK! mean "thrown" may evolve into a convention.
                //
                Init_Unreadable(position);
                return THROWN;
            }

            Erase_Cell(position); // convention for shared data at endpoint

            if (Is_Void(OUT))
                return VOID;
            return OUT;
        }

        Level* L;
        if (not Is_Level_Style_Varargs_May_Fail(&L, source))
            panic (source); // Frame is the only other type

        // By definition, we are in the middle of a function call in the level
        // the varargs came from.  It's still on the stack, and we don't want
        // to disrupt its state.  Use a sublevel.

        if (Is_Level_At_End(L))
            return VOID;

        Init_Void(Alloc_Evaluator_Primed_Result());
        Level* sub = Make_Level(&Evaluator_Executor, L->feed, LEVEL_MASK_NONE);
        Push_Level(OUT, sub);
        return DELEGATE_SUBLEVEL(sub); }

      case REB_ERROR :
        fail (VAL_CONTEXT(source));  // would fail anyway [7]

      default:
        fail (PARAM(source));
    }

    DEAD_END;

} single_step_result_in_out: {  //////////////////////////////////////////////

    assert(REF(next));

    Specifier* specifier = Level_Specifier(SUBLEVEL);
    VAL_INDEX_UNBOUNDED(source) = Level_Array_Index(SUBLEVEL);  // new index
    Drop_Level(SUBLEVEL);

    BINDING(source) = specifier;  // integrate LETs [6]

} result_in_out: {  //////////////////////////////////////////////////////////

    if (not REF(undecayed)) {
        if (Is_Elision(OUT))
            Init_Void(OUT);
    }

    if (REF(next)) {
        if (Is_Raised(OUT))  // can't put raised errors in PACK!s
            fail (VAL_CONTEXT(OUT));

        Array *pack = Make_Array_Core(2, NODE_FLAG_MANAGED);
        Set_Flex_Len(pack, 2);
        Copy_Meta_Cell(Array_At(pack, 0), source);  // pack wants META values
        Copy_Meta_Cell(Array_At(pack, 1), OUT);

        Init_Pack(OUT, pack);
    }

    return OUT;
}}


//
//  redo: native [
//
//  "Restart a frame's action from the top with its current state"
//
//      return: []
//      restartee "Frame to restart, or bound word (e.g. REDO $RETURN)"
//          [frame! any-word?]
//      /sibling "Restart execution in a frame-compatible function"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(redo)
//
// REDO starts the function phase again from its top, and reuses the frame
// already allocated.  It's a more generic form of tail call recursion (the
// RETURN/RUN option reuses the mechanism):
//
//   https://en.wikipedia.org/wiki/Tail_call
//
// 1. If we were given a sibling to restart, make sure it is frame compatible
//    (e.g. the product of ADAPT-ing, CHAIN-ing, ENCLOSE-ing, HIJACK-ing a
//    common underlying function).
//
// 2. We are reusing the frame and may be jumping to an "earlier phase" of
//    a composite function, or even to a "not-even-earlier-just-compatible"
//    phase of another function (sibling tail call).  Type checking is
//    necessary, as is zeroing out any locals...but if we're jumping to any
//    higher or different phase we need to reset the specialization
//    values as well.
//
//    !!! Consider folding this pass into the typechecking loop itself.
{
    INCLUDE_PARAMS_OF_REDO;

    Value* restartee = ARG(restartee);
    if (not Is_Frame(restartee)) {
        if (not Did_Get_Binding_Of(OUT, restartee))
            fail ("No context found from restartee in REDO");

        if (not Is_Frame(OUT))
            fail ("Context of restartee in REDO is not a FRAME!");

        Move_Cell(restartee, stable_OUT);
    }

    Context* c = VAL_CONTEXT(restartee);

    Level* L = CTX_LEVEL_IF_ON_STACK(c);
    if (L == NULL)
        fail ("Use EVAL to start a not-currently running FRAME! (not REDO)");

    if (REF(sibling)) {  // ensure frame compatibility [1]
        Value* sibling = ARG(sibling);
        if (
            ACT_KEYLIST(L->u.action.original)
            != ACT_KEYLIST(VAL_ACTION(sibling))
        ){
            fail ("/OTHER function passed to REDO has incompatible FRAME!");
        }

        INIT_LVL_PHASE(L, ACT_IDENTITY(VAL_ACTION(sibling)));
        INIT_LVL_COUPLING(L, VAL_FRAME_COUPLING(sibling));
    }
    else {
        INIT_LVL_PHASE(L, VAL_FRAME_PHASE(restartee));
        INIT_LVL_COUPLING(L, VAL_FRAME_COUPLING(restartee));
    }

    Action* redo_action = u_cast(Action*, Level_Phase(L));

    const Key* key_tail;
    const Key* key = ACT_KEYS(&key_tail, redo_action);
    Param* param = ACT_PARAMS_HEAD(redo_action);
    Value* arg = Level_Args_Head(L);
    for (; key != key_tail; ++key, ++arg, ++param) {
        if (
            Is_Specialized(param)  // must reset [2]
            or Cell_ParamClass(param) == PARAMCLASS_RETURN
            or Cell_ParamClass(param) == PARAMCLASS_OUTPUT
        ){
            Copy_Cell(arg, param);
        }
    }

    Copy_Cell(SPARE, Lib(REDO));  // label used for throw
    INIT_VAL_FRAME_COUPLING(SPARE, c);  // coupling has restartee as varlist

    const Value* gather_args = Lib(FALSE);
    return Init_Thrown_With_Label(LEVEL, gather_args, stable_SPARE);
}


//
//  applique: native [
//
//  "Invoke an ACTION! with all required arguments specified"
//
//      return: [any-atom?]
//      operation [<unrun> frame!]
//      def "Frame definition block (will be bound and evaluated)"
//          [block!]
//  ]
//
DECLARE_NATIVE(applique)
//
// 1. Make a FRAME! for the ACTION!, weaving in the ordered refinements
//    collected on the stack (if any).  Any refinements that are used in any
//    specialization level will be pushed as well, which makes them out
//    prioritize (e.g. higher-ordered) than any used in a PATH! that were
//    pushed during the Get of the ACTION!.
{
    INCLUDE_PARAMS_OF_APPLIQUE;

    Value* op = ARG(operation);
    Value* def = ARG(def);

    Value* frame = ARG(return);  // reuse as GC-safe cell for FRAME!

    enum {
        ST_APPLIQUE_INITIAL_ENTRY = STATE_0,
        ST_APPLIQUE_RUNNING_DEF_BLOCK
    };

    switch (STATE) {
      case ST_APPLIQUE_INITIAL_ENTRY :
        goto initial_entry;

      case ST_APPLIQUE_RUNNING_DEF_BLOCK :
        goto definition_result_in_spare;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Context* exemplar = Make_Context_For_Action_Push_Partials(  // [1]
        op,
        STACK_BASE,  // lowest_stackindex of refinements to weave in
        nullptr  // no binder needed
    );
    Manage_Flex(CTX_VARLIST(exemplar));
    Init_Frame(frame, exemplar, VAL_FRAME_LABEL(op));

    Drop_Data_Stack_To(STACK_BASE);  // refinement order unimportant

    Virtual_Bind_Deep_To_Existing_Context(
        def,
        exemplar,
        nullptr,  // !!! Unused binder parameter
        REB_SET_WORD
    );

    STATE = ST_APPLIQUE_RUNNING_DEF_BLOCK;
    return CONTINUE(SPARE, def);  // first run block bound to frame

} definition_result_in_spare: {  /////////////////////////////////////////////

    return DELEGATE(OUT, frame);  // now run the frame
}}


//
//  apply: native [
//
//  "Invoke an action with all required arguments specified"
//
//      return: [any-atom?]
//      operation [<unrun> frame!]
//      args "Arguments and Refinements, e.g. [arg1 arg2 /ref refine1]"
//          [block!]
//      /relax "Don't worry about too many arguments to the APPLY"
//      <local> frame index
//  ]
//
DECLARE_NATIVE(apply)
//
// 1. Binders cannot be held across evaluations at this time.  Do slow
//    lookups for refinements, but this is something that needs rethinking.
//
// 2. Make a FRAME! for the ACTION!, weaving in the ordered refinements
//    collected on the stack (if any).  Any refinements that are used in any
//    specialization level will be pushed as well, which makes them
//    out-prioritize (e.g. higher-ordered) than any used in a PATH! that were
//    pushed during the Get of the ACTION!.
//
// 3. Two argument-name labels in a row is not legal...treat it like the next
//    refinement is reaching a comma or end of block.  (Though this could be
//    treated as an <end> case?)
//
// 4. We treat <skip> parameters as if they can only be requested by name,
//    like a refinement.  This is because the evaluative nature of APPLY is
//    not compatible with the quoting requirement of skippability.
//
// 5. Low-level frame mechanics require that no-argument refinements be either
//    # or null.  As a higher-level utility, APPLY can throw in some assistance
//    so it converts (true => #) and (false => null)
//
// 6. We need to remove the binder indices, whether we are raising an error
//    or not.  But we also want any fields not assigned to be set to `~`.
//    (We wanted to avoid the situation where someone purposefully set a
//    meta-parameter to `~` being interpreted as never setting a field).
{
    INCLUDE_PARAMS_OF_APPLY;

    Value* op = ARG(operation);
    Value* args = ARG(args);

    Value* frame = ARG(frame);  // local variable for holding GC-safe frame
    Value* iterator = ARG(return);  // reuse to hold Evars iterator

    Value* var;  // may come from evars iterator or found by index
    Param* param;  // (same)

    enum {
        ST_APPLY_INITIAL_ENTRY = STATE_0,
        ST_APPLY_LABELED_EVAL_STEP,
        ST_APPLY_UNLABELED_EVAL_STEP
    };

    if (Get_Level_Flag(level_, ABRUPT_FAILURE))  // a fail() in this dispatcher
        goto finalize_apply;

    switch (STATE) {
      case ST_APPLY_INITIAL_ENTRY :
        goto initial_entry;

      case ST_APPLY_LABELED_EVAL_STEP :
        if (THROWING)
            goto finalize_apply;
        goto labeled_step_result_in_spare;

      case ST_APPLY_UNLABELED_EVAL_STEP :
        if (THROWING)
            goto finalize_apply;
        if (Is_Nothing(iterator)) {
            assert(REF(relax));
            goto handle_next_item;
        }
        goto unlabeled_step_result_in_spare;

      default : assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    /*struct Reb_Binder binder;  // [1]
    INIT_BINDER(&binder);*/
    Context* exemplar = Make_Context_For_Action_Push_Partials(  // [2]
        op,
        STACK_BASE,  // lowest_stackindex of refinements to weave in
        nullptr /* &binder */
    );
    Manage_Flex(CTX_VARLIST(exemplar)); // Putting into a frame
    Init_Frame(frame, exemplar, VAL_FRAME_LABEL(op));  // GC guarded

    Drop_Data_Stack_To(STACK_BASE);  // partials ordering unimportant

    Level* L = Make_Level_At(
        &Stepper_Executor,
        args,
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
    );
    Push_Level(SPARE, L);

    EVARS *e = Try_Alloc(EVARS);
    Init_Evars(e, frame);  // CTX_ARCHETYPE(exemplar) is phased, sees locals
    Init_Handle_Cdata(iterator, e, sizeof(EVARS));

    Set_Level_Flag(level_, NOTIFY_ON_ABRUPT_FAILURE);  // to clean up iterator
    goto handle_next_item;

} handle_next_item: {  ///////////////////////////////////////////////////////

    Level* L = SUBLEVEL;

    if (Is_Level_At_End(L))
        goto finalize_apply;

    const Element* at = At_Level(L);

    if (Is_Comma(at)) {
        Fetch_Next_In_Feed(L->feed);
        goto handle_next_item;
    }

    // We do special handling if we see a /REFINEMENT ... that is taken
    // to mean we are naming the next argument.

  #if !defined(NDEBUG)
    Corrupt_Pointer_If_Debug(param);
  #endif

    if (Is_Path(at) and Is_Refinement(at)) {
        STATE = ST_APPLY_LABELED_EVAL_STEP;

        const Symbol* symbol = VAL_REFINEMENT_SYMBOL(At_Level(L));

        REBLEN index = Find_Symbol_In_Context(frame, symbol, false);
        if (index == 0)
            fail (Error_Bad_Parameter_Raw(at));

        var = CTX_VAR(VAL_CONTEXT(frame), index);
        param = ACT_PARAM(VAL_ACTION(op), index);

        if (Is_Specialized(var))
            fail (Error_Bad_Parameter_Raw(at));

        DECLARE_ELEMENT (lookback);  // for error
        Copy_Cell(lookback, At_Level(L));
        Fetch_Next_In_Feed(L->feed);
        at = Try_At_Level(L);

        if (at == nullptr or Is_Comma(at))
            fail (Error_Need_Non_End_Raw(lookback));

        if (Is_Path(at) and Is_Refinement(at))  // [3]
            fail (Error_Need_Non_End_Raw(lookback));

        Init_Integer(ARG(index), index);
    }
    else if (Is_Nothing(iterator)) {
        STATE = ST_APPLY_UNLABELED_EVAL_STEP;
        param = nullptr;  // throw away result
    }
    else {
        STATE = ST_APPLY_UNLABELED_EVAL_STEP;

        EVARS *e = VAL_HANDLE_POINTER(EVARS, iterator);

        while (true) {
            if (not Did_Advance_Evars(e)) {
                if (not REF(relax))
                    fail (Error_Apply_Too_Many_Raw());

                Free(EVARS, e);
                Init_Nothing(iterator);
                param = nullptr;  // we're throwing away the evaluated product
                break;
            }

            if (
                Cell_ParamClass(e->param) == PARAMCLASS_RETURN
                or Cell_ParamClass(e->param) == PARAMCLASS_OUTPUT
                or Get_Parameter_Flag(e->param, REFINEMENT)
                or Get_Parameter_Flag(e->param, SKIPPABLE)
            ){
                continue;  // skippable only requested by name [4]
            }
            if (Not_Specialized(e->var)) {
                param = e->param;
                break;
            }
        }
    }

    assert(not Is_Pointer_Corrupt_Debug(param));  // nullptr means toss result

    if (param and Cell_ParamClass(param) == PARAMCLASS_META)
        Set_Level_Flag(SUBLEVEL, META_RESULT);  // get decayed result otherwise
    else
        Clear_Level_Flag(SUBLEVEL, META_RESULT);

    Restart_Stepper_Level(SUBLEVEL);
    return CATCH_CONTINUE_SUBLEVEL(SUBLEVEL);

} labeled_step_result_in_spare: {  ///////////////////////////////////////////

    REBLEN index = VAL_UINT32(ARG(index));

    var = CTX_VAR(VAL_CONTEXT(frame), index);
    param = ACT_PARAM(VAL_ACTION(op), index);

    goto copy_spare_to_var_in_frame;

} unlabeled_step_result_in_spare: {  /////////////////////////////////////////

    EVARS *e = VAL_HANDLE_POINTER(EVARS, iterator);

    var = e->var;
    param = e->param;

    goto copy_spare_to_var_in_frame;

} copy_spare_to_var_in_frame: {  /////////////////////////////////////////////

    if (  // help convert logic for no-arg refinement [5]
        Is_Logic(SPARE)
        and Get_Parameter_Flag(param, REFINEMENT)
        and Is_Parameter_Unconstrained(param)
    ){
        if (Cell_Logic(SPARE))
            Init_Blackhole(var);
        else
            Init_Nulled(var);

        Freshen_Cell(SPARE);
    }
    else {
        Move_Cell(var, stable_SPARE);  // !!! Review stability
    }

    goto handle_next_item;

} finalize_apply: {  /////////////////////////////////////////////////////////

    if (Is_Nothing(iterator))
        assert(REF(relax));
    else {
        EVARS *e = VAL_HANDLE_POINTER(EVARS, iterator);
        Shutdown_Evars(e);
        Free(EVARS, e);
        Init_Nothing(iterator);
    }

    if (THROWING)  // assume Drop_Level() called on SUBLEVEL?
        return THROWN;

    Drop_Level(SUBLEVEL);

    Clear_Level_Flag(level_, NOTIFY_ON_ABRUPT_FAILURE);  // necessary?

    return DELEGATE(OUT, frame);
}}


// From %c-eval.c -- decide if this should be shared or otherwise.
//
#define Make_Action_Sublevel(parent) \
    Make_Level(&Action_Executor, (parent)->feed, \
        LEVEL_FLAG_RAISED_RESULT_OK \
        | ((parent)->flags.bits & EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_TUPLE))



//
//  run: native [
//
//  "Invoke code inline as if it had been invoked via a WORD!"
//
//      return: [any-atom?]
//      frame [<unrun> frame!]
//      args [any-value? <variadic>]
//  ]
//
DECLARE_NATIVE(run)
{
    INCLUDE_PARAMS_OF_RUN;

    Value* action = ARG(frame);
    UNUSED(ARG(args));  // uses internal mechanisms to act variadic

    Level* sub = Make_Action_Sublevel(level_);
    Push_Level(OUT, sub);
    Push_Action(
        sub,
        VAL_ACTION(action),
        VAL_FRAME_COUPLING(action)
    );
    Begin_Prefix_Action(sub, VAL_FRAME_LABEL(action));

    return DELEGATE_SUBLEVEL(sub);
}
