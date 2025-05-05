//
//  file: %d-gc.c
//  summary: "Debug-Build Checks for the Garbage Collector"
//  section: debug
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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

#undef fail  /* cannot fail in this file, use assert() and panic() instead */

#if RUNTIME_CHECKS

static bool Not_Node_Accessible_Canon(const Node* n) {
    if (Is_Node_Readable(n))
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
    Assert_Cell_Readable(v);  // then we use unchecked() on v below

    Option(Heart) heart = Unchecked_Heart_Of(v);

    while (Is_Bindable_Heart(heart)) {  // for `break` convenience
        Context* binding = Cell_Binding(v);
        if (not binding)
            break;

        if (Not_Node_Accessible_Canon(binding))
            break;

        assert(Is_Node_Managed(binding));
        assert(Stub_Holds_Cells(binding));

        if (not Is_Stub_Varlist(binding))
            break;

        if (CTX_TYPE(cast(VarList*, binding)) != TYPE_FRAME)
            break;

        KeyList* keylist = Bonus_Keylist(cast(VarList*, binding));
        if (
            (keylist->leader.bits & FLEX_MASK_KEYLIST)
            != FLEX_MASK_KEYLIST
        ){
            panic (binding);
        }
        if (Not_Node_Managed(keylist))
            panic (keylist);
        break;
    }

    // This switch was originally done via contiguous TYPE_XXX values, in order
    // to facilitate use of a "jump table optimization":
    //
    // http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables
    //
    // Since this is debug-only, it's not as important any more.  But it
    // still can speed things up to go in order.
    //
    switch (heart) {
      case HEART_ENUM(0):
        if (Is_Cell_Erased(v)) {  // legal if Mark_Maybe_Erased() was called
            NOOP;
        }
        else {  // it's an extension type
            if (Not_Cell_Flag(v, DONT_MARK_NODE1))
                assert(Is_Node_Marked(CELL_NODE1(v)));
            if (Not_Cell_Flag(v, DONT_MARK_NODE2))
                assert(Is_Node_Marked(CELL_NODE2(v)));
        }
        break;

      case TYPE_BLANK:
      case TYPE_COMMA:
        break;

      case TYPE_INTEGER:
      case TYPE_DECIMAL:
      case TYPE_PERCENT:
      case TYPE_MONEY:
        break;

      case TYPE_SIGIL:
        assert(not Stringlike_Has_Node(v));
        break;

      case TYPE_EMAIL:
      case TYPE_URL:
      case TYPE_ISSUE: {
        if (Stringlike_Has_Node(v)) {
            const Flex* f = Cell_String(v);
            assert(Is_Flex_Frozen(f));

            assert(Flex_Used(f) + 1 > Size_Of(v->payload.at_least_8));
        }
        else {
            // it's bytes
        }
        break; }

      case TYPE_PAIR: {
        Pairing* pairing = x_cast(Pairing*, CELL_PAIRLIKE_PAIRING_NODE(v));
        assert(Is_Node_Marked(pairing));
        break; }

      case TYPE_TIME:
      case TYPE_DATE:
        break;

      case TYPE_PARAMETER: {
        if (Cell_Parameter_Spec(v))
            assert(Is_Node_Marked(unwrap Cell_Parameter_Spec(v)));
        break; }

      case TYPE_BITSET: {
        assert(Cell_Has_Node1(v));
        if (Not_Node_Accessible_Canon(CELL_BITSET_BINARY(v)))
            break;
        const Flex* f = c_cast(Flex*, CELL_BITSET_BINARY(v));
        Assert_Flex_Term_Core(f);
        assert(Is_Node_Marked(f));
        break; }

      case TYPE_MAP: {
        assert(Cell_Has_Node1(v));
        const Map* map = VAL_MAP(v);
        assert(Is_Node_Marked(map));
        assert(Stub_Holds_Cells(MAP_PAIRLIST(map)));
        break; }

      case TYPE_HANDLE: { // See %sys-handle.h
        if (not Cell_Has_Node1(v)) {
            // simple handle, no GC interaction
        }
        else {
            // Handle was created with Init_Handle_XXX_Managed.  It holds a
            // singular array containing exactly one handle, and the actual
            // data for the handle lives in that shared location.

            Stub* stub = Extract_Cell_Handle_Stub(v);
            assert(Is_Node_Marked(stub));

            Value* single = Stub_Cell(stub);
            assert(Is_Handle(single));
            assert(Extract_Cell_Handle_Stub(single) == stub);
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
                assert(CELL_HANDLE_CDATA_P(v) == nullptr);
            }
        }
        break; }

      case TYPE_BLOB: {
        assert(Cell_Has_Node1(v));
        if (Not_Node_Accessible_Canon(CELL_SERIESLIKE_NODE(v)))
            break;

        const Binary* b = c_cast(Binary*, CELL_SERIESLIKE_NODE(v));
        assert(Flex_Wide(b) == sizeof(Byte));
        Assert_Flex_Term_If_Needed(b);
        assert(Is_Node_Marked(b));
        break; }

      case TYPE_TEXT:
      case TYPE_FILE:
      case TYPE_TAG: {
        if (Not_Node_Accessible_Canon(CELL_SERIESLIKE_NODE(v)))
            break;

        const String* s = c_cast(String*, CELL_SERIESLIKE_NODE(v));
        Assert_Flex_Term_If_Needed(s);

        assert(Flex_Wide(s) == sizeof(Byte));
        assert(Is_Node_Marked(s));

        if (not Is_String_Symbol(s)) {
            BookmarkList* book = maybe Link_Bookmarks(s);
            if (book) {
                assert(Flex_Used(book) == 1);  // just one for now
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

      case TYPE_FRAME: {
        Node* node = CELL_FRAME_PHASE(v);
        if (not Is_Node_Readable(node))  // e.g. EVAL-FREE freed it
            break;
        if (Is_Stub_Varlist(cast(Stub*, node)))
            goto mark_object;

        assert((v->header.bits & CELL_MASK_FRAME) == CELL_MASK_FRAME);

        Details* details = cast(Details*, node);
        assert(Is_Node_Marked(details));
        if (CELL_FRAME_LENS_OR_LABEL(v))
            assert(Is_Node_Marked(CELL_FRAME_LENS_OR_LABEL(v)));

        // We used to check the [0] slot of the details holds an archetype
        // that is consistent with the details itself.  That is no longer true
        // (by design), see HIJACK and COPY of actions for why.
        //
        Value* archetype = Phase_Archetype(details);
        assert(Is_Frame(archetype));
        break; }

      mark_object:
      case TYPE_OBJECT:
      case TYPE_ERROR:
      case TYPE_PORT: {
        Node* node = CELL_CONTEXT_VARLIST(v);
        if (Not_Node_Accessible_Canon(node))
            break;

        assert(
            (v->header.bits & CELL_MASK_ANY_CONTEXT)
            == CELL_MASK_ANY_CONTEXT
        );
        VarList* context = cast(VarList*, node);
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
        if (CELL_EXTRA(v) != nullptr) {
            if (CTX_TYPE(context) == TYPE_FRAME) {
                // !!! Needs review
                /*Level* L = Level_Of_Varlist_If_Running(context);
                if (L)  // comes from execution, not MAKE FRAME!
                    assert(Cell_Frame_Coupling(v) == Level_Coupling(L)); */
            }
            else
                assert(Is_Stub_Let(Compact_Stub_From_Cell(v)));
        }

        if (v->payload.split.two.node) {
            assert(heart == TYPE_FRAME); // may be heap-based frame
            assert(Is_Node_Marked(v->payload.split.two.node));  // lens/label
        }

        const Value* archetype = Varlist_Archetype(context);
        possibly(Cell_Varlist(archetype) == context);  // no longer a rule
        assert(CTX_TYPE(context) == heart);  // but this still is

        // Note: for VAL_CONTEXT_FRAME, the FRM_CALL is either on the stack
        // (in which case it's already taken care of for marking) or it
        // has gone bad, in which case it should be ignored.

        break; }

      case TYPE_MODULE:  // add checks
        break;

      case TYPE_VARARGS: {
        assert((v->header.bits & CELL_MASK_VARARGS) == CELL_MASK_VARARGS);
        Phase* phase = Extract_Cell_Varargs_Phase(v);
        if (phase)  // null if came from MAKE VARARGS!
            assert(Is_Node_Marked(phase));
        break; }

      case TYPE_BLOCK:
      case TYPE_THE_BLOCK:
      case TYPE_META_BLOCK:
      case TYPE_VAR_BLOCK:
        //
      case TYPE_FENCE:
      case TYPE_THE_FENCE:
      case TYPE_META_FENCE:
      case TYPE_VAR_FENCE:
        //
      case TYPE_GROUP:
      case TYPE_THE_GROUP:
      case TYPE_META_GROUP:
      case TYPE_VAR_GROUP: {
        if (Not_Node_Accessible_Canon(CELL_SERIESLIKE_NODE(v)))
            break;

        const Array* a = c_cast(Array*, CELL_SERIESLIKE_NODE(v));
        Assert_Flex_Term_If_Needed(a);
        assert(Is_Node_Marked(a));
        break; }

      case TYPE_TUPLE:
      case TYPE_CHAIN:
      case TYPE_PATH:
        goto any_sequence;

      any_sequence: {
        if (not Sequence_Has_Node(v))
            break;  // should be just bytes

        const Node* node1 = CELL_NODE1(v);
        assert(Is_Node_Marked(node1));
        break; }

      case TYPE_WORD:
      case TYPE_THE_WORD:
      case TYPE_META_WORD:
      case TYPE_VAR_WORD: {
        assert(Cell_Has_Node1(v));

        const String *spelling = Cell_Word_Symbol(v);
        assert(Is_Flex_Frozen(spelling));

        assert(Is_Node_Marked(spelling));

        // GC can't run during bind
        //
        assert(Not_Flavor_Flag(SYMBOL, spelling, HITCH_IS_BIND_STUMP));

        REBINT index = CELL_WORD_INDEX_I32(v);
        Context* binding = Cell_Binding(v);
        if (binding) {
            if (Is_Stub_Varlist(binding)) {
                assert(index != 0);
            }
            else if (Is_Stub_Let(binding))
                assert(index == INDEX_PATCHED);
            else
                assert(index != 0 or Is_Stub_Details(binding));
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
void Assert_Array_Marked_Correctly(const Array* a) {
    assert(Is_Node_Marked(a));

    #ifdef HEAVY_CHECKS
        //
        // The GC is a good general hook point that every Flex which has been
        // managed will go through, so it's a good time to assert properties
        // about the Array.
        //
        Assert_Array(a);
    #else
        //
        // For a lighter check, make sure it's marked as a value-bearing array
        // and that it hasn't been diminished.
        //
        assert(Is_Node_Readable(a));
        assert(Stub_Holds_Cells(a));
    #endif

    if (Is_Stub_Details(a)) {
        const Element* archetype = Array_Head(a);
        assert(Is_Frame(archetype));

        // These queueings cannot be done in Queue_Mark_Function_Deep
        // because of the potential for overflowing the C stack with calls
        // to Queue_Mark_Function_Deep.

        Phase* arch_phase = Cell_Frame_Phase(archetype);
        assert(Is_Node_Marked(arch_phase));
        assert(Is_Stub_Varlist(arch_phase) or Is_Stub_Details(arch_phase));
    }
    else if (Is_Stub_Varlist(a)) {
        const Value* archetype = Varlist_Archetype(
            cast(VarList*, m_cast(Array*, a))
        );

        // Currently only FRAME! archetypes use coupling
        //
        assert(Any_Context(archetype));
        assert(
            archetype->extra.node == nullptr
            or Type_Of(archetype) == TYPE_FRAME
        );

        // These queueings cannot be done in Queue_Mark_Context_Deep
        // because of the potential for overflowing the C stack with calls
        // to Queue_Mark_Context_Deep.

        KeyList* keylist = Bonus_Keylist(cast(VarList*, m_cast(Array*, a)));
        assert(Is_Stub_Keylist(keylist));

        if (Is_Frame(archetype)) {
            // Frames use paramlists as their "keylist", there is no
            // place to put an ancestor link.
        }
        else {
            KeyList* ancestor = Link_Keylist_Ancestor(keylist);
            UNUSED(ancestor);  // maybe keylist
        }
    }
    else if (Is_Stub_Pairlist(a)) {
        //
        // There was once a "small map" optimization that wouldn't
        // produce a hashlist for small maps and just did linear search.
        // @giuliolunati deleted that for the time being because it
        // seemed to be a source of bugs, but it may be added again...in
        // which case the hashlist may be NULL.
        //
        HashList* hashlist = Link_Hashlist(a);
        assert(Stub_Flavor(hashlist) == FLAVOR_HASHLIST);
        UNUSED(hashlist);
    }
}

#endif
