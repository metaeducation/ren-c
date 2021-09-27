//
//  File: %c-bind.c
//  Summary: "Word Binding Routines"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Binding relates a word to a context.  Every word can be either bound,
// specifically bound to a particular context, or bound relatively to a
// function (where additional information is needed in order to find the
// specific instance of the variable for that word as a key).
//

#include "sys-core.h"


//
//  Get_Word_Container: C
//
// Find the context a word is bound into.  This must account for the various
// binding forms: Relative Binding, Derived Binding, and Virtual Binding.
//
// The reason this is broken out from the Lookup_Word() routines is because
// sometimes read-only-ness of the context is heeded, and sometimes it is not.
// Splitting into a step that returns the context and the index means the
// main work of finding where to look up doesn't need to be parameterized
// with that.
//
// This function is used by Derelativize(), and so it shouldn't have any
// failure mode while it's running...even if the context is inaccessible or
// the word is unbound.  Errors should be raised by callers if applicable.
//
option(REBSER*) Get_Word_Container(
    REBLEN *index_out,
    const RELVAL* any_word,
    REBSPC *specifier,
    enum Reb_Attach_Mode mode
){
  #if !defined(NDEBUG)
    *index_out = 0xDECAFBAD;  // trash index to make sure it gets set
  #endif

    const REBSYM *symbol = VAL_WORD_SYMBOL(VAL_UNESCAPED(any_word));

    if (!strcmp("what-dir", STR_UTF8(symbol)) and TG_Tick > 104783)
        symbol = symbol;

    // What we're trying now is that once something becomes *concretely* bound
    // it is no longer a candidate for overriding.  This means that a LET
    // would be useless as an override once concrete binding is achieved...the
    // binding would have to be forcibly removed.
    //
    REBSER *binding = VAL_WORD_BINDING(any_word);

    REBCTX *emerge = nullptr;

    for (; specifier != nullptr; specifier = NextPatch(specifier)) {
        REBSPC *follow = nullptr;

      blockscope {
        // Patches have enough bits to rule out certain kinds of binds.  It
        // may be all right to think about sparing a bit on varlists too so
        // that if they are used for binding at their time of creation they
        // can have such control as well.
        //
        if (IS_PATCH(specifier)) {
            if (GET_SUBCLASS_FLAG(PATCH, specifier, SET_WORDS_ONLY))
                if (REB_SET_WORD != CELL_KIND(VAL_UNESCAPED(any_word)))
                    continue;
        }

        if (
            IS_PATCH(specifier)
            and NOT_SUBCLASS_FLAG(PATCH, specifier, LET)
            and IS_BLOCK(ARR_SINGLE(specifier))
        ){
            follow = m_cast(REBSPC*, VAL_ARRAY(ARR_SINGLE(specifier)));
            goto maybe_follow;
        }

        // A LET patch is just one variable.  It matches if the symbol matches.
        //
        // You might want to virtually bind to a previously existing LET vs.
        // just capture the stream of the let itself.  (This can happen in
        // merging scenarios, right?)  So that form is done with a WORD! in
        // the patch that has the binding.
        //
        REBARR *let;
        if (
            IS_PATCH(specifier) and ((
                GET_SUBCLASS_FLAG(PATCH, specifier, LET)
                and (let = specifier)
            ) or (
                IS_WORD(ARR_SINGLE(specifier))
                and (let = ARR(BINDING(ARR_SINGLE(specifier))))
            ))
        ){
            if (INODE(LetSymbol, let) == symbol) {
                *index_out = INDEX_PATCHED;
                return let;
            }
            continue;
        }

        // A MODULE can be searched relatively quickly since the word's symbol
        // points directly to a linked list of variable instances for modules
        // using that word.
        //
        REBCTX *mod;
        if ((
            IS_VARLIST(specifier)
            and CTX_TYPE(CTX(specifier)) == REB_MODULE
            and (mod = CTX(specifier))
        ) or (
            IS_PATCH(specifier)
            and IS_MODULE(ARR_SINGLE(specifier))
            and (mod = VAL_CONTEXT(ARR_SINGLE(specifier)))
        )){
            // Trying to boot.  :-/  Don't let modules win over top of things
            // that are concretely bound...unless they are in the Lib_Context.
            // (Yes, these exceptions are terrible and messy, but if it boots
            // then maybe something can shake out that is less terrible.)
            //
            if (binding and not (
                IS_PATCH(binding) and GET_SUBCLASS_FLAG(PATCH, binding, LET)
                and INODE(ModvarContext, binding) == Lib_Context
            )){
                continue;
            }

            REBVAL *var = MOD_VAR(mod, symbol, true);
            if (var) {
                if (emerge and (mod == Lib_Context or mod == Sys_Context))
                    continue;  // let the emerge win

                if (mode == ATTACH_WRITE and GET_CELL_FLAG(var, PROTECTED))
                    fail (Error_Protected_Word_Raw(any_word));


                *index_out = INDEX_PATCHED;
                return Singular_From_Cell(var);
            }

            // If a variable is unbound and we ask to emerge the variable
            // then we do this only if it's a last resort, e.g. this is the
            // last bind in the chain.
            //
            // !!! This "promiscuous" behavior is a lot like JavaScript without
            // its "strict mode" turned on, as it has a good chance of letting
            // you write to variable names that are typos.  Some way of
            // exposing a strict mode is needed in the module system...but for
            // now just make Lib and Sys strict.  Everything else is non-strict
            // just to start with basic compatibility with history.
            //
            if (mode == ATTACH_WRITE or mode == ATTACH_COPY) {
                if (mod != Lib_Context and mod != Sys_Context) {
                    emerge = mod;
                    binding = nullptr;  // !!! Yes, crazy, but... WHAT-DIR
                }
            }

            continue;
        }

        // Seaching OBJECT is slower.  There used to be some attempts at
        // optimizing this, but now that modules have distributed lookups it
        // would have to be a large object to be that much of a problem.
        //
        // !!! Specialization does a binding of SET-WORD!s using a FRAME!.
        // For now when a context is used directly as the specifier there are
        // different rules and enforcements for frame...see next section.
        //
        // !!! This maybe should be merged with the module code, with the
        // exception of the emergence on not finding it...review if the
        // branch costs gets better or worse when this stabilizes.
        //
        const RELVAL *obj;
        if ((
            IS_VARLIST(specifier)
            and (obj = CTX_ARCHETYPE(CTX(specifier)))
        ) or (
            IS_PATCH(specifier)
            and ANY_CONTEXT(ARR_SINGLE(specifier))
            and (obj = ARR_SINGLE(specifier))
            and (follow = NextPatch(SPC(VAL_CONTEXT(obj))), true)
        )){
      /*    try_lookup_obj: */
            *index_out = Find_Symbol_In_Context(obj, symbol, true);
            if (*index_out == 0)
                goto maybe_follow;

            // !!! FOR-EACH uses the slots in an object to count how
            // many arguments there are...and if a slot is reusing an
            // existing variable it holds that variable.  This ties into
            // general questions of hiding which is the same bit.  Don't
            // count it as a hit.
            //
            // !!! If this is still relevant, Find_Symbol_In_Context() is the
            // right place for it--not here.
            //
            if (GET_CELL_FLAG(
                CTX_VAR(VAL_CONTEXT(obj), *index_out),
                BIND_NOTE_REUSE
            )){
                goto maybe_follow;
            }

            return VAL_CONTEXT(obj);
        }

        // Historically if a frame is in a binding chain, it only applies if
        // the body had been copied.  We have to honor that because otherwise
        // we have a problem, in that we do not know what phase to use.
        //
     /*   REBCTX *frm;
        if (
            IS_VARLIST(specifier)
            and CTX_TYPE(CTX(specifier)) == REB_FRAME
            and (frm = CTX(specifier))
        ){
            if (binding == UNBOUND or not IS_DETAILS(binding)) {
                //
                obj = CTX_ARCHETYPE(frm);
                goto try_lookup_obj;
            }
            else {
                // RELATIVE BINDING: The word was made during a deep copy of the block
                // that was given as a function's body, and stored a reference to that
                // ACTION! as its binding.  To get a variable for the word, we must
                // find the right function call on the stack (if any) for the word to
                // refer to (the FRAME!)

            #if !defined(NDEBUG)
                if (specifier == SPECIFIED) {
                    printf("Get_Context_Core on relative value without specifier\n");
                    panic (any_word);
                }
            #endif

                // We can only check for a match of the underlying function.  If we
                // checked for an exact match, then the same function body could not
                // be repurposed for dispatch e.g. in copied, hijacked, or adapted
                // code, because the identity of the derived function would not match
                // up with the body it intended to reuse.
                //
                if (Action_Is_Base_Of(ACT(binding), CTX_FRAME_ACTION(frm))) {
                    *index_out = VAL_WORD_INDEX(any_word);
                    return CTX_VARLIST(frm);
                }
                continue;
            }
        }*/

        if (IS_DETAILS(specifier))
            assert(!"relative binding used as specifier");
        assert(!"Unknown specifier category in chain");
        panic (specifier);
      }

      maybe_follow: {
        if (not IS_PATCH(specifier))
            continue;  // follow happens automatically

        if (NOT_SUBCLASS_FLAG(PATCH, specifier, FOLLOW))
            continue;

        if (not follow)
            continue;

        option(REBSER*) lookup = Get_Word_Container(
            index_out,
            any_word,
            follow,
            mode
        );
        if (lookup)
            return lookup;
      }
    }

    if (binding == UNBOUND) {
        if (emerge) {
            if (mode == ATTACH_COPY) {
                *index_out = INDEX_ATTACHED;
                return emerge;
            }
            assert(mode == ATTACH_WRITE);
            REBVAR *var = Append_Context(emerge, nullptr, symbol);
            *index_out = INDEX_PATCHED;
            return Singular_From_Cell(var);
        }
        return nullptr;
    }

    // !!! Work was done on trying to chew some cache bits out of the word
    // cell.  This is made troublesome by the fact that the cell may be shared
    // among many views via specifiers...but the rules are changing now with
    // the new ideas.

    REBCTX *c;

    if (IS_PATCH(binding)) {
        //
        // LET BINDING: Directly bound to a LET variable.  This happens when
        // a word that is bound to a LET gets copied so it's not virtual.
        //
        assert(GET_SUBCLASS_FLAG(PATCH, binding, LET));
        *index_out = 1;  // !!! lie, review
        return binding;
    }

    if (IS_VARLIST(binding)) {
        //
        // !!! Work in progress...shortcut that allows finding variables
        // in Lib_Context, that is to be designed with a "force reified vs not"
        // concept.  Idea would be (I guess) that a special form of mutable
        // lookup would say "I want that but be willing to make it."
        //
        if (CTX_TYPE(CTX(binding)) == REB_MODULE) {
            REBSER *patch = MISC(Hitch, symbol);
            while (GET_SERIES_FLAG(patch, BLACK))  // binding temps
                patch = SER(node_MISC(Hitch, patch));

            for (; patch != symbol; patch = SER(node_MISC(Hitch, patch))) {
                if (INODE(ModvarContext, patch) != binding)
                    continue;

                // Since this is now resolving to the context, update the
                // cache inside the word itself.  Don't do this for inherited
                // variables, since if we hardened the reference to the
                // inherited variable we'd not see an override if it came
                // into existence in the actual context.
                //
                INIT_VAL_WORD_BINDING(m_cast(RELVAL*, any_word), patch);
                INIT_VAL_WORD_INDEX(m_cast(RELVAL*, any_word), 1);

                *index_out = 1;
                return patch;
            }

            // !!! One original goal with Sea of Words was to enable something
            // like JavaScript's "strict mode", to prevent writing to variables
            // that had not been somehow previously declared.  However, that
            // is a bit too ambitious for a first rollout...as just having the
            // traditional behavior of "any assignment works" is something
            // people are used to.  Don't do it for the Lib_Context (so
            // mezzanine is still guarded) but as a first phase, permit the
            // "emergence" of any variable that is attached to a module.
            //
            if (
                mode == ATTACH_WRITE
                and binding != Lib_Context
                and binding != Sys_Context
            ){
                *index_out = INDEX_ATTACHED;
                return Singular_From_Cell(
                    Init_Unset(Append_Context(CTX(binding), nullptr, symbol))
                );
            }

            // non generic inheritance; inherit only from Lib for now
            //
            if (mode != ATTACH_READ or binding == Lib_Context)
                return nullptr;

            patch = MISC(Hitch, symbol);
            while (GET_SERIES_FLAG(patch, BLACK))  // binding temps
                patch = SER(node_MISC(Hitch, patch));

            for (; patch != symbol; patch = SER(node_MISC(Hitch, patch))) {
                if (INODE(ModvarContext, patch) != Lib_Context)
                    continue;

                // We return it, but don't cache it in the cell.  Note that
                // Derelativize() or other operations should not cache either
                // as it would commit to the inherited version, never seeing
                // derived overrides.
                //
                *index_out = 1;
                return patch;
            }

            return nullptr;
        }

        // SPECIFIC BINDING: The context the word is bound to is explicitly
        // contained in the `any_word` REBVAL payload.  Extract it, but check
        // to see if there is an override via "DERIVED BINDING", e.g.:
        //
        //    o1: make object [a: 10 f: meth [] [print a]]
        //    o2: make o1 [a: 20]
        //
        // O2 doesn't copy F's body, but its copy of the ACTION! cell in o2/f
        // gets its ->binding to point at O2 instead of O1.  When o2/f runs,
        // the frame stores that pointer, and we take it into account when
        // looking up `a` here, instead of using a's stored binding directly.

        c = CTX(binding); // start with stored binding

        // !!! This stuff is complex but was worked out in the past for some
        // cases, it will have to be rethought because it's crashing.

    /*    if (specifier == SPECIFIED) {
            //
            // Lookup must be determined solely from bits in the value
            //
        }
        else {
            REBSER *f_binding = SPC_BINDING(specifier); // can't fail()
            if (f_binding and Is_Overriding_Context(c, CTX(f_binding))) {
                //
                // The specifier binding overrides--because what's happening
                // is that this cell came from a METHOD's body, where the
                // particular ACTION! value cell triggering it held a binding
                // of a more derived version of the object to which the
                // instance in the method body refers.
                //
                c = CTX(f_binding);
            }
        } */
    }
    else {
        assert(IS_DETAILS(binding));
        c = nullptr;
        assert(!"relative binding handled above");
    }

    *index_out = VAL_WORD_INDEX(any_word);
    return CTX_VARLIST(c);
}


