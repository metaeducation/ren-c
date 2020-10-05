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
REBINT CT_Datatype(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);

    if (VAL_TYPE_KIND_OR_CUSTOM(a) != VAL_TYPE_KIND_OR_CUSTOM(b))
        return VAL_TYPE_KIND_OR_CUSTOM(a) > VAL_TYPE_KIND_OR_CUSTOM(b)
            ? 1
            : -1;

    if (VAL_TYPE_KIND_OR_CUSTOM(a) == REB_CUSTOM) {
        if (VAL_TYPE_HOOKS_NODE(a) == VAL_TYPE_HOOKS_NODE(b))
            return 0;
        return 1;  // !!! all cases of "just return greater" are bad
    }

    return 0;
}


//
//  MAKE_Datatype: C
//
REB_R MAKE_Datatype(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (IS_URL(arg)) {
        REBVAL *custom = Datatype_From_Url(arg);
        if (custom != nullptr)
            return Move_Value(out, custom);
    }
    if (IS_WORD(arg)) {
        REBSYM sym = VAL_WORD_SYM(arg);
        if (sym == SYM_0 or sym >= SYM_FROM_KIND(REB_MAX))
            goto bad_make;

        return Init_Builtin_Datatype(out, KIND_FROM_SYM(sym));
    }

  bad_make:;
    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Datatype: C
//
REB_R TO_Datatype(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    return MAKE_Datatype(out, kind, nullptr, arg);
}


//
//  MF_Datatype: C
//
void MF_Datatype(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    if (not form)
        Pre_Mold_All(mo, v);  // e.g. `#[datatype!`

    const REBSTR *name = Canon(VAL_TYPE_SYM(v));
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

    switch (VAL_WORD_SYM(verb)) {

    case SYM_REFLECT: {
        REBSYM sym = VAL_WORD_SYM(arg);
        if (sym == SYM_SPEC) {
            //
            // The "type specs" were loaded as an array, but this reflector
            // wants to give back an object.  Combine the array with the
            // standard object that mirrors its field order.
            //
            REBCTX *context = Copy_Context_Shallow_Managed(
                VAL_CONTEXT(Get_System(SYS_STANDARD, STD_TYPE_SPEC))
            );

            assert(CTX_TYPE(context) == REB_OBJECT);

            REBVAL *var = CTX_VARS_HEAD(context);
            REBVAL *key = CTX_KEYS_HEAD(context);

            // !!! Account for the "invisible" self key in the current
            // stop-gap implementation of self, still default on MAKE OBJECT!s
            //
            assert(VAL_KEY_SYM(key) == SYM_SELF);
            ++key; ++var;

            RELVAL *item = ARR_HEAD(VAL_TYPE_SPEC(type));

            for (; NOT_END(var); ++var, ++key) {
                if (IS_END(item))
                    Init_Blank(var);
                else {
                    // typespec array does not contain relative values
                    //
                    Derelativize(var, item, SPECIFIED);
                    ++item;
                }
            }

            return Init_Object(D_OUT, context);
        }

        fail (Error_Cannot_Reflect(VAL_TYPE(type), arg)); }

    default:
        break;
    }

    return R_UNHANDLED;
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
        "]",
    rebEND);

    if (i != -1)
        return SPECIFIC(ARR_AT(PG_Extension_Types, i));
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
static void Startup_Fake_Type_Constraint(REBSYM sym)
{
    const REBSTR *canon = Canon(sym);
    REBVAL *char_x = Append_Context(Lib_Context, nullptr, canon);
    Init_Sym_Word(char_x, canon);
}


//
//  Matches_Fake_Type_Constraint: C
//
// Called on SYM-WORD!s by PARSE and MATCH.
//
bool Matches_Fake_Type_Constraint(const RELVAL *v, enum Reb_Symbol sym) {
    switch (sym) {
      case SYM_LIT_WORD_X: 
        return IS_QUOTED_WORD(v);

      case SYM_LIT_PATH_X:
        return IS_QUOTED_PATH(v);

      case SYM_REFINEMENT_X:
        return IS_REFINEMENT(v);

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
REBARR *Startup_Datatypes(REBARR *boot_types, REBARR *boot_typespecs)
{
    if (ARR_LEN(boot_types) != REB_MAX - 2)  // exclude REB_0_END, REB_NULLED
        panic (boot_types);  // every other type should have a WORD!

    RELVAL *word = ARR_HEAD(boot_types);

    if (VAL_WORD_SYM(word) != SYM_VOID_X)
        panic (word);  // First "real" type should be VOID!

    REBARR *catalog = Make_Array(REB_MAX - 2);

    // Put a nulled cell in position [1], just to have something there (the
    // 0 slot is reserved in contexts, so there's no worry about filling space
    // to line up with REB_0_END).  Note this is different from NULL the
    // native, which generates a null (since you'd have to type :NULLED to
    // get a null value, which is awkward).
    //
    REBVAL *nulled = Append_Context(Lib_Context, nullptr, Canon(SYM_NULLED));
    Init_Nulled(nulled);

    REBINT n;
    for (n = 2; NOT_END(word); word++, n++) {
        assert(n < REB_MAX);

        enum Reb_Kind kind = cast(enum Reb_Kind, n);

        REBVAL *value = Append_Context(Lib_Context, SPECIFIC(word), NULL);
        if (kind == REB_CUSTOM) {
            //
            // There shouldn't be any literal CUSTOM! datatype instances.
            // But presently, it lives in the middle of the range of valid
            // cell kinds, so that it will properly register as being in the
            // "not bindable" range.  (Is_Bindable() would be a slower test
            // if it had to account for it.)
            //
            Init_Nulled(value);
            continue;
        }

        RESET_CELL(value, REB_DATATYPE, CELL_FLAG_FIRST_IS_NODE);
        VAL_TYPE_KIND_ENUM(value) = kind;
        VAL_TYPE_SPEC_NODE(value) = NOD(
            VAL_ARRAY_KNOWN_MUTABLE(ARR_AT(boot_typespecs, n - 2))
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
        assert(value == CTX_VAR(Lib_Context, n));
        SET_CELL_FLAG(value, PROTECTED);

        Append_Value(catalog, SPECIFIC(word));
    }

    // !!! Temporary solution until actual type constraints exist.
    //
    Startup_Fake_Type_Constraint(SYM_LIT_WORD_X);
    Startup_Fake_Type_Constraint(SYM_LIT_PATH_X);
    Startup_Fake_Type_Constraint(SYM_REFINEMENT_X);
    Startup_Fake_Type_Constraint(SYM_PREDICATE_X);

    // Extensions can add datatypes.  These types are not identified by a
    // single byte, but give up the `extra` portion of their cell to hold
    // the type information.  The list of types has to be kept by the system
    // in order to translate URL! references to those types.
    //
    // !!! For the purposes of just getting this mechanism off the ground,
    // this establishes it for just the 4 extension types we currently have.
    //
    REBARR *a = Make_Array(4);
    int i;
    for (i = 0; i < 5; ++i) {
        REBTYP *type = Make_Binary(sizeof(CFUNC*) * IDX_HOOKS_MAX);
        CFUNC** hooks = cast(CFUNC**, BIN_HEAD(type));

        hooks[IDX_GENERIC_HOOK] = cast(CFUNC*, &T_Unhooked);
        hooks[IDX_PATH_HOOK] = cast(CFUNC*, &PD_Unhooked);
        hooks[IDX_COMPARE_HOOK] = cast(CFUNC*, &CT_Unhooked);
        hooks[IDX_MAKE_HOOK] = cast(CFUNC*, &MAKE_Unhooked);
        hooks[IDX_TO_HOOK] = cast(CFUNC*, &TO_Unhooked);
        hooks[IDX_MOLD_HOOK] = cast(CFUNC*, &MF_Unhooked);
        hooks[IDX_HOOK_NULLPTR] = nullptr;

        Manage_Series(type);
        Init_Custom_Datatype(Alloc_Tail_Array(a), type);
    }
    TERM_ARRAY_LEN(a, 5);

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
    PATH_HOOK *path,
    COMPARE_HOOK *compare,
    MAKE_HOOK *make,
    TO_HOOK *to,
    MOLD_HOOK *mold
){
    UNUSED(description);

    REBVAL *url_value = rebText(url);
    REBVAL *datatype = Datatype_From_Url(url_value);

    if (not datatype)
        fail (url_value);
    rebRelease(url_value);

    CFUNC** hooks = VAL_TYPE_HOOKS(datatype);

    if (hooks[IDX_GENERIC_HOOK] != cast(CFUNC*, &T_Unhooked))
        fail ("Extension type already registered");

    // !!! Need to fail if already hooked

    hooks[IDX_GENERIC_HOOK] = cast(CFUNC*, generic);
    hooks[IDX_PATH_HOOK] = cast(CFUNC*, path);
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
void Unhook_Datatype(REBSER *type)
{
    // need to fail if not hooked

    CFUNC** hooks = cast(CFUNC**, BIN_HEAD(type));

    if (hooks[IDX_GENERIC_HOOK] == cast(CFUNC*, &T_Unhooked))
        fail ("Extension type not registered to unhook");

    hooks[IDX_GENERIC_HOOK] = cast(CFUNC*, &T_Unhooked);
    hooks[IDX_PATH_HOOK] = cast(CFUNC*, &PD_Unhooked);
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
    Free_Unmanaged_Array(PG_Extension_Types);
    PG_Extension_Types = nullptr;
}
