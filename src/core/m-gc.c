//
//  File: %m-gc.c
//  Summary: "main memory garbage collection"
//  Section: memory
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
// The garbage collector is based on a conventional "mark and sweep":
//
//     https://en.wikipedia.org/wiki/Tracing_garbage_collection
//
// From an optimization perspective, there is an attempt to not incur
// function call overhead just to check if a GC-aware item has its
// SERIES_FLAG_MARK flag set.  So the flag is checked by a macro before making
// any calls to process the references inside of an item.
//
// "Shallow" marking only requires setting the flag, and is suitable for
// series like strings (which are not containers for other REBVALs).  In
// debug builds shallow marking is done with a function anyway, to give
// a place to put assertion code or set breakpoints to catch when a
// shallow mark is set (when that is needed).
//
// "Deep" marking was originally done with recursion, and the recursion
// would stop whenever a mark was hit.  But this meant deeply nested
// structures could quickly wind up overflowing the C stack.  Consider:
//
//     a: copy []
//     loop 200'000 [a: append/only copy [] a]
//     recycle
//
// The simple solution is that when an unmarked item is hit that it is
// marked and put into a queue for processing (instead of recursed on the
// spot.  This queue is then handled as soon as the marking stack is
// exited, and the process repeated until no more items are queued.
//
// Regarding the two stages:
//
// MARK -  Mark all series and gobs ("collectible values")
//         that can be found in:
//
//         Root Block: special structures and buffers
//         Task Block: special structures and buffers per task
//         Data Stack: current state of evaluation
//         Safe Series: saves the last N allocations
//
// SWEEP - Free all collectible values that were not marked.
//
// GC protection methods:
//
// KEEP flag - protects an individual series from GC, but
//     does not protect its contents (if it holds values).
//     Reserved for non-block system series.
//
// Root_Vars - protects all series listed. This list is
//     used by Sweep as the root of the in-use memory tree.
//     Reserved for important system series only.
//
// Task_Vars - protects all series listed. This list is
//     the same as Root, but per the current task context.
//
// Save_Series - protects temporary series. Used with the
//     SAVE_SERIES and UNSAVE_SERIES macros. Throws and errors
//     must roll back this series to avoid "stuck" memory.
//
// Safe_Series - protects last MAX_SAFE_SERIES series from GC.
//     Can only be used if no deeply allocating functions are
//     called within the scope of its protection. Not affected
//     by throws and errors.
//
// Data_Stack - all values in the data stack that are below
//     the TOP (DSP) are automatically protected. This is a
//     common protection method used by native functions.
//
// DONE flag - do not scan the series; it has no links.
//

#include "sys-core.h"

#include "mem-pools.h" // low-level memory pool access
#include "mem-series.h" // low-level series memory access

#include "reb-evtypes.h"


//
//  Push_Array_Marked_Deep: C
// 
// Note: Call MARK_ARRAY_DEEP or QUEUE_MARK_ARRAY_DEEP instead!
// 
// Submits the block into the deferred stack to be processed later
// with Propagate_All_GC_Marks().  We have already set this series
// mark as it's now "spoken for".  (Though we haven't marked its
// dependencies yet, we want to prevent it from being wastefully
// submitted multiple times by another reference that would still
// see it as "unmarked".)
// 
// The data structure used for this processing is a stack and not
// a queue (for performance reasons).  But when you use 'queue'
// as a verb it has more leeway than as the CS noun, and can just
// mean "put into a list for later processing", hence macro names.
//
static void Push_Array_Marked_Deep(REBARR *array)
{
#if !defined(NDEBUG)
    if (!IS_ARRAY_MANAGED(array)) {
        Debug_Fmt("Link to non-MANAGED item reached by GC");
        Panic_Array(array);
    }
#endif

    assert(GET_ARR_FLAG(array, SERIES_FLAG_ARRAY));

    if (GET_ARR_FLAG(array, CONTEXT_FLAG_STACK)) {
        //
        // If the array's storage was on the stack and that stack level has
        // been popped, its data has been nulled out, and the series only
        // exists to keep words or objects holding it from crashing.
        //
        if (!GET_ARR_FLAG(array, SERIES_FLAG_ACCESSIBLE))
            return;
    }

    // !!! Are there actually any "external" series that are value-bearing?
    // e.g. a REBSER node which has a ->data pointer to REBVAL[...] and
    // expects this to be managed with GC, even though if the REBSER is
    // GC'd it shouldn't free that data?
    //
    assert(!GET_ARR_FLAG(array, SERIES_FLAG_EXTERNAL));

    // set by calling macro (helps catch direct calls of this function)
    assert(IS_REBSER_MARKED(ARR_SERIES(array)));

    // Add series to the end of the mark stack series and update terminator

    if (SER_FULL(GC_Mark_Stack)) Extend_Series(GC_Mark_Stack, 8);

    *SER_AT(REBARR*, GC_Mark_Stack, SER_LEN(GC_Mark_Stack)) = array;

    SET_SERIES_LEN(GC_Mark_Stack, SER_LEN(GC_Mark_Stack) + 1);

    *SER_AT(REBARR*, GC_Mark_Stack, SER_LEN(GC_Mark_Stack)) = NULL;
}


static void Propagate_All_GC_Marks(void);

#ifndef NDEBUG
    static REBOOL in_mark = FALSE;
#endif


// Deferred form for marking series that prevents potentially overflowing the
// C execution stack.

inline static void QUEUE_MARK_ARRAY_DEEP(REBARR *a) {
    if (IS_REBSER_MARKED(ARR_SERIES(a)))
        return;

    MARK_REBSER(ARR_SERIES(a));
    Push_Array_Marked_Deep(a);
}

inline static void QUEUE_MARK_CONTEXT_DEEP(REBCTX *c) {
    assert(GET_ARR_FLAG(CTX_VARLIST(c), ARRAY_FLAG_CONTEXT_VARLIST));
    QUEUE_MARK_ARRAY_DEEP(CTX_KEYLIST(c));
    QUEUE_MARK_ARRAY_DEEP(CTX_VARLIST(c));
}


// Non-Queued form for marking blocks.  Used for marking a *root set item*,
// don't recurse from within Mark_Value/Mark_Gob/Mark_Array_Deep/etc.

inline static void MARK_ARRAY_DEEP(REBARR *a) {
    assert(!in_mark);
    QUEUE_MARK_ARRAY_DEEP(a);
    Propagate_All_GC_Marks();
}

inline static void MARK_CONTEXT_DEEP(REBCTX *c) {
    assert(!in_mark);
    QUEUE_MARK_CONTEXT_DEEP(c);
    Propagate_All_GC_Marks();
}


// Assertion for making sure that all the deferred marks have been propagated

#define ASSERT_NO_GC_MARKS_PENDING() \
    assert(SER_LEN(GC_Mark_Stack) == 0)


