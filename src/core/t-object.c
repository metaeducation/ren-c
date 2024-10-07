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


static void Append_Vars_To_Context_From_Group(Value* context, Value* block)
{
    VarList* c = Cell_Varlist(context);

    if (CTX_TYPE(c) == REB_MODULE)
        fail ("Appending KEY/VALUE pairs to modules not working yet");

    assert(Is_Group(block));

    const Element* tail;
    const Element* item = Cell_List_At(&tail, block);

    // Can't actually fail() during a collect, so make sure any errors are
    // set and then jump to a Collect_End()
    //
    Option(Error*) error = nullptr;

    DECLARE_COLLECTOR (cl);
    Collect_Start(
        cl,
        COLLECT_ONLY_SET_WORDS,
        c  // preload binder with words already in context
    );

    REBLEN first_new_index = Collector_Index_If_Pushed(cl);

    // Do a pass to collect the [set-word: <value>] keys and add them to the
    // binder.  But don't modify the object yet, in case the block turns out
    // to be malformed (we don't want partial expansions applied).
    //
    // !!! This allows plain WORD! in the key spot.  Review, this should
    // really be EXTEND!
    //
  blockscope {
    const Element* word;
    for (word = item; word != tail; word += 2) {
        if (not Is_Word(word) and not Is_Set_Word(word)) {
            error = Error_Bad_Value(word);
            goto collect_end;
        }

        const Symbol* symbol = Cell_Word_Symbol(word);

        if (Try_Add_Binder_Index(
            &cl->binder,
            symbol,
            Collector_Index_If_Pushed(cl)
        )){
            Init_Word(PUSH(), Cell_Word_Symbol(word));
        }
        if (word + 1 == tail)  // catch malformed case with no value (#708)
            break;
    }
  }

  blockscope {  // Append new words to obj
    REBLEN num_added = Collector_Index_If_Pushed(cl) - first_new_index;
    Expand_Varlist(c, num_added);

    StackValue(*) new_word = Data_Stack_At(cl->stack_base) + first_new_index;
    for (; new_word != TOP + 1; ++new_word)
        Init_Nothing(Append_Context(c, Cell_Word_Symbol(new_word)));
  }

  blockscope {  // Set new values to obj words
    const Element* word = item;
    for (; word != tail; word += 2) {
        const Symbol* symbol = Cell_Word_Symbol(word);

        REBLEN i = Get_Binder_Index_Else_0(&cl->binder, symbol);
        assert(i != 0);
        assert(*Varlist_Key(c, i) == symbol);
        Value* var = Varlist_Slot(c, i);

        if (Get_Cell_Flag(var, PROTECTED)) {
            error = Error_Protected_Key(symbol);
            goto collect_end;
        }

        // !!! There was discussion in R3-Alpha that errors which exposed the
        // existence of hidden variables were bad in a "security" sense,
        // because they were supposed to be effectively "not there".  Putting
        // security aside; once a variable has been hidden from binding, is
        // there a reason to disallow a new variable of that name from being
        // added to the context?  Functions are being rigged up to allow the
        // addition of public parameters that overlap the names of private
        // fields on the black box internals...perhaps contexts should too?
        //
        if (Get_Cell_Flag(var, VAR_MARKED_HIDDEN)) {
            error = Error_Hidden_Raw();
            goto collect_end;
        }

        if (word + 1 == tail) {
            Init_Nothing(var);
            break;  // fix bug#708
        }
        else
            Copy_Cell(var, &word[1]);
    }
  }

  collect_end:
    Collect_End(cl);

    if (error)
        fail (unwrap error);
}


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
//////////////////////////////////////////////////////////////////////////////
//
//
// 1. There's no clear best answer to whether the locals should be visible when
//    enumerating an action, only the caller knows if it's a context where they
//    should be.  Guess conservatively and let them set e->visibility if they
//    think they should see more.
//
// 2. !!! Module enumeration is slow, and you should not do it often...it
//    requires walking over the global word table.  The global table gets
//    rehashed in a way that we'd have a hard time maintainining a consistent
//    enumerator state in the current design.  So for the moment we fabricate
//    an array to enumerate.  The enumeration won't see changes.
//
// 3. The frame can be phaseless, which means it is not running (such as the
//    direct result of a MAKE FRAME! call, which is awaiting an EVAL to start).
//    These frames should only show variables on the public interface.
//
// 4. Since phases can reuse exemplars, we have to check for an exact match of
//    the action of the exemplar with the phase in order to know if the locals
//    should be visible.  If you ADAPT a function that reuses its exemplar,
//    but should not be able to see the locals (for instance).
//
void Init_Evars(EVARS *e, const Cell* v) {
    Heart heart = Cell_Heart(v);

    e->visibility = VAR_VISIBILITY_ALL;  // ensure not uninitialized

    if (heart == REB_FRAME and Is_Frame_Details(v)) {
        e->index = 0;  // will be bumped to 1

        Corrupt_Pointer_If_Debug(e->ctx);

        Action* act = VAL_ACTION(v);
        e->key = ACT_KEYS(&e->key_tail, act) - 1;
        e->var = nullptr;
        e->param = ACT_PARAMS_HEAD(act) - 1;

        assert(Flex_Used(ACT_KEYLIST(act)) <= ACT_NUM_PARAMS(act));

        e->visibility = VAR_VISIBILITY_INPUTS;  // conservative guess [1]

        Corrupt_Pointer_If_Debug(e->wordlist);
        e->word = nullptr;
        Corrupt_Pointer_If_Debug(e->word_tail);
    }
    else if (heart == REB_MODULE) {  // !!! module enumeration is bad/slow [2]
        e->index = INDEX_PATCHED;

        e->ctx = Cell_Varlist(v);

        StackIndex base = TOP_INDEX;

        Symbol** psym = Flex_Head(Symbol*, g_symbols.by_hash);
        Symbol** psym_tail = Flex_Tail(Symbol*, g_symbols.by_hash);
        for (; psym != psym_tail; ++psym) {
            if (*psym == nullptr or *psym == &g_symbols.deleted_symbol)
                continue;

            Stub* patch = MISC(Hitch, *psym);
            if (Get_Subclass_Flag(SYMBOL, *psym, MISC_IS_BINDINFO))
                patch = cast(Stub*, node_MISC(Hitch, patch));  // skip bindinfo

            Flex* found = nullptr;

            for (
                ;
                patch != *psym;
                patch = cast(Stub*, node_MISC(Hitch, patch))
            ){
                if (e->ctx == INODE(PatchContext, patch)) {
                    found = patch;
                    break;
                }
             /*   if (Lib_Context == INODE(PatchContext, patch))
                    found = patch;  // will match if not overridden */
            }
            if (found) {
                Init_Any_Word(PUSH(), REB_WORD, *psym);
                Tweak_Cell_Word_Index(TOP, INDEX_PATCHED);
                BINDING(TOP) = found;
            }
        }

        e->wordlist = Pop_Stack_Values_Core(base, NODE_FLAG_MANAGED);
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
            e->var = Varlist_Slots_Head(e->ctx) - 1;

            Phase* phase;
            if (not IS_FRAME_PHASED(v)) {  // not running, inputs visible [3]
                phase = CTX_FRAME_PHASE(e->ctx);

                Array* varlist = Varlist_Array(e->ctx);
                if (Get_Subclass_Flag(
                    VARLIST,
                    varlist,
                    FRAME_HAS_BEEN_INVOKED  // optimization, see definition
                )){
                    e->visibility = VAR_VISIBILITY_NONE;
                } else
                    e->visibility = VAR_VISIBILITY_INPUTS;
            }
            else {  // is running, phase determines field visibility
                phase = VAL_FRAME_PHASE(v);

                VarList* exemplar = ACT_EXEMPLAR(phase);
                if (CTX_FRAME_PHASE(exemplar) == phase)  // phase reuses [4]
                    e->visibility = VAR_VISIBILITY_ALL;
                else
                    e->visibility = VAR_VISIBILITY_INPUTS;
            }

            Action* action = phase;
            e->param = ACT_PARAMS_HEAD(action) - 1;
            e->key = ACT_KEYS(&e->key_tail, action) - 1;
            assert(Flex_Used(ACT_KEYLIST(action)) <= ACT_NUM_PARAMS(action));
        }

        Corrupt_Pointer_If_Debug(e->wordlist);
        e->word = nullptr;
        UNUSED(e->word_tail);
    }

  #if DEBUG
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
// 2. !!! Unfortunately, the code for associating comments with return and
//    output parameters uses a FRAME! for the function to do it.  This means
//    that it expects keys for those values as public.  A rethought mechanism
//    will be needed to keep HELP working if we actually suppress these from
//    the "input" view of a FRAME!.
//
bool Try_Advance_Evars(EVARS *e) {
    if (e->visibility == VAR_VISIBILITY_NONE)
        return false;

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

            if (e->visibility == VAR_VISIBILITY_ALL)
                return true;  // private sees ONE level of specialization

            if (Is_Specialized(e->param))  // parameter replaced with the value
                continue;  // public should not see specialized args

            if (e->visibility == VAR_VISIBILITY_INPUTS) {
              #if 0
                ParamClass pclass = Cell_ParamClass(e->param);
                if (pclass == PARAMCLASS_RETURN)  // false "input" [2]
                    continue;
              #endif
            }
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
      #if DEBUG
        assert(Is_Pointer_Corrupt_Debug(e->wordlist));
      #endif
    }

  #if DEBUG
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
//  MAKE_Frame: C
//
// !!! The feature of MAKE FRAME! from a VARARGS! would be interesting as a
// way to support usermode authoring of things like MATCH.
//
// For now just support ACTION! (or path/word to specify an action)
//
Bounce MAKE_Frame(
    Level* level_,
    Kind kind,
    Option(const Value*) parent,
    const Value* arg
){
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap parent));

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
            if (not Is_Block_Style_Varargs(&shared, arg))
                fail ("Expected BLOCK!-style varargs");  // shouldn't happen

            feed = Make_At_Feed_Core(shared, SPECIFIED);
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
        return RAISE(Error_Bad_Make(kind, arg));

    VarList* exemplar = Make_Varlist_For_Action(
        arg,  // being used here as input (e.g. the ACTION!)
        lowest_stackindex,  // will weave in any refinements pushed
        nullptr  // no binder needed, not running any code
    );

    // See notes in %c-specialize.c about the special encoding used to
    // put /REFINEMENTs in refinement slots (instead of true/false/null)
    // to preserve the order of execution.

    return Init_Frame(OUT, exemplar, VAL_FRAME_LABEL(arg));
}


