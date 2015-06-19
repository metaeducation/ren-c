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
**  Title: <platform> Event Handler
**  Author: Richard Smolak
**  Purpose: This code handles windowing related events.
**	Related: host-window.c, dev-event.c
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
#include <stdio.h>
#include <math.h>	//for floor()

#include "reb-host.h"
#include "host-lib.h"

#include "host-jni.h"

//***** Constants *****

enum input_events {
	EV_MOVE = 0,
	EV_DOWN,
	EV_UP,
	EV_CLOSE,
	EV_RESIZE,
	EV_ROTATE,
	EV_KEY_DOWN,
	EV_KEY_UP
};

//***** Externs *****
extern REBOOL Resize_Window(REBGOB *gob, REBOOL redraw);

/***********************************************************************
**
**	Local Functions
**
***********************************************************************/

static void Add_Event_XY(REBGOB *gob, REBINT id, REBINT xy, REBINT flags)
{
	REBEVT evt;
	REBINT res;
	evt.type  = id;
	evt.flags = (u8) (flags | (1<<EVF_HAS_XY));
	evt.model = EVM_GUI;
	evt.data  = xy;
	evt.ser = (void*)gob;
//	LOGI("adding event...\n");
	res = RL_Event(&evt);	// returns 0 if queue is full
//	LOGI("event dispatched: %d\n", res);
}

static void Add_Event_Key(REBGOB *gob, REBINT id, REBINT key, REBINT flags)
{
	REBEVT evt;

	evt.type  = id;
	evt.flags = flags;
	evt.model = EVM_GUI;
	evt.data  = key;
	evt.ser = (void*)gob;

	RL_Event(&evt);	// returns 0 if queue is full
}

void dispatch_event(REBGOB *gob, REBINT type, REBINT x, REBINT y)
{
//	LOGI("Got event! %d %dx%d", type, x, y);
	REBGOB* g = (REBGOB*)gob;
	REBINT ev_type = -1;
	REBINT p_x = ROUND_TO_INT(PHYS_COORD_X(x));
	REBINT p_y = ROUND_TO_INT(PHYS_COORD_Y(y));
	switch (type) {
		case EV_MOVE:
			ev_type = EVT_MOVE;
			break;
		case EV_DOWN:
			ev_type = EVT_DOWN;
			break;
		case EV_UP:
			ev_type = EVT_UP;
			break;
		case EV_KEY_DOWN:
			ev_type = EVT_KEY;
		case EV_KEY_UP:
			Add_Event_Key(g, ((ev_type == -1) ? EVT_KEY_UP : ev_type) , x, 0);
			return;
		case EV_RESIZE:
			g->size.x = p_x;
			g->size.y = p_y;
			Resize_Window(g, FALSE);
			ev_type = EVT_RESIZE;
			break;
		case EV_ROTATE:
			g->size.x = p_x;
			g->size.y = p_y;
			Resize_Window(g, TRUE);
			ev_type = EVT_ROTATE;
			break;
		case EV_CLOSE:
			ev_type = EVT_CLOSE;
			break;
	}
	if (ev_type != -1)
		Add_Event_XY(g, ev_type, ((p_y << 16) | (p_x & 0xFFFF)), 0);
}
