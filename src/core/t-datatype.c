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

    if (IS_URL(arg)) {
        REBVAL *custom = Datatype_From_Url(arg);
        if (custom != nullptr)
            return Copy_Cell(OUT, custom);
    }
    if (IS_WORD(arg)) {
        SYMID sym = VAL_WORD_ID(arg);
        if (sym == SYM_0 or sym >= SYM_FROM_KIND(REB_MAX))
            goto bad_make;

        return Init_Builtin_Datatype(OUT, KIND_FROM_SYM(sym));
    }

  bad_make:

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
void MF_Datatype(REB_MOLD *mo, noquote(Cell(const*)) v, bool form)
{
    if (not form)
        Pre_Mold_All(mo, v);  // e.g. `#[datatype!`

    String(const*) name = Canon_Symbol(VAL_TYPE_SYM(v));
    Append_Spelling(mo->series, name);

    if (not form)
        End_Mold_All(mo);  // e.g. `]`
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

    case SYM_REFLECT: {
        SYMID sym = VAL_WORD_ID(arg);
        if (sym == SYM_SPEC) {
            //
            // The "type specs" were loaded as an array, but this reflector
            // wants to give back an object.  Combine the array with the
            // standard object that mirrors its field order.
            //
            Context(*) context = Copy_Context_Shallow_Managed(
                VAL_CONTEXT(Get_System(SYS_STANDARD, STD_TYPE_SPEC))
            );

            assert(CTX_TYPE(context) == REB_OBJECT);

            const REBKEY *key_tail;
            const REBKEY *key = CTX_KEYS(&key_tail, context);

            REBVAR *var = CTX_VARS_HEAD(context);

            Cell(const*) item_tail = ARR_TAIL(VAL_TYPE_SPEC(type));
            Cell(*) item = ARR_HEAD(VAL_TYPE_SPEC(type));

            for (; key != key_tail; ++key, ++var) {
                if (item == item_tail)
                    Init_Blank(var);
                else {
                    // typespec array does not contain relative values
                    //
                    Derelativize(var, item, SPECIFIED);
                    ++item;
                }
            }

            return Init_Object(OUT, context);
        }

        fail (Error_Cannot_Reflect(VAL_TYPE(type), arg)); }

    default:
        break;
    }

    return BOUNCE_UNHANDLED;
}



