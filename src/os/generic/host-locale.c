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
**  Title: Locale Support
**  Purpose:
**		Support for language and language groups (ISO 639)...as well as
**		country, state, and province codes (ISO 3166)
**
**      https://en.wikipedia.org/wiki/ISO_639
**		https://en.wikipedia.org/wiki/ISO_3166
**
***********************************************************************/

#include <string.h>
#include <stdlib.h> // for getenv() - which is standard C, not POSIX

#include "reb-host.h"

const char ** iso639_find_entry_by_2_code(const char* code);
const char ** iso3166_find_entry_by_2_code(const char* code);


//
//  OS_Get_Locale: C
// 
// Used to obtain locale information from the system.
// The returned value must be freed with OS_FREE_MEM.
//
REBCHR *OS_Get_Locale(int what)
{
	// Must be compile-time const for '= {...}' style init (-Wc99-extensions)
	const char *ret[4];

	unsigned int i = 0;
	unsigned int j = 0;
	char *lang = NULL;
	char *territory = NULL;
	char *locale = NULL;

	// something like: 'lang_territory.codeset'
	const char *lang_env = getenv("LANG");

	const char ** iso639_entry;
	const char ** iso3166_entry;

	if (what > 3 || what < 0) {
		return NULL;
	}
	if (lang_env == NULL){
		return NULL;
	}
	for(i = 0; i < strlen(lang_env); i ++){
		if (lang_env[i] == '_'){
			if (lang != NULL) { /* duplicate "_" */
				goto error;
			}
			lang = OS_ALLOC_ARRAY(char, i + 1);
			if (lang == NULL) goto error;
			strncpy(lang, lang_env, i);
			lang[i] = '\0';
			j = i;
		} else if (lang_env[i] == '.'){
			if (i == j) goto error;
			territory = OS_ALLOC_ARRAY(char, i - j);
			if (territory == NULL) goto error;
			strncpy(territory, lang_env + j + 1, i - j - 1);
			territory[i - j - 1] = '\0';
			break;
		}
	}

	if (lang == NULL || territory == NULL) goto error;

	iso639_entry = iso639_find_entry_by_2_code(lang);
	OS_FREE(lang);
	lang = NULL;
	if (iso639_entry == NULL) goto error;

	iso3166_entry = iso3166_find_entry_by_2_code(territory);
	OS_FREE(territory);
	territory = NULL;

	ret[0] = iso639_entry[3];
	ret[1] = iso639_entry[3];
	ret[2] = iso3166_entry[1];
	ret[3] = iso3166_entry[1];

	locale = OS_ALLOC_ARRAY(char, strlen(ret[what]) + 1);
	strcpy(locale, ret[what]);
	return locale;

error:
	if (lang != NULL) {
		OS_FREE(lang);
	}
	if (territory != NULL) {
		OS_FREE(territory);
	}
	return NULL;
}
