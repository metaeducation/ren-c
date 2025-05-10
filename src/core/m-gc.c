//
//  file: %m-gc.c
//  summary: "main memory garbage collection"
//  section: memory
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// of Stub "nodes", which is how it was done in R3-Alpha:
//
//     https://en.wikipedia.org/wiki/Tracing_garbage_collection
//
// A cell's "payload" and "extra" field may or may not contain pointers to
// stubs that the GC needs to be aware of.  Some small values like LOGIC!
// or INTEGER! don't, because they can fit the entirety of their data into the
// cell's 4*sizeof(void) cell...though this would change if INTEGER! added
// support for arbitrary-sized-numbers.
//
// Some cells embed Stub pointers even when the payload would technically
// fit inside their cell.  They do this in order to create a level of
// indirection so that their data can be shared among copies of that cell.
// For instance, HANDLE! does this.
//
// "Deep" marking in R3-Alpha was originally done with recursion, and the
// recursion would stop whenever a mark was hit.  But this meant deeply nested
// structures could quickly wind up overflowing the C stack.  Consider:
//
//     a: copy []
//     repeat 200'000 [a: append/only copy [] a]
//     recycle
//
// The simple solution is that when an unmarked array is hit that it is
// marked and put into a queue for processing (instead of recursed on the
// spot).  This queue is then handled as soon as the marking call is exited,
// and the process repeated until no more items are queued.
//
// !!! There is actually not a specific list of roots of the garbage collect,
// so a first pass of all the Stub nodes must be done to find them.  This is
// because with the redesigned Librebol in Ren-C, ordinary Stub nodes do
// double duty as lifetime-managed containers for REBVALs handed out by the
// API--without requiring a separate series data allocation.  These could be
// in their own "pool", but that would prevent mingling and reuse among Stub
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

static void Queue_Mark_Event_Deep(const Cell* value);

static void Mark_Devices_Deep(void);


#if RUNTIME_CHECKS
    static bool in_mark = false; // needs to be per-GC thread
#endif

#define ASSERT_NO_GC_MARKS_PENDING() \
    assert(Flex_Len(GC_Mark_Stack) == 0)


// Private routines for dealing with the GC mark bit.  Note that not all
// Stubs are actually series at the present time, because some are
// "pairings".  Plus the name Mark_Stub_Only helps drive home that it's
// not actually marking an "any_series" type (like array) deeply.
//
INLINE void Mark_Stub_Only(Stub* s)
{
  #if RUNTIME_CHECKS
    if (Not_Node_Readable(s))
        crash (s);
    if (Not_Node_Managed(s)) {
        printf("Link to non-MANAGED item reached by GC\n");
        crash (s);
    }
    if (Get_Flex_Info(s, INACCESSIBLE))
        assert(not Is_Flex_Dynamic(s));
  #endif

    s->leader.bits |= NODE_FLAG_MARKED; // may be already set
}


//
//  Queue_Mark_Array_Subclass_Deep: C
//
// Submits the array into the deferred stack to be processed later with
// Propagate_All_GC_Marks().  If it were not queued and just used recursion
// (as R3-Alpha did) then deeply nested arrays could overflow the C stack.
//
// Although there are subclasses of Array which have ->link and ->misc
// and other properties that must be marked, the subclass processing is done
// during the propagation.  This is to prevent recursion from within the
// subclass queueing routine itself.  Hence this routine is the workhorse for
// the subclasses, but there are type-checked specializations for clarity
// if you have a REBACT*, VarList*, etc.
//
// (Note: The data structure used for this processing is a "stack" and not
// a "queue".  But when you use 'queue' as a verb, it has more leeway than as
// the CS noun, and can just mean "put into a list for later processing".)
//
static void Queue_Mark_Array_Subclass_Deep(Array* a)
{
  #if RUNTIME_CHECKS
    if (not Is_Flex_Array(a))
        crash (a);
  #endif

    if (Is_Node_Marked(a))
        return; // may not be finished marking yet, but has been queued

    Mark_Stub_Only(cast(Flex*, a));

    // Add series to the end of the mark stack series.  The length must be
    // maintained accurately to know when the stack needs to grow.
    //
    // !!! Should this use a "bumping a nullptr at the end" technique to grow,
    // like the data stack?
    //
    if (Is_Flex_Full(GC_Mark_Stack))
        Extend_Flex(GC_Mark_Stack, 8);
    *Flex_At(Array*, GC_Mark_Stack, Flex_Len(GC_Mark_Stack)) = a;
    Set_Flex_Len(GC_Mark_Stack, Flex_Len(GC_Mark_Stack) + 1); // unterminated
}

INLINE void Queue_Mark_Array_Deep(Array* a) { // plain array
    assert(Not_Array_Flag(a, IS_VARLIST));
    assert(Not_Array_Flag(a, IS_PARAMLIST));
    assert(Not_Array_Flag(a, IS_PAIRLIST));

    if (Get_Array_Flag(a, HAS_FILE_LINE) and LINK(a).file)
        LINK(a).file->leader.bits |= NODE_FLAG_MARKED;

    Queue_Mark_Array_Subclass_Deep(a);
}

INLINE void Queue_Mark_Context_Deep(VarList* c) { // ARRAY_FLAG_IS_VARLIST
    Array* varlist = Varlist_Array(c);
    assert(
        Get_Flex_Info(varlist, INACCESSIBLE)
        or SERIES_MASK_CONTEXT == (varlist->leader.bits & (
            SERIES_MASK_CONTEXT // these should be set, not the others
                | ARRAY_FLAG_IS_PAIRLIST
                | ARRAY_FLAG_IS_PARAMLIST
                | ARRAY_FLAG_HAS_FILE_LINE
        ))
    );

    Queue_Mark_Array_Subclass_Deep(varlist); // see Propagate_All_GC_Marks()
}

INLINE void Queue_Mark_Action_Deep(REBACT *a) { // ARRAY_FLAG_IS_PARAMLIST
    Array* paramlist = ACT_PARAMLIST(a);
    assert(
        SERIES_MASK_ACTION == (paramlist->leader.bits & (
            SERIES_MASK_ACTION // these should be set, not the others
                | ARRAY_FLAG_IS_PAIRLIST
                | ARRAY_FLAG_IS_VARLIST
                | ARRAY_FLAG_HAS_FILE_LINE
        ))
    );

    Queue_Mark_Array_Subclass_Deep(paramlist); // see Propagate_All_GC_Marks()
}

