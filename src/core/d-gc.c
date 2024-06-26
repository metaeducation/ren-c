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

static bool Not_Node_Accessible_Canon(const Node* n) {
    if (Not_Node_Free(n))
        return false;
    assert(n == &PG_Inaccessible_Stub);
    return true;
}

//
//  Assert_Cell_Marked_Correctly: C
//
// Note: We assume the binding was marked correctly if the type was bindable.
//
void Assert_Cell_Marked_Correctly(const Cell* v)
{
    ASSERT_CELL_READABLE(v);  // then we use unchecked() on v below

    Heart heart = Cell_Heart_Unchecked(v);

    while (Is_Bindable_Heart(heart)) {  // for `break` convenience
        Stub* binding = BINDING(v);
        if (not binding)
            break;

        if (Not_Node_Accessible_Canon(binding))
            break;

        assert(Is_Node_Managed(binding));
        assert(Is_Series_Array(binding));

        if (not IS_VARLIST(binding))
            break;

        if (CTX_TYPE(cast(Context*, binding)) != REB_FRAME)
            break;

        Node* keysource = BONUS(KeySource, binding);
        if (Is_Node_A_Cell(keysource))  // actually a Level
            break;

        KeyList* keylist = cast(KeyList*, keysource);
        if (
            (keylist->leader.bits & SERIES_MASK_KEYLIST)
            != SERIES_MASK_KEYLIST
        ){
            panic (binding);
        }
        if (Not_Node_Managed(keysource))
            panic (keysource);
        break;
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
      case 0:  // legal if using Mark_Maybe_Fresh()
        assert(Is_Fresh(v));
        break;

      case REB_BLANK:
      case REB_COMMA:
        break;

      case REB_INTEGER:
      case REB_DECIMAL:
      case REB_PERCENT:
      case REB_MONEY:
        break;

      case REB_SIGIL:
        assert(Not_Cell_Flag_Unchecked(v, STRINGLIKE_HAS_NODE));
        break;

      case REB_ISSUE: {
        if (Get_Cell_Flag_Unchecked(v, STRINGLIKE_HAS_NODE)) {
            const Series* s = Cell_Issue_String(v);
            assert(Is_Series_Frozen(s));

            // We do not want ISSUE!s to use series if the payload fits in
            // a cell.  It would offer some theoretical benefits for reuse,
            // e.g. an `as text! as issue! "foo"` would share the same
            // small series...the way it would share a larger one.  But this
            // fringe-ish benefit comes at the cost of keeping a GC reference
            // live on something that doesn't need to be live, and also makes
            // the invariants more complex.
            //
            assert(Series_Used(s) + 1 > sizeof(PAYLOAD(Bytes, v).at_least_8));
        }
        else {
            // it's bytes
        }
        break; }

      case REB_PAIR: {
        Value* paired = x_cast(Value*, Cell_Node1(v));
        assert(Is_Node_Marked(paired));
        break; }

      case REB_TIME:
      case REB_DATE:
        break;

      case REB_PARAMETER: {
        if (Cell_Parameter_Spec(v))
            assert(Is_Node_Marked(unwrap(Cell_Parameter_Spec(v))));
        break; }

      case REB_BITSET: {
        assert(Get_Cell_Flag_Unchecked(v, FIRST_IS_NODE));
        if (Not_Node_Accessible_Canon(Cell_Node1(v)))
            break;
        Series* s = cast(Series*, Cell_Node1(v));
        Assert_Series_Term_Core(s);
        assert(Is_Node_Marked(s));
        break; }

      case REB_MAP: {
        assert(Get_Cell_Flag_Unchecked(v, FIRST_IS_NODE));
        const Map* map = VAL_MAP(v);
        assert(Is_Node_Marked(map));
        assert(Is_Series_Array(MAP_PAIRLIST(map)));
        break; }

      case REB_HANDLE: { // See %sys-handle.h
        if (Not_Cell_Flag_Unchecked(v, FIRST_IS_NODE)) {
            // simple handle, no GC interaction
        }
        else {
            Stub* stub = VAL_HANDLE_STUB(v);

            // Handle was created with Init_Handle_XXX_Managed.  It holds a
            // singular array containing exactly one handle, and the actual
            // data for the handle lives in that shared location.
            //
            assert(v->header.bits & CELL_FLAG_FIRST_IS_NODE);
            assert(Is_Node_Marked(stub));

            Value* single = Stub_Cell(stub);
            assert(Is_Handle(single));
            assert(VAL_HANDLE_STUB(single) == stub);
            if (v != single) {
                //
                // In order to make it clearer that individual handles do not
                // hold the shared data (there'd be no way to update all the
                // references at once), the data pointers in all but the
                // shared singular value are NULL.
                //
                // (Corrupt not used because release build complains about lack
                // of initialization, so null is always used)
                //
                assert(VAL_HANDLE_CDATA_P(v) == nullptr);
            }
        }
        break; }

      case REB_BINARY: {
        assert(Get_Cell_Flag_Unchecked(v, FIRST_IS_NODE));
        if (Not_Node_Accessible_Canon(Cell_Node1(v)))
            break;

        Binary* s = cast(Binary*, Cell_Node1(v));
        assert(Series_Wide(s) == sizeof(Byte));
        Assert_Series_Term_If_Needed(s);
        assert(Is_Node_Marked(s));
        break; }

      case REB_TEXT:
      case REB_FILE:
      case REB_EMAIL:
      case REB_URL:
      case REB_TAG: {
        assert(Get_Cell_Flag_Unchecked(v, FIRST_IS_NODE));
        if (Not_Node_Accessible_Canon(Cell_Node1(v)))
            break;

        const String* s = c_cast(String*, Cell_Node1(v));
        Assert_Series_Term_If_Needed(s);

        assert(Series_Wide(s) == sizeof(Byte));
        assert(Is_Node_Marked(s));

        if (Is_String_NonSymbol(s)) {
            BookmarkList* book = LINK(Bookmarks, s);
            if (book) {
                assert(Series_Used(book) == 1);  // just one for now
                //
                // The intent is that bookmarks are unmanaged stubs, which
                // get freed when the string GCs.
                //
                assert(not Is_Node_Marked(book));
                assert(Not_Node_Managed(book));
            }
        }
        break; }

    //=//// BEGIN BINDABLE TYPES ////////////////////////////////////////=//

      case REB_FRAME:
        if (Is_Frame_Exemplar(v))
            goto mark_object;
        {
        assert((v->header.bits & CELL_MASK_FRAME) == CELL_MASK_FRAME);

        Phase* a = cast(Phase*, VAL_ACTION(v));
        assert(Is_Node_Marked(a));
        if (VAL_ACTION_PARTIALS_OR_LABEL(v))
            assert(Is_Node_Marked(VAL_ACTION_PARTIALS_OR_LABEL(v)));

        if (Is_Action_Native(a)) {
            Details* details = Phase_Details(a);
            assert(Array_Len(details) >= IDX_NATIVE_MAX);
            Value* body = Details_At(details, IDX_NATIVE_BODY);
            Value* context = Details_At(details, IDX_NATIVE_CONTEXT);
            assert(
                Is_Blank(body)
                or Is_Handle(body)  // Intrinsics use the slot for Intrinsic*
                or Is_Text(body)  // TCC uses the slot for "source"
                or Is_Word(body)  // GENERIC uses the slot for the "verb"
            );
            assert(Any_Context(context));
        }

        // We used to check the [0] slot of the details holds an archetype
        // that is consistent with the details itself.  That is no longer true
        // (by design), see HIJACK and COPY of actions for why.
        //
        Value* archetype = Phase_Archetype(a);
        assert(Is_Frame(archetype));
        break; }

      mark_object:
      case REB_OBJECT:
      case REB_MODULE:
      case REB_ERROR:
      case REB_PORT: {
        if (Not_Node_Accessible_Canon(Cell_Node1(v)))
            break;

        assert(
            (v->header.bits & CELL_MASK_ANY_CONTEXT)
            == CELL_MASK_ANY_CONTEXT
        );
        Context* context = VAL_CONTEXT(v);
        assert(Is_Node_Marked(context));

        // Currently the "binding" in a context is only used by FRAME! to
        // preserve the binding of the ACTION! value that spawned that
        // frame.  Currently that binding is typically NULL in of a
        // function's Cell unless it is a definitional RETURN.
        //
        // !!! Expanded usages may be found in other situations that mix an
        // archetype with an instance (e.g. an archetypal function body that
        // could apply to any OBJECT!, but the binding cheaply makes it
        // a method for that object.)
        //
        if (BINDING(v) != UNBOUND) {
            if (CTX_TYPE(context) == REB_FRAME) {
                Level* L = CTX_LEVEL_IF_ON_STACK(context);
                if (L)  // comes from execution, not MAKE FRAME!
                    assert(VAL_FRAME_BINDING(v) == Level_Binding(L));
            }
            else
                assert(IS_LET(Singular_From_Cell(v)));
        }

        if (PAYLOAD(Any, v).second.node) {
            assert(heart == REB_FRAME); // may be heap-based frame
            assert(Is_Node_Marked(PAYLOAD(Any, v).second.node));  // phase or label
        }

        const Value* archetype = CTX_ARCHETYPE(context);
        assert(CTX_TYPE(context) == heart);
        assert(VAL_CONTEXT(archetype) == context);

        // Note: for VAL_CONTEXT_FRAME, the FRM_CALL is either on the stack
        // (in which case it's already taken care of for marking) or it
        // has gone bad, in which case it should be ignored.

        break; }

      case REB_VARARGS: {
        assert((v->header.bits & CELL_MASK_VARARGS) == CELL_MASK_VARARGS);
        Action* phase = VAL_VARARGS_PHASE(v);
        if (phase)  // null if came from MAKE VARARGS!
            assert(Is_Node_Marked(phase));
        break; }

      case REB_BLOCK:
      case REB_THE_BLOCK:
      case REB_SET_BLOCK:
      case REB_GET_BLOCK:
      case REB_META_BLOCK:
      case REB_TYPE_BLOCK:
      case REB_VAR_BLOCK:
      case REB_GROUP:
      case REB_THE_GROUP:
      case REB_SET_GROUP:
      case REB_GET_GROUP:
      case REB_META_GROUP:
      case REB_TYPE_GROUP:
      case REB_VAR_GROUP: {
        assert(Get_Cell_Flag_Unchecked(v, FIRST_IS_NODE));
        if (Not_Node_Accessible_Canon(Cell_Node1(v)))
            break;

        Array* a = cast(Array*, Cell_Node1(v));
        Assert_Series_Term_If_Needed(a);
        assert(Is_Node_Marked(a));
        break; }

      case REB_TUPLE:
      case REB_THE_TUPLE:
      case REB_SET_TUPLE:
      case REB_GET_TUPLE:
      case REB_META_TUPLE:
      case REB_TYPE_TUPLE:
      case REB_VAR_TUPLE:
        goto any_sequence;

      case REB_PATH:
      case REB_THE_PATH:
      case REB_SET_PATH:
      case REB_GET_PATH:
      case REB_META_PATH:
      case REB_TYPE_PATH:
      case REB_VAR_PATH:
        goto any_sequence;

      any_sequence: {
        if (Not_Cell_Flag_Unchecked(v, SEQUENCE_HAS_NODE))
            break;  // should be just bytes

        const Node* node1 = Cell_Node1(v);

        if (Is_Node_A_Cell(node1)) {
            // it's a pairing
        }
        else switch (Series_Flavor(x_cast(Stub*, node1))) {
          case FLAVOR_SYMBOL :
            break;

          // With most arrays we may risk direct recursion, hence we have to
          // use Queue_Mark_Array_Deep().  But paths are guaranteed to not have
          // other paths directly in them.  Walk it here so that we can also
          // check that there are no paths embedded.
          //
          // Note: This doesn't catch cases which don't wind up reachable from
          // the root set, e.g. anything that would be GC'd.
          //
          // !!! Optimization abandoned
          //
          case FLAVOR_ARRAY : {
            Assert_Node_Accessible(node1);
            Array* a = x_cast(Array*, node1);

            assert(Array_Len(a) >= 2);
            const Element* tail = Array_Tail(a);
            const Element* item = Array_Head(a);
            for (; item != tail; ++item) {
                assert(not Any_Path_Kind(Cell_Heart_Ensure_Noquote(item)));
            }
            assert(Is_Node_Marked(a));
            break; }

          default:
            panic (v);
        }
        break; }

      case REB_WORD:
      case REB_THE_WORD:
      case REB_SET_WORD:
      case REB_GET_WORD:
      case REB_META_WORD:
      case REB_TYPE_WORD:
      case REB_VAR_WORD: {
        assert(Get_Cell_Flag_Unchecked(v, FIRST_IS_NODE));

        const String *spelling = Cell_Word_Symbol(v);
        assert(Is_Series_Frozen(spelling));

        assert(Is_Node_Marked(spelling));

        // GC can't run during bind
        //
        assert(Not_Subclass_Flag(SYMBOL, spelling, MISC_IS_BINDINFO));

        REBINT index = VAL_WORD_INDEX_I32(v);
        Series* binding = BINDING(v);
        if (binding) {
            if (IS_VARLIST(binding)) {
                if (CTX_TYPE(cast(Context*, binding)) == REB_MODULE)
                    assert(index == INDEX_ATTACHED);
                else
                    assert(index != 0 and index != INDEX_ATTACHED);
            }
            else if (IS_LET(binding))
                assert(index == INDEX_PATCHED);
            else
                assert(index != 0 or IS_DETAILS(binding));
        }
        else
            assert(index == 0);
        break; }

      default:
        panic (v);
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
void Assert_Array_Marked_Correctly(const Array* a) {
    assert(Is_Node_Marked(a));

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
        assert(not Is_Node_Free(a));
        assert(Is_Series_Array(a));
    #endif

    if (IS_DETAILS(a)) {
        const Element* archetype = Array_Head(a);
        assert(Is_Frame(archetype));
        assert(VAL_FRAME_BINDING(archetype) == UNBOUND);

        // These queueings cannot be done in Queue_Mark_Function_Deep
        // because of the potential for overflowing the C stack with calls
        // to Queue_Mark_Function_Deep.

        Details* details = cast(Details*, VAL_ACTION(archetype));
        assert(Is_Node_Marked(details));

        Array* list = CTX_VARLIST(ACT_EXEMPLAR(VAL_ACTION(archetype)));
        assert(IS_VARLIST(list));
    }
    else if (IS_VARLIST(a)) {
        const Value* archetype = CTX_ARCHETYPE(
            cast(Context*, m_cast(Array*, a))
        );

        // Currently only FRAME! archetypes use binding
        //
        assert(Any_Context(archetype));
        assert(
            BINDING(archetype) == UNBOUND
            or VAL_TYPE(archetype) == REB_FRAME
        );

        // These queueings cannot be done in Queue_Mark_Context_Deep
        // because of the potential for overflowing the C stack with calls
        // to Queue_Mark_Context_Deep.

        Node* keysource = BONUS(KeySource, a);
        if (not keysource) {
            assert(VAL_TYPE(archetype) == REB_MODULE);
        }
        else if (Is_Non_Cell_Node_A_Level(keysource)) {
            //
            // Must be a FRAME! and it must be on the stack running.  If
            // it has stopped running, then the keylist must be set to
            // UNBOUND which would not be a cell.
            //
            // There's nothing to mark for GC since the frame is on the
            // stack, which should preserve the function paramlist.
            //
            assert(Is_Frame(archetype));
        }
        else {
            KeyList* keylist = cast(KeyList*, keysource);
            assert(IS_KEYLIST(keylist));

            if (Is_Frame(archetype)) {
                // Frames use paramlists as their "keylist", there is no
                // place to put an ancestor link.
            }
            else {
                KeyList* ancestor = LINK(Ancestor, keylist);
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
        Series* hashlist = LINK(Hashlist, a);
        assert(Series_Flavor(hashlist) == FLAVOR_HASHLIST);
        UNUSED(hashlist);
    }
}

#endif
