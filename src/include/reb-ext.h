//
//  File: %reb-ext.h
//  Summary: "R3-Alpha Extension Mechanism API"
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
// These are definitions that need to be visible to both %a-lib.c and
// "libRebol" clients.
//
// Historically, routines exported as libRebol were prefixed by "RL_"
// (Rebol Lib).  Interactions with the garbage collector were quite shaky,
// because they used their own proxy for REBVAL cells which contained raw
// pointers to series...and generally speaking, raw series pointers were
// being held in arbitrary locations in user code the GC could not find.
//
// Ren-C split this into two kinds of clients: one that can use the internal
// API, including things like PUSH_GUARD_VALUE() and SER_HEAD(), with all
// the powers and responsibility of a native in the EXE.  Then the libRebol
// clients do not know what a REBSER is, they only have REBVAL pointers...
// which are opaque, and they can't pick them apart.  This means the GC
// stays in control.
//
// Clients would use the libRebol API for simple embedding where the concerns
// are mostly easy bridging to run some Rebol code and get information back.
// The internal API is used for extensions or the authoring of "user natives"
// which are Rebol functions whose body is a compiled string of C code.
//

#include "reb-defs.h"
