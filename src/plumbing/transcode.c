/**
 *  Transcoding
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <sys/types.h>

#include <vlc/vlc.h>

#include <vlc/plugins/vlc_es.h>
#include <vlc/plugins/vlc_block.h>

#include "tvheadend.h"
#include "streaming.h"
#include "service.h"
#include "packet.h"
#include "transcode.h"
#include "queue.h"

#define VLC_ARG_SIZE 20
#define MAX_PASSTHROUGH_STREAMS 31

/**
 * 
 */
typedef struct transcoder_stream {
  int ts_index;
  streaming_component_type_t ts_type;

  streaming_target_t *ts_target;

  struct th_pktref_queue ts_q;
  pthread_cond_t         ts_qcond;
  pthread_mutex_t        ts_qlock; 

  libvlc_instance_t     *ts_vlc;
  libvlc_media_player_t *ts_mp;
  const es_format_t     *ts_fmt;
} transcoder_stream_t;

typedef struct transcoder_passthrough {
  int tp_sindex;
  int tp_tindex;
  streaming_target_t *tp_target;
} transcoder_passthrough_t;


/**
 * 
 */
typedef struct transcoder {
  streaming_target_t t_input;  // must be first
  streaming_target_t *t_output;

  // client preferences for the target stream
  streaming_component_type_t atype; // audio
  streaming_component_type_t vtype; // video
  streaming_component_type_t stype; // subtitle
  size_t max_height;

  // Audio & Video stream transcoders
  transcoder_stream_t *ts_audio;
  transcoder_stream_t *ts_video;

  // Passthrough streams
  int pt_streams[MAX_PASSTHROUGH_STREAMS+1];
  uint8_t pt_count;
} transcoder_t;


/**
 *
 */
static void
transcoder_pkt_deliver(streaming_target_t *st, th_pkt_t *pkt)
{
  streaming_message_t *sm;

  sm = streaming_msg_create_pkt(pkt);
  streaming_target_deliver2(st, sm);
  pkt_ref_dec(pkt);
}


/**
 * 
 */
static int 
imem_get_data(void *arg, const char *cookie, int64_t *dts,  int64_t *pts, 
	      unsigned *flags, size_t *size, void **data)
{
  th_pktref_t *pr;
  struct timespec time_spec;
  transcoder_stream_t *ts = (transcoder_stream_t *)arg;

  while(1) {
    pthread_mutex_lock(&ts->ts_qlock);
    pr = TAILQ_FIRST(&ts->ts_q);

    if(pr == NULL && ts->ts_index) {
      time_spec.tv_nsec = 0;
      time(&time_spec.tv_sec);
      time_spec.tv_sec++; 
      pthread_cond_timedwait(&ts->ts_qcond, &ts->ts_qlock, &time_spec);

      pthread_mutex_unlock(&ts->ts_qlock);
      continue;
    } else if(pr != NULL) {
      *size = pktbuf_len(pr->pr_pkt->pkt_payload);
      *data = pktbuf_ptr(pr->pr_pkt->pkt_payload);

      *pts = pr->pr_pkt->pkt_pts;
      *dts = pr->pr_pkt->pkt_dts;

      pthread_mutex_unlock(&ts->ts_qlock);
      return 0;
    } else {
      pthread_mutex_unlock(&ts->ts_qlock);
      return -1;
    }
  }
}


/**
 * 
 */
static int
imem_del_data(void *arg, const char *cookie, size_t size, void *data)
{
  th_pktref_t *pr;
  transcoder_stream_t *ts = (transcoder_stream_t *)arg;

  pthread_mutex_lock(&ts->ts_qlock);

  TAILQ_FOREACH(pr, &ts->ts_q, pr_link) {
    if(data == pktbuf_ptr(pr->pr_pkt->pkt_payload)) {
      pktref_remove(&ts->ts_q, pr);
      break;
    }
  }

  pthread_mutex_unlock(&ts->ts_qlock);

  return !pr;
}


/**
 * 
 */
