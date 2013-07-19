#ifndef _EGL_WINDOW_H_
#define _EGL_WINDOW_H_

#include  <X11/Xlib.h>
#include  <X11/Xatom.h>
#include  <X11/Xutil.h>
 
#include  <GLES2/gl2.h>
#include <EGL/egl.h>

typedef struct {
	Window 	x_window;
	EGLSurface *egl_surface;
	EGLContext *egl_context;
	GLuint shaderProgram;
	char	*pixbuf;
} egl_window_t;


void* Find_Window(REBGOB *gob);
#endif
