//
//  file: %n-io.c
//  summary: "native functions for input and output"
//  section: natives
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


//
//  form: native [
//
//  "Converts a value to a human-readable string"
//
//      return: [<null> text!]
//      value "The value to form (currently errors on antiforms)"
//          [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(FORM)
{
    INCLUDE_PARAMS_OF_FORM;

    Element* elem = Element_ARG(VALUE);

    return Init_Text(OUT, Copy_Form_Element(elem, 0));
}


//
//  moldify: native:generic [
//
//  "Stopgap concept for methodizing mold using new generics"
//
//      return: []  ; returning a string would be too slow to compound
//      value [fundamental?]
//      molder "Settings for the mold, including in progress series"
//          [handle!]
//      form "Do not put system delimiters on item"
//          [logic?]
//  ]
//
DECLARE_NATIVE(MOLDIFY)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    USED(ARG(MOLDER));  // passed via LEVEL
    USED(ARG(FORM));

    return Dispatch_Generic(MOLDIFY, Element_ARG(VALUE), LEVEL);
}


IMPLEMENT_GENERIC(MOLDIFY, Any_Fundamental)  // catch-all for ExtraHeart*
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    UNUSED(ARG(FORM));

    Element* custom = Element_ARG(VALUE);
    /*assert(Heart_Of_Is_0(custom));*/  // !!! currently does HANDLE!

    Begin_Non_Lexical_Mold(mo, custom);
    Mold_Or_Form_Cell_Ignore_Quotes(mo, g_empty_block, false);
    End_Non_Lexical_Mold(mo);

    return TRASH;  // no return value
}


//
//  mold: native [
//
//  "Converts value to a REBOL-readable string"
//
//      return: [
//          ~[text! [<null> integer!]]~
//          "source string, and if truncated returns integer :LIMIT"
//
//          <null> "when input is void"
//      ]
//      value [<opt-out> element? splice!]
//      :flat "No indentation"
//      :limit "Limit to a certain length"
//          [integer!]
//  ]
//
DECLARE_NATIVE(MOLD)
{
    INCLUDE_PARAMS_OF_MOLD;

    Value* v = ARG(VALUE);

    DECLARE_MOLDER (mo);
    if (Bool_ARG(FLAT))
        SET_MOLD_FLAG(mo, MOLD_FLAG_INDENT);
    if (Bool_ARG(LIMIT)) {
        SET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT);
        mo->limit = Int32(ARG(LIMIT));
    }

    Push_Mold(mo);

    if (Is_Splice(v)) {
        SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
        bool form = false;
        Mold_Or_Form_Cell_Ignore_Quotes(mo, v, form);
    }
    else
        Mold_Element(mo, cast(Element*, v));

    Source* pack = Make_Source_Managed(2);
    Set_Flex_Len(pack, 2);

    Strand* popped = Pop_Molded_Strand(mo);  // sets MOLD_FLAG_TRUNCATED
    Liftify(Init_Text(Array_At(pack, 0), popped));

    if (mo->opts & MOLD_FLAG_WAS_TRUNCATED) {
        assert(Bool_ARG(LIMIT));
        Copy_Lifted_Cell(Array_At(pack, 1), ARG(LIMIT));
    }
    else
        Init_Lifted_Null(Array_At(pack, 1));

    return Init_Pack(OUT, pack);
}


//
//  write-stdout: native [
//
//  "Boot-only implementation of WRITE-STDOUT (HIJACK'd by STDIO module)"
//
//      return: []
//      value [<opt-out> text! char? blob!]
//          "Text to write, if a STRING! or CHAR! is converted to OS format"
//  ]
//
DECLARE_NATIVE(WRITE_STDOUT)
//
// This code isn't supposed to run during normal bootup.  But for debugging
// we don't want a parallel set of PRINT operations and specializations just
// on the off chance something goes wrong in boot.  So this stub is present
// to do debug I/O.
{
    INCLUDE_PARAMS_OF_WRITE_STDOUT;

    Value* v = ARG(VALUE);

  #if (! DEBUG_HAS_PROBE)
    UNUSED(v);
    panic (
        "Bootstrap WRITE-STDOUT needs DEBUG_HAS_PROBE or loaded I/O module"
    );
  #else
    if (Is_Text(v)) {
        printf("WRITE-STDOUT: %s\n", Strand_Utf8(Cell_Strand(v)));
        fflush(stdout);
    }
    else if (Is_Rune_And_Is_Char(v)) {
        Codepoint c = Rune_Known_Single_Codepoint(v);
        printf("WRITE-STDOUT: codepoint %d\n", cast(int, c));
    }
    else {
        assert(Is_Blob(v));
        PROBE(v);
    }
    return TRASH;
  #endif
}


//
//  new-line: native [
//
//  "Sets or clears the new-line marker within a block or group"
//
//      return: [block!]
//      position "Position to change marker (modified)"
//          [block! group!]
//      mark "Set YES for newline, NO for no newline"
//          [yesno?]
//      :all "Set or clear marker to end of series"
//      :skip "Set or clear marker periodically to the end of the series"
//          [integer!]
//  ]
//
DECLARE_NATIVE(NEW_LINE)
{
    INCLUDE_PARAMS_OF_NEW_LINE;

    bool mark = Cell_Yes(ARG(MARK));

    Value* pos = ARG(POSITION);
    const Element* tail;
    Element* item = List_At_Ensure_Mutable(&tail, pos);
    Source* a = Cell_Array_Known_Mutable(pos);  // need if setting flag at tail

    REBINT skip;
    if (Bool_ARG(ALL))
        skip = 1;
    else if (Bool_ARG(SKIP)) {
        skip = Int32s(ARG(SKIP), 1);
        if (skip < 1)
            skip = 1;
    }
    else
        skip = 0;

    REBLEN n;
    for (n = 0; true; ++n, ++item) {
        if (item == tail) {  // no cell at tail; use flag on array
            if (mark)
                Set_Source_Flag(a, NEWLINE_AT_TAIL);
            else
                Clear_Source_Flag(a, NEWLINE_AT_TAIL);
            break;
        }

        if (skip != 0 and (n % skip != 0))
            continue;

        if (mark)
            Set_Cell_Flag(item, NEWLINE_BEFORE);
        else
            Clear_Cell_Flag(item, NEWLINE_BEFORE);

        if (skip == 0)
            break;
    }

    return COPY(pos);
}


