//
//  file: %c-hijack.c
//  summary: "Method for intercepting one function invocation with another"
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2024 Ren-C Open Source Contributors
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
// HIJACK is a tricky-but-useful mechanism for replacing calls to one function
// with another function, based on identity.  This is distinct from overwriting
// a variable, because all references are affected:
//
//     >> victim: func [] [print "This gets hijacked."]
//
//     >> reference: victim/  ; both words point to the same function identity
//
//     >> victim
//     This gets hijacked.
//
//     >> reference
//     This gets hijacked.
//
//     >> hijack victim/ (func [] [print "HIJACK!"])
//
//     >> victim
//     HIJACK!
//
//     >> reference
//     HIJACK!
//
// Though it originated as a somewhat hacky experiment, it was solidified as
// it became increasingly leaned on for important demos.  HIJACK is now
// considered to be safe for mezzanine usages (where appropriate).
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * Specializations, adaptations, enclosures, or other compositional tools
//   hold "references" to functions internally.  These references are also
//   affected by the hijacking, which means it's easy to get infinite loops:
//
//       >> hijack transcode/ (adapt transcode/ [print "TRANSCODING!"])
//
//       >> transcode "<for example>"
//       TRANSCODING!
//       TRANSCODING!
//       TRANSCODING!  ; ... infinite loop
//
//   The problem there is that the adaptation performs its printout and then
//   falls through to the original TRANSCODE, that is now the hijacked version
//   with the adaptation.
//
//   Working around this problem requires saving the old function (which is
//   returned under a new identity from HIJACK):
//
//       >> old-transcode: hijack transcode/ void
//
//       >> hijack transcode/ (adapt old-transcode/ [print "LOADING!"])
//
//       >> transcode "<for example>"
//       TRANSCODING!
//       == [<for example>]
//
// * Hijacking is only efficient when the frames of the functions match--e.g.
//   when the "hijacker" is an ADAPT or ENCLOSE of the old "victim".  But
//   if the frames don't line up, there's an attempt to remap the parameters in
//   the frame based on their name.  This should be avoided if possible.
//

#include "sys-core.h"

enum {
    IDX_HIJACKER_FRAME = 1,  // The action to run in lieu of the original one
    MAX_IDX_HIJACKER = IDX_HIJACKER_FRAME
};


//
//  Push_Redo_Action_Level: C
//
// This code takes a running call frame that has been built for one action
// and then tries to map its parameters to invoke another action.  The new
// action may have different orders and names of parameters.
//
// R3-Alpha had a rather brittle implementation, that had no error checking
// and repetition of logic in Eval_Core.  Because R3-Alpha refinements took
// multiple arguments, it could also fail with "adversarial" prototypes:
//
//     mfoo: func [a :b c] [...]  =>  bar: func [:b d e] [...]
//                     foo:b 1 2  =>  bar:b 1 2
//
void Push_Redo_Action_Level(Value* out, Level* L1, const Stable* run)
{
    Source* normals = Make_Source(Level_Num_Args(L1));  // max, e.g. no refines

    StackIndex base = TOP_INDEX;  // we push refinements as we find them

    ParamList* varlist = Varlist_Of_Level_Force_Managed(L1);
    ParamList* lens = Phase_Paramlist(Frame_Phase(Phase_Archetype(varlist)));

    DECLARE_ELEMENT (frame1);
    Init_Lensed_Frame(
        frame1,
        varlist,
        lens,
        Level_Coupling(L1)
    );

    EVARS e;  // use EVARS to get parameter reordering right (in theory?)
    Init_Evars(&e, frame1);

    while (Try_Advance_Evars(&e)) {
        if (Is_Specialized(e.param))  // specialized or local
            continue;

        if (Get_Parameter_Flag(e.param, REFINEMENT)) {
            if (Is_Nulled(Slot_Hack(e.slot)))  // don't add to PATH!
                continue;

            Init_Pushed_Refinement(PUSH(), Key_Symbol(e.key));

            if (Is_Parameter_Unconstrained(e.param)) {
                assert(Is_Okay(Slot_Hack(e.slot)));  // used but argless refine
                continue;
            }
        }

        // The arguments were already evaluated to put them in the frame, do
        // not evaluate them again.
        //
        require (
          Sink(Element) cell = Alloc_Tail_Array(normals)
        );
        Copy_Lifted_Cell(cell, Slot_Hack(e.slot));
    }

    Shutdown_Evars(&e);

    Flags flags = LEVEL_MASK_NONE;

    DECLARE_ELEMENT (block);
    Init_Block(block, normals);
    require (
      Level* L2 = Make_Level_At(&Action_Executor, block, flags)
    );
    L2->baseline.stack_base = base;

    Push_Level_Erase_Out_If_State_0(out, L2);
    require (
      Push_Action(L2, run, PREFIX_0)
    );
}


