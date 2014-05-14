/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2014 Atronix Engineering, Inc.
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Summary: Integer Datatype Functions
**  Module:  sys-int-funcs.h
**  Notes:
*/

#ifndef __SYS_INT_FUNCS_H_
#define __SYS_INT_FUNCS_H_

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if __has_builtin(__builtin_sadd_overflow)
#define	REB_I32_ADD_OF(x, y, sum) __builtin_sadd_overflow(x, y, sum)
#else
REBOOL reb_i32_add_overflow(i32 x, i32 y, i32 *sum);
#define	REB_I32_ADD_OF(x, y, sum) reb_i32_add_overflow(x, y, sum)
#endif

#if __has_builtin(__builtin_saddl_overflow) && __has_builtin(__builtin_saddll_overflow)
#ifdef __LP64__
#define	REB_I64_ADD_OF(x, y, sum) __builtin_saddl_overflow(x, y, sum)
#else // presumably __LLP64__ or __LP32__
#define	REB_I64_ADD_OF(x, y, sum) __builtin_saddll_overflow(x, y, sum)
#endif //__LP64__
#else
REBOOL reb_i64_add_overflow(i64 x, i64 y, i64 *sum);
#define	REB_I64_ADD_OF(x, y, sum) reb_i64_add_overflow(x, y, sum)
#endif

#if __has_builtin(__builtin_ssub_overflow)
#define	REB_I32_SUB_OF(x, y, diff) __builtin_ssub_overflow(x, y, diff)
#else
REBOOL reb_i32_sub_overflow(i32 x, i32 y, i32 *diff);
#define	REB_I32_SUB_OF(x, y, diff) reb_i32_sub_overflow(x, y, diff)
#endif

#if __has_builtin(__builtin_ssubl_overflow) && __has_builtin(__builtin_ssubll_overflow)
#ifdef __LP64__
#define	REB_I64_SUB_OF(x, y, diff) __builtin_ssubl_overflow(x, y, diff)
#else // presumably __LLP64__ or __LP32__
#define	REB_I64_SUB_OF(x, y, diff) __builtin_ssubll_overflow(x, y, diff)
#endif //__LP64__
#else
REBOOL reb_i64_sub_overflow(i64 x, i64 y, i64 *diff);
#define	REB_I64_SUB_OF(x, y, diff) reb_i64_sub_overflow(x, y, diff)
#endif

#if __has_builtin(__builtin_smul_overflow)
#define	REB_I32_MUL_OF(x, y, prod) __builtin_smul_overflow(x, y, prod)
#else
REBOOL reb_i32_mul_overflow(i32 x, i32 y, i32 *prod);
#define	REB_I32_MUL_OF(x, y, prod) reb_i32_mul_overflow(x, y, prod)
#endif

#if __has_builtin(__builtin_smull_overflow) && __has_builtin(__builtin_smulll_overflow)
#ifdef __LP64__
#define	REB_I64_MUL_OF(x, y, prod) __builtin_smull_overflow(x, y, prod)
#else // presumably __LLP64__ or __LP32__
#define	REB_I64_MUL_OF(x, y, prod) __builtin_smulll_overflow(x, y, prod)
#endif //__LP64__
#else
REBOOL reb_i64_mul_overflow(i64 x, i64 y, i64 *prod);
#define	REB_I64_MUL_OF(x, y, prod) reb_i64_mul_overflow(x, y, prod)
#endif

#endif //__SYS_INT_FUNCS_H_