static void
omem_handle_block(void *arg, int id, const block_t *block)
{
  th_pkt_t *n;
  transcoder_stream_t *ts = (transcoder_stream_t *)arg;

  pthread_mutex_lock(&ts->ts_qlock);
  
  if(ts->ts_index) {
    n = pkt_alloc(block->p_buffer, block->i_buffer, 
		  block->i_pts, block->i_dts);

    n->pkt_componentindex = ts->ts_index;
    n->pkt_duration = block->i_length;

    if(ts->ts_fmt->i_cat == AUDIO_ES) {
      n->pkt_channels = ts->ts_fmt->audio.i_channels;
      n->pkt_sri = rate_to_sri(ts->ts_fmt->audio.i_rate);
    } else if(ts->ts_fmt->i_cat == VIDEO_ES) {
      n->pkt_aspect_num = ts->ts_fmt->video.i_sar_num;
      n->pkt_aspect_den = ts->ts_fmt->video.i_sar_den;
    }

    if(block->i_flags & BLOCK_FLAG_TYPE_I)
      n->pkt_frametype = PKT_I_FRAME;
    else if(block->i_flags & BLOCK_FLAG_TYPE_P)
      n->pkt_frametype = PKT_P_FRAME;
    else if(block->i_flags & BLOCK_FLAG_TYPE_B)
      n->pkt_frametype = PKT_B_FRAME;

    if(block->i_flags & BLOCK_FLAG_INTERLACED_MASK)
      n->pkt_field = 1;

    n->pkt_commercial = 0;

    if(ts->ts_fmt->i_extra > 0)
      n->pkt_header = pktbuf_alloc(ts->ts_fmt->p_extra, ts->ts_fmt->i_extra);

    transcoder_pkt_deliver(ts->ts_target, n);
  }

  pthread_mutex_unlock(&ts->ts_qlock);
}


/**
 * 
 */
static void
omem_add_stream(void *arg, int index, const es_format_t *p_fmt)
{
  transcoder_stream_t *ts = (transcoder_stream_t *)arg;
  printf("omem_add_stream\n");

  pthread_mutex_lock(&ts->ts_qlock);
  ts->ts_fmt = p_fmt;
  pthread_mutex_unlock(&ts->ts_qlock);
}


/**
 * 
 */
static void
omem_del_stream(void *arg, int index)
{
  transcoder_stream_t *ts = (transcoder_stream_t *)arg;
  printf("omem_del_stream\n");

  pthread_mutex_lock(&ts->ts_qlock);
  ts->ts_fmt = NULL;
  pthread_mutex_unlock(&ts->ts_qlock);
}


/**
 *
 */
static void
transcoder_stream_destroy(transcoder_stream_t *ts)
{
  if(ts->ts_mp)
    libvlc_media_player_release(ts->ts_mp);

  if(ts->ts_vlc)
    libvlc_release(ts->ts_vlc);

  free(ts);
}

/**
 *
 */
static const char*
transcoder_get_codec_name(streaming_component_type_t type)
{
  switch(type) {
  case SCT_H264:
    return "h264";
  case SCT_MPEG2VIDEO:
    return "mp2v";
  case SCT_AC3:
    return "ac-3";
  case SCT_EAC3:
    return "eac3";
  case SCT_AAC:
    return "mp4a";
  case SCT_MPEG2AUDIO:
    return "mpga";
  case SCT_MPEG4VIDEO:
    return "mp4v";
  case SCT_VP8:
    return "VP80";
  case SCT_VORBIS:
    return "vorb";
  case SCT_DVBSUB:
    return "dvbs";
  case SCT_TEXTSUB:
    return "text";
  case SCT_TELETEXT:
    return "telx";
  default:
    return NULL;
  }
}

/**
 * find the codecs and allocate buffers used by a transcoder stream
 */