//
//  TO_Frame: C
//
// Currently can't convert anything TO a frame; nothing has enough information
// to have an equivalent representation (an OBJECT! could be an expired frame
// perhaps, but still would have no ACTION OF property)
//
Bounce TO_Frame(Level* level_, Kind kind, const Value* arg)
{
    return RAISE(Error_Bad_Make(kind, arg));
}


//
//  MAKE_Context: C
//
Bounce MAKE_Context(
    Level* level_,
    Kind k,
    Option(const Value*) parent,
    const Value* arg
){
    // Other context kinds (LEVEL!, ERROR!, PORT!) have their own hooks.
    //
    assert(k == REB_OBJECT or k == REB_MODULE);
    Heart heart = cast(Heart, k);

    if (heart == REB_MODULE) {
        if (not Any_List(arg))
            return RAISE("Currently only (MAKE MODULE! LIST) is allowed");

        assert(not parent);

        VarList* ctx = Alloc_Varlist_Core(REB_MODULE, 1, NODE_FLAG_MANAGED);
        node_LINK(NextVirtual, ctx) = BINDING(arg);
        return Init_Context_Cell(OUT, REB_MODULE, ctx);
    }

    Option(VarList*) parent_ctx = parent
        ? Cell_Varlist(unwrap parent)
        : nullptr;

    if (Is_Block(arg)) {
        const Element* tail;
        const Element* at = Cell_List_At(&tail, arg);

        VarList* ctx = Make_Varlist_Detect_Managed(
            COLLECT_ONLY_SET_WORDS,
            heart,
            at,
            tail,
            parent_ctx
        );
        Init_Context_Cell(OUT, heart, ctx); // GC guards it

        DECLARE_VALUE (virtual_arg);
        Copy_Cell(virtual_arg, arg);

        Virtual_Bind_Deep_To_Existing_Context(
            virtual_arg,
            ctx,
            nullptr,  // !!! no binder made at present
            CELL_MASK_0  // all internal refs are to the object
        );

        DECLARE_ATOM (dummy);
        if (Eval_Any_List_At_Throws(dummy, virtual_arg, SPECIFIED))
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
            parent_ctx
        );

        return Init_Context_Cell(OUT, heart, context);
    }

    if (parent)
        return RAISE(Error_Bad_Make_Parent(heart, unwrap parent));

    // make object! map!
    if (Is_Map(arg)) {
        VarList* c = Alloc_Varlist_From_Map(VAL_MAP(arg));
        return Init_Context_Cell(OUT, heart, c);
    }

    return RAISE(Error_Bad_Make(heart, arg));
}


