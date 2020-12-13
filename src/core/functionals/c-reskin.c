//
//  File: %c-reskin.c
//  Summary: "Tools for changing the interface or types of function arguments"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2019-2020 Ren-C Open Source Contributors
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
// This is a proof of concept for changing the parameter conventions of
// functions--either the types they accept, or the parameter class and its
// quotedness.  The dialect is very preliminary and definitely needs more
// design work, but it covers the basic operations.
//
// You can #change a parameter's category, for instance to make a variant of
// APPEND which appended its argument literally:
//
//     >> append-q: reskinned [#change :value] :append
//
//     >> append-q [a b c] d
//     == [a b c d]
//
// You can expand types that a function accepts or returns with #add:
//
//     >> foo: func [x [integer!]] [x]
//     >> skin: reskinned [x #add [text!]] (adapt :foo [x: to integer! x])
//
//     >> skin "10"
//     == 10
//
//     >> skin 10
//     == 10
//
// Similarly you can remove them with #remove.  If you don't include #add or
// #remove it is assumed you want to overwrite with a type block completely.
//
// The type block may be omitted if you are using #change to change the
// parameter convention.
//
// Any reskinning that expands argument types (or restricts return types)
// requires the injection of a new dispatcher.  Because this dispatcher must
// have a way to guarantee it can't leak unexpected type bits into natives
// (which would cause crashes), the only functions that may be reskinned in an
// expansive way are those that do a parameter check after usermode code
// runs...so EXPAND and ADAPT.
//
// Narrowing parameter cases, or broadening return cases, do not require new
// checks on top of what the target action already did.
//
// !!! This code is very preliminary and requires review, but demonstrates the
// basic premise of how such a facility would work.
//

#include "sys-core.h"

enum {
    IDX_SKINNER_SKINNED = 1,  // Underlying function that was reskinned
    IDX_SKINNER_MAX
};


//
//  Skinner_Dispatcher: C
//
// Reskinned functions may expand what types the original function took, in
// which case the typechecking the skinned function did may not be enough for
// any parameters that appear to be ARG_MARKED_CHECKED in the frame...they
// were checked against the expanded criteria, not that of the original
// function.  So it has to clear the ARG_MARKED_CHECKED off any of those
// parameters it finds...so if they wind up left in the frame the evaluator
// still knows it has to recheck them.
//
REB_R Skinner_Dispatcher(REBFRM *f)
{
    REBACT *phase = FRM_PHASE(f);
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_SKINNER_MAX);

    REBVAL *skinned = DETAILS_AT(details, IDX_SKINNER_SKINNED);

    REBVAL *param = ACT_PARAMS_HEAD(phase);
    REBVAL *arg = FRM_ARGS_HEAD(f);
    for (; NOT_END(param); ++param, ++arg) {
        if (Is_Param_Skin_Expanded(param))  // !!! always says true (for now)
            CLEAR_CELL_FLAG(arg, ARG_MARKED_CHECKED);
    }

    // We captured the binding for the skin when the action was made; if the
    // user rebound the action, then don't overwrite with the one in the
    // initial skin--assume they meant to change it.

    // If the return type has been altered, then we need to check the value
    // against the returned type.

    param = ACT_PARAMS_HEAD(phase);
    assert(VAL_PARAM_SYM(param) == SYM_RETURN);

    if (not Is_Param_Skin_Expanded(param)) {  // don't need to retain control
        //
        // If we frame checked now, we'd fail, because we just put the new
        // phase into place with more restricted types.  Let the *next* check
        // kick in, and it will now react to the cleared ARG_MARKED_CHECKED
        // flags.
        //
        INIT_FRM_PHASE(f, VAL_ACTION(skinned));
        return R_REDO_UNCHECKED;
    }

    // We move the built `f` contents into a REBFRM* underneath this one.
    //
    Init_Void(FRM_SPARE(f), SYM_UNSET);
    REBFRM *sub = Push_Downshifted_Frame(FRM_SPARE(f), f);

    INIT_FRM_PHASE(sub, VAL_ACTION(skinned));
    sub->original = VAL_ACTION(skinned);
    sub->label = VAL_ACTION_LABEL(skinned);
  #if !defined(NDEBUG)
    sub->label_utf8 = sub->label ? STR_UTF8(sub->label) : "(anonymous)";
  #endif

    if (Process_Action_Throws(sub)) {
        Abort_Frame(sub);
        return R_THROWN;
    }

    Drop_Frame(sub);

    // Typeset bits for locals in frames are usually ignored, but the RETURN:
    // local uses them for the return types of a function.
    //
    if (not Typecheck_Including_Constraints(param, FRM_SPARE(f)))
        fail (Error_Bad_Return_Type(f, VAL_TYPE(FRM_SPARE(f))));

    Move_Value(f->out, FRM_SPARE(f));
    return f->out;
}


