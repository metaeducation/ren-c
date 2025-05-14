//
//  file: %s-mold.c
//  summary: "value to string conversion"
//  section: strings
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// "Molding" is a term in Rebol for getting a TEXT! representation of an
// element that is intended to be LOADed back into the system.  So if you mold
// a TEXT!, you would get back another TEXT! that would include the delimiters
// for that string (and any required escaping, e.g. for embedded quotes).
//
// "Forming" is the term for creating a string representation of a value that
// is intended for print output.  So if you were to form a TEXT!, it would
// *not* add delimiters or escaping--just giving the string back as-is.
//
// There are several technical problems in molding regarding the handling of
// cells that do not have natural expressions in Rebol source.  For instance,
// it was legal (in Rebol2) to say (to word! "123") but that can't mold as
// 123 because that would LOAD as an integer.  There are additional problems
// with `mold next [a b c]`, because there is no natural representation for a
// series that is not at its head.  These problems were addressed with
// "construction syntax", e.g. #[word! "123"] or #[block! [a b c] 1].  But
// to get this output MOLD:ALL had to be used, and it was implemented in
// something of an ad-hoc way.  :ALL was deemed too meaningless to wield
// effectively and was removed.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * The name "mold" allegedly originates from when Rebol was supposedly
//   called "Clay".  But it now looks random and off-putting, like it's
//   referring to fungal mold.  Some progress has been made on reducing the
//   need to use the term, e.g. (print @val) or (print ["val:" @(next val)])
//   will perform the operation without needing to explicitly name it.  But
//   further finessing the name is desirable.
//
// * Because molding and forming of a type share a lot of code, they are
//   implemented in "(M)old or (F)orm" hooks (MF_Xxx).  Also, since classes
//   of types can share behavior, several types are sometimes handled in the
//   same hook.  See %types.r for these categorizations in the "mold" column.
//
// * Molding is done via a Molder structure, which in addition to the
//   String to mold into contains options for the mold--including length
//   limits, whether commas or periods should be used for decimal points,
//   indentation rules, etc.
//
// * If you use the Push_Mold() function to fill a Molder, then it will
//   append in a stacklike way to the thread-local "mold buffer".  This
//   allows new molds to start running and use that buffer while another is in
//   progress, so long as it pops or drops the buffer before returning to the
//   code doing the higher level mold.
//
// * It's hard to know in advance how long molded output will be.  Using the
//   mold buffer allows one to use a "hot" preallocated UTF-8 buffer for the
//   mold...and copy out a String of the precise width and length needed.
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
Byte* Prep_Mold_Overestimated(Molder* mo, REBLEN num_bytes)
{
    REBLEN tail = String_Len(mo->string);
    Expand_Flex_Tail(mo->string, num_bytes);  // terminates at guess
    return Binary_At(mo->string, tail);
}


//
//  Begin_Non_Lexical_Mold: C
//
// For datatypes that don't have lexical representations, use a legacy
// format (like #[object! ...]) just to have something to say.
//
// At one type an attempt was made to TRANSCODE these forms.  That idea is
// under review, likely in favor of a more thought-out concept involving
// FENCE! and UNMAKE:
//
// https://forum.rebol.info/t/2225
//
void Begin_Non_Lexical_Mold(Molder* mo, const Cell* v)
{
    Append_Ascii(mo->string, "#[");

    Heart heart = Heart_Of_Builtin_Fundamental(v);
    const Symbol* type_name = Canon_Symbol(Symbol_Id_From_Type(heart));
    Append_Spelling(mo->string, type_name);  // includes the "!"

    Append_Codepoint(mo->string, ' ');
}


//
//  End_Non_Lexical_Mold: C
//
// Finish the mold of types that don't have lexical representations.
//
void End_Non_Lexical_Mold(Molder* mo)
{
    Append_Codepoint(mo->string, ']');
}


