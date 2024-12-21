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


//=//// CONTEXT ENUMERATION ////////////////////////////////////////////////=//
//
// All hidden parameters in the exemplar frame of an ACTION! are not shown
// on the public interface of that function.  This means type information
// is not relevant (though the type information for later phases of that
// slot may be pertinent).  So instead of type information, hidden param slots
// hold the initialization value for that position.
//
// In terms of whether the parameter is truly "hidden" from a view of a FRAME!
// with MOLD or to BIND depends on the frame's phase.  For instance, while a
// frame is running the body of an interpreted function...that phase has to
// see the locals defined for that function.  This means you can't tell from a
// frame context node pointer alone whether a key is visible...the full FRAME!
// cell--phase included--must be used.
//
// Because this logic is tedious to honor every time a context is enumerated,
// it is abstracted into an enumeration routine.
//
// !!! This enumeration does not take into account the adjusted positions of
// parameters in functions caused by partials and explicit reordering.  It
// goes in order of the frame.  It would probably be best if it went in the
// adjusted order, and if this code unified with the enumeration for ACTION!
// (so just had the evars.var be nullptr in that case).

//
//  Init_Evars: C
//
// The init initializes to one behind the enumeration, so you have to call
// Try_Advance_Evars() on even the first.
//
void Init_Evars(EVARS *e, const Cell* v) {
    Heart heart = Cell_Heart(v);

    e->lens_mode = LENS_MODE_ALL_UNSEALED;  // ensure not uninitialized

    if (heart == REB_MODULE) {  // !!! module enumeration is bad/slow [2]

  //=//// MODULE ENUMERATION //////////////////////////////////////////////=//

    // !!! Module enumeration is slow, and you should not do it often...it
    // requires walking over the global word table.  The global table gets
    // rehashed in a way that we'd have a hard time maintainining a consistent
    // enumerator state in the current design.  So for the moment we fabricate
    // an array to enumerate.  The enumeration won't see changes.

        e->index = INDEX_PATCHED;

        e->ctx = Cell_Varlist(v);

        StackIndex base = TOP_INDEX;

        Symbol** psym = Flex_Head(Symbol*, g_symbols.by_hash);
        Symbol** psym_tail = Flex_Tail(Symbol*, g_symbols.by_hash);
        for (; psym != psym_tail; ++psym) {
            if (*psym == nullptr or *psym == &g_symbols.deleted_symbol)
                continue;

            Stub* patch = Misc_Hitch(*psym);
            if (Get_Flavor_Flag(SYMBOL, *psym, MISC_IS_BIND_STUMP))
                patch = Misc_Hitch(patch);  // skip binding stump

            Stub* found = nullptr;

            for (; patch != *psym; patch = Misc_Hitch(patch)) {
                if (e->ctx == Info_Patch_Sea(patch)) {
                    found = patch;
                    break;
                }
             /*   if (g_lib_context == Info_Patch_Sea(patch))
                    found = patch;  // will match if not overridden */
            }
            if (found) {
                Init_Any_Word(PUSH(), REB_WORD, *psym);
                Tweak_Cell_Word_Index(TOP, INDEX_PATCHED);
                Tweak_Cell_Binding(TOP, found);
            }
        }

        e->wordlist = Pop_Managed_Source_From_Stack(base);
        Clear_Node_Managed_Bit(e->wordlist);  // [1]

        e->word = cast(Value*, Array_Head(e->wordlist)) - 1;
        e->word_tail = cast(Value*, Array_Tail(e->wordlist));

        Corrupt_Pointer_If_Debug(e->key_tail);
        e->var = nullptr;
        e->param = nullptr;
    }
    else {
        e->index = 0;  // will be bumped to 1

        e->ctx = Cell_Varlist(v);

        e->var = Varlist_Slots_Head(e->ctx) - 1;

        assert(Flex_Used(Keylist_Of_Varlist(e->ctx)) <= Varlist_Len(e->ctx));

        if (heart != REB_FRAME) {
            e->param = nullptr;
            e->key = Varlist_Keys(&e->key_tail, e->ctx) - 1;
        }
        else {

  //=//// FRAME ENUMERATION ///////////////////////////////////////////////=//

    // 1. It makes the most sense for unlensed frames to show the inputs only.
    //    This is because the Lens slot is used for a label when not lensed,
    //    common with antiforms.

            e->var = Varlist_Slots_Head(e->ctx) - 1;

            Phase* lens = maybe Cell_Frame_Lens(v);
            if (not lens) {  // unlensed, only inputs visible [1]
                e->lens_mode = LENS_MODE_INPUTS;
                lens = Cell_Frame_Phase(v);
            }
            else if (Is_Stub_Varlist(lens)) {
                e->lens_mode = LENS_MODE_PARTIALS;
            }
            else {
                assert(Is_Stub_Details(lens));
                if (Get_Details_Flag(cast(Details*, lens), OWNS_PARAMLIST))
                    e->lens_mode = LENS_MODE_ALL_UNSEALED;  // (func, etc.)
                else
                    e->lens_mode = LENS_MODE_INPUTS;  // (adapt, etc.)
            }

            e->param = Phase_Params_Head(lens) - 1;
            e->key = Phase_Keys(&e->key_tail, lens) - 1;
            assert(Flex_Used(Phase_Keylist(lens)) <= Phase_Num_Params(lens));
        }

        Corrupt_Pointer_If_Debug(e->wordlist);
        e->word = nullptr;
        UNUSED(e->word_tail);
    }

  #if RUNTIME_CHECKS
    ++g_num_evars_outstanding;
  #endif
}


