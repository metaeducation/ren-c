//
//  File: %t-object.c
//  Summary: "object datatype"
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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"



static REBOOL Equal_Context(const RELVAL *val, const RELVAL *arg)
{
    REBCTX *f1;
    REBCTX *f2;
    REBVAL *key1;
    REBVAL *key2;
    REBVAL *var1;
    REBVAL *var2;

    // ERROR! and OBJECT! may both be contexts, for instance, but they will
    // not compare equal just because their keys and fields are equal
    //
    if (VAL_TYPE(arg) != VAL_TYPE(val)) return FALSE;

    f1 = VAL_CONTEXT(val);
    f2 = VAL_CONTEXT(arg);

    // Short circuit equality: `same?` objects always equal
    //
    if (f1 == f2) return TRUE;

    // We can't short circuit on unequal frame lengths alone, because hidden
    // fields of objects (notably `self`) do not figure into the `equal?`
    // of their public portions.

    key1 = CTX_KEYS_HEAD(f1);
    key2 = CTX_KEYS_HEAD(f2);
    var1 = CTX_VARS_HEAD(f1);
    var2 = CTX_VARS_HEAD(f2);

    // Compare each entry, in order.  This order dependence suggests that
    // an object made with `make object! [[a b][a: 1 b: 2]]` will not be equal
    // to `make object! [[b a][b: 1 a: 2]]`.  Although Rebol does not allow
    // positional picking out of objects, it does allow positional setting
    // currently (which it likely should not), hence they are functionally
    // distinct for now.  Yet those two should probably be `equal?`.
    //
    for (; NOT_END(key1) && NOT_END(key2); key1++, key2++, var1++, var2++) {
    no_advance:
        //
        // Hidden vars shouldn't affect the comparison.
        //
        if (GET_VAL_FLAG(key1, TYPESET_FLAG_HIDDEN)) {
            key1++; var1++;
            if (IS_END(key1)) break;
            goto no_advance;
        }
        if (GET_VAL_FLAG(key2, TYPESET_FLAG_HIDDEN)) {
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
        if (VAL_KEY_CANON(key1) != VAL_KEY_CANON(key2))
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
        if (NOT_VAL_FLAG(key1, TYPESET_FLAG_HIDDEN))
            return FALSE;
    }
    for (; NOT_END(key2); key2++, var2++) {
        if (NOT_VAL_FLAG(key2, TYPESET_FLAG_HIDDEN))
            return FALSE;
    }

    return TRUE;
}


static void Append_To_Context(REBCTX *context, REBVAL *arg)
{
    // Can be a word:
    if (ANY_WORD(arg)) {
        if (0 == Find_Canon_In_Context(context, VAL_WORD_CANON(arg), TRUE)) {
            Expand_Context(context, 1); // copy word table also
            Append_Context(context, 0, VAL_WORD_SPELLING(arg));
            // default of Append_Context is that arg's value is void
        }
        return;
    }

    if (!IS_BLOCK(arg)) fail (Error_Invalid_Arg(arg));

    // Process word/value argument block:

    RELVAL *item = VAL_ARRAY_AT(arg);

    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    Collect_Keys_Start(COLLECT_ANY_WORD);

    // Setup binding table with obj words.  Binding table is empty so don't
    // bother checking for duplicates.
    //
    Collect_Context_Keys(&binder, context, FALSE);

    // Examine word/value argument block

    RELVAL *word;
    for (word = item; NOT_END(word); word += 2) {
        if (!IS_WORD(word) && !IS_SET_WORD(word))
            fail (Error_Invalid_Arg_Core(word, VAL_SPECIFIER(arg)));

        REBSTR *canon = VAL_WORD_CANON(word);

        if (Try_Add_Binder_Index(&binder, canon, ARR_LEN(BUF_COLLECT))) {
            //
            // Wasn't already collected...so we added it...
            //
            EXPAND_SERIES_TAIL(AS_SERIES(BUF_COLLECT), 1);
            Init_Typeset(
                ARR_LAST(BUF_COLLECT), ALL_64, VAL_WORD_SPELLING(word)
            );
        }
        if (IS_END(word + 1)) break; // fix bug#708
    }

    TERM_ARRAY_LEN(BUF_COLLECT, ARR_LEN(BUF_COLLECT));

    // Append new words to obj
    //
    REBCNT len = CTX_LEN(context) + 1;
    Expand_Context(context, ARR_LEN(BUF_COLLECT) - len);

    RELVAL *key;
    for (key = ARR_AT(BUF_COLLECT, len); NOT_END(key); key++) {
        assert(IS_TYPESET(key));
        Append_Context(context, NULL, VAL_KEY_SPELLING(key));
    }

    // Set new values to obj words
    for (word = item; NOT_END(word); word += 2) {
        REBCNT i = Try_Get_Binder_Index(&binder, VAL_WORD_CANON(word));
        assert(i != 0);

        REBVAL *key = CTX_KEY(context, i);
        REBVAL *var = CTX_VAR(context, i);

        if (GET_VAL_FLAG(var, VALUE_FLAG_PROTECTED))
            fail (Error_Protected_Key(key));

        if (GET_VAL_FLAG(key, TYPESET_FLAG_HIDDEN))
            fail (Error_Hidden_Raw());

        if (IS_END(word + 1)) {
            SET_BLANK(var);
            break; // fix bug#708
        }
        else {
            Derelativize(var, &word[1], VAL_SPECIFIER(arg));

            // Should the VALUE_FLAG_ENFIXED be preserved here?
            //
            if (GET_VAL_FLAG(&word[1], VALUE_FLAG_ENFIXED))
                SET_VAL_FLAG(var, VALUE_FLAG_ENFIXED);

        }
    }

    // release binding table
    Collect_Keys_End(&binder);

    SHUTDOWN_BINDER(&binder);
}


static REBCTX *Trim_Context(REBCTX *context)
{
    REBVAL *var;
    REBCNT copy_count = 0;
    REBCTX *context_new;
    REBVAL *var_new;
    REBVAL *key;
    REBVAL *key_new;

    // First pass: determine size of new context to create by subtracting out
    // any void (unset fields), NONE!, or hidden fields
    //
    key = CTX_KEYS_HEAD(context);
    var = CTX_VARS_HEAD(context);
    for (; NOT_END(var); var++, key++) {
        if (VAL_TYPE(var) != REB_BLANK && NOT_VAL_FLAG(key, TYPESET_FLAG_HIDDEN))
            copy_count++;
    }

    // Create new context based on the size found
    //
    context_new = Alloc_Context(copy_count);

    // Make it pass ASSERT_CONTEXT
    VAL_RESET_HEADER(CTX_VALUE(context_new), VAL_TYPE(CTX_VALUE(context)));

    // Second pass: copy the values that were not skipped in the first pass
    //
    key = CTX_KEYS_HEAD(context);
    var = CTX_VARS_HEAD(context);
    var_new = CTX_VARS_HEAD(context_new);
    key_new = CTX_KEYS_HEAD(context_new);
    for (; NOT_END(var); var++, key++) {
        if (VAL_TYPE(var) != REB_BLANK && NOT_VAL_FLAG(key, TYPESET_FLAG_HIDDEN)) {
            Move_Value(var_new, var);
            ++var_new;
            Move_Value(key_new, key);
            ++key_new;
        }
    }

    // Terminate the new context
    //
    TERM_ARRAY_LEN(CTX_VARLIST(context_new), copy_count + 1);
    TERM_ARRAY_LEN(CTX_KEYLIST(context_new), copy_count + 1);

    return context_new;
}


//
//  CT_Context: C
//
REBINT CT_Context(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode < 0) return -1;
    return Equal_Context(a, b) ? 1 : 0;
}


