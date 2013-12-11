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
