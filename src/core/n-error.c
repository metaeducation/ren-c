//
//  file: %n-error.c
//  summary: "native functions for raising and trapping errors"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Note that the mechanism by which errors are raised is based on longjmp(),
// and thus can interrupt stacks in progress.  Trapping errors is only done
// by those levels of the stack that have done a PUSH_TRAP (as opposed to
// detecting thrown values, that is "cooperative" and "bubbles" up through
// every stack level in its return slot, with no longjmp()).
//

#include "sys-core.h"


// This is the code which is protected by the exception mechanism.  See the
// rebRescue() API for more information.
//
static const Value* Rescue_Dangerous(Level* level_) {
    INCLUDE_PARAMS_OF_RESCUE;

    if (Do_Branch_Throws(OUT, ARG(CODE)))
        return TRASH_VALUE;  // not API value, no proxying needed

    return nullptr;
}


//
//  rescue: native [
//
//  {Tries to DO a block, recovering from abrupt failures}
//
//      return: "ERROR! if failure intercepted, else null"
//          [~null~ error!]
//      code "Code to execute and monitor"
//          [block! action!]
//  ]
//
DECLARE_NATIVE(RESCUE)
{
    INCLUDE_PARAMS_OF_RESCUE;

    Value* error = rebRescue(f_cast(REBDNG*, &Rescue_Dangerous), level_);
    UNUSED(ARG(CODE));  // gets used by the above call, via the level_ pointer

    if (not error)  // code didn't panic() or throw
        return nullptr;

    if (Is_Trash(error))  // signal used to indicate a throw
        return BOUNCE_THROWN;

    assert(Is_Error(error));

    return error;
}


static Value* Enrescue_Dangerous(Level* level_) {
    INCLUDE_PARAMS_OF_ENRESCUE;

    if (Do_Branch_Throws(OUT, ARG(CODE))) {
        Init_Error(OUT, Error_No_Catch_For_Throw(OUT));
        return nullptr;
    }

    Meta_Quotify(OUT);
    return nullptr;
}


//
//  enrescue: native [
//
//  {DO a block and return LIFT'd result, unless error is raised}
//
//      return: "ERROR! if raised, or lift'd result"
//          [error! word! group!]
//      code "Code to execute and monitor"
//          [block! action!]
//  ]
//
DECLARE_NATIVE(ENRESCUE)
{
    INCLUDE_PARAMS_OF_ENRESCUE;

    Bounce error = rebRescue(f_cast(REBDNG*, &Enrescue_Dangerous), level_);
    UNUSED(ARG(CODE)); // gets used by the above call, via the level_ pointer

    if (error)
        return error;

    return OUT;
}


//
//  set-location-of-error: native [
//
//  {Sets the WHERE, NEAR, FILE, and LINE fields of an error}
//
//      return: [~null~]
//      error [error!]
//      location [frame! any-word!]
//  ]
//
DECLARE_NATIVE(SET_LOCATION_OF_ERROR)
{
    INCLUDE_PARAMS_OF_SET_LOCATION_OF_ERROR;

    Value* location = ARG(LOCATION);

    VarList* context;
    if (Is_Word(location)) {
        if (not IS_WORD_BOUND(location))
            panic ("SET-LOCATION-OF-ERROR requires bound WORD!");
        context = VAL_WORD_CONTEXT(location);
    }
    else {
        assert(Is_Frame(location));
        context = Cell_Varlist(location);
    }

    Option(Level*) where = Level_Of_Varlist_May_Panic(context);

    if (not where) {
        // should this error?
    }
    else {
        Error* error = cast(Error*, Cell_Varlist(ARG(ERROR)));
        Set_Location_Of_Error(error, unwrap where);
    }

    return nullptr;
}
