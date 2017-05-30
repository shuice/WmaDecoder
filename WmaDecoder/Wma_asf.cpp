/*
 * ASF compatible encoder and decoder.
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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
#include "Wma_Decoder.h"
#include "Wma_avformat.h"
#include "Wma_avi.h"

#include <assert.h>

namespace WMADECODER_NAMESPACE{

#undef NDEBUG

#define MPA_FRAME_SIZE 1152
#define PACKET_SIZE 3200
#define PACKET_HEADER_SIZE 12
#define FRAME_HEADER_SIZE 17

typedef struct {
    int num;
    int seq;
    /* use for reading */
    AVPacket pkt;
    int frag_offset;
    int timestamp;
    int64_t duration;

    int ds_span;		/* descrambling  */
    int ds_packet_size;
    int ds_chunk_size;
    int ds_data_size;
    int ds_silence_data;
    
    int packet_pos;

} ASFStream;

typedef struct {
    uint32_t v1;
    uint16_t v2;
    uint16_t v3;
    uint8_t v4[8];
} GUID;

typedef struct {
    GUID guid;			// generated by client computer
    uint64_t file_size;		// in bytes
                                // invalid if broadcasting
    uint64_t create_time;	// time of creation, in 100-nanosecond units since 1.1.1601
                                // invalid if broadcasting
    uint64_t packets_count;	// how many packets are there in the file
                                // invalid if broadcasting
    uint64_t play_time;		// play time, in 100-nanosecond units
                                // invalid if broadcasting
    uint64_t send_time;		// time to send file, in 100-nanosecond units
                                // invalid if broadcasting (could be ignored)
    uint32_t preroll;		// timestamp of the first packet, in milliseconds
    				// if nonzero - substract from time
    uint32_t ignore;            // preroll is 64bit - but let's just ignore it
    uint32_t flags;		// 0x01 - broadcast
    				// 0x02 - seekable
                                // rest is reserved should be 0
    uint32_t min_pktsize;	// size of a data packet
                                // invalid if broadcasting
    uint32_t max_pktsize;	// shall be the same as for min_pktsize
                                // invalid if broadcasting
    uint32_t max_bitrate;	// bandwith of stream in bps
    				// should be the sum of bitrates of the
                                // individual media streams
} ASFMainHeader;


typedef struct {
    int seqno;
    int packet_size;
    int is_streamed;
    int asfid2avid[128];        /* conversion table from asf ID 2 AVStream ID */
    ASFStream streams[128];	/* it's max number and it's not that big */
    /* non streamed additonnal info */
    int64_t nb_packets;
    int64_t duration; /* in 100ns units */
    /* packet filling */
    int packet_size_left;
    int packet_timestamp_start;
    int packet_timestamp_end;
    int packet_nb_frames;
    uint8_t packet_buf[PACKET_SIZE];
    ByteIOContext pb;
    /* only for reading */
    uint64_t data_offset; /* begining of the first data packet */

    ASFMainHeader hdr;

    int packet_flags;
    int packet_property;
    int packet_timestamp;
    int packet_segsizetype;
    int packet_segments;
    int packet_seq;
    int packet_replic_size;
    int packet_key_frame;
    int packet_padsize;
    int packet_frag_offset;
    int packet_frag_size;
    int packet_frag_timestamp;
    int packet_multi_size;
    int packet_obj_size;
    int packet_time_delta;
    int packet_time_start;
    int packet_pos;

    int stream_index;
    ASFStream* asf_st; /* currently decoded stream */
} ASFContext;

static const GUID asf_header = {
    0x75B22630, 0x668E, 0x11CF, { 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C },

};

static const GUID file_header = {
    0x8CABDCA1, 0xA947, 0x11CF, { 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 },
};

static const GUID stream_header = {
    0xB7DC0791, 0xA9B7, 0x11CF, { 0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65 },
};

static const GUID audio_stream = {
    0xF8699E40, 0x5B4D, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};

static const GUID audio_conceal_none = {
    // 0x49f1a440, 0x4ece, 0x11d0, { 0xa3, 0xac, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 },
    // New value lifted from avifile
    0x20fb5700, 0x5b55, 0x11cf, { 0xa8, 0xfd, 0x00, 0x80, 0x5f, 0x5c, 0x44, 0x2b },
};

static const GUID video_stream = {
    0xBC19EFC0, 0x5B4D, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};

static const GUID video_conceal_none = {
    0x20FB5700, 0x5B55, 0x11CF, { 0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B },
};


static const GUID comment_header = {
    0x75b22633, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c },
};

static const GUID codec_comment_header = {
    0x86D15240, 0x311D, 0x11D0, { 0xA3, 0xA4, 0x00, 0xA0, 0xC9, 0x03, 0x48, 0xF6 },
};
static const GUID codec_comment1_header = {
    0x86d15241, 0x311d, 0x11d0, { 0xa3, 0xa4, 0x00, 0xa0, 0xc9, 0x03, 0x48, 0xf6 },
};

static const GUID data_header = {
    0x75b22636, 0x668e, 0x11cf, { 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c },
};

static const GUID index_guid = {
    0x33000890, 0xe5b1, 0x11cf, { 0x89, 0xf4, 0x00, 0xa0, 0xc9, 0x03, 0x49, 0xcb },
};