//
//  TO_Context: C
//
Bounce TO_Context(Level* level_, Kind kind, const Value* arg)
{
    // Other context kinds (LEVEL!, ERROR!, PORT!) have their own hooks.
    //
    assert(kind == REB_OBJECT or kind == REB_MODULE);

    if (kind == REB_OBJECT) {
        //
        // !!! Contexts hold canon values now that are typed, this init
        // will assert--a TO conversion would thus need to copy the varlist
        //
        return Init_Object(OUT, Cell_Varlist(arg));
    }

    return RAISE(Error_Bad_Make(kind, arg));
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

    VarList* meta;
    if (Is_Frame(v)) {
        if (not Is_Frame_Details(v))
            return nullptr;

        meta = ACT_ADJUNCT(VAL_ACTION(v));
    }
    else {
        assert(Any_Context(v));
        meta = CTX_ADJUNCT(Cell_Varlist(v));
    }

    if (not meta)
        return nullptr;

    return COPY(Varlist_Archetype(meta));
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

    VarList* ctx;
    if (Any_Context(adjunct)) {
        if (Is_Frame(adjunct))
            fail ("SET-ADJUNCT can't store bindings, FRAME! disallowed");

        ctx = Cell_Varlist(adjunct);
    }
    else {
        assert(Is_Nulled(adjunct));
        ctx = nullptr;
    }

    Value* v = ARG(value);

    if (Is_Frame(v)) {
        if (Is_Frame_Details(v))
            MISC(DetailsAdjunct, ACT_IDENTITY(VAL_ACTION(v))) = ctx;
    }
    else
        MISC(VarlistAdjunct, Varlist_Array(Cell_Varlist(v))) = ctx;

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
        len + extra + 1,
        FLEX_MASK_VARLIST | NODE_FLAG_MANAGED,
        nullptr // original_array, N/A because LINK()/MISC() used otherwise
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

        if (CTX_ADJUNCT(original)) {
            MISC(VarlistAdjunct, varlist) = Copy_Varlist_Shallow_Managed(
                CTX_ADJUNCT(original)
            );
        }
        else {
            MISC(VarlistAdjunct, varlist) = nullptr;
        }
        Tweak_Bonus_Keysource(varlist, nullptr);
        node_LINK(NextVirtual, varlist) = nullptr;

        VarList* copy = cast(VarList*, varlist); // now a well-formed context
        assert(Get_Flex_Flag(varlist, DYNAMIC));

        Symbol** psym = Flex_Head(Symbol*, g_symbols.by_hash);
        Symbol** psym_tail = Flex_Tail(Symbol*, g_symbols.by_hash);
        for (; psym != psym_tail; ++psym) {
            if (*psym == nullptr or *psym == &g_symbols.deleted_symbol)
                continue;

            Stub* patch = MISC(Hitch, *psym);
            if (Get_Subclass_Flag(SYMBOL, *psym, MISC_IS_BINDINFO))
                patch = MISC(Hitch, patch);  // skip bindinfo

            for (
                ;
                patch != *psym;
                patch = cast(Stub*, node_MISC(Hitch, patch))
            ){
                if (original == INODE(PatchContext, patch)) {
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
            CELL_MASK_COPY | CELL_FLAG_VAR_MARKED_HIDDEN
        );

        Flags flags = NODE_FLAG_MANAGED;  // !!! Review, which flags?
        Clonify(dest, flags, deeply);
    }

    varlist->leader.bits |= FLEX_MASK_VARLIST;

    VarList* copy = cast(VarList*, varlist); // now a well-formed context

    if (extra == 0)
        Tweak_Keylist_Of_Varlist_Shared(
            Varlist_Array(copy),
            Keylist_Of_Varlist(original)
        );
    else {
        assert(CTX_TYPE(original) != REB_FRAME);  // can't expand FRAME!s

        KeyList* keylist = cast(KeyList*, Copy_Flex_At_Len_Extra(
            Keylist_Of_Varlist(original),
            0,
            Varlist_Len(original),
            extra,
            FLEX_MASK_KEYLIST | NODE_FLAG_MANAGED
        ));

        LINK(Ancestor, keylist) = Keylist_Of_Varlist(original);

        Tweak_Keylist_Of_Varlist_Unique(Varlist_Array(copy), keylist);
    }

    // A FRAME! in particular needs to know if it points back to a stack
    // frame.  The pointer is NULLed out when the stack level completes.
    // If we're copying a frame here, we know it's not running.
    //
    if (CTX_TYPE(original) == REB_FRAME)
        MISC(VarlistAdjunct, varlist) = nullptr;
    else {
        // !!! Should the meta object be copied for other context types?
        // Deep copy?  Shallow copy?  Just a reference to the same object?
        //
        MISC(VarlistAdjunct, varlist) = nullptr;
    }

    node_LINK(NextVirtual, varlist) = nullptr;

    return copy;
}


//
//  MF_Context: C
//
void MF_Context(REB_MOLD *mo, const Cell* v, bool form)
{
    String* s = mo->string;

    VarList* c = Cell_Varlist(v);

    // Prevent endless mold loop:
    //
    if (Find_Pointer_In_Flex(g_mold.stack, c) != NOT_FOUND) {
        if (not form) {
            Pre_Mold(mo, v); // If molding, get #[object! etc.
            Append_Codepoint(s, '[');
        }
        Append_Ascii(s, "...");

        if (not form) {
            Append_Codepoint(s, ']');
            End_Mold(mo);
        }
        return;
    }
    Push_Pointer_To_Flex(g_mold.stack, c);

    if (Cell_Heart(v) == REB_FRAME and not IS_FRAME_PHASED(v)) {
        Array* varlist = Varlist_Array(Cell_Varlist(v));
        if (Get_Subclass_Flag(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED)) {
            Append_Ascii(s, "make frame! [...invoked frame...]\n");
            Drop_Pointer_From_Flex(g_mold.stack, c);
            return;
        }
    }

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

    Pre_Mold(mo, v);

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

    End_Mold(mo);

    Drop_Pointer_From_Flex(g_mold.stack, c);
}


const Symbol* Symbol_From_Picker(const Value* context, const Value* picker)
{
    UNUSED(context);  // Might the picker be context-sensitive?

    if (not Is_Word(picker))
        fail (picker);

    return Cell_Word_Symbol(picker);
}


//
//  REBTYPE: C
//
// Handles object!, module!, and error! datatypes.
//
REBTYPE(Context)
{
    Value* context = D_ARG(1);
    VarList* c = Cell_Varlist(context);

    Option(SymId) symid = Symbol_Id(verb);

    // !!! The PORT! datatype wants things like LENGTH OF to give answers
    // based on the content of the port, not the number of fields in the
    // PORT! object.  This ties into a number of other questions:
    //
    // https://forum.rebol.info/t/1689
    //
    // At the moment only PICK* and POKE* are routed here.
    //
    if (Is_Port(context))
        assert(symid == SYM_PICK_P or symid == SYM_POKE_P);

    switch (symid) {
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

        fail (Error_Cannot_Reflect(VAL_TYPE(context), property)); }

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        const Symbol* symbol = Symbol_From_Picker(context, picker);

        const Value* var = TRY_VAL_CONTEXT_VAR(context, symbol);
        if (not var)
            return RAISE(Error_Bad_Pick_Raw(picker));

        Copy_Cell(OUT, var);

        if (HEART_BYTE(var) == REB_FRAME)
            Tweak_Cell_Frame_Coupling(OUT, c);

        return OUT; }


    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE_P: {
        INCLUDE_PARAMS_OF_POKE_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        const Symbol* symbol = Symbol_From_Picker(context, picker);

        Value* setval = ARG(value);

        Value* var = TRY_VAL_CONTEXT_MUTABLE_VAR(context, symbol);
        if (not var)
            fail (Error_Bad_Pick_Raw(picker));

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
            fail (Error_Bad_Pick_Raw(picker));

        if (not Is_Word(setval))
            fail ("PROTECT* currently takes just WORD!");

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
            fail (var);
        }

        return nullptr; }  // caller's VarList* is not stale, no update needed

      case SYM_APPEND: {
        Value* arg = D_ARG(2);
        if (Is_Void(arg))
            return COPY(context);  // don't fail on R/O if it would be a no-op

        Ensure_Mutable(context);
        if (not Is_Object(context) and not Is_Module(context))
            fail ("APPEND only works on OBJECT! and MODULE! contexts");

        if (Is_Splice(arg)) {
            QUOTE_BYTE(arg) = NOQUOTE_1;  // make plain group
        }
        else if (Any_Word(arg)) {
            // Add an unset word: `append context 'some-word`
            const bool strict = true;
            if (not Find_Symbol_In_Context(
                context,
                Cell_Word_Symbol(arg),
                strict
            )){
                Init_Nothing(Append_Context(c, Cell_Word_Symbol(arg)));
            }
            return COPY(context);
        }
        else
            fail (arg);

        Append_Vars_To_Context_From_Group(context, arg);
        return COPY(context); }

      case SYM_COPY: {  // Note: words are not copied and bindings not changed!
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PARAM(value));  // covered by `context`

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        // !!! Special attention on copying frames is going to be needed,
        // because copying a frame will be expected to create a new identity
        // for an ACTION! if that frame is aliased AS ACTION!.  The design
        // of this is still evolving, but we don't want archetypal values
        // otherwise we could not `do copy f`, so initialize with label.
        //
        if (Is_Frame(context)) {
            return Init_Frame(
                OUT,
                Copy_Varlist_Extra_Managed(c, 0, did REF(deep)),
                VAL_FRAME_LABEL(context)
            );
        }

        return Init_Context_Cell(
            OUT,
            Cell_Heart_Ensure_Noquote(context),
            Copy_Varlist_Extra_Managed(c, 0, did REF(deep))
        ); }

      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_SELECT;
        UNUSED(ARG(series));  // extracted as `c`

        if (REF(part) or REF(skip) or REF(match))
            fail (Error_Bad_Refines_Raw());

        Value* pattern = ARG(value);
        if (Is_Antiform(pattern))
            fail (pattern);

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

    fail (UNHANDLED);
}


