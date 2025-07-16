//
//  file: %t-typeset.c
//  summary: "typeset datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
// symbol-to-typeset-bits mapping table
//
// NOTE: Order of symbols is important, because this is used to build a
// list of typeset word symbols ordered relative to their symbol #,
// which lays out the legal unbound WORD! values you can use during
// a MAKE TYPESET! (bound words will be looked up as variables to see
// if they contain a DATATYPE! or a typeset, but general reduction is
// not performed on the block passed in.)
//
// !!! Is it necessary for MAKE TYPESET! to allow unbound words at all,
// or should the typesets be required to be in bound variables?  Should
// clients be asked to pass in only datatypes and typesets, hence doing
// their own reduce before trying to make a typeset out of a block?
//
const struct {
    SymId sym;
    REBU64 bits;
} Typesets[] = {
    {SYM_ANY_VALUE_X, TS_VALUE},
    {SYM_ANY_STABLE_X, TS_STABLE},
    {SYM_ANY_EQUATABLE_X, TS_STABLE},
    {SYM_ANY_ELEMENT_X, TS_ELEMENT},
    {SYM_LOGIC_X, TS_LOGIC},
    {SYM_ANY_METAFORM_X, TS_METAFORM},
    {SYM_ANY_WORD_X, TS_WORD},
    {SYM_ANY_PATH_X, TS_PATH},
    {SYM_ANY_NUMBER_X, TS_NUMBER},
    {SYM_ANY_SCALAR_X, TS_SCALAR},
    {SYM_ANY_SERIES_X, TS_SERIES},
    {SYM_ANY_STRING_X, TS_STRING},
    {SYM_ANY_CONTEXT_X, TS_CONTEXT},
    {SYM_ANY_LIST_X, TS_LIST},

    {SYM_0_internal, 0}
};


//
//  CT_Typeset: C
//
REBINT CT_Typeset(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode < 0) return -1;
    return Typesets_Equal(a, b);
}


//
//  Startup_Typesets: C
//
// Create typeset variables that are defined above.
// For example: NUMBER is both integer and decimal.
// Add the new variables to the system context.
//
void Startup_Typesets(void)
{
    StackIndex base = TOP_INDEX;

    REBINT n;
    for (n = 0; Typesets[n].sym != 0; n++) {
        //
        // Note: the symbol in the typeset is not the symbol of a word holding
        // the typesets, rather an extra data field used when the typeset is
        // in a context key slot to identify that field's name
        //
        Init_Typeset(PUSH(), Typesets[n].bits, nullptr);

        Copy_Cell(
            Append_Context(Lib_Context, nullptr, Canon_From_Id(Typesets[n].sym)),
            TOP
        );
    }

    // !!! Why does the system access the typesets through Lib_Context, vs.
    // using the Root_Typesets?
    //
    Root_Typesets = Init_Block(Alloc_Value(), Pop_Stack_Values(base));

    Flex* locker = nullptr;
    Force_Value_Frozen_Deep(Root_Typesets, locker);
}


//
//  Shutdown_Typesets: C
//
void Shutdown_Typesets(void)
{
    rebRelease(Root_Typesets);
    Root_Typesets = nullptr;
}


//
//  Init_Typeset: C
//
// Name should be set when a typeset is being used as a function parameter
// specifier, or as a key in an object.
//
Value* Init_Typeset(Cell* out, REBU64 bits, Symbol* opt_name)
{
    RESET_CELL(out, TYPE_TYPESET);
    INIT_TYPESET_NAME(out, opt_name);
    Cell_Typeset_Bits(out) = bits;
    return cast(Value*, out);
}


