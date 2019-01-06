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
// Copyright 2012-2018 Rebol Open Source Contributors
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
// Today's garbage collector is based on a conventional "mark and sweep",
// of REBSER "nodes", which is how it was done in R3-Alpha:
//
//     https://en.wikipedia.org/wiki/Tracing_garbage_collection
//
// A REBVAL's "payload" and "extra" field may or may not contain pointers to
// REBSERs that the GC needs to be aware of.  Some small values like LOGIC!
// or INTEGER! don't, because they can fit the entirety of their data into the
// REBVAL's 4*sizeof(void) cell...though this would change if INTEGER! added
// support for arbitrary-sized-numbers.
//
// Some REBVALs embed REBSER pointers even when the payload would technically
// fit inside their cell.  They do this in order to create a level of
// indirection so that their data can be shared among copies of that REBVAL.
// For instance, HANDLE! does this.
//
// "Deep" marking in R3-Alpha was originally done with recursion, and the
// recursion would stop whenever a mark was hit.  But this meant deeply nested
// structures could quickly wind up overflowing the C stack.  Consider:
//
//     a: copy []
//     loop 200'000 [a: append/only copy [] a]
//     recycle
//
// The simple solution is that when an unmarked array is hit that it is
// marked and put into a queue for processing (instead of recursed on the
// spot).  This queue is then handled as soon as the marking call is exited,
// and the process repeated until no more items are queued.
//
// !!! There is actually not a specific list of roots of the garbage collect,
// so a first pass of all the REBSER nodes must be done to find them.  This is
// because with the redesigned "RL_API" in Ren-C, ordinary REBSER nodes do
// double duty as lifetime-managed containers for REBVALs handed out by the
// API--without requiring a separate series data allocation.  These could be
// in their own "pool", but that would prevent mingling and reuse among REBSER
// nodes used for other purposes.  Review in light of any new garbage collect
// approaches used.
//

#include "sys-core.h"

#include "sys-int-funcs.h"


//
// !!! In R3-Alpha, the core included specialized structures which required
// their own GC participation.  This is because rather than store their
// properties in conventional Rebol types (like an OBJECT!) they wanted to
// compress their data into a tighter bit pattern than that would allow.
//
// Ren-C has attempted to be increasingly miserly about bytes, and also
// added the ability for C extensions to hook the GC for a cleanup callback
// relating to HANDLE! for any non-Rebol types.  Hopefully this will reduce
// the desire to hook the core garbage collector more deeply.  If a tighter
// structure is desired, that can be done with a HANDLE! or BINARY!, so long
// as any Rebol series/arrays/contexts/functions are done with full values.
//
// Events, Devices, and Gobs are slated to be migrated to structures that
// lean less heavily on C structs and raw C pointers, and leverage higher
// level Rebol services.  So ultimately their implementations would not
// require including specialized code in the garbage collector.  For the
// moment, they still need the hook.
//

#include "reb-evtypes.h"
static void Queue_Mark_Event_Deep(const RELVAL *value);

#define IS_GOB_MARK(g) \
    (did ((g)->header.bits & NODE_FLAG_MARKED))
#define MARK_GOB(g) \
    ((g)->header.bits |= NODE_FLAG_MARKED)
#define UNMARK_GOB(g) \
    ((g)->header.bits &= ~NODE_FLAG_MARKED)

static void Queue_Mark_Gob_Deep(REBGOB *gob);
static REBCNT Sweep_Gobs(void);

static void Mark_Devices_Deep(void);


#ifndef NDEBUG
    static bool in_mark = false; // needs to be per-GC thread
#endif

#define ASSERT_NO_GC_MARKS_PENDING() \
    assert(SER_LEN(GC_Mark_Stack) == 0)


// Private routines for dealing with the GC mark bit.  Note that not all
// REBSERs are actually series at the present time, because some are
// "pairings".  Plus the name Mark_Rebser_Only helps drive home that it's
// not actually marking an "any_series" type (like array) deeply.
//
static inline void Mark_Rebser_Only(REBSER *s)
{
  #if !defined(NDEBUG)
    if (IS_FREE_NODE(s))
        panic (s);
    if (NOT_SER_FLAG((s), NODE_FLAG_MANAGED)) {
        printf("Link to non-MANAGED item reached by GC\n");
        panic (s);
    }
    if (GET_SER_INFO((s), SERIES_INFO_INACCESSIBLE))
        assert(not IS_SER_DYNAMIC(s));
  #endif

    s->header.bits |= NODE_FLAG_MARKED; // may be already set
}

static inline void Unmark_Rebser(REBSER *rebser) {
    rebser->header.bits &= ~NODE_FLAG_MARKED;
}


//
//  Queue_Mark_Array_Subclass_Deep: C
//
// Submits the array into the deferred stack to be processed later with
// Propagate_All_GC_Marks().  If it were not queued and just used recursion
// (as R3-Alpha did) then deeply nested arrays could overflow the C stack.
//
// Although there are subclasses of REBARR which have ->link and ->misc
// and other properties that must be marked, the subclass processing is done
// during the propagation.  This is to prevent recursion from within the
// subclass queueing routine itself.  Hence this routine is the workhorse for
// the subclasses, but there are type-checked specializations for clarity
// if you have a REBACT*, REBCTX*, etc.
//
// (Note: The data structure used for this processing is a "stack" and not
// a "queue".  But when you use 'queue' as a verb, it has more leeway than as
// the CS noun, and can just mean "put into a list for later processing".)
//
static void Queue_Mark_Array_Subclass_Deep(REBARR *a)
{
  #if !defined(NDEBUG)
    if (not IS_SER_ARRAY(a))
        panic (a);
  #endif

    if (GET_SER_FLAG(a, NODE_FLAG_MARKED))
        return; // may not be finished marking yet, but has been queued

    Mark_Rebser_Only(cast(REBSER*, a));

    // Add series to the end of the mark stack series.  The length must be
    // maintained accurately to know when the stack needs to grow.
    //
    // !!! Should this use a "bumping a NULL at the end" technique to grow,
    // like the data stack?
    //
    if (SER_FULL(GC_Mark_Stack))
        Extend_Series(GC_Mark_Stack, 8);
    *SER_AT(REBARR*, GC_Mark_Stack, SER_LEN(GC_Mark_Stack)) = a;
    SET_SERIES_LEN(GC_Mark_Stack, SER_LEN(GC_Mark_Stack) + 1); // unterminated
}

inline static void Queue_Mark_Array_Deep(REBARR *a) { // plain array
    assert(NOT_SER_FLAG(a, ARRAY_FLAG_VARLIST));
    assert(NOT_SER_FLAG(a, ARRAY_FLAG_PARAMLIST));
    assert(NOT_SER_FLAG(a, ARRAY_FLAG_PAIRLIST));

    if (GET_SER_FLAG(a, ARRAY_FLAG_FILE_LINE))
        LINK(a).file->header.bits |= NODE_FLAG_MARKED;

    Queue_Mark_Array_Subclass_Deep(a);
}

inline static void Queue_Mark_Context_Deep(REBCTX *c) { // ARRAY_FLAG_VARLIST
    REBARR *varlist = CTX_VARLIST(c);
    assert(
        GET_SER_INFO(varlist, SERIES_INFO_INACCESSIBLE)
        or SERIES_MASK_CONTEXT == (SER(varlist)->header.bits & (
            SERIES_MASK_CONTEXT // these should be set, not the others
                | ARRAY_FLAG_PAIRLIST
                | ARRAY_FLAG_PARAMLIST
                | ARRAY_FLAG_FILE_LINE
        ))
    );

    Queue_Mark_Array_Subclass_Deep(varlist); // see Propagate_All_GC_Marks()
}

inline static void Queue_Mark_Action_Deep(REBACT *a) { // ARRAY_FLAG_PARAMLIST
    REBARR *paramlist = ACT_PARAMLIST(a);
    assert(
        SERIES_MASK_ACTION == (SER(paramlist)->header.bits & (
            SERIES_MASK_ACTION // these should be set, not the others
                | ARRAY_FLAG_PAIRLIST
                | ARRAY_FLAG_VARLIST
                | ARRAY_FLAG_FILE_LINE
        ))
    );

    Queue_Mark_Array_Subclass_Deep(paramlist); // see Propagate_All_GC_Marks()
}

