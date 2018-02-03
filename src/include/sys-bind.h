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
// is wedged into REBSER nodes that represent the canon words themselves.
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

inline static REBOOL Same_Binding(void *a_ptr, void *b_ptr) {
    REBNOD *a = NOD(a_ptr);
    REBNOD *b = NOD(b_ptr);
    if (a == b)
        return TRUE;
    if (IS_CELL(a)) {
        if (IS_CELL(b))
            return FALSE;
        REBFRM *f_a = cast(REBFRM*, a);
        if (f_a->varlist != NULL && NOD(f_a->varlist) == b)
            return TRUE;
        return FALSE;
    }
    if (IS_CELL(b)) {
        REBFRM *f_b = cast(REBFRM*, b);
        if (f_b->varlist != NULL && NOD(f_b->varlist) == a)
            return TRUE;
        return FALSE;
    }
    return FALSE;
}


// Tells whether when a FUNCTION! has a binding to a context, if that binding
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
inline static REBOOL Is_Overriding_Context(REBCTX *stored, REBCTX *override)
{
    REBNOD *stored_keysource = LINK(CTX_VARLIST(stored)).keysource;
    REBNOD *temp = LINK(CTX_VARLIST(override)).keysource;

    // In a FRAME! the "keylist" is actually a paramlist, and the LINK.facade
    // field is used in paramlists (precluding a LINK.ancestor).  Plus, since
    // frames are tied to a function they invoke, they cannot be expanded.
    // For now, deriving from FRAME! is just disabled.
    //
    // Use a faster check for REB_FRAME than CTX_TYPE() == REB_FRAME, since
    // we were extracting keysources anyway. 
    //
    if (
        (
            stored_keysource->header.bits
            & (ARRAY_FLAG_PARAMLIST | NODE_FLAG_CELL)
        ) || (
            temp->header.bits
            & (ARRAY_FLAG_PARAMLIST | NODE_FLAG_CELL)
        )
    ){
        return FALSE; // one or the other are actually FRAME!s
    }

    while (TRUE) {
        if (temp == stored_keysource)
            return TRUE;

        if (NOD(LINK(temp).ancestor) == temp)
            break;

        temp = NOD(LINK(temp).ancestor);
    }

    return FALSE;
}


// Modes allowed by Bind related functions:
enum {
    BIND_0 = 0, // Only bind the words found in the context.
    BIND_DEEP = 1 << 1, // Recurse into sub-blocks.
    BIND_FUNC = 1 << 2 // Recurse into functions.
};


struct Reb_Binder {
    REBOOL high;
#if !defined(NDEBUG)
    REBCNT count;
#endif

#if defined(CPLUSPLUS_11)
    //
    // The C++ debug build can help us make sure that no binder ever fails to
    // get an INIT_BINDER() and SHUTDOWN_BINDER() pair called on it, which
    // would leave lingering binding values on REBSER nodes.
    //
    REBOOL initialized;
    Reb_Binder () { initialized = FALSE; }
    ~Reb_Binder () { assert(initialized == FALSE); }
#endif
};


inline static void INIT_BINDER(struct Reb_Binder *binder) {
    binder->high = TRUE; //LOGICAL(SPORADICALLY(2)); sporadic?

#if !defined(NDEBUG)
    binder->count = 0;

    #ifdef CPLUSPLUS_11
        binder->initialized = TRUE;
    #endif
#endif
}


inline static void SHUTDOWN_BINDER(struct Reb_Binder *binder) {
#ifdef NDEBUG
    UNUSED(binder);
#else
    assert(binder->count == 0);

    #ifdef CPLUSPLUS_11
        binder->initialized = FALSE;
    #endif
#endif
}


// Tries to set the binder index, but return false if already there.
//
inline static REBOOL Try_Add_Binder_Index(
    struct Reb_Binder *binder,
    REBSTR *canon,
    REBINT index
){
    assert(index != 0);
    assert(GET_SER_INFO(canon, STRING_INFO_CANON));
    if (binder->high) {
        if (MISC(canon).bind_index.high != 0)
            return FALSE;
        MISC(canon).bind_index.high = index;
    }
    else {
        if (MISC(canon).bind_index.low != 0)
            return FALSE;
        MISC(canon).bind_index.low = index;
    }

#if !defined(NDEBUG)
    ++binder->count;
#endif
    return TRUE;
}