//
//  MAKE_Context: C
//
void MAKE_Context(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (kind == REB_FRAME) {
        //
        // !!! Current experiment for making frames lets you give it
        // a FUNCTION! only.
        //
        if (!IS_FUNCTION(arg))
            fail (Error_Bad_Make(kind, arg));

        // In order to have the frame survive the call to MAKE and be
        // returned to the user it can't be stack allocated, because it
        // would immediately become useless.  Allocate dynamically.
        //
        Init_Any_Context(out, REB_FRAME, Make_Frame_For_Function(arg));

        // The frame's keylist is the same as the function's paramlist, and
        // the [0] canon value of that array can be used to find the
        // archetype of the function.  But if the `arg` is a RETURN with a
        // binding in the REBVAL to where to return from, that unique
        // instance information must be carried in the REBVAL of the context.
        //
        assert(VAL_BINDING(out) == VAL_BINDING(arg));
        return;
    }

    if (kind == REB_OBJECT && IS_BLANK(arg)) {
        //
        // Special case (necessary?) to return an empty object.
        //
        Init_Object(
            out,
            Construct_Context(
                REB_OBJECT,
                NULL, // head
                SPECIFIED,
                NULL
            )
        );
        return;
    }

    if (kind == REB_OBJECT && IS_BLOCK(arg)) {
        //
        // Simple object creation with no evaluation, so all values are
        // handled "as-is".  Should have a spec block and a body block.
        //
        // Note: In %r3-legacy.r, the old evaluative MAKE OBJECT! is
        // done by redefining MAKE itself, and calling the CONSTRUCT
        // generator if the make def is not the [[spec][body]] format.

        if (
            VAL_LEN_AT(arg) != 2
            || !IS_BLOCK(VAL_ARRAY_AT(arg)) // spec
            || !IS_BLOCK(VAL_ARRAY_AT(arg) + 1) // body
        ) {
            fail (Error_Bad_Make(kind, arg));
        }

        // !!! Spec block is currently ignored, but required.

        Init_Object(
            out,
            Construct_Context(
                REB_OBJECT,
                VAL_ARRAY_AT(VAL_ARRAY_AT(arg) + 1),
                VAL_SPECIFIER(arg),
                NULL // no parent
            )
        );

        return;
    }

    // make error! [....]
    //
    // arg is block/string, but let Make_Error_Object_Throws do the
    // type checking.
    //
    if (kind == REB_ERROR) {
        //
        // !!! Evaluation should not happen during a make.  FAIL should
        // be the primitive that does the evaluations, and then call
        // into this with the reduced block.
        //
        if (Make_Error_Object_Throws(out, arg))
            fail (Error_No_Catch_For_Throw(out));

        return;
    }

    // `make object! 10` - currently not prohibited for any context type
    //
    if (ANY_NUMBER(arg)) {
        //
        // !!! Temporary!  Ultimately SELF will be a user protocol.
        // We use Make_Selfish_Context while MAKE is filling in for
        // what will be responsibility of the generators, just to
        // get "completely fake SELF" out of index slot [0]
        //
        REBCTX *context = Make_Selfish_Context_Detect(
            kind, // type
            NULL, // body
            END, // values to scan for toplevel set-words (empty)
            NULL // parent
        );

        // !!! Allocation when SELF is not the responsibility of MAKE
        // will be more basic and look like this.
        //
        /*
        REBINT n = Int32s(arg, 0); 
        context = Alloc_Context(n);
        VAL_RESET_HEADER(CTX_VALUE(context), target);
        CTX_SPEC(context) = NULL;
        CTX_BODY(context) = NULL; */
        Init_Any_Context(out, kind, context);

        return;
    }

    // make object! map!
    if (IS_MAP(arg)) {
        REBCTX *context = Alloc_Context_From_Map(VAL_MAP(arg));
        Init_Any_Context(out, kind, context);
        return;
    }

    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Context: C
//
void TO_Context(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (kind == REB_ERROR) {
        //
        // arg is checked to be block or string
        //
        if (Make_Error_Object_Throws(out, arg))
            fail (Error_No_Catch_For_Throw(out));

        return;
    }

    if (kind == REB_OBJECT) {
        if (IS_ERROR(arg)) {
            if (VAL_ERR_NUM(arg) < 100)
                fail (Error_Invalid_Arg(arg)); // !!! ???
        }

        // !!! Contexts hold canon values now that are typed, this init
        // will assert--a TO conversion would thus need to copy the varlist
        //
        Init_Object(out, VAL_CONTEXT(arg));
        return;
    }

    fail (Error_Bad_Make(kind, arg));
}


//
//  PD_Context: C
//
REBINT PD_Context(REBPVS *pvs)
{
    REBCTX *c = VAL_CONTEXT(pvs->value);

    if (NOT(IS_WORD(pvs->selector)))
        fail (Error_Bad_Path_Select(pvs));

    REBCNT n = Find_Canon_In_Context(
        c, VAL_WORD_CANON(pvs->selector), FALSE
    );

    if (n == 0) {
        //
        // !!! The logic for allowing a GET-PATH! to be void if it's the last
        // lookup that fails here is hacked in, but desirable for parity
        // with the behavior of GET-WORD!
        //
        if (IS_GET_PATH(pvs->orig) && IS_END(pvs->item + 1)) {
            SET_VOID(pvs->store);
            return PE_USE_STORE;
        }
        fail (Error_Bad_Path_Select(pvs));
    }

    if (CTX_VARS_UNAVAILABLE(c))
        fail (Error_No_Relative_Raw(pvs->selector));

    if (pvs->opt_setval && IS_END(pvs->item + 1)) {
        FAIL_IF_READ_ONLY_CONTEXT(c);

        if (GET_VAL_FLAG(CTX_VAR(c, n), VALUE_FLAG_PROTECTED))
            fail (Error_Protected_Word_Raw(pvs->selector));
    }

    pvs->value = CTX_VAR(c, n);
    pvs->value_specifier = SPECIFIED;

    return PE_SET_IF_END;
}


//
//  meta-of: native [
//
//  {Get a reference to the "meta" object associated with a value.}
//
//      value [function! object! module!]
//  ]
//
REBNATIVE(meta_of)
//
// The first implementation of linking a "meta object" to another object
// originates from the original module system--where it was called the
// "module spec".  By moving it out of object REBVALs to the misc field of
// a keylist, it becomes possible to change the meta object and have that
// change seen by all references.
//
// As modules are still the first client of this meta information, it works
// a similar way.  It is mutable by all references by default, unless
// it is protected.
//
// !!! This feature is under development and expected to extend to functions
// and possibly other types of values--both as the meta information, and
// as being able to have the meta information.
{
    INCLUDE_PARAMS_OF_META_OF;

    REBVAL *value = ARG(value);

    REBCTX *meta;
    if (IS_FUNCTION(value))
        meta = VAL_FUNC_META(value);
    else {
        assert(ANY_CONTEXT(value));
        meta = VAL_CONTEXT_META(value);
    }

    if (!meta) return R_BLANK;

    Init_Object(D_OUT, meta);
    return R_OUT;
}


//
//  set-meta: native [
//
//  {Set "meta" object associated with all references to a value.}
//
//      return: [<opt>]
//      value [function! object! module!]
//      meta [object! blank!]
//  ]
//
REBNATIVE(set_meta)
//
// !!! You cannot currently put meta information onto a FRAME!, because the
// slot where the meta information would go is where the meta information
// would live for the function--since frames use a functions "paramlist"
// as their keylist.  Types taken are deliberately narrow for the moment.
{
    INCLUDE_PARAMS_OF_SET_META;

    REBCTX *meta;
    if (ANY_CONTEXT(ARG(meta))) {
        meta = VAL_CONTEXT(ARG(meta));
    }
    else {
        assert(IS_BLANK(ARG(meta)));
        meta = NULL;
    }

    REBVAL *value = ARG(value);

    if (IS_FUNCTION(value))
        AS_SERIES(VAL_FUNC_PARAMLIST(value))->link.meta = meta;
    else {
        assert(ANY_CONTEXT(value));
        INIT_CONTEXT_META(VAL_CONTEXT(value), meta);
    }

    return R_VOID;
}


//
//  Copy_Context_Core: C
//
// R3-Alpha hadn't factored out a routine to copy objects, it was just in the
// COPY action.  This is a basic factoring of that, which had the ability to
// specify what types would be copied and whether they would be done deeply.
//
REBCTX *Copy_Context_Core(REBCTX *original, REBOOL deep, REBU64 types)
{
    REBARR *varlist = Copy_Array_Shallow(CTX_VARLIST(original), SPECIFIED);
    SET_SER_FLAG(varlist, ARRAY_FLAG_VARLIST);

    // The type information and fields in the rootvar (at head of the varlist)
    // are filled in because it's a copy, but the varlist needs to be updated
    // in the copy to the one just created.
    //
    ARR_HEAD(varlist)->payload.any_context.varlist = varlist;

    REBCTX *copy = AS_CONTEXT(varlist); // now a well-formed context

    // Reuse the keylist of the original.  (If the context of the source or
    // the copy are expanded, the sharing is unlinked and a copy is made).
    //
    INIT_CTX_KEYLIST_SHARED(copy, CTX_KEYLIST(original));

    if (types != 0) {
        Clonify_Values_Len_Managed(
            CTX_VARS_HEAD(copy),
            SPECIFIED,
            CTX_LEN(copy),
            deep,
            types
        );
    }

    return copy;
}


//
//  REBTYPE: C
//
// Handles object!, module!, and error! datatypes.
//
REBTYPE(Context)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    switch (action) {
    case SYM_APPEND:
        FAIL_IF_READ_ONLY_CONTEXT(VAL_CONTEXT(value));
        if (!IS_OBJECT(value) && !IS_MODULE(value))
            fail (Error_Illegal_Action(VAL_TYPE(value), action));
        Append_To_Context(VAL_CONTEXT(value), arg);
        Move_Value(D_OUT, D_ARG(1));
        return R_OUT;

    case SYM_LENGTH:
        if (!IS_OBJECT(value))
            fail (Error_Illegal_Action(VAL_TYPE(value), action));
        SET_INTEGER(D_OUT, CTX_LEN(VAL_CONTEXT(value)));
        return R_OUT;

    case SYM_COPY: { // Note: words are not copied and bindings not changed!
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }

        REBU64 types;
        if (REF(types)) {
            if (IS_DATATYPE(ARG(kinds)))
                types = FLAGIT_KIND(VAL_TYPE_KIND(ARG(kinds)));
            else
                types = VAL_TYPESET_BITS(ARG(kinds));
        }
        else if (REF(deep))
            types = TS_STD_SERIES;
        else
            types = 0;

        Init_Any_Context(
            D_OUT,
            VAL_TYPE(value),
            Copy_Context_Core(VAL_CONTEXT(value), REF(deep), types)
        );
        return R_OUT; }

    case SYM_SELECT:
    case SYM_FIND: {
        if (!IS_WORD(arg))
            return R_BLANK;

        REBCNT n = Find_Canon_In_Context(
            VAL_CONTEXT(value), VAL_WORD_CANON(arg), FALSE
        );

        if (n == 0)
            return R_BLANK;

        if (cast(REBCNT, n) > CTX_LEN(VAL_CONTEXT(value)))
            return R_BLANK;

        if (action == SYM_FIND) return R_TRUE;

        Move_Value(D_OUT, CTX_VAR(VAL_CONTEXT(value), n));
        return R_OUT;
    }

    case SYM_REFLECT: {
        REBSYM sym = VAL_WORD_SYM(arg);
        REBCNT reflector;

        switch (sym) {
        case SYM_WORDS: reflector = 1; break;
        case SYM_VALUES: reflector = 2; break;
        case SYM_BODY: reflector = 3; break;
        default:
            fail (Error_Cannot_Reflect(VAL_TYPE(value), arg));
        }

        Init_Block(D_OUT, Context_To_Array(VAL_CONTEXT(value), reflector));
        return R_OUT; }

    case SYM_TRIM: {
        INCLUDE_PARAMS_OF_TRIM;

        UNUSED(ARG(series));

        if (
            REF(head) || REF(tail)
            || REF(auto) || REF(all) || REF(lines)
        ){
            fail (Error_Bad_Refines_Raw());
        }

        if (REF(with)) {
            UNUSED(ARG(str));
            fail (Error_Bad_Refines_Raw());
        }

        Init_Any_Context(
            D_OUT,
            VAL_TYPE(value),
            Trim_Context(VAL_CONTEXT(value))
        );
        return R_OUT; }

    case SYM_TAIL_Q:
        if (IS_OBJECT(value)) {
            SET_LOGIC(D_OUT, LOGICAL(CTX_LEN(VAL_CONTEXT(value)) == 0));
            return R_OUT;
        }
        fail (Error_Illegal_Action(VAL_TYPE(value), action));

    default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(value), action));
}