static transcoder_stream_t*
transcoder_stream_create(streaming_component_type_t stype, streaming_component_type_t ttype)
{
  int i;
  char* arg[VLC_ARG_SIZE];
  transcoder_stream_t *ts;
  libvlc_media_t *m;
  const char *codec;

  ts = calloc(1, sizeof(transcoder_stream_t));
  ts->ts_type = ttype;

  TAILQ_INIT(&ts->ts_q);
  pthread_cond_init(&ts->ts_qcond, NULL);
  pthread_mutex_init(&ts->ts_qlock, NULL);

  for(i=0; i<VLC_ARG_SIZE; i++){
    arg[i] = (char*)malloc(128);
  }

  codec = transcoder_get_codec_name(ttype);

  i = 0;
  sprintf(arg[i++], "-I=dummy");
  sprintf(arg[i++], "--ignore-config");
  if(SCT_ISAUDIO(stype)) {
    sprintf(arg[i++], "--sout=#transcode{acodec=%s,ab=128,channels=2}:omem", codec);
    sprintf(arg[i++], "--imem-cat=%d", 1); // Audio
  } else if(SCT_ISVIDEO(stype)) {
    sprintf(arg[i++], "--sout=#transcode{vcodec=%s,vb=800,deinterlace}:omem", codec);
    sprintf(arg[i++], "--imem-cat=%d", 2); // Video
  } else if(SCT_ISSUBTITLE(stype)) {
    sprintf(arg[i++], "--sout=#transcode{scodec=%s}:omem", codec);
    sprintf(arg[i++], "--imem-cat=%d", 3); // Subtitle
  } else {
    sprintf(arg[i++], "--sout=#transcode{dcodec=%s}:omem", codec);
    sprintf(arg[i++], "--imem-cat=%d", 4); // Data
  }

  sprintf(arg[i++], "--imem-get=%ld" , (long) imem_get_data);
  sprintf(arg[i++], "--imem-release=%ld", (long) imem_del_data);
  sprintf(arg[i++], "--imem-data=%ld", (long) ts);
  sprintf(arg[i++], "--imem-codec=%s", transcoder_get_codec_name(stype));

  sprintf(arg[i++], "--omem-handle-block=%ld", (long) omem_handle_block);
  sprintf(arg[i++], "--omem-add-stream=%ld", (long) omem_add_stream);
  sprintf(arg[i++], "--omem-del-stream=%ld", (long) omem_del_stream);
  sprintf(arg[i++], "--omem-argument=%ld", (long) ts);

  sprintf(arg[i++], "-vvv" );

  ts->ts_vlc = libvlc_new(VLC_ARG_SIZE, (const char * const*) arg);
  m = libvlc_media_new_path(ts->ts_vlc, "imem://");
  ts->ts_mp = libvlc_media_player_new_from_media(m);
  libvlc_media_release(m);
  libvlc_media_player_play(ts->ts_mp);

  return ts;
}


/**
 *
 */
static void
transcoder_stream_passthrough(streaming_target_t *st, th_pkt_t *pkt)
{
  th_pkt_t *n;

  n = pkt_alloc(pktbuf_ptr(pkt->pkt_payload),
		pktbuf_len(pkt->pkt_payload),
		pkt->pkt_pts,
		pkt->pkt_dts);

  n->pkt_duration = pkt->pkt_duration;
  n->pkt_commercial = pkt->pkt_commercial;
  n->pkt_componentindex = pkt->pkt_componentindex;
  n->pkt_frametype = pkt->pkt_frametype;
  n->pkt_field = pkt->pkt_field;
  n->pkt_channels = pkt->pkt_channels;
  n->pkt_sri = pkt->pkt_sri;
  n->pkt_aspect_num = pkt->pkt_aspect_num;
  n->pkt_aspect_den = pkt->pkt_aspect_den;

  transcoder_pkt_deliver(st, n);
}


/**
 * transcode a packet
 */
static void
transcoder_packet(transcoder_t *t, th_pkt_t *pkt)
{
  transcoder_stream_t *ts = NULL;
  int i = 0;

  if(t->ts_video && pkt->pkt_componentindex == t->ts_video->ts_index) {
    ts = t->ts_video;
  } else if (t->ts_audio && pkt->pkt_componentindex == t->ts_audio->ts_index) {
    ts = t->ts_audio;
  }

  if(ts) {
    pkt_ref_inc(pkt);
    pthread_mutex_lock(&ts->ts_qlock);
    pktref_enqueue(&ts->ts_q, pkt);
    pthread_cond_signal(&ts->ts_qcond);
    pthread_mutex_unlock(&ts->ts_qlock);
    return;
  }

  // Look for passthrough streams
  for(i=0; i<t->pt_count; i++) {
    if(t->pt_streams[i] == pkt->pkt_componentindex) {
      transcoder_stream_passthrough(t->t_output, pkt);
      return;
    }
  }
}

#define IS_PASSTHROUGH(t, ssc) ((SCT_ISAUDIO(ssc->ssc_type) && t->atype == SCT_UNKNOWN) || \
				(SCT_ISVIDEO(ssc->ssc_type) && t->vtype == SCT_UNKNOWN) || \
				(SCT_ISSUBTITLE(ssc->ssc_type) && t->stype == SCT_UNKNOWN))


/**
 * Figure out how many streams we will use.
 */