// Non-Deep form of mark, to be used on non-BLOCK! series or a block series
// for which deep marking is known to be unnecessary.
//
static inline void MARK_SERIES_ONLY(REBSER *series)
{
#if !defined(NDEBUG)
    if (NOT(IS_SERIES_MANAGED(series))) {
        Debug_Fmt("Link to non-MANAGED item reached by GC");
        Panic_Series(series);
    }
#endif

    // Don't use MARK_REBSER, because that expects unmarked.  This should be
    // fast and tolerate setting the bit again without checking.
    //
    series->header.bits |= REBSER_REBVAL_FLAG_MARK;
}


//
//  Queue_Mark_Gob_Deep: C
// 
// 'Queue' refers to the fact that after calling this routine,
// one will have to call Propagate_All_GC_Marks() to have the
// deep transitive closure be guaranteed fully marked.
// 
// Note: only referenced blocks are queued, the GOB structure
// itself is processed via recursion.  Deeply nested GOBs could
// in theory overflow the C stack.
//
static void Queue_Mark_Gob_Deep(REBGOB *gob)
{
    REBGOB **pane;
    REBCNT i;

    if (IS_GOB_MARK(gob)) return;

    MARK_GOB(gob);

    if (GOB_PANE(gob)) {
        MARK_REBSER(GOB_PANE(gob));
        pane = GOB_HEAD(gob);
        for (i = 0; i < GOB_LEN(gob); i++, pane++)
            Queue_Mark_Gob_Deep(*pane);
    }

    if (GOB_PARENT(gob)) Queue_Mark_Gob_Deep(GOB_PARENT(gob));

    if (GOB_CONTENT(gob)) {
        if (GOB_TYPE(gob) >= GOBT_IMAGE && GOB_TYPE(gob) <= GOBT_STRING)
            MARK_REBSER(GOB_CONTENT(gob));
        else if (GOB_TYPE(gob) >= GOBT_DRAW && GOB_TYPE(gob) <= GOBT_EFFECT)
            QUEUE_MARK_ARRAY_DEEP(AS_ARRAY(GOB_CONTENT(gob)));
    }

    if (GOB_DATA(gob)) {
        switch (GOB_DTYPE(gob)) {
        case GOBD_INTEGER:
        case GOBD_NONE:
        default:
            break;
        case GOBD_OBJECT:
            QUEUE_MARK_CONTEXT_DEEP(AS_CONTEXT(GOB_DATA(gob)));
            break;
        case GOBD_STRING:
        case GOBD_BINARY:
            MARK_SERIES_ONLY(GOB_DATA(gob));
            break;
        case GOBD_BLOCK:
            QUEUE_MARK_ARRAY_DEEP(AS_ARRAY(GOB_DATA(gob)));
        }
    }
}


//
//  Queue_Mark_Field_Deep: C
// 
// 'Queue' refers to the fact that after calling this routine,
// one will have to call Propagate_All_GC_Marks() to have the
// deep transitive closure be guaranteed fully marked.
// 
// Note: only referenced blocks are queued, fields that are structs
// will be processed via recursion.  Deeply nested structs could
// in theory overflow the C stack.
//
static void Queue_Mark_Field_Deep(
    struct Struct_Field *field,
    REBSER *data_bin,
    REBCNT offset
){
    if (field->is_rebval) {
        //
        // !!! The FFI apparently can tunnel REBVALs through to callbacks.
        // They would generally appear as raw sizeof(REBVAL) blobs to the
        // C routines processing them.  The GC considers the REBVAL* to be
        // "live", and there may be an array of them...so they are marked
        // much as a REBARR would.
        //
        assert(field->type == FFI_TYPE_POINTER);
        assert(field->dimension % 4 == 0);
        assert(field->size == sizeof(REBVAL));

        REBCNT i;
        for (i = 0; i < field->dimension; i += 4) {
            REBVAL *value = cast(REBVAL*,
                SER_AT(
                    REBYTE,
                    data_bin,
                    offset + field->offset + i * field->size
                )
            );

            if (field->done)
                Queue_Mark_Value_Deep(value);
        }
    }
    else if (field->type == FFI_TYPE_STRUCT) {
        assert(!field->is_rebval);
        MARK_SERIES_ONLY(field->fields);
        QUEUE_MARK_ARRAY_DEEP(field->spec);

        REBCNT i;
        for (i = 0; i < SER_LEN(field->fields); ++i) {
            struct Struct_Field *subfield
                = SER_AT(struct Struct_Field, field->fields, i);

            // !!! If offset doesn't reflect the actual offset of this field
            // inside the structure this will have to be revisited (it should
            // be because you need to be able to reuse schemas
            //
            assert(subfield->offset >= offset);

            Queue_Mark_Field_Deep(subfield, data_bin, subfield->offset);
        }
    }
    else {
        // ignore primitive datatypes
    }

    if (field->name != NULL)
        MARK_SERIES_ONLY(field->name);
}


//
//  Queue_Mark_Routine_Deep: C
// 
// 'Queue' refers to the fact that after calling this routine,
// one will have to call Propagate_All_GC_Marks() to have the
// deep transitive closure completely marked.
// 
// Note: only referenced blocks are queued, the routine's RValue
// is processed via recursion.  Deeply nested RValue structs could
// in theory overflow the C stack.
//
static void Queue_Mark_Routine_Deep(REBRIN *r)
{
    SET_RIN_FLAG(r, ROUTINE_FLAG_MARK);

    // Mark the descriptions for the return type and argument types.
    //
    // !!! This winds up being a bit convoluted, because an OBJECT!-like thing
    // is being implemented as a HANDLE! to a series, in order to get the
    // behavior of multiple references and GC'd when the last goes away.
    // This "schema" concept also allows the `ffi_type` descriptive structures
    // to be garbage collected.  Replace with OBJECT!s in the future.

    if (IS_HANDLE(&r->ret_schema)) {
        REBSER *schema = cast(REBSER*, VAL_HANDLE_DATA(&r->ret_schema));
        MARK_SERIES_ONLY(schema);
        Queue_Mark_Field_Deep(
            *SER_HEAD(struct Struct_Field*, schema), NULL, 0
        );
    }
    else // special, allows NONE (e.g. void return)
        assert(IS_INTEGER(&r->ret_schema) || IS_BLANK(&r->ret_schema));

    QUEUE_MARK_ARRAY_DEEP(r->args_schemas);
    REBCNT n;
    for (n = 0; n < ARR_LEN(r->args_schemas); ++n) {
        if (IS_HANDLE(ARR_AT(r->args_schemas, n))) {
            REBSER *schema
                = cast(REBSER*, VAL_HANDLE_DATA(ARR_AT(r->args_schemas, n)));
            MARK_SERIES_ONLY(schema);
            Queue_Mark_Field_Deep(
                *SER_HEAD(struct Struct_Field*, schema), NULL, 0
            );
        }
        else
            assert(IS_INTEGER(ARR_AT(r->args_schemas, n)));
    }

    if (GET_RIN_FLAG(r, ROUTINE_FLAG_VARIADIC)) {
        assert(r->cif == NULL);
        assert(r->args_fftypes == NULL);
    }
    else {
        // !!! r->cif should always be set to something in non-variadic
        // routines, but currently the implementation has to tolerate partially
        // formed routines...because evaluations are called during make-routine
        // before the CIF is ready to be created or not.
        //
        if (r->cif)
            MARK_SERIES_ONLY(r->cif);
        if (r->args_fftypes)
            MARK_SERIES_ONLY(r->args_fftypes);
    }

    if (GET_RIN_FLAG(r, ROUTINE_FLAG_CALLBACK)) {
        REBFUN *cb_func = RIN_CALLBACK_FUNC(r);
        if (cb_func) {
            // Should take care of spec, body, etc.
            QUEUE_MARK_ARRAY_DEEP(FUNC_PARAMLIST(cb_func));
        }
        else {
            // !!! There is a call during MAKE_Routine that does an evaluation
            // while creating a callback function, before the CALLBACK_FUNC
            // has been set.  If the garbage collector is invoked at that
            // time, this will happen.  This should be reviewed to see if
            // it can be done another way--e.g. by not making the relevant
            // series visible to the garbage collector via MANAGE_SERIES()
            // until fully constructed.
        }
    } else {
        if (RIN_LIB(r))
            QUEUE_MARK_ARRAY_DEEP(RIN_LIB(r));
        else {
            // may be null if called before the routine is fully constructed
            // !!! Review if this can be made not possible
        }
    }
}