//
//  construct: native [
//
//  "Creates an ANY-CONTEXT! instance"
//
//      spec [datatype! block! any-context!]
//          "Datatype to create, specification, or parent/prototype context"
//      body [block! any-context! blank!]
//          "keys and values defining instance contents (bindings modified)"
//      /only
//          "Values are kept as-is"
//  ]
//
REBNATIVE(construct)
//
// CONSTRUCT in Ren-C is an effective replacement for what MAKE ANY-OBJECT!
// was able to do in Rebol2 and R3-Alpha.  It takes a spec that can be an
// ANY-CONTEXT! datatype, or it can be a parent ANY-CONTEXT!, or a block that
// represents a "spec".
//
// !!! This assumes you want a SELF defined.  The entire concept of SELF
// needs heavy review, but at minimum this needs an override to match the
// `<with> return` or `<with> local` for functions.
//
// !!! This mutates the bindings of the body block passed in, should it
// be making a copy instead (at least by default, perhaps with performance
// junkies saying `construct/rebind` or something like that?
{
    INCLUDE_PARAMS_OF_CONSTRUCT;

    REBVAL *spec = ARG(spec);
    REBVAL *body = ARG(body);
    REBCTX *parent = NULL;

    enum Reb_Kind target;
    REBCTX *context;

    if (IS_STRUCT(spec)) {
        //
        // !!! Compatibility for `MAKE struct [...]` from Atronix R3.  There
        // isn't any real "inheritance management" for structs but it allows
        // the re-use of the structure's field definitions, so it is a means
        // of saving on memory (?)
        //
        REBSTU *stu = Copy_Struct_Managed(VAL_STRUCT(spec));

        Move_Value(D_OUT, STU_VALUE(stu));

        // !!! Comment said "only accept value initialization"
        //
        Init_Struct_Fields(D_OUT, body);
        return R_OUT;
    }
    else if (IS_GOB(spec)) {
        //
        // !!! Compatibility for `MAKE gob [...]` or `MAKE gob NxN` from
        // R3-Alpha GUI.  Start by copying the gob (minus pane and parent),
        // then apply delta to its properties from arg.  Doesn't save memory,
        // or keep any parent linkage--could be done in user code as a copy
        // and then apply the difference.
        //
        REBGOB *gob = Make_Gob();
        *gob = *VAL_GOB(spec);
        gob->pane = NULL;
        gob->parent = NULL;

        if (!IS_BLOCK(body))
            fail (Error_Bad_Make(REB_GOB, body));

        Extend_Gob_Core(gob, body);
        SET_GOB(D_OUT, gob);
        return R_OUT;
    }
    else if (IS_EVENT(spec)) {
        //
        // !!! As with GOB!, the 2-argument form of MAKE-ing an event is just
        // a shorthand for copy-and-apply.  Could be user code.
        //
        if (!IS_BLOCK(body))
            fail (Error_Bad_Make(REB_EVENT, body));

        Move_Value(D_OUT, spec); // !!! very "shallow" clone of the event
        Set_Event_Vars(
            D_OUT,
            VAL_ARRAY_AT(body),
            VAL_SPECIFIER(body)
        );
        return R_OUT;
    }
    else if (ANY_CONTEXT(spec)) {
        parent = VAL_CONTEXT(spec);
        target = VAL_TYPE(spec);
    }
    else if (IS_DATATYPE(spec)) {
        //
        // Should this be supported, or just assume OBJECT! ?  There are
        // problems trying to create a FRAME! without a function (for
        // instance), and making an ERROR! from scratch is currently dangerous
        // as well though you can derive them.
        //
        fail (Error_Misc_Raw());
    }
    else {
        assert(IS_BLOCK(spec));
        target = REB_OBJECT;
    }

    // This parallels the code originally in CONSTRUCT.  Run it if the /ONLY
    // refinement was passed in.
    //
    if (REF(only)) {
        Init_Object(
            D_OUT,
            Construct_Context(
                REB_OBJECT,
                VAL_ARRAY_AT(body),
                VAL_SPECIFIER(body),
                parent
            )
        );
        return R_OUT;
    }

    // This code came from REBTYPE(Context) for implementing MAKE OBJECT!.
    // Now that MAKE ANY-CONTEXT! has been pulled back, it no longer does
    // any evaluation or creates SELF fields.  It also obeys the rule that
    // the first argument is an exemplar of the type to create only, bringing
    // uniformity to MAKE.
    //
    if (
        (target == REB_OBJECT || target == REB_MODULE)
        && (IS_BLOCK(body) || IS_BLANK(body))) {

        // First we scan the object for top-level set words in
        // order to make an appropriately sized context.  Then
        // we put it into an object in D_OUT to GC protect it.
        //
        context = Make_Selfish_Context_Detect(
            target, // type
            NULL, // binding
            // scan for toplevel set-words
            IS_BLANK(body)
                ? cast(const RELVAL*, END) // needed by gcc/g++ 2.95 (bug)
                : VAL_ARRAY_AT(body),
            parent
        );
        Init_Object(D_OUT, context);

        if (!IS_BLANK(body)) {
            //
            // !!! This binds the actual body data, not a copy of it
            // (functions make a copy of the body they are passed to
            // be rebound).  This seems wrong.
            //
            Bind_Values_Deep(VAL_ARRAY_AT(body), context);

            // Do the block into scratch space (we ignore the result,
            // unless it is thrown in which case it must be returned.
            //
            DECLARE_LOCAL (dummy);
            if (Do_Any_Array_At_Throws(dummy, body)) {
                Move_Value(D_OUT, dummy);
                return R_OUT_IS_THROWN;
            }
        }

        return R_OUT;
    }

    // "multiple inheritance" case when both spec and body are objects.
    //
    // !!! As with most R3-Alpha concepts, this needs review.
    //
    if ((target == REB_OBJECT) && parent && IS_OBJECT(body)) {
        //
        // !!! Again, the presumption that the result of a merge is to
        // be selfish should not be hardcoded in the C, but part of
        // the generator choice by the person doing the derivation.
        //
        context = Merge_Contexts_Selfish(parent, VAL_CONTEXT(body));
        Init_Object(D_OUT, context);
        return R_OUT;
    }

    fail (Error_Misc_Raw());
}
