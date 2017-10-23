//
//  File: %dev-stdio.c
//  Summary: "Device: Standard I/O for Win32"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Provides basic I/O streams support for redirection and
// opening a console window if necessary.
//

#include <stdio.h>
#include <windows.h>
#include <process.h>
#include <assert.h>

#include <fcntl.h>
#include <io.h>

#include "reb-host.h"

#define BUF_SIZE (16 * 1024)    // MS restrictions apply

#define SF_DEV_NULL 31          // Local flag to mark NULL device.

#define CONSOLE_MODES \
        ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT \
        | 0x0040 | 0x0020       // quick edit and insert mode (not defined in VC6)

static HANDLE Std_Out = NULL;
static HANDLE Std_Inp = NULL;
static wchar_t *Std_Buf = NULL; // Used for UTF-8 conversion of stdin/stdout.

static BOOL Redir_Out = 0;
static BOOL Redir_Inp = 0;

//** ANSI emulation definition ****************************************** 
#define FOREGROUND_BLACK           0x0000
//#define FOREGROUND_BLUE          0x0001
//#define FOREGROUND_GREEN         0x0002
#define FOREGROUND_CYAN            0x0003
//#define FOREGROUND_RED           0x0004
#define FOREGROUND_MAGENTA         0x0005
#define FOREGROUND_YELLOW          0x0006
#define FOREGROUND_GREY            0x0007
//#define FOREGROUND_INTENSITY     0x0008
#define FOREGROUND_WHITE           0x000F
//#define BACKGROUND_BLUE          0x0010
#define BACKGROUND_CYAN            0x0030
//#define BACKGROUND_GREEN         0x0020
//#define BACKGROUND_RED           0x0040
#define BACKGROUND_MAGENTA         0x0050
#define BACKGROUND_YELLOW          0x0060
#define BACKGROUND_GREY            0x0070
//#define BACKGROUND_INTENSITY     0x0080
#define COMMON_LVB_UNDERSCORE      0x8000

static COORD Std_coord = {0, 0};

int Update_Graphic_Mode(int attribute, int value);
REBYTE* Parse_ANSI_sequence(REBYTE *cp, REBYTE *ep);

//**********************************************************************


static void Close_Stdio(void)
{
    if (Std_Buf) {
        OS_FREE(Std_Buf);
        Std_Buf = 0;
        //FreeConsole();  // problem: causes a delay
    }
}


//
//  Quit_IO: C
//
DEVICE_CMD Quit_IO(REBREQ *dr)
{
    REBDEV *dev = (REBDEV*)dr; // just to keep compiler happy above

    Close_Stdio();
    //if (GET_FLAG(dev->flags, RDF_OPEN)) FreeConsole();
    CLR_FLAG(dev->flags, RDF_OPEN);
    return DR_DONE;
}


//
//  Open_IO: C
//
DEVICE_CMD Open_IO(REBREQ *req)
{
    REBDEV *dev;

    dev = Devices[req->device];

    // Avoid opening the console twice (compare dev and req flags):
    if (GET_FLAG(dev->flags, RDF_OPEN)) {
        // Device was opened earlier as null, so req must have that flag:
        if (GET_FLAG(dev->flags, SF_DEV_NULL))
            SET_FLAG(req->modes, RDM_NULL);
        SET_FLAG(req->flags, RRF_OPEN);
        return DR_DONE; // Do not do it again
    }

    if (!GET_FLAG(req->modes, RDM_NULL)) {
        // Get the raw stdio handles:
        Std_Out = GetStdHandle(STD_OUTPUT_HANDLE);
        Std_Inp = GetStdHandle(STD_INPUT_HANDLE);
        //Std_Err = GetStdHandle(STD_ERROR_HANDLE);

        Redir_Out = (GetFileType(Std_Out) != FILE_TYPE_CHAR);
        Redir_Inp = (GetFileType(Std_Inp) != FILE_TYPE_CHAR);

        if (!Redir_Inp || !Redir_Out) {
            // If either input or output is not redirected, preallocate
            // a buffer for conversion from/to UTF-8.
            Std_Buf = OS_ALLOC_N(wchar_t, BUF_SIZE);
        }

        if (!Redir_Inp) {
            // Make the Win32 console a bit smarter by default.
            SetConsoleMode(Std_Inp, CONSOLE_MODES);
        }
    }
    else
        SET_FLAG(dev->flags, SF_DEV_NULL);

    SET_FLAG(req->flags, RRF_OPEN);
    SET_FLAG(dev->flags, RDF_OPEN);

    return DR_DONE;
}


