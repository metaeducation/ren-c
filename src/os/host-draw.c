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
**  Title: DRAW and SHAPE dialect command dispatcher
**  Author: Richard Smolak, Carl Sassenrath
**  Purpose: Evaluates DRAW commands; calls graphics functions.
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
#include "reb-host.h"
//#include "host-lib.h"

//#include "agg-draw.h"
#include "host-draw-api.h"

#define INCLUDE_EXT_DATA
#include "host-ext-draw.h"
#include "host-ext-shape.h"

//***** Externs *****

//***** Locals *****

static u32* draw_ext_words;
static u32* shape_ext_words;

/***********************************************************************
**
*/	RXIEXT int RXD_Shape(int cmd, RXIFRM *frm, REBCEC *ctx)
/*
**		DRAW command dispatcher.
**
***********************************************************************/
{
//    Reb_Print("SHAPE called\n");
    REBCNT rel = 0;

	switch (cmd) {

    case CMD_SHAPE_INIT_WORDS:
        shape_ext_words = RL_MAP_WORDS(RXA_SERIES(frm,1));
        break;

    case CMD_SHAPE_ARC_LIT:
        rel = 1;
    case CMD_SHAPE_ARC:
        rebshp_arc(
            ctx->envr,
            rel,
            RXA_LOG_PAIR(frm, 1),
            RXA_LOG_PAIR(frm, 2),
            (RXA_TYPE(frm, 3) == RXT_DECIMAL) ? RXA_DEC64(frm, 3) : RXA_INT64(frm, 3),
            RL_FIND_WORD(shape_ext_words , RXA_WORD(frm, 4)) - W_SHAPE_NEGATIVE,
            RL_FIND_WORD(shape_ext_words , RXA_WORD(frm, 5)) - W_SHAPE_SMALL
        );
        break;

    case CMD_SHAPE_CLOSE:
        rebshp_close(ctx->envr);
        break;

    case CMD_SHAPE_CURV_LIT:
        rel = 1;
    case CMD_SHAPE_CURV:
        {
			RXIARG val[2];
			REBCNT type;
			REBCNT n, m = 0;
			REBSER blk = RXA_SERIES(frm, 1);

			for (n = 0; type = RL_GET_VALUE(blk, n, &val[m]); n++) {
			    if (type == RXT_PAIR && ++m == 2) {
//                    rebshp_curv(ctx->envr, rel, val[0].pair, val[1].pair);
					rebshp_curv(ctx->envr, rel, RXI_LOG_PAIR(val[0]), RXI_LOG_PAIR(val[1]));
                    m = 0;
			    }
			}
        }
        break;

    case CMD_SHAPE_CURVE_LIT:
        rel = 1;
    case CMD_SHAPE_CURVE:
        {
			RXIARG val[3];
			REBCNT type;
			REBCNT n, m = 0;
			REBSER blk = RXA_SERIES(frm, 1);

			for (n = 0; type = RL_GET_VALUE(blk, n, &val[m]); n++) {
                if (type == RXT_PAIR && ++m == 3) {
//                    rebshp_curve(ctx->envr, rel, val[0].pair, val[1].pair, val[2].pair);
					rebshp_curve(ctx->envr, rel, RXI_LOG_PAIR(val[0]), RXI_LOG_PAIR(val[1]), RXI_LOG_PAIR(val[2]));
                    m = 0;
                }
			}
        }
        break;

    case CMD_SHAPE_HLINE_LIT:
        rel = 1;
    case CMD_SHAPE_HLINE:
        rebshp_hline(ctx->envr, rel, LOG_COORD_X((RXA_TYPE(frm, 1) == RXT_DECIMAL) ? RXA_DEC64(frm, 1) : RXA_INT64(frm, 1)));
        break;

    case CMD_SHAPE_LINE_LIT:
        rel = 1;
    case CMD_SHAPE_LINE:
        if (RXA_TYPE(frm, 1) == RXT_PAIR)
            rebshp_line(ctx->envr, rel, RXA_LOG_PAIR(frm, 1));
        else {
			RXIARG val;
			REBCNT type;
			REBCNT n;
			REBSER blk = RXA_SERIES(frm, 1);

			for (n = 0; type = RL_GET_VALUE(blk, n, &val); n++) {
				if (type == RXT_PAIR)
//                    rebshp_line(ctx->envr, rel, val.pair);
					rebshp_line(ctx->envr, rel, RXI_LOG_PAIR(val));
			}
        }
        break;

    case CMD_SHAPE_MOVE_LIT:
        rel = 1;
    case CMD_SHAPE_MOVE:
        rebshp_move(ctx->envr, rel, RXA_LOG_PAIR(frm, 1));
        break;

    case CMD_SHAPE_QCURV_LIT:
        rel = 1;
    case CMD_SHAPE_QCURV:
        rebshp_qcurv(ctx->envr, rel, RXA_LOG_PAIR(frm, 1));
        break;

    case CMD_SHAPE_QCURVE_LIT:
        rel = 1;
    case CMD_SHAPE_QCURVE:
        {
			RXIARG val[2];
			REBCNT type;
			REBCNT n, m = 0;
			REBSER blk = RXA_SERIES(frm, 1);

			for (n = 0; type = RL_GET_VALUE(blk, n, &val[m]); n++) {
			    if (type == RXT_PAIR && ++m == 2) {
//                    rebshp_qcurve(ctx->envr, rel, val[0].pair, val[1].pair);
					rebshp_qcurve(ctx->envr, rel, RXI_LOG_PAIR(val[0]), RXI_LOG_PAIR(val[1]));
                    m = 0;
			    }
			}
        }
        break;

    case CMD_SHAPE_VLINE_LIT:
        rel = 1;
    case CMD_SHAPE_VLINE:
        rebshp_vline(ctx->envr, rel, LOG_COORD_Y((RXA_TYPE(frm, 1) == RXT_DECIMAL) ? RXA_DEC64(frm, 1) : RXA_INT64(frm, 1)));
        break;

    default:
		return RXR_NO_COMMAND;
	}

    return RXR_UNSET;
}