//
//  Queue_Mark_Event_Deep: C
// 
// 'Queue' refers to the fact that after calling this routine,
// one will have to call Propagate_All_GC_Marks() to have the
// deep transitive closure completely marked.
//
static void Queue_Mark_Event_Deep(const RELVAL *value)
{
    REBREQ *req;

    if (
        IS_EVENT_MODEL(value, EVM_PORT)
        || IS_EVENT_MODEL(value, EVM_OBJECT)
        || (
            VAL_EVENT_TYPE(value) == EVT_DROP_FILE
            && GET_FLAG(VAL_EVENT_FLAGS(value), EVF_COPIED)
        )
    ) {
        // !!! Comment says void* ->ser field of the REBEVT is a "port or
        // object" but it also looks to store maps.  (?)
        //
        QUEUE_MARK_ARRAY_DEEP(AS_ARRAY(VAL_EVENT_SER(m_cast(RELVAL*, value))));
    }

    if (IS_EVENT_MODEL(value, EVM_DEVICE)) {
        // In the case of being an EVM_DEVICE event type, the port! will
        // not be in VAL_EVENT_SER of the REBEVT structure.  It is held
        // indirectly by the REBREQ ->req field of the event, which
        // in turn possibly holds a singly linked list of other requests.
        req = VAL_EVENT_REQ(value);

        while (req) {
            // Comment says void* ->port is "link back to REBOL port object"
            if (req->port)
                QUEUE_MARK_CONTEXT_DEEP(AS_CONTEXT(cast(REBSER*, req->port)));
            req = req->next;
        }
    }
}


//
//  Mark_Devices_Deep: C
// 
// Mark all devices. Search for pending requests.
// 
// This should be called at the top level, and as it is not
// 'Queued' it guarantees that the marks have been propagated.
//
static void Mark_Devices_Deep(void)
{
    REBDEV **devices = Host_Lib->devices;

    int d;
    for (d = 0; d < RDI_MAX; d++) {
        REBREQ *req;
        REBDEV *dev = devices[d];
        if (!dev)
            continue;

        for (req = dev->pending; req; req = req->next)
            if (req->port)
                QUEUE_MARK_CONTEXT_DEEP(AS_CONTEXT(cast(REBSER*, req->port)));
    }
}


//
//  Mark_Frame_Stack_Deep: C
// 
// Mark all function call frames.  In addition to containing the
// arguments that are referred to by pointer during a function
// invocation (acquired via D_ARG(N) calls), it is able to point
// to an arbitrary stable memory location for D_OUT.  This may
// be giving awareness to the GC of a variable on the C stack
// (for example).  This also keeps the function value itself
// live, as well as the "label" word and "where" block value.
// 
// Note that prior to a function invocation, the output value
// slot is written with "safe" TRASH.  This helps the evaluator
// catch cases of when a function dispatch doesn't consciously
// write any value into the output in debug builds.  The GC is
// willing to overlook this safe trash, however, and it will just
// be an UNSET! in the release build.
// 
// This should be called at the top level, and not from inside a
// Propagate_All_GC_Marks().  All marks will be propagated.
//
static void Mark_Frame_Stack_Deep(void)
{
    // The GC must consider all entries, not just those that have been pushed
    // into active evaluation.
    //
    struct Reb_Frame *f = TG_Frame_Stack;

    for (; f != NULL; f = f->prior) {
        assert(f->eval_type != ET_TRASH);

        // Should have taken care of reifying all the VALIST on the stack
        // earlier in the recycle process (don't want to create new arrays
        // once the recycling has started...)
        //
        assert(f->index != VA_LIST_FLAG);

        // END_FLAG is possible, because the frame could be sitting at the
        // end of a block when a function runs, e.g. `do [zero-arity]`.
        // That frame will stay on the stack while the zero-arity
        // function is running, which could be arbitrarily long...so
        // a GC could happen.
        //
        // !!! FETCH_NEXT could do the array unprotect, and make it possible
        // to GC the series sooner.
        //
        ASSERT_ARRAY_MANAGED(f->source.array);
        QUEUE_MARK_ARRAY_DEEP(f->source.array);

        if (f->value && NOT_END(f->value) && Is_Value_Managed(f->value))
            Queue_Mark_Value_Deep(f->value);

        if (f->specifier != SPECIFIED)
            QUEUE_MARK_CONTEXT_DEEP(f->specifier);

        // Specialization code may run while an f->out is being held as the
        // left-hand-side of an infix operation.  And SET-PATH! also holds
        // f->out alive across an evaluation.
        //
        if (Is_Any_Function_Frame(f) || f->eval_type == ET_SET_PATH)
            if (!IS_END(f->out) && !IS_VOID_OR_SAFE_TRASH(f->out))
                Queue_Mark_Value_Deep(f->out); // never NULL

        if (NOT(Is_Any_Function_Frame(f))) {
            //
            // The only fields we protect if no function is pending or running
            // with this frame is the array and the potentially pending value.
            //
            // Consider something like `eval copy quote (recycle)`, because
            // while evaluating the group it has no anchor anywhere in the
            // root set and could be GC'd.  The Reb_Frame's array ref is it.
            //
            // !!! Consider the ->param field for SET-WORD! and SET-PATH!,
            // these require protection too (!)
            //
            continue;
        }

        QUEUE_MARK_ARRAY_DEEP(FUNC_PARAMLIST(f->func)); // never NULL
        MARK_SERIES_ONLY(f->label); // also never NULL

        if (f->func == NAT_FUNC(eval)) {
            //
            // EVAL is special because it doesn't use argument lists, it
            // evaluates directly into the f->cell.  (This should be protected
            // by the evaluation's f->out into that cell.)
            //
            continue;
        }

        // The subfeed may be in use by VARARGS!, and it may be either a
        // context or a single element array.  It will only be valid during
        // the function's actual running.
        //
        if (!Is_Function_Frame_Fulfilling(f)) {
            if (f->cell.subfeed) {
                if (GET_ARR_FLAG(f->cell.subfeed, ARRAY_FLAG_CONTEXT_VARLIST))
                    QUEUE_MARK_CONTEXT_DEEP(AS_CONTEXT(f->cell.subfeed));
                else {
                    assert(ARR_LEN(f->cell.subfeed) == 1);
                    QUEUE_MARK_ARRAY_DEEP(f->cell.subfeed);
                }
            }

            assert(IS_END(f->param)); // indicates function is running

            if (
                f->refine // currently allowed to be NULL
                && !IS_END(f->refine)
                && !IS_VOID_OR_SAFE_TRASH(f->refine)
                && Is_Value_Managed(f->refine)
            ) {
                Queue_Mark_Value_Deep(f->refine);
            }
        }

        // !!! symbols are not currently GC'd, but if they were this would
        // need to keep the label sym alive!
        /* Mark_Symbol_Still_In_Use?(f->label_sym); */

        // In the current implementation (under review) functions use
        // stack-based chunks to gather their arguments, and closures use
        // ordinary arrays.  If the call mode is pending then
        // the arglist is under construction, but guaranteed to have all
        // cells be safe for garbage collection.
        //
        if (f->varlist != NULL) {
            //
            // We need to GC protect the values in the varlist no matter what,
            // but it might not be managed yet (e.g. could still contain END
            // markers during argument fulfillment).  But if it is managed,
            // then it needs to be handed to normal GC.
            //
            if (IS_ARRAY_MANAGED(f->varlist)) {
                assert(!IS_TRASH_DEBUG(ARR_AT(f->varlist, 0)));
                assert(GET_ARR_FLAG(f->varlist, ARRAY_FLAG_CONTEXT_VARLIST));
                QUEUE_MARK_CONTEXT_DEEP(AS_CONTEXT(f->varlist));
            }
            else {
                REBCNT num_params = FUNC_NUM_PARAMS(f->func);
                REBVAL *slot = FRM_ARGS_HEAD(f); // may be stack or dynamic
                while (num_params != 0) {
                    if (!IS_END(slot) && !IS_VOID_OR_SAFE_TRASH(slot))
                        Queue_Mark_Value_Deep(slot);
                    ++slot;
                    --num_params;
                }
                assert(IS_END(slot));
            }
        }
        else  {
            // If it's just sequential REBVALs sitting in memory in the chunk
            // stack, then the chunk stack walk already took care of it.
            // (the chunk stack can be used for things other than the call
            // stack, so long as they are stack-like in a call relative way)
        }

        Propagate_All_GC_Marks();
    }
}