//
//  Close_IO: C
//
DEVICE_CMD Close_IO(REBREQ *req)
{
    REBDEV *dev = Devices[req->device];

    Close_Stdio();

    CLR_FLAG(dev->flags, RRF_OPEN);

    return DR_DONE;
}


//
//  Write_IO: C
//
// Low level "raw" standard output function.
//
// Allowed to restrict the write to a max OS buffer size.
//
// Returns the number of chars written.
//
DEVICE_CMD Write_IO(REBREQ *req)
{
    if (GET_FLAG(req->modes, RDM_NULL)) {
        req->actual = req->length;
        return DR_DONE;
    }

    BOOL ok = FALSE; // Note: Windows BOOL, not REBOOL
    long len;
    REBYTE *bp;
    REBYTE *cp;
    REBYTE *ep;

    if (Std_Out) {

        bp = req->common.data;
        ep = bp + req->length;

        // Using this loop for seeking escape char and processing ANSI sequence
        do {
            DWORD total_bytes;
            //from some reason, I must decrement the tail pointer in function bellow,
            //else escape char is found past the end and processed in rare cases - like in console: do [help] do [help func]
            //It looks dangerous, but it should be safe as it looks the req->length is always at least 1.
            cp = (REBYTE *)Skip_To_Byte(bp, ep-1, (REBYTE)27); //find ANSI escape char "^["

            if (Redir_Out) { // for Console SubSystem (always UTF-8)
                if (cp){
                    ok = WriteFile(Std_Out, bp, cp - bp, &total_bytes, 0);
                    bp = Parse_ANSI_sequence(++cp, ep);
                } else {
                    ok = WriteFile(Std_Out, bp, ep - bp, &total_bytes, 0);
                    bp = ep;
                }
                if (!ok) {
                    req->error = GetLastError();
                    return DR_ERROR;
                }
            } else { // for Windows SubSystem - must be converted to Win32 wide-char format

                // Thankfully, MS provides something other than mbstowcs();
                // however, if our buffer overflows, it's an error. There's no
                // efficient way at this level to split-up the input data,
                // because its UTF-8 with variable char sizes.

                //if found, write to the console content before it starts, else everything
                if (cp){
                    len = MultiByteToWideChar(CP_UTF8, 0, bp, cp - bp, Std_Buf, BUF_SIZE);
                } else {
                    len = MultiByteToWideChar(CP_UTF8, 0, bp, ep - bp, Std_Buf, BUF_SIZE);
                    bp = ep;
                }
                if (len > 0) {// no error
                    ok = WriteConsoleW(Std_Out, Std_Buf, len, &total_bytes, 0);
                    if (!ok) {
                        req->error = GetLastError();
                        return DR_ERROR;
                    }
                }
                //is escape char was found, parse the ANSI sequence...
                if (cp) {
                    bp = Parse_ANSI_sequence(++cp, ep);
                }
            }
            UNUSED(total_bytes);
        } while (bp < ep);

        req->actual = req->length;  // do not use "total_bytes" (can be byte or wide)

        //if (GET_FLAG(req->flags, RRF_FLUSH)) {
        //  FLUSH();
        //}
    }

    return DR_DONE;
}


//
//  Read_IO: C
//
// Low level "raw" standard input function.
//
// The request buffer must be long enough to hold result.
//
// Result is NOT terminated (the actual field has length.)
//
DEVICE_CMD Read_IO(REBREQ *req)
{
    DWORD total = 0;
    DWORD len;
    BOOL ok;

    if (GET_FLAG(req->modes, RDM_NULL)) {
        req->common.data[0] = 0;
        return DR_DONE;
    }

    req->actual = 0;

    if (Std_Inp) {

        if (Redir_Inp) { // always UTF-8
            len = MIN(req->length, BUF_SIZE);
            ok = ReadFile(Std_Inp, req->common.data, len, &total, 0);
        }
        else {
            ok = ReadConsoleW(Std_Inp, Std_Buf, BUF_SIZE-1, &total, 0);
            if (ok) {
                if (total == 0) {
                    // WideCharToMultibyte fails if cchWideChar is 0.
                    assert(req->length >= 2);
                    strcpy(s_cast(req->common.data), "");
                }
                else {
                    total = WideCharToMultiByte(
                        CP_UTF8,
                        0,
                        Std_Buf,
                        total,
                        s_cast(req->common.data),
                        req->length,
                        0,
                        0
                    );
                    if (total == 0)
                        ok = FALSE;
                }
            }
        }

        if (NOT(ok)) {
            req->error = GetLastError();
            return DR_ERROR;
        }

        req->actual = total;
    }

    return DR_DONE;
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] =
{
    0,  // init
    Quit_IO,
    Open_IO,
    Close_IO,
    Read_IO,
    Write_IO,
    0,  // poll
    0,  // connect
    0,  // query
    0,  // modify
    0,  // CREATE was once used for opening echo file
};