inline static void Queue_Mark_Map_Deep(REBMAP *m) { // ARRAY_FLAG_PAIRLIST
    REBARR *pairlist = MAP_PAIRLIST(m);
    assert(
        ARRAY_FLAG_PAIRLIST == (SER(pairlist)->header.bits & (
            ARRAY_FLAG_VARLIST | ARRAY_FLAG_PAIRLIST | ARRAY_FLAG_PARAMLIST
            | ARRAY_FLAG_FILE_LINE
        ))
    );

    Queue_Mark_Array_Subclass_Deep(pairlist); // see Propagate_All_GC_Marks()
}

inline static void Queue_Mark_Binding_Deep(const RELVAL *v) {
    REBNOD *binding = VAL_BINDING(v);
    if (not binding)
        return;

  #if !defined(NDEBUG)
    if (binding->header.bits & ARRAY_FLAG_PARAMLIST) {
        //
        // It's an action, any reasonable added check?
    }
    else if (binding->header.bits & ARRAY_FLAG_VARLIST) {
        //
        // It's a context, any reasonable added check?
    }
    else {
        assert(KIND_BYTE(v) % REB_64 == REB_VARARGS);
        assert(IS_SER_ARRAY(binding));
        assert(not IS_SER_DYNAMIC(binding)); // singular
    }
  #endif

    if (binding->header.bits & NODE_FLAG_MANAGED)
        Queue_Mark_Array_Subclass_Deep(ARR(binding));
    else {
        // If a stack cell is holding onto an unmanaged stack-based pointer,
        // it's assumed the lifetime is taken care of by other means and
        // the GC does not need to be involved.  But only stack cells are
        // allowed to do this.
        //
      #if !defined(NDEBUG)
        if (not ANY_VAL_FLAGS(v, CELL_FLAG_STACK | CELL_FLAG_TRANSIENT))
            panic (v);
      #endif
    }
}

// A singular array, if you know it to be singular, can be marked a little
// faster by avoiding a queue step for the array node or walk.
//
inline static void Queue_Mark_Singular_Array(REBARR *a) {
    assert(
        0 == (SER(a)->header.bits & (
            ARRAY_FLAG_VARLIST | ARRAY_FLAG_PAIRLIST | ARRAY_FLAG_PARAMLIST
            | ARRAY_FLAG_FILE_LINE
        ))
    );

    assert(not IS_SER_DYNAMIC(a));

    // While it would be tempting to just go ahead and try to queue the
    // ARR_SINGLE() value here, that could keep recursing if that value had
    // further singular array values to mark.  It's really no different for
    // an array with one value than with many.
    //
    Queue_Mark_Array_Subclass_Deep(a);
}


static void Queue_Mark_Opt_End_Cell_Deep(const RELVAL *v);

inline static void Queue_Mark_Opt_Value_Deep(const RELVAL *v)
{
    assert(NOT_END(v)); // can be NULLED, just not END
    Queue_Mark_Opt_End_Cell_Deep(v);
}

inline static void Queue_Mark_Value_Deep(const RELVAL *v)
{
    assert(NOT_END(v));
    assert(KIND_BYTE_UNCHECKED(v) != REB_MAX_NULLED); // Unreadable blank ok
    Queue_Mark_Opt_End_Cell_Deep(v);
}


