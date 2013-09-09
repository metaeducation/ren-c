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
**  Title: TEXT Dialect command dispatcher
**  Author: Richard Smolak, Carl Sassenrath
**  Purpose: Evaluates rich-text commands; calls graphics functions.
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

#include <string.h>

//#include <windows.h>

#include "reb-host.h"
//#include "host-lib.h"

//#include "agg-text.h"
#include "host-text-api.h"

#define INCLUDE_EXT_DATA
#include "host-ext-text.h"

//***** Externs *****

extern REBINT As_OS_Str(REBSER *series, REBCHR **string);

//***** Locals *****

static u32* text_ext_words;

//**********************************************************************
//** Helper Functions **************************************************
//**********************************************************************

/***********************************************************************
**
*/	REBOOL As_UTF32_Str(REBSER *series, REBCHR **string)
/*
**	Convert a string series to UTF32 wide-chars.
**  If the string series is empty the resulting string is set to NULL
**
**  Function returns:
**      TRUE - if the resulting string needs to be deallocated by the caller code
**      FALSE - if REBOL string is used (no dealloc needed)
**
**  Note: REBOL strings are allowed to contain nulls.
**
***********************************************************************/
{
	int res, len, n;
	void *str;
	unsigned long *wstr;

	if ((res = RL_Get_String(series, 0, &str)) == 0) {
		*string = NULL;
		return FALSE;
	}
	
	len = abs(res);
	wstr = (unsigned long *)OS_Make((len+1) * sizeof(unsigned long));
	
	if (res < 0) {
		for (n = 0; n < len; n++)
			wstr[n] = ((REBYTE*)str)[n];
	} else {
		for (n = 0; n < len; n++){
			REBUNI ch = ((REBUNI*)str)[n];
			if (ch < 0xd800 || ch >= 0xe000)
				wstr[n] = ch;
			else
				wstr[n] = ((ch & 0x3ff) << 10) | (((REBUNI*)str)[n+1] & 0x3ff) + 0x10000;
		}
	}
	wstr[len] = 0;
	//note: following string needs be deallocated in the code that uses this function
	*string = (REBCHR*)wstr;
	return TRUE;
}

//**********************************************************************
//** Text commands! dipatcher **************************************
//**********************************************************************

