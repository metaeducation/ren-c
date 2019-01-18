//
//  File: %sys-do.h
//  Summary: {DO-until-end (of block or variadic feed) evaluation API}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
// DO is a higher-level concept, built on top of EVALUATE.  It always implies
// running to the end of its input, and always produces a single value...
// typically the last value an evaluation step computed.
//
// If no evaluative product can be produced (as in `do [comment "hi"]` or
// `do [| | ()]` or just plain `do []`) then Do_XXX() will synthesize a VOID!.
//


inline static bool Do_At_Mutability_Throws(
    REBVAL *out,
    REBARR *array,
    REBCNT index,
    REBSPC *specifier,
    bool mutability
){
    return THROWN_FLAG == Eval_Array_At_Core(
        Init_Void(out),
        nullptr, // opt_first (null indicates nothing, not nulled cell)
        array,
        index,
        specifier,
        (DO_MASK_DEFAULT & ~DO_FLAG_CONST)
            | DO_FLAG_TO_END
            | (mutability ? 0 : (FS_TOP->flags.bits & DO_FLAG_CONST))
    );
}

#define Do_At_Throws(out,array,index,specifier) \
    Do_At_Mutability_Throws((out), (array), (index), (specifier), false)


inline static bool Do_Any_Array_At_Throws(
    REBVAL *out,
    const REBVAL *any_array // Note: can be same pointer as `out`
){
    assert(out != any_array); // Was legal at one time, but no longer

    // If the user said something like `do mutable load %data.reb`, then the
    // value carries along with it a disablement of inheriting constness...
    // even if the frame has it set.
    //
    bool mutability = GET_VAL_FLAG(any_array, VALUE_FLAG_EXPLICITLY_MUTABLE);

    return THROWN_FLAG == Eval_Array_At_Core(
        Init_Void(out),
        nullptr, // opt_first (null indicates nothing, not nulled cell)
        VAL_ARRAY(any_array),
        VAL_INDEX(any_array),
        VAL_SPECIFIER(any_array),
        (DO_MASK_DEFAULT & ~DO_FLAG_CONST)
            | DO_FLAG_TO_END
            | (mutability ? 0 : (
                (FS_TOP->flags.bits & DO_FLAG_CONST)
                | (any_array->header.bits & DO_FLAG_CONST)
            ))
            // ^-- Even if you are using a DO MUTABLE, in deeper levels
            // evaluating a const value flips the constification back on.
    );
}


inline static bool Do_Va_Throws(
    REBVAL *out,
    const void *opt_first,
    va_list *vaptr // va_end() will be called on success, fail, throw, etc.
){
    return THROWN_FLAG == Eval_Va_Core(
        Init_Void(out),
        opt_first,
        vaptr,
        DO_MASK_DEFAULT
            | DO_FLAG_TO_END
            | DO_FLAG_EXPLICIT_EVALUATE
            | (FS_TOP->flags.bits & DO_FLAG_CONST)
    );
}


// Takes a list of arguments terminated by an end marker and will do something
// similar to R3-Alpha's "apply/only" with a value.  If that value is a
// function, it will be called...if it's a SET-WORD! it will be assigned, etc.
//
// This is equivalent to putting the value at the head of the input and
// then calling EVAL/ONLY on it.  If all the inputs are not consumed, an
// error will be thrown.
//
inline static bool Apply_Only_Throws(
    REBVAL *out,
    bool fully,
    const REBVAL *applicand, // last param before ... mentioned in va_start()
    ...
) {
    va_list va;
    va_start(va, applicand);

    DECLARE_LOCAL (applicand_eval);
    Move_Value(applicand_eval, applicand);
    SET_VAL_FLAG(applicand_eval, VALUE_FLAG_EVAL_FLIP);

    REBIXO indexor = Eval_Va_Core(
        SET_END(out), // start at END to detect error if no eval product
        applicand_eval, // opt_first
        &va, // va_end() handled by Eval_Va_Core on success, fail, throw, etc.
        (DO_MASK_DEFAULT & ~DO_FLAG_CONST)
            | DO_FLAG_EXPLICIT_EVALUATE
            | DO_FLAG_NO_LOOKAHEAD
            | (fully ? DO_FLAG_NO_RESIDUE : 0)
            | (FS_TOP->flags.bits & DO_FLAG_CONST)
            | (applicand->header.bits & DO_FLAG_CONST)
    );

    if (IS_END(out))
        fail ("Apply_Only_Throws() empty or just COMMENTs/ELIDEs/BAR!s");

    return indexor == THROWN_FLAG;
}


// Conditional constructs allow branches that are either BLOCK!s or ACTION!s.
// If an action, the triggering condition is passed to it as an argument:
// https://trello.com/c/ay9rnjIe
//
// Allowing other values was deemed to do more harm than good:
// https://forum.rebol.info/t/backpedaling-on-non-block-branches/476
//
inline static bool Do_Branch_Core_Throws(
    REBVAL *out,
    const REBVAL *branch,
    const REBVAL *condition // can be END or nullptr--can't be a NULLED cell!
){
    assert(branch != out and condition != out);

    if (IS_QUOTED(branch)) {
        Unquotify(Move_Value(out, branch), 1);
        return false;
    }

    if (IS_BLOCK(branch))
        return Do_Any_Array_At_Throws(out, branch);

    assert(IS_ACTION(branch));
    return Apply_Only_Throws(
        out,
        false, // !fully, e.g. arity-0 functions can ignore condition
        branch,
        condition, // may be an END marker, if not Do_Branch_With() case
        rebEND // ...but if condition wasn't an END marker, we need one
    );
}

#define Do_Branch_With_Throws(out,branch,condition) \
    Do_Branch_Core_Throws((out), (branch), NULLIFY_NULLED(condition))

#define Do_Branch_Throws(out,branch) \
    Do_Branch_Core_Throws((out), (branch), END_NODE)


enum {
    REDUCE_FLAG_TRY = 1 << 0, // null should be converted to blank, vs fail
    REDUCE_FLAG_OPT = 1 << 1 // discard nulls (incompatible w/REDUCE_FLAG_TRY)
};

#define REDUCE_MASK_NONE 0
