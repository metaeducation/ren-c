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

#include <stdlib.h>
#include <math.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>

#include "reb-host.h"
#include "host-window.h"

//***** Externs *****
RXIEXT int RXD_Graphics(int cmd, RXIFRM *frm, REBCEC *data);
RXIEXT int RXD_Draw(int cmd, RXIFRM *frm, REBCEC *ctx);
RXIEXT int RXD_Shape(int cmd, RXIFRM *frm, REBCEC *ctx);
RXIEXT int RXD_Text(int cmd, RXIFRM *frm, REBCEC *ctx);

extern const unsigned char RX_graphics[];
extern const unsigned char RX_draw[];
extern const unsigned char RX_shape[];
extern const unsigned char RX_text[];

extern x_info_t *global_x_info;
extern REBGOBWINDOWS *Gob_Windows;

//**********************************************************************
//** Helper Functions **************************************************
//**********************************************************************

REBUPT cursor_maps [] = {
	32512, XC_left_ptr,		/* Standard arrow*/
	32513, XC_xterm,	/* I-beam*/
	32514, XC_watch, /* Hourglass*/
	32515, XC_crosshair, 	/* Crosshair*/
	32516, XC_center_ptr, /* Vertical arrow*/
	32640, XC_sizing, /* Obsolete for applications marked version 4.0 or later. Use IDC_SIZEALL.*/
	32641, XC_icon, /* Obsolete for applications marked version 4.0 or later.*/
	32642, 0, /* Double-pointed arrow pointing northwest and southeast*/
	32643, 0, /* Double-pointed arrow pointing northeast and southwest*/
	32644, XC_sb_h_double_arrow, /* Double-pointed arrow pointing west and east*/
	32645, XC_sb_v_double_arrow, /* Double-pointed arrow pointing north and south*/
	32646, XC_sizing, /* Four-pointed arrow pointing north, south, east, and west*/
	32648, XC_circle, /* Slashed circle*/
	32649, XC_hand2,		/* Hand*/
	32650, XC_watch, 		/* Standard arrow and small hourglass*/
	32651, XC_question_arrow, /* Arrow and question mark*/
	0, 0
};

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
#define MAX_WINDOWS 64
	int i = 0;
	for(i = 0; i < MAX_WINDOWS; i ++){
		if (Gob_Windows[i].win != 0){
			XDefineCursor(global_x_info->display, (Window)Gob_Windows[i].win, (Cursor)cursor);
		}
	}
}

/***********************************************************************
**
*/	void* OS_Load_Cursor(void *cursor)
/*
**
**
***********************************************************************/
{
	unsigned int shape = 0;
	REBUPT *ptr = NULL;
	for (ptr = cursor_maps; *ptr != 0; ptr += 2){
		if (*ptr > (REBUPT)cursor)
			break;
		if (*ptr == (REBUPT)cursor){
			shape = *(ptr + 1);
			break;
		}
	}
	if (shape != 0) {
		return (void*)XCreateFontCursor(global_x_info->display, shape);
	}
	return NULL;
}

/***********************************************************************
**
*/	void OS_Destroy_Cursor(void *cursor)
/*
**
**
***********************************************************************/
{
	if (cursor != NULL)
		XFreeCursor(global_x_info->display, (Cursor)cursor);
}

