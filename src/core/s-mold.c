//
//  File: %s-mold.c
//  Summary: "value to string conversion"
//  Section: strings
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// "Molding" is a term in Rebol for getting a string representation of a
// value that is intended to be LOADed back into the system.  So if you mold
// a TEXT!, you would get back another TEXT! that would include the delimiters
// for that string (and any required escaping, e.g. for embedded quotes).
//
// "Forming" is the term for creating a string representation of a value that
// is intended for print output.  So if you were to form a TEXT!, it would
// *not* add delimiters or escaping--just giving the string back as-is.
//
// There are several technical problems in molding regarding the handling of
// values that do not have natural expressions in Rebol source.  For instance,
// it was legal (in Rebol2) to `make word! "123"` but that can't be molded as
// 123 because that would LOAD as an integer.  There are additional problems
// with `mold next [a b c]`, because there is no natural representation for a
// series that is not at its head.  These problems were addressed with
// "construction syntax", e.g. #[word! "123"] or #[block! [a b c] 1].  But
// to get this behavior MOLD/ALL had to be used, and it was implemented in
// something of an ad-hoc way.
//
// !!! These are some fuzzy concepts, and though the name MOLD may have made
// sense when Rebol was supposedly called "Clay", it now looks off-putting.
// Most of Ren-C's focus has been on the evaluator, and few philosophical
// problems of R3-Alpha's mold have been addressed.  However, the mechanical
// side has changed to use UTF-8 (instead of UCS-2) and allow nested molds.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Because molding and forming of a type share a lot of code, they are
//   implemented in "(M)old or (F)orm" hooks (MF_Xxx).  Also, since classes
//   of types can share behavior, several types are sometimes handled in the
//   same hook.  See %types.r for these categorizations in the "mold" column.
//
// * Molding is done via a REB_MOLD structure, which in addition to the
//   series to mold into contains options for the mold--including length
//   limits, whether commas or periods should be used for decimal points,
//   indentation rules, etc.
//
// * If you use the Push_Mold() function to fill a REB_MOLD, then it will
//   append in a stacklike way to the thread-local "mold buffer".  This
//   allows new molds to start running and use that buffer while another is in
//   progress, so long as it pops or drops the buffer before returning to the
//   code doing the higher level mold.
//
// * It's hard to know in advance how long molded output will be.  Using the
//   mold buffer allows one to use a "hot" preallocated UTF-8 buffer for the
//   mold...and copy out a series of the precise width and length needed.
//   (That is, if copying out the result is needed at all.)
//

#include "sys-core.h"


//
//  Prep_Mold_Overestimated: C
//
// A premise of the mold buffer is that it is reused and generally bigger than
// your output, so you won't expand it often.  Routines like Append_Ascii() or
// Append_Spelling() will automatically handle resizing, but other code which
// wishes to write bytes into the mold buffer must ensure adequate space has
// been allocated before doing so.
//
// This routine locates places in the code that want to minimize expansions in
// mid-mold by announcing a possibly overestimated byte count of what space
// will be needed.  Guesses tend to involve some multiplication of codepoint
// counts by 4, since that's the largest a UTF-8 character can encode as.
//
// !!! How often these guesses are worth it should be reviewed.  Alternate
// techniques might use an invalid UTF-8 character as an end-of-buffer signal
// and notice it during writes, how END markers are used by the data stack.
//
Byte* Prep_Mold_Overestimated(REB_MOLD *mo, REBLEN num_bytes)
{
    REBLEN tail = String_Len(mo->series);
    Expand_Series_Tail(mo->series, num_bytes);  // terminates at guess
    return Binary_At(mo->series, tail);
}


