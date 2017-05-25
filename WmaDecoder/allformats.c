/*
 * Register all the formats and protocols
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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
#include "avformat.h"

/* If you do not call this function, then you can select exactly which
   formats you want to support */

/**
 * Initialize libavcodec and register all the codecs and formats.
 */
#define  CONFIG_ASF //////////////////WMA decoder

void av_register_all(void)
{
    avcodec_init();
    avcodec_register_all();

    //mpegps_init();
    //mpegts_init();
#ifdef CONFIG_ENCODERS
    crc_init();
    img_init();
#endif //CONFIG_ENCODERS
    //raw_init();
    //mp3_init();
    //rm_init();
#ifdef CONFIG_ASF
    asf_init();
#endif
#ifdef CONFIG_ENCODERS
    avienc_init();
#endif //CONFIG_ENCODERS
    //avidec_init();
    //wav_init();
    //swf_init();
    //au_init();
#ifdef CONFIG_ENCODERS
    gif_init();
#endif //CONFIG_ENCODERS
    //mov_init();
#ifdef CONFIG_ENCODERS
    movenc_init();
    jpeg_init();
#endif //CONFIG_ENCODERS
    //dv_init();
    //fourxm_init();
#ifdef CONFIG_ENCODERS
    flvenc_init();
#endif //CONFIG_ENCODERS
    //flvdec_init();
    //str_init();
    //roq_init();
    //ipmovie_init();
    //wc3_init();
    //westwood_init();
    //film_init();
    //idcin_init();
    //flic_init();
    //vmd_init();

#if defined(AMR_NB) || defined(AMR_NB_FIXED) || defined(AMR_WB)
    amr_init();
#endif
    //yuv4mpeg_init();
    
#ifdef CONFIG_VORBIS
    ogg_init();
#endif

#ifndef CONFIG_WIN32
    //ffm_init();
#endif
#ifdef CONFIG_VIDEO4LINUX
    video_grab_init();
#endif
#if defined(CONFIG_AUDIO_OSS) || defined(CONFIG_AUDIO_BEOS)
    audio_init();
#endif

#ifdef CONFIG_DV1394
    dv1394_init();
#endif

    //nut_init();

#ifdef CONFIG_ENCODERS
    /* image formats */
    av_register_image_format(&pnm_image_format);
    av_register_image_format(&pbm_image_format);
    av_register_image_format(&pgm_image_format);
    av_register_image_format(&ppm_image_format);
    av_register_image_format(&pam_image_format);
    av_register_image_format(&pgmyuv_image_format);
    av_register_image_format(&yuv_image_format);
#ifdef CONFIG_ZLIB
    av_register_image_format(&png_image_format);
#endif
    av_register_image_format(&jpeg_image_format);
    av_register_image_format(&gif_image_format);
#endif //CONFIG_ENCODERS

    /* file protocols */
    register_protocol(&file_protocol); // del by yuanbin
    register_protocol(&pipe_protocol); // del by yuanbin
#ifdef CONFIG_NETWORK
    rtsp_init();
    rtp_init();
    register_protocol(&udp_protocol);
    register_protocol(&rtp_protocol);
    register_protocol(&tcp_protocol);
    register_protocol(&http_protocol);
#endif



}
