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
// If no evaluative product can be produced (as in `eval [comment "hi"]` or
// `eval [| | ()]` or just plain `eval []`) then Do_XXX() will synthesize a trash.
//


INLINE bool Eval_Array_At_Throws(
    Value* out,
    Array* array,
    REBLEN index,
    Specifier* specifier
){
    return THROWN_FLAG == Eval_At_Core(
        Init_Void(out),
        nullptr, // opt_first (null indicates nothing, not nulled cell)
        array,
        index,
        specifier,
        DO_FLAG_TO_END
    );
}


INLINE bool Eval_List_At_Throws(
    Value* out,
    const Value* any_list  // Note: can be same pointer as `out`
){
    assert(out != any_list);  // Was legal at one time, but no longer

    return Eval_Array_At_Throws(
        out,
        Cell_Array(any_list),
        VAL_INDEX(any_list),
        VAL_SPECIFIER(any_list)
    );
}


INLINE bool Do_Va_Throws(
    Value* out,
    const void *opt_first,
    va_list *vaptr // va_end() will be called on success, fail, throw, etc.
){
    return THROWN_FLAG == Eval_Va_Core(
        Init_Trash(out),
        opt_first,
        vaptr,
        DO_FLAG_TO_END
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
INLINE bool Apply_Only_Throws(
    Value* out,
    bool fully,
    const Value* applicand, // last param before ... mentioned in va_start()
    ...
) {
    va_list va;
    va_start(va, applicand);

    DECLARE_VALUE (applicand_eval);
    Copy_Cell(applicand_eval, applicand);

    REBIXO indexor = Eval_Va_Core(
        SET_END(out), // start at END to detect error if no eval product
        applicand_eval, // opt_first
        &va, // va_end() handled by Eval_Va_Core on success, fail, throw, etc.
        DO_FLAG_NO_LOOKAHEAD
            | (fully ? DO_FLAG_NO_RESIDUE : 0)
    );

    if (IS_END(out))
        fail ("Apply_Only_Throws() empty or just COMMENTs/ELIDEs");

    return indexor == THROWN_FLAG;
}


// Conditional constructs allow branches that are either BLOCK!s or ACTION!s.
// If an action, the triggering condition is passed to it as an argument:
// https://trello.com/c/ay9rnjIe
//
// Allowing other values was deemed to do more harm than good:
// https://forum.rebol.info/t/backpedaling-on-non-block-branches/476
//
INLINE bool Do_Branch_Core_Throws(
    Value* out,
    const Value* branch,
    const Value* condition // can be END or nullptr--can't be a NULLED cell!
){
    assert(branch != out and condition != out);

    if (Is_Block(branch))
        return Eval_List_At_Throws(out, branch);

    assert(Is_Action(branch));
    return Apply_Only_Throws(
        out,
        false, // !fully, e.g. arity-0 functions can ignore condition
        branch,
        condition, // may be an END marker, if not Do_Branch_With() case
        rebEND // ...but if condition wasn't an END marker, we need one
    );
}

#define Do_Branch_With_Throws(out,branch,condition) \
    Do_Branch_Core_Throws((out), (branch), (condition))

#define Do_Branch_Throws(out,branch) \
    Do_Branch_Core_Throws((out), (branch), END_NODE)
