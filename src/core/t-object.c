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



static bool Equal_Context(const Cell* val, const Cell* arg)
{
    VarList* f1;
    VarList* f2;
    Value* key1;
    Value* key2;
    Value* var1;
    Value* var2;

    // ERROR! and OBJECT! may both be contexts, for instance, but they will
    // not compare equal just because their keys and fields are equal
    //
    if (Type_Of(arg) != Type_Of(val))
        return false;

    f1 = Cell_Varlist(val);
    f2 = Cell_Varlist(arg);

    // Short circuit equality: `same?` objects always equal
    //
    if (f1 == f2)
        return true;

    // We can't short circuit on unequal frame lengths alone, because hidden
    // fields of objects (notably `self`) do not figure into the `equal?`
    // of their public portions.

    key1 = Varlist_Keys_Head(f1);
    key2 = Varlist_Keys_Head(f2);
    var1 = Varlist_Slots_Head(f1);
    var2 = Varlist_Slots_Head(f2);

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
        if (Is_Param_Hidden(key1)) {
            key1++; var1++;
            if (IS_END(key1)) break;
            goto no_advance;
        }
        if (Is_Param_Hidden(key2)) {
            key2++; var2++;
            if (IS_END(key2)) break;
            goto no_advance;
        }

        // Do ordinary comparison of the typesets
        //
        if (Cmp_Value(key1, key2, false) != 0)
            return false;

        // The typesets contain a symbol as well which must match for
        // objects to consider themselves to be equal (but which do not
        // count in comparison of the typesets)
        //
        if (Key_Canon(key1) != Key_Canon(key2))
            return false;

        // !!! A comment here said "Use Compare_Modify_Values();"...but it
        // doesn't... it calls Cmp_Value (?)
        //
        if (Cmp_Value(var1, var2, false) != 0)
            return false;
    }

    // Either key1 or key2 is at the end here, but the other might contain
    // all hidden values.  Which is okay.  But if a value isn't hidden,
    // they don't line up.
    //
    for (; NOT_END(key1); key1++, var1++) {
        if (not Is_Param_Hidden(key1))
            return false;
    }
    for (; NOT_END(key2); key2++, var2++) {
        if (not Is_Param_Hidden(key2))
            return false;
    }

    return true;
}


