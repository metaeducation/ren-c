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
**  Title: OS independent windowing/graphics code and dispatcher
**  File:  host-view.c
**  Author: Richard Smolak, Carl Sassenrath
**  Purpose: Provides shared functions for windowing/graphics
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

#include <string.h>
#include <math.h>	//for floor()
#include "reb-host.h"

#include "host-compositor.h"

#define INCLUDE_EXT_DATA
#include "host-ext-graphics.h"

//***** Constants *****

#define MAX_WINDOWS 64
#define GOB_COMPOSITOR(gob)	(Find_Compositor(gob)) //gets handle to window's

//***** Externs *****

extern void OS_Init_Windows(void);
extern void rebdrw_to_image(REBYTE *image, REBINT w, REBINT h, REBSER *block);
extern REBD32 OS_Get_Metrics(METRIC_TYPE type);

//***** Globals *****
REBGOBWINDOWS *Gob_Windows;
REBGOB *Gob_Root;				// Top level GOB (the screen)
REBXYF log_size = {1.0, 1.0};	//logical pixel size measured in physical pixels (can be changed using GUI-METRIC command)
REBXYF phys_size = {1.0, 1.0};	//physical pixel size measured in logical pixels(reciprocal value of log_size)
void *Cursor; // active mouse cursor object
void *Rich_Text;

//***** Locals *****
static REBXYF Zero_Pair = {0, 0};
static REBOOL Custom_Cursor = FALSE;
static u32* graphics_ext_words;

/***********************************************************************
**
*/	void Init_Windows(void)
/*
**	Initialize special variables of the windowing subsystem.
**
***********************************************************************/
{
	Gob_Windows = (struct gob_window *)OS_Make(sizeof(struct gob_window) * (MAX_WINDOWS+1));
	CLEAR(Gob_Windows, sizeof(struct gob_window) * (MAX_WINDOWS+1));

	//call OS specific initialization code
	OS_Init_Windows();
}


/**********************************************************************
**
**	Window Allocator
**
**	The window handle is not stored in the gob to avoid wasting
**	memory or creating too many exceptions in the gob.parent field.
**	Instead, we store gob and window pointers in an array that we
**	scan when required.
**
**	This code below is not optimial, but works ok because:
**		1) there are usually very few windows open
**		2) window functions are not called often
**		2) window events are mapped directly to gobs
**
**********************************************************************/

REBINT Alloc_Window(REBGOB *gob) {
	int n;
	for (n = 0; n < MAX_WINDOWS; n++) {
		if (Gob_Windows[n].gob == 0) {
			Gob_Windows[n].gob = gob;
			return n;
		}
	}
	return -1;
}

/***********************************************************************
**
*/	void* Find_Window(REBGOB *gob)
/*
**	Return window handle of given gob.
**
***********************************************************************/
{
	int n;
	for (n = 0; n < MAX_WINDOWS; n++) {
		if (Gob_Windows[n].gob == gob) return Gob_Windows[n].win;
	}
	return 0;
}

/***********************************************************************
**
*/	void* Find_Compositor(REBGOB *gob)
/*
**	Return compositor handle of given gob.
**
***********************************************************************/
{
	int n;
	for (n = 0; n < MAX_WINDOWS; n++) {
		if (Gob_Windows[n].gob == gob) return Gob_Windows[n].compositor;
	}
	return 0;
}

/***********************************************************************
**
*/	void Free_Window(REBGOB *gob)
/*
**	Release the Gob_Windows slot used by given gob.
**
***********************************************************************/
{
	int n;
	for (n = 0; n < MAX_WINDOWS; n++) {
		if (Gob_Windows[n].gob == gob) {
			rebcmp_destroy(Gob_Windows[n].compositor);
//			Reb_Print("rebcmp_destroy %d", Gob_Windows[n].compositor);
			Gob_Windows[n].gob = 0;
			return;
		}
	}
}

