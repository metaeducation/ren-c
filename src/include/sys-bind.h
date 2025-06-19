//
//  file: %sys-bind.h
//  summary: "System Binding Include"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// Due to the performance-critical nature of these routines, they are inline
// so that locations using them may avoid overhead in invocation.


//=////////////////////////////////////////////////////////////////////////=//
//
//  COPYING RELATIVE VALUES TO SPECIFIC
//
//=////////////////////////////////////////////////////////////////////////=//
//
// If the Cell is indeed relative and needs to be made specific to be put into
// the target, then the binding is used to do that.
//
// It is nearly as fast as just assigning the value directly in the release
// build, though checked builds assert that the function in the binding
// indeed matches the target in the relative value (because relative values
// in an array may only be relative to the function that deep copied them, and
// that is the only kind of binding you can use with them).
//
// Interface designed to line up with Copy_Cell()
//
// !!! At the moment, there is a fair amount of overlap in this code with
// Get_Context_Core().  One of them resolves a value's real binding and then
// fetches it, while the other resolves a value's real binding but then stores
// that back into another value without fetching it.  This suggests sharing
// a mechanic between both...TBD.
//


INLINE Element* Derelativize_Untracked(
    Sink(Element) out,
    const Element* v,
    Context* context
){
    Copy_Cell_Header(out, v);
    out->payload = v->payload;

    Option(Heart) heart = Unchecked_Heart_Of(v);

    if (
        not context  // should bindings always be left as-is in this case?
        or not Is_Bindable_Heart(heart)
    ){
        out->extra = v->extra;
        return out;
    }

    Context* binding = Cell_Binding(v);

    if (Bindable_Heart_Is_Any_Word(unwrap heart)) {
      any_wordlike:
        if (
            binding
            and not Is_Stub_Details(binding)  // relativized binding is cache/hint
        ){
            out->extra = v->extra;
        }
        else {
            REBLEN index;
            Stub* s = maybe Get_Word_Container(&index, v, context);
            if (not s) {
                out->extra = v->extra;
            }
            else {
                Tweak_Cell_Word_Index(out, index);
                Tweak_Cell_Binding(out, s);
            }
        }
    }
    else if (Bindable_Heart_Is_Any_List(unwrap heart)) {
      any_listlike:
        if (binding) {  // currently not overriding (review: hole punch)
            assert(not Is_Stub_Details(binding));  // shouldn't be relativized
            out->extra = v->extra;
        }
        else if (
            Is_Stub_Use(context)
            and Get_Flavor_Flag(USE, context, SET_WORDS_ONLY)
        ){
            Tweak_Cell_Binding(out, Link_Inherit_Bind(context));
        }
        else
            Tweak_Cell_Binding(out, context);
    }
    else if (not Sequence_Has_Node(v)) {
        out->extra = v->extra;  // packed numeric sequence, 1.2.3 or similar
    }
    else {  // path or tuple, may be wordlike or listlike
        const Node* node1 = CELL_NODE1(v);
        if (Is_Node_A_Cell(node1))  // x.y pairing
            goto any_listlike;
        const Stub* stub1 = c_cast(Stub*, node1);
        if (FLAVOR_SYMBOL == Stub_Flavor(stub1)) {  // x. or /x, wordlike
            if (
                heart == TYPE_TUPLE
                and Get_Cell_Flag(v, LEADING_SPACE)  // !!! HACK for .word form
            ){
                context = Adjust_Context_For_Coupling(context);
                if (not context) {
                    out->extra = v->extra;
                    return out;
                }
            }
            goto any_wordlike;
        }
        goto any_listlike;
    }

    return out;
}


#define Derelativize(dest,v,context) \
    Derelativize_Untracked(TRACK(dest), (v), (context))


// Inefficient - replaced in subsequent commit
//
INLINE Element* Bind_If_Unbound(Element* elem, Context* context) {
    DECLARE_ELEMENT (temp);
    Derelativize(temp, elem, context);
    Move_Cell(elem, temp);
    return elem;
}


