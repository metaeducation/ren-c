//
//  File: %t-datatype.c
//  Summary: "datatype datatype"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  CT_Datatype: C
//
REBINT CT_Datatype(noquote(Cell(const*)) a, noquote(Cell(const*)) b, bool strict)
{
    UNUSED(strict);

    if (VAL_TYPE_QUOTEDNESS(a) != VAL_TYPE_QUOTEDNESS(b))
        return VAL_TYPE_QUOTEDNESS(a) > VAL_TYPE_QUOTEDNESS(b)
            ? 1
            : -1;

    if (VAL_TYPE_KIND_OR_CUSTOM(a) != VAL_TYPE_KIND_OR_CUSTOM(b))
        return VAL_TYPE_KIND_OR_CUSTOM(a) > VAL_TYPE_KIND_OR_CUSTOM(b)
            ? 1
            : -1;

    if (VAL_TYPE_KIND_OR_CUSTOM(a) == REB_CUSTOM) {
        if (VAL_TYPE_HOOKS(a) == VAL_TYPE_HOOKS(b))
            return 0;
        return 1;  // !!! all cases of "just return greater" are bad
    }

    return 0;
}


//
//  MAKE_Datatype: C
//
Bounce MAKE_Datatype(
    Frame(*) frame_,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

    return RAISE(Error_Bad_Make(kind, arg));
}


//
//  TO_Datatype: C
//
Bounce TO_Datatype(Frame(*) frame_, enum Reb_Kind kind, const REBVAL *arg) {
    return MAKE_Datatype(frame_, kind, nullptr, arg);
}


//
//  MF_Datatype: C
//
// !!! Today's datatype is not actually an ANY-BLOCK!, but wants to render
// as one.  To avoid writing duplicate code, we synthesize the block that would
// be avaliable to us if types were actually implemented via arrays...so that
// the quotedness/quasiness renders correctly.
//
void MF_Datatype(REB_MOLD *mo, noquote(Cell(const*)) v, bool form)
{
    Array(*) a = Alloc_Singular(NODE_FLAG_MANAGED);
    Init_Word(ARR_SINGLE(a), VAL_TYPE_SYMBOL(v));
    mutable_QUOTE_BYTE(ARR_SINGLE(a)) = VAL_TYPE_QUOTEDNESS(v);

    DECLARE_LOCAL (temp);
    Init_Block(temp, a);

    Append_Codepoint(mo->series, '&');

    PUSH_GC_GUARD(temp);
    Mold_Or_Form_Value(mo, temp, form);
    DROP_GC_GUARD(temp);
}


//
//  REBTYPE: C
//
REBTYPE(Datatype)
{
    REBVAL *type = D_ARG(1);
    assert(IS_DATATYPE(type));

    REBVAL *arg = D_ARG(2);

    switch (ID_OF_SYMBOL(verb)) {
      case SYM_REFLECT:
        //
        // !!! This used to support SPEC OF for datatypes, coming from strings
        // that are built into the executable from the information in %types.r.
        // It was removed when DATATYPE! no longer existed as its own reified
        // type with a pointer in the cell for such information.
        //
        // But generally speaking, help information of this nature seems like
        // something that shouldn't be built into the core, but an optional
        // part of the help system...covered by usermode code.
        //
        fail (Error_Cannot_Reflect(VAL_TYPE(type), arg));

      default:
        break;
    }

    return BOUNCE_UNHANDLED;
}


//
//  Startup_Fake_Type_Constraints: C
//
// Consolidating types like REFINEMENT! into a specific instance of PATH!,
// or CHAR! into a specific instance of ISSUE!, reduces the total number of
// fundamental datatypes and offers consistency and flexibility.  But there
// is no standard mechanism for expressing a type constraint in a function
// spec (e.g. "integer!, but it must be even") so the unification causes
// a loss of that check.
//
// A true solution to the problem needs to be found.  But until it is, this
// creates some fake values that can be used by function specs which at least
// give an annotation of the constraint.  They are in Lib_Context so that
// native specs can use them.
//
// While they have no teeth in typeset creation (they only verify that the
// unconstrained form of the type matches), PARSE recognizes the symbol and
// enforces it.
//
static void Startup_Fake_Type_Constraint(SymId sym)
{
    Init_Meta_Word(Force_Lib_Var(sym), Canon_Symbol(sym));
}