//
//  Pre_Mold_Core: C
//
// Emit the initial datatype function, depending on /ALL option
//
void Pre_Mold_Core(REB_MOLD *mo, NoQuote(const Cell*) v, bool all)
{
    if (all)
        Append_Ascii(mo->series, "#[");
    else
        Append_Ascii(mo->series, "make ");

    const String* type_name = Canon_Symbol(SYM_FROM_KIND(Cell_Heart(v)));
    Append_Spelling(mo->series, type_name);
    Append_Codepoint(mo->series, '!');  // !!! `make object!` not `make object`

    Append_Codepoint(mo->series, ' ');
}


//
//  End_Mold_Core: C
//
// Finish the mold, depending on /ALL with close block.
//
void End_Mold_Core(REB_MOLD *mo, bool all)
{
    if (all)
        Append_Codepoint(mo->series, ']');
}


//
//  Post_Mold: C
//
// For series that has an index, add the index for mold/all.
// Add closing block.
//
void Post_Mold(REB_MOLD *mo, NoQuote(const Cell*) v)
{
    if (VAL_INDEX(v)) {
        Append_Codepoint(mo->series, ' ');
        Append_Int(mo->series, VAL_INDEX(v) + 1);
    }
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL))
        Append_Codepoint(mo->series, ']');
}


//
//  New_Indented_Line: C
//
// Create a newline with auto-indent on next line if needed.
//
void New_Indented_Line(REB_MOLD *mo)
{
    // Check output string has content already but no terminator:
    //
    Byte* bp;
    if (String_Len(mo->series) == 0)
        bp = nullptr;
    else {
        bp = Binary_Last(mo->series);  // legal way to check UTF-8
        if (*bp == ' ' or *bp == '\t')
            *bp = '\n';
        else
            bp = nullptr;
    }

    // Add terminator:
    if (bp == nullptr)
        Append_Codepoint(mo->series, '\n');

    // Add proper indentation:
    if (NOT_MOLD_FLAG(mo, MOLD_FLAG_INDENT)) {
        REBINT n;
        for (n = 0; n < mo->indent; n++)
            Append_Ascii(mo->series, "    ");
    }
}


//=//// DEALING WITH CYCLICAL MOLDS ///////////////////////////////////////=//
//
// While Rebol has never had a particularly coherent story about how cyclical
// data structures will be handled in evaluation, they do occur--and the GC
// is robust to their existence.  These helper functions can be used to
// maintain a stack of series.
//
// !!! TBD: Unify this with the Push_GC_Guard and Drop_GC_Guard implementation
// so that improvements in one will improve the other?
//
//=////////////////////////////////////////////////////////////////////////=//

//
//  Find_Pointer_In_Series: C
//
REBINT Find_Pointer_In_Series(Series* s, const void *p)
{
    REBLEN index = 0;
    for (; index < Series_Used(s); ++index) {
        if (*Series_At(void*, s, index) == p)
            return index;
    }
    return NOT_FOUND;
}

//
//  Push_Pointer_To_Series: C
//
void Push_Pointer_To_Series(Series* s, const void *p)
{
    if (Is_Series_Full(s))
        Extend_Series_If_Necessary(s, 8);
    *Series_At(const void*, s, Series_Used(s)) = p;
    Set_Series_Used(s, Series_Used(s) + 1);
}

//
//  Drop_Pointer_From_Series: C
//
void Drop_Pointer_From_Series(Series* s, const void *p)
{
    assert(p == *Series_At(void*, s, Series_Used(s) - 1));
    UNUSED(p);
    Set_Series_Used(s, Series_Used(s) - 1);

    // !!! Could optimize so mold stack is always dynamic, and just use
    // s->content.dynamic.len--
}


//=/// ARRAY MOLDING //////////////////////////////////////////////////////=//

