//
//  File: %mod-image.c
//  Summary: "IMAGE! extension main C file"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Atronix Engineering
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// See notes in %extensions/image/README.md

#include "sys-core.h"

#include "tmp-mod-image.h"

#include "sys-image.h"

REBTYP *EG_Image_Type = nullptr;

Symbol(const*) S_Image(void) {
    return Canon(IMAGE_X);
}

//
//  startup*: native [
//
//  {Make the IMAGE! datatype work with GENERIC actions, comparison ops, etc}
//
//      return: <none>
//  ]
//
DECLARE_NATIVE(startup_p)
{
    IMAGE_INCLUDE_PARAMS_OF_STARTUP_P;

    Extend_Generics_Someday(nullptr);  // !!! vaporware, see comments

    // !!! See notes on Hook_Datatype for this poor-man's substitute for a
    // coherent design of an extensible object system (as per Lisp's CLOS)
    //
    EG_Image_Type = Hook_Datatype(
        "http://datatypes.rebol.info/image",
        "RGB image with alpha channel",
        &S_Image,
        &T_Image,
        &CT_Image,
        &MAKE_Image,
        &TO_Image,
        &MF_Image
    );

    return NONE;
}


//
//  shutdown_p: native [
//
//  {Remove behaviors for IMAGE! added by REGISTER-IMAGE-HOOKS}
//
//      return: <none>
//  ]
//
DECLARE_NATIVE(shutdown_p)
{
    IMAGE_INCLUDE_PARAMS_OF_SHUTDOWN_P;

    Unhook_Datatype(EG_Image_Type);

    return NONE;
}