//
//  Update_Typeset_Bits_Core: C
//
// This sets the bits in a bitset according to a block of datatypes.
//
// !!! R3-Alpha supported fixed word symbols for datatypes and typesets.
// Confusingly, this means that if you have said `word!: integer!` and use
// WORD!, you will get the integer type... but if WORD! is unbound then it
// will act as WORD!.  Also, is essentially having "keywords" and should be
// reviewed to see if anything actually used it.
//
bool Update_Typeset_Bits_Core(
    Cell* typeset,
    const Cell* head,
    Specifier* specifier
){
    bool clear_trash_flag = true;
    if (Key_Symbol(typeset) and (Key_Id(typeset) == SYM_RETURN))
        clear_trash_flag = false;

    bool dont_take_null_as_input = false;

    assert(Is_Typeset(typeset));
    Cell_Typeset_Bits(typeset) = 0;

    const Cell* maybe_word = head;
    for (; NOT_END(maybe_word); ++maybe_word) {
        const Cell* item;

        if (Is_Word(maybe_word)) {
            if (Word_Id(maybe_word) == SYM_TILDE_1) {  // ~
                Set_Typeset_Flag(typeset, TYPE_TRASH);
                clear_trash_flag = false;
                continue;
            }
            if (
                Word_Id(maybe_word) == SYM__TNULL_T  // ~null~
                or Word_Id(maybe_word) == SYM_NULL_Q  // null?
            ){
                Set_Typeset_Flag(typeset, TYPE_NULLED);
                continue;
            }
            else if (
                Word_Id(maybe_word) == SYM__TVOID_T  // ~void~
                or Word_Id(maybe_word) == SYM_VOID_Q  // void?
            ){
                Set_Typeset_Flag(typeset, TYPE_VOID);
                continue;
            }
            else if (
                Word_Id(maybe_word) == SYM__TOKAY_T  // ~void~
                or Word_Id(maybe_word) == SYM_OKAY_Q  // void?
            ){
                Set_Typeset_Flag(typeset, TYPE_OKAY);
                continue;
            }
            item = Get_Opt_Var_May_Panic(maybe_word, specifier);
            if (not item)
                panic (Error_No_Value_Core(maybe_word, specifier));
        }
        else
            item = maybe_word; // wasn't variable

        if (Is_Tag(item)) {
            if (0 == Compare_String_Vals(item, Root_Ellipsis_Tag, true)) {
                Set_Typeset_Flag(typeset, TYPE_TS_VARIADIC);
            }
            else if (0 == Compare_String_Vals(item, Root_End_Tag, true)) {
                Set_Typeset_Flag(typeset, TYPE_TS_ENDABLE);
            }
            else if (0 == Compare_String_Vals(item, Root_Opt_Out_Tag, true)) {
                dont_take_null_as_input = true;
                Set_Typeset_Flag(typeset, TYPE_VOID);  // accepts, but noops
                Set_Typeset_Flag(typeset, TYPE_TS_NOOP_IF_VOID);
            }
            else if (0 == Compare_String_Vals(item, Root_Undo_Opt_Tag, true)) {
                dont_take_null_as_input = true;
                Set_Typeset_Flag(typeset, TYPE_VOID);  // accepts, but nulls
                Set_Typeset_Flag(typeset, TYPE_TS_NULL_IF_VOID);
            }
            else if (0 == Compare_String_Vals(item, Root_Skip_Tag, true)) {
                if (Cell_Parameter_Class(typeset) != PARAMCLASS_HARD_QUOTE)
                    panic ("Only hard-quoted parameters are <skip>-able");

                Set_Typeset_Flag(typeset, TYPE_TS_SKIPPABLE);
                Set_Typeset_Flag(typeset, TYPE_TS_ENDABLE); // skip => null
            }
        }
        else if (Is_Datatype(item)) {
            assert(CELL_DATATYPE_TYPE(item) != TYPE_0);
            Set_Typeset_Flag(typeset, CELL_DATATYPE_TYPE(item));
            if (CELL_DATATYPE_TYPE(item) == TYPE_TRASH)
                clear_trash_flag = false;
        }
        else if (Is_Typeset(item)) {
            Cell_Typeset_Bits(typeset) |= Cell_Typeset_Bits(item);
        }
        else
            panic (Error_Invalid_Core(maybe_word, specifier));
    }

    // If you say ANY-VALUE! on a non-RETURN: then most arguments don't get
    // TRASH! even though it's a "member" of ANY-VALUE! (e.g. something a
    // variable can hold, even though you can't put it in blocks).  You have
    // to explicitly say TRASH! to get it.
    //
    // We override this if using the TS_VALUE typeset
    //
    if (clear_trash_flag)
        if (TS_VALUE != (Cell_Typeset_Bits(typeset) & TS_VALUE))
            Clear_Typeset_Flag(typeset, TYPE_TRASH);

    // If you use <opt-out> or <undo-opt> then null is not legal as an input
    // even if you say ANY-VALUE! in the types.  But do note that <undo-opt>
    // will turn the cell into a null for the function run, despite not
    // typechecking null on the interface.
    //
    if (dont_take_null_as_input)
        Clear_Typeset_Flag(typeset, TYPE_NULLED);

    return true;
}


