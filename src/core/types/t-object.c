//
//  file: %t-object.c
//  summary: "object datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
void Init_Evars(EVARS *e, const Element* v) {
    Heart heart = Heart_Of_Builtin_Fundamental(v);

    e->lens_mode = LENS_MODE_ALL_UNSEALED;  // ensure not uninitialized

    if (heart == TYPE_MODULE) {  // !!! module enumeration is bad/slow [2]

  //=//// MODULE ENUMERATION //////////////////////////////////////////////=//

    // !!! Module enumeration is slow, and you should not do it often...it
    // requires walking over the global word table.  The global table gets
    // rehashed in a way that we'd have a hard time maintainining a consistent
    // enumerator state in the current design.  So for the moment we fabricate
    // an array to enumerate.  The enumeration won't see changes.

        e->index = INDEX_PATCHED;

        e->ctx = Cell_Module_Sea(v);

        StackIndex base = TOP_INDEX;

        Symbol** psym = Flex_Head(Symbol*, g_symbols.by_hash);
        Symbol** psym_tail = Flex_Tail(Symbol*, g_symbols.by_hash);
        for (; psym != psym_tail; ++psym) {
            if (*psym == nullptr or *psym == &g_symbols.deleted_symbol)
                continue;

            Stub* stub = Misc_Hitch(*psym);
            if (Get_Flavor_Flag(SYMBOL, *psym, HITCH_IS_BIND_STUMP))
                stub = Misc_Hitch(stub);  // skip binding stump

            Stub* patch_found = nullptr;

            for (; stub != *psym; stub = Misc_Hitch(stub)) {
                if (e->ctx == Info_Patch_Sea(cast(Patch*, stub))) {
                    patch_found = stub;
                    break;
                }
            }
            if (patch_found) {
                Init_Word(PUSH(), *psym);
                Tweak_Cell_Word_Index(TOP_ELEMENT, INDEX_PATCHED);
                Tweak_Cell_Binding(TOP_ELEMENT, patch_found);
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

        VarList* varlist = Cell_Varlist(v);
        e->ctx = varlist;

        e->var = Varlist_Slots_Head(varlist) - 1;

        assert(Flex_Used(Bonus_Keylist(varlist)) <= Varlist_Len(varlist));

        if (heart != TYPE_FRAME) {
            e->param = nullptr;
            e->key = Varlist_Keys(&e->key_tail, varlist) - 1;
        }
        else {

  //=//// FRAME ENUMERATION ///////////////////////////////////////////////=//

    // 1. It makes the most sense for unlensed frames to show the inputs only.
    //    This is because the Lens slot is used for a label when not lensed,
    //    common with antiforms.

            e->var = Varlist_Slots_Head(varlist) - 1;

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
            e->var = unwrap Sea_Slot(
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
REBINT CT_Context(const Element* a, const Element* b, bool strict)
{
    Heart a_heart = Heart_Of_Builtin_Fundamental(a);
    Heart b_heart = Heart_Of_Builtin_Fundamental(b);

    assert(Any_Context_Type(a_heart));
    assert(Any_Context_Type(b_heart));

    if (a_heart != b_heart)  // e.g. ERROR! won't equal OBJECT!
        return u_cast(Byte, a_heart) > u_cast(Byte, b_heart) ? 1 : 0;

    Node* n1 = CELL_NODE1(a);
    Node* n2 = CELL_NODE1(b);
    if (n1 == n2)
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

        bool lesser;
        if (Try_Lesser_Value(&lesser, e1.var, e2.var)) {  // works w/LESSER?
            if (lesser) {
                diff = -1;
                goto finished;
            }

            if (not Equal_Values(e1.var, e2.var, strict)) {
                diff = 1;
                goto finished;
            }
            continue;
        }

        if (Equal_Values(e1.var, e2.var, strict))  // if equal, we can continue
            continue;

        Shutdown_Evars(&e1);
        Shutdown_Evars(&e2);

        panic ("Illegal comparison");
    }

  finished:
    Shutdown_Evars(&e1);
    Shutdown_Evars(&e2);

    return diff;
}


IMPLEMENT_GENERIC(EQUAL_Q, Any_Context)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;
    bool strict = not Bool_ARG(RELAX);

    Element* value1 = Element_ARG(VALUE1);
    Element* value2 = Element_ARG(VALUE2);

    return LOGIC(CT_Context(value1, value2, strict) == 0);
}


IMPLEMENT_GENERIC(LESSER_Q, Any_Context)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Element* value1 = Element_ARG(VALUE1);
    Element* value2 = Element_ARG(VALUE2);

    return LOGIC(CT_Context(value1, value2, true) == -1);
}


// !!! The feature of MAKE FRAME! from a VARARGS! would be interesting as a
// way to support usermode authoring of things like MATCH.
//
// For now just support ACTION! (or path/word to specify an action)
//
IMPLEMENT_GENERIC(MAKE, Is_Frame)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Cell_Datatype_Type(ARG(TYPE)) == TYPE_FRAME);
    UNUSED(ARG(TYPE));

    Element* arg = Element_ARG(DEF);

    // MAKE FRAME! on a VARARGS! was an experiment designed before REFRAMER
    // existed, to allow writing things like REQUOTE.  It's still experimental
    // but has had its functionality unified with reframer, so that it doesn't
    // really cost that much to keep around.  Use it sparingly (if at all).
    //
    if (Is_Varargs(arg)) {
        Level* L_varargs;
        Feed* feed;
        if (Is_Level_Style_Varargs_May_Panic(&L_varargs, arg)) {
            assert(Is_Action_Level(L_varargs));
            feed = L_varargs->feed;
        }
        else {
            Element* shared;
            if (not Is_Block_Style_Varargs(&shared, arg)) {
                assert(false);  // shouldn't happen
                return PANIC("Expected BLOCK!-style varargs");
            }

            feed = Prep_At_Feed(
                Alloc_Feed(), shared, SPECIFIED, FEED_MASK_DEFAULT
            );
        }

        Add_Feed_Reference(feed);

        bool error_on_deferred = true;

        Option(Error*) e = Trap_Init_Frame_From_Feed(
            OUT,
            nullptr,
            feed,
            error_on_deferred
        );
        if (e)
            return PANIC(unwrap e);

        Release_Feed(feed);

        return OUT;
    }

    StackIndex lowest_stackindex = TOP_INDEX;  // for refinements

    if (not Is_Frame(arg))
        return FAIL(Error_Bad_Make(TYPE_FRAME, arg));

    Option(VarList*) coupling = Cell_Frame_Coupling(arg);

    ParamList* exemplar = Make_Varlist_For_Action(
        arg,  // being used here as input (e.g. the ACTION!)
        lowest_stackindex,  // will weave in any refinements pushed
        nullptr,  // no binder needed, not running any code
        g_trash  // use COPY UNRUN FRAME! for parameters vs. nothing
    );

    ParamList* lens = Phase_Paramlist(Cell_Frame_Phase(arg));
    Init_Lensed_Frame(OUT, exemplar, lens, coupling);

    return OUT;
}


IMPLEMENT_GENERIC(MAKE, Is_Module)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Cell_Datatype_Builtin_Heart(ARG(TYPE)) == TYPE_MODULE);
    UNUSED(ARG(TYPE));

    Element* arg = Element_ARG(DEF);

    if (not Any_List(arg))
        return FAIL("Currently only (MAKE MODULE! LIST) is allowed");

    SeaOfVars* sea = Alloc_Sea_Core(NODE_FLAG_MANAGED);
    Tweak_Link_Inherit_Bind(sea, Cell_Binding(arg));
    return Init_Module(OUT, sea);
}


