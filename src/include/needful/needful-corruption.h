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
// Helper stuct for Corrupt_If_Needful() with generalized memset() fallback.
//
// Function templates can't be partially specialized, class templates can.
// Using a struct allows us to provide custom behavior for type families
// (e.g. "all classes derived from some Base") while still being able to
// have a generic fallback.  (A function template would see a generic
// fallback as ambiguous with SFINAE trying to "carve up" the space.)
//
// 1. It's unsafe to memory fill an arbitrary C++ class by value with
//    garbage bytes, because they can have extra vtables and such--you
//    can overwrite private compiler data.  But this is a C codebase
//    which uses just a few C++ features.  If you don't have virtual
//    methods then is_standard_layout<> should be true, and the memset()
//    shouldn't be a problem...
//
// 2. See definition of Cell and Mem_Fill() for why casting to void* is
//    needed.  (Mem_Fill() macro is not defined in the Needful library)
//
// 3. Having tried a lot of variations of this code...including using masking
//    to avoid branching...it seems that using uint_fast8_t and decrement
//    with a test against 0 is about the fastest way to get good perodicity
//    of zeroing and non-zeroing.
//
// 4. Use macro for efficiency, avoid another function call overhead
//
//    decltype(ref) deduces the type of ref (incl. reference/cv qualifiers)
//
//    std::remove_reference strips the reference, so the template matches
//    the CorruptHelper<T> specializations
//
#if (! NEEDFUL_DOES_CORRUPTIONS)
    #define Corrupt_If_Needful(x)  NOOP

    #define NEEDFUL_USES_CORRUPT_HELPER  0

#elif (! CPLUSPLUS_11)
    #define Corrupt_If_Needful(x) \
        memset(&(x), 0xBD, sizeof(x))  // C99 fallback mechanism

    #define NEEDFUL_USES_CORRUPT_HELPER  0
#else
    #include <cstring>  // for memset

    template<typename T, typename Enable = void>
    struct CorruptHelper {
      static_assert(
        std::is_standard_layout<T>::value,  // would break C++ [1]
        "Cannot memset() a C++ struct or class that's not standard layout"
      );

      static_assert(  // see common specializations later in this file
        not std::is_pointer<T>::value
            and not std::is_fundamental<T>::value
            and not std::is_enum<T>::value,
        "Fallback CorruptHelper<T> should be overridden by specialization"
      );

      static void corrupt(T& ref) {  // fallback if no other specialization
      #if NEEDFUL_PSEUDO_RANDOM_CORRUPTIONS
        static uint_fast8_t countdown = NEEDFUL_CORRUPTION_SEED;
        memset(
            u_cast(void*, &ref),  // void* cast needed [2]
            countdown,  // countdown does double-duty as the fill byte
            sizeof(T)
        );
        if (countdown == 0)
            countdown = NEEDFUL_CORRUPTION_DOSE;
        --countdown;  // `else` to avoid decrementing would slow it down [3]
      #else
        memset(u_cast(void*, &ref), 0xBD, sizeof(T));
      #endif
      }
    };

    #define Corrupt_If_Needful(ref) /* macro for efficiency [4] */ \
        CorruptHelper<rr_decltype(ref)>::corrupt(ref)

    #define NEEDFUL_USES_CORRUPT_HELPER  1
#endif


//=//// POINTER CORRUPTION ////////////////////////////////////////////////=//
//
// 1. Unlike the standard memset() fallback which doesn't know what it's
//    corrupting, this pointer corrupter knows...and there's not a lot of
//    good reason to pay additional cost to try and randomize states vs.
//    "bad pointer" and "null pointer".

#if NEEDFUL_USES_CORRUPT_HELPER
    template<typename T>
    struct CorruptHelper<T*> {  // Pointer (faster than memset() fallback)
      static void corrupt(T*& ref) {
      #if NEEDFUL_PSEUDO_RANDOM_CORRUPTIONS
        static uint_fast8_t countdown = NEEDFUL_CORRUPTION_SEED;
        if (countdown == 0) {
            ref = nullptr;  // nullptr occasionally, deterministic
            countdown = NEEDFUL_CORRUPTION_DOSE;
        }
        else {
            ref = p_cast(T*, u_cast(intptr_t, 0xDECAFBAD));  // fixed value [1]
            --countdown;
        }
      #else
        ref = p_cast(T*, u_cast(intptr_t, 0xDECAFBAD));  // fixed value [1]
      #endif
      }
    };
#endif


//=//// BOOLEAN CORRUPTION (MUST FLUCTUATE TRUE + FALSE) //////////////////=//
//
// 1. Booleans are special in the sense that writing a fixed garbage value
//    into them is not attention-getting, since they're only interpreted as
//    true and false.  Always use pseudorandom values to corrupt them, even
//    if the build requests not to use NEEDFUL_PSEUDO_RANDOM_CORRUPTIONS.
//
#if NEEDFUL_USES_CORRUPT_HELPER
    template<>
    struct CorruptHelper<bool> {
      static void corrupt(bool& ref) {
        possibly(NEEDFUL_PSEUDO_RANDOM_CORRUPTIONS);  // ignore it [1]
        static uint_fast8_t countdown = NEEDFUL_CORRUPTION_SEED;
        ref = (countdown & 0x1);
        if (countdown == 0)
            countdown = NEEDFUL_CORRUPTION_DOSE;
        --countdown;
      }
    };
#endif


//=//// NON-POINTER CORRUPTION FOR FUNDAMENTALS/ENUMS /////////////////////=//
//
#if NEEDFUL_USES_CORRUPT_HELPER
    template<typename T>
    struct CorruptHelper<
        T,  // Integer/bool/float/enum/etc. (faster than memset() fallback)
        typename std::enable_if<
            not std::is_same<T, bool>::value and (
                std::is_fundamental<T>::value
                or std::is_enum<T>::value
            )
        >::type
    >{
      static void corrupt(T& ref) {
      #if NEEDFUL_PSEUDO_RANDOM_CORRUPTIONS
        static uint_fast8_t countdown = NEEDFUL_CORRUPTION_SEED;
        if (countdown == 0) {
            ref = static_cast<T>(0);  // false/0 occasionally, deterministic
            countdown = NEEDFUL_CORRUPTION_DOSE;
        }
        else {
            ref = static_cast<T>(12345678);  // garbage the rest of the time
            --countdown;
        }
      #else
        ref = u_cast(T, 12345678);
      #endif
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

#if NO_CPLUSPLUS_11 || (! NEEDFUL_DOES_CORRUPTIONS)
    #define UNUSED(x) \
        ((void)(x))
#else
    template<typename T>
    void Unused_Helper(const T& ref)  // const reference... can't corrupt
      { USED(ref); }

    template<typename T>
    void Unused_Helper(T& ref)  // mutable references... we can corrupt
      { Corrupt_If_Needful(ref); }

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
#if NEEDFUL_ASSIGNS_UNUSED_FIELDS
  #if RUNTIME_CHECKS
    #define Corrupt_Unused_Field(ref)  Corrupt_If_Needful(ref)
  #else
    #define Corrupt_Unused_Field(ref)  ((ref) = 0)
  #endif
#else
    #define Corrupt_Unused_Field(ref)  NOOP
#endif