//
//  Queue_Mark_Value_Deep: C
// 
// This routine is not marked `static` because it is needed by
// Ren/C++ in order to implement its GC_Mark_Hook.
//
void Queue_Mark_Value_Deep(const RELVAL *val)
{
    REBSER *ser = NULL;

    // If this happens, it means somehow Recycle() got called between
    // when an `if (Do_XXX_Throws())` branch was taken and when the throw
    // should have been caught up the stack (before any more calls made).
    //
    assert(!THROWN(val));

    switch (VAL_TYPE(val)) {
        case REB_0:
            //
            // Critical error; the only array that can handle unsets are the
            // varlists of contexts, and they must do so before getting here.
            //
            panic (Error(RE_MISC));

        case REB_TYPESET:
            //
            // Not all typesets have symbols--only those that serve as the
            // keys of objects (or parameters of functions)
            //
            if (val->extra.key_spelling != NULL)
                MARK_SERIES_ONLY(val->extra.key_spelling);
            break;

        case REB_HANDLE:
            break;

        case REB_DATATYPE:
            // Type spec is allowed to be NULL.  See %typespec.r file
            if (VAL_TYPE_SPEC(val))
                QUEUE_MARK_ARRAY_DEEP(VAL_TYPE_SPEC(val));
            break;

        case REB_TASK: // not yet implemented
            fail (Error(RE_MISC));

        case REB_OBJECT:
        case REB_MODULE:
        case REB_PORT:
        case REB_FRAME:
        case REB_ERROR: {
            REBCTX *context = VAL_CONTEXT(val);

            assert(CTX_TYPE(context) == VAL_TYPE(val));
            assert(VAL_CONTEXT(CTX_VALUE(context)) == context);
            assert(VAL_CONTEXT_META(CTX_VALUE(context)) == CTX_META(context));

            QUEUE_MARK_CONTEXT_DEEP(context);

            // !!! Currently a FRAME! has a keylist which is storing a non-
            // context block spec.  This will be changed to be compatible
            // with the meta on object keylists.
            //
            if (!IS_FRAME(val) && VAL_CONTEXT_META(val))
                QUEUE_MARK_CONTEXT_DEEP(VAL_CONTEXT_META(val));

            // For VAL_CONTEXT_FRAME, the FRM_CALL is either on the stack
            // (in which case it's already taken care of for marking) or it
            // has gone bad, in which case it should be ignored.

            break;
        }

        case REB_FUNCTION: {
            assert(VAL_FUNC_PARAMLIST(val) == FUNC_PARAMLIST(VAL_FUNC(val)));
            QUEUE_MARK_ARRAY_DEEP(VAL_FUNC_PARAMLIST(val));

            // Need to queue the mark of the array for the body--as trying
            // to mark the "singular" value directly could infinite loop.
            //
            QUEUE_MARK_ARRAY_DEEP(val->payload.function.body_holder);

            if (VAL_FUNC_META(val) != NULL)
                QUEUE_MARK_CONTEXT_DEEP(VAL_FUNC_META(val));

            // Of all the function types, only the routines and callbacks use
            // HANDLE! and must be explicitly pointed out in the body.
            //
            if (IS_FUNCTION_RIN(val))
                Queue_Mark_Routine_Deep(VAL_FUNC_ROUTINE(val));
            break;
        }

        case REB_VARARGS: {
            REBARR *subfeed;
            if (GET_VAL_FLAG(val, VARARGS_FLAG_NO_FRAME)) {
                //
                // A single-element shared series node is kept between
                // instances of the same vararg that was created with
                // MAKE ARRAY! - which fits compactly in a REBSER.
                //
                subfeed = *SUBFEED_ADDR_OF_FEED(VAL_VARARGS_ARRAY1(val));
                QUEUE_MARK_ARRAY_DEEP(VAL_VARARGS_ARRAY1(val));
            }
            else {
                //
                // VARARGS! can wind up holding a pointer to a frame that is
                // not managed, because arguments are still being fulfilled
                // in the frame where the varargs lives.  This is a bit snakey,
                // but if that's the state it's in, then it need not worry
                // about GC protecting the frame...because it protects itself
                // so long as the function is running.  (If it tried to
                // protect it, then it could hit unfinished/corrupt arg cells)
                //
                REBARR *varlist = VAL_BINDING(val);
                if (GET_ARR_FLAG(varlist, ARRAY_FLAG_CONTEXT_VARLIST)) {
                    if (IS_ARRAY_MANAGED(varlist)) {
                        QUEUE_MARK_CONTEXT_DEEP(AS_CONTEXT(varlist)); // good
                        subfeed = *SUBFEED_ADDR_OF_FEED(varlist);
                    }
                    else
                        subfeed = NULL; // function still getting args, ENDs
                }
                else {
                    // This can happen because VARARGS! cells are used to
                    // do pickups of param/arg pairs, after conversions from
                    // words, which might have relative binding.  It's not
                    // paid attention to.
                }
            }

            if (subfeed) {
                if (GET_ARR_FLAG(subfeed, ARRAY_FLAG_CONTEXT_VARLIST))
                    QUEUE_MARK_CONTEXT_DEEP(AS_CONTEXT(subfeed));
                else
                    QUEUE_MARK_ARRAY_DEEP(subfeed);
            }

            break;
        }

        case REB_WORD:  // (and also used for function STACK backtrace frame)
        case REB_SET_WORD:
        case REB_GET_WORD:
        case REB_LIT_WORD:
        case REB_REFINEMENT:
        case REB_ISSUE: {
            REBSTR *spelling = val->payload.any_word.spelling;

            // A word marks the specific spelling it uses, but not the canon
            // value.  That's because if the canon value gets GC'd, then
            // another value might become the new canon during that sweep.
            //
            MARK_SERIES_ONLY(spelling);

            // A GC cannot run during a binding process--which is the only
            // time a canon word's "index" field is allowed to be nonzero.
            //
            assert(
                !GET_SER_FLAG(spelling, STRING_FLAG_CANON)
                || (
                    spelling->misc.bind_index.high == 0
                    && spelling->misc.bind_index.low == 0
                )
            );

            // All bound words should keep their contexts from being GC'd...
            // even stack-relative contexts for functions.
            //
            if (GET_VAL_FLAG(val, VALUE_FLAG_RELATIVE)) {
                //
                // Marking the function's paramlist should be enough to
                // mark all the function's properties (there is an embedded
                // function value...)
                //
                REBFUN* func = VAL_WORD_FUNC(val);
                assert(GET_VAL_FLAG(val, WORD_FLAG_BOUND)); // should be set
                QUEUE_MARK_ARRAY_DEEP(FUNC_PARAMLIST(func));
            }
            else if (GET_VAL_FLAG(val, WORD_FLAG_BOUND)) {
                if (IS_SPECIFIC(val)) {
                    REBCTX* context = VAL_WORD_CONTEXT(const_KNOWN(val));
                    QUEUE_MARK_CONTEXT_DEEP(context);
                }
                else {
                    // We trust that if a relative word's context needs to make
                    // it into the transitive closure, that will be taken care
                    // of by the array reference that holds it.
                    //
                    REBFUN* func = VAL_WORD_FUNC(val);
                    QUEUE_MARK_ARRAY_DEEP(FUNC_PARAMLIST(func));
                }
            }
            else if (GET_VAL_FLAG(val, WORD_FLAG_PICKUP)) {
                //
                // Special word class that might be seen on the stack during
                // a GC that's used by argument fulfillment when searching
                // for out-of-order refinements.  It holds two REBVAL*s
                // (for the parameter and argument of the refinement) and
                // both should be covered for GC already, because the
                // paramlist and arg variables are "in progress" for a call.
            }
            else {
                // The word is unbound...make sure index is 0 in debug build.
                //
            #if !defined(NDEBUG)
                assert(val->payload.any_word.index == 0);
            #endif
            }
            break; }

        case REB_BLANK:
        case REB_BAR:
        case REB_LIT_BAR:
        case REB_LOGIC:
        case REB_INTEGER:
        case REB_DECIMAL:
        case REB_PERCENT:
        case REB_MONEY:
        case REB_TIME:
        case REB_DATE:
        case REB_CHAR:
        case REB_PAIR:
        case REB_TUPLE:
            break;

        case REB_STRING:
        case REB_BINARY:
        case REB_FILE:
        case REB_EMAIL:
        case REB_URL:
        case REB_TAG:
        case REB_BITSET:
            ser = VAL_SERIES(val);
            assert(SER_WIDE(ser) <= sizeof(REBUNI));
            MARK_SERIES_ONLY(ser);
            break;

        case REB_IMAGE:
            //SET_SER_FLAG(VAL_SERIES_SIDE(val), SERIES_FLAG_MARK); //????
            MARK_SERIES_ONLY(VAL_SERIES(val));
            break;

        case REB_VECTOR:
            MARK_SERIES_ONLY(VAL_SERIES(val));
            break;

        case REB_BLOCK:
        case REB_GROUP:
        case REB_PATH:
        case REB_SET_PATH:
        case REB_GET_PATH:
        case REB_LIT_PATH: {
            if (IS_SPECIFIC(val)) {
                REBCTX *context = VAL_SPECIFIER(const_KNOWN(val));
                if (context != SPECIFIED)
                    QUEUE_MARK_CONTEXT_DEEP(context);
            }
            else {
                // We trust that if a relative array's context needs to make
                // it into the transitive closure, that will be taken care
                // of by a higher-up array reference that holds it.
                //
                REBFUN* func = VAL_RELATIVE(val);
                QUEUE_MARK_ARRAY_DEEP(FUNC_PARAMLIST(func));
            }

            QUEUE_MARK_ARRAY_DEEP(VAL_ARRAY(val));
            break;
        }

        case REB_MAP: {
            REBMAP* map = VAL_MAP(val);
            QUEUE_MARK_ARRAY_DEEP(MAP_PAIRLIST(map));
            if (MAP_HASHLIST(map))
                MARK_SERIES_ONLY(MAP_HASHLIST(map));
            break;
        }

        case REB_LIBRARY: {
            QUEUE_MARK_ARRAY_DEEP(VAL_LIBRARY(val));
            REBCTX *meta = VAL_LIBRARY_META(val);
            if (meta != NULL)
                QUEUE_MARK_CONTEXT_DEEP(meta);
            break; }

        case REB_STRUCT:
            {
            // The struct gets its GC'able identity and is passable by one
            // pointer from the fact that it is a single-element array that
            // contains the REBVAL of the struct itself.  (Because it is
            // "singular" it is only a REBSER node--no data allocation.)
            //
            QUEUE_MARK_ARRAY_DEEP(VAL_STRUCT(val));

            // Though the REBVAL payload carries the data series and offset
            // position of this struct into that data, the hierarchical
            // description of the structure's fields is stored in another
            // single element series--the "schema"--which is held in the
            // miscellaneous slot of the main array.
            //
            MARK_SERIES_ONLY(ARR_SERIES(VAL_STRUCT(val))->link.schema);

            // The data series needs to be marked.  It needs to be marked
            // even for structs that aren't at the 0 offset--because their
            // lifetime can be longer than the struct which they represent
            // a "slice" out of.
            //
            MARK_SERIES_ONLY(VAL_STRUCT_DATA_BIN(val));

            // The symbol needs to be GC protected, but only fields have them

            assert(VAL_STRUCT_SCHEMA(val)->name == NULL);

            // These series are backing stores for the `ffi_type` data that
            // is needed to use the struct with the FFI api.
            //
            MARK_SERIES_ONLY(VAL_STRUCT_SCHEMA(val)->fftype);
            MARK_SERIES_ONLY(VAL_STRUCT_SCHEMA(val)->fields_fftype_ptrs);

            // Recursively mark the schema and any nested structures (or
            // REBVAL-typed fields, specially recognized by the interface)
            //
            Queue_Mark_Field_Deep(
                VAL_STRUCT_SCHEMA(val),
                VAL_STRUCT_DATA_BIN(val),
                VAL_STRUCT_OFFSET(val)
            );
            }
            break;

        case REB_GOB:
            Queue_Mark_Gob_Deep(VAL_GOB(val));
            break;

        case REB_EVENT:
            Queue_Mark_Event_Deep(val);
            break;

        default:
            panic (Error_Invalid_Datatype(VAL_TYPE(val)));
    }
}


