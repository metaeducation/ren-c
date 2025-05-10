//
//  file: %n-do.c
//  summary: "native functions for DO, EVAL, APPLY"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// Ren-C's philosophy of DO is that the argument to it represents a place to
// find source code.  Hence `DO 3` does not evaluate to the number 3, any
// more than `DO "print hello"` would evaluate to `"print hello"`.  If a
// generalized evaluator is needed, use the special-purpose function EVAL.
//
// Note that although the code for running blocks and frames is implemented
// here as C, the handler for processing STRING!, FILE!, TAG!, URL!, etc. is
// dispatched out to some Rebol code.  See `system/intrinsic/do*`.
//

#include "sys-core.h"


//
//  reeval: native [
//
//  {Process received value *inline* as the evaluator loop would.}
//
//      return: [any-atom!]
//      value [any-element!]
//          {BLOCK! passes-thru, ACTION! runs, SET-WORD! assigns...}
//      expressions [any-value! <...>]
//          {Depending on VALUE, more expressions may be consumed}
//  ]
//
DECLARE_NATIVE(REEVAL)
{
    INCLUDE_PARAMS_OF_REEVAL;

    // EVAL only *acts* variadic, but uses EVAL_FLAG_REEVALUATE_CELL
    //
    UNUSED(ARG(EXPRESSIONS));

    DECLARE_SUBLEVEL (child, level_);

    // We need a way to slip the value through to the evaluator.  Can't run
    // it from the frame's cell.
    //
    child->u.reval.value = ARG(VALUE);

    Flags flags = EVAL_FLAG_REEVALUATE_CELL;

    Init_Trash(OUT);  // !!! R3C patch, better than error on `reeval :elide`
    Set_Cell_Flag(OUT, OUT_MARKED_STALE);

    if (Eval_Step_In_Subframe_Throws(OUT, level_, flags, child))
        return BOUNCE_THROWN;

    return OUT;
}


//
//  eval-infix: native [
//
//  {Service routine for implementing ME (needs review/generalization)}
//
//      return: [any-value!]
//      left [any-value!]
//          {Value to preload as the left hand-argument (won't reevaluate)}
//      rest [varargs!]
//          {The code stream to execute (head element must be infixed)}
//      /prefix
//          {Variant used when rest is prefix (e.g. for MY operator vs. ME)}
//  ]
//
DECLARE_NATIVE(EVAL_INFIX)
//
// !!! Being able to write `some-var: me + 10` isn't as "simple" <ahem> as:
//
// * making ME a backwards quoting operator that fetches the value of some-var
// * quoting its next argument (e.g. +) to get a word looking up to a function
// * making the next argument variadic, and normal-infix TAKE-ing it
// * APPLYing the quoted function on those two values
// * setting the left set-word (e.g. some-var:) to the result
//
// The problem with that strategy is that the parameter conventions of +
// matter.  Removing it from the evaluator and taking matters into one's own
// hands means one must reproduce the evaluator's logic--and that means it
// will probably be done poorly.  It's clearly not as sensible as having some
// way of slipping the value of some-var into the flow of normal evaluation.
//
// But generalizing this mechanic is...non-obvious.  It needs to be done, but
// this hacks up the specific case of "infix with left hand side and variadic
// feed" by loading the given value into OUT and then re-entering the
// evaluator via the EVAL_FLAG_POST_SWITCH mechanic (which was actuallly
// designed for backtracking on infix normal deferment.)
{
    INCLUDE_PARAMS_OF_EVAL_INFIX;

    Level* L;
    if (not Is_Level_Style_Varargs_May_Panic(&L, ARG(REST))) {
        //
        // It wouldn't be *that* hard to support block-style varargs, but as
        // this routine is a hack to implement ME, don't make it any longer
        // than it needs to be.
        //
        panic ("EVAL-INFIX is not made to support MAKE VARARGS! [...] rest");
    }

    if (IS_END(L->value)) // no PATH! yet...
        panic ("ME and MY hit end of input");

    DECLARE_SUBLEVEL (child, L);  // saves TOP_INDEX before refinement push

    const bool push_refinements = true;
    Symbol* opt_label;
    DECLARE_VALUE (temp);
    if (Get_If_Word_Or_Path_Throws(
        temp,
        &opt_label,
        L->value,
        L->specifier,
        push_refinements
    )){
        RETURN (temp);
    }

    if (not Is_Action(temp))
        panic ("ME and MY only work if right hand WORD! is an ACTION!");

    // Here we do something devious.  We subvert the system by setting
    // L->gotten to an infixed version of the function even if it is
    // not infixed.  This lets us slip in a first argument to a function
    // *as if* it were infixed, e.g. `series: my next`.
    //
    Set_Cell_Flag(temp, INFIX_IF_ACTION);
    Push_GC_Guard(temp);
    L->gotten = temp;

    // !!! If we were to give an error on using ME with non-infix or MY with
    // non-prefix, we'd need to know the fetched infix state.  At the moment,
    // Get_If_Word_Or_Path_Throws() does not pass back that information.  But
    // if PATH! is going to do infix dispatch, it should be addressed then.
    //
    UNUSED(Bool_ARG(PREFIX));

    // Since infix dispatch only works for words (for the moment), we lie
    // and use the label found in path processing as a word.
    //
    DECLARE_VALUE (word);
    Init_Word(word, opt_label);
    L->value = word;

    // Simulate as if the passed-in value was calculated into the output slot,
    // which is where infix functions usually find their left hand values.
    //
    Copy_Cell(OUT, ARG(LEFT));

    Flags flags = EVAL_FLAG_FULFILLING_ARG | EVAL_FLAG_POST_SWITCH;
    if (Eval_Step_In_Subframe_Throws(OUT, L, flags, child)) {
        Drop_GC_Guard(temp);
        return BOUNCE_THROWN;
    }

    Drop_GC_Guard(temp);
    return OUT;
}