//
//  Try_Advance_Evars: C
//
// !!! When enumerating an ordinary context, this currently does not put a
// HOLD on the context.  So running user code during the enumeration that can
// modify the object and add fields is dangerous.  The FOR-EACH variants do
// put on the hold and use a rebRescue() to make sure the hold gets removed
// in case of errors.  That becomes cheaper in the stackless model where a
// single setjmp/exception boundary can wrap an arbitrary number of stack
// levels.  Ultimately there should probably be a Shutdown_Evars().
//
// 1. A simple specialization of a function would provide a value that the
//    function should see as an argument when it runs.  But layers above that
//    will use VAR_MARKED_HIDDEN so higher abstractions will not be aware of
//    that specialized-out variable.
//
//    (Put another way: when a function copies an exemplar and uses it as its
//    own, the fact that exemplar points at the phase does not suddenly give
//    access to the private variables that would have been inaccessible before
//    the copy.  The hidden bit must be added during that copy in order to
//    honor this property.)
//
bool Try_Advance_Evars(EVARS *e) {
    if (e->word) {
        while (++e->word != e->word_tail) {
            e->var = MOD_VAR(
                cast(SeaOfVars*, e->ctx), Cell_Word_Symbol(e->word), true
            );
            if (Get_Cell_Flag(e->var, VAR_MARKED_HIDDEN))
                continue;
            e->keybuf = Cell_Word_Symbol(e->word);
            e->key = &e->keybuf;
            return true;
        }
        return false;
    }

    ++e->key;  // !! Note: keys can move if an ordinary context expands
    if (e->param)
        ++e->param;  // params are locked and should never move
    if (e->var)
        ++e->var;  // !! Note: vars can move if an ordinary context expands
    ++e->index;

    for (
        ;
        e->key != e->key_tail;
        (++e->index, ++e->key,
            e->param ? ++e->param : cast(Param*, nullptr),
            e->var ? ++e->var : cast(Value*, nullptr)
        )
    ){
        if (e->var and Get_Cell_Flag(e->var, VAR_MARKED_HIDDEN))
            continue;  // user-specified hidden bit, on the variable itself

        if (e->param) {
            if (
                Get_Cell_Flag(
                    e->param,
                    VAR_MARKED_HIDDEN  // hidden bit on *exemplar* [1]
                )){
                assert(Is_Specialized(e->param));  // *not* anti PARAMETER!
                continue;
            }

            if (e->lens_mode == LENS_MODE_ALL_UNSEALED)
                return true;  // anything that wasn't "sealed" is fair game

            if (e->lens_mode == LENS_MODE_INPUTS) {
                if (Is_Specialized(e->param))
                    continue;
                return true;
            }

            assert(e->lens_mode == LENS_MODE_PARTIALS);
            if (not Is_Parameter(e->param))
                continue;

            return true;
        }

        return true;
    }

    return false;
}


//
//  Shutdown_Evars: C
//
void Shutdown_Evars(EVARS *e)
{
    if (e->word)
        GC_Kill_Flex(e->wordlist);
    else {
      #if RUNTIME_CHECKS
        assert(Is_Pointer_Corrupt_Debug(e->wordlist));
      #endif
    }

  #if RUNTIME_CHECKS
    --g_num_evars_outstanding;
  #endif
}


//
//  CT_Context: C
//
REBINT CT_Context(const Cell* a, const Cell* b, bool strict)
{
    assert(Any_Context_Kind(Cell_Heart(a)));
    assert(Any_Context_Kind(Cell_Heart(b)));

    if (Cell_Heart(a) != Cell_Heart(b))  // e.g. ERROR! won't equal OBJECT!
        return Cell_Heart(a) > Cell_Heart(b) ? 1 : 0;

    VarList* c1 = Cell_Varlist(a);
    VarList* c2 = Cell_Varlist(b);
    if (c1 == c2)
        return 0;  // short-circuit, always equal if same context pointer

    // Note: can't short circuit on unequal frame lengths alone, as hidden
    // fields of objects do not figure into the `equal?` of their public
    // portions.

    EVARS e1;
    Init_Evars(&e1, a);

    EVARS e2;
    Init_Evars(&e2, b);

    // Compare each entry, in order.  Skip any hidden fields, field names are
    // compared case-insensitively.
    //
    // !!! The order dependence suggests that `make object! [a: 1 b: 2]` will
    // not be equal to `make object! [b: 1 a: 2]`.  See #2341
    //
    REBINT diff = 0;
    while (true) {
        if (not Try_Advance_Evars(&e1)) {
            if (not Try_Advance_Evars(&e2))
                diff = 0;  // if both exhausted, they're equal
            else
                diff = -1;  // else the first had fewer fields
            goto finished;
        }
        else {
            if (not Try_Advance_Evars(&e2)) {
                diff = 1;  // the second had fewer fields
                goto finished;
            }
        }

        const Symbol* symbol1 = Key_Symbol(e1.key);
        const Symbol* symbol2 = Key_Symbol(e2.key);
        diff = Compare_Spellings(symbol1, symbol2, strict);
        if (diff != 0)
            goto finished;

        diff = Cmp_Value(e1.var, e2.var, strict);
        if (diff != 0)
            goto finished;
    }

  finished:
    Shutdown_Evars(&e1);
    Shutdown_Evars(&e2);

    return diff;
}


//
//  Makehook_Frame: C
//
// !!! The feature of MAKE FRAME! from a VARARGS! would be interesting as a
// way to support usermode authoring of things like MATCH.
//
// For now just support ACTION! (or path/word to specify an action)
//
Bounce Makehook_Frame(Level* level_, Heart heart, Element* arg) {
    assert(heart == REB_FRAME);
    UNUSED(heart);

    // MAKE FRAME! on a VARARGS! was an experiment designed before REFRAMER
    // existed, to allow writing things like REQUOTE.  It's still experimental
    // but has had its functionality unified with reframer, so that it doesn't
    // really cost that much to keep around.  Use it sparingly (if at all).
    //
    if (Is_Varargs(arg)) {
        Level* L_varargs;
        Feed* feed;
        if (Is_Level_Style_Varargs_May_Fail(&L_varargs, arg)) {
            assert(Is_Action_Level(L_varargs));
            feed = L_varargs->feed;
        }
        else {
            Element* shared;
            if (not Is_Block_Style_Varargs(&shared, arg)) {
                assert(false);  // shouldn't happen
                return FAIL("Expected BLOCK!-style varargs");
            }

            feed = Prep_At_Feed(
                Alloc_Feed(), shared, SPECIFIED, FEED_MASK_DEFAULT
            );
        }

        Add_Feed_Reference(feed);

        bool error_on_deferred = true;
        if (Init_Frame_From_Feed_Throws(
            OUT,
            nullptr,
            feed,
            error_on_deferred
        )){
            return BOUNCE_THROWN;
        }

        Release_Feed(feed);

        return OUT;
    }

    StackIndex lowest_stackindex = TOP_INDEX;  // for refinements

    if (not Is_Frame(arg))
        return RAISE(Error_Bad_Make(REB_FRAME, arg));

    Option(VarList*) coupling = Cell_Frame_Coupling(arg);

    ParamList* exemplar = Make_Varlist_For_Action(
        arg,  // being used here as input (e.g. the ACTION!)
        lowest_stackindex,  // will weave in any refinements pushed
        nullptr,  // no binder needed, not running any code
        NOTHING_VALUE  // use COPY UNRUN FRAME! for parameters vs. nothing
    );

    ParamList* lens = Phase_Paramlist(Cell_Frame_Phase(arg));
    Init_Lensed_Frame(OUT, exemplar, lens, coupling);

    return OUT;
}


