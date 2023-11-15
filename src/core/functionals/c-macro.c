//
//  File: %c-macro.c
//  Summary: "ACTION! that splices a block of code into the execution stream"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the GNU Lesser General Public License (LGPL), Version 3.0.
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.en.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// MACRO is an unusual function dispatcher that does surgery directly on the
// feed of instructions being processed.  This makes it easy to build partial
// functions based on expressing them how you would write them:
//
//     >> m: macro [x] [return [append x first]]
//
//     >> m [a b c] [1 2 3]
//     == [a b c 1]  ; e.g. `<<append [a b c] first>> [1 2 3]`
//
// Using macros can be expedient, though as with "macros" in any language
// they don't mesh as well with other language features as formally specified
// functions do.  For instance, you can see above that the macro spec has
// a single parameter, but the invocation gives the effect of having two.
//

#include "sys-core.h"


//
//  Splice_Block_Into_Feed: C
//
void Splice_Block_Into_Feed(Feed(*) feed, const REBVAL *splice) {
    //
    // !!! The mechanics for taking and releasing holds on arrays needs work,
    // but this effectively releases the hold on the code array while the
    // splice is running.  It does so because the holding flag is currently
    // on a feed-by-feed basis.  It should be on a splice-by-splice basis.
    //
    if (Get_Feed_Flag(feed, TOOK_HOLD)) {
        assert(Get_Series_Info(FEED_ARRAY(feed), HOLD));
        Clear_Series_Info(m_cast(Array(*), FEED_ARRAY(feed)), HOLD);
        Clear_Feed_Flag(feed, TOOK_HOLD);
    }

    // Each feed has a static allocation of a Stub-sized entity for managing
    // its "current splice".  This splicing action will pre-empt that, so it
    // is moved into a dynamically allocated splice which is then linked to
    // be used once the splice runs out.
    //
    if (FEED_IS_VARIADIC(feed) or Not_End(feed->p)) {
        Array(*) saved = Alloc_Singular(
            FLAG_FLAVOR(FEED) | SERIES_FLAG_MANAGED  // no tracking
        );
        memcpy(saved, FEED_SINGULAR(feed), sizeof(ArrayT));
        assert(Not_Series_Flag(saved, MANAGED));

        // old feed data resumes after the splice
        mutable_LINK(Splice, &feed->singular) = saved;

        // The feed->p which would have been seen next has to be preserved
        // as the first thing to run when the next splice happens.
        //
        mutable_MISC(Pending, saved) = At_Feed(feed);
    }

    feed->p = VAL_ARRAY_ITEM_AT(splice);
    Copy_Cell(FEED_SINGLE(feed), splice);
    ++VAL_INDEX_UNBOUNDED(FEED_SINGLE(feed));

    mutable_MISC(Pending, &feed->singular) = nullptr;

    // !!! See remarks above about this per-feed hold logic that should be
    // per-splice hold logic.  Pending whole system review of iteration.
    //
    if (Not_Feed_At_End(feed) and Not_Series_Info(FEED_ARRAY(feed), HOLD)) {
        Set_Series_Info(m_cast(Array(*), FEED_ARRAY(feed)), HOLD);
        Set_Feed_Flag(feed, TOOK_HOLD);
    }
}


//
//  Macro_Dispatcher: C
//
Bounce Macro_Dispatcher(Level(*) const L)
{
    USE_LEVEL_SHORTHANDS (L);

    Details(*) details = ACT_DETAILS(PHASE);
    Cell(*) body = Array_At(details, IDX_DETAILS_1);  // code to run
    assert(IS_BLOCK(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    assert(ACT_HAS_RETURN(PHASE));
    assert(KEY_SYM(ACT_KEYS_HEAD(PHASE)) == SYM_RETURN);

    // !!! Using this form of RETURN is based on UNWIND, which means we must
    // catch UNWIND ourselves to process that return.  This is probably not
    // a good idea, and if macro wants a RETURN that it processes it should
    // use a different form of return.  Because under this model, UNWIND
    // can't unwind a macro frame to make it return an arbitrary result.
    //
    REBVAL *cell = Level_Arg(L, 1);
    Init_Activation(
        cell,
        ACT_IDENTITY(VAL_ACTION(Lib(DEFINITIONAL_RETURN))),
        Canon(RETURN),  // relabel (the RETURN in lib is a dummy action)
        CTX(L->varlist)  // bind this return to know where to return from
    );

    // Must catch RETURN ourselves, as letting it bubble up to generic UNWIND
    // handling would return a BLOCK! instead of splice it.
    //
    if (Do_Any_Array_At_Throws(SPARE, body, SPC(L->varlist))) {
        const REBVAL *label = VAL_THROWN_LABEL(L);
        if (
            IS_FRAME(label)  // catch UNWIND here, see [2]
            and VAL_ACTION(label) == VAL_ACTION(Lib(UNWIND))
            and g_ts.unwind_level == L
        ){
            CATCH_THROWN(SPARE, L);  // preserves CELL_FLAG_UNEVALUATED
        }
        else
            return THROWN;  // we didn't catch the throw
    }

    if (Is_Void(SPARE))
        return Init_Nihil(OUT);

    if (not IS_BLOCK(SPARE))
        fail ("MACRO must return VOID or BLOCK! for the moment");

    Splice_Block_Into_Feed(L->feed, stable_SPARE);

    Level(*) sub = Make_Level(
        L->feed,
        LEVEL_MASK_NONE
    );

    Push_Level(OUT, sub);
    return DELEGATE_SUBLEVEL(sub);
}


//
//  macro: native [
//
//  {Makes function that generates code to splice into the execution stream}
//
//      return: [activation?]
//      spec "Help string (opt) followed by arg words (and opt type + string)"
//          [block!]
//      body "Code implementing the macro--use RETURN to yield a result"
//          [block!]
//  ]
//
DECLARE_NATIVE(macro)
{
    INCLUDE_PARAMS_OF_MACRO;

    Phase(*) macro = Make_Interpreted_Action_May_Fail(
        ARG(spec),
        ARG(body),
        MKF_RETURN | MKF_KEYWORDS,
        &Macro_Dispatcher,
        IDX_DETAILS_1 + 1  // details capacity, just body slot (and archetype)
    );

    return Init_Activation(OUT, macro, ANONYMOUS, UNBOUND);
}


//
//  inline: native [
//
//  {Inject an array of content into the execution stream, or single value}
//
//      return: [<opt> <void> any-value!]
//      splice "If quoted single value, if blank no insertion (e.g. invisible)"
//          [blank! block! quoted!]
//  ]
//
DECLARE_NATIVE(inline)
{
    INCLUDE_PARAMS_OF_INLINE;

    REBVAL *splice = ARG(splice);
    if (IS_BLANK(splice)) {
        // do nothing, just return invisibly
    }
    else if (Is_Quoted(splice)) {
        //
        // This could probably be done more efficiently, but for now just
        // turn it into a block.
        //
        Array(*) a = Alloc_Singular(SERIES_FLAGS_NONE);
        Unquotify(Move_Cell(Array_Single(a), splice), 1);
        Init_Block(splice, a);
        Splice_Block_Into_Feed(level_->feed, ARG(splice));
    }
    else {
        assert(IS_BLOCK(splice));
        Splice_Block_Into_Feed(level_->feed, ARG(splice));
    }

    Level(*) sub = Make_Level(
        level_->feed,
        LEVEL_MASK_NONE
    );
    Push_Level(OUT, sub);
    return DELEGATE_SUBLEVEL(sub);
}
