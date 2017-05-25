/*
 * Copyright (c) 2003 The FFmpeg Project.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * How to use this decoder:
 * SVQ3 data is transported within Apple Quicktime files. Quicktime files
 * have stsd atoms to describe media trak properties. A stsd atom for a
 * video trak contains 1 or more ImageDescription atoms. These atoms begin
 * with the 4-byte length of the atom followed by the codec fourcc. Some
 * decoders need information in this atom to operate correctly. Such
 * is the case with SVQ3. In order to get the best use out of this decoder,
 * the calling app must make the SVQ3 ImageDescription atom available
 * via the AVCodecContext's extradata[_size] field:
 *
 * AVCodecContext.extradata = pointer to ImageDescription, first characters 
 * are expected to be 'S', 'V', 'Q', and '3', NOT the 4-byte atom length
 * AVCodecContext.extradata_size = size of ImageDescription atom memory 
 * buffer (which will be the same as the ImageDescription atom size field 
 * from the QT file, minus 4 bytes since the length is missing)
 *
 * You will know you have these parameters passed correctly when the decoder
 * correctly decodes this file:
 *  ftp://ftp.mplayerhq.hu/MPlayer/samples/V-codecs/SVQ3/Vertical400kbit.sorenson3.mov
 *
 */
 
/**
 * @file svq3.c
 * svq3 decoder.
 */
#include "common.h"
#include "dsputil.h"
#include "avcodec.h"


#define FULLPEL_MODE  1 
#define HALFPEL_MODE  2 
#define THIRDPEL_MODE 3
#define PREDICT_MODE  4


 
/* dual scan (from some older h264 draft)
 o-->o-->o   o
         |  /|
 o   o   o / o
 | / |   |/  |
 o   o   o   o
   / 
 o-->o-->o-->o
*/
static const uint8_t svq3_scan[16]={
 0+0*4, 1+0*4, 2+0*4, 2+1*4,
 2+2*4, 3+0*4, 3+1*4, 3+2*4,
 0+1*4, 0+2*4, 1+1*4, 1+2*4,
 0+3*4, 1+3*4, 2+3*4, 3+3*4,
};