// Instance where MAKE allows not just a type, but an object instance to
// inherit from.
//
IMPLEMENT_GENERIC(MAKE, Is_Object)
{
    INCLUDE_PARAMS_OF_MAKE;

    Value* type = ARG(TYPE);  // may be antiform datatype
    Element* arg = Element_ARG(DEF);

    if (Is_Object(type)) {
        VarList* varlist = cast(VarList*, Cell_Context(type));
        if (Is_Block(arg)) {
            const Element* tail;
            const Element* at = Cell_List_At(&tail, arg);

            VarList* derived = Make_Varlist_Detect_Managed(
                COLLECT_ONLY_SET_WORDS,
                TYPE_OBJECT,
                at,
                tail,
                varlist
            );

            Use* use = Alloc_Use_Inherits(Cell_List_Binding(arg));
            Copy_Cell(Stub_Cell(use), Varlist_Archetype(derived));

            Tweak_Cell_Binding(arg, use);  // def is GC-safe, use will be too
            Remember_Cell_Is_Lifeguard(Stub_Cell(use));  // keeps derived alive

            DECLARE_ATOM (dummy);
            if (Eval_Any_List_At_Throws(dummy, arg, SPECIFIED))
                return BOUNCE_THROWN;

            return Init_Context_Cell(OUT, TYPE_OBJECT, derived);
        }

        return FAIL(Error_Bad_Make(TYPE_OBJECT, arg));
    }

    assert(Cell_Datatype_Builtin_Heart(type) == TYPE_OBJECT);

    if (Is_Block(arg)) {
        const Element* tail;
        const Element* at = Cell_List_At(&tail, arg);

        VarList* ctx = Make_Varlist_Detect_Managed(
            COLLECT_ONLY_SET_WORDS,
            TYPE_OBJECT,
            at,
            tail,
            nullptr  // no parent (MAKE SOME-OBJ ... calls any_context generic)
        );

        Use* use = Alloc_Use_Inherits(Cell_List_Binding(arg));
        Copy_Cell(Stub_Cell(use), Varlist_Archetype(ctx));

        Tweak_Cell_Binding(arg, use);  // arg is GC-safe, so use will be too
        Remember_Cell_Is_Lifeguard(Stub_Cell(use));  // keeps context alive

        bool threw = Eval_Any_List_At_Throws(SPARE, arg, SPECIFIED);
        UNUSED(SPARE);  // result disregarded

        if (threw)
            return BOUNCE_THROWN;

        return Init_Object(OUT, ctx);
    }

    // `make object! 10` - currently not prohibited for any context type
    //
    if (Any_Number(arg)) {
        VarList* context = Make_Varlist_Detect_Managed(
            COLLECT_ONLY_SET_WORDS,
            TYPE_OBJECT,
            Array_Head(g_empty_array),  // scan for toplevel set-words (empty)
            Array_Tail(g_empty_array),
            nullptr  // no parent
        );

        return Init_Object(OUT, context);
    }

    // make object! map!
    if (Is_Map(arg)) {
        VarList* c = Alloc_Varlist_From_Map(VAL_MAP(arg));
        return Init_Object(OUT, c);
    }

    return FAIL(Error_Bad_Make(TYPE_OBJECT, arg));
}


