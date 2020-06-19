//
//  File: %n-reduce.h
//  Summary: {REDUCE and COMPOSE natives and associated service routines}
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

#include "sys-core.h"

//
//  Reduce_To_Stack_Throws: C
//
// Reduce array from the index position specified in the value.
//
bool Reduce_To_Stack_Throws(
    REBVAL *out,
    REBVAL *any_array,
    REBFLGS flags
){
    // Can't have more than one policy on null conversion in effect.
    //
    assert(not ((flags & REDUCE_FLAG_TRY) and (flags & REDUCE_FLAG_OPT)));

    REBDSP dsp_orig = DSP;

    DECLARE_FRAME (f);
    Push_Frame(f, any_array);

    while (NOT_END(f->value)) {
        bool line = GET_VAL_FLAG(f->value, VALUE_FLAG_NEWLINE_BEFORE);

        if (Eval_Step_Throws(SET_END(out), f)) {
            DS_DROP_TO(dsp_orig);
            Abort_Frame(f);
            return true;
        }

        if (IS_END(out)) { // e.g. `reduce [comment "hi"]`
            assert(IS_END(f->value));
            break;
        }

        if (IS_NULLED(out)) {
            if (flags & REDUCE_FLAG_TRY) {
                DS_PUSH_TRASH;
                Init_Blank(DS_TOP);
                if (line)
                    SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);
            }
            else if (not (flags & REDUCE_FLAG_OPT))
                fail (Error_Reduce_Made_Null_Raw());
        }
        else {
            DS_PUSH(out);
            if (line)
                SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);
        }
    }

    Drop_Frame_Unbalanced(f); // Drop_Frame() asserts on accumulation
    return false;
}


//
//  reduce: native [
//
//  {Evaluates expressions, keeping each result (DO only gives last result)}
//
//      return: "New array or value"
//          [<opt> any-value!]
//      value "GROUP! and BLOCK! evaluate each item, single values evaluate"
//          [any-value!]
//      /try "If an evaluation returns null, convert to blank vs. failing"
//      /opt "If an evaluation returns null, omit the result" ; !!! EXPERIMENT
//  ]
//
REBNATIVE(reduce)
{
    INCLUDE_PARAMS_OF_REDUCE;

    REBVAL *value = ARG(value);

    if (REF(opt) and REF(try))
        fail (Error_Bad_Refines_Raw());

    if (IS_BLOCK(value) or IS_GROUP(value)) {
        REBDSP dsp_orig = DSP;

        if (Reduce_To_Stack_Throws(
            D_OUT,
            value,
            REDUCE_MASK_NONE
                | (REF(try) ? REDUCE_FLAG_TRY : 0)
                | (REF(opt) ? REDUCE_FLAG_OPT : 0)
        )){
            return R_THROWN;
        }

        REBFLGS pop_flags = NODE_FLAG_MANAGED | ARRAY_FLAG_FILE_LINE;
        if (GET_SER_FLAG(VAL_ARRAY(value), ARRAY_FLAG_TAIL_NEWLINE))
            pop_flags |= ARRAY_FLAG_TAIL_NEWLINE;

        return Init_Any_Array(
            D_OUT,
            VAL_TYPE(value),
            Pop_Stack_Values_Core(dsp_orig, pop_flags)
        );
    }

    // Single element REDUCE does an EVAL, but doesn't allow arguments.
    // (R3-Alpha, would just return the input, e.g. `reduce :foo` => :foo)
    // If there are arguments required, Eval_Value_Throws() will error.
    //
    // !!! Should the error be more "reduce-specific" if args were required?

    if (ANY_INERT(value)) // don't bother with the evaluation
        RETURN (value);

    if (Eval_Value_Throws(D_OUT, value))
        return R_THROWN;

    if (not IS_NULLED(D_OUT))
        return D_OUT;

    if (REF(try))
        return Init_Blank(D_OUT);

    return nullptr; // let caller worry about whether to error on nulls
}


bool Match_For_Compose(const RELVAL *group, const REBVAL *pattern) {
    if (IS_NULLED(pattern))
        return true;

    if (VAL_LEN_AT(group) == 0) // you have a pattern, so leave `()` as-is
        return false;

    RELVAL *first = VAL_ARRAY_AT(group);
    RELVAL *last = VAL_ARRAY_TAIL(group) - 1;
    if (IS_BAR(first) != IS_BAR(last))
        fail ("Pattern for COMPOSE must be on both ends of GROUP!");
    if (not IS_BAR(first))
        return false; // leave as-is
    if (first == last) // e.g. (*), needs to be at least (* *)
        fail ("Two patterns, not one, must appear used in COMPOSE of GROUP!");
    return true;
}