//
//  REBTYPE: C
//
// FRAME! adds some additional reflectors to the usual things you can do with
// an object, but falls through to REBTYPE(Context) for most things.
//
REBTYPE(Frame)
{
    Value* frame = D_ARG(1);
    VarList* c = Cell_Varlist(frame);

    Option(SymId) symid = Symbol_Id(verb);

    switch (symid) {

      case SYM_REFLECT :
        if (Is_Frame_Details(frame))
            goto handle_reflect_action;
      {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // covered by `frame`

        Option(SymId) prop = Cell_Word_Id(ARG(property));

        if (prop == SYM_LABEL) {
            //
            // Can be answered for frames that have no execution phase, if
            // they were initialized with a label.
            //
            Option(const Symbol*) label = VAL_FRAME_LABEL(frame);
            if (label)
                return Init_Word(OUT, unwrap label);

            // If the frame is executing, we can look at the label in the
            // Level*, which will tell us what the overall execution label
            // would be.  This might be confusing, however...if the phase
            // is drastically different.  Review.
        }

       /* if (prop == SYM_ACTION) {
            //
            // Currently this can be answered for any frame, even if it is
            // expired...though it probably shouldn't do this unless it's
            // an indefinite lifetime object, so that paramlists could be
            // GC'd if all the frames pointing to them were expired but still
            // referenced somewhere.
            //
            return Init_Frame_Details(
                OUT,
                VAL_FRAME_PHASE(frame),  // just a Action*, no binding
                VAL_FRAME_LABEL(frame),
                Cell_Frame_Coupling(frame)  // e.g. where RETURN returns to
            );
        } */

        if (prop == SYM_WORDS)
            return T_Context(level_, verb);

        if (prop == SYM_PARAMETERS) {
            Init_Frame_Details(
                ARG(value),
                CTX_FRAME_PHASE(c),
                VAL_FRAME_LABEL(frame),
                Cell_Frame_Coupling(frame)
            );
            goto handle_reflect_action;
        }

        Level* L = Level_Of_Varlist_May_Fail(c);

        switch (prop) {
          case SYM_FILE: {
            Option(const String*) file = File_Of_Level(L);
            if (not file)
                return nullptr;
            return Init_File(OUT, unwrap file); }

          case SYM_LINE: {
            LineNumber line = LineNumber_Of_Level(L);
            if (line == 0)
                return nullptr;
            return Init_Integer(OUT, line); }

          case SYM_LABEL: {
            if (not L->label)
                return nullptr;
            return Init_Word(OUT, unwrap L->label); }

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
      }

    handle_reflect_action: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));

        Phase* act = cast(Phase*, VAL_ACTION(frame));
        assert(Is_Stub_Details(act));

        Value* property = ARG(property);
        Option(SymId) sym = Cell_Word_Id(property);
        switch (sym) {
          case SYM_BINDING: {
            if (Try_Get_Binding_Of(OUT, frame))
                return OUT;
            return nullptr; }

          case SYM_LABEL: {
            Option(const Symbol*) label = VAL_FRAME_LABEL(frame);
            if (not label)
                return nullptr;
            return Init_Word(OUT, unwrap label); }

          case SYM_WORDS:
          case SYM_PARAMETERS: {
            bool just_words = (sym == SYM_WORDS);
            return Init_Block(
                OUT,
                Make_Action_Parameters_Arr(act, just_words)
            ); }

          case SYM_BODY:
            Get_Maybe_Fake_Action_Body(OUT, frame);
            return OUT;

          case SYM_EXEMPLAR: {
            //
            // We give back the exemplar of the frame, which contains the
            // parameter descriptions.  Since exemplars are reused, this is
            // not enough to make the right action out of...so the phase has
            // to be set to the action that we are returning.
            //
            // !!! This loses the label information.  Technically the space
            // for the varlist could be reclaimed in this case and a label
            // used, as the read-only frame is archetypal.
            //
            Reset_Cell_Header_Untracked(TRACK(OUT), CELL_MASK_FRAME);
            Tweak_Cell_Context_Varlist(OUT, ACT_PARAMLIST(act));
            Tweak_Cell_Frame_Coupling(OUT, Cell_Frame_Coupling(frame));
            Tweak_Cell_Frame_Phase_Or_Label(OUT, act);
            return OUT; }

          case SYM_TYPES:
            return Copy_Cell(OUT, Varlist_Archetype(ACT_EXEMPLAR(act)));

          case SYM_FILE:
          case SYM_LINE: {
            //
            // Use a heuristic that if the first element of a function's body
            // is an Array with the file and line bits set, then that's what
            // it returns for FILE OF and LINE OF.

            Details* details = Phase_Details(act);
            if (Array_Len(details) < 1 or not Any_List(Array_Head(details)))
                return nullptr;

            const Array* a = Cell_Array(Array_Head(details));
            if (Not_Array_Flag(a, HAS_FILE_LINE_UNMASKED))
                return nullptr;

            // !!! How to tell URL! vs FILE! ?
            //
            if (Cell_Word_Id(property) == SYM_FILE)
                Init_File(OUT, LINK(Filename, a));
            else
                Init_Integer(OUT, a->misc.line);

            return OUT; }

          default:
            fail (Error_Cannot_Reflect(REB_FRAME, property));
        }
        break; }


  //=//// COPY /////////////////////////////////////////////////////////////=//

    // Being able to COPY functions was added so that you could create a new
    // function identity which behaved the same as an existing function, but
    // kept working if the original function was HIJACK'ed.  (See %c-hijack.c)
    // To do this means being able to create an independent identity that can
    // run the same code without needing to invoke the prior identity to do so.
    //
    // (By contrast: specialization also creates a new identity, but then falls
    // through via a reference to the old identity to run the implementation.
    // Hence hijacking a function that has been specialized will hijack all of
    // its specializations.)
    //
    // Originally COPY was done just by copying the details array.  But that
    // puts two copies of the details array in play--which can be technically
    // dangerous, since the relationship between a function dispatcher and its
    // details is currently treated as a black box.  (The array could contain a
    // reference to an arbitrary C pointer, which might get freed in one clone
    // with an extant reference still lingering in the other.)
    //
    // The modified solution tweaks it so that the identity array for an
    // action is not necessarily where it looks for its Phase_Details(), with
    // the details instead coming out of the archetype slot [0] of that array.
    //
    // !!! There are higher-level interesting mechanics that might be called
    // COPY that aren't covered at all here.  For instance: Someone might like
    // to have a generator that counts from 1 to 10 that is at 5, and be able
    // to COPY it...then have two generators that will count from 5 to 10
    // independently.  That requires methodization and cooperation with the
    // specific dispatcher.

      case SYM_COPY:
        if (Is_Frame_Exemplar(frame))
            break;
      {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PARAM(value));

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        if (REF(deep)) {
            // !!! always "deep", allow it?
        }

        Phase* act = cast(Phase*, VAL_ACTION(frame));

        // If the function had code, then that code will be bound relative
        // to the original paramlist that's getting hijacked.  So when the
        // proxy is called, we want the frame pushed to be relative to
        // whatever underlied the function...even if it was foundational
        // so `underlying = VAL_ACTION(value)`

        Phase* proxy = Make_Action(
            ACT_PARAMLIST(act),  // not changing the interface
            ACT_PARTIALS(act),  // keeping partial specializations
            ACT_DISPATCHER(act),  // have to preserve in case original hijacked
            1  // copy doesn't need details of its own, just archetype
        );

        VarList* meta = ACT_ADJUNCT(act);
        assert(ACT_ADJUNCT(proxy) == nullptr);
        mutable_ACT_ADJUNCT(proxy) = meta;  // !!! Note: not a copy of meta

        // !!! Do this with masking?

        if (Get_Action_Flag(act, IS_NATIVE))
            Set_Action_Flag(proxy, IS_NATIVE);
        if (Get_Action_Flag(act, DEFERS_LOOKBACK))
            Set_Action_Flag(proxy, DEFERS_LOOKBACK);
        if (Get_Action_Flag(act, POSTPONES_ENTIRELY))
            Set_Action_Flag(proxy, POSTPONES_ENTIRELY);

        Clear_Cell_Flag(Phase_Archetype(proxy), PROTECTED);  // changing it
        Copy_Cell(Phase_Archetype(proxy), Phase_Archetype(act));
        Set_Cell_Flag(Phase_Archetype(proxy), PROTECTED);  // restore invariant

        return Init_Frame_Details(
            OUT,
            proxy,
            VAL_FRAME_LABEL(frame),  // keep symbol (if any) from original
            Cell_Frame_Coupling(frame)  // same (e.g. RETURN to same frame)
        ); }

      default:
        break;
    }

    return T_Context(level_, verb);
}


