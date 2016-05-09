/* 
 * porting from ffmpeg to dtplayer 
 *
 * author: peter_future@outlook.com
 *
 * */


#include <android/log.h>

#include <binder/ProcessState.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaBufferGroup.h>
//#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/OMXClient.h>
#include <media/stagefright/OMXCodec.h>
#include <utils/List.h>
#include <new>
#include <map>

extern "C" {

#include "vd_wrapper.h"

}

#define TAG "VD-STAGEFRIGHT"

namespace android {
extern "C" {

#define OMX_QCOM_COLOR_FormatYVU420SemiPlanar 0x7FA30C00
#define FF_INPUT_BUFFER_PADDING_SIZE 32
#define TAG "VD-STAGEFRIGHT"

#define av_freep free
#define av_frame_free free
#define av_mallocz malloc
#define av_malloc malloc

struct Frame {
    status_t status;
    size_t size;
    int64_t time;
    int key;
    uint8_t *buffer;
    dt_av_frame_t *vframe; // for output
};

struct TimeStamp {
    int64_t pts;
    int64_t reordered_opaque;
};

class CustomSource;

struct StagefrightContext {
    uint8_t* orig_extradata;
    int orig_extradata_size;
    sp<MediaSource> *source;
    List<Frame*> *in_queue, *out_queue;
    pthread_mutex_t in_mutex, out_mutex;
    pthread_cond_t condition;
    pthread_t decode_thread_id;

    Frame *end_frame;
    bool source_done;
    volatile sig_atomic_t thread_started, thread_exited, stop_decode;

    dt_av_frame_t *prev_frame;
    std::map<int64_t, TimeStamp> *ts_map;
    int64_t frame_index;

    uint8_t *dummy_buf;
    int dummy_bufsize;

    OMXClient *client;
    sp<MediaSource> *decoder;
    const char *decoder_component;
    int info_changed;
};

class CustomSource : public MediaSource {
public:
    CustomSource(dtvideo_decoder_t *decoder, sp<MetaData> meta) {
        dtvideo_para_t *vd_para = &decoder->para;
        s = (StagefrightContext*)decoder->vd_priv;
        source_meta = meta;
        frame_size  = (vd_para->s_width * vd_para->s_height * 3) / 2;
        buf_group.add_buffer(new MediaBuffer(frame_size));
    }

    virtual sp<MetaData> getFormat() {
        return source_meta;
    }

    virtual status_t start(MetaData *params) {
        return OK;
    }

    virtual status_t stop() {
        return OK;
    }

    virtual status_t read(MediaBuffer **buffer,
                          const MediaSource::ReadOptions *options) {
        Frame *frame;
        status_t ret;

        if (s->thread_exited)
            return ERROR_END_OF_STREAM;
        pthread_mutex_lock(&s->in_mutex);
        
        while (s->in_queue->empty())
        {
            //__android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step read, list empty wait \n");
            pthread_cond_wait(&s->condition, &s->in_mutex);
        }
        //__android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step read, list->size:%d \n",s->in_queue->size());
        frame = *s->in_queue->begin();
        ret = frame->status;

        if (ret == OK) {
            ret = buf_group.acquire_buffer(buffer);
            if (ret == OK) {
                memcpy((*buffer)->data(), frame->buffer, frame->size);
                (*buffer)->set_range(0, frame->size);
                (*buffer)->meta_data()->clear();
                (*buffer)->meta_data()->setInt32(kKeyIsSyncFrame,frame->key);
                (*buffer)->meta_data()->setInt64(kKeyTime, frame->time);
            } else {
                __android_log_print(ANDROID_LOG_DEBUG,TAG, "Failed to acquire MediaBuffer \n");
                //av_log(s->avctx, AV_LOG_ERROR, "Failed to acquire MediaBuffer\n");
            }
            av_freep(frame->buffer);
        }

        s->in_queue->erase(s->in_queue->begin());
        pthread_mutex_unlock(&s->in_mutex);
        //__android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step read one frame ok , size:%d key:%d \n",frame->size,frame->key);

        av_freep(frame);
        return ret;
    }

private:
    MediaBufferGroup buf_group;
    sp<MetaData> source_meta;
    StagefrightContext *s;
    int frame_size;
};

static int dt_get_line_size(int pix_fmt, int w, int plane)
{
    if(pix_fmt == DTAV_PIX_FMT_YUV420P)
    {
       switch(plane) 
       {
           case 0:
               return w + 32;
           default:
               return w/2 + 16;
       }
    }

    if(pix_fmt == DTAV_PIX_FMT_NV21)
    {
        switch(plane) 
        {
            case 0:
                return w + 32;
            default:
                return w/2 + 16;
       }
    }

    return -1; // not support
}

void* decode_thread(void *arg)
{
    dtvideo_decoder_t *decoder = (dtvideo_decoder_t *)arg;
    StagefrightContext *s = (StagefrightContext*)decoder->vd_priv;
    Frame* frame;
    MediaBuffer *buffer;
    int32_t w, h;
    int decode_done = 0;
    int ret;
    int src_linesize[3];
    const uint8_t *src_data[3];
    int64_t out_frame_index = 0;
    int pic_size = 0;

    int pix_fmt = decoder->wrapper->para->s_pixfmt;

    do {
        buffer = NULL;
        frame = (Frame*)av_mallocz(sizeof(Frame));
        if (!frame) {
            continue;
#if 0
            frame         = s->end_frame;
            frame->status = -1;
            //frame->status = AVERROR(ENOMEM);
            decode_done   = 1;
            s->end_frame  = NULL;
            goto push_frame;
#endif
        }
        memset(frame,0,sizeof(Frame));
        frame->status = (*s->decoder)->read(&buffer);
        //__android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step read one frame, status:%d  \n",frame->status);
        if (frame->status == OK) {

            if(buffer->range_length() == 0) // invalid buf, release
            {
                buffer->release();
                buffer = NULL;
                free(frame);
                continue;
            }

            sp<MetaData> outFormat = (*s->decoder)->getFormat();
            outFormat->findInt32(kKeyWidth , &w);
            outFormat->findInt32(kKeyHeight, &h);
            frame->vframe = (dt_av_frame_t *)malloc(sizeof(dt_av_frame_t));
            if (!frame->vframe) {
                //frame->status = AVERROR(ENOMEM);
                frame->status = -1;
                decode_done   = 1;
                buffer->release();
                goto push_frame;
            }
            memset(frame->vframe, 0, sizeof(dt_av_frame_t));
            
            pic_size = buffer->range_length();
            //__android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step malloc buffer, size:%di rangesize:%d\n",pic_size,buffer->range_length());
            frame->vframe->data[0] = (uint8_t *)malloc(pic_size);

            // The OMX.SEC decoder doesn't signal the modified width/height
            if (s->decoder_component && !strncmp(s->decoder_component, "OMX.SEC", 7) &&
                (w & 15 || h & 15)) {
                if (((w + 15)&~15) * ((h + 15)&~15) * 3/2 == buffer->range_length()) {
                    w = (w + 15)&~15;
                    h = (h + 15)&~15;
                }
            }

           //line size no need to set
            frame->vframe->linesize[0] = dt_get_line_size(pix_fmt, w, 0);
            frame->vframe->linesize[1] = dt_get_line_size(pix_fmt, w, 1);
            frame->vframe->linesize[2] = dt_get_line_size(pix_fmt, w, 2);

            uint8_t *tmp_buf = (uint8_t*)buffer->data();
            memcpy(frame->vframe->data[0],tmp_buf,pic_size);

            buffer->meta_data()->findInt64(kKeyTime, &out_frame_index);
            frame->vframe->pts = out_frame_index;
#if 0
            if (out_frame_index && s->ts_map->count(out_frame_index) > 0) {
                frame->vframe->pts = (*s->ts_map)[out_frame_index].pts;
                //frame->vframe->reordered_opaque = (*s->ts_map)[out_frame_index].reordered_opaque;
                s->ts_map->erase(out_frame_index);
            }
#endif
            buffer->release();
            //__android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step decoded one frame : pts:%llx outindex:%llx size:%d \n",frame->vframe->pts, out_frame_index, pic_size);
            
        } else if (frame->status == INFO_FORMAT_CHANGED) {
                __android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step info chaned :%d \n",frame->status);
                if (buffer)
                    buffer->release();
                av_freep(frame);
                continue;
        } else {
            //__android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step decode failed, maybe no data left \n");
            usleep(1000);
            continue;
            decode_done = 1;
        }
push_frame:
        while (true) {
            pthread_mutex_lock(&s->out_mutex);
            if (s->out_queue->size() >= 10) {
                pthread_mutex_unlock(&s->out_mutex);
                usleep(10000);
                continue;
            }
            break;
        }
        s->out_queue->push_back(frame);
        pthread_mutex_unlock(&s->out_mutex);
    } while (!decode_done && !s->stop_decode);

    s->thread_exited = true;

    return 0;
}

static int Stagefright_init(dtvideo_decoder_t *decoder)
{
    vd_wrapper_t *wrapper = decoder->wrapper;
    //StagefrightContext *s = (StagefrightContext *)malloc(sizeof(StagefrightContext));
    //memset(s,0,sizeof(StagefrightContext));

    StagefrightContext *s = new StagefrightContext();
    decoder->vd_priv = s;
    dtvideo_para_t *vd_para = &decoder->para;

    sp<MetaData> meta, outFormat;
    int32_t colorFormat = 0;
    int pix_fmt;
    int ret;

    if(vd_para->extradata_size > 0 && vd_para->extradata[0] != 0x1)
    	vd_para->extradata_size = 0;

    //if (!vd_para->extradata || !vd_para->extradata_size || vd_para->extradata[0] != 1)
    if (!vd_para->extradata || !vd_para->extradata_size)
    {
        __android_log_print(ANDROID_LOG_INFO,TAG, "NO Valid Extradata Find \n");
        s->orig_extradata_size = 0;
        //return -1;
    }
    else
    {
        s->orig_extradata_size = vd_para->extradata_size;
        s->orig_extradata = (uint8_t*) malloc(vd_para->extradata_size +
                                              FF_INPUT_BUFFER_PADDING_SIZE);
        if (!s->orig_extradata) {
            ret = -1;
            goto fail;
        }
        memcpy(s->orig_extradata, vd_para->extradata, vd_para->extradata_size);
    }

    meta = new MetaData;
    if (meta == NULL) {
        ret = -1;
        goto fail;
    }
    meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_AVC);
    meta->setInt32(kKeyWidth, vd_para->s_width);
    meta->setInt32(kKeyHeight, vd_para->s_height);
    if(s->orig_extradata_size > 0)
        meta->setData(kKeyAVCC, kTypeAVCC, vd_para->extradata, vd_para->extradata_size);

    __android_log_print(ANDROID_LOG_DEBUG,TAG, "meta set ok \n");
    __android_log_print(ANDROID_LOG_INFO,TAG, "==================================\n");
    __android_log_print(ANDROID_LOG_INFO,TAG, "Start To Init OMX Codec\n");
    __android_log_print(ANDROID_LOG_INFO,TAG, "width - %d height - %d\n", vd_para->s_width, vd_para->s_height);
    __android_log_print(ANDROID_LOG_INFO,TAG, "extrasize:%d\n", vd_para->extradata_size);
    if(s->orig_extradata_size > 0){
    	__android_log_print(ANDROID_LOG_INFO,TAG, "extradata:%02x - %02x %02x %02x \n", vd_para->extradata[0], vd_para->extradata[1], vd_para->extradata[2], vd_para->extradata[3]);
    	__android_log_print(ANDROID_LOG_INFO,TAG, "extradata:%02x - %02x %02x %02x \n", vd_para->extradata[4], vd_para->extradata[5], vd_para->extradata[6], vd_para->extradata[7]);
    	__android_log_print(ANDROID_LOG_INFO,TAG, "extradata:%02x - %02x %02x %02x \n", vd_para->extradata[8], vd_para->extradata[9], vd_para->extradata[10], vd_para->extradata[11]);
    }
    __android_log_print(ANDROID_LOG_INFO,TAG, "==================================\n");

    android::ProcessState::self()->startThreadPool();

    s->source    = new sp<MediaSource>();
    *s->source   = new CustomSource(decoder, meta);
    //s->source = new sp<MediaSource>(new CustomSource(decoder, meta));
    s->in_queue  = new List<Frame*>;
    s->out_queue = new List<Frame*>;
    s->ts_map    = new std::map<int64_t, TimeStamp>;
    s->client    = new OMXClient;
    s->end_frame = (Frame*)malloc(sizeof(Frame));
    if (s->source == NULL || !s->in_queue || !s->out_queue || !s->client ||
        !s->ts_map || !s->end_frame) {
        ret = -1;
        goto fail;
    }

    if (s->client->connect() !=  OK) {
    	__android_log_print(ANDROID_LOG_DEBUG,TAG,"Cannot connect OMX client\n");
        ret = -1;
        goto fail;
    }
    __android_log_print(ANDROID_LOG_DEBUG,TAG, "client->connect ok\n");

    s->decoder  = new sp<MediaSource>();
    *s->decoder = OMXCodec::Create(s->client->interface(), meta,
                                  false, *s->source, NULL,
                                  OMXCodec::kClientNeedsFramebuffer, NULL);
    if(*s->decoder == NULL)
    {
        __android_log_print(ANDROID_LOG_DEBUG,TAG, "create omxcodec failed \n");
        goto fail;
    }
    __android_log_print(ANDROID_LOG_DEBUG,TAG, "omxcodec create ok \n");
    if ((*s->decoder)->start() !=  OK) {
    	__android_log_print(ANDROID_LOG_DEBUG,TAG,"Cannot start decoder\n");
        ret = -1;
        s->client->disconnect();
        goto fail;
    }
    __android_log_print(ANDROID_LOG_DEBUG,TAG, "omxcodec start ok \n");

    outFormat = (*s->decoder)->getFormat();
    outFormat->findInt32(kKeyColorFormat, &colorFormat);
   
    if (colorFormat == OMX_QCOM_COLOR_FormatYVU420SemiPlanar ||
        colorFormat == OMX_COLOR_FormatYUV420SemiPlanar)
        pix_fmt = DTAV_PIX_FMT_NV21;
    else if (colorFormat == OMX_COLOR_FormatYCbYCr)
        pix_fmt = DTAV_PIX_FMT_YUYV422;
    else if (colorFormat == OMX_COLOR_FormatCbYCrY)
        pix_fmt = DTAV_PIX_FMT_UYVY422;
    else
        pix_fmt = DTAV_PIX_FMT_YUV420P;
    
    __android_log_print(ANDROID_LOG_DEBUG,TAG, "get colorFormat info, color format:%d pix_fmt:%d  \n", colorFormat, pix_fmt);
    
    outFormat->findCString(kKeyDecoderComponent, &s->decoder_component);
    if (s->decoder_component)
    {
        s->decoder_component = strdup(s->decoder_component);
        __android_log_print(ANDROID_LOG_DEBUG,TAG, "decoder component:%s  \n",s->decoder_component);
    }
    pthread_mutex_init(&s->in_mutex, NULL);
    pthread_mutex_init(&s->out_mutex, NULL);
    pthread_cond_init(&s->condition, NULL);
    wrapper->para = &decoder->para;

    if(pix_fmt != wrapper->para->s_pixfmt)
    {
        wrapper->para->s_pixfmt = pix_fmt;
        s->info_changed = 1;
        __android_log_print(ANDROID_LOG_DEBUG,TAG, "Info chanaged, dst picfmt:%d \n", pix_fmt);
    }
    __android_log_print(ANDROID_LOG_DEBUG,TAG, "plugin stagefright omx decoder init ok \n");
    return 0;

fail:
    //av_bitstream_filter_close(s->bsfc);
    if(s->orig_extradata_size)
        av_freep(&s->orig_extradata);
    av_freep(&s->end_frame);
    delete s->in_queue;
    delete s->out_queue;
    delete s->ts_map;
    delete s->client;
    return ret;
}

static int Stagefright_decode_frame(dtvideo_decoder_t *decoder, dt_av_pkt_t *vd_pkt, dt_av_frame_t **data)
{
    StagefrightContext *s = (StagefrightContext*)decoder->vd_priv;
    Frame *frame;
    status_t status;
    int orig_size = vd_pkt->size;

    dt_av_frame_t *ret_frame;
//    __android_log_print(ANDROID_LOG_INFO, TAG, "enter decode frame, size:%d \n", vd_pkt->size);
    if (!s->thread_started) {
        pthread_create(&s->decode_thread_id, NULL, &decode_thread, decoder);
        s->thread_started = true;
        __android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step 1 start decode thread ok\n");
    }
    
    if(vd_pkt->size <= 0)
        goto OUT;
    
    if (!s->source_done) {
        if(!s->dummy_buf) {
            s->dummy_buf = (uint8_t*)av_malloc(vd_pkt->size);
            if (!s->dummy_buf)
                return -1;
            s->dummy_bufsize = vd_pkt->size;
            memcpy(s->dummy_buf, vd_pkt->data, vd_pkt->size);
        }

        frame = (Frame*)av_mallocz(sizeof(Frame));
        memset(frame, 0, sizeof(Frame));
        if (vd_pkt->data) {
            frame->status  = OK;
            frame->size    = vd_pkt->size;
            frame->key     = vd_pkt->key_frame;
            frame->buffer  = (uint8_t*)av_malloc(frame->size);
            if (!frame->buffer) {
                av_freep(&frame);
                return -1;
            }
            uint8_t *ptr = vd_pkt->data;
            memcpy(frame->buffer, ptr, orig_size);
            //frame->time = ++s->frame_index;
            frame->time = vd_pkt->pts;
            //(*s->ts_map)[s->frame_index].pts = vd_frame->pts; // do not store pts
           // __android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step, fill frame,size:%d  %02x %02x %02x %02x %02x %02x\n",frame->size,frame->buffer[0],frame->buffer[1],frame->buffer[2],frame->buffer[3],frame->buffer[4],frame->buffer[5]);
            //__android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step, fill frame, %02x %02x %02x %02x %02x %02x\n",ptr[0],ptr[1],ptr[2],ptr[3],ptr[4],ptr[5]);
            //(*s->ts_map)[s->frame_index].reordered_opaque = vd_frame->reordered_opaque;
            //__android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step, push frame, index:%llx pts:%llx \n",s->frame_index, vd_pkt->pts);
        } 
        
        while (true) 
        {
            if (s->thread_exited) {
                __android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step decoder thread quit , sourdoen set to 1\n");
                s->source_done = true;
                break;
            }
            pthread_mutex_lock(&s->in_mutex);
            if (s->in_queue->size() >= 10) {
                pthread_mutex_unlock(&s->in_mutex);
                usleep(10000);
                //__android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step 10 frames in queue, wait decode\n");
                continue;
            }
            s->in_queue->push_back(frame);
            pthread_cond_signal(&s->condition);
            pthread_mutex_unlock(&s->in_mutex);
            //__android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step push one frame to in queue ok\n");
            break;
        }
    }
OUT:
    if (s->out_queue->empty())
    {
        //__android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step have no frame out\n");
        return 0;
    }

    frame = *s->out_queue->begin();
    s->out_queue->erase(s->out_queue->begin());
    pthread_mutex_unlock(&s->out_mutex);

    
    ret_frame = frame->vframe;
    status  = frame->status;
    av_freep(&frame);

    dt_av_frame_t *frame_ret = (dt_av_frame_t *)malloc(sizeof(dt_av_frame_t));
    //*got_frame = 1;
    memcpy(frame_ret,ret_frame,sizeof(dt_av_frame_t));
    *data = frame_ret;
#if 0
    __android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step decode one frame ok, %p %p %d %p \n", *data, frame_ret, sizeof(dt_av_frame_t), decoder);
    __android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step decode one frame ok, pts:%llx dts:%llx \n", (*data)->pts, (*data)->dts);
    __android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step decode one frame ok, pts:%llx dts:%llx \n", ret_frame->pts, ret_frame->dts);
    __android_log_print(ANDROID_LOG_DEBUG,TAG, "-------------step decode one frame ok, width:%d height:%d duration:%d  \n", frame_ret->width, frame_ret->height, frame_ret->duration);
#endif
    return 1;
    //return orig_size;
}

static int Stagefright_info_changed(dtvideo_decoder_t *decoder)
{
    StagefrightContext *s = (StagefrightContext*)decoder->vd_priv;
    if(s->info_changed) 
    {
        s->info_changed = 0;
        return 1;
    }
    return 0;
}
static int Stagefright_close(dtvideo_decoder_t *decoder)
{
    StagefrightContext *s = (StagefrightContext*)decoder->vd_priv;
    Frame *frame;

    if (s->thread_started) {
        if (!s->thread_exited) {
            s->stop_decode = 1;

            // Make sure decode_thread() doesn't get stuck
            pthread_mutex_lock(&s->out_mutex);
            while (!s->out_queue->empty()) {
                frame = *s->out_queue->begin();
                s->out_queue->erase(s->out_queue->begin());
                if (frame->vframe)
                {
                    dtav_free_frame(frame->vframe);
                }
                av_freep(&frame);
            }
            pthread_mutex_unlock(&s->out_mutex);

            // Feed a dummy frame prior to signalling EOF.
            // This is required to terminate the decoder(OMX.SEC)
            // when only one frame is read during stream info detection.
            if (s->dummy_buf && (frame = (Frame*)av_mallocz(sizeof(Frame)))) {
                frame->status = OK;
                frame->size   = s->dummy_bufsize;
                frame->key    = 1;
                frame->buffer = s->dummy_buf;
                pthread_mutex_lock(&s->in_mutex);
                s->in_queue->push_back(frame);
                pthread_cond_signal(&s->condition);
                pthread_mutex_unlock(&s->in_mutex);
                s->dummy_buf = NULL;
            }

            pthread_mutex_lock(&s->in_mutex);
            s->end_frame->status = ERROR_END_OF_STREAM;
            s->in_queue->push_back(s->end_frame);
            pthread_cond_signal(&s->condition);
            pthread_mutex_unlock(&s->in_mutex);
            s->end_frame = NULL;
        }

        pthread_join(s->decode_thread_id, NULL);

        if (s->prev_frame)
            av_frame_free(&s->prev_frame);

        s->thread_started = false;
    }

#if 0
    while (!s->in_queue->empty()) {
        frame = *s->in_queue->begin();
        s->in_queue->erase(s->in_queue->begin());
        if (frame->size)
            av_freep(&frame->buffer);
        av_freep(&frame);
    }

    while (!s->out_queue->empty()) {
        frame = *s->out_queue->begin();
        s->out_queue->erase(s->out_queue->begin());
        if (frame->vframe)
            dtav_free_frame(frame->vframe);
        av_freep(&frame);
    }
#endif
    (*s->decoder)->stop();
    s->client->disconnect();

    if (s->decoder_component)
        av_freep(&s->decoder_component);
    if(s->dummy_buf)
    av_freep(&s->dummy_buf);
    if(s->end_frame)
        av_freep(&s->end_frame);

    // Reset the extradata back to the original mp4 format, so that
    // the next invocation (both when decoding and when called from
    // av_find_stream_info) get the original mp4 format extradata.
    //av_freep(&avctx->extradata);
    //avctx->extradata = s->orig_extradata;
    //avctx->extradata_size = s->orig_extradata_size;

    delete s->in_queue;
    delete s->out_queue;
    delete s->ts_map;
    delete s->client;
    delete s->decoder;
    delete s->source;

    pthread_mutex_destroy(&s->in_mutex);
    pthread_mutex_destroy(&s->out_mutex);
    pthread_cond_destroy(&s->condition);
    return 0;
}

const char *vd_stagefright_name = "OMX HW DECODER";

void vd_stagefright_setup(vd_wrapper_t *vd)
{
    vd->name = vd_stagefright_name;
    vd->vfmt = DT_VIDEO_FORMAT_H264;
    vd->type = DT_TYPE_VIDEO;
    vd->is_hw = 1;
    vd->init = Stagefright_init;
    vd->decode_frame = Stagefright_decode_frame;
    vd->info_changed = Stagefright_info_changed;
    vd->release = Stagefright_close;
    __android_log_print(ANDROID_LOG_DEBUG,TAG, "vd stagefright setup ok \n");
    return;
}

}// end extern "C"

}// end namespace
