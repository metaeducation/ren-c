/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  t-object.c
**  Summary: object datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


static REBOOL Same_Object(REBVAL *val, REBVAL *arg)
{
    if (
        VAL_TYPE(arg) == VAL_TYPE(val) &&
        //VAL_CONTEXT_SPEC(val) == VAL_CONTEXT_SPEC(arg) &&
        VAL_FRAME(val) == VAL_FRAME(arg)
    ) return TRUE;
    return FALSE;
}


static REBOOL Equal_Object(REBVAL *val, REBVAL *arg)
{
    REBFRM *f1;
    REBFRM *f2;
    REBVAL *key1;
    REBVAL *key2;
    REBVAL *var1;
    REBVAL *var2;

    // ERROR! and OBJECT! may both be contexts, for instance, but they will
    // not compare equal just because their keys and fields are equal
    //
    if (VAL_TYPE(arg) != VAL_TYPE(val)) return FALSE;

    f1 = VAL_FRAME(val);
    f2 = VAL_FRAME(arg);

    // Short circuit equality: `same?` objects always equal
    //
    if (f1 == f2) return TRUE;

    // We can't short circuit on unequal frame lengths alone, because hidden
    // fields of objects (notably `self`) do not figure into the `equal?`
    // of their public portions.

    key1 = FRAME_KEYS_HEAD(f1);
    key2 = FRAME_KEYS_HEAD(f2);
    var1 = FRAME_VARS_HEAD(f1);
    var2 = FRAME_VARS_HEAD(f2);

    // Compare each entry, in order.  This order dependence suggests that
    // an object made with `make object! [a: 1 b: 2]` will not compare equal
    // to `make object! [b: 1 a: 2]`.  Although Rebol does not allow
    // positional picking out of objects, it does allow positional setting
    // currently (which it likely should not), hence they are functionally
    // distinct for now.  Yet those two should probably be `equal?`.
    //
    for (; NOT_END(key1) && NOT_END(key2); key1++, key2++, var1++, var2++) {
    no_advance:
        //
        // Hidden vars shouldn't affect the comparison.
        //
        if (VAL_GET_EXT(key1, EXT_WORD_HIDE)) {
            key1++; var1++;
            if (IS_END(key1)) break;
            goto no_advance;
        }
        if (VAL_GET_EXT(key2, EXT_WORD_HIDE)) {
            key2++; var2++;
            if (IS_END(key2)) break;
            goto no_advance;
        }

        // Do ordinary comparison of the typesets
        //
        if (Cmp_Value(key1, key2, FALSE) != 0)
            return FALSE;

        // The typesets contain a symbol as well which must match for
        // objects to consider themselves to be equal (but which do not
        // count in comparison of the typesets)
        //
        if (VAL_TYPESET_CANON(key1) != VAL_TYPESET_CANON(key2))
            return FALSE;

        // !!! A comment here said "Use Compare_Modify_Values();"...but it
        // doesn't... it calls Cmp_Value (?)
        //
        if (Cmp_Value(var1, var2, FALSE) != 0)
            return FALSE;
    }

    // Either key1 or key2 is at the end here, but the other might contain
    // all hidden values.  Which is okay.  But if a value isn't hidden,
    // they don't line up.
    //
    for (; NOT_END(key1); key1++, var1++) {
        if (!VAL_GET_EXT(key1, EXT_WORD_HIDE))
            return FALSE;
    }
    for (; NOT_END(key2); key2++, var2++) {
        if (!VAL_GET_EXT(key2, EXT_WORD_HIDE))
            return FALSE;
    }

    return TRUE;
}


