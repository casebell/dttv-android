#ifndef DT_MEDIA_INFO_H
#define DT_MEDIA_INFO_H

#include "dt_type.h"
#include "dt_av.h"
#include "dt_macro.h"

#include "stdint.h"

#define MAX_VIDEO_STREAM_NUM 5
#define MAX_AUDIO_STREAM_NUM 20
#define MAX_SUBTITLE_STREAM_NUM 20

#define LANGUAGE_MAX_SIZE 1024

typedef struct {
    int num;
    int den;
} dtratio;

typedef struct {
    int index;
    int id;
    int bit_rate;
    int width;
    int height;
    int pix_fmt;
    int64_t duration;
    dtratio sample_aspect_ratio; //witdh:height
    dtratio frame_rate_ratio;
    dtratio time_base;
    int extradata_size;
    uint8_t extradata[VIDEO_EXTRADATA_SIZE];
    char language[LANGUAGE_MAX_SIZE];
    dtvideo_format_t format;
    void *codec_priv;
} vstream_info_t;

typedef struct {
    char title[512];
    char author[512];
    char album[512];
    char comment[512];
    char year[4];
    int track;
    char genre[32];
    char copyright[512];
    int cover_type; // 0-none 1-jpg 2-png
} album_info_t;

typedef struct {
    int index;
    int id;
    int bit_rate;
    int sample_rate;
    int channels;
    int bps;
    int64_t duration;
    dtratio time_base;
    int extradata_size;
    uint8_t *extradata;
    char language[LANGUAGE_MAX_SIZE];
    dtaudio_format_t format;
    album_info_t album_info;
    void *codec_priv;
} astream_info_t;

typedef struct {
    int index;
    int id;
    int bit_rate;
    int width;
    int height;
    int extradata_size;
    uint8_t *extradata;
    char language[LANGUAGE_MAX_SIZE];
    dtsub_format_t format;
    void *codec_priv;
} sstream_info_t;

typedef struct {
    char file_name[FILE_NAME_MAX_LENGTH];
    dtmedia_format_t format;
    int64_t start_time;
    int64_t duration;
    int64_t file_size;
    int bit_rate;

    unsigned int nb_stream;
    int has_video;
    int disable_video;
    int vst_num;
    int cur_vst_index;
    int has_audio;
    int disable_audio;
    int ast_num;
    int cur_ast_index;
    int has_sub;
    int disable_sub;
    int sst_num;
    int cur_sst_index;

    vstream_info_t *vstreams[MAX_VIDEO_STREAM_NUM];
    astream_info_t *astreams[MAX_AUDIO_STREAM_NUM];
    sstream_info_t *sstreams[MAX_SUBTITLE_STREAM_NUM];
} dt_media_info_t;

#endif