//
//  reskinned: native [
//
//  {Returns alias of an ACTION! with modified typing for the given parameter}
//
//      return: "A new action value with the modified parameter conventions"
//          [action!]
//      skin "Mutation spec, e.g. [param1 @add [integer!] 'param2 [tag!]]"
//          [block!]
//      action [action!]
//  ]
//
REBNATIVE(reskinned)
//
// This avoids having to create a usermode function stub for something where
// the only difference is a parameter convention (e.g. an identical function
// that quotes its third argument doesn't actually need a new body).
//
// Care should be taken not to allow the expansion of parameter types accepted
// to allow passing unexpected types to a native, because it could crash.  At
// least for natives, accepted types should only be able to be narrowed.
//
// Keeps the parameter types and help notes in sync, also.
{
    INCLUDE_PARAMS_OF_RESKINNED;

    REBACT *original = VAL_ACTION(ARG(action));

    // We make a copy of the ACTION's paramlist vs. trying to fiddle the
    // action in place.  One reason to do this is that there'd have to be code
    // written to account for the caching done by Make_Action() based on the
    // parameters and their conventions (e.g. PARAMLIST_QUOTES_FIRST),
    // and we don't want to try and update all that here and get it wrong.
    //
    // Another good reason is that if something messes up halfway through
    // the transformation process, the partially built new action gets thrown
    // out.  It would not be atomic if we were fiddling bits directly in
    // something the user already has pointers to.
    //
    // Another reason is to give the skin its own dispatcher, so it can take
    // responsibility for any performance hit incurred by extra type checking
    // that has to be done due to its meddling.  Typically if you ADAPT a
    // function and the frame is fulfilled, with ARG_MARKED_CHECKED on an
    // argument, it's known that there's no point in checking it again if
    // the arg doesn't get freshly overwritten.  Reskinning changes that.
    //
    // !!! Note: Typechecking today is nearly as cheap as the check to avoid
    // it, but the attempt to avoid typechecking is based on a future belief
    // of a system in which the checks are more expensive...which it will be
    // if it has to search hierarchies or lists of quoted forms/etc.
    //
    REBARR *paramlist = Copy_Array_Shallow_Flags(
        ACT_PARAMLIST(original),
        SPECIFIED,  // no relative values in parameter lists
        SERIES_MASK_PARAMLIST
    );

    // Indicate "safe" relationship for frames; e.g. that a frame built for
    // the reskinned function is safe to use with the original function.
    //
    LINK_ANCESTOR_NODE(paramlist) = NOD(ACT_PARAMLIST(original));

    bool need_skin_phase = false;  // only needed if types were broadened

    unstable RELVAL *param = ARR_AT(paramlist, 1);  // 0 is ACT_ARCHETYPE
    unstable const RELVAL *item = VAL_ARRAY_AT(ARG(skin));
    Reb_Param_Class pclass;
    while (NOT_END(item)) {
        bool change;
        if (
            KIND3Q_BYTE(item) != REB_SYM_WORD
            or VAL_WORD_SYM(item) != SYM_CHANGE
        ){
            change = false;
        }
        else {
            change = true;
            ++item;
        }

        if (IS_WORD(item))
            pclass = REB_P_NORMAL;
        else if (IS_SET_WORD(item))
            pclass = REB_P_LOCAL;
        else if (IS_GET_WORD(item))
            pclass = REB_P_SOFT_QUOTE;
        else if (IS_SYM_WORD(item))
            pclass = REB_P_MODAL;
        else if (
            IS_QUOTED(item)
            and VAL_NUM_QUOTES(item) == 1
            and CELL_KIND(VAL_UNESCAPED(item)) == REB_WORD
        ){
            pclass = REB_P_HARD_QUOTE;
        }
        else
            fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(ARG(skin))));

        const REBSTR *canon = VAL_WORD_CANON(VAL_UNESCAPED(item));

        // We assume user gives us parameters in order, but if they don't we
        // cycle around to the beginning again.  So it's most efficient if
        // in order, but still works if not.

        bool wrapped_around = false;
        while (true) {
            if (IS_END(param)) {
                if (wrapped_around) {
                    DECLARE_LOCAL (word);
                    Init_Word(word, canon);
                    fail (word);
                }

                param = ARR_AT(paramlist, 1);
                wrapped_around = true;
            }

            if (VAL_PARAM_CANON(param) == canon)
                break;
            ++param;
        }

        // Got a match and a potential new parameter class.  Don't let the
        // class be changed on accident just because they forgot to use the
        // right marking, require an instruction.  (Better names needed, these
        // were just already in %words.r)

        if (pclass != KIND3Q_BYTE(param)) {
            assert(HEART_BYTE(param) == REB_TYPESET);
            if (change)
                mutable_KIND3Q_BYTE(param) = pclass;
            else if (pclass != REB_P_NORMAL)  // assume plain word = no change
                fail ("If parameter convention is reskinned, use #change");
        }

        ++item;

        // The next thing is either a BLOCK! (in which case we take its type
        // bits verbatim), or @add or @remove, so you can tweak w.r.t. just
        // some bits.

        REBSYM sym = SYM_0;
        if (REB_SYM_WORD == KIND3Q_BYTE(item)) {
            sym = VAL_WORD_SYM(item);
            if (sym != SYM_REMOVE and sym != SYM_ADD)
                fail ("RESKIN only supports @add and @remove instructions");
            ++item;
        }

        if (REB_BLOCK != KIND3Q_BYTE(item)) {
            if (change)  // [@change 'arg] is okay w/no block
                continue;
            fail ("Expected BLOCK! after instruction");
        }

        REBSPC *specifier = VAL_SPECIFIER(item);
        bool hidden = Is_Param_Hidden(param, param);  // special = param

        switch (sym) {
          case SYM_0:  // completely override type bits
            VAL_TYPESET_LOW_BITS(param) = 0;
            VAL_TYPESET_HIGH_BITS(param) = 0;
            Add_Typeset_Bits_Core(param, VAL_ARRAY_AT(item), specifier);
            Set_Param_Skin_Expanded(param);
            need_skin_phase = true;  // !!! Worth it to check for expansion?
            break;

          case SYM_ADD:  // leave existing bits, add new ones
            Add_Typeset_Bits_Core(param, VAL_ARRAY_AT(item), specifier);
            Set_Param_Skin_Expanded(param);
            need_skin_phase = true;
            break;

          case SYM_REMOVE: {
            DECLARE_LOCAL (temp); // make temporary typeset, remove its bits
            Init_Typeset(temp, 0);
            Add_Typeset_Bits_Core(temp, VAL_ARRAY_AT(item), specifier);

            VAL_TYPESET_LOW_BITS(param) &= ~VAL_TYPESET_LOW_BITS(temp);
            VAL_TYPESET_HIGH_BITS(param) &= ~VAL_TYPESET_HIGH_BITS(temp);

            // ENCLOSE doesn't type check the return result by default.  So
            // if you constrain the return types, there will have to be a
            // phase to throw a check into the stack.  Otherwise, constraining
            // types is no big deal...any type that passed the narrower check
            // will pass the broader one.
            //
            if (VAL_PARAM_SYM(param) == SYM_RETURN)
                need_skin_phase = true;
            break; }

          default:
            assert(false);
        }

        if (hidden)
            Hide_Param(param);

        ++item;
    }

    // The most sensible case for a type-expanding reskin is if there is some
    // amount of injected usermode code to narrow the type back to something
    // the original function can deal with.  It might be argued that usermode
    // code would have worked on more types than it annotated, and you may
    // know that and be willing to risk an error if you're wrong.  But with
    // a native--if you give it types it doesn't expect--it can crash.
    //
    // Hence we abide by the type contract, and need a phase to check that
    // we are honoring it.  The only way to guarantee we get that phase is if
    // we're using something that already does the checks...e.g. an Adapter
    // or an Encloser.
    //
    // (Type-narrowing and quoting convention changing things are fine, there
    // is no risk posed to the underlying action call.)
    //
    if (ACT_DISPATCHER(original) == &Skinner_Dispatcher)
        need_skin_phase = false;  // already taken care of, reuse it
    else if (
        need_skin_phase and (
            ACT_DISPATCHER(original) != &Adapter_Dispatcher
            and ACT_DISPATCHER(original) != &Encloser_Dispatcher
        )
    ){
        fail ("Type-expanding RESKIN only works on ADAPT/ENCLOSE actions");
    }

    if (ACT_HAS_RETURN(original))
        SER(paramlist)->header.bits |= PARAMLIST_FLAG_HAS_RETURN;

    // !!! This does not make a unique copy of the meta information context.
    // Hence updates to the title/parameter-descriptions/etc. of the tightened
    // function will affect the original, and vice-versa.
    //
    REBCTX *meta = ACT_META(original);

    Manage_Array(paramlist);

    // If we only *narrowed* the type conventions, then we don't need to put
    // in a new dispatcher.  But if we *expanded* them, the type checking
    // done by the skinned version for ARG_MARKED_CHECKED may not be enough.
    //
    REBLEN details_len = need_skin_phase
        ? cast(REBLEN, IDX_SKINNER_MAX)
        : ARR_LEN(ACT_DETAILS(original));

    REBACT *defers = Make_Action(
        paramlist,
        meta,
        need_skin_phase ? &Skinner_Dispatcher : ACT_DISPATCHER(original),
        ACT_EXEMPLAR(original),  // don't add to the original's specialization
        details_len  // details array capacity
    );

    if (not need_skin_phase)  // inherit the native flag if no phase change
        SER(defers)->header.bits
            |= SER(original)->header.bits & DETAILS_FLAG_IS_NATIVE;

    if (need_skin_phase)
        Move_Value(
            ARR_AT(ACT_DETAILS(defers), IDX_SKINNER_SKINNED),
            ARG(action)
        );
    else {
        // We're reusing the original dispatcher, so also reuse the original
        // function body.  Note Blit_Relative() ensures that the cell formatting
        // on the source and target are the same, and it preserves relative
        // value information (rarely what you meant, but it's meant here).
        //
        unstable RELVAL *src = ARR_HEAD(ACT_DETAILS(original)) + 1;
        unstable RELVAL *dest = ARR_HEAD(ACT_DETAILS(defers)) + 1;
        for (; NOT_END(src); ++src, ++dest)
            Blit_Relative(dest, src);
    }

    return Init_Action(
        D_OUT,
        defers,  // REBACT* archetype doesn't contain a binding
        VAL_ACTION_LABEL(ARG(action)),
        VAL_BINDING(ARG(action))  // inherit binding (user can rebind)
    );
}