static void Append_To_Context(REBFRM *frame, REBVAL *arg)
{
    REBCNT i, len;
    REBVAL *word;
    REBVAL *key;
    REBINT *binds; // for binding table

    // Can be a word:
    if (ANY_WORD(arg)) {
        if (!Find_Word_Index(frame, VAL_WORD_SYM(arg), TRUE)) {
            Expand_Frame(frame, 1, 1); // copy word table also
            Append_Frame(frame, 0, VAL_WORD_SYM(arg));
            // val is UNSET
        }
        return;
    }

    if (!IS_BLOCK(arg)) fail (Error_Invalid_Arg(arg));

    // Process word/value argument block:
    arg = VAL_ARRAY_AT(arg);

    // Use binding table
    binds = WORDS_HEAD(Bind_Table);

    Collect_Keys_Start(BIND_ALL);

    // Setup binding table with obj words.  Binding table is empty so don't
    // bother checking for duplicates.
    //
    Collect_Context_Keys(frame, FALSE);

    // Examine word/value argument block
    for (word = arg; NOT_END(word); word += 2) {
        REBCNT canon;

        if (!IS_WORD(word) && !IS_SET_WORD(word))
            fail (Error_Invalid_Arg(word));

        canon = VAL_WORD_CANON(word);

        if (binds[canon] == 0) {
            //
            // Not already collected, so add it...
            //
            binds[canon] = ARRAY_LEN(BUF_COLLECT);
            EXPAND_SERIES_TAIL(ARRAY_SERIES(BUF_COLLECT), 1);
            Val_Init_Typeset(
                ARRAY_LAST(BUF_COLLECT), ALL_64, VAL_WORD_SYM(word)
            );
        }
        if (IS_END(word + 1)) break; // fix bug#708
    }

    TERM_ARRAY(BUF_COLLECT);

    // Append new words to obj
    //
    len = FRAME_LEN(frame) + 1;
    Expand_Frame(frame, ARRAY_LEN(BUF_COLLECT) - len, 1);
    for (key = ARRAY_AT(BUF_COLLECT, len); NOT_END(key); key++) {
        assert(IS_TYPESET(key));
        Append_Frame(frame, NULL, VAL_TYPESET_SYM(key));
    }

    // Set new values to obj words
    for (word = arg; NOT_END(word); word += 2) {
        REBVAL *key;
        REBVAL *var;

        i = binds[VAL_WORD_CANON(word)];
        var = FRAME_VAR(frame, i);
        key = FRAME_KEY(frame, i);

        if (VAL_GET_EXT(key, EXT_WORD_LOCK))
            fail (Error_Protected_Key(key));

        if (VAL_GET_EXT(key, EXT_WORD_HIDE))
            fail (Error(RE_HIDDEN));

        if (IS_END(word + 1)) SET_NONE(var);
        else *var = word[1];

        if (IS_END(word + 1)) break; // fix bug#708
    }

    // release binding table
    Collect_Keys_End();
}


static REBFRM *Trim_Frame(REBFRM *frame)
{
    REBVAL *var;
    REBCNT copy_count = 0;
    REBFRM *frame_new;
    REBVAL *var_new;
    REBVAL *key;
    REBVAL *key_new;

    // First pass: determine size of new frame to create by subtracting out
    // any UNSET!, NONE!, or hidden fields
    //
    key = FRAME_KEYS_HEAD(frame);
    var = FRAME_VARS_HEAD(frame);
    for (; NOT_END(var); var++, key++) {
        if (VAL_TYPE(var) > REB_NONE && !VAL_GET_EXT(key, EXT_WORD_HIDE))
            copy_count++;
    }

    // Create new frame based on the size found
    //
    frame_new = Alloc_Frame(copy_count);
    VAL_CONTEXT_SPEC(FRAME_CONTEXT(frame_new)) = NULL;
    VAL_CONTEXT_BODY(FRAME_CONTEXT(frame_new)) = NULL;

    // Second pass: copy the values that were not skipped in the first pass
    //
    key = FRAME_KEYS_HEAD(frame);
    var = FRAME_VARS_HEAD(frame);
    var_new = FRAME_VARS_HEAD(frame_new);
    key_new = FRAME_KEYS_HEAD(frame_new);
    for (; NOT_END(var); var++, key++) {
        if (VAL_TYPE(var) > REB_NONE && !VAL_GET_EXT(key, EXT_WORD_HIDE)) {
            *var_new++ = *var;
            *key_new++ = *key;
        }
    }

    // Terminate the new frame
    //
    SET_END(var_new);
    SET_END(key_new);
    SET_ARRAY_LEN(FRAME_VARLIST(frame_new), copy_count + 1);
    SET_ARRAY_LEN(FRAME_KEYLIST(frame_new), copy_count + 1);

    return frame_new;
}


//
//  CT_Object: C
//
REBINT CT_Object(REBVAL *a, REBVAL *b, REBINT mode)
{
    if (mode < 0) return -1;
    if (mode == 3) return Same_Object(a, b);
    return Equal_Object(a, b);
}


