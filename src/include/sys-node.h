//
//  File: %sys-node.h
//  Summary: {Convenience routines for the Node "superclass" structure}
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
// This provides some convenience routines that require more definitions than
// are available when %sys-rebnod.h is being processed.  (e.g. REBVAL, 
// REBSER, REBFRM...)
//
// See %sys-rebnod.h for what a "node" means in this context.
//

// !!! TBD: Make a fancier checking version of this
//
inline static REBNOD *NOD(void *p) {
    assert(p != NULL);

    REBNOD *node = cast(REBNOD*, p);
    assert(
        (node->header.bits & NODE_FLAG_NODE)
        && NOT(node->header.bits & NODE_FLAG_FREE)
    );
    return node;
}

#ifdef NDEBUG
    inline static REBOOL IS_CELL(REBNOD *node) {
        return LOGICAL(node->header.bits & NODE_FLAG_CELL);
    }

    inline static REBOOL NOT_CELL(REBNOD *node) {
        return NOT(node->header.bits & NODE_FLAG_CELL);
    }
#else
    // We want to get a compile-time check on whether the argument is a
    // REBNOD (and not, say, a REBSER or REBVAL).  But we don't want to pay
    // for the function call in debug builds, so only check in release builds.
    //
    #define IS_CELL(node) \
        LOGICAL((node)->header.bits & NODE_FLAG_CELL)

    #define NOT_CELL(node) \
        NOT((node)->header.bits & NODE_FLAG_CELL)
#endif
