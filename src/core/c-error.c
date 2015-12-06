/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  c-error.c
**  Summary: error handling
**  Section: core
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/


#include "sys-core.h"


//
//  Push_Trap_Helper: C
// 
// Used by both TRY and TRY_ANY, whose differentiation comes
// from how they react to HALT.
//
void Push_Trap_Helper(REBOL_STATE *s)
{
    assert(Saved_State || (DSP == -1 && !DSF));

    s->dsp = DSP;
    s->top_chunk = TG_Top_Chunk;

    s->call = DSF;

    s->series_guard_tail = GC_Series_Guard->tail;
    s->value_guard_tail = GC_Value_Guard->tail;
    s->gc_disable = GC_Disabled;

    s->manuals_tail = SERIES_TAIL(GC_Manuals);

    s->last_state = Saved_State;
    Saved_State = s;

    // !!! Is this initialization necessary?
    s->error = NULL;
}


//
//  Trapped_Helper_Halted: C
// 
// This is used by both PUSH_TRAP and PUSH_UNHALTABLE_TRAP to do
// the work of responding to a longjmp.  (Hence it is run when
// setjmp returns TRUE.)  Its job is to safely recover from
// a sudden interruption, though the list of things which can
// be safely recovered from is finite.  Among the countless
// things that are not handled automatically would be a memory
// allocation.
// 
// (Note: This is a crucial difference between C and C++, as
// C++ will walk up the stack at each level and make sure
// any constructors have their associated destructors run.
// *Much* safer for large systems, though not without cost.
// Rebol's greater concern is not so much the cost of setup
// for stack unwinding, but being able to be compiled without
// requiring a C++ compiler.)
// 
// Returns whether the trapped error was a RE_HALT or not.
//
REBOOL Trapped_Helper_Halted(REBOL_STATE *state)
{
    REBOOL halted;

    // Check for more "error frame validity"?
    ASSERT_FRAME(state->error);
    assert(FRAME_TYPE(state->error) == REB_ERROR);

    halted = (ERR_NUM(state->error) == RE_HALT);

    // Restore Rebol call stack frame at time of Push_Trap.  Also, our
    // topmost call state (which may have been pushed but not put into
    // effect) has been accounted for by the drop.
    //
    CS_Running = CS_Top = state->call;

    // Restore Rebol data stack pointer at time of Push_Trap
    DS_DROP_TO(state->dsp);

    // Drop to the chunk state at the time of Push_Trap
    while (TG_Top_Chunk != state->top_chunk)
        Drop_Chunk(NULL);

    // Free any manual series that were extant at the time of the error
    // (that were created since this PUSH_TRAP started).  This includes
    // any arglist series in call frames that have been wiped off the stack.
    // (Closure series will be managed.)
    //
    assert(GC_Manuals->tail >= state->manuals_tail);
    while (GC_Manuals->tail != state->manuals_tail) {
        // Freeing the series will update the tail...
        Free_Series(cast(REBSER**, GC_Manuals->data)[GC_Manuals->tail - 1]);
    }

    GC_Series_Guard->tail = state->series_guard_tail;
    GC_Value_Guard->tail = state->value_guard_tail;

    GC_Disabled = state->gc_disable;

    Saved_State = state->last_state;

    return halted;
}


//
//  Convert_Name_To_Thrown_Debug: C
// 
// Debug-only version of CONVERT_NAME_TO_THROWN
// 
// Sets a task-local value to be associated with the name and
// mark it as the proxy value indicating a THROW().
//
void Convert_Name_To_Thrown_Debug(REBVAL *name, const REBVAL *arg)
{
    assert(!THROWN(name));
    VAL_SET_OPT(name, OPT_VALUE_THROWN);

    assert(IS_TRASH_DEBUG(&TG_Thrown_Arg));
    assert(!IS_TRASH_DEBUG(arg));

    TG_Thrown_Arg = *arg;
}


//
//  Catch_Thrown_Debug: C
// 
// Debug-only version of TAKE_THROWN_ARG
// 
// Gets the task-local value associated with the thrown,
// and clears the thrown bit from thrown.
// 
// WARNING: 'out' can be the same pointer as 'thrown'
//
void Catch_Thrown_Debug(REBVAL *out, REBVAL *thrown)
{
    assert(THROWN(thrown));
    VAL_CLR_OPT(thrown, OPT_VALUE_THROWN);

    assert(!IS_TRASH_DEBUG(&TG_Thrown_Arg));

    *out = TG_Thrown_Arg;

    SET_TRASH_IF_DEBUG(&TG_Thrown_Arg);
}


