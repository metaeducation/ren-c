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
**  Title: <platform> Windowing support
**  Author: Richard Smolak
**  File:  host-window.c
**  Purpose: Provides functions for windowing.
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

#include <math.h>
#include <unistd.h>
#include "reb-host.h"
#include "host-compositor.h"

#include "host-lib.h"
#include  <X11/Xlib.h>
#include  <X11/Xatom.h>
#include  <X11/Xutil.h>
 
#include  <GLES2/gl2.h>
#include <EGL/egl.h>

#include "egl-window.h"

//***** Constants *****

#define GOB_HWIN(gob)	(Find_Window(gob))
#define GOB_COMPOSITOR(gob)	(Find_Compositor(gob)) //gets handle to window's compositor

//***** Externs *****
extern REBGOBWINDOWS *Gob_Windows;
extern void Free_Window(REBGOB *gob);
extern void* Find_Compositor(REBGOB *gob);
extern REBINT Alloc_Window(REBGOB *gob);
extern void Draw_Window(REBGOB *wingob, REBGOB *gob);

Display *x_display;
EGLDisplay egl_display;
//***** Locals *****

#define MAX_WINDOWS 64 //must be in sync with os/host-view.c

REBGOB *Find_Gob_By_Window(Window win)
{
	int i = 0;
	for(i = 0; i < MAX_WINDOWS; i ++ ){
		if (((egl_window_t*)Gob_Windows[i].win)->x_window == win){
			return Gob_Windows[i].gob;
		}
	}
	return NULL;
}

static REBXYF Zero_Pair = {0, 0};
const char vertex_src [] =
	// uniforms used by the vertex shader
	//"uniform mat4 u_mvp_matrix; 							\n"
	// matrix to convert P from	model space to clip space.
	// attributes input to the vertex shader
	"attribute vec4 a_position;								\n"
	// input position value
	//"attribute vec4 a_color;								\n"
	"attribute vec2 a_texture_coord;						\n"
	// input vertex color
	// varying variables – input to the fragment shader
	//"varying vec4	v_color;								\n" // output vertex color
	"varying vec2	v_texture_coord;						\n"
	"void													\n"
	"main()													\n"
	"{														\n"
		//"v_color = a_color;									\n"
		"v_texture_coord = a_texture_coord;					\n"
		//"gl_Position = u_mvp_matrix * a_position;			\n"
		"gl_Position = a_position;							\n"
	"}														\n";

const char fragment_src [] =
	"precision mediump float; 											\n"
	"uniform sampler2D s_texture;										\n"
	"varying vec2 v_texture_coord;										\n"
	//"varying vec4 v_color;												\n"
	"void main()														\n"
	"{																	\n"
	//"	gl_FragColor =  v_color;										\n"
	//"	gl_FragColor =  vec4(1.0, 0.0, 0.0, 1.0);						\n"
	//"	gl_FragColor = texture2D(s_texture, v_texture_coord) * v_color;	\n"
	"	gl_FragColor = texture2D(s_texture, v_texture_coord);			\n"
	"}																	\n";

void
print_shader_info_log (
   GLuint  shader      // handle to the shader
)
{
}

GLuint
load_shader (
   const char  *shader_source,
   GLenum       type
)
{
   GLuint  shader = glCreateShader( type );
   GLint compiled = 0;
 
   glShaderSource  ( shader , 1 , &shader_source , NULL );
   glCompileShader ( shader );
 
   // Check the compile status
   glGetShaderiv ( shader, GL_COMPILE_STATUS, &compiled );

   if ( !compiled ) 
   {
      GLint infoLen = 0;

      glGetShaderiv ( shader, GL_INFO_LOG_LENGTH, &infoLen );
      
      if ( infoLen > 1 )
      {
         char* infoLog = OS_Make (sizeof(char) * infoLen );

         glGetShaderInfoLog ( shader, infoLen, NULL, infoLog );
         RL_Print ( "Error compiling shader:\n%s\n", infoLog );            
         
         OS_Free( infoLog );
      }

      glDeleteShader ( shader );
      return 0;
   }

   return shader;
}
//**********************************************************************
//** OSAL Library Functions ********************************************
//**********************************************************************

/***********************************************************************
**
*/	void OS_Init_Windows()
/*
**		Initialize special variables of the graphics subsystem.
**
***********************************************************************/
{
	x_display = XOpenDisplay(NULL);
	if (x_display == NULL){
		RL_Print("XOpenDisplay failed");
	}else{
		RL_Print("XOpenDisplay succeeded: x_dislay = %x\n", x_display);
	}

	egl_display = eglGetDisplay((NativeDisplayType)x_display);
	if (egl_display == EGL_NO_DISPLAY){
		RL_Print("NO EGL DISPLAY\n");
	} else {
		RL_Print("EGL DISPLAY is obtained\n");
	}
	EGLBoolean inited = eglInitialize(egl_display, NULL, NULL);
	if (inited == EGL_TRUE){
		RL_Print("EGL initialization succeeded\n");
	} else {
		RL_Print("EGL initialization failed\n");
	}
}

/***********************************************************************
**
*/	void OS_Update_Window(REBGOB *gob)
/*
**		Update window parameters.
**
***********************************************************************/
{
	RL_Print("updating window:");
	REBINT x = GOB_LOG_X_INT(gob);
	REBINT y = GOB_LOG_Y_INT(gob);
	REBINT w = GOB_LOG_W_INT(gob);
	REBINT h = GOB_LOG_H_INT(gob);
	RL_Print("x: %d, y: %d, width: %d, height: %d\n", x, y, w, h);
}

