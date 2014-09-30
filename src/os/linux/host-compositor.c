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
**  Title: <platform/backend> Compositor abstraction layer API.
**  Author: Richard Smolak
**  File:  host-compositor.c
**  Purpose: Provides simple example of gfx backend specific compositor.
**  Note: This is not fully working code, see the notes for insertion
**        of your backend specific code. Of course the example can be fully
**        modified according to the specific backend. Only the declarations
**        of compositor API calls must remain consistent.
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

#include <stdio.h> //for NULL
#include <math.h>	//for floor()
#include <string.h> //for memset
#include <assert.h>
#include <unistd.h> //for size_t

#include <X11/Xlib.h>
#include <X11/Xregion.h>
#include <X11/Xutil.h>
#ifdef USE_XSHM
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xdbe.h>
#endif

#include "reb-host.h"
#include "host-lib.h" //for OS_Make

#include "host-window.h"

#define BYTE_PER_PIXEL 4
void rebdrw_gob_color(REBGOB *gob, REBYTE* buf, REBXYI buf_size, REBXYI abs_oft, REBXYI clip_oft, REBXYI clip_siz);
void rebdrw_gob_image(REBGOB *gob, REBYTE* buf, REBXYI buf_size, REBXYI abs_oft, REBXYI clip_oft, REBXYI clip_siz);
void rebdrw_gob_draw(REBGOB *gob, REBYTE* buf, REBXYI buf_size, REBXYI abs_oft, REBXYI clip_oft, REBXYI clip_siz);
REBINT rt_gob_text(REBGOB *gob, REBYTE* buf, REBXYI buf_size, REBXYF abs_oft, REBXYI clip_oft, REBXYI clip_siz);
void Host_Crash(const char *reason);
void put_image(Display *display,
			   Drawable drawable,
			   GC gc,
			   XImage * image,
			   int w,
			   int h,
			   pixmap_format_t sys_pixmap_format);
//***** Macros *****
#define GOB_HWIN(gob)	((host_window_t*)Find_Window(gob))

//***** Locals *****

static REBXYF Zero_Pair = {0, 0};

typedef struct {
	REBINT left;
	REBINT top;
	REBINT right;
	REBINT bottom;
} REBRECT;

//NOTE: Following structure holds just basic compositor 'instance' values that
//are used internally by the compositor API.
//None of the values should be accessed directly from external code.
//The structure can be extended/modified according to the specific backend needs.
typedef struct rebcmp_ctx {
	REBYTE *Window_Buffer;
	REBXYI winBufSize;
	REBGOB *Win_Gob;
	REBGOB *Root_Gob;
	REBXYF absOffset; //Offset of current gob, relative to the gob passed to rebcmp_compose 
	host_window_t *host_window;
	GC	   x_gc;
	XImage *x_image;
#ifdef USE_XSHM
	XImage *x_image_back;
	XShmSegmentInfo x_shminfo;
	XShmSegmentInfo x_shminfo_back;
#endif
	pixmap_format_t pixmap_format;
	REBYTE *pixbuf;
	REBCNT pixbuf_len;
	Region Win_Region;
	XRectangle Win_Clip;
	XRectangle New_Clip;
	XRectangle Old_Clip;
} REBCMP_CTX;

/***********************************************************************
**
*/ REBYTE* rebcmp_get_buffer(REBCMP_CTX* ctx)
/*
**	Provide pointer to window compositing buffer.
**  Return NULL if buffer not available of call failed.
**
**  NOTE: The buffer may be "locked" during this call on some platforms.
**        Always call rebcmp_release_buffer() to be sure it is released.
**
***********************************************************************/
{
	//memset(ctx->pixbuf, 0, ctx->pixbuf_len);
	return ctx->pixbuf;
}

/***********************************************************************
**
*/ void rebcmp_release_buffer(REBCMP_CTX* ctx)
/*
**	Release the window compositing buffer acquired by rebcmp_get_buffer().
**
**  NOTE: this call can be "no-op" on platforms that don't need locking.
**
***********************************************************************/
{
}