static void Append_To_Context(VarList* context, Value* arg)
{
    // Can be a word:
    if (Any_Word(arg)) {
        if (0 == Find_Canon_In_Context(context, VAL_WORD_CANON(arg), true)) {
            Expand_Context(context, 1); // copy word table also
            Append_Context(context, nullptr, Cell_Word_Symbol(arg));
            // default of Append_Context is that arg's value is void
        }
        return;
    }

    if (not Is_Block(arg))
        fail (Error_Invalid(arg));

    // Process word/value argument block:

    Cell* item = Cell_List_At(arg);

    // Can't actually fail() during a collect, so make sure any errors are
    // set and then jump to a Collect_End()
    //
    Error* error = nullptr;

    struct Reb_Collector collector;
    Collect_Start(&collector, COLLECT_ANY_WORD | COLLECT_AS_TYPESET);

    // Leave the [0] slot blank while collecting (ROOTKEY/ROOTPARAM), but
    // valid (but "unreadable") bits so that the copy will still work.
    //
    Init_Unreadable(Array_Head(BUF_COLLECT));
    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, 1);

    // Setup binding table with obj words.  Binding table is empty so don't
    // bother checking for duplicates.
    //
    Collect_Context_Keys(&collector, context, false);

    // Examine word/value argument block

    Cell* word;
    for (word = item; NOT_END(word); word += 2) {
        if (!Is_Word(word) && !Is_Set_Word(word)) {
            error = Error_Invalid_Core(word, VAL_SPECIFIER(arg));
            goto collect_end;
        }

        Symbol* canon = VAL_WORD_CANON(word);

        if (Try_Add_Binder_Index(
            &collector.binder, canon, Array_Len(BUF_COLLECT))
        ){
            //
            // Wasn't already collected...so we added it...
            //
            Expand_Flex_Tail(BUF_COLLECT, 1);
            Init_Typeset(
                Array_Last(BUF_COLLECT),
                TS_VALUE, // !!! Currently ignored
                Cell_Word_Symbol(word)
            );
        }
        if (IS_END(word + 1))
            break; // fix bug#708
    }

    Term_Array_Len(BUF_COLLECT, Array_Len(BUF_COLLECT));

    // Append new words to obj
    //
    REBLEN len; // goto crosses initialization
    len = Varlist_Len(context) + 1;
    Expand_Context(context, Array_Len(BUF_COLLECT) - len);

    Cell* collect_key;
    for (
        collect_key = Array_At(BUF_COLLECT, len);
        NOT_END(collect_key);
        ++collect_key
    ){
        assert(Is_Typeset(collect_key));
        Append_Context(context, nullptr, Key_Symbol(collect_key));
    }

    // Set new values to obj words
    for (word = item; NOT_END(word); word += 2) {
        REBLEN i = Get_Binder_Index_Else_0(
            &collector.binder, VAL_WORD_CANON(word)
        );
        assert(i != 0);

        Value* key = Varlist_Key(context, i);
        Value* var = Varlist_Slot(context, i);

        if (Get_Cell_Flag(var, PROTECTED)) {
            error = Error_Protected_Key(key);
            goto collect_end;
        }

        if (Is_Param_Hidden(key)) {
            error = Error_Hidden_Raw();
            goto collect_end;
        }

        if (IS_END(word + 1)) {
            Init_Trash(var);
            break; // fix bug#708
        }
        else {
            Derelativize(var, &word[1], VAL_SPECIFIER(arg));
        }
    }

collect_end:
    Collect_End(&collector);

    if (error != nullptr)
        fail (error);
}


//
//  CT_Context: C
//
REBINT CT_Context(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode < 0) return -1;
    return Equal_Context(a, b) ? 1 : 0;
}