//
//  adjunct-of: native [
//
//  "Get a reference to the 'adjunct' context associated with a value"
//
//      return: [null? any-context?]
//      value [<unrun> <opt-out> frame! any-context?]
//  ]
//
DECLARE_NATIVE(ADJUNCT_OF)
{
    INCLUDE_PARAMS_OF_ADJUNCT_OF;

    Value* v = ARG(VALUE);

    Option(VarList*) adjunct;
    if (Is_Frame(v)) {
        adjunct = Misc_Phase_Adjunct(Cell_Frame_Phase(v));
    }
    else {
        assert(Any_Context(v));
        if (Is_Module(v))
            adjunct = Misc_Sea_Adjunct(Cell_Module_Sea(v));
        else
            adjunct = Misc_Varlist_Adjunct(Cell_Varlist(v));
    }

    if (not adjunct)
        return nullptr;

    return COPY(Varlist_Archetype(unwrap adjunct));
}


//
//  set-adjunct: native [
//
//  "Set 'adjunct' object associated with all references to a value"
//
//      return: [null? any-context?]
//      value [<unrun> frame! any-context?]
//      adjunct [<undo-opt> any-context?]
//  ]
//
DECLARE_NATIVE(SET_ADJUNCT)
//
// See notes accompanying the `adjunct` field in DetailsAdjunct/VarlistAdjunct.
{
    INCLUDE_PARAMS_OF_SET_ADJUNCT;

    Value* adjunct = ARG(ADJUNCT);

    Option(VarList*) ctx;
    if (Any_Context(adjunct)) {
        if (Is_Frame(adjunct))
            return PANIC("SET-ADJUNCT can't store bindings, FRAME! disallowed");

        ctx = Cell_Varlist(adjunct);
    }
    else {
        assert(Is_Nulled(adjunct));
        ctx = nullptr;
    }

    Value* v = ARG(VALUE);

    if (Is_Frame(v)) {
        Tweak_Misc_Phase_Adjunct(Cell_Frame_Phase(v), ctx);
    }
    else if (Is_Module(v)) {
        Tweak_Misc_Sea_Adjunct(Cell_Module_Sea(v), ctx);
    }
    else
        Tweak_Misc_Varlist_Adjunct(Cell_Varlist(v), ctx);

    return COPY(adjunct);
}


//
//  Copy_Sea_Managed: C
//
// Modules hold no data in the SeaOfVars.  Instead, the Symbols themselves
// point to a linked list of variable instances from all the modules that use
// that symbol.  So copying requires walking the global symbol list and
// duplicating those links.
//
SeaOfVars* Copy_Sea_Managed(SeaOfVars* original) {
    Stub* sea = Alloc_Sea_Core(NODE_FLAG_MANAGED);

    if (Misc_Sea_Adjunct(original)) {
        Tweak_Misc_Sea_Adjunct(sea, Copy_Varlist_Shallow_Managed(
            unwrap Misc_Sea_Adjunct(original)
        ));
    }
    else {
        Tweak_Misc_Sea_Adjunct(sea, nullptr);
    }
    Tweak_Link_Inherit_Bind(sea, nullptr);

    SeaOfVars* copy = cast(SeaOfVars*, sea);  // now a well-formed context
    assert(Not_Stub_Flag(copy, DYNAMIC));

    Symbol** psym = Flex_Head(Symbol*, g_symbols.by_hash);
    Symbol** psym_tail = Flex_Tail(Symbol*, g_symbols.by_hash);
    for (; psym != psym_tail; ++psym) {
        if (*psym == nullptr or *psym == &g_symbols.deleted_symbol)
            continue;

        Stub* stub = Misc_Hitch(*psym);
        if (Get_Flavor_Flag(SYMBOL, *psym, HITCH_IS_BIND_STUMP))
            stub = Misc_Hitch(stub);  // skip binding stump

        for (; stub != *psym; stub = Misc_Hitch(stub)) {
            if (original == Info_Patch_Sea(cast(Patch*, stub))) {
                Value* var = Append_Context(copy, *psym);
                Copy_Cell(var, Stub_Cell(stub));
                break;
            }
        }
    }

    return copy;
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
    REBLEN len = Varlist_Len(original);

    Array* varlist = Make_Array_For_Copy(
        FLEX_MASK_VARLIST | NODE_FLAG_MANAGED,
        nullptr,  // original_array, N/A because link/misc used otherwise
        len + extra + 1
    );
    Set_Flex_Len(varlist, Varlist_Len(original) + 1);

    Value* dest = Flex_Head(Value, varlist);

    // The type information and fields in the rootvar (at head of the varlist)
    // get filled in with a copy, but the varlist needs to be updated in the
    // copied rootvar to the one just created.
    //
    Copy_Cell(dest, Varlist_Archetype(original));
    CELL_CONTEXT_VARLIST(dest) = varlist;

    Assert_Flex_Managed(Bonus_Keylist(original));

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
        if (not Is_Antiform(dest)) {
            Clonify(Known_Element(dest), flags, deeply);
        }
    }

    varlist->leader.bits |= FLEX_MASK_VARLIST;

    VarList* copy = cast(VarList*, varlist); // now a well-formed context

    if (extra == 0)
        Tweak_Bonus_Keylist_Shared(
            copy,
            Bonus_Keylist(original)
        );
    else {
        assert(CTX_TYPE(original) != TYPE_FRAME);  // can't expand FRAME!s

        KeyList* keylist = cast(KeyList*, Copy_Flex_At_Len_Extra(
            FLEX_MASK_KEYLIST | NODE_FLAG_MANAGED,
            Bonus_Keylist(original),
            0,
            Varlist_Len(original),
            extra
        ));

        Tweak_Link_Keylist_Ancestor(keylist, Bonus_Keylist(original));

        Tweak_Bonus_Keylist_Unique(copy, keylist);
    }

    // A FRAME! in particular needs to know if it points back to a stack
    // frame.  The pointer is NULLed out when the stack level completes.
    // If we're copying a frame here, we know it's not running.
    //
    if (CTX_TYPE(original) == TYPE_FRAME)
        Tweak_Misc_Varlist_Adjunct(varlist, nullptr);
    else {
        // !!! Should the adjunct object be copied for other context types?
        // Deep copy?  Shallow copy?  Just a reference to the same object?
        //
        Tweak_Misc_Varlist_Adjunct(varlist, nullptr);
    }

    Tweak_Link_Inherit_Bind(varlist, nullptr);

    return copy;
}


