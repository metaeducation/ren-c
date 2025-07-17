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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// "Molding" is the term in Rebol for getting a string representation of a
// value that is intended to be LOADed back into the system.  So if you
// mold a STRING!, you would get back another STRING! that would include
// the delimiters for that string.
//
// "Forming" is the term for creating a string representation of a value that
// is intended for print output.  So if you were to form a STRING!, it would
// *not* add delimiters--just giving the string back as-is.
//
// There are several technical problems in molding regarding the handling of
// values that do not have natural expressions in Rebol source.  For instance,
// it might be legal to `make word! "123"` but that cannot just be molded as
// 123 because that would LOAD as an integer.  There are additional problems
// with `mold next [a b c]`, because there is no natural representation for a
// series that is not at its head.  These problems were addressed with
// "construction syntax", e.g. #[word! "123"] or #[block! [a b c] 1].  But
// to get this behavior MOLD/ALL had to be used, and it was implemented in
// something of an ad-hoc way.
//
// These concepts are a bit fuzzy in general, and though MOLD might have made
// sense when Rebol was supposedly called "Clay", it now looks off-putting.
// (Who wants to deal with old, moldy code?)  Most of Ren-C's focus has been
// on the evaluator, so there are not that many advances in molding--other
// than the code being tidied up and methodized a little.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Notes:
//
// * Because molding and forming of a type share a lot of code, they are
//   implemented in "(M)old or (F)orm" hooks (MF_Xxx).  Also, since classes
//   of types can share behavior, several types are sometimes handled in the
//   same hook.  See %types.r for these categorizations in the "mold" column.
//
// * Molding is done into a Molder structure, which in addition to the
//   UTF8 Flex to mold into contains options for the mold--including length
//   limits, whether commas or periods should be used for decimal points,
//   indentation rules, etc.
//
// * If you create the Molder using the Push_Mold() function, then it will
//   append in a stacklike way to the thread-local "mold buffer".  This
//   allows new molds to start running and use that buffer while another is in
//   progress, so long as it pops or drops the buffer before returning to the
//   code doing the higher level mold.
//
// * It's hard to know in advance how long molded output will be or whether
//   it will use any wide characters, using the mold buffer allows one to use
//   a "hot" preallocated wide-char buffer for the mold...and copy out a
//   series of the precise width and length needed.  (That is, if copying out
//   the result is needed at all.)
//

#include "sys-core.h"


//
//  Emit: C
//
// This is a general "printf-style" utility function, which R3-Alpha used to
// make some formatting tasks easier.  It was not applied consistently, and
// some callsites avoided using it because it would be ostensibly slower
// than calling the functions directly.
//
void Emit(Molder* mo, const char *fmt, ...)
{
    Binary* s = mo->utf8flex;
    assert(Flex_Wide(s) == 1);

    va_list va;
    va_start(va, fmt);

    Byte ender = '\0';

    for (; *fmt; fmt++) {
        switch (*fmt) {
        case 'W': { // Word symbol
            const Value* any_word = va_arg(va, const Value*);
            Symbol* symbol = Word_Symbol(any_word);
            Append_Utf8_Utf8(
                s, Symbol_Head(symbol), Symbol_Size(symbol)
            );
            break;
        }

        case 'V': // Value
            Mold_Value(mo, va_arg(va, const Value*));
            break;

        case 'S': // String of bytes
            Append_Unencoded(s, va_arg(va, const char *));
            break;

        case 'C': // Char
            Append_Codepoint(s, va_arg(va, uint32_t));
            break;

        case 'I': // Integer
            Append_Int(s, va_arg(va, REBINT));
            break;

        case 'i':
            Append_Int_Pad(s, va_arg(va, REBINT), -9);
            Trim_Tail(s, '0');
            break;

        case '2': // 2 digit int (for time)
            Append_Int_Pad(s, va_arg(va, REBINT), 2);
            break;

        case 'T': {  // Type name
            Symbol* type_name = Get_Type_Name(va_arg(va, Value*));
            Append_Utf8_Utf8(s, Symbol_Head(type_name), Symbol_Size(type_name));
            break; }

        case 'N': {  // Symbol name
            Symbol* symbol = va_arg(va, Symbol*);
            Append_Utf8_Utf8(s, Symbol_Head(symbol), Symbol_Size(symbol));
            break; }

        case '+': // Add #[ if mold/all
            Append_Unencoded(s, "#[");
            ender = ']';
            break;

        case 'D': // Datatype symbol: #[type
            if (ender != '\0') {
                Symbol* canon = Canon_From_Id(cast(SymId, va_arg(va, int)));
                Append_Utf8_Utf8(s, Symbol_Head(canon), Symbol_Size(canon));
                Append_Codepoint(s, ' ');
            }
            else
                va_arg(va, REBLEN); // ignore it
            break;

        default:
            Append_Codepoint(s, *fmt);
        }
    }

    va_end(va);

    if (ender != '\0')
        Append_Codepoint(s, ender);
}


