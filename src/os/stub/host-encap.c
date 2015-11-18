/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Title: Stub File for Encap
**  Purpose:
**      Under the current design of the hostkit, certain functions that
**      are called must be provided.  The encap hook is called by the
**      main, so we need a null function.
**
***********************************************************************/

#include <stddef.h>

#include "reb-host.h"


//
//  OS_Read_Embedded: C
//
REBYTE * OS_Read_Embedded (REBI64 *script_size)
{
    return NULL;
}

