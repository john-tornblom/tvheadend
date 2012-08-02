#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

#include "tvheadend.h"
#include "streaming.h"
#include "mux.h"

#define MUX_BUF_SIZE 4096

/**
 * translate a component type to a libavcodec id
 */
static enum CodecID
mux_get_codec_id(streaming_component_type_t type)
{
  enum CodecID codec_id = CODEC_ID_NONE;

  switch(type) {
  case SCT_H264:
    codec_id = CODEC_ID_H264;
    break;
  case SCT_MPEG2VIDEO:
    codec_id = CODEC_ID_MPEG2VIDEO;
    break;
  case SCT_AC3:
    codec_id = CODEC_ID_AC3;
    break;
  case SCT_EAC3:
    codec_id = CODEC_ID_EAC3;
    break;
  case SCT_AAC:
    codec_id = CODEC_ID_AAC;
    break;
  case SCT_MPEG2AUDIO:
    codec_id = CODEC_ID_MP2;
    break;
  case SCT_DVBSUB:
    codec_id = CODEC_ID_DVB_SUBTITLE;
    break;
  case SCT_TEXTSUB:
    codec_id = CODEC_ID_TEXT;
    break;
 case SCT_TELETEXT:
    codec_id = CODEC_ID_DVB_TELETEXT;
    break;
  default:
    codec_id = CODEC_ID_NONE;
    break;
  }

  return codec_id;
}


/**
 * 
 */
const char*
mux_container_type2txt(mux_container_type_t mc) {
  switch(mc) {
  case MC_MATROSKA:
    return "matroska";
  case MC_MPEGTS:
    return "mpegts";
  case MC_WEBM:
    return "webm";
  default:
    return "unknown";
  }
}


/**
 *
 */
mux_container_type_t
mux_container_txt2type(const char *str) {
  if(!str)
    return MC_UNKNOWN;
  else if(!strcmp("matroska", str))
    return MC_MATROSKA;
  else if(!strcmp("mpegts", str))
    return MC_MPEGTS;
  else if(!strcmp("webm", str))
    return MC_WEBM;
  else
    return MC_UNKNOWN;
}


/**
 * callback function for libavformat
 */
static int 
mux_write(void *opaque, uint8_t *buf, int buf_size)
{
  int r;
  mux_t *mux = (mux_t*)opaque;
  
  r = write(mux->fd, buf, buf_size);
  mux->errors += (r != buf_size);

  return r;
}


/**
 * 
 */
static int
mux_add_stream(mux_t *mux, const streaming_start_component_t *ssc)
{
  AVStream *st;
  AVCodecContext *c;

  st = avformat_new_stream(mux->oc, NULL);
  if (!st)
    return -1;

  st->id = ssc->ssc_index;
  st->time_base = AV_TIME_BASE_Q;

  c = st->codec;
  c->codec_id = mux_get_codec_id(ssc->ssc_type);

  if(ssc->ssc_gh) {
    c->extradata = pktbuf_ptr(ssc->ssc_gh);
    c->extradata_size = pktbuf_len(ssc->ssc_gh);
  }

  if(SCT_ISAUDIO(ssc->ssc_type)) {
    c->codec_type = AVMEDIA_TYPE_AUDIO;
    c->sample_fmt = AV_SAMPLE_FMT_S16;
    c->sample_rate = sri_to_rate(ssc->ssc_sri);
    c->channels = ssc->ssc_channels;
    c->time_base.num = 1;
    c->time_base.den = c->sample_rate;
  } else if(SCT_ISVIDEO(ssc->ssc_type)) {
    c->codec_type = AVMEDIA_TYPE_VIDEO;
    c->width = ssc->ssc_width;
    c->height = ssc->ssc_height;
    c->time_base.num = 1;
    c->time_base.den = 25;
    c->sample_aspect_ratio.num = ssc->ssc_aspect_num;
    c->sample_aspect_ratio.den = ssc->ssc_aspect_den;

    st->sample_aspect_ratio.num = c->sample_aspect_ratio.num;
    st->sample_aspect_ratio.den = c->sample_aspect_ratio.den;
  } else if(SCT_ISSUBTITLE(ssc->ssc_type)) {
    c->codec_type = AVMEDIA_TYPE_SUBTITLE;
  }

  if(mux->oc->oformat->flags & AVFMT_GLOBALHEADER)
    c->flags |= CODEC_FLAG_GLOBAL_HEADER;

  return 0;
}