//
//  Fail_Core: C
// 
// Cause a "trap" of an error by longjmp'ing to the enclosing
// PUSH_TRAP or PUSH_TRAP_ANY.  Although the error being passed
// may not be something that strictly represents an error
// condition (e.g. a BREAK or CONTINUE or THROW), if it gets
// passed to this routine then it has not been caught by its
// intended recipient, and is being treated as an error.
//
ATTRIBUTE_NO_RETURN void Fail_Core(REBFRM *frame)
{
    ASSERT_FRAME(frame);
    assert(FRAME_TYPE(frame) == REB_ERROR);

#if !defined(NDEBUG)
    // All calls to Fail_Core should originate from the `fail` macro,
    // which in the debug build sets TG_Erroring_C_File and TG_Erroring_C_Line.
    // Any error creations as arguments to that fail should have picked
    // it up, and we now need to NULL it out so other Make_Error calls
    // that are not inside of a fail invocation don't get confused and
    // have the wrong information

    assert(TG_Erroring_C_File);
    TG_Erroring_C_File = NULL;

    // If we raise the error we'll lose the stack, and if it's an early
    // error we always want to see it (do not use ATTEMPT or TRY on
    // purpose in Init_Core()...)

    if (PG_Boot_Phase < BOOT_DONE) {
        REBVAL error;
        Val_Init_Error(&error, frame);
        Debug_Fmt("** Error raised during Init_Core(), should not happen!");
        Debug_Fmt("%v", &error);
        assert(FALSE);
    }
#endif

    if (!Saved_State) {
        // There should be a PUSH_TRAP of some kind in effect if a `fail` can
        // ever be run, so mention that before panicking.  The error contains
        // arguments and information, however, so that should be the panic

        Debug_Fmt("*** NO \"SAVED STATE\" - PLEASE MENTION THIS FACT! ***");
        panic (frame);
    }

    if (Trace_Level) {
        Debug_Fmt(
            cs_cast(BOOT_STR(RS_TRACE, 10)),
            &ERR_VALUES(frame)->type,
            &ERR_VALUES(frame)->id
        );
    }

    // We pass the error as a frame rather than as a value.

    Saved_State->error = frame;

    // If a THROWN() was being processed up the stack when the error was
    // raised, then it had the thrown argument set.  Trash it in debug
    // builds.  (The value will not be kept alive, it is not seen by GC)

    SET_TRASH_IF_DEBUG(&TG_Thrown_Arg);

    LONG_JUMP(Saved_State->cpu_state, 1);
}


//
//  Trap_Stack_Overflow: C
// 
// See comments on C_STACK_OVERFLOWING.  This routine is
// deliberately separate and simple so that it allocates no
// objects or locals...and doesn't run any code that itself
// might wind up calling C_STACK_OVERFLOWING.  Hence it uses
// the preallocated TASK_STACK_ERROR frame.
//
void Trap_Stack_Overflow(void)
{
    if (!Saved_State) {
        // The most likely case for there not being a PUSH_TRAP in effect
        // would be a stack overflow during boot.

        Debug_Fmt("*** NO \"SAVED STATE\" - PLEASE MENTION THIS FACT! ***");
        panic (VAL_FRAME(TASK_STACK_ERROR));
    }

    Saved_State->error = VAL_FRAME(TASK_STACK_ERROR);

    LONG_JUMP(Saved_State->cpu_state, 1);
}


//
//  Stack_Depth: C
//
REBCNT Stack_Depth(void)
{
    struct Reb_Call *call = DSF;
    REBCNT count = 0;

    while (call) {
        count++;
        call = PRIOR_DSF(call);
    }

    return count;
}


//
//  Make_Backtrace: C
// 
// Return a block of backtrace words.
//
REBSER *Make_Backtrace(REBINT start)
{
    REBCNT depth = Stack_Depth();
    REBSER *blk = Make_Array(depth - start);
    struct Reb_Call *call;
    REBVAL *val;

    for (call = DSF; call != NULL; call = PRIOR_DSF(call)) {
        if (start-- <= 0) {
            val = Alloc_Tail_Array(blk);
            Val_Init_Word_Unbound(val, REB_WORD, DSF_LABEL_SYM(call));
        }
    }

    return blk;
}


//
//  Find_Error_For_Code: C
// 
// Find the id word, the error type (category) word, and the error
// message template block-or-string for a given error number.
// 
// This scans the data which is loaded into the boot file by
// processing %errors.r
// 
// If the message is not found, return NULL.  Will not write to
// `id_out` or `type_out` unless returning a non-NULL pointer.
//
REBVAL *Find_Error_For_Code(REBVAL *id_out, REBVAL *type_out, REBCNT code)
{
    // See %errors.r for the list of data which is loaded into the boot
    // file as objects for the "error catalog"
    REBFRM *categories = VAL_FRAME(Get_System(SYS_CATALOG, CAT_ERRORS));

    REBFRM *category;
    REBCNT n;
    REBVAL *message;

    // Find the correct catalog category
    n = code / 100; // 0 for Special, 1 for Internal...
    if (n > FRAME_LEN(categories))
        return NULL;

    // Get frame of object representing the elements of the category itself
    if (!IS_OBJECT(FRAME_VAR(categories, n + 1))) {
        assert(FALSE);
        return NULL;
    }
    category = VAL_FRAME(FRAME_VAR(categories, n + 1));

    // Find the correct template in the catalog category (see %errors.r)
    n = code % 100; // 0-based order within category
    if (n + 2 > FRAME_LEN(category)) // +2 account for CODE: TYPE:
        return NULL;

    // Sanity check CODE: field of category object
    if (!IS_INTEGER(FRAME_VAR(category, 1))) {
        assert(FALSE);
        return NULL;
    }
    assert(
        cast(REBCNT, VAL_INT32(FRAME_VAR(category, 1))) == (code / 100) * 100
    );

    // Sanity check TYPE: field of category object
    // !!! Same spelling as what we set in VAL_WORD_SYM(type_out))?
    if (!IS_STRING(FRAME_VAR(category, 2))) {
        assert(FALSE);
        return NULL;
    }

    message = FRAME_VAR(category, n + 3);

    // Error message template must be string or block
    assert(IS_BLOCK(message) || IS_STRING(message));

    // Success! Write category word from the category list frame key sym,
    // and specific error ID word from the frame key sym within category
    Val_Init_Word_Unbound(
        type_out,
        REB_WORD,
        VAL_TYPESET_SYM(FRAME_KEY(categories, (code / 100) + 1))
    );
    Val_Init_Word_Unbound(
        id_out,
        REB_WORD,
        VAL_TYPESET_SYM(FRAME_KEY(category, (code % 100) + 3))
    );

    return message;
}


#if !defined(NDEBUG)

