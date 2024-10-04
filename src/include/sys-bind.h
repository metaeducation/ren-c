//
//  File: %sys-bind.h
//  Summary: "System Binding Include"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// R3-Alpha had a per-thread "bind table"; a large and sparsely populated hash
// into which index numbers would be placed, for what index those words would
// have as keys or parameters.  Ren-C's strategy is that binding information
// is wedged into Stub nodes that represent the canon words themselves.
//
// This would create problems if multiple threads were trying to bind at the
// same time.  While threading was never realized in R3-Alpha, Ren-C doesn't
// want to have any "less of a plan".  So the Reb_Binder is used by binding
// clients as a placeholder for whatever actual state would be used to augment
// the information in the canon word series about which client is making a
// request.  This could be coupled with some kind of lockfree adjustment
// strategy whereby a word that was contentious would cause a structure to
// "pop out" and be pointed to by some atomic thing inside the word.
//
// For the moment, a binder has some influence by saying whether the high 16
// bits or low 16 bits of the canon's misc.index are used.  If the index
// were atomic this would--for instance--allow two clients to bind at once.
// It's just a demonstration of where more general logic using atomics
// that could work for N clients would be.
//
// The debug build also adds another feature, that makes sure the clear count
// matches the set count.
//


#ifdef NDEBUG
    #define SPC(p) \
        cast(Specifier*, (p))  // makes UNBOUND look like SPECIFIED

    #define VAL_SPECIFIER(v) \
        SPC(v->extra.binding)
#else
    INLINE Specifier* SPC(void *p) {
        assert(p != SPECIFIED);  // use SPECIFIED, not SPC(SPECIFIED)

        VarList* c = CTX(p);
        assert(CTX_TYPE(c) == REB_FRAME);

        // Note: May be managed or unamanged.

        return cast(Specifier*, c);
    }

    INLINE Specifier* VAL_SPECIFIER(const Value* v) {
        assert(Any_List(v));
        if (not v->extra.binding)
            return SPECIFIED;

        // While an ANY-WORD! can be bound specifically to an arbitrary
        // object, an ANY-ARRAY! only becomes bound specifically to frames.
        // The keylist for a frame's context should come from a function's
        // paramlist, which should have an ACTION! value in keylist[0]
        //
        VarList* c = CTX(v->extra.binding);
        assert(CTX_TYPE(c) == REB_FRAME); // may be inaccessible
        return cast(Specifier*, c);
    }
#endif


// Tells whether when an ACTION! has a binding to a context, if that binding
// should override the stored binding inside of a WORD! being looked up.
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
INLINE bool Is_Overriding_Context(VarList* stored, VarList* override)
{
    Node* stored_source = LINK(stored).keysource;
    Node* temp = LINK(override).keysource;

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
    if (
        Is_Node_A_Stub(stored_source)
        and cast_Flex(stored_source)->leader.bits & ARRAY_FLAG_IS_PARAMLIST
    ){
        return false;
    }
    if (
        Is_Node_A_Stub(temp)
        and cast_Flex(temp)->leader.bits & ARRAY_FLAG_IS_PARAMLIST
    ){
        return false;
    }

    while (true) {
        if (temp == stored_source)
            return true;

        if (LINK(cast_Flex(temp)).ancestor == temp)
            break;

        temp = LINK(cast_Flex(temp)).ancestor;
    }

    return false;
}


// Modes allowed by Bind related functions:
enum {
    BIND_0 = 0, // Only bind the words found in the context.
    BIND_DEEP = 1 << 1 // Recurse into sub-blocks.
};


struct Reb_Binder {
    VarList* context;
  #if !defined(NDEBUG)
    REBLEN count;
  #endif

  #if CPLUSPLUS_11
    //
    // The C++ debug build can help us make sure that no binder ever fails to
    // get an INIT_BINDER() and SHUTDOWN_BINDER() pair called on it, which
    // would leave lingering binding values on Symbol stubs.
    //
    bool initialized;
    Reb_Binder () { initialized = false; }
    ~Reb_Binder () { assert(not initialized); }
  #endif
};