//
//  Queue_Mark_Opt_End_Cell_Deep: C
//
// If a slot is not supposed to allow END, use Queue_Mark_Opt_Value_Deep()
// If a slot allows neither END nor NULLED cells, use Queue_Mark_Value_Deep()
//
static void Queue_Mark_Opt_End_Cell_Deep(const RELVAL *v)
{
    assert(not in_mark);
  #if !defined(NDEBUG)
    in_mark = true;
  #endif

    enum Reb_Kind kind;
    if (KIND_BYTE_UNCHECKED(v) != REB_QUOTED)
        kind = CELL_KIND_UNCHECKED(cast(const REBCEL*, v)); // mod 64 of byte
    else {
        assert(v->payload.quoted.depth > 3);

        RELVAL *cell = v->payload.quoted.cell;
      #if !defined(NDEBUG)
        if (Is_Bindable(cell))
            assert(v->extra.binding == cell->extra.binding);
        else
            assert(not v->extra.binding);
      #endif

        Mark_Rebser_Only(SER(Singular_From_Cell(cell)));

        assert(KIND_BYTE(cell) <= REB_MAX_NULLED); // REB_QUOTED check below
        kind = cast(enum Reb_Kind, KIND_BYTE(cell));
        v = cell;
    }

    // This switch is done via contiguous REB_XXX values, in order to
    // facilitate use of a "jump table optimization":
    //
    // http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables
    //
    switch (kind) {
      case REB_0_END:
        break; // use Queue_Mark_Opt_Value_Deep() if END would be a bug

      case REB_ACTION: {
        REBACT *a = VAL_ACTION(v);
        Queue_Mark_Action_Deep(a);
        Queue_Mark_Binding_Deep(v);

      #if !defined(NDEBUG)
        //
        // Make sure the [0] slot of the paramlist holds an archetype that is
        // consistent with the paramlist itself.
        //
        REBVAL *archetype = ACT_ARCHETYPE(a);
        assert(ACT_PARAMLIST(a) == VAL_ACT_PARAMLIST(archetype));
        assert(ACT_DETAILS(a) == VAL_ACT_DETAILS(archetype));
      #endif
        break; }

      case REB_WORD:
      case REB_SET_WORD:
      case REB_GET_WORD:
      case REB_ISSUE: {
        REBSTR *spelling = v->payload.any_word.spelling;

        // A word marks the specific spelling it uses, but not the canon
        // value.  That's because if the canon value gets GC'd, then
        // another value might become the new canon during that sweep.
        //
        Mark_Rebser_Only(spelling);

        // A GC cannot run during a binding process--which is the only
        // time a canon word's "index" field is allowed to be nonzero.
        //
        assert(
            NOT_SER_INFO(spelling, STRING_INFO_CANON)
            or (
                MISC(spelling).bind_index.high == 0
                and MISC(spelling).bind_index.low == 0
            )
        );

        Queue_Mark_Binding_Deep(v);

    #if !defined(NDEBUG)
        if (IS_WORD_BOUND(v)) {
            assert(v->payload.any_word.index != 0);
        }
        else {
            // The word is unbound...make sure index is 0 in debug build.
            // (it can be left uninitialized in release builds, for now)
            //
            assert(v->payload.any_word.index == 0);
        }
    #endif
        break; }

      case REB_QUOTED:
        //
        // REB_QUOTED should not be contained in a quoted; instead, the
        // depth of the existing literal should just have been incremented.
        //
        panic ("REB_QUOTED with (KIND_BYTE() % REB_64) > 0");

      case REB_PATH:
      case REB_SET_PATH:
      case REB_GET_PATH: {
        REBARR *a = ARR(v->payload.any_series.series);
        assert(NOT_SER_INFO(a, SERIES_INFO_INACCESSIBLE));
        Queue_Mark_Binding_Deep(v);

        // With most arrays we may risk direct recursion, hence we have to
        // use Queue_Mark_Array_Deep().  But paths are guaranteed to not have
        // other paths directly in them.  Walk it here so that we can also
        // check that there are no paths embedded.
        //
        // Note: This doesn't catch cases which don't wind up reachable from
        // the root set, e.g. anything that would be GC'd.

      #if !defined(NDEBUG)
        in_mark = false;
      #endif

        assert(ARR_LEN(a) >= 2);
        RELVAL *item = ARR_HEAD(a);
        for (; NOT_END(item); ++item) {
            assert(not ANY_PATH_KIND(KIND_BYTE_UNCHECKED(item)));
            Queue_Mark_Value_Deep(item);
        }
        Mark_Rebser_Only(SER(a));
      
      #if !defined(NDEBUG)
        in_mark = true;
      #endif
        break; }

      case REB_BLOCK:
      case REB_GROUP: {
        REBARR *a = ARR(v->payload.any_series.series);
        if (GET_SER_INFO(a, SERIES_INFO_INACCESSIBLE)) {
            //
            // !!! Review: preserving the identity of inaccessible array nodes
            // is likely uninteresting--the only reason the node wasn't freed
            // in the first place was so this code wouldn't crash trying to
            // mark it.  So this should probably be used as an opportunity to
            // update the pointer in the cell to some global inaccessible
            // REBARR, and *not* mark the dead node at all.
            //
            Mark_Rebser_Only(SER(a));
            Queue_Mark_Binding_Deep(v); // !!! Review this too, is it needed?
        }
        else {
            Queue_Mark_Array_Deep(a);
            Queue_Mark_Binding_Deep(v);
        }
        break; }

      case REB_BINARY:
      case REB_TEXT:
      case REB_FILE:
      case REB_EMAIL:
      case REB_URL:
      case REB_TAG:
      case REB_BITSET: {
        REBSER *s = v->payload.any_series.series;

        assert(SER_WIDE(s) <= sizeof(REBUNI));
        assert(not v->extra.binding); // for future use

        if (GET_SER_INFO(s, SERIES_INFO_INACCESSIBLE)) {
            //
            // !!! See notes above on REB_BLOCK/etc. RE: letting series die.
            //
            Mark_Rebser_Only(s);
        }
        else
            Mark_Rebser_Only(s);
        break; }

      case REB_HANDLE: { // See %sys-handle.h
        REBARR *singular = v->extra.singular;
        if (singular == NULL) {
            //
            // This HANDLE! was created with Init_Handle_Simple.  There is
            // no GC interaction.
        }
        else {
            // Handle was created with Init_Handle_Managed.  It holds a
            // REBSER node that contains exactly one handle, and the actual
            // data for the handle lives in that shared location.  There is
            // nothing the GC needs to see inside a handle.
            //
            SER(singular)->header.bits |= NODE_FLAG_MARKED;

        #if !defined(NDEBUG)
            assert(ARR_LEN(singular) == 1);
            RELVAL *single = ARR_SINGLE(singular);
            assert(IS_HANDLE(single));
            assert(single->extra.singular == v->extra.singular);
            if (v != single) {
                //
                // In order to make it clearer that individual handles do not
                // hold the shared data (there'd be no way to update all the
                // references at once), the data pointers in all but the
                // shared singular value are NULL.
                //
                if (GET_VAL_FLAG(v, HANDLE_FLAG_CFUNC))
                    assert(
                        IS_CFUNC_TRASH_DEBUG(v->payload.handle.data.cfunc)
                    );
                else
                    assert(
                        IS_POINTER_TRASH_DEBUG(v->payload.handle.data.pointer)
                    );
            }
        #endif
        }
        break; }

      case REB_IMAGE:
        Mark_Rebser_Only(VAL_SERIES(v));
        break;

      case REB_VECTOR:
        Mark_Rebser_Only(VAL_SERIES(v));
        break;

      case REB_LOGIC:
      case REB_INTEGER:
      case REB_DECIMAL:
      case REB_PERCENT:
      case REB_MONEY:
      case REB_CHAR:
        break;

      case REB_PAIR: {
        //
        // Ren-C's PAIR! uses a special kind of REBSER that does no additional
        // memory allocation, but embeds two REBVALs in the REBSER itself.
        // A REBVAL has a uintptr_t header at the beginning of its struct,
        // just like a REBSER, and the NODE_FLAG_MARKED bit is a 0
        // if unmarked...so it can stealthily participate in the marking
        // process, as long as the bit is cleared at the end.
        //
        REBSER *pairing = cast(REBSER*, v->payload.pair);
        pairing->header.bits |= NODE_FLAG_MARKED; // read via REBSER
        break; }

      case REB_TUPLE:
      case REB_TIME:
      case REB_DATE:
        break;

      case REB_MAP: {
        REBMAP* map = VAL_MAP(v);
        Queue_Mark_Map_Deep(map);
        break; }

      case REB_DATATYPE:
        // Type spec is allowed to be NULL.  See %typespec.r file
        if (VAL_TYPE_SPEC(v))
            Queue_Mark_Array_Deep(VAL_TYPE_SPEC(v));
        break;

      case REB_TYPESET:
        //
        // Not all typesets have symbols--only those that serve as the
        // keys of objects (or parameters of functions)
        //
        if (v->extra.key_spelling != NULL)
            Mark_Rebser_Only(v->extra.key_spelling);
        break;

      case REB_VARARGS: {
        if (v->payload.varargs.phase) // null if came from MAKE VARARGS!
            Queue_Mark_Action_Deep(v->payload.varargs.phase);

        Queue_Mark_Binding_Deep(v);
        break; }

      case REB_OBJECT:
      case REB_FRAME:
      case REB_MODULE:
      case REB_ERROR:
      case REB_PORT: { // Note: VAL_CONTEXT() fails on SER_INFO_INACCESSIBLE
        REBCTX *context = CTX(v->payload.any_context.varlist);
        Queue_Mark_Context_Deep(context);

        // Currently the "binding" in a context is only used by FRAME! to
        // preserve the binding of the ACTION! value that spawned that
        // frame.  Currently that binding is typically NULL inside of a
        // function's REBVAL unless it is a definitional RETURN or LEAVE.
        //
        // !!! Expanded usages may be found in other situations that mix an
        // archetype with an instance (e.g. an archetypal function body that
        // could apply to any OBJECT!, but the binding cheaply makes it
        // a method for that object.)
        //
        Queue_Mark_Binding_Deep(v);

      #if !defined(NDEBUG)
        if (v->extra.binding != UNBOUND) {
            assert(CTX_TYPE(context) == REB_FRAME);

            if (GET_SER_INFO(context, SERIES_INFO_INACCESSIBLE)) {
                //
                // !!! It seems a bit wasteful to keep alive the binding of a
                // stack frame you can no longer get values out of.  But
                // However, FUNCTION-OF still works on a FRAME! value after
                // the function is finished, if the FRAME! value was kept.
                // And that needs to give back a correct binding.
                //
            }
            else {
                struct Reb_Frame *f = CTX_FRAME_IF_ON_STACK(context);
                if (f) // comes from execution, not MAKE FRAME!
                    assert(VAL_BINDING(v) == FRM_BINDING(f));
            }
        }
      #endif

        REBACT *phase = v->payload.any_context.phase;
        if (phase) {
            assert(VAL_TYPE(v) == REB_FRAME); // may be heap-based frame
            Queue_Mark_Action_Deep(phase);
        }
        else
            assert(VAL_TYPE(v) != REB_FRAME); // phase if-and-only-if frame

        if (GET_SER_INFO(context, SERIES_INFO_INACCESSIBLE))
            break;

      #if !defined(NDEBUG)
        REBVAL *archetype = CTX_ARCHETYPE(context);
        assert(CTX_TYPE(context) == kind);
        assert(VAL_CONTEXT(archetype) == context);
      #endif

        // Note: for VAL_CONTEXT_FRAME, the FRM_CALL is either on the stack
        // (in which case it's already taken care of for marking) or it
        // has gone bad, in which case it should be ignored.

        break; }

      case REB_GOB:
        Queue_Mark_Gob_Deep(VAL_GOB(v));
        break;

      case REB_EVENT:
        Queue_Mark_Event_Deep(v);
        break;

      case REB_STRUCT: {
        //
        // !!! The ultimate goal for STRUCT! is that it be part of the FFI
        // extension and fall into the category of a "user defined type".
        // This essentially means it would be an opaque variant of a context.
        // User-defined types aren't fully designed, so struct is achieved
        // through a hacky set of hooks for now...but it does use arrays in
        // a fairly conventional way that should translate to the user
        // defined type system once it exists.
        //
        // The struct gets its GC'able identity and is passable by one
        // pointer from the fact that it is a single-element array that
        // contains the REBVAL of the struct itself.  (Because it is
        // "singular" it is only a REBSER node--no data allocation.)
        //
        REBSTU *stu = v->payload.structure.stu;
        Queue_Mark_Array_Deep(stu);

        // The schema is the hierarchical description of the struct.
        //
        REBFLD *schema = LINK(stu).schema;
        Queue_Mark_Array_Deep(schema);

        // The data series needs to be marked.  It needs to be marked
        // even for structs that aren't at the 0 offset--because their
        // lifetime can be longer than the struct which they represent
        // a "slice" out of.
        //
        // Note this may be a singular array handle, or it could be a BINARY!
        //
        if (IS_SER_ARRAY(v->payload.structure.data))
            Queue_Mark_Singular_Array(ARR(v->payload.structure.data));
        else
            Mark_Rebser_Only(v->payload.structure.data);
        break; }

      case REB_LIBRARY: {
        Queue_Mark_Array_Deep(VAL_LIBRARY(v));
        REBCTX *meta = VAL_LIBRARY_META(v);
        if (meta != NULL)
            Queue_Mark_Context_Deep(meta);
        break; }

      case REB_BLANK:
      case REB_BAR:
      case REB_VOID:
        break;

      case REB_MAX_NULLED:
        break; // use Queue_Mark_Value_Deep() if NULLED would be a bug

      default:
        panic (v);
    }

  #if !defined(NDEBUG)
    in_mark = false;
  #endif
}


