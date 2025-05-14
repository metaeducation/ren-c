//
//  file: %t-blank.c
//  summary: "Blank datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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


// Considerable debate was invested into whether BLANK! should act like a
// space when formed in string contexts.  As blanks have moved further away
// from representing "nothing" (delegating shades of that to NULL and VOID)
// it seems to make sense that their presence indicate *something*:
//
//    >> append [a b c] _
//    == [a b c _]
//
// But although some contexts (such as DELIMIT) will treat source-level blanks
// as spaces, their general meaning is underscore.
//
//    >> unspaced ["a" _ "b"]
//    == "a b"
//
//    >> unspaced ["a" @blank "b"]
//    == "a_b"
//
//    >> append "abc" _   ; is it better to support this than not?
//    == "abc_"
//
IMPLEMENT_GENERIC(MOLDIFY, Is_Blank)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(v);
    UNUSED(form);

    Append_Ascii(mo->string, "_");

    return TRASH;
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Blank)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    UNUSED(ARG(VALUE1));
    UNUSED(ARG(VALUE2));
    UNUSED(Bool_ARG(RELAX));

    return LOGIC(true);  // all blanks are equal
}


IMPLEMENT_GENERIC(OLDGENERIC, Is_Blank)
{
    switch (Symbol_Id(Level_Verb(LEVEL))) {
      case SYM_SELECT:
      case SYM_FIND:
        return nullptr;

      default:
        break;
    }

    return UNHANDLED;
}


// Because BLANK! is considered EMPTY?, its TO and AS equivalencies are
// to empty series.  TO conversions have to create new stubs, so that
// the series are freshly mutable.
//
IMPLEMENT_GENERIC(TO, Is_Blank)
{
    INCLUDE_PARAMS_OF_TO;

    assert(Is_Blank(ARG(ELEMENT)));
    UNUSED(ARG(ELEMENT));

    Heart to = Cell_Datatype_Builtin_Heart(ARG(TYPE));

    if (Any_List_Type(to))
        return Init_Any_List(OUT, to, Make_Source(0));

    if (Any_String_Type(to))
        return Init_Any_String(OUT, to, Make_String(0));

    if (to == TYPE_WORD)
        return UNHANDLED;

    if (to == TYPE_ISSUE) {
        bool check = Try_Init_Small_Utf8(
            OUT, to, cast(Utf8(const*), ""), 0, 0
        );
        assert(check);
        UNUSED(check);
        return OUT;
    }

    if (to == TYPE_BLOB)
        return Init_Blob(OUT, Make_Binary(0));

    return UNHANDLED;
}


//
//   Trap_Alias_Blank_As: C
//
// AS conversions of blanks to any series or utf8 type can create an
// immutable empty instance, using globally allocated nodes if needed.
//
Option(Error*) Trap_Alias_Blank_As(
    Sink(Element) out,  // unlike TO BLANK!, AS BLANK! result will be immutable
    Heart as
){
    if (Any_List_Type(as)) {
        Init_Any_List(out, as, Cell_Array(g_empty_block));
        return SUCCESS;
    }

    if (Any_String_Type(as)) {
        Init_Any_String(out, as, Cell_String(g_empty_text));
        return SUCCESS;
    }

    if (as == TYPE_ISSUE) {
        bool check = Try_Init_Small_Utf8(
            out, as, cast(Utf8(const*), ""), 0, 0
        );
        assert(check);
        UNUSED(check);
        return SUCCESS;
    }

    if (as == TYPE_BLOB) {
        Init_Blob(out, Cell_Binary(g_empty_blob));
        return SUCCESS;
    }

    return Error_Invalid_Type(as);
}


IMPLEMENT_GENERIC(AS, Is_Blank)
{
    INCLUDE_PARAMS_OF_AS;

    assert(Is_Blank(ARG(ELEMENT)));
    UNUSED(ARG(ELEMENT));

    Heart as = Cell_Datatype_Builtin_Heart(ARG(TYPE));

    Option(Error*) e = Trap_Alias_Blank_As(OUT, as);
    if (e)
        return PANIC(unwrap e);

    return OUT;
}


// The concept is that wherever it can, blank responds the same way that an
// empty list would.  So, we give an error you can TRY to disarm.
//
IMPLEMENT_GENERIC(PICK, Is_Blank)
{
    INCLUDE_PARAMS_OF_PICK;
    UNUSED(ARG(LOCATION));

    return FAIL(Error_Bad_Pick_Raw(ARG(PICKER)));  // act as out of range [1]
}


IMPLEMENT_GENERIC(LENGTH_OF, Is_Blank)
{
    INCLUDE_PARAMS_OF_LENGTH_OF;
    UNUSED(ARG(ELEMENT));

    return Init_Integer(OUT, 0);
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Handle)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(form);  // !!! Handles have "no printable form", what to do here?
    UNUSED(v);

    Append_Ascii(mo->string, "#[handle!]");

    return TRASH;
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Handle)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    Element* a = Element_ARG(VALUE1);
    Element* b = Element_ARG(VALUE2);
    UNUSED(Bool_ARG(RELAX));

    if (Cell_Has_Node1(a) != Cell_Has_Node1(b))
        return LOGIC(false);  // one is shared but other is not

    if (Cell_Has_Node1(a)) {  // shared handles not equal if nodes not equal
        if (CELL_NODE1(a) != CELL_NODE1(b))
            return LOGIC(false);
    }

    // There is no "identity" when it comes to a non-shared handle, so we
    // can only compare the pointers.

    if (Is_Handle_Cfunc(a) != Is_Handle_Cfunc(b))
        return LOGIC(false);

    if (Is_Handle_Cfunc(a)) {
        if (Cell_Handle_Cfunc(a) != Cell_Handle_Cfunc(b))
            return LOGIC(false);
    }
    else {
        if (Cell_Handle_Pointer(Byte, a) != Cell_Handle_Pointer(Byte, b))
            return LOGIC(false);

        if (Cell_Handle_Len(a) != Cell_Handle_Len(b))
            return LOGIC(false);
    }

    return LOGIC(Cell_Handle_Cleaner(a) == Cell_Handle_Cleaner(b));
}
