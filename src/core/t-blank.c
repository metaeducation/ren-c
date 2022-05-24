//
//  File: %t-blank.c
//  Summary: "Blank datatype"
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


//
//  MF_Null: C
//
// Prior to generalized quoting, NULL did not have a rendering function and
// it was considered an error to try and mold them.  When quoting arrived,
// escaped NULL was renderable as its ticks, followed by nothing.  This is
// the "nothing" part, saving on a special-case for that.
//
void MF_Null(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(mo);
    UNUSED(form);
    UNUSED(v);
}


//
//  MF_Blank: C
//
void MF_Blank(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(v);

    // While it was tempting to say that _ could act as "space", that overload
    // turns out to not be good mojo.
    //
    if (not form)
        Append_Ascii(mo->series, "_");
}


//
//  CT_Blank: C
//
// Must have a comparison function, otherwise SORT would not work on arrays
// with blanks in them.
//
REBINT CT_Blank(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);  // no strict form of comparison
    UNUSED(a);
    UNUSED(b);

    return 0;  // All blanks are equal
}


//
//  REBTYPE: C
//
// While generics like SELECT are able to dispatch on BLANK! and return NULL,
// they do so by not running at all...see PARAM_FLAG_NOOP_IF_BLANK.
//
REBTYPE(Blank)
{
    switch (ID_OF_SYMBOL(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // taken care of by `unit` above.

        // !!! REFLECT cannot use PARAM_FLAG_NOOP_IF_BLANK, due to the special
        // case of TYPE OF...where a BLANK! in needs to provide BLANK! the
        // datatype out.  Also, there currently exist "reflectors" that
        // return LOGIC!, e.g. TAIL?...and logic cannot blindly return null:
        //
        // https://forum.rebol.info/t/954
        //
        // So for the moment, we just ad-hoc return nullptr for some that
        // R3-Alpha returned NONE! for.  Review.
        //
        switch (VAL_WORD_ID(ARG(property))) {
          case SYM_INDEX:
          case SYM_LENGTH:
            return nullptr;

          default: break;
        }
        break; }

      case SYM_PICK_P: {
          INCLUDE_PARAMS_OF_PICK_P;

        UNUSED(ARG(location));
        UNUSED(ARG(picker));

        // !!! The idea of allowing you to pick one step of anything out of
        // a BLANK! and return NULL was thrown in as a potential way of
        // getting an interesting distinction between NULL and BLANK!.  It
        // may not be the best idea.
        //
        return nullptr; }

      case SYM_COPY: { // since `copy/deep [1 _ 2]` is legal, allow `copy _`
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(ARG(value)); // already referenced as `unit`

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        UNUSED(REF(deep));
        UNUSED(REF(types));

        return Init_Blank(OUT); }

      default: break;
    }

    return R_UNHANDLED;
}



//
//  MF_Handle: C
//
void MF_Handle(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);  // !!! Handles have "no printable form", what to do here?
    UNUSED(v);

    Append_Ascii(mo->series, "#[handle!]");
}


//
//  CT_Handle: C
//
// !!! Comparing handles is something that wasn't in R3-Alpha and wasn't
// specially covered by Cmp_Value() in R3-Alpha...it fell through to the
// `default:` that just returned a "difference" of 0, so all handles were
// equal.  Ren-C eliminated the default case and instead made comparison of
// handles an error...but that meant comparing objects that contained
// fields that were handles an error.  This meant code looking for "equal"
// PORT!s via FIND did not work.  This raises a larger issue about sameness
// vs. equality that should be studied.
//
REBINT CT_Handle(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);

    // Shared handles are equal if their nodes are equal.  (It may not make
    // sense to have other ideas of equality, e.g. if two nodes incidentally
    // point to the same thing?)
    //
    if (GET_CELL_FLAG(a, FIRST_IS_NODE)) {
        if (NOT_CELL_FLAG(b, FIRST_IS_NODE))
            return 1;

        if (VAL_NODE1(a) == VAL_NODE1(b))
            return 0;

        return VAL_NODE1(a) > VAL_NODE1(b) ? 1 : -1;
    }
    else if (GET_CELL_FLAG(b, FIRST_IS_NODE))
        return -1;

    // There is no "identity" when it comes to a non-shared handles, so we
    // can only compare the pointers.
    //
    if (Is_Handle_Cfunc(a)) {
        if (not Is_Handle_Cfunc(b))
            return 1;

        if (VAL_HANDLE_CFUNC(a) == VAL_HANDLE_CFUNC(b))
            return 0;

        // !!! Function pointers aren't > or < comparable in ISO C.  This is
        // indicative of what we know already, that HANDLE!s are members of
        // "Eq" but not "Ord" (in Haskell speak).  Comparison is designed to
        // not know whether we're asking for equality or orderedness and must
        // return -1, 0, or 1...so until that is remedied, give back an
        // inconsistent result that just conveys inequality.
        //
        return 1;
    }
    else if (Is_Handle_Cfunc(b))
        return -1;

    if (VAL_HANDLE_POINTER(REBYTE, a) == VAL_HANDLE_POINTER(REBYTE, b)) {
        if (VAL_HANDLE_LEN(a) == VAL_HANDLE_LEN(b))
            return 0;

        return VAL_HANDLE_LEN(a) > VAL_HANDLE_LEN(b) ? 1 : -1;
    }

    return VAL_HANDLE_POINTER(REBYTE, a) > VAL_HANDLE_POINTER(REBYTE, b)
        ? 1
        : -1;
}


//
// REBTYPE: C
//
// !!! Currently, in order to have a comparison function a datatype must also
// have a dispatcher for generics, and the comparison is essential.  Hence
// this cannot use a `-` in the %reb-types.r in lieu of this dummy function.
//
REBTYPE(Handle)
{
    UNUSED(frame_);
    UNUSED(verb);

    return R_UNHANDLED;
}
