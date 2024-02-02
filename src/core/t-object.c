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


static void Append_Vars_To_Context_From_Group(REBVAL *context, REBVAL *block)
{
    Context* c = VAL_CONTEXT(context);

    assert(Is_Group(block));

    const Element* tail;
    const Element* item = Cell_Array_At(&tail, block);

    struct Reb_Collector collector;
    //
    // Can't actually fail() during a collect, so make sure any errors are
    // set and then jump to a Collect_End()
    //
    Context* error = nullptr;

  if (not Is_Module(context)) {
    Collect_Start(&collector, COLLECT_ANY_WORD);

  blockscope {  // Start out binding table with words already in context
    const Symbol* duplicate;
    Collect_Context_Keys(&duplicate, &collector, c);
    assert(not duplicate);  // context should have all unique keys
  }

    REBLEN first_new_index = Collector_Index_If_Pushed(&collector);

    // Do a pass to collect the [set-word: <value>] keys and add them to the
    // binder.  But don't modify the object yet, in case the block turns out
    // to be malformed (we don't want partial expansions applied).
    //
    // !!! This allows plain WORD! in the key spot, in addition to SET-WORD!.
    // Should it allow ANY-WORD!?  Restrict to just SET-WORD!?
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
            &collector.binder,
            symbol,
            Collector_Index_If_Pushed(&collector)
        )){
            Init_Word(PUSH(), Cell_Word_Symbol(word));
        }
        if (word + 1 == tail)  // catch malformed case with no value (#708)
            break;
    }
  }

  blockscope {  // Append new words to obj
    REBLEN num_added = Collector_Index_If_Pushed(&collector) - first_new_index;
    Expand_Context(c, num_added);

    StackValue(*) new_word = Data_Stack_At(collector.stack_base) + first_new_index;
    for (; new_word != TOP + 1; ++new_word)
        Finalize_Trash(Append_Context(c, Cell_Word_Symbol(new_word)));
  }
  }  // end the non-module part

  blockscope {  // Set new values to obj words
    const Element* word = item;
    for (; word != tail; word += 2) {
        const Symbol* symbol = Cell_Word_Symbol(word);
        Value* var;
        if (Is_Module(context)) {
            bool strict = true;
            var = MOD_VAR(c, symbol, strict);
            if (not var) {
                var = Append_Context(c, symbol);
                Finalize_Trash(var);
            }
        }
        else {
            REBLEN i = Get_Binder_Index_Else_0(&collector.binder, symbol);
            assert(i != 0);
            assert(*CTX_KEY(c, i) == symbol);
            var = CTX_VAR(c, i);
        }

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
            Finalize_Trash(var);
            break;  // fix bug#708
        }
        else
            Copy_Cell(var, &word[1]);
    }
  }

  collect_end:
    if (not Is_Module(context))
        Collect_End(&collector);

    if (error)
        fail (error);
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
// Did_Advance_Evars() on even the first.
//
//////////////////////////////////////////////////////////////////////////////
//
// 1. We allocate a wordlist just to notice leaks when there are no shutdowns,
//    but there is a problem with using an unmanaged array in the case of an
//    LEVEL_FLAG_ABRUPT_FAILURE.  So we make them "fake unmanaged" so they
//    are "untracked" by saying they're managed, and taking that flag off.
//
void Init_Evars(EVARS *e, const Cell* v) {
    enum Reb_Kind kind = Cell_Heart(v);

    e->visibility = VAR_VISIBILITY_ALL;  // ensure not uninitialized

    if (kind == REB_FRAME and Is_Frame_Details(v)) {
        e->index = 0;  // will be bumped to 1

        Corrupt_Pointer_If_Debug(e->ctx);

        Action* act = VAL_ACTION(v);
        e->key = ACT_KEYS(&e->key_tail, act) - 1;
        e->var = nullptr;
        e->param = ACT_PARAMS_HEAD(act) - 1;

        assert(Series_Used(ACT_KEYLIST(act)) <= ACT_NUM_PARAMS(act));

        // There's no clear best answer to whether the locals should be
        // visible when enumerating an action, only the caller knows if it's
        // a context where they should be.  Guess conservatively and let them
        // set e->visibility if they think they should see more.
        //
        e->visibility = VAR_VISIBILITY_INPUTS;

      #if !defined(NDEBUG)
        e->wordlist = Make_Array_Core(1, NODE_FLAG_MANAGED);
        Clear_Node_Managed_Bit(e->wordlist);  // dummy series [1]
      #endif

        e->word = nullptr;
        Corrupt_Pointer_If_Debug(e->word_tail);
    }
    else if (kind == REB_MODULE) {
        //
        // !!! Module enumeration is slow, and you should not do it often...it
        // requires walking over the global word table.  The global table gets
        // rehashed in a way that we would have a hard time maintaining a
        // consistent enumerator state in the current design.  So for the
        // moment we fabricate an array to enumerate.

        e->index = INDEX_PATCHED;

        e->ctx = VAL_CONTEXT(v);

        StackIndex base = TOP_INDEX;

        Symbol** psym = Series_Head(Symbol*, g_symbols.by_hash);
        Symbol** psym_tail = Series_Tail(Symbol*, g_symbols.by_hash);
        for (; psym != psym_tail; ++psym) {
            if (*psym == nullptr or *psym == &g_symbols.deleted_symbol)
                continue;

            Stub* patch = MISC(Hitch, *psym);
            if (Get_Subclass_Flag(SYMBOL, *psym, MISC_IS_BINDINFO))
                patch = cast(Stub*, node_MISC(Hitch, patch));  // skip bindinfo

            Series* found = nullptr;

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
                INIT_VAL_WORD_INDEX(TOP, INDEX_PATCHED);
                BINDING(TOP) = found;
            }
        }

        e->wordlist = Pop_Stack_Values_Core(base, NODE_FLAG_MANAGED);
        Clear_Node_Managed_Bit(e->wordlist);  // [1]

        e->word = cast(REBVAL*, Array_Head(e->wordlist)) - 1;
        e->word_tail = cast(REBVAL*, Array_Tail(e->wordlist));

        Corrupt_Pointer_If_Debug(e->key_tail);
        e->var = nullptr;
        e->param = nullptr;
    }
    else {
        e->index = 0;  // will be bumped to 1

        e->ctx = VAL_CONTEXT(v);

        e->var = CTX_VARS_HEAD(e->ctx) - 1;

        assert(Series_Used(CTX_KEYLIST(e->ctx)) <= CTX_LEN(e->ctx));

        if (kind != REB_FRAME) {
            e->param = nullptr;
            e->key = CTX_KEYS(&e->key_tail, e->ctx) - 1;
        }
        else {
            e->var = CTX_VARS_HEAD(e->ctx) - 1;

            // The frame can be phaseless, which means it is not running (such
            // as the direct result of a MAKE FRAME! call, which is awaiting a
            // DO to begin running).  These frames should only show variables
            // on the public interface.  Or it can be running, in which case
            // the phase determines which additional fields should be seen.
            //
            Phase* phase;
            if (not IS_FRAME_PHASED(v)) {
                phase = CTX_FRAME_PHASE(e->ctx);

                // See FRAME_HAS_BEEN_INVOKED about the efficiency trick used
                // to make sure archetypal frame views do not DO a frame after
                // being run where the action could've tainted the arguments.
                //
                Array* varlist = CTX_VARLIST(e->ctx);
                if (Get_Subclass_Flag(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED))
                    e->visibility = VAR_VISIBILITY_NONE;
                else
                    e->visibility = VAR_VISIBILITY_INPUTS;
            }
            else {
                phase = VAL_FRAME_PHASE(v);

                // Since phases can reuse exemplars, we have to check for an
                // exact match of the action of the exemplar with the phase in
                // order to know if the locals should be visible.  If you ADAPT
                // a function that reuses its exemplar, but should not be able
                // to see the locals (for instance).
                //
                Context* exemplar = ACT_EXEMPLAR(phase);
                if (CTX_FRAME_PHASE(exemplar) == phase)
                    e->visibility = VAR_VISIBILITY_ALL;
                else
                    e->visibility = VAR_VISIBILITY_INPUTS;
            }

            Action* action = phase;
            e->param = ACT_PARAMS_HEAD(action) - 1;
            e->key = ACT_KEYS(&e->key_tail, action) - 1;
            assert(Series_Used(ACT_KEYLIST(action)) <= ACT_NUM_PARAMS(action));
        }

      #if !defined(NDEBUG)
        e->wordlist = Make_Array_Core(1, NODE_FLAG_MANAGED);
        Clear_Node_Managed_Bit(e->wordlist);  // [1]
      #endif
        e->word = nullptr;
        UNUSED(e->word_tail);
    }
}


