//
//  File: %sys-bind.h
//  Summary: "System Binding Include"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
// R3-Alpha had a per-thread "bind table"; a large and sparsely populated hash
// into which index numbers would be placed, for what index those words would
// have as keys or parameters.  Ren-C's strategy is that binding information
// is linked onto Symbol stubs that represent the canon words themselves.
//
// Currently it is assumed a symbol has zero or one of these linked bindings.
// This would create problems if multiple threads were trying to bind at the
// same time.  While threading was never realized in R3-Alpha, Ren-C doesn't
// want to have any "less of a plan".  So the BinderStruct is used by binding
// clients as a placeholder for any state needed to interpret more than one
// binding at a time.
//
// The debug build also adds another feature, that makes sure the clear count
// matches the set count.
//
// The binding will be either a REBACT (relative to a function) or a
// VarList* (specific to a context), or simply a plain Array* such as
// EMPTY_ARRAY which indicates UNBOUND.  The FLAVOR_BYTE() says what it is
//
//     ANY-WORD?: binding is the word's binding
//
//     ANY-LIST?: binding is the relativization or for the REBVALs
//     which can be found in the frame (for recursive resolution of ANY-WORD?s)
//
//     ACTION!: slot where binding would be is the instance data for archetypal
//     invocation, so although all the RETURN instances have the same
//     paramlist, it is the coupling which is unique to the cell specifying
//     which to exit
//
//     ANY-CONTEXT?: if a FRAME!, the binding carries the instance data from
//     the function it is for.  So if the frame was produced for an instance
//     of RETURN, the keylist only indicates the archetype RETURN.  Putting
//     the binding back together can indicate the instance.
//
//     VARARGS!: the binding identifies the feed from which the values are
//     coming.  It can be an ordinary singular array which was created with
//     MAKE VARARGS! and has its index updated for all shared instances.
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
// build, though debug builds assert that the function in the binding
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

    Heart heart = Cell_Heart_Unchecked(v);

    if (
        not context  // should bindings always be left as-is in this case?
        or not Is_Bindable_Heart(heart)
    ){
        out->extra = v->extra;
        return out;
    }

    Context* binding = BINDING(v);

    if (Bindable_Heart_Is_Any_Word(heart)) {  // any-word?
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
                BINDING(out) = s;
            }
        }
    }
    else if (Bindable_Heart_Is_Any_List(heart)) {  // any-block? or any-group?
      any_listlike:
        if (binding) {  // currently not overriding (review: hole punch)
            assert(not Is_Stub_Details(binding));  // shouldn't be relativized
            out->extra = v->extra;
        }
        else if (
            Is_Stub_Use(context)
            and Get_Cell_Flag(Stub_Cell(context), USE_NOTE_SET_WORDS)
        ){
            BINDING(out) = LINK(NextUse, context);
        }
        else
            BINDING(out) = context;
    }
    else if (not Sequence_Has_Node(v)) {
        out->extra = v->extra;  // packed numeric sequence, 1.2.3 or similar
    }
    else {  // any-path? or any-tuple?, may be wordlike or listlike
        Node* node1 = Cell_Node1(v);
        if (Is_Node_A_Cell(node1))  // x.y pairing
            goto any_listlike;
        Stub* stub1 = cast(Stub*, node1);
        if (FLAVOR_SYMBOL == Stub_Flavor(stub1))  // x. or /x, wordlike
            goto any_wordlike;
        goto any_listlike;
    }

    return out;
}


#define Derelativize(dest,v,context) \
    TRACK(Derelativize_Untracked((dest), (v), (context)))


// The concept behind `Cell` usage is that it represents a view of a cell
// where the quoting doesn't matter.  This view is taken by things like the
// handlers for MOLD, where it's assumed the quoting levels were rendered by
// the MOLD routine itself...and so accessors for picking apart the payload
// don't require the cell to not be quoted.  However some of those agnostic
// routines want to do things like raise errors, and when they do they need
// to strip the quotes off (typically).
//
INLINE Element* Copy_Dequoted_Cell(Sink(Element) out, const Cell* in) {
    assert(QUOTE_BYTE(in) != ANTIFORM_0);
    Copy_Cell(out, c_cast(Element*, in));
    QUOTE_BYTE(out) = NOQUOTE_1;
    return out;
}

// Modes allowed by Bind related functions:
enum {
    BIND_0 = 0, // Only bind the words found in the context.
    BIND_DEEP = 1 << 1 // Recurse into sub-blocks.
};


struct BinderStruct {
    Stub* hitch_list;