static const GUID head1_guid = {
    0x5fbf03b5, 0xa92e, 0x11cf, { 0x8e, 0xe3, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 },
};

static const GUID head2_guid = {
    0xabd3d211, 0xa9ba, 0x11cf, { 0x8e, 0xe6, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 },
};

static const GUID extended_content_header = {
        0xD2D0A440, 0xE307, 0x11D2, { 0x97, 0xF0, 0x00, 0xA0, 0xC9, 0x5E, 0xA8, 0x50 },
};

/* I am not a number !!! This GUID is the one found on the PC used to
   generate the stream */
static const GUID my_guid = {
    0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 },
};

const CodecTag codec_wav_tags[] = {
    { CODEC_ID_MP2, 0x50 },
    { CODEC_ID_MP3, 0x55 },
    { CODEC_ID_AC3, 0x2000 },
    { CODEC_ID_PCM_S16LE, 0x01 },
    { CODEC_ID_PCM_U8, 0x01 }, /* must come after s16le in this list */
    { CODEC_ID_PCM_ALAW, 0x06 },
    { CODEC_ID_PCM_MULAW, 0x07 },
    { CODEC_ID_ADPCM_MS, 0x02 },
    { CODEC_ID_ADPCM_IMA_WAV, 0x11 },
    { CODEC_ID_ADPCM_IMA_DK4, 0x61 },  /* rogue format number */
    { CODEC_ID_ADPCM_IMA_DK3, 0x62 },  /* rogue format number */
    { CODEC_ID_WMAV1, 0x160 },
    { CODEC_ID_WMAV2, 0x161 },
    { 0, 0 },
};

enum CodecID codec_get_id(const CodecTag *tags, unsigned int tag)
{
    while (tags->id != 0) {
        if(   toupper((tag >> 0)&0xFF) == toupper((tags->tag >> 0)&0xFF)
           && toupper((tag >> 8)&0xFF) == toupper((tags->tag >> 8)&0xFF)
           && toupper((tag >>16)&0xFF) == toupper((tags->tag >>16)&0xFF)
           && toupper((tag >>24)&0xFF) == toupper((tags->tag >>24)&0xFF))
            return (CodecID)tags->id;
        tags++;
    }
    return CODEC_ID_NONE;
}

int wav_codec_get_id(unsigned int tag, int bps)
{
    int id;
    id = codec_get_id(codec_wav_tags, tag);
    if (id <= 0)
        return id;
    /* handle specific u8 codec */
    if (id == CODEC_ID_PCM_S16LE && bps == 8)
        id = CODEC_ID_PCM_U8;
    return id;
}

void get_wav_header(ByteIOContext *pb, AVCodecContext *codec, int size)
{
    int id;

    id = get_le16(pb);
    codec->codec_type = CODEC_TYPE_AUDIO;
    codec->codec_tag = id;
    codec->channels = get_le16(pb);
    codec->sample_rate = get_le32(pb);
    codec->bit_rate = get_le32(pb) * 8;
    codec->block_align = get_le16(pb);
    if (size == 14) {  /* We're dealing with plain vanilla WAVEFORMAT */
        codec->bits_per_sample = 8;
    }else
        codec->bits_per_sample = get_le16(pb);
    codec->codec_id = (CodecID)wav_codec_get_id(id, codec->bits_per_sample);

    if (size > 16) {  /* We're obviously dealing with WAVEFORMATEX */
        codec->extradata_size = get_le16(pb);
        if (codec->extradata_size > 0) {
            if (codec->extradata_size > size - 18)
                codec->extradata_size = size - 18;
            codec->extradata = av_mallocz(codec->extradata_size);
            get_buffer(pb, (unsigned char *)codec->extradata, codec->extradata_size);
        } else
            codec->extradata_size = 0;

        /* It is possible for the chunk to contain garbage at the end */
        if (size - codec->extradata_size - 18 > 0)
            url_fskip(pb, size - codec->extradata_size - 18);
    }
}



/**********************************/
/* decoding */

//#define DEBUG

