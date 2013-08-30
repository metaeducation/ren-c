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
#include "reb-host.h"

#include "egl-window.h"

//***** Macros *****
#define GOB_HWIN(gob)	(Find_Window(gob))

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
typedef struct {
	REBYTE *Window_Buffer;
	REBXYI winBufSize;
	REBGOB *Win_Gob;
	REBGOB *Root_Gob;
	REBXYF absOffset;
} REBCMP_CTX;

extern EGLDisplay egl_display;

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
	egl_window_t *ew = GOB_HWIN(ctx->Win_Gob);
	//eglMakeCurrent(ew->egl_display, ew->egl_surface, ew->egl_surface, ew->egl_context );
	return ew->pixbuf;
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
	if ((GOB_PW(winGob) != GOB_WO(winGob)) || (GOB_PH(winGob) != GOB_HO(winGob))) {

		REBINT w = GOB_PW_INT(winGob);
		REBINT h = GOB_PH_INT(winGob);

		//------------------------------
		//Put backend specific code here
		//------------------------------

		//update the buffer size values
		ctx->winBufSize.x = w;
		ctx->winBufSize.y = h;

		//update old gob area
		GOB_XO(winGob) = GOB_PX(winGob);
		GOB_YO(winGob) = GOB_PY(winGob);
		GOB_WO(winGob) = GOB_PW(winGob);
		GOB_HO(winGob) = GOB_PH(winGob);
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

	//shortcuts
	ctx->Root_Gob = rootGob;
	ctx->Win_Gob = gob;

	//------------------------------
	//Put backend specific code here
	//------------------------------

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
	//------------------------------
	//Put backend specific code here
	//------------------------------
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
	RL_Print("process_gobs: %x\n", gob);

	REBINT x = ROUND_TO_INT(ctx->absOffset.x);
	REBINT y = ROUND_TO_INT(ctx->absOffset.y);
	REBYTE* color;

	if (GET_GOB_STATE(gob, GOBS_NEW)){
		//reset old-offset and old-size if newly added
		GOB_XO(gob) = GOB_PX(gob);
		GOB_YO(gob) = GOB_PY(gob);
		GOB_WO(gob) = GOB_PW(gob);
		GOB_HO(gob) = GOB_PH(gob);

		CLR_GOB_STATE(gob, GOBS_NEW);
	}

	//intersect gob dimensions with actual window clip region
	REBOOL valid_intersection = 1;
	//------------------------------
	//Put backend specific code here
	//------------------------------

	//get the current Window clip box
	REBRECT gob_clip = {
		GOB_PX(gob), //left
		GOB_PY(gob), //top
		GOB_PW(gob) + GOB_PX(gob), //right
		GOB_PH(gob) + GOB_PY(gob), //bottom
	};
	//------------------------------
	//Put backend specific code here
	//------------------------------

	RL_Print("Window_Buffer: 0x%x\n", ctx->Window_Buffer);
	if (valid_intersection)
	{
		//render GOB content
		switch (GOB_TYPE(gob)) {
			case GOBT_COLOR:
				//------------------------------
				//Put backend specific code here
				//------------------------------
				// or use the similar draw api call:
				rebdrw_gob_color(gob, ctx->Window_Buffer, ctx->winBufSize, (REBXYI){x,y}, (REBXYI){gob_clip.left, gob_clip.top}, (REBXYI){gob_clip.right, gob_clip.bottom});
				break;

			case GOBT_IMAGE:
				{
				RL_Print("Draw Image\n");
					//------------------------------
					//Put backend specific code here
					//------------------------------
					// or use the similar draw api call:
					rebdrw_gob_image(gob, ctx->Window_Buffer, ctx->winBufSize, (REBXYI){x,y}, (REBXYI){gob_clip.left, gob_clip.top}, (REBXYI){gob_clip.right, gob_clip.bottom});
				}
				break;

			case GOBT_DRAW:
				{
				RL_Print("Draw Draw\n");
					//------------------------------
					//Put backend specific code here
					//------------------------------
					// or use the similar draw api call:
					rebdrw_gob_draw(gob, ctx->Window_Buffer ,ctx->winBufSize, (REBXYI){x,y}, (REBXYI){gob_clip.left, gob_clip.top}, (REBXYI){gob_clip.right, gob_clip.bottom});
				}
				break;

			case GOBT_TEXT:
			case GOBT_STRING:
				RL_Print("Draw Text\n");
				//------------------------------
				//Put backend specific code here
				//------------------------------
				// or use the similar draw api call:
				rt_gob_text(gob, ctx->Window_Buffer ,ctx->winBufSize,ctx->absOffset, (REBXYI){gob_clip.left, gob_clip.top}, (REBXYI){gob_clip.right, gob_clip.bottom});
				break;

			case GOBT_EFFECT:
				RL_Print("Draw Effect\n");
				//not yet implemented
				break;
		}

	RL_Print("glDrawn\n");

		//recursively process sub GOBs
		if (GOB_PANE(gob)) {
			REBINT n;
			REBINT len = GOB_TAIL(gob);
			REBGOB **gp = GOB_HEAD(gob);

			for (n = 0; n < len; n++, gp++) {
				REBINT g_x = GOB_PX(*gp);
				REBINT g_y = GOB_PY(*gp);

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
	REBINT x = GOB_PX_INT(gob);
	REBINT y = GOB_PY_INT(gob);
	REBINT w = GOB_PW_INT(gob);
	REBINT h = GOB_PH_INT(gob);

	//reset clip region to window area
	//------------------------------
	//Put backend specific code here
	//------------------------------

	//calculate absolute offset of the gob
	while (GOB_PARENT(parent_gob) && (max_depth-- > 0) && !GET_GOB_FLAG(parent_gob, GOBF_WINDOW))
	{
		abs_x += GOB_PX(parent_gob);
		abs_y += GOB_PY(parent_gob);
		parent_gob = GOB_PARENT(parent_gob);
	}

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

	//handle newly added gob case
	if (!GET_GOB_STATE(gob, GOBS_NEW)){
		//calculate absolute old offset of the gob
		abs_ox = abs_x + (GOB_XO(gob) - GOB_PX(gob));
		abs_oy = abs_y + (GOB_YO(gob) - GOB_PY(gob));

		//set region with old gob location and dimensions
		//------------------------------
		//Put backend specific code here
		//------------------------------
		glViewport (x, y, w, h);
	}

	//Create union of "new" and "old" gob location
	REBOOL valid_intersection = 1;
	//------------------------------
	//Put backend specific code here
	//------------------------------

	//intersect resulting region with window clip region
	//------------------------------
	//Put backend specific code here
	//------------------------------

	if (valid_intersection)
	{
		ctx->Window_Buffer = rebcmp_get_buffer(ctx);

		//redraw gobs
		process_gobs(ctx, winGob);

		rebcmp_release_buffer(ctx);

		ctx->Window_Buffer = NULL;
	}

	//update old GOB area
	GOB_XO(gob) = GOB_PX(gob);
	GOB_YO(gob) = GOB_PY(gob);
	GOB_WO(gob) = GOB_PW(gob);
	GOB_HO(gob) = GOB_PH(gob);
}

/***********************************************************************
**
*/ void rebcmp_blit(REBCMP_CTX* ctx)
/*
**	Blit window content on the screen.
**
***********************************************************************/
{
	RL_Print("rebcmp_blit\n");
	REBINT w = GOB_PW_INT(ctx->Win_Gob);
	REBINT h = GOB_PH_INT(ctx->Win_Gob);
	egl_window_t *ew = GOB_HWIN(ctx->Win_Gob);
	GLfloat vVertices[] = {
	  	-1.0f,  -1.0f, 0.0f,
		0.0f, 0.0f,
	  	-1.0f,  1.0f, 0.0f,
		0.0f, 0.1f,
	  	1.0f,  1.0f, 0.0f,
		1.0f, 1.0f,
	  	1.0f,  -1.0f, 0.0f,
		1.0f, 0.0f,
	};
	GLuint	indices [] = {
		0, 1, 2,
		0, 2, 3
	};

	glUseProgram(ew->shaderProgram );    // and select it for usage
	//GLint color = glGetAttribLocation(ew->shaderProgram, "a_color");
	//GLfloat v_color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
	//glVertexAttrib4fv(color, v_color);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	GLint texture_id;
	glGenTextures(1, &texture_id);
	glBindTexture(GL_TEXTURE_2D, texture_id);
	RL_Print("w: %d, h: %d\n", w, h);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, ew->pixbuf);
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, ew->pixbuf);

	GLuint text0 = glGetUniformLocation(ew->shaderProgram, "s_texture");
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture_id);
	glUniform1i(text0, 0);

	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,GL_REPEAT);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,GL_NEAREST);

   	glViewport ( 0, 0, w, h);

	GLint loc = glGetAttribLocation(ew->shaderProgram, "a_position");
	glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), vVertices);
	glEnableVertexAttribArray (loc); //coordinates

	GLint sampler = glGetAttribLocation(ew->shaderProgram, "a_texture_coord");
	glVertexAttribPointer(sampler, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), vVertices);
	glEnableVertexAttribArray (sampler); //coordinates

	glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_INT, indices);
	eglSwapBuffers(egl_display, ew->egl_surface);
	RL_Print("rebcmp_blit done\n");
}