/***********************************************************************
**
*/	REBD32 OS_Get_Metrics(METRIC_TYPE type)
/*
**	Provide OS specific UI related information.
**
***********************************************************************/
{
       Screen *sc = NULL;
	   Window root = DefaultRootWindow(global_x_info->display);
       int dot, mm;
	   Atom     actual_type;
	   int      actual_format;
	   long     nitems;
	   long     bytes;
	   long     *data = NULL;
	   int      status;
	   int 		i;
	   REBD32	ret;

       if (global_x_info->display == NULL){
               return 0;
       }
       sc = XDefaultScreenOfDisplay(global_x_info->display);
       switch(type) {
               case SM_SCREEN_WIDTH:
                       return XWidthOfScreen(sc);
               case SM_SCREEN_HEIGHT:
                       return XHeightOfScreen(sc);
               case SM_WORK_X:
               case SM_WORK_Y:
               case SM_WORK_WIDTH:
               case SM_WORK_HEIGHT:
					   status = XGetWindowProperty(global_x_info->display,
												   RootWindowOfScreen(sc),
												   XInternAtom(global_x_info->display, "_NET_WORKAREA", True),
												   0,
												   (~0L),
												   False,
												   AnyPropertyType,
												   &actual_type,
												   &actual_format,
												   &nitems,
												   &bytes,
												   (unsigned char**)&data);
					   if (status != Success) {
						   //RL_Print("status = %d\n", status);
						   Host_Crash("XGetWindowProperty failed in OS_Get_Metrics");
					   }

					   /*
					   RL_Print("actual_type %d\n", actual_type);
					   RL_Print("actual_format %d\n", actual_format);
					   RL_Print("nitems %d\n", nitems);
					   RL_Print("bytes %d\n", bytes);
					   for (i=0; i < nitems; i++){
						   RL_Print("data[%d] %d\n", i, data[i]);
					   }
					   */
					   switch(type) {
						   case SM_WORK_X:
							   ret = data[0];
							   break;
						   case SM_WORK_Y:
							   ret = data[1];
							   break;
						   case SM_WORK_WIDTH:
							   ret = data[2];
							   break;
						   case SM_WORK_HEIGHT:
							   ret = data[3];
							   break;
					   }
					   XFree(data);
					   return ret;
               case SM_TITLE_HEIGHT:
					   status = XGetWindowProperty(global_x_info->display,
												   RootWindowOfScreen(sc),
												   XInternAtom(global_x_info->display, "_NET_FRAME_EXTENTS", True),
												   0,
												   (~0L),
												   False,
												   AnyPropertyType,
												   &actual_type,
												   &actual_format,
												   &nitems,
												   &bytes,
												   (unsigned char**)&data);
					   if (status != Success || data == NULL) {
						   //RL_Print("status = %d, nitmes = %d\n", status, nitems);
						   //Host_Crash("XGetWindowProperty failed in OS_Get_Metrics");
						   return 20; //FIXME
					   }

                       ret = data[2]; //left, right, top, bottom
					   XFree(data);
					   return ret;
               case SM_SCREEN_DPI_X:
                       dot = XWidthOfScreen(sc);
                       mm = XWidthMMOfScreen(sc);
                       return round(dot * 25.4 / mm);
               case SM_SCREEN_DPI_Y:
                       dot = XHeightOfScreen(sc);
                       mm = XHeightMMOfScreen(sc);
                       return round(dot * 25.4 / mm);
               case SM_BORDER_WIDTH:
               case SM_BORDER_HEIGHT:
               case SM_BORDER_FIXED_WIDTH:
               case SM_BORDER_FIXED_HEIGHT:
					   return REB_WINDOW_BORDER_WIDTH;
               case SM_WINDOW_MIN_WIDTH:
					   return 132; //FIXME; from windows
               case SM_WINDOW_MIN_HEIGHT:
					   return 38; //FIXME; from windows
               default:
					   Host_Crash("NOT implemented others in OS_Get_Metrics");
                       return 0; //FIXME, not implemented 
       }
}

/***********************************************************************
**
*/	void OS_Show_Soft_Keyboard(void* win, REBINT x, REBINT y)
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
#if defined(AGG_WIN32_FONTS) || defined(AGG_FREETYPE)
	RL_Extend((REBYTE *)(&RX_text[0]), &RXD_Text);
#endif
}

/***********************************************************************
**
*/	void OS_Destroy_Graphics(void)
/*
**	Finalize any special variables of the graphics subsystem.
**
***********************************************************************/
{
	if(global_x_info) {
	   if(global_x_info->display){
		   XCloseDisplay(global_x_info->display);
	   }
	   OS_Free(global_x_info);
	}
}