//
//  New_Indented_Line: C
//
// Create a newline with auto-indent on next line if needed.
//
void New_Indented_Line(Molder* mo)
{
    // Check output string has content already but no terminator:
    //
    Byte* bp;
    if (String_Len(mo->string) == 0)
        bp = nullptr;
    else {
        bp = Binary_Last(mo->string);  // legal way to check UTF-8
        if (*bp == ' ' or *bp == '\t')
            *bp = '\n';
        else
            bp = nullptr;
    }

    // Add terminator:
    if (bp == nullptr)
        Append_Codepoint(mo->string, '\n');

    // Add proper indentation:
    if (NOT_MOLD_FLAG(mo, MOLD_FLAG_INDENT)) {
        REBINT n;
        for (n = 0; n < mo->indent; n++)
            Append_Ascii(mo->string, "    ");
    }
}


//=//// DEALING WITH CYCLICAL MOLDS ///////////////////////////////////////=//
//
// While Rebol has never had a particularly coherent story about how cyclical
// data structures will be handled in evaluation, they do occur--and the GC
// is robust to their existence.  These helper functions can be used to
// maintain a stack of Flex.
//
// !!! TBD: Unify this with the Push_Lifeguard and Drop_Lifeguard implementation
// so that improvements in one will improve the other?
//
//=////////////////////////////////////////////////////////////////////////=//

//
//  Find_Pointer_In_Flex: C
//
REBINT Find_Pointer_In_Flex(Flex* f, const void *p)
{
    REBLEN index = 0;
    for (; index < Flex_Used(f); ++index) {
        if (*Flex_At(void*, f, index) == p)
            return index;
    }
    return NOT_FOUND;
}

//
//  Push_Pointer_To_Flex: C
//
void Push_Pointer_To_Flex(Flex* f, const void *p)
{
    if (Is_Flex_Full(f))
        Extend_Flex_If_Necessary(f, 8);
    *Flex_At(const void*, f, Flex_Used(f)) = p;
    Set_Flex_Used(f, Flex_Used(f) + 1);
}

//
//  Drop_Pointer_From_Flex: C
//
void Drop_Pointer_From_Flex(Flex* f, const void *p)
{
    assert(p == *Flex_At(void*, f, Flex_Used(f) - 1));
    UNUSED(p);
    Set_Flex_Used(f, Flex_Used(f) - 1);

    // !!! Could optimize so mold stack is always dynamic, and just use
    // s->content.dynamic.len--
}


//=/// ARRAY MOLDING //////////////////////////////////////////////////////=//

//
//  Mold_Array_At: C
//
void Mold_Array_At(
    Molder* mo,
    const Array* a,
    REBLEN index,
    const char *sep
){
    // Recursion check:
    if (Find_Pointer_In_Flex(g_mold.stack, a) != NOT_FOUND) {
        if (sep[0] != '\0')
            Append_Codepoint(mo->string, sep[0]);
        Append_Ascii(mo->string, "...");
        if (sep[1] != '\0')
            Append_Codepoint(mo->string, sep[1]);
        return;
    }

    Push_Pointer_To_Flex(g_mold.stack, a);

    bool indented = false;

    if (sep[0] != '\0')
        Append_Codepoint(mo->string, sep[0]);

    bool first_item = true;

    const Element* item_tail = Array_Tail(a);
    const Element* item = Array_At(a, index);
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

        Mold_Element(mo, item);

        ++item;
        if (item == item_tail)
            break;

        if (Not_Cell_Flag(item, NEWLINE_BEFORE))
            Append_Codepoint(mo->string, ' ');
    }

    if (indented)
        --mo->indent;

    if (sep[1] != '\0') {
        if (Stub_Flavor(a) != FLAVOR_SOURCE) {
            // Note: We accommodate varlist/etc. for internal PROBE()
        }
        else {
            if (Get_Source_Flag(cast(const Source*, a), NEWLINE_AT_TAIL))
                New_Indented_Line(mo);
        }
        Append_Codepoint(mo->string, sep[1]);
    }

    Drop_Pointer_From_Flex(g_mold.stack, a);
}


