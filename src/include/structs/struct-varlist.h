//
//  file: %struct-varlist.h
//  summary: "Extremely Simple Symbol/Value Array preceding %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A "VarList" is the abstraction behind OBJECT!, PORT!, FRAME!, ERROR!, etc.
// It maps keys to values using two parallel Flexes, whose indices line up in
// correspondence:
//
//   "KEYLIST" - a Flex of pointer-sized elements holding Symbol* pointers
//
//   "VARLIST" - an Array which holds an archetypal ANY-CONTEXT? value in its
//   [0] element, and then a cell-sized slot for each variable.
//
// A `VarList*` is an alias of the varlist's `Array*`, and keylists are
// reached through the `->link` of the varlist.  The reason varlists
// are used as the identity of the context is that keylists can be shared
// between contexts.
//
// Indices into the arrays are 0-based for keys and 1-based for values, with
// the [0] elements of the varlist used an archetypal value:
//
//    VARLIST ARRAY (aka VarList*) --Bonus--+
//  +------------------------------+        |
//  +          "ROOTVAR"           |        |
//  | Archetype ANY-CONTEXT? Value |        v         KEYLIST SERIES
//  +------------------------------+        +-------------------------------+
//  |         Value Cell 1         |        |         Symbol* Key 1         |
//  +------------------------------+        +-------------------------------+
//  |         Value Cell 2         |        |         Symbol* key 2         |
//  +------------------------------+        +-------------------------------+
//  |         Value Cell ...       |        |         Symbol* key ...       |
//  +------------------------------+        +-------------------------------+
//
// The "ROOTVAR" is used to store a context value.  At one time, this was
// a way of having a Cell instance that represented the object on hand, but
// the permutations of VarList-based types like FRAME! made it impossible
// to consider there being a useful "canon" value.  So it is instead used
// to store what object or frame was derived from.
//
// Contexts coordinate with words, which can have their VAL_WORD_CONTEXT()
// set to a context's Array pointer.  Then they cache the index of that
// word's symbol in the context's KeyList, for a fast lookup to get to the
// corresponding var.
//


//=////////////////////////////////////////////////////////////////////////=//
//
//  KEYLIST DEFINITIONS
//
//=////////////////////////////////////////////////////////////////////////=//

#if CPLUSPLUS_11
    struct KeyList : public Flex {};
#else
    typedef Stub KeyList;
#endif

//=//// KEYLIST_FLAG_SHARED ///////////////////////////////////////////////=//
//
// This is indicated on the keylist array of a context when that same array
// is the keylist for another object.  If this flag is set, then modifying an
// object using that keylist (such as by adding a key/value pair) will require
// that object to make its own copy.
//
// Note: This flag did not exist in R3-Alpha, so all expansions would copy--
// even if expanding the same object by 1 item 100 times with no sharing of
// the keylist.  That would make 100 copies of an arbitrary long keylist that
// the GC would have to clean up.
//
#define KEYLIST_FLAG_SHARED \
    STUB_SUBCLASS_FLAG_24


#define STUB_MASK_KEYLIST \
    (BASE_FLAG_BASE  /* NOT always dynamic */ \
        | FLAG_FLAVOR(FLAVOR_KEYLIST) \
        | STUB_FLAG_LINK_NEEDS_MARK  /* ancestor */ )

#define LINK_KEYLIST_ANCESTOR(keylist)  STUB_LINK(keylist)



//=////////////////////////////////////////////////////////////////////////=//
//
//  VARLIST DEFINITIONS
//
//=////////////////////////////////////////////////////////////////////////=//

#if CPLUSPLUS_11
    struct VarList : public Context {};  // pragmatic inheritance decision [1]
#else
    typedef Stub VarList;
#endif


//=//// VARLIST_FLAG_24 ///////////////////////////////////////////////////=//
//
#define VARLIST_FLAG_24 \
    STUB_SUBCLASS_FLAG_24


//=//// VARLIST_FLAG_FRAME_HAS_BEEN_INVOKED ///////////////////////////////=//
//
// It is intrinsic to the design of Redbols that they are allowed to mutate
// their argument cells.  Hence if you build a frame and then EVAL it, the
// arguments will very likely be changed.  Being able to see these changes
// from the outside in non-debugging cases is dangerous, since it's part of
// the implementation detail of the function (like how it handles locals)
// and is not part of the calling contract.
//
#define VARLIST_FLAG_FRAME_HAS_BEEN_INVOKED \
    STUB_SUBCLASS_FLAG_25


//=//// VARLIST_FLAG_PARAMLIST_LITERAL_FIRST //////////////////////////////=//
//
// This is a calculated property, which is cached by Make_Dispatch_Details().
//
// This is another cached property, needed because lookahead/lookback is done
// so frequently, and it's quicker to check a bit on the function than to
// walk the parameter list every time that function is called.
//
#define VARLIST_FLAG_PARAMLIST_LITERAL_FIRST \
    STUB_SUBCLASS_FLAG_26


