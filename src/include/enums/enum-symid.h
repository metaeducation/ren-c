//
//  file: %enum-symid.h
//  summary: "Small 16-bit integers for built-in symbols"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// Built-in symbols get a hardcoded integer number that can be used in the
// C code--for instance in switch() statements.  However, any symbols which
// are not in the hardcoded table have a symbol ID of 0.
//
// We want to avoid bugs that can happen when you say things like:
//
//     if (Cell_Word_Id(a) == Cell_Word_Id(b)) { ... }
//
// If you were allowed to do that, then all non-built-ins would give back
// SYM_) and appear to be equal.  It's a tricky enough bug to catch to warrant
// an extra check in C++ that stops comparing SymId that may be 0 with `==`
//
// So we wrap the enum into an Option(), which the C++ build is able to do
// added type checking on.  It also prohibits comparisons unless you unwrap
// the values, which in checked builds has a runtime check of non-zeroness.
//

#include "tmp-symid.h"  // enum built by %make-boot.r

typedef enum SymIdEnum SymId;

typedef uint_fast16_t SymId16;  // 16 bits for SymId in symbol header

#define SYM_0 \
    u_cast(Option(SymId), SYM_0_constexpr)

#if NEEDFUL_OPTION_USES_WRAPPER
    bool operator==(Option(SymId)& a, Option(SymId)& b) = delete;
    bool operator!=(Option(SymId)& a, Option(SymId)& b) = delete;
#endif