//
//  Unimplemented_Dispatcher: C
//
// If you HIJACK a function with void, it puts an unimplemented dispatcher
// that will generate an error if the function is called.
//
Bounce Unimplemented_Dispatcher(Level* const L)
{
    Details* details = Ensure_Level_Details(L);
    assert(Details_Max(details) == 1);  // no details slots needed
    assert(Get_Stub_Flag(details, DYNAMIC));  // but all details are dynamic
    UNUSED(details);

    panic ("FRAME! hasn't been associated with code, or HIJACK'd VOID");
}


//
//  Unimplemented_Details_Querier: C
//
bool Unimplemented_Details_Querier(
    Sink(Stable) out,
    Details* details,
    SymId property
){
    UNUSED(out);
    UNUSED(details);
    UNUSED(property);
    return false;
}


//
//  Hijacker_Dispatcher: C
//
// A hijacker takes over another function's identity, replacing it with its
// own implementation.
//
// Sometimes the hijacking function has a compatible underlying function
// to the victim, in which case there's no need to build a new frame.
//
Bounce Hijacker_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Ensure_Level_Details(L);

    Stable* hijacker_frame = Details_At(details, IDX_HIJACKER_FRAME);

    Phase* hijacker = Frame_Phase(hijacker_frame);
    Option(VarList*) hijacker_coupling = Frame_Coupling(hijacker_frame);

    // If the hijacked function was called directly -or- by an adaptation or
    // specalization etc. which was made *after* the hijack, the frame should
    // be compatible.  Check by seeing if the keylists are derived.
    //
    KeyList* hijacker_keylist = Phase_Keylist(hijacker);
    KeyList* keylist = Bonus_Keylist(Level_Varlist(L));
    while (true) {
        if (keylist == hijacker_keylist) {
            Tweak_Level_Phase(L, hijacker);
            Tweak_Level_Coupling(L, hijacker_coupling);
            return BOUNCE_REDO_UNCHECKED;
        }
        if (keylist == Link_Keylist_Ancestor(keylist))  // terminates with self
            break;
        keylist = Link_Keylist_Ancestor(keylist);
    }

    // Otherwise, we assume the frame was built for the function prior to
    // the hijacking...and has to be remapped.
    //
    Push_Redo_Action_Level(OUT, L, hijacker_frame);
    return DELEGATE_SUBLEVEL(TOP_LEVEL);
}


//
//  Hijacker_Details_Querier: C
//
// All questions are forwarded to the hijacker.
//
// !!! If asked for SYM_BODY_OF, should the body come back with some added
// content, like (comment "this is a hijacking!")
//
bool Hijacker_Details_Querier(
    Sink(Stable) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Hijacker_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_HIJACKER);

    Stable* hijacker = Details_At(details, IDX_HIJACKER_FRAME);

    Details* hijacker_details = Phase_Details(Frame_Phase(hijacker));
    DetailsQuerier* querier = Details_Querier(hijacker_details);
    return (*querier)(out, hijacker_details, property);
}


