//
//  File: %ext-zeromq.c
//  Summary: "ØMQ extension"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2011 Andreas Bolka
// Copyright 2018 Rebol Open Source Contributors
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
// ==================================================================
//
#include "sys-core.h"
#include "sys-ext.h"

#include "tmp-ext-zeromq-init.inc"

#define MODULE_INCLUDE_DECLARATION_ONLY
#include "tmp-mod-zeromq-last.h"

DEFINE_EXT_INIT_COMPRESSED(ZeroMQ, //name of the extension
    script_bytes, // REBOL script for the extension in the source form
    {
        // init all modules in this extension
        int init = CALL_MODULE_INIT(ZeroMQ);
        if (init < 0) return init;
    }
)

DEFINE_EXT_QUIT(ZeroMQ,
{
    return CALL_MODULE_QUIT(ZeroMQ);
}
)