// This routine will merge virtual binding patches, returning one where the
// child is at the beginning of the chain.  This will preserve the child's
// frame resolving context (if any) that terminates it.
//
// If the returned chain manages to reuse an existing case, then the result
// will have ARRAY_FLAG_PATCH_REUSED set.  This can inform higher levels of
// whether it's worth searching their patchlist or not...as newly created
// patches can't appear in their prior create list.
//
inline static REBARR *Merge_Patches_May_Reuse(
    REBARR *parent,
    REBARR *child
){
    assert(IS_PATCH(parent));
    assert(IS_PATCH(child));

    // Case of already incorporating.  Came up with:
    //
    //    1 then x -> [2 also y -> [3]]
    //
    // A virtual link for Y is added on top of the virtual link for X that
    // resides on the [3] block.  But then feed generation for [3] tries to
    // apply the Y virtual link again.  !!! Review if that's just inefficient.
    //
    if (NextPatch(parent) == child) {
        SET_SUBCLASS_FLAG(PATCH, parent, REUSED);
        return parent;
    }

    // If we get to the end of the merge chain and don't find the child, then
    // we're going to need a patch that incorporates it.
    //
    REBARR *next;
    bool was_next_reused;
    if (NextPatch(parent) == nullptr or IS_VARLIST(NextPatch(parent))) {
        next = child;
        was_next_reused = true;
    }
    else {
        next = Merge_Patches_May_Reuse(NextPatch(parent), child);
        was_next_reused = GET_SUBCLASS_FLAG(PATCH, next, REUSED);
    }

    // If we have to make a new patch due to non-reuse, then we cannot make
    // one out of a LET, since the patch *is* the variable.  It's actually
    // uncommon for this to happen, but here's an example of how to force it:
    //
    //     block1: do [let x: 10, [x + y]]
    //     block2: do compose/deep [let y: 20, [(block1)]]
    //     30 = do first block2
    //
    // So we have to make a new patch that points to the LET, or promote it
    // (using node-identity magic) into an object.  We point at the LET.
    //
    REBARR *patch = Alloc_Singular(
        FLAG_FLAVOR(PATCH)
            | NODE_FLAG_MANAGED
            | SERIES_FLAG_LINK_NODE_NEEDS_MARK
    );
    if (GET_SUBCLASS_FLAG(PATCH, parent, LET)) {
        Init_Any_Word_Patched(ARR_SINGLE(patch), REB_WORD, parent);
    }
    else {
        Copy_Cell(ARR_SINGLE(patch), ARR_SINGLE(parent));
    }

    mutable_LINK(NextPatch, patch) = next;
    mutable_MISC(Variant, patch) = nullptr;  // defunct feature atm.
    mutable_INODE(VbindUnused, patch) = nullptr;

    UNUSED(was_next_reused);  // !!! revisit
    return patch;
}