//
//  MAKE_Context: C
//
// !!! MAKE functions currently don't have an explicit protocol for
// thrown values.  So out just might be set as thrown.  Review.
//
Bounce MAKE_Context(Value* out, enum Reb_Kind kind, const Value* arg)
{
    if (kind == TYPE_FRAME) {
        //
        // !!! The feature of MAKE FRAME! from a VARARGS! would be interesting
        // as a way to support usermode authoring of things like MATCH.
        // For now just support ACTION! (or path/word to specify an action)
        //
        StackIndex lowest_stackindex = TOP_INDEX;

        Symbol* opt_label;
        if (Get_If_Word_Or_Path_Throws(
            out,
            &opt_label,
            arg,
            SPECIFIED,
            true // push_refinements, don't specialize ACTION! if PATH!
        )){
            return out; // !!! no explicit Throws() protocol, review
        }

        if (not Is_Action(out))
            fail (Error_Bad_Make(kind, arg));

        VarList* exemplar = Make_Managed_Context_For_Action_May_Fail(
            out,  // being used here as input (e.g. the ACTION!)
            lowest_stackindex,  // will weave in the refinements pushed
            nullptr  // no binder needed, not running any code
        );

        // See notes in %c-specialize.c about the special encoding used to
        // put /REFINEMENTs in refinement slots (instead of true/false/null)
        // to preserve the order of execution.
        //
        return Init_Frame(out, exemplar);
    }

    if (kind == TYPE_OBJECT && Is_Block(arg)) {
        //
        // Simple object creation with no evaluation, so all values are
        // handled "as-is".  Should have a spec block and a body block.

        return MAKE_With_Parent(
            out,
            TYPE_OBJECT,
            arg,
            nullptr  // no parent
        );
    }

    // make error! [....]
    //
    // arg is block/string, but let Make_Error_Object_Throws do the
    // type checking.
    //
    if (kind == TYPE_ERROR) {
        if (Make_Error_Object_Throws(out, arg))
            return BOUNCE_THROWN;

        return out;
    }

    // `make object! 10` - currently not prohibited for any context type
    //
    if (Any_Number(arg)) {
        //
        // !!! Temporary!  Ultimately SELF will be a user protocol.
        // We use Make_Selfish_Context while MAKE is filling in for
        // what will be responsibility of the generators, just to
        // get "completely fake SELF" out of index slot [0]
        //
        VarList* context = Make_Selfish_Context_Detect_Managed(
            kind, // type
            END_NODE, // values to scan for toplevel set-words (empty)
            nullptr  // parent
        );

        // !!! Allocation when SELF is not the responsibility of MAKE
        // will be more basic and look like this.
        //
        /*
        REBINT n = Int32s(arg, 0);
        context = Alloc_Context(kind, n);
        RESET_CELL(Varlist_Archetype(context), target);
        CTX_SPEC(context) = nullptr;
        CTX_BODY(context) = nullptr; */

        return Init_Any_Context(out, kind, context);
    }

    // make object! map!
    if (Is_Map(arg)) {
        VarList* c = Alloc_Context_From_Map(VAL_MAP(arg));
        return Init_Any_Context(out, kind, c);
    }

    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Context: C
//
Bounce TO_Context(Value* out, enum Reb_Kind kind, const Value* arg)
{
    if (kind == TYPE_ERROR) {
        //
        // arg is checked to be block or string
        //
        if (Make_Error_Object_Throws(out, arg))
            fail (Error_No_Catch_For_Throw(out));

        return out;
    }

    if (kind == TYPE_OBJECT) {
        //
        // !!! Contexts hold canon values now that are typed, this init
        // will assert--a TO conversion would thus need to copy the varlist
        //
        return Init_Object(out, Cell_Varlist(arg));
    }

    fail (Error_Bad_Make(kind, arg));
}


//
//  PD_Context: C
//
Bounce PD_Context(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    VarList* c = Cell_Varlist(pvs->out);

    if (not Is_Word(picker))
        return BOUNCE_UNHANDLED;

    const bool always = false;
    REBLEN n = Find_Canon_In_Context(c, VAL_WORD_CANON(picker), always);

    if (n == 0)
        return nullptr;

    if (opt_setval) {
        FAIL_IF_READ_ONLY_CONTEXT(c);

        if (Get_Cell_Flag(Varlist_Slot(c, n), PROTECTED))
            fail (Error_Protected_Word_Raw(picker));
    }

    pvs->u.ref.cell = Varlist_Slot(c, n);
    pvs->u.ref.specifier = SPECIFIED;
    return BOUNCE_REFERENCE;
}


//
//  meta-of: native [
//
//  {Get a reference to the "meta" context associated with a value.}
//
//      return: [~null~ any-context!]
//      value [<maybe> action! any-context!]
//  ]
//
DECLARE_NATIVE(META_OF)
//
// See notes accompanying the `meta` field in the StubStruct definition.
{
    INCLUDE_PARAMS_OF_META_OF;

    Value* v = ARG(VALUE);

    VarList* meta;
    if (Is_Action(v))
        meta = VAL_ACT_META(v);
    else {
        assert(Any_Context(v));
        meta = MISC(Cell_Varlist(v)).meta;
    }

    if (not meta)
        return nullptr;

    RETURN (Varlist_Archetype(meta));
}


//
//  set-meta: native [
//
//  {Set "meta" object associated with all references to a value.}
//
//      return: [~null~ any-context!]
//      value [action! any-context!]
//      meta [~null~ any-context!]
//  ]
//
DECLARE_NATIVE(SET_META)
//
// See notes accompanying the `meta` field in the StubStruct definition.
{
    INCLUDE_PARAMS_OF_SET_META;

    VarList* meta;
    if (Any_Context(ARG(META))) {
        if (VAL_BINDING(ARG(META)) != UNBOUND)
            fail ("SET-META can't store context bindings, must be unbound");

        meta = Cell_Varlist(ARG(META));
    }
    else {
        assert(Is_Nulled(ARG(META)));
        meta = nullptr;
    }

    Value* v = ARG(VALUE);

    if (Is_Action(v))
        MISC(VAL_ACT_PARAMLIST(v)).meta = meta;
    else {
        assert(Any_Context(v));
        MISC(Cell_Varlist(v)).meta = meta;
    }

    if (not meta)
        return nullptr;

    RETURN (Varlist_Archetype(meta));
}


//
//  Copy_Context_Core_Managed: C
//
// Copying a generic context is not as simple as getting the original varlist
// and duplicating that.  For instance, a "live" FRAME! context (e.g. one
// which is created by a function call on the stack) has to have its "vars"
// (the args and locals) copied from the chunk stack.  Several other things
// have to be touched up to ensure consistency of the rootval and the
// relevant ->link and ->misc fields in the series node.
//
VarList* Copy_Context_Core_Managed(VarList* original, REBU64 types)
{
    assert(Not_Flex_Info(original, INACCESSIBLE));

    Array* varlist = Make_Arr_For_Copy(
        Varlist_Len(original) + 1,
        SERIES_MASK_CONTEXT | NODE_FLAG_MANAGED,
        nullptr // original_array, N/A because LINK()/MISC() used otherwise
    );
    Value* dest = KNOWN(Array_Head(varlist)); // all context vars are SPECIFIED

    // The type information and fields in the rootvar (at head of the varlist)
    // get filled in with a copy, but the varlist needs to be updated in the
    // copied rootvar to the one just created.
    //
    Copy_Cell(dest, Varlist_Archetype(original));
    dest->payload.any_context.varlist = varlist;

    ++dest;

    // Now copy the actual vars in the context, from wherever they may be
    // (might be in an array, or might be in the chunk stack for FRAME!)
    //
    Value* src = Varlist_Slots_Head(original);
    for (; NOT_END(src); ++src, ++dest)
        Move_Var(dest, src); // keep ARG_MARKED_CHECKED

    Term_Array_Len(varlist, Varlist_Len(original) + 1);

    VarList* copy = CTX(varlist); // now a well-formed context

    // Reuse the keylist of the original.  (If the context of the source or
    // the copy are expanded, the sharing is unlinked and a copy is made).
    // This goes into the ->link field of the Stub node.
    //
    Tweak_Keylist_Of_Varlist_Shared(copy, Keylist_Of_Varlist(original));

    // A FRAME! in particular needs to know if it points back to a stack
    // frame.  The pointer is NULLed out when the stack level completes.
    // If we're copying a frame here, we know it's not running.
    //
    if (CTX_TYPE(original) == TYPE_FRAME)
        MISC(varlist).meta = nullptr;
    else {
        // !!! Should the meta object be copied for other context types?
        // Deep copy?  Shallow copy?  Just a reference to the same object?
        //
        MISC(varlist).meta = nullptr;
    }

    if (types != 0) {
        Clonify_Values_Len_Managed(
            Varlist_Slots_Head(copy),
            SPECIFIED,
            Varlist_Len(copy),
            types
        );
    }

    return copy;
}


//
//  MF_Context: C
//
void MF_Context(Molder* mo, const Cell* v, bool form)
{
    Binary* out = mo->utf8flex;

    VarList* c = Cell_Varlist(v);

    // Prevent endless mold loop:
    //
    if (Find_Pointer_In_Flex(TG_Mold_Stack, c) != NOT_FOUND) {
        if (not form) {
            Begin_Non_Lexical_Mold(mo, v); // If molding, get #[object! etc.
            Append_Codepoint(out, '[');
        }
        Append_Unencoded(out, "...");

        if (not form) {
            Append_Codepoint(out, ']');
            End_Non_Lexical_Mold(mo);
        }
        return;
    }
    Push_Pointer_To_Flex(TG_Mold_Stack, c);

    if (form) {
        //
        // Mold all words and their values:
        //
        Value* key = Varlist_Keys_Head(c);
        Value* var = Varlist_Slots_Head(c);
        bool had_output = false;
        for (; NOT_END(key); key++, var++) {
            if (not Is_Param_Hidden(key)) {
                had_output = true;
                Emit(mo, "N: V\n", Key_Symbol(key), var);
            }
        }

        // Remove the final newline...but only if WE added to the buffer
        //
        if (had_output) {
            Set_Flex_Len(out, Flex_Len(out) - 1);
            Term_Non_Array_Flex(out);
        }

        Drop_Pointer_From_Flex(TG_Mold_Stack, c);
        return;
    }

    // Otherwise we are molding

    Begin_Non_Lexical_Mold(mo, v);

    Append_Codepoint(out, '[');

    // !!! New experimental Ren-C code for the [[spec][body]] format of the
    // non-evaluative MAKE OBJECT!.

    // First loop: spec block.  This is difficult because unlike functions,
    // objects are dynamically modified with new members added.  If the spec
    // were captured with strings and other data in it as separate from the
    // "keylist" information, it would have to be updated to reflect newly
    // added fields in order to be able to run a corresponding MAKE OBJECT!.
    //
    // To get things started, we aren't saving the original spec that made
    // the object...but regenerate one from the keylist.  If this were done
    // with functions, they would "forget" their help strings in MOLDing.

    Value* keys_head = Varlist_Keys_Head(c);
    Value* vars_head = Varlist_Slots_Head(Cell_Varlist(v));

    mo->indent++;

    Value* key = keys_head;
    Value* var = vars_head;

    for (; NOT_END(key); ++key, ++var) {
        if (Is_Param_Hidden(key))
            continue;

        New_Indented_Line(mo);

        Symbol* symbol = Key_Symbol(key);
        Append_Utf8_Utf8(out, Symbol_Head(symbol), Symbol_Size(symbol));

        Append_Unencoded(out, ": ");

        if (Is_Nulled(var))
            Append_Unencoded(out, "~null~");
        else
            Mold_Value(mo, var);
    }

    mo->indent--;
    New_Indented_Line(mo);
    Append_Codepoint(out, ']');

    End_Non_Lexical_Mold(mo);

    Drop_Pointer_From_Flex(TG_Mold_Stack, c);
}


//
//  Context_Common_Action_Maybe_Unhandled: C
//
// Similar to Series_Common_Action_Maybe_Unhandled().  Introduced because
// PORT! wants to act like a context for some things, but if you ask an
// ordinary object if it's OPEN? it doesn't know how to do that.
//
Bounce Context_Common_Action_Maybe_Unhandled(
    Level* level_,
    Value* verb
){
    Value* value = D_ARG(1);
    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;

    VarList* c = Cell_Varlist(value);

    switch (Cell_Word_Id(verb)) {

    case SYM_REFLECT: {
        Option(SymId) property = Cell_Word_Id(arg);
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH: // !!! Should this be legal?
            return Init_Integer(OUT, Varlist_Len(c));

        case SYM_TAIL_Q: // !!! Should this be legal?
            return Init_Logic(OUT, Varlist_Len(c) == 0);

        case SYM_WORDS:
            //
            // !!! For FRAME!, it is desirable to know the parameter classes
            // and to know what's a local vs. a refinement, etc.  This is
            // the intersection of some "new" stuff with some crufty R3-Alpha
            // reflection abilities.
            //
            if (Is_Frame(value))
                return Init_Block(
                    OUT,
                    List_Func_Words(ACT_ARCHETYPE(ACT(Keylist_Of_Varlist(c))), true)
                );

            return Init_Block(OUT, Context_To_Array(c, 1));

        case SYM_VALUES:
            return Init_Block(OUT, Context_To_Array(c, 2));

        case SYM_BODY:
            return Init_Block(OUT, Context_To_Array(c, 3));

        // Noticeably not handled by average objects: SYM_OPEN_Q (`open?`)

        default:
            break;
        }

        break; }

    default:
        break;
    }

    return BOUNCE_UNHANDLED;
}