//
//  MAKE_Typeset: C
//
Bounce MAKE_Typeset(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == TYPE_TYPESET);
    UNUSED(kind);

    if (Is_Typeset(arg))
        return Copy_Cell(out, arg);

    if (!Is_Block(arg)) goto bad_make;

    Init_Typeset(out, 0, nullptr);
    Update_Typeset_Bits_Core(out, List_At(arg), VAL_SPECIFIER(arg));
    return out;

  bad_make:
    panic (Error_Bad_Make(TYPE_TYPESET, arg));
}


//
//  TO_Typeset: C
//
Bounce TO_Typeset(Value* out, enum Reb_Kind kind, const Value* arg)
{
    return MAKE_Typeset(out, kind, arg);
}


//
//  Typeset_To_Array: C
//
// Converts typeset value to a block of datatypes, no order is guaranteed.
//
Array* Typeset_To_Array(const Value* tset)
{
    StackIndex base = TOP_INDEX;

    REBINT n;
    for (n = 1; n < TYPE_NULLED; ++n) {
        if (Typeset_Check(tset, cast(enum Reb_Kind, n))) {
            if (n == TYPE_NULLED) {
                Init_Word(PUSH(), CANON(_TNULL_T));
            }
            else if (n == TYPE_VOID) {
                Init_Word(PUSH(), CANON(_TVOID_T));
            }
            else
                Init_Datatype(PUSH(), cast(enum Reb_Kind, n));
        }
    }

    return Pop_Stack_Values(base);
}


//
//  MF_Typeset: C
//
void MF_Typeset(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form);

    REBINT n;

    Begin_Non_Lexical_Mold(mo, v);  // #[typeset! or make typeset!
    Append_Codepoint(mo->utf8flex, '[');

  #if RUNTIME_CHECKS
    Symbol* symbol = Key_Symbol(v);
    if (symbol != nullptr) {
        //
        // In debug builds we're probably more interested in the symbol than
        // the typesets, if we are looking at a PARAMLIST or KEYLIST.
        //
        Append_Unencoded(mo->utf8flex, "(");

        Append_Utf8_Utf8(mo->utf8flex, Symbol_Head(symbol), Symbol_Size(symbol));
        Append_Unencoded(mo->utf8flex, ") ");

        // REVIEW: should detect when a lot of types are active and condense
        // only if the number of types is unreasonable (often for keys/params)
        //
        if (true) {
            Append_Unencoded(mo->utf8flex, "...");
            goto skip_types;
        }
    }
  #endif

    assert(not Typeset_Check(v, TYPE_0)); // TYPE_0 is used for internal purposes

    // Convert bits to types.
    //
    for (n = TYPE_0 + 1; n < TYPE_MAX; n++) {
        if (Typeset_Check(v, cast(enum Reb_Kind, n))) {
            MF_Datatype(mo, Datatype_From_Kind(cast(enum Reb_Kind, n)), false);
            Append_Codepoint(mo->utf8flex, ' ');
        }
    }
    Trim_Tail(mo->utf8flex, ' ');

#if RUNTIME_CHECKS
skip_types:
#endif

    Append_Codepoint(mo->utf8flex, ']');
    End_Non_Lexical_Mold(mo);
}


//
//  REBTYPE: C
//
REBTYPE(Typeset)
{
    Value* val = D_ARG(1);
    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;

    switch (Word_Id(verb)) {

    case SYM_FIND:
        if (not Is_Datatype(arg))
            panic (Error_Invalid(arg));

        if (Typeset_Check(val, CELL_DATATYPE_TYPE(arg)))
            return LOGIC(true);

        return LOGIC(false);

    case SYM_INTERSECT:
    case SYM_UNION:
    case SYM_DIFFERENCE:
        if (Is_Datatype(arg)) {
            Cell_Typeset_Bits(arg) = FLAGIT_KIND(Type_Of(arg));
        }
        else if (not Is_Typeset(arg))
            panic (Error_Invalid(arg));

        if (Word_Id(verb) == SYM_UNION)
            Cell_Typeset_Bits(val) |= Cell_Typeset_Bits(arg);
        else if (Word_Id(verb) == SYM_INTERSECT)
            Cell_Typeset_Bits(val) &= Cell_Typeset_Bits(arg);
        else {
            assert(Word_Id(verb) == SYM_DIFFERENCE);
            Cell_Typeset_Bits(val) ^= Cell_Typeset_Bits(arg);
        }
        RETURN (val);

    case SYM_COMPLEMENT:
        Cell_Typeset_Bits(val) = ~Cell_Typeset_Bits(val);
        RETURN (val);

    default:
        break;
    }

    panic (Error_Illegal_Action(TYPE_TYPESET, verb));
}
