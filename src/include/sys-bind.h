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
//     ANY-WORD!: binding is the word's binding
//
//     ANY-ARRAY!: binding is the relativization or specifier for the REBVALs
//     which can be found in the frame (for recursive resolution of ANY-WORD!s)
//
//     ACTION!: binding is the instance data for archetypal invocation, so
//     although all the RETURN instances have the same paramlist, it is
//     the binding which is unique to the REBVAL specifying which to exit
//
//     ANY-CONTEXT!: if a FRAME!, the binding carries the instance data from
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
// This can be used to turn a Cell into a REBVAL.  If the Cell is indeed
// relative and needs to be made specific to be put into the target, then the
// specifier is used to do that.
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
    NoQuote(const Cell*) any_array
);

INLINE REBVAL *Derelativize_Untracked(
    Cell* out,  // relative dest overwritten w/specific value
    const Cell* v,
    Specifier* specifier
){
    Copy_Cell_Header(out, v);
    out->payload = v->payload;
    if (not Is_Bindable(v) or (BINDING(v) and not IS_DETAILS(BINDING(v)))) {
        out->extra = v->extra;
        return cast(REBVAL*, out);
    }

    // The specifier is not going to have a say in the derelativized cell.
    // This means any information it encodes must be taken into account now.
    //
    if (Any_Wordlike(v)) {
        REBLEN index;
        Series* s = try_unwrap(
            Get_Word_Container(&index, v, specifier, ATTACH_READ)
        );
        if (not s) {
            // Getting back NULL here could mean that it's actually unbound,
            // or that it's bound to a "sea" context like User or Lib and
            // there's nothing there...yet.
            //
            out->extra = v->extra;
        }
        else {
            INIT_VAL_WORD_INDEX(out, index);
            BINDING(out) = s;
        }

        return cast(REBVAL*, out);
    }
    else if (Any_Arraylike(v)) {
        //
        // The job of an array in a derelativize operation is to carry along
        // the specifier.  However, it cannot lose any prior existing info
        // that's in the specifier it holds.
        //
        // The mechanism otherwise is shared with specifier derivation.
        // That includes the case of if specifier==SPECIFIED.
        //
        BINDING(out) = Derive_Specifier(specifier, v);
    }
    else {
        // Things like contexts and varargs are not affected by specifiers,
        // at least not currently.
        //
        out->extra = v->extra;
    }

    return cast(REBVAL*, out);
}


#define Derelativize(dest,v,specifier) \
    TRACK(Derelativize_Untracked((dest), (v), (specifier)))

#define Dequoted_Derelativize(out,in,specifier) \
    Dequotify(Derelativize((out), \
        cast(const Cell*, ensure(NoQuote(const Cell*), (in))), (specifier)))