#ifdef USE_XSHM
static int (*orig_error_handler)(Display *, XErrorEvent *);

static int shm_error_handler(Display *d, XErrorEvent *e) {
    if(e->error_code == BadAccess) {
	global_x_info->has_xshm = 0;
        return 0;
    } else
        return (*orig_error_handler)(d, e);
}
#endif

/***********************************************************************
**
*/ REBOOL rebcmp_resize_buffer(REBCMP_CTX* ctx, REBGOB* winGob)
/*
**	Resize the window compositing buffer.
**
**  Returns TRUE if buffer size was really changed, otherwise FALSE.
**
***********************************************************************/
{

	//check if window size really changed
	if ((GOB_LOG_W(winGob) != GOB_WO(winGob)) || (GOB_LOG_H(winGob) != GOB_HO(winGob))
		|| ctx->pixbuf == NULL) {//ctx might haven't been initialized yet

		REBINT w = GOB_LOG_W_INT(winGob);
		REBINT h = GOB_LOG_H_INT(winGob);

		if (ctx->x_image) {
			XDestroyImage(ctx->x_image); //frees ctx->pixbuf as well
		}
#ifdef USE_XSHM
		if (ctx->x_image_back) {
			XDestroyImage(ctx->x_image_back); //frees ctx->pixbuf as well
		}
		if (global_x_info->has_xshm
			&& global_x_info->sys_pixmap_format == pix_format_bgra32) {

			if (ctx->x_shminfo.shmaddr != NULL) {
				XShmDetach(global_x_info->display, &ctx->x_shminfo);
				shmdt(ctx->x_shminfo.shmaddr);
				//RL_Print("Removing SHM %x\n", ctx->x_shminfo.shmid);
				shmctl(ctx->x_shminfo.shmid, IPC_RMID, NULL);
			}
			ctx->x_image = XShmCreateImage(global_x_info->display,
										   global_x_info->default_visual,
										   global_x_info->default_depth,
										   ZPixmap,
										   0,
										   &ctx->x_shminfo,
										   w, h);

			if (ctx->x_image == NULL) {
				global_x_info->has_xshm = 0;
			} else {
				ctx->pixbuf_len = ctx->x_image->bytes_per_line * ctx->x_image->height;
				ctx->x_shminfo.shmid = shmget(IPC_PRIVATE,
											  ctx->pixbuf_len,
											  IPC_CREAT | 0644 );
				//RL_Print("Allocated SHM %x\n", ctx->x_shminfo.shmid);
				if (ctx->x_shminfo.shmid < 0) {
					//RL_Print("shmget failed, fallback to non-shm\n");
					global_x_info->has_xshm = 0;
				} else {
					ctx->pixbuf = ctx->x_shminfo.shmaddr = ctx->x_image->data
						= (char *)shmat(ctx->x_shminfo.shmid, 0, 0);
				}
				if (ctx->pixbuf == NULL) {
					//RL_Print("shmat failed, fallback to non-shm\n");
					global_x_info->has_xshm = 0;
					//RL_Print("Removing SHM %x\n", ctx->x_shminfo.shmid);
					shmctl(ctx->x_shminfo.shmid, IPC_RMID, NULL);
				} else {
					memset(ctx->pixbuf, 0, ctx->pixbuf_len);
					ctx->x_shminfo.readOnly = False;
					XSync(global_x_info->display, False);
					orig_error_handler = XSetErrorHandler(shm_error_handler);
					XShmAttach(global_x_info->display, &ctx->x_shminfo); //Bad Access error when talking to a remote X server
					XSync(global_x_info->display, False);
					XSetErrorHandler(orig_error_handler);
					if (!global_x_info->has_xshm) {
						//RL_Print("XShmAttach failed, fallback to non-shm\n");
						XDestroyImage(ctx->x_image);
						shmdt(ctx->x_shminfo.shmaddr);
						//RL_Print("Removing SHM %x\n", ctx->x_shminfo.shmid);
						shmctl(ctx->x_shminfo.shmid, IPC_RMID, NULL);
					};
				}
			}
			if (global_x_info->has_xshm) {
				if (ctx->x_shminfo_back.shmaddr != NULL) {
					XShmDetach(global_x_info->display, &ctx->x_shminfo_back);
					shmdt(ctx->x_shminfo_back.shmaddr);
					//RL_Print("Removing SHM %x\n", ctx->x_shminfo_back.shmid);
					shmctl(ctx->x_shminfo_back.shmid, IPC_RMID, NULL);
				}
				ctx->x_image_back = XShmCreateImage(global_x_info->display,
											   global_x_info->default_visual,
											   global_x_info->default_depth,
											   ZPixmap,
											   0,
											   &ctx->x_shminfo_back,
											   w, h);
				assert(ctx->x_image_back != NULL);

				ctx->x_shminfo_back.shmid = shmget(IPC_PRIVATE,
											  ctx->x_image_back->bytes_per_line * ctx->x_image->height,
											  IPC_CREAT | 0644 );
				ctx->x_shminfo_back.shmaddr = ctx->x_image_back->data
					= (char *)shmat(ctx->x_shminfo_back.shmid, 0, 0);
				XShmAttach(global_x_info->display, &ctx->x_shminfo_back); //Bad Access error when talking to a remote X server
				XSync(global_x_info->display, False);
			}
		}

		if (!global_x_info->has_xshm
			|| global_x_info->sys_pixmap_format != pix_format_bgra32) {//fall back to non-xshm version
			global_x_info->has_xshm = 0;
#endif
			//RL_Print("Non-shm version\n");
			ctx->pixbuf_len = w * h * BYTE_PER_PIXEL; //BGRA32;
			ctx->pixbuf = OS_Make(ctx->pixbuf_len);
			if (ctx->pixbuf == NULL){
				//RL_Print("Allocation of %d bytes memory failed\n", ctx->pixbuf_len);
				Host_Crash("Not enough memory\n");
			}
			memset(ctx->pixbuf, 0, ctx->pixbuf_len);
			if (global_x_info->display != NULL) {
				ctx->x_image = XCreateImage(global_x_info->display,
										  global_x_info->default_visual,
										  global_x_info->default_depth,
										  ZPixmap,
										  0,
										  ctx->pixbuf,
										  w, h,
										  global_x_info->bpp,
										  w * global_x_info->bpp / 8);
			}
#ifdef USE_XSHM
		}
#endif

		if (ctx->x_image != NULL) {
#ifdef ENDIAN_BIG
			ctx->x_image->byte_order = MSBFirst;
#else
			ctx->x_image->byte_order = LSBFirst;
#endif
		}
		//update the buffer size values
		ctx->winBufSize.x = w;
		ctx->winBufSize.y = h;

		//update old gob area
		GOB_XO(winGob) = GOB_LOG_X(winGob);
		GOB_YO(winGob) = GOB_LOG_Y(winGob);
		GOB_WO(winGob) = GOB_LOG_W(winGob);
		GOB_HO(winGob) = GOB_LOG_H(winGob);
		return TRUE;
	}
	return FALSE;
}

