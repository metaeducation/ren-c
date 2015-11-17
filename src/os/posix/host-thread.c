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
**  Title: Host Thread Services
**  Purpose:
**		Support for the TASK! type (not currently implemented).
**
***********************************************************************/

#include <stddef.h>

#include "reb-host.h"

// Semaphore lock to sync sub-task launch:
static void *Task_Ready;


//
//  OS_Create_Thread: C
// 
// Creates a new thread for a REBOL task datatype.
// 
// NOTE:
// For this to work, the multithreaded library option is
// needed in the C/C++ code generation settings.
// 
// The Task_Ready stops return until the new task has been
// initialized (to avoid unknown new thread state).
//
REBINT OS_Create_Thread(THREADFUNC *init, void *arg, REBCNT stack_size)
{
	REBINT thread;
/*
	Task_Ready = CreateEvent(NULL, TRUE, FALSE, "REBOL_Task_Launch");
	if (!Task_Ready) return -1;

	thread = _beginthread(init, stack_size, arg);

	if (thread) WaitForSingleObject(Task_Ready, 2000);
	CloseHandle(Task_Ready);
*/
	return 1;
}


//
//  OS_Delete_Thread: C
// 
// Can be called by a REBOL task to terminate its thread.
//
void OS_Delete_Thread(void)
{
	//_endthread();
}


//
//  OS_Task_Ready: C
// 
// Used for new task startup to resume the thread that
// launched the new task.
//
void OS_Task_Ready(REBINT tid)
{
	//SetEvent(Task_Ready);
}