//
//  Did_Advance_Evars: C
//
// !!! When enumerating an ordinary context, this currently does not put a
// HOLD on the context.  So running user code during the enumeration that can
// modify the object and add fields is dangerous.  The FOR-EACH variants do
// put on the hold and use a rebRescue() to make sure the hold gets removed
// in case of errors.  That becomes cheaper in the stackless model where a
// single setjmp/exception boundary can wrap an arbitrary number of stack
// levels.  Ultimately there should probably be a Shutdown_Evars().
//
bool Did_Advance_Evars(EVARS *e) {
    if (e->visibility == VAR_VISIBILITY_NONE)
        return false;

    if (e->word) {
        while (++e->word != e->word_tail) {
            e->var = MOD_VAR(e->ctx, Cell_Word_Symbol(e->word), true);
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

        // A simple specialization of a function would provide a value that
        // the function should see as an argument when it runs.  But layers
        // above that will use VAR_MARKED_HIDDEN so higher abstractions will
        // not be aware of that specialized out variable.
        //
        // (Put another way: when a function copies an exemplar and uses it
        // as its own, the fact that exemplar points at the phase does not
        // suddenly give access to the private variables that would have been
        // inaccessible before the copy.  The hidden bit must be added during
        // that copy to honor this property.)
        //
        if (e->param) {  // v-- system-level hidden bit on *exemplar*
            if (Get_Cell_Flag(e->param, VAR_MARKED_HIDDEN)) {
                assert(Is_Specialized(e->param));  // don't hide param typesets
                continue;
            }

            if (e->visibility == VAR_VISIBILITY_ALL)
                return true;  // private sees ONE level of specialization

            if (Is_Specialized(e->param))  // parameter replaced with the value
                continue;  // public should not see specialized args

            if (e->visibility == VAR_VISIBILITY_INPUTS) {
                //
                // !!! Unfortunately, the code for associating comments with
                // return and output parameters uses a FRAME! for the function
                // to do it.  This means that it expects keys for those values
                // as public.  A rethought mechanism will be needed to keep
                // HELP working if we actually suppress these from the
                // "input" view of a FRAME!.
                //
              #if 0
                ParamClass pclass = Cell_ParamClass(e->param);
                if (pclass == PARAMCLASS_RETURN)
                    continue;
                if (pclass == PARAMCLASS_OUTPUT)
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
        GC_Kill_Series(e->wordlist);
    else {
      #if !defined(NDEBUG)
        GC_Kill_Series(e->wordlist);  // dummy to catch missing shutdown
      #endif
    }
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

    Context* c1 = VAL_CONTEXT(a);
    Context* c2 = VAL_CONTEXT(b);
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
        if (not Did_Advance_Evars(&e1)) {
            if (not Did_Advance_Evars(&e2))
                diff = 0;  // if both exhausted, they're equal
            else
                diff = -1;  // else the first had fewer fields
            goto finished;
        }
        else {
            if (not Did_Advance_Evars(&e2)) {
                diff = 1;  // the second had fewer fields
                goto finished;
            }
        }

        const Symbol* symbol1 = KEY_SYMBOL(e1.key);
        const Symbol* symbol2 = KEY_SYMBOL(e2.key);
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
    enum Reb_Kind kind,
    Option(const Value*) parent,
    const REBVAL *arg
){
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

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

    StackIndex lowest_ordered_stackindex = TOP_INDEX;  // for refinements

    if (not Is_Frame(arg))
        return RAISE(Error_Bad_Make(kind, arg));

    Context* exemplar = Make_Context_For_Action(
        arg, // being used here as input (e.g. the ACTION!)
        lowest_ordered_stackindex, // will weave in any refinements pushed
        nullptr // no binder needed, not running any code
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
Bounce TO_Frame(Level* level_, enum Reb_Kind kind, const REBVAL *arg)
{
    return RAISE(Error_Bad_Make(kind, arg));
}


//
//  MAKE_Context: C
//
Bounce MAKE_Context(
    Level* level_,
    enum Reb_Kind kind,
    Option(const Value*) parent,
    const REBVAL *arg
){
    // Other context kinds (LEVEL!, ERROR!, PORT!) have their own hooks.
    //
    assert(kind == REB_OBJECT or kind == REB_MODULE);

    if (kind == REB_MODULE) {
        if (not Any_Array(arg))
            return RAISE("Currently only (MAKE MODULE! any-array) is allowed");

        assert(not parent);

        Context* ctx = Alloc_Context_Core(REB_MODULE, 1, NODE_FLAG_MANAGED);
        node_LINK(NextVirtual, ctx) = BINDING(arg);
        return Init_Context_Cell(OUT, REB_MODULE, ctx);
    }

    Option(Context*) parent_ctx = parent
        ? VAL_CONTEXT(unwrap(parent))
        : nullptr;

    if (Is_Block(arg)) {
        const Element* tail;
        const Element* at = Cell_Array_At(&tail, arg);

        Context* ctx = Make_Context_Detect_Managed(
            kind,
            at,
            tail,
            parent_ctx
        );
        Init_Context_Cell(OUT, kind, ctx); // GC guards it

        DECLARE_STABLE (virtual_arg);
        Copy_Cell(virtual_arg, arg);

        Virtual_Bind_Deep_To_Existing_Context(
            virtual_arg,
            ctx,
            nullptr,  // !!! no binder made at present
            REB_WORD  // all internal refs are to the object
        );

        DECLARE_LOCAL (dummy);
        if (Do_Any_Array_At_Throws(dummy, virtual_arg, SPECIFIED))
            return BOUNCE_THROWN;

        return OUT;
    }

    // `make object! 10` - currently not prohibited for any context type
    //
    if (Any_Number(arg)) {
        Context* context = Make_Context_Detect_Managed(
            kind,
            nullptr,  // values to scan for toplevel set-words (empty)
            nullptr,
            parent_ctx
        );

        return Init_Context_Cell(OUT, kind, context);
    }

    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

    // make object! map!
    if (Is_Map(arg)) {
        Context* c = Alloc_Context_From_Map(VAL_MAP(arg));
        return Init_Context_Cell(OUT, kind, c);
    }

    return RAISE(Error_Bad_Make(kind, arg));
}


//
//  TO_Context: C
//
Bounce TO_Context(Level* level_, enum Reb_Kind kind, const REBVAL *arg)
{
    // Other context kinds (LEVEL!, ERROR!, PORT!) have their own hooks.
    //
    assert(kind == REB_OBJECT or kind == REB_MODULE);

    if (kind == REB_OBJECT) {
        //
        // !!! Contexts hold canon values now that are typed, this init
        // will assert--a TO conversion would thus need to copy the varlist
        //
        return Init_Object(OUT, VAL_CONTEXT(arg));
    }

    return RAISE(Error_Bad_Make(kind, arg));
}


//
//  adjunct-of: native [
//
//  "Get a reference to the 'adjunct' context associated with a value"
//
//      return: [<opt> any-context!]
//      value [<unrun> <maybe> frame! any-context!]
//  ]
//
DECLARE_NATIVE(adjunct_of)
{
    INCLUDE_PARAMS_OF_ADJUNCT_OF;

    REBVAL *v = ARG(value);

    Context* meta;
    if (Is_Frame(v)) {
        if (not Is_Frame_Details(v))
            return nullptr;

        meta = ACT_ADJUNCT(VAL_ACTION(v));
    }
    else {
        assert(Any_Context(v));
        meta = CTX_ADJUNCT(VAL_CONTEXT(v));
    }

    if (not meta)
        return nullptr;

    return COPY(CTX_ARCHETYPE(meta));
}


//
//  set-adjunct: native [
//
//  "Set 'adjunct' object associated with all references to a value"
//
//      return: [<opt> any-context!]
//      value [<unrun> frame! any-context!]
//      adjunct [<opt> any-context!]
//  ]
//
DECLARE_NATIVE(set_adjunct)
//
// See notes accompanying the `adjunct` field in DetailsAdjunct/VarlistAdjunct.
{
    INCLUDE_PARAMS_OF_SET_ADJUNCT;

    REBVAL *adjunct = ARG(adjunct);

    Context* ctx;
    if (Any_Context(adjunct)) {
        if (Is_Frame(adjunct))
            fail ("SET-ADJUNCT can't store bindings, FRAME! disallowed");

        ctx = VAL_CONTEXT(adjunct);
    }
    else {
        assert(Is_Nulled(adjunct));
        ctx = nullptr;
    }

    REBVAL *v = ARG(value);

    if (Is_Frame(v)) {
        if (Is_Frame_Details(v))
            MISC(DetailsAdjunct, ACT_IDENTITY(VAL_ACTION(v))) = ctx;
    }
    else
        MISC(VarlistAdjunct, CTX_VARLIST(VAL_CONTEXT(v))) = ctx;

    return COPY(adjunct);
}


//
//  Copy_Context_Extra_Managed: C
//
// If no extra space is requested, the same keylist will be reused.
//
// !!! Copying a context used to be more different from copying an ordinary
// array.  But at the moment, much of the difference is that the marked bit
// in cells gets duplicated (so new context has the same VAR_MARKED_HIDDEN
// settings on its variables).  Review if the copying can be cohered better.
//
Context* Copy_Context_Extra_Managed(
    Context* original,
    REBLEN extra,
    REBU64 types
){
    REBLEN len = (CTX_TYPE(original) == REB_MODULE) ? 0 : CTX_LEN(original);

    Array* varlist = Make_Array_For_Copy(
        len + extra + 1,
        SERIES_MASK_VARLIST | NODE_FLAG_MANAGED,
        nullptr // original_array, N/A because LINK()/MISC() used otherwise
    );
    if (CTX_TYPE(original) == REB_MODULE)
        Set_Series_Used(varlist, 1);  // all variables linked from word table
    else
        Set_Series_Len(varlist, CTX_LEN(original) + 1);

    Value* dest = Series_Head(Value, varlist);

    // The type information and fields in the rootvar (at head of the varlist)
    // get filled in with a copy, but the varlist needs to be updated in the
    // copied rootvar to the one just created.
    //
    Copy_Cell(dest, CTX_ARCHETYPE(original));
    INIT_VAL_CONTEXT_VARLIST(dest, varlist);

    if (CTX_TYPE(original) == REB_MODULE) {
        //
        // Copying modules is different because they have no data in the
        // varlist and no keylist.  The symbols themselves point to a linked
        // list of variable instances from all the modules that use that
        // symbol.  So copying requires walking the global symbol list and
        // duplicating those links.

        assert(extra == 0);

        if (CTX_ADJUNCT(original)) {
            MISC(VarlistAdjunct, varlist) = Copy_Context_Shallow_Managed(
                CTX_ADJUNCT(original)
            );
        }
        else {
            MISC(VarlistAdjunct, varlist) = nullptr;
        }
        INIT_BONUS_KEYSOURCE(varlist, nullptr);
        LINK(Patches, varlist) = nullptr;

        Context* copy = cast(Context*, varlist); // now a well-formed context
        assert(Get_Series_Flag(varlist, DYNAMIC));

        Symbol** psym = Series_Head(Symbol*, g_symbols.by_hash);
        Symbol** psym_tail = Series_Tail(Symbol*, g_symbols.by_hash);
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
                    REBVAL *var = Append_Context(copy, *psym);
                    Copy_Cell(var, Stub_Cell(patch));
                    break;
                }
            }
        }

        return copy;
    }

    Assert_Series_Managed(CTX_KEYLIST(original));

    ++dest;

    // Now copy the actual vars in the context, from wherever they may be
    // (might be in an array, or might be in the chunk stack for FRAME!)
    //
    const Value* src_tail;
    REBVAL *src = CTX_VARS(&src_tail, original);
    for (; src != src_tail; ++src, ++dest) {
        Copy_Cell_Core(  // trying to duplicate slot precisely
            dest,
            src,
            CELL_MASK_COPY | CELL_FLAG_VAR_MARKED_HIDDEN
        );

        Flags flags = NODE_FLAG_MANAGED;  // !!! Review, which flags?
        Clonify(dest, flags, types);
    }

    varlist->leader.bits |= SERIES_MASK_VARLIST;

    Context* copy = cast(Context*, varlist); // now a well-formed context

    if (extra == 0)
        INIT_CTX_KEYLIST_SHARED(copy, CTX_KEYLIST(original));  // ->link field
    else {
        assert(CTX_TYPE(original) != REB_FRAME);  // can't expand FRAME!s

        KeyList* keylist = cast(KeyList*, Copy_Series_At_Len_Extra(
            CTX_KEYLIST(original),
            0,
            CTX_LEN(original),
            extra,
            SERIES_MASK_KEYLIST | NODE_FLAG_MANAGED
        ));

        LINK(Ancestor, keylist) = CTX_KEYLIST(original);

        INIT_CTX_KEYLIST_UNIQUE(copy, keylist);  // ->link field
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

    LINK(Patches, varlist) = nullptr;  // no virtual bind patches yet

    return copy;
}


//
//  MF_Context: C
//
void MF_Context(REB_MOLD *mo, const Cell* v, bool form)
{
    String* s = mo->series;

    Context* c = VAL_CONTEXT(v);

    // Prevent endless mold loop:
    //
    if (Find_Pointer_In_Series(g_mold.stack, c) != NOT_FOUND) {
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
    Push_Pointer_To_Series(g_mold.stack, c);

    if (Cell_Heart(v) == REB_FRAME and not IS_FRAME_PHASED(v)) {
        Array* varlist = CTX_VARLIST(VAL_CONTEXT(v));
        if (Get_Subclass_Flag(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED)) {
            Append_Ascii(s, "make frame! [...invoked frame...]\n");
            Drop_Pointer_From_Series(g_mold.stack, c);
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
        while (Did_Advance_Evars(&e)) {
            Append_Spelling(mo->series, KEY_SYMBOL(e.key));
            Append_Ascii(mo->series, ": ");

            if (Is_Antiform(e.var)) {
                fail (Error_Bad_Antiform(e.var));  // can't FORM antiforms
            }
            else if (not Is_Blank(e.var))
                Mold_Value(mo, cast(Element*, e.var));

            Append_Codepoint(mo->series, LF);
            had_output = true;
        }
        Shutdown_Evars(&e);

        // Remove the final newline...but only if WE added to the buffer
        //
        if (had_output)
            Trim_Tail(mo, '\n');

        Drop_Pointer_From_Series(g_mold.stack, c);
        return;
    }

    // Otherwise we are molding

    Pre_Mold(mo, v);

    Append_Codepoint(s, '[');

    mo->indent++;

    while (Did_Advance_Evars(&e)) {
        New_Indented_Line(mo);

        const Symbol* spelling = KEY_SYMBOL(e.key);

        DECLARE_ELEMENT (set_word);
        Init_Set_Word(set_word, spelling);  // want escaping, e.g `|::|: 10`

        Mold_Value(mo, set_word);
        Append_Codepoint(mo->series, ' ');

        if (Is_Void(e.var)) {
            Append_Ascii(s, "'");
        }
        else if (Is_Antiform(e.var)) {
            assert(Is_Antiform_Stable(cast(Atom*, e.var)));  // extra check

            DECLARE_LOCAL (reified);
            Copy_Cell(reified, e.var);
            Quasify_Antiform(reified);  // will become quasi...
            Mold_Value(mo, cast(Element*, reified));  // ...molds as `~xxx~`
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
            Mold_Value(mo, cast(Element*, e.var));
        }
    }
    Shutdown_Evars(&e);

    mo->indent--;
    New_Indented_Line(mo);
    Append_Codepoint(s, ']');

    End_Mold(mo);

    Drop_Pointer_From_Series(g_mold.stack, c);
}


const Symbol* Symbol_From_Picker(const REBVAL *context, const Value* picker)
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
    REBVAL *context = D_ARG(1);
    Context* c = VAL_CONTEXT(context);

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

        REBVAL *property = ARG(property);
        Option(SymId) prop = Cell_Word_Id(property);

        switch (prop) {
          case SYM_LENGTH: // !!! Should this be legal?
            return Init_Integer(OUT, CTX_LEN(c));

          case SYM_TAIL_Q: // !!! Should this be legal?
            return Init_Logic(OUT, CTX_LEN(c) == 0);

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

        const REBVAL *var = TRY_VAL_CONTEXT_VAR(context, symbol);
        if (not var)
            fail (Error_Bad_Pick_Raw(picker));

        if (Is_Void(var))
            return VOID;  // GET/ANY will allow, PICK and SELECT won't
        return Copy_Cell(OUT, var); }


    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE_P: {
        INCLUDE_PARAMS_OF_POKE_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        const Symbol* symbol = Symbol_From_Picker(context, picker);

        REBVAL *setval = ARG(value);

        REBVAL *var = TRY_VAL_CONTEXT_MUTABLE_VAR(context, symbol);
        if (not var)
            fail (Error_Bad_Pick_Raw(picker));

        assert(Not_Cell_Flag(var, PROTECTED));
        Copy_Cell(var, setval);
        return nullptr; }  // caller's Context* is not stale, no update needed


    //=//// PROTECT* ///////////////////////////////////////////////////////=//

      case SYM_PROTECT_P: {
        INCLUDE_PARAMS_OF_PROTECT_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        const Symbol* symbol = Symbol_From_Picker(context, picker);

        REBVAL *setval = ARG(value);

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

        return nullptr; }  // caller's Context* is not stale, no update needed

      case SYM_APPEND: {
        REBVAL *arg = D_ARG(2);
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
            if (0 == Find_Symbol_In_Context(
                context,
                Cell_Word_Symbol(arg),
                strict
            )){
                Finalize_Trash(Append_Context(c, Cell_Word_Symbol(arg)));
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

        REBU64 types = 0;
        if (REF(deep))
            types = TS_STD_SERIES;

        // !!! Special attention on copying frames is going to be needed,
        // because copying a frame will be expected to create a new identity
        // for an ACTION! if that frame is aliased AS ACTION!.  The design
        // of this is still evolving, but we don't want archetypal values
        // otherwise we could not `do copy f`, so initialize with label.
        //
        if (Is_Frame(context)) {
            return Init_Frame(
                OUT,
                Copy_Context_Extra_Managed(c, 0, types),
                VAL_FRAME_LABEL(context)
            );
        }

        return Init_Context_Cell(
            OUT,
            VAL_TYPE(context),
            Copy_Context_Extra_Managed(c, 0, types)
        ); }

      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_SELECT;
        UNUSED(ARG(series));  // extracted as `c`
        UNUSED(PARAM(tail));  // not supported

        if (REF(part) or REF(skip) or REF(match))
            fail (Error_Bad_Refines_Raw());

        REBVAL *pattern = ARG(value);
        if (Is_Antiform(pattern))
            fail (pattern);

        if (not Is_Word(pattern))
            return nullptr;

        REBLEN n = Find_Symbol_In_Context(
            context,
            Cell_Word_Symbol(pattern),
            REF(case)
        );
        if (n == 0)
            return nullptr;

        if (Symbol_Id(verb) == SYM_FIND)
            return Init_True(OUT); // !!! obscures non-LOGIC! result?

        return COPY(CTX_VAR(c, n)); }

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
    REBVAL *frame = D_ARG(1);
    Context* c = VAL_CONTEXT(frame);

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
                return Init_Word(OUT, unwrap(label));

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
                VAL_FRAME_BINDING(frame)  // e.g. where RETURN returns to
            );
        } */

        if (prop == SYM_WORDS)
            return T_Context(level_, verb);

        if (prop == SYM_PARAMETERS) {
            Init_Frame_Details(
                ARG(value),
                CTX_FRAME_PHASE(c),
                VAL_FRAME_LABEL(frame),
                VAL_FRAME_BINDING(frame)
            );
            goto handle_reflect_action;
        }

        Level* L = CTX_LEVEL_MAY_FAIL(c);

        switch (prop) {
          case SYM_FILE: {
            Option(const String*) file = File_Of_Level(L);
            if (not file)
                return nullptr;
            return Init_File(OUT, unwrap(file)); }

          case SYM_LINE: {
            LineNumber line = LineNumber_Of_Level(L);
            if (line == 0)
                return nullptr;
            return Init_Integer(OUT, line); }

          case SYM_LABEL: {
            if (not L->label)
                return nullptr;
            return Init_Word(OUT, unwrap(L->label)); }

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

                Context* ctx_parent = Context_For_Level_May_Manage(parent);
                return COPY(CTX_ARCHETYPE(ctx_parent));
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
        assert(IS_DETAILS(act));

        REBVAL *property = ARG(property);
        Option(SymId) sym = Cell_Word_Id(property);
        switch (sym) {
          case SYM_BINDING: {
            if (Did_Get_Binding_Of(OUT, frame))
                return OUT;
            return nullptr; }

          case SYM_LABEL: {
            Option(const Symbol*) label = VAL_FRAME_LABEL(frame);
            if (not label)
                return nullptr;
            return Init_Word(OUT, unwrap(label)); }

          case SYM_WORDS:
          case SYM_PARAMETERS: {
            bool just_words = (sym == SYM_WORDS);
            return Init_Block(
                OUT,
                Make_Action_Parameters_Arr(act, just_words)
            ); }

          case SYM_OUTPUTS: {
            return Init_Block(
                OUT,
                Make_Action_Outputs_Arr(act)
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
            Reset_Unquoted_Header_Untracked(TRACK(OUT), CELL_MASK_FRAME);
            INIT_VAL_CONTEXT_VARLIST(OUT, ACT_PARAMLIST(act));
            BINDING(OUT) = VAL_FRAME_BINDING(frame);
            INIT_VAL_FRAME_PHASE_OR_LABEL(OUT, act);
            return OUT; }

          case SYM_TYPES:
            return Copy_Cell(OUT, CTX_ARCHETYPE(ACT_EXEMPLAR(act)));

          case SYM_FILE:
          case SYM_LINE: {
            //
            // Use a heuristic that if the first element of a function's body
            // is a series with the file and line bits set, then that's what
            // it returns for FILE OF and LINE OF.

            Details* details = Phase_Details(act);
            if (Array_Len(details) < 1 or not Any_Array(Array_Head(details)))
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

        Context* meta = ACT_ADJUNCT(act);
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
            VAL_FRAME_BINDING(frame)  // same (e.g. RETURN to same frame)
        ); }


  //=//// PICK* (see %sys-pick.h for explanation) //////////////////////////=//

    // !!! This is an interim implementation hack for REDBOL-PATHS, which
    // transforms something like `lib/append/dup` into `lib.append.dup` when
    // it notices that LIB is not an ACTION!.  This is a *very slow* way of
    // dealing with refinements because it produces a specialized action
    // at each stage.

      case SYM_PICK_P:
        if (Is_Frame_Exemplar(frame))
            break;
      {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        REBVAL *redbol = Get_System(SYS_OPTIONS, OPTIONS_REDBOL_PATHS);
        if (not Is_Logic(redbol) or Cell_Logic(redbol) == false) {
            fail (
                "SYSTEM.OPTIONS.REDBOL-PATHS is false, so you can't"
                " use paths to do ordinary picking.  Use TUPLE!"
            );
          }

        REBVAL *picker = ARG(picker);
        if (Is_Blank(picker))
            return COPY(frame);

        const Symbol* symbol;
        if (Is_Word(picker))
            symbol = Cell_Word_Symbol(picker);
        else if (Is_Path(picker) and Is_Refinement(picker))
            symbol = VAL_REFINEMENT_SYMBOL(picker);
        else
            fail (picker);

        StackIndex base = TOP_INDEX;
        Init_Pushed_Refinement(PUSH(), symbol);
        if (Specialize_Action_Throws(OUT, frame, nullptr, base))
            return THROWN;

        return OUT; }

      default:
        break;
    }

    return T_Context(level_, verb);
}


static bool Same_Action(const Cell* a, const Cell* b)
{
    assert(Cell_Heart(a) == REB_FRAME and Cell_Heart(b) == REB_FRAME);
    if (not Is_Frame_Details(a) or not Is_Frame_Details(b))
        return false;

    if (VAL_ACTION_KEYLIST(a) == VAL_ACTION_KEYLIST(b)) {
        //
        // All actions that have the same paramlist are not necessarily the
        // "same action".  For instance, every RETURN shares a common
        // paramlist, but the binding is different in the REBVAL instances
        // in order to know where to "exit from".
        //
        return VAL_FRAME_BINDING(a) == VAL_FRAME_BINDING(b);
    }

    return false;
}


//
//  CT_Frame: C
//
REBINT CT_Frame(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);  // no lax form of comparison

    if (Is_Frame_Details(a)) {
        if (not Is_Frame_Details(b))
            return -1;
        if (Same_Action(a, b))
            return 0;
        assert(VAL_ACTION(a) != VAL_ACTION(b));
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

    Append_Ascii(mo->series, "#[frame! ");

    Option(const Symbol*) label = VAL_FRAME_LABEL(v);
    if (label) {
        Append_Codepoint(mo->series, '{');
        Append_Spelling(mo->series, unwrap(label));
        Append_Ascii(mo->series, "} ");
    }

    // !!! The system is no longer keeping the spec of functions, in order
    // to focus on a generalized "meta info object" service.  MOLD of
    // functions temporarily uses the word list as a substitute (which
    // drops types)
    //
    const bool just_words = false;
    Array* parameters = Make_Action_Parameters_Arr(VAL_ACTION(v), just_words);
    Mold_Array_At(mo, parameters, 0, "[]");
    Free_Unmanaged_Series(parameters);

    // !!! Previously, ACTION! would mold the body out.  This created a large
    // amount of output, and also many function variations do not have
    // ordinary "bodies".  It's more useful to show the cached name, and maybe
    // some base64 encoding of a UUID (?)  In the meantime, having the label
    // of the last word used is actually a lot more useful than most things.

    Append_Codepoint(mo->series, ']');
    End_Mold(mo);
}


//
//  construct: native [
//
//  "Creates an ANY-CONTEXT! instance"
//
//      return: [<opt> any-context!]
//      spec [<maybe> block!]
//          "Object specification block (bindings modified)"
//      /only "Values are kept as-is"
//      /with "Use a parent/prototype context"
//          [any-context!]
//  ]
//
DECLARE_NATIVE(construct)
//
// !!! This assumes you want a SELF defined.  The entire concept of SELF
// needs heavy review.
//
// !!! This mutates the bindings of the spec block passed in, should it
// be making a copy instead (at least by default, perhaps with performance
// junkies saying `construct/rebind` or something like that?
//
// !!! /ONLY should be done with a "predicate", e.g. `construct .quote [...]`
{
    INCLUDE_PARAMS_OF_CONSTRUCT;

    REBVAL *spec = ARG(spec);
    Context* parent = REF(with)
        ? VAL_CONTEXT(ARG(with))
        : nullptr;

    // This parallels the code originally in CONSTRUCT.  Run it if the /ONLY
    // refinement was passed in.
    //
  blockscope {
    const Element* tail;
    Element* at = Cell_Array_At_Mutable_Hack(&tail, spec);
    if (REF(only)) {
        Init_Object(
            OUT,
            Construct_Context_Managed(
                REB_OBJECT,
                at,  // warning: modifies binding!
                tail,
                Cell_Specifier(spec),
                parent
            )
        );
        return OUT;
    }
  }

    // Scan the object for top-level set words in order to make an
    // appropriately sized context.
    //
    const Element* tail;
    Element* at = Cell_Array_At_Ensure_Mutable(&tail, spec);

    Context* ctx = Make_Context_Detect_Managed(
        parent ? CTX_TYPE(parent) : REB_OBJECT,  // !!! Presume object?
        at,
        tail,
        parent
    );
    Init_Object(OUT, ctx);  // GC protects context

    // !!! This binds the actual body data, not a copy of it.  See
    // Virtual_Bind_Deep_To_New_Context() for future directions.
    //
    Bind_Values_Deep(at, tail, CTX_ARCHETYPE(ctx));

    if (Do_Any_Array_At_Throws(SPARE, spec, SPECIFIED)) // eval result ignored
        return THROWN;

    return OUT;
}
