/*
 * utils for libavcodec
 * Copyright (c) 2001 Fabrice Bellard.
 * Copyright (c) 2003 Michel Bardiaux for the av_log API
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
 */
 
/**
 * @file utils.c
 * utils.
 */
 
#include "avcodec.h"
#include "dsputil.h"
#include <stdarg.h>
#include <limits.h>

static void avcodec_default_free_buffers(AVCodecContext *s);

void *av_mallocz(unsigned int size)
{
    void *ptr;
    
    ptr = av_malloc(size);
    if (!ptr)
        return NULL;
    memset(ptr, 0, size);
    return ptr;
}
char *av_strdup(const char *s)
{
    char *ptr;
    int len;
    len = strlen(s) + 1;
    ptr = (char*)av_malloc(len);
    if (!ptr)
        return NULL;
    memcpy(ptr, s, len);
    return ptr;
}
/**
 * realloc which does nothing if the block is large enough
 */
void *av_fast_realloc(void *ptr, unsigned int *size, unsigned int min_size)
{
    if(min_size < *size) 
        return ptr;
    
    *size= 17*min_size/16 + 32;
    return av_realloc(ptr, *size);
}

/*
 * Frees memory and sets the pointer to NULL.
 * @param arg pointer to the pointer which should be freed
 */
void av_freep(void *arg)
{
    void **ptr= (void**)arg;
    av_free(*ptr);
    *ptr = NULL;
}
///* encoder management */
AVCodec *first_avcodec;
void register_avcodec(AVCodec *format)
{
    AVCodec **p;
    p = &first_avcodec;
    while (*p != NULL) p = &(*p)->next;
    *p = format;
    format->next = NULL;
}
typedef struct InternalBuffer{
    int last_pic_num;
    uint8_t *base[4];
    uint8_t *data[4];
    int linesize[4];
}InternalBuffer;
#define INTERNAL_BUFFER_SIZE 32



void avcodec_default_release_buffer(AVCodecContext *s, AVFrame *pic){
    int i;
    InternalBuffer *buf, *last, temp;
    assert(pic->type==FF_BUFFER_TYPE_INTERNAL);
    assert(s->internal_buffer_count);
    buf = NULL; /* avoids warning */
    for(i=0; i<s->internal_buffer_count; i++){ //just 3-5 checks so is not worth to optimize
        buf= &((InternalBuffer*)s->internal_buffer)[i];
        if(buf->data[0] == pic->data[0])
            break;
    }
    assert(i < s->internal_buffer_count);
    s->internal_buffer_count--;
    last = &((InternalBuffer*)s->internal_buffer)[s->internal_buffer_count];
    temp= *buf;
    *buf= *last;
    *last= temp;
    for(i=0; i<3; i++){
        pic->data[i]=NULL;
//        pic->base[i]=NULL;
    }
//printf("R%X\n", pic->opaque);
}

int avcodec_default_execute(AVCodecContext *c, int (*func)(AVCodecContext *c2, void *arg2),void **arg, int *ret, int count){
    int i;
    for(i=0; i<count; i++){
        int r= func(c, arg[i]);
        if(ret) ret[i]= r;
    }
    return 0;
}
enum PixelFormat avcodec_default_get_format(struct AVCodecContext *s, const enum PixelFormat * fmt){
    return fmt[0];
}
static const char* context_to_name(void* ptr) {
    AVCodecContext *avc= (AVCodecContext *)ptr;
    if(avc && avc->codec && avc->codec->name)
        return avc->codec->name; 
    else
        return "NULL";
}
static AVClass av_codec_context_class = { "AVCodecContext", context_to_name };
void avcodec_get_context_defaults(AVCodecContext *s){
    memset(s, 0, sizeof(AVCodecContext));
    s->av_class= &av_codec_context_class;
    s->bit_rate= 800*1000;
    s->bit_rate_tolerance= s->bit_rate*10;
    s->qmin= 2;
    s->qmax= 31;
    s->mb_qmin= 2;
    s->mb_qmax= 31;
    s->rc_eq= "tex^qComp";
    s->qcompress= 0.5;
    s->max_qdiff= 3;
    s->b_quant_factor=1.25;
    s->b_quant_offset=1.25;
    s->i_quant_factor=-0.8;
    s->i_quant_offset=0.0;
    s->error_concealment= 3;
    s->error_resilience= 1;
    s->workaround_bugs= FF_BUG_AUTODETECT;
    s->frame_rate_base= 1;
    s->frame_rate = 25;
    s->gop_size= 50;
    s->me_method= ME_EPZS;
    s->get_buffer= NULL;
    s->release_buffer= avcodec_default_release_buffer;
    s->get_format= avcodec_default_get_format;
    s->execute= avcodec_default_execute;
    s->thread_count=1;
    s->me_subpel_quality=8;
    s->lmin= FF_QP2LAMBDA * s->qmin;
    s->lmax= FF_QP2LAMBDA * s->qmax;
//cs by tiany
//    s->sample_aspect_ratio = (AVRational){0,1};
    s->sample_aspect_ratio.num = 0;
    s->sample_aspect_ratio.den = 1;
//ce by tiany
    s->ildct_cmp= FF_CMP_VSAD;
    
    s->intra_quant_bias= FF_DEFAULT_QUANT_BIAS;
    s->inter_quant_bias= FF_DEFAULT_QUANT_BIAS;
    s->palctrl = NULL;
    s->reget_buffer= NULL;
}
/**
 * allocates a AVCodecContext and set it to defaults.
 * this can be deallocated by simply calling free() 
 */
