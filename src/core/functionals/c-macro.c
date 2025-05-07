//
//  file: %c-macro.c
//  summary: "ACTION! that splices a block of code into the execution stream"
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
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
    MAX_IDX_MACRO = IDX_MACRO_BODY
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
        assert(Get_Flex_Info(Feed_Array(feed), HOLD));
        Clear_Flex_Info(Feed_Array(feed), HOLD);
        Clear_Feed_Flag(feed, TOOK_HOLD);
    }

    if (FEED_IS_VARIADIC(feed) or Not_End(feed->p)) {
        Stub* saved = Make_Untracked_Stub(  // save old feed stub [2]
            FLAG_FLAVOR(FEED)
        );
        Mem_Copy(saved, Feed_Singular(feed), sizeof(Stub));
        assert(Not_Node_Managed(saved));

        Tweak_Link_Feedstub_Splice(&feed->singular, saved);  // old feed after
        Tweak_Misc_Feedstub_Pending(saved, At_Feed(feed));  // save feed->p [3]
    }

    feed->p = Cell_List_Item_At(splice);
    Copy_Cell(Feed_Data(feed), splice);
    ++VAL_INDEX_UNBOUNDED(Feed_Data(feed));

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
//  Macro_Dispatcher: C
//
Bounce Macro_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    enum {
        ST_MACRO_INITIAL_ENTRY = STATE_0,
        ST_MACRO_REEVALUATING
    };

    switch (STATE) {
      case ST_MACRO_INITIAL_ENTRY:
        goto initial_entry;

      case ST_MACRO_REEVALUATING:  // stepper uses meta protocol
        return Meta_Unquotify_Undecayed(OUT);

      default:
        assert(false);
    }

  initial_entry: { ///////////////////////////////////////////////////////////

    Details* details = Ensure_Level_Details(L);
    Element* body = cast(Element*, Details_At(details, IDX_DETAILS_1));
    assert(Is_Block(body) and VAL_INDEX(body) == 0);

    assert(Key_Id(Phase_Keys_Head(details)) == SYM_RETURN);

    Add_Link_Inherit_Bind(L->varlist, Cell_List_Binding(body));
    Force_Level_Varlist_Managed(L);

    // !!! Using this form of RETURN is based on UNWIND, which means we must
    // catch UNWIND ourselves to process that return.  This is probably not
    // a good idea, and if macro wants a RETURN that it processes it should
    // use a different form of return.  Because under this model, UNWIND
    // can't unwind a macro frame to make it return an arbitrary result.
    //
    Inject_Definitional_Returner(L, LIB(DEFINITIONAL_RETURN), SYM_RETURN);

    Element* body_in_spare = Copy_Cell(SPARE, body);
    Tweak_Cell_Binding(body_in_spare, L->varlist);

    // Must catch RETURN ourselves, as letting it bubble up to generic UNWIND
    // handling would return a BLOCK! instead of splice it.
    //
    if (Eval_Any_List_At_Throws(OUT, body_in_spare, SPECIFIED)) {
        const Value* label = VAL_THROWN_LABEL(L);
        if (
            Is_Frame(label)  // catch UNWIND here [2]
            and Cell_Frame_Phase(label) == Cell_Frame_Phase(LIB(UNWIND))
            and g_ts.unwind_level == L
        ){
            CATCH_THROWN(OUT, L);
        }
        else
            return THROWN;  // we didn't catch the throw
    }

    if (Is_Void(OUT))
        return OUT;

    if (not Is_Block(OUT))
        return FAIL("MACRO must return VOID or BLOCK! for the moment");

    Splice_Block_Into_Feed(L->feed, stable_OUT);

    Level* sub = Make_Level(&Meta_Stepper_Executor, L->feed, LEVEL_MASK_NONE);
    Erase_Cell(OUT);
    Push_Level_Erase_Out_If_State_0(OUT, sub);

    STATE = ST_MACRO_REEVALUATING;
    return CONTINUE_SUBLEVEL(sub);
}}


//
//  Macro_Details_Querier: C
//
bool Macro_Details_Querier(
    Sink(Value) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Macro_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_MACRO);

    switch (property) {
      case SYM_RETURN_OF: {
        Extract_Paramlist_Returner(out, Phase_Paramlist(details), SYM_RETURN);
        return true; }

      default:
        break;
    }

    return false;
}


//
//  macro: native [
//
//  "Makes function that generates code to splice into the execution stream"
//
//      return: [action!]
//      spec "Help string (opt) followed by arg words (and opt type + string)"
//          [block!]
//      body "Code implementing the macro--use RETURN to yield a result"
//          [block!]
//  ]
//
DECLARE_NATIVE(MACRO)
{
    INCLUDE_PARAMS_OF_MACRO;

    Element* spec = Element_ARG(SPEC);
    Element* body = Element_ARG(BODY);

    Details* details;
    Option(Error*) e = Trap_Make_Interpreted_Action(
        &details,
        spec,
        body,
        SYM_RETURN,
        &Macro_Dispatcher,
        MAX_IDX_MACRO  // details capacity, just body slot (and archetype)
    );
    if (e)
        return FAIL(unwrap e);

    return Init_Action(OUT, details, ANONYMOUS, UNBOUND);
}


//
//  inline: native [
//
//  "Inject a list of content into the execution stream, or single value"
//
//      return: [any-value?]
//      ^code "If quoted single value, if void no insertion (e.g. invisible)"
//          [~[]~ block! quoted!]
//  ]
//
DECLARE_NATIVE(INLINE)
{
    INCLUDE_PARAMS_OF_INLINE;

    enum {
        ST_INLINE_INITIAL_ENTRY = STATE_0,
        ST_INLINE_REEVALUATING
    };

    switch (STATE) {
      case ST_INLINE_INITIAL_ENTRY:
        goto initial_entry;

      case ST_INLINE_REEVALUATING:  // stepper uses meta protocol
        return Meta_Unquotify_Undecayed(OUT);

      default:
        assert(false);
    }

  initial_entry: { ///////////////////////////////////////////////////////////

    Option(const Element*) opt_code = Optional_Element_ARG(CODE);
    if (not opt_code)
        return VOID;  // do nothing, just return invisibly
    const Element* code = unwrap opt_code;

    if (Is_Quoted(code)) {
        //
        // This could probably be done more efficiently, but for now just
        // turn it into a block.
        //
        Source* a = Alloc_Singular(FLEX_MASK_UNMANAGED_SOURCE);
        Unquotify(Copy_Cell(Stub_Cell(a), code));
        Init_Block(code, a);
        Splice_Block_Into_Feed(level_->feed, ARG(CODE));
    }
    else {
        assert(Is_Block(code));
        Splice_Block_Into_Feed(level_->feed, ARG(CODE));
    }

    Level* sub = Make_Level(&Meta_Stepper_Executor, level_->feed, LEVEL_MASK_NONE);
    Push_Level_Erase_Out_If_State_0(OUT, sub);

    STATE = ST_INLINE_REEVALUATING;
    return CONTINUE_SUBLEVEL(sub);
}}
