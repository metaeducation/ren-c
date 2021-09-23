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


static void Append_To_Context(REBVAL *context, REBVAL *arg)
{
    REBCTX *c = VAL_CONTEXT(context);

    if (ANY_WORD(arg)) {  // Add an unset word: `append context 'some-word`
        const bool strict = true;
        if (0 == Find_Symbol_In_Context(
            context,
            VAL_WORD_SYMBOL(arg),
            strict
        )){
            Init_Unset(Append_Context(c, nullptr, VAL_WORD_SYMBOL(arg)));
        }
        return;
    }

    if (not IS_BLOCK(arg))
        fail (arg);

    const RELVAL *tail;
    const RELVAL *item = VAL_ARRAY_AT(&tail, arg);

    struct Reb_Collector collector;
    //
    // Can't actually fail() during a collect, so make sure any errors are
    // set and then jump to a Collect_End()
    //
    REBCTX *error = nullptr;

  if (not IS_MODULE(context)) {
    Collect_Start(&collector, COLLECT_ANY_WORD);

  blockscope {  // Start out binding table with words already in context
    const REBSTR *duplicate;
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
    const RELVAL *word;
    for (word = item; word != tail; word += 2) {
        if (not IS_WORD(word) and not IS_SET_WORD(word)) {
            error = Error_Bad_Value(word);
            goto collect_end;
        }

        const REBSYM *symbol = VAL_WORD_SYMBOL(word);

        if (Try_Add_Binder_Index(
            &collector.binder,
            symbol,
            Collector_Index_If_Pushed(&collector)
        )){
            Init_Word(DS_PUSH(), VAL_WORD_SYMBOL(word));
        }
        if (word + 1 == tail)  // catch malformed case with no value (#708)
            break;
    }
  }

  blockscope {  // Append new words to obj
    REBLEN num_added = Collector_Index_If_Pushed(&collector) - first_new_index;
    Expand_Context(c, num_added);

    STKVAL(*) new_word = DS_AT(collector.dsp_orig) + first_new_index;
    for (; new_word != DS_TOP + 1; ++new_word)
        Init_Unset(Append_Context(c, nullptr, VAL_WORD_SYMBOL(new_word)));
  }
  }  // end the non-module part

  blockscope {  // Set new values to obj words
    const RELVAL *word = item;
    for (; word != tail; word += 2) {
        const REBSYM *symbol = VAL_WORD_SYMBOL(word);
        REBVAR *var;
        if (IS_MODULE(context)) {
            bool strict = true;
            var = MOD_VAR(c, symbol, strict);
            if (not var) {
                var = Append_Context(c, nullptr, symbol);
                Init_Unset(var);
            }
        }
        else {
            REBLEN i = Get_Binder_Index_Else_0(&collector.binder, symbol);
            assert(i != 0);
            assert(*CTX_KEY(c, i) == symbol);
            var = CTX_VAR(c, i);
        }

        if (GET_CELL_FLAG(var, PROTECTED)) {
            error = Error_Protected_Key(&symbol);
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
        if (GET_CELL_FLAG(var, VAR_MARKED_HIDDEN)) {
            error = Error_Hidden_Raw();
            goto collect_end;
        }

        if (word + 1 == tail) {
            Init_Unset(var);
            break;  // fix bug#708
        }
        else
            Derelativize(var, &word[1], VAL_SPECIFIER(arg));
    }
  }

  collect_end:
    if (not IS_MODULE(context))
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
void Init_Evars(EVARS *e, REBCEL(const*) v) {
    enum Reb_Kind kind = CELL_KIND(v);

    if (kind == REB_ACTION) {
        e->index = 0;  // will be bumped to 1

        TRASH_POINTER_IF_DEBUG(e->ctx);

        REBACT *act = VAL_ACTION(v);
        e->key = ACT_KEYS(&e->key_tail, act) - 1;
        e->var = nullptr;
        e->param = ACT_PARAMS_HEAD(act) - 1;

        assert(SER_USED(ACT_KEYLIST(act)) <= ACT_NUM_PARAMS(act));

        // There's no clear best answer to whether the locals should be
        // visible when enumerating an action, only the caller knows if it's
        // a context where they should be.  Guess conservatively and let them
        // set `e->locals_visible = true` if they think they should be.
        //
        e->locals_visible = false;

        e->wordlist = Make_Array(1);  // dummy to catch missing shutdown
        e->word = nullptr;
        TRASH_POINTER_IF_DEBUG(e->word_tail);
    }
    else if (kind == REB_MODULE) {
        //
        // !!! Module enumeration is slow, and you should not do it often...it
        // requires walking over the global word table.  The global table gets
        // rehashed in a way that we would have a hard time maintaining a
        // consistent enumerator state in the current design.  So for the
        // moment we fabricate an array to enumerate.

        e->index = INDEX_ATTACHED;

        e->ctx = VAL_CONTEXT(v);

        REBDSP dsp_orig = DSP;

        REBSYM **psym = SER_HEAD(REBSYM*, PG_Symbols_By_Hash);
        REBSYM **psym_tail = SER_TAIL(REBSYM*, PG_Symbols_By_Hash);
        for (; psym != psym_tail; ++psym) {
            if (*psym == nullptr or *psym == &PG_Deleted_Symbol)
                continue;

            REBSER *patch = MISC(Hitch, *psym);
            while (GET_SERIES_FLAG(patch, BLACK))  // binding temps
                patch = SER(node_MISC(Hitch, patch));

            REBSER *found = nullptr;

            for (; patch != *psym; patch = SER(node_MISC(Hitch, patch))) {
                if (e->ctx == LINK(PatchContext, patch)) {
                    found = patch;
                    break;
                }
             /*   if (Lib_Context == LINK(PatchContext, patch))
                    found = patch;  // will match if not overridden */
            }
            if (found) {
                Init_Any_Word(DS_PUSH(), REB_WORD, *psym);
                mutable_BINDING(DS_TOP) = found;
                INIT_VAL_WORD_PRIMARY_INDEX(DS_TOP, INDEX_ATTACHED);
            }
        }

        e->wordlist = Pop_Stack_Values(dsp_orig);
        e->word = cast(REBVAL*, ARR_HEAD(e->wordlist)) - 1;
        e->word_tail = cast(REBVAL*, ARR_TAIL(e->wordlist));

        TRASH_POINTER_IF_DEBUG(e->key_tail);
        e->var = nullptr;
        e->param = nullptr;
    }
    else {
        e->index = 0;  // will be bumped to 1

        e->ctx = VAL_CONTEXT(v);

        e->var = CTX_VARS_HEAD(e->ctx) - 1;

        assert(SER_USED(CTX_KEYLIST(e->ctx)) <= CTX_LEN(e->ctx));

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
            REBACT *phase;
            if (not IS_FRAME_PHASED(v)) {
                //
                // See FRAME_HAS_BEEN_INVOKED about the efficiency trick used
                // to make sure archetypal frame views do not DO a frame after
                // being run where the action could've tainted the arguments.
                //
                REBARR *varlist = CTX_VARLIST(e->ctx);
                if (GET_SUBCLASS_FLAG(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED))
                    fail (Error_Stale_Frame_Raw());

                phase = CTX_FRAME_ACTION(e->ctx);
                e->locals_visible = false;
            }
            else {
                phase = VAL_FRAME_PHASE(v);

                // Since phases can reuse exemplars, we have to check for an
                // exact match of the action of the exemplar with the phase in
                // order to know if the locals should be visible.  If you ADAPT
                // a function that reuses its exemplar, but should not be able
                // to see the locals (for instance).
                //
                REBCTX *exemplar = ACT_EXEMPLAR(phase);
                e->locals_visible = (CTX_FRAME_ACTION(exemplar) == phase);
            }

            e->param = ACT_PARAMS_HEAD(phase) - 1;
            e->key = ACT_KEYS(&e->key_tail, phase) - 1;
            assert(SER_USED(ACT_KEYLIST(phase)) <= ACT_NUM_PARAMS(phase));
        }

      #if !defined(NDEBUG)
        e->wordlist = Make_Array(1);  // dummy to catch missing Shutdown
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
    if (e->word) {
        while (++e->word != e->word_tail) {
            e->var = MOD_VAR(e->ctx, VAL_WORD_SYMBOL(e->word), true);
            if (GET_CELL_FLAG(e->var, VAR_MARKED_HIDDEN))
                continue;
            e->keybuf = VAL_WORD_SYMBOL(e->word);
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
            e->param ? ++e->param : cast(REBPAR*, nullptr),
            e->var ? ++e->var : cast(REBVAR*, nullptr)
        )
    ){
        if (e->var and GET_CELL_FLAG(e->var, VAR_MARKED_HIDDEN))
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
            if (GET_CELL_FLAG(e->param, VAR_MARKED_HIDDEN)) {
                assert(Is_Specialized(e->param));  // don't hide param typesets
                continue;
            }

            if (Is_Specialized(e->param)) {  // not TYPESET! with a PARAM_CLASS
                if (e->locals_visible)
                    return true;  // private sees ONE level of specialization
                continue;  // public should not see specialized args
            }

            // Note: while RETURN: parameters were considered to be "local"
            // they are actually part of the public interface of a function.
            // A special feature makes it so that if you put a WORD! in the
            // return slot, it will be assigned the result at the end of
            // the call (like other output arguments).  But during the function
            // execution it will be a definitional return.
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
        Free_Unmanaged_Series(e->wordlist);
    else {
      #if !defined(NDEBUG)
        Free_Unmanaged_Series(e->wordlist);  // dummy to catch missing shutdown
      #endif
    }
}


//
//  CT_Context: C
//
REBINT CT_Context(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    assert(ANY_CONTEXT_KIND(CELL_KIND(a)));
    assert(ANY_CONTEXT_KIND(CELL_KIND(b)));

    if (CELL_KIND(a) != CELL_KIND(b))  // e.g. ERROR! won't equal OBJECT!
        return CELL_KIND(a) > CELL_KIND(b) ? 1 : 0;

    REBCTX *c1 = VAL_CONTEXT(a);
    REBCTX *c2 = VAL_CONTEXT(b);
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

        const REBSYM *symbol1 = KEY_SYMBOL(e1.key);
        const REBSYM *symbol2 = KEY_SYMBOL(e2.key);
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
REB_R MAKE_Frame(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

    // MAKE FRAME! on a VARARGS! was an experiment designed before REFRAMER
    // existed, to allow writing things like REQUOTE.  It's still experimental
    // but has had its functionality unified with reframer, so that it doesn't
    // really cost that much to keep around.  Use it sparingly (if at all).
    //
    if (IS_VARARGS(arg)) {
        REBFRM *f_varargs;
        if (not Is_Frame_Style_Varargs_May_Fail(&f_varargs, arg))
            fail (
                "Currently MAKE FRAME! on a VARARGS! only works with a varargs"
                " which is tied to an existing, running frame--not one that is"
                " being simulated from a BLOCK! (e.g. MAKE VARARGS! [...])"
            );

        assert(Is_Action_Frame(f_varargs));

        if (Make_Frame_From_Feed_Throws(out, END_CELL, f_varargs->feed))
            return R_THROWN;

        return out;
    }

    REBDSP lowest_ordered_dsp = DSP;  // Data stack gathers any refinements

    if (not IS_ACTION(arg))
        fail (Error_Bad_Make(kind, arg));

    REBCTX *exemplar = Make_Context_For_Action(
        arg, // being used here as input (e.g. the ACTION!)
        lowest_ordered_dsp, // will weave in any refinements pushed
        nullptr // no binder needed, not running any code
    );

    // See notes in %c-specialize.c about the special encoding used to
    // put /REFINEMENTs in refinement slots (instead of true/false/null)
    // to preserve the order of execution.

    return Init_Frame(out, exemplar, VAL_ACTION_LABEL(arg));
}


//
//  TO_Frame: C
//
// Currently can't convert anything TO a frame; nothing has enough information
// to have an equivalent representation (an OBJECT! could be an expired frame
// perhaps, but still would have no ACTION OF property)
//
REB_R TO_Frame(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    UNUSED(out);
    fail (Error_Bad_Make(kind, arg));
}


//
//  MAKE_Context: C
//
REB_R MAKE_Context(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    // Other context kinds (FRAME!, ERROR!, PORT!) have their own hooks.
    //
    assert(kind == REB_OBJECT or kind == REB_MODULE);

    if (kind == REB_MODULE) {
        if (not Is_Blackhole(arg))
            fail ("Currently only (MAKE MODULE! #) is allowed.  Review.");

        assert(not parent);

        REBCTX *ctx = Alloc_Context_Core(REB_MODULE, 1, NODE_FLAG_MANAGED);
        return Init_Any_Context(out, REB_MODULE, ctx);
    }

    option(REBCTX*) parent_ctx = parent
        ? VAL_CONTEXT(unwrap(parent))
        : cast(REBCTX*, nullptr);  // C++98 ambiguous w/o cast

    if (IS_BLOCK(arg)) {
        const RELVAL *tail;
        const RELVAL *at = VAL_ARRAY_AT(&tail, arg);

        REBCTX *ctx = Make_Context_Detect_Managed(
            kind,
            at,
            tail,
            parent_ctx
        );
        Init_Any_Context(out, kind, ctx); // GC guards it

        DECLARE_LOCAL (virtual_arg);
        Copy_Cell(virtual_arg, arg);

        Virtual_Bind_Deep_To_Existing_Context(
            virtual_arg,
            ctx,
            nullptr,  // !!! no binder made at present
            REB_WORD  // all internal refs are to the object
        );

        DECLARE_LOCAL (dummy);
        if (Do_Any_Array_At_Throws(dummy, virtual_arg, SPECIFIED)) {
            Move_Cell(out, dummy);  // GC-guarded context was in out
            return R_THROWN;
        }

        return out;
    }

    // `make object! 10` - currently not prohibited for any context type
    //
    if (ANY_NUMBER(arg)) {
        REBCTX *context = Make_Context_Detect_Managed(
            kind,
            END_CELL,  // values to scan for toplevel set-words (empty)
            END_CELL,
            parent_ctx
        );

        return Init_Any_Context(out, kind, context);
    }

    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

    // make object! map!
    if (IS_MAP(arg)) {
        REBCTX *c = Alloc_Context_From_Map(VAL_MAP(arg));
        return Init_Any_Context(out, kind, c);
    }

    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Context: C
//
REB_R TO_Context(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    // Other context kinds (FRAME!, ERROR!, PORT!) have their own hooks.
    //
    assert(kind == REB_OBJECT or kind == REB_MODULE);

    if (kind == REB_OBJECT) {
        //
        // !!! Contexts hold canon values now that are typed, this init
        // will assert--a TO conversion would thus need to copy the varlist
        //
        return Init_Object(out, VAL_CONTEXT(arg));
    }

    fail (Error_Bad_Make(kind, arg));
}


//
//  PD_Context: C
//
REB_R PD_Context(
    REBPVS *pvs,
    const RELVAL *picker
){
    REBCTX *c = VAL_CONTEXT(pvs->out);

    if (not IS_WORD(picker))
        return R_UNHANDLED;

    const bool strict = false;

    // See if the binding of the word is already to the context (so there's
    // no need to go hunting).  'x
    //
    REBVAL *var;
    if (IS_MODULE(pvs->out)) {
        var = MOD_VAR(c, VAL_WORD_SYMBOL(picker), strict);
        if (var == nullptr)
            return R_UNHANDLED;
    }
    else if (BINDING(picker) == c) {
        var = CTX_VAR(c, VAL_WORD_INDEX(picker));
    }
    else {
        REBLEN n = Find_Symbol_In_Context(
            pvs->out,
            VAL_WORD_SYMBOL(picker),
            strict
        );

        if (n == 0)
            return R_UNHANDLED;

        var = CTX_VAR(c, n);

        // !!! As an experiment, try caching the binding index in the word.
        // This "corrupts" it, but if we say paths effectively own their
        // top-level words that could be all right.  Note this won't help if
        // the word is an evaluative product, as the bits live in the cell
        // and it will be discarded.
        //
        INIT_VAL_WORD_BINDING(m_cast(RELVAL*, picker), c);
        INIT_VAL_WORD_PRIMARY_INDEX(m_cast(RELVAL*, picker), n);
    }

    return Copy_Cell(pvs->out, var);
}


//
//  meta-of: native [
//
//  {Get a reference to the "meta" context associated with a value.}
//
//      return: [<opt> any-context!]
//      value [<blank> action! any-context!]
//  ]
//
REBNATIVE(meta_of)  // see notes on MISC_META()
{
    INCLUDE_PARAMS_OF_META_OF;

    REBVAL *v = ARG(value);

    REBCTX *meta;
    if (IS_ACTION(v))
        meta = ACT_META(VAL_ACTION(v));
    else {
        assert(ANY_CONTEXT(v));
        meta = CTX_META(VAL_CONTEXT(v));
    }

    if (not meta)
        return nullptr;

    RETURN (CTX_ARCHETYPE(meta));
}


//
//  set-meta: native [
//
//  {Set "meta" object associated with all references to a value.}
//
//      return: [<opt> any-context!]
//      value [action! any-context!]
//      meta [<opt> any-context!]
//  ]
//
REBNATIVE(set_meta)
//
// See notes accompanying the `meta` field in the REBSER definition.
{
    INCLUDE_PARAMS_OF_SET_META;

    REBVAL *meta = ARG(meta);

    REBCTX *meta_ctx;
    if (ANY_CONTEXT(meta)) {
        if (IS_FRAME(meta))
            fail ("SET-META can't store context bindings, frames disallowed");

        meta_ctx = VAL_CONTEXT(meta);
    }
    else {
        assert(IS_NULLED(meta));
        meta_ctx = nullptr;
    }

    REBVAL *v = ARG(value);

    if (IS_ACTION(v))
        mutable_MISC(DetailsMeta, ACT_DETAILS(VAL_ACTION(v))) = meta_ctx;
    else
        mutable_MISC(VarlistMeta, CTX_VARLIST(VAL_CONTEXT(v))) = meta_ctx;

    RETURN (meta);
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
REBCTX *Copy_Context_Extra_Managed(
    REBCTX *original,
    REBLEN extra,
    REBU64 types
){
    assert(NOT_SERIES_FLAG(CTX_VARLIST(original), INACCESSIBLE));

    REBLEN len = (CTX_TYPE(original) == REB_MODULE) ? 0 : CTX_LEN(original);

    REBARR *varlist = Make_Array_For_Copy(
        len + extra + 1,
        SERIES_MASK_VARLIST | NODE_FLAG_MANAGED,
        nullptr // original_array, N/A because LINK()/MISC() used otherwise
    );
    RELVAL *dest = ARR_HEAD(varlist);

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

        SET_SERIES_USED(varlist, 1);

        if (CTX_META(original)) {
            mutable_MISC(VarlistMeta, varlist) = Copy_Context_Shallow_Managed(
                CTX_META(original)
            );
        }
        else {
            mutable_MISC(VarlistMeta, varlist) = nullptr;
        }
        INIT_LINK_KEYSOURCE(varlist, nullptr);
        mutable_BONUS(Patches, varlist) = nullptr;

        REBCTX *copy = CTX(varlist); // now a well-formed context
        assert(GET_SERIES_FLAG(varlist, DYNAMIC));

        REBSYM **psym = SER_HEAD(REBSYM*, PG_Symbols_By_Hash);
        REBSYM **psym_tail = SER_TAIL(REBSYM*, PG_Symbols_By_Hash);
        for (; psym != psym_tail; ++psym) {
            if (*psym == nullptr or *psym == &PG_Deleted_Symbol)
                continue;

            REBSER *patch = MISC(Hitch, *psym);
            while (GET_SERIES_FLAG(patch, BLACK))  // binding temps
                patch = SER(node_MISC(Hitch, patch));

            for (; patch != *psym; patch = SER(node_MISC(Hitch, patch))) {
                if (original == LINK(PatchContext, patch)) {
                    REBVAL *var = Append_Context(copy, nullptr, *psym);
                    Copy_Cell(var, SPECIFIC(ARR_SINGLE(ARR(patch))));
                    break;
                }
            }
        }

        return copy;
    }

    ASSERT_SERIES_MANAGED(CTX_KEYLIST(original));

    ++dest;

    // Now copy the actual vars in the context, from wherever they may be
    // (might be in an array, or might be in the chunk stack for FRAME!)
    //
    const REBVAR *src_tail;
    REBVAL *src = CTX_VARS(&src_tail, original);
    for (; src != src_tail; ++src, ++dest) {
        Copy_Cell_Core(  // trying to duplicate slot precisely
            dest,
            src,
            CELL_MASK_COPY | CELL_FLAG_VAR_MARKED_HIDDEN
        );

        REBFLGS flags = NODE_FLAG_MANAGED;  // !!! Review, which flags?
        Clonify(dest, flags, types);
    }

    SET_SERIES_LEN(varlist, CTX_LEN(original) + 1);
    varlist->leader.bits |= SERIES_MASK_VARLIST;

    REBCTX *copy = CTX(varlist); // now a well-formed context

    if (extra == 0)
        INIT_CTX_KEYLIST_SHARED(copy, CTX_KEYLIST(original));  // ->link field
    else {
        assert(CTX_TYPE(original) != REB_FRAME);  // can't expand FRAME!s

        REBSER *keylist = Copy_Series_At_Len_Extra(
            CTX_KEYLIST(original),
            0,
            CTX_LEN(original),
            extra,
            SERIES_MASK_KEYLIST | NODE_FLAG_MANAGED
        );

        mutable_LINK(Ancestor, keylist) = CTX_KEYLIST(original);

        INIT_CTX_KEYLIST_UNIQUE(copy, keylist);  // ->link field
    }

    // A FRAME! in particular needs to know if it points back to a stack
    // frame.  The pointer is NULLed out when the stack level completes.
    // If we're copying a frame here, we know it's not running.
    //
    if (CTX_TYPE(original) == REB_FRAME)
        mutable_MISC(VarlistMeta, varlist) = nullptr;
    else {
        // !!! Should the meta object be copied for other context types?
        // Deep copy?  Shallow copy?  Just a reference to the same object?
        //
        mutable_MISC(VarlistMeta, varlist) = nullptr;
    }

    mutable_BONUS(Patches, varlist) = nullptr;  // no virtual bind patches yet

    return copy;
}


//
//  MF_Context: C
//
void MF_Context(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    REBSTR *s = mo->series;

    REBCTX *c = VAL_CONTEXT(v);

    // Prevent endless mold loop:
    //
    if (Find_Pointer_In_Series(TG_Mold_Stack, c) != NOT_FOUND) {
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
    Push_Pointer_To_Series(TG_Mold_Stack, c);

    if (CELL_KIND(v) == REB_FRAME and not IS_FRAME_PHASED(v)) {
        REBARR *varlist = CTX_VARLIST(VAL_CONTEXT(v));
        if (GET_SUBCLASS_FLAG(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED)) {
            Append_Ascii(s, "make frame! [...invoked frame...]\n");
            Drop_Pointer_From_Series(TG_Mold_Stack, c);
            return;
        }
    }

    EVARS e;
    Init_Evars(&e, v);

    if (form) {
        //
        // Mold all words and their values ("key: <molded value>").
        // Because FORM-ing is lossy, we don't worry much about showing the
        // bad word isotopes as the plain bad words, or that blanks are
        // showing up as NULL.
        //
        bool had_output = false;
        while (Did_Advance_Evars(&e)) {
            Append_Spelling(mo->series, KEY_SYMBOL(e.key));
            Append_Ascii(mo->series, ": ");

            if (Is_Isotope(e.var)) {
                Append_Codepoint(mo->series, '~');
                Append_Spelling(mo->series, VAL_BAD_WORD_LABEL(e.var));
                Append_Codepoint(mo->series, '~');
            }
            else if (not IS_NULLED(e.var) and not IS_BLANK(e.var))
                Mold_Value(mo, e.var);

            Append_Codepoint(mo->series, LF);
            had_output = true;
        }
        Shutdown_Evars(&e);

        // Remove the final newline...but only if WE added to the buffer
        //
        if (had_output)
            Trim_Tail(mo, '\n');

        Drop_Pointer_From_Series(TG_Mold_Stack, c);
        return;
    }

    // Otherwise we are molding

    Pre_Mold(mo, v);

    Append_Codepoint(s, '[');

    mo->indent++;

    while (Did_Advance_Evars(&e)) {
        New_Indented_Line(mo);

        const REBSTR *spelling = KEY_SYMBOL(e.key);

        // If an interned string is for a SYMBOL! then it doesn't come in
        // SET-WORD! form.  For example, `::` has no SET- or GET- version.
        // Hence if the symbol is in a context, we have to use an alternate
        // representation of a SET-BLOCK, like `[::]: 10`
        //
        if (GET_SUBCLASS_FLAG(INTERN, spelling, ARROW)) {
            Append_Ascii(s, "[");
            Append_Utf8(s, STR_UTF8(spelling), STR_SIZE(spelling));
            Append_Ascii(s, "]");
        }
        else
            Append_Utf8(s, STR_UTF8(spelling), STR_SIZE(spelling));

        Append_Ascii(s, ": ");

        if (IS_NULLED(e.var))
            Append_Ascii(s, "'");  // `field: '` would evaluate to null
        else {
            // We want the molded object to be able to "round trip" back to
            // the state it's in based on reloading the values.  Currently
            // this is conservative and doesn't put quote marks on things
            // that don't need it because they are inert, but maybe that
            // isn't a good idea...depends on the whole block/object model.
            //
            if (Is_Isotope(e.var)) {
                //
                // Mold_Value() rejects isotopes.  Do it manually.  (Service
                // routine Mold_Bad_Word_Isotope_Ok() might be useful?  Calling
                // it Mold_Isotope() would be confusing because the isotope
                // status would be lost).
                //
                Append_Ascii(s, "~");
                Append_Spelling(s, VAL_BAD_WORD_LABEL(e.var));
                Append_Ascii(s, "~");
            }
            else {
                if (not ANY_INERT(e.var))
                    Append_Ascii(s, "'");
                Mold_Value(mo, e.var);
            }
        }
    }
    Shutdown_Evars(&e);

    mo->indent--;
    New_Indented_Line(mo);
    Append_Codepoint(s, ']');

    End_Mold(mo);

    Drop_Pointer_From_Series(TG_Mold_Stack, c);
}


//
//  Context_Common_Action_Maybe_Unhandled: C
//
// Similar to Series_Common_Action_Maybe_Unhandled().  Introduced because
// PORT! wants to act like a context for some things, but if you ask an
// ordinary object if it's OPEN? it doesn't know how to do that.
//
REB_R Context_Common_Action_Maybe_Unhandled(
    REBFRM *frame_,
    const REBSYM *verb
){
    REBVAL *v = D_ARG(1);
    REBCTX *c = VAL_CONTEXT(v);

    // !!! The PORT! datatype wants things like LENGTH OF to give answers
    // based on the content of the port, not the number of fields in the
    // PORT! object.  This ties into a number of other questions:
    //
    // https://forum.rebol.info/t/1689
    //
    if (CTX_TYPE(c) == REB_PORT)
        return R_UNHANDLED;

    switch (ID_OF_SYMBOL(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // covered by `v`

        REBVAL *property = ARG(property);
        switch (VAL_WORD_ID(property)) {
          case SYM_LENGTH: // !!! Should this be legal?
            return Init_Integer(D_OUT, CTX_LEN(c));

          case SYM_TAIL_Q: // !!! Should this be legal?
            return Init_Logic(D_OUT, CTX_LEN(c) == 0);

          case SYM_WORDS:
            return Init_Block(D_OUT, Context_To_Array(v, 1));

          case SYM_VALUES:
            return Init_Block(D_OUT, Context_To_Array(v, 2));

          case SYM_BODY:
            return Init_Block(D_OUT, Context_To_Array(v, 3));

        // Noticeably not handled by average objects: SYM_OPEN_Q (`open?`)

          default:
            break;
        }

        return R_UNHANDLED; }

      default:
        break;
    }

    return R_UNHANDLED;
}


//
//  REBTYPE: C
//
// Handles object!, module!, and error! datatypes.
//
REBTYPE(Context)
{
  blockscope {
    REB_R r = Context_Common_Action_Maybe_Unhandled(frame_, verb);
    if (r != R_UNHANDLED)
        return r;
  }

    REBVAL *context = D_ARG(1);
    REBCTX *c = VAL_CONTEXT(context);

    switch (ID_OF_SYMBOL(verb)) {

    //=//// PICK-POKE* (see %sys-pick.h for explanation) ///////////////////=//

      case SYM_PICK_POKE_P: {
        INCLUDE_PARAMS_OF_PICK_POKE_P;
        UNUSED(ARG(location));

        REBVAL *steps = ARG(steps);  // STEPS block: 'a/(1 + 2)/b => [a 3 b]
        REBLEN steps_left = VAL_LEN_AT(steps);
        if (steps_left == 0)
            fail (steps);

        const RELVAL *picker = VAL_ARRAY_ITEM_AT(steps);
        if (not IS_WORD(picker))
            fail (picker);

        const REBSYM *symbol = VAL_WORD_SYMBOL(picker);

        REBVAL *setval = ARG(value);
        bool poking = not IS_NULLED(setval);

        if (steps_left == 1 and poking) {  // `obj.field: 10`, handle now
            Meta_Unquotify(setval);

          update_bits: ;
            REBVAL *var = TRY_VAL_CONTEXT_MUTABLE_VAR(context, symbol);
            if (not var)
                fail (Error_Bad_Path_Pick_Raw(picker));

            Copy_Cell(var, setval);
        }
        else {
            const REBVAL *var = TRY_VAL_CONTEXT_VAR(context, symbol);
            if (not var)
                fail (Error_Bad_Path_Pick_Raw(picker));

          #if !defined(NDEBUG)
            enum Reb_Kind var_type = VAL_TYPE(var);
          #endif

            if (steps_left == 1) {  // `obj.field` and not `outer.inner.field`
                assert(not poking);  // would have been handled above
                RETURN (var);  // no dispatch on more steps needed
            }

            ++VAL_INDEX_RAW(ARG(steps));  // take `inner` out of steps

            REB_R r = Run_Pickpoke_Dispatch(frame_, verb, var);
            if (r == R_THROWN)
                return R_THROWN;

            TRASH_POINTER_IF_DEBUG(var);  // arbitrary code may moved memory

            if (not poking)
                return r;

            if (r != nullptr) {  // the update needs our cell's bits to change
                assert(r == D_OUT);
              #if !defined(NDEBUG)
                assert(VAL_TYPE(D_OUT) == var_type);
              #endif
                setval = D_OUT;
                goto update_bits;
            }
        }

        assert(poking);
        return nullptr; }  // caller's REBCTX* is not stale, no update needed


      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // covered by `v`

        if (VAL_TYPE(context) != REB_FRAME)
            break;

        REBVAL *property = ARG(property);
        SYMID sym = VAL_WORD_ID(property);

        if (sym == SYM_LABEL) {
            //
            // Can be answered for frames that have no execution phase, if
            // they were initialized with a label.
            //
            option(const REBSYM*) label = VAL_FRAME_LABEL(context);
            if (label)
                return Init_Word(D_OUT, unwrap(label));

            // If the frame is executing, we can look at the label in the
            // REBFRM*, which will tell us what the overall execution label
            // would be.  This might be confusing, however...if the phase
            // is drastically different.  Review.
        }

        if (sym == SYM_ACTION) {
            //
            // Currently this can be answered for any frame, even if it is
            // expired...though it probably shouldn't do this unless it's
            // an indefinite lifetime object, so that paramlists could be
            // GC'd if all the frames pointing to them were expired but still
            // referenced somewhere.
            //
            return Init_Action(
                D_OUT,
                VAL_FRAME_PHASE(context),  // just a REBACT*, no binding
                VAL_FRAME_LABEL(context),
                VAL_FRAME_BINDING(context)  // e.g. where RETURN returns to
            );
        }

        REBFRM *f = CTX_FRAME_MAY_FAIL(c);

        switch (sym) {
          case SYM_FILE: {
            const REBSTR *file = FRM_FILE(f);
            if (not file)
                return nullptr;
            return Init_File(D_OUT, file); }

          case SYM_LINE: {
            REBLIN line = FRM_LINE(f);
            if (line == 0)
                return nullptr;
            return Init_Integer(D_OUT, line); }

          case SYM_LABEL: {
            if (not f->label)
                return nullptr;
            return Init_Word(D_OUT, unwrap(f->label)); }

          case SYM_NEAR:
            return Init_Near_For_Frame(D_OUT, f);

          case SYM_PARENT: {
            //
            // Only want action frames (though `pending? = true` ones count).
            //
            REBFRM *parent = f;
            while ((parent = parent->prior) != FS_BOTTOM) {
                if (not Is_Action_Frame(parent))
                    continue;

                REBCTX* ctx_parent = Context_For_Frame_May_Manage(parent);
                RETURN (CTX_ARCHETYPE(ctx_parent));
            }
            return nullptr; }

          default:
            break;
        }
        fail (Error_Cannot_Reflect(VAL_TYPE(context), property)); }


      case SYM_APPEND: {
        REBVAL *arg = D_ARG(2);
        if (IS_NULLED_OR_BLANK(arg))
            RETURN (context);  // don't fail on R/O if it would be a no-op

        ENSURE_MUTABLE(context);
        if (not IS_OBJECT(context) and not IS_MODULE(context))
            return R_UNHANDLED;
        Append_To_Context(context, arg);
        RETURN (context); }

      case SYM_COPY: {  // Note: words are not copied and bindings not changed!
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PAR(value));  // covered by `context`

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        REBU64 types = 0;
        if (REF(types)) {
            if (IS_DATATYPE(ARG(types)))
                types = FLAGIT_KIND(VAL_TYPE_KIND(ARG(types)));
            else {
                types |= VAL_TYPESET_LOW_BITS(ARG(types));
                types |= cast(REBU64, VAL_TYPESET_HIGH_BITS(ARG(types))) << 32;
            }
        }
        else if (REF(deep))
            types = TS_STD_SERIES;

        // !!! Special attention on copying frames is going to be needed,
        // because copying a frame will be expected to create a new identity
        // for an ACTION! if that frame is aliased AS ACTION!.  The design
        // of this is still evolving, but we don't want archetypal values
        // otherwise we could not `do copy f`, so initialize with label.
        //
        if (IS_FRAME(context)) {
            return Init_Frame(
                D_OUT,
                Copy_Context_Extra_Managed(c, 0, types),
                VAL_FRAME_LABEL(context)
            );
        }

        return Init_Any_Context(
            D_OUT,
            VAL_TYPE(context),
            Copy_Context_Extra_Managed(c, 0, types)
        ); }

      case SYM_SELECT:
      case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;
        UNUSED(ARG(series));  // extracted as `c`
        UNUSED(ARG(reverse));
        UNUSED(ARG(last));

        if (REF(part) or REF(skip) or REF(tail) or REF(match))
            fail (Error_Bad_Refines_Raw());

        REBVAL *pattern = ARG(pattern);
        if (not IS_WORD(pattern))
            return nullptr;

        REBLEN n = Find_Symbol_In_Context(
            context,
            VAL_WORD_SYMBOL(pattern),
            did REF(case)
        );
        if (n == 0)
            return nullptr;

        if (ID_OF_SYMBOL(verb) == SYM_FIND)
            return Init_True(D_OUT); // !!! obscures non-LOGIC! result?

        RETURN (CTX_VAR(c, n)); }

      default:
        break;
    }

    return R_UNHANDLED;
}


//
//  construct: native [
//
//  "Creates an ANY-CONTEXT! instance"
//
//      return: [<opt> any-context!]
//      spec [<blank> block!]
//          "Object specification block (bindings modified)"
//      /only "Values are kept as-is"
//      /with "Use a parent/prototype context"
//          [any-context!]
//  ]
//
REBNATIVE(construct)
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
    REBCTX *parent = REF(with)
        ? VAL_CONTEXT(ARG(with))
        : cast(REBCTX*, nullptr);  // C++98 ambiguous w/o cast

    // This parallels the code originally in CONSTRUCT.  Run it if the /ONLY
    // refinement was passed in.
    //
  blockscope {
    const RELVAL *tail;
    RELVAL *at = VAL_ARRAY_AT_MUTABLE_HACK(&tail, spec);
    if (REF(only)) {
        Init_Object(
            D_OUT,
            Construct_Context_Managed(
                REB_OBJECT,
                at,  // warning: modifies binding!
                tail,
                VAL_SPECIFIER(spec),
                parent
            )
        );
        return D_OUT;
    }
  }

    // Scan the object for top-level set words in order to make an
    // appropriately sized context.
    //
    const RELVAL *tail;
    RELVAL *at = VAL_ARRAY_AT_ENSURE_MUTABLE(&tail, spec);

    REBCTX *ctx = Make_Context_Detect_Managed(
        parent ? CTX_TYPE(parent) : REB_OBJECT,  // !!! Presume object?
        at,
        tail,
        parent
    );
    Init_Object(D_OUT, ctx);  // GC protects context

    // !!! This binds the actual body data, not a copy of it.  See
    // Virtual_Bind_Deep_To_New_Context() for future directions.
    //
    Bind_Values_Deep(at, tail, CTX_ARCHETYPE(ctx));

    DECLARE_LOCAL (dummy);
    if (Do_Any_Array_At_Throws(dummy, spec, SPECIFIED)) {
        Move_Cell(D_OUT, dummy);
        return R_THROWN;  // evaluation result ignored unless thrown
    }

    return D_OUT;
}
