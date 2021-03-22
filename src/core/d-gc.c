//
//  File: %d-gc.c
//  Summary: "Debug-Build Checks for the Garbage Collector"
//  Section: debug
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// The R3-Alpha GC had to do switch() on the kind of cell to know how to
// handle it.  Ren-C makes bits in the value cell itself dictate what needs
// to be done...which is faster, but it doesn't get the benefit of checking
// additional invariants that the switch() branches were doing.
//
// This file extracts the switch()-based checks so that they do not clutter
// the readability of the main GC code.
//

#include "sys-core.h"

#if !defined(NDEBUG)

#define Is_Marked(n) \
    (did (NODE_BYTE(n) & NODE_BYTEMASK_0x10_MARKED))


//
//  Assert_Cell_Marked_Correctly: C
//
// Note: We assume the binding was marked correctly if the type was bindable.
//
void Assert_Cell_Marked_Correctly(const RELVAL *v)
{
    if (KIND3Q_BYTE_UNCHECKED(v) == REB_QUOTED) {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        assert(HEART_BYTE(v) == REB_QUOTED);
        assert(Is_Marked(VAL_NODE1(v)));
        assert(VAL_QUOTED_DEPTH(v) >= 3);
        REBCEL(const*) cell = VAL_UNESCAPED(v);
        if (ANY_WORD_KIND(CELL_KIND(cell)))
            assert(BINDING(cell) == UNBOUND);  // escaped cell has no binding
        return;
    }

    enum Reb_Kind heart = CELL_HEART(cast(REBCEL(const*), v));

    REBSER *binding;
    if (
        IS_BINDABLE_KIND(heart)
        and (binding = BINDING(v))
        and not IS_SYMBOL(binding)
        and NOT_SERIES_FLAG(binding, INACCESSIBLE)
    ){
        if (not IS_SER_ARRAY(binding))
            panic(binding);

        assert(IS_SER_ARRAY(binding));
        if (IS_VARLIST(binding) and CTX_TYPE(CTX(binding)) == REB_FRAME) {
            REBNOD *keysource = LINK(KeySource, ARR(binding));
            if (not Is_Node_Cell(keysource)) {
                if (
                    (SER(keysource)->leader.bits & SERIES_MASK_KEYLIST)
                    != SERIES_MASK_KEYLIST
                ){
                    panic (binding);
                }
                if (NOT_SERIES_FLAG(SER(keysource), MANAGED))
                    panic (keysource);
            }
        }
    }

    // This switch was originally done via contiguous REB_XXX values, in order
    // to facilitate use of a "jump table optimization":
    //
    // http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables
    //
    // Since this is debug-only, it's not as important any more.  But it
    // still can speed things up to go in order.
    //
    switch (heart) {
      case REB_0_END:
      case REB_NULL:
      case REB_BAD_WORD:
      case REB_BLANK:
      case REB_COMMA:
      case REB_META:
      case REB_THE:
        break;

      case REB_LOGIC:
      case REB_INTEGER:
      case REB_DECIMAL:
      case REB_PERCENT:
      case REB_MONEY:
        break;

      case REB_BYTES:  // e.g. for ISSUE! when fits in cell
        break;

      case REB_PAIR: {
        REBVAL *paired = VAL(VAL_NODE1(v));
        assert(Is_Marked(paired));
        break; }

      case REB_TIME:
      case REB_DATE:
        break;

      case REB_DATATYPE:
        if (VAL_TYPE_SPEC(v))  // currently allowed to be null, see %types.r
            assert(Is_Marked(VAL_TYPE_SPEC(v)));
        assert(VAL_TYPE_KIND_OR_CUSTOM(v) != REB_0);
        break;

      case REB_TYPESET: {  // bitset bits don't need marking
        break; }

      case REB_BITSET: {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        REBSER *s = SER(VAL_NODE1(v));
        Assert_Series_Term_Core(s);
        if (GET_SERIES_FLAG(s, INACCESSIBLE))
            assert(Is_Marked(s));  // TBD: clear out reference and GC `s`?
        else
            assert(Is_Marked(s));
        break; }

      case REB_MAP: {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        const REBMAP* map = VAL_MAP(v);
        assert(Is_Marked(map));
        assert(IS_SER_ARRAY(MAP_PAIRLIST(map)));
        break; }

      case REB_HANDLE: { // See %sys-handle.h
        if (NOT_CELL_FLAG(v, FIRST_IS_NODE)) {
            // simple handle, no GC interaction
        }
        else {
            REBARR *a = VAL_HANDLE_SINGULAR(v);

            // Handle was created with Init_Handle_XXX_Managed.  It holds a
            // REBSER node that contains exactly one handle, and the actual
            // data for the handle lives in that shared location.  There is
            // nothing the GC needs to see inside a handle.
            //
            assert(v->header.bits & CELL_FLAG_FIRST_IS_NODE);
            assert(Is_Marked(a));

            RELVAL *single = ARR_SINGLE(a);
            assert(IS_HANDLE(single));
            assert(VAL_HANDLE_SINGULAR(single) == a);
            if (v != single) {
                //
                // In order to make it clearer that individual handles do not
                // hold the shared data (there'd be no way to update all the
                // references at once), the data pointers in all but the
                // shared singular value are NULL.
                //
                // (Trash not used because release build complains about lack
                // of initialization, so null is always used)
                //
                assert(VAL_HANDLE_CDATA_P(v) == nullptr);
            }
        }
        break; }

      case REB_EVENT: {  // packed cell structure with one GC-able slot
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        REBNOD *n = VAL_NODE1(v);  // REBGOB*, REBREQ*, etc.
        assert(n == nullptr or Is_Marked(n));
        break; }

      case REB_BINARY: {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        REBBIN *s = BIN(VAL_NODE1(v));
        if (GET_SERIES_FLAG(s, INACCESSIBLE))
            break;

        assert(SER_WIDE(s) == sizeof(REBYTE));
        ASSERT_SERIES_TERM_IF_NEEDED(s);
        assert(Is_Marked(s));
        break; }

      case REB_TEXT:
      case REB_FILE:
      case REB_EMAIL:
      case REB_URL:
      case REB_TAG: {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        if (GET_SERIES_FLAG(STR(VAL_NODE1(v)), INACCESSIBLE))
            break;

        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        const REBSER *s = VAL_SERIES(v);
        ASSERT_SERIES_TERM_IF_NEEDED(s);

        assert(SER_WIDE(s) == sizeof(REBYTE));
        assert(Is_Marked(s));

        if (IS_NONSYMBOL_STRING(s)) {
            REBBMK *bookmark = LINK(Bookmarks, s);
            if (bookmark) {
                assert(SER_USED(bookmark) == 1);  // just one for now
                //
                // The intent is that bookmarks are unmanaged REBSERs, which
                // get freed when the string GCs.  This mechanic could be a by
                // product of noticing that the SERIES_FLAG_LINK_NODE_NEEDS_MARK is
                // true but that the managed bit on the node is false.

                assert(not Is_Marked(bookmark));
                assert(NOT_SERIES_FLAG(bookmark, MANAGED));
            }
        }
        break; }

    //=//// BEGIN BINDABLE TYPES ////////////////////////////////////////=//

      case REB_OBJECT:
      case REB_MODULE:
      case REB_ERROR:
      case REB_FRAME:
      case REB_PORT: {
        if (GET_SERIES_FLAG(SER(VAL_NODE1(v)), INACCESSIBLE))
            break;

        assert((v->header.bits & CELL_MASK_CONTEXT) == CELL_MASK_CONTEXT);
        REBCTX *context = VAL_CONTEXT(v);
        assert(Is_Marked(context));

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
        if (BINDING(v) != UNBOUND) {
            if (CTX_TYPE(context) == REB_FRAME) {
                struct Reb_Frame *f = CTX_FRAME_IF_ON_STACK(context);
                if (f)  // comes from execution, not MAKE FRAME!
                    assert(VAL_FRAME_BINDING(v) == FRM_BINDING(f));
            }
            else
                assert(IS_PATCH(Singular_From_Cell(v)));
        }

        if (PAYLOAD(Any, v).second.node) {
            assert(heart == REB_FRAME); // may be heap-based frame
            assert(Is_Marked(PAYLOAD(Any, v).second.node));  // phase or label
        }

        if (GET_SERIES_FLAG(CTX_VARLIST(context), INACCESSIBLE))
            break;

        const REBVAL *archetype = CTX_ARCHETYPE(context);
        assert(CTX_TYPE(context) == heart);
        assert(VAL_CONTEXT(archetype) == context);

        // Note: for VAL_CONTEXT_FRAME, the FRM_CALL is either on the stack
        // (in which case it's already taken care of for marking) or it
        // has gone bad, in which case it should be ignored.

        break; }

      case REB_VARARGS: {
        assert((v->header.bits & CELL_MASK_VARARGS) == CELL_MASK_VARARGS);
        REBACT *phase = VAL_VARARGS_PHASE(v);
        if (phase)  // null if came from MAKE VARARGS!
            assert(Is_Marked(phase));
        break; }

      case REB_BLOCK:
      case REB_THE_BLOCK:
      case REB_SET_BLOCK:
      case REB_GET_BLOCK:
      case REB_META_BLOCK:
      case REB_GROUP:
      case REB_THE_GROUP:
      case REB_SET_GROUP:
      case REB_GET_GROUP:
      case REB_META_GROUP: {
        REBARR *a = ARR(VAL_NODE1(v));
        if (GET_SERIES_FLAG(a, INACCESSIBLE))
            break;

        ASSERT_SERIES_TERM_IF_NEEDED(a);

        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        assert(Is_Marked(a));
        break; }

      case REB_TUPLE:
      case REB_THE_TUPLE:
      case REB_SET_TUPLE:
      case REB_GET_TUPLE:
      case REB_META_TUPLE:
        goto any_sequence;

      case REB_PATH:
      case REB_THE_PATH:
      case REB_SET_PATH:
      case REB_GET_PATH:
      case REB_META_PATH:
        goto any_sequence;

      any_sequence: {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        REBARR *a = ARR(VAL_NODE1(v));
        assert(NOT_SERIES_FLAG(a, INACCESSIBLE));

        // With most arrays we may risk direct recursion, hence we have to
        // use Queue_Mark_Array_Deep().  But paths are guaranteed to not have
        // other paths directly in them.  Walk it here so that we can also
        // check that there are no paths embedded.
        //
        // Note: This doesn't catch cases which don't wind up reachable from
        // the root set, e.g. anything that would be GC'd.
        //
        // !!! Optimization abandoned

        assert(ARR_LEN(a) >= 2);
        const RELVAL *tail = ARR_TAIL(a);
        const RELVAL *item = ARR_HEAD(a);
        for (; item != tail; ++item)
            assert(not ANY_PATH_KIND(KIND3Q_BYTE_UNCHECKED(item)));
        assert(Is_Marked(a));
        break; }

      case REB_WORD:
      case REB_THE_WORD:
      case REB_SET_WORD:
      case REB_GET_WORD:
      case REB_META_WORD: {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));

        const REBSTR *spelling = VAL_WORD_SYMBOL(v);
        assert(Is_Series_Frozen(spelling));

        // !!! Whether you can count at this point on a spelling being GC
        // marked depends on whether it's the binding or not; this is a
        // change from when spellings were always pointed to by the cell.
        //
        if (IS_WORD_UNBOUND(v))
            assert(Is_Marked(spelling));

        // GC can't run during bind
        //
        assert(NOT_SERIES_FLAG(MISC(Hitch, spelling), BLACK));

        REBLEN index = VAL_WORD_PRIMARY_INDEX_UNCHECKED(v);
        if (IS_WORD_BOUND(v)) {
            if (IS_VARLIST(BINDING(v))) {
                if (CTX_TYPE(CTX(BINDING(v))) == REB_MODULE)
                    assert(index != 0);
                else
                    assert(index != 0 and index != INDEX_ATTACHED);
            }
            else if (IS_PATCH(BINDING(v)))
                assert(index == 1);
            else
                assert(index != 0);
        }
        else
            assert(VAL_WORD_PRIMARY_INDEX_UNCHECKED(v) == 0);
        break; }

      case REB_ACTION: {
        assert((v->header.bits & CELL_MASK_ACTION) == CELL_MASK_ACTION);

        REBACT *a = VAL_ACTION(v);
        assert(Is_Marked(a));
        assert(Is_Marked(VAL_ACTION_SPECIALTY_OR_LABEL(v)));

        // Make sure the [0] slot of the paramlist holds an archetype that is
        // consistent with the paramlist itself.
        //
        REBVAL *archetype = ACT_ARCHETYPE(a);
        assert(a == VAL_ACTION(archetype));
        break; }

      case REB_QUOTED:
        //
        // REB_QUOTED should not be contained in a quoted; instead, the
        // depth of the existing literal should just have been incremented.
        //
        panic ("REB_QUOTED with (KIND3Q_BYTE() % REB_64) > 0");

    //=//// BEGIN INTERNAL TYPES ////////////////////////////////////////=//

      case REB_CUSTOM:  // !!! Might it have an "integrity check" hook?
        break;

      default:
        panic (v);
    }

    enum Reb_Kind kind = CELL_KIND(cast(REBCEL(const*), v));
    switch (kind) {
      case REB_NULL:
        assert(heart == REB_NULL or heart == REB_BLANK);  // may be "isotope"
        break;

      case REB_TUPLE:
      case REB_THE_TUPLE:
      case REB_SET_TUPLE:
      case REB_GET_TUPLE:
      case REB_META_TUPLE:
      case REB_PATH:
      case REB_THE_PATH:
      case REB_SET_PATH:
      case REB_GET_PATH:
      case REB_META_PATH:
         assert(
            heart == REB_BYTES
            or heart == REB_WORD
            or heart == REB_GET_WORD
            or heart == REB_GET_GROUP
            or heart == REB_GET_BLOCK
            or heart == REB_META_WORD
            or heart == REB_META_GROUP
            or heart == REB_META_BLOCK
            or heart == REB_BLOCK
         );
         break;

      case REB_ISSUE: {
        if (heart == REB_TEXT) {
            const REBSER *s = VAL_STRING(v);
            assert(Is_Series_Frozen(s));

            // We do not want ISSUE!s to use series if the payload fits in
            // a cell.  It would offer some theoretical benefits for reuse,
            // e.g. an `as text! as issue! "foo"` would share the same
            // small series...the way it would share a larger one.  But this
            // fringe-ish benefit comes at the cost of keeping a GC reference
            // live on something that doesn't need to be live, and also makes
            // the invariants more complex.
            //
            assert(SER_USED(s) + 1 > sizeof(PAYLOAD(Bytes, v).at_least_8));
        }
        else {
            assert(heart == REB_BYTES);
            assert(NOT_CELL_FLAG(v, FIRST_IS_NODE));
        }
        break; }

      default:
        if (kind < REB_MAX)  // psuedotypes for parameter are actually typeset
           assert(kind == heart);
        break;
    }
}


