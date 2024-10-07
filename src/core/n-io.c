//
//  File: %n-io.c
//  Summary: "native functions for input and output"
//  Section: natives
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
//  form: native [
//
//  "Converts a value to a human-readable string"
//
//      return: "Returns null if input is void"
//          [~null~ text!]
//      value "The value to form (currently errors on antiforms)"
//          [<maybe> element?]
//  ]
//
DECLARE_NATIVE(form)
{
    INCLUDE_PARAMS_OF_FORM;

    Element* elem = cast(Element*, ARG(value));

    return Init_Text(OUT, Copy_Form_Element(elem, 0));
}


//
//  mold: native [
//
//  "Converts value to a REBOL-readable string"
//
//      return: "null if input is void, if truncated returns integer /LIMIT "
//          [~null~ ~[text! [~null~ integer!]]~]
//      value [<maybe> element? splice?]
//      :all "Use construction syntax"
//      :flat "No indentation"
//      :limit "Limit to a certain length"
//          [integer!]
//  ]
//
DECLARE_NATIVE(mold)
{
    INCLUDE_PARAMS_OF_MOLD;

    Value* v = ARG(value);

    DECLARE_MOLD (mo);
    if (REF(all))
        SET_MOLD_FLAG(mo, MOLD_FLAG_ALL);
    if (REF(flat))
        SET_MOLD_FLAG(mo, MOLD_FLAG_INDENT);
    if (REF(limit)) {
        SET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT);
        mo->limit = Int32(ARG(limit));
    }

    Push_Mold(mo);

    if (Is_Splice(v)) {
        SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
        bool form = false;
        Mold_Or_Form_Cell_Ignore_Quotes(mo, v, form);
    }
    else
        Mold_Element(mo, cast(Element*, v));

    Array* pack = Make_Array_Core(2, NODE_FLAG_MANAGED);
    Set_Flex_Len(pack, 2);

    String* popped = Pop_Molded_String(mo);  // sets MOLD_FLAG_TRUNCATED
    Meta_Quotify(Init_Text(Array_At(pack, 0), popped));

    if (mo->opts & MOLD_FLAG_WAS_TRUNCATED) {
        assert(REF(limit));
        Copy_Meta_Cell(Array_At(pack, 1), ARG(limit));
    }
    else
        Init_Meta_Of_Null(Array_At(pack, 1));

    return Init_Pack(OUT, pack);
}


//
//  write-stdout: native [
//
//  "Boot-only implementation of WRITE-STDOUT (HIJACK'd by STDIO module)"
//
//      return: [~]
//      value [<maybe> text! char? binary!]
//          "Text to write, if a STRING! or CHAR! is converted to OS format"
//  ]
//
DECLARE_NATIVE(write_stdout)
//
// This code isn't supposed to run during normal bootup.  But for debugging
// we don't want a parallel set of PRINT operations and specializations just
// on the off chance something goes wrong in boot.  So this stub is present
// to do debug I/O.
{
    INCLUDE_PARAMS_OF_WRITE_STDOUT;

    Value* v = ARG(value);

  #if !DEBUG_HAS_PROBE
    UNUSED(v);
    fail ("Boot WRITE-STDOUT needs DEBUG_HAS_PROBE or loaded I/O module");
  #else
    if (Is_Text(v)) {
        printf("WRITE-STDOUT: %s\n", c_cast(char*, String_Head(Cell_String(v))));
        fflush(stdout);
    }
    else if (IS_CHAR(v)) {
        printf("WRITE-STDOUT: char %lu\n", cast(unsigned long, Cell_Codepoint(v)));
    }
    else {
        assert(Is_Binary(v));
        PROBE(v);
    }
    return NOTHING;
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
DECLARE_NATIVE(new_line)
{
    INCLUDE_PARAMS_OF_NEW_LINE;

    bool mark = Cell_Yes(ARG(mark));

    Value* pos = ARG(position);
    const Element* tail;
    Element* item = Cell_List_At_Ensure_Mutable(&tail, pos);
    Array* a = Cell_Array_Known_Mutable(pos);  // need if setting flag at tail

    REBINT skip;
    if (REF(all))
        skip = 1;
    else if (REF(skip)) {
        skip = Int32s(ARG(skip), 1);
        if (skip < 1)
            skip = 1;
    }
    else
        skip = 0;

    REBLEN n;
    for (n = 0; true; ++n, ++item) {
        if (item == tail) {  // no cell at tail; use flag on array
            if (mark)
                Set_Array_Flag(a, NEWLINE_AT_TAIL);
            else
                Clear_Array_Flag(a, NEWLINE_AT_TAIL);
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
DECLARE_NATIVE(new_line_q)
{
    INCLUDE_PARAMS_OF_NEW_LINE_Q;

    Value* pos = ARG(position);

    const Array* arr;
    const Element* item;
    const Element* tail;

    if (Is_Varargs(pos)) {
        Level* L;
        Element* shared;
        if (Is_Level_Style_Varargs_May_Fail(&L, pos)) {
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
                return Init_Logic(OUT, false);
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
            item = Cell_List_At(&tail, shared);
        }
        else
            panic ("Bad VARARGS!");
    }
    else {
        assert(Is_Group(pos) or Is_Block(pos));
        arr = Cell_Array(pos);
        item = Cell_List_At(&tail, pos);
    }

    if (item != tail)
        return Init_Logic(OUT, Get_Cell_Flag(item, NEWLINE_BEFORE));

    return Init_Logic(OUT, Get_Array_Flag(arr, NEWLINE_AT_TAIL));
}


//
//  Milliseconds_From_Value: C
//
// Note that this routine is used by the SLEEP extension, as well as by WAIT.
//
REBLEN Milliseconds_From_Value(const Value* v) {
    REBINT msec;

    switch (VAL_TYPE(v)) {
    case REB_INTEGER:
        msec = 1000 * Int32(v);
        break;

    case REB_DECIMAL:
        msec = cast(REBINT, 1000 * VAL_DECIMAL(v));
        break;

    case REB_TIME:
        msec = cast(REBINT, VAL_NANO(v) / (SEC_SEC / 1000));
        break;

    default:
        panic (NULL); // avoid uninitialized msec warning
    }

    if (msec < 0)
        fail (Error_Out_Of_Range(v));

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
//       return: [binary!]
//       file [file!]
//  ]
//
DECLARE_NATIVE(basic_read)
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

  #if !TO_WASI
    UNUSED(ARG(file));
    fail ("BASIC-READ is a simple demo used in WASI only");
  #else
    const String* filename = Cell_String(ARG(file));
    FILE* f = fopen(String_UTF8(filename), "rb");
    if (f == nullptr)
        fail (rebError_OS(errno));
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
//       return: [~]
//       file [file!]
//       data [binary! text!]
//  ]
//
DECLARE_NATIVE(basic_write)
//
// !!! See remarks on BASIC-READ.
{
    INCLUDE_PARAMS_OF_BASIC_WRITE;

  #if !TO_WASI
    UNUSED(ARG(file));
    UNUSED(ARG(data));
    fail ("BASIC-WRITE is a simple demo used in WASI only");
  #else
    const String* filename = Cell_String(ARG(file));
    FILE* f = fopen(String_UTF8(filename), "wb");
    if (f == nullptr)
        fail (rebError_OS(errno));

    Size size;
    const Byte* data = Cell_Bytes_At(&size, ARG(data));
    fwrite(data, size, 1, f);
    fclose(f);

    return NOTHING;
  #endif
}