#ifdef DEBUG
#define PRINT_IF_GUID(g,cmp) \
if (!memcmp(g, &cmp, sizeof(GUID))) \
    printf("(GUID: %s) ", #cmp)

static void print_guid(const GUID *g)
{
    int i;
    PRINT_IF_GUID(g, asf_header);
    else PRINT_IF_GUID(g, file_header);
    else PRINT_IF_GUID(g, stream_header);
    else PRINT_IF_GUID(g, audio_stream);
    else PRINT_IF_GUID(g, audio_conceal_none);
    else PRINT_IF_GUID(g, video_stream);
    else PRINT_IF_GUID(g, video_conceal_none);
    else PRINT_IF_GUID(g, comment_header);
    else PRINT_IF_GUID(g, codec_comment_header);
    else PRINT_IF_GUID(g, codec_comment1_header);
    else PRINT_IF_GUID(g, data_header);
    else PRINT_IF_GUID(g, index_guid);
    else PRINT_IF_GUID(g, head1_guid);
    else PRINT_IF_GUID(g, head2_guid);
    else PRINT_IF_GUID(g, my_guid);
    else
        printf("(GUID: unknown) ");
    printf("0x%08x, 0x%04x, 0x%04x, {", g->v1, g->v2, g->v3);
    for(i=0;i<8;i++)
        printf(" 0x%02x,", g->v4[i]);
    printf("}\n");
}
#undef PRINT_IF_GUID
#endif

static void get_guid(ByteIOContext *s, GUID *g)
{
    int i;

    g->v1 = get_le32(s);
    g->v2 = get_le16(s);
    g->v3 = get_le16(s);
    for(i=0;i<8;i++)
        g->v4[i] = get_byte(s);
}

#if 0
static void get_str16(ByteIOContext *pb, char *buf, int buf_size)
{
    int len, c;
    char *q;

    len = get_le16(pb);
    q = buf;
    while (len > 0) {
        c = get_le16(pb);
        if ((q - buf) < buf_size - 1)
            *q++ = c;
        len--;
    }
    *q = '\0';
}
#endif

static void tag_recode(char *before, int len)
{
    return;
}

static void get_str16_nolen(ByteIOContext *pb, int len, char *buf, int buf_size)
{
    int c, lenz;
    char *q;

    q = buf;
    lenz = len;
    while (len > 0) {
        c = get_byte(pb);
        if ((q - buf) < buf_size-1)
            *q++ = c;
        len--;
    }
    tag_recode(buf, lenz);
}

static int asf_probe(AVProbeData *pd)
{
    GUID g;
    const unsigned char *p;
    int i;

    /* check file header */
    if (pd->buf_size <= 32)
        return 0;
    p = pd->buf;
    g.v1 = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    p += 4;
    g.v2 = p[0] | (p[1] << 8);
    p += 2;
    g.v3 = p[0] | (p[1] << 8);
    p += 2;
    for(i=0;i<8;i++)
        g.v4[i] = *p++;

    if (!memcmp(&g, &asf_header, sizeof(GUID)))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int asf_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    ASFContext *asf = (ASFContext *)s->priv_data;
    GUID g;
    ByteIOContext *pb = &s->pb;
    AVStream *st;
    ASFStream *asf_st;
    //int size, i;
    int i;
    int64_t gsize;

    av_set_pts_info(s, 32, 1, 1000); /* 32 bit pts in ms */

    get_guid(pb, &g);
    if (memcmp(&g, &asf_header, sizeof(GUID)))
        goto fail;
    get_le64(pb);
    get_le32(pb);
    get_byte(pb);
    get_byte(pb);
    memset(&asf->asfid2avid, -1, sizeof(asf->asfid2avid));
    for(;;) {
        get_guid(pb, &g);
        gsize = get_le64(pb);
#ifdef DEBUG
        printf("%08llx: ", url_ftell(pb) - 24);
        print_guid(&g);
        printf("  size=0x%llx\n", gsize);
#endif
        if (gsize < 24)
            goto fail;
        if (!memcmp(&g, &file_header, sizeof(GUID))) {
            get_guid(pb, &asf->hdr.guid);
	    asf->hdr.file_size		= get_le64(pb);
	    asf->hdr.create_time	= get_le64(pb);
	    asf->hdr.packets_count	= get_le64(pb);
	    asf->hdr.play_time		= get_le64(pb);
	    asf->hdr.send_time		= get_le64(pb);
	    asf->hdr.preroll		= get_le32(pb);
	    asf->hdr.ignore		= get_le32(pb);
	    asf->hdr.flags		= get_le32(pb);
	    asf->hdr.min_pktsize	= get_le32(pb);
	    asf->hdr.max_pktsize	= get_le32(pb);
	    asf->hdr.max_bitrate	= get_le32(pb);
	    asf->packet_size = asf->hdr.max_pktsize;
            asf->nb_packets = asf->hdr.packets_count;
        } else if (!memcmp(&g, &stream_header, sizeof(GUID))) {
            int type, total_size, type_specific_size;
            //unsigned int tag1;
            int64_t pos1, pos2;

            pos1 = url_ftell(pb);

            st = av_new_stream(s, 0);
            if (!st)
                goto fail;
            asf_st = (ASFStream*)av_mallocz(sizeof(ASFStream));
            if (!asf_st)
                goto fail;
            st->priv_data = asf_st;
            st->start_time = asf->hdr.preroll / (10000000 / AV_TIME_BASE);
	    st->duration = (asf->hdr.send_time - asf->hdr.preroll) / 
                (10000000 / AV_TIME_BASE);
            get_guid(pb, &g);
            if (!memcmp(&g, &audio_stream, sizeof(GUID))) {
                type = CODEC_TYPE_AUDIO;
            } else if (!memcmp(&g, &video_stream, sizeof(GUID))) {
                type = CODEC_TYPE_VIDEO;
            } else {
                goto fail;
            }
            get_guid(pb, &g);
            total_size = get_le64(pb);
            type_specific_size = get_le32(pb);
            get_le32(pb);
	    st->id = get_le16(pb) & 0x7f; /* stream id */
            // mapping of asf ID to AV stream ID;
            asf->asfid2avid[st->id] = s->nb_streams - 1;

            get_le32(pb);
	    st->codec.codec_type = (CodecType)type;
            /* 1 fps default (XXX: put 0 fps instead) */
            st->codec.frame_rate = 1; 
            st->codec.frame_rate_base = 1;
            if (type == CODEC_TYPE_AUDIO) {
                get_wav_header(pb, &st->codec, type_specific_size);
                st->need_parsing = 1;
		/* We have to init the frame size at some point .... */
		pos2 = url_ftell(pb);
		if (gsize > (pos2 + 8 - pos1 + 24)) {
		    asf_st->ds_span = get_byte(pb);
		    asf_st->ds_packet_size = get_le16(pb);
		    asf_st->ds_chunk_size = get_le16(pb);
		    asf_st->ds_data_size = get_le16(pb);
		    asf_st->ds_silence_data = get_byte(pb);
		}
		//printf("Descrambling: ps:%d cs:%d ds:%d s:%d  sd:%d\n",
		//       asf_st->ds_packet_size, asf_st->ds_chunk_size,
		//       asf_st->ds_data_size, asf_st->ds_span, asf_st->ds_silence_data);
		if (asf_st->ds_span > 1) {
		    if (!asf_st->ds_chunk_size
			|| (asf_st->ds_packet_size/asf_st->ds_chunk_size <= 1))
			asf_st->ds_span = 0; // disable descrambling
		}
                switch (st->codec.codec_id) {
                case CODEC_ID_MP3:
                    st->codec.frame_size = MPA_FRAME_SIZE;
                    break;
                case CODEC_ID_PCM_S16LE:
                case CODEC_ID_PCM_S16BE:
                case CODEC_ID_PCM_U16LE:
                case CODEC_ID_PCM_U16BE:
                case CODEC_ID_PCM_S8:
                case CODEC_ID_PCM_U8:
                case CODEC_ID_PCM_ALAW:
                case CODEC_ID_PCM_MULAW:
                    st->codec.frame_size = 1;
                    break;
                default:
                    /* This is probably wrong, but it prevents a crash later */
                    st->codec.frame_size = 1;
                    break;
                }
	    }
#if 0
	    } else {
		get_le32(pb);
                get_le32(pb);
                get_byte(pb);
                size = get_le16(pb); /* size */
                get_le32(pb); /* size */
                st->codec.width = get_le32(pb);
		st->codec.height = get_le32(pb);
                /* not available for asf */
                get_le16(pb); /* panes */
		st->codec.bits_per_sample = get_le16(pb); /* depth */
                tag1 = get_le32(pb);
		url_fskip(pb, 20);
		if (size > 40) {
		    st->codec.extradata_size = size - 40;
		    st->codec.extradata = av_mallocz(st->codec.extradata_size);
		    get_buffer(pb, st->codec.extradata, st->codec.extradata_size);
		}

        /* Extract palette from extradata if bpp <= 8 */
        /* This code assumes that extradata contains only palette */
        /* This is true for all paletted codecs implemented in ffmpeg */
        if (st->codec.extradata_size && (st->codec.bits_per_sample <= 8)) {
            st->codec.palctrl = av_mallocz(sizeof(AVPaletteControl));
#ifdef WORDS_BIGENDIAN
            for (i = 0; i < FFMIN(st->codec.extradata_size, AVPALETTE_SIZE)/4; i++)
                st->codec.palctrl->palette[i] = bswap_32(((uint32_t*)st->codec.extradata)[i]);
#else
            memcpy(st->codec.palctrl->palette, st->codec.extradata,
                   FFMIN(st->codec.extradata_size, AVPALETTE_SIZE));
#endif
            st->codec.palctrl->palette_changed = 1;
        }

                st->codec.codec_tag = tag1;
		st->codec.codec_id = codec_get_id(codec_bmp_tags, tag1);
            }
#endif	    
            pos2 = url_ftell(pb);
            url_fskip(pb, gsize - (pos2 - pos1 + 24));
        } else if (!memcmp(&g, &data_header, sizeof(GUID))) {
            break;
        } else if (!memcmp(&g, &comment_header, sizeof(GUID))) {
            int len1, len2, len3, len4, len5;

            len1 = get_le16(pb);
            len2 = get_le16(pb);
            len3 = get_le16(pb);
            len4 = get_le16(pb);
            len5 = get_le16(pb);
            get_str16_nolen(pb, len1, s->title, sizeof(s->title));
            get_str16_nolen(pb, len2, s->author, sizeof(s->author));
            get_str16_nolen(pb, len3, s->copyright, sizeof(s->copyright));
            get_str16_nolen(pb, len4, s->comment, sizeof(s->comment));
	    url_fskip(pb, len5);
       } else if (!memcmp(&g, &extended_content_header, sizeof(GUID))) {
                int desc_count, i;

                desc_count = get_le16(pb);
                for(i=0;i<desc_count;i++)
                {
                        int name_len,value_type,value_len,value_num = 0;
                        char *name, *value;

                        name_len = get_le16(pb);
                        name = (char *)av_mallocz(name_len);
                        get_str16_nolen(pb, name_len, name, name_len);
                        value_type = get_le16(pb);
                        value_len = get_le16(pb);
                        if ((value_type == 0) || (value_type == 1)) // unicode or byte
                        {
                                value = (char *)av_mallocz(value_len);
                                get_str16_nolen(pb, value_len, value, value_len);
                                if (strcmp(name,"WM/AlbumTitle")==0) { strcpy(s->album, value); }
                                if (strcmp(name,"WM/Genre")==0) { strcpy(s->genre, value); }
                                if (strcmp(name,"WM/Year")==0) s->year = atoi(value);
                                av_free(value);
                        }
                        if ((value_type >= 2) || (value_type <= 5)) // boolean or DWORD or QWORD or WORD
                        {
                                if (value_type==2) value_num = get_le32(pb);
                                if (value_type==3) value_num = get_le32(pb);
                                if (value_type==4) value_num = get_le64(pb);
                                if (value_type==5) value_num = get_le16(pb);
                                if (strcmp(name,"WM/Track")==0) s->track = value_num + 1;
                                if (strcmp(name,"WM/TrackNumber")==0) s->track = value_num;
                        }
                        av_free(name);
                }
#if 0
        } else if (!memcmp(&g, &head1_guid, sizeof(GUID))) {
            int v1, v2;
            get_guid(pb, &g);
            v1 = get_le32(pb);
            v2 = get_le16(pb);
        } else if (!memcmp(&g, &codec_comment_header, sizeof(GUID))) {
            int len, v1, n, num;
            char str[256], *q;
            char tag[16];

            get_guid(pb, &g);
            print_guid(&g);

            n = get_le32(pb);
            for(i=0;i<n;i++) {
                num = get_le16(pb); /* stream number */
                get_str16(pb, str, sizeof(str));
                get_str16(pb, str, sizeof(str));
                len = get_le16(pb);
                q = tag;
                while (len > 0) {
                    v1 = get_byte(pb);
                    if ((q - tag) < sizeof(tag) - 1)
                        *q++ = v1;
                    len--;
                }
                *q = '\0';
            }
#endif
        } else if (url_feof(pb)) {
            goto fail;
        } else {
            url_fseek(pb, gsize - 24, SEEK_CUR);
        }
    }
    get_guid(pb, &g);
    get_le64(pb);
    get_byte(pb);
    get_byte(pb);
    if (url_feof(pb))
        goto fail;
    asf->data_offset = url_ftell(pb);
    asf->packet_size_left = 0;

    return 0;

 fail:
     for(i=0;i<s->nb_streams;i++) {
        AVStream *st = s->streams[i];
	if (st) {
	    av_free(st->priv_data);
            av_free(st->codec.extradata);
	}
        av_free(st);
    }
    return -1;
}

