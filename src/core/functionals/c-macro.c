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
void Splice_Block_Into_Feed(REBFED *feed, const REBVAL *splice) {
    //
    // !!! The mechanics for taking and releasing holds on arrays needs work,
    // but this effectively releases the hold on the code array while the
    // splice is running.  It does so because the holding flag is currently
    // on a feed-by-feed basis.  It should be on a splice-by-splice basis.
    //
    if (GET_FEED_FLAG(feed, TOOK_HOLD)) {
        assert(GET_SERIES_INFO(FEED_ARRAY(feed), HOLD));
        CLEAR_SERIES_INFO(m_cast(REBARR*, FEED_ARRAY(feed)), HOLD);
        CLEAR_FEED_FLAG(feed, TOOK_HOLD);
    }

    // Each feed has a static allocation of a REBSER-sized entity for managing
    // its "current splice".  This splicing action will pre-empt that, so it
    // is moved into a dynamically allocated splice which is then linked to
    // be used once the splice runs out.
    //
    if (FEED_IS_VARIADIC(feed) or NOT_END(feed->value)) {
        REBARR *saved = Alloc_Singular(
            FLAG_FLAVOR(FEED) | SERIES_FLAG_MANAGED  // no tracking
        );
        memcpy(saved, FEED_SINGULAR(feed), sizeof(REBARR));
        assert(NOT_SERIES_FLAG(saved, MANAGED));

        // old feed data resumes after the splice
        mutable_LINK(Splice, &feed->singular) = saved;

        // The feed->value which would have been seen next has to be preserved
        // as the first thing to run when the next splice happens.
        //
        mutable_MISC(Pending, saved) = feed->value;
    }

    feed->value = VAL_ARRAY_ITEM_AT(splice);
    Copy_Cell(FEED_SINGLE(feed), splice);
    ++VAL_INDEX_UNBOUNDED(FEED_SINGLE(feed));

    mutable_MISC(Pending, &feed->singular) = nullptr;

    // !!! See remarks above about this per-feed hold logic that should be
    // per-splice hold logic.  Pending whole system review of iteration.
    //
    if (NOT_END(feed->value) and NOT_SERIES_INFO(FEED_ARRAY(feed), HOLD)) {
        SET_SERIES_INFO(m_cast(REBARR*, FEED_ARRAY(feed)), HOLD);
        SET_FEED_FLAG(feed, TOOK_HOLD);
    }
}


//
//  Macro_Dispatcher: C
//
REB_R Macro_Dispatcher(REBFRM *f)
{
    REBFRM *frame_ = f;  // for RETURN macros

    REBVAL *spare = FRM_SPARE(f);  // write to spare, return will be invisible
    bool returned;
    if (Interpreted_Dispatch_Details_1_Throws(&returned, spare, f)) {
        Move_Cell(f->out, spare);
        return_thrown (f->out);
    }
    UNUSED(returned);  // no additional work to bypass

    if (not IS_BLOCK(spare))
        fail ("MACRO must return BLOCK! for the moment");

    Splice_Block_Into_Feed(f->feed, spare);

    return f->out;
}


//
//  macro: native [
//
//  {Makes function that generates code to splice into the execution stream}
//
//      return: [action!]
//      spec "Help string (opt) followed by arg words (and opt type + string)"
//          [block!]
//      body "Code implementing the macro--use RETURN to yield a result"
//          [block!]
//  ]
//
REBNATIVE(macro)
{
    INCLUDE_PARAMS_OF_MACRO;

    REBACT *macro = Make_Interpreted_Action_May_Fail(
        ARG(spec),
        ARG(body),
        MKF_RETURN | MKF_KEYWORDS,
        IDX_DETAILS_1 + 1  // details capacity, just body slot (and archetype)
    );

    INIT_ACT_DISPATCHER(macro, &Macro_Dispatcher);

    if (ACT_HAS_RETURN(macro)) {
        REBPAR *param = ACT_PARAMS_HEAD(macro);
        assert(KEY_SYM(ACT_KEYS_HEAD(macro)) == SYM_RETURN);
        SET_PARAM_FLAG(param, ENDABLE);  // dispatcher is invisible
    }

    return Init_Action(D_OUT, macro, ANONYMOUS, UNBOUND);
}


//
//  inline: native [
//
//  {Inject an array of content into the execution stream, or single value}
//
//      return: <void>
//      splice "If quoted single value, if blank no insertion (e.g. invisible)"
//          [blank! block! quoted!]
//  ]
//
REBNATIVE(inline)
{
    INCLUDE_PARAMS_OF_INLINE;

    REBVAL *splice = ARG(splice);
    if (IS_BLANK(splice))
        return_invisible (D_OUT);  // do nothing, just return invisibly

    if (IS_QUOTED(splice)) {
        //
        // This could probably be done more efficiently, but for now just
        // turn it into a block.
        //
        REBARR *a = Alloc_Singular(SERIES_FLAGS_NONE);
        Unquotify(Move_Cell(ARR_SINGLE(a), splice), 1);
        Init_Block(splice, a);
    }

    assert(IS_BLOCK(splice));
    Splice_Block_Into_Feed(frame_->feed, ARG(splice));

    return_invisible (D_OUT);
}