//
//  Prep_Mold_Overestimated: C
//
// But since R3-Alpha's mold buffer was fixed size at unicode, it could
// accurately know that one character in a STRING! or URL! or FILE! would only
// be one unit of mold buffer, unless it was escaped.  So it would prescan
// for escapes and compensate accordingly.  In the interim period where
// ANY-STRING! is two-bytes per codepoint and the mold buffer is UTF-8, it's
// hard to be precise.
//
// So this locates places in the code that pass in a potential guess which may
// (or may not) be right.  (Guesses will tend to involve some multiplication
// of codepoint counts by 4, since that's the largest a UTF-8 character can
// end up encoding).  Doing this more precisely is not worth it for this
// interim mode, as there will be no two-bytes-per-codepoint code eventaully.
//
// !!! One premise of the mold buffer is that it will generally be bigger than
// your output, so you won't expand it often.  This lets one be a little
// sloppy on expansion and keeping the series length up to date (could use an
// invalid UTF-8 character as an end-of-buffer signal, much as END markers are
// used by the data stack)
//
Byte *Prep_Mold_Overestimated(Molder* mo, REBLEN num_bytes)
{
    REBLEN tail = Flex_Len(mo->utf8flex);
    Expand_Flex_Tail(mo->utf8flex, num_bytes); // terminates, if guessed right
    return Binary_At(mo->utf8flex, tail);
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
    Emit(mo, "#[T ", v);
}


//
//  End_Non_Lexical_Mold: C
//
// Finish the mold, depending on /ALL with close block.
//
void End_Non_Lexical_Mold(Molder* mo)
{
    Append_Codepoint(mo->utf8flex, ']');
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
    Byte *bp;
    if (Flex_Len(mo->utf8flex) == 0)
        bp = nullptr;
    else {
        bp = Binary_Last(mo->utf8flex);
        if (*bp == ' ' || *bp == '\t')
            *bp = '\n';
        else
            bp = nullptr;
    }

    // Add terminator:
    if (bp == nullptr)
        Append_Codepoint(mo->utf8flex, '\n');

    // Add proper indentation:
    if (NOT_MOLD_FLAG(mo, MOLD_FLAG_INDENT)) {
        REBINT n;
        for (n = 0; n < mo->indent; n++)
            Append_Unencoded(mo->utf8flex, "    ");
    }
}


//=//// DEALING WITH CYCLICAL MOLDS ///////////////////////////////////////=//
//
// While Rebol has never had a particularly coherent story about how cyclical
// data structures will be handled in evaluation, they do occur--and the GC
// is robust to their existence.  These helper functions can be used to
// maintain a stack of Flex pointers.
//
// !!! TBD: Unify this with the Push_GC_Guard and Drop_GC_Guard implementation
// so that improvements in one will improve the other?
//
//=////////////////////////////////////////////////////////////////////////=//

//
//  Find_Pointer_In_Flex: C
//
REBLEN Find_Pointer_In_Flex(Flex* s, void *p)
{
    REBLEN index = 0;
    for (; index < Flex_Len(s); ++index) {
        if (*Flex_At(void*, s, index) == p)
            return index;
    }
    return NOT_FOUND;
}

//
//  Push_Pointer_To_Flex: C
//
void Push_Pointer_To_Flex(Flex* s, void *p)
{
    if (Is_Flex_Full(s))
        Extend_Flex(s, 8);
    *Flex_At(void*, s, Flex_Len(s)) = p;
    Set_Flex_Len(s, Flex_Len(s) + 1);
}