IMPLEMENT_GENERIC(MOLDIFY, Any_Context)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    String* s = mo->string;

    Context* c = Cell_Context(v);

    // Prevent endless mold loop:
    //
    if (Find_Pointer_In_Flex(g_mold.stack, c) != NOT_FOUND) {
        if (not form) {
            Begin_Non_Lexical_Mold(mo, v); // If molding, get &[object! etc.
            Append_Codepoint(s, '[');
        }
        Append_Ascii(s, "...");

        if (not form) {
            Append_Codepoint(s, ']');
            End_Non_Lexical_Mold(mo);
        }
        return TRASH;
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
                panic (Error_Bad_Antiform(e.var));  // can't FORM antiforms
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
        return TRASH;
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
            Copy_Lifted_Cell(reified, e.var);  // will become quasi...
            Mold_Element(mo, reified);  // ...molds as `~xxx~`
        }
        else {
            Element* elem = Known_Element(e.var);
            Output_Apostrophe_If_Not_Inert(s, elem);
            Mold_Element(mo, elem);
        }
    }
    Shutdown_Evars(&e);

    mo->indent--;
    New_Indented_Line(mo);
    Append_Codepoint(s, ']');

    End_Non_Lexical_Mold(mo);

    Drop_Pointer_From_Flex(g_mold.stack, c);

    return TRASH;
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Let)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    String* s = mo->string;

    Let* let = Cell_Let(v);

    // Prevent endless mold loop:
    //
    if (Find_Pointer_In_Flex(g_mold.stack, let) != NOT_FOUND) {
        if (not form) {
            Begin_Non_Lexical_Mold(mo, v); // If molding, get &[let! etc.
            Append_Codepoint(s, '[');
        }
        Append_Ascii(s, "...");

        if (not form) {
            Append_Codepoint(s, ']');
            End_Non_Lexical_Mold(mo);
        }
        return TRASH;
    }
    Push_Pointer_To_Flex(g_mold.stack, let);

    const Symbol* spelling = Let_Symbol(let);
    const Value* slot = Let_Slot(let);

    if (form) {
        Append_Spelling(mo->string, spelling);
        Append_Ascii(mo->string, ": ");

        if (Is_Antiform(slot))
            return PANIC(Error_Bad_Antiform(slot));  // can't FORM antiforms

        Mold_Element(mo, Known_Element(slot));

        Drop_Pointer_From_Flex(g_mold.stack, let);
        return TRASH;
    }

    // Otherwise we are molding

    Begin_Non_Lexical_Mold(mo, v);

    Append_Codepoint(s, '[');

    mo->indent++;
    New_Indented_Line(mo);

    DECLARE_ELEMENT (set_word);
    Init_Set_Word(set_word, spelling);  // want escaping, e.g `|::|: 10`

    Mold_Element(mo, set_word);
    Append_Codepoint(mo->string, ' ');

    if (Is_Antiform(slot)) {
        DECLARE_ELEMENT (reified);
        Copy_Lifted_Cell(reified, slot);  // will become quasi...
        Mold_Element(mo, reified);  // ...molds as `~xxx~`
    }
    else {
        const Element* elem = Known_Element(slot);
        Output_Apostrophe_If_Not_Inert(s, elem);
        Mold_Element(mo, elem);
    }

    mo->indent--;
    New_Indented_Line(mo);
    Append_Codepoint(s, ']');

    End_Non_Lexical_Mold(mo);

    Drop_Pointer_From_Flex(g_mold.stack, let);

    return TRASH;
}


const Symbol* Symbol_From_Picker(const Element* context, const Element* picker)
{
    UNUSED(context);  // Might the picker be context-sensitive?

    if (not Is_Word(picker))
        panic (picker);

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
    const Element* context,
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

    if (Is_Module(context))
        return Init_Module(out, Copy_Sea_Managed(Cell_Module_Sea(context)));

    return Init_Context_Cell(
        out,
        Heart_Of_Builtin_Fundamental(context),
        Copy_Varlist_Extra_Managed(Cell_Varlist(context), 0, deep)
    );
}


