#include  <X11/Xlib.h>
#include  <X11/Xutil.h>
#include  <assert.h>

extern "C" {
	#include "reb-host.h"
	#include "host-lib.h" //for OS_Make
	#include "host-window.h"
}
#include "util/agg_color_conv.h"
#include "util/agg_color_conv_rgb8.h"


extern "C" void put_image(Display *display,
						  Drawable drawable,
						  GC gc,
						  XImage * image,
						  int w, 
						  int h,
						  pixmap_format_t sys_pixmap_format)
{
		char *old_data = image->data;
		agg::rendering_buffer src((unsigned char*)image->data, w, h, w * 4);
		unsigned char *tmp = new unsigned char [global_x_info->bpp / 8 * w * h];
		assert(tmp != (unsigned char*) image->data);
		agg::rendering_buffer dest(tmp, w, h, w * global_x_info->bpp / 8);
		switch (sys_pixmap_format) {
			case pix_format_bgr555:
				agg::color_conv(&dest, &src, agg::color_conv_bgra32_to_rgb555());
				break;
			case pix_format_bgr565:
				agg::color_conv(&dest, &src, agg::color_conv_bgra32_to_rgb565());
				break;
			case pix_format_rgba32:
				agg::color_conv(&dest, &src, agg::color_conv_bgra32_to_rgba32());
				break;
			default:
				RL_Print((REBYTE*)"Unsupported pix_format: %d\n", sys_pixmap_format);
				delete [] tmp;
				return;
		}
		image->data = (char*)tmp;
		XPutImage(display,
				drawable,
				gc,
				image,
				0, 0,	//src x, y
				0, 0,	//dest x, y
				w, h);
		delete [] tmp;
		image->data = old_data;
}
