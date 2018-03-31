//
//  File: %f-random.c
//  Summary: "random number generation"
//  Section: functional
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"

/*  This program by D E Knuth is in the public domain and freely copyable.
 *  It is explained in Seminumerical Algorithms, 3rd edition, Section 3.6
 *  (or in the errata to the 2nd edition --- see
 *      http://www-cs-faculty.stanford.edu/~knuth/taocp.html
 *  in the changes to Volume 2 on pages 171 and following).             */

/*  N.B. The MODIFICATIONS introduced in the 9th printing (2002) are
        included here; there's no backwards compatibility with the original. */

/*  This version also adopts Brendan McKay's suggestion to
        accommodate naive users who forget to call Set_Random (seed).           */

/*  If you find any bugs, please report them immediately to
 *               taocp@cs.stanford.edu
 *  (and you will be rewarded if the bug is genuine). Thanks!           */

/************ see the book for explanations and caveats! *******************/
/************ in particular, you need two's complement arithmetic **********/

/* Modified by Ladislav Mecir for REBOL to generate 62-bit numbers */

#define KK 100                              /* the long lag */
#define LL  37                              /* the short lag */
#define MM ((REBI64)1<<62)                  /* the modulus, 2^62 */
#define mod_diff(x,y) (((x)-(y))&(MM-1))    /* subtraction mod MM */

static REBI64 ran_x[KK];                    /* the generator state */

void ran_array(REBI64 aa[], int n)
{
    int i,j;
    for (j=0;j<KK;j++) aa[j]=ran_x[j];
    for (;j<n;j++) aa[j]=mod_diff(aa[j-KK],aa[j-LL]);
    for (i=0;i<LL;i++,j++) ran_x[i]=mod_diff(aa[j-KK],aa[j-LL]);
    for (;i<KK;i++,j++) ran_x[i]=mod_diff(aa[j-KK],ran_x[i-LL]);
}

/* the following routines are from exercise 3.6--15 */
/* after calling Set_Random, get new randoms by, e.g., "x=ran_arr_next()" */

#define QUALITY 1009 /* recommended quality level for high-res use */
static REBI64 ran_arr_buf[QUALITY];
static REBI64 ran_arr_dummy=-1, ran_arr_started=-1;
static REBI64 *ran_arr_ptr=&ran_arr_dummy;  /* the next random number, or -1 */

#define TT  70      /* guaranteed separation between streams */
#define is_odd(x)   ((x)&1)         /* units bit of x */

//
//  Set_Random: C
//
void Set_Random(REBI64 seed)
{
    int t,j;
    REBI64 x[KK+KK-1];                  /* the preparation buffer */
    REBI64 ss=(seed+2)&(MM-2);
    for (j=0;j<KK;j++) {
        x[j]=ss;                        /* bootstrap the buffer */
        ss<<=1; if (ss>=MM) ss-=MM-2;   /* cyclic shift 61 bits */
    }
    x[1]++;             /* make x[1] (and only x[1]) odd */
    for (ss=seed&(MM-1),t=TT-1; t;) {
        for (j=KK-1;j>0;j--) x[j+j]=x[j], x[j+j-1]=0; /* "square" */
        for (j=KK+KK-2;j>=KK;j--)
            x[j-(KK-LL)]=mod_diff(x[j-(KK-LL)],x[j]),
            x[j-KK]=mod_diff(x[j-KK],x[j]);
        if (is_odd(ss)) {               /* "multiply by z" */
            for (j=KK;j>0;j--)  x[j]=x[j-1];
            x[0]=x[KK];         /* shift the buffer cyclically */
            x[LL]=mod_diff(x[LL],x[KK]);
        }
        if (ss) ss>>=1; else t--;
    }
    for (j=0;j<LL;j++) ran_x[j+KK-LL]=x[j];
    for (;j<KK;j++) ran_x[j-LL]=x[j];
    for (j=0;j<10;j++) ran_array(x,KK+KK-1); /* warm things up */
    ran_arr_ptr=&ran_arr_started;
}

#define ran_arr_next() (*ran_arr_ptr>=0? *ran_arr_ptr++: ran_arr_cycle())
static REBI64 ran_arr_cycle(void)
{
    if (ran_arr_ptr==&ran_arr_dummy)
        Set_Random(314159L); /* the user forgot to initialize */
    ran_array(ran_arr_buf,QUALITY);
    ran_arr_buf[KK]=-1;
    ran_arr_ptr=ran_arr_buf+1;
    return ran_arr_buf[0];
}

//
//  Random_Int: C
//
// Return random integer. Secure uses SHA1 for better safety.
//
REBI64 Random_Int(REBOOL secure)
{
    REBI64 tmp;
    tmp = ran_arr_next();

    if (secure) {
        REBYTE srcbuf[20], dstbuf[20];

        memcpy(srcbuf, &tmp, sizeof(tmp));
        memset(srcbuf + sizeof(tmp), *(REBYTE*)&tmp, 20 - sizeof(tmp));

        SHA1(srcbuf, 20, dstbuf);
        memcpy(&tmp, dstbuf, sizeof(tmp));
    }

    return tmp;
}

//
//  Random_Range: C
//
REBI64 Random_Range(REBI64 r, REBOOL secure)
{
    if (r == 0)
        return 0;

    REBU64 s = (r < 0) ? -r : r;
    if (NOT(secure) && s > MM)
        fail (Error_Overflow_Raw());

    REBU64 m; // rejection limit
    if (secure)
        m = UINT64_MAX - (UINT64_MAX - s + 1) % s;
    else
        m = MM - MM % s - 1; 

    REBU64 u;
    do {
        u = Random_Int(secure);
    } while (u > m); // get a random value below the limit

    u = u % s + 1;
    return (r > 0) ? cast(REBI64, u) : -cast(REBI64, u);
}

//
//  Random_Dec: C
//
REBDEC Random_Dec(REBDEC r, REBOOL secure)
{
    REBDEC t, s;
    t = secure ? 5.4210108624275222e-20 /* 2^-64 */ :  2.1684043449710089e-19 /* 2^-62 */;
    /* care is taken to never overflow and yield a correct sign */
    s = (REBDEC)Random_Int(secure);
    if (s < 0.0) s += 1.8446744073709552e19;
    return (s * t) * r;
}