  #if DEBUG && CPLUSPLUS_11
    //
    // C++ debug build can help us make sure that no binder ever fails to
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


INLINE void Construct_Binder_Core(Binder* binder) {
    binder->hitch_list = nullptr;

  #if DEBUG && CPLUSPLUS_11
    binder->initialized = true;
  #endif
}

INLINE void Destruct_Binder_Core(Binder* binder) {
    while (binder->hitch_list != nullptr) {
        Stub* hitch = binder->hitch_list;
        binder->hitch_list = LINK(NextBind, hitch);

        const Symbol* s = INODE(BindSymbol, hitch);
        assert(Get_Flavor_Flag(SYMBOL, s, MISC_IS_BINDINFO));
        Clear_Flavor_Flag(SYMBOL, s, MISC_IS_BINDINFO);
        node_MISC(Hitch, s) = node_MISC(Hitch, hitch);

        assert(Is_Node_Readable(hitch));
        Set_Node_Unreadable_Bit(hitch);
        GC_Kill_Stub(hitch);  // expects node to be decayed/inaccessible (free)
    }

  #if DEBUG && CPLUSPLUS_11
    binder->initialized = false;
  #endif
}


// Tries to set the binder index, but return false if already there.
//
// 1. GC does not run during binding, and we want this as cheap as possible.
//
// 2. When we clean up the binder, we have to remove the MISC_IS_BINDINFO
//    flag for all the symbols we attached hitches to.  But all we have is
//    a singly linked list of the hitches, so the symbol has to be poked
//    somewhere.  We aren't using the INFO bits, so we make this the kind
//    of stub that uses its info as a node, which we do by INFO_NEEDS_MARK,
//    but do notice the GC never runs during a bind.
//
INLINE bool Try_Add_Binder_Index(
    Binder* binder,
    const Symbol* s,
    REBINT index
){
  #if CPLUSPLUS_11 && DEBUG
    assert(binder->initialized);
  #endif

    assert(index != 0);
    if (Get_Flavor_Flag(SYMBOL, s, MISC_IS_BINDINFO))
        return false;  // already has a mapping

    Stub* hitch = Make_Untracked_Stub(  // don't pay for manuals tracking
        FLAG_FLAVOR(HITCH)
            | STUB_FLAG_BLACK  // !!! does not get counted if created like this
            | STUB_FLAG_INFO_NODE_NEEDS_MARK  // symbol (but no GC runs!) [1]
    );
    INODE(BindSymbol, hitch) = s;
    Init_Integer(Stub_Cell(hitch), index);
    node_MISC(Hitch, hitch) = node_MISC(Hitch, s);
    LINK(NextBind, hitch) = binder->hitch_list;
    binder->hitch_list = hitch;

    MISC(Hitch, s) = hitch;
    Set_Flavor_Flag(SYMBOL, s, MISC_IS_BINDINFO);

    return true;
}


INLINE void Add_Binder_Index(
    Binder* binder,
    const Symbol* s,
    REBINT index
){
    bool success = Try_Add_Binder_Index(binder, s, index);
    assert(success);
    UNUSED(success);
}


INLINE Option(REBINT) Try_Get_Binder_Index(  // 0 if not present
    Binder* binder,
    const Symbol* s
){
  #if CPLUSPLUS_11 && DEBUG
    assert(binder->initialized);
  #endif

    UNUSED(binder);
    if (Not_Flavor_Flag(SYMBOL, s, MISC_IS_BINDINFO))
        return 0;

    Stub* hitch = MISC(Hitch, s);  // unmanaged stub used for binding
    assert(INODE(BindSymbol, hitch) == s);
    REBINT index = VAL_INT32(Stub_Cell(hitch));
    assert(index != 0);
    return index;
}


INLINE void Update_Binder_Index(
    Binder* binder,
    const Symbol* s,
    REBINT index
){
    assert(index != 0);  // singly linked list, removal would be inefficient

  #if CPLUSPLUS_11 && DEBUG
    assert(binder->initialized);
  #endif

    UNUSED(binder);
    assert(Get_Flavor_Flag(SYMBOL, s, MISC_IS_BINDINFO));

    Stub* hitch = MISC(Hitch, s);  // unmanaged stub used for binding
    assert(INODE(BindSymbol, hitch) == s);
    assert(VAL_INT32(Stub_Cell(hitch)) != 0);
    Init_Integer(Stub_Cell(hitch), index);
}


struct CollectorStruct {
    CollectFlags initial_flags;
    Binder binder;
    Stub* base_hitch;
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
        assert(Is_Stub_Details(BINDING(v)));
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

INLINE void Unbind_Any_Word(Cell* v) {
    assert(Wordlike_Cell(v));
    CELL_WORD_INDEX_I32(v) = 0;
    BINDING(v) = nullptr;
}

INLINE VarList* VAL_WORD_CONTEXT(const Value* v) {
    assert(IS_WORD_BOUND(v));
    Context* binding = BINDING(v);
    if (Is_Stub_Patch(binding)) {
        VarList* patch_context = INODE(PatchContext, binding);
        binding = patch_context;
    }
    else if (Is_Stub_Let(binding))
        fail ("LET variables have no context at this time");

    return cast(VarList*, binding);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  ***LOW-LEVEL*** LOOKUP OF CELL SLOTS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// PLEASE TAKE NOTE: Most code should use higher level routines, like
// Trap_Get_Any_Word() or Trap_Get_Var_XXX().
//
// These routines will get the cell which a word looks up to, but that cell
// may *not* hold the intended "varible".  For instance: it may hold functions
// that the system has to call to generate the variable (an "Accessor").  So
// trying to read or write cells coming from this routine without using the
// proper higher layers will result in asserts.
//

INLINE Option(Error*) Trap_Lookup_Word(
    Sink(const Value*) out,  // returns read-only pointer to cell
    const Element* word,
    Context* context
){
    REBLEN index;
    Stub* s = maybe Get_Word_Container(&index, word, context);
    if (not s) {
        *out = nullptr;  // avoid aggressive callsite warnings
        return Error_Not_Bound_Raw(word);
    }

    if (Is_Stub_Let(s) or Is_Stub_Patch(s)) {
        *out = Stub_Cell(s);
        return nullptr;
    }
    assert(Is_Node_Readable(s));
    VarList* c = cast(VarList*, s);
    *out = Varlist_Slot(c, index);
    return nullptr;
}

INLINE Option(const Value*) Lookup_Word(
    const Element* word,
    Context* context
){
    REBLEN index;
    Stub* s = maybe Get_Word_Container(&index, word, context);
    if (not s)
        return nullptr;

    if (Is_Stub_Let(s) or Is_Stub_Patch(s))
        return Stub_Cell(s);

    assert(Is_Node_Readable(s));
    VarList* c = cast(VarList*, s);
    return Varlist_Slot(c, index);
}

// 1. Contexts can be permanently frozen (`lock obj`) or temporarily protected,
//    e.g. `protect obj | unprotect obj`.  A native will use FLEX_FLAG_HOLD on
//    a FRAME! context in order to prevent setting values to types with bit
//    patterns the C might crash on.  Lock bits are all in SER->info and
//    checked in the same instruction.
//
// 2. All variables can be put in a CELL_FLAG_PROTECTED state.  This is a flag
//    on the variable cell itself--not the key--so different instances of
//    the same object sharing the keylist don't all have to be protected just
//    because one instance is.  This is not one of the flags included in the
//    CELL_MASK_COPY, so it shouldn't be able to leak out of a cell.
//
INLINE Value* Lookup_Mutable_Word_May_Fail(
    const Element* any_word,
    Context* context
){
    REBLEN index;
    Stub* s = maybe Get_Word_Container(&index, any_word, context);
    if (not s)
        fail (Error_Not_Bound_Raw(any_word));

    Value* var;
    if (Is_Stub_Let(s) or Is_Stub_Patch(s))
        var = Stub_Cell(s);
    else {
        VarList* c = cast(VarList*, s);
        Fail_If_Read_Only_Flex(c);  // check lock bits [1]
        var = Varlist_Slot(c, index);
    }

    if (Get_Cell_Flag(var, PROTECTED)) {  // protect is per-cell [2]
        DECLARE_ATOM (unwritable);
        Init_Word(unwritable, Cell_Word_Symbol(any_word));
        fail (Error_Protected_Word_Raw(unwritable));
    }

    return var;
}

INLINE Sink(Value) Sink_Word_May_Fail(
    const Element* any_word,
    Context* context
){
    Value* var = Lookup_Mutable_Word_May_Fail(any_word, context);
    return var;
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
    const Cell* list
){
    assert(Listlike_Cell(list));

    Context* binding = BINDING(list);
    if (binding)
        return binding;

    return context;
}


//
// BINDING CONVENIENCE MACROS
//
// WARNING: Don't pass these routines something like a singular Value* (such
// as a REB_BLOCK) which you wish to have bound.  You must pass its *contents*
// as an array...as the plural "values" in the name implies!
//
// So don't do this:
//
//     Value* block = ARG(block);
//     Value* something = ARG(next_arg_after_block);
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
