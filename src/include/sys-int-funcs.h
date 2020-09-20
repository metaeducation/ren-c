//
//  File: %sys-int-funcs.h
//  Summary: "Integer Datatype Functions"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2014 Atronix Engineering, Inc.
// Copyright 2014-2017 Ren-C Open Source Contributors
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
// To grok these routine names, consider unsigned multiplication:
//
// umull is 'U MUL L' for unsigned multiplication of long
// umulll is 'U MUL LL' for unsigned multiplication of long long
//
// REBU64 may be an unsigned long long of equivalent size to
// unsigned long, and similarly for REBI64 and long long.  But
// the types may be incidentally the same size, if you turn up
// warnings it will require a cast instead of silently passing
// pointers of one to routines expecting a pointer to the other.
// So we cast to the singularly-long variant before calling any
// of the __builtin 'l' variants with a 64-bit REBU64 or REBI64.
//

#ifndef __SYS_INT_FUNCS_H_
#define __SYS_INT_FUNCS_H_

#if __has_builtin(__builtin_sadd_overflow) || GCC_VERSION_AT_LEAST(5, 1)
    #define REB_I32_ADD_OF(x, y, sum) \
        __builtin_sadd_overflow((x), (y), (sum))
#else
    bool reb_i32_add_overflow(int32_t x, int32_t y, int *sum);
    #define REB_I32_ADD_OF(x, y, sum) \
        reb_i32_add_overflow((x), (y), (sum))
#endif

#if __has_builtin(__builtin_uadd_overflow) || GCC_VERSION_AT_LEAST(5, 1)
    #define REB_U32_ADD_OF(x, y, sum) \
        __builtin_uadd_overflow((x), (y), (sum))
#else
    bool reb_u32_add_overflow(uint32_t x, uint32_t y, unsigned int *sum);
    #define REB_U32_ADD_OF(x, y, sum) \
        reb_u32_add_overflow((x), (y), (sum))
#endif

#if __has_builtin(__builtin_saddl_overflow) && __has_builtin(__builtin_saddll_overflow) || GCC_VERSION_AT_LEAST(5, 1)
    #ifdef __LP64__
        #define REB_INTPTR_ADD_OF(x, y, sum) \
            __builtin_saddl_overflow((x), (y), cast(long*, sum))
    #else // presumably __LLP64__ or __LP32__
        #define REB_INTPTR_ADD_OF(x, y, sum) \
            __builtin_saddll_overflow((x), (y), (sum))
    #endif
#else
    bool reb_intptr_add_overflow(int64_t x, int64_t y, int64_t *sum);
    #define REB_INTPTR_ADD_OF(x, y, sum) \
        reb_intptr_add_overflow((x), (y), (sum))
#endif

#if __has_builtin(__builtin_uaddl_overflow) && __has_builtin(__builtin_uaddll_overflow) || GCC_VERSION_AT_LEAST(5, 1)
    #ifdef __LP64__
        #define REB_UINTPTR_ADD_OF(x, y, sum) \
            __builtin_uaddl_overflow((x), (y), cast(unsigned long*, sum))
    #else // presumably __LLP64__ or __LP32__
        #define REB_UINTPTR_ADD_OF(x, y, sum) \
            __builtin_uaddll_overflow((x), (y), (sum))
    #endif
#else
    bool reb_uintptr_add_overflow(uint64_t x, uint64_t y, uint64_t *sum);
    #define REB_UINTPTR_ADD_OF(x, y, sum) \
        reb_uintptr_add_overflow((x), (y), (sum))
#endif

#if __has_builtin(__builtin_ssub_overflow) || GCC_VERSION_AT_LEAST(5, 1)
    #define REB_I32_SUB_OF(x, y, diff) \
        __builtin_ssub_overflow((x), (y), (diff))
#else
    bool reb_i32_sub_overflow(int32_t x, int32_t y, int32_t *diff);
    #define REB_I32_SUB_OF(x, y, diff) \
        reb_i32_sub_overflow((x), (y), (diff))