//
//  new-line?: native [
//
//  "Returns the state of the new-line marker within a block or group"
//
//      return: [logic?]
//      position "Position to check marker"
//          [block! group! varargs!]
//  ]
//
DECLARE_NATIVE(NEW_LINE_Q)
{
    INCLUDE_PARAMS_OF_NEW_LINE_Q;

    Value* pos = ARG(POSITION);

    const Source* arr;
    const Element* item;
    const Element* tail;

    if (Is_Varargs(pos)) {
        Level* L;
        Element* shared;
        if (Is_Level_Style_Varargs_May_Panic(&L, pos)) {
            if (Level_Is_Variadic(L)) {
                //
                // C va_args input to frame, as from the API, but not in the
                // process of using string components which *might* have
                // newlines.  Review edge cases, like:
                //
                //    Value* new_line_q = rebValue(":new-line?");
                //    bool case_one = rebUnboxLogic("new-line?", "[\n]");
                //    bool case_two = rebUnboxLogic(new_line_q, "[\n]");
                //
                return LOGIC(false);
            }

            arr = Level_Array(L);
            if (Is_Level_At_End(L)) {
                item = nullptr;
                tail = nullptr;
            }
            else {
                item = At_Feed(L->feed);
                tail = At_Feed(L->feed) + 1;  // !!! Review
            }
        }
        else if (Is_Block_Style_Varargs(&shared, pos)) {
            arr = Cell_Array(shared);
            item = List_At(&tail, shared);
        }
        else
            crash ("Bad VARARGS!");
    }
    else {
        assert(Is_Group(pos) or Is_Block(pos));
        arr = Cell_Array(pos);
        item = List_At(&tail, pos);
    }

    if (item != tail)
        return LOGIC(Get_Cell_Flag(item, NEWLINE_BEFORE));

    return LOGIC(Get_Source_Flag(arr, NEWLINE_AT_TAIL));
}


//
//  Milliseconds_From_Value: C
//
// Note that this routine is used by the SLEEP extension, as well as by WAIT.
//
REBLEN Milliseconds_From_Value(const Value* v) {
    REBINT msec;

    switch (opt Type_Of(v)) {
    case TYPE_INTEGER:
        msec = 1000 * Int32(v);
        break;

    case TYPE_DECIMAL:
        msec = cast(REBINT, 1000 * VAL_DECIMAL(v));
        break;

    case TYPE_TIME:
        msec = cast(REBINT, VAL_NANO(v) / (SEC_SEC / 1000));
        break;

    default:
        crash (NULL); // avoid uninitialized msec warning
    }

    if (msec < 0)
        panic (Error_Out_Of_Range(v));

    return msec;
}


#if TO_WASI
    #include <stdio.h>
    #include <errno.h>
#endif

//
//  basic-read: native [
//
//  "Very simplistic function for reading files, provided for WASI"
//
//       return: [blob!]
//       file [file!]
//  ]
//
DECLARE_NATIVE(BASIC_READ)
//
// !!! The filesystem support in Ren-C is based on libuv, and if you try and
// build the Posix implementation of libuv on WASI a lot is missing.  It's not
// clear that libuv will ever try to provide a specific WASI target--instead
// WASI appears to be targeting a lower common denominator of basic C stdio.
//
// It might be a good idea to have an alternative "basic filesystem" extension
// which just does things like dull whole-file reads and writes.  But as a
// near-term proof of concept, this gives a BASIC-READ routine to WASI.
{
    INCLUDE_PARAMS_OF_BASIC_READ;

  #if (! TO_WASI)
    UNUSED(ARG(FILE));
    panic ("BASIC-READ is a simple demo used in WASI only");
  #else
    const Strand* filename = Cell_Strand(ARG(FILE));
    FILE* f = fopen(Strand_Utf8(filename), "rb");
    if (f == nullptr)
        panic (rebError_OS(errno));
    fseek(f, 0, SEEK_END);
    Size size = ftell(f);
    fseek(f, 0, SEEK_SET);

    Binary* buf = Make_Binary(size);
    fread(Binary_Head(buf), size, 1, f);
    Term_Binary_Len(buf, size);
    fclose(f);

    return Init_Blob(OUT, buf);
  #endif
}


//
//  basic-write: native [
//
//  "Very simplistic function for writing files, provided for WASI"
//
//       return: []
//       file [file!]
//       data [blob! text!]
//  ]
//
DECLARE_NATIVE(BASIC_WRITE)
//
// !!! See remarks on BASIC-READ.
{
    INCLUDE_PARAMS_OF_BASIC_WRITE;

  #if (! TO_WASI)
    UNUSED(ARG(FILE));
    UNUSED(ARG(DATA));
    panic ("BASIC-WRITE is a simple demo used in WASI only");
  #else
    const Strand* filename = Cell_Strand(ARG(FILE));
    FILE* f = fopen(Strand_Utf8(filename), "wb");
    if (f == nullptr)
        panic (rebError_OS(errno));

    Size size;
    const Byte* data = Cell_Bytes_At(&size, ARG(DATA));
    fwrite(data, size, 1, f);
    fclose(f);

    return TRASH;
  #endif
}
