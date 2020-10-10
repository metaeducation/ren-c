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
REBYTE *Prep_Mold_Overestimated(REB_MOLD *mo, REBLEN num_bytes)
{
    REBLEN tail = STR_LEN(mo->series);
    EXPAND_SERIES_TAIL(SER(mo->series), num_bytes);  // terminates at guess
    return BIN_AT(SER(mo->series), tail);
}


//
//  Pre_Mold_Core: C
//
// Emit the initial datatype function, depending on /ALL option
//
void Pre_Mold_Core(REB_MOLD *mo, REBCEL(const*) v, bool all)
{
    if (all)
        Append_Ascii(mo->series, "#[");
    else
        Append_Ascii(mo->series, "make ");

    // If asked for the type name of a PARAM in a paramlist, VAL_TYPE()
    // will report an invalid value.  So use MIRROR_BYTE() so that TYPESET!
    // comes back as the answer.
    //
    const REBSTR *type_name = Canon(
        SYM_FROM_KIND(cast(enum Reb_Kind, MIRROR_BYTE(v)))
    );
    Append_Spelling(mo->series, type_name);

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
void Post_Mold(REB_MOLD *mo, REBCEL(const*) v)
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
    REBYTE *bp;
    if (STR_LEN(mo->series) == 0)
        bp = nullptr;
    else {
        bp = BIN_LAST(SER(mo->series));  // legal way to check UTF-8
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
// !!! TBD: Unify this with the PUSH_GC_GUARD and DROP_GC_GUARD implementation
// so that improvements in one will improve the other?
//
//=////////////////////////////////////////////////////////////////////////=//

//
//  Find_Pointer_In_Series: C
//
REBLEN Find_Pointer_In_Series(REBSER *s, const void *p)
{
    REBLEN index = 0;
    for (; index < SER_USED(s); ++index) {
        if (*SER_AT(void*, s, index) == p)
            return index;
    }
    return NOT_FOUND;
}

//
//  Push_Pointer_To_Series: C
//
void Push_Pointer_To_Series(REBSER *s, const void *p)
{
    if (SER_FULL(s))
        Extend_Series(s, 8);
    *SER_AT(const void*, s, SER_USED(s)) = p;
    SET_SERIES_USED(s, SER_USED(s) + 1);
}

//
//  Drop_Pointer_From_Series: C
//
void Drop_Pointer_From_Series(REBSER *s, const void *p)
{
    assert(p == *SER_AT(void*, s, SER_USED(s) - 1));
    UNUSED(p);
    SET_SERIES_USED(s, SER_USED(s) - 1);

    // !!! Could optimize so mold stack is always dynamic, and just use
    // s->content.dynamic.len--
}


//=/// ARRAY MOLDING //////////////////////////////////////////////////////=//

//
//  Mold_Array_At: C
//
void Mold_Array_At(
    REB_MOLD *mo,
    const REBARR *a,
    REBLEN index,
    const char *sep
){
    // Recursion check:
    if (Find_Pointer_In_Series(TG_Mold_Stack, a) != NOT_FOUND) {
        if (sep[0] != '\0')
            Append_Codepoint(mo->series, sep[0]);
        Append_Ascii(mo->series, "...");
        if (sep[1] != '\0')
            Append_Codepoint(mo->series, sep[1]);
        return;
    }

    Push_Pointer_To_Series(TG_Mold_Stack, a);

    bool indented = false;

    if (sep[0] != '\0')
        Append_Codepoint(mo->series, sep[0]);

    bool first_item = true;

    const RELVAL *item = ARR_AT(a, index);
    while (NOT_END(item)) {
        if (GET_CELL_FLAG(item, NEWLINE_BEFORE)) {
           if (not indented and (sep[1] != '\0')) {
                ++mo->indent;
                indented = true;
            }

            // If doing a MOLD/ONLY then a leading newline should not be
            // added, e.g. `mold/only new-line [a b] true` should not give
            // a newline at the start.
            //
            if (sep[1] != '\0' or not first_item)
                New_Indented_Line(mo);
        }

        first_item = false;

        Mold_Value(mo, item);

        ++item;
        if (IS_END(item))
            break;

        if (NOT_CELL_FLAG(item, NEWLINE_BEFORE))
            Append_Codepoint(mo->series, ' ');
    }

    if (indented)
        --mo->indent;

    if (sep[1] != '\0') {
        if (GET_ARRAY_FLAG(a, NEWLINE_AT_TAIL))
            New_Indented_Line(mo); // but not any indentation from *this* mold
        Append_Codepoint(mo->series, sep[1]);
    }

    Drop_Pointer_From_Series(TG_Mold_Stack, a);
}


//
//  Form_Array_At: C
//
void Form_Array_At(
    REB_MOLD *mo,
    const REBARR *array,
    REBLEN index,
    REBCTX *opt_context
){
    // Form a series (part_mold means mold non-string values):
    REBINT len = ARR_LEN(array) - index;
    if (len < 0)
        len = 0;

    REBINT n;
    for (n = 0; n < len;) {
        const RELVAL *item = ARR_AT(array, index + n);
        REBVAL *wval = nullptr;
        if (opt_context and (IS_WORD(item) or IS_GET_WORD(item))) {
            wval = Select_Canon_In_Context(opt_context, VAL_WORD_CANON(item));
            if (wval)
                item = wval;
        }
        Mold_Or_Form_Value(mo, item, wval == nullptr);
        n++;
        if (GET_MOLD_FLAG(mo, MOLD_FLAG_LINES)) {
            Append_Codepoint(mo->series, LF);
        }
        else {  // Add a space if needed
            if (
                n < len
                and STR_LEN(mo->series) != 0
                and *BIN_LAST(SER(mo->series)) != LF
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
void MF_Fail(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);

    if (CELL_KIND(v) == REB_0) {
        //
        // REB_0 is reserved for special purposes, and should only be molded
        // in debug scenarios.
        //
    #if defined(NDEBUG)
        UNUSED(mo);
        panic (v);
    #else
        printf("!!! Request to MOLD or FORM a REB_0 value !!!\n");
        Append_Ascii(mo->series, "!!!REB_0!!!");
        debug_break(); // don't crash if under a debugger, just "pause"
    #endif
    }

    fail ("Cannot MOLD or FORM datatype.");
}


//
//  MF_Unhooked: C
//
void MF_Unhooked(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(mo);
    UNUSED(form);

    const REBVAL *type = Datatype_From_Kind(CELL_KIND(v));
    UNUSED(type); // !!! put in error message?

    fail ("Datatype does not have extension with a MOLD handler registered");
}


//
//  Mold_Or_Form_Cell: C
//
// Variation which molds a cell, e.g. no quoting is considered.
//
void Mold_Or_Form_Cell(REB_MOLD *mo, REBCEL(const*) cell, bool form)
{
    REBSTR *s = mo->series;
    ASSERT_SERIES_TERM(SER(s));

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
        if (STR_LEN(s) >= mo->limit)
            return;
      #endif
    }

    MOLD_HOOK *hook = Mold_Or_Form_Hook_For_Type_Of(cell);
    hook(mo, cell, form);

    ASSERT_SERIES_TERM(SER(s));
}


//
//  Mold_Or_Form_Value: C
//
// Mold or form any value to string series tail.
//
void Mold_Or_Form_Value(REB_MOLD *mo, const RELVAL *v, bool form)
{
    // Mold hooks take a REBCEL* and not a RELVAL*, so they expect any quotes
    // applied to have already been done.

    REBLEN depth = VAL_NUM_QUOTES(v);

    REBLEN i;
    for (i = 0; i < depth; ++i)
        Append_Ascii(mo->series, "'");

    Mold_Or_Form_Cell(mo, VAL_UNESCAPED(v), form);
}


//
//  Copy_Mold_Or_Form_Value: C
//
// Form a value based on the mold opts provided.
//
REBSTR *Copy_Mold_Or_Form_Value(const RELVAL *v, REBFLGS opts, bool form)
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
REBSTR *Copy_Mold_Or_Form_Cell(REBCEL(const*) cell, REBFLGS opts, bool form)
{
    DECLARE_MOLD (mo);
    mo->opts = opts;

    Push_Mold(mo);
    Mold_Or_Form_Cell(mo, cell, form);
    return Pop_Molded_String(mo);
}


//
//  Form_Reduce_Throws: C
//
// Evaluates each item in a block and forms it, with an optional delimiter.
// If all the items in the block are null, or no items are found, this will
// return a nulled value.
//
// CHAR! suppresses the delimiter logic.  Hence:
//
//    >> delimit ":" ["a" space "b" | () "c" newline "d" "e"]
//    == `"a b^/c^/d:e"
//
// Note only the last interstitial is considered a candidate for delimiting.
//
bool Form_Reduce_Throws(
    REBVAL *out,
    const REBARR *array,
    REBLEN index,
    REBSPC *specifier,
    const REBVAL *delimiter
){
    assert(
        IS_NULLED(delimiter) or IS_CHAR(delimiter) or IS_TEXT(delimiter)
    );

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    DECLARE_ARRAY_FEED (feed, array, index, specifier);

    DECLARE_FRAME (f, feed, EVAL_MASK_DEFAULT);
    Push_Frame(nullptr, f);

    bool pending = false;  // pending delimiter output, *if* more non-nulls
    bool nothing = true;  // any elements seen so far have been null or blank

    do {
        // See philosophy on handling blanks differently from nulls, but only
        // at dialect "source level".
        // https://forum.rebol.info/t/1348
        //
        if (KIND_BYTE_UNCHECKED(f->feed->value) == REB_BLANK) {
            Literal_Next_In_Frame(out, f);
            Append_Codepoint(mo->series, ' ');
            pending = false;
            nothing = false;
            continue;
        }

        if (Eval_Step_Throws(out, f)) {
            Drop_Mold(mo);
            Abort_Frame(f);
            return true;
        }

        if (IS_END(out)) {
            if (IS_END(f->feed->value)) {  // spaced []
                assert(nothing);
                break;
            }
            continue;  // spaced [comment "a" ...]
        }

        if (IS_NULLED(out) or IS_BLANK(out))  // see note above on BLANK!
            continue;  // opt-out and maybe keep option open to return NULL

        nothing = false;

        if (IS_ISSUE(out)) {  // do not delimit (unified w/char)
            Form_Value(mo, out);
            pending = false;
        }
        else if (IS_NULLED(delimiter))
            Form_Value(mo, out);
        else {
            if (pending)
                Form_Value(mo, delimiter);

            Form_Value(mo, out);
            pending = true;
        }
    } while (NOT_END(f->feed->value));

    if (nothing)
        Init_Nulled(out);
    else
        Init_Text(out, Pop_Molded_String(mo));

    Drop_Frame(f);

    return false;
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
    assert(not TG_Pushing_Mold);  // Can't do debug molding during Push_Mold()
    TG_Pushing_Mold = true;
  #endif

    assert(mo->series == nullptr);  // Indicates not pushed, see DECLARE_MOLD

    REBSER *s = SER(MOLD_BUF);
    ASSERT_SERIES_TERM(s);

    mo->series = STR(s);
    mo->offset = STR_SIZE(mo->series);
    mo->index = STR_LEN(mo->series);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT))
        assert(mo->limit != 0);  // !!! Should a limit of 0 be allowed?

    if (
        GET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE)
        and SER_REST(s) < mo->reserve
    ){
        // Expand will add to the series length, so we set it back.
        //
        // !!! Should reserve actually leave the length expanded?  Some cases
        // definitely don't want this, others do.  The protocol most
        // compatible with the appending mold is to come back with an
        // empty buffer after a push.
        //
        Expand_Series(s, mo->offset, mo->reserve);
        SET_SERIES_USED(s, mo->offset);
    }
    else if (SER_REST(s) - SER_USED(s) > MAX_COMMON) {
        //
        // If the "extra" space in the series has gotten to be excessive (due
        // to some particularly large mold), back off the space.  But preserve
        // the contents, as there may be important mold data behind the
        // ->start index in the stack!
        //
        REBLEN len = STR_LEN(MOLD_BUF);
        Remake_Series(
            s,
            SER_USED(s) + MIN_COMMON,
            SER_WIDE(s),
            NODE_FLAG_NODE // NODE_FLAG_NODE means preserve the data
        );
        TERM_STR_LEN_SIZE(mo->series, len, SER_USED(s));
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
    TG_Pushing_Mold = false;
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

    if (STR_LEN(mo->series) - mo->index > mo->limit) {
        REBINT overage = (STR_LEN(mo->series) - mo->index) - mo->limit;

        // Mold buffer is UTF-8...length limit is (currently) in characters,
        // not bytes.  Have to back up the right number of bytes, but also
        // adjust the character length appropriately.

        REBCHR(*) tail = STR_TAIL(mo->series);
        REBUNI dummy;
        REBCHR(*) cp = SKIP_CHR(&dummy, tail, -(overage));

        SET_STR_LEN_SIZE(
            mo->series,
            STR_LEN(mo->series) - overage,
            STR_SIZE(mo->series) - (tail - cp)
        );

        assert(not (mo->opts & MOLD_FLAG_WAS_TRUNCATED));
        mo->opts |= MOLD_FLAG_WAS_TRUNCATED;
    }
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
REBSTR *Pop_Molded_String(REB_MOLD *mo)
{
    assert(mo->series != nullptr);  // if null, there was no Push_Mold()
    ASSERT_SERIES_TERM(SER(mo->series));

    // Limit string output to a specified size to prevent long console
    // garbage output if MOLD_FLAG_LIMIT was set in Push_Mold().
    //
    Throttle_Mold(mo);

    REBSIZ size = STR_SIZE(mo->series) - mo->offset;
    REBLEN len = STR_LEN(mo->series) - mo->index;

    REBSTR *popped = Make_String(size);
    memcpy(BIN_HEAD(SER(popped)), BIN_AT(SER(mo->series), mo->offset), size);
    TERM_STR_LEN_SIZE(popped, len, size);

    // Though the protocol of Mold_Value does terminate, it only does so if
    // it adds content to the buffer.  If we did not terminate when we
    // reset the size, then these no-op molds (e.g. mold of "") would leave
    // whatever value in the terminator spot was there.  This could be
    // addressed by making no-op molds terminate.
    //
    TERM_STR_LEN_SIZE(STR(mo->series), mo->index, mo->offset);

    mo->series = nullptr;  // indicates mold is not currently pushed
    return popped;
}


//
//  Pop_Molded_Binary: C
//
// !!! This particular use of the mold buffer might undermine tricks which
// could be used with invalid UTF-8 bytes--for instance.  Review.
//
REBSER *Pop_Molded_Binary(REB_MOLD *mo)
{
    assert(STR_LEN(mo->series) >= mo->offset);

    ASSERT_SERIES_TERM(SER(mo->series));
    Throttle_Mold(mo);

    REBSIZ size = STR_SIZE(mo->series) - mo->offset;
    REBSER *bin = Make_Binary(size);
    memcpy(BIN_HEAD(bin), BIN_AT(SER(mo->series), mo->offset), size);
    TERM_BIN_LEN(bin, size);

    // Though the protocol of Mold_Value does terminate, it only does so if
    // it adds content to the buffer.  If we did not terminate when we
    // reset the size, then these no-op molds (e.g. mold of "") would leave
    // whatever value in the terminator spot was there.  This could be
    // addressed by making no-op molds terminate.
    //
    TERM_STR_LEN_SIZE(mo->series, mo->index, mo->offset);

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
    NOTE_SERIES_MAYBE_TERM(mo->series);

    // see notes in Pop_Molded_String()
    //
    TERM_STR_LEN_SIZE(mo->series, mo->index, mo->offset);

    mo->series = nullptr;  // indicates mold is not currently pushed
}


//
//  Startup_Mold: C
//
void Startup_Mold(REBLEN size)
{
    TG_Mold_Stack = Make_Series(10, sizeof(const void*));

    // Most string code tries to optimize "bookmarks" that help map indices
    // to encoded codepoint positions in such a way that when the string
    // gets short, the bookmarks are discarded.  The mold buffer does not
    // do this.
    //
    // !!! Review, seems like the mold buffer logic is broken.  :-/
    //
    TG_Mold_Buf = Make_String_Core(size, SERIES_FLAG_ALWAYS_DYNAMIC);
}


//
//  Shutdown_Mold: C
//
void Shutdown_Mold(void)
{
    Free_Unmanaged_Series(SER(TG_Mold_Buf));
    TG_Mold_Buf = nullptr;

    Free_Unmanaged_Series(TG_Mold_Stack);
    TG_Mold_Stack = nullptr;
}