//
//  Make_Guarded_Arg123_Error_Frame: C
// 
// Needed only for compatibility trick to "fake in" ARG1: ARG2: ARG3:
// 
// Rebol2 and R3-Alpha errors were limited to three arguments with
// fixed names, arg1 arg2 arg3.  (Though R3 comments alluded to
// the idea that MAKE ERROR! from an OBJECT! would inherit that
// object's fields, it did not actually work.)  With FAIL and more
// flexible error creation this is being extended.
// 
// Change is not made to the root error object because there is no
// "moment" to effect that (e.g. <r3-legacy> mode will not be started
// at boot time, it happens after).  This allows the stock args to be
// enabled and disabled dynamically in the legacy settings, at the
// cost of creating a new error object each time.
// 
// To make code handling it like the regular error frame (and keep that
// code "relatively uncontaminated" by the #ifdefs), it must behave
// as GC managed.  So it has to be guarded, thus the client drops the
// guard and it will wind up being freed since it's not in the root set.
// This is a bit inefficient but it's for legacy mode only, so best
// to bend to the expectations of the non-legacy code.
//
static REBFRM *Make_Guarded_Arg123_Error_Frame(void)
{
    REBFRM *root_frame = VAL_FRAME(ROOT_ERROBJ);
    REBFRM *frame = Copy_Frame_Shallow_Extra_Managed(root_frame, 3);
    REBVAL *key;
    REBVAL *var;
    REBCNT n;

    // Update the length to suppress out of bounds assert from FRAME_KEY/VAL
    //
    FRAME_VARLIST(frame)->tail += 3;
    FRAME_KEYLIST(frame)->tail += 3;

    key = FRAME_KEY(frame, FRAME_LEN(root_frame) + 1);
    var = FRAME_KEY(frame, FRAME_LEN(root_frame) + 1);

    for (n = 0; n < 3; n++, key++, var++) {
        Val_Init_Typeset(key, ALL_64, SYM_ARG1 + n);
        SET_NONE(var);
    }

    SET_END(key);
    SET_END(var);

    MANAGE_FRAME(frame);
    PUSH_GUARD_FRAME(frame);
    return frame;
}

#endif