//
//  Propagate_All_GC_Marks: C
//
// The Mark Stack is a series containing series pointers.  They have already
// had their SERIES_FLAG_MARK set to prevent being added to the stack multiple
// times, but the items they can reach are not necessarily marked yet.
//
// Processing continues until all reachable items from the mark stack are
// known to be marked.
//
static void Propagate_All_GC_Marks(void)
{
    assert(not in_mark);

    while (SER_LEN(GC_Mark_Stack) != 0) {
        SET_SERIES_LEN(GC_Mark_Stack, SER_LEN(GC_Mark_Stack) - 1); // still ok

        // Data pointer may change in response to an expansion during
        // Mark_Array_Deep_Core(), so must be refreshed on each loop.
        //
        REBARR *a = *SER_AT(REBARR*, GC_Mark_Stack, SER_LEN(GC_Mark_Stack));

        // Termination is not required in the release build (the length is
        // enough to know where it ends).  But overwrite with trash in debug.
        //
        TRASH_POINTER_IF_DEBUG(
            *SER_AT(REBARR*, GC_Mark_Stack, SER_LEN(GC_Mark_Stack))
        );

        // We should have marked this series at queueing time to keep it from
        // being doubly added before the queue had a chance to be processed
         //
        assert(SER(a)->header.bits & NODE_FLAG_MARKED);

    #ifdef HEAVY_CHECKS
        //
        // The GC is a good general hook point that all series which have been
        // managed will go through, so it's a good time to assert properties
        // about the array.
        //
        ASSERT_ARRAY(a);
    #else
        //
        // For a lighter check, make sure it's marked as a value-bearing array
        // and that it hasn't been freed.
        //
        assert(IS_SER_ARRAY(a));
        assert(not IS_FREE_NODE(SER(a)));
    #endif

        RELVAL *v;

        if (GET_SER_FLAG(a, ARRAY_FLAG_PARAMLIST)) {
            v = ARR_HEAD(a); // archetype
            assert(IS_ACTION(v));
            assert(not v->extra.binding); // archetypes have no binding

            // These queueings cannot be done in Queue_Mark_Function_Deep
            // because of the potential for overflowing the C stack with calls
            // to Queue_Mark_Function_Deep.

            REBARR *details = v->payload.action.details;
            Queue_Mark_Array_Deep(details);

            REBACT *underlying = LINK(a).underlying;
            Queue_Mark_Action_Deep(underlying);

            REBARR *specialty = LINK(details).specialty;
            if (GET_SER_FLAG(specialty, ARRAY_FLAG_VARLIST))
                Queue_Mark_Context_Deep(CTX(specialty));
            else
                assert(specialty == a);

            REBCTX *meta = MISC(a).meta;
            if (meta)
                Queue_Mark_Context_Deep(meta);

            // Functions can't currently be freed by FREE...
            //
            assert(NOT_SER_INFO(a, SERIES_INFO_INACCESSIBLE));

            ++v; // function archetype completely marked by this process
        }
        else if (GET_SER_FLAG(a, ARRAY_FLAG_VARLIST)) {
            v = CTX_ARCHETYPE(CTX(a)); // works if SERIES_INFO_INACCESSIBLE

            // Currently only FRAME! uses binding
            //
            assert(ANY_CONTEXT(v));
            assert(not v->extra.binding or VAL_TYPE(v) == REB_FRAME);

            // These queueings cannot be done in Queue_Mark_Context_Deep
            // because of the potential for overflowing the C stack with calls
            // to Queue_Mark_Context_Deep.

            REBNOD *keysource = LINK(a).keysource;
            if (keysource->header.bits & NODE_FLAG_CELL) {
                //
                // Must be a FRAME! and it must be on the stack running.  If
                // it has stopped running, then the keylist must be set to
                // UNBOUND which would not be a cell.
                //
                // There's nothing to mark for GC since the frame is on the
                // stack, which should preserve the function paramlist.
                //
                assert(IS_FRAME(v));
            }
            else {
                REBARR *keylist = ARR(keysource);
                if (IS_FRAME(v)) {
                    assert(GET_SER_FLAG(keylist, ARRAY_FLAG_PARAMLIST));

                    // Frames use paramlists as their "keylist", there is no
                    // place to put an ancestor link.
                }
                else {
                    assert(NOT_SER_FLAG(keylist, ARRAY_FLAG_PARAMLIST));
                    ASSERT_UNREADABLE_IF_DEBUG(ARR_HEAD(keylist));

                    REBARR *ancestor = LINK(keylist).ancestor;
                    Queue_Mark_Array_Subclass_Deep(ancestor); // maybe keylist
                }
                Queue_Mark_Array_Subclass_Deep(keylist);
            }

            REBCTX *meta = MISC(a).meta;
            if (meta != NULL)
                Queue_Mark_Context_Deep(meta);

            // Stack-based frames will be inaccessible if they are no longer
            // running, so there's no data to mark...
            //
            if (GET_SER_INFO(a, SERIES_INFO_INACCESSIBLE))
                continue;

            ++v; // context archetype completely marked by this process
        }
        else if (GET_SER_FLAG(a, ARRAY_FLAG_PAIRLIST)) {
            //
            // There was once a "small map" optimization that wouldn't
            // produce a hashlist for small maps and just did linear search.
            // @giuliolunati deleted that for the time being because it
            // seemed to be a source of bugs, but it may be added again...in
            // which case the hashlist may be NULL.
            //
            REBSER *hashlist = LINK(a).hashlist;
            assert(hashlist != NULL);

            Mark_Rebser_Only(hashlist);

            // !!! Currently MAP! doesn't work with FREE, but probably should.
            //
            assert(NOT_SER_INFO(a, SERIES_INFO_INACCESSIBLE));

            v = ARR_HEAD(a);
        }
        else {
            // Users can free the data of a plain array with FREE, leaving
            // the array stub.
            //
            // !!! It could be possible to GC all these to a common freed
            // array stub, though that wouldn't permit equality comparisons.
            //
            if (GET_SER_INFO(a, SERIES_INFO_INACCESSIBLE))
                continue;

            v = ARR_HEAD(a);
        }

        for (; NOT_END(v); ++v) {
            Queue_Mark_Opt_Value_Deep(v);
            //
        #if !defined(NDEBUG)
            //
            // Voids are illegal in most arrays, but the varlist of a context
            // uses void values to denote that the variable is not set.  Also
            // reified C va_lists as Eval_Core_Throws() sources can have them.
            //
            if (
                not IS_BLANK_RAW(v)
                and IS_NULLED(v)
                and NOT_SER_FLAG(a, ARRAY_FLAG_VARLIST)
                and NOT_SER_FLAG(a, ARRAY_FLAG_NULLEDS_LEGAL)
            ){
                panic(a);
            }
        #endif
        }
    }
}


//
//  Reify_Any_C_Valist_Frames: C
//
// Some of the call stack frames may have been invoked with a C function call
// that took a comma-separated list of REBVAL (the way printf works, a
// variadic "va_list").
//
// http://en.cppreference.com/w/c/variadic
//
// Although it's a list of REBVAL*, these call frames have no REBARR series
// behind.  Yet they still need to be enumerated to protect the values coming
// up in the later EVALUATEs.  But enumerating a C va_list can't be undone.
// The REBVAL* is lost if it isn't saved, and these frames may be in
// mid-evaluation.
//
// Hence, the garbage collector has to "reify" the remaining portion of the
// va_list into a REBARR before starting the GC.  Then the rest of the
// evaluation happens on that array.
//
static void Reify_Any_C_Valist_Frames(void)
{
    // IMPORTANT: This must be done *before* any of the mark/sweep logic
    // begins, because it creates new arrays.  In the future it may be
    // possible to introduce new series in mid-garbage collection (which would
    // be necessary for an incremental garbage collector), but for now the
    // feature is not supported.
    //
    ASSERT_NO_GC_MARKS_PENDING();

    REBFRM *f = FS_TOP;
    for (; f != FS_BOTTOM; f = f->prior) {
        if (NOT_END(f->value) and FRM_IS_VALIST(f)) {
            const bool truncated = true;
            Reify_Va_To_Array_In_Frame(f, truncated);
        }
    }
}


