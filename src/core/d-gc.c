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
    (NOD(n)->header.bits & NODE_FLAG_MARKED)


//
//  Assert_Cell_Marked_Correctly: C
//
// Note: We assume the binding was marked correctly if the type was bindable.
//
void Assert_Cell_Marked_Correctly(const RELVAL *v)
{
    if (KIND_BYTE_UNCHECKED(v) == REB_QUOTED) {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        assert(MIRROR_BYTE(v) == REB_QUOTED);
        assert(Is_Marked(PAYLOAD(Any, v).first.node));
        return;
    }

    enum Reb_Kind heart = CELL_HEART(cast(REBCEL(const*), v));

    REBNOD *binding;
    if (
        IS_BINDABLE_KIND(heart)
        and (binding = VAL_BINDING(v))
        and NOT_SERIES_INFO(binding, INACCESSIBLE)
    ){
        if (not Is_Node_Cell(binding)) {
            assert(IS_SER_ARRAY(binding));
            if (
                GET_ARRAY_FLAG(binding, IS_VARLIST)
                and (CTX_TYPE(CTX(binding)) == REB_FRAME)
            ){
                if (
                    (binding->header.bits & SERIES_MASK_VARLIST)
                    != SERIES_MASK_VARLIST
                ){
                    panic (binding);
                }
                REBNOD *keysource = LINK_KEYSOURCE(binding);
                if (
                    not Is_Node_Cell(keysource)
                    and GET_ARRAY_FLAG(keysource, IS_PARAMLIST)
                ){
                    if (
                        (keysource->header.bits & SERIES_MASK_PARAMLIST)
                        != SERIES_MASK_PARAMLIST
                    ){
                        panic (binding);
                    }
                    if (NOT_SERIES_FLAG(keysource, MANAGED))
                        panic (keysource);
                }
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
      case REB_NULLED:
      case REB_VOID:
      case REB_BLANK:
        break;

      case REB_LOGIC:
      case REB_INTEGER:
      case REB_DECIMAL:
      case REB_PERCENT:
      case REB_MONEY:
        break;

      case REB_CHAR:
        assert(VAL_CHAR_ENCODED_SIZE(v) <= 4);
        break;

      case REB_PAIR: {
        REBVAL *paired = VAL(VAL_NODE(v));
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

      case REB_TYPESET: // !!! Currently just 64-bits of bitset
        break;

      case REB_BITSET: {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        REBSER *s = SER(PAYLOAD(Any, v).first.node);
        if (GET_SERIES_INFO(s, INACCESSIBLE))
            assert(Is_Marked(s));  // TBD: clear out reference and GC `s`?
        else
            assert(Is_Marked(s));
        break; }

      case REB_MAP: {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        const REBMAP* map = VAL_MAP(v);
        assert(Is_Marked(map));
        assert(IS_SER_ARRAY(map));
        break; }

      case REB_HANDLE: { // See %sys-handle.h
        REBARR *a = VAL_HANDLE_SINGULAR(v);
        if (not a) {  // simple handle, no GC interaction
            assert(not (v->header.bits & CELL_FLAG_FIRST_IS_NODE));
        }
        else {
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
        REBNOD *n = PAYLOAD(Any, v).first.node;  // REBGOB*, REBREQ*, etc.
        assert(n == nullptr or n->header.bits & NODE_FLAG_NODE);
        assert(Is_Marked(PAYLOAD(Any, v).first.node));
        break; }

      case REB_BINARY: {
        REBBIN *s = SER(PAYLOAD(Any, v).first.node);
        if (GET_SERIES_INFO(s, INACCESSIBLE))
            break;

        assert(SER_WIDE(s) == sizeof(REBYTE));
        ASSERT_SERIES_TERM(s);
        assert(Is_Marked(s));
        break; }

      case REB_TEXT:
      case REB_FILE:
      case REB_EMAIL:
      case REB_URL:
      case REB_TAG:
      case REB_ISSUE: {
        if (GET_SERIES_INFO(PAYLOAD(Any, v).first.node, INACCESSIBLE))
            break;

        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        const REBSER *s = VAL_SERIES(v);

        assert(SER_WIDE(s) == sizeof(REBYTE));
        assert(Is_Marked(s));

        if (not IS_STR_SYMBOL(STR(s))) {
            REBBMK *bookmark = LINK(s).bookmarks;
            if (bookmark) {
                assert(not LINK(bookmark).bookmarks);  // just one for now
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
        if (GET_SERIES_INFO(PAYLOAD(Any, v).first.node, INACCESSIBLE))
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
        if (EXTRA(Binding, v).node != UNBOUND) {
            assert(CTX_TYPE(context) == REB_FRAME);
            struct Reb_Frame *f = CTX_FRAME_IF_ON_STACK(context);
            if (f)  // comes from execution, not MAKE FRAME!
                assert(VAL_BINDING(v) == FRM_BINDING(f));
        }

        REBACT *phase = ACT(PAYLOAD(Any, v).second.node);
        if (phase) {
            assert(heart == REB_FRAME); // may be heap-based frame
            assert(Is_Marked(phase));
        }
        else
            assert(heart != REB_FRAME); // phase if-and-only-if frame

        if (GET_SERIES_INFO(context, INACCESSIBLE))
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
      case REB_SET_BLOCK:
      case REB_GET_BLOCK:
      case REB_SYM_BLOCK:
      case REB_GROUP:
      case REB_SET_GROUP:
      case REB_GET_GROUP:
      case REB_SYM_GROUP: {
        if (GET_SERIES_INFO(PAYLOAD(Any, v).first.node, INACCESSIBLE))
            break;

        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        const REBARR *a = VAL_ARRAY(v);
        assert(Is_Marked(a));
        break; }

      case REB_TUPLE:
      case REB_SET_TUPLE:
      case REB_GET_TUPLE:
      case REB_SYM_TUPLE:
        goto any_sequence;

      case REB_PATH:
      case REB_SET_PATH:
      case REB_GET_PATH:
      case REB_SYM_PATH:
        goto any_sequence;

      any_sequence: {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        REBARR *a = ARR(PAYLOAD(Any, v).first.node);
        assert(NOT_SERIES_INFO(a, INACCESSIBLE));

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
        RELVAL *item = ARR_HEAD(a);
        for (; NOT_END(item); ++item)
            assert(not ANY_PATH_KIND(KIND_BYTE_UNCHECKED(item)));
        assert(Is_Marked(a));
        break; }

      case REB_WORD:
      case REB_SET_WORD:
      case REB_GET_WORD:
      case REB_SYM_WORD: {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));

        const REBSTR *spelling = STR(PAYLOAD(Any, v).first.node);

        // A word marks the specific spelling it uses, but not the canon
        // value.  That's because if the canon value gets GC'd, then
        // another value might become the new canon during that sweep.
        //
        assert(Is_Marked(spelling));

        assert(  // GC can't run during binding, only time bind indices != 0
            NOT_SERIES_INFO(spelling, STRING_CANON)
            or (
                MISC(spelling).bind_index.high == 0
                and MISC(spelling).bind_index.low == 0
            )
        );

        if (IS_WORD_BOUND(v)) {
            assert(PAYLOAD(Any, v).second.i32 > 0);
        }
        else {
            // The word is unbound...make sure index is 0 in debug build.
            // (it can be left uninitialized in release builds, for now)
            //
            assert(PAYLOAD(Any, v).second.i32 == -1);
        }
        break; }

      case REB_ACTION: {
        assert((v->header.bits & CELL_MASK_ACTION) == CELL_MASK_ACTION);

        REBACT *a = VAL_ACTION(v);
        REBARR *paramlist = ACT_PARAMLIST(a);
        assert(Is_Marked(paramlist));

        assert(Is_Marked(PAYLOAD(Any, v).second.node));

        // Make sure the [0] slot of the paramlist holds an archetype that is
        // consistent with the paramlist itself.
        //
        REBVAL *archetype = ACT_ARCHETYPE(a);
        assert(paramlist == VAL_ACT_PARAMLIST(archetype));
        break; }

      case REB_QUOTED:
        //
        // REB_QUOTED should not be contained in a quoted; instead, the
        // depth of the existing literal should just have been incremented.
        //
        panic ("REB_QUOTED with (KIND_BYTE() % REB_64) > 0");

    //=//// BEGIN INTERNAL TYPES ////////////////////////////////////////=//

      case REB_P_NORMAL:
      case REB_P_HARD_QUOTE:
      case REB_P_MODAL:
      case REB_P_SOFT_QUOTE:
      case REB_P_LOCAL: {
        REBSTR *s = VAL_TYPESET_STRING(v);
        assert(Is_Marked(s));
        assert(MIRROR_BYTE(v) == REB_TYPESET);
        break; }

      case REB_G_XYF:
        //
        // This is a compact type that stores floats in the payload, and
        // miscellaneous information in the extra.  None of it needs GC
        // awareness--the cells that need GC awareness use ordinary values.
        // It's to help pack all the data needed for the GOB! into one
        // allocation and still keep it under 8 cells in size, without
        // having to get involved with using HANDLE!.
        //
        break;

      case REB_V_SIGN_INTEGRAL_WIDE:
        //
        // Similar to the above.  Since it has no GC behavior and the caller
        // knows where these cells are (stealing space in an array) there is
        // no need for a unique type, but it may help in debugging if these
        // values somehow escape their "details" arrays.
        //
        break;

      case REB_CUSTOM:  // !!! Might it have an "integrity check" hook?
        break;

      default:
        panic (v);
    }

    enum Reb_Kind kind = CELL_TYPE(cast(REBCEL(const*), v));
    switch (kind) {
      case REB_TUPLE:
      case REB_SET_TUPLE:
      case REB_GET_TUPLE:
      case REB_SYM_TUPLE:
      case REB_PATH:
      case REB_SET_PATH:
      case REB_GET_PATH:
      case REB_SYM_PATH:
         assert(
            heart == REB_CHAR
            or heart == REB_WORD
            or heart == REB_GET_WORD
            or heart == REB_SYM_WORD
            or heart == REB_BLOCK
         );
         break;

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

    if (GET_ARRAY_FLAG(a, IS_PARAMLIST)) {
        const RELVAL *archetype = ARR_HEAD(a);
        assert(IS_ACTION(archetype));
        assert(not EXTRA(Binding, archetype).node);

        // These queueings cannot be done in Queue_Mark_Function_Deep
        // because of the potential for overflowing the C stack with calls
        // to Queue_Mark_Function_Deep.

        REBARR *details = ACT_DETAILS(VAL_ACTION(archetype));
        assert(Is_Marked(details));

        REBARR *specialty = LINK_SPECIALTY(details);
        if (GET_ARRAY_FLAG(specialty, IS_VARLIST)) {
            REBCTX *ctx_specialty = CTX(specialty);
            UNUSED(ctx_specialty);
        }
        else
            assert(specialty == a);
    }
    else if (GET_ARRAY_FLAG(a, IS_VARLIST)) {
        const REBVAL *archetype = CTX_ARCHETYPE(CTX(a));

        // Currently only FRAME! archetypes use binding
        //
        assert(ANY_CONTEXT(archetype));
        assert(
            not EXTRA(Binding, archetype).node
            or VAL_TYPE(archetype) == REB_FRAME
        );

        // These queueings cannot be done in Queue_Mark_Context_Deep
        // because of the potential for overflowing the C stack with calls
        // to Queue_Mark_Context_Deep.

        REBNOD *keysource = LINK_KEYSOURCE(a);
        if (Is_Node_Cell(keysource)) {
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
            REBARR *keylist = ARR(keysource);
            if (IS_FRAME(archetype)) {
                assert(GET_ARRAY_FLAG(keylist, IS_PARAMLIST));

                // Frames use paramlists as their "keylist", there is no
                // place to put an ancestor link.
            }
            else {
                assert(NOT_ARRAY_FLAG(keylist, IS_PARAMLIST));
                ASSERT_UNREADABLE_IF_DEBUG(ARR_HEAD(keylist));

                REBARR *ancestor = LINK_ANCESTOR(keylist);
                UNUSED(ancestor);  // maybe keylist
            }
        }
    }
    else if (GET_ARRAY_FLAG(a, IS_PAIRLIST)) {
        //
        // There was once a "small map" optimization that wouldn't
        // produce a hashlist for small maps and just did linear search.
        // @giuliolunati deleted that for the time being because it
        // seemed to be a source of bugs, but it may be added again...in
        // which case the hashlist may be NULL.
        //
        REBSER *hashlist = LINK_HASHLIST(a);
        assert(hashlist != nullptr);
        UNUSED(hashlist);
    }
}

#endif
