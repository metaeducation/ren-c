//
//  File: %reb-host.h
//  Summary: "Include files for hosting"
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

#include <stddef.h> // size_t, wchar_t and other definitions API use

#include "reb-config.h"

// %reb-host.h is often used in third party code that was not written to
// use REBOOL.  Hence the definitions of TRUE and FALSE used in the "fake"
// build will trip it up.  We substitute in normal definitions for this
// file.  See explanations of this test in %reb-c.h for more information.
//
#undef STRICT_BOOL_COMPILER_TEST

#include "reb-c.h"
#include "reb-ext.h"        // includes reb-defs.h

// Must be defined at the end of reb-defs.h, but not *in* reb-defs.h so that
// files including sys-core.h and reb-host.h can have differing definitions of
// REBCHR.  (We want it opaque to the core, but the host to have it compatible
// with the native character type w/o casting)
//
// !!! This should become obsolete when all string exchanges with non-core
// code are done via STRING! values.
//
#ifdef OS_WIDE_CHAR
    typedef wchar_t REBCHR;
#else
    typedef char REBCHR;
#endif

#include "reb-device.h"
#include "reb-file.h"
#include "reb-event.h"
#include "reb-evtypes.h"

#include "sys-rebnod.h" // !!! Legacy dependency, REBGOB should not be REBNOD
#include "reb-gob.h"

#include "reb-lib.h"

#include "host-lib.h"