//
//  Mark_Array_Deep_Core: C
// 
// Mark all series reachable from the array.
//
// !!! At one time there was a notion of a "bare series" which would be marked
// to escape needing to be checked for GC--for instance because it only
// contained symbol words.  However skipping over the values is a limited
// optimization.  (For instance: symbols may become GC'd, and need to see the
// symbol references inside the values...or typesets might be expanded to
// contain dynamically allocated arrays of user types).
//
// !!! A more global optimization would be if there was a flag that was
// maintained about whether there might be any GC'able values in an array.
// It could start out saying there may be...but then if it did a visit and
// didn't see any mark it as not needing GC.  Modifications dirty that bit.
//
static void Mark_Array_Deep_Core(REBARR *array)
{
    REBCNT len;
    RELVAL *value;

#if !defined(NDEBUG)
    //
    // We should have marked this series at queueing time to keep it from
    // being doubly added before the queue had a chance to be processed
    //
    if (!IS_REBSER_MARKED(ARR_SERIES(array))) Panic_Array(array);

    // Make sure that a context's varlist wasn't marked without also marking
    // its keylist.  This could happen if QUEUE_MARK_ARRAY is used on a
    // context instead of QUEUE_MARK_CONTEXT.
    //
    if (GET_ARR_FLAG(array, ARRAY_FLAG_CONTEXT_VARLIST))
        assert(IS_REBSER_MARKED(ARR_SERIES(CTX_KEYLIST(AS_CONTEXT(array)))));
#endif

#ifdef HEAVY_CHECKS
    //
    // The GC is a good general hook point that all series which have been
    // managed will go through, so it's a good time to assert properties
    // about the array.
    //
    ASSERT_ARRAY(array);
#else
    //
    // For a lighter check, make sure it's marked as a value-bearing array
    // and that it hasn't been freed.
    //
    assert(GET_ARR_FLAG(array, SERIES_FLAG_ARRAY));
    assert(!SER_FREED(ARR_SERIES(array)));
#endif

    value = ARR_HEAD(array);
    for (; NOT_END(value); value++) {
        if (IS_VOID_OR_SAFE_TRASH(value)) {
            //
            // Voids are illegal in most arrays, but the varlist of a context
            // uses void values to denote that the variable is not set.  Also
            // reified C va_lists as Do_Core() sources can have them.
            //
            assert(
                GET_ARR_FLAG(array, ARRAY_FLAG_CONTEXT_VARLIST)
                || GET_ARR_FLAG(array, ARRAY_FLAG_VOIDS_LEGAL)
            );
        }
        else
            Queue_Mark_Value_Deep(value);
    }
}