//=//// VARLIST_FLAG_IMMUTABLE ////////////////////////////////////////////=//
//
#define VARLIST_FLAG_IMMUTABLE \
    STUB_SUBCLASS_FLAG_27


// These are the flags which are scanned for and set during Make_Phase
//
#define PARAMLIST_MASK_CACHED \
    (PARAMLIST_FLAG_QUOTES_FIRST)



// A context's varlist is always allocated dynamically, in order to speed
// up variable access--no need to test USED_BYTE_OR_255 for 255.
//
// !!! Ideally this would carry a flag to tell a GC "shrinking" process not
// to reclaim the dynamic memory to make a singular cell...but that flag
// can't be FLEX_FLAG_FIXED_SIZE, because most varlists can expand.
//
#define STUB_MASK_LEVEL_VARLIST ( \
    BASE_FLAG_BASE \
        | FLAG_FLAVOR(FLAVOR_VARLIST) \
        | STUB_FLAG_DYNAMIC \
        | 0 /* STUB_FLAG_LINK_NEEDS_MARK */  /* NextVirtual, maybe null */ \
        | (not STUB_FLAG_MISC_NEEDS_MARK)  /* Runlevel, not Adjunct */ \
)

#define STUB_MASK_VARLIST ( \
    STUB_MASK_LEVEL_VARLIST \
        | 0 /* STUB_FLAG_MISC_NEEDS_MARK */  /* Adjunct, maybe null */ \
)

// LINK of VarList is LINK_CONTEXT_INHERIT_BIND
#define BONUS_VARLIST_KEYLIST(varlist)     STUB_BONUS(varlist)
#define MISC_VARLIST_RUNLEVEL(varlist)     (varlist)->misc.p
#define MISC_VARLIST_ADJUNCT(varlist)      STUB_MISC(varlist)


#define Varlist_Array(ctx) \
    u_cast(Array*, ensure(VarList*, ctx))  // performance critical, u_cast()


//=//// ERROR VARLIST SUBLCASS ////////////////////////////////////////////=//
//
// Several implementation functions (e.g. Trap_XXX()) will return an optional
// error.  This isn't very clear as Option(VarList*), so although "Error" is
// a word that conflates the Stub with the ERROR! cell, we go along with
// Option(Error*) as the pragmatically cleanest answer.
//
// 1. Every time a function returning Option(Error*) returned nullptr, I felt
//    inclined to document that as saying "// no error".  It's a little bit
//    of a toss-up as to whether that obfuscates that it's just nullptr, but
//    I think it's more grounding.  At first this was NO_ERROR, but since
//    Windows.h defines that we use SUCCESS.
//
// 2. Error* is a typedef, and as such can't be [[nodiscard]] by itself.  But
//    template specializations can be [[nodiscard]]...so this is helpful in
//    catching cases of someone ignoring a returned Error*.
//
// 3. Enforcement of use of SUCCESS instead of nullptr is done by making a
//    specialization of the OptionWrapper<> template for Error*.  All we
//    want to do is stop it from constructing from nullptr and allow it to
//    initialize with a dummy struct (SuccessSentinal) that is SUCCESS.  But
//    trying to factor out some common OptionWrapperImpl<> class really
//    messes with ambiguities for global comparison operators, and tons of
//    other confusing errors...so the simplest answer is just to write out
//    the specialization as its own class.  Improvements welcome.

#if CPLUSPLUS_11
    struct Error : public VarList {};
#else
    typedef Stub Error;
#endif

#if (! NEEDFUL_OPTION_USES_WRAPPER)
    #define SUCCESS  nullptr
#else
    struct SuccessSentinel {};
    static const SuccessSentinel SUCCESS = SuccessSentinel();  // global ok

  namespace needful {
    template<>  // v-- template specializations can be NODISCARD [2]
    struct NEEDFUL_NODISCARD OptionWrapper<Error*> {  // repeats code [3]
        NEEDFUL_DECLARE_WRAPPED_FIELD (Error*, o);

        OptionWrapper() = default;

        OptionWrapper(SuccessSentinel) : o {nullptr} {}

        OptionWrapper(Error* ptr) : o {ptr} {
            dont(assert(o != nullptr));  // except() macro uses null assign
        }

        OptionWrapper(std::nullptr_t) = delete;  // explicitly disallow

        template<typename X>
        OptionWrapper(const OptionWrapper<X>& other)
            : o {other.o} {
            static_assert(std::is_convertible<X, Error*>::value,
                "Incompatible pointer type");
            assert(o != nullptr and "Use SUCCESS for null values");
        }

        operator uintptr_t() const
          { return reinterpret_cast<uintptr_t>(o); }

        explicit operator Error*() { return o; }

        explicit operator bool()
          { return o != nullptr; }
    };
  }  // end namespace needful
#endif