//
//  Form_Array_At: C
//
void Form_Array_At(
    Molder* mo,
    const Array* array,
    REBLEN index,
    Option(VarList*) context,
    bool relax  // make antiforms into quasiforms instead of erroring
){
    REBINT len = Array_Len(array) - index;
    if (len < 0)
        len = 0;

    REBINT n;
    for (n = 0; n < len;) {
        DECLARE_ELEMENT (safe);
        const Element* item = Array_At(array, index + n);
        Value* wval = nullptr;
        if (context and (Is_Word(item) or Is_Get_Word(item))) {
            wval = maybe Select_Symbol_In_Context(
                Varlist_Archetype(unwrap context),
                Cell_Word_Symbol(item)
            );
            if (wval) {
                if (relax and (Is_Antiform(wval)))
                    item = Copy_Meta_Cell(safe, wval);
                else
                    item = Ensure_Element(wval);
            }
        }
        Mold_Or_Form_Element(mo, item, wval == nullptr);
        n++;
        if (GET_MOLD_FLAG(mo, MOLD_FLAG_LINES)) {
            Append_Codepoint(mo->string, LF);
        }
        else {  // Add a space if needed
            if (
                n < len
                and String_Len(mo->string) != 0
                and *Binary_Last(mo->string) != LF
                and NOT_MOLD_FLAG(mo, MOLD_FLAG_TIGHT)
            ){
                Append_Codepoint(mo->string, ' ');
            }
        }
    }
}


//
//  Mold_Or_Form_Cell_Ignore_Quotes: C
//
// Variation which molds a cell.  Quoting is not considered, but quasi is.
//
// 1. It's hard to detect the exact moment of tripping over the length limit
//    unless all code paths that add to the mold buffer (e.g. tacking on
//    delimiters etc.) check the limit.  The easier thing to do is check at
//    the end and truncate.  We short circuit here, but it may already be
//    over the limit.
//
void Mold_Or_Form_Cell_Ignore_Quotes(
    Molder* mo,
    const Cell* cell,
    bool form
){
    String* s = mo->string;
    Assert_Flex_Term_If_Needed(s);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT)) {
        if (String_Len(s) >= mo->limit)  // >= : it may already be over [1]
            return;
    }

    DECLARE_ELEMENT (element);
    Copy_Dequoted_Cell(element, cell);
    Plainify(element);  // can't have Sigil and dispatch to mold
    Quotify(element);

    DECLARE_ELEMENT (molder);
    Init_Handle_Cdata(molder, mo, 1);

    DECLARE_VALUE (formval);
    Init_Logic(formval, form);
    Meta_Quotify(formval);

    if (
        GET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD)
        or (QUOTE_BYTE(cell) & NONQUASI_BIT)
    ){
        rebElide(CANON(MOLDIFY), element, molder, formval);
    }
    else {
        Append_Codepoint(mo->string, '~');
        if (HEART_BYTE(cell) != TYPE_BLANK) {
            rebElide(CANON(MOLDIFY), element, molder, formval);
            Append_Codepoint(mo->string, '~');
        }
    }

    Assert_Flex_Term_If_Needed(s);
}


//
//  Mold_Or_Form_Element: C
//
// Mold or form any reified value to string series tail.
//
void Mold_Or_Form_Element(Molder* mo, const Element* e, bool form)
{
    // Mold hooks take a noquote cell and not a Cell*, so they expect any
    // quotes applied to have already been done.

    if (Not_Cell_Readable(e)) {
      #if RUNTIME_CHECKS
        Append_Ascii(mo->string, "\\\\unreadable\\\\");
      #endif
        return;  // !!! should never happen in release builds
    }

    REBLEN i;
    for (i = 0; i < Quotes_Of(e); ++i)
        Append_Codepoint(mo->string, '\'');

    Option(Sigil) sigil = Sigil_Of(e);
    if (sigil)
        Append_Codepoint(mo->string, Char_For_Sigil(unwrap sigil));

    Mold_Or_Form_Cell_Ignore_Quotes(mo, e, form);
}


//
//  Copy_Mold_Or_Form_Element: C
//
String* Copy_Mold_Or_Form_Element(const Element* v, Flags opts, bool form)
{
    DECLARE_MOLDER (mo);
    mo->opts = opts;

    Push_Mold(mo);
    Mold_Or_Form_Element(mo, v, form);
    return Pop_Molded_String(mo);
}