IMPLEMENT_GENERIC(OLDGENERIC, Any_Context)
{
    Option(SymId) id = Symbol_Id(Level_Verb(LEVEL));

    Element* context = cast(Element*, ARG_N(1));
    Context* c = Cell_Context(context);

    // !!! The PORT! datatype wants things like LENGTH OF to give answers
    // based on the content of the port, not the number of fields in the
    // PORT! object.  This ties into a number of other questions:
    //
    // https://forum.rebol.info/t/1689
    //
    assert(not Is_Port(context));

    switch (id) {

    //=//// PROTECT* ///////////////////////////////////////////////////////=//

      case SYM_APPEND:
        panic ("APPEND on OBJECT!, MODULE!, etc. replaced with EXTEND");

      case SYM_EXTEND: {
        INCLUDE_PARAMS_OF_EXTEND;
        UNUSED(ARG(CONTEXT));
        Element* def = Element_ARG(DEF);

        if (Is_Word(def)) {
            bool strict = true;
            Option(Index) i = Find_Symbol_In_Context(
                context, Cell_Word_Symbol(def), strict
            );
            if (i) {
                CELL_WORD_INDEX_I32(def) = unwrap i;
                if (Is_Module(context))
                    Tweak_Cell_Binding(def, Sea_Patch(
                        cast(SeaOfVars*, c), Cell_Word_Symbol(def), strict
                    ));
                else
                    Tweak_Cell_Binding(def, c);
                return COPY(def);
            }
            Init_Trash(Append_Context_Bind_Word(c, def));
            return COPY(def);
        }

        assert(Is_Block(def));

        CollectFlags flags = COLLECT_ONLY_SET_WORDS;
        if (Bool_ARG(PREBOUND))
            flags |= COLLECT_TOLERATE_PREBOUND;

        Option(Error*) e = Trap_Wrap_Extend_Core(c, def, flags);
        if (e)
            return PANIC(unwrap e);

        Use* use = Alloc_Use_Inherits_Core(
            USE_FLAG_SET_WORDS_ONLY,
            Cell_Binding(def)
        );
        Copy_Cell(Stub_Cell(use), context);

        Tweak_Cell_Binding(def, use);

        bool threw = Eval_Any_List_At_Throws(OUT, def, SPECIFIED);
        if (threw)
            return BOUNCE_THROWN;

        return COPY(context); }

      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_SELECT;
        UNUSED(ARG(SERIES));  // extracted as `c`

        if (Bool_ARG(PART) or Bool_ARG(SKIP) or Bool_ARG(MATCH))
            return PANIC(Error_Bad_Refines_Raw());

        Value* pattern = ARG(VALUE);
        if (Is_Antiform(pattern))
            return PANIC(pattern);

        if (not Is_Word(pattern))
            return nullptr;

        Option(Index) index = Find_Symbol_In_Context(
            context,
            Cell_Word_Symbol(pattern),
            Bool_ARG(CASE)
        );
        if (not index)
            return nullptr;

        if (Is_Stub_Sea(c))
            return PANIC("SeaOfVars SELECT not implemented yet");

        return COPY(Varlist_Slot(cast(VarList*, c), unwrap index)); }

      default:
        break;
    }

    return UNHANDLED;
}


// 1. !!! Cannot convert TO a PORT! without copying the whole context...
//    which raises the question of why convert an object to a port,
//    vs. making it as a port to begin with (?)  Look into why
//    system.standard.port is made with CONTEXT and not with MAKE PORT!
//
IMPLEMENT_GENERIC(TO, Any_Context)
{
    INCLUDE_PARAMS_OF_TO;

    Element* context = Element_ARG(ELEMENT);
    Context* c = Cell_Context(context);
    Heart heart = Heart_Of_Builtin_Fundamental(context);
    Heart to = Cell_Datatype_Builtin_Heart(ARG(TYPE));
    assert(heart != to);  // TO should have called COPY in this case

    if (to == TYPE_PORT) {
        if (heart != TYPE_OBJECT)
            return PANIC(
                "Only TO convert OBJECT! -> PORT! (weird internal code)"
            );

        VarList* v = cast(VarList*, c);
        VarList* copy = Copy_Varlist_Shallow_Managed(v);  // !!! copy [1]
        Value* rootvar = Rootvar_Of_Varlist(copy);
        HEART_BYTE(rootvar) = TYPE_PORT;
        return Init_Port(OUT, copy);
    }

    if (to == heart) {  // can't TO FRAME! an ERROR!, etc.
        bool deep = false;
        return Copy_Any_Context(OUT, context, deep);
    }

    return UNHANDLED;
}


// Note that words are not copied and bindings not changed!
//
IMPLEMENT_GENERIC(COPY, Any_Context)
{
    INCLUDE_PARAMS_OF_COPY;

    const Element* context = Element_ARG(VALUE);

    if (Bool_ARG(PART))
        return PANIC(Error_Bad_Refines_Raw());

    bool deep = Bool_ARG(DEEP);
    return Copy_Any_Context(OUT, context, deep);
}


IMPLEMENT_GENERIC(TWEAK_P, Any_Context)
{
    INCLUDE_PARAMS_OF_TWEAK_P;

    Element* context = Element_ARG(LOCATION);
    possibly(Is_Port(context));

    const Element* picker = Element_ARG(PICKER);
    const Symbol* symbol = Symbol_From_Picker(context, picker);

    const Value* slot = maybe Cell_Context_Slot(context, symbol);
    if (not slot)
        return DUAL_SIGNAL_NULL_ABSENT;

    Value* dual = ARG(DUAL);
    if (Not_Lifted(dual)) {
        if (Is_Dual_Space_Pick_Signal(dual))
            goto handle_pick;

        if (Is_Dual_Word_Named_Signal(dual))
            goto handle_named_signal;

        return PANIC(Error_Bad_Poke_Dual_Raw(dual));  // smart error RE:remove?
    }

    goto handle_poke;

  handle_pick: { /////////////////////////////////////////////////////////////

    Value* out = Copy_Cell(OUT, slot);

    if (
        HEART_BYTE(slot) == TYPE_FRAME
        and QUOTE_BYTE(slot) == ANTIFORM_0
        and Cell_Frame_Coupling(slot) == UNCOUPLED
    ){
        Context* c = Cell_Context(context);
        Tweak_Cell_Frame_Coupling(out, cast(VarList*, c));
    }

    return DUAL_LIFTED(OUT);

} handle_poke: { /////////////////////////////////////////////////////////////

    Value* poke = Unliftify_Known_Stable(dual);

    if (Get_Cell_Flag(slot, PROTECTED))  // POKE, must check PROTECT status
        return PANIC(Error_Protected_Key(symbol));

    Copy_Cell(m_cast(Value*, slot), poke);

    return NO_WRITEBACK_NEEDED;  // VarList* in cell not changed

} handle_named_signal: { /////////////////////////////////////////////////////

    switch (Cell_Word_Id(dual)) {
      case SYM_PROTECT:
        Set_Cell_Flag(slot, PROTECTED);
        break;

      case SYM_UNPROTECT:
        Clear_Cell_Flag(slot, PROTECTED);
        break;

      case SYM_HIDE:
        Set_Cell_Flag(slot, VAR_MARKED_HIDDEN);
        break;

      default:
        return PANIC(Error_Bad_Poke_Dual_Raw(dual));
    }

    return NO_WRITEBACK_NEEDED;  // VarList* in context not changed
}}