//
//  Drop_Pointer_From_Flex: C
//
void Drop_Pointer_From_Flex(Flex* s, void *p)
{
    assert(p == *Flex_At(void*, s, Flex_Len(s) - 1));
    UNUSED(p);
    Set_Flex_Len(s, Flex_Len(s) - 1);

    // !!! Could optimize so mold stack is always dynamic, and just use
    // s->content.dynamic.len--
}


/***********************************************************************
************************************************************************
**
**  SECTION: Block Flex Datatypes
**
************************************************************************
***********************************************************************/

//
//  Mold_Array_At: C
//
void Mold_Array_At(
    Molder* mo,
    Array* a,
    REBLEN index,
    const char *sep
) {
    // Recursion check:
    if (Find_Pointer_In_Flex(TG_Mold_Stack, a) != NOT_FOUND) {
        Emit(mo, "C...C", sep[0], sep[1]);
        return;
    }

    Push_Pointer_To_Flex(TG_Mold_Stack, a);

    bool indented = false;

    if (sep[1])
        Append_Codepoint(mo->utf8flex, sep[0]);

    Cell* item = Array_At(a, index);
    while (NOT_END(item)) {
        if (Get_Cell_Flag(item, NEWLINE_BEFORE)) {
           if (not indented and (sep[1] != '\0')) {
                ++mo->indent;
                indented = true;
            }

            New_Indented_Line(mo);
        }

        if (sep[0] == '/' and Is_Blank(item) and IS_END(item + 1)) {
            // don't render blanks at tails of paths
        }
        else
            Mold_Value(mo, item);

        ++item;
        if (IS_END(item))
            break;

        if (sep[0] == '/')
            Append_Codepoint(mo->utf8flex, '/'); // !!! ignores newline
        else if (Not_Cell_Flag(item, NEWLINE_BEFORE))
            Append_Codepoint(mo->utf8flex, ' ');
    }

    if (indented)
        --mo->indent;

    if (sep[1] != '\0') {
        if (Get_Array_Flag(a, NEWLINE_AT_TAIL))
            New_Indented_Line(mo); // but not any indentation from *this* mold
        Append_Codepoint(mo->utf8flex, sep[1]);
    }

    Drop_Pointer_From_Flex(TG_Mold_Stack, a);
}


//
//  Form_Array_At: C
//
void Form_Array_At(
    Molder* mo,
    Array* array,
    REBLEN index,
    VarList* opt_context
) {
    // Form a series (part_mold means mold non-string values):
    REBINT len = Array_Len(array) - index;
    if (len < 0)
        len = 0;

    REBINT n;
    for (n = 0; n < len;) {
        Cell* item = Array_At(array, index + n);
        Value* wval = nullptr;
        if (opt_context && (Is_Word(item) || Is_Get_Word(item))) {
            wval = Select_Canon_In_Context(opt_context, VAL_WORD_CANON(item));
            if (wval)
                item = wval;
        }
        Mold_Or_Form_Value(mo, item, wval == nullptr);
        n++;
        if (GET_MOLD_FLAG(mo, MOLD_FLAG_LINES)) {
            Append_Codepoint(mo->utf8flex, LF);
        }
        else {
            // Add a space if needed:
            if (n < len && Flex_Len(mo->utf8flex)
                && *Binary_Last(mo->utf8flex) != LF
                && NOT_MOLD_FLAG(mo, MOLD_FLAG_TIGHT)
            ){
                Append_Codepoint(mo->utf8flex, ' ');
            }
        }
    }
}


//
//  MF_Panic: C
//
void MF_Panic(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form);

    if (Type_Of(v) == TYPE_0) {
        //
        // TYPE_0 is reserved for special purposes, and should only be molded
        // in debug scenarios.
        //
    #if NO_RUNTIME_CHECKS
        UNUSED(mo);
        crash (v);
    #else
        printf("!!! Request to MOLD or FORM a TYPE_0 value !!!\n");
        Append_Unencoded(mo->utf8flex, "!!!TYPE_0!!!");
        debug_break(); // don't crash if under a debugger, just "pause"
    #endif
    }

    panic ("Cannot MOLD or FORM datatype.");
}


//
//  MF_Unhooked: C
//
void MF_Unhooked(Molder* mo, const Cell* v, bool form)
{
    UNUSED(mo);
    UNUSED(form);

    const Value* type = Datatype_From_Kind(Type_Of(v));
    UNUSED(type); // !!! put in error message?

    panic ("Datatype does not have extension with a MOLD handler registered");
}


