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
**  Title: Graphics OS specific support
**  Author: Richard Smolak, Carl Sassenrath
**  Purpose: "View" commands support.
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

#include <windows.h>

#include "reb-host.h"

//***** Externs *****

extern void *Cursor;

extern RXIEXT int RXD_Graphics(int cmd, RXIFRM *frm, REBCEC *data);
extern RXIEXT int RXD_Draw(int cmd, RXIFRM *frm, REBCEC *ctx);
extern RXIEXT int RXD_Shape(int cmd, RXIFRM *frm, REBCEC *ctx);
extern RXIEXT int RXD_Text(int cmd, RXIFRM *frm, REBCEC *ctx);

extern const unsigned char RX_graphics[];
extern const unsigned char RX_draw[];
extern const unsigned char RX_shape[];
extern const unsigned char RX_text[];

//**********************************************************************
//** Helper Functions **************************************************
//**********************************************************************

/***********************************************************************
**
*/	void* OS_Image_To_Cursor(REBYTE* image, REBINT width, REBINT height)
/*
**      Converts REBOL image! to Windows CURSOR
**
***********************************************************************/
{
	int xHotspot = 0;
	int yHotspot = 0;

	HICON result = NULL;
	HBITMAP hSourceBitmap;
	BITMAPINFO  BitmapInfo;
	ICONINFO iconinfo;

    //Get the system display DC
    HDC hDC = GetDC(NULL);

	//Create DIB
	unsigned char* ppvBits;
	int bmlen = width * height * 4;
	int i;

	BitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	BitmapInfo.bmiHeader.biWidth = width;
	BitmapInfo.bmiHeader.biHeight = -(signed)height;
	BitmapInfo.bmiHeader.biPlanes = 1;
	BitmapInfo.bmiHeader.biBitCount = 32;
	BitmapInfo.bmiHeader.biCompression = BI_RGB;
	BitmapInfo.bmiHeader.biSizeImage = 0;
	BitmapInfo.bmiHeader.biXPelsPerMeter = 0;
	BitmapInfo.bmiHeader.biYPelsPerMeter = 0;
	BitmapInfo.bmiHeader.biClrUsed = 0;
	BitmapInfo.bmiHeader.biClrImportant = 0;

	hSourceBitmap = CreateDIBSection(hDC, &BitmapInfo, DIB_RGB_COLORS, (void**)&ppvBits, NULL, 0);

	//Release the system display DC
    ReleaseDC(NULL, hDC);

	//Copy the image content to DIB
	COPY_MEM(ppvBits, image, bmlen);

	//Invert alphachannel from the REBOL format
	for (i = 3;i < bmlen;i+=4){
		ppvBits[i] ^= 0xff;
	}

	//Create the cursor using the masks and the hotspot values provided
	iconinfo.fIcon		= FALSE;
	iconinfo.xHotspot	= xHotspot;
	iconinfo.yHotspot	= yHotspot;
	iconinfo.hbmMask	= hSourceBitmap;
	iconinfo.hbmColor	= hSourceBitmap;

	result = CreateIconIndirect(&iconinfo);

	DeleteObject(hSourceBitmap);

	return (void*)result;
}

/***********************************************************************
**
*/	void OS_Set_Cursor(void *cursor)
/*
**
**
***********************************************************************/
{
	SetCursor((HCURSOR)cursor);
}

/***********************************************************************
**
*/	void* OS_Load_Cursor(void *cursor)
/*
**
**
***********************************************************************/
{
	return (void*)LoadCursor(NULL, (LPCTSTR)cursor);
}

/***********************************************************************
**
*/	void OS_Destroy_Cursor(void *cursor)
/*
**
**
***********************************************************************/
{
	DestroyCursor((HCURSOR)Cursor);
}