INLINE void INIT_BINDER(struct Reb_Binder *binder, VarList* context) {
    binder->context = context;

  #if !defined(NDEBUG)
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
    Symbol* canon,
    REBINT index
){
    assert(index != 0);
    assert(Get_Flex_Info(canon, CANON_SYMBOL));

    if (binder->context == Lib_Context) {
        assert(MISC(canon).bind_index.lib == index);
        return true;
    }

    if (MISC(canon).bind_index.other != 0)
        return false;
    MISC(canon).bind_index.other = index;

  #if !defined(NDEBUG)
    ++binder->count;
  #endif
    return true;
}


INLINE void Add_Binder_Index(
    struct Reb_Binder *binder,
    Symbol* canon,
    REBINT index
){
    bool success = Try_Add_Binder_Index(binder, canon, index);
    assert(success);
    UNUSED(success);
}


INLINE REBINT Get_Binder_Index_Else_0( // 0 if not present
    struct Reb_Binder *binder,
    Symbol* canon
){
    assert(Get_Flex_Info(canon, CANON_SYMBOL));

    if (binder->context == Lib_Context)
        return MISC(canon).bind_index.lib;

    return MISC(canon).bind_index.other;
}


INLINE REBINT Remove_Binder_Index_Else_0( // return old value if there
    struct Reb_Binder *binder,
    Symbol* canon
){
    assert(Get_Flex_Info(canon, CANON_SYMBOL));

    if (binder->context == Lib_Context)
        return MISC(canon).bind_index.lib;

    REBINT old_index = MISC(canon).bind_index.other;
    if (old_index == 0)
        return 0;
    MISC(canon).bind_index.other = 0;

  #if !defined(NDEBUG)
    assert(binder->count > 0);
    --binder->count;
  #endif
    return old_index;
}


INLINE void Remove_Binder_Index(
    struct Reb_Binder *binder,
    Symbol* canon
){
    REBINT old_index = Remove_Binder_Index_Else_0(binder, canon);
    assert(old_index != 0);
    UNUSED(old_index);
}


// Modes allowed by Collect keys functions:
enum {
    COLLECT_ONLY_SET_WORDS = 0,
    COLLECT_ANY_WORD = 1 << 1,
    COLLECT_DEEP = 1 << 2,
    COLLECT_NO_DUP = 1 << 3, // Do not allow dups during collection (for specs)
    COLLECT_ENSURE_SELF = 1 << 4, // !!! Ensure SYM_SELF in context (temp)
    COLLECT_AS_TYPESET = 1 << 5
};

struct Reb_Collector {
    Flags flags;
    StackIndex base;
    struct Reb_Binder binder;
    REBLEN index;
};