//
//  Mark_Root_Series: C
//
// Root Series are any manual series that were allocated but have not been
// managed yet, as well as Alloc_Value() nodes that are explicitly "roots".
//
// For root nodes, this checks to see if their lifetime was dependent on a
// FRAME!, and if that frame is no longer on the stack.  If so, it (currently)
// will panic if that frame did not end due to a fail().  This could be
// relaxed to automatically free those nodes as a normal GC.
//
// !!! This implementation walks over *all* the nodes.  It wouldn't have to
// if API nodes were in their own pool, or if the outstanding manuals list
// were maintained even in non-debug builds--it could just walk those.  This
// should be weighed against background GC and other more sophisticated
// methods which might come down the road for the GC than this simple one.
//
static void Mark_Root_Series(void)
{
    REBSEG *seg;
    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        REBSER *s = cast(REBSER *, seg + 1);
        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; --n, ++s) {
            //
            // !!! A smarter switch statement here could do this more
            // optimally...see the sweep code for an example.
            //
            if (IS_FREE_NODE(s))
                continue;

            if (s->header.bits & NODE_FLAG_ROOT) {
                //
                // This came from Alloc_Value(); all references should be
                // from the C stack, only this visit should be marking it.
                //
                assert(not (s->header.bits & NODE_FLAG_MARKED));
                assert(not IS_SER_DYNAMIC(s));
                assert(
                    not LINK(s).owner
                    or LINK(s).owner->header.bits & NODE_FLAG_MANAGED
                );

                if (not (s->header.bits & NODE_FLAG_MANAGED))
                    assert(not LINK(s).owner);
                else if (
                    SER(LINK(s).owner)->info.bits & SERIES_INFO_INACCESSIBLE
                ){
                    if (NOT_SER_INFO(LINK(s).owner, FRAME_INFO_FAILED)) {
                        //
                        // Long term, it is likely that implicit managed-ness
                        // will allow users to leak API handles.  It will
                        // always be more efficient to not do that, so having
                        // the code be strict for now is better.
                        //
                      #if !defined(NDEBUG)
                        printf("handle not rebReleased(), not legal ATM\n");
                      #endif
                        panic (s);
                    }

                    GC_Kill_Series(s);
                    continue;
                }
                else // note that Mark_Frame_Stack_Deep() will mark the owner
                    s->header.bits |= NODE_FLAG_MARKED;

                // Note: Eval_Core_Throws() might target API cells, uses END
                //
                Queue_Mark_Opt_End_Cell_Deep(ARR_SINGLE(ARR(s)));
                continue;
            }

            if (s->header.bits & NODE_FLAG_CELL) { // a pairing
                if (s->header.bits & NODE_FLAG_STACK)
                    assert(!"stack pairings not believed to exist");

                if (s->header.bits & NODE_FLAG_MANAGED)
                    continue; // PAIR! or other value will mark it

                assert(!"unmanaged pairings not believed to exist yet");
                REBVAL *paired = cast(REBVAL*, s);
                Queue_Mark_Opt_Value_Deep(paired);
                Queue_Mark_Opt_Value_Deep(PAIRING_KEY(paired));
            }

            if (IS_SER_ARRAY(s)) {
                if (s->header.bits & (NODE_FLAG_MANAGED | NODE_FLAG_STACK))
                    continue; // BLOCK!, Mark_Frame_Stack_Deep() etc. mark it

                // This means someone did something like Make_Arr() and then
                // ran an evaluation before referencing it somewhere from the
                // root set.

                // Only plain arrays are supported as unmanaged across
                // evaluations, because REBCTX and REBACT and REBMAP are too
                // complex...they must be managed before evaluations happen.
                // Manage and use PUSH_GC_GUARD and DROP_GC_GUARD on them.
                //
                assert(not ANY_SER_FLAGS(
                    s,
                    ARRAY_FLAG_VARLIST
                        | ARRAY_FLAG_PARAMLIST
                        | ARRAY_FLAG_PAIRLIST
                ));

                // Note: Arrays which are using their LINK() or MISC() for
                // other purposes than file and line will not be marked here!
                //
                if (GET_SER_FLAG(s, ARRAY_FLAG_FILE_LINE))
                    LINK(s).file->header.bits |= NODE_FLAG_MARKED;

                RELVAL *item = ARR_HEAD(cast(REBARR*, s));
                for (; NOT_END(item); ++item)
                    Queue_Mark_Value_Deep(item);
            }

            // At present, no handling for unmanaged STRING!, BINARY!, etc.
            // This would have to change, e.g. if any of other types stored
            // something on the heap in their LINK() or MISC()
        }

        Propagate_All_GC_Marks(); // !!! is propagating on each segment good?
    }

}


//
//  Mark_Data_Stack: C
//
// The data stack logic is that it is contiguous values with no END markers
// except at the array end.  Bumping up against that END signal is how the
// stack knows when it needs to grow.
//
// But every drop of the stack doesn't overwrite the dropped value.  Since the
// values are not END markers, they are considered fine as far as a NOT_END()
// test is concerned to indicate unused capacity.  So the values are good
// for the testing purpose, yet the GC doesn't want to consider those to be
// "live" references.  So rather than to a full Queue_Mark_Array_Deep() on
// the capacity of the data stack's underlying array, it begins at DS_TOP.
//
static void Mark_Data_Stack(void)
{
    REBVAL *head = KNOWN(ARR_HEAD(DS_Array));
    ASSERT_UNREADABLE_IF_DEBUG(head);

    REBVAL *stackval = DS_TOP;
    for (; stackval != head; --stackval)
        Queue_Mark_Value_Deep(stackval);

    Propagate_All_GC_Marks();
}


//
//  Mark_Symbol_Series: C
//
// Mark symbol series.  These canon words for SYM_XXX are the only ones that
// are never candidates for GC (until shutdown).  All other symbol series may
// go away if no words, parameters, object keys, etc. refer to them.
//
static void Mark_Symbol_Series(void)
{
    REBSTR **canon = SER_HEAD(REBSTR*, PG_Symbol_Canons);
    assert(IS_POINTER_TRASH_DEBUG(*canon)); // SYM_0 is for all non-builtin words
    ++canon;
    for (; *canon != NULL; ++canon)
        Mark_Rebser_Only(*canon);

    ASSERT_NO_GC_MARKS_PENDING(); // doesn't ues any queueing
}


//
//  Mark_Natives: C
//
// For each native C implemenation, a REBVAL is created during init to
// represent it as an ACTION!.  These are kept in a global array and are
// protected from GC.  It might not technically be necessary to do so for
// all natives, but at least some have their paramlists referenced by the
// core code (such as RETURN).
//
static void Mark_Natives(void)
{
    REBCNT n;
    for (n = 0; n < Num_Natives; ++n)
        Queue_Mark_Value_Deep(&Natives[n]);

    Propagate_All_GC_Marks();
}


//
//  Mark_Guarded_Nodes: C
//
// Mark series and values that have been temporarily protected from garbage
// collection with PUSH_GC_GUARD.  Subclasses e.g. ARRAY_FLAG_CONTEXT will
// have their LINK() and MISC() fields guarded appropriately for the class.
//
static void Mark_Guarded_Nodes(void)
{
    REBNOD **np = SER_HEAD(REBNOD*, GC_Guarded);
    REBCNT n = SER_LEN(GC_Guarded);
    for (; n > 0; --n, ++np) {
        REBNOD *node = *np;
        if (node->header.bits & NODE_FLAG_CELL) {
            //
            // !!! What if someone tried to GC_GUARD a managed paired REBSER?
            //
            Queue_Mark_Opt_End_Cell_Deep(cast(REBVAL*, node));
        }
        else { // a series
            assert(node->header.bits & NODE_FLAG_MANAGED);
            REBSER *s = cast(REBSER*, node);
            if (IS_SER_ARRAY(s))
                Queue_Mark_Array_Subclass_Deep(ARR(s));
            else
                Mark_Rebser_Only(s);
        }
        Propagate_All_GC_Marks();
    }
}