//
//  Derive_Specifier_Core: C
//
// An ANY-ARRAY! cell has a pointer's-worth of spare space in it, which is
// used to keep track of the information required to further resolve the
// words and arrays that are inside of it.  Each time code wishes to take a
// step descending into an array's contents, this "specifier" information
// must be merged with the specifier that is being applied.
//
// Specifier state only accrues in this way while descending through nodes.
// Jumping to a new value...e.g. fetching a REBVAL* out of a WORD! variable,
// should restart the process with a new specifier.
//
// The returned specifier must not lose the ability to resolve relative
// values, so it has to remember what frame relative values are for.
//
REBSPC *Derive_Specifier_Core(
    REBSPC *specifier,  // merge this specifier...
    const RELVAL* v  // ...onto the one in this array
){
    enum Reb_Kind heart = CELL_HEART(VAL_UNESCAPED(v));
    assert(ANY_ARRAY_KIND(heart) or ANY_STRING_KIND(heart));
    UNUSED(heart);

    if (IS_TEXT(v) and !strcmp(STR_UTF8(VAL_STRING(v)), "alchemy"))
        v = v;

    REBARR *old = ARR(BINDING(v));

    // !!! Question: Does the "I was a parent" vs "I was a child" distinction
    // matter when doing a combination?  It may be that cells should hold a
    // bit saying whether their specifier is an "over specifier" or an
    // "under specifier".  Until that's figured out, if either of them are
    // nulled out then return the other one.

    REBSPC *result;
    if (old == UNBOUND) {
        if (specifier and GET_SERIES_FLAG(specifier, INACCESSIBLE))
            return &PG_Inaccessible_Series;
        result = specifier;
    }
    else if (specifier == SPECIFIED) {
        if (GET_SERIES_FLAG(old, INACCESSIBLE))
            return &PG_Inaccessible_Series;
        assert(not IS_DETAILS(old));  // requires specifier
        result = old;
    }
    else if (
        GET_SERIES_FLAG(specifier, INACCESSIBLE)
        or GET_SERIES_FLAG(old, INACCESSIBLE)
    ){
        return &PG_Inaccessible_Series;
    }
    else if (IS_DETAILS(old)) {
        //
        // The stored binding is relative to a function, and so the specifier
        // we have *must* be able to give us the matching FRAME! instance.
        //
        // We have to be content with checking for a match in underlying
        // functions, vs. checking for an exact match.  Else hijackings or
        // COPY'd actions, or adapted preludes, could not match up with
        // actions put in the specifier.  We'd have to make new and
        // re-relativized copies of the bodies--which is not only wasteful,
        // but breaks the "black box" quality of function composition.
        //

      #if !defined(NDEBUG)

      /*  REBSPC *test = specifier;
        while (not (
            IS_VARLIST(test) and CTX_TYPE(CTX(test)) == REB_FRAME
        )){
            test = NextPatch(test);
        }

        REBCTX *frame_ctx = try_unwrap(SPC_FRAME_CTX(test));
        if (
            frame_ctx == nullptr
            or (
                NOT_SERIES_FLAG(CTX_VARLIST(frame_ctx), INACCESSIBLE) and
                not Action_Is_Base_Of(ACT(old), CTX_FRAME_ACTION(frame_ctx))
            )
        ){
            printf("Function mismatch in specific binding, expected:\n");
            PROBE(ACT_ARCHETYPE(ACT(old)));
            printf("Panic on relative value\n");
            panic (v);
        } */
      #endif

        result = specifier;  // input specifier will serve for derelativizations
    }
    else if (specifier == old or NextPatch(specifier) == old) {
        result = specifier;
    }
    else {
        // We want to effectively tack the bindings that v has "underneath"
        // the bindings provided by the specifier.
        //
        REBARR *patch = Alloc_Singular(
            FLAG_FLAVOR(PATCH)
                | NODE_FLAG_MANAGED
                | SERIES_FLAG_LINK_NODE_NEEDS_MARK
                | PATCH_FLAG_FOLLOW
        );
        if (IS_VARLIST(specifier)) {
            //
            // Frame specifiers don't start out managed... have to manage
            //
            if (NOT_SERIES_FLAG(specifier, MANAGED)) {
                SET_SERIES_FLAG(specifier, MANAGED);
            }
            Copy_Cell(ARR_SINGLE(patch), CTX_ARCHETYPE(CTX(specifier)));
        }
        else {
            assert(IS_PATCH(specifier));
            Init_Block(ARR_SINGLE(patch), specifier);
        }
        mutable_LINK(NextPatch, patch) = old;
        mutable_MISC(Variant, patch) = nullptr;  // defunct feature atm.
        mutable_INODE(VbindUnused, patch) = nullptr;

        result = patch;
    }

    if (old and IS_DETAILS(old))
        assert(result != SPECIFIED);

    assert(result == SPECIFIED or IS_PATCH(result) or IS_VARLIST(result));
    return result;
}


//
//  Bind_Values_Inner_Loop: C
//
// Bind_Values_Core() sets up the binding table and then calls
// this recursive routine to do the actual binding.
//
void Bind_Values_Inner_Loop(
    struct Reb_Binder *binder,
    RELVAL *head,
    const RELVAL *tail,
    REBCTX *context,
    REBU64 bind_types, // !!! REVIEW: force word types low enough for 32-bit?
    REBU64 add_midstream_types,
    REBFLGS flags
){
    RELVAL *v = head;
    for (; v != tail; ++v) {
        REBCEL(const*) cell = VAL_UNESCAPED(v);
        enum Reb_Kind heart = CELL_HEART(cell);

        // !!! Review use of `heart` bit here, e.g. when a REB_PATH has an
        // REB_BLOCK heart, why would it be bound?  Problem is that if we
        // do not bind `/` when REB_WORD is asked then `/` won't be bound.
        //
        REBU64 type_bit = FLAGIT_KIND(heart);

        if (type_bit & bind_types) {
            const REBSYM *symbol = VAL_WORD_SYMBOL(cell);

          if (CTX_TYPE(context) == REB_MODULE) {
            bool strict = true;
            REBVAL *lookup = MOD_VAR(context, symbol, strict);
            if (lookup) {
                INIT_VAL_WORD_BINDING(v, Singular_From_Cell(lookup));
                INIT_VAL_WORD_INDEX(v, 1);
            }
            else if (type_bit & add_midstream_types) {
                Init_Unset(Append_Context(context, v, nullptr));
            }
          }
          else {
            REBINT n = Get_Binder_Index_Else_0(binder, symbol);
            if (n > 0) {
                //
                // A binder index of 0 should clearly not be bound.  But
                // negative binder indices are also ignored by this process,
                // which provides a feature of building up state about some
                // words while still not including them in the bind.
                //
                assert(cast(REBLEN, n) <= CTX_LEN(context));

                // We're overwriting any previous binding, which may have
                // been relative.

                INIT_VAL_WORD_BINDING(v, context);
                INIT_VAL_WORD_INDEX(v, n);
            }
            else if (type_bit & add_midstream_types) {
                //
                // Word is not in context, so add it if option is specified
                //
                Append_Context(context, v, nullptr);
                Add_Binder_Index(binder, symbol, VAL_WORD_INDEX(v));
            }
          }
        }
        else if (flags & BIND_DEEP) {
            if (ANY_ARRAY_KIND(heart)) {
                const RELVAL *sub_tail;
                RELVAL *sub_at = VAL_ARRAY_AT_MUTABLE_HACK(
                    &sub_tail,
                    VAL_UNESCAPED(v)
                );
                Bind_Values_Inner_Loop(
                    binder,
                    sub_at,
                    sub_tail,
                    context,
                    bind_types,
                    add_midstream_types,
                    flags
                );
            }
        }
    }
}


