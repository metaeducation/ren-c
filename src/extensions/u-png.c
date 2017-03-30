//
//  File: %u-png.c
//  Summary: "PNG image format conversion"
//  Section: utility
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
// This is an optional part of R3. This file can be replaced by
// library function calls into an updated implementation.
//

#include "sys-core.h"
#include "sys-zlib.h"
#include <ctype.h> // remove this later !!!!

#include "sys-ext.h"
#include "tmp-mod-upng-first.h"

#ifdef STRICT_BOOL_COMPILER_TEST
    //
    // This is third party code that is not written to use REBOOL, and hence
    // the definitions of TRUE and FALSE used in the "fake" build will trip
    // it up.  We substitute in normal definitions for this file.  See
    // the explanations of this test in %reb-c.h for more information.
    //
    #undef TRUE
    #undef FALSE
    #define TRUE 1
    #define FALSE 0
#endif

#if defined(ENDIAN_LITTLE)
#define CVT_END_L(a) a=(a<<24)|(((a>>8)&255)<<16)|(((a>>16)&255)<<8)|(a>>24)
#elif defined(ENDIAN_BIG)
#define CVT_END_L(a)
#else
#error Endianness must be defined in system.h
#endif

#define int_abs(a) (((a)<0)?(-(a)):(a))

/**********************************************************************/

static struct png_ihdr {
    unsigned int width;
    unsigned int height;
    unsigned char bit_depth;
    unsigned char color_type;
    unsigned char compression_method;
    unsigned char filter_method;
    unsigned char interlace_method;
} png_ihdr;

static unsigned char colormodes[]={0x1f,0x00,0x18,0x0f,0x18,0x00,0x18};
static unsigned char colormult[]={1,0,3,1,2,0,4};

static unsigned char adam7hoff[]={0,4,0,2,0,1,0};
static unsigned char adam7hskip[]={8,8,4,4,2,2,1};
static unsigned char adam7voff[]={0,0,4,0,2,0,1};
static unsigned char adam7vskip[]={8,8,8,4,4,2,2};
static unsigned char bytetab2[]={0x00,0x55,0xaa,0xff};

static int log2bitdepth;
static char haspalette;
static int bytesperpixel;
static int bitsperpixel;
static int rowlength;
static char hasalpha;
static unsigned char *imgbuffer;
static unsigned int palette[256];
static unsigned short palette_alpha[256];
static unsigned int *img_output;
static unsigned int transparent_red,transparent_green,transparent_blue;
static unsigned int transparent_gray;
static void (*process_row)(unsigned char *p,int width,int r,int hoff,int hskip);

typedef void (*ROW_PROCESSOR)(unsigned char *, int, int, int, int);