/***********************************************************************
**
*/	RXIEXT int RXD_Draw(int cmd, RXIFRM *frm, REBCEC *ctx)
/*
**		DRAW command dispatcher.
**
***********************************************************************/
{
	switch (cmd) {

    case CMD_DRAW_INIT_WORDS:
        draw_ext_words = RL_MAP_WORDS(RXA_SERIES(frm,1));
        break;
    case CMD_DRAW_ANTI_ALIAS:
        rebdrw_anti_alias(ctx->envr, RXA_LOGIC(frm, 1));
        break;

	case CMD_DRAW_ARC:
		rebdrw_arc(
            ctx->envr,
            RXA_LOG_PAIR(frm, 1),
            RXA_LOG_PAIR(frm, 2),
            (RXA_TYPE(frm, 3) == RXT_DECIMAL) ? RXA_DEC64(frm, 3) : RXA_INT64(frm, 3),
            (RXA_TYPE(frm, 4) == RXT_DECIMAL) ? RXA_DEC64(frm, 4) : RXA_INT64(frm, 4),
            RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 5)) - W_DRAW_OPENED
        );
		break;

	case CMD_DRAW_ARROW:
		rebdrw_arrow(ctx->envr, RXA_PAIR(frm, 1), (RXA_TYPE(frm, 2) == RXT_NONE) ? 0 : RXA_COLOR_TUPLE(frm, 2));
		break;

	case CMD_DRAW_BOX:
		rebdrw_box(ctx->envr, RXA_LOG_PAIR(frm, 1), RXA_LOG_PAIR(frm, 2), LOG_COORD_X((RXA_TYPE(frm, 3) == RXT_DECIMAL) ? RXA_DEC64(frm, 3) : RXA_INT64(frm, 3)));
		break;

	case CMD_DRAW_CIRCLE:
		rebdrw_circle(ctx->envr, RXA_LOG_PAIR(frm, 1), RXA_LOG_PAIR(frm, 2));
		break;

	case CMD_DRAW_CLIP:
		rebdrw_clip(ctx->envr, RXA_LOG_PAIR(frm, 1), RXA_LOG_PAIR(frm, 2));
		break;

	case CMD_DRAW_CURVE:
        if (RXA_TYPE(frm, 4) == RXT_NONE)
			rebdrw_curve3(ctx->envr, RXA_LOG_PAIR(frm, 1), RXA_LOG_PAIR(frm, 2), RXA_LOG_PAIR(frm, 3));
        else
			rebdrw_curve4(ctx->envr, RXA_LOG_PAIR(frm, 1), RXA_LOG_PAIR(frm, 2), RXA_LOG_PAIR(frm, 3), RXA_LOG_PAIR(frm, 4));

		break;

	case CMD_DRAW_ELLIPSE:
		rebdrw_ellipse(ctx->envr, RXA_LOG_PAIR(frm, 1), RXA_LOG_PAIR(frm, 2));
		break;

	case CMD_DRAW_FILL_PEN:
		{
   			//REBYTE* val;
			//REBCNT type;
			//REBSER* img;

        if (RXA_TYPE(frm, 1) == RXT_TUPLE)
            rebdrw_fill_pen(ctx->envr, RXA_COLOR_TUPLE(frm, 1));
        else if (RXA_TYPE(frm, 1) == RXT_LOGIC && !RXA_LOGIC(frm,1))
            rebdrw_fill_pen(ctx->envr, 0);
        else {
            rebdrw_fill_pen_image(ctx->envr, RXA_IMAGE_BITS(frm,1), RXA_IMAGE_WIDTH(frm,1), RXA_IMAGE_HEIGHT(frm,1));
            }
        }
		break;

    case CMD_DRAW_FILL_RULE:
        rebdrw_fill_rule(ctx->envr, RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 1)));
        break;

    case CMD_DRAW_GAMMA:
        rebdrw_gamma(ctx->envr, (RXA_TYPE(frm, 1) == RXT_DECIMAL) ? RXA_DEC64(frm, 1) : RXA_INT64(frm, 1));
        break;

	case CMD_DRAW_GRAD_PEN:
        if (RXA_TYPE(frm, 7) == RXT_NONE)
            rebdrw_reset_gradient_pen(ctx->envr);
        else
            rebdrw_gradient_pen(
                ctx->envr,
                RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 1)), //type
                RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 2)), //mode
                RXA_LOG_PAIR(frm, 3), //offset
                RXA_LOG_PAIR(frm, 4), //range - (begin, end)
                (RXA_TYPE(frm, 5) == RXT_DECIMAL) ? RXA_DEC64(frm, 5) : RXA_INT64(frm, 5), // angle
                RXA_PAIR(frm, 6), // scale
                RXA_SERIES(frm, 7) // unsigned char *colors
            );
        break;

    case CMD_DRAW_IMAGE:
        if (RXA_TYPE(frm, 2) == RXT_PAIR)
            rebdrw_image(ctx->envr, RXA_IMAGE_BITS(frm,1), RXA_IMAGE_WIDTH(frm,1), RXA_IMAGE_HEIGHT(frm,1), RXA_LOG_PAIR(frm, 2));
        else {
            rebdrw_image_scale(ctx->envr, RXA_IMAGE_BITS(frm,1), RXA_IMAGE_WIDTH(frm,1), RXA_IMAGE_HEIGHT(frm,1), RXA_SERIES(frm, 2));
        }
        break;

    case CMD_DRAW_IMAGE_FILTER:
        rebdrw_image_filter(
            ctx->envr,
            RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 1)) - W_DRAW_NEAREST,
            RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 2)) - W_DRAW_RESIZE,
            (RXA_TYPE(frm, 3) == RXT_NONE) ? 1.0 : (RXA_TYPE(frm, 3) == RXT_DECIMAL) ? RXA_DEC64(frm, 3) : RXA_INT64(frm, 3)
        );
        break;

    case CMD_DRAW_IMAGE_OPTIONS:
        rebdrw_image_options(ctx->envr, (RXA_TYPE(frm, 1) == RXT_NONE) ? 0 : RXA_COLOR_TUPLE(frm, 1), RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 2)) - W_DRAW_NO_BORDER);
        break;

    case CMD_DRAW_IMAGE_PATTERN:
        rebdrw_image_pattern(ctx->envr, RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 1)) - W_DRAW_NORMAL, RXA_PAIR(frm, 2), RXA_PAIR(frm, 3));
        break;


	case CMD_DRAW_LINE:
		{
			RXIARG val[2];
			REBCNT type;
			REBCNT n, m = 0;
			REBSER blk = RXA_SERIES(frm, 1);

			for (n = 0; type = RL_GET_VALUE(blk, n, &val[m]); n++) {
				if (type == RXT_PAIR) {
				    switch (++m){
                        case 1:
                            rebshp_open(ctx->envr);
                            break;
				        case 2:
//                            rebdrw_line(ctx->envr, val[0].pair,val[1].pair);
                            rebdrw_line(ctx->envr, RXI_LOG_PAIR(val[0]),RXI_LOG_PAIR(val[1]));
                            val[0] = val[1];
                            m--;
                            break;
				    }
				}
			}
		}
		break;

	case CMD_DRAW_LINE_CAP:
		rebdrw_line_cap(ctx->envr, RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 1)) - W_DRAW_BUTT);
		break;

	case CMD_DRAW_LINE_JOIN:
		rebdrw_line_join(ctx->envr, RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 1)) - W_DRAW_MITER);
		break;

	case CMD_DRAW_LINE_WIDTH:
		rebdrw_line_width(ctx->envr, ((RXA_TYPE(frm, 1) == RXT_DECIMAL) ? RXA_DEC64(frm, 1) : RXA_INT64(frm, 1)), RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 2)) - W_DRAW_VARIABLE);
		break;

	case CMD_DRAW_LINE_PATTERN:
        if (RXA_TYPE(frm, 2) == RXT_NONE)
            rebdrw_line_pattern(ctx->envr, 0, 0);
        else {
            REBSER patterns = RXA_SERIES(frm, 2);
            REBINT len = RL_SERIES(patterns, RXI_SER_TAIL);

            if (len > 1){

                RXIARG val;
                REBCNT type;
                REBCNT n;
                REBDEC* pattern = (REBDEC*) OS_Make((len + 1) * sizeof(REBDEC)) ;

                pattern[0] = len;

                for (n = 0; type = RL_GET_VALUE(patterns, n, &val); n++) {
                    if (type == RXT_DECIMAL)
                        pattern[n+1] = LOG_COORD_X(val.dec64);
                    else if (type == RXT_INTEGER)
                        pattern[n+1] = LOG_COORD_X(val.int64);
                    else
                        break;
                }
                rebdrw_line_pattern(ctx->envr, RXA_COLOR_TUPLE(frm, 1), pattern);
            }

        }
		break;

	case CMD_DRAW_INVERT_MATRIX:
		rebdrw_invert_matrix(ctx->envr);
		break;

	case CMD_DRAW_MATRIX:
        rebdrw_matrix(ctx->envr, RXA_SERIES(frm, 1));
		break;

	case CMD_DRAW_PEN:
        if (RXA_TYPE(frm, 1) == RXT_TUPLE)
            rebdrw_pen(ctx->envr, RXA_COLOR_TUPLE(frm, 1));
        else if (RXA_TYPE(frm, 1) == RXT_LOGIC && !RXA_LOGIC(frm,1))
            rebdrw_pen(ctx->envr, 0);
        else
            rebdrw_pen_image(ctx->envr, RXA_IMAGE_BITS(frm,1), RXA_IMAGE_WIDTH(frm,1), RXA_IMAGE_HEIGHT(frm,1));
		break;

	case CMD_DRAW_POLYGON:
		{
			RXIARG val;
			REBCNT type;
			REBCNT n;
			REBSER blk = RXA_SERIES(frm, 1);

			for (n = 0; type = RL_GET_VALUE(blk, n, &val); n++) {
				if (type == RXT_PAIR) {
					if (n > 0)
//						rebdrw_add_vertex(ctx->envr, val.pair);
						rebdrw_add_vertex(ctx->envr, RXI_LOG_PAIR(val));
					else
//						rebdrw_begin_poly(ctx->envr, val.pair);
						rebdrw_begin_poly(ctx->envr, RXI_LOG_PAIR(val));
				}
			}
			rebdrw_end_poly(ctx->envr);
		}
		break;

    case CMD_DRAW_PUSH:
        {
            REBCEC innerCtx;

            innerCtx.envr = ctx->envr;
            innerCtx.block = RXA_SERIES(frm, 1);
            innerCtx.index = 0;

            rebdrw_push_matrix(ctx->envr);
            RL_Do_Commands(RXA_SERIES(frm, 1), 0, &innerCtx);
            rebdrw_pop_matrix(ctx->envr);
        }
        break;

	case CMD_DRAW_RESET_MATRIX:
		rebdrw_reset_matrix(ctx->envr);
		break;

    case CMD_DRAW_ROTATE:
        rebdrw_rotate(ctx->envr, (RXA_TYPE(frm, 1) == RXT_DECIMAL) ? RXA_DEC64(frm, 1) : RXA_INT64(frm, 1));
        break;

    case CMD_DRAW_SCALE:
        rebdrw_scale(ctx->envr, RXA_PAIR(frm, 1));
        break;

    case CMD_DRAW_SHAPE:
        {
            REBCEC innerCtx;

            innerCtx.envr = ctx->envr;
            innerCtx.block = RXA_SERIES(frm, 1);
            innerCtx.index = 0;

            rebshp_open(ctx->envr);
            RL_Do_Commands(RXA_SERIES(frm, 1), 0, &innerCtx);
            rebshp_close(ctx->envr);
        }
        break;

    case CMD_DRAW_SKEW:
        rebdrw_skew(ctx->envr, RXA_PAIR(frm, 1));
        break;

	case CMD_DRAW_SPLINE:
        {
            REBSER points = RXA_SERIES(frm, 1);
            REBINT len = RL_SERIES(points, RXI_SER_TAIL);

            if (len > 3){
                RXIARG val;
                REBCNT type;
                REBCNT n;

                for (n = 0; type = RL_GET_VALUE(points, n, &val); n++) {
                    if (type == RXT_PAIR) {
                        if (n > 0)
//                            rebdrw_add_vertex(ctx->envr, val.pair);
							rebdrw_add_vertex(ctx->envr, RXI_LOG_PAIR(val));
                        else
//                            rebdrw_begin_poly(ctx->envr, val.pair);
							rebdrw_begin_poly(ctx->envr, RXI_LOG_PAIR(val));
                    }
                }
                rebdrw_end_spline(ctx->envr, RXA_INT32(frm, 2), RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 3)) - W_DRAW_OPENED);
            }

		}
		break;

    case CMD_DRAW_TEXT:
