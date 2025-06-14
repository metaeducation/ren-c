//
//  file: %needful-corruption.h
//  summary: "Helpers for deliberately corrupting memory in debug builds"
//  homepage: <needful homepage TBD>
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2025 hostilefork.com
//
// Licensed under the MIT License
//
// https://en.wikipedia.org/wiki/MIT_License
//
//=/////////////////////////////////////////////////////////////////////////=//



//=//// CORRUPTION HELPER /////////////////////////////////////////////////=//
//
// Helper stuct for Corrupt_If_Debug() with generalized memset() fallback.
//
// Function templates can't be partially specialized, class templates can.
// Using a struct allows us to provide custom behavior for type families
// (e.g. "all classes derived from some Base") while still being able to
// have a generic fallback.  (A function template would see a generic
// fallback as ambiguous with SFINAE trying to "carve up" the space.)
//
// 1. We do not do Corrupt_If_Debug() with static analysis, because that would
//    make variables look like they had been assigned to the static analyzer.
//    It should use its own notion of when things are "garbage" (e.g. this
//    allows reporting of use of unassigned values from inline functions.)
//
// 2. It's unsafe to memory fill an arbitrary C++ class by value with
//    garbage bytes, because they can have extra vtables and such--you
//    can overwrite private compiler data.  But this is a C codebase
//    which uses just a few C++ features.  If you don't have virtual
//    methods then is_standard_layout<> should be true, and the memset()
//    shouldn't be a problem...
//
// 3. See definition of Cell and Mem_Set() for why casting to void* is
//    needed.  (Mem_Set() macro that is not defined for %c-enhanced.h)
//
// 4. Use macro for efficiency, avoid another function call overhead
//
//    decltype(ref) deduces the type of ref (incl. reference/cv qualifiers)
//
//    std::remove_reference strips the reference, so the template matches
//    the Corrupter<T> specializations
//
#if (! PERFORM_CORRUPTIONS)

    #define Corrupt_If_Debug(x)  NOOP

    #define USE_CORRUPTER_HELPERS  0

#elif NO_CPLUSPLUS_11
    STATIC_ASSERT(! DEBUG_STATIC_ANALYZING);  // [1]

    #include <string.h>

    #define Corrupt_If_Debug(x) \
        memset(u_cast(void*, &(x)), 0xBD, sizeof(x));

    #define USE_CORRUPTER_HELPERS  0
#else
    STATIC_ASSERT(! DEBUG_STATIC_ANALYZING);  // [1]

    #include <cstring>  // for memset

    template<typename T, typename Enable = void>
    struct Corrupter {
      static void corrupt(T& ref) {  // fallback if no other specialization
        static_assert(
            std::is_standard_layout<T>::value,  // would break C++ [2]
            "Cannot memset() a C++ struct or class that's not standard layout"
        );
        static uint_fast8_t countdown = CORRUPT_IF_DEBUG_SEED;
        if (countdown == 0) {
            memset(u_cast(void*, &ref), 0, sizeof(T));  // cast needed [3]
            countdown = CORRUPT_IF_DEBUG_DOSE;
        }
        else {
            memset(u_cast(void*, &ref), 189, sizeof(T));  // cast needed [3]
            --countdown;
        }
      }
    };

    #define Corrupt_If_Debug(ref) /* macro for efficiency [4] */ \
        Corrupter< \
            typename std::remove_reference<decltype(ref)>::type \
        >::corrupt(ref)

    #define USE_CORRUPTER_HELPERS  1
#endif


//=//// POINTER CORRUPTION ////////////////////////////////////////////////=//
//

#if NO_RUNTIME_CHECKS
    #define Corrupt_Pointer_If_Debug(p)                 NOOP
    #define Corrupt_Function_Pointer_If_Debug(p)        NOOP
#elif NO_CPLUSPLUS_11
    #define Corrupt_Pointer_If_Debug(p) \
        ((p) = p_cast(void*, cast(uintptr_t, 0xDECAFBAD)))

    #define Corrupt_Function_Pointer_If_Debug(p) \
        ((p) = 0)  // is there any way to do this generically in C?

    #define FreeCorrupt_Pointer_Debug(p) \
        ((p) = p_cast(void*, cast(uintptr_t, 0xF4EEF4EE)))

    #define Is_Pointer_Corrupt_Debug(p) \
        ((p) == p_cast(void*, cast(uintptr_t, 0xDECAFBAD)))