//
//  Sweep_Series: C
// 
// Scans all series in all segments that are part of the
// SER_POOL.  If a series had its lifetime management
// delegated to the garbage collector with MANAGE_SERIES(),
// then if it didn't get "marked" as live during the marking
// phase then free it.
// 
// The current exception is that any GC-managed series that has
// been marked with the SER_KEEP flag will not be freed--unless
// this sweep call is during shutdown.  During shutdown, those
// kept series will be freed as well.
// 
// !!! Review the idea of SER_KEEP, as it is a lot like
// Guard_Series (which was deleted).  Although SER_KEEP offers a
// less inefficient way to flag a series as protected from the
// garbage collector, it can be put on and left for an arbitrary
// amount of time...making it seem contentious with the idea of
// delegating it to the garbage collector in the first place.
//
ATTRIBUTE_NO_SANITIZE_ADDRESS static REBCNT Sweep_Series(REBOOL shutdown)
{
    REBSEG *seg;
    REBCNT count = 0;

    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        REBSER *series = cast(REBSER *, seg + 1);
        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; n--, series++) {
            // See notes on Make_Node() about how the first allocation of a
            // unit zero-fills *most* of it.  But after that it's up to the
            // caller of Free_Node() to zero out whatever bits it uses to
            // indicate "freeness".  We check the zeroness of the `wide`.
            if (SER_FREED(series))
                continue;

            if (IS_SERIES_MANAGED(series)) {
                if (shutdown || !IS_REBSER_MARKED(series)) {
                    GC_Kill_Series(series);
                    count++;
                } else
                    UNMARK_REBSER(series);
            }
            else
                assert(!IS_REBSER_MARKED(series));
        }
    }

    return count;
}


//
//  Sweep_Gobs: C
// 
// Free all unmarked gobs.
// 
// Scans all gobs in all segments that are part of the
// GOB_POOL. Free gobs that have not been marked.
//
ATTRIBUTE_NO_SANITIZE_ADDRESS static REBCNT Sweep_Gobs(void)
{
    REBSEG  *seg;
    REBGOB  *gob;
    REBCNT  n;
    REBCNT  count = 0;

    for (seg = Mem_Pools[GOB_POOL].segs; seg; seg = seg->next) {
        gob = (REBGOB *) (seg + 1);
        for (n = Mem_Pools[GOB_POOL].units; n > 0; n--) {
            if (IS_GOB_USED(gob)) {
                if (IS_GOB_MARK(gob))
                    UNMARK_GOB(gob);
                else {
                    Free_Gob(gob);
                    count++;
                }
            }
            gob++;
        }
    }

    return count;
}


//
//  Sweep_Routines: C
// 
// Free all unmarked routines.
// 
// Scans all routines in all segments that are part of the
// RIN_POOL. Free routines that have not been marked.
//
ATTRIBUTE_NO_SANITIZE_ADDRESS static REBCNT Sweep_Routines(void)
{
    REBCNT count = 0;

    REBSEG *seg;
    for (seg = Mem_Pools[RIN_POOL].segs; seg; seg = seg->next) {
        REBRIN *rin = cast(REBRIN*, seg + 1);
        REBCNT n;
        for (n = Mem_Pools[RIN_POOL].units; n > 0; n--) {
            if (GET_RIN_FLAG(rin, ROUTINE_FLAG_USED)) {
                if (GET_RIN_FLAG(rin, ROUTINE_FLAG_MARK))
                    CLEAR_RIN_FLAG(rin, ROUTINE_FLAG_MARK);
                else {
                    CLEAR_RIN_FLAG(rin, ROUTINE_FLAG_USED);
                    Free_Routine(rin);
                    ++count;
                }
            }
            ++rin;
        }
    }

    return count;
}