// Temporarily unused... COUPLING is being worked on and breaking CATCH/QUIT
// in comparisons, when actions are used for identity.
//
/*static bool Same_Action(const Cell* a, const Cell* b)
{
    assert(Cell_Heart(a) == REB_FRAME and Cell_Heart(b) == REB_FRAME);
    if (not Is_Frame_Details(a) or not Is_Frame_Details(b))
        return false;

    if (VAL_ACTION_KEYLIST(a) == VAL_ACTION_KEYLIST(b)) {
        //
        // All actions that have the same paramlist are not necessarily the
        // "same action".  For instance, every RETURN shares a common
        // paramlist, but the binding is different in the cell instances
        // in order to know where to "exit from".
        //
        return Cell_Frame_Coupling(a) == Cell_Frame_Coupling(b);
    }

    return false;
}*/


//
//  CT_Frame: C
//
REBINT CT_Frame(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);  // no lax form of comparison

    if (Is_Frame_Details(a)) {
        if (not Is_Frame_Details(b))
            return -1;
        /*if (Same_Action(a, b))
            return 0; */  // REVIEW: Coupling, interferes with CATCH/QUIT
        if (VAL_ACTION(a) == VAL_ACTION(b))
            return 0;
        return a > b ? 1 : -1;  // !!! Review arbitrary ordering
    }

    if (not Is_Frame_Exemplar(b))
        return 1;

    return CT_Context(a, b, strict);
}