//
//  do: native [
//
//  {Evaluates a block of source code (directly or fetched according to type)}
//
//      return: [any-atom!]
//      source [
//          <opt-out> ;-- useful for `do maybe ...` scenarios when no match
//          text! ;-- source code in text form
//          binary! ;-- treated as UTF-8
//          url! ;-- load code from URL via protocol
//          file! ;-- load code from file on local disk
//          tag! ;-- module name (URL! looked up from table)
//      ]
//      /args
//          {If value is a script, this will set its system/script/args}
//      arg
//          "Args passed to a script (normally a string)"
//      /only
//          "Don't catch QUIT (default behavior for BLOCK!)"
//  ]
//
DECLARE_NATIVE(DO)
{
    INCLUDE_PARAMS_OF_DO;

    Value* source = ARG(SOURCE); // may be only GC reference, don't lose it!
  #if RUNTIME_CHECKS
    Set_Cell_Flag(ARG(SOURCE), PROTECTED);
  #endif

    switch (Type_Of(source)) {
    case TYPE_BLANK:
        return nullptr; // "blank in, null out" convention

    case TYPE_BLOCK:
    case TYPE_GROUP: {
        REBIXO indexor = Eval_At_Core(
            Init_Void(OUT),  // so `eval []` vanishes
            nullptr, // opt_head (interpreted as no head, not nulled cell)
            Cell_Array(source),
            VAL_INDEX(source),
            VAL_SPECIFIER(source),
            EVAL_FLAG_TO_END
        );

        if (indexor == THROWN_FLAG)
            return BOUNCE_THROWN;

        return OUT; }

    case TYPE_BINARY:
    case TYPE_TEXT:
    case TYPE_URL:
    case TYPE_FILE:
    case TYPE_TAG: {
        //
        // See code called in system/intrinsic/do*
        //
        Value* sys_do_helper = Varlist_Slot(Sys_Context, SYS_CTX_DO_P);
        assert(Is_Action(sys_do_helper));

        UNUSED(Bool_ARG(ARGS)); // detected via `not null? :arg`

        const bool fully = true; // error if not all arguments consumed
        if (Apply_Only_Throws(
            OUT,
            fully,
            sys_do_helper,
            source,
            rebQ(ARG(ARG)), // may be nulled cell
            rebQ(Bool_ARG(ONLY) ? OKAY_VALUE : NULLED_CELL),
            rebEND
        )){
            return BOUNCE_THROWN;
        }
        return OUT; }

    case TYPE_ERROR:
        //
        // PANIC is the preferred operation for triggering errors, as it has
        // a natural behavior for blocks passed to construct readable messages
        // and "PANIC X" more clearly communicates an exception than "DO X"
        // does.  However DO of an ERROR! would have to raise an error
        // anyway, so it might as well raise the one it is given...and this
        // allows the more complex logic of PANIC to be written in Rebol code.
        //
        panic (cast(Error*, Cell_Varlist(source)));

    default:
        break;
    }

    panic (Error_Use_Eval_For_Eval_Raw()); // https://trello.com/c/YMAb89dv
}


