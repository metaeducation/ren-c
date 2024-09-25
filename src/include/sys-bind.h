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
// want to have any "less of a plan".  So the Reb_Binder is used by binding
// clients as a placeholder for any state needed to interpret more than one
// binding at a time.
//
// The debug build also adds another feature, that makes sure the clear count
// matches the set count.
//
// The binding will be either a REBACT (relative to a function) or a
// Context* (specific to a context), or simply a plain Array* such as
// EMPTY_ARRAY which indicates UNBOUND.  The FLAVOR_BYTE() says what it is
//
//     ANY-WORD?: binding is the word's binding
//
//     ANY-LIST?: binding is the relativization or specifier for the REBVALs
//     which can be found in the frame (for recursive resolution of ANY-WORD?s)
//
//     ACTION!: binding is the instance data for archetypal invocation, so
//     although all the RETURN instances have the same paramlist, it is
//     the binding which is unique to the cell specifying which to exit
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
// the target, then the specifier is used to do that.
//
// It is nearly as fast as just assigning the value directly in the release
// build, though debug builds assert that the function in the specifier
// indeed matches the target in the relative value (because relative values
// in an array may only be relative to the function that deep copied them, and
// that is the only kind of specifier you can use with them).
//
// Interface designed to line up with Copy_Cell()
//
// !!! At the moment, there is a fair amount of overlap in this code with
// Get_Context_Core().  One of them resolves a value's real binding and then
// fetches it, while the other resolves a value's real binding but then stores
// that back into another value without fetching it.  This suggests sharing
// a mechanic between both...TBD.
//

INLINE Specifier* Derive_Specifier(
    Specifier* parent,
    const Cell* list
);

