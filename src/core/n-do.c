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
//  eval: native [
//
//  {(Special) Process received value *inline* as the evaluator loop would.}
//
//      value [<opt> any-value!]
//          {BLOCK! passes-thru, FUNCTION! runs, SET-WORD! assigns...}
//      /only
//          {Suppress evaluation on any ensuing arguments value consumes}
//  ]
//
REBNATIVE(eval)
{
    INCLUDE_PARAMS_OF_EVAL;

    REBFRM *f = frame_; // implicit parameter to every dispatcher/native

    // The REEVALUATE instructions explicitly understand that the value to
    // do reevaluation of is held in the frame's f->cell.  (It would be unsafe
    // to evaluate something held in f->out.)
    //
    Move_Value(&f->cell, ARG(value));

    return REF(only) ? R_REEVALUATE_CELL_ONLY : R_REEVALUATE_CELL;
}


//
//  do: native [
//
//  {Evaluates a block of source code (directly or fetched according to type)}
//
//      return: [<opt> any-value!]
//      source [
//          <opt> ;-- should DO accept an optional argument (chaining?)
//          blank! ;-- same question... necessary, or not?
//          block! ;-- source code in block form
//          string! ;-- source code in text form
//          binary! ;-- treated as UTF-8
//          url! ;-- load code from URL via protocol
//          file! ;-- load code from file on local disk
//          tag! ;-- proposed as module library tag name, hacked as demo
//          error! ;-- should use FAIL instead
//          function! ;-- will only run arity 0 functions (avoids DO variadic)
//          frame! ;-- acts like APPLY (voids are optionals, not unspecialized)
//          varargs! ;-- simulates as if frame! or block! is being executed
//      ]
//      /args
//          {If value is a script, this will set its system/script/args}
//      arg
//          "Args passed to a script (normally a string)"
//      /next
//          {Do next expression only, return it, update block variable}
//      var [any-word! blank!]
//          "If not blank, then a variable updated with new block position"
//      /only
//          "Don't catch QUIT (default behavior for BLOCK!)"
//  ]
//
REBNATIVE(do)
{
    INCLUDE_PARAMS_OF_DO;

    REBVAL *source = ARG(source);

    switch (VAL_TYPE(source)) {
    case REB_MAX_VOID:
        // useful for `do if ...` types of scenarios
        return R_VOID;

    case REB_BLANK:
        // useful for `do all ...` types of scenarios
        return R_BLANK;

    do_block_source:;
    case REB_BLOCK:
    case REB_GROUP:
        if (REF(next)) {
            REBIXO indexor = DO_NEXT_MAY_THROW(
                D_OUT,
                VAL_ARRAY(source),
                VAL_INDEX(source),
                VAL_SPECIFIER(source)
            );

            if (indexor == THROWN_FLAG) {
                //
                // the throw should make the value irrelevant, but if caught
                // then have it indicate the start of the thrown expression
                //
                if (!IS_BLANK(ARG(var))) {
                    Move_Value(
                        Sink_Var_May_Fail(ARG(var), SPECIFIED),
                        source
                    );
                }

                return R_OUT_IS_THROWN;
            }

            if (!IS_BLANK(ARG(var))) {
                //
                // "continuation" of block...turn END_FLAG into the end so it
                // can test TAIL? as true to know the evaluation finished.
                //
                // !!! Is there merit to setting to NONE! instead?  Easier to
                // test and similar to FIND.  On the downside, "lossy" in
                // that after the DOs are finished the var can't be used to
                // recover the series again...you'd have to save it.
                //
                // ***Note:*** While `source` is a local by default, if we
                // jump here via `goto do_block_source`, it will be a singular
                // array inside a varargs, whose position being updated is
                // important.
                //
                if (indexor == END_FLAG)
                    VAL_INDEX(source) = VAL_LEN_HEAD(source);
                else
                    VAL_INDEX(source) = cast(REBCNT, indexor);

                Move_Value(
                    Sink_Var_May_Fail(ARG(var), SPECIFIED),
                    ARG(source)
                );
            }

            return R_OUT;
        }

        if (Do_Any_Array_At_Throws(D_OUT, source))
            return R_OUT_IS_THROWN;

        return R_OUT;

    case REB_BINARY:
    case REB_STRING:
    case REB_URL:
    case REB_FILE:
    case REB_TAG: {
        //
        // See code called in system/intrinsic/do*
        //
        const REBOOL fully = TRUE; // error if not all arguments consumed
        if (Apply_Only_Throws(
            D_OUT,
            fully,
            Sys_Func(SYS_CTX_DO_P),
            source,
            REF(args) ? TRUE_VALUE : FALSE_VALUE,
            REF(args) ? ARG(arg) : BLANK_VALUE, // can't put void in block
            REF(next) ? TRUE_VALUE : FALSE_VALUE,
            REF(next) ? ARG(var) : BLANK_VALUE, // can't put void in block
            REF(only) ? TRUE_VALUE : FALSE_VALUE,
            END
        )) {
            return R_OUT_IS_THROWN;
        }
        return R_OUT; }

    case REB_ERROR:
        //
        // FAIL is the preferred operation for triggering errors, as it has
        // a natural behavior for blocks passed to construct readable messages
        // and "FAIL X" more clearly communicates a failure than "DO X"
        // does.  However DO of an ERROR! would have to raise an error
        // anyway, so it might as well raise the one it is given...and this
        // allows the more complex logic of FAIL to be written in Rebol code.
        //
        fail (VAL_CONTEXT(source));

    case REB_FUNCTION: {
        //
        // Ren-C will only run arity 0 functions from DO, otherwise EVAL
        // must be used.  Look for the first non-local parameter to tell.
        //
        REBVAL *param = FUNC_PARAMS_HEAD(VAL_FUNC(source));
        while (
            NOT_END(param)
            && (VAL_PARAM_CLASS(param) == PARAM_CLASS_LOCAL)
        ) {
            ++param;
        }
        if (NOT_END(param))
            fail (Error_Use_Eval_For_Eval_Raw());

        if (Eval_Value_Throws(D_OUT, source))
            return R_OUT_IS_THROWN;
        return R_OUT;
    }

    case REB_FRAME: {
        REBCTX *c = VAL_CONTEXT(source);

        // It doesn't matter if a FRAME! is currently running or not, because
        // the variables are copied and a new frame is made.  However, the
        // variables must be available, so the function can't have finished
        // and thrown them away.  (This doesn't happen with a frame that
        // was made with `MAKE FRAME!`)
        //
        // !!! It may come to pass that a trick lets you reuse a stack context
        // and unwind it as a kind of tail recursion to reuse it.  But one
        // would not want that strange voodoo to be what DO does on a FRAME!,
        // it would have to be another operation (REDO ?)
        //
        if (CTX_VARS_UNAVAILABLE(c))
            fail (Error_Do_Expired_Frame_Raw());

        const REBSTR *opt_label = NULL; // no label available
        return Apply_Def_Or_Exemplar(
            D_OUT,
            source->payload.any_context.phase,
            VAL_BINDING(source),
            opt_label,
            NOD(VAL_CONTEXT(source))
        ); }

    case REB_VARARGS: {
        REBVAL *position;
        if (Is_Block_Style_Varargs(&position, source)) {
            //
            // We can execute the array, but we must "consume" elements out
            // of it (e.g. advance the index shared across all instances)
            // This is done by the REB_BLOCK case, which we jump to.
            //
            // !!! If any VARARGS! op does not honor the "locked" flag on the
            // array during execution, there will be problems if it is TAKE'n
            // or DO'd while this operation is in progress.
            //
            source = position;
            goto do_block_source;
        }

        REBFRM *f;
        if (NOT(Is_Frame_Style_Varargs_May_Fail(&f, source)))
            panic(source); // Frame is the only other type

        // Pretty much by definition, we are in the middle of a function call
        // in the frame the varargs came from.  It's still on the stack, and
        // we don't want to disrupt its state.  Use a subframe.
        //
        if (Do_Next_In_Subframe_Throws(
            D_OUT,
            f,
            REF(next) ? DO_FLAG_NORMAL : DO_FLAG_TO_END
        )){
            return R_OUT_IS_THROWN;
        }

        // The variable passed in /NEXT is just set ot the vararg itself,
        // which has its positioning updated automatically by virtue of the
        // evaluation performing a "consumption" of VARARGS! content.
        //
        if (REF(next)) {
            Move_Value(
                Sink_Var_May_Fail(ARG(var), SPECIFIED),
                ARG(source)
            );
        }

        return R_OUT; }

    default:
        break;
    }

    // Note: it is not possible to write a wrapper function in Rebol
    // which can do what EVAL can do for types that consume arguments
    // (like SET-WORD!, SET-PATH! and FUNCTION!).  DO used to do this for
    // functions only, EVAL generalizes it.
    //
    fail (Error_Use_Eval_For_Eval_Raw());
}


