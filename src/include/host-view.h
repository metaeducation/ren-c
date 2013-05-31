/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
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
**  Title: Misc View related shared definitions
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

typedef enum {
	SM_SCREEN_WIDTH = 0,
	SM_SCREEN_HEIGHT,
	SM_WORK_WIDTH,
	SM_WORK_HEIGHT,
	SM_TITLE_HEIGHT,
	SM_SCREEN_DPI_X,
	SM_SCREEN_DPI_Y,
	SM_BORDER_WIDTH,
	SM_BORDER_HEIGHT,
	SM_BORDER_FIXED_WIDTH,
	SM_BORDER_FIXED_HEIGHT,
	SM_WINDOW_MIN_WIDTH,
	SM_WINDOW_MIN_HEIGHT,
	SM_WORK_X,
	SM_WORK_Y
} METRIC_TYPE;
