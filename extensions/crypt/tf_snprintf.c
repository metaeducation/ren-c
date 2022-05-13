// Note: The reason we incorporate this file is because mbedTLS's oid.c uses
// snprintf for a somewhat trivial reason, and if we want to use their
// `#define MBEDTLS_PKCS1_V15` that pulls in that code.  (It's how they get
// hash sizes from the hash in the enum.)  Someone made this file as an
// alternative:
//
// https://github.com/Mbed-TLS/mbedtls/issues/929
//
// The code was later put into the "trusted firmware libc":
//
// https://github.com/ARM-software/arm-trusted-firmware/commit/870ce3ddd3b33c59418a7dba703e8a66ec75f98f
//
// But we use it in its original form.  Because the "system" that mbedtls
// headers include (e.g. via #include <stdlib.h> or similar) don't get a
// prototype for this function, it has to be passed in via a function pointer
// in the crypto module startup with mbedtls_platform_set_snprintf().
//
// The setting that provides that hook is MBEDTLS_PLATFORM_SNPRINTF_ALT.


/*
 * Copyright (c) 2017, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

//#include <debug.h>  // trusted firmware file, not in mbedtls
#include <mbedtls/include/mbedtls/platform.h>  // path changed
#include <stdarg.h>

#include "rebol.h"  // added for panic()

static void unsigned_dec_print(char **s, size_t n, size_t *chars_printed,
                   unsigned int unum)
{
    /* Enough for a 32-bit unsigned decimal integer (4294967295). */
    unsigned char num_buf[10];
    int i = 0, rem;

    do {
        rem = unum % 10;
        num_buf[i++] = '0' + rem;
    } while (unum /= 10);

    while (--i >= 0) {
        if (*chars_printed < n)
            *(*s)++ = num_buf[i];
        (*chars_printed)++;
    }
}

/*******************************************************************
 * Reduced snprintf to be used for Trusted firmware.
 * The following type specifiers are supported:
 *
 * %d or %i - signed decimal format
 * %u - unsigned decimal format
 *
 * The function panics on all other formats specifiers.
 *
 * It returns the number of characters that would be written if the
 * buffer was big enough. If it returns a value lower than n, the
 * whole string has been written.
 *******************************************************************/
int tf_snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list args;
    int num;
    unsigned int unum;
    size_t chars_printed = 0;

    if (n == 1) {
        /* Buffer is too small to actually write anything else. */
        *s = '\0';
        n = 0;
    } else if (n >= 2) {
        /* Reserve space for the terminator character. */
        n--;
    }

    va_start(args, fmt);
    while (*fmt) {

        if (*fmt == '%') {
            fmt++;
            /* Check the format specifier. */
            switch (*fmt) {
            case 'i':
            case 'd':
                num = va_arg(args, int);

                if (num < 0) {
                    if (chars_printed < n)
                        *s++ = '-';
                    chars_printed++;

                    unum = (unsigned int)-num;
                } else {
                    unum = (unsigned int)num;
                }

                unsigned_dec_print(&s, n, &chars_printed, unum);
                break;
            case 'u':
                unum = va_arg(args, unsigned int);
                unsigned_dec_print(&s, n, &chars_printed, unum);
                break;
            default:
                /* Panic on any other format specifier. */

                // Comment out the trusted firmware code
                /*ERROR("tf_snprintf: specifier with ASCII code '%d' not supported.",
                      *fmt);
                plat_panic_handler(); */

                // Replace with a Ren-C panic
                rebJumps("panic [",
                   "{unsupported tf_snprintf specifier, see tf_snprintf.c}",
                "]");
            }
            fmt++;
            continue;
        }

        if (chars_printed < n)
            *s++ = *fmt;
        fmt++;
        chars_printed++;
    }

    va_end(args);

    if (n > 0)
        *s = '\0';

    return chars_printed;
}