//
//  Make_Error_Object_Throws: C
// 
// Creates an error object from arg and puts it in value.
// The arg can be a string or an object body block.
// 
// Returns TRUE if a THROWN() value is made during evaluation.
// 
// This function is called by MAKE ERROR!.  Note that most often
// system errors from %errors.r are thrown by C code using
// Make_Error(), but this routine accommodates verification of
// errors created through user code...which may be mezzanine
// Rebol itself.  A goal is to not allow any such errors to
// be formed differently than the C code would have made them,
// and to cross through the point of R3-Alpha error compatibility,
// which makes this a rather tortured routine.  However, it
// maps out the existing landscape so that if it is to be changed
// then it can be seen exactly what is changing.
//
REBFLG Make_Error_Object_Throws(
    REBVAL *out, // output location **MUST BE GC SAFE**!
    REBVAL *arg
) {
    // Frame from the error object template defined in %sysobj.r
    REBFRM *root_frame = VAL_FRAME(ROOT_ERROBJ);

    REBFRM *frame;
    ERROR_OBJ *error_obj;

#if !defined(NDEBUG)
    if (LEGACY(OPTIONS_ARG1_ARG2_ARG3_ERROR))
        root_frame = Make_Guarded_Arg123_Error_Frame();
#endif

    if (IS_ERROR(arg) || IS_OBJECT(arg)) {
        // Create a new error object from another object, including any
        // non-standard fields.  WHERE: and NEAR: will be overridden if
        // used.  If ID:, TYPE:, or CODE: were used in a way that would
        // be inconsistent with a Rebol system error, an error will be
        // raised later in the routine.

        frame = Merge_Frames(root_frame, VAL_FRAME(arg));
        error_obj = ERR_VALUES(frame);
    }
    else if (IS_BLOCK(arg)) {
        // If a block, then effectively MAKE OBJECT! on it.  Afterward,
        // apply the same logic as if an OBJECT! had been passed in above.

        REBVAL evaluated;

        // Bind and do an evaluation step (as with MAKE OBJECT! with A_MAKE
        // code in REBTYPE(Object) and code in REBNATIVE(construct))

        frame = Make_Frame_Detect(
            REB_ERROR, // type
            NULL, // spec
            NULL, // body
            VAL_BLK_DATA(arg), // values to scan for toplevel set-words
            root_frame // parent
        );

        // Protect the frame from GC by putting into out, which must be
        // passed in as a GC-protecting value slot.
        //
        Val_Init_Error(out, frame);

        Rebind_Frame_Deep(root_frame, frame, REBIND_FUNC);
        Bind_Values_Deep(VAL_BLK_DATA(arg), frame);

        if (DO_ARRAY_THROWS(&evaluated, arg)) {
            *out = evaluated;

        #if !defined(NDEBUG)
            // Let our fake root_frame that had arg1: arg2: arg3: on it be
            // garbage collected.
            if (LEGACY(OPTIONS_ARG1_ARG2_ARG3_ERROR))
                DROP_GUARD_FRAME(root_frame);
        #endif

            return TRUE;
        }

        error_obj = ERR_VALUES(frame);
    }
    else if (IS_STRING(arg)) {
        //
        // String argument to MAKE ERROR! makes a custom error from user:
        //
        //     code: 1000 ;-- default none
        //     type: 'user
        //     id: 'message
        //     message: "whatever the string was" ;-- default none
        //
        // Minus the code number and message, this is the default state of
        // root_frame if not overridden.

        frame = Copy_Frame_Shallow_Managed(root_frame);

        VAL_SET(FRAME_CONTEXT(frame), REB_ERROR); // !!! fix in Init_Errors()?

        error_obj = ERR_VALUES(frame);
        assert(IS_NONE(&error_obj->code));

        // fill in RE_USER (1000) later if it passes the check

        Val_Init_String(&error_obj->message, Copy_Sequence_At_Position(arg));
    }
    else {
        // No other argument types are handled by this routine at this time.

        fail (Error(RE_INVALID_ERROR, arg));
    }

    // Validate the error contents, and reconcile message template and ID
    // information with any data in the object.  Do this for the IS_STRING
    // creation case just to make sure the rules are followed there too.

    // !!! Note that this code is very cautious because the goal isn't to do
    // this as efficiently as possible, rather to put up lots of alarms and
    // traffic cones to make it easy to pick and choose what parts to excise
    // or tighten in an error enhancement upgrade.

    if (IS_INTEGER(&error_obj->code)) {
        if (VAL_INT32(&error_obj->code) < RE_USER) {
            // Users can make up anything for error codes allocated to them,
            // but Rebol's historical default is to "own" error codes less
            // than 1000.  If a code is used in the sub-1000 range then make
            // sure any id or type provided do not conflict.

            REBVAL id;
            REBVAL type;
            REBVAL *message;

            if (!IS_NONE(&error_obj->message)) // assume a MESSAGE: is wrong
                fail (Error(RE_INVALID_ERROR, arg));

            message = Find_Error_For_Code(
                &id,
                &type,
                cast(REBCNT, VAL_INT32(&error_obj->code))
            );

            if (!message)
                fail (Error(RE_INVALID_ERROR, arg));

            error_obj->message = *message;

            if (!IS_NONE(&error_obj->id)) {
                if (
                    !IS_WORD(&error_obj->id)
                    || !SAME_SYM(
                        VAL_WORD_SYM(&error_obj->id), VAL_WORD_SYM(&id)
                    )
                ) {
                    fail (Error(RE_INVALID_ERROR, arg));
                }
            }
            error_obj->id = id; // normalize binding and case

            if (!IS_NONE(&error_obj->type)) {
                if (
                    !IS_WORD(&error_obj->id)
                    || !SAME_SYM(
                        VAL_WORD_SYM(&error_obj->type), VAL_WORD_SYM(&type)
                    )
                ) {
                    fail (Error(RE_INVALID_ERROR, arg));
                }
            }
            error_obj->type = type; // normalize binding and case

            // !!! TBD: Check that all arguments were provided!
        }
    }
    else if (IS_WORD(&error_obj->type) && IS_WORD(&error_obj->id)) {
        // If there was no CODE: supplied but there was a TYPE: and ID: then
        // this may overlap a combination used by Rebol where we wish to
        // fill in the code.  (No fast lookup for this, must search.)

        REBFRM *categories = VAL_FRAME(Get_System(SYS_CATALOG, CAT_ERRORS));
        REBVAL *category;

        assert(IS_NONE(&error_obj->code));

        // Find correct category for TYPE: (if any)
        category = Find_Word_Value(categories, VAL_WORD_SYM(&error_obj->type));
        if (category) {
            REBCNT code;
            REBVAL *message;

            assert(IS_OBJECT(category)); // SELF: 0

            assert(
                SAME_SYM(VAL_TYPESET_SYM(VAL_CONTEXT_KEY(category, 1)), SYM_CODE)
            );
            assert(IS_INTEGER(VAL_CONTEXT_VALUE(category, 1)));
            code = cast(REBCNT, VAL_INT32(VAL_CONTEXT_VALUE(category, 1)));

            assert(
                SAME_SYM(VAL_TYPESET_SYM(VAL_CONTEXT_KEY(category, 2)), SYM_TYPE)
            );
            assert(IS_STRING(VAL_CONTEXT_VALUE(category, 2)));

            // Find correct message for ID: (if any)
            message = Find_Word_Value(
                VAL_FRAME(category), VAL_WORD_SYM(&error_obj->id)
            );

            if (message) {
                assert(IS_STRING(message) || IS_BLOCK(message));

                if (!IS_NONE(&error_obj->message))
                    fail (Error(RE_INVALID_ERROR, arg));

                error_obj->message = *message;

                SET_INTEGER(&error_obj->code,
                    code
                    + Find_Word_Index(frame, VAL_WORD_SYM(&error_obj->id), FALSE)
                    - Find_Word_Index(frame, SYM_TYPE, FALSE)
                    - 1
                );
            }
            else {
                // At the moment, we don't let the user make a user-ID'd
                // error using a category from the internal list just
                // because there was no id from that category.  In effect
                // all the category words have been "reserved"

                // !!! Again, remember this is all here just to show compliance
                // with what the test suite tested for, it disallowed e.g.
                // it expected the following to be an illegal error because
                // the `script` category had no `set-self` error ID.
                //
                //     make error! [type: 'script id: 'set-self]

                fail (Error(RE_INVALID_ERROR, arg));
            }
        }
        else {
            // The type and category picked did not overlap any existing one
            // so let it be a user error.
            SET_INTEGER(&error_obj->code, RE_USER);
        }
    }
    else {
        // It's either a user-created error or otherwise.  It may
        // have bad ID, TYPE, or message fields, or a completely
        // strange code #.  The question of how non-standard to
        // tolerate is an open one.

        // For now we just write 1000 into the error code field, if that was
        // not already there.

        if (IS_NONE(&error_obj->code))
            SET_INTEGER(&error_obj->code, RE_USER);
        else if (IS_INTEGER(&error_obj->code)) {
            if (VAL_INT32(&error_obj->code) != RE_USER)
                fail (Error(RE_INVALID_ERROR, arg));
        }
        else
            fail (Error(RE_INVALID_ERROR, arg));

        // !!! Because we will experience crashes in the molding logic,
        // we put some level of requirement besides "code # not 0".
        // This is conservative logic and not good for general purposes.

        if (
            !(IS_WORD(&error_obj->id) || IS_NONE(&error_obj->id))
            || !(IS_WORD(&error_obj->type) || IS_NONE(&error_obj->type))
            || !(
                IS_BLOCK(&error_obj->message)
                || IS_STRING(&error_obj->message)
                || IS_NONE(&error_obj->message)
            )
        ) {
            fail (Error(RE_INVALID_ERROR, arg));
        }
    }

    assert(IS_INTEGER(&error_obj->code));

#if !defined(NDEBUG)
    // Let our fake root_frame that had arg1: arg2: arg3: on it be
    // garbage collected.
    if (LEGACY(OPTIONS_ARG1_ARG2_ARG3_ERROR))
        DROP_GUARD_FRAME(root_frame);
#endif

    Val_Init_Error(out, frame);
    return FALSE;
}