//
//  Mold_Array_At: C
//
void Mold_Array_At(
    REB_MOLD *mo,
    const Array* a,
    REBLEN index,
    const char *sep
){
    // Recursion check:
    if (Find_Pointer_In_Series(g_mold.stack, a) != NOT_FOUND) {
        if (sep[0] != '\0')
            Append_Codepoint(mo->series, sep[0]);
        Append_Ascii(mo->series, "...");
        if (sep[1] != '\0')
            Append_Codepoint(mo->series, sep[1]);
        return;
    }

    Push_Pointer_To_Series(g_mold.stack, a);

    bool indented = false;

    if (sep[0] != '\0')
        Append_Codepoint(mo->series, sep[0]);

    bool first_item = true;

    const Cell* item_tail = Array_Tail(a);
    const Cell* item = Array_At(a, index);
    assert(item <= item_tail);
    while (item != item_tail) {
        if (Get_Cell_Flag(item, NEWLINE_BEFORE)) {
           if (not indented and (sep[1] != '\0')) {
                ++mo->indent;
                indented = true;
            }

            // If doing a MOLD SPREAD then a leading newline should not be
            // added, e.g. `mold spread new-line [a b] true` should not give
            // a newline at the start.
            //
            if (sep[1] != '\0' or not first_item)
                New_Indented_Line(mo);
        }

        first_item = false;

        Mold_Value(mo, item);

        ++item;
        if (item == item_tail)
            break;

        if (Not_Cell_Flag(item, NEWLINE_BEFORE))
            Append_Codepoint(mo->series, ' ');
    }

    if (indented)
        --mo->indent;

    if (sep[1] != '\0') {
        if (Has_Newline_At_Tail(a))  // accommodates varlists, etc. for PROBE
            New_Indented_Line(mo); // but not any indentation from *this* mold
        Append_Codepoint(mo->series, sep[1]);
    }

    Drop_Pointer_From_Series(g_mold.stack, a);
}


//
//  Form_Array_At: C
//
void Form_Array_At(
    REB_MOLD *mo,
    const Array* array,
    REBLEN index,
    Option(Context*) context
){
    // Form a series (part_mold means mold non-string values):
    REBINT len = Array_Len(array) - index;
    if (len < 0)
        len = 0;

    REBINT n;
    for (n = 0; n < len;) {
        const Cell* item = Array_At(array, index + n);
        Option(Value(*)) wval = nullptr;
        if (context and (Is_Word(item) or Is_Get_Word(item))) {
            wval = Select_Symbol_In_Context(
                CTX_ARCHETYPE(unwrap(context)),
                Cell_Word_Symbol(item)
            );
            if (wval)
                item = unwrap(wval);
        }
        Mold_Or_Form_Value(mo, item, wval == nullptr);
        n++;
        if (GET_MOLD_FLAG(mo, MOLD_FLAG_LINES)) {
            Append_Codepoint(mo->series, LF);
        }
        else {  // Add a space if needed
            if (
                n < len
                and String_Len(mo->series) != 0
                and *Binary_Last(mo->series) != LF
                and NOT_MOLD_FLAG(mo, MOLD_FLAG_TIGHT)
            ){
                Append_Codepoint(mo->series, ' ');
            }
        }
    }
}


//
//  MF_Fail: C
//
void MF_Fail(REB_MOLD *mo, NoQuote(const Cell*) v, bool form)
{
    UNUSED(form);
    UNUSED(mo);

  #if defined(NDEBUG)
    UNUSED(v);
    fail ("Cannot MOLD or FORM datatype.");
  #else
    panic(v);
  #endif
}


//
//  MF_Unhooked: C
//
void MF_Unhooked(REB_MOLD *mo, NoQuote(const Cell*) v, bool form)
{
    UNUSED(mo);
    UNUSED(form);

    const REBVAL *type = Datatype_From_Kind(Cell_Heart(v));
    UNUSED(type); // !!! put in error message?

    fail ("Datatype does not have extension with a MOLD handler registered");
}