#define DO_2BITS(bits, var, defval) \
    switch (bits & 3) \
    { \
    case 3: var = get_le32(pb); rsize += 4; break; \
    case 2: var = get_le16(pb); rsize += 2; break; \
    case 1: var = get_byte(pb); rsize++; break; \
    default: var = defval; break; \
    }

static int asf_get_packet(AVFormatContext *s)
{
    ASFContext *asf = (ASFContext *)s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint32_t packet_length, padsize;
    int rsize = 9;
    int c;
    
    assert((url_ftell(&s->pb) - s->data_offset) % asf->packet_size == 0);
    
    c = get_byte(pb);
    if (c != 0x82) {
        if (!url_feof(pb))
	    printf("ff asf bad header %x  at:%lld\n", c, url_ftell(pb));
    }
    if ((c & 0x0f) == 2) { // always true for now
	if (get_le16(pb) != 0) {
            if (!url_feof(pb))
		printf("ff asf bad non zero\n");
	    return -EIO;
	}
        rsize+=2;
/*    }else{
        if (!url_feof(pb))
	    printf("ff asf bad header %x  at:%lld\n", c, url_ftell(pb));
	return -EIO;*/
    }

    asf->packet_flags = get_byte(pb);
    asf->packet_property = get_byte(pb);

    DO_2BITS(asf->packet_flags >> 5, packet_length, asf->packet_size);
    DO_2BITS(asf->packet_flags >> 1, padsize, 0); // sequence ignored
    DO_2BITS(asf->packet_flags >> 3, padsize, 0); // padding length

    asf->packet_timestamp = get_le32(pb);
    get_le16(pb); /* duration */
    // rsize has at least 11 bytes which have to be present

    if (asf->packet_flags & 0x01) {
	asf->packet_segsizetype = get_byte(pb); rsize++;
        asf->packet_segments = asf->packet_segsizetype & 0x3f;
    } else {
	asf->packet_segments = 1;
        asf->packet_segsizetype = 0x80;
    }
    asf->packet_size_left = packet_length - padsize - rsize;
    if (packet_length < asf->hdr.min_pktsize)
        padsize += asf->hdr.min_pktsize - packet_length;
    asf->packet_padsize = padsize;
#ifdef DEBUG
    printf("packet: size=%d padsize=%d  left=%d\n", asf->packet_size, asf->packet_padsize, asf->packet_size_left);
#endif
    return 0;
}

