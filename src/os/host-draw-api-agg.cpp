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
**  Title: DRAW dialect API functions
**  Author: Richard Smolak
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

#include "../agg/agg_graphics.h"
//#undef IS_ERROR
extern "C" void *RL_Series(REBSER *ser, REBINT what);

namespace agg
{
	extern "C" RL_LIB *RL;

	extern "C" void rebdrw_add_vertex (void* gr, REBXYF p)
	{
		((agg_graphics*)gr)->agg_add_vertex(p.x, p.y);
	}

	extern "C" void rebdrw_anti_alias(void* gr, REBINT mode)
	{
		((agg_graphics*)gr)->agg_anti_alias(mode!=0);
	}

	extern "C" void rebdrw_arc(void* gr, REBXYF c, REBXYF r, REBDEC ang1, REBDEC ang2, REBINT closed)
	{
		((agg_graphics*)gr)->agg_arc(c.x, c.y, r.x, r.y, ang1, ang2, closed);
	}

	extern "C" void rebdrw_arrow(void* gr, REBXYF mode, REBCNT col)
	{
		((agg_graphics*)gr)->agg_arrows((col) ? (REBYTE*)&col : NULL, (REBINT)mode.x, (REBINT)mode.y);
	}

	extern "C" void rebdrw_begin_poly (void* gr, REBXYF p)
	{
		((agg_graphics*)gr)->agg_begin_poly(p.x, p.y);
	}

	extern "C" void rebdrw_box(void* gr, REBXYF p1, REBXYF p2, REBDEC r)
	{
		if (r) {
			((agg_graphics*)gr)->agg_rounded_rect(p1.x, p1.y, p2.x, p2.y, r);
		} else {
			((agg_graphics*)gr)->agg_box(p1.x, p1.y, p2.x, p2.y);
		}
	}

	extern "C" void rebdrw_circle(void* gr, REBXYF p, REBXYF r)
	{
		((agg_graphics*)gr)->agg_ellipse(p.x, p.y, r.x, r.y);
	}

	extern "C" void rebdrw_clip(void* gr, REBXYF p1, REBXYF p2)
	{
		((agg_graphics*)gr)->agg_set_clip(p1.x, p1.y, p2.x, p2.y);
	}

