// 0 is defined in C as "conditionally false", while all non-zero things are
// considered "conditionally true".  Yet the language standard mandates that
// comparison operators (==, !=, >, <, etc) will return either 0 or 1, and
// the C++ language standard defines conversion of its built-in boolean type
// to an integral value as either 0 or 1.
//
// This could be exploited by optimized code *IF* it could truly trust a true
// "boolean" is exactly 0 or 1. But unfortunately, C only standardized an
// actual boolean type in C99 with <stdbool.h>.  Older compilers have to use
// integral types for booleans, and may wind up with bugs like this:
//
//     #define fake_bool int
//     int My_Optimized_Function(fake_bool bit) {
//         return bit << 4; // should be 16 if logic is TRUE, 0 if FALSE
//     }
//     int zero_or_sixteen = My_Optimized_Function(flags & SOME_BIT_FLAG);
//
// This code shims in a definition of bool as 0 or 1 for older compilers.
// It is *not safe* in a general sense, but since parts of the code use the
// word `bool` it would have to be defined *somewhere* for the code to compile
// on platforms without it!


#ifdef __cplusplus
    // bool, true, and false defined in the language since the beginning
#else
  #if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L // C99 or later
    #include <stdbool.h>
  #else
    //
    // Follow the pattern of pstdint.h for announcing that the standard bool
    // shim has had an effect.  This will signal rebol.h *not* to auto-include
    // <stdbool.h>, as that would cause a problem.
    //
    #ifndef _PSTDBOOL_H_INCLUDED
        #if !defined(true)
            #define true 1
        #endif
        #if !defined(false)
            #define false 0
        #endif
        typedef int_fast8_t bool; // fastest type that can represent 8-bits
    #endif
  #endif

  #if TO_AMIGA
    //
    // Note to anyone porting to Amiga: it has BOOL which could be used
    // for a bool shim
    //
  #endif
#endif