//
//  do-all: native [
//
//  {Execute a series of BAR!-separated statements with error/quit recovery.}
//
//      return: [<opt> any-value!]
//      block [block!]
//  ]
//
REBNATIVE(do_all)
//
// !!! The name of this construct is under review, as well as whether it
// should be a block-of-blocks or use BAR!.  It was added to try and solve
// a problem, but then not used--however some variant of this feature is
// useful.
{
    INCLUDE_PARAMS_OF_DO_ALL;

    // Holds either an error value that is raised, or the thrown value.
    //
    DECLARE_LOCAL (arg_or_error);
    SET_END(arg_or_error);
    PUSH_GUARD_VALUE(arg_or_error);

    // If arg_or_error is not end, but thrown_name is an end, a throw tried
    // to propagate, but was caught...but if thrown_name is an end and the
    // arg_or_error is also not, it is an error which tried to propagate.
    //
    DECLARE_LOCAL (thrown_name);
    SET_END(thrown_name);
    PUSH_GUARD_VALUE(thrown_name);

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(block));

    // The trap must be pushed *after* the frame has been pushed, so that
    // when a fail() happens it won't pop the running frame.
    //
    struct Reb_State state;
    REBCTX *error;

repush:
    PUSH_TRAP(&error, &state);

    // The first time through the following code 'error' will be NULL, but...
    // `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        if (NOT_END(arg_or_error)) { // already a throw or fail pending!
            DECLARE_LOCAL (arg1);
            if (IS_END(thrown_name)) {
                assert(IS_ERROR(arg_or_error));
                Move_Value(arg1, arg_or_error);
            }
            else {
                CONVERT_NAME_TO_THROWN(thrown_name, arg_or_error);
                Init_Error(arg1, Error_No_Catch_For_Throw(thrown_name));
            }

            DECLARE_LOCAL (arg2);
            Init_Error(arg2, error);

            fail (Error_Multiple_Do_Errors_Raw(arg1, arg2));
        }

        f->eval_type = REB_0; // invariant of Do_Next_In_Frame

        assert(IS_END(thrown_name));
        Init_Error(arg_or_error, error);

        while (NOT_END(f->value) && NOT(IS_BAR(f->value)))
            Fetch_Next_In_Frame(f);

        goto repush;
    }

    Init_Void(D_OUT); // default return result of DO-ALL []

    while (NOT_END(f->value)) {
        if (IS_BAR(f->value)) {
            //
            // BAR! is handled explicitly, because you might have f->value as
            // the BAR! in `| asdf`, call into the evaluator and get an error,
            // yet then come back and still have f->value positioned at the
            // BAR!.  This comes from how child frames and optimizations work.
            // Hence it's not easy to know where to skip forward to in case
            // of an error.
            //
            // !!! Review if the invariant of Do_Next_In_Frame_Throws()
            // should be changed.  So far, this is the only routine affected,
            // because no other functions try and "resume" a throwing/failing
            // frame--as that's not generically possible unless you skip to
            // the next BAR!, as this routine does.
            //
            Init_Void(D_OUT);
            Fetch_Next_In_Frame(f);
            continue;
        }

        if (Do_Next_In_Frame_Throws(D_OUT, f)) {
            if (NOT_END(arg_or_error)) { // already a throw or fail pending!
                DECLARE_LOCAL (arg1);
                if (IS_END(thrown_name)) {
                    assert(IS_ERROR(arg_or_error));
                    Move_Value(arg1, arg_or_error);
                }
                else {
                    CONVERT_NAME_TO_THROWN(thrown_name, arg_or_error);
                    Init_Error(arg1, Error_No_Catch_For_Throw(thrown_name));
                }

                DECLARE_LOCAL (arg2);
                Init_Error(arg2, Error_No_Catch_For_Throw(D_OUT));

                // We're still inside the pushed trap for this throw.  Have
                // to drop the trap to avoid transmitting the error to the
                // `if (error)` longjmp branch above!
                //
                DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

                fail (Error_Multiple_Do_Errors_Raw(arg1, arg2));
            }

            CATCH_THROWN(arg_or_error, D_OUT);
            Move_Value(thrown_name, D_OUT); // THROWN cleared by CATCH_THROWN

            while (NOT_END(f->value) && NOT(IS_BAR(f->value)))
                Fetch_Next_In_Frame(f);
        }
    }

    Drop_Frame(f);

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    DROP_GUARD_VALUE(thrown_name); // no GC (via Do_Core()) after this point
    DROP_GUARD_VALUE(arg_or_error);

    if (IS_END(arg_or_error)) { // no throws or errors tried to propagate
        assert(IS_END(thrown_name));
        return R_OUT;
    }

    if (NOT_END(thrown_name)) { // throw tried propagating, re-throw it
        Move_Value(D_OUT, thrown_name);
        CONVERT_NAME_TO_THROWN(D_OUT, arg_or_error);
        return R_OUT_IS_THROWN;
    }

    assert(IS_ERROR(arg_or_error));
    fail (VAL_CONTEXT(arg_or_error)); // error tried propagating, re-raise it
}