//
//  Makehook_Context: C
//
Bounce Makehook_Context(Level* level_, Heart heart, Element* arg) {
    //
    // Other context kinds (LEVEL!, ERROR!, PORT!) have their own hooks.
    //
    assert(heart == REB_OBJECT or heart == REB_MODULE);

    if (heart == REB_MODULE) {
        if (not Any_List(arg))
            return RAISE("Currently only (MAKE MODULE! LIST) is allowed");

        VarList* ctx = Alloc_Varlist_Core(NODE_FLAG_MANAGED, REB_MODULE, 0);
        Tweak_Link_Inherit_Bind(ctx, Cell_Binding(arg));
        return Init_Context_Cell(OUT, REB_MODULE, ctx);
    }

    if (Is_Block(arg)) {
        const Element* tail;
        const Element* at = Cell_List_At(&tail, arg);

        VarList* ctx = Make_Varlist_Detect_Managed(
            COLLECT_ONLY_SET_WORDS,
            heart,
            at,
            tail,
            nullptr  // no parent (MAKE SOME-OBJ [...] calls DECLARE_GENERICS(Context))
        );
        Init_Context_Cell(OUT, heart, ctx);

        Tweak_Cell_Binding(arg, Make_Use_Core(
            Varlist_Archetype(ctx),
            Cell_List_Binding(arg),
            CELL_MASK_ERASED_0
        ));

        bool threw = Eval_Any_List_At_Throws(SPARE, arg, SPECIFIED);
        UNUSED(SPARE);  // result disregarded

        if (threw)
            return BOUNCE_THROWN;

        return OUT;
    }

    // `make object! 10` - currently not prohibited for any context type
    //
    if (Any_Number(arg)) {
        VarList* context = Make_Varlist_Detect_Managed(
            COLLECT_ONLY_SET_WORDS,
            heart,
            Array_Head(EMPTY_ARRAY),  // scan for toplevel set-words (empty)
            Array_Tail(EMPTY_ARRAY),
            nullptr  // no parent
        );

        return Init_Context_Cell(OUT, heart, context);
    }

    // make object! map!
    if (Is_Map(arg)) {
        VarList* c = Alloc_Varlist_From_Map(VAL_MAP(arg));
        return Init_Context_Cell(OUT, heart, c);
    }

    return RAISE(Error_Bad_Make(heart, arg));
}


//
//  /adjunct-of: native [
//
//  "Get a reference to the 'adjunct' context associated with a value"
//
//      return: [~null~ any-context?]
//      value [<unrun> <maybe> frame! any-context?]
//  ]
//
DECLARE_NATIVE(adjunct_of)
{
    INCLUDE_PARAMS_OF_ADJUNCT_OF;

    Value* v = ARG(value);

    Option(VarList*) adjunct;
    if (Is_Frame(v)) {
        adjunct = Misc_Phase_Adjunct(Cell_Frame_Phase(v));
    }
    else {
        assert(Any_Context(v));
        adjunct = Misc_Varlist_Adjunct(Cell_Varlist(v));
    }

    if (not adjunct)
        return nullptr;

    return COPY(Varlist_Archetype(unwrap adjunct));
}


//
//  /set-adjunct: native [
//
//  "Set 'adjunct' object associated with all references to a value"
//
//      return: [~null~ any-context?]
//      value [<unrun> frame! any-context?]
//      adjunct [~null~ any-context?]
//  ]
//
DECLARE_NATIVE(set_adjunct)
//
// See notes accompanying the `adjunct` field in DetailsAdjunct/VarlistAdjunct.
{
    INCLUDE_PARAMS_OF_SET_ADJUNCT;

    Value* adjunct = ARG(adjunct);

    Option(VarList*) ctx;
    if (Any_Context(adjunct)) {
        if (Is_Frame(adjunct))
            return FAIL("SET-ADJUNCT can't store bindings, FRAME! disallowed");

        ctx = Cell_Varlist(adjunct);
    }
    else {
        assert(Is_Nulled(adjunct));
        ctx = nullptr;
    }

    Value* v = ARG(value);

    if (Is_Frame(v)) {
        Tweak_Misc_Phase_Adjunct(Cell_Frame_Phase(v), ctx);
    }
    else
        Tweak_Misc_Varlist_Adjunct(Varlist_Array(Cell_Varlist(v)), ctx);

    return COPY(adjunct);
}