static int
transcoder_get_stream_count(transcoder_t *t, streaming_start_t *ss) {
  int i = 0;
  int video = 0;
  int audio = 0;
  int passthrough = 0;
  streaming_start_component_t *ssc = NULL;

  for(i = 0; i < ss->ss_num_components; i++) {
    ssc = &ss->ss_components[i];

    if(IS_PASSTHROUGH(t, ssc))
      passthrough++;
    else if(SCT_ISVIDEO(ssc->ssc_type))
      video = 1;
    else if(SCT_ISAUDIO(ssc->ssc_type))
      audio = 1;
  }

  passthrough = MIN(passthrough, MAX_PASSTHROUGH_STREAMS);

  return (video + audio + passthrough);
}



/**
 * initializes eatch transcoding stream
 */
static streaming_start_t *
transcoder_start(transcoder_t *t, streaming_start_t *src)
{
  int i = 0;
  int j = 0;
  int stream_count = transcoder_get_stream_count(t, src);
  streaming_start_t *ss = NULL;

  ss = malloc(sizeof(streaming_start_t) + sizeof(streaming_start_component_t) * stream_count);
  memset(ss, 0, sizeof(streaming_start_t) + sizeof(streaming_start_component_t) * stream_count);

  ss->ss_num_components = stream_count;
  ss->ss_refcount = 1;
  ss->ss_pcr_pid = src->ss_pcr_pid;
  service_source_info_copy(&ss->ss_si, &src->ss_si);

  for(i = 0; i < src->ss_num_components; i++) {
    streaming_start_component_t *ssc_src = &src->ss_components[i];

    // Sanity check, should not happend
    if(j >= stream_count) {
      break;
    }

    streaming_start_component_t *ssc = &ss->ss_components[j];

    if (IS_PASSTHROUGH(t, ssc_src)) {
      int pt_index = t->pt_count++;

      // Use same index for both source and target, globalheaders seems to need it.
      t->pt_streams[pt_index] = ssc_src->ssc_index;

      ssc->ssc_index          = ssc_src->ssc_index;
      ssc->ssc_type           = ssc_src->ssc_type;
      ssc->ssc_composition_id = ssc_src->ssc_composition_id;
      ssc->ssc_ancillary_id   = ssc_src->ssc_ancillary_id;
      ssc->ssc_pid            = ssc_src->ssc_pid;
      ssc->ssc_width          = ssc_src->ssc_width;
      ssc->ssc_height         = ssc_src->ssc_height;
      ssc->ssc_aspect_num     = ssc_src->ssc_aspect_num;
      ssc->ssc_aspect_den     = ssc_src->ssc_aspect_den;
      ssc->ssc_sri            = ssc_src->ssc_sri;
      ssc->ssc_channels       = ssc_src->ssc_channels;
      ssc->ssc_disabled       = ssc_src->ssc_disabled;

      memcpy(ssc->ssc_lang, ssc_src->ssc_lang, 4);
      j++;

      tvhlog(LOG_INFO, "transcode", "%d:%s ==> PASSTHROUGH", 
	     ssc_src->ssc_index,
	     streaming_component_type2txt(ssc_src->ssc_type));

    } else if (!t->ts_audio && SCT_ISAUDIO(ssc_src->ssc_type) && SCT_ISAUDIO(t->atype)) {
      transcoder_stream_t *ts = transcoder_stream_create(ssc_src->ssc_type, t->atype);
      if(!ts)
	continue;

      ts->ts_target = t->t_output;
      ts->ts_index = ssc_src->ssc_index;

      ssc->ssc_index    = ts->ts_index;
      ssc->ssc_type     = ts->ts_type;
      ssc->ssc_sri      = ssc_src->ssc_sri;
      ssc->ssc_channels = MIN(2, ssc_src->ssc_channels);
      memcpy(ssc->ssc_lang, ssc_src->ssc_lang, 4);

      tvhlog(LOG_INFO, "transcode", "%d:%s ==> %s", 
	     ssc_src->ssc_index,
	     streaming_component_type2txt(ssc_src->ssc_type),
	     streaming_component_type2txt(ts->ts_type));

      j++;
      t->ts_audio = ts;

    } else if (!t->ts_video && SCT_ISVIDEO(ssc_src->ssc_type) && SCT_ISVIDEO(t->vtype)) {
      transcoder_stream_t *ts = transcoder_stream_create(ssc_src->ssc_type, t->vtype);
      if(!ts)
	continue;

      ts->ts_target = t->t_output;
      ts->ts_index = ssc_src->ssc_index;

      ssc->ssc_index         = ts->ts_index;
      ssc->ssc_type          = ts->ts_type;
      ssc->ssc_aspect_num    = ssc_src->ssc_aspect_num;
      ssc->ssc_aspect_den    = ssc_src->ssc_aspect_den;
      ssc->ssc_frameduration = ssc_src->ssc_frameduration;
      ssc->ssc_height        = MIN(t->max_height, ssc_src->ssc_height);
      if(ssc->ssc_height&1) // Must be even
	ssc->ssc_height++;

      ssc->ssc_width         = ssc->ssc_height * ((double)ssc_src->ssc_width / ssc_src->ssc_height);
      if(ssc->ssc_width&1) // Must be even
	ssc->ssc_width++;

      tvhlog(LOG_INFO, "transcode", "%d:%s %dx%d ==> %d:%s %dx%d", 
	     ssc_src->ssc_index,
	     streaming_component_type2txt(ssc_src->ssc_type),
	     ssc_src->ssc_width,
	     ssc_src->ssc_height,
	     ts->ts_index,
	     streaming_component_type2txt(ts->ts_type),
	     ssc->ssc_width,
	     ssc->ssc_height);
      j++;
      t->ts_video = ts;
    }
  }

  return ss;
}