#if defined(AGG_WIN32_FONTS) || defined(AGG_FREETYPE)	
        rebdrw_text(
            ctx->envr,
            (RL_FIND_WORD(draw_ext_words , RXA_WORD(frm, 3)) == W_DRAW_VECTORIAL) ? 1 : 0,
             &RXA_LOG_PAIR(frm, 1),
             (RXA_TYPE(frm, 2) == RXT_PAIR) ? &RXA_LOG_PAIR(frm, 2) : NULL,
             RXA_SERIES(frm, 4)
        );
#endif		
        break;

	case CMD_DRAW_TRANSFORM:
		rebdrw_transform(
            ctx->envr,
            (RXA_TYPE(frm, 1) == RXT_DECIMAL) ? RXA_DEC64(frm, 1) : RXA_INT64(frm, 1), // angle
            RXA_LOG_PAIR(frm, 2), // center
            RXA_PAIR(frm, 3), // scale
            RXA_LOG_PAIR(frm, 4) // offset
        );
        break;

    case CMD_DRAW_TRANSLATE:
        rebdrw_translate(ctx->envr, RXA_LOG_PAIR(frm, 1));
		break;

	case CMD_DRAW_TRIANGLE:
        {
            REBCNT b = 0xff000000;
            rebdrw_triangle(
                ctx->envr,
                RXA_LOG_PAIR(frm, 1), // vertex-1
                RXA_LOG_PAIR(frm, 2), // vertex-2
                RXA_LOG_PAIR(frm, 3), // vertex-3
                (RXA_TYPE(frm, 4) == RXT_NONE) ? 0 : RXA_COLOR_TUPLE(frm, 4), // color-1
                (RXA_TYPE(frm, 5) == RXT_NONE) ? b : RXA_COLOR_TUPLE(frm, 5), // color-2
                (RXA_TYPE(frm, 6) == RXT_NONE) ? b : RXA_COLOR_TUPLE(frm, 6), // color-3
                (RXA_TYPE(frm, 7) == RXT_DECIMAL) ? RXA_DEC64(frm, 7) : RXA_INT64(frm, 7) // dilation
            );
        }
		break;

	default:
		return RXR_NO_COMMAND;
	}

    return RXR_UNSET;
}