//
//  Assert_Array_Marked_Correctly: C
//
// This code used to be run in the GC because outside of the flags dictating
// what type of array it was, it didn't know whether it needed to mark the
// LINK() or MISC(), or which fields had been assigned to correctly use for
// reading back what to mark.  This has been standardized.
//
void Assert_Array_Marked_Correctly(const REBARR *a) {
    assert(Is_Marked(a));

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
        assert(not IS_FREE_NODE(a));
        assert(IS_SER_ARRAY(a));
    #endif

    if (IS_DETAILS(a)) {
        const RELVAL *archetype = ARR_HEAD(a);
        assert(IS_ACTION(archetype));
        assert(VAL_ACTION_BINDING(archetype) == UNBOUND);

        // These queueings cannot be done in Queue_Mark_Function_Deep
        // because of the potential for overflowing the C stack with calls
        // to Queue_Mark_Function_Deep.

        REBARR *details = ACT_DETAILS(VAL_ACTION(archetype));
        assert(Is_Marked(details));

        REBARR *list = ACT_SPECIALTY(VAL_ACTION(archetype));
        if (IS_PARTIALS(list))
            list = CTX_VARLIST(LINK(PartialsExemplar, list));
        assert(IS_VARLIST(list));
    }
    else if (IS_VARLIST(a)) {
        const REBVAL *archetype = CTX_ARCHETYPE(CTX(m_cast(REBARR*, a)));

        // Currently only FRAME! archetypes use binding
        //
        assert(ANY_CONTEXT(archetype));
        assert(
            BINDING(archetype) == UNBOUND
            or VAL_TYPE(archetype) == REB_FRAME
        );

        // These queueings cannot be done in Queue_Mark_Context_Deep
        // because of the potential for overflowing the C stack with calls
        // to Queue_Mark_Context_Deep.

        REBNOD *keysource = LINK(KeySource, a);
        if (not keysource) {
            assert(VAL_TYPE(archetype) == REB_MODULE);
        }
        else if (Is_Node_Cell(keysource)) {
            //
            // Must be a FRAME! and it must be on the stack running.  If
            // it has stopped running, then the keylist must be set to
            // UNBOUND which would not be a cell.
            //
            // There's nothing to mark for GC since the frame is on the
            // stack, which should preserve the function paramlist.
            //
            assert(IS_FRAME(archetype));
        }
        else {
            REBSER *keylist = SER(keysource);
            assert(IS_KEYLIST(keylist));

            if (IS_FRAME(archetype)) {
                // Frames use paramlists as their "keylist", there is no
                // place to put an ancestor link.
            }
            else {
                REBSER *ancestor = LINK(Ancestor, keylist);
                UNUSED(ancestor);  // maybe keylist
            }
        }
    }
    else if (IS_PAIRLIST(a)) {
        //
        // There was once a "small map" optimization that wouldn't
        // produce a hashlist for small maps and just did linear search.
        // @giuliolunati deleted that for the time being because it
        // seemed to be a source of bugs, but it may be added again...in
        // which case the hashlist may be NULL.
        //
        REBSER *hashlist = LINK(Hashlist, a);
        assert(SER_FLAVOR(hashlist) == FLAVOR_HASHLIST);
        UNUSED(hashlist);
    }
}

#endif