//
//  tweak: native [
//
//  {Modify a special property (currently only for ACTION!)}
//
//      return: "Same action identity as input"
//          [action!]
//      action "(modified) Action to modify property of"
//          [action!]
//      property "Currently must be [defer postpone]"
//          [word!]
//      enable [logic!]
//  ]
//
REBNATIVE(tweak)
{
    INCLUDE_PARAMS_OF_TWEAK;

    REBACT *act = VAL_ACTION(ARG(action));
    REBVAL *first = First_Unspecialized_Param(act);

    Reb_Param_Class pclass = first
        ? VAL_PARAM_CLASS(first)
        : REB_P_NORMAL;  // imagine it as <end>able

    REBFLGS flag;

    switch (VAL_WORD_SYM(ARG(property))) {
      case SYM_BARRIER:   // don't allow being taken as an argument, e.g. |
        flag = DETAILS_FLAG_IS_BARRIER;
        break;

      case SYM_DEFER:  // Special enfix behavior used by THEN, ELSE, ALSO...
        if (pclass != REB_P_NORMAL)
            fail ("TWEAK defer only actions with evaluative 1st params");
        flag = DETAILS_FLAG_DEFERS_LOOKBACK;
        break;

      case SYM_POSTPONE:  // Wait as long as it can to run w/o changing order
        if (pclass != REB_P_NORMAL and pclass != REB_P_SOFT_QUOTE)
            fail ("TWEAK postpone only actions with evaluative 1st params");
        flag = DETAILS_FLAG_POSTPONES_ENTIRELY;
        break;

      default:
        fail ("TWEAK currently only supports [barrier defer postpone]");
    }

    if (VAL_LOGIC(ARG(enable)))
        SER(act)->header.bits |= flag;
    else
        SER(act)->header.bits &= ~flag;

    RETURN (ARG(action));
}
