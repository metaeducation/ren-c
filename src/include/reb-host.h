/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
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
**  Summary: Include files for hosting
**  Module:  reb-host.h
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "reb-config.h"

#include "reb-c.h"

// Must be defined at the end of reb-c.h, but not *in* reb-c.h so that
// files including sys-core.h and reb-host.h can have differing
// definitions of REBCHR.  (We want it opaque to the core, but the
// host to have it compatible with the native character type w/o casting)
#ifdef OS_WIDE_CHAR
    typedef wchar_t REBCHR;
#else
    typedef char REBCHR;
#endif

#include "reb-ext.h"        // includes reb-defs.h
#include "reb-args.h"
#include "reb-device.h"
#include "reb-file.h"
#include "reb-event.h"
#include "reb-evtypes.h"
#include "reb-net.h"
#include "reb-filereq.h"

#include "reb-gob.h"
#include "reb-lib.h"

// !!! None of the above currently include anything that *necessarily* defines
// size_t.  However the host-lib API currently uses it in defining its
// allocator.  In order to match the signature of Alloc_Mem() and malloc(),
// we include it for the moment, but a more formal policy decision on "what
// parameter types are legal in the host API" would be ideal.
#include <stdlib.h>

#include "host-lib.h"
