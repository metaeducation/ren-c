//
//  File: %c-hijack.c
//  Summary: "Method for intercepting one function invocation with another"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2021 Ren-C Open Source Contributors
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
//     >> reference: :victim  ; both words point to the same function identity
//
//     >> victim
//     This gets hijacked.
//
//     >> reference
//     This gets hijacked.
//
//     >> hijack :victim (func [] [print "HIJACK!"])
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
//       >> hijack :load (adapt load/ [print "LOADING!"])
//
//       >> load "<for example>"
//       LOADING!
//       LOADING!
//       LOADING!  ; ... infinite loop
//
//   The problem there is that the adaptation performs its printout and then
//   falls through to the original LOAD, which is now the hijacked version
//   that has the adaptation.  Working around this problem means remembering
//   to ADAPT a COPY:
//
//       >> hijack :load (adapt copy :load [print "LOADING!"])
//
//       >> load "<for example>"
//       LOADING!
//       == [<for example>]
//
// * Hijacking is only efficient when the frames of the functions match--e.g.
//   when the "hijacker" is an ADAPT or ENCLOSE of a copy of the "victim".  But
//   if the frames don't line up, there's an attempt to remap the parameters in
//   the frame based on their name.  This should be avoided if possible.
//

#include "sys-core.h"


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
//     foo: func [a /b c] [...]  =>  bar: func [/b d e] [...]
//                    foo/b 1 2  =>  bar/b 1 2
//
void Push_Redo_Action_Level(Atom* out, Level* L1, const Value* run)
{
    Array* normals = Make_Array(Level_Num_Args(L1));  // max, e.g. no refines

    StackIndex base = TOP_INDEX;  // we push refinements as we find them

    EVARS e;  // use EVARS to get parameter reordering right (in theory?)
    Init_Evars(&e, Varlist_Archetype(Varlist_Of_Level_Force_Managed(L1)));

    while (Did_Advance_Evars(&e)) {
        if (Is_Specialized(e.param))  // specialized or local
            continue;

        if (Cell_ParamClass(e.param) == PARAMCLASS_RETURN)
            continue;  // !!! hack, has PARAMETER_FLAG_REFINEMENT, don't stack it

        if (Get_Parameter_Flag(e.param, REFINEMENT)) {
            if (Is_Nulled(e.var))  // don't add to PATH!
                continue;

            Init_Pushed_Refinement(PUSH(), Key_Symbol(e.key));

            if (Is_Parameter_Unconstrained(e.param)) {
                assert(Is_Okay(e.var));  // used but argless refinement
                continue;
            }
        }

        // The arguments were already evaluated to put them in the frame, do
        // not evaluate them again.
        //
        Copy_Meta_Cell(Alloc_Tail_Array(normals), e.var);
    }

    Shutdown_Evars(&e);

    Flags flags = LEVEL_MASK_NONE;
    if (Get_Level_Flag(L1, RAISED_RESULT_OK))
        flags |= LEVEL_FLAG_RAISED_RESULT_OK;  // inherit failure tolerance

    DECLARE_ATOM (block);
    Init_Block(block, normals);
    Level* L2 = Make_Level_At(&Action_Executor, block, flags);
    L2->baseline.stack_base = base;

    Push_Level(out, L2);
    Push_Action(L2, VAL_ACTION(run), Cell_Frame_Coupling(run));
    Begin_Prefix_Action(L2, VAL_FRAME_LABEL(run));
}


