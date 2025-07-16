//
//  file: %n-reduce.h
//  summary:{REDUCE and COMPOSE natives and associated service routines}
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
//  Startup_Reduce_Errors: C
//
void Startup_Reduce_Errors(void)
{
    g_error_veto = Init_Error(
        Alloc_Value(),
        Error_Veto_Raw()
    );
}


//
//  Shutdown_Reduce_Errors: C
//
void Shutdown_Reduce_Errors(void)
{
    rebRelease(g_error_veto);
    g_error_veto = nullptr;
}


//
//  Is_Error_Veto_Signal: C
//
bool Is_Error_Veto_Signal(Error* e) {
    ERROR_VARS *vars = ERR_VARS(e);
    if (not Is_Word(&vars->id))
        return false;
    return Word_Id(&vars->id) == SYM_VETO;
}


//
//  veto: native [
//
//  "Give back an error with (id = 'veto), used to cancel an operation"
//
//      return: [error!]
//  ]
//
DECLARE_NATIVE(VETO)
{
    INCLUDE_PARAMS_OF_VETO;

    return Copy_Cell(OUT, g_error_veto);
}


//
//  veto?: native [
//
//  "Detect whether argument is an error with (id = 'veto)"
//
//      return: [logic!]
//      atom [any-atom!]
//  ]
//
DECLARE_NATIVE(VETO_Q)
{
    INCLUDE_PARAMS_OF_VETO_Q;

    const Value* atom = ARG(ATOM);

    if (not Is_Error(atom))
        return nullptr;

    return LOGIC(Is_Error_Veto_Signal(cast(Error*, Cell_Varlist(atom))));
}


static Value* Init_Lib_Word(Cell* out, SymId id) {
    return Init_Any_Word_Bound(
        out,
        TYPE_WORD,
        Canon_From_Id(id),
        Lib_Context,
        Find_Canon_In_Context(Lib_Context, Canon_From_Id(id), false)
    );
}


//
//  Any_Metaform: C
//
bool Any_Metaform(Value* v) {
    if (Is_Word(v)) {
        if (
            Word_Id(v) == SYM__TNULL_T
            or Word_Id(v) == SYM__TOKAY_T
            or Word_Id(v) == SYM__TVOID_T
            or Word_Id(v) == SYM_TILDE_1
        ){
            return true;
        }
        return false;
    }

    if (Is_Lit_Word(v) or Is_Lit_Path(v))
        return true;

    return (
        Is_Group(v)
        and Series_Len_At(v) == 2
        and Is_Word(List_At(v))
        and Word_Id(List_At(v)) == SYM_THE
    );
}


//
//  Meta_Quotify: C
//
// Poor man's implementation of the "meta" functionality.
//
// Note that the "quasi-words" don't look up to anything, and are simply
// special cases in the evaluator handling of TYPE_WORD.
//
Value* Meta_Quotify(Value* v)
{
    if (Is_Void(v))
        return Init_Word(v, CANON(_TVOID_T));

    if (Is_Okay(v))
        return Init_Word(v, CANON(_TOKAY_T));

    if (Is_Nulled(v))
        return Init_Word(v, CANON(_TNULL_T));

    if (Is_Trash(v))
        return Init_Word(v, CANON(TILDE_1));

    if (Is_Word(v)) {
        KIND_BYTE(v) = TYPE_LIT_WORD;
        return v;
    }

    if (Is_Path(v)) {
        KIND_BYTE(v) = TYPE_LIT_PATH;
        return v;
    }

    Array* a = Make_Array_Core(2, NODE_FLAG_MANAGED);
    Set_Flex_Len(a, 2);
    Init_Lib_Word(Array_At(a, 0), SYM_THE);
    Copy_Cell(Array_At(a, 1), v);

    return Init_Group(v, a);  // (the <whatever>)
}


//
//  Meta_Unquotify: C
//
Value* Meta_Unquotify(Value* v)
{
    if (Is_Word(v)) {
        switch (Word_Id(v)) {
          case SYM__TVOID_T:
            return Init_Void(v);
          case SYM__TOKAY_T:
            return Init_Okay(v);
          case SYM__TNULL_T:
            return Init_Nulled(v);
          case SYM_TILDE_1:
            return Init_Trash(v);
        default:
          break;
        }
    }
    else if (Is_Lit_Word(v)) {
        KIND_BYTE(v) = TYPE_WORD;
        return v;
    }
    else if (Is_Lit_Path(v)) {
        KIND_BYTE(v) = TYPE_PATH;
        return v;
    }
    else if (
        Is_Group(v)
        and Series_Len_At(v) == 2
        and Is_Word(List_At(v))
        and Word_Id(List_At(v)) == SYM_THE
    ){
        return Copy_Cell(v, cast(Value*, List_At(v) + 1));
    }

    panic (
        "UNMETA needs [~ ~NULL~ ~VOID~ ~OKAY~ LIT-WORD! LIT-PATH! (THE <item>)]"
    );
}