//
//  Copy_Mold_Or_Form_Cell_Ignore_Quotes: C
//
String* Copy_Mold_Or_Form_Cell_Ignore_Quotes(
    const Cell* cell,
    Flags opts,
    bool form
){
    DECLARE_MOLDER (mo);
    mo->opts = opts;

    Push_Mold(mo);
    Mold_Or_Form_Cell_Ignore_Quotes(mo, cell, form);
    return Pop_Molded_String(mo);
}


//
//  Push_Mold: C
//
// Like the data stack, a single contiguous String Flex is used for the mold
// buffer.  So if a mold needs to happen during another mold, it is pushed
// into a stack and must balance (with either a Pop() or Drop() of the nested
// string).  The panic() mechanics will automatically balance the stack.
//
void Push_Mold(Molder* mo)
{
  #if RUNTIME_CHECKS
    assert(not g_mold.currently_pushing);  // Can't mold during Push_Mold()
    g_mold.currently_pushing = true;
  #endif

    assert(mo->string == nullptr);  // Indicates not pushed, see DECLARE_MOLDER

    String* s = g_mold.buffer;
    assert(Link_Bookmarks(s) == nullptr);  // should never bookmark buffer

    Assert_Flex_Term_If_Needed(s);

    mo->string = s;
    mo->base.size = String_Size(s);
    mo->base.index = String_Len(s);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT))
        assert(mo->limit != 0);  // !!! Should a limit of 0 be allowed?

    if (
        GET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE)
        and Flex_Rest(s) < mo->reserve
    ){
        // Expand will add to the series length, so we set it back.
        //
        // !!! Should reserve actually leave the length expanded?  Some cases
        // definitely don't want this, others do.  The protocol most
        // compatible with the appending mold is to come back with an
        // empty buffer after a push.
        //
        Expand_Flex(s, mo->base.size, mo->reserve);
        Set_Flex_Used(s, mo->base.size);
    }
    else if (Flex_Rest(s) - Flex_Used(s) > MAX_COMMON) {
        //
        // If the "extra" space in the series has gotten to be excessive (due
        // to some particularly large mold), back off the space.  But preserve
        // the contents, as there may be important mold data behind the
        // ->start index in the stack!
        //
        Length len = String_Len(g_mold.buffer);
        Remake_Flex(
            s,
            Flex_Used(s) + MIN_COMMON,
            NODE_FLAG_NODE // NODE_FLAG_NODE means preserve the data
        );
        Term_String_Len_Size(mo->string, len, Flex_Used(s));
    }

    mo->digits = MAX_DIGITS;

  #if RUNTIME_CHECKS
    g_mold.currently_pushing = false;
  #endif
}


//
//  Throttle_Mold: C
//
// Contain a mold's series to its limit (if it has one).
//
void Throttle_Mold(Molder* mo) {
    if (NOT_MOLD_FLAG(mo, MOLD_FLAG_LIMIT))
        return;

    if (String_Len(mo->string) - mo->base.index > mo->limit) {
        REBINT overage = (String_Len(mo->string) - mo->base.index) - mo->limit;

        // Mold buffer is UTF-8...length limit is (currently) in characters,
        // not bytes.  Have to back up the right number of bytes, but also
        // adjust the character length appropriately.

        Utf8(*) tail = String_Tail(mo->string);
        Codepoint dummy;
        Utf8(*) cp = Utf8_Skip(&dummy, tail, -(overage));

        Term_String_Len_Size(
            mo->string,
            String_Len(mo->string) - overage,
            String_Size(mo->string) - (tail - cp)
        );

        possibly(GET_MOLD_FLAG(mo, MOLD_FLAG_WAS_TRUNCATED));  // mold may set
        SET_MOLD_FLAG(mo, MOLD_FLAG_WAS_TRUNCATED);
    }
}