INLINE void Queue_Mark_Map_Deep(REBMAP *m) { // ARRAY_FLAG_IS_PAIRLIST
    Array* pairlist = MAP_PAIRLIST(m);
    assert(
        ARRAY_FLAG_IS_PAIRLIST == (pairlist->leader.bits & (
            ARRAY_FLAG_IS_VARLIST | ARRAY_FLAG_IS_PAIRLIST | ARRAY_FLAG_IS_PARAMLIST
            | ARRAY_FLAG_HAS_FILE_LINE
        ))
    );

    Queue_Mark_Array_Subclass_Deep(pairlist); // see Propagate_All_GC_Marks()
}

INLINE void Queue_Mark_Binding_Deep(const Cell* v) {
    Stub* binding = VAL_BINDING(v);
    if (not binding)
        return;

  #if RUNTIME_CHECKS
    if (binding->leader.bits & ARRAY_FLAG_IS_PARAMLIST) {
        //
        // It's an action, any reasonable added check?
    }
    else if (binding->leader.bits & ARRAY_FLAG_IS_VARLIST) {
        //
        // It's a context, any reasonable added check?
    }
    else {
        assert(Is_Varargs(v));
        assert(Is_Flex_Array(binding));
        assert(not Is_Flex_Dynamic(binding)); // singular
    }
  #endif

    if (binding->leader.bits & NODE_FLAG_MANAGED)
        Queue_Mark_Array_Subclass_Deep(cast_Array(binding));
}


