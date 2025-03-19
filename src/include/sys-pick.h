//
//  File: %sys-pick.h
//  Summary: "Definitions for Processing Sequence Picking/Poking"
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



// New concept of generic dispatch: use sparse tables which are scanned for
// during the build process to find IMPLEMENT_GENERIC(name, type) instances.
//
// The name is taken in all-caps so we can get a SYM_XXX from token pasting.
//
#define Dispatch_Generic(name,cue,L) \
    Dispatch_Generic_Core( \
        SYM_##name, g_generic_##name, Cell_Heart_Ensure_Noquote(cue), (L) \
    )


// If you pass in a nullptr for the steps in the Get_Var() and Set_Var()
// mechanics, they will disallow groups.  This is a safety measure which helps
// avoid unwanted side effects in SET and GET, and motivates passing in a
// variable that will be assigned a "hardened" path of steps to get to the
// location more repeatedly (e.g. if something like default wanted to make
// sure it updates the same variable it checked to see if it had a value...
// and only run code in groups once.)
//
// Requesting steps will supress that, but sometimes you don't actually need
// the steps (as the evaluator doesn't when doing SET-TUPLE!).  Rather than
// passing a separate flag, the NOTHING_VALUE pointer is used (mutable, but it
// has the protected bit set to avoid accidents)
//
#define GROUPS_OK &PG_Nothing_Value