//
//  Mold_Or_Form_Cell: C
//
// Variation which molds a cell, e.g. no quoting is considered.
//
void Mold_Or_Form_Cell(
    REB_MOLD *mo,
    NoQuote(const Cell*) cell,
    bool form
){
    String* s = mo->series;
    Assert_Series_Term_If_Needed(s);

    if (C_STACK_OVERFLOWING(&s))
        Fail_Stack_Overflow();

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT)) {
        //
        // It's hard to detect the exact moment of tripping over the length
        // limit unless all code paths that add to the mold buffer (e.g.
        // tacking on delimiters etc.) check the limit.  The easier thing
        // to do is check at the end and truncate.  This adds a lot of data
        // wastefully, so short circuit here in the release build.  (Have
        // the debug build keep going to exercise mold on the data.)
        //
      #ifdef NDEBUG
        if (String_Len(s) >= mo->limit)
            return;
      #endif
    }

    MOLD_HOOK *hook = Mold_Or_Form_Hook_For_Type_Of(cell);
    hook(mo, cell, form);

    Assert_Series_Term_If_Needed(s);
}


//
//  Mold_Or_Form_Value: C
//
// Mold or form any value to string series tail.
//
void Mold_Or_Form_Value(REB_MOLD *mo, const Cell* v, bool form)
{
    // Mold hooks take a noquote cell and not a Cell*, so they expect any
    // quotes applied to have already been done.

  #if DEBUG_UNREADABLE_CELLS
    if (Is_Unreadable_Debug(v)) {  // would assert otherwise
        Append_Ascii(mo->series, "\\\\unreadable\\\\");
        return;
    }
  #endif

    if (Is_Antiform(v))
        fail (Error_Bad_Antiform(v));

    REBLEN depth = Cell_Num_Quotes(v);

    REBLEN i;
    for (i = 0; i < depth; ++i)
        Append_Ascii(mo->series, "'");

    if (QUOTE_BYTE(v) & NONQUASI_BIT)
        Mold_Or_Form_Cell(mo, v, form);
    else {
        Append_Codepoint(mo->series, '~');
        if (HEART_BYTE(v) != REB_VOID) {
            Mold_Or_Form_Cell(mo, v, form);
            Append_Codepoint(mo->series, '~');
        }
    }
}


//
//  Copy_Mold_Or_Form_Value: C
//
// Form a value based on the mold opts provided.
//
String* Copy_Mold_Or_Form_Value(const Cell* v, Flags opts, bool form)
{
    DECLARE_MOLD (mo);
    mo->opts = opts;

    Push_Mold(mo);
    Mold_Or_Form_Value(mo, v, form);
    return Pop_Molded_String(mo);
}


//
//  Copy_Mold_Or_Form_Value: C
//
// Form a value based on the mold opts provided.
//
String* Copy_Mold_Or_Form_Cell(NoQuote(const Cell*) cell, Flags opts, bool form)
{
    DECLARE_MOLD (mo);
    mo->opts = opts;

    Push_Mold(mo);
    Mold_Or_Form_Cell(mo, cell, form);
    return Pop_Molded_String(mo);
}