//
//  Copy_Varlist_Extra_Managed: C
//
// If no extra space is requested, the same keylist will be reused.
//
// !!! Copying a context used to be more different from copying an ordinary
// array.  But at the moment, much of the difference is that the marked bit
// in cells gets duplicated (so new context has the same VAR_MARKED_HIDDEN
// settings on its variables).  Review if the copying can be cohered better.
//
VarList* Copy_Varlist_Extra_Managed(
    VarList* original,
    REBLEN extra,
    bool deeply
){
    REBLEN len = (CTX_TYPE(original) == REB_MODULE) ? 0 : Varlist_Len(original);

    Array* varlist = Make_Array_For_Copy(
        FLEX_MASK_VARLIST | NODE_FLAG_MANAGED,
        nullptr,  // original_array, N/A because link/misc used otherwise
        len + extra + 1
    );
    if (CTX_TYPE(original) == REB_MODULE)
        Set_Flex_Used(varlist, 1);  // all variables linked from word table
    else
        Set_Flex_Len(varlist, Varlist_Len(original) + 1);

    Value* dest = Flex_Head(Value, varlist);

    // The type information and fields in the rootvar (at head of the varlist)
    // get filled in with a copy, but the varlist needs to be updated in the
    // copied rootvar to the one just created.
    //
    Copy_Cell(dest, Varlist_Archetype(original));
    Tweak_Cell_Context_Varlist(dest, varlist);

    if (CTX_TYPE(original) == REB_MODULE) {
        //
        // Copying modules is different because they have no data in the
        // varlist and no keylist.  The symbols themselves point to a linked
        // list of variable instances from all the modules that use that
        // symbol.  So copying requires walking the global symbol list and
        // duplicating those links.

        assert(extra == 0);

        if (Misc_Varlist_Adjunct(original)) {
            Tweak_Misc_Varlist_Adjunct(varlist, Copy_Varlist_Shallow_Managed(
                unwrap Misc_Varlist_Adjunct(original)
            ));
        }
        else {
            Tweak_Misc_Varlist_Adjunct(varlist, nullptr);
        }
        BONUS(KeyList, varlist) = nullptr;  // modules don't have keylists
        Tweak_Link_Inherit_Bind(varlist, nullptr);

        VarList* copy = cast(VarList*, varlist); // now a well-formed context
        assert(Get_Stub_Flag(varlist, DYNAMIC));

        Symbol** psym = Flex_Head(Symbol*, g_symbols.by_hash);
        Symbol** psym_tail = Flex_Tail(Symbol*, g_symbols.by_hash);
        for (; psym != psym_tail; ++psym) {
            if (*psym == nullptr or *psym == &g_symbols.deleted_symbol)
                continue;

            Stub* patch = Misc_Hitch(*psym);
            if (Get_Flavor_Flag(SYMBOL, *psym, MISC_IS_BIND_STUMP))
                patch = Misc_Hitch(patch);  // skip binding stump

            for (; patch != *psym; patch = Misc_Hitch(patch)) {
                if (original == Info_Patch_Sea(patch)) {
                    Value* var = Append_Context(copy, *psym);
                    Copy_Cell(var, Stub_Cell(patch));
                    break;
                }
            }
        }

        return copy;
    }

    Assert_Flex_Managed(Keylist_Of_Varlist(original));

    ++dest;

    // Now copy the actual vars in the context, from wherever they may be
    // (might be in an array, or might be in the chunk stack for FRAME!)
    //
    const Value* src_tail;
    Value* src = Varlist_Slots(&src_tail, original);
    for (; src != src_tail; ++src, ++dest) {
        Copy_Cell_Core(  // trying to duplicate slot precisely
            dest,
            src,
            CELL_MASK_ALL  // include VAR_MARKED_HIDDEN, PARAM_NOTE_TYPECHECKED
        );

        Flags flags = NODE_FLAG_MANAGED;  // !!! Review, which flags?
        Clonify(dest, flags, deeply);
    }

    varlist->leader.bits |= FLEX_MASK_VARLIST;

    VarList* copy = cast(VarList*, varlist); // now a well-formed context

    if (extra == 0)
        Tweak_Keylist_Of_Varlist_Shared(
            copy,
            Keylist_Of_Varlist(original)
        );
    else {
        assert(CTX_TYPE(original) != REB_FRAME);  // can't expand FRAME!s

        KeyList* keylist = cast(KeyList*, Copy_Flex_At_Len_Extra(
            FLEX_MASK_KEYLIST | NODE_FLAG_MANAGED,
            Keylist_Of_Varlist(original),
            0,
            Varlist_Len(original),
            extra
        ));

        Tweak_Link_Keylist_Ancestor(keylist, Keylist_Of_Varlist(original));

        Tweak_Keylist_Of_Varlist_Unique(copy, keylist);
    }

    // A FRAME! in particular needs to know if it points back to a stack
    // frame.  The pointer is NULLed out when the stack level completes.
    // If we're copying a frame here, we know it's not running.
    //
    if (CTX_TYPE(original) == REB_FRAME)
        Tweak_Misc_Varlist_Adjunct(varlist, nullptr);
    else {
        // !!! Should the meta object be copied for other context types?
        // Deep copy?  Shallow copy?  Just a reference to the same object?
        //
        Tweak_Misc_Varlist_Adjunct(varlist, nullptr);
    }

    Tweak_Link_Inherit_Bind(varlist, nullptr);

    return copy;
}