static void process_row_0_1(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_0_2(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_0_4(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_0_8(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_0_16(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_2_8(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_2_16(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_3_1(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_3_2(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_3_4(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_3_8(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_4_8(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_4_16(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_6_8(unsigned char *p,int width,int r,int hoff,int hskip);
static void process_row_6_16(unsigned char *p,int width,int r,int hoff,int hskip);

static ROW_PROCESSOR process_row0[] = {
    process_row_0_1, process_row_0_2, process_row_0_4,
    process_row_0_8, process_row_0_16
};
static ROW_PROCESSOR process_row2[] = {
    NULL, NULL, NULL, process_row_2_8, process_row_2_16
};
static ROW_PROCESSOR process_row3[] = {
    process_row_3_1, process_row_3_2, process_row_3_4,
    process_row_3_8, NULL
};
static ROW_PROCESSOR process_row4[] = {
    NULL, NULL, NULL, process_row_4_8, process_row_4_16
};
static ROW_PROCESSOR process_row6[] = {
    NULL, NULL, NULL, process_row_6_8, process_row_6_16
};
static ROW_PROCESSOR *process_row_lookup[]={
    process_row0,
    NULL,
    process_row2,
    process_row3,
    process_row4,
    NULL,
    process_row6
};


jmp_buf png_state;

static void trap_png(void)
{
    longjmp(png_state, 1);
}

/**********************************************************************/

static int find_msb(int val) {
    int i;
    for(i=30;val<(1<<i);i--);
    return i;
}

static int is_supported_chunk(unsigned char *p) {
    if(memcmp(p,"IHDR",4)&&memcmp(p,"IDAT",4)&&
     memcmp(p,"PLTE",4)&&memcmp(p,"IEND",4)&&
     memcmp(p,"tRNS",4)) {
        if(p[0]&0x20)
            return 0;
        else
            trap_png();
    }
    return 1;
}

static unsigned char *get_chunk(unsigned char **bufp,int *np,char *type,int *lenp) {
    unsigned char *p,*rp;
    int n;
    unsigned int len;
    p=*bufp;
    n=*np;
    while(1) {
        if(n<12)
            trap_png();
        if((!isalpha(p[4]))||(!isalpha(p[5]))||(!isalpha(p[6]))||(!isalpha(p[7])))
            trap_png();
        memcpy(&len,p,4);
        CVT_END_L(len);
        if(n<((int)(12+len)))
            trap_png();
        if(!is_supported_chunk(p+4)) {
            p+=12+len;
            n-=12+len;
            continue;
        }
        *lenp=len;
        rp=p+8;
        memcpy(type,p+4,4);
        p+=12+len;
        n-=12+len;
        *bufp=p;
        *np=n;
        return rp;
    }
}

static void process_chunk(char *type,unsigned char *p,int length) {
    int i;
    if(!memcmp(type,"PLTE",4)) {
        if((length%3)||(length>256*3))
            trap_png();
        for(i=0;i<length/3;i++) {
            palette[i]=(p[0]<<16)|(p[1]<<8)|p[2];
            palette_alpha[i]=65535;
            p+=3;
        }
        haspalette=1;
    } else if(!memcmp(type,"tRNS",4)) {
        switch(png_ihdr.color_type) {
            case 0:
                transparent_gray=(p[0]<<8)|p[1];
                break;
            case 2:
                transparent_red=(p[0]<<8)|p[1];
                transparent_green=(p[2]<<8)|p[3];
                transparent_blue=(p[4]<<8)|p[5];
                break;
            case 3:
                if(length>256)
                    length=256;
                for(i=0;i<length;i++)
                    palette_alpha[i]=(p[i]<<8)|p[i];
                break;
        }
    }
}

static unsigned int calc_color(unsigned int color,unsigned short alpha) {
    if(alpha==65535){
        return TO_PIXEL_COLOR(color>>16, color>>8&255, color&255, 0xff);
    }else if(alpha==0) {
        hasalpha=TRUE;
        return 0x00000000;
    } else {
        unsigned int red,green,blue;
        hasalpha=TRUE;
        red=color>>16;
        green=(color>>8)&255;
        blue=color&255;
        return TO_PIXEL_COLOR(red, green, blue, (alpha/255));
    }
}

static void process_row_0_1(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned char m = 0xAE; // overwritten, but avoid uninitialized warning 
    unsigned int v,*imgp;
    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        if(!(c&7))
            m=*p++;
        v=m>>7;
        if(v==transparent_gray) {
            hasalpha=TRUE;
            *imgp=0x00000000;
        } else {
            v ? v=0xff : 0x00;
            *imgp=TO_PIXEL_COLOR(v, v, v, 0xff);
        }
        imgp+=hskip;
        m<<=1;
    }
}

static void process_row_0_2(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned char m = 0xAE; // overwritten, but avoid uninitialized warning 
    unsigned int v,*imgp;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        if(!(c&3))
            m=*p++;
        v=m>>6;
        if(v==transparent_gray) {
            hasalpha=TRUE;
            *imgp=0x00000000;
        } else {
            v=bytetab2[v];
            *imgp=TO_PIXEL_COLOR(v, v, v, 0xff);
        }
        imgp+=hskip;
        m<<=2;
    }
}

static void process_row_0_4(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned char m = 0xAE; // overwritten, but avoid uninitialized warning 
    unsigned int v,*imgp;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        if(!(c&1))
            m=*p++;
        v=m>>4;
        if(v==transparent_gray) {
            hasalpha=TRUE;
            *imgp=0x00000000;
        } else {
            v|=(v<<4);
            *imgp=TO_PIXEL_COLOR(v, v, v, 0xff);
        }
        imgp+=hskip;
        m<<=4;
    }
}

static void process_row_0_8(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned int v,*imgp;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        v=*p++;
        if(v==transparent_gray) {
            hasalpha=TRUE;
            *imgp=0x00000000;
        } else {
            *imgp=TO_PIXEL_COLOR(v, v, v, 0xff);
        }
        imgp+=hskip;
    }
}

static void process_row_0_16(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned int v,*imgp;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        v=(p[0]<<8)|p[1];
        p+=2;
        if(v==transparent_gray) {
            hasalpha=TRUE;
            *imgp=0x00000000;
        } else {
            v>>=8;
            *imgp=TO_PIXEL_COLOR(v, v, v, 0xff);
        }
        imgp+=hskip;
    }
}

static void process_row_2_8(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned int *imgp,red,green,blue;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        red=p[0];
        green=p[1];
        blue=p[2];
        p+=3;
        if((red==transparent_red)&&(green==transparent_green)&&(blue==transparent_blue)) {
            hasalpha=TRUE;
            *imgp=0x00000000;
        } else
            *imgp=TO_PIXEL_COLOR(red, green, blue, 0xff);
        imgp+=hskip;
    }
}

static void process_row_2_16(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned int *imgp,red,green,blue;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        red=(p[0]<<8)|p[1];
        green=(p[2]<<8)|p[3];
        blue=(p[4]<<8)|p[5];
        p+=6;
        if((red==transparent_red)&&(green==transparent_green)&&(blue==transparent_blue)) {
            hasalpha=TRUE;
            *imgp=0x00000000;
        } else
            *imgp=TO_PIXEL_COLOR((red>>8), (green&0xff00), (blue>>8), 0xff);
        imgp+=hskip;
    }
}

static void process_row_3_1(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned char m = 0xAE; // overwritten, but avoid uninitialized warning 
    unsigned int v,*imgp;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        if(!(c&7))
            m=*p++;
        v=m>>7;
        *imgp=calc_color(palette[v],palette_alpha[v]);
        imgp+=hskip;
        m<<=1;
    }
}

static void process_row_3_2(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned char m = 0xAE; // overwritten, but avoid uninitialized warning 
    unsigned int v,*imgp;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        if(!(c&3))
            m=*p++;
        v=m>>6;
        *imgp=calc_color(palette[v],palette_alpha[v]);
        imgp+=hskip;
        m<<=2;
    }
}

static void process_row_3_4(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned char m = 0xAE; // overwritten, but avoid uninitialized warning 
    unsigned int v,*imgp;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        if(!(c&1))
            m=*p++;
        v=m>>4;
        *imgp=calc_color(palette[v],palette_alpha[v]);
        imgp+=hskip;
        m<<=4;
    }
}

static void process_row_3_8(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned int v,*imgp;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        v=*p++;
        *imgp=calc_color(palette[v],palette_alpha[v]);
        imgp+=hskip;
    }
}

static void process_row_4_8(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned int v,*imgp,alpha;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        v=*p++;
        alpha=*p++;
        v|=(v<<8)|(v<<16);
        *imgp=calc_color(v,(unsigned short)((alpha<<8)|alpha));
        imgp+=hskip;
    }
}

static void process_row_4_16(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned int v,*imgp,alpha;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        v=p[0];
        alpha=(p[2]<<8)|p[3];
        p+=4;
        v|=(v<<8)|(v<<16);
        *imgp=calc_color(v,(unsigned short)alpha);
        imgp+=hskip;
    }
}