AVCodecContext *avcodec_alloc_context(void){
    AVCodecContext *avctx= (AVCodecContext *)av_malloc(sizeof(AVCodecContext));
    
    if(avctx==NULL) return NULL;
    
    avcodec_get_context_defaults(avctx);
    
    return avctx;
}
void avcodec_get_frame_defaults(AVFrame *pic){
    memset(pic, 0, sizeof(AVFrame));
    pic->pts= AV_NOPTS_VALUE;
}
/**
 * allocates a AVPFrame and set it to defaults.
 * this can be deallocated by simply calling free() 
 */
AVFrame *avcodec_alloc_frame(void){
    AVFrame *pic= (AVFrame*)av_malloc(sizeof(AVFrame));
    
    if(pic==NULL) return NULL;
    
    avcodec_get_frame_defaults(pic);
    
    return pic;
}
int avcodec_open(AVCodecContext *avctx, AVCodec *codec)
{
    int ret;
    if(avctx->codec)
        return -1;
    avctx->codec = codec;
    avctx->codec_id = (CodecID)codec->id;
    avctx->frame_number = 0;
    if (codec->priv_data_size > 0) {
        avctx->priv_data = av_mallocz(codec->priv_data_size);
        if (!avctx->priv_data) 
            return -ENOMEM;
    } else {
        avctx->priv_data = NULL;
    }
    ret = avctx->codec->init(avctx);
    if (ret < 0) {
        av_freep(&avctx->priv_data);
        return ret;
    }
    return 0;
}
/* decode an audio frame. return -1 if error, otherwise return the
   *number of bytes used. If no frame could be decompressed,
   *frame_size_ptr is zero. Otherwise, it is the decompressed frame
   *size in BYTES. */
int avcodec_decode_audio(AVCodecContext *avctx, int16_t *samples, 
                         int *frame_size_ptr,
                         uint8_t *buf, int buf_size)
{
    int ret;
    *frame_size_ptr= 0;
    ret = avctx->codec->decode(avctx, samples, frame_size_ptr, 
                               buf, buf_size);
    avctx->frame_number++;
    return ret;
}
int avcodec_close(AVCodecContext *avctx)
{
    if (avctx->codec->close)
        avctx->codec->close(avctx);
    avcodec_default_free_buffers(avctx);
    av_freep(&avctx->priv_data);
    avctx->codec = NULL;
    return 0;
}
AVCodec *avcodec_find_encoder(enum CodecID id)
{
    AVCodec *p;
    p = first_avcodec;
    while (p) {
        if (p->encode != NULL && p->id == id)
            return p;
        p = p->next;
    }
    return NULL;
}
AVCodec *avcodec_find_encoder_by_name(const char *name)
{
    AVCodec *p;
    p = first_avcodec;
    while (p) {
        if (p->encode != NULL && strcmp(name,p->name) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}
AVCodec *avcodec_find_decoder(enum CodecID id)
{
    AVCodec *p;
    p = first_avcodec;
    while (p) {
        if (p->decode != NULL && p->id == id)
            return p;
        p = p->next;
    }
    return NULL;
}
AVCodec *avcodec_find_decoder_by_name(const char *name)
{
    AVCodec *p;
    p = first_avcodec;
    while (p) {
        if (p->decode != NULL && strcmp(name,p->name) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

unsigned avcodec_version( void )
{
  return LIBAVCODEC_VERSION_INT;
}
unsigned avcodec_build( void )
{
  return LIBAVCODEC_BUILD;
}
/* must be called before any other functions */
void avcodec_init(void)
{
    static int inited = 0;
    if (inited != 0)
    {
        return;
    }
    inited = 1;
    dsputil_static_init();
}
/**
 * Flush buffers, should be called when seeking or when swicthing to a different stream.
 */
void avcodec_flush_buffers(AVCodecContext *avctx)
{
    if(avctx->codec->flush)
        avctx->codec->flush(avctx);
}
static void avcodec_default_free_buffers(AVCodecContext *s){
    int i, j;
    if(s->internal_buffer==NULL) return;
    
    for(i=0; i<INTERNAL_BUFFER_SIZE; i++)
    {
        InternalBuffer *buf= &((InternalBuffer*)s->internal_buffer)[i];
        for(j=0; j<4; j++)
        {
            av_freep(&buf->base[j]);
            buf->data[j]= NULL;
        }
    }
    av_freep(&s->internal_buffer);
    
    s->internal_buffer_count=0;
}


static int av_log_level = AV_LOG_DEBUG;
static void av_log_default_callback(void* ptr, int level, const char* fmt, va_list vl)
{
    static int print_prefix=1;
    AVClass* avc= ptr ? *(AVClass**)ptr : NULL;
    if(level>av_log_level)
	return;
#undef fprintf
    if(print_prefix && avc) {
	    fprintf(stderr, "[%s @ %p]", avc->item_name(ptr), avc);
    }
//#define fprintf please_use_av_log	//Del by ty
        
    print_prefix= strstr(fmt, "\n") != NULL;
        
    vfprintf(stderr, fmt, vl);
}
static void (*av_log_callback)(void*, int, const char*, va_list) = av_log_default_callback;
void av_log(void* avcl, int level, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    av_vlog(avcl, level, fmt, vl);
    va_end(vl);
}
void av_vlog(void* avcl, int level, const char *fmt, va_list vl)
{
    av_log_callback(avcl, level, fmt, vl);
}
int av_log_get_level(void)
{
    return av_log_level;
}
void av_log_set_level(int level)
{
    av_log_level = level;
}
void av_log_set_callback(void (*callback)(void*, int, const char*, va_list))
{
    av_log_callback = callback;
}
#if !defined(HAVE_PTHREADS) && !defined(HAVE_W32THREADS)
int avcodec_thread_init(AVCodecContext *s, int thread_count){
    return -1;
}
#endif