//
//  evaluate: native [
//
//  "Run a list through the evaluator iteratively, or take a single step"
//
//      return: "Evaluation product, or ~[position product]~ pack if /STEP3"
//          [any-atom!]  ; /STEP3 changes primary return product [1]
//      source [
//          <opt-out>  ; useful for `evaluate maybe ...` scenarios
//          any-list!  ; code
//          frame!  ; invoke the frame (no arguments, see RUN)
//          error!  ; raise the error
//          varargs!  ; simulates as if frame! or block! is being executed
//      ]
//      /step3 "Take a step and store result in var"
//      var [any-word!]
//  ]
//
DECLARE_NATIVE(EVALUATE)
{
    INCLUDE_PARAMS_OF_EVALUATE;

    Value* source = ARG(SOURCE); // may be only GC reference, don't lose it!
  #if RUNTIME_CHECKS
    Set_Cell_Flag(ARG(SOURCE), PROTECTED);
  #endif

    Value* var = ARG(VAR);

    switch (Type_Of(source)) {
      case TYPE_BLOCK:
      case TYPE_GROUP: {
        REBIXO indexor = Eval_At_Core(
            SET_END(OUT), // use END to distinguish residual non-values
            nullptr, // opt_head
            Cell_Array(source),
            VAL_INDEX(source),
            VAL_SPECIFIER(source),
            Bool_ARG(STEP3) ? DO_MASK_NONE : EVAL_FLAG_TO_END
        );

        if (indexor == THROWN_FLAG)
            return BOUNCE_THROWN;

        if (not Bool_ARG(STEP3)) {
            if (IS_END(OUT))
                Init_Void(OUT);
            return OUT;
        }

        if (indexor == END_FLAG or IS_END(OUT)) {
            if (not Is_Nulled(var))
                Init_Nulled(Sink_Var_May_Panic(var, SPECIFIED));
            return nullptr; // no disruption of output result
        }

        if (not Is_Nulled(var))
            Copy_Cell(Sink_Var_May_Panic(var, SPECIFIED), OUT);

        Copy_Cell(OUT, source);
        VAL_INDEX(OUT) = cast(REBLEN, indexor) - 1; // was one past
        assert(VAL_INDEX(OUT) <= VAL_LEN_HEAD(source));
        return OUT; }

      case TYPE_ACTION: {
        if (Bool_ARG(STEP3))
            panic ("Can't use EVAL/STEP3 on actions");

        // Ren-C will only run arity 0 functions from DO, otherwise REEVAL
        // must be used.  Look for the first non-local parameter to tell.
        //
        Value* param = ACT_PARAMS_HEAD(VAL_ACTION(source));
        while (
            NOT_END(param)
            and (Cell_Parameter_Class(param) == PARAMCLASS_LOCAL)
        ){
            ++param;
        }
        if (NOT_END(param))
            panic (Error_Use_Eval_For_Eval_Raw());

        if (Eval_Value_Throws(OUT, source))
            return BOUNCE_THROWN;
        return OUT; }

      case TYPE_FRAME: {
        if (Bool_ARG(STEP3))
            panic ("Can't use EVAL/STEP3 on frames");

        VarList* c = Cell_Varlist(source); // checks for INACCESSIBLE
        REBACT *phase = VAL_PHASE(source);

        if (Level_Of_Varlist_If_Running(c))
            panic ("Bootstrap executable cannot REDO already running FRAME!s");

        // To DO a FRAME! will "steal" its data.  If a user wishes to use a
        // frame multiple times, they must say DO COPY FRAME, so that the
        // data is stolen from the copy.  This allows for efficient reuse of
        // the context's memory in the cases where a copy isn't needed.

        DECLARE_END_LEVEL (L);
        L->out = OUT;
        Push_Level_At_End(
            L,
            EVAL_FLAG_PROCESS_ACTION
        );

        assert(Varlist_Keys_Head(c) == ACT_PARAMS_HEAD(phase));
        L->param = Varlist_Keys_Head(c);
        VarList* stolen = Steal_Context_Vars(c, phase);
        LINK(stolen).keysource = L;  // changes Varlist_Keys_Head() result

        // Its data stolen, the context's node should now be GC'd when
        // references in other FRAME! value cells have all gone away.
        //
        assert(Is_Node_Managed(c));
        assert(Get_Flex_Info(c, INACCESSIBLE));

        L->varlist = Varlist_Array(stolen);
        L->rootvar = Varlist_Archetype(stolen);
        L->arg = L->rootvar + 1;
        //L->param set above
        L->special = L->arg;

        assert(Level_Phase(L) == phase);
        LVL_BINDING(L) = VAL_BINDING(source); // !!! should archetype match?

        Symbol* opt_label = nullptr;
        Begin_Action(L, opt_label, ORDINARY_ARG);

        bool threw = Eval_Core_Throws(L);

        Drop_Level(L);

        if (threw)
            return BOUNCE_THROWN; // prohibits recovery from exits

        assert(IS_END(L->value)); // we started at END_FLAG, can only throw

        return L->out; }

      case TYPE_VARARGS: {
        Value* position;
        if (Is_Block_Style_Varargs(&position, source)) {
            //
            // We can execute the array, but we must "consume" elements out
            // of it (e.g. advance the index shared across all instances)
            //
            // !!! If any VARARGS! op does not honor the "locked" flag on the
            // array during execution, there will be problems if it is TAKE'n
            // or DO'd while this operation is in progress.
            //
            REBIXO indexor = Eval_At_Core(
                SET_END(OUT),
                nullptr, // opt_head (interpreted as nothing, not nulled cell)
                Cell_Array(position),
                VAL_INDEX(position),
                VAL_SPECIFIER(source),
                Bool_ARG(STEP3) ? DO_MASK_NONE : EVAL_FLAG_TO_END
            );

            if (indexor == THROWN_FLAG) {
                //
                // !!! A BLOCK! varargs doesn't technically need to "go bad"
                // on a throw, since the block is still around.  But a FRAME!
                // varargs does.  This will cause an assert if reused, and
                // having BLANK! mean "thrown" may evolve into a convention.
                //
                Init_Unreadable(position);
                return BOUNCE_THROWN;
            }

            if (not Bool_ARG(STEP3)) {
                if (IS_END(OUT))
                    Init_Void(OUT);
                return OUT;
            }

            if (indexor == END_FLAG or IS_END(OUT)) {
                SET_END(position);  // convention for shared data at end point
                if (not Is_Nulled(var))
                    Init_Nulled(Sink_Var_May_Panic(var, SPECIFIED));
                return nullptr;
            }

            if (not Is_Nulled(var))
                Copy_Cell(Sink_Var_May_Panic(var, SPECIFIED), OUT);

            RETURN (source);  // original VARARGS! will have updated position
        }

        Level* L;
        if (not Is_Level_Style_Varargs_May_Panic(&L, source))
            crash (source); // Frame is the only other type

        // By definition, we are in the middle of a function call in the frame
        // the varargs came from.  It's still on the stack, and we don't want
        // to disrupt its state.  Use a subframe.
        //
        DECLARE_SUBLEVEL (child, L);
        Flags flags = 0;
        if (IS_END(L->value))
            return nullptr;

        while (NOT_END(L->value)) {
            if (Eval_Step_In_Subframe_Throws(SET_END(OUT), L, flags, child))
                return BOUNCE_THROWN;

            if (Bool_ARG(STEP3))
                break;
        }

        if (not Bool_ARG(STEP3)) {
            if (IS_END(OUT))
                Init_Void(OUT);
            return OUT;
        }

        if (IS_END(OUT)) {
            if (not Is_Nulled(var))
                Init_Nulled(Sink_Var_May_Panic(var, SPECIFIED));
            return nullptr;
        }

        if (not Is_Nulled(var))
            Copy_Cell(Sink_Var_May_Panic(var, SPECIFIED), OUT);

        RETURN (source); } // original VARARGS! will have an updated position

      case TYPE_ERROR:
        //
        // PANIC is the preferred operation for triggering errors, as it has
        // a natural behavior for blocks passed to construct readable messages
        // and "PANIC X" more clearly communicates an exception than "EVAL X"
        // does.  However EVAL of an ERROR! would have to raise an error
        // anyway, so it might as well raise the one it is given...and this
        // allows the more complex logic of PANIC to be written in Rebol code.
        //
        panic (cast(Error*, Cell_Varlist(source)));

      default:
        crash (source);
    }
}