//
//  Mold_Or_Form_Value: C
//
// Mold or form any value to string series tail.
//
void Mold_Or_Form_Value(Molder* mo, const Cell* v, bool form)
{
    assert(not THROWN(v)); // !!! Note: Thrown bit is being eliminated

    Binary* s = mo->utf8flex;
    assert(Flex_Wide(s) == sizeof(Byte));
    Assert_Flex_Term(s);

    if (C_STACK_OVERFLOWING(&s))
        Panic_Stack_Overflow();

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT)) {
        //
        // It's hard to detect the exact moment of tripping over the length
        // limit unless all code paths that add to the mold buffer (e.g.
        // tacking on delimiters etc.) check the limit.  The easier thing
        // to do is check at the end and truncate.  This adds a lot of data
        // wastefully, so short circuit here in the release build.  (Have
        // the debug build keep going to exercise mold on the data.)
        //
    #if NO_RUNTIME_CHECKS
        if (Flex_Len(s) >= mo->limit)
            return;
    #endif
    }

    if (Is_Cell_Unreadable(v)) {
      #if NO_RUNTIME_CHECKS
        crash (v);
      #else
        printf("!!! Request to MOLD or FORM an unreadable cell !!!\n");
        Append_Unencoded(s, "!!!unreadable!!!");
        return;
      #endif
    }

    if (Is_Antiform(v)) {
        //
        // antiforms should only be mold in debug scenarios, but this still
        // happens a lot, e.g. PROBE() of context arrays when they have unset
        // variables.  This happens so often in debug builds, in fact, that a
        // debug_break() here would be very annoying (the method used for
        // TYPE_0 items)
        //
      #if NO_RUNTIME_CHECKS
        crash (v);
      #else
        printf("!!! Request to MOLD or FORM an antiform !!!\n");
        if (Is_Nulled(v))
            Append_Unencoded(s, "!!!null!!!");
        else if (Is_Okay(v))
            Append_Unencoded(s, "!!!okay!!!");
        else if (Is_Void(v))
            Append_Unencoded(s, "!!!void!!!");
        else {
            assert(Is_Trash(v));
            Append_Unencoded(s, "!!!trash!!!");
        }
        return;
      #endif
    }

    MOLD_HOOK hook = Mold_Or_Form_Hooks[Type_Of(v)];
    assert(hook != nullptr); // all types have a hook, even if it just fails
    hook(mo, v, form);

    Assert_Flex_Term(s);
}


//
//  Copy_Mold_Or_Form_Value: C
//
// Form a value based on the mold opts provided.
//
Strand* Copy_Mold_Or_Form_Value(const Cell* v, Flags opts, bool form)
{
    DECLARE_MOLDER (mo);
    mo->opts = opts;

    Push_Mold(mo);
    Mold_Or_Form_Value(mo, v, form);
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
    Value* out,
    Array* array,
    REBLEN index,
    Specifier* specifier,
    const Value* delimiter
){
    assert(
        Is_Nulled(delimiter) or Is_Char(delimiter) or Is_Text(delimiter)
    );

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    DECLARE_LEVEL (L);
    Push_Level_At(L, array, index, specifier, DO_MASK_NONE);

    bool pending = false; // pending delimiter output, *if* more non-nulls
    bool nothing = true; // any elements seen so far have been null or blank

    while (NOT_END(L->value)) {
        if (Eval_Step_Throws(SET_END(out), L)) {
            Drop_Mold(mo);
            Abort_Level(L);
            return true;
        }

        if (IS_END(out))
            break;  // e.g. `spaced [comment "hi"]`

        PANIC_IF_ERROR(out);

        if (Is_Nulled(out))
            panic (Error_Need_Non_Null_Raw());

        if (Is_Void(out))
            continue; // <opt-out> and <undo-opt> keep it open to return NULL

        nothing = false;

        if (Is_Blank(out)) {  // acting like a space character seems useful
            Append_Codepoint(mo->utf8flex, ' ');
            pending = false;
        }
        else if (Is_Char(out)) { // no delimit on CHAR! (e.g. space, newline)
            Append_Codepoint(mo->utf8flex, VAL_CHAR(out));
            pending = false;
        }
        else if (Is_Nulled(delimiter))
            Form_Value(mo, out);
        else {
            if (pending)
                Form_Value(mo, delimiter);

            Form_Value(mo, out);
            pending = true;
        }
    }

    Drop_Level(L);

    if (nothing)
        Init_Nulled(out);
    else
        Init_Text(out, Pop_Molded_String(mo));

    return false;
}