//
//  unimplemented: native [
//
//  "Panic function returned on HIJACK-ing a function that was void-HIJACKED"
//
//      return: []
//  ]
//
DECLARE_NATIVE(UNIMPLEMENTED)
{
    INCLUDE_PARAMS_OF_UNIMPLEMENTED;

    panic (
        "Invoked function returned from HIJACK after hijacking a void-HIJACK"
    );
}


//
//  hijack: native [
//
//  "Make victim references run another frame, return new identity for victim"
//
//      return: [~[action!]~ frame!]
//      victim "Frame whose inherited instances are to be affected"
//          [action! frame!]
//      hijacker "The frame to run in its place (void to leave TBD)"
//          [<opt> <unrun> frame!]
//  ]
//
DECLARE_NATIVE(HIJACK)
//
// 1. It may seem useful to change the interface to that of the hijacker,
//    so that any added refinements would be exposed.  However, that would
//    create a variance in terms of specializations created before the
//    hijack and those after.  It seems better to avoid the "sometimes it
//    will work, and sometimes it won't" property and keep the interface
//    consistent.  (Perhaps it could be an option to :EXPAND the interface?)
//
// 2. Miserly initial attempts at HIJACK tried to get away with a single
//    element Details array, so it could fit in a Stub.  But when you
//    consider that you're trying to maintain the old interface, and fit in
//    a whole FRAME! Cell's worth of information for the hijacker, it was
//    not working--and the "Archetype" cell was no longer representing an
//    instance of the action.  A 2-cell array works and is cleaner.
//
// 3. It's not totally understood what ADJUNCT is or is not for, so this just
//    assigns a shared copy.
{
    INCLUDE_PARAMS_OF_HIJACK;

    Phase* victim = Frame_Phase(ARG(VICTIM));

    Option(const Stable*) opt_hijacker = ARG(HIJACKER);

    bool victim_unimplemented =
        Is_Stub_Details(victim) and
        Details_Dispatcher(cast(Details*, victim)) == &Unimplemented_Dispatcher;

    if (ARG(HIJACKER)) {
        Phase* hijacker = Frame_Phase(unwrap ARG(HIJACKER));
        if (victim == hijacker)
            panic ("Cannot HIJACK function with itself");  // right?
    }

    Option(VarList*) adjunct = Misc_Phase_Adjunct(victim);

    Details* proxy = Make_Dispatch_Details(
        BASE_FLAG_MANAGED,
        ARG(VICTIM),  // not changing the interface [1]
        opt_hijacker
            ? &Hijacker_Dispatcher
            : &Unimplemented_Dispatcher,
        opt_hijacker
            ? MAX_IDX_HIJACKER  // tried just archetype, it was messed up [2]
            : 0  // no data used (stub is still dynamic)
    );

    if (opt_hijacker)
        Copy_Cell(Details_At(proxy, IDX_HIJACKER_FRAME), unwrap opt_hijacker);

    Tweak_Misc_Phase_Adjunct(proxy, adjunct);  // shared reference [3]

    Swap_Stub_Content(victim, proxy);  // after swap, victim is hijacker

    Element* victim_archetype = Phase_Archetype(victim);  // v-- inf. recurse!
    assert(CELL_FRAME_PAYLOAD_1_PHASE(victim_archetype) == victim);
    CELL_FRAME_PAYLOAD_1_PHASE(victim_archetype) = proxy;  // adjust for swap

    Stable* out;

    if (victim_unimplemented) {
        assert(Get_Cell_Flag(LIB(UNIMPLEMENTED), PROTECTED));
        out = Copy_Plain_Cell(OUT, LIB(UNIMPLEMENTED));
    }
    else {
        out = Init_Frame(
            OUT,
            proxy,  // after Swap_Stub_Content(), new identity for victim
            Frame_Label(ARG(VICTIM)),
            Frame_Coupling(ARG(VICTIM))
        );
    }

    if (Is_Frame(ARG(VICTIM)))
        return OUT;

    Actionify(out);
    return Packify_Action(OUT);
}