//
//  MF_Context: C
//
void MF_Context(Molder* mo, const Cell* v, bool form)
{
    String* s = mo->string;

    VarList* c = Cell_Varlist(v);

    // Prevent endless mold loop:
    //
    if (Find_Pointer_In_Flex(g_mold.stack, c) != NOT_FOUND) {
        if (not form) {
            Begin_Non_Lexical_Mold(mo, v); // If molding, get #[object! etc.
            Append_Codepoint(s, '[');
        }
        Append_Ascii(s, "...");

        if (not form) {
            Append_Codepoint(s, ']');
            End_Non_Lexical_Mold(mo);
        }
        return;
    }
    Push_Pointer_To_Flex(g_mold.stack, c);

    EVARS e;
    Init_Evars(&e, v);

    if (form) {
        //
        // Mold all words and their values ("key: <molded value>").
        //
        bool had_output = false;
        while (Try_Advance_Evars(&e)) {
            Append_Spelling(mo->string, Key_Symbol(e.key));
            Append_Ascii(mo->string, ": ");

            if (Is_Antiform(e.var)) {
                fail (Error_Bad_Antiform(e.var));  // can't FORM antiforms
            }
            else
                Mold_Element(mo, cast(Element*, e.var));

            Append_Codepoint(mo->string, LF);
            had_output = true;
        }
        Shutdown_Evars(&e);

        // Remove the final newline...but only if WE added to the buffer
        //
        if (had_output)
            Trim_Tail(mo, '\n');

        Drop_Pointer_From_Flex(g_mold.stack, c);
        return;
    }

    // Otherwise we are molding

    Begin_Non_Lexical_Mold(mo, v);

    Append_Codepoint(s, '[');

    mo->indent++;

    while (Try_Advance_Evars(&e)) {
        New_Indented_Line(mo);

        const Symbol* spelling = Key_Symbol(e.key);

        DECLARE_ELEMENT (set_word);
        Init_Set_Word(set_word, spelling);  // want escaping, e.g `|::|: 10`

        Mold_Element(mo, set_word);
        Append_Codepoint(mo->string, ' ');

        if (Is_Antiform(e.var)) {
            assert(Is_Antiform_Stable(cast(Atom*, e.var)));  // extra check

            DECLARE_ELEMENT (reified);
            Copy_Meta_Cell(reified, e.var);  // will become quasi...
            Mold_Element(mo, cast(Element*, reified));  // ...molds as `~xxx~`
        }
        else {
            // We want the molded object to be able to "round trip" back to the
            // state it's in based on reloading the values.  Currently this is
            // conservative and doesn't put quote marks on things that don't
            // need it because they are inert, but maybe not a good idea...
            // depends on the whole block/object model.
            //
            // https://forum.rebol.info/t/997
            //
            if (not Any_Inert(e.var))
                Append_Ascii(s, "'");
            Mold_Element(mo, cast(Element*, e.var));
        }
    }
    Shutdown_Evars(&e);

    mo->indent--;
    New_Indented_Line(mo);
    Append_Codepoint(s, ']');

    End_Non_Lexical_Mold(mo);

    Drop_Pointer_From_Flex(g_mold.stack, c);
}


const Symbol* Symbol_From_Picker(const Value* context, const Value* picker)
{
    UNUSED(context);  // Might the picker be context-sensitive?

    if (not Is_Word(picker))
        fail (picker);

    return Cell_Word_Symbol(picker);
}


// 1. !!! Special attention on copying frames is going to be needed, because
//    copying a frame will be expected to create a new identity for an ACTION!
//    if that frame is aliased AS ACTION!.  The design is still evolving, but
//    we don't want archetypal values otherwise we could not `do copy f`, so
//    initialize with label.
//
static Element* Copy_Any_Context(
    Sink(Element) out,
    Element* context,
    bool deep
){
    if (Is_Frame(context)) {  // handled specially [1]
        return Init_Frame(
            out,
            cast(ParamList*,
                Copy_Varlist_Extra_Managed(Cell_Varlist(context), 0, deep)
            ),
            Cell_Frame_Label(context),
            Cell_Frame_Coupling(context)
        );
    }

    return Init_Context_Cell(
        out,
        Cell_Heart_Ensure_Noquote(context),
        Copy_Varlist_Extra_Managed(Cell_Varlist(context), 0, deep)
    );
}