//
//  Form_Tight_Block: C
//
Strand* Form_Tight_Block(const Value* blk)
{
    DECLARE_MOLDER (mo);

    Push_Mold(mo);

    Cell* item;
    for (item = List_At(blk); NOT_END(item); ++item)
        Form_Value(mo, item);

    return Pop_Molded_String(mo);
}


//
//  Push_Mold: C
//
void Push_Mold(Molder* mo)
{
  #if RUNTIME_CHECKS
    //
    // If some kind of Debug_Fmt() happens while this Push_Mold is happening,
    // it will lead to a recursion.  It's necessary to look at the stack in
    // the debugger and figure it out manually.  (e.g. any failures in this
    // function will break the very mechanism by which failure messages
    // are reported.)
    //
    // !!! This isn't ideal.  So if all the routines below guaranteed to
    // use some kind of assert reporting mechanism "lower than mold"
    // (hence "lower than Debug_Fmt") that would be an improvement.
    //
    assert(!TG_Pushing_Mold);
    TG_Pushing_Mold = true;

    // Sanity check that if they set a limit it wasn't 0.  (Perhaps over the
    // long term it would be okay, but for now we'll consider it a mistake.)
    //
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT))
        assert(mo->limit != 0);
  #endif

    // Set by DECLARE_MOLDER/pops so you don't same `mo` twice w/o popping.
    // Is assigned even in debug build, scanner uses to determine if pushed.
    //
    assert(mo->utf8flex == nullptr);

    Binary* s = mo->utf8flex = MOLD_BUF;
    mo->start = Flex_Len(s);

    Assert_Flex_Term(s);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE) && Flex_Rest(s) < mo->reserve) {
        //
        // Expand will add to the series length, so we set it back.
        //
        // !!! Should reserve actually leave the length expanded?  Some cases
        // definitely don't want this, others do.  The protocol most
        // compatible with the appending mold is to come back with an
        // empty buffer after a push.
        //
        Expand_Flex(s, mo->start, mo->reserve);
        Set_Flex_Len(s, mo->start);
    }
    else if (Flex_Rest(s) - Flex_Len(s) > MAX_COMMON) {
        //
        // If the "extra" space in the series has gotten to be excessive (due
        // to some particularly large mold), back off the space.  But preserve
        // the contents, as there may be important mold data behind the
        // ->start index in the stack!
        //
        Remake_Flex(
            s,
            Flex_Len(s) + MIN_COMMON,
            Flex_Wide(s),
            NODE_FLAG_NODE // NODE_FLAG_NODE means preserve the data
        );
    }

    mo->digits = MAX_DIGITS;

  #if RUNTIME_CHECKS
    TG_Pushing_Mold = false;
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

    if (Flex_Len(mo->utf8flex) > mo->limit) {
        Set_Flex_Len(mo->utf8flex, mo->limit - 3); // account for ellipsis
        Append_Unencoded(mo->utf8flex, "..."); // adds a null at the tail
    }
}


//
//  Pop_Molded_String_Core: C
//
// When a Push_Mold is started, then string data for the mold is accumulated
// at the tail of the task-global unicode buffer.  Once the molding is done,
// this allows extraction of the string, and resets the buffer to its length
// at the time when the last push began.
//
// Can limit string output to a specified size to prevent long console
// garbage output if MOLD_FLAG_LIMIT was set in Push_Mold().
//
// If len is END_FLAG then all the string content will be copied, otherwise
// it will be copied up to `len`.  If there are not enough characters then
// the debug build will assert.
//
Strand* Pop_Molded_String_Core(Molder* mo, REBLEN len)
{
    assert(mo->utf8flex);  // if nullptr there was no Push_Mold()

    Assert_Flex_Term(mo->utf8flex);
    Throttle_Mold(mo);

    assert(Flex_Len(mo->utf8flex) >= mo->start);
    if (len == UNKNOWN)
        len = Flex_Len(mo->utf8flex) - mo->start;

    Strand* result = Make_Sized_String_UTF8(
        s_cast(Binary_At(mo->utf8flex, mo->start)),
        len
    );
    assert(Flex_Wide(result) == sizeof(Ucs2Unit));

    // Though the protocol of Mold_Value does terminate, it only does so if
    // it adds content to the buffer.  If we did not terminate when we
    // reset the size, then these no-op molds (e.g. mold of "") would leave
    // whatever value in the terminator spot was there.  This could be
    // addressed by making no-op molds terminate.
    //
    Term_Binary_Len(mo->utf8flex, mo->start);

    mo->utf8flex = nullptr;  // indicates mold is not currently pushed
    return result;
}