DEFINE_DEV(
    Dev_StdIO,
    "Standard IO", 1, Dev_Cmds, RDC_MAX, sizeof(struct devreq_file)
);


/***********************************************************************
**
*/  int Update_Graphic_Mode(int attribute, int value)
/*
**
***********************************************************************/
{
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo; 
    int tmp;

    if (attribute < 0) {
        GetConsoleScreenBufferInfo(Std_Out, &csbiInfo);
        attribute = csbiInfo.wAttributes;
    }

    switch (value) {
        case 0: attribute = FOREGROUND_GREY;                           break;
        case 1: attribute = attribute | FOREGROUND_INTENSITY | BACKGROUND_INTENSITY; break;
        case 4: attribute = attribute | COMMON_LVB_UNDERSCORE;         break;
        case 7: tmp = (attribute & 0xF0) >> 4;
                attribute = ((attribute & 0x0F) << 4) | tmp;           break; //reverse
        case 30: attribute =  attribute & 0xF8;                        break;
        case 31: attribute = (attribute & 0xF8) | FOREGROUND_RED;      break;
        case 32: attribute = (attribute & 0xF8) | FOREGROUND_GREEN;    break;
        case 33: attribute = (attribute & 0xF8) | FOREGROUND_YELLOW;   break;
        case 34: attribute = (attribute & 0xF8) | FOREGROUND_BLUE;     break;
        case 35: attribute = (attribute & 0xF8) | FOREGROUND_MAGENTA;  break;
        case 36: attribute = (attribute & 0xF8) | FOREGROUND_CYAN;     break;
        case 37: attribute = (attribute & 0xF8) | FOREGROUND_GREY;     break;
        case 39: attribute =  attribute & 0xF7;                        break;  //FOREGROUND_INTENSITY reset 
        case 40: attribute =  attribute & 0x8F;                        break;
        case 41: attribute = (attribute & 0x8F) | BACKGROUND_RED;      break;
        case 42: attribute = (attribute & 0x8F) | BACKGROUND_GREEN;    break;
        case 43: attribute = (attribute & 0x8F) | BACKGROUND_YELLOW;   break;
        case 44: attribute = (attribute & 0x8F) | BACKGROUND_BLUE;     break;
        case 45: attribute = (attribute & 0x8F) | BACKGROUND_MAGENTA;  break;
        case 46: attribute = (attribute & 0x8F) | BACKGROUND_CYAN;     break;
        case 47: attribute = (attribute & 0x8F) | BACKGROUND_GREY;     break;
        case 49: attribute =  attribute & 0x7F;                        break; //BACKGROUND_INTENSITY reset
        default: attribute = value;
    }
    return attribute;
}