//
//  Queue_Mark_Opt_End_Cell_Deep: C
//
// If a slot is not supposed to allow END, use Queue_Mark_Opt_Value_Deep()
// If a slot allows neither END nor NULLED cells, use Queue_Mark_Value_Deep()
//
static void Queue_Mark_Opt_End_Cell_Deep(const Cell* v)
{
    if (Is_Cell_Unreadable(v))
        return;

    assert(not in_mark);
  #if RUNTIME_CHECKS
    in_mark = true;
  #endif

    // If this happens, it means somehow Recycle() got called between
    // when an `if (Do_XXX_Throws())` branch was taken and when the throw
    // should have been caught up the stack (before any more calls made).
    //
    assert(Not_Cell_Flag(v, THROW_SIGNAL));

    // This switch is done via contiguous TYPE_XXX values, in order to
    // facilitate use of a "jump table optimization":
    //
    // http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables
    //
    enum Reb_Kind kind = VAL_TYPE_RAW(v); // Note: unreadable BLANK!s are ok
    switch (kind) {
    case TYPE_0_END:
        break; // use Queue_Mark_Opt_Value_Deep() if END would be a bug

    case TYPE_ACTION: {
        REBACT *a = VAL_ACTION(v);
        Queue_Mark_Action_Deep(a);
        Queue_Mark_Binding_Deep(v);

      #if RUNTIME_CHECKS
        //
        // Make sure the [0] slot of the paramlist holds an archetype that is
        // consistent with the paramlist itself.
        //
        Value* archetype = ACT_ARCHETYPE(a);
        assert(ACT_PARAMLIST(a) == VAL_ACT_PARAMLIST(archetype));
        assert(ACT_DETAILS(a) == VAL_ACT_DETAILS(archetype));
      #endif
        break; }

    case TYPE_WORD:
    case TYPE_SET_WORD:
    case TYPE_GET_WORD:
    case TYPE_LIT_WORD:
    case TYPE_REFINEMENT:
    case TYPE_ISSUE: {
        Symbol* symbol = v->payload.any_word.symbol;

        // A word marks the specific spelling it uses, but not the canon
        // value.  That's because if the canon value gets GC'd, then
        // another value might become the new canon during that sweep.
        //
        Mark_Stub_Only(symbol);

        // A GC cannot run during a binding process--which is the only
        // time a canon word's "index" field is allowed to be nonzero.
        //
        assert(
            Not_Flex_Info(symbol, CANON_SYMBOL)
            or MISC(symbol).bind_index.other == 0
        );

        Queue_Mark_Binding_Deep(v);

    #if RUNTIME_CHECKS
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

    case TYPE_PATH:
    case TYPE_SET_PATH:
    case TYPE_GET_PATH:
    case TYPE_LIT_PATH:
    case TYPE_BLOCK:
    case TYPE_GROUP: {
        Flex* s = v->payload.any_series.series;
        if (Get_Flex_Info(s, INACCESSIBLE)) {
            //
            // !!! Review: preserving the identity of inaccessible array nodes
            // is likely uninteresting--the only reason the node wasn't freed
            // in the first place was so this code wouldn't crash trying to
            // mark it.  So this should probably be used as an opportunity to
            // update the pointer in the cell to some global inaccessible
            // Array, and *not* mark the dead node at all.
            //
            Mark_Stub_Only(s);
            Queue_Mark_Binding_Deep(v); // !!! Review this too, is it needed?
        }
        else {
            Queue_Mark_Array_Deep(cast_Array(s));
            Queue_Mark_Binding_Deep(v);
        }
        break; }

    case TYPE_BINARY:
    case TYPE_TEXT:
    case TYPE_FILE:
    case TYPE_EMAIL:
    case TYPE_URL:
    case TYPE_MONEY:
    case TYPE_TAG:
    case TYPE_TRIPWIRE:
    case TYPE_BITSET: {
        Flex* s = v->payload.any_series.series;

        assert(Flex_Wide(s) <= sizeof(Ucs2Unit));
        assert(not v->extra.binding); // for future use

        if (Get_Flex_Info(s, INACCESSIBLE)) {
            //
            // !!! See notes above on TYPE_BLOCK/etc. RE: letting series die.
            //
            Mark_Stub_Only(s);
        }
        else
            Mark_Stub_Only(s);
        break; }

    case TYPE_HANDLE: { // See %sys-handle.h
        Array* singular = v->extra.singular;
        if (singular == nullptr) {
            //
            // This HANDLE! was created with Init_Handle_Simple.  There is
            // no GC interaction.
        }
        else {
            // Handle was created with Init_Handle_Managed.  It holds a
            // Stub node that contains exactly one HANDLE!, and the actual
            // data for the handle lives in that shared location.  There is
            // nothing the GC needs to see inside a handle.
            //
            singular->leader.bits |= NODE_FLAG_MARKED;

        #if RUNTIME_CHECKS
            assert(Array_Len(singular) == 1);
            Cell* single = ARR_SINGLE(singular);
            assert(Is_Handle(single));
            assert(single->extra.singular == v->extra.singular);
            if (v != single) {
                //
                // In order to make it clearer that individual handles do not
                // hold the shared data (there'd be no way to update all the
                // references at once), the data pointers in all but the
                // shared singular value are nullptr.
                //
                if (Get_Cell_Flag(v, HANDLE_CFUNC))
                    assert(
                        Is_CFunction_Corrupt_Debug(v->payload.handle.data.cfunc)
                    );
                else
                    assert(
                        Is_Pointer_Corrupt_Debug(v->payload.handle.data.pointer)
                    );
            }
        #endif
        }
        break; }

    case TYPE_INTEGER:
    case TYPE_DECIMAL:
    case TYPE_PERCENT:
    case TYPE_CHAR:
        break;

    case TYPE_PAIR: {
        //
        // Ren-C's PAIR! uses a special kind of node that does no additional
        // memory allocation, but embeds two cellss in a Stub-sized slot.
        // A Cell has a uintptr_t header at the beginning of its struct,
        // just like a Stub, and the NODE_FLAG_MARKED bit is a 0
        // if unmarked...so it can stealthily participate in the marking
        // process, as long as the bit is cleared at the end.
        //
        Flex* pairing = cast(Flex*, v->payload.pair);
        pairing->leader.bits |= NODE_FLAG_MARKED;  // read via Stub
        break; }

    case TYPE_TUPLE:
    case TYPE_TIME:
    case TYPE_DATE:
        break;

    case TYPE_MAP: {
        REBMAP* map = VAL_MAP(v);
        Queue_Mark_Map_Deep(map);
        break;
    }

    case TYPE_DATATYPE:
        // Type spec is allowed to be nullptr.  See %typespec.r file
        if (CELL_DATATYPE_SPEC(v))
            Queue_Mark_Array_Deep(CELL_DATATYPE_SPEC(v));
        break;

    case TYPE_TYPESET:
        //
        // Not all typesets have symbols--only those that serve as the
        // keys of objects (or parameters of functions)
        //
        if (v->extra.key_symbol != nullptr)
            Mark_Stub_Only(v->extra.key_symbol);
        break;

    case TYPE_VARARGS: {
        if (v->payload.varargs.phase) // null if came from MAKE VARARGS!
            Queue_Mark_Action_Deep(v->payload.varargs.phase);

        Queue_Mark_Binding_Deep(v);
        break; }

    case TYPE_OBJECT:
    case TYPE_FRAME:
    case TYPE_MODULE:
    case TYPE_ERROR:
    case TYPE_PORT: { // Note: Cell_Varlist() fails on SER_INFO_INACCESSIBLE
        VarList* context = CTX(v->payload.any_context.varlist);
        Queue_Mark_Context_Deep(context);

        // Currently the "binding" in a context is only used by FRAME! to
        // preserve the binding of the ACTION! value that spawned that
        // frame.  Currently that binding is typically nullptr inside of a
        // function's cell unless it is a definitional RETURN or LEAVE.
        //
        // !!! Expanded usages may be found in other situations that mix an
        // archetype with an instance (e.g. an archetypal function body that
        // could apply to any OBJECT!, but the binding cheaply makes it
        // a method for that object.)
        //
        Queue_Mark_Binding_Deep(v);

      #if RUNTIME_CHECKS
        if (v->extra.binding != UNBOUND) {
            assert(CTX_TYPE(context) == TYPE_FRAME);

            if (Get_Flex_Info(context, INACCESSIBLE)) {
                //
                // !!! It seems a bit wasteful to keep alive the binding of a
                // stack frame you can no longer get values out of.  But
                // However, FUNCTION-OF still works on a FRAME! value after
                // the function is finished, if the FRAME! value was kept.
                // And that needs to give back a correct binding.
                //
            }
            else {
                Option(Level*) L = Level_Of_Varlist_If_Running(context);
                if (L) // comes from execution, not MAKE FRAME!
                    assert(VAL_BINDING(v) == LVL_BINDING(unwrap L));
            }
        }
      #endif

        REBACT *phase = v->payload.any_context.phase;
        if (phase) {
            assert(Type_Of(v) == TYPE_FRAME); // may be heap-based frame
            Queue_Mark_Action_Deep(phase);
        }
        else
            assert(Type_Of(v) != TYPE_FRAME); // phase if-and-only-if frame

        if (Get_Flex_Info(context, INACCESSIBLE))
            break;

      #if RUNTIME_CHECKS
        Value* archetype = Varlist_Archetype(context);
        assert(CTX_TYPE(context) == kind);
        assert(Cell_Varlist(archetype) == context);
      #endif

        // Note: for Cell_Varlist_FRAME, the LVL_CALL is either on the stack
        // (in which case it's already taken care of for marking) or it
        // has gone bad, in which case it should be ignored.

        break; }

    case TYPE_EVENT:
        Queue_Mark_Event_Deep(v);
        break;

    case TYPE_BLANK:
        break;

    case TYPE_TRASH:
    case TYPE_VOID:
    case TYPE_OKAY:
    case TYPE_NULLED:
        break;

    default:
        crash (v);
    }

  #if RUNTIME_CHECKS
    in_mark = false;
  #endif
}

INLINE void Queue_Mark_Opt_Value_Deep(const Cell* v)
{
    assert(NOT_END(v)); // can be NULLED, just not END
    Queue_Mark_Opt_End_Cell_Deep(v);
}

INLINE void Queue_Mark_Value_Deep(const Cell* v)
{
    assert(NOT_END(v));
    assert(VAL_TYPE_RAW(v) != TYPE_NULLED); // Note: Unreadable blanks ok
    Queue_Mark_Opt_End_Cell_Deep(v);
}


//
//  Propagate_All_GC_Marks: C
//
// The Mark Stack is a series containing series pointers.  They have already
// had their FLEX_FLAG_MARK set to prevent being added to the stack multiple
// times, but the items they can reach are not necessarily marked yet.
//
// Processing continues until all reachable items from the mark stack are
// known to be marked.
//
static void Propagate_All_GC_Marks(void)
{
    assert(not in_mark);

    while (Flex_Len(GC_Mark_Stack) != 0) {
        Set_Flex_Len(GC_Mark_Stack, Flex_Len(GC_Mark_Stack) - 1); // still ok

        // Data pointer may change in response to an expansion during
        // Mark_Array_Deep_Core(), so must be refreshed on each loop.
        //
        Array* a = *Flex_At(Array*, GC_Mark_Stack, Flex_Len(GC_Mark_Stack));

        // Termination is not required in the release build (the length is
        // enough to know where it ends).  But overwrite with trash in debug.
        //
        Corrupt_Pointer_If_Debug(
            *Flex_At(Array*, GC_Mark_Stack, Flex_Len(GC_Mark_Stack))
        );

        // We should have marked this series at queueing time to keep it from
        // being doubly added before the queue had a chance to be processed
         //
        assert(a->leader.bits & NODE_FLAG_MARKED);

    #ifdef HEAVY_CHECKS
        //
        // The GC is a good general hook point that all series which have been
        // managed will go through, so it's a good time to assert properties
        // about the array.
        //
        Assert_Array(a);
    #else
        //
        // For a lighter check, make sure it's marked as a value-bearing array
        // and that it hasn't been freed.
        //
        assert(Is_Flex_Array(a));
        assert(not Not_Node_Readable(a));
    #endif

        Cell* v;

        if (Get_Array_Flag(a, IS_PARAMLIST)) {
            v = Array_Head(a); // archetype
            assert(Is_Action(v));
            assert(not v->extra.binding); // archetypes have no binding

            // These queueings cannot be done in Queue_Mark_Function_Deep
            // because of the potential for overflowing the C stack with calls
            // to Queue_Mark_Function_Deep.

            Array* details = v->payload.action.details;
            Queue_Mark_Array_Deep(details);

            REBACT *underlying = LINK(a).underlying;
            Queue_Mark_Action_Deep(underlying);

            Array* specialty = LINK(details).specialty;
            if (Get_Array_Flag(specialty, IS_VARLIST))
                Queue_Mark_Context_Deep(CTX(specialty));
            else
                assert(specialty == a);

            VarList* meta = MISC(a).meta;
            if (meta)
                Queue_Mark_Context_Deep(meta);

            // Functions can't currently be freed by FREE...
            //
            assert(Not_Flex_Info(a, INACCESSIBLE));

            ++v; // function archetype completely marked by this process
        }
        else if (Get_Array_Flag(a, IS_VARLIST)) {
            v = Varlist_Archetype(CTX(a)); // works if FLEX_INFO_INACCESSIBLE

            // Currently only FRAME! uses binding
            //
            assert(Any_Context(v));
            assert(not v->extra.binding or Type_Of(v) == TYPE_FRAME);

            // These queueings cannot be done in Queue_Mark_Context_Deep
            // because of the potential for overflowing the C stack with calls
            // to Queue_Mark_Context_Deep.

            Node* keysource = LINK(a).keysource;
            if (not Is_Node_A_Stub(keysource)) {
                //
                // Must be a FRAME! and it must be on the stack running.  If
                // it has stopped running, then the keylist must be set to
                // UNBOUND which would not be a cell.
                //
                // There's nothing to mark for GC since the frame is on the
                // stack, which should preserve the function paramlist.
                //
                assert(Is_Frame(v));
            }
            else {
                Array* keylist = cast_Array(keysource);
                if (Is_Frame(v)) {
                    assert(Get_Array_Flag(keylist, IS_PARAMLIST));

                    // Frames use paramlists as their "keylist", there is no
                    // place to put an ancestor link.
                }
                else {
                    assert(Not_Array_Flag(keylist, IS_PARAMLIST));
                    assert(Is_Cell_Unreadable(Array_Head(keylist)));

                    Array* ancestor = LINK(keylist).ancestor;
                    Queue_Mark_Array_Subclass_Deep(ancestor); // maybe keylist
                }
                Queue_Mark_Array_Subclass_Deep(keylist);
            }

            VarList* meta = MISC(a).meta;
            if (meta != nullptr)
                Queue_Mark_Context_Deep(meta);

            // Stack-based frames will be inaccessible if they are no longer
            // running, so there's no data to mark...
            //
            if (Get_Flex_Info(a, INACCESSIBLE))
                continue;

            ++v; // context archetype completely marked by this process
        }
        else if (Get_Array_Flag(a, IS_PAIRLIST)) {
            //
            // There was once a "small map" optimization that wouldn't
            // produce a hashlist for small maps and just did linear search.
            // @giuliolunati deleted that for the time being because it
            // seemed to be a source of bugs, but it may be added again...in
            // which case the hashlist may be nullptr.
            //
            Flex* hashlist = LINK(a).hashlist;
            assert(hashlist != nullptr);

            Mark_Stub_Only(hashlist);

            // !!! Currently MAP! doesn't work with FREE, but probably should.
            //
            assert(Not_Flex_Info(a, INACCESSIBLE));

            v = Array_Head(a);
        }
        else {
            // Users can free the data of a plain array with FREE, leaving
            // the array stub.
            //
            // !!! It could be possible to GC all these to a common freed
            // array stub, though that wouldn't permit equality comparisons.
            //
            if (Get_Flex_Info(a, INACCESSIBLE))
                continue;

            v = Array_Head(a);
        }

        for (; NOT_END(v); ++v) {
            Queue_Mark_Opt_Value_Deep(v);
            //
        #if RUNTIME_CHECKS
            //
            // The enforcement of antiforms not making it into certain places
            // is done with compile-time checking in the main branch, but
            // all we do in this old bootstrap executable are periodic checks.
            //
            if (
                not Is_Cell_Unreadable(v)
                and Is_Antiform(v)
                and Not_Array_Flag(a, IS_VARLIST)
                and Not_Array_Flag(a, ANTIFORMS_LEGAL)
            ){
                crash (a);
            }
        #endif
        }
    }
}


//
//  Reify_Any_C_Valist_Frames: C
//
// Some of the call stack frames may have been invoked with a C function call
// that took a comma-separated list of Value* (the way printf works, a
// variadic "va_list").
//
// http://en.cppreference.com/w/c/variadic
//
// Although it's a list of Value*, these call frames have no Array series
// behind.  Yet they still need to be enumerated to protect the values coming
// up in the later EVALUATEs.  But enumerating a C va_list can't be undone.
// The Value* is lost if it isn't saved, and these frames may be in
// mid-evaluation.
//
// Hence, the garbage collector has to "reify" the remaining portion of the
// va_list into an Array before starting the GC.  Then the rest of the
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

    Level* L = TOP_LEVEL;
    for (; L != BOTTOM_LEVEL; L = L->prior) {
        if (NOT_END(L->value) and LVL_IS_VALIST(L)) {
            const bool truncated = true;
            Reify_Va_To_Array_In_Level(L, truncated);
        }
    }
}


