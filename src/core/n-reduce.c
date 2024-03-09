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
    Value* out,
    Value* any_array
){
    StackIndex base = TOP_INDEX;

    DECLARE_LEVEL (L);
    Push_Level(L, any_array);

    while (NOT_END(L->value)) {
        bool line = GET_VAL_FLAG(L->value, VALUE_FLAG_NEWLINE_BEFORE);

        if (Eval_Step_Throws(SET_END(out), L)) {
            Drop_Data_Stack_To(base);
            Abort_Level(L);
            return true;
        }

        if (IS_END(out)) { // e.g. `reduce [comment "hi"]`
            assert(IS_END(L->value));
            break;
        }

        if (IS_NULLED(out))
            fail (Error_Need_Non_Null_Raw());

        if (IS_VOID(out)) {
            // ignore
        }
        else {
            Copy_Cell(PUSH(), out);
            if (line)
                SET_VAL_FLAG(TOP, VALUE_FLAG_NEWLINE_BEFORE);
        }
    }

    Drop_Level_Unbalanced(L); // Drop_Level() asserts on accumulation
    return false;
}


//
//  reduce: native [
//
//  {Evaluates expressions, keeping each result (DO only gives last result)}
//
//      return: "New array or value"
//          [~null~ any-value!]
//      value "GROUP! and BLOCK! evaluate each item, single values evaluate"
//          [any-value!]
//  ]
//
DECLARE_NATIVE(reduce)
{
    INCLUDE_PARAMS_OF_REDUCE;

    Value* value = ARG(value);

    if (IS_BLOCK(value) or IS_GROUP(value)) {
        StackIndex base = TOP_INDEX;

        if (Reduce_To_Stack_Throws(OUT, value))
            return R_THROWN;

        REBFLGS pop_flags = NODE_FLAG_MANAGED | ARRAY_FLAG_FILE_LINE;
        if (GET_SER_FLAG(Cell_Array(value), ARRAY_FLAG_TAIL_NEWLINE))
            pop_flags |= ARRAY_FLAG_TAIL_NEWLINE;

        return Init_Any_Array(
            OUT,
            VAL_TYPE(value),
            Pop_Stack_Values_Core(base, pop_flags)
        );
    }

    // Single element REDUCE does an EVAL, but doesn't allow arguments.
    // (R3-Alpha, would just return the input, e.g. `reduce :foo` => :foo)
    // If there are arguments required, Eval_Value_Throws() will error.
    //
    // !!! Should the error be more "reduce-specific" if args were required?

    if (ANY_INERT(value)) // don't bother with the evaluation
        RETURN (value);

    if (Eval_Value_Throws(OUT, value))
        return R_THROWN;

    if (not IS_NULLED(OUT))
        return OUT;

    return nullptr; // let caller worry about whether to error on nulls
}


