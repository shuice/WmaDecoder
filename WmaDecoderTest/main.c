/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 */

/**
 * @file h264.c
 * H.264 / AVC / MPEG4 part10 codec.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "common.h"
#include "dsputil.h"
#include "avcodec.h"
//#include "mpegvideo.h"
//#include "h264data.h"
//#include "golomb.h"
//#include "cabac.h"
#include <stdlib.h>	//Add by ty
#include <stdio.h>	//Add by ty

#include "avformat.h"

#undef NDEBUG
#include <assert.h>


#define WRITE_U32(buf, x) *(buf)     = (unsigned char)((x)&0xff);\
*((buf)+1) = (unsigned char)(((x)>>8)&0xff);\
*((buf)+2) = (unsigned char)(((x)>>16)&0xff);\
*((buf)+3) = (unsigned char)(((x)>>24)&0xff);

#define WRITE_U16(buf, x) *(buf)     = (unsigned char)((x)&0xff);\
*((buf)+1) = (unsigned char)(((x)>>8)&0xff);

static unsigned char headbuf[44];


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


#define VER  "0.1beta3"
#define DATE "07-09-2004"
#define COMMENT "WMA&OGG2WAV v." VER " converter from http://mcmcc.bat.ru"

static void infover()
{
    fprintf(stderr,"\nWMA&OGG to WAV converter v.%s %s.\n", VER, DATE);
    fprintf(stderr,"Copyright (C) 2004 McMCC <mcmcc@mail.ru> (see 'COPYING').\n");
    fprintf(stderr,"THIS SOFTWARE COMES WITH ABSOLUTELY NO WARRANTY! USE AT YOUR OWN RISK!\n");
}

static void help(char *aa)
{
    infover();
    fprintf(stderr,"\nUse %s file.wma - info only\n", aa);
    fprintf(stderr,"Use %s [-n] file.wma file.wav - converting wma to wav\n", aa);
    fprintf(stderr,"Converting wma format over script to other format:\n");
    fprintf(stderr,"Use %s [options] -r runscript file.wma file.out\nor\n", aa);
    fprintf(stderr,"Use %s [options] -e extention -r runscript file.wma\n", aa);
    fprintf(stderr,"Converting directories (include subdirectories) with wma's over\n"
            "script in directory with other format:\n");
    fprintf(stderr,"Use %s [options] -d -e extention -r runscript dir_wma dir_out\nor\n", aa);
    fprintf(stderr,"Use %s [options] -d -e extention -r runscript dir_wma\n", aa);
    fprintf(stderr,"\nExamples:\n");
    fprintf(stderr,"%s test.wma\n", aa);
    fprintf(stderr,"%s test.wma test.wav\n", aa);
    fprintf(stderr,"%s -d -e ogg -r ./oggscript test.wma test.ogg\nor\n", aa);
    fprintf(stderr,"%s -d -e ogg -r ./oggscript test.wma\n", aa);
    fprintf(stderr,"%s -n -t cp1251 -d -e mp3 -r ./mp3script /home/test/dir_wma /home/test/dir_out\nor\n", aa);
    fprintf(stderr,"cd /home/test/dir_out\n");
    fprintf(stderr,"%s -n -t cp1251 -d -e mp3 -r ./mp3script /home/test/dir_wma\n", aa);
    fprintf(stderr,"\nOptions:\n");
    fprintf(stderr,"\t-e <extension> Set extension.\n");
    fprintf(stderr,"\t-t <codepage>  Set convert tag to codepage.\n");
    fprintf(stderr,"\t-n             No print tag info.\n\n");
    exit(0);
}


//#if 0 //selftest
#define COUNT 8000
#define SIZE (COUNT*40)
int main(int argc,char ** argv){
    
    //AS by tiany for test on 20060331
    FILE * inpf;
    FILE * outf;
    int nWrite;
    int nalLen;
    unsigned char* Buf;
    int  got_picture, consumed_bytes;
    char cpage[16] = "KOI-7";
    
    AVCodec *codec;			  // Codec
    AVCodecContext *c;		  // Codec Context
    AVFrame *picture;		  // Frame
    AVPacket pkt;			  //
    AVFormatContext *ic= NULL;
    
    int out_size, size, len, i;
    int tns, thh, tmm, tss;
    uint8_t *outbuf = NULL;
    uint8_t *inbuf_ptr;
    FILE *outfile = NULL;
    char tmp[24];
    unsigned int written = 0;
    char filename[256]={""};
    char filenameout[256]={"test.wav"};
    int no_info = 1; //default
    
    if(argc == 1) 	help(argv[0]);
    strcpy(filename,argv[1]);
    
    avcodec_init();
    avcodec_register_all();
    av_register_all();
    
    if(av_open_input_file(&ic, filename, NULL, 0, NULL) < 0) {
        printf("Error: could not open file %s\n", filename);
        exit(1);
    }
    
    for(i = 0; i < ic->nb_streams; i++) {
        c = &ic->streams[i]->codec;
        if(c->codec_type == CODEC_TYPE_AUDIO)
            break;
    }
    
    av_find_stream_info(ic);
    codec = avcodec_find_decoder(c->codec_id);
    if (!codec) {
        fprintf(stderr, "Error: codec not found\n");
        exit(1);
    }
    
    /* open it */
    if (avcodec_open(c, codec) < 0) {
        fprintf(stderr, "Error: could not open codec\n");
        exit(1);
    }
    
    if(filenameout)
    {
        outfile = fopen(filenameout, "wb");
        if (!outfile) {
            fprintf(stderr, "Error: not open to write output file\n");
            exit(1);
        }
        outbuf = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
    }
    
    ////dump  info of stream
    if(1)
    {
        fprintf(stderr, "\n");
        dump_format(ic, 0, filename, 0);
        if (ic->title[0] != '\0')
            fprintf(stderr, "Title: %s\n", ic->title);
        if (ic->author[0] != '\0')
            fprintf(stderr, "Author: %s\n", ic->author);
        if (ic->album[0] != '\0')
            fprintf(stderr, "Album: %s\n", ic->album);
        if (ic->year != 0)
            fprintf(stderr, "Year: %d\n", ic->year);
        if (ic->track != 0)
            fprintf(stderr, "Track: %d\n", ic->track);
        if (ic->genre[0] != '\0')
            fprintf(stderr, "Genre: %s\n", ic->genre);
        if (ic->copyright[0] != '\0')
            fprintf(stderr, "Copyright: %s\n", ic->copyright);
        if (ic->comment[0] != '\0')
            fprintf(stderr, "Comments: %s\n", ic->comment);
        
        if (ic->duration > 0)
        {
            tns = ic->duration/1000000L;
            thh = tns/3600;
            tmm = (tns%3600)/60;
            tss = (tns%60);
            fprintf(stderr, "Time: %2d:%02d:%02d\n", thh, tmm, tss);
        }
        tns = 0;
        thh = 0;
        tmm = 0;
        tss = 0;
        fprintf(stderr, "\n");
    }
    
    if(write_prelim_header(c, outfile))
    {
        fprintf(stderr, "Error: not write WAV header in output file\n");
        goto end;
    }
    
    
    ////////////////////----------------decoding----------------------
    for(;;){
        
        if (av_read_frame(ic, &pkt) < 0)
            break;
        
        size = pkt.size;
        inbuf_ptr = pkt.data;
        tns = pkt.pts/1000000L;
        thh = tns/3600;
        tmm = (tns%3600)/60;
        tss = (tns%60);
        if(tns)
            fprintf(stderr, "Decode Time: %2d:%02d:%02d bitrate: %d kb/s\r", thh, tmm,
                    tss, c->bit_rate / 1000);
        if (size == 0)
            break;
        
        while (size > 0) {
            len = avcodec_decode_audio(c, (short *)outbuf, &out_size,
                                       inbuf_ptr, size);
            
            for(i=0;i<out_size;i++)
            {
                if ((outbuf[i]>=256)) {
                    break;
                }
            }
            
            if (len < 0) {
                break;
            }
            
            if (out_size <= 0) {
                continue;
            }
            
            if (out_size > 0) {
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
        fclose(outfile);
    if(outbuf)
        free(outbuf);
    if(c)
        avcodec_close(c);
    if(ic)
        av_close_input_file(ic);
    return;
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
//#endif