/***********************************************************************
**
*/ void* rebcmp_create(REBGOB* rootGob, REBGOB* gob)
/*
**	Create new Compositor instance.
**
***********************************************************************/
{
	//new compositor struct
	REBCMP_CTX *ctx = (REBCMP_CTX*)OS_Make(sizeof(REBCMP_CTX));

	memset(ctx, 0, sizeof(REBCMP_CTX));

	//shortcuts
	ctx->Root_Gob = rootGob;
	ctx->Win_Gob = gob;

	//initialize clipping regions
	ctx->Win_Clip.x = 0;
	ctx->Win_Clip.y = 0;
	ctx->Win_Clip.width = GOB_LOG_W_INT(gob);
	ctx->Win_Clip.height = GOB_LOG_H_INT(gob);

	host_window_t *hw = GOB_HWIN(gob);
	if (hw != NULL) {
		ctx->host_window = hw;
		ctx->x_gc = XCreateGC(global_x_info->display, ctx->host_window->x_id, 0, 0);
		int screen_num = DefaultScreen(global_x_info->display);
		unsigned long black = BlackPixel(global_x_info->display, screen_num);
		unsigned long white = WhitePixel(global_x_info->display, screen_num);
		XSetBackground(global_x_info->display, ctx->x_gc, white);
		XSetForeground(global_x_info->display, ctx->x_gc, black);
	}
	
	//call resize to init buffer
	rebcmp_resize_buffer(ctx, gob);
	return ctx;
}

