//
//  File: %n-reduce.h
//  Summary: {REDUCE and COMPOSE natives and associated service routines}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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

#include "sys-core.h"

//
//  Reduce_Any_Array_Throws: C
//
// Reduce array from the index position specified in the value.
//
// If `into` then splice into the existing `out`.  Otherwise, overwrite the
// `out` with all values collected from the stack, into an array matching the
// type of the input.  So [1 + 1 2 + 2] => [3 4], and 1/+/1/2/+/2 => 3/4
//
REBOOL Reduce_Any_Array_Throws(
    REBVAL *out,
    REBVAL *any_array,
    REBOOL into
) {
    REBDSP dsp_orig = DSP;

    Reb_Enumerator e;
    PUSH_SAFE_ENUMERATOR(&e, any_array); // REDUCE-ing could disrupt any-array

    while (NOT_END(e.value)) {
        UPDATE_EXPRESSION_START(&e); // informs the error delivery better

        REBVAL reduced;
        DO_NEXT_REFETCH_MAY_THROW(&reduced, &e, DO_FLAG_NORMAL);
        if (THROWN(&reduced)) {
            *out = reduced;
            DS_DROP_TO(dsp_orig);
            DROP_SAFE_ENUMERATOR(&e);
            return TRUE;
        }

        if (IS_VOID(&reduced)) {
            //
            // !!! Review if there should be a form of reduce which allows
            // void expressions.  The general feeling is that it shouldn't
            // be allowed by default, since N expressions would not make N
            // results...and reduce is often used for positional purposes.
            // Substituting anything (like a NONE!, or anything else) would
            // perhaps be disingenuous.
            //
            fail (Error(RE_REDUCE_MADE_VOID));
        }

        DS_PUSH(&reduced);
    }

    if (into)
        Pop_Stack_Values_Into(out, dsp_orig);
    else
        Val_Init_Array(out, VAL_TYPE(any_array), Pop_Stack_Values(dsp_orig));

    DROP_SAFE_ENUMERATOR(&e);
    return FALSE;
}


//
//  reduce: native [
//
//  {Evaluates expressions and returns multiple results.}
//
//      return: [<opt> any-value!]
//      value [<opt> any-value!]
//          {If BLOCK!, expressions are reduced, otherwise single value.}
//      /into
//          {Output results into a series with no intermediate storage}
//      target [any-array!]
//  ]
//
REBNATIVE(reduce)
{
    INCLUDE_PARAMS_OF_REDUCE;

    REBVAL *value = ARG(value);

    if (IS_VOID(value))
        return R_VOID; // !!! Should this be allowed?  (Red allows it)

    if (IS_BLOCK(value)) {
        if (REF(into))
            *D_OUT = *ARG(target);

        if (Reduce_Any_Array_Throws(D_OUT, value, REF(into))) {
            return R_OUT_IS_THROWN;
        }

        return R_OUT;
    }

    // A single element should do what is effectively an evaluation but with
    // no arguments.  This is a change in behavior from R3-Alpha, which would
    // just return the input as is, e.g. `reduce quote (1 + 2)` => (1 + 2).
    //
    // !!! Should the error be more "reduce-specific" if args were required?
    //
    if (EVAL_VALUE_THROWS(D_OUT, value))
        return R_OUT_IS_THROWN;

    if (NOT(REF(into)))
        return R_OUT; // just return the evaluated item if no /INTO target

    REBVAL *into = ARG(target);
    assert(ANY_ARRAY(into));
    FAIL_IF_READ_ONLY_ARRAY(VAL_ARRAY(into));

    // Insert the single item into the target array at its current position,
    // and return the position after the insertion (the /INTO convention)

    VAL_INDEX(into) = Insert_Series(
        ARR_SERIES(VAL_ARRAY(into)),
        VAL_INDEX(into),
        cast(REBYTE*, D_OUT),
        1 // multiplied by width (sizeof(REBVAL)) in Insert_Series
    );

    *D_OUT = *into;
    return R_OUT;
}