// !!! Should this be legal?
//
IMPLEMENT_GENERIC(LENGTH_OF, Any_Context)
{
    INCLUDE_PARAMS_OF_LENGTH_OF;

    Element* context = Element_ARG(ELEMENT);
    Context* c = Cell_Context(context);
    possibly(Is_Port(context));

    if (Is_Stub_Sea(c))
        return PANIC("SeaOfVars length counting code not done yet");
    return Init_Integer(OUT, Varlist_Len(cast(VarList*, c)));
}


//
//  words-of: native:generic [
//
//  "Get the keys of a context or map (should be KEYS-OF)"
//
//      return: [null? block!]
//      element [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(WORDS_OF)
{
    INCLUDE_PARAMS_OF_WORDS_OF;

    return Dispatch_Generic(WORDS_OF, Element_ARG(ELEMENT), LEVEL);
}


IMPLEMENT_GENERIC(WORDS_OF, Any_Context)
{
    INCLUDE_PARAMS_OF_WORDS_OF;

    Element* context = Element_ARG(ELEMENT);
    return Init_Block(OUT, Context_To_Array(context, 1));
}


//
//  values-of: native:generic [
//
//  "Get the values of a context or map (may panic if context has antiforms)"
//
//      return: [null? block!]
//      element [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(VALUES_OF)
{
    INCLUDE_PARAMS_OF_VALUES_OF;

    return Dispatch_Generic(VALUES_OF, Element_ARG(ELEMENT), LEVEL);
}


IMPLEMENT_GENERIC(VALUES_OF, Any_Context)
{
    INCLUDE_PARAMS_OF_WORDS_OF;

    Element* context = Element_ARG(ELEMENT);
    return Init_Block(OUT, Context_To_Array(context, 2));
}



//
//  bytes-of: native:generic [
//
//  "Get the underlying data e.g. of an image or struct as a BLOB! value"
//
//      return: [null? blob!]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(BYTES_OF)
{
    INCLUDE_PARAMS_OF_BYTES_OF;

    return Dispatch_Generic(BYTES_OF, Element_ARG(VALUE), LEVEL);
}


IMPLEMENT_GENERIC(TAIL_Q, Any_Context)
{
    INCLUDE_PARAMS_OF_TAIL_Q;

    Element* context = Element_ARG(ELEMENT);
    Context* c = Cell_Context(context);

    if (Is_Stub_Sea(c))
        return PANIC("SeaOfVars TAIL? not implemented");
    return Init_Logic(OUT, Varlist_Len(cast(VarList*, c)) == 0);
}


// Copying a frame has a little bit more to deal with than copying an object,
// and needs to initialize the lens correctly.
//
IMPLEMENT_GENERIC(COPY, Is_Frame)
{
    INCLUDE_PARAMS_OF_COPY;

    const Element* frame = Element_ARG(VALUE);

    if (Bool_ARG(DEEP))
        return PANIC("COPY/DEEP on FRAME! not implemented");

    if (Bool_ARG(PART))
        return PANIC(Error_Bad_Refines_Raw());

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
    );
}


//
//  parameters-of: native [
//
//  "Get the unspecialized PARAMETER! descriptions for a FRAME! or ACTION?"
//
//      return: "Frame with lens showing only PARAMETER! values"
//          [frame!]
//      frame [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(PARAMETERS_OF)
{
    INCLUDE_PARAMS_OF_PARAMETERS_OF;

    Element* frame = Element_ARG(FRAME);

    return Init_Frame(
        OUT,
        Cell_Frame_Phase(frame),
        ANONYMOUS,
        Cell_Frame_Coupling(frame)
    );
}


//
//  return-of: native [
//
//  "Get the return parameter specification of a frame"
//
//      return: "May be unconstrained (spec: ~null~) or divergent (spec: [])"
//          [parameter!]  ; always returns parameter!, not null [1]
//      frame [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(RETURN_OF)
//
// 1. At one point things like LAMBDA would give null back.  But this led to
//    more combinatorics callers had to handle, so fabricating an unconstrained
//    parameter with no description text is better.  (Review simplifying
//    access to the spec via something like (return.spec of xxx/)
{
    INCLUDE_PARAMS_OF_RETURN_OF;

    Element* frame = Element_ARG(FRAME);
    Phase* phase = Cell_Frame_Phase(frame);

    Details* details = Phase_Details(phase);
    DetailsQuerier* querier = Details_Querier(details);
    if (not (*querier)(OUT, details, SYM_RETURN_OF))
        return PANIC("Frame Details does not offer RETURN (shouldn't happen)");

    return OUT;
}