//
//  Bind_Values_Core: C
//
// Bind words in an array of values terminated with END
// to a specified context.  See warnings on the functions like
// Bind_Values_Deep() about not passing just a singular REBVAL.
//
// NOTE: If types are added, then they will be added in "midstream".  Only
// bindings that come after the added value is seen will be bound.
//
void Bind_Values_Core(
    RELVAL *head,
    const RELVAL *tail,
    const RELVAL *context,
    REBU64 bind_types,
    REBU64 add_midstream_types,
    REBFLGS flags // see %sys-core.h for BIND_DEEP, etc.
) {
    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    REBCTX *c = VAL_CONTEXT(context);

    // Associate the canon of a word with an index number.  (This association
    // is done by poking the index into the REBSER of the series behind the
    // ANY-WORD!, so it must be cleaned up to not break future bindings.)
    //
  if (not IS_MODULE(context)) {
    REBLEN index = 1;
    const REBKEY *key_tail;
    const REBKEY *key = CTX_KEYS(&key_tail, c);
    const REBVAR *var = CTX_VARS_HEAD(c);
    for (; key != key_tail; key++, var++, index++)
        Add_Binder_Index(&binder, KEY_SYMBOL(key), index);
  }

    Bind_Values_Inner_Loop(
        &binder,
        head,
        tail,
        c,
        bind_types,
        add_midstream_types,
        flags
    );

  if (not IS_MODULE(context)) {  // Reset all the binder indices to zero
    const REBKEY *key_tail;
    const REBKEY *key = CTX_KEYS(&key_tail, c);
    const REBVAR *var = CTX_VARS_HEAD(c);
    for (; key != key_tail; ++key, ++var)
        Remove_Binder_Index(&binder, KEY_SYMBOL(key));
  }

    SHUTDOWN_BINDER(&binder);
}


//
//  Unbind_Values_Core: C
//
// Unbind words in a block, optionally unbinding those which are
// bound to a particular target (if target is NULL, then all
// words will be unbound regardless of their VAL_WORD_CONTEXT).
//
void Unbind_Values_Core(
    RELVAL *head,
    const RELVAL *tail,
    option(REBCTX*) context,
    bool deep
){
    RELVAL *v = head;
    for (; v != tail; ++v) {
        //
        // !!! We inefficiently dequote all values just to make sure we don't
        // damage shared bindings; review more efficient means of doing this.
        //
        enum Reb_Kind heart = CELL_HEART(VAL_UNESCAPED(v));

        if (
            ANY_WORD_KIND(heart)
            and (not context or BINDING(v) == unwrap(context))
        ){
            Unbind_Any_Word(v);
        }
        else if (ANY_ARRAY_KIND(heart) and deep) {
            const RELVAL *sub_tail;
            RELVAL *sub_at = VAL_ARRAY_AT_MUTABLE_HACK(&sub_tail, v);
            Unbind_Values_Core(sub_at, sub_tail, context, true);
        }
    }
}


//
//  Try_Bind_Word: C
//
// Returns 0 if word is not part of the context, otherwise the index of the
// word in the context.
//
REBLEN Try_Bind_Word(const RELVAL *context, REBVAL *word)
{
    const bool strict = true;
    REBLEN n = Find_Symbol_In_Context(
        context,
        VAL_WORD_SYMBOL(word),
        strict
    );
    if (n != 0) {
        INIT_VAL_WORD_BINDING(word, VAL_CONTEXT(context));
        INIT_VAL_WORD_INDEX(word, n);  // ^-- may have been relative
    }
    return n;
}


//
//  Make_Let_Patch: C
//
// Efficient form of "mini-object" allocation that can hold exactly one
// variable.  Unlike a context, it does not have the ability to hold an
// archetypal form of that context...because the only value cell in the
// singular array is taken for the variable content itself.
//
REBARR *Make_Let_Patch(
    const REBSYM *symbol,
    REBSPC *specifier
){
    // We create a virtual binding patch to link into the binding.  The
    // difference with this patch is that its singular value is the value
    // of a new variable.

    REBARR *patch = Alloc_Singular(
        //
        // LINK is the symbol that the virtual binding matches.
        //
        // MISC is a node, but it's used for linking patches to variants
        // with different chains underneath them...and shouldn't keep that
        // alternate version alive.  So no SERIES_FLAG_MISC_NODE_NEEDS_MARK.
        //
        FLAG_FLAVOR(PATCH)
            | PATCH_FLAG_LET
            | NODE_FLAG_MANAGED
            | SERIES_FLAG_LINK_NODE_NEEDS_MARK
            | SERIES_FLAG_INFO_NODE_NEEDS_MARK
    );

    Init_Unset(ARR_SINGLE(patch));  // start variable off as unset

    // The way it is designed, the list of patches terminates in either a
    // nullptr or a context pointer that represents the specifying frame for
    // the chain.  So we can simply point to the existing specifier...whether
    // it is a patch, a frame context, or nullptr.
    //
    assert(not specifier or GET_SERIES_FLAG(specifier, MANAGED));
    mutable_LINK(NextPatch, patch) = specifier;

    // A circularly linked list of variations of this patch with different
    // NextPatch() data is maintained, to assist in avoiding creating
    // unnecessary duplicates.  But since this is an absolutely new instance
    // (from a LET) we won't find any existing chains for this.
    //
    // !!! This feature is on hold for the moment.
    //
    mutable_MISC(Variant, patch) = nullptr;

    // Store the symbol so the patch knows it.
    //
    mutable_INODE(LetSymbol, patch) = symbol;

    return patch;
}