//
//  Pop_Molded_UTF8: C
//
// Same as Pop_Molded_String() except gives back the data in UTF8 byte-size
// series form.
//
Flex* Pop_Molded_UTF8(Molder* mo)
{
    assert(Flex_Len(mo->utf8flex) >= mo->start);

    Assert_Flex_Term(mo->utf8flex);
    Throttle_Mold(mo);

    Flex* bytes = Copy_Non_Array_Flex_At_Len(
        mo->utf8flex, mo->start, Flex_Len(mo->utf8flex) - mo->start
    );
    assert(BYTE_SIZE(bytes));

    // Though the protocol of Mold_Value does terminate, it only does so if
    // it adds content to the buffer.  If we did not terminate when we
    // reset the size, then these no-op molds (e.g. mold of "") would leave
    // whatever value in the terminator spot was there.  This could be
    // addressed by making no-op molds terminate.
    //
    Term_Binary_Len(mo->utf8flex, mo->start);

    mo->utf8flex = nullptr;  // indicates mold is not currently pushed
    return bytes;
}


//
//  Pop_Molded_Binary: C
//
// !!! This particular use of the mold buffer might undermine tricks which
// could be used with invalid UTF-8 bytes--for instance.  Review.
//
// In its current form, the implementation is not distinguishable from
// Pop_Molded_UTF8.
//
Flex* Pop_Molded_Binary(Molder* mo)
{
    return Pop_Molded_UTF8(mo);
}


//
//  Drop_Mold_Core: C
//
// When generating a molded string, sometimes it's enough to have access to
// the molded data without actually creating a new series out of it.  If the
// information in the mold has done its job and Pop_Molded_String() is not
// required, just call this to drop back to the state of the last push.
//
void Drop_Mold_Core(Molder* mo, bool not_pushed_ok)
{
    // The tokenizer can often identify tokens to load by their start and end
    // pointers in the UTF8 data it is loading alone.  However, scanning
    // string escapes is a process that requires converting the actual
    // characters to unicode.  To avoid redoing this work later in the scan,
    // it uses the mold buffer as a storage space from the tokenization
    // that did UTF-8 decoding of string contents to reuse.
    //
    // Despite this usage, it's desirable to be able to do things like output
    // debug strings or do basic molding in that code.  So to reuse the
    // buffer, it has to properly participate in the mold stack protocol.
    //
    // However, only a few token types use the buffer.  Rather than burden
    // the tokenizer with an additional flag, having a modality to be willing
    // to "drop" a mold that hasn't ever been pushed is the easiest way to
    // avoid intervening.  Drop_Mold_If_Pushed(mo) macro makes this clearer.
    //
    if (not_pushed_ok && mo->utf8flex == nullptr)
        return;

    assert(mo->utf8flex != nullptr);  // if nullptr there was no Push_Mold

    // When pushed data are to be discarded, mo->utf8flex may be unterminated.
    // (Indeed that happens when Scan_Item_Push_Mold returns nullptr.)
    //
    Note_Flex_Maybe_Term(mo->utf8flex);

    Term_Binary_Len(mo->utf8flex, mo->start); // see Pop_Molded_String() notes

    mo->utf8flex = nullptr;  // indicates mold is not currently pushed
}


//
//  Startup_Mold: C
//
void Startup_Mold(REBLEN size)
{
    TG_Mold_Stack = Make_Flex(10, sizeof(void*));

    TG_Mold_Buf = Make_Binary(size);
}


//
//  Shutdown_Mold: C
//
void Shutdown_Mold(void)
{
    Free_Unmanaged_Flex(TG_Mold_Buf);
    TG_Mold_Buf = nullptr;

    Free_Unmanaged_Flex(TG_Mold_Stack);
    TG_Mold_Stack = nullptr;
}