//
//  Make_Error_Core: C
// 
// (va_list by pointer: http://stackoverflow.com/a/3369762/211160)
// 
// Create and init a new error object based on a C vararg list
// and an error code.  This routine is responsible also for
// noticing if there is an attempt to make an error at a time
// that is too early for error creation, and not try and invoke
// the error creation machinery.  That means if you write:
// 
//     panic (Error(RE_SOMETHING, arg1, ...));
// 
// ...and it's too early to make an error, the inner call to
// Error will be the one doing the panic.  Hence, both fail and
// panic behave identically in that early phase of the system
// (though panic is better documentation that one knows the
// error cannot be trapped).
// 
// Besides that caveat and putting running-out-of-memory aside,
// this routine should not fail internally.  Hence it should
// return to the caller to properly call va_end with no longjmp
// to skip it.
//
REBFRM *Make_Error_Core(REBCNT code, REBFLG up_stack, va_list *args)
{
    REBFRM *root_frame;

    REBFRM *frame; // Error object frame
    ERROR_OBJ *error_obj; // Error object values
    REBVAL *message;
    REBVAL id;
    REBVAL type;

    REBCNT expected_args;

#if !defined(NDEBUG)
    // The legacy error mechanism expects us to have exactly three fields
    // in each error generated by the C code with names arg1: arg2: arg3.
    // Track how many of those we've gone through if we need to.
    static const REBCNT legacy_data[] = {SYM_ARG1, SYM_ARG2, SYM_ARG3, SYM_0};
    const REBCNT *arg1_arg2_arg3 = legacy_data;
#endif

    assert(code != 0);

    if (PG_Boot_Phase < BOOT_ERRORS) {
        Panic_Core(code, NULL, args);
        DEAD_END;
    }

    // Safe to initialize the root frame now...
    root_frame = VAL_FRAME(ROOT_ERROBJ);

    message = Find_Error_For_Code(&id, &type, code);
    assert(message);

    if (IS_BLOCK(message)) {
        // For a system error coming from a C vararg call, the # of
        // GET-WORD!s in the format block should match the varargs supplied.

        REBVAL *temp = VAL_BLK_HEAD(message);
        expected_args = 0;
        while (NOT_END(temp)) {
            if (IS_GET_WORD(temp))
                expected_args++;
            else
                assert(IS_STRING(temp));
            temp++;
        }
    }
    else {
        // Just a string, no arguments expected.

        assert(IS_STRING(message));
        expected_args = 0;
    }

#if !defined(NDEBUG)
    if (LEGACY(OPTIONS_ARG1_ARG2_ARG3_ERROR)) {
        // However many arguments were expected, forget it in legacy mode...
        // there will be 3 even if they're not all used, arg1: arg2: arg3:
        expected_args = 3;
    }
    else {
        // !!! We may have the C source file and line information for where
        // the error was triggered, if this error is being created during
        // invocation of a `fail` or `panic`.  (The file and line number are
        // captured before the parameter to the invoker is evaluated).
        // Add them in the error so they can be seen with PROBE but not
        // when FORM'd to users.

        if (TG_Erroring_C_File)
            expected_args += 2;
    }
#endif

    if (expected_args == 0) {
        // If there are no arguments, we don't need to make a new keylist...
        // just a new varlist to hold this instance's settings. (root
        // frame keylist is already managed)

        frame = Copy_Frame_Shallow_Managed(root_frame);

        // !!! Should tweak root frame during boot so it actually is an ERROR!
        // (or use literal error construction syntax, if it worked?)
        //
        VAL_SET(FRAME_CONTEXT(frame), REB_ERROR);
    }
    else {
        REBVAL *key;
        REBVAL *value;
        REBVAL *temp;
        REBSER *keylist;

        // Should the error be well-formed, we'll need room for the new
        // expected values *and* their new keys in the keylist.
        //
        frame = Copy_Frame_Shallow_Extra_Managed(root_frame, expected_args);

        // !!! Should tweak root frame during boot so it actually is an ERROR!
        // (or use literal error construction syntax, if it worked?)
        //
        VAL_SET(FRAME_CONTEXT(frame), REB_ERROR);

        // Fix up the tail first so FRAME_KEY and FRAME_VAR don't complain
        // in the debug build that they're accessing beyond the frame length
        //
        FRAME_VARLIST(frame)->tail += expected_args;
        FRAME_KEYLIST(frame)->tail += expected_args;

        key = FRAME_KEY(frame, FRAME_LEN(root_frame) + 1);
        value = FRAME_VAR(frame, FRAME_LEN(root_frame) + 1);

    #ifdef NDEBUG
        temp = VAL_BLK_HEAD(message);
    #else
        // Will get here even for a parameterless string due to throwing in
        // the extra "arguments" of the __FILE__ and __LINE__
        temp = IS_STRING(message) ? END_VALUE : VAL_BLK_HEAD(message);
    #endif

        while (NOT_END(temp)) {
            if (IS_GET_WORD(temp)) {
                REBVAL *arg = va_arg(*args, REBVAL*);

                if (!arg) {
                    // Terminating with a NULL is optional but can help
                    // catch errors here of too few args passed when the
                    // template expected more substitutions.

                #ifdef NDEBUG
                    // If the C code passed too few args in a debug build,
                    // prevent a crash in the release build by filling it.
                    // No perfect answer if you're going to keep running...
                    // something like ISSUE! #404 could be an homage:
                    //
                    //     http://www.room404.com/page.php?pg=homepage
                    //
                    // But we'll just use NONE.  Debug build asserts here.

                    arg = NONE_VALUE;
                #else
                    Debug_Fmt(
                        "too few args passed for error code %d at %s line %d",
                        code,
                        TG_Erroring_C_File ? TG_Erroring_C_File : "<unknown>",
                        TG_Erroring_C_File ? TG_Erroring_C_Line : -1
                    );
                    assert(FALSE);

                    // !!! Note that we have no way of checking for too *many*
                    // args with C's vararg machinery
                #endif
                }

                ASSERT_VALUE_MANAGED(arg);

            #if !defined(NDEBUG)
                if (LEGACY(OPTIONS_ARG1_ARG2_ARG3_ERROR)) {
                    if (*arg1_arg2_arg3 == SYM_0) {
                        Debug_Fmt("Legacy arg1_arg2_arg3 error with > 3 args");
                        panic (Error(RE_MISC));
                    }
                    Val_Init_Typeset(key, ALL_64, *arg1_arg2_arg3);
                    arg1_arg2_arg3++;
                }
                else
            #endif
                    Val_Init_Typeset(key, ALL_64, VAL_WORD_SYM(temp));

                *value = *arg;

                key++;
                value++;
            }
            temp++;
        }

    #if !defined(NDEBUG)
        if (LEGACY(OPTIONS_ARG1_ARG2_ARG3_ERROR)) {
            // Need to fill in nones for any remaining args.
            while (*arg1_arg2_arg3 != SYM_0) {
                Val_Init_Typeset(key, ALL_64, *arg1_arg2_arg3);
                arg1_arg2_arg3++;
                key++;
                SET_NONE(value);
                value++;
            }
        }
        else if (TG_Erroring_C_File) {
            // This error is being created during a `fail` or `panic`
            // (two extra fields accounted for above in creation)

            // error/__FILE__ (a FILE! value)
            Val_Init_Typeset(key, ALL_64, SYM___FILE__);
            key++;
            Val_Init_File(
                value,
                Append_UTF8(
                    NULL,
                    cb_cast(TG_Erroring_C_File),
                    LEN_BYTES(cb_cast(TG_Erroring_C_File))
                )
            );
            value++;

            // error/__LINE__ (an INTEGER! value)
            Val_Init_Typeset(key, ALL_64, SYM___LINE__);
            key++;
            SET_INTEGER(value, TG_Erroring_C_Line);
            value++;
        }
    #endif

        SET_END(key);
        SET_END(value);
    }

    error_obj = ERR_VALUES(frame);

    // Set error number:
    SET_INTEGER(&error_obj->code, code);

    error_obj->message = *message;
    error_obj->id = id;
    error_obj->type = type;

    // Set backtrace and location information:
    if (DSF && (!up_stack || DSF->prior)) {
        // Where (what function) is the error:
        Val_Init_Block(&error_obj->where, Make_Backtrace(up_stack ? 1 : 0));
        // Nearby location of the error (in block being evaluated):
        Val_Init_Block_Index(
            &error_obj->nearest, DSF_ARRAY(DSF), DSF_EXPR_INDEX(DSF)
        );
    }

    return frame;
}


