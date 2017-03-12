//
//  File: %ext-crypt.c
//  Summary: "Crypt functions"
//  Section: Extension
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
// ==================================================================
//
#include "sys-core.h"
#include "sys-ext.h"

static const char script_bytes[] =
"REBOL ["
    "Title: \"Crypt Extension\"\n"
    "name: 'Crypt\n"
    "type: 'Extension\n"
    "version: 1.0.0\n"
    "license: {Apache 2.0}\n"
"]\n"
"hmac-sha256: function [{computes the hmac-sha256 for message m using key k}\n"
"    k [binary!] m [binary!]][\n"
"    key: copy k\n"
"    message: copy m\n"
"    blocksize: 64\n"
"    if (length key) > blocksize [\n"
"        key: sha256 key\n"
"    ]\n"
"    if (length key) < blocksize [\n"
"        insert/dup tail key #{00} (blocksize - length key)\n"
"    ]\n"
"    insert/dup opad: copy #{} #{5C} blocksize\n"
"    insert/dup ipad: copy #{} #{36} blocksize\n"
"    o_key_pad: XOR~ opad key\n"
"    i_key_pad: XOR~ ipad key\n"
"    sha256 join-of o_key_pad sha256 join-of i_key_pad message\n"
"]\n"
;

void Init_Crypto(void);
void Shutdown_Crypto(void);

#define MODULE_INCLUDE_DECLARATION_ONLY
#include "tmp-mod-crypt-last.h"

DEFINE_EXT_INIT(Crypt, //name of the extension
    script_bytes, // REBOL script for the extension in the source form
    {
        // init all modules in this extension
        Init_Crypto();
        int init = CALL_MODULE_INIT(Crypt);
        if (init < 0) return init;
    }
)

DEFINE_EXT_QUIT(Crypt,
{
    Shutdown_Crypto();
    return CALL_MODULE_QUIT(Crypt);
}
)