//
//  REBTYPE: C
//
// Handles object!, module!, and error! datatypes.
//
REBTYPE(Context)
{
    Bounce bounce = Context_Common_Action_Maybe_Unhandled(level_, verb);
    if (bounce != BOUNCE_UNHANDLED)
        return bounce;

    Value* value = D_ARG(1);
    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;

    VarList* c = Cell_Varlist(value);

    switch (Cell_Word_Id(verb)) {

    case SYM_REFLECT: {
        Option(SymId) sym = Cell_Word_Id(arg);
        if (Type_Of(value) != TYPE_FRAME)
            break;

        Level* L = Level_Of_Varlist_May_Fail(c);

        switch (sym) {
          case SYM_FILE: {
            Option(String*) file = File_Of_Level(L);
            if (not file)
                return nullptr;
            return Init_File(OUT, unwrap file); }

          case SYM_LINE: {
            LineNumber line = LVL_LINE(L);
            if (line == 0)
                return nullptr;
            return Init_Integer(OUT, line); }

          case SYM_LABEL: {
            if (not L->opt_label)
                return nullptr;
            return Init_Word(OUT, L->opt_label); }

          case SYM_NEAR:
            return Init_Near_For_Frame(OUT, L);

          case SYM_ACTION: {
            return Init_Action_Maybe_Bound(
                OUT,
                value->payload.any_context.phase, // archetypal, so no binding
                value->extra.binding // e.g. where to return for a RETURN
            ); }

          case SYM_PARENT: {
            //
            // Only want action frames (though `pending? = true` ones count).
            //
            assert(LVL_PHASE_OR_DUMMY(L) != PG_Dummy_Action); // not exposed
            Level* parent = L;
            while ((parent = parent->prior) != BOTTOM_LEVEL) {
                if (not Is_Action_Level(parent))
                    continue;
                if (LVL_PHASE_OR_DUMMY(parent) == PG_Dummy_Action)
                    continue;

                VarList* ctx_parent = Varlist_For_Level_May_Manage(parent);
                RETURN (Varlist_Archetype(ctx_parent));
            }
            return nullptr; }

          default:
            break;
        }
        fail (Error_Cannot_Reflect(Type_Of(value), arg)); }


      case SYM_APPEND:
        FAIL_IF_ERROR(arg);

        if (Is_Nulled(arg) or Is_Blank(arg))
            RETURN (value); // don't fail on read only if it would be a no-op

        FAIL_IF_READ_ONLY_CONTEXT(c);
        if (not Is_Object(value) and not Is_Module(value))
            fail (Error_Illegal_Action(Type_Of(value), verb));
        Append_To_Context(c, arg);
        RETURN (value);

      case SYM_COPY: { // Note: words are not copied and bindings not changed!
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PARAM(VALUE));

        if (Bool_ARG(PART)) {
            UNUSED(ARG(LIMIT));
            fail (Error_Bad_Refines_Raw());
        }

        REBU64 types;
        if (Bool_ARG(TYPES)) {
            if (Is_Datatype(ARG(KINDS)))
                types = FLAGIT_KIND(CELL_DATATYPE_TYPE(ARG(KINDS)));
            else
                types = Cell_Typeset_Bits(ARG(KINDS));
        }
        else if (Bool_ARG(DEEP))
            types = TS_STD_SERIES;
        else
            types = 0;

        return Init_Any_Context(
            OUT,
            Type_Of(value),
            Copy_Context_Core_Managed(c, types)
        ); }

      case SYM_SELECT:
      case SYM_FIND: {
        if (not Is_Word(arg))
            return nullptr;

        REBLEN n = Find_Canon_In_Context(c, VAL_WORD_CANON(arg), false);
        if (n == 0)
            return nullptr;

        if (Cell_Word_Id(verb) == SYM_FIND)
            return Init_Trash(OUT); // TRUE would obscure non-LOGIC! result

        RETURN (Varlist_Slot(c, n)); }

      default:
        break;
    }

    fail (Error_Illegal_Action(Type_Of(value), verb));
}


