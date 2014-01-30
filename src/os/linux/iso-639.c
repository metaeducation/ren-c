#include <string.h>
#include "iso-639.h"

const char ** iso639_find_entry_by_2_code(const char* code)
{
	int i = 0;
	if (code == NULL || strlen(code) != 2) {
		return NULL;
	}
	for(i = 0; i < sizeof(iso_639_table)/sizeof(iso_639_table[0]); i ++) {
		const char **inner = iso_639_table[i];
		if (inner[2] != NULL && !strncasecmp(inner[2], code, 3)) {
			return inner;
		}
	}

	return NULL;
}