//
//  let: native [
//
//  {Dynamically add a new binding into the stream of evaluation}
//
//      return: "Vanishes if argument is a SET form, else gives the new vars"
//          [<invisible> word! block!]
//      :vars "Variable(s) to create, GROUP!s must evaluate to BLOCK! or WORD!"
//          [<variadic> word! block! set-word! set-block! group! set-group!]
//  ]
//
REBNATIVE(let)
{
    INCLUDE_PARAMS_OF_LET;

    // Though LET shows as a variadic function on its interface, it does not
    // need to use the variadic argument...since it is a native (and hence
    // can access the frame and feed directly).
    //
    UNUSED(ARG(vars));
    REBFRM *f = frame_;

    if (IS_END(f_value))  // e.g. `(let)`
        fail ("LET needs argument");

    // A first level of indirection is permitted since LET allows the syntax
    // `let (word_or_block): <whatever>`.  Handle those groups in such a way
    // that it updates `f_value` itself to reflect the group product.
    //
    // For convenience, double-set is allowed.  e.g.
    //
    //     block: just [x y]:
    //     (block): <whatever>  ; no real reason to prohibit this
    //
    // But be conservative in what the product of these GROUP!s can be, since
    // there are conflicting demands where we want `(thing):` to be equivalent
    // to `[(thing)]:`, while at the same time we don't want to wind up with
    // "mixed decorations" where `('^thing):` would become both SET!-like and
    // SYM!-like.
    //
    REBSPC *f_value_specifier;  // f_value may become specified by this
    if (IS_GROUP(f_value) or IS_SET_GROUP(f_value)) {
        if (Do_Any_Array_At_Throws(D_SPARE, f_value, f_specifier)) {
            Move_Cell(D_OUT, D_SPARE);
            return R_THROWN;
        }

        switch (VAL_TYPE(D_SPARE)) {
          case REB_WORD:
          case REB_BLOCK:
            if (IS_SET_GROUP(f_value))
                Setify(D_SPARE);  // convert `(word):` to be SET-WORD!
            break;

          case REB_SET_WORD:
          case REB_SET_BLOCK:
            if (IS_SET_GROUP(f_value)) {
                // Allow `(set-word):` to ignore the "redundant colon"
            }
            break;

          default:
            fail ("LET GROUP! limited to WORD! and BLOCK!");
        }

        // Move the evaluative product into the feed's "fetched" slot and
        // re-point f_value at it.  (Note that f_value may have been in the
        // fetched slot originally--we may be overwriting the GROUP! that was
        // just evaluated.  But we don't need it anymore.)
        //
        Move_Cell(&f->feed->fetched, D_SPARE);
        f_value = &f->feed->fetched;
        f_value_specifier = SPECIFIED;
    }
    else {
        f_value_specifier = f_specifier;  // not group, so handle as-is
    }

    // !!! Should it be allowed to write `let 'x: <whatever>` and have it
    // act as if you had written `x: <whatever>`, e.g. no LET behavior at
    // all?  This may seem useless, but it could be useful in generated
    // code to "escape out of" a LET in some boilerplate.  And it would be
    // consistent with the behavior of `let ['x]: <whatever>`
    //
    if (IS_QUOTED(f_value))
        fail ("QUOTED! escapes not currently supported at top level of LET");

    // We are going to be adding new "patches" as linked list elements onto
    // the binding that the frame is using.  Since there are a lot of
    // "specifiers" involved with the elements in the let dialect, give this
    // a weird-but-relevant name of "bindings".
    //
    REBSPC *bindings = f_specifier;
    if (bindings and NOT_SERIES_FLAG(bindings, MANAGED))
        SET_SERIES_FLAG(bindings, MANAGED);  // natives don't always manage

    // !!! Right now what is permitted is conservative, due to things like the
    // potential confusion when someone writes:
    //
    //     word: just :b
    //     let [a (word) c]: transcode "<whatever>"
    //
    // They could reasonably think that this would behave as if they had
    // written in source `let [a :b c]: transcode <whatever>`.  If that meant
    // to look up the word B to find out were to actually write, we wouldn't
    // want to create a LET binding for B...but for what B looked up to.
    //
    // Bias it so that if you want something to just "pass through the LET"
    // that you use a quote mark on it, and the LET will ignore it.
    //
    if (IS_WORD(f_value)) {
        const REBSYM *symbol = VAL_WORD_SYMBOL(f_value);
        bindings = Make_Let_Patch(symbol, bindings);
        Init_Word(D_OUT, symbol);  // definitely not invisible
        INIT_VAL_WORD_BINDING(D_OUT, bindings);
    }
    else if (IS_SET_WORD(f_value)) {
        const REBSYM *symbol = VAL_WORD_SYMBOL(f_value);
        bindings = Make_Let_Patch(symbol, bindings);
    }
    else if (IS_BLOCK(f_value) or IS_SET_BLOCK(f_value)) {
        const RELVAL *tail;
        const RELVAL *item = VAL_ARRAY_AT(&tail, f_value);
        REBSPC *item_specifier = Derive_Specifier(f_value_specifier, f_value);

        // Making a LET binding patch for each item we are enumerating has
        // another opportunity for escaping.  Items inside a BLOCK! can be
        // evaluated to get the word to set.  Used with multi-return:
        //
        //     words: [foo position]
        //     let [value /position (second words) 'error]: transcode "abc"
        //
        // Several things to notice:
        //
        // * The evaluation of `(second words)` must be done by the LET in
        //   order to see the word it is creating a binding for.  That should
        //   not run twice, so the LET must splice the evaluated block into
        //   the input feed so TRANSCODE will see the product.  That means
        //   making a new block.
        //
        // * The multi-return dialect is planned to be able to use things like
        //   refinement names to reinforce the name of what is being returned.
        //   This doesn't have any meaning to LET and must be skipped...yet
        //   retained in the product.
        //
        // * It's planned that quoted words be handled as a way to pass through
        //   things with their existing binding, skipping the LET but still
        //   being in the block.  Since LET ascribes meaning to this in a
        //   dialect sense, `'error` should probably become `error` in the
        //   output.  This limits the potential meanings for quoted words in
        //   the multi-return dialect since it is assumed to work with LET.  But
        //   simply dequoting the item permits quoted things to have meaning.
        //
        REBDSP dsp_orig = DSP;

        bool need_copy = false;

        for (; item != tail; ++item) {
            const RELVAL *temp = item;
            REBSPC *temp_specifier = item_specifier;

            // Unquote and ignore anything that is quoted.  This is to assume
            // it's for the multiple return dialect--not LET.
            //
            if (IS_QUOTED(temp)) {
                Derelativize(DS_PUSH(), temp, temp_specifier);
                Unquotify(DS_TOP, 1);
                need_copy = true;
                continue;  // do not make binding
            }

            // If there's a non-quoted GROUP! we evaluate it, as intended
            // for the LET.
            //
            if (IS_GROUP(temp)) {
                if (Do_Any_Array_At_Throws(D_SPARE, temp, item_specifier)) {
                    Move_Cell(D_OUT, D_SPARE);
                    return R_THROWN;
                }
                temp = D_SPARE;
                temp_specifier = SPECIFIED;

                need_copy = true;
            }

            switch (VAL_TYPE(temp)) {
              case REB_ISSUE:
              case REB_BLANK:
                Derelativize(DS_PUSH(), temp, temp_specifier);
                break;

              case REB_WORD:
              case REB_SET_WORD: {
                Derelativize(DS_PUSH(), temp, temp_specifier);
                const REBSYM *symbol = VAL_WORD_SYMBOL(temp);
                bindings = Make_Let_Patch(symbol, bindings);
                break; }

              default:
                fail (Derelativize(D_OUT, temp, temp_specifier));
            }
        }

        // !!! There probably needs to be a protocol where cells that are in
        // the feed as a fully specified cell are assumed to not need to be
        // specified again.  Otherwise, we run into the problem that doing
        // something like `let [x 'x]: <whatever>` would produce a block like
        // `[x x]` and then add a specifier to it that specifies both.  This
        // would mean not only GROUP!s would imply making a new block.
        //
        if (need_copy) {
            Init_Any_Array(
                &f->feed->fetched,
                VAL_TYPE(f_value),
                Pop_Stack_Values_Core(dsp_orig, NODE_FLAG_MANAGED)
            );
            f_value = &f->feed->fetched;
        }
        else
            DS_DROP_TO(dsp_orig);
    }

    // Going forward we want the feed's binding to include the LETs.  Note
    // that this can create the problem of applying the binding twice; this
    // needs systemic review.
    //
    mutable_BINDING(FEED_SINGLE(f->feed)) = bindings;

    // If the expression is a SET-WORD!, e.g. `let x: 1 + 2`, then the LET
    // vanishes and leaves behind the `x: 1 + 2` for the ensuing evaluation.
    //
    if (IS_SET_WORD(f_value) or IS_SET_BLOCK(f_value))
        RETURN_INVISIBLE;

    assert(IS_WORD(f_value) or IS_BLOCK(f_value));
    Derelativize(D_OUT, f_value, f_specifier);
    Fetch_Next_In_Feed(f->feed);  // skip over the word
    return D_OUT;  // return the WORD! or BLOCK!
}


//
//  add-let-binding: native [
//
//  {Experimental function for adding a new variable binding to a frame}
//
//      return: [any-word!]
//      frame [frame!]
//      word [any-word!]
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(add_let_binding) {
    INCLUDE_PARAMS_OF_ADD_LET_BINDING;

    REBFRM *f = CTX_FRAME_MAY_FAIL(VAL_CONTEXT(ARG(frame)));

    if (f_specifier)
        SET_SERIES_FLAG(f_specifier, MANAGED);
    REBSPC *patch = Make_Let_Patch(VAL_WORD_SYMBOL(ARG(word)), f_specifier);

    // !!! Should Make_Let_Patch() return an reset cell?
    //
    Move_Cell(ARR_SINGLE(patch), ARG(value));

    mutable_BINDING(FEED_SINGLE(f->feed)) = patch;

    Move_Cell(D_OUT, ARG(word));
    INIT_VAL_WORD_BINDING(D_OUT, patch);
    INIT_VAL_WORD_INDEX(D_OUT, 1);

    return D_OUT;
}


