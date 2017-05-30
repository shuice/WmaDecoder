/*
 * DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * gmc & q-pel & 32/64 bit based MC by Michael Niedermayer <michaelni@gmx.at>
 */
 
/**
 * @file dsputil.c
 * DSP utils
 */
 #include "Wma_Decoder.h"
#include "Wma_avcodec.h"
#include "Wma_dsputil.h"

namespace WMADECODER_NAMESPACE{

uint8_t cropTbl[256 + 2 * MAX_NEG_CROP];
uint32_t squareTbl[512];

//AS by ty
const uint8_t ff_h263_loop_filter_strength[32]={
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9,10,10,10,11,11,11,12,12,12
};
//AE by ty

const uint8_t ff_zigzag_direct[64] = {
    0,   1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* Specific zigzag scan for 248 idct. NOTE that unlike the
   specification, we interleave the fields */
const uint8_t ff_zigzag248_direct[64] = {
     0,  8,  1,  9, 16, 24,  2, 10,
    17, 25, 32, 40, 48, 56, 33, 41,
    18, 26,  3, 11,  4, 12, 19, 27,
    34, 42, 49, 57, 50, 58, 35, 43,
    20, 28,  5, 13,  6, 14, 21, 29,
    36, 44, 51, 59, 52, 60, 37, 45,
    22, 30,  7, 15, 23, 31, 38, 46,
    53, 61, 54, 62, 39, 47, 55, 63,
};

/* not permutated inverse zigzag_direct + 1 for MMX quantizer */
uint16_t __align8 inv_zigzag_direct16[64];

const uint8_t ff_alternate_horizontal_scan[64] = {
    0,  1,   2,  3,  8,  9, 16, 17, 
    10, 11,  4,  5,  6,  7, 15, 14,
    13, 12, 19, 18, 24, 25, 32, 33, 
    26, 27, 20, 21, 22, 23, 28, 29,
    30, 31, 34, 35, 40, 41, 48, 49, 
    42, 43, 36, 37, 38, 39, 44, 45,
    46, 47, 50, 51, 56, 57, 58, 59, 
    52, 53, 54, 55, 60, 61, 62, 63,
};

const uint8_t ff_alternate_vertical_scan[64] = {
    0,  8,  16, 24,  1,  9,  2, 10, 
    17, 25, 32, 40, 48, 56, 57, 49,
    41, 33, 26, 18,  3, 11,  4, 12, 
    19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28,  5, 13,  6, 14, 
    21, 29, 36, 44, 52, 60, 37, 45,
    53, 61, 22, 30,  7, 15, 23, 31, 
    38, 46, 54, 62, 39, 47, 55, 63,
};

/* a*inverse[b]>>32 == a/b for all 0<=a<=65536 && 2<=b<=255 */
const uint32_t inverse[256]={
         0, 4294967295U,2147483648U,1431655766, 1073741824,  858993460,  715827883,  613566757, 
 536870912,  477218589,  429496730,  390451573,  357913942,  330382100,  306783379,  286331154, 
 268435456,  252645136,  238609295,  226050911,  214748365,  204522253,  195225787,  186737709, 
 178956971,  171798692,  165191050,  159072863,  153391690,  148102321,  143165577,  138547333, 
 134217728,  130150525,  126322568,  122713352,  119304648,  116080198,  113025456,  110127367, 
 107374183,  104755300,  102261127,   99882961,   97612894,   95443718,   93368855,   91382283, 
  89478486,   87652394,   85899346,   84215046,   82595525,   81037119,   79536432,   78090315, 
  76695845,   75350304,   74051161,   72796056,   71582789,   70409300,   69273667,   68174085, 
  67108864,   66076420,   65075263,   64103990,   63161284,   62245903,   61356676,   60492498, 
  59652324,   58835169,   58040099,   57266231,   56512728,   55778797,   55063684,   54366675, 
  53687092,   53024288,   52377650,   51746594,   51130564,   50529028,   49941481,   49367441, 
  48806447,   48258060,   47721859,   47197443,   46684428,   46182445,   45691142,   45210183, 
  44739243,   44278014,   43826197,   43383509,   42949673,   42524429,   42107523,   41698712, 
  41297763,   40904451,   40518560,   40139882,   39768216,   39403370,   39045158,   38693400, 
  38347923,   38008561,   37675152,   37347542,   37025581,   36709123,   36398028,   36092163, 
  35791395,   35495598,   35204650,   34918434,   34636834,   34359739,   34087043,   33818641, 
  33554432,   33294321,   33038210,   32786010,   32537632,   32292988,   32051995,   31814573, 
  31580642,   31350127,   31122952,   30899046,   30678338,   30460761,   30246249,   30034737, 
  29826162,   29620465,   29417585,   29217465,   29020050,   28825284,   28633116,   28443493, 
  28256364,   28071682,   27889399,   27709467,   27531842,   27356480,   27183338,   27012373, 
  26843546,   26676816,   26512144,   26349493,   26188825,   26030105,   25873297,   25718368, 
  25565282,   25414008,   25264514,   25116768,   24970741,   24826401,   24683721,   24542671, 
  24403224,   24265352,   24129030,   23994231,   23860930,   23729102,   23598722,   23469767, 
  23342214,   23216040,   23091223,   22967740,   22845571,   22724695,   22605092,   22486740, 
  22369622,   22253717,   22139007,   22025474,   21913099,   21801865,   21691755,   21582751, 
  21474837,   21367997,   21262215,   21157475,   21053762,   20951060,   20849356,   20748635, 
  20648882,   20550083,   20452226,   20355296,   20259280,   20164166,   20069941,   19976593, 
  19884108,   19792477,   19701685,   19611723,   19522579,   19434242,   19346700,   19259944, 
  19173962,   19088744,   19004281,   18920561,   18837576,   18755316,   18673771,   18592933, 
  18512791,   18433337,   18354562,   18276457,   18199014,   18122225,   18046082,   17970575, 
  17895698,   17821442,   17747799,   17674763,   17602325,   17530479,   17459217,   17388532, 
  17318417,   17248865,   17179870,   17111424,   17043522,   16976156,   16909321,   16843010,
};



/**
 * permutes an 8x8 block.
 * @param block the block which will be permuted according to the given permutation vector
 * @param permutation the permutation vector
 * @param last the last non zero coefficient in scantable order, used to speed the permutation up
 * @param scantable the used scantable, this is only used to speed the permutation up, the block is not 
 *                  (inverse) permutated to scantable order!
 */
void ff_block_permute(DCTELEM *block, uint8_t *permutation, const uint8_t *scantable, int last)
{
    int i;
    DCTELEM temp[64];
    
    if(last<=0) return;
    //if(permutation[1]==1) return; //FIXME its ok but not clean and might fail for some perms
    for(i=0; i<=last; i++){
        const int j= scantable[i];
        temp[j]= block[j];
        block[j]=0;
    }
    
    for(i=0; i<=last; i++){
        const int j= scantable[i];
        const int perm_j= permutation[j];
        block[perm_j]= temp[j];
    }
}
static int zero_cmp(void *s, uint8_t *a, uint8_t *b, int stride, int h){
    return 0;
}
void ff_set_cmp(DSPContext* c, me_cmp_func *cmp, int type){
    int i;
    
    memset(cmp, 0, sizeof(void*)*5);
        
    for(i=0; i<5; i++){
        switch(type&0xFF){
        case FF_CMP_SAD:
            cmp[i]= c->sad[i];
            break;
        case FF_CMP_SATD:
            cmp[i]= c->hadamard8_diff[i];
            break;
        case FF_CMP_SSE:
            cmp[i]= c->sse[i];
            break;
        case FF_CMP_DCT:
            cmp[i]= c->dct_sad[i];
            break;
        case FF_CMP_PSNR:
            cmp[i]= c->quant_psnr[i];
            break;
        case FF_CMP_BIT:
            cmp[i]= c->bit[i];
            break;
        case FF_CMP_RD:
            cmp[i]= c->rd[i];
            break;
        case FF_CMP_VSAD:
            cmp[i]= c->vsad[i];
            break;
        case FF_CMP_VSSE:
            cmp[i]= c->vsse[i];
            break;
        case FF_CMP_ZERO:
            cmp[i]= zero_cmp;
            break;
        case FF_CMP_NSSE:
            cmp[i]= c->nsse[i];
            break;
        default:
            av_log(NULL, AV_LOG_ERROR,"internal error in cmp function selection\n");
        }
    }
}

/* init static data */
void dsputil_static_init(void)
{
    int i;
    for(i=0;i<256;i++) cropTbl[i + MAX_NEG_CROP] = i;
    for(i=0;i<MAX_NEG_CROP;i++) {
        cropTbl[i] = 0;
        cropTbl[i + MAX_NEG_CROP + 256] = 255;
    }
    
    for(i=0;i<512;i++) {
        squareTbl[i] = (i - 256) * (i - 256);
    }
    
    for(i=0; i<64; i++) inv_zigzag_direct16[ff_zigzag_direct[i]]= i+1;
}

void fft_calc(FFTContext *s, FFTComplex *z)
{
    s->fft_calc(s, z);
}


long int lrintf(float x)
{
#ifdef CONFIG_WIN32
    /* XXX: incorrect, but make it compile */
    return (int)(x);
#else
    return (int)(rint(x));
#endif
}

}