//
//  Push_Mold: C
//
// Much like the data stack, a single contiguous series is used for the mold
// buffer.  So if a mold needs to happen during another mold, it is pushed
// into a stack and must balance (with either a Pop() or Drop() of the nested
// string).  The fail() mechanics will automatically balance the stack.
//
void Push_Mold(REB_MOLD *mo)
{
  #if !defined(NDEBUG)
    assert(not g_mold.currently_pushing);  // Can't mold during Push_Mold()
    g_mold.currently_pushing = true;
  #endif

    assert(mo->series == nullptr);  // Indicates not pushed, see DECLARE_MOLD

    String* s = g_mold.buffer;
    assert(LINK(Bookmarks, s) == nullptr);  // should never bookmark buffer

    Assert_Series_Term_If_Needed(s);

    mo->series = s;
    mo->base.size = String_Size(s);
    mo->base.index = String_Len(s);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT))
        assert(mo->limit != 0);  // !!! Should a limit of 0 be allowed?

    if (
        GET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE)
        and Series_Rest(s) < mo->reserve
    ){
        // Expand will add to the series length, so we set it back.
        //
        // !!! Should reserve actually leave the length expanded?  Some cases
        // definitely don't want this, others do.  The protocol most
        // compatible with the appending mold is to come back with an
        // empty buffer after a push.
        //
        Expand_Series(s, mo->base.size, mo->reserve);
        Set_Series_Used(s, mo->base.size);
    }
    else if (Series_Rest(s) - Series_Used(s) > MAX_COMMON) {
        //
        // If the "extra" space in the series has gotten to be excessive (due
        // to some particularly large mold), back off the space.  But preserve
        // the contents, as there may be important mold data behind the
        // ->start index in the stack!
        //
        Length len = String_Len(g_mold.buffer);
        Remake_Series(
            s,
            Series_Used(s) + MIN_COMMON,
            NODE_FLAG_NODE // NODE_FLAG_NODE means preserve the data
        );
        Term_String_Len_Size(mo->series, len, Series_Used(s));
    }

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL))
        mo->digits = MAX_DIGITS;
    else {
        // If there is no notification when the option is changed, this
        // must be retrieved each time.
        //
        // !!! It may be necessary to mold out values before the options
        // block is loaded, and this 'Get_System_Int' is a bottleneck which
        // crashes that in early debugging.  BOOT_ERRORS is sufficient.
        //
        if (PG_Boot_Phase >= BOOT_ERRORS) {
            REBINT idigits = Get_System_Int(
                SYS_OPTIONS, OPTIONS_DECIMAL_DIGITS, MAX_DIGITS
            );
            if (idigits < 0)
                mo->digits = 0;
            else if (idigits > MAX_DIGITS)
                mo->digits = cast(REBLEN, idigits);
            else
                mo->digits = MAX_DIGITS;
        }
        else
            mo->digits = MAX_DIGITS;
    }

  #if !defined(NDEBUG)
    g_mold.currently_pushing = false;
  #endif
}


//
//  Throttle_Mold: C
//
// Contain a mold's series to its limit (if it has one).
//
void Throttle_Mold(REB_MOLD *mo) {
    if (NOT_MOLD_FLAG(mo, MOLD_FLAG_LIMIT))
        return;

    if (String_Len(mo->series) - mo->base.index > mo->limit) {
        REBINT overage = (String_Len(mo->series) - mo->base.index) - mo->limit;

        // Mold buffer is UTF-8...length limit is (currently) in characters,
        // not bytes.  Have to back up the right number of bytes, but also
        // adjust the character length appropriately.

        Utf8(*) tail = String_Tail(mo->series);
        Codepoint dummy;
        Utf8(*) cp = Utf8_Skip(&dummy, tail, -(overage));

        Term_String_Len_Size(
            mo->series,
            String_Len(mo->series) - overage,
            String_Size(mo->series) - (tail - cp)
        );

        assert(not (mo->opts & MOLD_FLAG_WAS_TRUNCATED));
        mo->opts |= MOLD_FLAG_WAS_TRUNCATED;
    }
}


//
//  Pop_Molded_String_Core: C
//
String* Pop_Molded_String_Core(String* buf, Size offset, Index index)
{
    Size size = String_Size(buf) - offset;
    Length len = String_Len(buf) - index;

    String* popped = Make_String(size);
    memcpy(Binary_Head(popped), Binary_At(buf, offset), size);
    Term_String_Len_Size(popped, len, size);

    // Though the protocol of Mold_Value does terminate, it only does so if
    // it adds content to the buffer.  If we did not terminate when we
    // reset the size, then these no-op molds (e.g. mold of "") would leave
    // whatever value in the terminator spot was there.  This could be
    // addressed by making no-op molds terminate.
    //
    Term_String_Len_Size(buf, index, offset);

    return popped;
}