//
//  construct: native [
//
//  "Creates an ANY-CONTEXT! instance"
//
//      body [block!]
//          "keys and values defining instance contents (bindings modified)"
//      /only "Values are kept as-is"
//      /with
//      other [any-context!]
//  ]
//
DECLARE_NATIVE(CONSTRUCT)
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

    Value* body = ARG(BODY);

    // This parallels the code originally in CONSTRUCT.  Run it if the /ONLY
    // refinement was passed in.
    //
    if (Bool_ARG(ONLY)) {
        Init_Object(
            OUT,
            Construct_Context_Managed(
                TYPE_OBJECT,
                Cell_List_At(body),
                VAL_SPECIFIER(body),
                Bool_ARG(WITH) ? Cell_Varlist(ARG(OTHER)) : nullptr
            )
        );
        return OUT;
    }

    if (Bool_ARG(WITH))
        return MAKE_With_Parent(OUT, Type_Of(ARG(OTHER)), body, ARG(OTHER));

    return MAKE_With_Parent(OUT, TYPE_OBJECT, body, nullptr);
}


//
//  MAKE_With_Parent: C
//
// !!! Hack to try and undo awkward interim state for object construction the
// bootstrap executable had when it was snapshotted.
//
Bounce MAKE_With_Parent(
    Value* out,
    enum Reb_Kind target,
    const Value* body,
    Option(const Value*) spec  // the parent
){
    VarList* parent = spec ? Cell_Varlist(unwrap spec) : nullptr;

    VarList* context;

    if (
        (target == TYPE_OBJECT or target == TYPE_MODULE)
        and (Is_Block(body) or Is_Blank(body))
    ){
        // First we scan the object for top-level set words in
        // order to make an appropriately sized context.  Then
        // we put it into an object in OUT to GC protect it.
        //
        context = Make_Selfish_Context_Detect_Managed(
            target, // type
            // scan for toplevel set-words
            Is_Blank(body)
                ? cast(const Cell*, END_NODE) // gcc/g++ 2.95 needs (bug)
                : Cell_List_At(body),
            parent
        );
        Init_Object(out, context);

        if (!Is_Blank(body)) {
            //
            // !!! This binds the actual body data, not a copy of it.  See
            // Virtual_Bind_Deep_To_New_Context() for future directions.
            //
            Bind_Values_Deep(Cell_List_At(body), context);

            DECLARE_VALUE (temp);
            if (Eval_List_At_Throws(temp, body)) {
                Copy_Cell(out, temp);
                return BOUNCE_THROWN; // evaluation result ignored unless thrown
            }
        }

        return out;
    }

    // "multiple inheritance" case when both spec and body are objects.
    //
    // !!! As with most R3-Alpha concepts, this needs review.
    //
    if ((target == TYPE_OBJECT) && parent && Is_Object(body)) {
        //
        // !!! Again, the presumption that the result of a merge is to
        // be selfish should not be hardcoded in the C, but part of
        // the generator choice by the person doing the derivation.
        //
        context = Merge_Contexts_Selfish_Managed(parent, Cell_Varlist(body));
        return Init_Object(out, context);
    }

    fail ("Unsupported MAKE arguments");
}