/***********************************************************************
**
*/ void rebcmp_destroy(REBCMP_CTX* ctx)
/*
**	Destroy existing Compositor instance.
**
***********************************************************************/
{
#ifdef USE_XSHM
	if (global_x_info->has_xshm) {
		if (ctx->x_shminfo.shmid != 0) {
			XShmDetach(global_x_info->display, &ctx->x_shminfo);
			if (ctx->x_shminfo.shmaddr != NULL) {
				shmdt(ctx->x_shminfo.shmaddr);
			}
			//RL_Print("Removing SHM %x\n", ctx->x_shminfo.shmid);
			shmctl(ctx->x_shminfo.shmid, IPC_RMID, NULL);
		}
		if (ctx->x_shminfo_back.shmid != 0) {
			XShmDetach(global_x_info->display, &ctx->x_shminfo_back);
			if (ctx->x_shminfo_back.shmaddr != NULL) {
				shmdt(ctx->x_shminfo_back.shmaddr);
			}
			//RL_Print("Removing SHM %x\n", ctx->x_shminfo.shmid);
			shmctl(ctx->x_shminfo_back.shmid, IPC_RMID, NULL);
		}
		if (ctx->x_image_back) {
			XDestroyImage(ctx->x_image_back);
		}
	}
#endif
	if (ctx->x_image) {
		XDestroyImage(ctx->x_image); //frees ctx->pixbuf as well
	} else { //no display
		OS_Free(ctx->pixbuf);
	}

	if (ctx->x_gc != 0) {
		XFreeGC(global_x_info->display, ctx->x_gc);
	}

	if (ctx->Win_Region) {
		XDestroyRegion(ctx->Win_Region);
	}
	OS_Free(ctx);
}