// Tells whether when an ACTION! has a binding to a context, if that binding
// should override the stored binding in a WORD! being looked up.
//
//    o1: make object! [a: 10 f: does [print a]]
//    o2: make o1 [a: 20 b: 22]
//    o3: make o2 [b: 30]
//
// In the scenario above, when calling `f` bound to o2 stored in o2, or the
// call to `f` bound to o3 and stored in o3, the `a` in the relevant objects
// must be found from the override.  This is done by checking to see if a
// walk from the derived keylist makes it down to the keylist for a.
//
// Note that if a new keylist is not made, it's not possible to determine a
// "parent/child" relationship.  There is no information stored which could
// tell that o3 was made from o2 vs. vice-versa.  The only thing that happens
// is at MAKE-time, o3 put its binding into any functions bound to o2 or o1,
// thus getting its overriding behavior.
//
INLINE bool Is_Overriding_Context(Context* stored, Context* override)
{
    Node* stored_source = BONUS(KeySource, CTX_VARLIST(stored));
    Node* temp = BONUS(KeySource, CTX_VARLIST(override));

    // FRAME! "keylists" are actually paramlists, and the LINK.underlying
    // field is used in paramlists (precluding a LINK.ancestor).  Plus, since
    // frames are tied to a function they invoke, they cannot be expanded.
    // For now, deriving from FRAME! is just disabled.
    //
    // Use a faster check for REB_FRAME than CTX_TYPE() == REB_FRAME, since
    // we were extracting keysources anyway.
    //
    // !!! Note that in virtual binding, something like a FOR-EACH would
    // wind up overriding words bound to FRAME!s, even though not "derived".
    //
    if (Is_Node_A_Cell(stored_source))
        return false;
    if (Is_Node_A_Cell(temp))
        return false;

    while (true) {
        if (temp == stored_source)
            return true;

        if (LINK(Ancestor, x_cast(Series*, temp)) == temp)
            break;

        temp = LINK(Ancestor, x_cast(Series*, temp));
    }

    return false;
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
        NODE_FLAG_MANAGED | SERIES_FLAG_BLACK | FLAG_FLAVOR(HITCH)
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
    GC_Kill_Series(hitch);

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


// The unbound state for an ANY-WORD! is to hold its spelling.  Once bound,
// the spelling is derived by indexing into the keylist of the binding (if
// bound directly to a context) or into the paramlist (if relative to an
// action, requiring a frame specifier to fully resolve).
//
INLINE bool IS_WORD_UNBOUND(const Cell* v) {
    assert(Any_Wordlike(v));
    if (VAL_WORD_INDEX_I32(v) == INDEX_ATTACHED)
        return true;
    if (VAL_WORD_INDEX_I32(v) < 0)
        assert(IS_DETAILS(BINDING(v)));
    return VAL_WORD_INDEX_I32(v) <= 0;
}

#define IS_WORD_BOUND(v) \
    (not IS_WORD_UNBOUND(v))


INLINE REBINT VAL_WORD_INDEX(const Cell* v) {
    assert(Any_Wordlike(v));
    uint32_t i = VAL_WORD_INDEX_I32(v);
    assert(i > 0);
    return cast(REBLEN, i);
}

INLINE void Unbind_Any_Word(Cell* v) {
    assert(Any_Wordlike(v));
    VAL_WORD_INDEX_I32(v) = 0;
    BINDING(v) = nullptr;
}

INLINE Context* VAL_WORD_CONTEXT(const REBVAL *v) {
    assert(IS_WORD_BOUND(v));
    Stub* binding = BINDING(v);
    if (IS_PATCH(binding)) {
        Context* patch_context = INODE(PatchContext, binding);
        binding = CTX_VARLIST(patch_context);
    }
    else if (IS_LET(binding))
        fail ("LET variables have no context at this time");

    assert(Is_Node_Accessible(binding));
    assert(
        Is_Node_Managed(binding) or
        not Is_Level_Fulfilling(cast(Level*, node_BONUS(KeySource, binding)))
    );
    Set_Node_Managed_Bit(binding);  // !!! review managing needs
    Context* c = cast(Context*, binding);
    return c;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  VARIABLE ACCESS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// When a word is bound to a context by an index, it becomes a means of
// reading and writing from a persistent storage location.  We use "variable"
// or just VAR to refer to REBVAL slots reached via binding in this way.
// More narrowly, a VAR that represents an argument to a function invocation
// may be called an ARG (and an ARG's "persistence" is only as long as that
// function call is on the stack).
//
// All variables can be put in a CELL_FLAG_PROTECTED state.  This is a flag
// on the variable cell itself--not the key--so different instances of
// the same object sharing the keylist don't all have to be protected just
// because one instance is.  This is not one of the flags included in the
// CELL_MASK_COPY, so it shouldn't be able to leak out of the varlist.
//
// The Lookup_Word_May_Fail() function takes the conservative default that
// only const access is needed.  A const pointer to a REBVAL is given back
// which may be inspected, but the contents not modified.  While a bound
// variable that is not currently set will return an antiform void value,
// Lookup_Word_May_Fail() on an *unbound* word will raise an error.
//
// Lookup_Mutable_Word_May_Fail() offers a parallel facility for getting a
// non-const REBVAL back.  It will fail if the variable is either unbound
// -or- marked with OPT_TYPESET_LOCKED to protect against modification.
//

INLINE Value(const*) Lookup_Word_May_Fail(
    const Cell* any_word,
    Specifier* specifier
){
    REBLEN index;
    Series* s = try_unwrap(
        Get_Word_Container(&index, any_word, specifier, ATTACH_READ)
    );
    if (not s)
        fail (Error_Not_Bound_Raw(any_word));
    if (index == INDEX_ATTACHED)
        fail (Error_Unassigned_Attach_Raw(any_word));

    if (IS_LET(s) or IS_PATCH(s))
        return SPECIFIC(Stub_Cell(s));

    Assert_Node_Accessible(s);
    Context* c = cast(Context*, s);
    return CTX_VAR(c, index);
}

INLINE Option(Value(const*)) Lookup_Word(
    const Cell* any_word,
    Specifier* specifier
){
    REBLEN index;
    Series* s = try_unwrap(
        Get_Word_Container(&index, any_word, specifier, ATTACH_READ)
    );
    if (not s or index == INDEX_ATTACHED)
        return nullptr;
    if (IS_LET(s) or IS_PATCH(s))
        return SPECIFIC(Stub_Cell(s));

    Assert_Node_Accessible(s);
    Context* c = cast(Context*, s);
    return CTX_VAR(c, index);
}

INLINE const REBVAL *Get_Word_May_Fail(
    Cell* out,
    const Cell* any_word,
    Specifier* specifier
){
    const REBVAL *var = Lookup_Word_May_Fail(any_word, specifier);
    if (Is_Antiform(var) and not Is_Logic(var))
        fail (Error_Bad_Word_Get(any_word, var));

    return Copy_Cell(out, var);
}

INLINE REBVAL *Lookup_Mutable_Word_May_Fail(
    const Cell* any_word,
    Specifier* specifier
){
    REBLEN index;
    Series* s = try_unwrap(
        Get_Word_Container(&index, any_word, specifier, ATTACH_WRITE)
    );
    if (not s)
        fail (Error_Not_Bound_Raw(any_word));

    REBVAL *var;
    if (IS_LET(s) or IS_PATCH(s))
        var = SPECIFIC(Stub_Cell(s));
    else {
        Context* c = cast(Context*, s);

        // A context can be permanently frozen (`lock obj`) or temporarily
        // protected, e.g. `protect obj | unprotect obj`.  A native will
        // use SERIES_FLAG_HOLD on a FRAME! context in order to prevent
        // setting values to types with bit patterns the C might crash on.
        //
        // Lock bits are all in SER->info and checked in the same instruction.
        //
        Fail_If_Read_Only_Series(CTX_VARLIST(c));

        var = CTX_VAR(c, index);
    }

    // The PROTECT command has a finer-grained granularity for marking
    // not just contexts, but individual fields as protected.
    //
    if (Get_Cell_Flag(var, PROTECTED)) {
        DECLARE_LOCAL (unwritable);
        Init_Word(unwritable, Cell_Word_Symbol(any_word));
        fail (Error_Protected_Word_Raw(unwritable));
    }

    return var;
}

INLINE REBVAL *Sink_Word_May_Fail(
    const Cell* any_word,
    Specifier* specifier
){
    REBVAL *var = Lookup_Mutable_Word_May_Fail(any_word, specifier);
    return FRESHEN(var);
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
// solution in C89.
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
    assert(IS_LET(specifier) or IS_USE(specifier));
    while (
        NextVirtual(specifier) != nullptr
        and not IS_VARLIST(NextVirtual(specifier))
    ){
        specifier = NextVirtual(specifier);
    }
    return &node_LINK(NextLet, specifier);
}

INLINE Option(Context*) SPC_FRAME_CTX(Specifier* specifier)
{
    if (specifier == UNBOUND)  // !!! have caller check?
        return nullptr;
    if (IS_VARLIST(specifier))
        return cast(Context*, specifier);
    return cast(Context*, x_cast(Node*, *SPC_FRAME_CTX_ADDRESS(specifier)));
}


// An ANY-ARRAY! cell has a pointer's-worth of spare space in it, which is
// used to keep track of the information required to further resolve the
// words and arrays that reside in it.
//
INLINE Specifier* Derive_Specifier(
    Specifier* specifier,
    NoQuote(const Cell*) any_array
){
    if (BINDING(any_array) != UNBOUND)
        return BINDING(any_array);

    return specifier;
}


//
// BINDING CONVENIENCE MACROS
//
// WARNING: Don't pass these routines something like a singular REBVAL* (such
// as a REB_BLOCK) which you wish to have bound.  You must pass its *contents*
// as an array...as the plural "values" in the name implies!
//
// So don't do this:
//
//     REBVAL *block = ARG(block);
//     REBVAL *something = ARG(next_arg_after_block);
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
    Bind_Values_Core((at), (tail), (context), TS_WORD, 0, BIND_DEEP)

#define Bind_Values_All_Deep(at,tail,context) \
    Bind_Values_Core((at), (tail), (context), TS_WORD, TS_WORD, BIND_DEEP)

#define Bind_Values_Shallow(at,tail,context) \
    Bind_Values_Core((at), (tail), (context), TS_WORD, 0, BIND_0)

// Gave this a complex name to warn of its peculiarities.  Calling with
// just BIND_SET is shallow and tricky because the set words must occur
// before the uses (to be applied to bindings of those uses)!
//
#define Bind_Values_Set_Midstream_Shallow(at,tail,context) \
    Bind_Values_Core( \
        (at), (tail), (context), TS_WORD, FLAGIT_KIND(REB_SET_WORD), BIND_0)

#define Unbind_Values_Deep(at,tail) \
    Unbind_Values_Core((at), (tail), nullptr, true)