//
//  body-of: native [
//
//  "Get a loose representation of a function's implementation"
//
//      return: [block! error!]
//      frame [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(BODY_OF)  // !!! should this be SOURCE-OF ?
//
// Getting the "body" of a function is dicey, because it's not a question
// that always has an answer (e.g. what's the "body" of a native? or of
// a specialization?)  But if you're writing a command like SOURCE it's good
// to give as best an answer as you can give.
{
    INCLUDE_PARAMS_OF_BODY_OF;

    Element* frame = Element_ARG(FRAME);
    Phase* phase = Cell_Frame_Phase(frame);

    Details* details = Phase_Details(phase);
    DetailsQuerier* querier = Details_Querier(details);
    if (not (*querier)(OUT, details, SYM_BODY_OF))
        return FAIL("Frame Details does not offer BODY, use TRY for NULL");

    return OUT;
}


//
//  coupling-of: native [
//
//  "Get what object a FRAME! or ACTION? uses to looks up .XXX references"
//
//      return: "Returns TRASH if uncoupled, ~null~ if non-method"
//          [trash? null? object!]
//      frame [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(COUPLING_OF)
{
    INCLUDE_PARAMS_OF_COUPLING_OF;

    Element* frame = Element_ARG(FRAME);
    Option(VarList*) coupling = Cell_Frame_Coupling(frame);

    if (not coupling)  // NONMETHOD
        return nullptr;

    if (unwrap coupling == UNCOUPLED)
        return TRASH;

    return COPY(Varlist_Archetype(unwrap coupling));
}


//
//  label-of: native [
//
//  "Get the cached name a FRAME! or ACTION? was last referred to by"
//
//      return: [null? word!]
//      frame [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(LABEL_OF)
//
// 1. If the frame is executing, we can look at the label in the Level*, which
//    will tell us what the overall execution label would be.  This might be
//    confusing, however...if the phase is drastically different.  Review.
{
    INCLUDE_PARAMS_OF_LABEL_OF;

    Element* frame = Element_ARG(FRAME);

    Option(const Symbol*) label = Cell_Frame_Label_Deep(frame);
    if (label)
        return Init_Word(OUT, unwrap label);

    if (Is_Frame_Details(frame))
        return nullptr;  // not handled by Level lookup

    Phase* phase = Cell_Frame_Phase(frame);
    if (Is_Stub_Details(phase))
        return PANIC("Phase not details error... should this return NULL?");

    Level* L = Level_Of_Varlist_May_Panic(cast(ParamList*, phase));

    label = Try_Get_Action_Level_Label(L);
    if (label)
        return Init_Word(OUT, unwrap label);

    return nullptr;
}


// 1. Heuristic that if the first element of a function's body is an Array
//    with the file and line bits set, then that's what it returns for FILE OF
//    and LINE OF.
//
static void File_Line_Frame_Heuristic(
    Sink(Level*) level,
    Sink(const Source*) source,
    Element* frame
){
    Phase* phase = Cell_Frame_Phase(frame);

    if (Is_Stub_Details(phase)) {
        Details* details = cast(Details*, phase);

        if (  // heuristic check [1]
            Details_Max(details) < 1
            or not Any_List(Details_At(details, 1))
        ){
            *level = nullptr;
            *source = nullptr;
            return;
        }

        *source = Cell_Array(Details_At(details, 1));
        *level = nullptr;
    }
    else { // try to check and see if there's runtime info
        if (Is_Stub_Details(phase)) {
            *level = nullptr;
            *source = nullptr;
            return;
        }

        *source = nullptr;
        *level = Level_Of_Varlist_May_Panic(cast(ParamList*, phase));
    }
}


IMPLEMENT_GENERIC(FILE_OF, Is_Frame)
{
    INCLUDE_PARAMS_OF_FILE_OF;

    Element* frame = Element_ARG(ELEMENT);
    Level* L;
    const Source* a;
    File_Line_Frame_Heuristic(&L, &a, frame);

    if (a) {
        Option(const String*) filename = Link_Filename(a);
        if (filename)
            return Init_File(OUT, unwrap filename);  // !!! URL! vs. FILE! ?
    }

    if (L) {
        Option(const String*) file = File_Of_Level(L);
        if (file)
            return Init_File(OUT, unwrap file);
    }

    return FAIL("File not available for frame");
}


IMPLEMENT_GENERIC(LINE_OF, Is_Frame)
{
    INCLUDE_PARAMS_OF_LINE_OF;

    Element* frame = Element_ARG(ELEMENT);
    Level* L;
    const Source* a;
    File_Line_Frame_Heuristic(&L, &a, frame);

    if (a) {
        if (MISC_SOURCE_LINE(a) != 0)
            return Init_Integer(OUT, MISC_SOURCE_LINE(a));
    }

    if (L) {
        Option(LineNumber) line = Line_Number_Of_Level(L);
        if (line)
            return Init_Integer(OUT, unwrap line);
    }

    return FAIL("Line not available for frame");
}


//
//  near-of: native [
//
//  "Get the near information for an executing frame"
//
//      return: [null? block!]
//      frame [<opt-out> <unrun> frame!]
//  ]
//
DECLARE_NATIVE(NEAR_OF)
{
    INCLUDE_PARAMS_OF_NEAR_OF;

    Element* frame = Element_ARG(FRAME);
    Phase* phase = Cell_Frame_Phase(frame);

    if (Is_Stub_Details(phase))
        return PANIC("Phase is details, can't get NEAR-OF");

    Level* L = Level_Of_Varlist_May_Panic(cast(ParamList*, phase));
    return Init_Near_For_Level(OUT, L);
}


