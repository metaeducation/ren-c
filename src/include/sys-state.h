//
//  File: %sys-state.h
//  Summary: "Interpreter State"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
// Structure holding the information about the last point in the stack that
// wanted to set up an opportunity to intercept a `fail (Error_XXX())`
//
// For operations using this structure, see %sys-trap.h
//

struct Reb_State {
    //
    // We put the jmp_buf first, since it has alignment specifiers on Windows
    //
#ifdef HAS_POSIX_SIGNAL
    sigjmp_buf cpu_state;
#else
    jmp_buf cpu_state;
#endif

    struct Reb_State *last_state;

    REBDSP dsp;
    struct Reb_Chunk *top_chunk;
    REBFRM *frame;
    REBCNT guarded_len;
    REBCTX *error;

    REBCNT manuals_len; // Where GC_Manuals was when state started
    REBCNT uni_buf_len;
    REBCNT mold_loop_tail;
};