INLINE Element* Derelativize_Untracked(
    Sink(Element*) out,  // relative dest overwritten w/specific value
    const Element* v,
    Specifier* specifier
){
    Copy_Cell_Header(out, v);
    out->payload = v->payload;

    Heart heart = Cell_Heart_Unchecked(v);

    if (not Is_Bindable_Heart(heart)) {
        out->extra = v->extra;
        return out;
    }

    Stub* binding = BINDING(v);

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
            Flex* f =
                maybe Get_Word_Container(&index, v, specifier, ATTACH_READ);
            if (not f) {
                out->extra = v->extra;
            }
            else {
                Tweak_Cell_Word_Index(out, index);
                BINDING(out) = f;
            }
        }
    }
    else if (Bindable_Heart_Is_Any_List(heart)) {  // any-block? or any-group?
      any_listlike:
        if (binding) {  // currently not overriding (review: hole punch)
            assert(not Is_Stub_Details(binding));  // shouldn't be relativized
            out->extra = v->extra;
        }
        else
            BINDING(out) = specifier;
    }
    else if (Not_Cell_Flag(v, SEQUENCE_HAS_NODE)) {
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


#define Derelativize(dest,v,specifier) \
    TRACK(Derelativize_Untracked((dest), (v), (specifier)))


// The concept behind `Cell` usage is that it represents a view of a cell
// where the quoting doesn't matter.  This view is taken by things like the
// handlers for MOLD, where it's assumed the quoting levels were rendered by
// the MOLD routine itself...and so accessors for picking apart the payload
// don't require the cell to not be quoted.  However some of those agnostic
// routines want to do things like raise errors, and when they do they need
// to strip the quotes off (typically).
//
INLINE Element* Copy_Dequoted_Cell(Sink(Element*) out, const Cell* in) {
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


struct Reb_Binder {
  #if !defined(NDEBUG)
    REBLEN count;
  #endif

  #if CPLUSPLUS_11
    //
    // The C++ debug build can help us make sure that no binder ever fails to
    // get an INIT_BINDER() and SHUTDOWN_BINDER() pair called on it, which
    // would leave lingering binding values on symbol stubs.
    //
    bool initialized;
    Reb_Binder () { initialized = false; }
    ~Reb_Binder () { assert(not initialized); }
  #else
    int pedantic_warnings_dont_allow_empty_struct;
  #endif
};


INLINE void INIT_BINDER(struct Reb_Binder *binder) {
  #if defined(NDEBUG)
    UNUSED(binder);
  #else
    binder->count = 0;

    #if CPLUSPLUS_11
        binder->initialized = true;
    #endif
  #endif
}


INLINE void SHUTDOWN_BINDER(struct Reb_Binder *binder) {
  #if !defined(NDEBUG)
    assert(binder->count == 0);

    #if CPLUSPLUS_11
        binder->initialized = false;
    #endif
  #endif

    UNUSED(binder);
}


// Tries to set the binder index, but return false if already there.
//
INLINE bool Try_Add_Binder_Index(
    struct Reb_Binder *binder,
    const Symbol* s,
    REBINT index
){
    assert(index != 0);
    if (Get_Subclass_Flag(SYMBOL, s, MISC_IS_BINDINFO))
        return false;  // already has a mapping

    // Not actually managed...but GC doesn't run while binders are active,
    // and we don't want to pay for putting this in the manual tracking list.
    //
    Array* hitch = Alloc_Singular(
        NODE_FLAG_MANAGED | FLEX_FLAG_BLACK | FLAG_FLAVOR(HITCH)
    );
    Clear_Node_Managed_Bit(hitch);
    Init_Integer(Stub_Cell(hitch), index);
    node_MISC(Hitch, hitch) = node_MISC(Hitch, s);

    MISC(Hitch, s) = hitch;
    Set_Subclass_Flag(SYMBOL, s, MISC_IS_BINDINFO);

  #if defined(NDEBUG)
    UNUSED(binder);
  #else
    ++binder->count;
  #endif
    return true;
}


INLINE void Add_Binder_Index(
    struct Reb_Binder *binder,
    const Symbol* s,
    REBINT index
){
    bool success = Try_Add_Binder_Index(binder, s, index);
    assert(success);
    UNUSED(success);
}


INLINE REBINT Get_Binder_Index_Else_0( // 0 if not present
    struct Reb_Binder *binder,
    const Symbol* s
){
    UNUSED(binder);
    if (Not_Subclass_Flag(SYMBOL, s, MISC_IS_BINDINFO))
        return 0;

    Stub* hitch = MISC(Hitch, s);  // unmanaged stub used for binding
    return VAL_INT32(Stub_Cell(hitch));
}


INLINE REBINT Remove_Binder_Index_Else_0( // return old value if there
    struct Reb_Binder *binder,
    const Symbol* s
){
    if (Not_Subclass_Flag(SYMBOL, s, MISC_IS_BINDINFO))
        return 0;

    Stub* hitch = MISC(Hitch, s);

    REBINT index = VAL_INT32(Stub_Cell(hitch));
    MISC(Hitch, s) = cast(Stub*, node_MISC(Hitch, hitch));
    Clear_Subclass_Flag(SYMBOL, s, MISC_IS_BINDINFO);

    Set_Node_Managed_Bit(hitch);  // we didn't manuals track it
    GC_Kill_Flex(hitch);

  #if defined(NDEBUG)
    UNUSED(binder);
  #else
    assert(binder->count > 0);
    --binder->count;
  #endif
    return index;
}


INLINE void Remove_Binder_Index(
    struct Reb_Binder *binder,
    const Symbol* s
){
    REBINT old_index = Remove_Binder_Index_Else_0(binder, s);
    assert(old_index != 0);
    UNUSED(old_index);
}


// Modes allowed by Collect keys functions:
enum {
    COLLECT_ONLY_SET_WORDS = 0,
    COLLECT_ANY_WORD = 1 << 1,
    COLLECT_DEEP = 1 << 2,
    COLLECT_NO_DUP = 1 << 3  // Do not allow dups during collection (for specs)
};

struct Reb_Collector {
    Flags flags;
    StackIndex stack_base;
    struct Reb_Binder binder;
};

#define Collector_Index_If_Pushed(collector) \
    ((TOP_INDEX - (collector)->stack_base) + 1)  // index of *next* item to add


INLINE bool IS_WORD_UNBOUND(const Cell* v) {
    assert(Any_Wordlike(v));
    if (CELL_WORD_INDEX_I32(v) == INDEX_ATTACHED)
        return true;
    if (CELL_WORD_INDEX_I32(v) < 0)
        assert(Is_Stub_Details(BINDING(v)));
    return CELL_WORD_INDEX_I32(v) <= 0;
}

#define IS_WORD_BOUND(v) \
    (not IS_WORD_UNBOUND(v))


INLINE REBINT VAL_WORD_INDEX(const Cell* v) {
    assert(Any_Wordlike(v));
    uint32_t i = CELL_WORD_INDEX_I32(v);
    assert(i > 0);
    return cast(REBLEN, i);
}

INLINE void Unbind_Any_Word(Cell* v) {
    assert(Any_Wordlike(v));
    CELL_WORD_INDEX_I32(v) = 0;
    BINDING(v) = nullptr;
}

INLINE Context* VAL_WORD_CONTEXT(const Value* v) {
    assert(IS_WORD_BOUND(v));
    Stub* binding = BINDING(v);
    if (Is_Stub_Patch(binding)) {
        Context* patch_context = INODE(PatchContext, binding);
        binding = CTX_VARLIST(patch_context);
    }
    else if (Is_Stub_Let(binding))
        fail ("LET variables have no context at this time");

    return cast(Context*, binding);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  VARIABLE ACCESS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// When a word is bound to a context by an index, it becomes a means of
// reading and writing from a persistent storage location.
//
// All variables can be put in a CELL_FLAG_PROTECTED state.  This is a flag
// on the variable cell itself--not the key--so different instances of
// the same object sharing the keylist don't all have to be protected just
// because one instance is.  This is not one of the flags included in the
// CELL_MASK_COPY, so it shouldn't be able to leak out of the varlist.
//
// The Trap_Lookup_Word() function takes the conservative default that
// only const access is needed.  A const pointer to a Value is given back
// which may be inspected, but the contents not modified.  While a bound
// variable that is not currently set will return an antiform void value,
// Trap_Lookup_Word() on an *unbound* word return an error.
//
// Lookup_Mutable_Word_May_Fail() offers a parallel facility for getting a
// non-const Value back.  It will fail if the variable is either unbound
// -or- marked with OPT_TYPESET_LOCKED to protect against modification.
//

INLINE Option(Context*) Trap_Lookup_Word(
    const Value** out,
    const Element* word,
    Specifier* specifier
){
    REBLEN index;
    Flex* f = maybe Get_Word_Container(
        &index,
        word,
        specifier,
        ATTACH_READ
    );
    if (not f)
        return Error_Not_Bound_Raw(word);
    if (index == INDEX_ATTACHED)
        return Error_Unassigned_Attach_Raw(word);

    if (Is_Stub_Let(f) or Is_Stub_Patch(f)) {
        *out = Stub_Cell(f);
        return nullptr;
    }

    Assert_Node_Accessible(f);
    Context* c = cast(Context*, f);
    *out = CTX_VAR(c, index);
    return nullptr;
}

INLINE Option(const Value*) Lookup_Word(
    const Element* word,
    Specifier* specifier
){
    REBLEN index;
    Flex* f = maybe Get_Word_Container(
        &index,
        word,
        specifier,
        ATTACH_READ
    );
    if (not f or index == INDEX_ATTACHED)
        return nullptr;
    if (Is_Stub_Let(f) or Is_Stub_Patch(f))
        return Stub_Cell(f);

    Assert_Node_Accessible(f);
    Context* c = cast(Context*, f);
    return CTX_VAR(c, index);
}

INLINE Value* Lookup_Mutable_Word_May_Fail(
    const Element* any_word,
    Specifier* specifier
){
    REBLEN index;
    Flex* f = maybe Get_Word_Container(
        &index,
        any_word,
        specifier,
        ATTACH_WRITE
    );
    if (not f)
        fail (Error_Not_Bound_Raw(any_word));

    Value* var;
    if (Is_Stub_Let(f) or Is_Stub_Patch(f))
        var = Stub_Cell(f);
    else {
        Context* c = cast(Context*, f);

        // A context can be permanently frozen (`lock obj`) or temporarily
        // protected, e.g. `protect obj | unprotect obj`.  A native will
        // use FLEX_FLAG_HOLD on a FRAME! context in order to prevent
        // setting values to types with bit patterns the C might crash on.
        //
        // Lock bits are all in SER->info and checked in the same instruction.
        //
        Fail_If_Read_Only_Flex(CTX_VARLIST(c));

        var = CTX_VAR(c, index);
    }

    // The PROTECT command has a finer-grained granularity for marking
    // not just contexts, but individual fields as protected.
    //
    if (Get_Cell_Flag(var, PROTECTED)) {
        DECLARE_ATOM (unwritable);
        Init_Word(unwritable, Cell_Word_Symbol(any_word));
        fail (Error_Protected_Word_Raw(unwritable));
    }

    return var;
}

INLINE Sink(Value*) Sink_Word_May_Fail(
    const Element* any_word,
    Specifier* specifier
){
    Value* var = Lookup_Mutable_Word_May_Fail(any_word, specifier);
    return var;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  DETERMINING SPECIFIER FOR CHILDREN IN AN ARRAY
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A relative array must be combined with a specifier in order to find the
// actual context instance where its values can be found.  Since today's
// specifiers are always nothing or a FRAME!'s context, this is fairly easy...
// if you find a specific child value resident in a relative array then
// it's that child's specifier that overrides the specifier in effect.
//
// With virtual binding this could get more complex, since a specifier may
// wish to augment or override the binding in a deep way on read-only blocks.
// That means specifiers may need to be chained together.  This would create
// needs for GC or reference counting mechanics, which may defy a simple
// solution in pure C.
//
// But as a first step, this function locates all the places in the code that
// would need such derivation.
//

// A specifier can be a FRAME! context for fulfilling relative words.  Or it
// may be a chain of virtual binds where the last link in the chain is to
// a frame context.
//
// It's Derive_Specifier()'s job to make sure that if specifiers get linked on
// top of each other, the chain always bottoms out on the same FRAME! that
// the original specifier was pointing to.
//
INLINE Node** SPC_FRAME_CTX_ADDRESS(Specifier* specifier)
{
    assert(Is_Stub_Let(specifier) or Is_Stub_Use(specifier));
    while (
        NextVirtual(specifier) != nullptr
        and not Is_Stub_Varlist(NextVirtual(specifier))
    ){
        specifier = NextVirtual(specifier);
    }
    return &node_LINK(NextLet, specifier);
}

INLINE Option(Context*) SPC_FRAME_CTX(Specifier* specifier)
{
    if (specifier == UNBOUND)  // !!! have caller check?
        return nullptr;
    if (Is_Stub_Varlist(specifier))
        return cast(Context*, specifier);
    return cast(Context*, x_cast(Node*, *SPC_FRAME_CTX_ADDRESS(specifier)));
}


// An ANY-LIST? cell has a pointer's-worth of spare space in it, which is
// used to keep track of the information required to further resolve the
// words and lists that reside in it.
//
INLINE Specifier* Derive_Specifier(
    Specifier* specifier,
    const Cell* list
){
    if (BINDING(list) != UNBOUND)
        return BINDING(list);

    return specifier;
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

// Gave this a complex name to warn of its peculiarities.  Calling with
// just BIND_SET is shallow and tricky because the set words must occur
// before the uses (to be applied to bindings of those uses)!
//
#define Bind_Values_Set_Midstream_Shallow(at,tail,context) \
    Bind_Values_Core((at), (tail), (context), SYM_SET, BIND_0)

#define Unbind_Values_Deep(at,tail) \
    Unbind_Values_Core((at), (tail), nullptr, true)