/***********************************************************************
**
*/	REBOOL Resize_Window(REBGOB *gob, REBOOL redraw)
/*
**		Resize window buffer.
**
***********************************************************************/
{
	void *compositor;
	REBOOL changed;
	compositor = GOB_COMPOSITOR(gob);
	changed = rebcmp_resize_buffer(compositor, gob);
	if (redraw){
		rebcmp_compose(compositor, gob, gob, FALSE);
		rebcmp_blit(compositor);
	}
	return changed;
}

/***********************************************************************
**
*/	void Draw_Window(REBGOB *wingob, REBGOB *gob)
/*
**		Refresh the GOB within the given window. If the wingob
**		is zero, then find the correct window for it.
**
***********************************************************************/
{
	void *compositor;

	if (!wingob) {
		wingob = gob;
		while (GOB_PARENT(wingob) && GOB_PARENT(wingob) != Gob_Root
			&& GOB_PARENT(wingob) != wingob) // avoid infinite loop
			wingob = GOB_PARENT(wingob);

		//check if it is really open
		if (!IS_WINDOW(wingob) || !GET_GOB_STATE(wingob, GOBS_OPEN)) return;
	}

//	Reb_Print("draw: %d %8x", nnn++, gob);

#ifdef AGG_OPENGL
	HDC paintDC = GetDC(GOB_HWIN(wingob));
	int ow = GOB_LOG_W_INT(wingob);
	int oh = GOB_LOG_H_INT(wingob);
    compositor = GOB_COMPOSITOR(wingob);
	rebcmp_compose(compositor, wingob, gob, FALSE);

    SwapBuffers( paintDC );
	ReleaseDC(GOB_HWIN(gob),paintDC);
#else
	//render and blit the GOB
	compositor = GOB_COMPOSITOR(wingob);
	rebcmp_compose(compositor, wingob, gob, FALSE);
	rebcmp_blit(compositor);
#endif
}

/***********************************************************************
**
*/  void Show_Gob(REBGOB *gob)
/*
**	Notes:
**		1.	Can be called with NONE (0), Gob_Root (All), or a
**			specific gob to open, close, or refresh.
**
**		2.	A new window will be in Gob_Root/pane but will not
**			have GOBF_WINDOW set.
**
**		3.	A closed window will have no PARENT and will not be
**			in the Gob_Root/pane but will have GOBF_WINDOW set.
**
***********************************************************************/
{
	REBINT n;
	REBGOB *g;
	REBGOB **gp;

	if (!gob) return;

	// Are we asked to open/close/refresh all windows?
	if (gob == Gob_Root) {  // show none, and show screen-gob

		// Remove any closed windows:
		for (n = 0; n < MAX_WINDOWS; n++) {
			if (g = Gob_Windows[n].gob) {
				if (!GOB_PARENT(g) && GET_GOB_FLAG(g, GOBF_WINDOW))
					OS_Close_Window(g);
			}
		}

		// Open any new windows:
		if (GOB_PANE(Gob_Root)) {
			gp = GOB_HEAD(Gob_Root);
			for (n = GOB_TAIL(Gob_Root)-1; n >= 0; n--, gp++) {
				if (!GET_GOB_FLAG(*gp, GOBF_WINDOW))
					OS_Open_Window(*gp);
					Draw_Window(0, *gp);
			}
		}
		return;
	}
	// Is it a window gob that needs to be closed?
	else if (!GOB_PARENT(gob) && GET_GOB_FLAG(gob, GOBF_WINDOW)) {
		OS_Close_Window(gob);
		return;
	}
	// Is it a window gob that needs to be opened or refreshed?
	else if (GOB_PARENT(gob) == Gob_Root) {
		if (!GET_GOB_STATE(gob, GOBS_OPEN))
			OS_Open_Window(gob);
		else
			OS_Update_Window(gob); // Problem! We may not want this all the time.
	}

	// Otherwise, composite and referesh the gob or all gobs:
	Draw_Window(0, gob);  // 0 = window parent of gob
}