#else
    template<class T>
    INLINE void Corrupt_Pointer_If_Debug(T* &p)
      { p = p_cast(T*, u_cast(uintptr_t, 0xDECAFBAD)); }

    #define Corrupt_Function_Pointer_If_Debug Corrupt_Pointer_If_Debug

    template<class T>
    INLINE void FreeCorrupt_Pointer_Debug(T* &p)
      { p = p_cast(T*, u_cast(uintptr_t, 0xF4EEF4EEE)); }

    template<class T>
    INLINE bool Is_Pointer_Corrupt_Debug(T* p)
      { return (p == p_cast(T*, u_cast(uintptr_t, 0xDECAFBAD))); }
#endif

#if USE_CORRUPTER_HELPERS
    template<typename T>
    struct Corrupter<T*> {  // Pointer (faster than memset() generic fallback)
      static void corrupt(T*& ref) {
        static uint_fast8_t countdown = CORRUPT_IF_DEBUG_SEED;
        if (countdown == 0) {
            ref = nullptr;  // nullptr occasionally, deterministic
            countdown = CORRUPT_IF_DEBUG_DOSE;
        }
        else {
            Corrupt_Pointer_If_Debug(ref); // corrupt other half of the time
            --countdown;
        }
      }
    };
#endif


//=//// ARITHMETIC NON-POINTER CORRUPTION ////////////////////////////////=//
//
#if USE_CORRUPTER_HELPERS
    template<typename T>
    struct Corrupter<
        T,  // Integer/bool/float (faster than memset() generic fallback)
        typename std::enable_if<
            not std::is_pointer<T>::value and std::is_arithmetic<T>::value
        >::type
    >{
      static void corrupt(T& ref) {
        static uint_fast8_t countdown = CORRUPT_IF_DEBUG_SEED;
        if (countdown == 0) {
            ref = static_cast<T>(0);  // false/0 occasionally, deterministic
            countdown = CORRUPT_IF_DEBUG_DOSE;
        }
        else {
            ref = static_cast<T>(12345678);  // garbage the rest of the time
            --countdown;
        }
      }
    };
#endif


//=//// MARK UNUSED VARIABLES /////////////////////////////////////////////=//
//
// Used in coordination with the `-Wunused-variable` setting of the compiler.
// While a simple cast to void is what people usually use for this purpose,
// there's some potential for side-effects with volatiles:
//
//   http://stackoverflow.com/a/4030983/211160
//
// The tricks suggested there for avoiding it seem to still trigger warnings
// as compilers get new ones, so assume that won't be an issue.  As an
// added check, this gives the UNUSED() macro "teeth" in C++11:
//
//   http://codereview.stackexchange.com/q/159439
//

#define USED(x) \
    ((void)(x))

#if NO_CPLUSPLUS_11 || (! PERFORM_CORRUPTIONS)
    #define UNUSED(x) \
        ((void)(x))
#else
    template<typename T>
    void Unused_Helper(const T& ref)  // const reference... can't corrupt
      { USED(ref); }

    template<typename T>
    void Unused_Helper(T& ref)  // mutable references... we can corrupt
      { Corrupt_If_Debug(ref); }

    #define UNUSED Unused_Helper
#endif


//=//// CORRUPT UNUSED FIELDS /////////////////////////////////////////////=//
//
// It would seem that structs which don't use their payloads could just leave
// them uninitialized...saving time on the assignments.
//
// Unfortunately, this is a technically gray area in C.  If you try to
// copy the memory of that cell (as cells are often copied), it might be a
// "trap representation".  Reading such representations to copy them...
// even if not interpreted... is undefined behavior:
//
//   https://stackoverflow.com/q/60112841
//   https://stackoverflow.com/q/33393569/
//
// Odds are it would still work fine if you didn't zero them.  However,
// compilers will warn you--especially at higher optimization levels--if
// they notice uninitialized values being used in copies.  This is a bad
// warning to turn off, because it often points out defective code.
//
// So to play it safe and be able to keep warnings on, fields are zeroed out.
// But it's set up as its own independent flag, so that someone looking
// to squeak out a tiny bit more optimization could turn this off in a
// release build.  It would save on a few null assignments.
//
// (In release builds, the fields are assigned 0 because it's presumably a
// fast value to assign as an immediate.  In checked builds, they're assigned
// a corrupt value because it's more likely to cause trouble if accessed.)
//
#if ASSIGN_UNUSED_FIELDS
  #if RUNTIME_CHECKS
    #define Corrupt_Unused_Field(ref)  Corrupt_If_Debug(ref)
  #else
    #define Corrupt_Unused_Field(ref)  ((ref) = 0)
  #endif
#else
    #define Corrupt_Unused_Field(ref)  NOOP
#endif