static void process_row_6_8(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned int v,*imgp,alpha;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        v=(p[0]<<16)|(p[1]<<8)|p[2];
        alpha=p[3];
        p+=4;
        *imgp=calc_color(v,(unsigned short)((alpha<<8)|alpha));
        imgp+=hskip;
    }
}

static void process_row_6_16(unsigned char *p,int width,int r,int hoff,int hskip) {
    int c;
    unsigned int v,*imgp,alpha;

    imgp=img_output+r*png_ihdr.width+hoff;
    for(c=0;c<width;c++) {
        v=(p[0]<<16)|(p[2]<<8)|p[4];
        alpha=(p[6]<<8)|p[7];
        p+=8;
        *imgp=calc_color(v,(unsigned short)alpha);
        imgp+=hskip;
    }
}

static int paeth_predictor(int a,int b,int c) {
    int p,pa,pb,pc;

    p=a+b-c;
    pa=int_abs(p-a);
    pb=int_abs(p-b);
    pc=int_abs(p-c);
    if((pa<=pb)&&(pa<=pc))
        return a;
    else if(pb<=pc)
        return b;
    return c;
}

static void process_image(int width,int height,int cwidth,int hoff,int hskip,
 int voff,int vskip) {
    int r,c;
    unsigned char *p,filter;
    //printf("process_image: w: %d, h: %d, cw: %d, ho: %d, hs: %d, vo: %d, vs: %d\r\n",
    // width,height,cwidth,hoff,hskip,voff,vskip);
    //printf("bpp: %d\r\n",bytesperpixel);
    for(r=1;r<=height;r++) {
        p=imgbuffer+r*rowlength+bytesperpixel-1;
        filter=*p++;
        //printf("filter: %d\r\n",filter);
        for(c=1;c<=bytesperpixel;c++)
            p[-c]=0;
        switch(filter) {
            case 1:
                for(c=0;c<cwidth;c++)
                    p[c]+=p[c-bytesperpixel];
                break;
            case 2:
                for(c=0;c<cwidth;c++)
                    p[c]+=p[c-rowlength];
                break;
            case 3:
                for(c=0;c<cwidth;c++)
                    p[c]+=(p[c-bytesperpixel]+p[c-rowlength])/2;
                break;
            case 4:
                for(c=0;c<cwidth;c++)
                    p[c]+=paeth_predictor(p[c-bytesperpixel],p[c-rowlength],p[c-rowlength-bytesperpixel]);
                break;
        }
        process_row(p,width,voff+(r-1)*vskip,hoff,hskip);
    }
}