//
//  Mark_Root_Stubs: C
//
// Root Series are any manual series that were allocated but have not been
// managed yet, as well as Alloc_Value() nodes that are explicitly "roots".
//
// For root nodes, this checks to see if their lifetime was dependent on a
// FRAME!, and if that frame is no longer on the stack.  If so, it (currently)
// will crash if that frame did not end due to a panic().  This could be
// relaxed to automatically free those nodes as a normal GC.
//
// !!! This implementation walks over *all* the nodes.  It wouldn't have to
// if API nodes were in their own pool, or if the outstanding manuals list
// were maintained even in non-debug builds--it could just walk those.  This
// should be weighed against background GC and other more sophisticated
// methods which might come down the road for the GC than this simple one.
//
static void Mark_Root_Stubs(void)
{
    REBSEG *seg;
    for (seg = Mem_Pools[STUB_POOL].segs; seg; seg = seg->next) {
        Flex* s = cast(Flex* , seg + 1);
        REBLEN n;
        for (n = Mem_Pools[STUB_POOL].units; n > 0; --n, ++s) {
            //
            // !!! A smarter switch statement here could do this more
            // optimally...see the sweep code for an example.
            //
            if (Not_Node_Readable(s))
                continue;

            if (s->leader.bits & NODE_FLAG_ROOT) {
                //
                // This came from Alloc_Value(); all references should be
                // from the C stack, only this visit should be marking it.
                //
                assert(not (s->leader.bits & NODE_FLAG_MARKED));
                assert(not Is_Flex_Dynamic(s));
                assert(
                    not LINK(s).owner
                    or LINK(s).owner->leader.bits & NODE_FLAG_MANAGED
                );

                if (not (s->leader.bits & NODE_FLAG_MANAGED))
                    assert(not LINK(s).owner);
                else if (
                    Varlist_Array(LINK(s).owner)->info.bits
                    & FLEX_INFO_INACCESSIBLE
                ){
                    if (Not_Flex_Info(LINK(s).owner, FRAME_PANICKED)) {
                        //
                        // Long term, it is likely that implicit managed-ness
                        // will allow users to leak API handles.  It will
                        // always be more efficient to not do that, so having
                        // the code be strict for now is better.
                        //
                      #if RUNTIME_CHECKS
                        printf("handle not rebReleased(), not legal ATM\n");
                      #endif
                        crash (s);
                    }

                    GC_Kill_Flex(s);
                    continue;
                }
                else // note that Mark_Level_Stack_Deep() will mark the owner
                    s->leader.bits |= NODE_FLAG_MARKED;

                // Note: Eval_Core_Throws() might target API cells, uses END
                //
                Queue_Mark_Opt_End_Cell_Deep(ARR_SINGLE(cast_Array(s)));
                continue;
            }

            if (s->leader.bits & NODE_FLAG_CELL) { // a pairing
                if (s->leader.bits & NODE_FLAG_MANAGED)
                    continue; // PAIR! or other value will mark it

                assert(!"unmanaged pairings not believed to exist yet");
                Value* paired = cast(Value*, s);
                Queue_Mark_Opt_Value_Deep(paired);
                Queue_Mark_Opt_Value_Deep(PAIRING_KEY(paired));
            }

            if (Is_Flex_Array(s)) {
                if (s->leader.bits & NODE_FLAG_MANAGED)
                    continue; // BLOCK!, Mark_Level_Stack_Deep() etc. mark it

                if (s->leader.bits & ARRAY_FLAG_IS_VARLIST) {
                    //
                    // Legal when unmanaged varlists are held onto by
                    // Level*, and marked by them.  We check for that by
                    // whether the keysource points to a frame (cell bit
                    // set in node).
                    //
                    assert(not Is_Node_A_Stub(LINK(s).keysource));
                    continue;
                }

                // This means someone did something like Make_Array() and then
                // ran an evaluation before referencing it somewhere from the
                // root set.  The bootstrap executable has a hard time with
                // this, so only a limited number of arrays are allowed.

                if (s == DS_Array)
                    continue;  // handled by Mark_Data_Stack()

                if (s == BUF_COLLECT) {
                    if (Array_Len(BUF_COLLECT) != 0)
                        crash (BUF_COLLECT);
                    continue;  // shouldn't recycle while collecting
                }

                crash (s);
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
// the capacity of the data stack's underlying array, it begins at TOP.
//
static void Mark_Data_Stack(void)
{
    Value* head = KNOWN(Array_Head(DS_Array));
    assert(Is_Cell_Unreadable(head));

    Value* stackval = TOP;
    for (; stackval != head; --stackval)
        Queue_Mark_Value_Deep(stackval);

    Propagate_All_GC_Marks();
}


//
//  Mark_Symbols: C
//
// Mark Symbol Flexes.  These canon words for SYM_XXX are the only ones that
// are never candidates for GC (until shutdown).  All other symbol series may
// go away if no words, parameters, object keys, etc. refer to them.
//
static void Mark_Symbols(void)
{
    Symbol* *canon = Flex_Head(Symbol*, PG_Symbol_Canons);
    assert(Is_Pointer_Corrupt_Debug(*canon)); // SYM_0 is for all non-builtin words
    ++canon;
    for (; *canon != nullptr; ++canon)
        Mark_Stub_Only(*canon);

    ASSERT_NO_GC_MARKS_PENDING(); // doesn't ues any queueing
}


//
//  Mark_Natives: C
//
// For each native C implemenation, a cell is created during init to
// represent it as an ACTION!.  These are kept in a global array and are
// protected from GC.  It might not technically be necessary to do so for
// all natives, but at least some have their paramlists referenced by the
// core code (such as RETURN).
//
static void Mark_Natives(void)
{
    REBLEN n;
    for (n = 0; n < Num_Natives; ++n)
        Queue_Mark_Value_Deep(&Natives[n]);

    Propagate_All_GC_Marks();
}


//
//  Mark_Guarded_Nodes: C
//
// Mark series and values that have been temporarily protected from garbage
// collection with Push_GC_Guard.  Subclasses e.g. ARRAY_FLAG_CONTEXT will
// have their LINK() and MISC() fields guarded appropriately for the class.
//
static void Mark_Guarded_Nodes(void)
{
    Node** np = Flex_Head(Node*, GC_Guarded);
    REBLEN n = Flex_Len(GC_Guarded);
    for (; n > 0; --n, ++np) {
        Node* node = *np;
        if (Is_Node_A_Cell(node)) {
            //
            // !!! What if someone tried to GC_GUARD a managed pairing?
            //
            Queue_Mark_Opt_End_Cell_Deep(cast(Value*, node));
        }
        else { // a series
            Flex* s = cast(Flex*, node);
            assert(s->leader.bits & NODE_FLAG_MANAGED);
            if (Is_Flex_Array(s))
                Queue_Mark_Array_Subclass_Deep(cast_Array(s));
            else
                Mark_Stub_Only(s);
        }
        Propagate_All_GC_Marks();
    }
}


//
//  Mark_Level_Stack_Deep: C
//
// Mark values being kept live by all call frames.  If a function is running,
// then this will keep the function itself live, as well as the arguments.
// There is also an "out" slot--which may point to an arbitrary Value cell
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
static void Mark_Level_Stack_Deep(void)
{
    Level* L = TOP_LEVEL;

    while (true) { // mark all frames (even BOTTOM_LEVEL)
        //
        // Should have taken care of reifying all the VALIST on the stack
        // earlier in the recycle process (don't want to create new arrays
        // once the recycling has started...)
        //
        assert(not L->source->vaptr or Is_Pointer_Corrupt_Debug(L->source->vaptr));

        // Note: L->source->pending should either live in L->source->array, or
        // it may be trash (e.g. if it's an apply).  GC can ignore it.
        //
        if (L->source->array)
            Queue_Mark_Array_Deep(L->source->array);

        // END is possible, because the frame could be sitting at the end of
        // a block when a function runs, e.g. `eval [zero-arity]`.  That frame
        // will stay on the stack while the zero-arity function is running.
        // The array still might be used in an error, so can't GC it.
        //
        Queue_Mark_Opt_End_Cell_Deep(L->value);

        // If L->gotten is set, it usually shouldn't need markeding because
        // it's fetched via L->value and so would be kept alive by it.  Any
        // code that a frame runs that might disrupt that relationship so it
        // would fetch differently should have meant clearing L->gotten.
        //
        // However, the SHOVE operation is special, and puts an infix ACTION!
        // into the frame's `shove` cell and points L->gotten to that.  It
        // needs to be marked here.
        //
        if (not L->gotten)
            NOOP;
        else
            assert(
                Is_Pointer_Corrupt_Debug(L->gotten)
                or L->gotten == Try_Get_Opt_Var(L->value, L->specifier)
            );

        if (
            L->specifier != SPECIFIED
            and (L->specifier->leader.bits & NODE_FLAG_MANAGED)
        ){
            Queue_Mark_Context_Deep(CTX(L->specifier));
        }

        Queue_Mark_Opt_End_Cell_Deep(L->out); // END legal, but not nullptr

        // Frame temporary cell should always contain initialized bits, as
        // DECLARE_LEVEL sets it up and no one is supposed to trash it.
        //
        Queue_Mark_Opt_End_Cell_Deep(Level_Spare(L));

        if (not Is_Action_Level(L)) {
            //
            // Consider something like `eval copy the (recycle)`, because
            // while evaluating the group it has no anchor anywhere in the
            // root set and could be GC'd.  The Level's array ref is it.
            //
            goto propagate_and_continue;
        }

        Queue_Mark_Action_Deep(L->original);  // never nullptr
        if (L->opt_label)  // will be nullptr if no symbol
            Mark_Stub_Only(L->opt_label);

        // refine and special can be used to GC protect an arbitrary value
        // while a function is running, currently.  nullptr is permitted as
        // well for flexibility (e.g. path frames use nullptr to indicate no
        // set value on a path)
        //
        if (L->refine)
            Queue_Mark_Opt_End_Cell_Deep(L->refine);
        if (L->special)
            Queue_Mark_Opt_End_Cell_Deep(L->special);

        if (L->varlist and Is_Node_Managed(L->varlist)) {
            //
            // If the context is all set up with valid values and managed,
            // then it can just be marked normally...no need to do custom
            // partial parameter traversal.
            //
            assert(IS_END(L->param)); // done walking
            Queue_Mark_Context_Deep(CTX(L->varlist));
            goto propagate_and_continue;
        }

        if (L->varlist and Get_Flex_Info(L->varlist, INACCESSIBLE)) {
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
        // by the *evaluating frame's L->out* (!)
        //
        // Refinements need special treatment, and also consideration of if
        // this is the "doing pickups" or not.  If doing pickups then skip the
        // cells for pending refinement arguments.
        //
        REBACT *phase; // goto would cross initialization
        phase = LVL_PHASE_OR_DUMMY(L);
        Value* param;
        if (phase == PG_Dummy_Action)
            param = ACT_PARAMS_HEAD(L->original); // no phases will run
        else
            param = ACT_PARAMS_HEAD(phase);

        Value* arg;
        for (arg = Level_Args_Head(L); NOT_END(param); ++param, ++arg) {
            if (param == L->param) {
                //
                // When param and L->param match, that means that arg is the
                // output slot for some other frame's L->out.  Let that frame
                // do the marking (which tolerates END, an illegal state for
                // prior arg slots we've visited...unless deferred!)

                // If we're not doing "pickups" then the cell slots after
                // this one have not been initialized, not even to trash.
                //
                if (Not_Eval_Flag(L, DOING_PICKUPS))
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
            assert(NOT_END(arg));
            Queue_Mark_Opt_End_Cell_Deep(arg);
        }

      propagate_and_continue:;

        Propagate_All_GC_Marks();
        if (L == BOTTOM_LEVEL)
            break;

        L = L->prior;
    }
}


//
//  Sweep_Stubs: C
//
// Scans all series nodes (Stub structs) in all segments that are part of
// the STUB_POOL.  If a series had its lifetime management delegated to the
// garbage collector with Manage_Flex(), then if it didn't get "marked" as
// live during the marking phase then free it.
//
static REBLEN Sweep_Stubs(void)
{
    REBLEN count = 0;

    REBSEG* seg = Mem_Pools[STUB_POOL].segs;

    for (; seg != nullptr; seg = seg->next) {
        Count n = Mem_Pools[STUB_POOL].units;
        Byte* unit = cast(Byte*, seg + 1);  // byte beats strict alias [1]

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;  // only unit without NODE_FLAG_NODE (in ASCII range)

            assert(unit[0] & NODE_BYTEMASK_0x80_NODE);

            if (not (unit[0] & NODE_BYTEMASK_0x04_MANAGED)) {
                assert(not (unit[0] & NODE_BYTEMASK_0x01_MARKED));

                /*if (unit[0] == DECAYED_NON_CANON_BYTE) {
                    Free_Pooled(STUB_POOL, unit);
                    continue;
                }*/
                assert(not (unit[0] & NODE_BYTEMASK_0x40_UNREADABLE));
                continue;  // ignore all unmanaged Stubs/Pairings
            }

            if (unit[0] & NODE_BYTEMASK_0x01_MARKED) {  // managed and marked
                unit[0] &= (~ NODE_BYTEMASK_0x01_MARKED);  // just remove mark
                continue;
            }

            assert(not (unit[0] & NODE_BYTEMASK_0x02_ROOT));  // roots marked

            ++count;  // managed but not marked => free it!

            if (unit[0] & NODE_BYTEMASK_0x08_CELL) {  // managed pairing
                Free_Pooled(STUB_POOL, unit);  // manuals use Free_Pairing()
                continue;
            }

            /*if (unit[0] & NODE_BYTEMASK_0x40_UNREADABLE) {
                //
                // Stubs that have been marked freed may have outstanding
                // references at the moment of being marked free...but the GC
                // canonizes the reference pointers to PG_Inaccessible_Stub.
                // So they should not have been marked, and once the GC pass
                // is over we can just free them.
            }
            else
                Decay_Flex(u_cast(Flex*, unit)); */

            GC_Kill_Flex(u_cast(Flex*, unit));
        }
    }

    // For efficiency of memory use, Stub is nominally defined as
    // 2*sizeof(Cell), and so pairs can use the same nodes.  But features
    // that might make the cells a size greater than Stub size require
    // doing pairings in a different pool.
    //
  #if UNUSUAL_CELL_SIZE
    for (seg = Mem_Pools[PAR_POOL].segs; seg != nullptr; seg = seg->next) {
        if (NODE_BYTE(seg + 1) == FREE_POOLUNIT_BYTE)
            continue;

        Cell* c = cast(Value*, seg + 1);
        assert(c->header.bits & NODE_FLAG_CELL);

        if (c->header.bits & NODE_FLAG_MANAGED) {
            assert(not (c->header.bits & NODE_FLAG_ROOT));
            if (c->header.bits & NODE_FLAG_MARKED)
                c->header.bits &= ~NODE_FLAG_MARKED;
            else {
                Free_Pooled(PAR_POOL, c); // Free_Pairing is for manuals
                ++count;
            }
        }
    }
  #endif

    return count;
}


#if RUNTIME_CHECKS

//
//  Fill_Sweeplist: C
//
REBLEN Fill_Sweeplist(Flex* sweeplist)
{
    assert(Flex_Wide(sweeplist) == sizeof(Node*));
    assert(Flex_Len(sweeplist) == 0);

    REBLEN count = 0;

    REBSEG *seg;
    for (seg = Mem_Pools[STUB_POOL].segs; seg != nullptr; seg = seg->next) {
        Flex* s = cast(Flex*, seg + 1);
        REBLEN n;
        for (n = Mem_Pools[STUB_POOL].units; n > 0; --n, ++s) {
            switch (FIRST_BYTE(&s->leader) >> 4) {
            case 9: // 0x8 + 0x1
                assert(Is_Flex_Managed(s));
                if (s->leader.bits & NODE_FLAG_MARKED)
                    s->leader.bits &= ~NODE_FLAG_MARKED;
                else {
                    Expand_Flex_Tail(sweeplist, 1);
                    *Flex_At(Node*, sweeplist, count) = s;
                    ++count;
                }
                break;

            case 11: // 0x8 + 0x2 + 0x1
                //
                // It's a cell which is managed where the value is not an END.
                // This is a managed pairing, so mark bit should be heeded.
                //
                // !!! It is a Node, but *not* a "Stub".
                //
                assert(Is_Flex_Managed(s));
                if (s->leader.bits & NODE_FLAG_MARKED)
                    s->leader.bits &= ~NODE_FLAG_MARKED;
                else {
                    Expand_Flex_Tail(sweeplist, 1);
                    *Flex_At(Node*, sweeplist, count) = s;
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
// Recycle memory no longer needed.  If sweeplist is not nullptr, then it needs
// to be a series whose width is sizeof(Flex*), and it will be filled with
// the list of series that *would* be recycled.
//
REBLEN Recycle_Core(bool shutdown, Flex* sweeplist)
{
    // Ordinarily, it should not be possible to spawn a recycle during a
    // recycle.  But when debug code is added into the recycling code, it
    // could cause a recursion.  Be tolerant of such recursions to make that
    // debugging easier...but make a note that it's not ordinarily legal.
    //
#if RUNTIME_CHECKS
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

  #if RUNTIME_CHECKS
    GC_Recycling = true;
  #endif

    ASSERT_NO_GC_MARKS_PENDING();
    Reify_Any_C_Valist_Frames();

  #if RUNTIME_CHECKS
    PG_Reb_Stats->Recycle_Counter++;
    PG_Reb_Stats->Num_Flex_Recycled = Mem_Pools[STUB_POOL].free;
    PG_Reb_Stats->Mark_Count = 0;
  #endif

    assert(Array_Len(BUF_COLLECT) == 0);

    // The TG_Reuse list consists of entries which could grow to arbitrary
    // length, and which aren't being tracked anywhere.  Cull them during GC
    // in case the stack at one point got very deep and isn't going to use
    // them again, and the memory needs reclaiming.
    //
    while (TG_Reuse) {
        Array* varlist = TG_Reuse;
        TG_Reuse = LINK(TG_Reuse).reuse;
        GC_Kill_Flex(varlist);  // no track for Free_Unmanaged_Flex()
    }

    // MARKING PHASE: the "root set" from which we determine the liveness
    // (or deadness) of a series.  If we are shutting down, we do not mark
    // several categories of series...but we do need to run the root marking.
    // (In particular because that is when API series whose lifetimes
    // are bound to frames will be freed, if the frame is expired.)
    //
    Mark_Root_Stubs();

    if (not shutdown) {
        Mark_Natives();
        Mark_Symbols();

        Mark_Data_Stack();

        Mark_Guarded_Nodes();

        Mark_Level_Stack_Deep();

        Propagate_All_GC_Marks();

        Mark_Devices_Deep();
    }

    // SWEEPING PHASE

    ASSERT_NO_GC_MARKS_PENDING();

    REBLEN count = 0;

    if (sweeplist != nullptr) {
    #if NO_RUNTIME_CHECKS
        crash (sweeplist);
    #else
        count += Fill_Sweeplist(sweeplist);
    #endif
    }
    else
        count += Sweep_Stubs();

#if RUNTIME_CHECKS
    // Compute new stats:
    PG_Reb_Stats->Num_Flex_Recycled
        = Mem_Pools[STUB_POOL].free - PG_Reb_Stats->Num_Flex_Recycled;
    PG_Reb_Stats->Recycle_Flex_Total += PG_Reb_Stats->Num_Flex_Recycled;
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

  #if RUNTIME_CHECKS
    GC_Recycling = false;
  #endif

    return count;
}


//
//  Recycle: C
//
// Recycle memory no longer needed.
//
REBLEN Recycle(void)
{
    // Default to not passing the `shutdown` flag.
    //
    REBLEN n = Recycle_Core(false, nullptr);

  #ifdef DOUBLE_RECYCLE_TEST
    //
    // If there are two recycles in a row, then the second should not free
    // any additional series that were not freed by the first.  (It also
    // shouldn't crash.)  This is an expensive check, but helpful to try if
    // it seems a GC left things in a bad state that crashed a later GC.
    //
    REBLEN n2 = Recycle_Core(false, nullptr);
    assert(n2 == 0);
  #endif

    return n;
}


//
//  Push_Guard_Node: C
//
void Push_Guard_Node(const Node* node)
{
  #if RUNTIME_CHECKS
    if (Is_Node_A_Cell(node)) {
        //
        // It is a value.  Cheap check: require that it already contain valid
        // data when the guard call is made (even if GC isn't necessarily
        // going to happen immediately, and value could theoretically become
        // valid before then.)
        //
        const Value* value = cast(const Value*, node);
        assert(
            IS_END(value)
            or Is_Cell_Unreadable(value)
            or Type_Of(value) <= TYPE_NULLED
        );

      #ifdef STRESS_CHECK_GUARD_VALUE_POINTER
        //
        // Technically we should never call this routine to guard a value
        // that lives inside of a series.  Not only would we have to guard the
        // containing series, we would also have to lock the series from
        // being able to resize and reallocate the data pointer.  But this is
        // a somewhat expensive check, so only feasible to run occasionally.
        //
        Node* containing = Try_Find_Containing_Node_Debug(value);
        if (containing)
            crash (containing);
      #endif
    }
    else {
        // It's a series.  Does not ensure the series being guarded is
        // managed, since it can be interesting to guard the managed
        // *contents* of an unmanaged array.  The calling wrappers ensure
        // managedness or not.
    }
  #endif

    if (Is_Flex_Full(GC_Guarded))
        Extend_Flex(GC_Guarded, 8);

    *Flex_At(
        const Node*,
        GC_Guarded,
        Flex_Len(GC_Guarded)
    ) = node;

    Set_Flex_Len(GC_Guarded, Flex_Len(GC_Guarded) + 1);
}


//
//  Snapshot_All_Actions: C
//
// This routine can be used to get a list of all the functions in the system
// at a given moment in time.  Be sure to protect this array from GC when
// enumerating if there is any chance the GC might run (e.g. if user code
// is called to process the function list)
//
Array* Snapshot_All_Actions(void)
{
    StackIndex base = TOP_INDEX;

    REBSEG *seg;
    for (seg = Mem_Pools[STUB_POOL].segs; seg != nullptr; seg = seg->next) {
        Flex* s = cast(Flex*, seg + 1);
        REBLEN n;
        for (n = Mem_Pools[STUB_POOL].units; n > 0; --n, ++s) {
            switch (s->leader.bits & 0x7) {
            case 5:
                // A managed Stub which has no cell mask and is marked as
                // *not* an END.  This is the typical signature of what one
                // would call an "ordinary managed series".  (For the meanings
                // of other bits, see Sweep_Stubs.)
                //
                assert(Is_Flex_Managed(s));
                if (Get_Array_Flag(s, IS_PARAMLIST)) {
                    Value* v = KNOWN(Array_Head(cast_Array(s)));
                    assert(Is_Action(v));
                    Copy_Cell(PUSH(), v);
                }
                break;
            }
        }
    }

    return Pop_Stack_Values(base);
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
    GC_Guarded = Make_Flex(15, sizeof(Node*));

    // The marking queue used in lieu of recursion to ensure that deeply
    // nested structures don't cause the C stack to overflow.
    //
    GC_Mark_Stack = Make_Flex(100, sizeof(Array*));
    Term_Non_Array_Flex(GC_Mark_Stack);
}


//
//  Shutdown_GC: C
//
void Shutdown_GC(void)
{
    Free_Unmanaged_Flex(GC_Guarded);
    Free_Unmanaged_Flex(GC_Mark_Stack);
}


//=////////////////////////////////////////////////////////////////////////=//
//
// DEPRECATED HOOKS INTO THE CORE GARBAGE COLLECTOR
//
//=////////////////////////////////////////////////////////////////////////=//

//
//  Queue_Mark_Event_Deep: C
//
// 'Queue' refers to the fact that after calling this routine,
// one will have to call Propagate_All_GC_Marks() to have the
// deep transitive closure completely marked.
//
static void Queue_Mark_Event_Deep(const Cell* value)
{
    REBREQ *req;

    if (
        IS_EVENT_MODEL(value, EVM_PORT)
        or IS_EVENT_MODEL(value, EVM_OBJECT)
    ){
        Queue_Mark_Context_Deep(CTX(VAL_EVENT_FLEX(m_cast(Cell*, value))));
    }

    if (IS_EVENT_MODEL(value, EVM_DEVICE)) {
        // In the case of being an EVM_DEVICE event type, the port! will
        // not be in VAL_EVENT_FLEX of the REBEVT structure.  It is held
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
