#ifndef _HOST_WINDOW_H_
#define _HOST_WINDOW_H_

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xdbe.h>

#define REB_WINDOW_BORDER_WIDTH 0
#ifdef __cplusplus
extern "C" {
#endif
	typedef enum {
		pix_format_undefined,
		pix_format_bgr555 = 0,
		pix_format_bgr565,
		pix_format_rgba32,
		pix_format_bgra32
	} pixmap_format_t;

	typedef struct {
		Window win; /* the hidden window for copy and paste */
		int		status; /* -1 request hasn't been sent
						   0, request sent
						   1, response received */
		Atom 	property;
		void	*data;
		REBCNT	data_length;
	} selection_t;

	typedef struct x_atom_node {
		Atom				atom;
		char 				*name;
		struct x_atom_node 	*next;
	} x_atom_node_t;

	typedef struct {
		struct x_atom_node *next;
	} x_atom_list_t;

	Atom x_atom_list_find_atom(x_atom_list_t *list,
							   Display *display,
							   const char* atom_name,
							   unsigned char only_if_exists);

	void x_atom_list_free(x_atom_list_t *list);

	typedef struct {
		Display *display;
		Screen 	*default_screen;
		Visual 	*default_visual;
		int 	default_depth;
		int		bpp;
		pixmap_format_t sys_pixmap_format;
		selection_t selection; /* for copy and paste */
		Window leader_window;
		x_atom_list_t *x_atom_list;
		Atom	*net_supported;
		long	n_net_supported; /* size of net_supported */
#ifdef USE_XSHM
		REBOOL  has_xshm;
#endif
		REBOOL has_double_buffer;
	} x_info_t;

	typedef struct {
		Window 			x_id;
		Window 			x_parent_id;
		Region 			exposed_region;
		XdbeBackBuffer		x_back_buffer;
		unsigned int	old_width;
		unsigned int	old_height;
		unsigned int	window_flags; /* last window flags from _NET_WM_STATE */
		unsigned char	mapped; /* windows has to be mapped before calling PutImage */
	} host_window_t;

	extern x_info_t *global_x_info;

	void* Find_Window(REBGOB *gob);
	REBGOB *Find_Gob_By_Window(Window win);
	void OS_Init_Windows();
	void OS_Update_Window(REBGOB *gob);
	void* OS_Open_Window(REBGOB *gob);
	void OS_Close_Window(REBGOB *gob);

#ifdef __cplusplus
}
#endif

#endif