//
//  Propagate_All_GC_Marks: C
// 
// The Mark Stack is a series containing series pointers.  They
// have already had their SERIES_FLAG_MARK set to prevent being added
// to the stack multiple times, but the items they can reach
// are not necessarily marked yet.
// 
// Processing continues until all reachable items from the mark
// stack are known to be marked.
//
static void Propagate_All_GC_Marks(void)
{
    assert(!in_mark);

    while (SER_LEN(GC_Mark_Stack) != 0) {
        SET_SERIES_LEN(GC_Mark_Stack, SER_LEN(GC_Mark_Stack) - 1); // still ok

        // Data pointer may change in response to an expansion during
        // Mark_Array_Deep_Core(), so must be refreshed on each loop.
        //
        REBARR *array = *SER_AT(REBARR*, GC_Mark_Stack, SER_LEN(GC_Mark_Stack));

        // Drop the series we are processing off the tail, as we could be
        // queuing more of them (hence increasing the tail).
        //
        *SER_AT(REBARR*, GC_Mark_Stack, SER_LEN(GC_Mark_Stack)) = NULL;

        Mark_Array_Deep_Core(array);
    }
}


//
//  Recycle_Core: C
// 
// Recycle memory no longer needed.
//
REBCNT Recycle_Core(REBOOL shutdown)
{
    REBINT n;
    REBCNT count;

    //Debug_Num("GC", GC_Disabled);

    ASSERT_NO_GC_MARKS_PENDING();

    // If disabled, exit now but set the pending flag.
    if (GC_Disabled || !GC_Active) {
        SET_SIGNAL(SIG_RECYCLE);
        //Print("pending");
        return 0;
    }

    // Some of the call stack frames may have been invoked with a C function
    // call that took a comma-separated list of REBVAL (the way printf works,
    // a variadic va_list).  These call frames have no REBARR series behind
    // them, but still need to be enumerated to protect the values coming up
    // in the later DO/NEXTs.  But enumerating a C va_list can't be undone;
    // the information were be lost if it weren't saved.  We "reify" the
    // va_list into a REBARR before we start the GC (as it makes new series).
    //
    {
        struct Reb_Frame *f = FS_TOP;
        for (; f != NULL; f = f->prior) {
            const REBOOL truncated = TRUE;
            if (f->flags & DO_FLAG_VA_LIST)
                Reify_Va_To_Array_In_Frame(f, truncated); // see function
        }
    }

    if (Reb_Opts->watch_recycle) Debug_Str(cs_cast(BOOT_STR(RS_WATCH, 0)));

    GC_Disabled = 1;

#if !defined(NDEBUG)
    PG_Reb_Stats->Recycle_Counter++;
    PG_Reb_Stats->Recycle_Series = Mem_Pools[SER_POOL].free;

    PG_Reb_Stats->Mark_Count = 0;
#endif

    // WARNING: These terminate existing open blocks. This could
    // be a problem if code is building a new value at the tail,
    // but has not yet updated the TAIL marker.
    VAL_TERM_ARRAY(TASK_BUF_EMIT);
    VAL_TERM_ARRAY(TASK_BUF_COLLECT);

    // The data stack logic is that it is contiguous values that has no
    // REB_ENDs in it except at the series end.  Bumping up against that
    // END signal is how the stack knows when it needs to grow.  But every
    // drop of the stack doesn't clean up the value dropped--because the
    // values are not END markers, they are considered fine as far as the
    // stack is concerned to indicate unused capacity.  However, the GC
    // doesn't want to mark these "marker-only" values live.
    //
    REBVAL *stackval = DS_TOP;
    assert(IS_TRASH_DEBUG(&DS_Movable_Base[0]));
    while (stackval != &DS_Movable_Base[0]) {
        if (NOT(IS_VOID_OR_SAFE_TRASH(stackval)))
            Queue_Mark_Value_Deep(stackval);
        --stackval;
    }
    Propagate_All_GC_Marks();

    // MARKING PHASE: the "root set" from which we determine the liveness
    // (or deadness) of a series.  If we are shutting down, we are freeing
    // *all* of the series that are managed by the garbage collector, so
    // we don't mark anything as live.
    //
    // !!! Should a root set be "frozen" that's not cleaned out until shutdown
    // time?  There used to be a SERIES_FLAG_KEEP, which was removed due
    // to its usages being bad (and having few flags at the time).  It could
    // be reintroduced in a more limited fashion for this purpose.

    if (!shutdown) {
        REBSER **sp;
        REBVAL **vp;

        // Mark symbol series.  These canon words for SYM_XXX are the only ones
        // that are never candidates for GC.  All other symbol series may
        // go away if no words, parameters, object keys, etc. refer to them.
        {
            REBSTR **canon = SER_HEAD(REBSTR*, PG_Symbol_Canons);
            assert(*canon == NULL); // SYM_0 is for all non-builtin words
            ++canon;
            for (; *canon != NULL; ++canon)
                MARK_SERIES_ONLY(*canon);
        }

        // Mark all natives
        {
            REBCNT n;
            for (n = 0; n < NUM_NATIVES; ++n)
                MARK_ARRAY_DEEP(AS_ARRAY(VAL_FUNC(&Natives[n])));
        }

        // Mark series that have been temporarily protected from garbage
        // collection with PUSH_GUARD_SERIES.  We have to check if the
        // series is a context (so the keylist gets marked) or an array (so
        // the values are marked), or if it's just a data series which
        // should just be marked shallow.
        //
        sp = SER_HEAD(REBSER*, GC_Series_Guard);
        for (n = SER_LEN(GC_Series_Guard); n > 0; n--, sp++) {
            if (GET_SER_FLAG(*sp, ARRAY_FLAG_CONTEXT_VARLIST))
                MARK_CONTEXT_DEEP(AS_CONTEXT(*sp));
            else if (Is_Array_Series(*sp))
                MARK_ARRAY_DEEP(AS_ARRAY(*sp));
            else
                MARK_SERIES_ONLY(*sp);
        }

        // Mark value stack (temp-saved values):
        vp = SER_HEAD(REBVAL*, GC_Value_Guard);
        for (n = SER_LEN(GC_Value_Guard); n > 0; n--, vp++) {
            if (NOT_END(*vp) && !IS_VOID_OR_SAFE_TRASH(*vp))
                Queue_Mark_Value_Deep(*vp);
            Propagate_All_GC_Marks();
        }

        // Mark chunk stack (non-movable saved arrays of values)
        {
            struct Reb_Chunk *chunk = TG_Top_Chunk;
            while (chunk) {
                REBVAL *chunk_value = &chunk->values[0];
                while (
                    cast(REBYTE*, chunk_value)
                    < cast(REBYTE*, chunk) + chunk->size.bits
                ) {
                    if (
                        NOT_END(chunk_value)
                        && !IS_VOID_OR_SAFE_TRASH(chunk_value)
                    ) {
                        assert(NOT(
                            GET_VAL_FLAG(chunk_value, VALUE_FLAG_RELATIVE)
                        ));
                        Queue_Mark_Value_Deep(chunk_value);
                    }
                    chunk_value++;
                }
                chunk = chunk->prev;
            }
        }

        // Mark all root series:
        //
        MARK_CONTEXT_DEEP(PG_Root_Context);
        MARK_CONTEXT_DEEP(TG_Task_Context);

        // Mark potential error object from callback!
        if (!IS_VOID_OR_SAFE_TRASH(&Callback_Error)) {
            assert(NOT(GET_VAL_FLAG(&Callback_Error, VALUE_FLAG_RELATIVE)));
            Queue_Mark_Value_Deep(&Callback_Error);
        }
        Propagate_All_GC_Marks();

        // !!! This hook point is an interim measure for letting a host
        // mark REBVALs that it is holding onto which are not contained in
        // series.  It is motivated by Ren/C++, which wraps REBVALs in
        // `ren::Value` class instances, and is able to enumerate the
        // "live" classes (they "die" when the destructor runs).
        //
        if (GC_Mark_Hook) {
            (*GC_Mark_Hook)();
            Propagate_All_GC_Marks();
        }

        // Mark all devices:
        Mark_Devices_Deep();
        Propagate_All_GC_Marks();

        // Mark function call frames:
        Mark_Frame_Stack_Deep();
        Propagate_All_GC_Marks();
    }

    // SWEEPING PHASE

    // this needs to run before Sweep_Series(), because Routine has series
    // with pointers, which can't be simply discarded by Sweep_Series
    count = Sweep_Routines();

    count += Sweep_Series(shutdown);
    count += Sweep_Gobs();

    CHECK_MEMORY(4);

#if !defined(NDEBUG)
    // Compute new stats:
    PG_Reb_Stats->Recycle_Series = Mem_Pools[SER_POOL].free - PG_Reb_Stats->Recycle_Series;
    PG_Reb_Stats->Recycle_Series_Total += PG_Reb_Stats->Recycle_Series;
    PG_Reb_Stats->Recycle_Prior_Eval = Eval_Cycles;
#endif

    // Do not adjust task variables or boot strings in shutdown when they
    // are being freed.
    //
    if (!shutdown) {
        //
        // !!! This code was added by Atronix to deal with frequent garbage
        // collection, but the logic is not correct.  The issue has been
        // raised and is commented out pending a correct solution.
        //
        // https://github.com/zsx/r3/issues/32
        //
        /*if (GC_Ballast <= VAL_INT32(TASK_BALLAST) / 2
            && VAL_INT64(TASK_BALLAST) < MAX_I32) {
            //increasing ballast by half
            VAL_INT64(TASK_BALLAST) /= 2;
            VAL_INT64(TASK_BALLAST) *= 3;
        } else if (GC_Ballast >= VAL_INT64(TASK_BALLAST) * 2) {
            //reduce ballast by half
            VAL_INT64(TASK_BALLAST) /= 2;
        }

        // avoid overflow
        if (
            VAL_INT64(TASK_BALLAST) < 0
            || VAL_INT64(TASK_BALLAST) >= MAX_I32
        ) {
            VAL_INT64(TASK_BALLAST) = MAX_I32;
        }*/

        GC_Ballast = VAL_INT32(TASK_BALLAST);
        GC_Disabled = 0;

        if (Reb_Opts->watch_recycle)
            Debug_Fmt(cs_cast(BOOT_STR(RS_WATCH, 1)), count);
    }

    ASSERT_NO_GC_MARKS_PENDING();

    return count;
}


