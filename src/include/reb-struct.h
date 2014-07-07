/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2014 Atronix Engineering, Inc.
**
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
**  Summary: Struct to C function
**  Module:  reb-struct.h
**  Author:  Shixin Zeng
**
***********************************************************************/

struct Struct_Field {
	REBSER* spec; /* for nested struct */
	REBSER* fields; /* for nested struct */
	REBCNT sym;

	u16 type; /* rebol type */

	/* size is limited by struct->offset, so only 16-bit */
	u16 offset;
	u16 dimension; /* for arrays */
	u16 size; /* size of element, in bytes */
};