static int asf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASFContext *asf = (ASFContext *)s->priv_data;
    ASFStream *asf_st = 0;
    ByteIOContext *pb = &s->pb;
    //static int pc = 0;
    for (;;) {
	int rsize = 0;
	if (asf->packet_size_left < FRAME_HEADER_SIZE
	    || asf->packet_segments < 1) {
	    //asf->packet_size_left <= asf->packet_padsize) {
	    int ret = asf->packet_size_left + asf->packet_padsize;
	    //printf("PacketLeftSize:%d  Pad:%d Pos:%Ld\n", asf->packet_size_left, asf->packet_padsize, url_ftell(pb));
	    /* fail safe */
	    url_fskip(pb, ret);
            asf->packet_pos= url_ftell(&s->pb);
	    ret = asf_get_packet(s);
	    //printf("READ ASF PACKET  %d   r:%d   c:%d\n", ret, asf->packet_size_left, pc++);
	    if (ret < 0 || url_feof(pb))
		return -EIO;
            asf->packet_time_start = 0;
            continue;
	}
	if (asf->packet_time_start == 0) {
	    /* read frame header */
            int num = get_byte(pb);
	    asf->packet_segments--;
	    rsize++;
	    asf->packet_key_frame = (num & 0x80) >> 7;
	    asf->stream_index = asf->asfid2avid[num & 0x7f];
	    // sequence should be ignored!
	    DO_2BITS(asf->packet_property >> 4, asf->packet_seq, 0);
	    DO_2BITS(asf->packet_property >> 2, asf->packet_frag_offset, 0);
	    DO_2BITS(asf->packet_property, asf->packet_replic_size, 0);
//printf("key:%d stream:%d seq:%d offset:%d replic_size:%d\n", asf->packet_key_frame, asf->stream_index, asf->packet_seq, //asf->packet_frag_offset, asf->packet_replic_size);
	    if (asf->packet_replic_size > 1) {
                assert(asf->packet_replic_size >= 8);
                // it should be always at least 8 bytes - FIXME validate
		asf->packet_obj_size = get_le32(pb);
		asf->packet_frag_timestamp = get_le32(pb); // timestamp
		if (asf->packet_replic_size > 8)
		    url_fskip(pb, asf->packet_replic_size - 8);
		rsize += asf->packet_replic_size; // FIXME - check validity
	    } else if (asf->packet_replic_size==1){
		// multipacket - frag_offset is begining timestamp
		asf->packet_time_start = asf->packet_frag_offset;
                asf->packet_frag_offset = 0;
		asf->packet_frag_timestamp = asf->packet_timestamp;

                asf->packet_time_delta = get_byte(pb);
		rsize++;
	    }else{
                assert(asf->packet_replic_size==0);
            }
	    if (asf->packet_flags & 0x01) {
		DO_2BITS(asf->packet_segsizetype >> 6, asf->packet_frag_size, 0); // 0 is illegal
#undef DO_2BITS
		//printf("Fragsize %d\n", asf->packet_frag_size);
	    } else {
		asf->packet_frag_size = asf->packet_size_left - rsize;
		//printf("Using rest  %d %d %d\n", asf->packet_frag_size, asf->packet_size_left, rsize);
	    }
	    if (asf->packet_replic_size == 1) {
		asf->packet_multi_size = asf->packet_frag_size;
		if (asf->packet_multi_size > asf->packet_size_left) {
		    asf->packet_segments = 0;
                    continue;
		}
	    }
	    asf->packet_size_left -= rsize;
	    //printf("___objsize____  %d   %d    rs:%d\n", asf->packet_obj_size, asf->packet_frag_offset, rsize);

	    if (asf->stream_index < 0) {
                asf->packet_time_start = 0;
		/* unhandled packet (should not happen) */
		url_fskip(pb, asf->packet_frag_size);
		asf->packet_size_left -= asf->packet_frag_size;
		printf("ff asf skip %d  %d\n", asf->packet_frag_size, num & 0x7f);
                continue;
	    }
	    asf->asf_st = (ASFStream *)s->streams[asf->stream_index]->priv_data;
	}
	asf_st = asf->asf_st;

	if ((asf->packet_frag_offset != asf_st->frag_offset
	     || (asf->packet_frag_offset
		 && asf->packet_seq != asf_st->seq)) // seq should be ignored
	   ) {
	    /* cannot continue current packet: free it */
	    // FIXME better check if packet was already allocated
	    printf("ff asf parser skips: %d - %d     o:%d - %d    %d %d   fl:%d\n",
		   asf_st->pkt.size,
		   asf->packet_obj_size,
		   asf->packet_frag_offset, asf_st->frag_offset,
		   asf->packet_seq, asf_st->seq, asf->packet_frag_size);
	    if (asf_st->pkt.size)
		av_free_packet(&asf_st->pkt);
	    asf_st->frag_offset = 0;
	    if (asf->packet_frag_offset != 0) {
		url_fskip(pb, asf->packet_frag_size);
		printf("ff asf parser skiping %db\n", asf->packet_frag_size);
		asf->packet_size_left -= asf->packet_frag_size;
		continue;
	    }
	}
	if (asf->packet_replic_size == 1) {
	    // frag_offset is here used as the begining timestamp
	    asf->packet_frag_timestamp = asf->packet_time_start;
	    asf->packet_time_start += asf->packet_time_delta;
	    asf->packet_obj_size = asf->packet_frag_size = get_byte(pb);
	    asf->packet_size_left--;
            asf->packet_multi_size--;
	    if (asf->packet_multi_size < asf->packet_obj_size)
	    {
		asf->packet_time_start = 0;
		url_fskip(pb, asf->packet_multi_size);
		asf->packet_size_left -= asf->packet_multi_size;
                continue;
	    }
	    asf->packet_multi_size -= asf->packet_obj_size;
	    //printf("COMPRESS size  %d  %d  %d   ms:%d\n", asf->packet_obj_size, asf->packet_frag_timestamp, asf->packet_size_left, asf->packet_multi_size);
	}
	if (asf_st->frag_offset == 0) {
	    /* new packet */
	    av_new_packet(&asf_st->pkt, asf->packet_obj_size);
	    asf_st->seq = asf->packet_seq;
	    asf_st->pkt.pts = asf->packet_frag_timestamp - asf->hdr.preroll;
	    asf_st->pkt.stream_index = asf->stream_index;
            asf_st->packet_pos= asf->packet_pos;            
//printf("new packet: stream:%d key:%d packet_key:%d audio:%d size:%d\n", 
//asf->stream_index, asf->packet_key_frame, asf_st->pkt.flags & PKT_FLAG_KEY,
//s->streams[asf->stream_index]->codec.codec_type == CODEC_TYPE_AUDIO, asf->packet_obj_size);
	    if (s->streams[asf->stream_index]->codec.codec_type == CODEC_TYPE_AUDIO) 
		asf->packet_key_frame = 1;
	    if (asf->packet_key_frame)
		asf_st->pkt.flags |= PKT_FLAG_KEY;
	}

	/* read data */
	//printf("READ PACKET s:%d  os:%d  o:%d,%d  l:%d   DATA:%p\n",
	//       asf->packet_size, asf_st->pkt.size, asf->packet_frag_offset,
	//       asf_st->frag_offset, asf->packet_frag_size, asf_st->pkt.data);
	asf->packet_size_left -= asf->packet_frag_size;
	if (asf->packet_size_left < 0)
            continue;
	get_buffer(pb, asf_st->pkt.data + asf->packet_frag_offset,
		   asf->packet_frag_size);
	asf_st->frag_offset += asf->packet_frag_size;
	/* test if whole packet is read */
	if (asf_st->frag_offset == asf_st->pkt.size) {
	    /* return packet */
	    if (asf_st->ds_span > 1) {
		/* packet descrambling */
		char* newdata = (char*)av_malloc(asf_st->pkt.size);
		if (newdata) {
		    int offset = 0;
		    while (offset < asf_st->pkt.size) {
			int off = offset / asf_st->ds_chunk_size;
			int row = off / asf_st->ds_span;
			int col = off % asf_st->ds_span;
			int idx = row + col * asf_st->ds_packet_size / asf_st->ds_chunk_size;
			//printf("off:%d  row:%d  col:%d  idx:%d\n", off, row, col, idx);
			memcpy(newdata + offset,
			       asf_st->pkt.data + idx * asf_st->ds_chunk_size,
			       asf_st->ds_chunk_size);
			offset += asf_st->ds_chunk_size;
		    }
		    av_free(asf_st->pkt.data);
		    asf_st->pkt.data = (uint8_t *)newdata;
		}
	    }
	    asf_st->frag_offset = 0;
	    memcpy(pkt, &asf_st->pkt, sizeof(AVPacket));
	    //printf("packet %d %d\n", asf_st->pkt.size, asf->packet_frag_size);
	    asf_st->pkt.size = 0;
	    asf_st->pkt.data = 0;
	    break; // packet completed
	}
    }
    return 0;
}

