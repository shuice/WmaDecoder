#ifndef AVFORMAT_H
#define AVFORMAT_H

#include "Wma_Decoder.h"
#include <time.h>
#include <stdio.h>  /* FILE */
#include "Wma_avcodec.h"
#include "Wma_avio.h"
#include "Wma_os_support.h"

namespace WMADECODER_NAMESPACE
{



#define LIBAVFORMAT_BUILD       4611

#define LIBAVFORMAT_VERSION_INT FFMPEG_VERSION_INT
#define LIBAVFORMAT_VERSION     FFMPEG_VERSION
#define LIBAVFORMAT_IDENT	"FFmpeg" FFMPEG_VERSION "b" AV_STRINGIFY(LIBAVFORMAT_BUILD)



#ifndef AV_NOPTS_VALUE
#define AV_NOPTS_VALUE INT64_MIN
#endif

    
#define AV_TIME_BASE 1000000

typedef struct AVPacket {
    int64_t pts; /* presentation time stamp in AV_TIME_BASE units (or
                    pts_den units in muxers or demuxers) */
    int64_t dts; /* decompression time stamp in AV_TIME_BASE units (or
                    pts_den units in muxers or demuxers) */
    uint8_t *data;
    int   size;
    int   stream_index;
    int   flags;
    int   duration; /* presentation duration (0 if not available) */
    void  (*destruct)(struct AVPacket *);
    void  *priv;
} AVPacket; 
#define PKT_FLAG_KEY   0x0001

/* initialize optional fields of a packet */
static inline void av_init_packet(AVPacket *pkt)
{
    pkt->pts   = AV_NOPTS_VALUE;
    pkt->dts   = AV_NOPTS_VALUE;
    pkt->duration = 0;
    pkt->flags = 0;
    pkt->stream_index = 0;
}

int av_new_packet(AVPacket *pkt, int size);
int av_dup_packet(AVPacket *pkt);

/**
 * Free a packet
 *
 * @param pkt packet to free
 */
static inline void av_free_packet(AVPacket *pkt)
{
    if (pkt && pkt->destruct) {
	pkt->destruct(pkt);
    }
}

/*************************************************/
/* fractional numbers for exact pts handling */

/* the exact value of the fractional number is: 'val + num / den'. num
   is assumed to be such as 0 <= num < den */
typedef struct AVFrac {
    int64_t val, num, den; 
} AVFrac;

void av_frac_init(AVFrac *f, int64_t val, int64_t num, int64_t den);
void av_frac_add(AVFrac *f, int64_t incr);
void av_frac_set(AVFrac *f, int64_t val);

/*************************************************/
/* input/output formats */

struct AVFormatContext;

/* this structure contains the data a format has to probe a file */
typedef struct AVProbeData {
    const char *filename;
    unsigned char *buf;
    int buf_size;
} AVProbeData;

#define AVPROBE_SCORE_MAX 100

typedef struct AVFormatParameters {
    int frame_rate;
    int frame_rate_base;
    int sample_rate;
    int channels;
    int width;
    int height;
    enum PixelFormat pix_fmt;
    struct AVImageFormat *image_format;
    int channel; /* used to select dv channel */
    const char *device; /* video4linux, audio or DV device */
    const char *standard; /* tv standard, NTSC, PAL, SECAM */
    int mpeg2ts_raw:1;  /* force raw MPEG2 transport stream output, if possible */
    int mpeg2ts_compute_pcr:1; /* compute exact PCR for each transport
                                  stream packet (only meaningful if
                                  mpeg2ts_raw is TRUE */
    int initial_pause:1;       /* do not begin to play the stream
                                  immediately (RTSP only) */
} AVFormatParameters;

#define AVFMT_NOFILE        0x0001 /* no file should be opened */
#define AVFMT_NEEDNUMBER    0x0002 /* needs '%d' in filename */ 
#define AVFMT_SHOW_IDS      0x0008 /* show format stream IDs numbers */
#define AVFMT_RAWPICTURE    0x0020 /* format wants AVPicture structure for
                                      raw picture data */

typedef struct AVOutputFormat {
    const char *name;
    const char *long_name;
    const char *mime_type;
    const char *extensions; /* comma separated extensions */
    /* size of private data so that it can be allocated in the wrapper */
    int priv_data_size;
    /* output support */
    enum CodecID audio_codec; /* default audio codec */
    enum CodecID video_codec; /* default video codec */
    int (*write_header)(struct AVFormatContext *);
    int (*write_packet)(struct AVFormatContext *, 
                        int stream_index,
                        const uint8_t *buf, int size, int64_t pts);
    int (*write_trailer)(struct AVFormatContext *);
    /* can use flags: AVFMT_NOFILE, AVFMT_NEEDNUMBER */
    int flags;
    /* currently only used to set pixel format if not YUV420P */
    int (*set_parameters)(struct AVFormatContext *, AVFormatParameters *);
    /* private fields */
    struct AVOutputFormat *next;
} AVOutputFormat;

typedef struct AVInputFormat {
    const char *name;
    const char *long_name;
    /* size of private data so that it can be allocated in the wrapper */
    int priv_data_size;
    /* tell if a given file has a chance of being parsing by this format */
    int (*read_probe)(AVProbeData *);
    /* read the format header and initialize the AVFormatContext
       structure. Return 0 if OK. 'ap' if non NULL contains
       additionnal paramters. Only used in raw format right
       now. 'av_new_stream' should be called to create new streams.  */
    int (*read_header)(struct AVFormatContext *,
                       AVFormatParameters *ap);
    /* read one packet and put it in 'pkt'. pts and flags are also
       set. 'av_new_stream' can be called only if the flag
       AVFMTCTX_NOHEADER is used. */
    int (*read_packet)(struct AVFormatContext *, AVPacket *pkt);
    /* close the stream. The AVFormatContext and AVStreams are not
       freed by this function */
    int (*read_close)(struct AVFormatContext *);
    /* seek at or before a given timestamp (given in AV_TIME_BASE
       units) relative to the frames in stream component stream_index */
    int (*read_seek)(struct AVFormatContext *, 
                     int stream_index, int64_t timestamp);
    /* can use flags: AVFMT_NOFILE, AVFMT_NEEDNUMBER */
    int flags;
    /* if extensions are defined, then no probe is done. You should
       usually not use extension format guessing because it is not
       reliable enough */
    const char *extensions;
    /* general purpose read only value that the format can use */
    int value;

    /* start/resume playing - only meaningful if using a network based format
       (RTSP) */
    int (*read_play)(struct AVFormatContext *);

    /* pause playing - only meaningful if using a network based format
       (RTSP) */
    int (*read_pause)(struct AVFormatContext *);

    /* private fields */
    struct AVInputFormat *next;
} AVInputFormat;

typedef struct AVIndexEntry {
    int64_t pos;
    int64_t timestamp;
#define AVINDEX_KEYFRAME 0x0001
/* the following 2 flags indicate that the next/prev keyframe is known, and scaning for it isnt needed */
    int flags;
    int min_distance;         /* min distance between this and the previous keyframe, used to avoid unneeded searching */
} AVIndexEntry;

typedef struct AVStream {
    int index;    /* stream index in AVFormatContext */
    int id;       /* format specific stream id */
    AVCodecContext codec; /* codec context */
    int r_frame_rate;     /* real frame rate of the stream */
    int r_frame_rate_base;/* real frame rate base of the stream */
    void *priv_data;
    /* internal data used in av_find_stream_info() */
    int64_t codec_info_duration;     
    int codec_info_nb_frames;
    /* encoding: PTS generation when outputing stream */
    AVFrac pts;
    /* ffmpeg.c private use */
    int stream_copy; /* if TRUE, just copy stream */
    /* quality, as it has been removed from AVCodecContext and put in AVVideoFrame
     * MN:dunno if thats the right place, for it */
    float quality; 
    /* decoding: position of the first frame of the component, in
       AV_TIME_BASE fractional seconds. */
    int64_t start_time; 
    /* decoding: duration of the stream, in AV_TIME_BASE fractional
       seconds. */
    int64_t duration;

    /* av_read_frame() support */
    int need_parsing;
    struct AVCodecParserContext *parser;

    int64_t cur_dts;
    int last_IP_duration;
    /* av_seek_frame() support */
    AVIndexEntry *index_entries; /* only used if the format does not
                                    support seeking natively */
    int nb_index_entries;
    int index_entries_allocated_size;
} AVStream;

#define AVFMTCTX_NOHEADER      0x0001 /* signal that no header is present
                                         (streams are added dynamically) */

#define MAX_STREAMS 20

/* format I/O context */
typedef struct AVFormatContext {
    /* can only be iformat or oformat, not both at the same time */
    struct AVInputFormat *iformat;
    struct AVOutputFormat *oformat;
    void *priv_data;
    ByteIOContext pb;
    int nb_streams;
    AVStream *streams[MAX_STREAMS];
    char filename[1024]; /* input or output filename */
    /* stream info */
    char title[512];
    char author[512];
    char copyright[512];
    char comment[512];
    char album[512];
    int year;  /* ID3 year, 0 if none */
    int track; /* track number, 0 if none */
    char genre[32]; /* ID3 genre */
    
    int ctx_flags; /* format specific flags, see AVFMTCTX_xx */
    /* private data for pts handling (do not modify directly) */
    int pts_wrap_bits; /* number of bits in pts (used for wrapping control) */
    int pts_num, pts_den; /* value to convert to seconds */
    /* This buffer is only needed when packets were already buffered but
       not decoded, for example to get the codec parameters in mpeg
       streams */
    struct AVPacketList *packet_buffer;

    /* decoding: position of the first frame of the component, in
       AV_TIME_BASE fractional seconds. NEVER set this value directly:
       it is deduced from the AVStream values.  */
    int64_t start_time; 
    /* decoding: duration of the stream, in AV_TIME_BASE fractional
       seconds. NEVER set this value directly: it is deduced from the
       AVStream values.  */
    int64_t duration;
    /* decoding: total file size. 0 if unknown */
    int64_t file_size;
    /* decoding: total stream bitrate in bit/s, 0 if not
       available. Never set it directly if the file_size and the
       duration are known as ffmpeg can compute it automatically. */
    int bit_rate;

    /* av_read_frame() support */
    AVStream *cur_st;
    const uint8_t *cur_ptr;
    int cur_len;
    AVPacket cur_pkt;

    /* the following are used for pts/dts unit conversion */
    int64_t last_pkt_stream_pts;
    int64_t last_pkt_stream_dts;
    int64_t last_pkt_pts;
    int64_t last_pkt_dts;
    int last_pkt_pts_frac;
    int last_pkt_dts_frac;

    /* av_seek_frame() support */
    int64_t data_offset; /* offset of the first packet */
    int index_built;
} AVFormatContext;

typedef struct AVPacketList {
    AVPacket pkt;
    struct AVPacketList *next;
} AVPacketList;

extern AVInputFormat *first_iformat;
extern AVOutputFormat *first_oformat;

/* still image support */
struct AVInputImageContext;
typedef struct AVInputImageContext AVInputImageContext;

typedef struct AVImageInfo {
    enum PixelFormat pix_fmt; /* requested pixel format */
    int width; /* requested width */
    int height; /* requested height */
    int interleaved; /* image is interleaved (e.g. interleaved GIF) */
    AVPicture pict; /* returned allocated image */
} AVImageInfo;

/* AVImageFormat.flags field constants */
#define AVIMAGE_INTERLEAVED 0x0001 /* image format support interleaved output */


/* asf.c */
int asf_init(void);

/* yuv4mpeg.c */
extern AVOutputFormat yuv4mpegpipe_oformat;

/* utils.c */
void av_register_input_format(AVInputFormat *format);
void av_register_output_format(AVOutputFormat *format);
AVOutputFormat *guess_stream_format(const char *short_name, 
                                    const char *filename, const char *mime_type);
AVOutputFormat *guess_format(const char *short_name, 
                             const char *filename, const char *mime_type);

void av_hex_dump(FILE *f, uint8_t *buf, int size);
void av_pkt_dump(FILE *f, AVPacket *pkt, int dump_payload);

void av_register_all(void);

typedef struct FifoBuffer {
    uint8_t *buffer;
    uint8_t *rptr, *wptr, *end;
} FifoBuffer;

int fifo_init(FifoBuffer *f, int size);
void fifo_free(FifoBuffer *f);
int fifo_size(FifoBuffer *f, uint8_t *rptr);
int fifo_read(FifoBuffer *f, uint8_t *buf, int buf_size, uint8_t **rptr_ptr);
void fifo_write(FifoBuffer *f, uint8_t *buf, int size, uint8_t **wptr_ptr);

/* media file input */
AVInputFormat *av_find_input_format(const char *short_name);
AVInputFormat *av_probe_input_format(AVProbeData *pd, int is_opened);
int av_open_input_stream(AVFormatContext **ic_ptr, 
                         ByteIOContext *pb, const char *filename, 
                         AVInputFormat *fmt, AVFormatParameters *ap);
int av_open_input_file(AVFormatContext **ic_ptr, const char *filename, 
                       AVInputFormat *fmt,
                       int buf_size,
                       AVFormatParameters *ap);

#define AVERROR_UNKNOWN     (-1)  /* unknown error */
#define AVERROR_IO          (-2)  /* i/o error */
#define AVERROR_NUMEXPECTED (-3)  /* number syntax expected in filename */
#define AVERROR_INVALIDDATA (-4)  /* invalid data found */
#define AVERROR_NOMEM       (-5)  /* not enough memory */
#define AVERROR_NOFMT       (-6)  /* unknown format */
#define AVERROR_NOTSUPP     (-7)  /* operation not supported */
 
int av_find_stream_info(AVFormatContext *ic);
int av_read_packet(AVFormatContext *s, AVPacket *pkt);
int av_read_frame(AVFormatContext *s, AVPacket *pkt);
int av_seek_frame(AVFormatContext *s, int stream_index, int64_t timestamp);
int av_read_play(AVFormatContext *s);
int av_read_pause(AVFormatContext *s);
void av_close_input_file(AVFormatContext *s);
AVStream *av_new_stream(AVFormatContext *s, int id);
void av_set_pts_info(AVFormatContext *s, int pts_wrap_bits,
                     int pts_num, int pts_den);

int av_find_default_stream_index(AVFormatContext *s);
int av_index_search_timestamp(AVStream *st, int timestamp);
int av_add_index_entry(AVStream *st,
                       int64_t pos, int64_t timestamp, int distance, int flags);

/* media file output */
int av_set_parameters(AVFormatContext *s, AVFormatParameters *ap);
int av_write_header(AVFormatContext *s);
int av_write_frame(AVFormatContext *s, int stream_index, const uint8_t *buf, 
                   int size);
int av_write_trailer(AVFormatContext *s);

int parse_image_size(int *width_ptr, int *height_ptr, const char *str);
int parse_frame_rate(int *frame_rate, int *frame_rate_base, const char *arg);


int64_t av_gettime(void);

/* ffm specific for ffserver */
#define FFM_PACKET_SIZE 4096
offset_t ffm_read_write_index(int fd);
void ffm_write_write_index(int fd, offset_t pos);
void ffm_set_write_index(AVFormatContext *s, offset_t pos, offset_t file_size);


int get_frame_filename(char *buf, int buf_size,
                       const char *path, int number);
int filename_number_test(const char *filename);

/* grab specific */
int video_grab_init(void);
int audio_init(void);

/* DV1394 */
int dv1394_init(void);




int strstart(const char *str, const char *val, const char **ptr);
void pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);




void url_split(char *proto, int proto_size,
               char *hostname, int hostname_size,
               int *port_ptr,
               char *path, int path_size,
               const char *url);

int match_ext(const char *filename, const char *extensions);


}
#endif /* AVFORMAT_H */
