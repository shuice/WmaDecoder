

#include "common.h"
#include "dsputil.h"
#include "avcodec.h"
#include <stdlib.h>
#include <stdio.h>

#include "avformat.h"

#undef NDEBUG
#include <assert.h>



static unsigned char headbuf[44];



static int write_prelim_header(AVCodecContext *c, FILE *out);
static int rewrite_header(FILE *out, unsigned int written);


int main(int argc,char ** argv)
{
    char folderPath[1024] = {0};
    strncpy(folderPath, argv[0], sizeof(folderPath) -1);
    *(strrchr(folderPath, '/') + 1) = '\0';
    
    char inputFilename[1024] = {0};
    char outputFilename[1024] = {0};
    strncpy(inputFilename, folderPath, sizeof(inputFilename) -1);
    strncat(inputFilename, "input.wma", sizeof(inputFilename) -1);
    
    strncpy(outputFilename, folderPath, sizeof(inputFilename) -1);
    strncat(outputFilename, "output.wav", sizeof(inputFilename) -1);

    
    AVCodec *codec = NULL;			  // Codec
    AVCodecContext *c = NULL;		  // Codec Context
    AVPacket pkt;
    AVFormatContext *ic = NULL;
    
    int out_size, size, len, i;
    uint8_t *outbuf = NULL;
    uint8_t *inbuf_ptr;
    FILE *outfile = NULL;
    unsigned int written = 0;

    
    avcodec_init();
    avcodec_register_all();
    av_register_all();
    
    if(av_open_input_file(&ic, inputFilename, NULL, 0, NULL) < 0)
    {
        printf("Error: could not open file %s\n", inputFilename);
        exit(1);
    }
    
    for(i = 0; i < ic->nb_streams; i++)
    {
        c = &ic->streams[i]->codec;
        if(c->codec_type == CODEC_TYPE_AUDIO)
        {
            break;
        }
    }
    
    av_find_stream_info(ic);
    codec = avcodec_find_decoder(c->codec_id);
    if (!codec)
    {
        fprintf(stderr, "Error: codec not found\n");
        exit(1);
    }
    
    /* open it */
    if (avcodec_open(c, codec) < 0)
    {
        fprintf(stderr, "Error: could not open codec\n");
        exit(1);
    }
    
    outfile = fopen(outputFilename, "wb");
    if (!outfile) {
        fprintf(stderr, "Error: not open to write output file\n");
        exit(1);
    }
    outbuf = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
    
    if(write_prelim_header(c, outfile))
    {
        fprintf(stderr, "Error: not write WAV header in output file\n");
        goto end;
    }
    
    
    ////////////////////----------------decoding----------------------
    for(;;)
    {
        
        if (av_read_frame(ic, &pkt) < 0)
        {
            break;
        }
        
        size = pkt.size;
        inbuf_ptr = pkt.data;
        if (size == 0)
        {
            break;
        }
        
        while (size > 0)
        {
            len = avcodec_decode_audio(c, (short *)outbuf, &out_size,
                                       inbuf_ptr, size);
            
            if (len < 0)
            {
                break;
            }
            
            if (out_size <= 0)
            {
                continue;
            }
            
            if (out_size > 0)
            {
                if(fwrite(outbuf, 1, out_size, outfile) <= 0)
                {
                    fprintf(stderr, "Error: not data write in output file\n");
                    goto end;
                }
                written += out_size;
            }
            size -= len;
            inbuf_ptr += len;
        }
        
    }
    
    fprintf(stderr, "\n");
    if(rewrite_header(outfile, written))
    {
        fprintf(stderr, "Error: not rewrite WAV header in output file\n");
        goto end;
    }
    if(outfile)
    {
        fclose(outfile);
    }
    if(outbuf)
    {
        free(outbuf);
    }
    if(c)
    {
        avcodec_close(c);
    }
    if(ic)
    {
        av_close_input_file(ic);
    }
    return 0;
end:
    if(outfile)
        fclose(outfile);
    if(outbuf)
        free(outbuf);
    if(c)
        avcodec_close(c);
    if(ic)
        av_close_input_file(ic);
    exit(1);
}



#define WRITE_U32(buf, x) *(buf)     = (unsigned char)((x)&0xff);\
*((buf)+1) = (unsigned char)(((x)>>8)&0xff);\
*((buf)+2) = (unsigned char)(((x)>>16)&0xff);\
*((buf)+3) = (unsigned char)(((x)>>24)&0xff);

#define WRITE_U16(buf, x) *(buf)     = (unsigned char)((x)&0xff);\
*((buf)+1) = (unsigned char)(((x)>>8)&0xff);


static int write_prelim_header(AVCodecContext *c, FILE *out)
{
    unsigned int size = 0x7fffffff;
    int bits = 16;
    int channels = c->channels;
    int samplerate = c->sample_rate;
    int bytespersec = channels*samplerate*bits/8;
    int align = channels*bits/8;
    int samplesize = bits;
    
    memcpy(headbuf, "RIFF", 4);
    WRITE_U32(headbuf+4, size-8);
    memcpy(headbuf+8, "WAVE", 4);
    memcpy(headbuf+12, "fmt ", 4);
    WRITE_U32(headbuf+16, 16);
    WRITE_U16(headbuf+20, 1); /* format */
    WRITE_U16(headbuf+22, channels);
    WRITE_U32(headbuf+24, samplerate);
    WRITE_U32(headbuf+28, bytespersec);
    WRITE_U16(headbuf+32, align);
    WRITE_U16(headbuf+34, samplesize);
    memcpy(headbuf+36, "data", 4);
    WRITE_U32(headbuf+40, size - 44);
    
    if(fwrite(headbuf, 1, 44, out) != 44) {
        fprintf(stderr, "Error: Failed to write wav header: %s\n", strerror(errno));
        return 1;
    }
    
    return 0;
}

static int rewrite_header(FILE *out, unsigned int written)
{
    unsigned int length = written;
    
    length += 44;
    
    WRITE_U32(headbuf+4, length-8);
    WRITE_U32(headbuf+40, length-44);
    if(fseek(out, 0, SEEK_SET) != 0)
        return 1;
    
    if(fwrite(headbuf, 1, 44, out) != 44) {
        fprintf(stderr, "Error: Failed to write wav header: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