static int asf_read_close(AVFormatContext *s)
{
    int i;

    for(i=0;i<s->nb_streams;i++) {
	AVStream *st = s->streams[i];
	av_free(st->priv_data);
	av_free(st->codec.extradata);
    av_free(st->codec.palctrl);
    }
    return 0;
}

// Added to support seeking after packets have been read
// If information is not reset, read_packet fails due to
// leftover information from previous reads
static void asf_reset_header(AVFormatContext *s)
{
    ASFContext *asf = (ASFContext *)s->priv_data;
    ASFStream *asf_st;
    int i;

    asf->packet_nb_frames = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end = -1;
    asf->packet_size_left = 0;
    asf->packet_segments = 0;
    asf->packet_flags = 0;
    asf->packet_property = 0;
    asf->packet_timestamp = 0;
    asf->packet_segsizetype = 0;
    asf->packet_segments = 0;
    asf->packet_seq = 0;
    asf->packet_replic_size = 0;
    asf->packet_key_frame = 0;
    asf->packet_padsize = 0;
    asf->packet_frag_offset = 0;
    asf->packet_frag_size = 0;
    asf->packet_frag_timestamp = 0;
    asf->packet_multi_size = 0;
    asf->packet_obj_size = 0;
    asf->packet_time_delta = 0;
    asf->packet_time_start = 0;
    
    for(i=0; i<s->nb_streams; i++){
        asf_st= (ASFStream *)(s->streams[i]->priv_data);
        av_free_packet(&asf_st->pkt);
        asf_st->frag_offset=0;
        asf_st->seq=0;
    }
    asf->asf_st= NULL;
}