//
//  Pop_Molded_String: C
//
// When a Push_Mold is started, then string data for the mold is accumulated
// at the tail of the task-global UTF-8 buffer.  It's possible to copy this
// data directly into a target prior to calling Drop_Mold()...but this routine
// is a helper that extracts the data as a string series.  It resets the
// buffer to its length at the time when the last push began.
//
String* Pop_Molded_String(REB_MOLD *mo)
{
    assert(mo->series != nullptr);  // if null, there was no Push_Mold()
    Assert_Series_Term_If_Needed(mo->series);

    // Limit string output to a specified size to prevent long console
    // garbage output if MOLD_FLAG_LIMIT was set in Push_Mold().
    //
    Throttle_Mold(mo);

    String* popped = Pop_Molded_String_Core(
        mo->series,
        mo->base.size,
        mo->base.index
    );

    mo->series = nullptr;  // indicates mold is not currently pushed
    return popped;
}


//
//  Pop_Molded_Binary: C
//
// !!! This particular use of the mold buffer might undermine tricks which
// could be used with invalid UTF-8 bytes--for instance.  Review.
//
Binary* Pop_Molded_Binary(REB_MOLD *mo)
{
    assert(String_Len(mo->series) >= mo->base.size);

    Assert_Series_Term_If_Needed(mo->series);
    Throttle_Mold(mo);

    Size size = String_Size(mo->series) - mo->base.size;
    Binary* bin = Make_Binary(size);
    memcpy(Binary_Head(bin), Binary_At(mo->series, mo->base.size), size);
    Term_Binary_Len(bin, size);

    // Though the protocol of Mold_Value does terminate, it only does so if
    // it adds content to the buffer.  If we did not terminate when we
    // reset the size, then these no-op molds (e.g. mold of "") would leave
    // whatever value in the terminator spot was there.  This could be
    // addressed by making no-op molds terminate.
    //
    Term_String_Len_Size(mo->series, mo->base.index, mo->base.size);

    mo->series = nullptr;  // indicates mold is not currently pushed
    return bin;
}


//
//  Drop_Mold_Core: C
//
// When generating a molded string, sometimes it's enough to have access to
// the molded data without actually creating a new series out of it.  If the
// information in the mold has done its job and Pop_Molded_String() is not
// required, just call this to drop back to the state of the last push.
//
// Note: Direct pointers into the mold buffer are unstable if another mold
// runs during it!  Do not pass these pointers into code that can run an
// additional mold (that can be just about anything, even debug output...)
//
void Drop_Mold_Core(
    REB_MOLD *mo,
    bool not_pushed_ok  // see Drop_Mold_If_Pushed()
){
    if (mo->series == nullptr) {  // there was no Push_Mold()
        assert(not_pushed_ok);
        UNUSED(not_pushed_ok);
        return;
    }

    // When pushed data are to be discarded, mo->series may be unterminated.
    // (Indeed that happens when Scan_Item_Push_Mold returns NULL/0.)
    //
    Note_Series_Maybe_Term(mo->series);

    // see notes in Pop_Molded_String()
    //
    Term_String_Len_Size(mo->series, mo->base.index, mo->base.size);

    mo->series = nullptr;  // indicates mold is not currently pushed
}


//
//  Startup_Mold: C
//
void Startup_Mold(REBLEN size)
{
    g_mold.stack = Make_Series_Core(10, FLAG_FLAVOR(MOLDSTACK));

    ensure(nullptr, g_mold.buffer) = Make_String_Core(size, SERIES_FLAG_DYNAMIC);
}


//
//  Shutdown_Mold: C
//
void Shutdown_Mold(void)
{
    assert(LINK(Bookmarks, g_mold.buffer) == nullptr);  // should not be set
    Free_Unmanaged_Series(g_mold.buffer);
    g_mold.buffer = nullptr;

    Free_Unmanaged_Series(g_mold.stack);
    g_mold.stack = nullptr;
}