int png_info(unsigned char *buffer, int nbytes, int *w, int *h) {
    unsigned char *p;
    int length;
    char type[4];

    if(nbytes<45) return 0;
    buffer+=8;
    nbytes-=8;
    p=get_chunk(&buffer,&nbytes,type,&length);
    if(memcmp(type,"IHDR",4)||(length!=13)) return 0;
    memcpy(&png_ihdr,p,sizeof(png_ihdr));
    CVT_END_L(png_ihdr.width);
    CVT_END_L(png_ihdr.height);
    if((!png_ihdr.bit_depth)||(!png_ihdr.width)||(!png_ihdr.height)) return 0;
    log2bitdepth=find_msb(png_ihdr.bit_depth);
    if((log2bitdepth>4)||(png_ihdr.color_type>6)||png_ihdr.compression_method||
     png_ihdr.filter_method||(png_ihdr.interlace_method>1)||
     (!(colormodes[png_ihdr.color_type]&(1<<log2bitdepth))))
        return 0;
    if (w == 0) return 1; // (just a check)
    process_row=(void (*)(unsigned char *,int,int,int,int))
     (process_row_lookup[png_ihdr.color_type][log2bitdepth]);
    *w=png_ihdr.width;
    *h=png_ihdr.height;
    return 1;
}