/***********************************************************************
**
*/  REBSER* Gob_To_Image(REBGOB *gob)
/*
**		Render gob into an image.
**		Clip to keep render inside the image provided.
**
***********************************************************************/
{
	REBINT w,h,result;
	REBSER* img;
	void* cp;
	REBGOB* parent;
	REBGOB* topgob;

	w = (REBINT)GOB_LOG_W(gob);
	h = (REBINT)GOB_LOG_H(gob);
	img = (REBSER*)RL_MAKE_IMAGE(w,h);

	//search the window(or topmost) gob
	topgob = gob;
	while (GOB_PARENT(topgob) && GOB_PARENT(topgob) != Gob_Root
		&& GOB_PARENT(topgob) != topgob) // avoid infinite loop
		topgob = GOB_PARENT(topgob);

	cp = rebcmp_create(Gob_Root, gob);
	rebcmp_compose(cp, topgob, gob, TRUE);

	//copy the composed result to image
	memcpy((REBYTE *)RL_SERIES(img, RXI_SER_DATA), rebcmp_get_buffer(cp), w * h * 4);

	rebcmp_release_buffer(cp);
	
	rebcmp_destroy(cp);

	return img;
}

//**********************************************************************
//** Graphics commands! dipatcher **************************************
//**********************************************************************