//
//  Pop_Molded_String_Core: C
//
String* Pop_Molded_String_Core(String* buf, Size offset, Length index)
{
    Size size = String_Size(buf) - offset;
    Length len = String_Len(buf) - index;

    String* popped = Make_String(size);
    memcpy(Binary_Head(popped), Binary_At(buf, offset), size);
    Term_String_Len_Size(popped, len, size);

    // Though the protocol of Mold_Element() does terminate, it only does so if
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
// is a helper that extracts the data as a String Flex.  It resets the
// buffer to its length at the time when the last push began.
//
String* Pop_Molded_String(Molder* mo)
{
    assert(mo->string != nullptr);  // if null, there was no Push_Mold()
    Assert_Flex_Term_If_Needed(mo->string);

    // Limit string output to a specified size to prevent long console
    // garbage output if MOLD_FLAG_LIMIT was set in Push_Mold().
    //
    Throttle_Mold(mo);

    String* popped = Pop_Molded_String_Core(
        mo->string,
        mo->base.size,
        mo->base.index
    );

    mo->string = nullptr;  // indicates mold is not currently pushed
    return popped;
}


//
//  Pop_Molded_Binary: C
//
// !!! This particular use of the mold buffer might undermine tricks which
// could be used with invalid UTF-8 bytes--for instance.  Review.
//
Binary* Pop_Molded_Binary(Molder* mo)
{
    assert(String_Len(mo->string) >= mo->base.size);

    Assert_Flex_Term_If_Needed(mo->string);
    Throttle_Mold(mo);

    Size size = String_Size(mo->string) - mo->base.size;
    Binary* b = Make_Binary(size);
    memcpy(Binary_Head(b), Binary_At(mo->string, mo->base.size), size);
    Term_Binary_Len(b, size);

    // Though the protocol of Mold_Element() does terminate, it only does so if
    // it adds content to the buffer.  If we did not terminate when we
    // reset the size, then these no-op molds (e.g. mold of "") would leave
    // whatever value in the terminator spot was there.  This could be
    // addressed by making no-op molds terminate.
    //
    Term_String_Len_Size(mo->string, mo->base.index, mo->base.size);

    mo->string = nullptr;  // indicates mold is not currently pushed
    return b;
}


//
//  Drop_Mold_Core: C
//
// When generating a molded String, sometimes it's enough to have access to
// the molded data without actually creating a new String Flex.  If the
// information in the mold has done its job and Pop_Molded_String() is not
// required, just call this to drop back to the state of the last push.
//
// Note: Direct pointers into the mold buffer are unstable if another mold
// runs during it!  Do not pass these pointers into code that can run an
// additional mold (that can be just about anything, even debug output...)
//
void Drop_Mold_Core(
    Molder* mo,
    bool not_pushed_ok  // see Drop_Mold_If_Pushed()
){
    if (mo->string == nullptr) {  // there was no Push_Mold()
        assert(not_pushed_ok);
        UNUSED(not_pushed_ok);
        return;
    }

    // When pushed data are to be discarded, mo->string may be unterminated.
    // (Indeed that happens when Try_Scan_Utf8_Item_Push_Mold returns NULL/0.)
    //
    Note_Flex_Maybe_Term(mo->string);

    // see notes in Pop_Molded_String()
    //
    Term_String_Len_Size(mo->string, mo->base.index, mo->base.size);

    mo->string = nullptr;  // indicates mold is not currently pushed
}


//
//  Startup_Mold: C
//
void Startup_Mold(Size encoded_capacity)
{
    g_mold.stack = Make_Flex(FLAG_FLAVOR(MOLDSTACK), Flex, 10);

    ensure(nullptr, g_mold.buffer) = Make_String_Core(
        FLEX_MASK_STRING
            | (not NODE_FLAG_MANAGED)
            | STUB_FLAG_DYNAMIC,
        encoded_capacity
    );
}


//
//  Shutdown_Mold: C
//
void Shutdown_Mold(void)
{
    assert(Link_Bookmarks(g_mold.buffer) == nullptr);  // should not be set
    Free_Unmanaged_Flex(g_mold.buffer);
    g_mold.buffer = nullptr;

    Free_Unmanaged_Flex(g_mold.stack);
    g_mold.stack = nullptr;
}