//
//  Compose_Any_Array_Throws: C
//
// Compose a block from a block of un-evaluated values and GROUP! arrays that
// are evaluated.  This calls into Do_Core, so if 'into' is provided, then its
// series must be protected from garbage collection.
//
//     deep - recurse into sub-blocks
//     only - parens that return blocks are kept as blocks
//
// Writes result value at address pointed to by out.
//
REBOOL Compose_Any_Array_Throws(
    REBVAL *out,
    const REBVAL *any_array,
    REBOOL deep,
    REBOOL only,
    REBOOL into
) {
    REBDSP dsp_orig = DSP;

    Reb_Enumerator e;
    PUSH_SAFE_ENUMERATOR(&e, any_array); // evaluating could disrupt any_array

    while (NOT_END(e.value)) {
        UPDATE_EXPRESSION_START(&e); // informs the error delivery better

        if (IS_GROUP(e.value)) {
            //
            // We evaluate here, but disable lookahead so it only evaluates
            // the GROUP! and doesn't trigger errors on what's after it.
            //
            REBVAL evaluated;
            DO_NEXT_REFETCH_MAY_THROW(&evaluated, &e, DO_FLAG_NO_LOOKAHEAD);
            if (THROWN(&evaluated)) {
                *out = evaluated;
                DS_DROP_TO(dsp_orig);
                DROP_SAFE_ENUMERATOR(&e);
                return TRUE;
            }

            if (IS_BLOCK(&evaluated) && !only) {
                //
                // compose [blocks ([a b c]) merge] => [blocks a b c merge]
                //
                RELVAL *push = VAL_ARRAY_AT(&evaluated);
                while (NOT_END(push)) {
                    //
                    // `evaluated` is known to be specific, but its specifier
                    // may be needed to derelativize its children.
                    //
                    DS_PUSH_RELVAL(push, VAL_SPECIFIER(&evaluated));
                    push++;
                }
            }
            else if (!IS_VOID(&evaluated)) {
                //
                // compose [(1 + 2) inserts as-is] => [3 inserts as-is]
                // compose/only [([a b c]) unmerged] => [[a b c] unmerged]
                //
                DS_PUSH(&evaluated);
            }
            else {
                //
                // compose [(print "Voids *vanish*!")] => []
                //
            }
        }
        else if (deep) {
            if (IS_BLOCK(e.value)) {
                //
                // compose/deep [does [(1 + 2)] nested] => [does [3] nested]

                REBVAL specific;
                COPY_VALUE(&specific, e.value, e.specifier);

                REBVAL composed;
                if (Compose_Any_Array_Throws(
                    &composed,
                    &specific,
                    TRUE,
                    only,
                    into
                )) {
                    *out = composed;
                    DS_DROP_TO(dsp_orig);
                    DROP_SAFE_ENUMERATOR(&e);
                    return TRUE;
                }

                DS_PUSH(&composed);
            }
            else {
                if (ANY_ARRAY(e.value)) {
                    //
                    // compose [copy/(orig) (copy)] => [copy/(orig) (copy)]
                    // !!! path and second group are copies, first group isn't
                    //
                    REBARR *copy = Copy_Array_Shallow(
                        VAL_ARRAY(e.value),
                        IS_RELATIVE(e.value)
                            ? e.specifier // use parent specifier if relative...
                            : VAL_SPECIFIER(const_KNOWN(e.value)) // else child's
                    );
                    DS_PUSH_TRASH;
                    Val_Init_Array_Index(
                        DS_TOP, VAL_TYPE(e.value), copy, VAL_INDEX(e.value)
                    ); // ...manages
                }
                else
                    DS_PUSH_RELVAL(e.value, e.specifier);
            }
            FETCH_NEXT_ONLY_MAYBE_END(&e);
        }
        else {
            //
            // compose [[(1 + 2)] (reverse "wollahs")] => [[(1 + 2)] "shallow"]
            //
            DS_PUSH_RELVAL(e.value, e.specifier);
            FETCH_NEXT_ONLY_MAYBE_END(&e);
        }
    }

    if (into)
        Pop_Stack_Values_Into(out, dsp_orig);
    else
        Val_Init_Array(out, VAL_TYPE(any_array), Pop_Stack_Values(dsp_orig));

    DROP_SAFE_ENUMERATOR(&e);
    return FALSE;
}


//
//  compose: native [
//
//  {Evaluates only the GROUP!s in a block of expressions, returning a block.}
//
//      value
//          "Block to compose (or any other type evaluates to itself)"
//                                          ; ^-- is this sensible?
//      /deep
//          "Compose nested blocks"
//      /only
//          {Insert a block as a single value (not the contents of the block)}
//      /into
//          {Output results into a series with no intermediate storage}
//      out [any-array! any-string! binary!]
//  ]
//
REBNATIVE(compose)
{
    INCLUDE_PARAMS_OF_COMPOSE;

    // !!! Should 'compose quote (a (1 + 2) b)' give back '(a 3 b)' ?
    // What about 'compose quote a/(1 + 2)/b' ?
    //
    if (!IS_BLOCK(ARG(value))) {
        *D_OUT = *ARG(value);
        return R_OUT;
    }

    // Compose_Values_Throws() expects `out` to contain the target if it is
    // passed TRUE as the `into` flag.
    //
    if (REF(into)) *D_OUT = *ARG(out);

    if (Compose_Any_Array_Throws(
        D_OUT,
        ARG(value),
        REF(deep),
        REF(only),
        REF(into)
    )) {
        return R_OUT_IS_THROWN;
    }

    return R_OUT;
}
