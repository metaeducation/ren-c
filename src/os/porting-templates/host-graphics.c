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
**  Title: Graphics Commmands
**  Author: Richard Smolak, Carl Sassenrath
**  Purpose: "View" commands support.
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

//***** Externs *****
RXIEXT int RXD_Graphics(int cmd, RXIFRM *frm, REBCEC *data);
RXIEXT int RXD_Draw(int cmd, RXIFRM *frm, REBCEC *ctx);
RXIEXT int RXD_Shape(int cmd, RXIFRM *frm, REBCEC *ctx);
RXIEXT int RXD_Text(int cmd, RXIFRM *frm, REBCEC *ctx);

extern const unsigned char RX_graphics[];
extern const unsigned char RX_draw[];
extern const unsigned char RX_shape[];
extern const unsigned char RX_text[];

//**********************************************************************
//** Helper Functions **************************************************
//**********************************************************************


/***********************************************************************
**
*/	void* OS_Image_To_Cursor(REBYTE* image, REBINT width, REBINT height)
/*
**      Converts REBOL image! to Windows CURSOR
**
***********************************************************************/
{
	return 0;
}

/***********************************************************************
**
*/	void OS_Set_Cursor(void *cursor)
/*
**
**
***********************************************************************/
{
}

/***********************************************************************
**
*/	void* OS_Load_Cursor(void *cursor)
/*
**
**
***********************************************************************/
{
	return 0;
}

/***********************************************************************
**
*/	void OS_Destroy_Cursor(void *cursor)
/*
**
**
***********************************************************************/
{
}

/***********************************************************************
**
*/	REBD32 OS_Get_Metrics(METRIC_TYPE type)
/*
**	Provide OS specific UI related information.
**
***********************************************************************/
{
	return 0;
}

/***********************************************************************
**
*/	void OS_Show_Soft_Keyboard(REBINT win, REBINT x, REBINT y)
/*
**  Display software/virtual keyboard on the screen.
**  (mainly used on mobile platforms)
**
***********************************************************************/
{
}

/***********************************************************************
**
*/	void OS_Init_Graphics(void)
/*
**	Initialize special variables of the graphics subsystem.
**
***********************************************************************/
{
	RL_Extend((REBYTE *)(&RX_graphics[0]), &RXD_Graphics);
	RL_Extend((REBYTE *)(&RX_draw[0]), &RXD_Draw);
	RL_Extend((REBYTE *)(&RX_shape[0]), &RXD_Shape);
	RL_Extend((REBYTE *)(&RX_text[0]), &RXD_Text);
}

/***********************************************************************
**
*/	void OS_Destroy_Graphics(void)
/*
**	Finalize any special variables of the graphics subsystem.
**
***********************************************************************/
{
}
