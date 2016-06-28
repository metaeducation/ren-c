//
//  File: %d-legacy.h
//  Summary: "Legacy Support Routines for Debug Builds"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016 Rebol Open Source Contributors
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
// In order to make porting code from R3-Alpha or Rebol2 easier, Ren-C set
// up several LEGACY() switches and a <r3-legacy> mode.  The switches are
// intended to only be available in debug builds, so that compatibility for
// legacy code will not be a runtime cost in the release build.  However,
// they could be enabled by any sufficiently motivated individual who
// wished to build a version of the interpreter with the old choices in an
// optimized build as well.
//
// Support routines for legacy mode are quarantined here when possible.
//

#include "sys-core.h"


#if !defined(NDEBUG)

//
//  In_Legacy_Function_Debug: C
//
// Determine if a legacy function is "in effect" currently.  To the extent
// that compatibility in debug builds or legacy mode with R3-Alpha is
// "important" this should be used sparingly, because code can be bound and
// passed around in blocks.  So you might be running a legacy function passed
// new code or new code passed legacy code (e.g. a mezzanine that uses DO)
//
REBOOL In_Legacy_Function_Debug(void)
{
    // Find the first bit of code that's actually running ordinarily in
    // the evaluator, and not just dispatching.
    //
    struct Reb_Frame *frame = FS_TOP;
    for (; frame != NULL; frame = frame->prior) {
        if (frame->flags & DO_FLAG_VA_LIST)
            return FALSE; // no source array to look at

        break; // whatever's dispatching here, there is a source array
    }

    if (frame == NULL)
        return FALSE;

    // Check the flag on the source series
    //
    if (GET_ARR_FLAG(frame->source.array, SERIES_FLAG_LEGACY))
        return TRUE;

    return FALSE;
}


//
//  Legacy_Convert_Function_Args: C
//
// R3-Alpha and Rebol2 used BLANK for unused refinements and arguments to
// a refinement which is not present.  Ren-C uses FALSE for unused refinements
// and arguments to unused refinements are not set.
//
// Could be woven in efficiently, but as it's a debug build only feature it's
// better to isolate it into a post-phase.  This improves the readability of
// the mainline code.
//
// Trigger is when OPTIONS_REFINEMENTS_TRUE is set during function creation,
// which will give it FUNC_FLAG_LEGACY--leading to this being used.
//
void Legacy_Convert_Function_Args(struct Reb_Frame *f)
{
    REBVAL *param = FUNC_PARAMS_HEAD(f->func);
    REBVAL *arg = FRM_ARGS_HEAD(f);

    REBOOL set_blank = FALSE;

    for (; NOT_END(param); ++param, ++arg) {
        switch (VAL_PARAM_CLASS(param)) {
        case PARAM_CLASS_REFINEMENT:
            if (IS_LOGIC(arg)) {
                if (VAL_LOGIC(arg))
                    set_blank = FALSE;
                else {
                    SET_BLANK(arg);
                    set_blank = TRUE;
                }
            }
            else assert(FALSE);
            break;

        case PARAM_CLASS_LOCAL:
            assert(IS_VOID(arg)); // keep *pure* locals as void, even in legacy
            break;

        case PARAM_CLASS_RETURN:
        case PARAM_CLASS_LEAVE:
            assert(IS_FUNCTION(arg) || IS_VOID(arg));
            break;

        case PARAM_CLASS_NORMAL:
        case PARAM_CLASS_HARD_QUOTE:
        case PARAM_CLASS_SOFT_QUOTE:
            if (set_blank) {
                assert(IS_VOID(arg));
                SET_BLANK(arg);
            }
            break;

        default:
            assert(FALSE);
        }
    }
}


//
//  Make_Guarded_Arg123_Error: C
//
// Needed only for compatibility trick to "fake in" ARG1: ARG2: ARG3:
//
// Rebol2 and R3-Alpha errors were limited to three arguments with
// fixed names, arg1 arg2 arg3.  (Though R3 comments alluded to
// the idea that MAKE ERROR! from an OBJECT! would inherit that
// object's fields, it did not actually work.)  With FAIL and more
// flexible error creation this is being extended.
//
// Change is not made to the root error object because there is no
// "moment" to effect that (e.g. <r3-legacy> mode will not be started
// at boot time, it happens after).  This allows the stock args to be
// enabled and disabled dynamically in the legacy settings, at the
// cost of creating a new error object each time.
//
// To make code handling it like the regular error context (and keep that
// code "relatively uncontaminated" by the #ifdefs), it must behave
// as GC managed.  So it has to be guarded, thus the client drops the
// guard and it will wind up being freed since it's not in the root set.
// This is a bit inefficient but it's for legacy mode only, so best
// to bend to the expectations of the non-legacy code.
//
REBCTX *Make_Guarded_Arg123_Error(void)
{
    REBCTX *root_error = VAL_CONTEXT(ROOT_ERROBJ);
    REBCTX *error = Copy_Context_Shallow_Extra(root_error, 3);

    REBCNT root_len = ARR_LEN(CTX_VARLIST(root_error));

    // Update the length to suppress out of bounds assert from CTX_KEY/VAL
    //
    SET_ARRAY_LEN(CTX_VARLIST(error), root_len + 3);
    SET_ARRAY_LEN(CTX_KEYLIST(error), root_len + 3);

    REBVAL *key = CTX_KEY(error, CTX_LEN(root_error)) + 1;
    REBVAL *var = CTX_VAR(error, CTX_LEN(root_error)) + 1;

    REBCNT n;
    for (n = 0; n < 3; n++, key++, var++) {
        Val_Init_Typeset(
            key,
            ALL_64,
            Canon(cast(REBSYM, cast(REBCNT, SYM_ARG1) + n))
        );
        SET_BLANK(var);
    }

    SET_END(key);
    SET_END(var);

    MANAGE_ARRAY(CTX_VARLIST(error));
    PUSH_GUARD_CONTEXT(error);
    return error;
}

#endif