//
//  Hijacker_Dispatcher: C
//
// A hijacker takes over another function's identity, replacing it with its
// own implementation.  It leaves the details array intact (in case it is
// being used by some other COPY of the action), but slips its own archetype
// into the [0] slot of that array.
//
// Sometimes the hijacking function has the same underlying function
// as the victim, in which case there's no need to insert a new dispatcher.
// The hijacker just takes over the identity.  But otherwise it cannot, and
// it's not legitimate to reshape the exemplar of the victim (as something like
// an ADAPT or SPECIALIZE or a MAKE FRAME! might depend on the existing
// paramlist shape of the identity.)  Those cases need this "shim" dispatcher.
//
Bounce Hijacker_Dispatcher(Level* level_)
{
    // The PHASE here is the *identity that the hijacker has overtaken*
    // But the actual hijacker is in the archetype.

    Action* hijacker = VAL_ACTION(Phase_Archetype(PHASE));

    // If the hijacked function was called directly -or- by an adaptation or
    // specalization etc. which was made *after* the hijack, the frame should
    // be compatible.  Check by seeing if the keylists are derived.
    //
    KeyList* exemplar_keylist = Keylist_Of_Varlist(ACT_EXEMPLAR(hijacker));
    KeyList* keylist = Keylist_Of_Varlist(cast(VarList*, LEVEL->varlist));
    while (true) {
        if (keylist == exemplar_keylist)
            return ACT_DISPATCHER(hijacker)(LEVEL);
        if (keylist == LINK(Ancestor, keylist))  // terminates with self ref.
            break;
        keylist = LINK(Ancestor, keylist);
    }

    // Otherwise, we assume the frame was built for the function prior to
    // the hijacking...and has to be remapped.
    //
    Push_Redo_Action_Level(OUT, LEVEL, Phase_Archetype(PHASE));
    return DELEGATE_SUBLEVEL(TOP_LEVEL);
}


//
//  hijack: native [
//
//  "Cause all existing references to a frame to invoke another frame"
//
//      return: "The hijacked action value, null if self-hijack (no-op)"
//          [~null~ action?]
//      victim "Frame whose inherited instances are to be affected"
//          [<unrun> frame!]
//      hijacker "The frame to run in its place"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(hijack)
//
// 1. Should the paramlists of the hijacker and victim match, that means any
//    ADAPT or CHAIN or SPECIALIZE of the victim can work equally well if we
//    just use the hijacker's dispatcher directly.  This is a reasonably
//    common case, and especially common when putting a copy of the originally
//    hijacked function back.
//
// 2. A mismatch means there could be someone out there pointing at the
//    victim function function who expects it to have a different frame than
//    it does.  In case that someone needs to run the function with that frame,
//    a proxy "shim" is needed.
//
//    !!! It could be possible to do things here like test to see if frames
//    were compatible in some way that could accelerate the process of building
//    a new frame.  But in general one basically needs a new function call.
//
// 3. We do not return a copy of the original function that can be used to
//    restore the behavior.  Because you can make such a copy yourself if
//    you intend to put the behavior back:
//
//        foo-saved: copy unrun foo/  ; should antiform frame be copyable?
//        hijack foo/ bar/
//        ...
//        hijack foo/ foo-saved
//
//    Making such a copy in this routine would be wasteful if it wasn't used.
//
// 4. !!! What should be done about MISC(victim_paramlist).meta?  Leave it
//    alone?  Add a note about the hijacking?  Also: how should binding and
//    hijacking interact?
{
    INCLUDE_PARAMS_OF_HIJACK;

    Action* victim = VAL_ACTION(ARG(victim));
    Action* hijacker = VAL_ACTION(ARG(hijacker));

    if (victim == hijacker)
        return nullptr;  // permitting no-op hijack has some practical uses

    Phase* victim_identity = ACT_IDENTITY(victim);
    Phase* hijacker_identity = ACT_IDENTITY(hijacker);

    if (Action_Is_Base_Of(victim, hijacker)) {  // no shim needed [1]
        mutable_LINK_DISPATCHER(victim_identity)
            = cast(CFunction*, LINK_DISPATCHER(hijacker_identity));
    }
    else {  // mismatch, so shim required [2]
        mutable_LINK_DISPATCHER(victim_identity)
            = cast(CFunction*, &Hijacker_Dispatcher);
    }

    Clear_Cell_Flag(  // change on purpose
        Phase_Archetype(victim_identity),
        PROTECTED
    );
    Copy_Cell(  // move the archetype into the 0 slot of victim's identity
        Phase_Archetype(victim_identity),
        Phase_Archetype(hijacker_identity)
    );
    Set_Cell_Flag(  // restore invariant
        Phase_Archetype(victim_identity),
        PROTECTED
    );

    return Init_Action(  // don't bother returning copy of original [3]
        OUT,
        victim_identity,
        VAL_FRAME_LABEL(ARG(victim)),  // MISC(victim_paramlist).meta? [4]
        Cell_Frame_Coupling(ARG(hijacker))
    );
}
