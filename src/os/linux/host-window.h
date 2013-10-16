#ifndef _HOST_WINDOW_H_
#define _HOST_WINDOW_H_

#include  <X11/Xlib.h>
#include  <X11/Xatom.h>
#include  <X11/Xutil.h>
 
typedef struct {
	Window 	x_window;
	GC		x_gc;
	char	*pixbuf;
	REBCNT	pixbuf_len;
} host_window_t;


void* Find_Window(REBGOB *gob);
#endif