// The concept behind `Cell` usage is that it represents a view of a cell
// where the quoting doesn't matter.  This view is taken by things like the
// handlers for MOLD, where it's assumed the quoting levels were rendered by
// the MOLD routine itself...and so accessors for picking apart the payload
// don't require the cell to not be quoted.  However some of those agnostic
// routines want to do things like return errors, and when they do they need
// to strip the quotes off (typically).
//
INLINE Element* Copy_Dequoted_Cell(Sink(Element) out, const Cell* in) {
    Assert_Cell_Stable(in);
    Copy_Cell_Untracked(u_cast(Cell*, out), in, CELL_MASK_COPY);
    LIFT_BYTE(out) = NOQUOTE_1;
    return out;
}

// Modes allowed by Bind related functions:
enum {
    BIND_0 = 0, // Only bind the words found in the context.
    BIND_DEEP = 1 << 1 // Recurse into sub-blocks.
};


struct BinderStruct {
    Option(Stump*) stump_list;

  #if RUNTIME_CHECKS && CPLUSPLUS_11
    //
    // C++ checked build can help us make sure that no binder ever fails to
    // get an Construct_Binder() and Destruct_Binder() pair called on it, which
    // would leave lingering binding hitches on symbol stubs.
    //
    bool initialized;
    BinderStruct () { initialized = false; }
    ~BinderStruct () { assert(not initialized); }
  #endif
};

#define DECLARE_BINDER(name) \
    Binder name##_struct; \
    Binder* name = &name##_struct; \


#if DEBUG_STATIC_ANALYZING  // malloc leak check ensures destruct on all paths!
    #define Construct_Binder(name) \
        void* name##_guard = malloc(1); \
        Construct_Binder_Core(name)

    #define Destruct_Binder(name) do { \
        free(name##_guard); \
        Destruct_Binder_Core(name); \
    } while (0);
#else
    #define Construct_Binder    Construct_Binder_Core
    #define Destruct_Binder     Destruct_Binder_Core
#endif


INLINE const Symbol* Info_Stump_Bind_Symbol(const Stump* stump) {
    assert(Is_Stub_Stump(stump));
    return u_cast(const Symbol*, INFO_STUMP_SYMBOL(stump));
}

INLINE void Tweak_Info_Stump_Bind_Symbol(Stump* stump, const Symbol* symbol) {
    assert(Is_Stub_Stump(stump));
    INFO_STUMP_SYMBOL(stump) = m_cast(Symbol*, symbol);  // extracted as const
}

INLINE Option(Stump*) Link_Stump_Next(const Stump* stump) {
    assert(Is_Stub_Stump(stump));
    return u_cast(Stump*, LINK_STUMP_NEXT(stump));
}

INLINE void Tweak_Link_Stump_Next(Stump* stump, Option(Stump*) next) {
    assert(Is_Stub_Stump(stump));
    assert(next == nullptr or Is_Stub_Stump(unwrap next));
    LINK_STUMP_NEXT(stump) = maybe next;
}

INLINE void Construct_Binder_Core(Binder* binder) {
    binder->stump_list = nullptr;

  #if RUNTIME_CHECKS && CPLUSPLUS_11
    binder->initialized = true;
  #endif
}

INLINE void Destruct_Binder_Core(Binder* binder) {
    while (binder->stump_list != nullptr) {
        Stump* stump = unwrap binder->stump_list;
        binder->stump_list = Link_Stump_Next(stump);

        const Symbol* symbol = Info_Stump_Bind_Symbol(stump);
        assert(Get_Flavor_Flag(SYMBOL, symbol, HITCH_IS_BIND_STUMP));
        Clear_Flavor_Flag(SYMBOL, symbol, HITCH_IS_BIND_STUMP);
        Tweak_Misc_Hitch(m_cast(Symbol*, symbol), Misc_Hitch(stump));

        assert(Is_Node_Readable(stump));
        Set_Node_Unreadable_Bit(stump);
        GC_Kill_Stub(stump);  // expects node diminished/inaccessible (free)
    }

  #if RUNTIME_CHECKS && CPLUSPLUS_11
    binder->initialized = false;
  #endif
}