//
//  Matches_Fake_Type_Constraint: C
//
// Called on META-WORD!s by PARSE and MATCH.
//
bool Matches_Fake_Type_Constraint(Cell(const*) v, enum Reb_Symbol_Id sym) {
    switch (sym) {
      case SYM_CHAR_X:
        return IS_CHAR(v);

      case SYM_BLACKHOLE_X:
        return IS_CHAR(v) and VAL_CHAR(v) == 0;

      case SYM_LIT_WORD_X:
        return IS_QUOTED_WORD(v);

      case SYM_LIT_PATH_X:
        return IS_QUOTED_PATH(v);

      case SYM_REFINEMENT_X:
        return IS_PATH(v) and IS_REFINEMENT(v);

      case SYM_PREDICATE_X:
        return IS_PREDICATE(v);

      default:
        fail ("Invalid fake type constraint");
    }
}


//
//  Startup_Datatypes: C
//
// Create library words for each type, (e.g. make INTEGER! correspond to
// the integer datatype value).  Returns an array of words for the added
// datatypes to use in SYSTEM/CATALOG/DATATYPES.  See %boot/types.r
//
Array(*) Startup_Datatypes(Array(*) boot_types, Array(*) boot_typespecs)
{
    UNUSED(boot_typespecs);  // not currently used

    if (ARR_LEN(boot_types) != REB_MAX - 1)  // exclude REB_NULL (not a type)
        panic (boot_types);  // other types/internals should have a WORD!

    Array(*) catalog = Make_Array(REB_MAX - 1);

    Cell(const*) word_tail = ARR_TAIL(boot_types);
    Cell(*) word = ARR_HEAD(boot_types);

    REBINT n = VAL_WORD_ID(word);
    if (n != SYM_LOGIC_X)  // first symbol (SYM_NULL is something random)
        panic (word);

    for (; word != word_tail; ++word, ++n) {
        enum Reb_Kind kind = cast(enum Reb_Kind, n);

        assert(Canon_Symbol(cast(SymId, kind)) == VAL_WORD_SYMBOL(word));

        Value(*) value = Force_Lib_Var(cast(SymId, kind));

        if (kind == REB_BYTES) {
            Init_Word_Isotope(value, Canon(BYTES));
            Set_Cell_Flag(value, PROTECTED);
            continue;
        }
        if (kind == REB_CUSTOM) {
            //
            // There shouldn't be any literal CUSTOM! datatype instances.
            // But presently, it lives in the middle of the range of valid
            // cell kinds, so that it will properly register as being in the
            // "not bindable" range.  (Is_Bindable() would be a slower test
            // if it had to account for it.)
            //
            Init_Word_Isotope(value, Canon(CUSTOM));
            Set_Cell_Flag(value, PROTECTED);
            continue;
        }

        // !!! Currently datatypes are just molded specially to look like an
        // ANY-BLOCK! type, so they seem like &[integer] or &['word].  But the
        // idea is that they will someday actually be blocks, so having some
        // read-only copies of the common types remade would save on series
        // allocations.  We pre-build the types into the lib slots in an
        // anticipation of that change.
        //
        Reset_Unquoted_Header_Untracked(TRACK(value), CELL_MASK_DATATYPE);
        INIT_VAL_TYPE_SYMBOL(value, Canon_Symbol(SYM_FROM_KIND(kind)));
        INIT_VAL_TYPE_QUOTEDNESS(value, UNQUOTED_1);

        // !!! The system depends on these definitions, as they are used by
        // Get_Type and Type_Of.  Lock it for safety...though consider an
        // alternative like using the returned types catalog and locking
        // that.  (It would be hard to rewrite lib to safely change a type
        // definition, given the code doing the rewriting would likely depend
        // on lib...but it could still be technically possible, even in
        // a limited sense.)
        //
        assert(value == Datatype_From_Kind(kind));
        Set_Cell_Flag(value, PROTECTED);

        Append_Value(catalog, SPECIFIC(word));
    }

    // !!! Temporary solution until actual type constraints exist.
    //
    Startup_Fake_Type_Constraint(SYM_CHAR_X);
    Startup_Fake_Type_Constraint(SYM_LIT_WORD_X);
    Startup_Fake_Type_Constraint(SYM_LIT_PATH_X);
    Startup_Fake_Type_Constraint(SYM_REFINEMENT_X);
    Startup_Fake_Type_Constraint(SYM_PREDICATE_X);
    Startup_Fake_Type_Constraint(SYM_BLACKHOLE_X);

    return catalog;
}


//
//  Shutdown_Datatypes: C
//
void Shutdown_Datatypes(void)
{
}