//
//  meta: native [
//
//  "Make expression that when evaluated, will produce the input"
//
//      return: {~null~ if null, or `(the ...)` where ... is passed-in cell}
//          [any-metaform!]
//      value [any-atom!]
//   ]
//
DECLARE_NATIVE(META)
//
// Note: META is done far more elegantly with quasiforms and quoting in the
// mainline code.  This exists in R3C prior to those designs.
//
// (This was initially written in usermode, but since REDUCE and COMPOSE and
// APPEND all won't let you put errors into blocks--as part of the bootstrap
// executable's "simulation of definitional errors"--a native is needed.)
{
    INCLUDE_PARAMS_OF_META;

    Value* v = ARG(VALUE);
    Meta_Quotify(v);
    return Copy_Cell(OUT, v);
}


//
//  unmeta: native [
//
//  "Narrower form of evaluation that only evaluates META products"
//
//      return: [any-atom!]
//      value [any-metaform!]
//   ]
//
DECLARE_NATIVE(UNMETA)
//
// See notes on META
{
    INCLUDE_PARAMS_OF_UNMETA;

    Value* v = ARG(VALUE);
    Meta_Unquotify(v);
    return Copy_Cell(OUT, v);
}


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
        bool line = Get_Cell_Flag(L->value, NEWLINE_BEFORE);

        if (Eval_Step_Throws(SET_END(out), L)) {
            Drop_Data_Stack_To(base);
            Abort_Level(L);
            return true;
        }

        if (IS_END(out)) { // e.g. `reduce [comment "hi"]`
            assert(IS_END(L->value));
            break;
        }

        PANIC_IF_ERROR(out);

        if (Is_Void(out)) {
            // ignore
        }
        else if (Is_Antiform(out)) {
            panic (Error_Bad_Antiform(out));
        }
        else {
            Copy_Cell(PUSH(), out);
            if (line)
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);
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
//          [any-stable!]
//      value "GROUP! and BLOCK! evaluate each item, single values evaluate"
//          [any-stable!]
//  ]
//
DECLARE_NATIVE(REDUCE)
{
    INCLUDE_PARAMS_OF_REDUCE;

    Value* value = ARG(VALUE);

    if (Is_Block(value) or Is_Group(value)) {
        StackIndex base = TOP_INDEX;

        if (Reduce_To_Stack_Throws(OUT, value))
            return BOUNCE_THROWN;

        Flags pop_flags = NODE_FLAG_MANAGED | ARRAY_FLAG_HAS_FILE_LINE;
        if (Get_Array_Flag(Cell_Array(value), NEWLINE_AT_TAIL))
            pop_flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

        return Init_Any_List(
            OUT,
            Type_Of(value),
            Pop_Stack_Values_Core(base, pop_flags)
        );
    }

    // Single element REDUCE does an EVAL, but doesn't allow arguments.
    // (R3-Alpha, would just return the input, e.g. `reduce :foo` => :foo)
    // If there are arguments required, Eval_Value_Throws() will error.
    //
    // !!! Should the error be more "reduce-specific" if args were required?

    if (Any_Inert(value)) // don't bother with the evaluation
        RETURN (value);

    if (Eval_Value_Throws(OUT, value))
        return BOUNCE_THROWN;

    if (not Is_Nulled(OUT))
        return OUT;

    return nullptr; // let caller worry about whether to error on nulls
}