static const uint8_t svq3_pred_0[25][2] = {
  { 0, 0 },
  { 1, 0 }, { 0, 1 },
  { 0, 2 }, { 1, 1 }, { 2, 0 },
  { 3, 0 }, { 2, 1 }, { 1, 2 }, { 0, 3 },
  { 0, 4 }, { 1, 3 }, { 2, 2 }, { 3, 1 }, { 4, 0 },
  { 4, 1 }, { 3, 2 }, { 2, 3 }, { 1, 4 },
  { 2, 4 }, { 3, 3 }, { 4, 2 },
  { 4, 3 }, { 3, 4 },
  { 4, 4 }
};
static const int8_t svq3_pred_1[6][6][5] = {
  { { 2,-1,-1,-1,-1 }, { 2, 1,-1,-1,-1 }, { 1, 2,-1,-1,-1 },
    { 2, 1,-1,-1,-1 }, { 1, 2,-1,-1,-1 }, { 1, 2,-1,-1,-1 } },
  { { 0, 2,-1,-1,-1 }, { 0, 2, 1, 4, 3 }, { 0, 1, 2, 4, 3 },
    { 0, 2, 1, 4, 3 }, { 2, 0, 1, 3, 4 }, { 0, 4, 2, 1, 3 } },
  { { 2, 0,-1,-1,-1 }, { 2, 1, 0, 4, 3 }, { 1, 2, 4, 0, 3 },
    { 2, 1, 0, 4, 3 }, { 2, 1, 4, 3, 0 }, { 1, 2, 4, 0, 3 } },
  { { 2, 0,-1,-1,-1 }, { 2, 0, 1, 4, 3 }, { 1, 2, 0, 4, 3 },
    { 2, 1, 0, 4, 3 }, { 2, 1, 3, 4, 0 }, { 2, 4, 1, 0, 3 } },
  { { 0, 2,-1,-1,-1 }, { 0, 2, 1, 3, 4 }, { 1, 2, 3, 0, 4 },
    { 2, 0, 1, 3, 4 }, { 2, 1, 3, 0, 4 }, { 2, 0, 4, 3, 1 } },
  { { 0, 2,-1,-1,-1 }, { 0, 2, 4, 1, 3 }, { 1, 4, 2, 0, 3 },
    { 4, 2, 0, 1, 3 }, { 2, 0, 1, 4, 3 }, { 4, 2, 1, 0, 3 } },
};
static const struct { uint8_t run; uint8_t level; } svq3_dct_tables[2][16] = {
  { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 }, { 0, 2 }, { 3, 1 }, { 4, 1 }, { 5, 1 },
    { 0, 3 }, { 1, 2 }, { 2, 2 }, { 6, 1 }, { 7, 1 }, { 8, 1 }, { 9, 1 }, { 0, 4 } },
  { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 0, 2 }, { 2, 1 }, { 0, 3 }, { 0, 4 }, { 0, 5 },
    { 3, 1 }, { 4, 1 }, { 1, 2 }, { 1, 3 }, { 0, 6 }, { 0, 7 }, { 0, 8 }, { 0, 9 } }
};
static const uint32_t svq3_dequant_coeff[32] = {
   3881,  4351,  4890,  5481,  6154,  6914,  7761,  8718,
   9781, 10987, 12339, 13828, 15523, 17435, 19561, 21873,
  24552, 27656, 30847, 34870, 38807, 43747, 49103, 54683,
  61694, 68745, 77615, 89113,100253,109366,126635,141533
};
static void svq3_luma_dc_dequant_idct_c(DCTELEM *block, int qp){
    const int qmul= svq3_dequant_coeff[qp];
#define stride 16
    int i;
    int temp[16];
    static const int x_offset[4]={0, 1*stride, 4* stride,  5*stride};
    static const int y_offset[4]={0, 2*stride, 8* stride, 10*stride};
    for(i=0; i<4; i++){
        const int offset= y_offset[i];
        const int z0= 13*(block[offset+stride*0] +    block[offset+stride*4]);
        const int z1= 13*(block[offset+stride*0] -    block[offset+stride*4]);
        const int z2=  7* block[offset+stride*1] - 17*block[offset+stride*5];
        const int z3= 17* block[offset+stride*1] +  7*block[offset+stride*5];
        temp[4*i+0]= z0+z3;
        temp[4*i+1]= z1+z2;
        temp[4*i+2]= z1-z2;
        temp[4*i+3]= z0-z3;
    }
    for(i=0; i<4; i++){
        const int offset= x_offset[i];
        const int z0= 13*(temp[4*0+i] +    temp[4*2+i]);
        const int z1= 13*(temp[4*0+i] -    temp[4*2+i]);
        const int z2=  7* temp[4*1+i] - 17*temp[4*3+i];
        const int z3= 17* temp[4*1+i] +  7*temp[4*3+i];
        block[stride*0 +offset]= ((z0 + z3)*qmul + 0x80000)>>20;
        block[stride*2 +offset]= ((z1 + z2)*qmul + 0x80000)>>20;
        block[stride*8 +offset]= ((z1 - z2)*qmul + 0x80000)>>20;
        block[stride*10+offset]= ((z0 - z3)*qmul + 0x80000)>>20;
    }
}
#undef stride
static void svq3_add_idct_c (uint8_t *dst, DCTELEM *block, int stride, int qp, int dc){
    const int qmul= svq3_dequant_coeff[qp];
    int i;
    uint8_t *cm = cropTbl + MAX_NEG_CROP;
    if (dc) {
        dc = 13*13*((dc == 1) ? 1538*block[0] : ((qmul*(block[0] >> 3)) / 2));
        block[0] = 0;
    }
    for (i=0; i < 4; i++) {
        const int z0= 13*(block[0 + 4*i] +    block[2 + 4*i]);
        const int z1= 13*(block[0 + 4*i] -    block[2 + 4*i]);
        const int z2=  7* block[1 + 4*i] - 17*block[3 + 4*i];
        const int z3= 17* block[1 + 4*i] +  7*block[3 + 4*i];
        block[0 + 4*i]= z0 + z3;
        block[1 + 4*i]= z1 + z2;
        block[2 + 4*i]= z1 - z2;
        block[3 + 4*i]= z0 - z3;
    }
    for (i=0; i < 4; i++) {
        const int z0= 13*(block[i + 4*0] +    block[i + 4*2]);
        const int z1= 13*(block[i + 4*0] -    block[i + 4*2]);
        const int z2=  7* block[i + 4*1] - 17*block[i + 4*3];
        const int z3= 17* block[i + 4*1] +  7*block[i + 4*3];
        const int rr= (dc + 0x80000);
        dst[i + stride*0]= cm[ dst[i + stride*0] + (((z0 + z3)*qmul + rr) >> 20) ];
        dst[i + stride*1]= cm[ dst[i + stride*1] + (((z1 + z2)*qmul + rr) >> 20) ];
        dst[i + stride*2]= cm[ dst[i + stride*2] + (((z1 - z2)*qmul + rr) >> 20) ];
        dst[i + stride*3]= cm[ dst[i + stride*3] + (((z0 - z3)*qmul + rr) >> 20) ];
    }
}

