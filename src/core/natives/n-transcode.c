//
//  file: %n-transcode.h
//  summary: "TRANSCODE native for exposing scanner functionality to usermode"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Ren-C's TRANSCODE has some properties distinguishing it from historical
// Rebol or Red:
//
// * Because TEXT! (strings) use UTF-8 throughout their lifetime as their
//   internal representation (vs. turning into fixed-sized codepoint arrays),
//   a TRANSCODE on a TEXT! is equally efficient to transcoding binary data.
//
// * When one element at a time is scanned with TRANSCODE:NEXT, it uses a
//   multi-return interface where the next position is the primary return
//   result, and the scanned value is the second return result.  When there
//   are no further items to scan it returns null.  Since null is not a
//   valid element to be scanned, this provides a thorough interface solving
//   some edge cases that can't be discerned in historical Redbol:
//
//     https://rebol.metaeducation.com/t/incomplete-transcodes/1940
//

#include "sys-core.h"

//
//  Transcode_One: C
//
// This is a generic helper that powers things like (to integer! "1020").
//
// For now we implement it inefficiently, but it should be done without
// needing to call a native.
//
Result(Element*) Transcode_One(
    Sink(Element) out,
    Option(Heart) heart,
    const Element* any_utf8
){
    assert(Any_Utf8(any_utf8));  // use rebQ(), as SIGIL!, WORD!, evaluative
    Value* result;
    RebolValue* warning = rebRescue2(
        &result,
        "transcode:one as text!", rebQ(any_utf8)
    );
    if (warning) {
        Error* error = Cell_Error(warning);
        rebRelease(warning);
        return fail (error);
    }
    if (heart and Heart_Of(result) != heart) {
        rebRelease(result);
        return fail ("Transcode_One() gave unwanted type");
    }
    Copy_Cell(out, Known_Element(result));
    rebRelease(result);
    return out;
}


