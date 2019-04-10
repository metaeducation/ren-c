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
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  CT_Datatype: C
//
REBINT CT_Datatype(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    if (mode < 0)
        return -1;  // !!! R3-Alpha-ism (compare never made much sense)

    if (VAL_TYPE_KIND_OR_CUSTOM(a) != VAL_TYPE_KIND_OR_CUSTOM(b))
        return 0;

    if (VAL_TYPE_KIND_OR_CUSTOM(a) == REB_CUSTOM)
        return VAL_TYPE_HOOKS_NODE(a) == VAL_TYPE_HOOKS_NODE(b);

    return 1;
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
void MF_Datatype(REB_MOLD *mo, const REBCEL *v, bool form)
{
    REBSTR *name = Canon(VAL_TYPE_SYM(v));
    if (form)
        Emit(mo, "N", name);
    else
        Emit(mo, "+DN", SYM_DATATYPE_X, name);
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
            "http://datatypes.rebol.info/image [0]",
            "http://datatypes.rebol.info/vector [1]",
            "http://datatypes.rebol.info/gob [2]",
            "http://datatypes.rebol.info/struct [3]",
            "-1",
        "]",
    rebEND);

    if (i != -1)
        return KNOWN(ARR_AT(PG_Extension_Types, i));
    return nullptr;
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

        REBVAL *value = Append_Context(Lib_Context, KNOWN(word), NULL);
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
            VAL_ARRAY(ARR_AT(boot_typespecs, n - 2))
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
        SET_CELL_FLAG(CTX_VAR(Lib_Context, n), PROTECTED);

        Append_Value(catalog, KNOWN(word));
    }

    // !!! Near-term hack to create LIT-WORD! and LIT-PATH!, to try and keep
    // the typechecks working in function specs.  They are set to the words
    // themselves, so that parse rules will work with them (e.g. bootstrap)

    REBVAL *lit_word = Append_Context(
        Lib_Context,
        nullptr,
        Canon(SYM_LIT_WORD_X)
    );
    Init_Builtin_Datatype(lit_word, REB_WORD);
    Quotify(lit_word, 1);

    REBVAL *lit_path = Append_Context(
        Lib_Context,
        nullptr,
        Canon(SYM_LIT_PATH_X)
    );
    Init_Builtin_Datatype(lit_path, REB_PATH);
    Quotify(lit_path, 1);

    REBVAL *refinement = Append_Context(
        Lib_Context,
        nullptr,
        Canon(SYM_REFINEMENT_X)
    );
    Init_Issue(refinement, Canon(SYM_REFINEMENT_X));

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
    for (i = 0; i < 4; ++i) {
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
    TERM_ARRAY_LEN(a, 4);

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
