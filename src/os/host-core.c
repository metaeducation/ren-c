/***********************************************************************
**
**  REBOL 3.0 "Invasion"
**  Copyright 2010 REBOL Technologies
**  All rights reserved.
**
************************************************************************
**
**  Title: Core(OS independent) extension commands
**  Date:  1-Sep-2010
**  File:  host-core.c
**  Author: Cyphre
**  Purpose: Defines extended commands thatcan be used in /Core and/or /View
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
#ifdef TO_WIN32
#include <windows.h>
#endif

#include "reb-host.h"

#include "lodepng.h"
 
#include "rc4.h"
#include "rsa.h"

#define INCLUDE_EXT_DATA
#include "host-ext-core.h"

//***** Externs *****
#ifdef TO_WIN32
extern void Console_Window(BOOL show);
extern void Console_Output(BOOL state);
extern REBINT As_OS_Str(REBSER *series, REBCHR **string);
extern REBOOL OS_Request_Dir(REBCHR *title, REBCHR **folder, REBCHR *path);
#endif

RL_LIB *RL; // Link back to reb-lib from embedded extensions
static u32 *core_ext_words;

/***********************************************************************
**
*/	RXIEXT int RXD_Core(int cmd, RXIFRM *frm, REBCEC *data)
/*
**		Core command extension dispatcher.
**
***********************************************************************/
{
    switch (cmd) {

    case CMD_CORE_SHOW_CONSOLE:
#ifdef TO_WIN32	
        Console_Window(TRUE);
#endif
        break;

    case CMD_CORE_HIDE_CONSOLE:
#ifdef TO_WIN32	
        Console_Window(FALSE);
#endif
        break;

    case CMD_CORE_TO_PNG:
		{
			LodePNG_Encoder encoder; 
			size_t buffersize;
			REBYTE *buffer;
			REBSER *binary;
			REBYTE *binaryBuffer;
			REBINT *s;
			REBINT w = RXA_IMAGE_WIDTH(frm,1);
			REBINT h = RXA_IMAGE_HEIGHT(frm,1);

			//create encoder and set settings
			LodePNG_Encoder_init(&encoder);
			//disable autopilot ;)
			encoder.settings.auto_choose_color = 0;
			//input format
			encoder.infoRaw.color.colorType = 6;
			encoder.infoRaw.color.bitDepth = 8;
			//output format
			encoder.infoPng.color.colorType = 2; //6 to save alpha channel as well
			encoder.infoPng.color.bitDepth = 8;

			//encode and save
			LodePNG_Encoder_encode(&encoder, &buffer, &buffersize, RXA_IMAGE_BITS(frm,1), w, h);

			//cleanup
			LodePNG_Encoder_cleanup(&encoder);

			//allocate new binary!
			binary = (REBSER*)RL_Make_String(buffersize, FALSE);
			binaryBuffer = (REBYTE *)RL_SERIES(binary, RXI_SER_DATA);
			//copy PNG data
			memcpy(binaryBuffer, buffer, buffersize);
			
			//hack! - will set the tail to buffersize
			s = (REBINT*)binary;
			s[1] = buffersize;
			
			//setup returned binary! value
			RXA_TYPE(frm,1) = RXT_BINARY;			
			RXA_SERIES(frm,1) = binary;
			RXA_INDEX(frm,1) = 0;			
			return RXR_VALUE;
		}
        break;

    case CMD_CORE_CONSOLE_OUTPUT:
#ifdef TO_WIN32
        Console_Output(RXA_LOGIC(frm, 1));
#endif		
        break;

	case CMD_CORE_REQ_DIR:
		{
#ifdef TO_WIN32
			REBCHR *title;
			REBSER *string;
			REBCHR *stringBuffer;
			REBCHR *path = NULL;
			REBOOL osTitle = FALSE;
			REBOOL osPath = FALSE;
			
			//allocate new string!
			string = (REBSER*)RL_Make_String(MAX_PATH, TRUE);
			stringBuffer = (REBCHR*)RL_SERIES(string, RXI_SER_DATA);
			
			
			if (RXA_TYPE(frm, 2) == RXT_STRING) {
				osTitle = As_OS_Str(RXA_SERIES(frm, 2),  (REBCHR**)&title);
			} else {
				title = L"Please, select a directory...";
			}
			
			if (RXA_TYPE(frm, 4) == RXT_STRING) {
				osPath = As_OS_Str(RXA_SERIES(frm, 4),  (REBCHR**)&path);
			}
			
			if (OS_Request_Dir(title , &stringBuffer, path)){
				REBINT *s = (REBINT*)string;

				//hack! - will set the tail to string length
				s[1] = wcslen(stringBuffer);
				
				RXA_TYPE(frm, 1) = RXT_STRING;
				RXA_SERIES(frm,1) = string;
				RXA_INDEX(frm,1) = 0;
			} else {
				RXA_TYPE(frm, 1) = RXT_NONE;
			}

			//don't let the strings leak!
			if (osTitle) OS_Free(title);
			if (osPath) OS_Free(path);
			
			return RXR_VALUE;
#endif
		}
		break;
		
		case CMD_CORE_RC4:
		{
			RC4_CTX *ctx;
			REBSER *data, key;
			REBYTE *dataBuffer;

			if (RXA_TYPE(frm, 5) == RXT_HANDLE) {
				//set current context
				ctx = (RC4_CTX*)RXA_HANDLE(frm,5);

				if (RXA_TYPE(frm, 1) == RXT_NONE) {
					//destroy context
					OS_Free(ctx);
					return RXR_VALUE;
				}
			}
			
			data = RXA_SERIES(frm,1);
			dataBuffer = (REBYTE *)RL_SERIES(data, RXI_SER_DATA) + RXA_INDEX(frm,1);

			if (RXA_TYPE(frm, 3) == RXT_BINARY) {
				//key defined - setup new context
				ctx = (RC4_CTX*)OS_Make(sizeof(*ctx));
				memset(ctx, 0, sizeof(*ctx));
				
				key = RXA_SERIES(frm,3);

				RC4_setup(ctx, (REBYTE *)RL_SERIES(key, RXI_SER_DATA) + RXA_INDEX(frm,3), RL_SERIES(key, RXI_SER_TAIL) - RXA_INDEX(frm,3));
			}
			
			RC4_crypt(ctx, dataBuffer, dataBuffer, RL_SERIES(data, RXI_SER_TAIL) - RXA_INDEX(frm,1));

			RXA_TYPE(frm, 1) = RXT_HANDLE;
			RXA_HANDLE(frm,1) = ctx;
			return RXR_VALUE;
		}

		case CMD_CORE_RSA:
		{
			RXIARG val;
            u32 *words,*w;
			REBCNT type;
			REBSER *data = RXA_SERIES(frm, 1);
			REBYTE *dataBuffer = (REBYTE *)RL_SERIES(data, RXI_SER_DATA) + RXA_INDEX(frm,1);
			REBSER *obj = RXA_OBJECT(frm, 2);
			REBYTE *objData = NULL, *n = NULL, *e = NULL, *d = NULL, *p = NULL, *q = NULL, *dp = NULL, *dq = NULL, *qinv = NULL;
			REBINT data_len = RL_SERIES(data, RXI_SER_TAIL) - RXA_INDEX(frm,1), objData_len = 0, n_len = 0, e_len = 0, d_len = 0, p_len = 0, q_len = 0, dp_len = 0, dq_len = 0, qinv_len = 0;
			REBSER *binary;
			REBINT binary_len;
			REBYTE *binaryBuffer;
			REBINT *s;

			BI_CTX *bi_ctx;
			bigint *data_bi;
			RSA_CTX *rsa_ctx = NULL;

            words = RL_WORDS_OF_OBJECT(obj);
            w = words;

            while (type = RL_GET_FIELD(obj, w[0], &val))
            {
				if (type == RXT_BINARY){
					objData = (REBYTE *)RL_SERIES(val.series, RXI_SER_DATA) + val.index;
					objData_len = RL_SERIES(val.series, RXI_SER_TAIL) - val.index;
					
					switch(RL_FIND_WORD(core_ext_words,w[0]))
					{
						case W_CORE_N:
							n = objData;
							n_len = objData_len;
							break;
						case W_CORE_E:
							e = objData;
							e_len = objData_len;
							break;
						case W_CORE_D:
							d = objData;
							d_len = objData_len;
							break;
						case W_CORE_P:
							p = objData;
							p_len = objData_len;
							break;
						case W_CORE_Q:
							q = objData;
							q_len = objData_len;
							break;
						case W_CORE_DP:
							dp = objData;
							dp_len = objData_len;
							break;
						case W_CORE_DQ:
							dq = objData;
							dq_len = objData_len;
							break;
						case W_CORE_QINV:
							qinv = objData;
							qinv_len = objData_len;
							break;
					}
				}
				w++;
			}

			if (!n || !e) return RXR_NONE;

			if (RXA_WORD(frm, 4)) // private refinement
			{
				if (!d) return RXR_NONE;
				RSA_priv_key_new(
					&rsa_ctx, n, n_len, e, e_len, d, d_len,
					p, p_len, q, q_len, dp, dp_len, dq, dq_len, qinv, qinv_len
				);
				binary_len = d_len;
			} else {
				RSA_pub_key_new(&rsa_ctx, n, n_len, e, e_len);
				binary_len = n_len;
			}

			bi_ctx = rsa_ctx->bi_ctx;
			data_bi = bi_import(bi_ctx, dataBuffer, data_len);

			//allocate new binary!
			binary = (REBSER*)RL_Make_String(binary_len, FALSE);
			binaryBuffer = (REBYTE *)RL_SERIES(binary, RXI_SER_DATA);

			if (RXA_WORD(frm, 3)) // decrypt refinement
			{

				binary_len = RSA_decrypt(rsa_ctx, dataBuffer, binaryBuffer, RXA_WORD(frm, 4));

				if (binary_len == -1) return RXR_NONE;
			} else {
				RSA_encrypt(rsa_ctx, dataBuffer, data_len, binaryBuffer, RXA_WORD(frm, 4));
			}

			//hack! - will set the tail to buffersize
			s = (REBINT*)binary;
			s[1] = binary_len;
			
			//setup returned binary! value
			RXA_TYPE(frm,1) = RXT_BINARY;			
			RXA_SERIES(frm,1) = binary;
			RXA_INDEX(frm,1) = 0;			
			return RXR_VALUE;
		}

        case CMD_CORE_INIT_WORDS:
            core_ext_words = RL_MAP_WORDS(RXA_SERIES(frm,1));
            break;

        default:
            return RXR_NO_COMMAND;
    }

    return RXR_UNSET;
}

/***********************************************************************
**
*/	void Init_Core_Ext(void)
/*
**	Initialize special variables of the core extension.
**
***********************************************************************/
{
	RL = RL_Extend((REBYTE *)(&RX_core[0]), &RXD_Core);
}