// Tries to set the binder index, but return false if already there.
//
// 1. When we clean up the binder, we have to remove the HITCH_IS_BIND_STUMP
//    flag for all the symbols we attached stumps to.  But all we have is
//    a singly linked list of the hitches, so the symbol has to be poked
//    somewhere.  We aren't using the INFO bits, so we make this the kind
//    of stub that uses its info as a node, which we do by INFO_NEEDS_MARK,
//    but do notice the GC never runs during a bind.
//
INLINE bool Try_Add_Binder_Index(
    Binder* binder,
    const Symbol* symbol,
    REBINT index
){
  #if CPLUSPLUS_11 && RUNTIME_CHECKS
    assert(binder->initialized);
  #endif

    assert(index != 0);
    if (Get_Flavor_Flag(SYMBOL, symbol, HITCH_IS_BIND_STUMP))
        return false;  // already has a mapping

    Stump* stump = cast(Stump*, Make_Untracked_Stub(STUB_MASK_STUMP));
    Tweak_Link_Stump_Next(stump, binder->stump_list);
    Tweak_Misc_Hitch(stump, Misc_Hitch(symbol));
    Tweak_Info_Stump_Bind_Symbol(stump, symbol);
    Init_Integer(Stub_Cell(stump), index);

    binder->stump_list = stump;

    Tweak_Misc_Hitch(m_cast(Symbol*, symbol), stump);
    Set_Flavor_Flag(SYMBOL, symbol, HITCH_IS_BIND_STUMP);  // must remove [1]

    return true;
}


INLINE void Add_Binder_Index(
    Binder* binder,
    const Symbol* symbol,
    REBINT index
){
    bool success = Try_Add_Binder_Index(binder, symbol, index);
    assert(success);
    UNUSED(success);
}


INLINE Option(REBINT) Try_Get_Binder_Index(  // 0 if not present
    Binder* binder,
    const Symbol* symbol
){
  #if CPLUSPLUS_11 && RUNTIME_CHECKS
    assert(binder->initialized);
  #endif

    UNUSED(binder);
    if (Not_Flavor_Flag(SYMBOL, symbol, HITCH_IS_BIND_STUMP))
        return 0;

    Stump* stump = cast(Stump*, Misc_Hitch(symbol));
    assert(Info_Stump_Bind_Symbol(stump) == symbol);
    REBINT index = VAL_INT32(Known_Element(Stub_Cell(stump)));
    assert(index != 0);
    return index;
}


INLINE void Update_Binder_Index(
    Binder* binder,
    const Symbol* symbol,
    REBINT index
){
    assert(index != 0);  // singly linked list, removal would be inefficient

  #if CPLUSPLUS_11 && RUNTIME_CHECKS
    assert(binder->initialized);
  #endif

    UNUSED(binder);
    assert(Get_Flavor_Flag(SYMBOL, symbol, HITCH_IS_BIND_STUMP));

    Stump* stump = cast(Stump*, Misc_Hitch(symbol));
    assert(Info_Stump_Bind_Symbol(stump) == symbol);
    assert(VAL_INT32(Known_Element(Stub_Cell(stump))) != 0);
    Init_Integer(Stub_Cell(stump), index);
}


struct CollectorStruct {
    CollectFlags initial_flags;
    Binder binder;
    Option(Stub*) base_stump;
    Option(SeaOfVars*) sea;
    REBINT next_index;
};

#define DECLARE_COLLECTOR(name) \
    Collector name##_struct; \
    Collector* name = &name##_struct; \

#if DEBUG_STATIC_ANALYZING  // malloc leak check ensures destruct on all paths!
    #define Construct_Collector(name,flags,context) \
        void* name##_guard = malloc(1); \
        Construct_Collector_Core(name, (flags), (context))

    #define Destruct_Collector(name) do { \
        free(name##_guard); \
        Destruct_Collector_Core(name); \
    } while (0);
#else
    #define Construct_Collector    Construct_Collector_Core
    #define Destruct_Collector     Destruct_Collector_Core
#endif