//
//  Compose_To_Stack_Throws: C
//
// Use rules of composition to do template substitutions on values matching
// `pattern` by evaluating those slots, leaving all other slots as is.
//
// Values are pushed to the stack because it is a "hot" preallocated large
// memory range, and the number of values can be calculated in order to
// accurately size the result when it needs to be allocated.  Not returning
// an array also offers more options for avoiding that intermediate if the
// caller wants to add part or all of the popped data to an existing array.
//
bool Compose_To_Stack_Throws(
    REBVAL *out, // if return result is true, will hold the thrown value
    const RELVAL *any_array, // the template
    REBSPC *specifier, // specifier for relative any_array value
    const REBVAL *pattern, // e.g. if '*, only match `(* ... *)`
    bool deep, // recurse into sub-blocks
    bool only // pattern matches that return blocks are kept as blocks
){
    REBDSP dsp_orig = DSP;

    DECLARE_FRAME (f);
    Push_Frame_At(
        f, VAL_ARRAY(any_array), VAL_INDEX(any_array), specifier, DO_MASK_NONE
    );

    while (NOT_END(f->value)) {
        if (not ANY_ARRAY(f->value)) { // non-arrays don't substitute/recurse
            DS_PUSH_RELVAL(f->value, specifier); // preserves newline flag
            Fetch_Next_In_Frame(nullptr, f);
            continue;
        }

        bool splice = not only; // can force no splice if override via ((...))

        REBSPC *match_specifier = nullptr;
        const RELVAL *match = nullptr;

        if (not IS_GROUP(f->value)) {
            //
            // Don't compose at this level, but may need to walk deeply to
            // find compositions inside it if /DEEP and it's an array
        }
        else {
            if (Is_Doubled_Group(f->value)) { // non-spliced compose, if match
                RELVAL *inner = VAL_ARRAY_AT(f->value);
                if (Match_For_Compose(inner, pattern)) {
                    splice = false;
                    match = inner;
                    match_specifier = Derive_Specifier(specifier, inner);
                }
            }
            else { // plain compose, if match
                if (Match_For_Compose(f->value, pattern)) {
                    match = f->value;
                    match_specifier = specifier;
                }
            }
        }

        if (match) { // only f->value if pattern is just [] or (), else deeper
            REBIXO indexor = Eval_Array_At_Core(
                Init_Nulled(out), // want empty () to vanish as a NULL would
                nullptr, // no opt_first
                VAL_ARRAY(match),
                VAL_INDEX(match),
                match_specifier,
                DO_FLAG_TO_END
            );

            if (indexor == THROWN_FLAG) {
                DS_DROP_TO(dsp_orig);
                Abort_Frame(f);
                return true;
            }

            if (IS_NULLED(out)) {
                //
                // compose [("nulls *vanish*!" null)] => []
                // compose [(elide "so do 'empty' composes")] => []
            }
            else if (splice and IS_BLOCK(out)) {
                //
                // compose [not-only ([a b]) merges] => [not-only a b merges]

                RELVAL *push = VAL_ARRAY_AT(out);
                if (NOT_END(push)) {
                    //
                    // Only proxy newline flag from the template on *first*
                    // value spliced in (it may have its own newline flag)
                    //
                    DS_PUSH_RELVAL(push, VAL_SPECIFIER(out));
                    if (GET_VAL_FLAG(f->value, VALUE_FLAG_NEWLINE_BEFORE))
                        SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);

                    while (++push, NOT_END(push))
                        DS_PUSH_RELVAL(push, VAL_SPECIFIER(out));
                }
            }
            else {
                // compose [(1 + 2) inserts as-is] => [3 inserts as-is]
                // compose/only [([a b c]) unmerged] => [[a b c] unmerged]

                DS_PUSH(out); // Note: not legal to eval to stack direct!
                if (GET_VAL_FLAG(f->value, VALUE_FLAG_NEWLINE_BEFORE))
                    SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);
            }

          #ifdef DEBUG_UNREADABLE_BLANKS
            Init_Unreadable_Blank(out); // shouldn't leak temp eval to caller
          #endif
        }
        else if (deep) {
            // compose/deep [does [(1 + 2)] nested] => [does [3] nested]

            REBDSP dsp_deep = DSP;
            if (Compose_To_Stack_Throws(
                out,
                f->value,
                specifier,
                pattern,
                true, // deep (guaranteed true if we get here)
                only
            )){
                DS_DROP_TO(dsp_orig); // drop to outer DSP (@ function start)
                Abort_Frame(f);
                return true;
            }

            REBFLGS flags = NODE_FLAG_MANAGED | ARRAY_FLAG_FILE_LINE;
            if (GET_SER_FLAG(VAL_ARRAY(f->value), ARRAY_FLAG_TAIL_NEWLINE))
                flags |= ARRAY_FLAG_TAIL_NEWLINE;

            REBARR *popped = Pop_Stack_Values_Core(dsp_deep, flags);
            DS_PUSH_TRASH;
            Init_Any_Array(
                DS_TOP,
                VAL_TYPE(f->value),
                popped // can't push and pop in same step, need this variable!
            );

            if (GET_VAL_FLAG(f->value, VALUE_FLAG_NEWLINE_BEFORE))
                SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);
        }
        else {
            // compose [[(1 + 2)] (3 + 4)] => [[(1 + 2)] 7] ;-- non-deep
            //
            DS_PUSH_RELVAL(f->value, specifier); // preserves newline flag
        }

        Fetch_Next_In_Frame(nullptr, f);
    }

    Drop_Frame_Unbalanced(f); // Drop_Frame() asesrts on stack accumulation
    return false;
}