//
//  Mark_Frame_Stack_Deep: C
//
// Mark values being kept live by all call frames.  If a function is running,
// then this will keep the function itself live, as well as the arguments.
// There is also an "out" slot--which may point to an arbitrary REBVAL cell
// on the C stack.  The out slot is initialized to an END marker at the
// start of every function call, so that it won't be uninitialized bits
// which would crash the GC...but it must be turned into a value (or a void)
// by the time the function is finished running.
//
// Since function argument slots are not pre-initialized, how far the function
// has gotten in its fulfillment must be taken into account.  Only those
// argument slots through points of fulfillment may be GC protected.
//
// This should be called at the top level, and not from inside a
// Propagate_All_GC_Marks().  All marks will be propagated.
//
static void Mark_Frame_Stack_Deep(void)
{
    REBFRM *f = FS_TOP;

    while (true) { // mark all frames (even FS_BOTTOM)
        //
        // Should have taken care of reifying all the VALIST on the stack
        // earlier in the recycle process (don't want to create new arrays
        // once the recycling has started...)
        //
        assert(not f->source->vaptr or IS_POINTER_TRASH_DEBUG(f->source->vaptr));

        // Note: f->source->pending should either live in f->source->array, or
        // it may be trash (e.g. if it's an apply).  GC can ignore it.
        //
        if (f->source->array)
            Queue_Mark_Array_Deep(f->source->array);

        // END is possible, because the frame could be sitting at the end of
        // a block when a function runs, e.g. `do [zero-arity]`.  That frame
        // will stay on the stack while the zero-arity function is running.
        // The array still might be used in an error, so can't GC it.
        //
        Queue_Mark_Opt_End_Cell_Deep(f->value);

        // If f->gotten is set, it usually shouldn't need markeding because
        // it's fetched via f->value and so would be kept alive by it.  Any
        // code that a frame runs that might disrupt that relationship so it
        // would fetch differently should have meant clearing f->gotten.
        //
        // However, the SHOVE operation is special, and puts an enfix ACTION!
        // into the frame's `shove` cell and points f->gotten to that.  It
        // needs to be marked here.
        //
        if (not f->gotten)
            NOOP;
        else if (f->gotten == FRM_SHOVE(f)) {
            assert(GET_VAL_FLAG(FRM_SHOVE(f), VALUE_FLAG_ENFIXED));
            Queue_Mark_Value_Deep(FRM_SHOVE(f));
        }
        else
            assert(
                IS_POINTER_TRASH_DEBUG(f->gotten)
                or f->gotten == Try_Get_Opt_Var(f->value, f->specifier)
            );

        if (
            f->specifier != SPECIFIED
            and (f->specifier->header.bits & NODE_FLAG_MANAGED)
        ){
            Queue_Mark_Context_Deep(CTX(f->specifier));
        }

        Queue_Mark_Opt_End_Cell_Deep(f->out); // END legal, but not nullptr

        // Frame temporary cell should always contain initialized bits, as
        // DECLARE_FRAME sets it up and no one is supposed to trash it.
        //
        Queue_Mark_Opt_End_Cell_Deep(FRM_CELL(f));

        if (not Is_Action_Frame(f)) {
            //
            // Consider something like `eval copy '(recycle)`, because
            // while evaluating the group it has no anchor anywhere in the
            // root set and could be GC'd.  The Reb_Frame's array ref is it.
            //
            goto propagate_and_continue;
        }

        Queue_Mark_Action_Deep(f->original); // never NULL
        if (f->opt_label) // will be null if no symbol
            Mark_Rebser_Only(f->opt_label);

        // refine and special can be used to GC protect an arbitrary value
        // while a function is running, currently.  nullptr is permitted as
        // well for flexibility (e.g. path frames use nullptr to indicate no
        // set value on a path)
        //
        if (f->refine)
            Queue_Mark_Opt_End_Cell_Deep(f->refine);
        if (f->special)
            Queue_Mark_Opt_End_Cell_Deep(f->special);

        if (f->varlist and GET_SER_FLAG(f->varlist, NODE_FLAG_MANAGED)) {
            //
            // If the context is all set up with valid values and managed,
            // then it can just be marked normally...no need to do custom
            // partial parameter traversal.
            //
            assert(IS_END(f->param)); // done walking
            Queue_Mark_Context_Deep(CTX(f->varlist));
            goto propagate_and_continue;
        }

        if (f->varlist and GET_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE)) {
            //
            // This happens in Encloser_Dispatcher(), where it can capture a
            // varlist that may not be managed (e.g. if there were no ADAPTs
            // or other phases running that triggered it).
            //
            goto propagate_and_continue;
        }

        // Mark arguments as used, but only as far as parameter filling has
        // gotten (may be garbage bits past that).  Could also be an END value
        // of an in-progress arg fulfillment, but in that case it is protected
        // by the *evaluating frame's f->out* (!)
        //
        // Refinements need special treatment, and also consideration of if
        // this is the "doing pickups" or not.  If doing pickups then skip the
        // cells for pending refinement arguments.
        //
        REBACT *phase; // goto would cross initialization
        phase = FRM_PHASE_OR_DUMMY(f);
        REBVAL *param;
        if (phase == PG_Dummy_Action)
            param = ACT_PARAMS_HEAD(f->original); // no phases will run
        else
            param = ACT_PARAMS_HEAD(phase);

        REBVAL *arg;
        for (arg = FRM_ARGS_HEAD(f); NOT_END(param); ++param, ++arg) {
            //
            // At time of writing, all frame storage is in stack cells...not
            // varlists.
            //
            assert(arg->header.bits & CELL_FLAG_STACK);

            if (param == f->param) {
                //
                // When param and f->param match, that means that arg is the
                // output slot for some other frame's f->out.  Let that frame
                // do the marking (which tolerates END, an illegal state for
                // prior arg slots we've visited...unless deferred!)

                // If we're not doing "pickups" then the cell slots after
                // this one have not been initialized, not even to trash.
                //
                if (not (f->flags.bits & DO_FLAG_DOING_PICKUPS))
                    break;

                // But since we *are* doing pickups, we must have initialized
                // all the cells to something...even to trash.  Continue and
                // mark them.
                //
                continue;
            }

            // Filling in a deferred argument may mean Eval_Core_Throws() has
            // to put END markers into a cell that's behind the current param,
            // so that's a case where an END might be seen.
            //
            assert(NOT_END(arg) or arg == f->u.defer.arg);
            Queue_Mark_Opt_End_Cell_Deep(arg);
        }

      propagate_and_continue:;

        Propagate_All_GC_Marks();
        if (f == FS_BOTTOM)
            break;

        f = f->prior;
    }
}


//
//  Sweep_Series: C
//
// Scans all series nodes (REBSER structs) in all segments that are part of
// the SER_POOL.  If a series had its lifetime management delegated to the
// garbage collector with MANAGE_SERIES(), then if it didn't get "marked" as
// live during the marking phase then free it.
//
static REBCNT Sweep_Series(void)
{
    REBCNT count = 0;

    // Optimization here depends on SWITCH of a bank of 4 bits.
    //
    STATIC_ASSERT(NODE_FLAG_MARKED == FLAG_LEFT_BIT(3)); // 0x1 after shift
    STATIC_ASSERT(NODE_FLAG_MANAGED == FLAG_LEFT_BIT(2)); // 0x2 after shift
    STATIC_ASSERT(NODE_FLAG_FREE == FLAG_LEFT_BIT(1)); // 0x4 after shift
    STATIC_ASSERT(NODE_FLAG_NODE == FLAG_LEFT_BIT(0)); // 0x8 after shift

    REBSEG *seg;
    for (seg = Mem_Pools[SER_POOL].segs; seg != NULL; seg = seg->next) {
        REBSER *s = cast(REBSER*, seg + 1);
        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; --n, ++s) {
            switch (FIRST_BYTE(s->header) >> 4) {
            case 0:
            case 1: // 0x1
            case 2: // 0x2
            case 3: // 0x2 + 0x1
            case 4: // 0x4
            case 5: // 0x4 + 0x1
            case 6: // 0x4 + 0x2
            case 7: // 0x4 + 0x2 + 0x1
                //
                // NODE_FLAG_NODE (0x8) is clear.  This signature is
                // reserved for UTF-8 strings (corresponding to valid ASCII
                // values in the first byte).
                //
                panic (s);

            // v-- Everything below here has NODE_FLAG_NODE set (0x8)

            case 8:
                // 0x8: unmanaged and unmarked, e.g. a series that was made
                // with Make_Ser() and hasn't been managed.  It doesn't
                // participate in the GC.  Leave it as is.
                //
                // !!! Are there actually legitimate reasons to do this with
                // arrays, where the creator knows the cells do not need
                // GC protection?  Should finding an array in this state be
                // considered a problem (e.g. the GC ran when you thought it
                // couldn't run yet, hence would be able to free the array?)
                //
                break;

            case 9:
                // 0x8 + 0x1: marked but not managed, this can't happen,
                // because the marking itself asserts nodes are managed.
                //
                panic (s);

            case 10:
                // 0x8 + 0x2: managed but didn't get marked, should be GC'd
                //
                // !!! It would be nice if we could have NODE_FLAG_CELL here
                // as part of the switch, but see its definition for why it
                // is at position 8 from left and not an earlier bit.
                //
                if (s->header.bits & NODE_FLAG_CELL) {
                    assert(not (s->header.bits & NODE_FLAG_ROOT));
                    Free_Node(SER_POOL, s); // Free_Pairing is for manuals
                }
                else
                    GC_Kill_Series(s);
                ++count;
                break;

            case 11:
                // 0x8 + 0x2 + 0x1: managed and marked, so it's still live.
                // Don't GC it, just clear the mark.
                //
                s->header.bits &= ~NODE_FLAG_MARKED;
                break;

            // v-- Everything below this line has the two leftmost bits set
            // in the header.  In the *general* case this could be a valid
            // first byte of a multi-byte sequence in UTF-8...so only the
            // special bit pattern of the free case uses this.

            case 12:
                // 0x8 + 0x4: free node, uses special illegal UTF-8 byte
                //
                assert(FIRST_BYTE(s->header) == FREED_SERIES_BYTE);
                break;

            case 13:
            case 14:
            case 15:
                panic (s); // 0x8 + 0x4 + ... reserved for UTF-8
            }
        }
    }

    // For efficiency of memory use, REBSER is nominally defined as
    // 2*sizeof(REBVAL), and so pairs can use the same nodes.  But features
    // that might make the cells a size greater than REBSER size require
    // doing pairings in a different pool.
    //
  #ifdef UNUSUAL_REBVAL_SIZE
    for (seg = Mem_Pools[PAR_POOL].segs; seg != NULL; seg = seg->next) {
        REBVAL *v = cast(REBVAL*, seg + 1);
        if (v->header.bits & NODE_FLAG_FREE) {
            assert(FIRST_BYTE(v->header) == FREED_SERIES_BYTE);
            continue;
        }

        assert(v->header.bits & NODE_FLAG_CELL);

        if (v->header.bits & NODE_FLAG_MANAGED) {
            assert(not (v->header.bits & NODE_FLAG_ROOT));
            if (v->header.bits & NODE_FLAG_MARKED)
                v->header.bits &= ~NODE_FLAG_MARKED;
            else {
                Free_Node(PAR_POOL, v); // Free_Pairing is for manuals
                ++count;
            }
        }
    }
  #endif

    return count;
}