void png_load(unsigned char *buffer, int nbytes, char *output, BOOL *alpha) {
    unsigned char *p;
    int length,ret,adam7pass;
    int awidth,aheight,r,comp_awidth;
    char type[4];
//  z_stream zstream={0}; // Ren/C: changed for -Wmissing-field-initializers
    z_stream zstream;
    memset(&zstream, '\0', sizeof(zstream));

    img_output=(unsigned int *)output;
    buffer+=33;
    nbytes-=33;
    haspalette=0;
    hasalpha=0;
    transparent_gray=transparent_red=transparent_green=transparent_blue=0x00ffffff;
    while(1) {
        p=get_chunk(&buffer,&nbytes,type,&length);
        if(!memcmp(type,"IEND",4))
            trap_png();
        else if(!memcmp(type,"IDAT",4))
            break;
        else
            process_chunk(type,p,length);
    }
    if((png_ihdr.color_type==3)&&(!haspalette))
        trap_png();
    bitsperpixel=png_ihdr.bit_depth*colormult[png_ihdr.color_type];
    bytesperpixel=(bitsperpixel+7)/8;
    rowlength=bytesperpixel+(png_ihdr.width*bitsperpixel+7)/8;
    zstream.next_in=p;
    zstream.avail_in=length;
    ret=inflateInit(&zstream);
    if(ret!=Z_OK)
        trap_png();
    if(png_ihdr.interlace_method) {
        imgbuffer=(unsigned char*)malloc(rowlength*((png_ihdr.height+1)/2+1));
        for(adam7pass=0;adam7pass<7;adam7pass++) {
            awidth=(((int)png_ihdr.width)-adam7hoff[adam7pass]+adam7hskip[adam7pass]-1)/adam7hskip[adam7pass];
            aheight=(((int)png_ihdr.height)-adam7voff[adam7pass]+adam7vskip[adam7pass]-1)/adam7vskip[adam7pass];
            if((!awidth)||(!aheight))
                continue;
            comp_awidth=1+(awidth*bitsperpixel+7)/8;
            memset(imgbuffer,0,rowlength);
            for(r=1;r<=aheight;r++) {
                zstream.next_out=imgbuffer+r*rowlength+bytesperpixel-1;
                zstream.avail_out=comp_awidth;
                while(1) {
                    ret=inflate(&zstream,0);
                    if(((ret==Z_OK)||(ret==Z_STREAM_END))&&(!zstream.avail_out))
                        break;
                    if(((ret==Z_OK)||(ret==Z_BUF_ERROR))&&(!zstream.avail_in)) {
                        p=get_chunk(&buffer,&nbytes,type,&length);
                        if(!memcmp(type,"IDAT",4)) {
                            zstream.next_in=p;
                            zstream.avail_in=length;
                            continue;
                        }
                    }
                    goto error;
                }
            }
            process_image(awidth,aheight,comp_awidth-1,adam7hoff[adam7pass],
             adam7hskip[adam7pass],adam7voff[adam7pass],adam7vskip[adam7pass]);
        }
    } else {
        imgbuffer=(unsigned char*)malloc(rowlength*(png_ihdr.height+1));
        comp_awidth=1+(png_ihdr.width*bitsperpixel+7)/8;
        memset(imgbuffer,0,rowlength);
        for(r=1;r<=(int)png_ihdr.height;r++) {
            zstream.next_out=imgbuffer+r*rowlength+bytesperpixel-1;
            zstream.avail_out=comp_awidth;
            while(1) {
                ret=inflate(&zstream,0);
                if(((ret==Z_OK)||(ret==Z_STREAM_END))&&(!zstream.avail_out))
                    break;
                if(((ret==Z_OK)||(ret==Z_BUF_ERROR))&&(!zstream.avail_in)) {
                    p=get_chunk(&buffer,&nbytes,type,&length);
                    if(!memcmp(type,"IDAT",4)) {
                        zstream.next_in=p;
                        zstream.avail_in=length;
                        continue;
                    }
                }
                goto error;
            }
        }
        process_image(png_ihdr.width,png_ihdr.height,comp_awidth-1,0,1,0,1);
    }
    free(imgbuffer);
    inflateEnd(&zstream);
    *alpha=hasalpha;
    return;
 error:
    free(imgbuffer);
    inflateEnd(&zstream);
    trap_png();
}