	extern "C" void rebdrw_curve3(void* gr, REBXYF p1, REBXYF p2, REBXYF p3)
	{
		((agg_graphics*)gr)->agg_curve3(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
	}

	extern "C" void rebdrw_curve4(void* gr, REBXYF p1, REBXYF p2, REBXYF p3, REBXYF p4)
	{
		((agg_graphics*)gr)->agg_curve4(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y, p4.x, p4.y);
	}
									
	extern "C" REBINT rebdrw_effect(void* gr, REBPAR* p1, REBPAR* p2, REBSER* block)
	{
		return 0;
	}

	extern "C" void rebdrw_ellipse(void* gr, REBXYF p1, REBXYF p2)
	{
		REBDEC rx = p2.x / 2;
		REBDEC ry = p2.y / 2;
		((agg_graphics*)gr)->agg_ellipse(p1.x + rx, p1.y + ry, rx, ry);
	}

	extern "C" void rebdrw_end_poly (void* gr)
	{
		((agg_graphics*)gr)->agg_end_poly();
	}

	extern "C" void rebdrw_end_spline (void* gr, REBINT step, REBINT closed)
	{
		((agg_graphics*)gr)->agg_end_bspline(step, closed);
	}

	extern "C" void rebdrw_fill_pen(void* gr, REBCNT col)
	{
		if (col)
			((agg_graphics*)gr)->agg_fill_pen(((REBYTE*)&col)[0], ((REBYTE*)&col)[1], ((REBYTE*)&col)[2], ((REBYTE*)&col)[3]);
		else
			((agg_graphics*)gr)->agg_fill_pen(0, 0, 0, 0);

	}

	extern "C" void rebdrw_fill_pen_image(void* gr, REBYTE* img, REBINT w, REBINT h)
	{
#ifndef AGG_OPENGL	
		((agg_graphics*)gr)->agg_fill_pen(0, 0, 0, 255, img, w, h);
#endif
	}

	extern "C" void rebdrw_fill_rule(void* gr, REBINT mode)
	{
	    if (mode >= W_DRAW_EVEN_ODD && mode <= W_DRAW_NON_ZERO)
            ((agg_graphics*)gr)->agg_fill_rule((agg::filling_rule_e)mode);
	}

	extern "C" void rebdrw_gamma(void* gr, REBDEC gamma)
	{
		((agg_graphics*)gr)->agg_set_gamma(gamma);
	}

    extern "C" void rebdrw_gradient_pen(void* gr, REBINT gradtype, REBINT mode, REBXYF oft, REBXYF range, REBDEC angle, REBXYF scale, REBSER* colors){
#ifndef AGG_OPENGL
        unsigned char colorTuples[256*4+1] = {2, 0,0,0,255, 0,0,0,255, 255,255,255,255}; //max number of color tuples is 256 + one length information char
		REBDEC offsets[256] = {0.0 , 0.0, 1.0};

        //gradient fill
        RXIARG val;
        REBCNT type,i,j,k;
        REBDEC *matrix = new REBDEC[6];
		REBCNT *ptuples = (REBCNT*)&colorTuples[5];
		
        for (i = 0, j = 1, k = 5; type = RL_GET_VALUE(colors, i, &val); i++) {
            if (type == RXT_DECIMAL || type == RXT_INTEGER) {
                offsets[j] = (type == RXT_DECIMAL) ? val.dec64 : val.int64;

                //do some validation
                offsets[j] = MIN(MAX(offsets[j], 0.0), 1.0);
                if (j != 1 && offsets[j] < offsets[j-1])
                    offsets[j] = offsets[j-1];
                if (j != 1 && offsets[j] == offsets[j-1])
                    offsets[j-1]-= 0.0000000001;

                j++;
            } else if (type == RXT_TUPLE) {
				*ptuples++ = RXI_COLOR_TUPLE(val);
                k+=4;
            }
        }

        //sanity checks
        if (j == 1) offsets[0] = -1;
        colorTuples[0] = MAX(2, (k - 5) / 4);

	    ((agg_graphics*)gr)->agg_gradient_pen(gradtype, oft.x, oft.y, range.x, range.y, angle, scale.x, scale.y, colorTuples, offsets, mode);
#endif		
	}

	extern "C" void rebdrw_invert_matrix(void* gr)
	{
		((agg_graphics*)gr)->agg_invert_mtx();
	}

	extern "C" void rebdrw_image(void* gr, REBYTE* img, REBINT w, REBINT h,REBXYF offset)
	{
#ifndef AGG_OPENGL
		if (log_size.x == 1 && log_size.y == 1)
			((agg_graphics*)gr)->agg_image(img, offset.x, offset.y, w, h);
		else {
			REBINT x = offset.x + LOG_COORD_X(w);
			REBINT y = offset.y + LOG_COORD_Y(h);
			((agg_graphics*)gr)->agg_begin_poly(offset.x, offset.y);
			((agg_graphics*)gr)->agg_add_vertex(x, offset.y);
			((agg_graphics*)gr)->agg_add_vertex(x, y);
			((agg_graphics*)gr)->agg_add_vertex(offset.x, y);
			((agg_graphics*)gr)->agg_end_poly_img(img, w, h);
		}
#endif
	}

	extern "C" void rebdrw_image_filter(void* gr, REBINT type, REBINT mode, REBDEC blur)
	{
		((agg_graphics*)gr)->agg_image_filter(type, mode, blur);
	}

	extern "C" void rebdrw_image_options(void* gr, REBCNT keyCol, REBINT border)
	{
	    if (keyCol)
            ((agg_graphics*)gr)->agg_image_options(((REBYTE*)&keyCol)[0], ((REBYTE*)&keyCol)[1], ((REBYTE*)&keyCol)[2], ((REBYTE*)&keyCol)[3], border);
        else
            ((agg_graphics*)gr)->agg_image_options(0,0,0,0, border);
	}

    extern "C" void rebdrw_image_pattern(void* gr, REBINT mode, REBXYF offset, REBXYF size){
        if (mode)
            ((agg_graphics*)gr)->agg_image_pattern(mode,offset.x,offset.y,size.x,size.y);
        else
            ((agg_graphics*)gr)->agg_image_pattern(0,0,0,0,0);
    }

	extern "C" void rebdrw_image_scale(void* gr, REBYTE* img, REBINT w, REBINT h, REBSER* points)
	{
#ifndef AGG_OPENGL	
			RXIARG a;
			REBXYF p[4];
			REBCNT type;
			REBCNT n, len = 0;

			for (n = 0; type = RL_GET_VALUE(points, n, &a); n++) {
				if (type == RXT_PAIR){
					p[len] = RXI_LOG_PAIR(a);
                    if (++len == 4) break;
				}
			}

            if (!len) return;
            if (len == 1 && log_size.x == 1 && log_size.y == 1) {
				((agg_graphics*)gr)->agg_image(img, p[0].x, p[0].y, w, h);
				return;
			}

            ((agg_graphics*)gr)->agg_begin_poly(p[0].x, p[0].y);

            switch (len) {
                case 2:
       				((agg_graphics*)gr)->agg_add_vertex(p[1].x, p[0].y);
                    ((agg_graphics*)gr)->agg_add_vertex(p[1].x, p[1].y);
                    ((agg_graphics*)gr)->agg_add_vertex(p[0].x, p[1].y);
                    break;
                case 3:
       				((agg_graphics*)gr)->agg_add_vertex(p[1].x, p[1].y);
                    ((agg_graphics*)gr)->agg_add_vertex(p[2].x, p[2].y);
                    ((agg_graphics*)gr)->agg_add_vertex(p[0].x, p[2].y);
                    break;
                case 4:
       				((agg_graphics*)gr)->agg_add_vertex(p[1].x, p[1].y);
                    ((agg_graphics*)gr)->agg_add_vertex(p[2].x, p[2].y);
                    ((agg_graphics*)gr)->agg_add_vertex(p[3].x, p[3].y);
                    break;
            }

            ((agg_graphics*)gr)->agg_end_poly_img(img, w, h);
#endif
	}

	extern "C" void rebdrw_line(void* gr, REBXYF p1, REBXYF p2)
	{
		((agg_graphics*)gr)->agg_line(p1.x, p1.y, p2.x, p2.y);
	}

	extern "C" void rebdrw_line_cap(void* gr, REBINT mode)
	{
		((agg_graphics*)gr)->agg_stroke_cap((line_cap_e)mode);
	}

	extern "C" void rebdrw_line_join(void* gr, REBINT mode)
	{
		((agg_graphics*)gr)->agg_stroke_join((line_join_e)mode);
		((agg_graphics*)gr)->agg_dash_join((line_join_e)mode);
	}

	extern "C" void rebdrw_line_pattern(void* gr, REBCNT col, REBDEC* patterns)
	{
        ((agg_graphics*)gr)->agg_line_pattern((col) ? (REBYTE*)&col : NULL, patterns);
	}

	extern "C" void rebdrw_line_width(void* gr, REBDEC width, REBINT mode)
	{
		((agg_graphics*)gr)->agg_line_width(width, mode);
	}

	extern "C" void rebdrw_matrix(void* gr, REBSER* mtx)
	{
			RXIARG val;
			REBCNT type;
			REBCNT n;
			REBDEC* matrix = new REBDEC[6];

			for (n = 0; type = RL_GET_VALUE(mtx, n, &val),n < 6; n++) {
				if (type == RXT_DECIMAL)
				    matrix[n] = val.dec64;
				else if (type == RXT_INTEGER)
				    matrix[n] = val.int64;
				else
                    return;
			}

            if (n != 6) return;

            ((agg_graphics*)gr)->agg_set_mtx(matrix);

            delete[] matrix;
	}

	extern "C" void rebdrw_pen(void* gr, REBCNT col)
	{
		if (col)
			((agg_graphics*)gr)->agg_pen(((REBYTE*)&col)[0], ((REBYTE*)&col)[1], ((REBYTE*)&col)[2], ((REBYTE*)&col)[3]);
		else
			((agg_graphics*)gr)->agg_pen(0,0,0,0);

	}

	extern "C" void rebdrw_pen_image(void* gr, REBYTE* img, REBINT w, REBINT h)
	{
#ifndef AGG_OPENGL	
		((agg_graphics*)gr)->agg_pen(0, 0, 0, 255, img, w, h);
#endif
	}

	extern "C" void rebdrw_pop_matrix(void* gr)
	{
		((agg_graphics*)gr)->agg_pop_mtx();
	}

	extern "C" void rebdrw_push_matrix(void* gr)
	{
		((agg_graphics*)gr)->agg_push_mtx();
	}

	extern "C" void rebdrw_reset_gradient_pen(void* gr)
	{
		((agg_graphics*)gr)->agg_reset_gradient_pen();
	}

	extern "C" void rebdrw_reset_matrix(void* gr)
	{
		((agg_graphics*)gr)->agg_reset_mtx();
	}

	extern "C" void rebdrw_rotate(void* gr, REBDEC ang)
	{
		((agg_graphics*)gr)->agg_rotate(ang);
	}

	extern "C" void rebdrw_scale(void* gr, REBXYF sc)
	{
		((agg_graphics*)gr)->agg_scale(sc.x, sc.y);
	}

	extern "C" void rebdrw_skew(void* gr, REBXYF angle)
	{
		((agg_graphics*)gr)->agg_skew(angle.x, angle.y);
	}
#if defined(AGG_WIN32_FONTS) || defined(AGG_FREETYPE)
	extern "C" void rebdrw_text(void* gr, REBINT mode, REBXYF* p1, REBXYF* p2, REBSER* block)
	{
#ifndef AGG_OPENGL	
		((agg_graphics*)gr)->agg_text(mode, p1, p2, block);
#endif
	}
#endif
	extern "C" void rebdrw_transform(void* gr, REBDEC ang, REBXYF ctr, REBXYF sc, REBXYF oft)
	{
		((agg_graphics*)gr)->agg_transform(ang, ctr.x, ctr.y, sc.x, sc.y, oft.x, oft.y);
	}

	extern "C" void rebdrw_translate(void* gr, REBXYF p)
	{
		((agg_graphics*)gr)->agg_translate(p.x, p.y);
	}

	extern "C" void rebdrw_triangle(void* gr, REBXYF p1, REBXYF p2, REBXYF p3, REBCNT c1, REBCNT c2, REBCNT c3, REBDEC dilation)
	{
#ifndef AGG_OPENGL	
		((agg_graphics*)gr)->agg_gtriangle(p1, p2, p3, (c1) ? (REBYTE*)&c1 : NULL, (REBYTE*)&c2, (REBYTE*)&c3, dilation);
#endif
	}


	//SHAPE functions
	extern "C" void rebshp_arc(void* gr, REBINT rel, REBXYF p, REBXYF r, REBDEC ang, REBINT sweep, REBINT large)
	{
		((agg_graphics*)gr)->agg_path_arc(rel, r.x, r.y, ang, large, sweep, p.x, p.y);
	}

	extern "C" void rebshp_close(void* gr)
	{
		((agg_graphics*)gr)->agg_path_close();
	}

	extern "C" void rebshp_curv(void* gr, REBINT rel, REBXYF p1, REBXYF p2)
	{
		((agg_graphics*)gr)->agg_path_cubic_curve_to(rel, p1.x, p1.y, p2.x, p2.y);
	}

	extern "C" void rebshp_curve(void* gr, REBINT rel, REBXYF p1,REBXYF p2, REBXYF p3)
	{
		((agg_graphics*)gr)->agg_path_cubic_curve(rel, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
	}

	extern "C" void rebshp_hline(void* gr, REBCNT rel, REBDEC x)
	{
		((agg_graphics*)gr)->agg_path_hline(rel, x);
	}

	extern "C" void rebshp_line(void* gr, REBCNT rel, REBXYF p)
	{
		((agg_graphics*)gr)->agg_path_line(rel, p.x, p.y);
	}

	extern "C" void rebshp_move(void* gr, REBCNT rel, REBXYF p)
	{
		((agg_graphics*)gr)->agg_path_move(rel, p.x, p.y);
	}

	extern "C" void rebshp_open(void* gr)
	{
		((agg_graphics*)gr)->agg_begin_path();
	}

	extern "C" void rebshp_vline(void* gr, REBCNT rel, REBDEC y)
	{
		((agg_graphics*)gr)->agg_path_vline(rel, y);
	}

	extern "C" void rebshp_qcurv(void* gr, REBINT rel, REBXYF p)
	{
		((agg_graphics*)gr)->agg_path_quadratic_curve_to(rel, p.x, p.y);
	}

	extern "C" void rebshp_qcurve(void* gr, REBINT rel, REBXYF p1, REBXYF p2)
	{
		((agg_graphics*)gr)->agg_path_quadratic_curve(rel, p1.x, p1.y, p2.x, p2.y);
	}


	extern "C" void rebdrw_to_image(REBYTE *image, REBINT w, REBINT h, REBSER *block)
	{
		REBCEC ctx;
		REBSER *args = 0;

		agg_graphics::ren_buf rbuf_win(image, w, h, w * 4);
		agg_graphics::pixfmt pixf_win(rbuf_win);
		agg_graphics::ren_base rb_win(pixf_win);

		agg_graphics* graphics = new agg_graphics(&rbuf_win, w, h, 0, 0);


		ctx.envr = graphics;
		ctx.block = block;
		ctx.index = 0;

		RL_DO_COMMANDS(block, 0, &ctx);

		graphics->agg_render(rb_win);

        delete graphics;
	}

	extern "C" void rebdrw_gob_color(REBGOB *gob, REBYTE* buf, REBXYI buf_size, REBXYI abs_oft, REBXYI clip_oft, REBXYI clip_siz)
	{
		agg_graphics::ren_buf rbuf_win(buf, buf_size.x, buf_size.y, buf_size.x << 2);
		agg_graphics::pixfmt pixf_win(rbuf_win);
		agg_graphics::ren_base rb_win(pixf_win);
		
		REBYTE* color = (REBYTE*)&GOB_CONTENT(gob);
		
		rb_win.clip_box(clip_oft.x,clip_oft.y,clip_siz.x-1,clip_siz.y-1);
		
		if ((GOB_ALPHA(gob) == 255) && (color[C_A] == 255))
			rb_win.copy_bar(abs_oft.x, abs_oft.y, abs_oft.x+GOB_LOG_W_INT(gob), abs_oft.y+GOB_LOG_H_INT(gob), agg::rgba8(color[C_R], color[C_G], color[C_B], color[C_A]));
		else
			rb_win.blend_bar(abs_oft.x, abs_oft.y, abs_oft.x+GOB_LOG_W_INT(gob), abs_oft.y+GOB_LOG_H_INT(gob), agg::rgba8(color[C_R], color[C_G], color[C_B], color[C_A]), GOB_ALPHA(gob));
	}

	extern "C" void rebdrw_gob_image(REBGOB *gob, REBYTE* buf, REBXYI buf_size, REBXYI abs_oft, REBXYI clip_oft, REBXYI clip_siz)
	{
		//FIXME: temporary hack for getting image w,h
		u16* d = (u16*)GOB_CONTENT(gob);
		int w = d[8];
		int h = d[9];

		agg_graphics::ren_buf rbuf_win(buf, buf_size.x, buf_size.y, buf_size.x << 2);
		agg_graphics::pixfmt pixf_win(rbuf_win);
		agg_graphics::ren_base rb_win(pixf_win);
		
		agg_graphics::ren_buf rbuf_img((REBYTE*)GOB_BITMAP(gob),w,h,w << 2);
//		agg::pixfmt_bgra32 pixf_img(rbuf_img);
		agg_graphics::pixfmt pixf_img(rbuf_img);

		rb_win.clip_box(clip_oft.x,clip_oft.y,clip_siz.x-1,clip_siz.y-1);
		
		if (GOB_ALPHA(gob) == 255)
			rb_win.blend_from(pixf_img,0,abs_oft.x,abs_oft.y);
		else
			rb_win.blend_from(pixf_img,0,abs_oft.x,abs_oft.y, GOB_ALPHA(gob));
	}
	
	extern "C" void rebdrw_gob_draw(REBGOB *gob, REBYTE* buf, REBXYI buf_size, REBXYI abs_oft, REBXYI clip_oft, REBXYI clip_siz)
	{
		REBINT result;
	   	REBCEC ctx;
		agg_graphics *graphics;
		REBSER *block = (REBSER *)GOB_CONTENT(gob);
			
		REBINT w = clip_siz.x-clip_oft.x;
		REBINT h = clip_siz.y-clip_oft.y;
		REBINT stride = buf_size.x << 2;
		
		REBYTE* tmp_buf = 0;
		agg_graphics::ren_buf rbuf_tmp;
		agg_graphics::pixfmt pixf_tmp(rbuf_tmp);
		agg_graphics::ren_buf rbuf_win(buf, buf_size.x, buf_size.y, stride);		
		agg_graphics::pixfmt pixf_win(rbuf_win);
		agg_graphics::ren_base rb;
		agg_graphics::ren_base rb_win(pixf_win);
		agg_graphics::ren_base rb_tmp(pixf_tmp);

//		RL->print((REBYTE*)"GOB: %dx%d %dx%d\n",abs_oft.x, abs_oft.y, GOB_W_INT(gob), GOB_H_INT(gob));
//		RL->print((REBYTE*)"CLIP: %dx%d %dx%d (%dx%d)\n",clip_oft.x, clip_oft.y, clip_siz.x, clip_siz.y, w, h);

		if (GOB_ALPHA(gob) == 255){
			//render directly to the main buffer
			graphics = new agg_graphics(&rbuf_win, GOB_LOG_W_INT(gob), GOB_LOG_H_INT(gob), abs_oft.x, abs_oft.y);
			rb = rb_win;

			//!!!workaround to change agg clipping
			graphics->agg_set_buffer(&rbuf_win, clip_siz.x, clip_siz.y, clip_oft.x, clip_oft.y);
		} else {
			//create temporary buffer for later blending
			REBINT buf_len = w * h * 4;
			tmp_buf = new REBYTE [buf_len];
			rbuf_tmp.attach(tmp_buf, w, h, w * 4);
			graphics = new agg_graphics(&rbuf_tmp, w, h, 0, 0);
			rb = rb_tmp;
			rb.clip_box(0,0,w,h);
			
			//note: this copies whole background. todo: check for faster solution
			rb.copy_from(rbuf_win,0,-abs_oft.x, -abs_oft.y);
		}

		ctx.envr = graphics;
		ctx.block = block;
		ctx.index = 0;

		RL_DO_COMMANDS(block, 0, &ctx);
		graphics->agg_render(rb);

		delete graphics;

		if (tmp_buf){
			//blend with main buffer
			rb_win.blend_from(pixf_tmp,0,abs_oft.x, abs_oft.y, GOB_ALPHA(gob));

			//deallocate temoprary buffer
			delete tmp_buf;
		}
	}
}