//
//  compose: native [
//
//  {Evaluates only contents of GROUP!-delimited expressions in an array}
//
//      return: [any-array!]
//      :pattern "Distinguish compose groups, e.g. [(plain) (* composed *)]"
//          [<skip> lit-bar!]
//      value "Array to use as the template for substitution"
//          [any-array!]
//      /deep "Compose deeply into nested arrays"
//      /only "Insert arrays as single value (not as contents of array)"
//  ]
//
REBNATIVE(compose)
//
// Note: /INTO is intentionally no longer supported
// https://forum.rebol.info/t/stopping-the-into-virus/705
{
    INCLUDE_PARAMS_OF_COMPOSE;

    REBDSP dsp_orig = DSP;

    if (Compose_To_Stack_Throws(
        D_OUT,
        ARG(value),
        VAL_SPECIFIER(ARG(value)),
        ARG(pattern),
        REF(deep),
        REF(only)
    )){
        return R_THROWN;
    }

    // The stack values contain N NEWLINE_BEFORE flags, and we need N + 1
    // flags.  Borrow the one for the tail directly from the input REBARR.
    //
    REBFLGS flags = NODE_FLAG_MANAGED | ARRAY_FLAG_FILE_LINE;
    if (GET_SER_FLAG(VAL_ARRAY(ARG(value)), ARRAY_FLAG_TAIL_NEWLINE))
        flags |= ARRAY_FLAG_TAIL_NEWLINE;

    return Init_Any_Array(
        D_OUT,
        VAL_TYPE(ARG(value)),
        Pop_Stack_Values_Core(dsp_orig, flags)
    );
}


enum FLATTEN_LEVEL {
    FLATTEN_NOT,
    FLATTEN_ONCE,
    FLATTEN_DEEP
};


static void Flatten_Core(
    RELVAL *head,
    REBSPC *specifier,
    enum FLATTEN_LEVEL level
) {
    RELVAL *item = head;
    for (; NOT_END(item); ++item) {
        if (IS_BLOCK(item) and level != FLATTEN_NOT) {
            REBSPC *derived = Derive_Specifier(specifier, item);
            Flatten_Core(
                VAL_ARRAY_AT(item),
                derived,
                level == FLATTEN_ONCE ? FLATTEN_NOT : FLATTEN_DEEP
            );
        }
        else
            DS_PUSH_RELVAL(item, specifier);
    }
}


//
//  flatten: native [
//
//  {Flattens a block of blocks.}
//
//      return: [block!]
//          {The flattened result block}
//      block [block!]
//          {The nested source block}
//      /deep
//  ]
//
REBNATIVE(flatten)
{
    INCLUDE_PARAMS_OF_FLATTEN;

    REBDSP dsp_orig = DSP;

    Flatten_Core(
        VAL_ARRAY_AT(ARG(block)),
        VAL_SPECIFIER(ARG(block)),
        REF(deep) ? FLATTEN_DEEP : FLATTEN_ONCE
    );

    return Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
}