//
//  CT_Frame: C
//
REBINT CT_Frame(REBVAL *a, REBVAL *b, REBINT mode)
{
    if (mode < 0) return -1;
    return VAL_SERIES(a) == VAL_SERIES(b);
}



//
//  MT_Object: C
//
REBFLG MT_Object(REBVAL *out, REBVAL *data, enum Reb_Kind type)
{
    REBFRM *frame;
    if (!IS_BLOCK(data)) return FALSE;

    frame = Construct_Frame(type, VAL_ARRAY_AT(data), FALSE, NULL);

    Val_Init_Context(out, type, frame, NULL, NULL);

    if (type == REB_ERROR) {
        REBVAL result;
        if (Make_Error_Object_Throws(&result, out)) {
            *out = result;
            return FALSE;
        }
        assert(IS_ERROR(&result));
        *out = result;
    }
    return TRUE;
}


//
//  PD_Object: C
//
REBINT PD_Object(REBPVS *pvs)
{
    REBCNT n;
    REBFRM *frame = VAL_FRAME(pvs->value);

    if (IS_WORD(pvs->select)) {
        n = Find_Word_Index(frame, VAL_WORD_SYM(pvs->select), FALSE);
    }
    else
        return PE_BAD_SELECT;

    // !!! Can Find_Word_Index give back an index longer than the frame?!
    // There was a check here.  Adding a test for now, look into it.
    //
    assert(n <= FRAME_LEN(frame));
    if (n == 0 || n > FRAME_LEN(frame))
        return PE_BAD_SELECT;

    if (
        pvs->setval
        && IS_END(pvs->path + 1)
        && VAL_GET_EXT(FRAME_KEY(frame, n), EXT_WORD_LOCK)
    ) {
        fail (Error(RE_LOCKED_WORD, pvs->select));
    }

    pvs->value = FRAME_VAR(frame, n);
    return PE_SET;
}