/***********************************************************************
**
*/	REBD32 OS_Get_Metrics(METRIC_TYPE type)
/*
**	Provide OS specific UI related information.
**
***********************************************************************/
{
	REBD32 result = 0;
	switch(type){
		case SM_SCREEN_WIDTH:
			result = GetSystemMetrics(SM_CXSCREEN);
			break;
		case SM_SCREEN_HEIGHT:
			result = GetSystemMetrics(SM_CYSCREEN);
			break;
		case SM_WORK_WIDTH:
			{
				RECT rect;
				SystemParametersInfo(SPI_GETWORKAREA, 0, &rect, 0);
				result = rect.right;
			}
			break;
		case SM_WORK_HEIGHT:
			{
				RECT rect;
				SystemParametersInfo(SPI_GETWORKAREA, 0, &rect, 0);
				result = rect.bottom;
			}
			break;
		case SM_TITLE_HEIGHT:
			result = GetSystemMetrics(SM_CYCAPTION);
			break;
		case SM_SCREEN_DPI_X:
			{
				HDC hDC = GetDC(NULL);
				result = GetDeviceCaps(hDC, LOGPIXELSX);
				ReleaseDC(NULL, hDC);
			}
			break;
		case SM_SCREEN_DPI_Y:
			{
				HDC hDC = GetDC(NULL);
				result = GetDeviceCaps(hDC, LOGPIXELSY);
				ReleaseDC(NULL, hDC);
			}
			break;
		case SM_BORDER_WIDTH:
			result = GetSystemMetrics(SM_CXSIZEFRAME);
			break;
		case SM_BORDER_HEIGHT:
			result = GetSystemMetrics(SM_CYSIZEFRAME);
			break;
		case SM_BORDER_FIXED_WIDTH:
			result = GetSystemMetrics(SM_CXFIXEDFRAME);
			break;
		case SM_BORDER_FIXED_HEIGHT:
			result = GetSystemMetrics(SM_CYFIXEDFRAME);
			break;
		case SM_WINDOW_MIN_WIDTH:
			result = GetSystemMetrics(SM_CXMIN);
			break;
		case SM_WINDOW_MIN_HEIGHT:
			result = GetSystemMetrics(SM_CYMIN);
			break;
		case SM_WORK_X:
			{
				RECT rect;
				SystemParametersInfo(SPI_GETWORKAREA, 0, &rect, 0);
				result = rect.left;
			}
			break;
		case SM_WORK_Y:
			{
				RECT rect;
				SystemParametersInfo(SPI_GETWORKAREA, 0, &rect, 0);
				result = rect.top;
			}
			break;
	}
	return result;
}

/***********************************************************************
**
*/	void OS_Show_Soft_Keyboard(REBINT win, REBINT x, REBINT y)
/*
**  Display software/virtual keyboard on the screen.
**  (mainly used on mobile platforms)
**
***********************************************************************/
{
}

/***********************************************************************
**
*/	void OS_Init_Graphics(void)
/*
**	Initialize special variables of the graphics subsystem.
**
***********************************************************************/
{
	//Sets the current process as dpi aware. (Windows Vista and up only)
	HMODULE hUser32 = LoadLibraryW(L"user32.dll");
	typedef BOOL (*SetProcessDPIAwareFunc)(void);
	SetProcessDPIAwareFunc setDPIAware = (SetProcessDPIAwareFunc)GetProcAddress(hUser32, "SetProcessDPIAware");
	if (setDPIAware) setDPIAware();
	FreeLibrary(hUser32);	
	
	RL_Extend((REBYTE *)(&RX_graphics[0]), &RXD_Graphics);
	RL_Extend((REBYTE *)(&RX_draw[0]), &RXD_Draw);
	RL_Extend((REBYTE *)(&RX_shape[0]), &RXD_Shape);
	RL_Extend((REBYTE *)(&RX_text[0]), &RXD_Text);
}

/***********************************************************************
**
*/	void OS_Destroy_Graphics(void)
/*
**	Finalize any special variables of the graphics subsystem.
**
***********************************************************************/
{
}