#define IDATLENGTH  65536

struct idatnode {
    struct idatnode *next;
    int length;
    unsigned char data[IDATLENGTH];
};

struct ihdrchunk {
    int width;
    int height;
    unsigned char depth;
    unsigned char color_type;
    unsigned char comp_method;
    unsigned char filter_method;
    unsigned char interlace_method;
};


static void emitchunk(unsigned char **cpp,const char *type,const char *data,int length) {
    REBCNT tmplen;
    unsigned char *cp=*cpp,*crcp;

    tmplen=length;
    CVT_END_L(tmplen);
    memcpy(cp,&tmplen,4);
    cp+=4;
    crcp=cp;
    memcpy(cp,type,4);
    cp+=4;
    if(data) {
        memcpy(cp,data,length);
        cp+=length;
    }
    tmplen=CRC32(crcp, length+4);
    CVT_END_L(tmplen);
    memcpy(cp,&tmplen,4);
    cp+=4;
    *cpp=cp;
}


//
//  encode-png: native [
//
//  {Codec for encoding a PNG image}
//
//      return: [binary!]
//      image [image!]
//  ]
//
REBNATIVE(encode_png)
{
    INCLUDE_PARAMS_OF_ENCODE_PNG;

    assert(IS_END(D_OUT)); // evaluator guarantees--used as error signal

    // Error hook in the PNG encoder from R3-Alpha is done via longjmp.
    //
    if (setjmp(png_state)) {
        fail (Error_Bad_Media_Raw()); // can the error be more specific?
    }

    REBINT w = VAL_IMAGE_WIDE(ARG(image));
    REBINT h = VAL_IMAGE_HIGH(ARG(image));
    int hasalpha = Image_Has_Alpha(ARG(image)) ? 1 : 0;

    struct ihdrchunk ihdr;
    struct idatnode *firstidat,*currentidat;
    unsigned char *linebuf,*cp;
    int x,y,imgsize,ret;
    REBCNT *dp,cv;
//  z_stream zstream={0}; // Ren/C: changed for -Wmissing-field-initializers
    z_stream zstream;
    memset(&zstream, '\0', sizeof(zstream));

    ihdr.width=w;
    CVT_END_L(ihdr.width);
    ihdr.height=h;
    CVT_END_L(ihdr.height);
    ihdr.depth=8;
    ihdr.color_type=(hasalpha?6:2);
    ihdr.comp_method=0;
    ihdr.filter_method=0;
    ihdr.interlace_method=0;

    linebuf=(unsigned char*)malloc(hasalpha?(4*w+1):(3*w+1));
    firstidat=currentidat=(struct idatnode*)malloc(sizeof(struct idatnode));

    if(!firstidat) {
        free(linebuf);
        trap_png();
    }

    currentidat->next=0;
    deflateInit(&zstream, Z_DEFAULT_COMPRESSION);
    zstream.next_out=currentidat->data;
    zstream.avail_out=IDATLENGTH;
    dp=VAL_IMAGE_BITS(ARG(image));
    for(y=0;y<h;y++) {
        cp=linebuf;
        *cp++=0;
        for(x=0;x<w;x++) {
            cv=*dp++;
            *cp++=cv>>16;
            *cp++=cv>>8;
            *cp++=cv;
            if(hasalpha)
                *cp++=(cv>>24);
        }
        zstream.next_in=linebuf;
        zstream.avail_in=(hasalpha?(4*w+1):(3*w+1));
        while(zstream.avail_in||(y==h-1)) {
            if(!zstream.avail_out)
                goto refill;
            ret=deflate(&zstream,(y==h-1)?Z_FINISH:0);
            if((ret==Z_OK)||(ret==Z_BUF_ERROR))
                continue;
            if(ret==Z_STREAM_END)
                break;

            goto error;

         refill:
            currentidat->length=IDATLENGTH;
            currentidat->next=(struct idatnode*)malloc(sizeof(struct idatnode));
            currentidat=currentidat->next;
            currentidat->next=0;
            zstream.next_out=currentidat->data;
            zstream.avail_out=IDATLENGTH;
        }
    }
    currentidat->length=IDATLENGTH-zstream.avail_out;
    deflateEnd(&zstream);
    imgsize=8+(12+13)+(12+19)+(12+0);
    currentidat=firstidat;
    while(currentidat) {
        imgsize+=12+currentidat->length;
        currentidat=currentidat->next;
    }

    REBSER *bin; // goto would cross initialization
    bin = Make_Binary(imgsize);
   
    cp=(unsigned char *)BIN_HEAD(bin);
    memcpy(cp,"\211\120\116\107\015\012\032\012", 8);
    cp+=8;
    emitchunk(&cp,"IHDR",(char *)&ihdr,13);
    emitchunk(&cp,"tEXt","Software\000REBOL",14);
    currentidat=firstidat;
    while(currentidat) {
        emitchunk(&cp,"IDAT",(char *)currentidat->data,currentidat->length);
        currentidat=currentidat->next;
    }
    emitchunk(&cp,"IEND",0,0);

    TERM_BIN_LEN(bin, imgsize);

    Init_Binary(D_OUT, bin);
    goto cleanup;

error:
    assert(IS_END(D_OUT));

cleanup:
    free(linebuf);
    while(firstidat) {
        currentidat=firstidat->next;
        free(firstidat);
        firstidat=currentidat;
    }

    if (IS_END(D_OUT))
        fail (Error_Bad_Media_Raw()); // better error?

    assert(IS_BINARY(D_OUT));
    return R_OUT;
}