//
//  transcode: native [
//
//  "Translates UTF-8 source (from a text or binary) to Rebol elements"
//
//      return: "Transcoded elements block, or ~[remainder element]~ if :NEXT"
//          [<null> block! ~[[text! blob!] element?]~ element?]
//      source "If BINARY!, must be UTF-8 encoded"
//          [any-utf8? blob!]
//      :next "Translate one element and give back next position"
//      :one "Transcode one element and return it"
//      :file "File to be associated with BLOCK!s and GROUP!s in source"
//          [file! url!]
//      :line "Line number for start of scan, word variable will be updated"
//          [integer! any-word?]
//      {buffer}
//  ]
//
DECLARE_NATIVE(TRANSCODE)
{
    INCLUDE_PARAMS_OF_TRANSCODE;

    Element* source = Element_ARG(SOURCE);

    Size size;
    const Byte* bp = Cell_Bytes_At(&size, source);

    enum {
        ST_TRANSCODE_INITIAL_ENTRY = STATE_0,
        ST_TRANSCODE_SCANNING,
        ST_TRANSCODE_ENSURE_NO_MORE
    };

    if (STATE != ST_TRANSCODE_INITIAL_ENTRY)
        goto not_initial_entry;

  initial_entry: {  //////////////////////////////////////////////////////////

  // 1. Though all BLOB! leave a spare byte at the end in case they are
  //    turned into a string, they are not terminated by default.  (Read about
  //    BINARY_BAD_UTF8_TAIL_BYTE for why; it helps reinforce the fact that
  //    binaries consider 0 a legal content value, while strings do not.)
  //
  //    Most of the time this is a good thing because it helps make sure that
  //    people are passing around the `size` correctly.  But R3-Alpha's
  //    scanner was not written to test against a limit...it looks for `\0`
  //    bytes, so all input must have it.
  //
  //    Hack around the problem by forcing termination on the binary (there
  //    is always room to do this, in case it becomes string-aliased.)
  //
  // 2. Originally, interning was used on the file to avoid redundancy.  But
  //    that meant the interning mechanic was being given strings that were
  //    not necessarily valid WORD! symbols.  There's probably not *that* much
  //    redundancy of files being scanned, and plain old freezing can keep the
  //    user from changing the passed in filename after-the-fact (making a
  //    copy would likely be wasteful, so let them copy if they care to change
  //    the string later).
  //
  //    !!! Should the base name and extension be stored, or whole path?

    if (Is_Blob(source))  // scanner needs data to end in '\0' [1]
        Term_Binary(m_cast(Binary*, Cell_Binary(source)));

    Option(const Strand*) file;
    if (Bool_ARG(FILE)) {
        file = Cell_Strand(ARG(FILE));
        Freeze_Flex(unwrap file);  // freezes vs. interning [2]
    }
    else
        file = ANONYMOUS;

    Sink(Value) scratch_line_number = SCRATCH;  // use as scratch space
    if (Any_Word(ARG(LINE))) {
        require (
          Get_Var(
            scratch_line_number,
            NO_STEPS,
            Element_ARG(LINE),
            SPECIFIED
        ));
        // null not allowed, must be integer
    }
    else if (Is_Nulled(ARG(LINE)))  // not provided
        Init_Integer(scratch_line_number, 1);
    else
        Copy_Cell(scratch_line_number, ARG(LINE));

    LineNumber start_line;
    if (Is_Integer(scratch_line_number)) {
        start_line = VAL_INT32(scratch_line_number);
        if (start_line <= 0)
            panic (PARAM(LINE));  // definitional?
    }
    else
        panic (":LINE must be INTEGER! or an ANY-WORD? integer variable");

    // Because we're building a frame, we can't make a {bp, END} packed array
    // and start up a variadic feed...because the stack variable would go
    // bad as soon as we yielded to the trampoline.  Have to use an END feed
    // and preload the transcode->at of the scanner here.
    //
    // Note: Could reuse global TG_End_Feed if context was null.

    require (
      Feed* feed = Make_Array_Feed_Core(g_empty_array, 0, SPECIFIED)
    );

    Flags flags =
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // query pending newline
        | FLAG_STATE_BYTE(ST_SCANNER_OUTERMOST_SCAN);

    if (Bool_ARG(NEXT) or Bool_ARG(ONE))
        flags |= SCAN_EXECUTOR_FLAG_JUST_ONCE;

    Binary* bin = Make_Binary(sizeof(TranscodeState));
    TranscodeState* transcode = cast(TranscodeState*, Binary_Head(bin));
    Init_Transcode(transcode, file, start_line, bp);
    Term_Binary_Len(bin, sizeof(TranscodeState));

    Init_Blob(LOCAL(BUFFER), bin);

    UNUSED(size);  // currently we don't use this information

    Level* sub = Make_Scan_Level(transcode, feed, flags);

    Push_Level_Erase_Out_If_State_0(OUT, sub);
    STATE = ST_TRANSCODE_SCANNING;
    return CONTINUE_SUBLEVEL(sub);

} not_initial_entry: { //////////////////////////////////////////////////////

    Element* transcode_buffer = Element_LOCAL(BUFFER);  // BLOB!, gets GC'd
    TranscodeState* transcode = cast(
        TranscodeState*,
        Binary_Head(Cell_Binary_Known_Mutable(transcode_buffer))
    );

    switch (STATE) {
      case ST_TRANSCODE_SCANNING:
        goto scan_to_stack_maybe_failed;

      case ST_TRANSCODE_ENSURE_NO_MORE:
        if (not Is_Error(OUT)) {  // !!! return this error, or new one?
            if (TOP_INDEX == STACK_BASE + 1) {  // didn't scan anything else
                Move_Cell(OUT, TOP_ELEMENT);
                DROP();
            }
            else {  // scanned another item, we only wanted one!
                assert(TOP_INDEX == STACK_BASE + 2);
                Drop_Data_Stack_To(STACK_BASE);
                Init_Warning(
                    OUT,
                    Error_User("TRANSCODE:ONE scanned more than one element")
                );
                Failify(OUT);
            }
        }
        Drop_Level(SUBLEVEL);
        return OUT;

      default:
        assert(false);
    }

  scan_to_stack_maybe_failed: {  /////////////////////////////////////////////

    // If the source data bytes are "1" then the scanner will push INTEGER! 1
    // if the source data is "[1]" then the scanner will push BLOCK! [1]
    //
    // Return a block of the results, so [1] and [[1]] in those cases.

    if (Is_Error(OUT)) {
        assert(TOP_INDEX == STACK_BASE);
        Drop_Level(SUBLEVEL);
        return OUT;
    }

    assert(Is_Void(OUT));  // scanner returns void if it doesn't return error

    if (Bool_ARG(ONE)) {  // want *exactly* one element
        if (TOP_INDEX == STACK_BASE)
            return fail ("Transcode was empty (or all comments)");
        assert(TOP_INDEX == STACK_BASE + 1);
        STATE = ST_TRANSCODE_ENSURE_NO_MORE;
        return CONTINUE_SUBLEVEL(SUBLEVEL);
    }

    if (Bool_ARG(LINE) and Is_Word(ARG(LINE))) {  // want line number updated
        Init_Integer(OUT, transcode->line);
        Copy_Cell(Level_Scratch(SUBLEVEL), Element_ARG(LINE));  // variable
        heeded (Corrupt_Cell_If_Needful(Level_Spare(SUBLEVEL)));

        require (
          Set_Var_In_Scratch_To_Out(SUBLEVEL, NO_STEPS)
        );
        UNUSED(*OUT);
    }

} process_stack_results_if_any: {

  // 1. If you're doing a plain TRANSCODE on content that turns out to be
  //    empty (or all comments and whitespace), then the result is not NULL,
  //    but an empty BLOCK!.  This makes TRY TRANSCODE more useful (as you
  //    know that if you get NULL there was an actual error), and it is more
  //    often than not the case that empty content evaluating to GHOST! is
  //    what you want (e.g. scripts that are empty besides a header are ok).

    if (Bool_ARG(NEXT)) {
        if (TOP_INDEX == STACK_BASE) {
            Init_Nulled(OUT);
        }
        else {
            assert(TOP_INDEX == STACK_BASE + 1);
            Move_Cell(OUT, TOP_ELEMENT);
            DROP();
        }
    }
    else {
        possibly(TOP_INDEX == STACK_BASE);  // transcode "" is [], not null [1]

        Source* a = Pop_Managed_Source_From_Stack(STACK_BASE);
        if (Get_Executor_Flag(SCAN, SUBLEVEL, NEWLINE_PENDING))
            Set_Source_Flag(a, NEWLINE_AT_TAIL);

        MISC_SOURCE_LINE(a) = transcode->line;
        Tweak_Link_Filename(a, opt transcode->file);

        Init_Block(OUT, a);
    }

    Drop_Level(SUBLEVEL);

    if (not Bool_ARG(NEXT)) {
        assert(Is_Block(Known_Element(OUT)));
        return OUT;  // single block result
    }

    if (Is_Light_Null(OUT))  // no more Elements were left to transcode
        return NULLED;  // must return pure null for THEN/ELSE to work right

} calculate_and_return_how_far_transcode_advanced: {

  // 1. The scanner does not currently keep track of how many codepoints it
  //    went past, it only advances bytes.  But if TEXT! input was given, we
  //    need to push it forward by a codepoint-based index to return how
  //    much it advanced.  Count characters by going backwards from the byte
  //    position of the finished scan until the byte we started at is found.
  //
  //    (It would probably be better if the scanner kept count, though maybe
  //    that would make it slower when this isn't needed often?)

    Sink(Element) spare_rest = SPARE;
    Copy_Cell(spare_rest, source);

    if (Is_Blob(source)) {
        const Binary* b = Cell_Binary(source);
        if (transcode->at)
            SERIES_INDEX_UNBOUNDED(spare_rest) = transcode->at - Binary_Head(b);
        else
            SERIES_INDEX_UNBOUNDED(spare_rest) = Binary_Len(b);
    }
    else {  // must count codepoints [1]
        assert(Is_Text(source));

        if (transcode->at)
            SERIES_INDEX_UNBOUNDED(spare_rest) += Num_Codepoints_For_Bytes(
                bp, transcode->at
            );
        else
            SERIES_INDEX_UNBOUNDED(spare_rest) += (
                Binary_Tail(Cell_Strand(source)) - bp
            );
    }

    Source* pack = Make_Source_Managed(2);
    Set_Flex_Len(pack, 2);  // PACK! of advanced input, and transcoded item

    Copy_Lifted_Cell(Array_At(pack, 0), spare_rest);
    Copy_Lifted_Cell(Array_At(pack, 1), OUT);

    return Init_Pack(OUT, pack);
}}}