#if !defined(NDEBUG)

//
//  Fill_Sweeplist: C
//
REBCNT Fill_Sweeplist(REBSER *sweeplist)
{
    assert(SER_WIDE(sweeplist) == sizeof(REBNOD*));
    assert(SER_LEN(sweeplist) == 0);

    REBCNT count = 0;

    REBSEG *seg;
    for (seg = Mem_Pools[SER_POOL].segs; seg != NULL; seg = seg->next) {
        REBSER *s = cast(REBSER*, seg + 1);
        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; --n, ++s) {
            switch (FIRST_BYTE(s->header) >> 4) {
            case 9: // 0x8 + 0x1
                assert(IS_SERIES_MANAGED(s));
                if (s->header.bits & NODE_FLAG_MARKED)
                    s->header.bits &= ~NODE_FLAG_MARKED;
                else {
                    EXPAND_SERIES_TAIL(sweeplist, 1);
                    *SER_AT(REBNOD*, sweeplist, count) = NOD(s);
                    ++count;
                }
                break;

            case 11: // 0x8 + 0x2 + 0x1
                //
                // It's a cell which is managed where the value is not an END.
                // This is a managed pairing, so mark bit should be heeded.
                //
                // !!! It is a REBNOD, but *not* a "series".
                //
                assert(IS_SERIES_MANAGED(s));
                if (s->header.bits & NODE_FLAG_MARKED)
                    s->header.bits &= ~NODE_FLAG_MARKED;
                else {
                    EXPAND_SERIES_TAIL(sweeplist, 1);
                    *SER_AT(REBNOD*, sweeplist, count) = NOD(s);
                    ++count;
                }
                break;
            }
        }
    }

    return count;
}

#endif


//
//  Recycle_Core: C
//
// Recycle memory no longer needed.  If sweeplist is not NULL, then it needs
// to be a series whose width is sizeof(REBSER*), and it will be filled with
// the list of series that *would* be recycled.
//
REBCNT Recycle_Core(bool shutdown, REBSER *sweeplist)
{
    // Ordinarily, it should not be possible to spawn a recycle during a
    // recycle.  But when debug code is added into the recycling code, it
    // could cause a recursion.  Be tolerant of such recursions to make that
    // debugging easier...but make a note that it's not ordinarily legal.
    //
#if !defined(NDEBUG)
    if (GC_Recycling) {
        printf("Recycle re-entry; should only happen in debug scenarios.\n");
        SET_SIGNAL(SIG_RECYCLE);
        return 0;
    }
#endif

    // If disabled by RECYCLE/OFF, exit now but set the pending flag.  (If
    // shutdown, ignore so recycling runs and can be checked for balance.)
    //
    if (not shutdown and GC_Disabled) {
        SET_SIGNAL(SIG_RECYCLE);
        return 0;
    }

  #if !defined(NDEBUG)
    GC_Recycling = true;
  #endif

    ASSERT_NO_GC_MARKS_PENDING();
    Reify_Any_C_Valist_Frames();

  #if !defined(NDEBUG)
    PG_Reb_Stats->Recycle_Counter++;
    PG_Reb_Stats->Recycle_Series = Mem_Pools[SER_POOL].free;
    PG_Reb_Stats->Mark_Count = 0;
  #endif

    // WARNING: This terminates an existing open block.  This could be a
    // problem if code is building a new value at the tail, but has not yet
    // updated the TAIL marker.
    //
    TERM_ARRAY_LEN(BUF_COLLECT, ARR_LEN(BUF_COLLECT));

    // The TG_Reuse list consists of entries which could grow to arbitrary
    // length, and which aren't being tracked anywhere.  Cull them during GC
    // in case the stack at one point got very deep and isn't going to use
    // them again, and the memory needs reclaiming.
    //
    while (TG_Reuse) {
        REBARR *varlist = TG_Reuse;
        TG_Reuse = LINK(TG_Reuse).reuse;
        GC_Kill_Series(SER(varlist)); // no track for Free_Unmanaged_Series()
    }

    // MARKING PHASE: the "root set" from which we determine the liveness
    // (or deadness) of a series.  If we are shutting down, we do not mark
    // several categories of series...but we do need to run the root marking.
    // (In particular because that is when API series whose lifetimes
    // are bound to frames will be freed, if the frame is expired.)
    //
    Mark_Root_Series();

    if (not shutdown) {
        Mark_Natives();
        Mark_Symbol_Series();

        Mark_Data_Stack();

        Mark_Guarded_Nodes();

        Mark_Frame_Stack_Deep();

        Propagate_All_GC_Marks();

        Mark_Devices_Deep();
    }

    // SWEEPING PHASE

    ASSERT_NO_GC_MARKS_PENDING();

    REBCNT count = 0;

    if (sweeplist != NULL) {
    #if defined(NDEBUG)
        panic (sweeplist);
    #else
        count += Fill_Sweeplist(sweeplist);
    #endif
    }
    else
        count += Sweep_Series();

    // !!! The intent is for GOB! to be unified in the REBNOD pattern, the
    // way that the FFI structures were.  So they are not included in the
    // count, in order to help make the numbers returned consistent between
    // when the sweeplist is used and not.
    //
    Sweep_Gobs();

#if !defined(NDEBUG)
    // Compute new stats:
    PG_Reb_Stats->Recycle_Series
        = Mem_Pools[SER_POOL].free - PG_Reb_Stats->Recycle_Series;
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
        /*if (GC_Ballast <= TG_Ballast / 2
            && TG_Task_Ballast < INT32_MAX) {
            //increasing ballast by half
            TG_Ballast /= 2;
            TG_Ballast *= 3;
        } else if (GC_Ballast >= TG_Ballast * 2) {
            //reduce ballast by half
            TG_Ballast /= 2;
        }

        // avoid overflow
        if (
            TG_Ballast < 0
            || TG_Ballast >= INT32_MAX
        ) {
            TG_Ballast = INT32_MAX;
        }*/

        GC_Ballast = TG_Ballast;

        if (Reb_Opts->watch_recycle)
            Debug_Fmt(RM_WATCH_RECYCLE, count);
    }

    ASSERT_NO_GC_MARKS_PENDING();

  #if !defined(NDEBUG)
    GC_Recycling = false;
  #endif

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
    //
    REBCNT n = Recycle_Core(false, NULL);

  #ifdef DOUBLE_RECYCLE_TEST
    //
    // If there are two recycles in a row, then the second should not free
    // any additional series that were not freed by the first.  (It also
    // shouldn't crash.)  This is an expensive check, but helpful to try if
    // it seems a GC left things in a bad state that crashed a later GC.
    //
    REBCNT n2 = Recycle_Core(false, NULL);
    assert(n2 == 0);
  #endif

    return n;
}