/***********************************************************************
**
*/ static void process_gobs(REBCMP_CTX* ctx, REBGOB* gob)
/*
**	Recursively process and compose gob and its children.
**
** NOTE: this function is used internally by rebcmp_compose() call only.
**
***********************************************************************/
{
	//RL_Print("process_gobs: %x\n", gob);

	REBINT x = ROUND_TO_INT(ctx->absOffset.x);
	REBINT y = ROUND_TO_INT(ctx->absOffset.y);
	REBYTE* color;

	if (GET_GOB_STATE(gob, GOBS_NEW)){
		//reset old-offset and old-size if newly added
		GOB_XO(gob) = GOB_LOG_X(gob);
		GOB_YO(gob) = GOB_LOG_Y(gob);
		GOB_WO(gob) = GOB_LOG_W(gob);
		GOB_HO(gob) = GOB_LOG_H(gob);

		CLR_GOB_STATE(gob, GOBS_NEW);
	}

	//intersect gob dimensions with actual window clip region
	REBOOL valid_intersection = 1;

	
	//------------------------------
	//Put backend specific code here
	//------------------------------
	XRectangle rect;
	rect.x = x;
	rect.y = y;
	rect.width = GOB_LOG_W(gob);
	rect.height = GOB_LOG_H(gob);
	/*
	RL_Print("gob        , left: %d,\ttop: %d,\tright: %d,\tbottom: %d\n",
			 rect.x,
			 rect.y,
			 rect.x + rect.width,
			 rect.y + rect.height);
	*/

	Region reg = XCreateRegion();
	XUnionRectWithRegion(&rect, reg, reg);
	/*
	XClipBox(ctx->Win_Region, &rect);
	RL_Print("Win Region , left: %d,\ttop: %d,\tright: %d,\tbottom: %d\n",
			 rect.x,
			 rect.y,
			 rect.x + rect.width,
			 rect.y + rect.height);
	*/
	XIntersectRegion(reg, ctx->Win_Region, reg);
	XClipBox(reg, &rect);
	/*
	RL_Print("Win and Gob, left: %d,\ttop: %d,\tright: %d,\tbottom: %d\n",
			 rect.x,
			 rect.y,
			 rect.x + rect.width,
			 rect.y + rect.height);
			 */

	//get the current Window clip box
	REBRECT gob_clip = {
		rect.x, //left
		rect.y, //top
		rect.width + rect.x, //right
		rect.height + rect.y //bottom
			/*
		GOB_LOG_X(gob), //left
		GOB_LOG_Y(gob), //top
		GOB_LOG_W(gob) + GOB_LOG_X(gob), //right
		GOB_LOG_H(gob) + GOB_LOG_Y(gob), //bottom
		*/
	};

	//RL_Print("Window_Buffer: 0x%x\n", ctx->Window_Buffer);
	/*
	RL_Print("gob clip   , left: %d,\ttop: %d,\tright: %d,\tbottom: %d\n",
			 gob_clip.left,
			 gob_clip.top,
			 gob_clip.right,
			 gob_clip.bottom);
			 */
	if (!XEmptyRegion(reg))
	//if (valid_intersection)
	{
		//render GOB content
		switch (GOB_TYPE(gob)) {
			case GOBT_COLOR:
				//------------------------------
				//Put backend specific code here
				//------------------------------
				// or use the similar draw api call:
				//RL_Print("Draw Color at: %d, %d\n", x, y);
				rebdrw_gob_color(gob, ctx->Window_Buffer, ctx->winBufSize, (REBXYI){x,y}, (REBXYI){gob_clip.left, gob_clip.top}, (REBXYI){gob_clip.right, gob_clip.bottom});
				break;

			case GOBT_IMAGE:
				{
				//RL_Print("Draw Image\n");
					//------------------------------
					//Put backend specific code here
					//------------------------------
					// or use the similar draw api call:
					rebdrw_gob_image(gob, ctx->Window_Buffer, ctx->winBufSize, (REBXYI){x,y}, (REBXYI){gob_clip.left, gob_clip.top}, (REBXYI){gob_clip.right, gob_clip.bottom});
				}
				break;

			case GOBT_DRAW:
				{
				//RL_Print("Draw Draw at: %d, %d\n", x, y);
					//------------------------------
					//Put backend specific code here
					//------------------------------
					// or use the similar draw api call:
					rebdrw_gob_draw(gob, ctx->Window_Buffer ,ctx->winBufSize, (REBXYI){x,y}, (REBXYI){gob_clip.left, gob_clip.top}, (REBXYI){gob_clip.right, gob_clip.bottom});
				}
				break;

			case GOBT_TEXT:
			case GOBT_STRING:
				//RL_Print("Draw Text at: %d, %d\n", x, y);
				//------------------------------
				//Put backend specific code here
				//------------------------------
				// or use the similar draw api call:
				rt_gob_text(gob, ctx->Window_Buffer ,ctx->winBufSize,ctx->absOffset, (REBXYI){gob_clip.left, gob_clip.top}, (REBXYI){gob_clip.right, gob_clip.bottom});
				break;

			case GOBT_EFFECT:
				//RL_Print("Draw Effect\n");
				//not yet implemented
				break;
		}


		//recursively process sub GOBs
		if (GOB_PANE(gob)) {
			REBINT n;
			REBINT len = GOB_TAIL(gob);
			REBGOB **gp = GOB_HEAD(gob);

			for (n = 0; n < len; n++, gp++) {
				REBINT g_x = GOB_LOG_X(*gp);
				REBINT g_y = GOB_LOG_Y(*gp);

				//restore the "parent gob" clip region
				//------------------------------
				//Put backend specific code here
				//------------------------------

				ctx->absOffset.x += g_x;
				ctx->absOffset.y += g_y;

				process_gobs(ctx, *gp);

				ctx->absOffset.x -= g_x;
				ctx->absOffset.y -= g_y;
			}
		}
	}
	XDestroyRegion(reg);
}