//
//  applique: native [
//
//  {Invoke an ACTION! with all required arguments specified}
//
//      return: [any-value!]
//      applicand "Literal action, or location to find one (preserves name)"
//          [action! word! path!]
//      def "Frame definition block (will be bound and evaluated)"
//          [block!]
//  ]
//
DECLARE_NATIVE(APPLIQUE)
//
// !!! Because APPLIQUE is being written as a regular native (and not a
// special exception case inside of Eval_Core) it has to "re-enter" Eval_Core
// and jump to the argument processing.
//
// This could also be accomplished if function dispatch were a subroutine
// that would be called both here and from the evaluator loop.  But if
// the subroutine were parameterized with the frame state, it would be
// basically equivalent to a re-entry.  And re-entry is interesting to
// experiment with for other reasons (e.g. continuations), so that is what
// is used here.
{
    INCLUDE_PARAMS_OF_APPLIQUE;

    Value* applicand = ARG(APPLICAND);

    DECLARE_END_LEVEL (L);  // captures L->stack_base as current TOP_INDEX
    L->out = OUT;

    // Argument can be a literal action (APPLY :APPEND) or a WORD!/PATH!.
    // If it is a path, we push the refinements to the stack so they can
    // be taken into account, e.g. APPLY 'APPEND/ONLY/DUP pushes /ONLY, /DUP
    //
    StackIndex lowest_stackindex = TOP_INDEX;
    Symbol* opt_label;
    if (Get_If_Word_Or_Path_Throws(
        OUT,
        &opt_label,
        applicand,
        SPECIFIED,
        true // push_refinements, don't specialize ACTION! on 'APPEND/ONLY/DUP
    )){
        return BOUNCE_THROWN;
    }

    if (not Is_Action(OUT))
        panic (Error_Invalid(applicand));
    Copy_Cell(applicand, OUT);

    // Make a FRAME! for the ACTION!, weaving in the ordered refinements
    // collected on the stack (if any).  Any refinements that are used in
    // any specialization level will be pushed as well, which makes them
    // out-prioritize (e.g. higher-ordered) than any used in a PATH! that
    // were pushed during the Get of the ACTION!.
    //
    struct Reb_Binder binder;

    VarList* exemplar = Make_Managed_Context_For_Action_May_Panic(
        applicand,
        L->stack_base,  // lowest_stackindex of refinements to weave in
        &binder
    );

    // Bind any SET-WORD!s in the supplied code block into the FRAME!, so
    // e.g. APPLY 'APPEND [VALUE: 10]` will set VALUE in exemplar to 10.
    //
    // !!! Today's implementation mutates the bindings on the passed-in block,
    // like R3-Alpha's MAKE OBJECT!.  See Virtual_Bind_Deep_To_New_Context()
    // for potential future directions.
    //
    Bind_Values_Inner_Loop(
        &binder,
        VAL_ARRAY_HEAD(ARG(DEF)), // !!! bindings are mutated!  :-(
        exemplar,
        FLAGIT_KIND(TYPE_SET_WORD), // types to bind (just set-word!),
        0, // types to "add midstream" to binding as we go (nothing)
        BIND_DEEP
    );

    // Reset all the binder indices to zero, balancing out what was added.
    //
    Cleanup_Specialization_Binder(&binder, exemplar);

    // Run the bound code, ignore evaluative result (unless thrown)
    //
    Push_GC_Guard(exemplar);
    DECLARE_VALUE (temp);
    bool def_threw = Eval_List_At_Throws(temp, ARG(DEF));
    Drop_GC_Guard(exemplar);

    assert(Varlist_Keys_Head(exemplar) == ACT_PARAMS_HEAD(VAL_ACTION(applicand)));
    L->param = Varlist_Keys_Head(exemplar);
    VarList* stolen = Steal_Context_Vars(
        exemplar,
        VAL_ACTION(applicand)
    );
    LINK(stolen).keysource = L;  // changes Varlist_Keys_Head result

    if (def_threw) {
        Free_Unmanaged_Flex(Varlist_Array(stolen)); // could TG_Reuse it
        RETURN (temp);
    }

    Push_Level_At_End(L, EVAL_FLAG_PROCESS_ACTION);

    Drop_Data_Stack_To(lowest_stackindex);  // zero refinements on stack, now

    L->varlist = Varlist_Array(stolen);
    L->rootvar = Varlist_Archetype(stolen);
    L->arg = L->rootvar + 1;
    // L->param assigned above
    L->special = L->arg; // signal only type-check the existing data
    Level_Phase(L) = VAL_ACTION(applicand);
    LVL_BINDING(L) = VAL_BINDING(applicand);

    Begin_Action(L, opt_label, ORDINARY_ARG);

    bool action_threw = Eval_Core_Throws(L);

    Drop_Level(L);

    if (action_threw)
        return BOUNCE_THROWN;

    assert(IS_END(L->value)); // we started at END_FLAG, can only throw
    return OUT;
}