bool Match_For_Compose(const Cell* group, const Value* pattern) {
    if (Is_Nulled(pattern))
        return true;

    if (Series_Len_At(group) == 0) // yhave a pattern, so leave `()` as-is
        return false;

    Cell* first = List_At(group);
    if (not Is_Tag(first))
        return false;
    return 0 == Compare_String_Vals(cast(Value*, first), pattern, true);
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
        if (not Any_List(L->value)) { // non-arrays don't substitute/recurse
            Derelativize(PUSH(), L->value, specifier);  // preserves newline
            Fetch_Next_In_Level(nullptr, L);
            continue;
        }

        bool splice = not only; // can force no splice if override via ((...))

        Specifier* match_specifier = nullptr;
        const Cell* match = nullptr;

        if (not Is_Group(L->value)) {
            //
            // Don't compose at this level, but may need to walk deeply to
            // find compositions inside it if /DEEP and it's an array
        }
        else if (Match_For_Compose(L->value, pattern)) {
            match = L->value;
            match_specifier = specifier;
        }

        if (match) { // only L->value if pattern is just [] or (), else deeper
            REBIXO indexor = Eval_At_Core(
                Init_Void(out), // want empty () to vanish as a VOID would
                nullptr, // no opt_first
                Cell_Array(match),
                VAL_INDEX(match),
                match_specifier,
                EVAL_FLAG_TO_END
            );

            if (indexor == THROWN_FLAG) {
                Drop_Data_Stack_To(base);
                Abort_Level(L);
                return true;
            }

            PANIC_IF_ERROR(out);

            if (Is_Void(out)) {
                //
                // compose [("voids *vanish*!" null)] => []
                // compose [(elide "so do 'empty' composes")] => []
            }
            else if (Is_Antiform(out)) {
                panic (Error_Bad_Antiform(out));
            }
            else if (splice and Is_Block(out)) {
                //
                // compose [not-only ([a b]) merges] => [not-only a b merges]

                Cell* push = List_At(out);
                if (NOT_END(push)) {
                    //
                    // Only proxy newline flag from the template on *first*
                    // value spliced in (it may have its own newline flag)
                    //
                    Derelativize(PUSH(), push, VAL_SPECIFIER(out));
                    if (Get_Cell_Flag(L->value, NEWLINE_BEFORE))
                        Set_Cell_Flag(TOP, NEWLINE_BEFORE);

                    while (++push, NOT_END(push))
                        Derelativize(PUSH(), push, VAL_SPECIFIER(out));
                }
            }
            else {
                // compose [(1 + 2) inserts as-is] => [3 inserts as-is]
                // compose/only [([a b c]) unmerged] => [[a b c] unmerged]

                Copy_Cell(PUSH(), out);  // Not legal to eval to stack direct!
                if (Get_Cell_Flag(L->value, NEWLINE_BEFORE))
                    Set_Cell_Flag(TOP, NEWLINE_BEFORE);
            }

            Init_Unreadable(out); // shouldn't leak temp eval to caller
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

            Flags flags = NODE_FLAG_MANAGED | ARRAY_FLAG_HAS_FILE_LINE;
            if (Get_Array_Flag(Cell_Array(L->value), NEWLINE_AT_TAIL))
                flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

            Array* popped = Pop_Stack_Values_Core(deep_base, flags);
            Init_Any_List(
                PUSH(),
                Type_Of(L->value),
                popped // can't push and pop in same step, need this variable!
            );

            if (Get_Cell_Flag(L->value, NEWLINE_BEFORE))
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);
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
//      return: [any-list!]
//      :pattern "Distinguish compose groups, e.g. [(plain) (* composed *)]"
//          [<skip> tag!]
//      value "Array to use as the template for substitution"
//          [any-list!]
//      /deep "Compose deeply into nested arrays"
//      /only "Insert arrays as single value (not as contents of array)"
//  ]
//
DECLARE_NATIVE(COMPOSE)
//
// Note: /INTO is intentionally no longer supported
// https://forum.rebol.info/t/stopping-the-into-virus/705
{
    INCLUDE_PARAMS_OF_COMPOSE;

    StackIndex base = TOP_INDEX;

    if (Compose_To_Stack_Throws(
        OUT,
        ARG(VALUE),
        VAL_SPECIFIER(ARG(VALUE)),
        ARG(PATTERN),
        Bool_ARG(DEEP),
        Bool_ARG(ONLY)
    )){
        return BOUNCE_THROWN;
    }

    // The stack values contain N NEWLINE_BEFORE flags, and we need N + 1
    // flags.  Borrow the one for the tail directly from the input Array.
    //
    Flags flags = NODE_FLAG_MANAGED | ARRAY_FLAG_HAS_FILE_LINE;
    if (Get_Array_Flag(Cell_Array(ARG(VALUE)), NEWLINE_AT_TAIL))
        flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

    return Init_Any_List(
        OUT,
        Type_Of(ARG(VALUE)),
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
        if (Is_Block(item) and level != FLATTEN_NOT) {
            Specifier* derived = Derive_Specifier(specifier, item);
            Flatten_Core(
                List_At(item),
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
DECLARE_NATIVE(FLATTEN)
{
    INCLUDE_PARAMS_OF_FLATTEN;

    StackIndex base = TOP_INDEX;

    Flatten_Core(
        List_At(ARG(BLOCK)),
        VAL_SPECIFIER(ARG(BLOCK)),
        Bool_ARG(DEEP) ? FLATTEN_DEEP : FLATTEN_ONCE
    );

    return Init_Block(OUT, Pop_Stack_Values(base));
}
