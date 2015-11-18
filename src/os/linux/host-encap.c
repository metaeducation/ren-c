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
**  Title: Linux Encap Facility
**  Purpose:
**      This host routine is used to read out a binary blob stored in
**      an ELF executable, used for "encapping" a script and its
**      resources.  Unlike a large constant blob that is compiled into
**      the data segment requiring a C compiler, encapped data can be
**      written into an already compiled ELF executable.
**
**      Because this method is closely tied to the ELF format, it
**      cannot be used with systems besides Linux (unless they happen
**      to also use ELF):
**
**      https://en.wikipedia.org/wiki/Executable_and_Linkable_Format
**
***********************************************************************/

#ifndef __cplusplus
    // See feature_test_macros(7)
    // This definition is redundant under C++
    #define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <poll.h>
#include <fcntl.h>              /* Obtain O_* constant definitions */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "reb-host.h"

#include <elf.h>

//
//  OS_Read_Embedded: C
//
REBYTE * OS_Read_Embedded (REBI64 *script_size)
{
#ifdef __LP64__
    Elf64_Ehdr file_header;
    Elf64_Shdr *sec_headers;
#else
    Elf32_Ehdr file_header;
    Elf32_Shdr *sec_headers;
#endif

#define PAYLOAD_NAME ".EmbEddEdREbol"

    FILE *script = NULL;
    size_t nbytes = 0;
    int i = 0;
    char *ret = NULL;
    char *embedded_script = NULL;
    size_t sec_size;
    char *shstr;

    script = fopen("/proc/self/exe", "r");
    if (script == NULL) return NULL;

    nbytes = fread(&file_header, sizeof(file_header), 1, script);
    if (nbytes < 1) {
        fclose(script);
        return NULL;
    }

    sec_size = cast(size_t, file_header.e_shnum) * file_header.e_shentsize;

#ifdef __LP64__
    sec_headers = cast(Elf64_Shdr*, OS_ALLOC_ARRAY(char, sec_size));
#else
    sec_headers = cast(Elf32_Shdr*, OS_ALLOC_ARRAY(char, sec_size));
#endif

    if (sec_headers == NULL) {
        fclose(script);
        return NULL;
    }

    if (fseek(script, file_header.e_shoff, SEEK_SET) < 0) {
        OS_FREE(sec_headers);
        fclose(script);
        return NULL;
    }

    nbytes = fread(sec_headers, file_header.e_shentsize, file_header.e_shnum, script);
    if (nbytes < file_header.e_shnum) {
        ret = NULL;
        goto header_failed;
    }

    shstr = OS_ALLOC_ARRAY(char, sec_headers[file_header.e_shstrndx].sh_size);
    if (shstr == NULL) {
        ret = NULL;
        goto header_failed;
    }

    if (fseek(script, sec_headers[file_header.e_shstrndx].sh_offset, SEEK_SET) < 0) {
        ret = NULL;
        goto shstr_failed;
    }

    nbytes = fread(shstr, sec_headers[file_header.e_shstrndx].sh_size, 1, script);
    if (nbytes < 1) {
        ret = NULL;
        goto shstr_failed;
    }

    for (i = 0; i < file_header.e_shnum; i ++) {
        /* check the section name */
        if (!strncmp(shstr + sec_headers[i].sh_name, PAYLOAD_NAME, sizeof(PAYLOAD_NAME))) {
            *script_size = sec_headers[i].sh_size;
            break;
        }
    }

    if (i == file_header.e_shnum) {
        ret = NULL;
        goto cleanup;
    }

    /* will be free'ed by RL_Start */
    embedded_script = OS_ALLOC_ARRAY(char, sec_headers[i].sh_size);
    if (embedded_script == NULL) {
        ret = NULL;
        goto shstr_failed;
    }
    if (fseek(script, sec_headers[i].sh_offset, SEEK_SET) < 0) {
        ret = NULL;
        goto embedded_failed;
    }

    nbytes = fread(embedded_script, 1, sec_headers[i].sh_size, script);
    if (nbytes < sec_headers[i].sh_size) {
        ret = NULL;
        goto embedded_failed;
    }

    ret = embedded_script;
    goto cleanup;

embedded_failed:
    OS_FREE(embedded_script);
cleanup:
shstr_failed:
    OS_FREE(shstr);
header_failed:
    OS_FREE(sec_headers);
    fclose(script);
    return b_cast(ret);
}