//
//  apply: native [
//
//  {Invoke a function with all required arguments specified.}
//
//      return: [<opt> any-value!]
//      value [function! any-word! any-path!]
//          {Function or specifying word (preserves word name for debug info)}
//      def [block!]
//          {Frame definition block (will be bound and evaluated)}
//  ]
//
REBNATIVE(apply)
{
    INCLUDE_PARAMS_OF_APPLY;

    REBVAL *def = ARG(def);

    // We don't limit to taking a FUNCTION! value directly, because that loses
    // the symbol (for debugging, errors, etc.)  If caller passes a WORD!
    // then we lookup the variable to get the function, but save the symbol.
    //
    REBSTR *opt_label;
    Get_If_Word_Or_Path_Arg(D_OUT, &opt_label, ARG(value));

    if (!IS_FUNCTION(D_OUT))
        fail (Error_Apply_Non_Function_Raw(ARG(value))); // for SPECIALIZE too

    return Apply_Def_Or_Exemplar(
        D_OUT,
        VAL_FUNC(D_OUT),
        VAL_BINDING(D_OUT),
        opt_label,
        NOD(def)
    );
}


//
//  also: native [
//
//  {Returns the first value, but also evaluates the second.}
//
//      return: [<opt> any-value!]
//      returned [<opt> any-value!]
//      evaluated [<opt> any-value!]
//  ]
//
REBNATIVE(also)
{
    INCLUDE_PARAMS_OF_ALSO;

    UNUSED(PAR(evaluated)); // not used (but was evaluated)
    Move_Value(D_OUT, ARG(returned));
    return R_OUT;
}


//
//  comment: native [
//
//  {Ignores the argument value.}
//
//      return: [<opt>]
//          {Nothing.}
//      :value [block! any-string! binary! any-scalar!]
//          "Literal value to be ignored."
//  ]
//
REBNATIVE(comment)
{
    INCLUDE_PARAMS_OF_COMMENT;

    // All the work was already done (at the cost of setting up
    // state that would just have to be torn down).

    UNUSED(PAR(value)); // avoid unused variable warning
    return R_VOID;
}