//
//  add-use-object: native [
//
//  {Experimental function for adding an object's worth of binding to a frame}
//
//      return: <none>
//      frame [frame!]
//      object [object!]
//  ]
//
REBNATIVE(add_use_object) {
    INCLUDE_PARAMS_OF_ADD_USE_OBJECT;

    REBFRM *f = CTX_FRAME_MAY_FAIL(VAL_CONTEXT(ARG(frame)));

    if (f_specifier)
        SET_SERIES_FLAG(f_specifier, MANAGED);

    REBARR *patch = Alloc_Singular(
        FLAG_FLAVOR(PATCH)
            | NODE_FLAG_MANAGED
            | SERIES_FLAG_LINK_NODE_NEEDS_MARK
    );

    Copy_Cell(ARR_SINGLE(patch), ARG(object));

    mutable_LINK(NextPatch, patch) = f_specifier;
    mutable_MISC(Variant, patch) = nullptr;  // defunct feature atm.
    mutable_INODE(VbindUnused, patch) = nullptr;

    mutable_BINDING(FEED_SINGLE(f->feed)) = patch;

    return Init_None(D_OUT);
}


//
//  Clonify_And_Bind_Relative: C
//
// Recursive function for relative function word binding.  The code for
// Clonify() is merged in for efficiency, because it recurses...and we want
// to do the binding in the same pass.
//
// !!! Since the ultimate desire is to factor out common code, try not to
// constant-fold the Clonify implementation here--to make the factoring clear.
//
// !!! Should this return true if any relative bindings were made?
//
static void Clonify_And_Bind_Relative(
    REBVAL *v,  // Note: incoming value is not relative
    REBFLGS flags,
    REBU64 deep_types,
    struct Reb_Binder *binder,
    REBACT *relative,
    REBU64 bind_types
){
    if (C_STACK_OVERFLOWING(&bind_types))
        Fail_Stack_Overflow();

    assert(flags & NODE_FLAG_MANAGED);

    // !!! Could theoretically do what COPY does and generate a new hijackable
    // identity.  There's no obvious use for this; hence not implemented.
    //
    assert(not (deep_types & FLAGIT_KIND(REB_ACTION)));

    // !!! It may be possible to do this faster/better, the impacts on higher
    // quoting levels could be incurring more cost than necessary...but for
    // now err on the side of correctness.  Unescape the value while cloning
    // and then escape it back.
    //
    REBLEN num_quotes = VAL_NUM_QUOTES(v);
    Dequotify(v);

    enum Reb_Kind kind = cast(enum Reb_Kind, KIND3Q_BYTE_UNCHECKED(v));
    assert(kind < REB_MAX);  // we dequoted it

    enum Reb_Kind heart = CELL_HEART(cast(REBCEL(const*), v));

    if (deep_types & FLAGIT_KIND(kind) & TS_SERIES_OBJ) {
        //
        // Objects and series get shallow copied at minimum
        //
        REBSER *series;
        bool would_need_deep;

        if (ANY_CONTEXT_KIND(heart)) {
            INIT_VAL_CONTEXT_VARLIST(
                v,
                CTX_VARLIST(Copy_Context_Shallow_Managed(VAL_CONTEXT(v)))
            );
            series = CTX_VARLIST(VAL_CONTEXT(v));

            would_need_deep = true;
        }
        else if (ANY_ARRAY_KIND(heart)) {
            series = Copy_Array_At_Extra_Shallow(
                VAL_ARRAY(v),
                0, // !!! what if VAL_INDEX() is nonzero?
                VAL_SPECIFIER(v),
                0,
                NODE_FLAG_MANAGED
            );

            INIT_VAL_NODE1(v, series);  // copies args
            INIT_SPECIFIER(v, UNBOUND);  // copied w/specifier--not relative

            // See notes in Clonify()...need to copy immutable paths so that
            // binding pointers can be changed in the "immutable" copy.
            //
            if (ANY_SEQUENCE_KIND(kind))
                Freeze_Array_Shallow(ARR(series));

            would_need_deep = true;
        }
        else if (ANY_SERIES_KIND(heart)) {
            series = Copy_Series_Core(
                VAL_SERIES(v),
                NODE_FLAG_MANAGED
            );
            INIT_VAL_NODE1(v, series);

            would_need_deep = false;
        }
        else {
            would_need_deep = false;
            series = nullptr;
        }

        // If we're going to copy deeply, we go back over the shallow
        // copied series and "clonify" the values in it.
        //
        if (would_need_deep and (deep_types & FLAGIT_KIND(kind))) {
            RELVAL *sub = ARR_HEAD(ARR(series));
            RELVAL *sub_tail = ARR_TAIL(ARR(series));
            for (; sub != sub_tail; ++sub)
                Clonify_And_Bind_Relative(
                    SPECIFIC(sub),
                    flags,
                    deep_types,
                    binder,
                    relative,
                    bind_types
                );
        }
    }
    else {
        // We're not copying the value, so inherit the const bit from the
        // original value's point of view, if applicable.
        //
        if (NOT_CELL_FLAG(v, EXPLICITLY_MUTABLE))
            v->header.bits |= (flags & ARRAY_FLAG_CONST_SHALLOW);
    }

    // !!! Review use of `heart` here, in terms of meaning
    //
    if (FLAGIT_KIND(heart) & bind_types) {
        REBINT n = Get_Binder_Index_Else_0(binder, VAL_WORD_SYMBOL(v));
        if (n != 0) {
            //
            // Word' symbol is in frame.  Relatively bind it.  Note that the
            // action bound to can be "incomplete" (LETs still gathering)
            //
            INIT_VAL_WORD_BINDING(v, relative);
            INIT_VAL_WORD_INDEX(v, n);
        }
    }
    else if (ANY_ARRAY_KIND(heart)) {

        // !!! Technically speaking it is not necessary for an array to
        // be marked relative if it doesn't contain any relative words
        // under it.  However, for uniformity in the near term, it's
        // easiest to debug if there is a clear mark on arrays that are
        // part of a deep copy of a function body either way.
        //
        INIT_SPECIFIER(v, relative);  // "incomplete func" (LETs gathering?)
    }

    Quotify_Core(v, num_quotes);  // Quotify() won't work on RELVAL*
}


//
//  Copy_And_Bind_Relative_Deep_Managed: C
//
// This routine is called by Make_Action in order to take the raw material
// given as a function body, and de-relativize any IS_RELATIVE(value)s that
// happen to be in it already (as any Copy does).  But it also needs to make
// new relative references to ANY-WORD! that are referencing function
// parameters, as well as to relativize the copies of ANY-ARRAY! that contain
// these relative words...so that they refer to the archetypal function
// to which they should be relative.
//
REBARR *Copy_And_Bind_Relative_Deep_Managed(
    const REBVAL *body,
    REBACT *relative,
    bool locals_visible,
    REBU64 bind_types
){
    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    // Setup binding table from the argument word list.  Note that some cases
    // (like an ADAPT) reuse the exemplar from the function they are adapting,
    // and should not have the locals visible from their binding.  Other cases
    // such as the plain binding of the body of a FUNC created the exemplar
    // from scratch, and should see the locals.  Caller has to decide.
    //
  blockscope {
    EVARS e;
    Init_Evars(&e, ACT_ARCHETYPE(relative));
    e.locals_visible = locals_visible;
    while (Did_Advance_Evars(&e))
        Add_Binder_Index(&binder, KEY_SYMBOL(e.key), e.index);
    Shutdown_Evars(&e);
  }

    REBARR *copy;

  blockscope {
    const REBARR *original = VAL_ARRAY(body);
    REBLEN index = VAL_INDEX(body);
    REBSPC *specifier = VAL_SPECIFIER(body);
    REBLEN tail = VAL_LEN_AT(body);
    assert(tail <= ARR_LEN(original));

    if (index > tail)  // !!! should this be asserted?
        index = tail;

    REBFLGS flags = ARRAY_MASK_HAS_FILE_LINE | NODE_FLAG_MANAGED;
    REBU64 deep_types = (TS_SERIES | TS_SEQUENCE) & ~TS_NOT_COPIED;

    REBLEN len = tail - index;

    // Currently we start by making a shallow copy and then adjust it

    copy = Make_Array_For_Copy(len, flags, original);

    const RELVAL *src = ARR_AT(original, index);
    RELVAL *dest = ARR_HEAD(copy);
    REBLEN count = 0;
    for (; count < len; ++count, ++dest, ++src) {
        Clonify_And_Bind_Relative(
            Derelativize(dest, src, specifier),
            flags | NODE_FLAG_MANAGED,
            deep_types,
            &binder,
            relative,
            bind_types
        );
    }

    SET_SERIES_LEN(copy, len);
  }

  blockscope {  // Reset binding table, see notes above regarding locals
    EVARS e;
    Init_Evars(&e, ACT_ARCHETYPE(relative));
    e.locals_visible = locals_visible;
    while (Did_Advance_Evars(&e))
        Remove_Binder_Index(&binder, KEY_SYMBOL(e.key));
    Shutdown_Evars(&e);
  }

    SHUTDOWN_BINDER(&binder);
    return copy;
}


