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
**  Title: Host Memory Allocator
**  Purpose:
**		See notes about OS_ALLOC and OS_FREE in make-os-ext.r
**
***********************************************************************/

#include <stdlib.h>

#include "reb-host.h"


/***********************************************************************
**
*/	void *OS_Alloc_Mem(size_t size)
/*
**		Allocate memory of given size.
**
**		This is necessary because some environments may use their
**		own specific memory allocation (e.g. private heaps).
**
***********************************************************************/
{
	return malloc(size);
}


/***********************************************************************
**
*/	void OS_Free_Mem(void *mem)
/*
**		Free memory allocated in this OS environment. (See OS_Alloc_Mem)
**
***********************************************************************/
{
	free(mem);
}