//
//  parent-of: native [
//
//  "Get the frame corresponding to the parent of a frame"
//
//      return: [null? frame!]
//      frame [<opt-out> <unrun> frame!]
//  ]
//
DECLARE_NATIVE(PARENT_OF)
{
    INCLUDE_PARAMS_OF_PARENT_OF;

    Element* frame = Element_ARG(FRAME);
    Phase* phase = Cell_Frame_Phase(frame);

    if (Is_Stub_Details(phase))
        return PANIC("Phase is details, can't get PARENT-OF");

    Level* L = Level_Of_Varlist_May_Panic(cast(ParamList*, phase));
    Level* parent = L;

    while ((parent = parent->prior) != BOTTOM_LEVEL) {
        if (not Is_Action_Level(parent))  // Only want action levels
            continue;

        VarList* v_parent = Varlist_Of_Level_Force_Managed(parent);
        return COPY(Varlist_Archetype(v_parent));
    }
    return nullptr;
}


//
//  CT_Frame: C
//
// !!! What are the semantics of comparison in frames?
//
REBINT CT_Frame(const Element* a, const Element* b, bool strict)
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


IMPLEMENT_GENERIC(EQUAL_Q, Is_Frame)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;
    bool strict = not Bool_ARG(RELAX);

    Element* value1 = Element_ARG(VALUE1);
    Element* value2 = Element_ARG(VALUE2);

    return LOGIC(CT_Frame(value1, value2, strict) == 0);
}


IMPLEMENT_GENERIC(LESSER_Q, Is_Frame)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Element* value1 = Element_ARG(VALUE1);
    Element* value2 = Element_ARG(VALUE2);

    return LOGIC(CT_Frame(value1, value2, true) == 0);
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Frame)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));

    if (QUOTE_BYTE(v) != QUASIFORM_2) {
        return GENERIC_CFUNC(MOLDIFY, Any_Context)(LEVEL);  // heeds Bool_ARG(FORM)
    }

    bool form = Bool_ARG(FORM);
    UNUSED(form);

    Begin_Non_Lexical_Mold(mo, v);

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

    return TRASH;
}


//
//  construct: native [
//
//  "Creates an OBJECT! from a spec that is not bound into the object"
//
//      return: [null? object!]
//      spec "Object spec block, top-level SET-WORD!s will be object keys"
//          [<opt-out> block! @block! fence!]
//      :with "Use a parent/prototype context"
//          [object!]
//  ]
//
DECLARE_NATIVE(CONSTRUCT)
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

    Element* spec = Element_ARG(SPEC);

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
        goto eval_set_step_dual_in_spare;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    VarList* parent = Bool_ARG(WITH)
        ? Cell_Varlist(ARG(WITH))
        : nullptr;

    const Element* tail;
    const Element* at = Cell_List_At(&tail, spec);

    Heart heart;  // using ?: operator breaks in DEBUG_EXTRA_HEART_CHECKS
    if (parent)
        heart = CTX_TYPE(parent);
    else
        heart = TYPE_OBJECT;

    VarList* varlist = Make_Varlist_Detect_Managed(
        COLLECT_ONLY_SET_WORDS,
        heart,  // !!! Presume object?
        at,
        tail,
        parent
    );
    Init_Object(OUT, varlist);  // GC protects context

    Executor* executor;
    if (Is_Pinned(BLOCK, spec))
        executor = &Inert_Stepper_Executor;
    else {
        assert(Is_Block(spec) or Is_Fence(spec));
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
        Tweak_Cell_Binding(TOP_ELEMENT, varlist);
        CELL_WORD_INDEX_I32(TOP_ELEMENT) = unwrap index;

        Fetch_Next_In_Feed(SUBLEVEL->feed);

        if (Is_Level_At_End(SUBLEVEL))
            return PANIC("Unexpected end after SET-WORD! in CONTEXT");

        at = At_Level(SUBLEVEL);
        if (Is_Comma(at))
            return PANIC("Unexpected COMMA! after SET-WORD! in CONTEXT");

    } while ((symbol = Try_Get_Settable_Word_Symbol(nullptr, at)));

    if (not Is_Pinned(BLOCK, spec)) {
        Copy_Cell(Level_Scratch(SUBLEVEL), TOP);
        DROP();

        LEVEL_STATE_BYTE(SUBLEVEL) = ST_STEPPER_REEVALUATING;
    }

    STATE = ST_CONSTRUCT_EVAL_SET_STEP;
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} eval_set_step_dual_in_spare: {  ////////////////////////////////////////////

    Unliftify_Undecayed(SPARE);
    Value* spare = Decay_If_Unstable(SPARE);

    VarList* varlist = Cell_Varlist(OUT);

    while (TOP_INDEX != STACK_BASE) {
        Option(Index) index = CELL_WORD_INDEX_I32(TOP);
        assert(index);  // created a key for every SET-WORD! above!

        Copy_Cell(Varlist_Slot(varlist, unwrap index), spare);

        DROP();
    }

    assert(STATE == ST_CONSTRUCT_EVAL_SET_STEP);
    Reset_Evaluator_Erase_Out(SUBLEVEL);

    goto continue_processing_spec;
}}


//
//  extend: native:generic [
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
DECLARE_NATIVE(EXTEND)
{
    Element* number = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(number, LEVEL, CANON(EXTEND));
}