//
//  Push_Guard_Node: C
//
void Push_Guard_Node(const REBNOD *node)
{
  #if !defined(NDEBUG)
    if (node->header.bits & NODE_FLAG_CELL) {
        //
        // It is a value.  Cheap check: require that it already contain valid
        // data when the guard call is made (even if GC isn't necessarily
        // going to happen immediately, and value could theoretically become
        // valid before then.)
        //
        const REBVAL* v = cast(const REBVAL*, node);
        assert(CELL_KIND_UNCHECKED(v) <= REB_MAX_NULLED);

      #ifdef STRESS_CHECK_GUARD_VALUE_POINTER
        //
        // Technically we should never call this routine to guard a value
        // that lives inside of a series.  Not only would we have to guard the
        // containing series, we would also have to lock the series from
        // being able to resize and reallocate the data pointer.  But this is
        // a somewhat expensive check, so only feasible to run occasionally.
        //
        REBNOD *containing = Try_Find_Containing_Node_Debug(v);
        if (containing)
            panic (containing);
      #endif
    }
    else {
        // It's a series.  Does not ensure the series being guarded is
        // managed, since it can be interesting to guard the managed
        // *contents* of an unmanaged array.  The calling wrappers ensure
        // managedness or not.
    }
  #endif

    if (SER_FULL(GC_Guarded))
        Extend_Series(GC_Guarded, 8);

    *SER_AT(
        const REBNOD*,
        GC_Guarded,
        SER_LEN(GC_Guarded)
    ) = node;

    SET_SERIES_LEN(GC_Guarded, SER_LEN(GC_Guarded) + 1);
}


//
//  Snapshot_All_Actions: C
//
// This routine can be used to get a list of all the functions in the system
// at a given moment in time.  Be sure to protect this array from GC when
// enumerating if there is any chance the GC might run (e.g. if user code
// is called to process the function list)
//
REBARR *Snapshot_All_Actions(void)
{
    REBDSP dsp_orig = DSP;

    REBSEG *seg;
    for (seg = Mem_Pools[SER_POOL].segs; seg != NULL; seg = seg->next) {
        REBSER *s = cast(REBSER*, seg + 1);
        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; --n, ++s) {
            switch (s->header.bits & 0x7) {
            case 5:
                // A managed REBSER which has no cell mask and is marked as
                // *not* an END.  This is the typical signature of what one
                // would call an "ordinary managed REBSER".  (For the meanings
                // of other bits, see Sweep_Series.)
                //
                assert(IS_SERIES_MANAGED(s));
                if (GET_SER_FLAG(s, ARRAY_FLAG_PARAMLIST)) {
                    REBVAL *v = KNOWN(ARR_HEAD(ARR(s)));
                    assert(IS_ACTION(v));
                    Move_Value(DS_PUSH(), v);
                }
                break;
            }
        }
    }

    return Pop_Stack_Values(dsp_orig);
}


//
//  Startup_GC: C
//
// Initialize garbage collector.
//
void Startup_GC(void)
{
    assert(not GC_Disabled);
    assert(not GC_Recycling);

    GC_Ballast = MEM_BALLAST;

    // Temporary series and values protected from GC. Holds node pointers.
    //
    GC_Guarded = Make_Ser(15, sizeof(REBNOD*));

    // The marking queue used in lieu of recursion to ensure that deeply
    // nested structures don't cause the C stack to overflow.
    //
    GC_Mark_Stack = Make_Ser(100, sizeof(REBARR*));
    TERM_SEQUENCE(GC_Mark_Stack);
}


//
//  Shutdown_GC: C
//
void Shutdown_GC(void)
{
    Free_Unmanaged_Series(GC_Guarded);
    Free_Unmanaged_Series(GC_Mark_Stack);
}


//=////////////////////////////////////////////////////////////////////////=//
//
// DEPRECATED HOOKS INTO THE CORE GARBAGE COLLECTOR
//
//=////////////////////////////////////////////////////////////////////////=//

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
        Mark_Rebser_Only(GOB_PANE(gob));
        pane = GOB_HEAD(gob);
        for (i = 0; i < GOB_LEN(gob); i++, pane++)
            Queue_Mark_Gob_Deep(*pane);
    }

    if (GOB_PARENT(gob)) Queue_Mark_Gob_Deep(GOB_PARENT(gob));

    if (GOB_CONTENT(gob)) {
        if (GOB_TYPE(gob) >= GOBT_IMAGE and GOB_TYPE(gob) <= GOBT_STRING)
            Mark_Rebser_Only(GOB_CONTENT(gob));
        else if (GOB_TYPE(gob) >= GOBT_DRAW and GOB_TYPE(gob) <= GOBT_EFFECT)
            Queue_Mark_Array_Deep(ARR(GOB_CONTENT(gob)));
    }

    if (GOB_DATA(gob)) {
        switch (GOB_DTYPE(gob)) {
        case GOBD_INTEGER:
        case GOBD_NONE:
        default:
            break;
        case GOBD_OBJECT:
            Queue_Mark_Context_Deep(CTX(GOB_DATA(gob)));
            break;
        case GOBD_STRING:
        case GOBD_BINARY:
            Mark_Rebser_Only(GOB_DATA(gob));
            break;
        case GOBD_BLOCK:
            Queue_Mark_Array_Deep(ARR(GOB_DATA(gob)));
        }
    }
}


//
//  Sweep_Gobs: C
//
// Free all unmarked gobs.
//
// Scans all gobs in all segments that are part of the
// GOB_POOL. Free gobs that have not been marked.
//
static REBCNT Sweep_Gobs(void)
{
    REBCNT count = 0;

    REBSEG *seg;
    for (seg = Mem_Pools[GOB_POOL].segs; seg; seg = seg->next) {
        REBGOB *gob = cast(REBGOB*, seg + 1);

        REBCNT n;
        for (n = Mem_Pools[GOB_POOL].units; n > 0; --n, ++gob) {
            if (IS_FREE_NODE(gob)) // unused REBNOD
                continue;

            if (IS_GOB_MARK(gob))
                UNMARK_GOB(gob);
            else {
                Free_Node(GOB_POOL, gob);

                // GC_Ballast is of type REBINT, which might be long
                // and REB_I32_ADD_OF takes (int*)
                // it's illegal to convert form (long*) to (int*) in C++
                int tmp;
                GC_Ballast = REB_I32_ADD_OF(
                    GC_Ballast, Mem_Pools[GOB_POOL].wide, &tmp
                ) ? INT32_MAX : tmp;

                if (GC_Ballast > 0)
                    CLR_SIGNAL(SIG_RECYCLE);

                count++;
            }
        }
    }

    return count;
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
        or IS_EVENT_MODEL(value, EVM_OBJECT)
    ){
        Queue_Mark_Context_Deep(CTX(VAL_EVENT_SER(m_cast(RELVAL*, value))));
    }
    else if (IS_EVENT_MODEL(value, EVM_GUI)) {
        Queue_Mark_Gob_Deep(cast(REBGOB*, VAL_EVENT_SER(m_cast(RELVAL*, value))));
    }

    // FIXME: This test is not in parallel to others.
    if (
        VAL_EVENT_TYPE(value) == EVT_DROP_FILE
        and (VAL_EVENT_FLAGS(value) & EVF_COPIED)
    ){
        assert(false);
        Queue_Mark_Array_Deep(ARR(VAL_EVENT_SER(m_cast(RELVAL*, value))));
    }

    if (IS_EVENT_MODEL(value, EVM_DEVICE)) {
        // In the case of being an EVM_DEVICE event type, the port! will
        // not be in VAL_EVENT_SER of the REBEVT structure.  It is held
        // indirectly by the REBREQ ->req field of the event, which
        // in turn possibly holds a singly linked list of other requests.
        req = VAL_EVENT_REQ(value);

        while (req) {
            // Comment says void* ->port is "link back to REBOL port object"
            if (req->port_ctx)
                Queue_Mark_Context_Deep(CTX(req->port_ctx));
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
    for (d = 0; d != RDI_MAX; d++) {
        REBREQ *req;
        REBDEV *dev = devices[d];
        if (!dev)
            continue;

        for (req = dev->pending; req; req = req->next)
            if (req->port_ctx)
                Queue_Mark_Context_Deep(CTX(req->port_ctx));
    }

    Propagate_All_GC_Marks();
}
