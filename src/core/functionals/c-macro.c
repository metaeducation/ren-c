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

enum {
    IDX_MACRO_BODY = IDX_INTERPRETED_BODY,
    IDX_MACRO_MAX
};


//
//  Splice_Block_Into_Feed: C
//
//
// 1. The mechanics for taking and releasing holds on arrays needs work, but
//    this effectively releases the hold on the code array while the splice is
//    running.  It does so because the holding flag is currently on a
//    feed-by-feed basis.  It should be on a splice-by-splice basis.
//
// 2. Each feed has a static allocation of a Stub-sized entity for managing
//    its "current splice".  This splicing action will pre-empt that, so it
//    is moved into a dynamically allocated splice which is then linked to
//    be used once the splice runs out.
//
// 3. The feed->p (retrieved by At_Feed()) which would have been seen next has
//    to be preserved as the first thing to get when the saved splice happens.
//
void Splice_Block_Into_Feed(Feed* feed, const Value* splice) {
    if (Get_Feed_Flag(feed, TOOK_HOLD)) {  // !!! holds need work [1]
        assert(Get_Flex_Info(FEED_ARRAY(feed), HOLD));
        Clear_Flex_Info(FEED_ARRAY(feed), HOLD);
        Clear_Feed_Flag(feed, TOOK_HOLD);
    }

    if (FEED_IS_VARIADIC(feed) or Not_End(feed->p)) {
        Stub* saved = Make_Untracked_Stub(  // save old feed stub [2]
            FLAG_FLAVOR(FEED)
        );
        Mem_Copy(saved, FEED_SINGULAR(feed), sizeof(Stub));
        assert(Not_Node_Managed(saved));

        LINK(Splice, &feed->singular) = saved;  // old feed now after splice
        MISC(Pending, saved) = At_Feed(feed);  // save feed->p [3]
    }

    feed->p = Cell_List_Item_At(splice);
    Copy_Cell(FEED_SINGLE(feed), splice);
    ++VAL_INDEX_UNBOUNDED(FEED_SINGLE(feed));

    MISC(Pending, &feed->singular) = nullptr;

    if (  // take per-feed hold, should be per-splice [1]
        Not_Feed_At_End(feed)
        and Not_Flex_Info(FEED_ARRAY(feed), HOLD)
    ){
        Set_Flex_Info(FEED_ARRAY(feed), HOLD);
        Set_Feed_Flag(feed, TOOK_HOLD);
    }
}


//
//  Macro_Dispatcher: C
//
Bounce Macro_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Phase_Details(PHASE);
    Value* body = Details_At(details, IDX_DETAILS_1);  // code to run
    assert(Is_Block(body) and VAL_INDEX(body) == 0);

    assert(ACT_HAS_RETURN(PHASE));
    assert(KEY_SYM(ACT_KEYS_HEAD(PHASE)) == SYM_RETURN);

    Force_Level_Varlist_Managed(L);

    // !!! Using this form of RETURN is based on UNWIND, which means we must
    // catch UNWIND ourselves to process that return.  This is probably not
    // a good idea, and if macro wants a RETURN that it processes it should
    // use a different form of return.  Because under this model, UNWIND
    // can't unwind a macro frame to make it return an arbitrary result.
    //
    Value* cell = Level_Arg(L, 1);
    Init_Action(
        cell,
        ACT_IDENTITY(VAL_ACTION(LIB(DEFINITIONAL_RETURN))),
        Canon(RETURN),  // relabel (the RETURN in lib is a dummy action)
        cast(VarList*, L->varlist)  // so RETURN knows where to return from
    );

    Copy_Cell(SPARE, body);
    node_LINK(NextVirtual, L->varlist) = Cell_List_Binding(body);
    BINDING(SPARE) = L->varlist;

    // Must catch RETURN ourselves, as letting it bubble up to generic UNWIND
    // handling would return a BLOCK! instead of splice it.
    //
    if (Eval_Any_List_At_Throws(OUT, SPARE, SPECIFIED)) {
        const Value* label = VAL_THROWN_LABEL(L);
        if (
            Is_Frame(label)  // catch UNWIND here [2]
            and VAL_ACTION(label) == VAL_ACTION(LIB(UNWIND))
            and g_ts.unwind_level == L
        ){
            CATCH_THROWN(OUT, L);
        }
        else
            return THROWN;  // we didn't catch the throw
    }

    if (Is_Void(OUT))
        return Init_Nihil(OUT);

    if (not Is_Block(OUT))
        return FAIL("MACRO must return VOID or BLOCK! for the moment");

    Splice_Block_Into_Feed(L->feed, stable_OUT);

    Level* sub = Make_Level(&Stepper_Executor, L->feed, LEVEL_MASK_NONE);
    Erase_Cell(OUT);
    Push_Level_Erase_Out_If_State_0(OUT, sub);
    return DELEGATE_SUBLEVEL(sub);
}


//
//  /macro: native [
//
//  "Makes function that generates code to splice into the execution stream"
//
//      return: [action?]
//      spec "Help string (opt) followed by arg words (and opt type + string)"
//          [block!]
//      body "Code implementing the macro--use RETURN to yield a result"
//          [block!]
//  ]
//
DECLARE_NATIVE(macro)
{
    INCLUDE_PARAMS_OF_MACRO;

    Element* spec = cast(Element*, ARG(spec));
    Element* body = cast(Element*, ARG(body));

    Phase* macro = Make_Interpreted_Action_May_Fail(
        spec,
        body,
        MKF_RETURN,
        &Macro_Dispatcher,
        IDX_MACRO_MAX  // details capacity, just body slot (and archetype)
    );

    return Init_Action(OUT, macro, ANONYMOUS, UNBOUND);
}


//
//  /inline: native [
//
//  "Inject a list of content into the execution stream, or single value"
//
//      return: [any-value?]
//      splice "If quoted single value, if blank no insertion (e.g. invisible)"
//          [blank! block! quoted?]
//  ]
//
DECLARE_NATIVE(inline)
{
    INCLUDE_PARAMS_OF_INLINE;

    Value* splice = ARG(splice);
    if (Is_Blank(splice)) {
        // do nothing, just return invisibly
    }
    else if (Is_Quoted(splice)) {
        //
        // This could probably be done more efficiently, but for now just
        // turn it into a block.
        //
        Source* a = Alloc_Singular(FLEX_MASK_UNMANAGED_SOURCE);
        Unquotify(Move_Cell(Stub_Cell(a), splice), 1);
        Init_Block(splice, a);
        Splice_Block_Into_Feed(level_->feed, ARG(splice));
    }
    else {
        assert(Is_Block(splice));
        Splice_Block_Into_Feed(level_->feed, ARG(splice));
    }

    Level* sub = Make_Level(&Stepper_Executor, level_->feed, LEVEL_MASK_NONE);
    Push_Level_Erase_Out_If_State_0(OUT, sub);
    return DELEGATE_SUBLEVEL(sub);
}