//
//  identify-png?: native [
//
//  {Codec for identifying BINARY! data for a PNG}
//
//      return: [logic!]
//      data [binary!]
//  ]
//
REBNATIVE(identify_png_q)
{
    INCLUDE_PARAMS_OF_IDENTIFY_PNG_Q;

    REBYTE *data = VAL_BIN_AT(ARG(data));
    REBCNT len = VAL_LEN_AT(ARG(data));

    // Error hook in the PNG decoder from R3-Alpha is done via longjmp.
    //
    if (setjmp(png_state)) {
        return R_FALSE;
    }

    // !!! Should codec identifiers return any optional information they just
    // happen to get?  Intsead of passing NULL for the addresses of the width
    // and the height, this could incidentally get that information back
    // to return it.  Then any non-FALSE result could be "identified" while
    // still being potentially more informative about what was found out.
    //
    if (0 == png_info(data, len, NULL, NULL))
        return R_FALSE;
    return R_TRUE;
}


//
//  decode-png: native [
//
//  {Codec for decoding BINARY! data for a PNG}
//
//      return: [image!]
//      data [binary!]
//  ]
//
REBNATIVE(decode_png)
{
    INCLUDE_PARAMS_OF_DECODE_PNG;

    REBYTE *data = VAL_BIN_AT(ARG(data));
    REBCNT len = VAL_LEN_AT(ARG(data));

    // Error hook in the PNG decoder from R3-Alpha is done via longjmp.
    //
    if (setjmp(png_state)) {
        fail (Error_Bad_Media_Raw()); // can the error be more specific?
    }

    int w, h;
    if (0 == png_info(data, len, &w, &h))
        trap_png(); // longjmps to the "bad media" call above.

    REBSER *ser = Make_Image(w, h, TRUE);

    BOOL alpha = 0; // Note: not a REBOOL (PNG lib has minimal modification)
    png_load(data, len, cast(char*, IMG_DATA(ser)), &alpha);

    Init_Image(D_OUT, ser);

    // !!! This was commented out?
    // if (alpha) VAL_IMAGE_TRANSP(D_OUT) = VITT_ALPHA;

    return R_OUT;
}


#include "tmp-mod-upng-last.h"