/**
 * closes the codecs and resets the transcoding streams
 */
static void
transcoder_stop(transcoder_t *t)
{
  if(t->ts_audio) {
    pthread_mutex_lock(&t->ts_audio->ts_qlock);
    t->ts_audio->ts_index = 0;
    pthread_cond_signal(&t->ts_audio->ts_qcond);
    pthread_mutex_unlock(&t->ts_audio->ts_qlock);

    libvlc_media_player_stop(t->ts_audio->ts_mp);
    transcoder_stream_destroy(t->ts_audio);
  }

  if(t->ts_video) {
    pthread_mutex_lock(&t->ts_video->ts_qlock);
    t->ts_video->ts_index = 0;
    pthread_cond_signal(&t->ts_video->ts_qcond);
    pthread_mutex_unlock(&t->ts_video->ts_qlock);

    libvlc_media_player_stop(t->ts_video->ts_mp);
    transcoder_stream_destroy(t->ts_video);
  }

  
  

  
}


/**
 * handle a streaming message
 */
static void
transcoder_input(void *opaque, streaming_message_t *sm)
{
  transcoder_t *t;
  streaming_start_t *ss;
  th_pkt_t *pkt;

  t = opaque;

  switch(sm->sm_type) {
  case SMT_PACKET:
    pkt = pkt_merge_header(sm->sm_data);
    transcoder_packet(t, pkt);
    pkt_ref_dec(pkt);
    break;

  case SMT_START:
    ss = transcoder_start(t, sm->sm_data);
    streaming_start_unref(sm->sm_data);
    sm->sm_data = ss;

    streaming_target_deliver2(t->t_output, sm);
    break;

  case SMT_STOP:
    transcoder_stop(t);
    // Fallthrough

  case SMT_EXIT:
  case SMT_SERVICE_STATUS:
  case SMT_SIGNAL_STATUS:
  case SMT_NOSTART:
  case SMT_MPEGTS:
  case SMT_SPEED:
  case SMT_SKIP:
  case SMT_TIMESHIFT_STATUS:
    streaming_target_deliver2(t->t_output, sm);
    break;
  }
}


/**
 *
 */
streaming_target_t *
transcoder_create(streaming_target_t *output, 
		  size_t max_resolution,
		  streaming_component_type_t vtype, 
		  streaming_component_type_t atype,
		  streaming_component_type_t stype
		  )
{
  transcoder_t *t = calloc(1, sizeof(transcoder_t));

  memset(t, 0, sizeof(transcoder_t));
  t->t_output = output;
  t->max_height = max_resolution;
  t->atype = atype;
  t->vtype = vtype;
  t->stype = stype;

  streaming_target_init(&t->t_input, transcoder_input, t, 0);
  return &t->t_input;
}


/**
 * 
 */
void
transcoder_set_network_speed(streaming_target_t *st, int speed)
{

}


/**
 * 
 */
void
transcoder_destroy(streaming_target_t *st)
{
  transcoder_t *t = (transcoder_t *)st;

  transcoder_stop(t);
  free(t);
}


int
transcoder_get_encoders(htsmsg_t *array)
{
  return 0;
}