//
//  Error: C
// 
// This is a variadic function which is designed to be the
// "argument" of either a `fail` or a `panic` "keyword".
// It can be called directly, or indirectly by another proxy
// error function.  It takes a number of REBVAL* arguments
// appropriate for the error number passed.
// 
// With C variadic functions it is not known how many arguments
// were passed.  Make_Error_Core() knows how many arguments are
// in an error's template in %errors.r for a given error #, so
// that is the number of arguments it will attempt to use.
// If desired, a caller can pass a NULL after the last argument
// to double-check that too few arguments are not given, though
// this is not enforced (to help with callsite readability).
//
// If the error number is negative, this signals that it should not be seen
// as originating from the current stack frame but rather the frame above.
// This is useful for frameless natives that are doing argument checking
// from within their own bodies but do not want to appear in the error's
// call stack, because if they were not frameless then they wouldn't have
// been invoked yet.
//
REBFRM *Error(REBINT num, ... /* REBVAL *arg1, REBVAL *arg2, ... */)
{
    va_list args;
    REBFRM *frame;

    va_start(args, num);
    frame = Make_Error_Core((num < 0 ? -num : num), num < 0, &args);
    va_end(args);

    return frame;
}


//
//  Error_Bad_Func_Def: C
//
REBFRM *Error_Bad_Func_Def(const REBVAL *spec, const REBVAL *body)
{
    // !!! Improve this error; it's simply a direct emulation of arity-1
    // error that existed before refactoring code out of MT_Function().

    REBVAL def;
    REBSER *series = Make_Array(2);
    Append_Value(series, spec);
    Append_Value(series, body);
    Val_Init_Block(&def, series);
    return Error(RE_BAD_FUNC_DEF, &def, NULL);
}


//
//  Error_No_Arg: C
//
REBFRM *Error_No_Arg(REBCNT label_sym, const REBVAL *key)
{
    REBVAL key_word;
    REBVAL label;

    assert(IS_TYPESET(key));

    Val_Init_Word_Unbound(&key_word, REB_WORD, VAL_TYPESET_SYM(key));
    Val_Init_Word_Unbound(&label, REB_WORD, label_sym);

    return Error(
        (!DSF || DSF->arg ? RE_NO_ARG : -RE_NO_ARG), &label, &key_word, NULL
    );
}


