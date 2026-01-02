//
//  file: %c-macro.c
//  summary: "ACTION! that splices a block of code into the execution stream"
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020-2025 Ren-C Open Source Contributors
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
// INLINER is an unusual function dispatcher that does surgery directly on the
// feed of instructions being processed.  This makes it easy to build partial
// functions based on expressing them how you would write them:
//
//     >> i: inliner [x] [spread compose [append (x) first]]
//
//     >> i [a b c] [1 2 3]
//     == [a b c 1]  ; e.g. `append [a b c] first [1 2 3]`
//
// Using inliners can be expedient, though as with "macros" in any language
// they don't mesh as well with other language features as formally specified
// functions do.  For instance, you can see above that the inliner spec has
// a single parameter, but the invocation gives the effect of having two.
//

#include "sys-core.h"

enum {
    IDX_INLINER_BODY = IDX_INTERPRETED_BODY,
    MAX_IDX_INLINER = IDX_INLINER_BODY
};


//
//  Splice_Block_Into_Feed: C
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
void Splice_Block_Into_Feed(Feed* feed, const Element* splice) {
    assert(not Cell_Binding(splice));  // splices not bound
    Context* feed_binding = Feed_Binding(feed);  // persist binding

    if (Get_Feed_Flag(feed, TOOK_HOLD)) {  // !!! holds need work [1]
        assert(Get_Flex_Info(Feed_Array(feed), HOLD));
        Clear_Flex_Info(Feed_Array(feed), HOLD);
        Clear_Feed_Flag(feed, TOOK_HOLD);
    }

    if (FEED_IS_VARIADIC(feed) or Not_End(feed->p)) {
        require (  // save old feed stub [2]
          Stub* saved = Make_Untracked_Stub(FLAG_FLAVOR(FLAVOR_FEED))
        );
        Mem_Copy(saved, Feed_Singular(feed), sizeof(Stub));
        assert(Not_Base_Managed(saved));

        Tweak_Link_Feedstub_Splice(&feed->singular, saved);  // old feed after
        Tweak_Misc_Feedstub_Pending(saved, At_Feed(feed));  // save feed->p [3]
    }

    feed->p = List_Item_At(splice);
    Copy_Cell(Feed_Data(feed), splice);
    Tweak_Cell_Binding(Feed_Data(feed), feed_binding);

    ++SERIES_INDEX_UNBOUNDED(Feed_Data(feed));

    Tweak_Misc_Feedstub_Pending(&feed->singular, nullptr);

    if (  // take per-feed hold, should be per-splice [1]
        Not_Feed_At_End(feed)
        and Not_Flex_Info(Feed_Array(feed), HOLD)
    ){
        Set_Flex_Info(Feed_Array(feed), HOLD);
        Set_Feed_Flag(feed, TOOK_HOLD);
    }
}


//
//  Splice_Element_Into_Feed: C
//
// This could be done more efficiently, but just turn it into a block.
//
void Splice_Element_Into_Feed(Feed* feed, const Element* element) {
    Source* a = Alloc_Singular(STUB_MASK_UNMANAGED_SOURCE);
    Copy_Cell(Stub_Cell(a), element);

    DECLARE_ELEMENT (temp);
    Init_Block(temp, a);
    Splice_Block_Into_Feed(feed, temp);
}