// The process of derelativization will resolve a relative value with a
// specific one--storing frame references into cells.  But once that has
// happened, the cell may outlive the frame...but the binding override that
// the frame contributed might still matter.
//
// !!! The functioning of Decay_Flex() should be reviewed to see if it
// actually needs to preserve the Varlist_Archetype().  It's not entirely clear
// if the scenarios are meaningful--but Derelativize cannot fail(), and
// it would without this.  It might also put in some "fake" element that
// would fail later, but given that the Level's captured binding can outlive
// the frame that might lose important functionality.
//
INLINE Stub* SPC_BINDING(Specifier* specifier)
{
    assert(specifier != UNBOUND);
    Value* rootvar = Varlist_Archetype(CTX(specifier)); // works even if Decay()d
    assert(Is_Frame(rootvar));
    return rootvar->extra.binding;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  VARIABLE ACCESS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// When a word is bound to a context by an index, it becomes a means of
// reading and writing from a persistent storage location.  We use "variable"
// or just VAR to refer to Value slots reached via binding in this way.
// More narrowly, a VAR that represents an argument to a function invocation
// may be called an ARG (and an ARG's "persistence" is only as long as that
// function call is on the stack).
//
// All variables can be put in a CELL_FLAG_PROTECTED state.  This is a flag
// on the variable cell itself--not the key--so different instances of
// the same object sharing the keylist don't all have to be protected just
// because one instance is.  This is not one of the flags included in the
// CELL_MASK_COPIED, so it shouldn't be able to leak out of the varlist.
//
// The Get_Opt_Var_May_Fail() function takes the conservative default that
// only const access is needed.  A const pointer to a Value is given back
// which may be inspected, but the contents not modified.  While a bound
// variable that is not currently set will return a REB_MAX_NULLED value,
// Get_Opt_Var_May_Fail() on an *unbound* word will raise an error.
//
// Get_Mutable_Var_May_Fail() offers a parallel facility for getting a
// non-const Value back.  It will fail if the variable is either unbound
// -or- marked with OPT_TYPESET_LOCKED to protect against modification.
//


// Get the word--variable--value. (Generally, use the macros like
// GET_VAR or GET_MUTABLE_VAR instead of this).  This routine is
// called quite a lot and so attention to performance is important.
//
// Coded assuming most common case is to give an error on unbounds, and
// that only read access is requested (so no checking on protection)
//
// Due to the performance-critical nature of this routine, it is declared
// as inline so that locations using it can avoid overhead in invocation.
//
INLINE VarList* Get_Var_Context(
    const Cell* any_word,
    Specifier* specifier
){
    assert(Any_Word(any_word));

    Stub* binding = VAL_BINDING(any_word);
    assert(binding); // caller should check so context won't be null

    VarList* c;

    if (binding->leader.bits & ARRAY_FLAG_IS_VARLIST) {

        // SPECIFIC BINDING: The context the word is bound to is explicitly
        // contained in the `any_word` Value payload.  Extract it, but check
        // to see if there is an override via "DERIVED BINDING", e.g.:
        //
        //    o1: make object [a: 10 f: method [] [print a]]
        //    o2: make o1 [a: 20]
        //
        // O2 doesn't copy F's body, but its copy of the ACTION! cell in o2/f
        // gets its ->binding to point at O2 instead of O1.  When o2/f runs,
        // the frame stores that pointer, and we take it into account when
        // looking up `a` here, instead of using a's stored binding directly.

        c = CTX(binding); // start with stored binding

        if (specifier == SPECIFIED) {
            //
            // Lookup must be determined solely from bits in the value
            //
        }
        else {
            Stub* f_binding = SPC_BINDING(specifier);  // can't fail()
            if (f_binding and Is_Overriding_Context(c, CTX(f_binding))) {
                //
                // The specifier binding overrides--because what's happening
                // is that this cell came from a METHOD's body, where the
                // particular ACTION! value cell triggering it held a binding
                // of a more derived version of the object to which the
                // instance in the method body refers.
                //
                c = CTX(f_binding);
            }
        }
    }
    else {
        assert(binding->leader.bits & ARRAY_FLAG_IS_PARAMLIST);

        // RELATIVE BINDING: The word was made during a deep copy of the block
        // that was given as a function's body, and stored a reference to that
        // ACTION! as its binding.  To get a variable for the word, we must
        // find the right function call on the stack (if any) for the word to
        // refer to (the FRAME!)

      #if !defined(NDEBUG)
        if (specifier == SPECIFIED) {
            printf("Get_Context_Core on relative value without specifier\n");
            panic (any_word);
        }
      #endif

        c = CTX(specifier);

        // The underlying function is used for all relative bindings.  If it
        // were not, then the same function body could not be repurposed for
        // dispatch e.g. in copied, hijacked, or adapted code, because the
        // identity of the derived function would not match up with the body
        // it intended to reuse.
        //
        assert(binding == ACT_UNDERLYING(VAL_ACTION(CTX_ROOTKEY(c))));
    }

  #ifdef DEBUG_BINDING_NAME_MATCH // this is expensive, and hasn't happened
    assert(
        VAL_WORD_CANON(any_word)
        == Key_Canon(Varlist_Key(c, VAL_WORD_INDEX(any_word))));
  #endif

    FAIL_IF_INACCESSIBLE_CTX(c); // usually Cell_Varlist() checks, need to here
    return c;
}

INLINE const Value* Get_Opt_Var_May_Fail(
    const Cell* any_word,
    Specifier* specifier
){
    if (not VAL_BINDING(any_word))
        fail (Error_Not_Bound_Raw(KNOWN(any_word)));

    VarList* c = Get_Var_Context(any_word, specifier);
    if (Get_Flex_Info(c, INACCESSIBLE))
        fail (Error_No_Relative_Core(any_word));

    return Varlist_Slot(c, VAL_WORD_INDEX(any_word));
}

INLINE const Value* Try_Get_Opt_Var(
    const Cell* any_word,
    Specifier* specifier
){
    if (not VAL_BINDING(any_word))
        return nullptr;

    VarList* c = Get_Var_Context(any_word, specifier);
    if (Get_Flex_Info(c, INACCESSIBLE))
        return nullptr;

    return Varlist_Slot(c, VAL_WORD_INDEX(any_word));
}

INLINE void Move_Opt_Var_May_Fail(
    Value* out,
    const Cell* any_word,
    Specifier* specifier
){
    Copy_Cell(out, Get_Opt_Var_May_Fail(any_word, specifier));
}

INLINE Value* Get_Mutable_Var_May_Fail(
    const Cell* any_word,
    Specifier* specifier
){
    if (not VAL_BINDING(any_word))
        fail (Error_Not_Bound_Raw(KNOWN(any_word)));

    VarList* context = Get_Var_Context(any_word, specifier);

    // A context can be permanently frozen (`lock obj`) or temporarily
    // protected, e.g. `protect obj | unprotect obj`.  A native will
    // use FLEX_FLAG_HOLD on a FRAME! context in order to prevent
    // setting values to types with bit patterns the C might crash on.
    //
    // Lock bits are all in SER->info and checked in the same instruction.
    //
    FAIL_IF_READ_ONLY_CONTEXT(context);

    Value* var = Varlist_Slot(context, VAL_WORD_INDEX(any_word));

    // The PROTECT command has a finer-grained granularity for marking
    // not just contexts, but individual fields as protected.
    //
    if (GET_VAL_FLAG(var, CELL_FLAG_PROTECTED)) {
        DECLARE_VALUE (unwritable);
        Init_Word(unwritable, Cell_Word_Symbol(any_word));
        fail (Error_Protected_Word_Raw(unwritable));
    }

    return var;
}

INLINE Value* Sink_Var_May_Fail(
    const Cell* any_word,
    Specifier* specifier
){
    Value* var = Get_Mutable_Var_May_Fail(any_word, specifier);
    Erase_Cell(var);
    return var;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  COPYING RELATIVE VALUES TO SPECIFIC
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This can be used to turn a Cell into a Value.  If the Cell is indeed
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

INLINE Value* Derelativize(
    Cell* out, // relative destinations are overwritten with specified value
    const Cell* v,
    Specifier* specifier
){
    Move_Value_Header(out, v);
    out->payload = v->payload;

    if (Not_Bindable(v)) {
        out->extra = v->extra; // extra.binding union field isn't even active
        return KNOWN(out);
    }

    Stub* binding = v->extra.binding;

    if (not binding) {
        out->extra.binding = UNBOUND;
    }
    else if (binding->leader.bits & ARRAY_FLAG_IS_PARAMLIST) {
        //
        // The stored binding is relative to a function, and so the specifier
        // needs to be a frame to have a precise invocation to lookup in.

        assert(Any_Word(v) or Any_List(v));

      #if !defined(NDEBUG)
        if (not specifier) {
            printf("Relative item used with SPECIFIED\n");
            panic (v);
        }

        // The underlying function is always what's stored in the binding,
        // and what is checked here.  If it were not, then hijackings or
        // COPY'd actions, or adapted preludes, could not match up with the
        // identity of the derived action put in the specifier--and would
        // have to know how to make copies of any relativized action bodies.
        //
        // Despite the more general nature of the underlying action, a given
        // relativization *should* be unambiguous, as arrays are only relative
        // to one action at a time (each time arrays are copied derelativizes,
        // such as when creating a new action using relative material, and
        // then adding in the new relativism).
        //
        Value* rootkey = CTX_ROOTKEY(CTX(specifier));
        if (binding != ACT_UNDERLYING(VAL_ACTION(rootkey))) {
            printf("Function mismatch in specific binding, expected:\n");
            PROBE(ACT_ARCHETYPE(ACT(binding)));
            printf("Panic on relative value\n");
            panic (v);
        }
      #endif

        INIT_BINDING_MAY_MANAGE(out, specifier);
    }
    else if (specifier and (binding->leader.bits & ARRAY_FLAG_IS_VARLIST)) {
        Stub* f_binding = SPC_BINDING(specifier);  // can't fail(), see notes

        if (
            f_binding
            and Is_Overriding_Context(CTX(binding), CTX(f_binding))
        ){
            // !!! Repeats code in Get_Var_Core, see explanation there
            //
            INIT_BINDING_MAY_MANAGE(out, f_binding);
        }
        else
            INIT_BINDING_MAY_MANAGE(out, binding);
    }
    else { // no potential override
        assert(
            (binding->leader.bits & ARRAY_FLAG_IS_VARLIST)
            or Is_Varargs(v) // BLOCK! style varargs use binding to hold array
        );
        INIT_BINDING_MAY_MANAGE(out, binding);
    }

    // in case the caller had a relative value slot and wants to use its
    // known non-relative form... this is inline, so no cost if not used.
    //
    return KNOWN(out);
}


// In the C++ build, defining this overload that takes a Value* instead of
// a Cell*, and then not defining it...will tell you that you do not need
// to use Derelativize.  Juse Copy_Cell() if your source is a Value!
//
#if CPLUSPLUS_11
    Value* Derelativize(Cell* dest, const Value* v, Specifier* specifier);
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  DETERMINING SPECIFIER FOR CHILDREN IN AN ARRAY
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A relative array must be combined with a specifier in order to find the
// actual context instance where its values can be found.  Since today's
// specifiers are always nothing or a FRAME!'s context, this is fairly easy...
// if you find a specific child value living inside a relative array then
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

INLINE Specifier* Derive_Specifier(Specifier* parent, const Cell* item) {
    if (IS_SPECIFIC(item))
        return VAL_SPECIFIER(KNOWN(item));;
    return parent;
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
//     Bind_Values_Deep(VAL_ARRAY_HEAD(block), context);
//
// That will pass the address of the first value element of the block's
// contents.  You could use a later value element, but note that the interface
// as written doesn't have a length limit.  So although you can control where
// it starts, it will keep binding until it hits an end marker.
//

#define Bind_Values_Deep(values,context) \
    Bind_Values_Core((values), (context), TS_WORD, 0, BIND_DEEP)

#define Bind_Values_All_Deep(values,context) \
    Bind_Values_Core((values), (context), TS_WORD, TS_WORD, BIND_DEEP)

#define Bind_Values_Shallow(values, context) \
    Bind_Values_Core((values), (context), TS_WORD, 0, BIND_0)

// Gave this a complex name to warn of its peculiarities.  Calling with
// just BIND_SET is shallow and tricky because the set words must occur
// before the uses (to be applied to bindings of those uses)!
//
#define Bind_Values_Set_Midstream_Shallow(values, context) \
    Bind_Values_Core( \
        (values), (context), TS_WORD, FLAGIT_KIND(REB_SET_WORD), BIND_0)

#define Unbind_Values_Deep(values) \
    Unbind_Values_Core((values), nullptr, true)