//
//  MF_Frame: C
//
void MF_Frame(REB_MOLD *mo, const Cell* v, bool form) {

    if (Is_Frame_Exemplar(v)) {
        MF_Context(mo, v, form);
        return;
    }

    Append_Ascii(mo->string, "#[frame! ");

    Option(const Symbol*) label = VAL_FRAME_LABEL(v);
    if (label) {
        Append_Codepoint(mo->string, '{');
        Append_Spelling(mo->string, unwrap label);
        Append_Ascii(mo->string, "} ");
    }

    // !!! The system is no longer keeping the spec of functions, in order
    // to focus on a generalized "meta info object" service.  MOLD of
    // functions temporarily uses the word list as a substitute (which
    // drops types)
    //
    const bool just_words = false;
    Array* parameters = Make_Action_Parameters_Arr(VAL_ACTION(v), just_words);
    Mold_Array_At(mo, parameters, 0, "[]");
    Free_Unmanaged_Flex(parameters);

    // !!! Previously, ACTION! would mold the body out.  This created a large
    // amount of output, and also many function variations do not have
    // ordinary "bodies".  It's more useful to show the cached name, and maybe
    // some base64 encoding of a UUID (?)  In the meantime, having the label
    // of the last word used is actually a lot more useful than most things.

    Append_Codepoint(mo->string, ']');
    End_Mold(mo);
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

    enum {
        ST_CONSTRUCT_INITIAL_ENTRY = STATE_0,
        ST_CONSTRUCT_EVAL_STEP
    };

    switch (STATE) {
      case ST_CONSTRUCT_INITIAL_ENTRY: goto initial_entry;
      case ST_CONSTRUCT_EVAL_STEP: goto eval_step_result_in_spare;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Value* spec = ARG(spec);

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

    Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE;

    if (Is_The_Block(spec))
        flags |= EVAL_EXECUTOR_FLAG_NO_EVALUATIONS;
    else
        assert(Is_Block(spec));

    Level* sub = Make_Level_At(&Stepper_Executor, spec, flags);
    Push_Level(SPARE, sub);

} continue_processing_spec: {  ////////////////////////////////////////////////

    while (Not_Level_At_End(SUBLEVEL)) {
        const Element* at = At_Level(SUBLEVEL);
        if (Is_Comma(at)) {
            Fetch_Next_In_Feed(SUBLEVEL->feed);
            continue;
        }

        if (not Is_Set_Word(at))
            fail (Error_Invalid_Type(VAL_TYPE(at)));

        do {  // keep pushing SET-WORD!s so `construct [a: b: 1]` works
            assert(Is_Set_Word(at));
            Copy_Cell(PUSH(), at);

            Fetch_Next_In_Feed(SUBLEVEL->feed);

            if (Is_Level_At_End(SUBLEVEL))
                fail ("Unexpected end after SET-WORD! in CONTEXT");

            at = At_Level(SUBLEVEL);
            if (Is_Comma(at))
                fail ("Unexpected COMMA! after SET-WORD! in CONTEXT");

        } while (Is_Set_Word(at));

        STATE = ST_CONSTRUCT_EVAL_STEP;
        return CONTINUE_SUBLEVEL(SUBLEVEL);
    }

    Drop_Level(SUBLEVEL);

    return OUT;

} eval_step_result_in_spare: {  ///////////////////////////////////////////////

    VarList* varlist = Cell_Varlist(OUT);

    while (TOP_INDEX != BASELINE->stack_base) {
        assert(Is_Set_Word(TOP));

        Option(Index) index = Find_Symbol_In_Context(
            Varlist_Archetype(varlist),
            Cell_Word_Symbol(TOP),
            true
        );
        assert(index);  // created a key for every SET-WORD! above!

        Copy_Cell(Varlist_Slot(varlist, unwrap index), stable_SPARE);

        DROP();
    }

    assert(STATE == ST_CONSTRUCT_EVAL_STEP);
    Restart_Stepper_Level(SUBLEVEL);

    goto continue_processing_spec;
}}