#endif

#if __has_builtin(__builtin_ssubl_overflow) && __has_builtin(__builtin_ssubll_overflow) || GCC_VERSION_AT_LEAST(5, 1)
    #ifdef __LP64__
        #define REB_INTPTR_SUB_OF(x, y, diff) \
            __builtin_ssubl_overflow((x), (y), cast(long*, (diff)))
    #else
        // presumably __LLP64__ or __LP32__
        //
        #define REB_INTPTR_SUB_OF(x, y, diff) \
            __builtin_ssubll_overflow((x), (y), (diff))
    #endif
#else
    bool reb_intptr_sub_overflow(int64_t x, int64_t y, int64_t *diff);
    #define REB_INTPTR_SUB_OF(x, y, diff) \
        reb_intptr_sub_overflow((x), (y), (diff))
#endif

#if __has_builtin(__builtin_smul_overflow) || GCC_VERSION_AT_LEAST(5, 1)
    #define REB_I32_MUL_OF(x, y, prod) \
        __builtin_smul_overflow((x), (y), (prod))
#else
    bool reb_i32_mul_overflow(int32_t x, int32_t y, int32_t *prod);
    #define REB_I32_MUL_OF(x, y, prod) \
        reb_i32_mul_overflow((x), (y), (prod))
#endif

#if __has_builtin(__builtin_umul_overflow) || GCC_VERSION_AT_LEAST(5, 1)
    #define REB_U32_MUL_OF(x, y, prod) \
        __builtin_umul_overflow((x), (y), (prod))
#else
    bool reb_u32_mul_overflow(uint32_t x, uint32_t y, uint32_t *prod);
    #define REB_U32_MUL_OF(x, y, prod) \
        reb_u32_mul_overflow((x), (y), (prod))
#endif

#if __has_builtin(__builtin_smull_overflow) && __has_builtin(__builtin_smulll_overflow) || GCC_VERSION_AT_LEAST(5, 1)
    #ifdef __LP64__
        #define REB_INTPTR_MUL_OF(x, y, prod) \
            __builtin_smull_overflow((x), (y), cast(long*, prod))
    #elif !defined(__clang__)
        //
        // __builtin_smulll_overflow doesn't work on 32-bit systems yet, causing
        // undefined reference to __mulodi4
        //
        #define REB_INTPTR_MUL_OF(x, y, prod) \
            __builtin_smulll_overflow((x), (y), cast(long long*, prod))
    #else
        // presumably __LLP64__ or __LP32__
        //
        bool reb_intptr_mul_overflow(int64_t x, int64_t y, int64_t *prod);
        #define REB_INTPTR_MUL_OF(x, y, prod) \
            reb_intptr_mul_overflow((x), (y), cast(long long*, prod))
    #endif
#else
    bool reb_intptr_mul_overflow(int64_t x, int64_t y, int64_t *prod);
    #define REB_INTPTR_MUL_OF(x, y, prod) \
        reb_intptr_mul_overflow((x), (y), (prod))
#endif

#if __has_builtin(__builtin_umull_overflow) && __has_builtin(__builtin_umulll_overflow) || GCC_VERSION_AT_LEAST(5, 1)
    #ifdef __LP64__
        #define REB_UINTPTR_MUL_OF(x, y, prod) \
            __builtin_umull_overflow((x), (y), cast(unsigned long*, (prod)))
    #else
        // presumably __LLP64__ or __LP32__
        //
        #define REB_UINTPTR_MUL_OF(x, y, prod) __builtin_umulll_overflow((x), (y), (prod))
    #endif
#else
    bool reb_uintptr_mul_overflow(uint64_t x, uint64_t y, uint64_t *prod);
    #define REB_UINTPTR_MUL_OF(x, y, prod) \
        reb_uintptr_mul_overflow((x), (y), (prod))
#endif

#endif //__SYS_INT_FUNCS_H_
