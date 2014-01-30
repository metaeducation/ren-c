#include <string.h>
#include "iso-3166.h"

const char ** iso3166_find_entry_by_2_code(const char* code)
{
	int i = 0;
	if (code == NULL || strlen(code) != 2) {
		return NULL;
	}
	for(i = 0; i < sizeof(iso_3166_table)/sizeof(iso_3166_table[0]); i ++) {
		const char **inner = iso_3166_table[i];
		if (inner[0] != NULL && !strncasecmp(inner[0], code, 3)) {
			return inner;
		}
	}

	return NULL;
}
