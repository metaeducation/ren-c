//
//  file: %sys-mold.h
//  summary: "Rebol Value to Text Conversions ('MOLD'ing and 'FORM'ing)"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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


#define Drop_Mold_If_Pushed(mo) \
    Drop_Mold_Core((mo), true)

#define Drop_Mold(mo) \
    Drop_Mold_Core((mo), false)

#define Mold_Element(mo,v) \
    Mold_Or_Form_Element((mo), (v), false)

#define Form_Element(mo,v) \
    Mold_Or_Form_Element((mo), (v), true)

#define Copy_Mold_Element(v,opts) \
    Copy_Mold_Or_Form_Element((v), (opts), false)

#define Copy_Form_Element(v,opts) \
    Copy_Mold_Or_Form_Element((v), (opts), true)

#define Copy_Form_Cell_Ignore_Quotes(v,opts) \
    Copy_Mold_Or_Form_Cell_Ignore_Quotes((v), (opts), true)

#define Copy_Mold_Cell_Ignore_Quotes(v,opts) \
    Copy_Mold_Or_Form_Cell_Ignore_Quotes((v), (opts), false)



// We want the molded object to be able to "round trip" back to the state it's
// in based on reloading the values.  Currently this is conservative and
// doesn't put quote marks on things that don't need it because they are inert,
// but maybe not a good idea... depends on the whole block/object model.
//
// https://forum.rebol.info/t/997
//
INLINE void Output_Apostrophe_If_Not_Inert(Strand* s, const Element* cell) {
    if (not Any_Inert(cell))
        Append_Codepoint(s, '\'');
}


INLINE void Construct_Molder(Molder* mo) {
    mo->strand = nullptr;  // used to tell if pushed or not
    mo->opts = 0;
    mo->indent = 0;
}

#define DECLARE_MOLDER(name) \
    Molder name##_struct; \
    Molder* name = &name##_struct; \
    Construct_Molder(name)

#define SET_MOLD_FLAG(mo,f) \
    ((mo)->opts |= (f))

#define GET_MOLD_FLAG(mo,f) \
    (did ((mo)->opts & (f)))

#define NOT_MOLD_FLAG(mo,f) \
    (not ((mo)->opts & (f)))

#define CLEAR_MOLD_FLAG(mo,f) \
    ((mo)->opts &= ~(f))


// Special flags for decimal formatting:
enum {
    DEC_MOLD_MINIMAL = 1 << 0       // allow decimal to be integer
};

#define MAX_DIGITS 17   // number of digits
#define MAX_NUMCHR 32   // space for digits and -.e+000%

#define MAX_INT_LEN     21
#define MAX_HEX_LEN     16