inline static void Add_Binder_Index(
    struct Reb_Binder *binder,
    REBSTR *canon,
    REBINT index
){
    REBOOL success = Try_Add_Binder_Index(binder, canon, index);

#ifdef NDEBUG
    UNUSED(success);
#else
    assert(success);
#endif
}


inline static REBINT Get_Binder_Index_Else_0( // 0 if not present
    struct Reb_Binder *binder,
    REBSTR *canon
){
    assert(GET_SER_INFO(canon, STRING_INFO_CANON));

    if (binder->high)
        return MISC(canon).bind_index.high;
    else
        return MISC(canon).bind_index.low;
}


inline static REBINT Remove_Binder_Index_Else_0( // return old value if there
    struct Reb_Binder *binder,
    REBSTR *canon
){
    assert(GET_SER_INFO(canon, STRING_INFO_CANON));

    REBINT old_index;
    if (binder->high) {
        old_index = MISC(canon).bind_index.high;
        if (old_index == 0)
            return 0;
        MISC(canon).bind_index.high = 0;
    }
    else {
        old_index = MISC(canon).bind_index.low;
        if (old_index == 0)
            return 0;
        MISC(canon).bind_index.low = 0;
    }

#if !defined(NDEBUG)
    --binder->count;
#endif
    return old_index;
}


inline static void Remove_Binder_Index(
    struct Reb_Binder *binder,
    REBSTR *canon
){
    REBINT old_index = Remove_Binder_Index_Else_0(binder, canon);

#if defined(NDEBUG)
    UNUSED(old_index);
#else
    assert(old_index != 0);
#endif
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
    REBFLGS flags;
    REBDSP dsp_orig;
    struct Reb_Binder binder;
    REBCNT index;
};


//=////////////////////////////////////////////////////////////////////////=//
//
//  COPYING RELATIVE VALUES TO SPECIFIC
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This can be used to turn a RELVAL into a REBVAL.  If the RELVAL is indeed
// relative and needs to be made specific to be put into the target, then the
// specifier is used to do that.
//
// It is nearly as fast as just assigning the value directly in the release
// build, though debug builds assert that the function in the specifier
// indeed matches the target in the relative value (because relative values
// in an array may only be relative to the function that deep copied them, and
// that is the only kind of specifier you can use with them).
//
// Interface designed to line up with Move_Value()
//
// !!! At the moment, there is a fair amount of overlap in this code with
// Get_Var_Core().  One of them resolves a value's real binding and then
// fetches it, while the other resolves a value's real binding but then stores
// that back into another value without fetching it.  This suggests sharing
// a mechanic between both...TBD.
//

inline static REBVAL *Derelativize(
    RELVAL *out, // relative destinations are overwritten with specified value
    const RELVAL *v,
    REBSPC *specifier
){
    Move_Value_Header(out, v);

    if (Not_Bindable(v)) {
        out->extra = v->extra; // extra.binding union field isn't even active
    }
    else if (v->extra.binding == UNBOUND) {
        out->extra.binding = UNBOUND;
    }
    else if (IS_CELL(v->extra.binding)) {
        //
        // This would happen if we allowed cells to point directly to REBFRM*.
        // You could only do this safely for frame variables in the case where
        // that frame wouldn't outlive the frame pointer it was storing...so
        // it wouldn't count when appending cells to BLOCK!s.
        //
        assert(FALSE); // Optimization not yet implemented
    }
    else if (v->extra.binding->header.bits & ARRAY_FLAG_PARAMLIST) {
        //
        // The stored binding is relative to a function, and so the specifier
        // needs to be a frame to have a precise invocation to lookup in.

        assert(ANY_WORD(v) || ANY_ARRAY(v));

    #if !defined(NDEBUG)
        if (specifier == SPECIFIED) {
            printf("Relative item used with SPECIFIED\n");
            panic (v);
        }
    #endif

        if (IS_CELL(specifier)) {
            REBFRM *f = cast(REBFRM*, specifier);

        #if !defined(NDEBUG)
            if (VAL_RELATIVE(v) != FRM_UNDERLYING(f)) {
                printf("Function mismatch in specific binding (TBD)\n");
                printf("Panic on relative value\n");
                panic(v);
            }
        #endif

            // !!! Very conservatively reify.  Should share logic with the
            // innards of Move_Value().  Should specifier always be passed
            // in writable so it can be updated too?
            //
            INIT_BINDING(out, Context_For_Frame_May_Reify_Managed(f));
        }
        else {
        #if !defined(NDEBUG)
            if (
                VAL_RELATIVE(v) !=
                VAL_FUNC(CTX_FRAME_FUNC_VALUE(CTX(specifier)))
            ){
                printf("Function mismatch in specific binding, expected:\n");
                PROBE(FUNC_VALUE(VAL_RELATIVE(v)));
                printf("Panic on relative value\n");
                panic (v);
            }
        #endif
            INIT_BINDING(out, specifier);
        }
    }
    else if (specifier == SPECIFIED) { // no potential override
        assert(v->extra.binding->header.bits & ARRAY_FLAG_VARLIST);
        out->extra.binding = v->extra.binding;
    }
    else {
        assert(v->extra.binding->header.bits & ARRAY_FLAG_VARLIST);

        REBNOD *f_binding;
        if (IS_CELL(specifier))
            f_binding = cast(REBFRM*, specifier)->binding;
        else {
            // !!! Repeats code in Get_Var_Core, see explanation there
            //
            REBVAL *frame_value = CTX_VALUE(CTX(specifier));
            assert(IS_FRAME(frame_value));
            f_binding = frame_value->extra.binding;
        }

        if (
            f_binding != UNBOUND
            && NOT_CELL(f_binding)
            && Is_Overriding_Context(CTX(v->extra.binding), CTX(f_binding))
        ){
            // !!! Repeats code in Get_Var_Core, see explanation there
            //
            INIT_BINDING(out, f_binding);
        }
        else
            out->extra.binding = v->extra.binding;
    }

    out->payload = v->payload;

    // in case the caller had a relative value slot and wants to use its
    // known non-relative form... this is inline, so no cost if not used.
    //
    return KNOWN(out);
}