//
//  Inliner_Dispatcher: C
//
Bounce Inliner_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    enum {
        ST_INLINER_INITIAL_ENTRY = STATE_0,
        ST_INLINER_RUNNING_BODY
    };

    switch (STATE) {
      case ST_INLINER_INITIAL_ENTRY: goto initial_entry;
      case ST_INLINER_RUNNING_BODY: goto body_result_in_out;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Details* details = Ensure_Level_Details(L);
    Element* body = cast(Element*, Details_At(details, IDX_DETAILS_1));
    assert(Is_Block(body) and Series_Index(body) == 0);

    Add_Link_Inherit_Bind(L->varlist, List_Binding(body));
    Force_Level_Varlist_Managed(L);

    Inject_Methodization_If_Any(L);

    Element* body_in_spare = Copy_Cell(SPARE, body);
    Tweak_Cell_Binding(body_in_spare, L->varlist);

    STATE = ST_INLINER_RUNNING_BODY;
    return CONTINUE(OUT, body_in_spare);

} body_result_in_out: { //////////////////////////////////////////////////////

  // 1. Generating a void should do the same thing as an empty splice, and
  //    continue running as a single step...not return in its own step.

    if (Any_Void(OUT))
        goto continue_evaluating;  // MACRO never returns directly [1]

    require (
      Stable* out = Decay_If_Unstable(OUT)
    );
    if (Is_Splice(out)) {
        LIFT_BYTE(out) = NOQUOTE_2;
        KIND_BYTE(out) = TYPE_BLOCK;
        Splice_Block_Into_Feed(L->feed, Known_Element(out));
        goto continue_evaluating;
    }

    if (Is_Antiform(out))
        panic ("MACRO body must return GHOST, ANY-ELEMENT?, or SPLICE!");

    Splice_Element_Into_Feed(L->feed, Known_Element(out));
    goto continue_evaluating;

} continue_evaluating: {  ////////////////////////////////////////////////////

    require (
      Level* sub = Make_Level(&Stepper_Executor, L->feed, LEVEL_MASK_NONE)
    );
    Push_Level_Erase_Out_If_State_0(OUT, sub);

    return DELEGATE_SUBLEVEL(sub);
}}


//
//  Inliner_Details_Querier: C
//
bool Inliner_Details_Querier(
    Sink(Stable) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Inliner_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_INLINER);
    UNUSED(details);

    switch (property) {
      case SYM_RETURN_OF: {
        Api(Stable*) arbitrary = rebStable("return of @", LIB(RANDOMIZE));
        Copy_Cell(out, arbitrary);
        rebRelease(arbitrary);
        return true; }

      default:
        break;
    }

    return false;
}


//
//  inliner: native [
//
//  "Makes function that generates code to splice into the execution stream"
//
//      return: [~(action!)~]
//      spec [block! datatype!]
//      @(body) [<const> block! fence!]
//  ]
//
DECLARE_NATIVE(INLINER)
{
    INCLUDE_PARAMS_OF_INLINER;

    Bounce bounce = opt Irreducible_Bounce(LEVEL, Make_Interpreted_Action(
        LEVEL,
        none,  // no returner; inliners return code, not eval products
        &Inliner_Dispatcher,
        MAX_IDX_INLINER  // details capacity, just body slot (and archetype)
    ));

    if (bounce)
        return bounce;

    return Packify_Action(OUT);
}


//
//  inline: native [
//
//  "Inject a list of content into the execution stream, or single value"
//
//      return: [any-stable?]
//      code "If quoted single value, if void no insertion (e.g. invisible)"
//          [<opt> block! quoted!]
//  ]
//
DECLARE_NATIVE(INLINE)
{
    INCLUDE_PARAMS_OF_INLINE;

    if (not ARG(CODE))
        goto continue_evaluating;

  didnt_opt_out: {

    Element* code = Element_ARG(CODE);

    if (Is_Quoted(code)) {
        Unquote_Cell(code);
        Splice_Element_Into_Feed(level_->feed, code);
    }
    else {
        assert(Is_Block(code));
        Tweak_Cell_Binding(code, UNBOUND);
        Splice_Block_Into_Feed(level_->feed, code);
    }
    goto continue_evaluating;

} continue_evaluating: {  /////////////////////////////////////////////////////

    require (
      Level* sub = Make_Level(&Stepper_Executor, level_->feed, LEVEL_MASK_NONE)
    );
    Push_Level_Erase_Out_If_State_0(OUT, sub);

    return DELEGATE_SUBLEVEL(sub);
}}