//
//  DECLARE_GENERICS: C
//
// Handles object!, module!, and error! datatypes.
//
DECLARE_GENERICS(Context)
{
    Option(SymId) id = Symbol_Id(verb);

    Element* context = cast(Element*,
        (id == SYM_TO or id == SYM_AS) ? ARG_N(2) : ARG_N(1)
    );
    VarList* c = Cell_Varlist(context);
    Heart heart = Cell_Heart(context);

    // !!! The PORT! datatype wants things like LENGTH OF to give answers
    // based on the content of the port, not the number of fields in the
    // PORT! object.  This ties into a number of other questions:
    //
    // https://forum.rebol.info/t/1689
    //
    // At the moment only PICK and POKE are routed here.
    //
    if (Is_Port(context))
        assert(id == SYM_PICK or id == SYM_POKE);

    switch (id) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // covered by `v`

        Value* property = ARG(property);
        Option(SymId) prop = Cell_Word_Id(property);

        switch (prop) {
          case SYM_LENGTH: // !!! Should this be legal?
            return Init_Integer(OUT, Varlist_Len(c));

          case SYM_TAIL_Q: // !!! Should this be legal?
            return Init_Logic(OUT, Varlist_Len(c) == 0);

          case SYM_WORDS:
            return Init_Block(OUT, Context_To_Array(context, 1));

          case SYM_VALUES:
            return Init_Block(OUT, Context_To_Array(context, 2));

          case SYM_BODY:
            return Init_Block(OUT, Context_To_Array(context, 3));

          default: break;
        }

        // Noticeably not handled by average objects: SYM_OPEN_Q (`open?`)

        return FAIL(Error_Cannot_Reflect(VAL_TYPE(context), property)); }

    //=//// MAKE SOME-OBJ [...] (MAKE OBJECT! Handled By TYPE-BLOCK!) //////=//

      case SYM_MAKE: {
        INCLUDE_PARAMS_OF_MAKE;

        UNUSED(ARG(type));  // already in context
        Element* def = cast(Element*, ARG(def));

        if (heart == REB_MODULE)
            return FAIL("Cannot MAKE derived MODULE! instances (yet?)");

        if (Is_Block(def)) {
            const Element* tail;
            const Element* at = Cell_List_At(&tail, def);

            VarList* derived = Make_Varlist_Detect_Managed(
                COLLECT_ONLY_SET_WORDS,
                heart,
                at,
                tail,
                c
            );
            Init_Context_Cell(OUT, heart, derived);

            Tweak_Cell_Binding(def, Make_Use_Core(
                Varlist_Archetype(derived),
                Cell_List_Binding(def),
                CELL_MASK_ERASED_0
            ));

            DECLARE_ATOM (dummy);
            if (Eval_Any_List_At_Throws(dummy, def, SPECIFIED))
                return BOUNCE_THROWN;

            return OUT;
        }

        return Error_Bad_Make(heart, def); }

    //=//// TO CONVERSION /////////////////////////////////////////////////=//

    // 1. !!! Cannot convert TO a PORT! without copying the whole context...
    //    which raises the question of why convert an object to a port,
    //    vs. making it as a port to begin with (?)  Look into why
    //    system.standard.port is made with CONTEXT and not with MAKE PORT!

      case SYM_TO: {
        INCLUDE_PARAMS_OF_TO;
        UNUSED(ARG(element));  // context
        Heart to = VAL_TYPE_HEART(ARG(type));
        assert(heart != to);  // TO should have called COPY in this case

        if (to == REB_PORT) {
            if (heart != REB_OBJECT)
                return FAIL(
                    "Only TO convert OBJECT! -> PORT! (weird internal code)"
                );

            VarList* copy = Copy_Varlist_Shallow_Managed(c);  // !!! copy [1]
            Value* rootvar = Rootvar_Of_Varlist(copy);
            HEART_BYTE(rootvar) = REB_PORT;
            return Init_Port(OUT, copy);
        }

        if (to == VAL_TYPE(context)) {  // can't TO FRAME! an ERROR!, etc.
            bool deep = false;
            return Copy_Any_Context(OUT, context, deep);
        }

        return UNHANDLED; }

      case SYM_COPY: {  // Note: words are not copied and bindings not changed!
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PARAM(value));  // covered by `context`

        if (REF(part))
            return FAIL(Error_Bad_Refines_Raw());

        bool deep = REF(deep);
        return Copy_Any_Context(OUT, context, deep); }

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK: {
        INCLUDE_PARAMS_OF_PICK;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        const Symbol* symbol = Symbol_From_Picker(context, picker);

        const Value* var = TRY_VAL_CONTEXT_VAR(context, symbol);
        if (not var)
            return RAISE(Error_Bad_Pick_Raw(picker));

        Copy_Cell(OUT, var);

        if (
            HEART_BYTE(var) == REB_FRAME
            and QUOTE_BYTE(var) == ANTIFORM_0
            and Cell_Frame_Coupling(var) == UNCOUPLED
        ){
            Tweak_Cell_Frame_Coupling(OUT, c);
        }

        return OUT; }


    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE: {
        INCLUDE_PARAMS_OF_POKE;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        const Symbol* symbol = Symbol_From_Picker(context, picker);

        Value* setval = ARG(value);

        Value* var = TRY_VAL_CONTEXT_MUTABLE_VAR(context, symbol);
        if (not var)
            return FAIL(Error_Bad_Pick_Raw(picker));

        assert(Not_Cell_Flag(var, PROTECTED));
        Copy_Cell(var, setval);
        return nullptr; }  // caller's VarList* is not stale, no update needed


    //=//// PROTECT* ///////////////////////////////////////////////////////=//

      case SYM_PROTECT_P: {
        INCLUDE_PARAMS_OF_PROTECT_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        const Symbol* symbol = Symbol_From_Picker(context, picker);

        Value* setval = ARG(value);

        Value* var = m_cast(Value*, TRY_VAL_CONTEXT_VAR(context, symbol));
        if (not var)
            return FAIL(Error_Bad_Pick_Raw(picker));

        if (not Is_Word(setval))
            return FAIL("PROTECT* currently takes just WORD!");

        switch (Cell_Word_Id(setval)) {
          case SYM_PROTECT:
            Set_Cell_Flag(var, PROTECTED);
            break;

          case SYM_UNPROTECT:
            Clear_Cell_Flag(var, PROTECTED);
            break;

          case SYM_HIDE:
            Set_Cell_Flag(var, VAR_MARKED_HIDDEN);
            break;

          default:
            return FAIL(var);
        }

        return nullptr; }  // caller's VarList* is not stale, no update needed

      case SYM_APPEND:
        fail ("APPEND on OBJECT!, MODULE!, etc. replaced with EXTEND");

      case SYM_EXTEND: {
        INCLUDE_PARAMS_OF_EXTEND;
        UNUSED(ARG(context));
        Element* def = cast(Element*, ARG(def));

        if (Is_Word(def)) {
            bool strict = true;
            Option(Index) i = Find_Symbol_In_Context(
                context, Cell_Word_Symbol(def), strict
            );
            if (i) {
                CELL_WORD_INDEX_I32(def) = unwrap i;
                if (Is_Module(context))
                    Tweak_Cell_Binding(def, MOD_PATCH(
                        cast(SeaOfVars*, c), Cell_Word_Symbol(def), strict
                    ));
                else
                    Tweak_Cell_Binding(def, c);
                return COPY(def);
            }
            Init_Nothing(Append_Context_Bind_Word(c, def));
            return COPY(def);
        }

        assert(Is_Block(def));

        CollectFlags flags = COLLECT_ONLY_SET_WORDS;
        if (REF(prebound))
            flags |= COLLECT_TOLERATE_PREBOUND;

        Option(Error*) e = Trap_Wrap_Extend_Core(c, def, flags);
        if (e)
            return FAIL(unwrap e);

        Use* use = Make_Use_Core(
            context, Cell_Binding(def), CELL_FLAG_USE_NOTE_SET_WORDS
        );
        Tweak_Cell_Binding(def, use);

        bool threw = Eval_Any_List_At_Throws(OUT, def, SPECIFIED);
        if (threw)
            return BOUNCE_THROWN;

        return COPY(context); }

      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_SELECT;
        UNUSED(ARG(series));  // extracted as `c`

        if (REF(part) or REF(skip) or REF(match))
            return FAIL(Error_Bad_Refines_Raw());

        Value* pattern = ARG(value);
        if (Is_Antiform(pattern))
            return FAIL(pattern);

        if (not Is_Word(pattern))
            return nullptr;

        Option(Index) index = Find_Symbol_In_Context(
            context,
            Cell_Word_Symbol(pattern),
            REF(case)
        );
        if (not index)
            return nullptr;

        return COPY(Varlist_Slot(c, unwrap index)); }

      default:
        break;
    }

    return UNHANDLED;
}