//
//  Datatype_From_Url: C
//
// !!! This is a hack until there's a good way for types to encode the URL
// they represent in their spec somewhere.  It's just here to help get past
// the point of the fixed list of REB_XXX types--first step is just expanding
// to take four out.
//
REBVAL *Datatype_From_Url(const REBVAL *url) {
    int i = rebUnbox(
        "switch", url, "[",
            "http://datatypes.rebol.info/library [0]",
            "http://datatypes.rebol.info/image [1]",
            "http://datatypes.rebol.info/vector [2]",
            "http://datatypes.rebol.info/gob [3]",
            "http://datatypes.rebol.info/struct [4]",
            "-1",
        "]"
    );

    if (i != -1)
        return cast(REBVAL*, ARR_AT(PG_Extension_Types, i));
    return nullptr;
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
static void Startup_Fake_Type_Constraint(SYMID sym)
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
    if (ARR_LEN(boot_types) != REB_MAX - 1)  // exclude REB_NULL (not a type)
        panic (boot_types);  // other types/internals should have a WORD!

    Array(*) catalog = Make_Array(REB_MAX - 1);

    Cell(const*) word_tail = ARR_TAIL(boot_types);
    Cell(*) word = ARR_HEAD(boot_types);

    REBINT n = VAL_WORD_ID(word);
    if (n != SYM_0 + 1)  // first symbol (SYM_NULL is something random)
        panic (word);

    for (; word != word_tail; ++word, ++n) {
        enum Reb_Kind kind = cast(enum Reb_Kind, n);

        REBVAL *value = Append_Context(Lib_Context, SPECIFIC(word), nullptr);

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

        Reset_Unquoted_Header_Untracked(
            TRACK(value),
            FLAG_HEART_BYTE(REB_DATATYPE) | CELL_FLAG_FIRST_IS_NODE
        );
        VAL_TYPE_KIND_ENUM(value) = kind;
        INIT_VAL_TYPE_SPEC(value,
            VAL_ARRAY(ARR_AT(boot_typespecs, n - 1))
        );

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

    // Extensions can add datatypes.  These types are not identified by a
    // single byte, but give up the `extra` portion of their cell to hold
    // the type information.  The list of types has to be kept by the system
    // in order to translate URL! references to those types.
    //
    // !!! For the purposes of just getting this mechanism off the ground,
    // this establishes it for just the 4 extension types we currently have.
    //
    Array(*) a = Make_Array(4);
    int i;
    for (i = 0; i < 5; ++i) {
        REBTYP *type = Make_Binary(sizeof(CFUNC*) * IDX_HOOKS_MAX);
        CFUNC** hooks = cast(CFUNC**, BIN_HEAD(type));

        hooks[IDX_GENERIC_HOOK] = cast(CFUNC*, &T_Unhooked);
        hooks[IDX_COMPARE_HOOK] = cast(CFUNC*, &CT_Unhooked);
        hooks[IDX_MAKE_HOOK] = cast(CFUNC*, &MAKE_Unhooked);
        hooks[IDX_TO_HOOK] = cast(CFUNC*, &TO_Unhooked);
        hooks[IDX_MOLD_HOOK] = cast(CFUNC*, &MF_Unhooked);
        hooks[IDX_HOOK_NULLPTR] = nullptr;

        Manage_Series(type);
        Init_Custom_Datatype(Alloc_Tail_Array(a), type);
    }
    SET_SERIES_LEN(a, 5);

    PG_Extension_Types = a;

    return catalog;
}


//
//  Hook_Datatype: C
//
// Poor-man's user-defined type hack: this really just gives the ability to
// have the only thing the core knows about a "user-defined-type" be its
// value cell structure and datatype enum number...but have the behaviors
// come from functions that are optionally registered in an extension.
//
// (Actual facets of user-defined types will ultimately be dispatched through
// Rebol-frame-interfaced functions, not raw C structures like this.)
//
REBTYP *Hook_Datatype(
    const char *url,
    const char *description,
    GENERIC_HOOK *generic,
    COMPARE_HOOK *compare,
    MAKE_HOOK *make,
    TO_HOOK *to,
    MOLD_HOOK *mold
){
    UNUSED(description);

    REBVAL *url_value = rebValue("as url!", rebT(url));
    REBVAL *datatype = Datatype_From_Url(url_value);

    if (not datatype)
        fail (url_value);
    rebRelease(url_value);

    CFUNC** hooks = VAL_TYPE_HOOKS(datatype);

    if (hooks[IDX_GENERIC_HOOK] != cast(CFUNC*, &T_Unhooked))
        fail ("Extension type already registered");

    // !!! Need to fail if already hooked

    hooks[IDX_GENERIC_HOOK] = cast(CFUNC*, generic);
    hooks[IDX_COMPARE_HOOK] = cast(CFUNC*, compare);
    hooks[IDX_MAKE_HOOK] = cast(CFUNC*, make);
    hooks[IDX_TO_HOOK] = cast(CFUNC*, to);
    hooks[IDX_MOLD_HOOK] = cast(CFUNC*, mold);
    hooks[IDX_HOOK_NULLPTR] = nullptr;

    return VAL_TYPE_CUSTOM(datatype);  // filled in now
}


//
//  Unhook_Datatype: C
//
void Unhook_Datatype(REBTYP *type)
{
    // need to fail if not hooked

    CFUNC** hooks = cast(CFUNC**, BIN_HEAD(type));

    if (hooks[IDX_GENERIC_HOOK] == cast(CFUNC*, &T_Unhooked))
        fail ("Extension type not registered to unhook");

    hooks[IDX_GENERIC_HOOK] = cast(CFUNC*, &T_Unhooked);
    hooks[IDX_COMPARE_HOOK] = cast(CFUNC*, &CT_Unhooked);
    hooks[IDX_MAKE_HOOK] = cast(CFUNC*, &MAKE_Unhooked);
    hooks[IDX_TO_HOOK] = cast(CFUNC*, &TO_Unhooked);
    hooks[IDX_MOLD_HOOK] = cast(CFUNC*, &MF_Unhooked);
    hooks[IDX_HOOK_NULLPTR] = nullptr;
}


//
//  Shutdown_Datatypes: C
//
void Shutdown_Datatypes(void)
{
    Free_Unmanaged_Series(PG_Extension_Types);
    PG_Extension_Types = nullptr;
}