//
//  Error_Invalid_Datatype: C
//
REBFRM *Error_Invalid_Datatype(REBCNT id)
{
    REBVAL id_value;
    SET_INTEGER(&id_value, id);
    return Error(RE_INVALID_DATATYPE, &id_value, NULL);
}


//
//  Error_No_Memory: C
//
REBFRM *Error_No_Memory(REBCNT bytes)
{
    REBVAL bytes_value;
    SET_INTEGER(&bytes_value, bytes);
    return Error(RE_NO_MEMORY, &bytes_value, NULL);
}


//
//  Error_Invalid_Arg: C
// 
// This error is pretty vague...it's just "invalid argument"
// and the value with no further commentary or context.  It
// becomes a catch all for "unexpected input" when a more
// specific error would be more useful.
//
REBFRM *Error_Invalid_Arg(const REBVAL *value)
{
    return Error(RE_INVALID_ARG, value, NULL);
}


//
//  Error_No_Catch_For_Throw: C
//
REBFRM *Error_No_Catch_For_Throw(REBVAL *thrown)
{
    REBVAL arg;
    assert(THROWN(thrown));
    CATCH_THROWN(&arg, thrown); // clears bit

    if (IS_NONE(thrown))
        return Error(RE_NO_CATCH, &arg, NULL);

    return Error(RE_NO_CATCH_NAMED, &arg, thrown, NULL);
}


//
//  Error_Has_Bad_Type: C
// 
// <type> type is not allowed here
//
REBFRM *Error_Has_Bad_Type(const REBVAL *value)
{
    return Error(RE_INVALID_TYPE, Type_Of(value), NULL);
}


//
//  Error_Out_Of_Range: C
// 
// value out of range: <value>
//
REBFRM *Error_Out_Of_Range(const REBVAL *arg)
{
    return Error(RE_OUT_OF_RANGE, arg, NULL);
}


//
//  Error_Protected_Key: C
//
REBFRM *Error_Protected_Key(REBVAL *key)
{
    REBVAL key_name;
    assert(IS_TYPESET(key));
    Val_Init_Word_Unbound(&key_name, REB_WORD, VAL_TYPESET_SYM(key));

    return Error(RE_LOCKED_WORD, &key_name, NULL);
}


//
//  Error_Illegal_Action: C
//
REBFRM *Error_Illegal_Action(REBCNT type, REBCNT action)
{
    REBVAL action_word;
    Val_Init_Word_Unbound(&action_word, REB_WORD, Get_Action_Sym(action));

    return Error(RE_CANNOT_USE, &action_word, Get_Type(type), NULL);
}


//
//  Error_Math_Args: C
//
REBFRM *Error_Math_Args(enum Reb_Kind type, REBCNT action)
{
    REBVAL action_word;
    Val_Init_Word_Unbound(&action_word, REB_WORD, Get_Action_Sym(action));

    return Error(RE_NOT_RELATED, &action_word, Get_Type(type), NULL);
}


//
//  Error_Unexpected_Type: C
//
REBFRM *Error_Unexpected_Type(enum Reb_Kind expected, enum Reb_Kind actual)
{
    assert(expected < REB_MAX);
    assert(actual < REB_MAX);

    return Error(RE_EXPECT_VAL, Get_Type(expected), Get_Type(actual), NULL);
}


//
//  Error_Arg_Type: C
// 
// Function in frame of `call` expected parameter `param` to be
// a type different than the arg given (which had `arg_type`)
//
REBFRM *Error_Arg_Type(
    REBCNT label_sym,
    const REBVAL *param,
    const REBVAL *arg_type
) {
    REBVAL param_word;
    REBVAL label_word;

    assert(IS_TYPESET(param));
    Val_Init_Word_Unbound(&param_word, REB_WORD, VAL_TYPESET_SYM(param));
    Val_Init_Word_Unbound(&label_word, REB_WORD, label_sym);

    assert(IS_DATATYPE(arg_type));
    return Error(
        (!DSF || DSF->arg ? RE_EXPECT_ARG : -RE_EXPECT_ARG),
        &label_word,
        arg_type,
        &param_word,
        NULL
    );
}


//
//  Error_Bad_Make: C
//
REBFRM *Error_Bad_Make(REBCNT type, const REBVAL *spec)
{
    return Error(RE_BAD_MAKE_ARG, Get_Type(type), spec, NULL);
}


//
//  Error_Cannot_Reflect: C
//
REBFRM *Error_Cannot_Reflect(REBCNT type, const REBVAL *arg)
{
    return Error(RE_CANNOT_USE, arg, Get_Type(type), NULL);
}


//
//  Error_On_Port: C
//
REBFRM *Error_On_Port(REBCNT errnum, REBFRM *port, REBINT err_code)
{
    REBVAL *spec = FRAME_VAR(port, STD_PORT_SPEC);
    REBVAL *val;
    REBVAL err_code_value;

    if (!IS_OBJECT(spec)) fail (Error(RE_INVALID_PORT));

    val = Get_Object(spec, STD_PORT_SPEC_HEAD_REF); // most informative
    if (IS_NONE(val)) val = Get_Object(spec, STD_PORT_SPEC_HEAD_TITLE);

    SET_INTEGER(&err_code_value, err_code);
    return Error(errnum, val, &err_code_value, NULL);
}