//
//  DECLARE_GENERICS: C
//
// FRAME! adds some additional reflectors to the usual things you can do with
// an object, but falls through to DECLARE_GENERICS(Context) for most things.
//
DECLARE_GENERICS(Frame)
{
    Option(SymId) id = Symbol_Id(verb);

    Element* frame = cast(Element*,
        (id == SYM_TO or id == SYM_AS) ? ARG_N(2) : ARG_N(1)
    );

    Phase* phase = Cell_Frame_Phase(frame);

    switch (id) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // covered by `frame`

        Option(SymId) prop = Cell_Word_Id(ARG(property));

        switch (prop) {
          case SYM_COUPLING: {
            Option(VarList*) coupling = Cell_Frame_Coupling(frame);
            if (not coupling)  // NONMETHOD
                return nullptr;
            if (unwrap coupling == UNCOUPLED)
                return NOTHING;
            return COPY(Varlist_Archetype(unwrap coupling)); }

          case SYM_LABEL: {
            Option(const Symbol*) label = Cell_Frame_Label_Deep(frame);
            if (label)
                return Init_Word(OUT, unwrap label);

            // If the frame is executing, we can look at the label in the
            // Level*, which will tell us what the overall execution label
            // would be.  This might be confusing, however...if the phase
            // is drastically different.  Review.

            break; }

          case SYM_PARAMETERS:
            return Init_Frame(
                OUT,
                Cell_Frame_Phase(frame),
                ANONYMOUS,
                Cell_Frame_Coupling(frame)
            );

          case SYM_WORDS:
            return Init_Block(OUT, Context_To_Array(frame, 1));

          case SYM_BODY:
          case SYM_RETURN: {
            Details* details = Phase_Details(phase);
            DetailsQuerier* querier = Details_Querier(details);
            if (not (*querier)(OUT, details, unwrap prop))
                return FAIL("FRAME!'s Details does not offer BODY/RETURN");
            return OUT; }

          case SYM_FILE:
          case SYM_LINE: {
            if (not Is_Stub_Details(phase))
                break;  // try to check and see if there's runtime info

            Details* details = cast(Details*, phase);

            // Use a heuristic that if the first element of a function's body
            // is an Array with the file and line bits set, then that's what
            // it returns for FILE OF and LINE OF.

            if (
                Details_Max(details) < 1
                or not Any_List(Details_At(details, 1))
            ){
                return nullptr;
            }

            const Source* a = Cell_Array(Details_At(details, 1));

            // !!! How to tell URL! vs FILE! ?
            //
            if (prop == SYM_FILE) {
                Option(const String*) filename = Link_Filename(a);
                if (not filename)
                    return nullptr;
                return Init_File(OUT, unwrap filename);
            }

            assert(prop == SYM_LINE);
            if (a->misc.line == 0)
                return nullptr;

            return Init_Integer(OUT, a->misc.line); }

          default:
            break;
        }

        if (Is_Stub_Details(phase))
            return FAIL(Error_Cannot_Reflect(REB_FRAME, ARG(property)));

        Level* L = Level_Of_Varlist_May_Fail(cast(ParamList*, phase));

        switch (prop) {
          case SYM_FILE: {
            Option(const String*) file = File_Of_Level(L);
            if (not file)
                return nullptr;
            return Init_File(OUT, unwrap file); }

          case SYM_LINE: {
            Option(LineNumber) line = Line_Number_Of_Level(L);
            if (not line)
                return nullptr;
            return Init_Integer(OUT, unwrap line); }

          case SYM_LABEL: {
            if (Try_Get_Action_Level_Label(OUT, L))
                return OUT;
            return nullptr; }

          case SYM_NEAR:
            return Init_Near_For_Level(OUT, L);

          case SYM_PARENT: {
            //
            // Only want action levels (though `pending? = true` ones count).
            //
            Level* parent = L;
            while ((parent = parent->prior) != BOTTOM_LEVEL) {
                if (not Is_Action_Level(parent))
                    continue;

                VarList* v_parent = Varlist_Of_Level_Force_Managed(parent);
                return COPY(Varlist_Archetype(v_parent));
            }
            return nullptr; }

          default:
            break;
        }
        return FAIL(Error_Cannot_Reflect(REB_FRAME, ARG(property))); }

  //=//// COPY /////////////////////////////////////////////////////////////=//

      case SYM_COPY: {
        ParamList* copy = Make_Varlist_For_Action(
            frame,
            TOP_INDEX,
            nullptr,  // no binder
            nullptr  // no placeholder, use parameters
        );

        ParamList* lens = Phase_Paramlist(Cell_Frame_Phase(frame));
        return Init_Lensed_Frame(
            OUT,
            copy,
            lens,
            Cell_Frame_Coupling(frame)
        ); }

      default:
        break;
    }

    return T_Context(level_, verb);
}


//
//  CT_Frame: C
//
// !!! What are the semantics of comparison in frames?
//
REBINT CT_Frame(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);  // no lax form of comparison

    Phase* a_phase = Cell_Frame_Phase(a);
    Phase* b_phase = Cell_Frame_Phase(b);

    Details* a_details = Phase_Details(a_phase);
    Details* b_details = Phase_Details(b_phase);

    if (a_details != b_details)
        return a_details > b_details ? 1 : -1;

    VarList* a_coupling = maybe Cell_Frame_Coupling(a);
    VarList* b_coupling = maybe Cell_Frame_Coupling(b);

    if (a_coupling != b_coupling)
        return a_coupling > b_coupling ? 1 : -1;

    return CT_Context(a, b, strict);
}