/**
 * 
 */
static int
mux_support_stream(mux_container_type_t mc, 
		   streaming_component_type_t type)
{
  int ret = 0;

  switch(mc) {
  case MC_MATROSKA:
    ret |= SCT_ISAUDIO(type);
    ret |= SCT_ISVIDEO(type);
    ret |= SCT_ISSUBTITLE(type);
    break;

  case MC_MPEGTS:
    ret |= (type == SCT_MPEG2VIDEO);
    ret |= (type == SCT_MPEG2AUDIO);
    ret |= (type == SCT_H264);
    ret |= (type == SCT_AC3);
    ret |= (type == SCT_TELETEXT);
    ret |= (type == SCT_DVBSUB);
    ret |= (type == SCT_AAC);
    ret |= (type == SCT_EAC3);
    break;

  default:
    break;
  }

  return ret;
}

/**
 * 
 */
mux_t*
mux_create(int fd, const struct streaming_start *ss,
	   const channel_t *ch, mux_container_type_t mc)
{
  int i;
  const char *mux_name;
  mux_t *mux;
  AVFormatContext *oc;
  AVIOContext *pb;
  uint8_t *buf;
  const streaming_start_component_t *ssc;

  mux_name = mux_container_type2txt(mc);

  if(avformat_alloc_output_context2(&oc, NULL, mux_name, NULL) < 0) {
    tvhlog(LOG_ERR, "mux",  "Can't allocate output context with format '%s'", mux_name);
    return NULL;
  }

  mux = calloc(1, sizeof(mux_t));
  mux->fd = fd;
  mux->oc = oc;
  mux->mux_type = mc;

  for(i=0; i < ss->ss_num_components; i++) {
    ssc = &ss->ss_components[i];

    if(!mux_support_stream(mc, ssc->ssc_type))
      continue;

    if(ssc->ssc_disabled)
      continue;

    mux_add_stream(mux, ssc);
  }

  buf = av_malloc(MUX_BUF_SIZE);
  pb = avio_alloc_context(buf, MUX_BUF_SIZE, 1, mux, NULL, mux_write, NULL);
  pb->seekable = 0;
  oc->pb = pb;
  mux->pb = pb;

  if(avformat_write_header(oc, NULL) < 0) {
    tvhlog(LOG_WARNING, "mux",  "Failed to write %s header", mux_name);
    mux->errors++;
  }

  return mux;
}


/**
 * 
 */
int
mux_write_pkt(mux_t *mux, struct th_pkt *pkt)
{
  int i;
  AVFormatContext *oc;
  AVStream *st;
  AVPacket packet;

  oc = mux->oc;

  for(i=0; i<oc->nb_streams; i++) {
    st = oc->streams[i];

    if(st->id != pkt->pkt_componentindex)
      continue;

    av_init_packet(&packet);

    if(st->codec->codec_id == CODEC_ID_MPEG2VIDEO)
      pkt = pkt_merge_header(pkt);

    packet.data = pktbuf_ptr(pkt->pkt_payload);
    packet.size = pktbuf_len(pkt->pkt_payload);
    packet.stream_index = st->index;

    if(mux->mux_type != MC_MPEGTS) {
      packet.pts = ts_rescale(pkt->pkt_pts, 1000);
      packet.dts = ts_rescale(pkt->pkt_dts, 1000);
      packet.duration = ts_rescale(pkt->pkt_duration, 1000);
    } else {
      packet.pts = pkt->pkt_pts;
      packet.dts = pkt->pkt_dts;
      packet.duration = pkt->pkt_duration;
    }

    if(pkt->pkt_frametype < PKT_P_FRAME)
      packet.flags |= AV_PKT_FLAG_KEY;

    if (av_interleaved_write_frame(oc, &packet) != 0) {
        tvhlog(LOG_WARNING, "mux",  "Failed to write frame");
	mux->errors++;
    }

    break;
  }

  return mux->errors;
}


/**
 * 
 */
int
mux_write_meta(mux_t *mux, epg_broadcast_t *e)
{
  return mux->errors;
}

/**
 * 
 */
int
mux_close(mux_t *mux)
{
  return 0;
}


/**
 * 
 */
void
mux_destroy(mux_t *mux)
{
  av_free(mux->pb);
  av_free(mux->oc);
  free(mux);
}


/**
 * 
 */ 
void
mux_init(void)
{
  av_register_all();
  av_log_set_level(AV_LOG_DEBUG);  
}