//
//  Exit_Status_From_Value: C
// 
// This routine's job is to turn an arbitrary value into an
// operating system exit status:
// 
//     https://en.wikipedia.org/wiki/Exit_status
//
int Exit_Status_From_Value(REBVAL *value)
{
    assert(!THROWN(value));

    if (IS_INTEGER(value)) {
        // Fairly obviously, an integer should return an integer
        // result.  But Rebol integers are 64 bit and signed, while
        // exit statuses don't go that large.
        //
        return VAL_INT32(value);
    }
    else if (IS_UNSET(value) || IS_NONE(value)) {
        // An unset would happen with just QUIT or EXIT and no /WITH,
        // so treating that as a 0 for success makes sense.  A NONE!
        // seems like nothing to report as well, for instance:
        //
        //     exit/with if badthing [badthing-code]
        //
        return 0;
    }
    else if (IS_ERROR(value)) {
        // Rebol errors do have an error number in them, and if your
        // program tries to return a Rebol error it seems it wouldn't
        // hurt to try using that.  They may be out of range for
        // platforms using byte-sized error codes, however...but if
        // that causes bad things OS_EXIT() should be graceful about it.
        //
        return VAL_ERR_NUM(value);
    }

    // Just 1 otherwise.
    //
    return 1;
}


//
//  Init_Errors: C
//
void Init_Errors(REBVAL *errors)
{
    REBFRM *errs;
    REBVAL *val;

    // Create error objects and error type objects:
    *ROOT_ERROBJ = *Get_System(SYS_STANDARD, STD_ERROR);
    errs = Construct_Frame(REB_OBJECT, VAL_BLK_HEAD(errors), FALSE, NULL);

    Val_Init_Object(Get_System(SYS_CATALOG, CAT_ERRORS), errs);

    // Create objects for all error types:
    for (val = FRAME_VAR(errs, 1); NOT_END(val); val++) {
        errs = Construct_Frame(REB_OBJECT, VAL_BLK_HEAD(val), FALSE, NULL);
        Val_Init_Object(val, errs);
    }
}


//
//  Security_Policy: C
// 
// Given a security symbol (like FILE) and a value (like the file
// path) returns the security policy (RWX) allowed for it.
// 
// Args:
// 
//     sym:  word that represents the type ['file 'net]
//     name: file or path value
// 
// Returns BTYE array of flags for the policy class:
// 
//     flags: [rrrr wwww xxxx ----]
// 
//     Where each byte is:
//         0: SEC_ALLOW
//         1: SEC_ASK
//         2: SEC_THROW
//         3: SEC_QUIT
// 
// The secuity is defined by the system/state/policies object, that
// is of the form:
// 
//     [
//         file:  [%file1 tuple-flags %file2 ... default tuple-flags]
//         net:   [...]
//         call:  tuple-flags
//         stack: tuple-flags
//         eval:  integer (limit)
//     ]
//
REBYTE *Security_Policy(REBCNT sym, REBVAL *name)
{
    REBVAL *policy = Get_System(SYS_STATE, STATE_POLICIES);
    REBYTE *flags;
    REBCNT len;
    REBCNT errcode = RE_SECURITY_ERROR;

    if (!IS_OBJECT(policy)) goto error;

    // Find the security class in the block: (file net call...)
    policy = Find_Word_Value(VAL_FRAME(policy), sym);
    if (!policy) goto error;

    // Obtain the policies for it:
    // Check for a master tuple: [file rrrr.wwww.xxxx]
    if (IS_TUPLE(policy)) return VAL_TUPLE(policy); // non-aligned
    // removed A90: if (IS_INTEGER(policy)) return (REBYTE*)VAL_INT64(policy); // probably not used

    // Only other form is detailed block:
    if (!IS_BLOCK(policy)) goto error;

    // Scan block of policies for the class: [file [allow read quit write]]
    len = 0;    // file or url length
    flags = 0;  // policy flags
    for (policy = VAL_BLK_HEAD(policy); NOT_END(policy); policy += 2) {

        // Must be a policy tuple:
        if (!IS_TUPLE(policy+1)) goto error;

        // Is it a policy word:
        if (IS_WORD(policy)) { // any word works here
            // If no strings found, use the default:
            if (len == 0) flags = VAL_TUPLE(policy+1); // non-aligned
        }

        // Is it a string (file or URL):
        else if (ANY_BINSTR(policy) && name) {
            //Debug_Fmt("sec: %r %r", policy, name);
            if (Match_Sub_Path(VAL_SERIES(policy), VAL_SERIES(name))) {
                // Is the match adequate?
                if (VAL_TAIL(name) >= len) {
                    len = VAL_TAIL(name);
                    flags = VAL_TUPLE(policy+1); // non-aligned
                }
            }
        }
        else goto error;
    }

    if (!flags) {
        errcode = RE_SECURITY;
        policy = name ? name : 0;
error:
        if (!policy) {
            Val_Init_Word_Unbound(DS_TOP, REB_WORD, sym);
            policy = DS_TOP;
        }
        fail (Error(errcode, policy));
    }

    return flags;
}


//
//  Trap_Security: C
// 
// Take action on the policy flags provided. The sym and value
// are provided for error message purposes only.
//
void Trap_Security(REBCNT flag, REBCNT sym, REBVAL *value)
{
    if (flag == SEC_THROW) {
        if (!value) {
            Val_Init_Word_Unbound(DS_TOP, REB_WORD, sym);
            value = DS_TOP;
        }
        fail (Error(RE_SECURITY, value));
    }
    else if (flag == SEC_QUIT) OS_EXIT(101);
}


//
//  Check_Security: C
// 
// A helper function that fetches the security flags for
// a given symbol (FILE) and value (path), and then tests
// that they are allowed.
//
void Check_Security(REBCNT sym, REBCNT policy, REBVAL *value)
{
    REBYTE *flags;

    flags = Security_Policy(sym, value);
    Trap_Security(flags[policy], sym, value);
}