static void swap_buffer(REBCMP_CTX* ctx)
{
#ifdef USE_XSHM
	if (global_x_info->has_xshm) {
		XImage *tmp = ctx->x_image;
		ctx->x_image = ctx->x_image_back;
		ctx->x_image_back = tmp;
		ctx->pixbuf = ctx->x_image->data;
		//printf("copy %d bytes from old data\n", ctx->pixbuf_len);
		/* it could be a partial rendering, so copy the old data over */
		memcpy(ctx->pixbuf, ctx->x_image_back->data, ctx->pixbuf_len);
	}
#endif
}

/***********************************************************************
**
*/ void rebcmp_compose(REBCMP_CTX* ctx, REBGOB* winGob, REBGOB* gob, REBOOL only)
/*
**	Compose content of the specified gob. Main compositing function.
**
**  If the ONLY arg is TRUE then the specified gob area will be
**  rendered to the buffer at 0x0 offset.(used by TO-IMAGE)
**
***********************************************************************/
{
	REBINT max_depth = 1000; // avoid infinite loops
	REBD32 abs_x = 0;
	REBD32 abs_y = 0;
	REBD32 abs_ox;
	REBD32 abs_oy;
	REBGOB* parent_gob = gob;
	REBINT x = GOB_LOG_X_INT(gob);
	REBINT y = GOB_LOG_Y_INT(gob);
	REBINT w = GOB_LOG_W_INT(gob);
	REBINT h = GOB_LOG_H_INT(gob);
	/*
	RL_Print("Composing gob: %x (%dx%d, %dx%d) in wingob %x\n", 
			 gob,
			 (int)GOB_LOG_X(gob),
			 (int)GOB_LOG_Y(gob),
			 GOB_W_INT(gob),
			 GOB_H_INT(gob),
			 winGob);
			 */

	//reset clip region to window area
	if (ctx->Win_Region != NULL){
		XDestroyRegion(ctx->Win_Region);
	}
	ctx->Win_Region = XCreateRegion();

	//calculate absolute offset of the gob
	while (GOB_PARENT(parent_gob) && (max_depth-- > 0) && !GET_GOB_FLAG(parent_gob, GOBF_WINDOW))
	{
		abs_x += GOB_LOG_X(parent_gob);
		abs_y += GOB_LOG_Y(parent_gob);
		parent_gob = GOB_PARENT(parent_gob);
	}

	assert(max_depth > 0);

	//the offset is shifted to render given gob at offset 0x0 (used by TO-IMAGE)
	if (only){
		ctx->absOffset.x = -abs_x;
		ctx->absOffset.y = -abs_y;
		abs_x = 0;
		abs_y = 0;
	} else {
		ctx->absOffset.x = 0;
		ctx->absOffset.y = 0;
	}

	ctx->New_Clip.x = abs_x;
	ctx->New_Clip.y = abs_y;
	ctx->New_Clip.width = GOB_LOG_W_INT(gob);
	ctx->New_Clip.height = GOB_LOG_H_INT(gob);

	//handle newly added gob case
	if (!GET_GOB_STATE(gob, GOBS_NEW)){
		//calculate absolute old offset of the gob
		abs_ox = abs_x + (GOB_XO(gob) - GOB_LOG_X(gob));
		abs_oy = abs_y + (GOB_YO(gob) - GOB_LOG_Y(gob));

		//set region with old gob location and dimensions
		ctx->Old_Clip.x = abs_ox;
		ctx->Old_Clip.y = abs_oy;
		ctx->Old_Clip.width = GOB_WO_INT(gob);
		ctx->Old_Clip.height = GOB_HO_INT(gob);
		XUnionRectWithRegion(&ctx->Old_Clip, ctx->Win_Region, ctx->Win_Region);
		//RL_Print("OLD: %dx%d %dx%d\n",(REBINT)abs_ox, (REBINT)abs_oy, (REBINT)abs_ox + GOB_WO_INT(gob), (REBINT)abs_oy + GOB_HO_INT(gob));
	}
	//RL_Print("NEW: %dx%d %dx%d\n",(REBINT)abs_x, (REBINT)abs_y, (REBINT)abs_x + GOB_LOG_W_INT(gob), (REBINT)abs_y + GOB_LOG_H_INT(gob));

	//Create union of "new" and "old" gob location
	XUnionRectWithRegion(&ctx->New_Clip, ctx->Win_Region, ctx->Win_Region);
	/*
	XClipBox(ctx->Win_Region, &win_rect);
	RL_Print("Old+New, %dx%d,%dx%d\n",
			 win_rect.x,
			 win_rect.y,
			 win_rect.x + win_rect.width,
			 win_rect.y + win_rect.height);
			 */

	if (!XEmptyRegion(ctx->Win_Region))
	{
		swap_buffer(ctx);
		ctx->Window_Buffer = rebcmp_get_buffer(ctx);
		if (gob == winGob) {
			memset(ctx->Window_Buffer, 0, ctx->pixbuf_len);
		}

		//redraw gobs
		process_gobs(ctx, winGob);

		rebcmp_release_buffer(ctx);

		ctx->Window_Buffer = NULL;
	}

	//update old GOB area
	GOB_XO(gob) = GOB_LOG_X(gob);
	GOB_YO(gob) = GOB_LOG_Y(gob);
	GOB_WO(gob) = GOB_LOG_W(gob);
	GOB_HO(gob) = GOB_LOG_H(gob);
}