/***********************************************************************
**
*/  REBYTE* Parse_ANSI_sequence(REBYTE *cp, REBYTE *ep)
/*
**      Parses ANSI sequence and return number of bytes used.
**      Based on http://ascii-table.com/ansi-escape-sequences.php
**
***********************************************************************/
{
    if(*cp != '[') return cp;

    int state = 1;
    int value1 = 0;
    int value2 = 0;
    int attribute = -1;
    long unsigned int num;
    int len;
    COORD coordScreen; 
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    

    do {
        if(++cp == ep) return cp;

        switch (state) {

        case 1: //value1 start
            if( *cp >= (int)'0' && *cp <= (int)'9') {
                value1 = ((value1 * 10) + (*cp - (int)'0')) % 0xFFFF;
                state = 2;
            } else if (*cp == ';') {
                //do nothing
            } else if (*cp == 's') {
                //Saves the current cursor position.
                GetConsoleScreenBufferInfo(Std_Out, &csbiInfo);
                Std_coord.X = csbiInfo.dwCursorPosition.X;
                Std_coord.Y = csbiInfo.dwCursorPosition.Y;
                state = -1;
            } else if (*cp == 'u') {
                //Returns the cursor to the position stored by the Save Cursor Position sequence.
                SetConsoleCursorPosition(Std_Out, Std_coord);
                state = -1;
            } else if (*cp == 'K') {
                //TODO: Erase Line.
                state = -1;
            } else if (*cp == 'J') {
                //TODO: Clear screen from cursor down.
                state = -1;
            } else if (*cp == 'H' || *cp == 'f') {
                coordScreen.X = 0;
                coordScreen.Y = 0;
                SetConsoleCursorPosition(Std_Out, coordScreen);
                state = -1;
            } else {
                state = -1;
            }
            break;
        case 2: //value1 continue
            if( *cp >= (int)'0' && *cp <= (int)'9') {
                value1 = ((value1 * 10) + (*cp - (int)'0')) % 0xFFFF;
                state = 2;
            } else if (*cp == ';') {
                state = 3;
            } else if (*cp == 'm') {
                attribute = Update_Graphic_Mode(attribute, value1);
                SetConsoleTextAttribute(Std_Out, attribute);
                state = -1;
            } else if (*cp == 'A') {
                //Cursor Up.
                GetConsoleScreenBufferInfo(Std_Out, &csbiInfo);
                csbiInfo.dwCursorPosition.Y = MAX(0, csbiInfo.dwCursorPosition.Y - value1);
                SetConsoleCursorPosition(Std_Out, csbiInfo.dwCursorPosition);
                state = -1;
            } else if (*cp == 'B') {
                //Cursor Down.
                GetConsoleScreenBufferInfo(Std_Out, &csbiInfo);
                csbiInfo.dwCursorPosition.Y = MIN(csbiInfo.dwSize.Y, csbiInfo.dwCursorPosition.Y + value1);
                SetConsoleCursorPosition(Std_Out, csbiInfo.dwCursorPosition);
                state = -1;
            } else if (*cp == 'C') {
                //Cursor Forward.
                GetConsoleScreenBufferInfo(Std_Out, &csbiInfo);
                csbiInfo.dwCursorPosition.X = MIN(csbiInfo.dwSize.X, csbiInfo.dwCursorPosition.X + value1);
                SetConsoleCursorPosition(Std_Out, csbiInfo.dwCursorPosition);
                state = -1;
            } else if (*cp == 'D') {
                //Cursor Backward.
                GetConsoleScreenBufferInfo(Std_Out, &csbiInfo);
                csbiInfo.dwCursorPosition.X = MAX(0, csbiInfo.dwCursorPosition.X - value1);
                SetConsoleCursorPosition(Std_Out, csbiInfo.dwCursorPosition);
                state = -1;
            } else if (*cp == 'J') {
                if (value1 == 2) {
                    GetConsoleScreenBufferInfo(Std_Out, &csbiInfo);
                    len = csbiInfo.dwSize.X * csbiInfo.dwSize.Y;
                    coordScreen.X = 0;
                    coordScreen.Y = 0;
                    FillConsoleOutputCharacter(Std_Out, (TCHAR)' ', len, coordScreen, &num);
                    FillConsoleOutputAttribute(Std_Out, csbiInfo.wAttributes, len, coordScreen, &num);
                    SetConsoleCursorPosition(Std_Out, coordScreen);
                }
                state = -1;
            } else {
                state = -1;
            }
            break; //End CASE 2
        case 3: //value2 start
            if( *cp >= (int)'0' && *cp <= (int)'9') {
                value2 = ((value2 * 10) + (*cp - (int)'0')) % 0xFFFF;
                state = 4;
            } else if (*cp == ';') {
                //do nothing
            } else {
                state = -1;
            }
            break; //End CASE 3
        case 4: //value2 continue
            if( *cp >= (int)'0' && *cp <= (int)'9') {
                value2 = ((value2 * 10) + (*cp - (int)'0')) % 0xFFFF;
                state = 4;
            } else if (*cp == 'm') {
                attribute = Update_Graphic_Mode(attribute, value1);
                attribute = Update_Graphic_Mode(attribute, value2);
                SetConsoleTextAttribute(Std_Out, attribute);
                state = -1;
            } else if (*cp == ';') {
                attribute = Update_Graphic_Mode(attribute, value1);
                attribute = Update_Graphic_Mode(attribute, value2);
                SetConsoleTextAttribute(Std_Out, attribute);
                value1 = 0;
                value2 = 0;
                state = 1;
            } else if (*cp == 'H' || *cp == 'f') {
                coordScreen.Y = value1;
                coordScreen.X = value2;
                SetConsoleCursorPosition(Std_Out, coordScreen);
                state = -1;
            } else {
                state = -1;
            }


        } //End: switch (state)
    } while (state >= 0);

    return ++cp;
}