/***********************************************************************
**
*/	RXIEXT int RXD_Graphics(int cmd, RXIFRM *frm, REBCEC *data)
/*
**		Graphics command extension dispatcher.
**
***********************************************************************/
{
    switch (cmd) {
        case CMD_GRAPHICS_SHOW:
        {
            REBGOB* gob = (REBGOB*)RXA_SERIES(frm, 1);
            Show_Gob(gob);
            RXA_TYPE(frm, 1) = RXT_GOB;
            return RXR_VALUE;
        }
#if defined(AGG_WIN32_FONTS) || defined(AGG_FREETYPE)
        case CMD_GRAPHICS_SIZE_TEXT:
		
            if (Rich_Text) {
                RXA_TYPE(frm, 2) = RXT_PAIR;
                rt_size_text(Rich_Text, (REBGOB*)RXA_SERIES(frm, 1),&RXA_PAIR(frm, 2));
				RXA_PAIR(frm, 1).x = PHYS_COORD_X(RXA_PAIR(frm, 2).x);
				RXA_PAIR(frm, 1).y = PHYS_COORD_Y(RXA_PAIR(frm, 2).y);
                RXA_TYPE(frm, 1) = RXT_PAIR;
                return RXR_VALUE;
            }

            break;

        case CMD_GRAPHICS_OFFSET_TO_CARET:
            if (Rich_Text) {
                REBINT element = 0, position = 0;
                REBSER* dialect;
                REBSER* block;
                RXIARG val; //, str;
                REBCNT n, type;

                rt_offset_to_caret(Rich_Text, (REBGOB*)RXA_SERIES(frm, 1), RXA_LOG_PAIR(frm, 2), &element, &position);
//                RL_Print("OTC: %dx%d %d, %d\n", (int)RXA_LOG_PAIR(frm, 2).x, (int)RXA_LOG_PAIR(frm, 2).y, element, position);
                dialect = (REBSER *)GOB_CONTENT((REBGOB*)RXA_SERIES(frm, 1));
                block = RL_MAKE_BLOCK(RL_SERIES(dialect, RXI_SER_TAIL));
                for (n = 0; type = RL_GET_VALUE(dialect, n, &val); n++) {
                    if (n == element) val.index = position;
                    RL_SET_VALUE(block, n, val, type);
                }

                RXA_TYPE(frm, 1) = RXT_BLOCK;
                RXA_SERIES(frm, 1) = block;
                RXA_INDEX(frm, 1) = element;

                return RXR_VALUE;
            }

            break;

        case CMD_GRAPHICS_CARET_TO_OFFSET:
            if (Rich_Text) {
                REBXYF result;
                REBINT elem,pos;
                if (RXA_TYPE(frm, 2) == RXT_INTEGER){
                    elem = RXA_INT64(frm, 2)-1;
                } else {
                    elem = RXA_INDEX(frm, 2);
                }
                if (RXA_TYPE(frm, 3) == RXT_INTEGER){
                    pos = RXA_INT64(frm, 3)-1;
                } else {
                    pos = RXA_INDEX(frm, 3);
                }

                rt_caret_to_offset(Rich_Text, (REBGOB*)RXA_SERIES(frm, 1), &result, elem, pos);

                RXA_TYPE(frm, 1) = RXT_PAIR;
				RXA_ARG(frm, 1).pair.x = ROUND_TO_INT(PHYS_COORD_X(result.x));
				RXA_ARG(frm, 1).pair.y = ROUND_TO_INT(PHYS_COORD_Y(result.y));
                return RXR_VALUE;
            }
            break;
#endif
        case CMD_GRAPHICS_DRAW:
            {
                REBYTE* img = 0;
                REBINT w,h;
                if (RXA_TYPE(frm, 1) == RXT_IMAGE) {
                    img = RXA_IMAGE_BITS(frm, 1);
                    w = RXA_IMAGE_WIDTH(frm, 1);
                    h = RXA_IMAGE_HEIGHT(frm, 1);
                } else {
                    REBSER* i;
					REBXYF s = RXA_LOG_PAIR(frm,1);
                    w = s.x;
                    h = s.y;
                    i = RL_MAKE_IMAGE(w,h);
                    img = (REBYTE *)RL_SERIES(i, RXI_SER_DATA);

                    RXA_TYPE(frm, 1) = RXT_IMAGE;
                    RXA_ARG(frm, 1).width = w;
                    RXA_ARG(frm, 1).height = h;
                    RXA_ARG(frm, 1).image = i;
                }
				rebdrw_to_image(img, w, h, RXA_SERIES(frm, 2));

                return RXR_VALUE;
            }
            break;

        case CMD_GRAPHICS_GUI_METRIC:
            {

                REBD32 x,y;
                u32 w = RL_FIND_WORD(graphics_ext_words,RXA_WORD(frm, 1));

                switch(w)
                {
                    case W_GRAPHICS_SCREEN_SIZE:
                        x = PHYS_COORD_X(OS_Get_Metrics(SM_SCREEN_WIDTH));
                        y = PHYS_COORD_Y(OS_Get_Metrics(SM_SCREEN_HEIGHT));
                        break;
						
                    case W_GRAPHICS_LOG_SIZE:
						if (RXA_TYPE(frm, 3) == RXT_PAIR){
							log_size.x = RXA_PAIR(frm, 3).x;
							log_size.y = RXA_PAIR(frm, 3).y;
							phys_size.x = 1 / log_size.x;
							phys_size.y = 1 / log_size.y;
						}
						x = log_size.x;
						y = log_size.y;
                        break;

                    case W_GRAPHICS_PHYS_SIZE:
						x = phys_size.x;
						y = phys_size.y;
                        break;
						
                    case W_GRAPHICS_TITLE_SIZE:
                        x = 0;
                        y = PHYS_COORD_Y(OS_Get_Metrics(SM_TITLE_HEIGHT));
                        break;

                    case W_GRAPHICS_BORDER_SIZE:
                        x = OS_Get_Metrics(SM_BORDER_WIDTH);
                        y = OS_Get_Metrics(SM_BORDER_HEIGHT);
                        break;

                    case W_GRAPHICS_BORDER_FIXED:
                        x = OS_Get_Metrics(SM_BORDER_FIXED_WIDTH);
                        y = OS_Get_Metrics(SM_BORDER_FIXED_HEIGHT);
                        break;

                    case W_GRAPHICS_WINDOW_MIN_SIZE:
                        x = OS_Get_Metrics(SM_WINDOW_MIN_WIDTH);
                        y = OS_Get_Metrics(SM_WINDOW_MIN_HEIGHT);
                        break;

                    case W_GRAPHICS_WORK_ORIGIN:
                        x = OS_Get_Metrics(SM_WORK_X);
                        y = OS_Get_Metrics(SM_WORK_Y);
                        break;

                    case W_GRAPHICS_WORK_SIZE:
                        x = PHYS_COORD_X(OS_Get_Metrics(SM_WORK_WIDTH));
                        y = PHYS_COORD_Y(OS_Get_Metrics(SM_WORK_HEIGHT));
                        break;

					case W_GRAPHICS_SCREEN_DPI:
                        x = OS_Get_Metrics(SM_SCREEN_DPI_X);
                        y = OS_Get_Metrics(SM_SCREEN_DPI_Y);
                        break;
                }
	
                if (w){
                    RXA_PAIR(frm, 1).x = x;
                    RXA_PAIR(frm, 1).y = y;
                    RXA_TYPE(frm, 1) = RXT_PAIR;
                } else {
                    RXA_TYPE(frm, 1) = RXT_NONE;
                }
                return RXR_VALUE;
			
            }
            break;

        case CMD_GRAPHICS_INIT:
            Gob_Root = (REBGOB*)RXA_SERIES(frm, 1); // system/view/screen-gob
            Gob_Root->size.x = OS_Get_Metrics(SM_SCREEN_WIDTH);
            Gob_Root->size.y = OS_Get_Metrics(SM_SCREEN_HEIGHT);

#if defined(AGG_WIN32_FONTS) || defined(AGG_FREETYPE)
            //Initialize text rendering context
            if (Rich_Text) Destroy_RichText(Rich_Text);
            Rich_Text = (void*)Create_RichText();
#endif
            break;

        case CMD_GRAPHICS_INIT_WORDS:
            graphics_ext_words = RL_MAP_WORDS(RXA_SERIES(frm,1));
            break;

        case CMD_GRAPHICS_CURSOR:
			{
                REBINT n = 0;
                REBSER image = 0;

                if (RXA_TYPE(frm, 1) == RXT_IMAGE) {
                    image = RXA_IMAGE_BITS(frm,1);
                } else {
                    n = RXA_INT64(frm,1);
                }

                if (Custom_Cursor) {
                    //Destroy cursor object only if it is a custom image
                    OS_Destroy_Cursor(Cursor);
                    Custom_Cursor = FALSE;
                }

                if (n > 0)
                    Cursor = (void*)OS_Load_Cursor(n);
                else if (image) {
                    Cursor = (void*)OS_Image_To_Cursor(image, RXA_IMAGE_WIDTH(frm,1), RXA_IMAGE_HEIGHT(frm,1));
                    Custom_Cursor = TRUE;
                } else
                    Cursor = NULL;

                OS_Set_Cursor(Cursor);
			}
            break;

		case CMD_GRAPHICS_SHOW_SOFT_KEYBOARD:
			{
				REBINT x = 0;
				REBINT y = 0;
				REBINT win = 0;
				
				if (RXA_TYPE(frm, 2) == RXT_GOB){
					REBGOB* parent_gob = (REBGOB*)RXA_SERIES(frm, 2);
					REBINT max_depth = 1000; // avoid infinite loops
					while (GOB_PARENT(parent_gob) && (max_depth-- > 0) && !GET_GOB_FLAG(parent_gob, GOBF_WINDOW))
					{
						x += GOB_LOG_X_INT(parent_gob);
						y += GOB_LOG_Y_INT(parent_gob);
						parent_gob = GOB_PARENT(parent_gob);
					} 
					win = (REBINT)Find_Window(parent_gob);
				}
				OS_Show_Soft_Keyboard(win, x, y);
			}
			break;
        default:
            return RXR_NO_COMMAND;
    }
    return RXR_UNSET;
}