//
//  Rebind_Values_Deep: C
//
// Rebind all words that reference src target to dst target.
// Rebind is always deep.
//
void Rebind_Values_Deep(
    RELVAL *head,
    const RELVAL *tail,
    REBCTX *from,
    REBCTX *to,
    option(struct Reb_Binder*) binder
) {
    RELVAL *v = head;
    for (; v != tail; ++v) {
        if (ANY_ARRAY_OR_SEQUENCE(v)) {
            const RELVAL *sub_tail;
            RELVAL *sub_at = VAL_ARRAY_AT_MUTABLE_HACK(&sub_tail, v);
            Rebind_Values_Deep(sub_at, sub_tail, from, to, binder);
        }
        else if (ANY_WORD(v) and BINDING(v) == from) {
            INIT_VAL_WORD_BINDING(v, to);

            if (binder) {
                INIT_VAL_WORD_INDEX(
                    v,
                    Get_Binder_Index_Else_0(
                        unwrap(binder),
                        VAL_WORD_SYMBOL(v)
                    )
                );
            }
        }
        else if (IS_ACTION(v)) {
            //
            // !!! This is a new take on R3-Alpha's questionable feature of
            // deep copying function bodies and rebinding them when a
            // derived object was made.  Instead, if a function is bound to
            // a "base class" of the object we are making, that function's
            // binding pointer (in the function's value cell) is changed to
            // be this object.
            //
            REBCTX *stored = VAL_ACTION_BINDING(v);
            if (stored == UNBOUND) {
                //
                // Leave NULL bindings alone.  Hence, unlike in R3-Alpha, an
                // ordinary FUNC won't forward its references.  An explicit
                // BIND to an object must be performed, or METHOD should be
                // used to do it implicitly.
            }
            else if (REB_FRAME == CTX_TYPE(stored)) {
                //
                // Leave bindings to frame alone, e.g. RETURN's definitional
                // reference...may be an unnecessary optimization as they
                // wouldn't match any derivation since there are no "derived
                // frames" (would that ever make sense?)
            }
            else {
                if (Is_Overriding_Context(stored, to))
                    INIT_VAL_ACTION_BINDING(v, to);
                else {
                    // Could be bound to a reified frame context, or just
                    // to some other object not related to this derivation.
                }
            }
        }
    }
}