bool Match_For_Compose(const Cell* group, const Value* pattern) {
    if (IS_NULLED(pattern))
        return true;

    if (VAL_LEN_AT(group) == 0) // you have a pattern, so leave `()` as-is
        return false;

    Cell* first = Cell_Array_At(group);
    Cell* last = VAL_ARRAY_TAIL(group) - 1;
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
    Value* out, // if return result is true, will hold the thrown value
    const Cell* any_array, // the template
    Specifier* specifier, // specifier for relative any_array value
    const Value* pattern, // e.g. if '*, only match `(* ... *)`
    bool deep, // recurse into sub-blocks
    bool only // pattern matches that return blocks are kept as blocks
){
    StackIndex base = TOP_INDEX;

    DECLARE_LEVEL (L);
    Push_Level_At(
        L, Cell_Array(any_array), VAL_INDEX(any_array), specifier, DO_MASK_NONE
    );

    while (NOT_END(L->value)) {
        if (not ANY_ARRAY(L->value)) { // non-arrays don't substitute/recurse
            Derelativize(PUSH(), L->value, specifier);  // preserves newline
            Fetch_Next_In_Level(nullptr, L);
            continue;
        }

        bool splice = not only; // can force no splice if override via ((...))

        Specifier* match_specifier = nullptr;
        const Cell* match = nullptr;

        if (not IS_GROUP(L->value)) {
            //
            // Don't compose at this level, but may need to walk deeply to
            // find compositions inside it if /DEEP and it's an array
        }
        else {
            if (Is_Doubled_Group(L->value)) { // non-spliced compose, if match
                Cell* inner = Cell_Array_At(L->value);
                if (Match_For_Compose(inner, pattern)) {
                    splice = false;
                    match = inner;
                    match_specifier = Derive_Specifier(specifier, inner);
                }
            }
            else { // plain compose, if match
                if (Match_For_Compose(L->value, pattern)) {
                    match = L->value;
                    match_specifier = specifier;
                }
            }
        }

        if (match) { // only L->value if pattern is just [] or (), else deeper
            REBIXO indexor = Eval_Array_At_Core(
                Init_Void(out), // want empty () to vanish as a VOID would
                nullptr, // no opt_first
                Cell_Array(match),
                VAL_INDEX(match),
                match_specifier,
                DO_FLAG_TO_END
            );

            if (indexor == THROWN_FLAG) {
                Drop_Data_Stack_To(base);
                Abort_Level(L);
                return true;
            }

            if (IS_NULLED(out))
                fail (Error_Need_Non_Null_Raw());

            if (IS_VOID(out)) {
                //
                // compose [("voids *vanish*!" null)] => []
                // compose [(elide "so do 'empty' composes")] => []
            }
            else if (splice and IS_BLOCK(out)) {
                //
                // compose [not-only ([a b]) merges] => [not-only a b merges]

                Cell* push = Cell_Array_At(out);
                if (NOT_END(push)) {
                    //
                    // Only proxy newline flag from the template on *first*
                    // value spliced in (it may have its own newline flag)
                    //
                    Derelativize(PUSH(), push, VAL_SPECIFIER(out));
                    if (GET_VAL_FLAG(L->value, VALUE_FLAG_NEWLINE_BEFORE))
                        SET_VAL_FLAG(TOP, VALUE_FLAG_NEWLINE_BEFORE);

                    while (++push, NOT_END(push))
                        Derelativize(PUSH(), push, VAL_SPECIFIER(out));
                }
            }
            else {
                // compose [(1 + 2) inserts as-is] => [3 inserts as-is]
                // compose/only [([a b c]) unmerged] => [[a b c] unmerged]

                Copy_Cell(PUSH(), out);  // Not legal to eval to stack direct!
                if (GET_VAL_FLAG(L->value, VALUE_FLAG_NEWLINE_BEFORE))
                    SET_VAL_FLAG(TOP, VALUE_FLAG_NEWLINE_BEFORE);
            }

          #ifdef DEBUG_UNREADABLE_BLANKS
            Init_Unreadable(out); // shouldn't leak temp eval to caller
          #endif
        }
        else if (deep) {
            // compose/deep [does [(1 + 2)] nested] => [does [3] nested]

            StackIndex deep_base = TOP_INDEX;
            if (Compose_To_Stack_Throws(
                out,
                L->value,
                specifier,
                pattern,
                true, // deep (guaranteed true if we get here)
                only
            )){
                Drop_Data_Stack_To(base); // drop to outer stack (@ function start)
                Abort_Level(L);
                return true;
            }

            REBFLGS flags = NODE_FLAG_MANAGED | ARRAY_FLAG_FILE_LINE;
            if (GET_SER_FLAG(Cell_Array(L->value), ARRAY_FLAG_TAIL_NEWLINE))
                flags |= ARRAY_FLAG_TAIL_NEWLINE;

            Array* popped = Pop_Stack_Values_Core(deep_base, flags);
            Init_Any_Array(
                PUSH(),
                VAL_TYPE(L->value),
                popped // can't push and pop in same step, need this variable!
            );

            if (GET_VAL_FLAG(L->value, VALUE_FLAG_NEWLINE_BEFORE))
                SET_VAL_FLAG(TOP, VALUE_FLAG_NEWLINE_BEFORE);
        }
        else {
            // compose [[(1 + 2)] (3 + 4)] => [[(1 + 2)] 7] ;-- non-deep
            //
            Derelativize(PUSH(), L->value, L->specifier);  // preserves newline
        }

        Fetch_Next_In_Level(nullptr, L);
    }

    Drop_Level_Unbalanced(L); // Drop_Level() asesrts on stack accumulation
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
DECLARE_NATIVE(compose)
//
// Note: /INTO is intentionally no longer supported
// https://forum.rebol.info/t/stopping-the-into-virus/705
{
    INCLUDE_PARAMS_OF_COMPOSE;

    StackIndex base = TOP_INDEX;

    if (Compose_To_Stack_Throws(
        OUT,
        ARG(value),
        VAL_SPECIFIER(ARG(value)),
        ARG(pattern),
        REF(deep),
        REF(only)
    )){
        return R_THROWN;
    }

    // The stack values contain N NEWLINE_BEFORE flags, and we need N + 1
    // flags.  Borrow the one for the tail directly from the input Array.
    //
    REBFLGS flags = NODE_FLAG_MANAGED | ARRAY_FLAG_FILE_LINE;
    if (GET_SER_FLAG(Cell_Array(ARG(value)), ARRAY_FLAG_TAIL_NEWLINE))
        flags |= ARRAY_FLAG_TAIL_NEWLINE;

    return Init_Any_Array(
        OUT,
        VAL_TYPE(ARG(value)),
        Pop_Stack_Values_Core(base, flags)
    );
}


enum FLATTEN_LEVEL {
    FLATTEN_NOT,
    FLATTEN_ONCE,
    FLATTEN_DEEP
};


static void Flatten_Core(
    Cell* head,
    Specifier* specifier,
    enum FLATTEN_LEVEL level
) {
    Cell* item = head;
    for (; NOT_END(item); ++item) {
        if (IS_BLOCK(item) and level != FLATTEN_NOT) {
            Specifier* derived = Derive_Specifier(specifier, item);
            Flatten_Core(
                Cell_Array_At(item),
                derived,
                level == FLATTEN_ONCE ? FLATTEN_NOT : FLATTEN_DEEP
            );
        }
        else
            Derelativize(PUSH(), item, specifier);
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
DECLARE_NATIVE(flatten)
{
    INCLUDE_PARAMS_OF_FLATTEN;

    StackIndex base = TOP_INDEX;

    Flatten_Core(
        Cell_Array_At(ARG(block)),
        VAL_SPECIFIER(ARG(block)),
        REF(deep) ? FLATTEN_DEEP : FLATTEN_ONCE
    );

    return Init_Block(OUT, Pop_Stack_Values(base));
}