INLINE bool IS_WORD_UNBOUND(const Cell* v) {
    assert(Wordlike_Cell(v));
    if (CELL_WORD_INDEX_I32(v) < 0)
        assert(Is_Stub_Details(Cell_Binding(v)));
    return CELL_WORD_INDEX_I32(v) <= 0;
}

#define IS_WORD_BOUND(v) \
    (not IS_WORD_UNBOUND(v))


INLINE REBINT VAL_WORD_INDEX(const Cell* v) {
    assert(Wordlike_Cell(v));
    REBINT i = CELL_WORD_INDEX_I32(v);
    assert(i > 0);
    return i;
}

INLINE void Unbind_Any_Word(Element* v) {
    assert(Wordlike_Cell(v));
    CELL_WORD_INDEX_I32(v) = 0;
    Tweak_Cell_Binding(v, UNBOUND);
}

INLINE Context* VAL_WORD_CONTEXT(const Value* v) {
    assert(IS_WORD_BOUND(v));
    Context* binding = Cell_Binding(v);
    if (Is_Stub_Patch(binding)) {
        SeaOfVars* patch_context = Info_Patch_Sea(cast(Patch*, binding));
        binding = patch_context;
    }
    else if (Is_Stub_Let(binding))
        panic ("LET variables have no context at this time");

    return binding;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  DETERMINING BINDING FOR CHILDREN IN A LIST
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A relative array must be combined with a binding in order to find the
// actual context instance where its values can be found.  Since today's
// bindings are always nothing or a FRAME!'s context, this is fairly easy...
// if you find a specific child value resident in a relative array then
// it's that child's binding that overrides the binding in effect.
//
// With virtual binding this could get more complex, since a binding may
// wish to augment or override the binding in a deep way on read-only blocks.
// That means bindings may need to be chained together.  This would create
// needs for GC or reference counting mechanics, which may defy a simple
// solution in pure C.
//
// But as a first step, this function locates all the places in the code that
// would need such derivation.
//


// An ANY-LIST? cell has a pointer's-worth of spare space in it, which is
// used to keep track of the information required to further resolve the
// words and lists that reside in it.
//
INLINE Context* Derive_Binding(
    Context* context,
    const Element* list
){
    assert(Listlike_Cell(list));

    Context* binding = Cell_Binding(list);
    if (binding)
        return binding;

    return context;
}


//
// BINDING CONVENIENCE MACROS
//
// WARNING: Don't pass these routines something like a singular Value* (such
// as a TYPE_BLOCK) which you wish to have bound.  You must pass its *contents*
// as an array...as the plural "values" in the name implies!
//
// So don't do this:
//
//     Value* block = ARG(BLOCK);
//     Value* something = ARG(NEXT_ARG_AFTER_BLOCK);
//     Bind_Values_Deep(block, context);
//
// What will happen is that the block will be treated as an array of values
// and get incremented.  In the above case it would reach to the next argument
// and bind it too (likely crashing at some point not too long after that).
//
// Instead write:
//
//     Bind_Values_Deep(Array_Head(Cell_Array(block)), context);
//
// That will pass the address of the first value element of the block's
// contents.  You could use a later value element, but note that the interface
// as written doesn't have a length limit.  So although you can control where
// it starts, it will keep binding until it hits an end marker.
//

#define Bind_Values_Deep(at,tail,context) \
    Bind_Values_Core((at), (tail), (context), SYM_0, BIND_DEEP)

#define Bind_Values_All_Deep(at,tail,context) \
    Bind_Values_Core((at), (tail), (context), SYM_ANY, BIND_DEEP)

#define Bind_Values_Shallow(at,tail,context) \
    Bind_Values_Core((at), (tail), (context), SYM_0, BIND_0)

#define Unbind_Values_Deep(at,tail) \
    Unbind_Values_Core((at), (tail), nullptr, true)


// Loop Slots

#define CELL_FLAG_LOOP_SLOT_NOTE_TIE  CELL_FLAG_NOTE
#define CELL_FLAG_LOOP_SLOT_ROOT_META  NODE_FLAG_ROOT