//
//  Recycle: C
// 
// Recycle memory no longer needed.
//
REBCNT Recycle(void)
{
    // Default to not passing the `shutdown` flag.
    return Recycle_Core(FALSE);
}


//
//  Guard_Series_Core: C
//
void Guard_Series_Core(REBSER *series)
{
    // It would seem there isn't any reason to save a series from being
    // garbage collected if it is already invisible to the garbage
    // collector.  But some kind of "saving" feature which added a
    // non-managed series in as if it were part of the root set would
    // be useful.  That would be for cases where you are building a
    // series up from constituent values but might want to abort and
    // manually free it.  For the moment, we don't have that feature.
    ASSERT_SERIES_MANAGED(series);

    if (SER_FULL(GC_Series_Guard)) Extend_Series(GC_Series_Guard, 8);

    *SER_AT(
        REBSER*,
        GC_Series_Guard,
        SER_LEN(GC_Series_Guard)
    ) = series;

    SET_SERIES_LEN(GC_Series_Guard, SER_LEN(GC_Series_Guard) + 1);
}


//
//  Guard_Value_Core: C
//
void Guard_Value_Core(const RELVAL *value)
{
    // Cheap check; require that the value already contain valid data when
    // the guard call is made (even if GC isn't necessarily going to happen
    // immediately, and value could theoretically become valid before then.)
    //
    assert(
        IS_END(value)
        || IS_VOID_OR_SAFE_TRASH(value)
        || VAL_TYPE(value) < REB_MAX
    );

#ifdef STRESS_CHECK_GUARD_VALUE_POINTER
    //
    // Technically we should never call this routine to guard a value that
    // lives inside of a series.  Not only would we have to guard the
    // containing series, we would also have to lock the series from
    // being able to resize and reallocate the data pointer.  But this is
    // a somewhat expensive check, so it's only feasible to run occasionally.
    //
    ASSERT_NOT_IN_SERIES_DATA(value);
#endif

    if (SER_FULL(GC_Value_Guard)) Extend_Series(GC_Value_Guard, 8);

    *SER_AT(
        const RELVAL*,
        GC_Value_Guard,
        SER_LEN(GC_Value_Guard)
    ) = value;

    SET_SERIES_LEN(GC_Value_Guard, SER_LEN(GC_Value_Guard) + 1);
}


//
//  Init_GC: C
// 
// Initialize garbage collector.
//
void Init_GC(void)
{
    // TRUE when recycle is enabled (set by RECYCLE func)
    //
    GC_Active = FALSE;

    // GC disabled counter for critical sections.  Used liberally in R3-Alpha.
    // But with Ren-C's introduction of the idea that an allocated series is
    // not seen by the GC until such time as it gets the SERIES_FLAG_MANAGED flag
    // set, there are fewer legitimate justifications to disabling the GC.
    //
    GC_Disabled = 0;

    GC_Ballast = MEM_BALLAST;

    // Temporary series protected from GC. Holds series pointers.
    GC_Series_Guard = Make_Series(15, sizeof(REBSER *), MKS_NONE);

    // Temporary values protected from GC. Holds value pointers.
    GC_Value_Guard = Make_Series(15, sizeof(REBVAL *), MKS_NONE);

    // The marking queue used in lieu of recursion to ensure that deeply
    // nested structures don't cause the C stack to overflow.
    GC_Mark_Stack = Make_Series(100, sizeof(REBARR *), MKS_NONE);
    TERM_SEQUENCE(GC_Mark_Stack);
}


//
//  Shutdown_GC: C
//
void Shutdown_GC(void)
{
    Free_Series(GC_Series_Guard);
    Free_Series(GC_Value_Guard);
    Free_Series(GC_Mark_Stack);
}
