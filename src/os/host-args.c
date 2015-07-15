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
**  Title: Command line argument processing
**  Author: Carl Sassenrath
**  Caution: OS independent
**  Purpose:
**      Parses command line arguments and options, storing them
**      in a structure to be used by the REBOL library.
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

#include <stdlib.h>
#include <string.h>

#include "reb-host.h"

#define ARG_BUF_SIZE 1024

// REBOL Option --Words:

const struct {const REBCHR *word; const int flag;} arg_words[] = {
	// Keep in Alpha order!
	{OS_STR_LIT("args"),		RO_ARGS | RO_EXT},
	{OS_STR_LIT("boot"),		RO_BOOT | RO_EXT},
	{OS_STR_LIT("cgi"),			RO_CGI | RO_QUIET},
	{OS_STR_LIT("debug"),		RO_DEBUG | RO_EXT},
	{OS_STR_LIT("do"),			RO_DO | RO_EXT},
	{OS_STR_LIT("halt"),		RO_HALT},
	{OS_STR_LIT("help"),		RO_HELP},
	{OS_STR_LIT("import"),		RO_IMPORT | RO_EXT},
	{OS_STR_LIT("quiet"),		RO_QUIET},
	{OS_STR_LIT("script"),		RO_SCRIPT | RO_EXT},
	{OS_STR_LIT("secure"),		RO_SECURE | RO_EXT},
	{OS_STR_LIT("trace"),		RO_TRACE},
	{OS_STR_LIT("verbose"),		RO_VERBOSE},
	{OS_STR_LIT("version"),		RO_VERSION | RO_EXT},
	{OS_STR_LIT(""),			0},
};

// REBOL Option -Characters (in alpha sorted order):

const struct arg_chr {const char cflg; const int flag;} arg_chars[] = {
	{'?',	RO_HELP},
	{'V',	RO_VERS},
	{'b',	RO_BOOT | RO_EXT},
	{'c',	RO_CGI | RO_QUIET},
	{'h',	RO_HALT},
	{'q',	RO_QUIET},
	{'s',	RO_SECURE_MIN},
	{'t',	RO_TRACE},
	{'v',	RO_VERS},
	{'w',	RO_NO_WINDOW},
	{'\0',	0},
};

// REBOL Option +Characters:

const struct arg_chr arg_chars2[] = {
	{'s',	RO_SECURE_MAX},
	{'\0',	0},
};


/***********************************************************************
**
*/	static int find_option_word(REBCHR *word)
/*
**		Scan options, return flag bits, else zero.
**
***********************************************************************/
{
	int n;
	int i;
	REBCHR buf[16];

	// Some shells will pass us the line terminator. Ignore it.
	if (word[0] == '\r' || word[0] == '\n') return RO_IGNORE;

	OS_STRNCPY(buf, word, 15);

	for (i = 0; arg_words[i].flag; i++) {
		n = OS_STRNCMP(buf, arg_words[i].word, 15);
		if (n < 0) break;
		if (n == 0) return arg_words[i].flag;
	}
	return 0;
}


/***********************************************************************
**
*/	static int find_option_char(REBCHR chr, const struct arg_chr list[])
/*
**		Scan option char flags, return flag bits, else zero.
**
***********************************************************************/
{
	int i;

	// Some shells will pass us the line terminator. Ignore it.
	if (chr == '\r' || chr == '\n') return RO_IGNORE;

	for (i = 0; list[i].flag; i++) {
		if (chr < list[i].cflg) break;
		if (chr == list[i].cflg) return list[i].flag;
	}
	return 0;
}


/***********************************************************************
**
*/	static int Get_Ext_Arg(int flag, REBARGS *rargs, REBCHR *arg)
/*
**		Get extended argument field.
**
***********************************************************************/
{
	if (arg && arg[1] == (REBCHR)'-') return flag;

	flag &= ~RO_EXT;

	switch (flag) {

	case RO_VERSION:
		rargs->version = arg;
		break;

	case RO_SCRIPT:
		rargs->script = arg;
		break;

	case RO_ARGS:
		rargs->args = arg;
		break;

	case RO_DO:
		rargs->do_arg = arg;
		break;

	case RO_DEBUG:
		rargs->debug = arg;
		break;

	case RO_SECURE:
		rargs->secure = arg;
		break;

	case RO_IMPORT:
		rargs->import = arg;
		break;

	case RO_BOOT:
		rargs->boot = arg;
		break;
	}

	return flag;
}


/***********************************************************************
**
*/	void Parse_Args(int argc, REBCHR **argv, REBARGS *rargs)
/*
**		Parse REBOL's command line arguments, setting options
**		and values in the provided args structure.
**
***********************************************************************/
{
	REBCHR *arg;
	REBCHR *args = 0; // holds trailing args
	int flag;
	int i;

	CLEARS(rargs);

	// First arg is path to execuable (on most systems):
	if (argc > 0) rargs->exe_path = *argv;

	OS_Get_Current_Dir(&rargs->home_dir);

	// Parse each argument:
	for (i = 1; i < argc ; i++) {
		arg = argv[i];
		if (arg == 0) continue; // shell bug
		if (*arg == '-') {
			if (arg[1] == '-') {
				// --option words
				flag = find_option_word(arg+2);
				if (flag & RO_EXT) {
					flag = Get_Ext_Arg(flag, rargs, (i+1 >= argc) ? 0 : argv[i+1]);
					if ((flag & RO_EXT) == 0) i++; // used it
					else flag &= ~RO_EXT;
				}
				if (!flag) flag = RO_HELP;
				rargs->options |= flag;
			}
			else {
				// -x option chars
				while (*++arg) {
					flag = find_option_char(*arg, arg_chars);
					if (flag & RO_EXT) {
						flag = Get_Ext_Arg(flag, rargs, (i+1 >= argc) ? 0 : argv[i+1]);
						if ((flag & RO_EXT) == 0) i++; // used it
						else flag &= ~RO_EXT;
					}
					if (!flag) flag = RO_HELP;
					rargs->options |= flag;
				}
			}
		}
		else if (*arg == '+') {
			// +x option chars
			while (*++arg) {
				flag = find_option_char(*arg, arg_chars2);
				if (flag & RO_EXT) {
					flag = Get_Ext_Arg(flag, rargs, (i+1 >= argc) ? 0 : argv[i+1]);
					if ((flag & RO_EXT) == 0) i++; // used it
					else flag &= ~RO_EXT;
				}
				if (!flag) flag = RO_HELP;
				rargs->options |= flag;
			}
		}
		else {
			// script filename
			if (!rargs->script)
				rargs->script = arg;
			else {
				int len;
				if (!args) {
					args = OS_ALLOC_ARRAY(REBCHR, ARG_BUF_SIZE);
					args[0] = 0;
				}
				len = ARG_BUF_SIZE - OS_STRLEN(args) - 2; // space remaining
				OS_STRNCAT(args, arg, len);
				OS_STRNCAT(args, OS_STR_LIT(" "), 1);
			}
		}
	}

	if (args) {
		args[OS_STRLEN(args) - 1] = 0; // remove trailing space
		Get_Ext_Arg(RO_ARGS, rargs, args);
	}
}