void rebcmp_blit_region(REBCMP_CTX* ctx, Region reg)
{
	//RL_Print("rebcmp_blit_region, ctx: %x\n", ctx);
	XSetRegion(global_x_info->display, ctx->x_gc, reg);
	/*
	XRectangle rect;
	XClipBox(ctx->Win_Region, &rect);
	RL_Print("Setting window region at: %dx%d, size:%dx%d\n",
			 rect.x, rect.y, rect.width, rect.height);
			 */

#ifdef USE_XSHM
	if (global_x_info->has_xshm) {
		//RL_Print("XshmPutImage\n");
		XShmPutImage(global_x_info->display, 
				ctx->host_window->x_id, 
				ctx->x_gc, 
				ctx->x_image,
				0, 0, 	//src x, y
				0, 0, 	//dest x, y
				ctx->x_image->width, ctx->x_image->height,
				False);
		XFlush(global_x_info->display); //x_image could change if we don't flush here
	} else {
#endif
		Drawable dest = global_x_info->has_double_buffer ? 
			ctx->host_window->x_back_buffer :
			ctx->host_window->x_id;

		if (global_x_info->sys_pixmap_format == pix_format_bgra32){
			XPutImage (global_x_info->display,
					dest,
					ctx->x_gc,
					ctx->x_image,
					0, 0,	//src x, y
					0, 0,	//dest x, y
					ctx->x_image->width, ctx->x_image->height);
		} else {
			put_image(global_x_info->display,
					dest,
					ctx->x_gc,
					ctx->x_image,
					ctx->x_image->width, ctx->x_image->height,
					global_x_info->sys_pixmap_format);
		}

		if (global_x_info->has_double_buffer) {
			//RL_Print("Swapping...\n");
			XdbeSwapInfo swapInfo;
			swapInfo.swap_window = ctx->host_window->x_id;
			swapInfo.swap_action = XdbeUndefined;
			XdbeSwapBuffers(global_x_info->display, &swapInfo, 1);
		}
#ifdef USE_XSHM
	}
#endif

	//RL_Print("rebcmp_blit done\n");
}

/***********************************************************************
**
*/ void rebcmp_blit(REBCMP_CTX* ctx)
/*
**	Blit window content on the screen.
**
***********************************************************************/
{
	rebcmp_blit_region(ctx, ctx->Win_Region);
}