/***********************************************************************
**
*/  void* OS_Open_Window(REBGOB *gob)
/*
**		Initialize the graphics window.
**
**		The window handle is returned, but not expected to be used
**		other than for debugging conditions.
**
***********************************************************************/
{
	REBINT windex;
	REBINT x = GOB_LOG_X_INT(gob);
	REBINT y = GOB_LOG_Y_INT(gob);
	REBINT w = GOB_LOG_W_INT(gob);
	REBINT h = GOB_LOG_H_INT(gob);

	Window window;
	int screen_num;
	uint32_t mask = 0;
	uint32_t values[6];
	EGLSurface egl_surface;
	EGLContext egl_context;
	//xcb_drawable_t d;
	Window root;
	XSetWindowAttributes swa;

	egl_window_t *reb_egl_window;

	RL_Print("x: %d, y: %d, width: %d, height: %d\n", x, y, w, h);
	root = DefaultRootWindow(x_display);
	swa.event_mask  =  ExposureMask | PointerMotionMask | KeyPressMask | KeyReleaseMask| ButtonPressMask |ButtonReleaseMask | StructureNotifyMask;
	window = XCreateWindow(x_display, 
						   root,
						   x, y, w, h,
						   0,
						   CopyFromParent, InputOutput,
						   CopyFromParent, CWEventMask,
						   &swa);

	Atom wmDelete=XInternAtom(x_display, "WM_DELETE_WINDOW", 1);
	XSetWMProtocols(x_display, window, &wmDelete, 1);

	XMapWindow(x_display, window);

	windex = Alloc_Window(gob);

	if (windex < 0) Host_Crash("Too many windows");

	EGLint attr[] = {       // some attributes to set up our egl-interface
		EGL_BUFFER_SIZE, 16,
		EGL_RENDERABLE_TYPE,
		EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	EGLConfig  ecfg;
	EGLint     num_config;
	if ( !eglChooseConfig( egl_display, attr, &ecfg, 1, &num_config ) ) {
		RL_Print("Failed to choose config (eglError: %s)\n", eglGetError());
		return NULL;
	}

	if ( num_config != 1 ) {
		RL_Print("Didn't get exactly one config, but %d\n", num_config);
		return NULL;
	}

	egl_surface = eglCreateWindowSurface ( egl_display, ecfg, window, NULL );
	if ( egl_surface == EGL_NO_SURFACE ) {
		RL_Print("Unable to create EGL surface (eglError: %s)\n", eglGetError());
		return NULL;
	}

	//// egl-contexts collect all state descriptions needed required for operation
	EGLint ctxattr[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	egl_context = eglCreateContext ( egl_display, ecfg, EGL_NO_CONTEXT, ctxattr );
	if ( egl_context == EGL_NO_CONTEXT ) {
		RL_Print("Unable to create EGL context (eglError: %s)\n", eglGetError());
		return NULL;
	}
	egl_window_t *ew = OS_Make(sizeof(egl_window_t));
	ew->x_window = window;
	ew->egl_surface = egl_surface;
	ew->egl_context = egl_context;
	ew->pixbuf = OS_Make(w * h * 4); //RGB32;

	Gob_Windows[windex].win = ew;
	Gob_Windows[windex].compositor = rebcmp_create(Gob_Root, gob);


	//// associate the egl-context with the egl-surface
	if (GL_TRUE != eglMakeCurrent( egl_display, egl_surface, egl_surface, egl_context)){
		RL_Print("Unable to make context current(eglError: %s)\n", eglGetError());
	}

	///////  the openGL part  ///////////////////////////////////////////////////////////////

	GLuint vertexShader   = load_shader ( vertex_src , GL_VERTEX_SHADER  );     // load vertex shader
	GLuint fragmentShader = load_shader ( fragment_src , GL_FRAGMENT_SHADER );  // load fragment shader

	GLuint shaderProgram  = glCreateProgram ();                 // create program object
	glAttachShader ( shaderProgram, vertexShader );             // and attach both...
	glAttachShader ( shaderProgram, fragmentShader );           // ... shaders to it

	glLinkProgram ( shaderProgram );    // link the program

   // Check the link status
	GLint linked = 0;
	glGetProgramiv ( shaderProgram, GL_LINK_STATUS, &linked );

	if ( !linked ) 
	{
		GLint infoLen = 0;

		glGetProgramiv ( shaderProgram, GL_INFO_LOG_LENGTH, &infoLen );

		if ( infoLen > 1 )
		{
			char* infoLog = OS_Make (sizeof(char) * infoLen );

			glGetProgramInfoLog ( shaderProgram, infoLen, NULL, infoLog );
			RL_Print ( "Error linking program:\n%s\n", infoLog );            

			OS_Free ( infoLog );
		}

		glDeleteProgram ( shaderProgram );
		return NULL;
	}

	glClear(GL_COLOR_BUFFER_BIT );

	ew->shaderProgram = shaderProgram;
	//glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);

	CLEAR_GOB_STATE(gob);
	SET_GOB_STATE(gob, GOBS_NEW);

	SET_GOB_FLAG(gob, GOBF_WINDOW);
	SET_GOB_FLAG(gob, GOBF_ACTIVE);	
	SET_GOB_STATE(gob, GOBS_OPEN);

	return ew;
}

/***********************************************************************
**
*/  void OS_Close_Window(REBGOB *gob)
/*
**		Close the window.
**
***********************************************************************/
{
	RL_Print("Closing %x\n", gob);
	egl_window_t *win = GOB_HWIN(gob);
	eglDestroyContext (egl_display, win->egl_context );
	eglDestroySurface (egl_display, win->egl_surface );
	eglTerminate      (egl_display);
   	XDestroyWindow    (x_display, win->x_window);
	XCloseDisplay(x_display);
	OS_Free(win);
}