//
//  MF_Frame: C
//
void MF_Frame(Molder* mo, const Cell* v, bool form) {

    if (QUOTE_BYTE(v) != QUASIFORM_2) {
        MF_Context(mo, v, form);
        return;
    }

    Append_Ascii(mo->string, "#[frame! ");

    Option(const Symbol*) label = Cell_Frame_Label_Deep(v);
    if (label) {
        Append_Codepoint(mo->string, '"');
        Append_Spelling(mo->string, unwrap label);
        Append_Codepoint(mo->string, '"');
        Append_Codepoint(mo->string, ' ');
    }

    Array* parameters = Context_To_Array(v, 1);
    Mold_Array_At(mo, parameters, 0, "[]");
    Free_Unmanaged_Flex(parameters);

    // !!! Previously, ACTION! would mold the body out.  This created a large
    // amount of output, and also many function variations do not have
    // ordinary "bodies".  It's more useful to show the cached name, and maybe
    // some base64 encoding of a UUID (?)  In the meantime, having the label
    // of the last word used is actually a lot more useful than most things.

    Append_Codepoint(mo->string, ']');
    End_Non_Lexical_Mold(mo);
}


//
//  /construct: native [
//
//  "Creates an OBJECT! from a spec that is not bound into the object"
//
//      return: [~null~ object!]
//      spec "Object spec block, top-level SET-WORD!s will be object keys"
//          [<maybe> block! the-block!]
//      :with "Use a parent/prototype context"
//          [object!]
//  ]
//
DECLARE_NATIVE(construct)
//
// 1. In R3-Alpha you could do:
//
//      construct/only [a: b: 1 + 2 d: a e:]
//
//    This would yield `a` and `b` set to 1, while `+` and `2` would be
//    ignored, `d` will be the word `a` (where it is bound to the `a`
//    of the object being synthesized) and `e` would be left as it was.
//    Ren-C doesn't allow any discarding...a SET-WORD! must be followed
//    either by another SET-WORD! or a single array element followed by
//    another SET-WORD!, the end of the array, or a COMMA!.
{
    INCLUDE_PARAMS_OF_CONSTRUCT;

    Value* spec = ARG(spec);

    enum {
        ST_CONSTRUCT_INITIAL_ENTRY = STATE_0,
        ST_CONSTRUCT_EVAL_STEP,
        ST_CONSTRUCT_EVAL_SET_STEP
    };

    switch (STATE) {
      case ST_CONSTRUCT_INITIAL_ENTRY:
        goto initial_entry;

      case ST_CONSTRUCT_EVAL_STEP:
        Reset_Evaluator_Erase_Out(SUBLEVEL);
        goto continue_processing_spec;

      case ST_CONSTRUCT_EVAL_SET_STEP:
        goto eval_set_step_result_in_spare;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    VarList* parent = REF(with)
        ? Cell_Varlist(ARG(with))
        : nullptr;

    const Element* tail;
    const Element* at = Cell_List_At(&tail, spec);

    VarList* varlist = Make_Varlist_Detect_Managed(
        COLLECT_ONLY_SET_WORDS,
        parent ? CTX_TYPE(parent) : REB_OBJECT,  // !!! Presume object?
        at,
        tail,
        parent
    );
    Init_Object(OUT, varlist);  // GC protects context

    Executor* executor;
    if (Is_The_Block(spec))
        executor = &Inert_Stepper_Executor;
    else {
        assert(Is_Block(spec));
        executor = &Stepper_Executor;
    }

    Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE;

    Level* sub = Make_Level_At(executor, spec, flags);
    Push_Level_Erase_Out_If_State_0(SPARE, sub);

} continue_processing_spec: {  ////////////////////////////////////////////////

    if (Is_Level_At_End(SUBLEVEL)) {
        Drop_Level(SUBLEVEL);
        return OUT;
    }

    VarList* varlist = Cell_Varlist(OUT);

    const Element* at = At_Level(SUBLEVEL);

    Option(const Symbol*) symbol = Try_Get_Settable_Word_Symbol(nullptr, at);
    if (not symbol) {  // not /foo: or foo:
        STATE = ST_CONSTRUCT_EVAL_STEP;  // plain evaluation
        return CONTINUE_SUBLEVEL(SUBLEVEL);
    }

    do {  // keep pushing SET-WORD!s so `construct [a: b: 1]` works
        Option(Index) index = Find_Symbol_In_Context(
            Varlist_Archetype(varlist),
            unwrap symbol,
            true
        );
        assert(index);  // created a key for every SET-WORD! above!

        Copy_Cell(PUSH(), at);
        Tweak_Cell_Binding(TOP, varlist);
        CELL_WORD_INDEX_I32(TOP) = unwrap index;

        Fetch_Next_In_Feed(SUBLEVEL->feed);

        if (Is_Level_At_End(SUBLEVEL))
            return FAIL("Unexpected end after SET-WORD! in CONTEXT");

        at = At_Level(SUBLEVEL);
        if (Is_Comma(at))
            return FAIL("Unexpected COMMA! after SET-WORD! in CONTEXT");

    } while ((symbol = Try_Get_Settable_Word_Symbol(nullptr, at)));

    if (not Is_The_Block(spec)) {
        Copy_Cell(Level_Scratch(SUBLEVEL), TOP);
        DROP();

        LEVEL_STATE_BYTE(SUBLEVEL) = ST_STEPPER_REEVALUATING;
    }

    STATE = ST_CONSTRUCT_EVAL_SET_STEP;
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} eval_set_step_result_in_spare: {  //////////////////////////////////////////

    VarList* varlist = Cell_Varlist(OUT);

    while (TOP_INDEX != STACK_BASE) {
        Option(Index) index = CELL_WORD_INDEX_I32(TOP);
        assert(index);  // created a key for every SET-WORD! above!

        Copy_Cell(Varlist_Slot(varlist, unwrap index), stable_SPARE);

        DROP();
    }

    assert(STATE == ST_CONSTRUCT_EVAL_SET_STEP);
    Reset_Evaluator_Erase_Out(SUBLEVEL);

    goto continue_processing_spec;
}}


//
//  /extend: native:generic [
//
//  "Add more material to a context"
//
//      return: [word! any-context?]
//      context [any-context?]
//      def "If single word, adds an unset variable if not already added"
//          [block! word!]
//      :prebound "Tolerate pre-existing bindings on set words (do not collect)"
//  ]
//
DECLARE_NATIVE(extend)
{
    Element* number = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(number, LEVEL, CANON(EXTEND));
}