static int64_t asf_read_pts(AVFormatContext *s, int64_t *ppos, int stream_index)
{
    ASFContext *asf = (ASFContext *)s->priv_data;
    AVPacket pkt1, *pkt = &pkt1;
    ASFStream *asf_st;
    int64_t pts;
    int64_t pos= *ppos;
    int i;
    int64_t start_pos[s->nb_streams];
    
    for(i=0; i<s->nb_streams; i++)
    {
        start_pos[i]= pos;
    }

    url_fseek(&s->pb, pos*asf->packet_size + s->data_offset, SEEK_SET);
    asf_reset_header(s);
    for(;;){
        if (av_read_frame(s, pkt) < 0){
            printf("seek failed\n");
    	    return AV_NOPTS_VALUE;
        }
        pts= pkt->pts;

        av_free_packet(pkt);
        if(pkt->flags&PKT_FLAG_KEY){
            i= pkt->stream_index;

            asf_st= (ASFStream *)s->streams[i]->priv_data;

            assert((asf_st->packet_pos - s->data_offset) % asf->packet_size == 0);
            pos= (asf_st->packet_pos - s->data_offset) / asf->packet_size;

            av_add_index_entry(s->streams[i], pos, pts, pos - start_pos[i] + 1, AVINDEX_KEYFRAME);
            start_pos[i]= pos + 1;
            
            if(pkt->stream_index == stream_index)
               break;
        }
    }

    *ppos= pos;
//printf("found keyframe at %Ld stream %d stamp:%Ld\n", *ppos, stream_index, pts);

    return pts;
}

