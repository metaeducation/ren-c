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

#undef panic
#define panic \
    STATIC_FAIL(dont_use_panic_in_this_file_use_crash_or_assert)

#if RUNTIME_CHECKS

static bool Not_Base_Accessible_Canon(const Base* b) {
    if (Is_Base_Readable(b))
        return false;
    assert(b == &PG_Inaccessible_Stub);
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

    attempt {
        if (not Is_Bindable_Heart(heart))
            break;

        Context* binding = Cell_Binding(v);
        if (not binding)
            break;

        if (Not_Base_Accessible_Canon(binding))
            break;

        assert(Is_Base_Managed(binding));
        assert(Stub_Holds_Cells(binding));

        if (not Is_Stub_Varlist(binding))
            break;

        if (CTX_TYPE(cast(VarList*, binding)) != TYPE_FRAME)
            break;

        KeyList* keylist = Bonus_Keylist(cast(VarList*, binding));
        if (
            (keylist->header.bits & STUB_MASK_KEYLIST)
            != STUB_MASK_KEYLIST
        ){
            crash (binding);
        }

        if (Not_Base_Managed(keylist))
            crash (keylist);;
    }

    // This switch was originally done via contiguous TYPE_XXX values, in order
    // to facilitate use of a "jump table optimization":
    //
    // http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables
    //
    // Since this is debug-only, it's not as important any more.  But it
    // still can speed things up to go in order.
    //
    switch (opt heart) {
      case TYPE_0_constexpr:
        if (Is_Cell_Erased(v)) {  // legal if Mark_Maybe_Erased() was called
            NOOP;
        }
        else {  // it's an extension type
            if (Not_Cell_Flag(v, DONT_MARK_PAYLOAD_1))
                assert(Is_Base_Marked(CELL_PAYLOAD_1(v)));
            if (Not_Cell_Flag(v, DONT_MARK_PAYLOAD_2))
                assert(Is_Base_Marked(CELL_PAYLOAD_2(v)));
        }
        break;

      case TYPE_COMMA:
        break;

      case TYPE_INTEGER:
      case TYPE_DECIMAL:
      case TYPE_PERCENT:
      case TYPE_MONEY:
        break;

      case TYPE_EMAIL:
      case TYPE_URL:
      case TYPE_RUNE: {
        if (Stringlike_Has_Stub(v)) {
            const Flex* f = Cell_Strand(v);
            assert(Is_Flex_Frozen(f));

            assert(Flex_Used(f) + 1 > Size_Of(v->payload.at_least_8));
        }
        else {
            // it's bytes
        }
        break; }

      case TYPE_PAIR: {
        Pairing* pairing = cast(Pairing*, PAIRLIKE_PAYLOAD_1_PAIRING_BASE(v));
        assert(Is_Base_Marked(pairing));
        break; }

      case TYPE_TIME:
      case TYPE_DATE:
        break;

      case TYPE_PARAMETER: {
        if (Parameter_Spec(v))
            assert(Is_Base_Marked(unwrap Parameter_Spec(v)));
        if (Parameter_Strand(v))
            assert(Is_Base_Marked(unwrap Parameter_Strand(v)));
        break; }

      case TYPE_BITSET: {
        assert(Cell_Payload_1_Needs_Mark(v));
        if (Not_Base_Accessible_Canon(CELL_BITSET_BINARY(v)))
            break;
        const Flex* f = cast(Flex*, CELL_BITSET_BINARY(v));
        Assert_Flex_Term_Core(f);
        assert(Is_Base_Marked(f));
        break; }

      case TYPE_MAP: {
        assert(Cell_Payload_1_Needs_Mark(v));
        const Map* map = VAL_MAP(v);
        assert(Is_Base_Marked(map));
        assert(Stub_Holds_Cells(MAP_PAIRLIST(map)));
        break; }

      case TYPE_HANDLE: { // See %sys-handle.h
        if (not Cell_Payload_1_Needs_Mark(v)) {
            // simple handle, no GC interaction
        }
        else {
            // Handle was created with Init_Handle_XXX_Managed.  It holds a
            // singular array containing exactly one handle, and the actual
            // data for the handle lives in that shared location.

            Stub* stub = Extract_Cell_Handle_Stub(v);
            assert(Is_Base_Marked(stub));

            Element* single = Known_Element(Stub_Cell(stub));
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

      case TYPE_OPAQUE:  // nothing to check
        break;

      case TYPE_BLOB: {
        assert(Cell_Payload_1_Needs_Mark(v));
        if (Not_Base_Accessible_Canon(SERIESLIKE_PAYLOAD_1_BASE(v)))
            break;

        const Binary* b = cast(Binary*, SERIESLIKE_PAYLOAD_1_BASE(v));
        assert(Stub_Holds_Bytes(b));
        Assert_Flex_Term_If_Needed(b);
        assert(Is_Base_Marked(b));
        break; }

      case TYPE_TEXT:
      case TYPE_FILE:
      case TYPE_TAG: {
        if (Not_Base_Accessible_Canon(SERIESLIKE_PAYLOAD_1_BASE(v)))
            break;

        const Strand* s = cast(Strand*, SERIESLIKE_PAYLOAD_1_BASE(v));
        Assert_Flex_Term_If_Needed(s);

        assert(Stub_Holds_Bytes(s));
        assert(Is_Base_Marked(s));

        if (not Is_Strand_Symbol(s)) {
            BookmarkList* book = opt Link_Bookmarks(s);
            if (book) {
                assert(Flex_Used(book) == 1);  // just one for now
                //
                // The intent is that bookmarks are unmanaged stubs, which
                // get freed when the string GCs.
                //
                assert(not Is_Base_Marked(book));
                assert(Not_Base_Managed(book));
            }
        }
        break; }

    //=//// BEGIN BINDABLE TYPES ////////////////////////////////////////=//

      case TYPE_FRAME: {
        Base* base = CELL_FRAME_PAYLOAD_1_PHASE(v);
        if (not Is_Base_Readable(base))  // e.g. EVAL-FREE freed it
            break;
        if (Is_Stub_Varlist(cast(Stub*, base)))
            goto mark_object;

        Details* details = cast(Details*, base);
        assert(Is_Base_Marked(details));
        if (CELL_FRAME_EXTRA_LENS_OR_LABEL(v))
            assert(Is_Base_Marked(CELL_FRAME_EXTRA_LENS_OR_LABEL(v)));

        // We used to check the [0] slot of the details holds an archetype
        // that is consistent with the details itself.  That is no longer true
        // (by design), see HIJACK and COPY of actions for why.
        //
        Value* archetype = Phase_Archetype(details);
        assert(Is_Frame(archetype));
        break; }

      mark_object:
      case TYPE_OBJECT:
      case TYPE_WARNING:
      case TYPE_PORT: {
        Base* base = CELL_CONTEXT_VARLIST(v);
        if (Not_Base_Accessible_Canon(base))
            break;

        VarList* context = cast(VarList*, base);
        assert(Is_Base_Marked(context));

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
                    assert(Frame_Coupling(v) == Level_Coupling(L)); */
            }
            else
                assert(Is_Stub_Let(Compact_Stub_From_Cell(v)));
        }

        if (not v->payload.split.two.base)
            assert(Get_Cell_Flag(v, DONT_MARK_PAYLOAD_2));
        else {
            assert(Not_Cell_Flag(v, DONT_MARK_PAYLOAD_2));
            assert(heart == TYPE_FRAME); // may be heap-based frame
            assert(Is_Base_Marked(v->payload.split.two.base));  // lens/label
        }

        const Value* archetype = Varlist_Archetype(context);
        possibly(Cell_Varlist(archetype) == context);  // no longer a rule
        assert(CTX_TYPE(context) == heart);  // but this still is
        UNUSED(archetype);

        // Note: for VAL_CONTEXT_FRAME, the FRM_CALL is either on the stack
        // (in which case it's already taken care of for marking) or it
        // has gone bad, in which case it should be ignored.

        break; }

      case TYPE_LET:  // add checks
        break;

      case TYPE_MODULE:  // add checks
        break;

      case TYPE_VARARGS: {
        assert((v->header.bits & CELL_MASK_VARARGS) == CELL_MASK_VARARGS);
        Phase* phase = Extract_Cell_Varargs_Phase(v);
        if (phase)  // null if came from MAKE VARARGS!
            assert(Is_Base_Marked(phase));
        break; }

      case TYPE_BLOCK:
      case TYPE_FENCE:
      case TYPE_GROUP: {
        if (Not_Base_Accessible_Canon(SERIESLIKE_PAYLOAD_1_BASE(v)))
            break;

        const Array* a = cast(Array*, SERIESLIKE_PAYLOAD_1_BASE(v));
        Assert_Flex_Term_If_Needed(a);
        assert(Is_Base_Marked(a));

        if (LIFT_BYTE(v) == ANTIFORM_1) {
            if (heart == TYPE_FENCE) {
                const Value* value = cast(Value*, v);
                assert(
                    Datatype_Type(value)
                    == Datatype_Type_Slow_Debug(value)
                );
            }
        }
        break; }

      case TYPE_TUPLE:
      case TYPE_CHAIN:
      case TYPE_PATH:
        goto any_sequence;

      any_sequence: {
        if (not Sequence_Has_Pointer(v))
            break;  // should be just bytes

        const Base* payload1 = CELL_PAYLOAD_1(v);
        assert(Is_Base_Marked(payload1));
        break; }

      case TYPE_WORD: {
        assert(Cell_Payload_1_Needs_Mark(v));

        const Symbol *sym = Word_Symbol(v);
        assert(Is_Flex_Frozen(sym));

        assert(Is_Base_Marked(sym));

        // GC can't run during bind
        //
        assert(Not_Flavor_Flag(SYMBOL, sym, HITCH_IS_BIND_STUMP));

        if (Cell_Payload_2_Needs_Mark(v)) {
            Stub* stub = u_cast(Stub*, CELL_PAYLOAD_2(v));
            assert(Is_Stub_Let(stub) or Is_Stub_Patch(stub));
        }
        else
            possibly(CELL_WORD_INDEX_I32(v) == 0);
        break; }

      default:
        crash (v);
    }
}


//
//  Assert_Array_Marked_Correctly: C
//
void Assert_Array_Marked_Correctly(const Array* a) {
    assert(Is_Base_Marked(a));

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
        assert(Is_Base_Readable(a));
        assert(Stub_Holds_Cells(a));
    #endif

    if (Is_Stub_Details(a)) {
        const Element* archetype = Array_Head(a);
        assert(Is_Frame(archetype));

        // These queueings cannot be done in Queue_Mark_Function_Deep
        // because of the potential for overflowing the C stack with calls
        // to Queue_Mark_Function_Deep.

        Phase* arch_phase = Frame_Phase(archetype);
        assert(Is_Base_Marked(arch_phase));
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
            archetype->extra.base == nullptr
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