/***********************************************************************
**
*/	RXIEXT int RXD_Text(int cmd, RXIFRM *frm, REBCEC *ctx)
/*
**		TEXT command dispatcher.
**
***********************************************************************/
{
	switch (cmd) {

    case CMD_TEXT_INIT_WORDS:
        text_ext_words = RL_MAP_WORDS(RXA_SERIES(frm,1));
        break;

    case CMD_TEXT_ANTI_ALIAS:
        rt_anti_alias(ctx->envr, RXA_LOGIC(frm, 1));
        break;

    case CMD_TEXT_BOLD:
        rt_bold(ctx->envr, RXA_LOGIC(frm, 1));
        break;

    case CMD_TEXT_CARET:
        {
            RXIARG val;
            u32 *words, *w;
            REBSER *obj;
            REBCNT type;
            REBXYF caret, highlightStart, highlightEnd;
            REBXYF *pcaret = 0, *phighlightStart = 0;
            obj = RXA_OBJECT(frm, 1);
//Reb_Print("RXI_WORDS_OF_OBJECT() called\n");
            words = RL_WORDS_OF_OBJECT(obj);
//Reb_Print("RXI_WORDS_OF_OBJECT() OK\n");
            w = words;

            while (type = RL_GET_FIELD(obj, w[0], &val))
            {
//                RL_Print("word: %d %d %d\n", w[0],w[1], (REBYTE)w[1]);
                switch(RL_FIND_WORD(text_ext_words,w[0]))
                {
                    case W_TEXT_CARET:
                        if (type == RXT_BLOCK){
                            REBSER* block = val.series;
                            REBINT len = RL_SERIES(block, RXI_SER_TAIL);
                            if (len > 1){
                                RXIARG pos, elem;
                                if (
                                    RL_GET_VALUE(block, 0, &pos) == RXT_BLOCK &&
                                    RL_GET_VALUE(block, 1, &elem) == RXT_STRING
                                ){
                                    caret.x = 1 + pos.index;
                                    caret.y = 1 + elem.index;
                                    pcaret = &caret;
                                }
                            }
                        }
                        break;

                    case W_TEXT_HIGHLIGHT_START:
                        if (type == RXT_BLOCK){
                            REBSER* block = val.series;
                            REBINT len = RL_SERIES(block, RXI_SER_TAIL);
                            if (len > 1){
                                RXIARG pos, elem;
                                if (
                                    RL_GET_VALUE(block, 0, &pos) == RXT_BLOCK &&
                                    RL_GET_VALUE(block, 1, &elem) == RXT_STRING
                                ){
                                    highlightStart.x = 1 + pos.index;
                                    highlightStart.y = 1 + elem.index;
                                    phighlightStart = &highlightStart;
                                }
                            }
                        }
                        break;

                    case W_TEXT_HIGHLIGHT_END:
                        if (type == RXT_BLOCK){
                            REBSER* block = val.series;
                            REBINT len = RL_SERIES(block, RXI_SER_TAIL);
                            if (len > 1){
                                RXIARG pos, elem;
                                if (
                                    RL_GET_VALUE(block, 0, &pos) == RXT_BLOCK &&
                                    RL_GET_VALUE(block, 1, &elem) == RXT_STRING
                                ){
                                    highlightEnd.x = 1 + pos.index;
                                    highlightEnd.y = 1 + elem.index;
                                }
                            }
                        }
                        break;
                }

                w++;
            }
            OS_Free(words);
            rt_caret(ctx->envr, pcaret, phighlightStart, highlightEnd);
        }

        break;

    case CMD_TEXT_CENTER:
        rt_center(ctx->envr);
        break;

    case CMD_TEXT_COLOR:
        rt_color(ctx->envr, RXA_COLOR_TUPLE(frm,1));
       break;

    case CMD_TEXT_DROP:
        rt_drop(ctx->envr, RXA_INT32(frm,1));
        break;

    case CMD_TEXT_FONT:
        {
            RXIARG val;
            u32 *words,*w;
            REBSER *obj;
            REBCNT type;
            REBFNT *font = rt_get_font(ctx->envr);

            obj = RXA_OBJECT(frm, 1);
            words = RL_WORDS_OF_OBJECT(obj);
            w = words;

            while (type = RL_GET_FIELD(obj, w[0], &val))
            {
				if (type == RXT_PAIR)
					val.pair = RXI_LOG_PAIR(val);
					
                switch(RL_FIND_WORD(text_ext_words,w[0]))
                {
                    case W_TEXT_NAME:
                        if (type == RXT_STRING){
                            font->name_free = As_OS_Str(val.series, &(font->name));
                        }
                        break;

                    case W_TEXT_STYLE:
                        switch(type)
                        {
                            case RXT_WORD:
                            {
                                u32 styleWord = RL_FIND_WORD(text_ext_words,val.int32a);
                                if (styleWord) rt_set_font_styles(font, styleWord);
                            }
                            break;

                            case RXT_BLOCK:
                            {
                                RXIARG styleVal;
                                REBCNT styleType;
                                REBCNT n;
                                u32 styleWord;
                                for (n = 0; styleType = RL_GET_VALUE(val.series, n, &styleVal); n++) {
                                    if (styleType == RXT_WORD) {
                                        styleWord = RL_FIND_WORD(text_ext_words,styleVal.int32a);
                                        if (styleWord) rt_set_font_styles(font, styleWord);
                                    }
                                }
                            }
                            break;
							
							default:
								//reset font styles
								rt_set_font_styles(font, 0);
								break;
                        }
                        break;

                    case W_TEXT_SIZE:
                        if (type == RXT_INTEGER)
                            font->size = LOG_COORD_Y(val.int64);
                        break;

                    case W_TEXT_COLOR:
                        if (type == RXT_TUPLE) {
							REBCNT col = RXI_COLOR_TUPLE(val);
							memcpy(font->color,(REBYTE*)&col , 4);
						}
                        break;

                    case W_TEXT_OFFSET:
                        if (type == RXT_PAIR) {
                            font->offset_x = val.pair.x;
                            font->offset_y = val.pair.y;
                        }
                        break;

                    case W_TEXT_SPACE:
                        if (type == RXT_PAIR) {
                            font->space_x = val.pair.x;
                            font->space_y = val.pair.y;
                        }
                        break;

                    case W_TEXT_SHADOW:
                        switch(type)
                        {
                            case RXT_PAIR:
                            {
                                font->shadow_x = val.pair.x;
                                font->shadow_y = val.pair.y;
                            }
                            break;

                            case RXT_BLOCK:
                            {
                                RXIARG shadowVal;
                                REBCNT shadowType;
                                REBCNT n;
                                for (n = 0; shadowType = RL_GET_VALUE(val.series, n, &shadowVal); n++) {
                                    switch (shadowType)
                                    {
                                        case RXT_PAIR:
											shadowVal.pair = RXI_LOG_PAIR(shadowVal);
                                            font->shadow_x = shadowVal.pair.x;
                                            font->shadow_y = shadowVal.pair.y;
                                            break;

                                        case RXT_TUPLE:
											{
												REBCNT col = RXI_COLOR_TUPLE(shadowVal);
												memcpy(font->shadow_color,(REBYTE*)&col , 4);
											}
                                            break;

                                        case RXT_INTEGER:
                                            font->shadow_blur = shadowVal.int64;
                                            break;
                                    }
                                }
                            }
                            break;
                        }
                        break;
                }

                w++;
            }
            OS_Free(words);
            rt_font(ctx->envr, font);
        }
        break;

    case CMD_TEXT_ITALIC:
        rt_italic(ctx->envr, RXA_LOGIC(frm, 1));
        break;

    case CMD_TEXT_LEFT:
        rt_left(ctx->envr);
        break;

	case CMD_TEXT_NEWLINE:
        rt_newline(ctx->envr, ctx->index + 1);
		break;

    case CMD_TEXT_PARA:
        {
            RXIARG val;
            u32 *words,*w;
            REBSER *obj;
            REBCNT type;
            REBPRA *para = rt_get_para(ctx->envr);

            obj = RXA_OBJECT(frm, 1);
            words = RL_WORDS_OF_OBJECT(obj);
            w = words;

            while (type = RL_GET_FIELD(obj, w[0], &val))
            {
				if (type == RXT_PAIR)
					val.pair = RXI_LOG_PAIR(val);
			
                switch(RL_FIND_WORD(text_ext_words,w[0]))
                {
                    case W_TEXT_ORIGIN:
                       if (type == RXT_PAIR) {
                            para->origin_x = val.pair.x;
                            para->origin_y = val.pair.y;
                        }
                        break;
                    case W_TEXT_MARGIN:
                       if (type == RXT_PAIR) {
                            para->margin_x = val.pair.x;
                            para->margin_y = val.pair.y;
                        }
                        break;
                    case W_TEXT_INDENT:
                       if (type == RXT_PAIR) {
                            para->indent_x = val.pair.x;
                            para->indent_y = val.pair.y;
                        }
                        break;
                    case W_TEXT_TABS:
                       if (type == RXT_INTEGER) {
                            para->tabs = val.int64;
                        }
                        break;
                    case W_TEXT_WRAPQ:
                       if (type == RXT_LOGIC) {
                            para->wrap = val.int32a;
                        }
                        break;
                    case W_TEXT_SCROLL:
                       if (type == RXT_PAIR) {
                            para->scroll_x = val.pair.x;
                            para->scroll_y = val.pair.y;
                        }
                        break;
                    case W_TEXT_ALIGN:
                        if (type == RXT_WORD) {
                            para->align = RL_FIND_WORD(text_ext_words,val.int32a);
                        }
                        break;
                    case W_TEXT_VALIGN:
                        if (type == RXT_WORD) {
                            para->valign = RL_FIND_WORD(text_ext_words,val.int32a);
                        }
                        break;
                }

                w++;
            }
            OS_Free(words);
            rt_para(ctx->envr, para);
        }
        break;

    case CMD_TEXT_RIGHT:
        rt_right(ctx->envr);
        break;

	case CMD_TEXT_SCROLL:
		rt_scroll(ctx->envr, RXA_LOG_PAIR(frm, 1));
		break;

    case CMD_TEXT_SHADOW:
        rt_shadow(ctx->envr, RXA_LOG_PAIR(frm, 1), RXA_COLOR_TUPLE(frm,2), RXA_INT32(frm,3));
        break;

	case CMD_TEXT_SIZE:
		rt_font_size(ctx->envr, LOG_COORD_Y(RXA_INT32(frm,1)));
		break;

    case CMD_TEXT_TEXT:
        {
            REBCHR* str;
#ifdef TO_WIN32
			//Windows uses UTF16 wide chars
			REBOOL dealloc = As_OS_Str(RXA_SERIES(frm, 1), &str);
#else
			//linux, android use UTF32 wide chars
            REBOOL dealloc = As_UTF32_Str(RXA_SERIES(frm, 1), &str);
#endif			
            rt_text(ctx->envr, str, ctx->index + 2, dealloc);
        }
        break;

    case CMD_TEXT_UNDERLINE:
        rt_underline(ctx->envr, RXA_LOGIC(frm, 1));
		break;

	default:
		return RXR_NO_COMMAND;
	}
    return RXR_UNSET;
}