//
//  Virtual_Bind_Deep_To_New_Context: C
//
// Looping constructs which are parameterized by WORD!s to set each time
// through the loop must copy the body in R3-Alpha's model.  For instance:
//
//    for-each [x y] [1 2 3] [print ["this body must be copied for" x y]]
//
// The reason is because the context in which X and Y live does not exist
// prior to the execution of the FOR-EACH.  And if the body were destructively
// rebound, then this could mutate and disrupt bindings of code that was
// intended to be reused.
//
// (Note that R3-Alpha was somewhat inconsistent on the idea of being
// sensitive about non-destructively binding arguments in this way.
// MAKE OBJECT! purposefully mutated bindings in the passed-in block.)
//
// The context is effectively an ordinary object, and outlives the loop:
//
//     x-word: none
//     for-each x [1 2 3] [x-word: 'x, break]
//     get x-word  ; returns 3
//
// Ren-C adds a feature of letting LIT-WORD!s be used to indicate that the
// loop variable should be written into the existing bound variable that the
// LIT-WORD! specified.  If all loop variables are of this form, then no
// copy will be made.
//
// !!! Loops should probably free their objects by default when finished
//
void Virtual_Bind_Deep_To_New_Context(
    REBVAL *body_in_out, // input *and* output parameter
    REBCTX **context_out,
    const REBVAL *spec
){
    REBLEN num_vars = IS_BLOCK(spec) ? VAL_LEN_AT(spec) : 1;
    if (num_vars == 0)
        fail (spec);  // !!! should fail() take unstable?

    const RELVAL *tail;
    const RELVAL *item;

    REBSPC *specifier;
    bool rebinding;
    if (IS_BLOCK(spec)) {  // walk the block for errors BEFORE making binder
        specifier = VAL_SPECIFIER(spec);
        item = VAL_ARRAY_AT(&tail, spec);

        const RELVAL *check = item;

        rebinding = false;
        for (; check != tail; ++check) {
            if (IS_BLANK(check)) {
                // Will be transformed into dummy item, no rebinding needed
            }
            else if (IS_WORD(check))
                rebinding = true;
            else if (not IS_QUOTED_WORD(check)) {
                //
                // Better to fail here, because if we wait until we're in
                // the middle of building the context, the managed portion
                // (keylist) would be incomplete and tripped on by the GC if
                // we didn't do some kind of workaround.
                //
                fail (Error_Bad_Value(check));
            }
        }
    }
    else {
        item = spec;
        tail = spec;
        specifier = SPECIFIED;
        rebinding = IS_WORD(item);
    }

    // Keylists are always managed, but varlist is unmanaged by default (so
    // it can be freed if there is a problem)
    //
    *context_out = Alloc_Context(REB_OBJECT, num_vars);

    REBCTX *c = *context_out; // for convenience...

    // We want to check for duplicates and a Binder can be used for that
    // purpose--but note that a fail() cannot happen while binders are
    // in effect UNLESS the BUF_COLLECT contains information to undo it!
    // There's no BUF_COLLECT here, so don't fail while binder in effect.
    //
    struct Reb_Binder binder;
    if (rebinding)
        INIT_BINDER(&binder);

    const REBSYM *duplicate = nullptr;

    SYMID dummy_sym = SYM_DUMMY1;

    REBLEN index = 1;
    while (index <= num_vars) {
        const REBSYM *symbol;

        if (IS_BLANK(item)) {
            if (dummy_sym == SYM_DUMMY9)
                fail ("Current limitation: only up to 9 BLANK! keys");

            symbol = Canon_Symbol(dummy_sym);
            dummy_sym = cast(SYMID, cast(int, dummy_sym) + 1);

            REBVAL *var = Append_Context(c, nullptr, symbol);
            Init_Blank(var);
            SET_CELL_FLAG(var, BIND_NOTE_REUSE);
            SET_CELL_FLAG(var, PROTECTED);

            goto add_binding_for_check;
        }
        else if (IS_WORD(item)) {
            symbol = VAL_WORD_SYMBOL(item);
            REBVAL *var = Append_Context(c, nullptr, symbol);

            // !!! For loops, nothing should be able to be aware of this
            // synthesized variable until the loop code has initialized it
            // with something.  But this code is shared with USE, so the user
            // can get their hands on the variable.  Can't be trash.
            //
            Init_Unset(var);

            assert(rebinding); // shouldn't get here unless we're rebinding

            if (not Try_Add_Binder_Index(&binder, symbol, index)) {
                //
                // We just remember the first duplicate, but we go ahead
                // and fill in all the keylist slots to make a valid array
                // even though we plan on failing.  Duplicates count as a
                // problem even if they are LIT-WORD! (negative index) as
                // `for-each [x 'x] ...` is paradoxical.
                //
                if (duplicate == nullptr)
                    duplicate = symbol;
            }
        }
        else {
            assert(IS_QUOTED_WORD(item)); // checked previously

            // A LIT-WORD! indicates that we wish to use the original binding.
            // So `for-each 'x [1 2 3] [...]` will actually set that x
            // instead of creating a new one.
            //
            // !!! Enumerations in the code walks through the context varlist,
            // setting the loop variables as they go.  It doesn't walk through
            // the array the user gave us, so if it's a LIT-WORD! the
            // information is lost.  Do a trick where we put the LIT-WORD!
            // itself into the slot, and give it NODE_FLAG_MARKED...then
            // hide it from the context and binding.
            //
            symbol = VAL_WORD_SYMBOL(VAL_UNESCAPED(item));

          blockscope {
            REBVAL *var = Append_Context(c, nullptr, symbol);
            Derelativize(var, item, specifier);
            SET_CELL_FLAG(var, BIND_NOTE_REUSE);
            SET_CELL_FLAG(var, PROTECTED);
          }

          add_binding_for_check:

            // We don't want to stop `for-each ['x 'x] ...` necessarily,
            // because if we're saying we're using the existing binding they
            // could be bound to different things.  But if they're not bound
            // to different things, the last one in the list gets the final
            // assignment.  This would be harder to check against, but at
            // least allowing it doesn't make new objects with duplicate keys.
            // For now, don't bother trying to use a binder or otherwise to
            // stop it.
            //
            // However, `for-each [x 'x] ...` is intrinsically contradictory.
            // So we use negative indices in the binder, which the binding
            // process will ignore.
            //
            if (rebinding) {
                REBINT stored = Get_Binder_Index_Else_0(&binder, symbol);
                if (stored > 0) {
                    if (duplicate == nullptr)
                        duplicate = symbol;
                }
                else if (stored == 0) {
                    Add_Binder_Index(&binder, symbol, -1);
                }
                else {
                    assert(stored == -1);
                }
            }
        }

        ++item;
        ++index;
    }

    // As currently written, the loop constructs which use these contexts
    // will hold pointers into the arrays across arbitrary user code running.
    // If the context were allowed to expand, then this can cause memory
    // corruption:
    //
    // https://github.com/rebol/rebol-issues/issues/2274
    //
    // !!! Because SERIES_FLAG_DONT_RELOCATE is just a synonym for
    // SERIES_FLAG_FIXED_SIZE at this time, it means that there has to be
    // unwritable cells in the extra capacity, to help catch overwrites.  If
    // we wait too late to add the flag, that won't be true...but if we pass
    // it on creation we can't make the context via Append_Context().  Review
    // this mechanic; and for now forego the protection.
    //
    /* SET_SERIES_FLAG(CTX_VARLIST(c), DONT_RELOCATE); */

    // !!! In virtual binding, there would not be a Bind_Values call below;
    // so it wouldn't necessarily be required to manage the augmented
    // information.  For now it's a requirement for any references that
    // might be found...and INIT_BINDING_MAY_MANAGE() won't auto-manage
    // things unless they are stack-based.  Virtual bindings will be, but
    // contexts like this won't.
    //
    Manage_Series(CTX_VARLIST(c));

    if (not rebinding)
        return; // nothing else needed to do

    if (not duplicate) {
        //
        // This is effectively `Bind_Values_Deep(ARR_HEAD(body_out), context)`
        // but we want to reuse the binder we had anyway for detecting the
        // duplicates.
        //
/*        Virtual_Bind_Deep_To_Existing_Context(
            body_in_out,
            c,
            &binder,
            REB_WORD
        ); */

        mutable_LINK(Patches, c) = VAL_SPECIFIER(body_in_out);
        mutable_BINDING(body_in_out) = c;
    }

    // Must remove binder indexes for all words, even if about to fail
    //
  blockscope {
    const REBKEY *key_tail;
    const REBKEY *key = CTX_KEYS(&key_tail, c);
    REBVAL *var = CTX_VARS_HEAD(c); // only needed for debug, optimized out
    for (; key != key_tail; ++key, ++var) {
        REBINT stored = Remove_Binder_Index_Else_0(
            &binder, KEY_SYMBOL(key)
        );
        if (stored == 0)
            assert(duplicate);
        else if (stored > 0)
            assert(NOT_CELL_FLAG(var, BIND_NOTE_REUSE));
        else
            assert(GET_CELL_FLAG(var, BIND_NOTE_REUSE));
    }
  }

    SHUTDOWN_BINDER(&binder);

    if (duplicate) {
        DECLARE_LOCAL (word);
        Init_Word(word, duplicate);
        fail (Error_Dup_Vars_Raw(word));
    }

    // If the user gets ahold of these contexts, we don't want them to be
    // able to expand them...because things like FOR-EACH have historically
    // not been robust to the memory moving.
    //
    SET_SERIES_FLAG(CTX_VARLIST(c), FIXED_SIZE);
}


//
//  Virtual_Bind_Deep_To_Existing_Context: C
//
void Virtual_Bind_Deep_To_Existing_Context(
    REBVAL *any_array,
    REBCTX *context,
    struct Reb_Binder *binder,
    enum Reb_Kind kind
){
    // Most of the time if the context isn't trivially small then it's
    // probably best to go ahead and cache bindings.
    //
    UNUSED(binder);

/*
    // Bind any SET-WORD!s in the supplied code block into the FRAME!, so
    // e.g. APPLY 'APPEND [VALUE: 10]` will set VALUE in exemplar to 10.
    //
    // !!! Today's implementation mutates the bindings on the passed-in block,
    // like R3-Alpha's MAKE OBJECT!.  See Virtual_Bind_Deep_To_New_Context()
    // for potential future directions.
    //
    Bind_Values_Inner_Loop(
        &binder,
        VAL_ARRAY_AT_MUTABLE_HACK(ARG(def)),  // mutates bindings
        exemplar,
        FLAGIT_KIND(REB_SET_WORD),  // types to bind (just set-word!),
        0,  // types to "add midstream" to binding as we go (nothing)
        BIND_DEEP
    );
 */

    Virtual_Bind_Patchify(any_array, context, kind);
}


void Bind_Nonspecifically(RELVAL *head, const RELVAL *tail, REBCTX *context)
{
    RELVAL *v = head;
    for (; v != tail; ++v) {
        REBCEL(const*) cell = VAL_UNESCAPED(v);
        enum Reb_Kind heart = CELL_HEART(cell);

        if (ANY_ARRAY_KIND(heart)) {
            const RELVAL *sub_tail;
            RELVAL *sub_head = VAL_ARRAY_AT_MUTABLE_HACK(&sub_tail, cell);
            Bind_Nonspecifically(sub_head, sub_tail, context);
        }
        else if (ANY_WORD_KIND(heart)) {
            //
            // Give context but no index; this is how we attach to modules.
            //
            mutable_BINDING(v) = context;
            INIT_VAL_WORD_INDEX(v, INDEX_ATTACHED);  // may be quoted
        }
    }
}


//
//  intern*: native [
//      {Overwrite all bindings of a block deeply}
//
//      return: [block!]
//      where [module!]
//      data [block!]
//  ]
//
REBNATIVE(intern_p)
{
    INCLUDE_PARAMS_OF_INTERN_P;

    REBVAL *where = ARG(where);
    REBVAL *data = ARG(data);

    // !!! This behavior is being tried differently; leave it named INTERN*
    // for the moment...
    //
    return rebValue(Lib(MUTABLE), Lib(IN), where, data);

/*
    assert(IS_BLOCK(ARG(data)));

    const RELVAL *tail;
    RELVAL *head = VAL_ARRAY_AT_MUTABLE_HACK(&tail, ARG(data));
    Bind_Nonspecifically(head, tail, VAL_CONTEXT(ARG(where)));

    RETURN (ARG(data)); */
}