// In the C++ build, defining this overload that takes a REBVAL* instead of
// a RELVAL*, and then not defining it...will tell you that you do not need
// to use Derelativize.  Juse Move_Value() if your source is a REBVAL!
//
#ifdef CPLUSPLUS_11
    REBVAL *Derelativize(RELVAL *dest, const REBVAL *v, REBSPC *specifier);
#endif


inline static void DS_PUSH_RELVAL(const RELVAL *v, REBSPC *specifier) {
    ASSERT_VALUE_MANAGED(v); // would fail on END marker
    DS_PUSH_TRASH;
    Derelativize(DS_TOP, v, specifier);
}

inline static void DS_PUSH_RELVAL_KEEP_EVAL_FLIP(
    const RELVAL *v,
    REBSPC *specifier
){
    ASSERT_VALUE_MANAGED(v); // would fail on END marker
    DS_PUSH_TRASH;
    REBOOL flip = GET_VAL_FLAG(v, VALUE_FLAG_EVAL_FLIP);
    Derelativize(DS_TOP, v, specifier);
    if (flip)
        SET_VAL_FLAG(DS_TOP, VALUE_FLAG_EVAL_FLIP);
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
// All variables can be put in a protected state where they cannot be written.
// This protection status is marked on the KEY of the context.  Again, more
// narrowly we may refer to a KEY that represents a parameter to a function
// as a PARAM.
//
// The Get_Opt_Var_May_Fail() function takes the conservative default that
// only const access is needed.  A const pointer to a REBVAL is given back
// which may be inspected, but the contents not modified.  While a bound
// variable that is not currently set will return a REB_MAX_VOID value, trying
// to Get_Opt_Var_May_Fail() on an *unbound* word will raise an error.
//
// Get_Mutable_Var_May_Fail() offers a parallel facility for getting a
// non-const REBVAL back.  It will fail if the variable is either unbound
// -or- marked with OPT_TYPESET_LOCKED to protect against modification.
//


enum {
    GETVAR_READ_ONLY = 0,
    GETVAR_MUTABLE = 1 << 0,
    GETVAR_END_IF_UNAVAILABLE = 1 << 1
};


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
inline static REBVAL *Get_Var_Core(
    const RELVAL *any_word,
    REBSPC *specifier,
    REBFLGS flags
){
    assert(ANY_WORD(any_word));

    REBNOD *binding = VAL_BINDING(any_word);

    if (IS_CELL(binding)) {
        //
        // DIRECT BINDING: This will be the case hit when a REBFRM* is used
        // in a word's binding.  The frame should still be on the stack.
        //
        REBFRM *f = cast(REBFRM*, binding);
        REBVAL *var = FRM_ARG(f, VAL_WORD_INDEX(any_word));

        if (flags & GETVAR_MUTABLE) {
            if (f->flags.bits & DO_FLAG_NATIVE_HOLD)
                fail (Error(RE_PROTECTED_WORD, any_word)); // different error?
            
            if (GET_VAL_FLAG(var, CELL_FLAG_PROTECTED))
                fail (Error(RE_PROTECTED_WORD, any_word));
        }

        return var;
    }

    REBCTX *context;

    if (binding->header.bits & ARRAY_FLAG_PARAMLIST) {
        //
        // RELATIVE BINDING: The word was made during a deep copy of the block
        // that was given as a function's body, and stored a reference to that
        // FUNCTION! as its binding.  To get a variable for the word, we must
        // find the right function call on the stack (if any) for the word to
        // refer to (the FRAME!)
        //

    #if !defined(NDEBUG)
        if (specifier == SPECIFIED) {
            printf("Get_Var_Core on relative value without specifier\n");
            panic (any_word);
        }
    #endif

        if (IS_CELL(specifier)) {
            REBFRM *f = cast(REBFRM*, specifier);

            assert(Same_Binding(FRM_UNDERLYING(f), binding));

            REBVAL *var = FRM_ARG(f, VAL_WORD_INDEX(any_word));

            if (flags & GETVAR_MUTABLE) {
                if (f->flags.bits & DO_FLAG_NATIVE_HOLD)
                    fail (Error(RE_PROTECTED_WORD, any_word)); // different?
            
                if (GET_VAL_FLAG(var, CELL_FLAG_PROTECTED))
                    fail (Error(RE_PROTECTED_WORD, any_word));
            }

            return var;
        }

        context = CTX(specifier);
        REBFUN *frm_func = VAL_FUNC(CTX_FRAME_FUNC_VALUE(context));
        assert(Same_Binding(binding, frm_func));
        UNUSED(frm_func);
    }
    else if (binding->header.bits & ARRAY_FLAG_VARLIST) {
        //
        // SPECIFIC BINDING: The context the word is bound to is explicitly
        // contained in the `any_word` REBVAL payload.  Extract it, but check
        // to see if there is an override via "DERIVED BINDING", e.g.:
        //
        //    o1: make object [a: 10 f: does [print a]]
        //    o2: make object [a: 20]
        //
        // O2 doesn't copy F's body, but it does tweak a single pointer in the
        // FUNCTION! value cell (->binding) to point at o2.  When f is called,
        // the frame captures that pointer, and we take it into account here.

        if (specifier == SPECIFIED) {
            //
            // Lookup must be determined solely from bits in the value
        }
        else {
            REBNOD *f_binding;
            if (IS_CELL(specifier))
                f_binding = cast(REBFRM*, specifier)->binding;
            else {
                // Regardless of whether the frame is still on the stack
                // or not, the FRAME! value embedded into the REBSER ndoe
                // should still contain the binding that was inside the cell
                // of the FUNCTION! that was invoked to make the frame.  See
                // INIT_BINDING() in Context_For_Frame_May_Reify_Managed().
                //
                REBVAL *frame_value = CTX_VALUE(CTX(specifier));
                assert(IS_FRAME(frame_value));
                f_binding = frame_value->extra.binding;
            }

            if (
                f_binding != UNBOUND
                && NOT_CELL(f_binding)
                && Is_Overriding_Context(CTX(binding), CTX(f_binding))
            ){
                // The frame's binding overrides--because what's happening is
                // that this cell came from a function's body, where the
                // particular FUNCTION! value triggering it held a binding
                // of a more derived version of the object to which the
                // instance in the function body refers.
                //
                context = CTX(f_binding);
                goto have_context;
            }
        }

        // We use VAL_SPECIFIC_COMMON() here instead of the heavy-checked
        // VAL_WORD_CONTEXT(), because const_KNOWN() checks for specificity
        // and the context operations will ensure it's a context.
        //
        context = VAL_SPECIFIC_COMMON(const_KNOWN(any_word));
    }
    else {
        // UNBOUND: No variable location to retrieve.

        assert(binding == UNBOUND);

        if (flags & GETVAR_END_IF_UNAVAILABLE)
            return m_cast(REBVAL*, END); // only const callers should use

        DECLARE_LOCAL (unbound);
        Init_Word(unbound, VAL_WORD_SPELLING(any_word));
        fail (Error_Not_Bound_Raw(unbound));
    }

    if (CTX_VARS_UNAVAILABLE(context)) {
        //
        // Currently the storage for variables in a function frame are all
        // located on the chunk stack.  So when that level is popped, all the
        // vars will be unavailable.
        //
        // Historically the system became involved with something known as a
        // CLOSURE!, which used non-stack storage (like an OBJECT!) for all of
        // its arguments and locals.  One aspect of closures was that
        // recursions could uniquely identify their bindings (which is now a
        // feature of all functions).  But the other aspect was indefinite
        // lifetime of word bindings "leaked" after the closure was finished.
        //
        // The idea of allowing a single REBSER node to serve for both a
        // durable portion and a stack-lifetime portion of a FRAME! is on the
        // table, but not currently implemented.

        if (flags & GETVAR_END_IF_UNAVAILABLE)
            return m_cast(REBVAL*, END); // only const callers should use

        fail (Error_No_Relative_Core(any_word));
    }

have_context:;
    REBCNT i = VAL_WORD_INDEX(any_word);
    REBVAL *var = CTX_VAR(context, i);

    assert(VAL_WORD_CANON(any_word) == VAL_KEY_CANON(CTX_KEY(context, i)));

    if (flags & GETVAR_MUTABLE) {
        //
        // A context can be permanently frozen (`lock obj`) or temporarily
        // protected, e.g. `protect obj | unprotect obj`.
        //
        // !!! Technically speaking it could also be marked as immutable due
        // to "running", though that feature is not used at this time.
        // All 3 bits are checked in the same instruction.
        //
        FAIL_IF_READ_ONLY_CONTEXT(context);

        // The PROTECT command has a finer-grained granularity for marking
        // not just contexts, but individual fields as protected.
        //
        if (GET_VAL_FLAG(var, CELL_FLAG_PROTECTED)) {
            DECLARE_LOCAL (unwritable);
            Derelativize(unwritable, any_word, specifier);
            fail (Error_Protected_Word_Raw(unwritable));
        }
    }

    assert(!THROWN(var));
    return var;
}

static inline const REBVAL *Get_Opt_Var_May_Fail(
    const RELVAL *any_word,
    REBSPC *specifier
) {
    return Get_Var_Core(any_word, specifier, GETVAR_READ_ONLY);
}

static inline const REBVAL *Get_Opt_Var_Else_End(
    const RELVAL *any_word,
    REBSPC *specifier
) {
    return Get_Var_Core(
        any_word, specifier, GETVAR_READ_ONLY | GETVAR_END_IF_UNAVAILABLE
    );
}

inline static void Copy_Opt_Var_May_Fail(
    REBVAL *out,
    const RELVAL *any_word,
    REBSPC *specifier
) {
    Move_Value(out, Get_Var_Core(any_word, specifier, GETVAR_READ_ONLY));
}

static inline REBVAL *Get_Mutable_Var_May_Fail(
    const RELVAL *any_word,
    REBSPC *specifier
) {
    return Get_Var_Core(any_word, specifier, GETVAR_MUTABLE);
}

#define Sink_Var_May_Fail(any_word,specifier) \
    SINK(Get_Mutable_Var_May_Fail(any_word, specifier))


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

inline static REBSPC *Derive_Specifier(REBSPC *parent, const RELVAL *child) {
    if (IS_SPECIFIC(child))
        return VAL_SPECIFIER(const_KNOWN(child));
    return parent;
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
//     Bind_Values_Deep(VAL_ARRAY_HEAD(block), context);
//
// That will pass the address of the first value element of the block's
// contents.  You could use a later value element, but note that the interface
// as written doesn't have a length limit.  So although you can control where
// it starts, it will keep binding until it hits an end marker.
//

#define Bind_Values_Deep(values,context) \
    Bind_Values_Core((values), (context), TS_ANY_WORD, 0, BIND_DEEP)

#define Bind_Values_All_Deep(values,context) \
    Bind_Values_Core((values), (context), TS_ANY_WORD, TS_ANY_WORD, BIND_DEEP)

#define Bind_Values_Shallow(values, context) \
    Bind_Values_Core((values), (context), TS_ANY_WORD, 0, BIND_0)

// Gave this a complex name to warn of its peculiarities.  Calling with
// just BIND_SET is shallow and tricky because the set words must occur
// before the uses (to be applied to bindings of those uses)!
//
#define Bind_Values_Set_Midstream_Shallow(values, context) \
    Bind_Values_Core( \
        (values), (context), TS_ANY_WORD, FLAGIT_KIND(REB_SET_WORD), BIND_0)

#define Unbind_Values_Deep(values) \
    Unbind_Values_Core((values), NULL, TRUE)

