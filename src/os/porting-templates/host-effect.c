/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Additional code modifications and improvements Copyright 2012 Saphirion AG
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
**  Title: EFFECT Dialect Backend
**  Author: Richard Smolak, Carl Sassenrath
**  Purpose: Evaluates effect commands; calls graphics functions.
**  Tools: make-host-ext.r
**
************************************************************************
**
**  NOTE to PROGRAMMERS:
**
**    1. Keep code clear and simple.
**    2. Document unusual code, reasoning, or gotchas.
**    3. Use same style for code, vars, indent(4), comments, etc.
**    4. Keep in mind Linux, OS X, BSD, big/little endian CPUs.
**    5. Test everything, then test it again.
**
***********************************************************************/

#include "reb-host.h"
//#include "host-lib.h"

//#define INCLUDE_EXT_DATA
//#include "host-ext-effect.h"

//***** Externs *****

//***** Locals *****

static u32* effect_ext_words;

/***********************************************************************
**
*/	RXIEXT int RXD_Effect(int cmd, RXIFRM *frm, REBCEC *ctx)
/*
**		EFFECT command dispatcher.
**
***********************************************************************/
{
	switch (cmd) {

	case CMD_EFFECT_INIT_WORDS:
		effect_ext_words = RL_MAP_WORDS(RXA_SERIES(frm,1));
		break;

	default:
		return RXR_NO_COMMAND;
	}
    return RXR_UNSET;
}