//
//  REBTYPE: C
// 
// Handles object!, module!, and error! datatypes.
//
REBTYPE(Object)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    REBFRM *frame;
    REBFRM *src_frame;
    enum Reb_Kind target;

    switch (action) {

    case A_MAKE:
        //
        // `make object! | error! | module!`; first parameter must be either
        // a datatype or a type exemplar.
        //
        // !!! For objects historically, the "type exemplar" parameter was
        // also the parent... this is not the long term answer.  For the
        // future, `make (make object! [a: 10]) [b: 20]` will give the same
        // result back as `make object! [b: 20]`, with parents specified to
        // generators like `o: object [<parent> p] [...]`
        //
        if (!IS_DATATYPE(value) && !ANY_CONTEXT(value))
            fail (Error_Bad_Make(VAL_TYPE(value), value));

        if (IS_DATATYPE(value)) {
            target = VAL_TYPE_KIND(value);
            src_frame = NULL;
        }
        else {
            target = VAL_TYPE(value);
            src_frame = VAL_FRAME(value);
        }

        if (target == REB_OBJECT && (IS_BLOCK(arg) || IS_NONE(arg))) {
            //
            // make object! [init]
            //
            // First we scan the object for top-level set words in
            // order to make an appropriately sized frame.  Then
            // we put it into an object in D_OUT to GC protect it.
            //
            frame = Make_Selfish_Frame_Detect(
                REB_OBJECT, // type
                NULL, // spec
                NULL, // body
                // scan for toplevel set-words
                IS_NONE(arg) ? EMPTY_BLOCK : VAL_ARRAY_AT(arg),
                src_frame // parent
            );
            Val_Init_Object(D_OUT, frame);

            // !!! This binds the actual arg data, not a copy of it
            // (functions make a copy of the body they are passed to
            // be rebound).  This seems wrong.
            //
            Bind_Values_Deep(VAL_ARRAY_AT(arg), frame);

            // Do the block into scratch space (we ignore the result,
            // unless it is thrown in which case it must be returned.
            //
            if (DO_ARRAY_THROWS(D_CELL, arg)) {
                *D_OUT = *D_CELL;
                return R_OUT_IS_THROWN;
            }

            return R_OUT;
        }

        // make parent-object object
        //
        if ((target == REB_OBJECT) && IS_OBJECT(value) && IS_OBJECT(arg)) {
            //
            // !!! Again, the presumption that the result of a merge is to
            // be selfish should not be hardcoded in the C, but part of
            // the generator choice by the person doing the derivation.
            //
            frame = Merge_Frames_Selfish(src_frame, VAL_FRAME(arg));
            MANAGE_FRAME(frame);
            Val_Init_Object(D_OUT, frame);
            return R_OUT;
        }

        // make error! [....]
        //
        // arg is block/string, but let Make_Error_Object_Throws do the
        // type checking.
        //
        if (target == REB_ERROR) {
            if (Make_Error_Object_Throws(D_OUT, arg))
                return R_OUT_IS_THROWN;
            return R_OUT;
        }

        // `make object! 10` - currently not prohibited for any context type
        //
        if (IS_NUMBER(arg)) {
            REBINT n = Int32s(arg, 0);

            // !!! Temporary!  Ultimately SELF will be a user protocol.
            // We use Make_Selfish_Frame while MAKE is filling in for
            // what will be responsibility of the generators, just to
            // get "completely fake SELF" out of index slot [0]
            //
            frame = Make_Selfish_Frame_Detect(
                target, // type
                NULL, // spec
                NULL, // body
                END_VALUE, // scan for toplevel set-words, empty
                NULL // parent
            );

            // !!! Allocation when SELF is not the responsibility of MAKE
            // will be more basic and look like this.
            //
            /* frame = Alloc_Frame(n);
            VAL_RESET_HEADER(FRAME_CONTEXT(frame), target);
            FRAME_SPEC(frame) = NULL;
            FRAME_BODY(frame) = NULL; */
            Val_Init_Context(D_OUT, target, frame, NULL, NULL);
            return R_OUT;
        }

        // make object! map!
        if (IS_MAP(arg)) {
            frame = Map_To_Object(VAL_MAP(arg));
            Val_Init_Context(D_OUT, target, frame, NULL, NULL);
            return R_OUT;
        }
        fail (Error_Bad_Make(target, arg));

    case A_TO:
        target = IS_DATATYPE(value)
            ? VAL_TYPE_KIND(value)
            : VAL_TYPE(value);

        // special conversions to object! | error! | module!
        if (target == REB_ERROR) {
            // arg is block/string, returns value
            if (Make_Error_Object_Throws(D_OUT, arg))
                return R_OUT_IS_THROWN;
            return R_OUT;
        }
        else if (target == REB_OBJECT) {
            if (IS_ERROR(arg)) {
                if (VAL_ERR_NUM(arg) < 100) fail (Error_Invalid_Arg(arg));
                frame = VAL_FRAME(arg);
                break; // returns frame
            }
            Val_Init_Object(D_OUT, VAL_FRAME(arg));
            return R_OUT;
        }
        else if (target == REB_MODULE) {
            REBVAL *item;
            if (!IS_BLOCK(arg))
                fail (Error_Bad_Make(REB_MODULE, arg));

            item = VAL_ARRAY_AT(arg);

            // Called from `make-module*`, as `to module! reduce [spec obj]`
            //
            // item[0] should be module spec
            // item[1] should be module object
            //
            if (IS_END(item) || IS_END(item + 1))
                fail (Error_Bad_Make(REB_MODULE, arg));
            if (!IS_OBJECT(item))
                fail (Error_Invalid_Arg(item));
            if (!IS_OBJECT(item + 1))
                fail (Error_Invalid_Arg(item + 1));

            // !!! We must make a shallow copy of the frame, otherwise there
            // is no way to change the frame type to module without wrecking
            // the object passed in.

            frame = Copy_Frame_Shallow_Managed(VAL_FRAME(item + 1));
            VAL_CONTEXT_SPEC(FRAME_CONTEXT(frame)) = VAL_FRAME(item);
            assert(VAL_CONTEXT_BODY(FRAME_CONTEXT(frame)) == NULL);
            VAL_RESET_HEADER(FRAME_CONTEXT(frame), REB_MODULE);

            // !!! Again, not how this should be done but... if there is a
            // self we set it to the module we just made.  (Here we tolerate
            // it if there wasn't one in the object copied from.)
            //
            {
                REBCNT self_index = Find_Word_Index(frame, SYM_SELF, TRUE);
                if (self_index != 0) {
                    assert(FRAME_KEY_CANON(frame, self_index) == SYM_SELF);
                    *FRAME_VAR(frame, self_index) = *FRAME_CONTEXT(frame);
                }
            }

            Val_Init_Module(
                D_OUT,
                frame,
                VAL_FRAME(item),
                NULL
            );
            return R_OUT;
        }
        fail (Error_Bad_Make(target, arg));

    case A_APPEND:
        FAIL_IF_PROTECTED_FRAME(VAL_FRAME(value));
        if (!IS_OBJECT(value))
            fail (Error_Illegal_Action(VAL_TYPE(value), action));
        Append_To_Context(VAL_FRAME(value), arg);
        return R_ARG1;

    case A_LENGTH:
        if (!IS_OBJECT(value))
            fail (Error_Illegal_Action(VAL_TYPE(value), action));
        SET_INTEGER(D_OUT, FRAME_LEN(VAL_FRAME(value)));
        return R_OUT;

    case A_COPY:
        // Note: words are not copied and bindings not changed!
    {
        REBU64 types = 0;
        if (D_REF(ARG_COPY_PART)) fail (Error(RE_BAD_REFINES));
        if (D_REF(ARG_COPY_DEEP)) {
            types |= D_REF(ARG_COPY_TYPES) ? 0 : TS_STD_SERIES;
        }
        if (D_REF(ARG_COPY_TYPES)) {
            arg = D_ARG(ARG_COPY_KINDS);
            if (IS_DATATYPE(arg)) types |= FLAGIT_64(VAL_TYPE_KIND(arg));
            else types |= VAL_TYPESET_BITS(arg);
        }
        frame = AS_FRAME(Copy_Array_Shallow(FRAME_VARLIST(VAL_FRAME(value))));
        FRAME_KEYLIST(frame) = FRAME_KEYLIST(VAL_FRAME(value));
        MANAGE_ARRAY(FRAME_VARLIST(frame));
        ARRAY_SET_FLAG(FRAME_VARLIST(frame), SER_FRAME);
        VAL_FRAME(FRAME_CONTEXT(frame)) = frame;
        if (types != 0) {
            Clonify_Values_Len_Managed(
                FRAME_VARS_HEAD(frame),
                FRAME_LEN(frame),
                D_REF(ARG_COPY_DEEP),
                types
            );
        }
        Val_Init_Context(
            D_OUT,
            VAL_TYPE(value),
            frame,
            VAL_CONTEXT_SPEC(value),
            VAL_CONTEXT_BODY(value)
        );
        return R_OUT;
    }

    case A_SELECT:
    case A_FIND: {
        REBINT n;

        if (!IS_WORD(arg))
            return R_NONE;

        n = Find_Word_Index(VAL_FRAME(value), VAL_WORD_SYM(arg), FALSE);

        if (n <= 0)
            return R_NONE;

        if (cast(REBCNT, n) > FRAME_LEN(VAL_FRAME(value)))
            return R_NONE;

        if (action == A_FIND) return R_TRUE;

        *D_OUT = *FRAME_VAR(VAL_FRAME(value), n);
        return R_OUT;
    }

    case A_REFLECT:
        action = What_Reflector(arg); // zero on error
        if (action == OF_SPEC) {
            //
            // !!! Rename to META-OF
            //
            // We do not return this by copy because it belongs to the user
            // constructs to manage.  If they wish to PROTECT it they may,
            // but what we give back here can be modified.
            //
            Val_Init_Object(D_OUT, VAL_CONTEXT_SPEC(value));
            return R_OUT;
        }

        // Adjust for compatibility with PICK:
        if (action == OF_VALUES) action = 2;
        else if (action == OF_BODY) action = 3;

        if (action < 1 || action > 3)
            fail (Error_Cannot_Reflect(VAL_TYPE(value), arg));

        Val_Init_Block(D_OUT, Object_To_Array(VAL_FRAME(value), action));
        return R_OUT;

    case A_TRIM:
        if (Find_Refines(call_, ALL_TRIM_REFS)) {
            // no refinements are allowed
            fail (Error(RE_BAD_REFINES));
        }
        Val_Init_Context(
            D_OUT,
            VAL_TYPE(value),
            Trim_Frame(VAL_FRAME(value)),
            VAL_CONTEXT_SPEC(value),
            VAL_CONTEXT_BODY(value)
        );
        return R_OUT;

    case A_TAIL_Q:
        if (IS_OBJECT(value)) {
            SET_LOGIC(D_OUT, FRAME_LEN(VAL_FRAME(value)) == 0);
            return R_OUT;
        }
        fail (Error_Illegal_Action(VAL_TYPE(value), action));
    }

    fail (Error_Illegal_Action(VAL_TYPE(value), action));
}