static int asf_read_seek(AVFormatContext *s, int stream_index, int64_t pts)
{
    ASFContext *asf = (ASFContext *)s->priv_data;
    AVStream *st;
    int64_t pos;
    int64_t pos_min, pos_max, pts_min, pts_max, cur_pts, pos_limit;
    int no_change;
    
    if (stream_index == -1)
        stream_index= av_find_default_stream_index(s);
    
    if (asf->packet_size <= 0)
        return -1;

    pts_max=
    pts_min= AV_NOPTS_VALUE;
    pos_max= pos_limit= -1; // gcc thinks its uninitalized

    st= s->streams[stream_index];
    if(st->index_entries){
        AVIndexEntry *e;
        int index;

        index= av_index_search_timestamp(st, pts);
        e= &st->index_entries[index];
        if(e->timestamp <= pts){
            pos_min= e->pos;
            pts_min= e->timestamp;
#ifdef DEBUG_SEEK
        printf("unsing cached pos_min=0x%llx dts_min=%0.3f\n", 
               pos_min,pts_min / 90000.0);
#endif
        }else{
            assert(index==0);
        }
        index++;
        if(index < st->nb_index_entries){
            e= &st->index_entries[index];
            assert(e->timestamp >= pts);
            pos_max= e->pos;
            pts_max= e->timestamp;
            pos_limit= pos_max - e->min_distance;
#ifdef DEBUG_SEEK
        printf("unsing cached pos_max=0x%llx dts_max=%0.3f\n", 
               pos_max,pts_max / 90000.0);
#endif
        }
    }

    if(pts_min == AV_NOPTS_VALUE){
        pos_min = 0;
        pts_min = asf_read_pts(s, &pos_min, stream_index);
        if (pts_min == AV_NOPTS_VALUE) return -1;
    }
    if(pts_max == AV_NOPTS_VALUE){
        pos_max = (url_filesize(url_fileno(&s->pb)) - 1 - s->data_offset) / asf->packet_size; //FIXME wrong
        pts_max = s->duration; //FIXME wrong
        pos_limit= pos_max;
    } 

    no_change=0;
    while (pos_min < pos_limit) {
        int64_t start_pos;
        assert(pos_limit <= pos_max);

        if(no_change==0){
            int64_t approximate_keyframe_distance= pos_max - pos_limit;
            // interpolate position (better than dichotomy)
            pos = (int64_t)((double)(pos_max - pos_min) *
                            (double)(pts - pts_min) /
                            (double)(pts_max - pts_min)) + pos_min - approximate_keyframe_distance;
        }else if(no_change==1){
            // bisection, if interpolation failed to change min or max pos last time
            pos = (pos_min + pos_limit)>>1;
        }else{
            // linear search if bisection failed, can only happen if there are very few or no keyframes between min/max
            pos=pos_min;
        }
        if(pos <= pos_min)
            pos= pos_min + 1;
        else if(pos > pos_limit)
            pos= pos_limit;
        start_pos= pos;

        // read the next timestamp 
    	cur_pts = asf_read_pts(s, &pos, stream_index);    
        if(pos == pos_max)
            no_change++;
        else
            no_change=0;

#ifdef DEBUG_SEEK
printf("%Ld %Ld %Ld / %Ld %Ld %Ld target:%Ld limit:%Ld start:%Ld\n", pos_min, pos, pos_max, pts_min, cur_pts, pts_max, pts, pos_limit, start_pos);
#endif
        assert (cur_pts != AV_NOPTS_VALUE);
        if (pts < cur_pts) {
            pos_limit = start_pos - 1;
            pos_max = pos;
            pts_max = cur_pts;
        } else {
            pos_min = pos;
            pts_min = cur_pts;
            /* check if we are lucky */
            if (pts == cur_pts)
                break;
        }
    }
    pos = pos_min;
    url_fseek(&s->pb, pos*asf->packet_size + s->data_offset, SEEK_SET);
    asf_reset_header(s);
    return 0;
}

static AVInputFormat asf_iformat = {
    "asf",
    "asf format",
    sizeof(ASFContext),
    asf_probe,
    asf_read_header,
    asf_read_packet,
    asf_read_close,
    asf_read_seek,
};


int asf_init(void)
{
    av_register_input_format(&asf_iformat);
    return 0;
}
}
