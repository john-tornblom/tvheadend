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
#include <vlc/vlc.h>
#include <vlc/plugins/vlc_es.h>

#include "tvheadend.h"
#include "streaming.h"
#include "service.h"
#include "packet.h"
#include "transcode.h"

#define MAX_PASSTHROUGH_STREAMS 31

/**
 * Reference to a transcoder stream
 */
typedef struct transcoder_stream {
  streaming_component_type_t stype; // source
  streaming_component_type_t ttype; // target

  int sindex; // refers to the source stream index
  int tindex; // refers to the target stream index

  streaming_target_t *target;

  libvlc_instance_t *vlc_inst;
  libvlc_media_player_t *vlc_mp;
} transcoder_stream_t;

typedef struct transcoder_passthrough {
  int sindex; // refers to the source stream index
  int tindex; // refers to the target stream index
  streaming_target_t *target;
} transcoder_passthrough_t;

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
  transcoder_passthrough_t pt_streams[MAX_PASSTHROUGH_STREAMS+1];
  uint8_t pt_count;
} transcoder_t;


static int 
imem_get_data(void *arg, const char *cookie, int64_t *dts,  int64_t *pts, 
	      unsigned *flags, size_t *size, void **data)
{
  //transcoder_stream_t *ts = (transcoder_stream_t *)arg;

  *size = 576;
  *data = calloc(1, *size);

  *pts = 0;
  *dts = 0;

  return 0;
}

static int
imem_del_data(void *arg, const char *cookie, size_t size, void *data)
{
  //transcoder_stream_t *ts = (transcoder_stream_t *)arg;
  
  free(data);

  return 0;
}

static void
omem_handle_packet(int index, const uint8_t *data, size_t length, 
			int64_t dts, int64_t pts)
{
  printf("handle packet\n");
}

static void
omem_add_stream(int index, const es_format_t *p_fmt)
{
  printf("add stream\n");
}

static void
omem_del_stream(int index)
{
  printf("delete stream\n");
}


/**
 *
 */
static void
transcoder_stream_destroy(transcoder_stream_t *ts)
{
  if(ts->vlc_mp)
    libvlc_media_player_release(ts->vlc_mp);

  if(ts->vlc_inst)
    libvlc_release(ts->vlc_inst);

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
    return "a52";
  case SCT_EAC3:
    return "a52";
  case SCT_AAC:
    return "mp4a";
  case SCT_MPEG2AUDIO:
    return "mpga";
  case SCT_MPEG4VIDEO:
    return "mp4v";
  case SCT_VP8:
    return "VP8";
  case SCT_VORBIS:
    return "vorb";
  case SCT_DVBSUB:
    return "dvbsub";
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
#define ARG_SIZE 20
  char* arg[ARG_SIZE];
  transcoder_stream_t *ts = NULL;
  libvlc_media_t *m;
  const char *codec;

  for(i=0; i<ARG_SIZE; i++){
    arg[i] = (char*) malloc(128);
  }

  ts = calloc(1, sizeof(transcoder_stream_t));
  ts->stype = stype;
  ts->ttype = ttype;

  codec = transcoder_get_codec_name(ttype);

  i = 0;
  sprintf(arg[i++], "-I=dummy");
  sprintf(arg[i++], "--ignore-config");
  if(SCT_ISAUDIO(stype)) {
    sprintf(arg[i++], "--sout=#transcode{acodec=%s}:omem", codec);
    sprintf(arg[i++], "--imem-cat=%d", 1); // Audio
  } else if(SCT_ISVIDEO(stype)) {
    sprintf(arg[i++], "--sout=#transcode{vcodec=%s}:omem", codec);
    sprintf(arg[i++], "--imem-cat=%d", 2); // Video
  } else if(SCT_ISSUBTITLE(stype)) {
    sprintf(arg[i++], "--sout=#transcode{scodec=%s}:omem", codec);
    sprintf(arg[i++], "--imem-cat=%d", 3); // Subtitle
  } else {
    sprintf(arg[i++], "--sout=#transcode{dcodec=%s}:omem", codec);
    sprintf(arg[i++], "--imem-cat=%d", 4); // Data
  }

  codec = transcoder_get_codec_name(stype);
  sprintf(arg[i++], "--imem-get=%ld" , (long) imem_get_data);
  sprintf(arg[i++], "--imem-release=%ld", (long) imem_del_data);
  sprintf(arg[i++], "--imem-data=%ld", (long) ts);
  sprintf(arg[i++], "--imem-codec=%s", codec);

  sprintf(arg[i++], "--omem-handle-packet=%ld" , (long) omem_handle_packet);
  sprintf(arg[i++], "--omem-add-stream=%ld", (long) omem_add_stream);
  sprintf(arg[i++], "--omem-del-stream=%ld", (long) omem_del_stream);

  sprintf(arg[i++], "-vvv" );

  ts->vlc_inst = libvlc_new(ARG_SIZE, (const char * const*) arg);
  m = libvlc_media_new_path(ts->vlc_inst, "imem://");
  ts->vlc_mp = libvlc_media_player_new_from_media(m);
  libvlc_media_release(m);

  return ts;
}


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
static void
transcoder_stream_passthrough(transcoder_passthrough_t *pt, th_pkt_t *pkt)
{
  th_pkt_t *n;

  n = pkt_alloc(pktbuf_ptr(pkt->pkt_payload), 
		pktbuf_len(pkt->pkt_payload), 
		pkt->pkt_pts, 
		pkt->pkt_dts);

  n->pkt_duration = pkt->pkt_duration;
  n->pkt_commercial = pkt->pkt_commercial;
  n->pkt_componentindex = pt->tindex;
  n->pkt_frametype = pkt->pkt_frametype;
  n->pkt_field = pkt->pkt_field;
  n->pkt_channels = pkt->pkt_channels;
  n->pkt_sri = pkt->pkt_sri;
  n->pkt_aspect_num = pkt->pkt_aspect_num;
  n->pkt_aspect_den = pkt->pkt_aspect_den;

  transcoder_pkt_deliver(pt->target, n);
}



/**
 * transcode an audio stream
 */
static void
transcoder_stream_convert(transcoder_stream_t *ts, th_pkt_t *pkt)
{
}


/**
 * transcode a packet
 */
static void
transcoder_packet(transcoder_t *t, th_pkt_t *pkt)
{
  transcoder_stream_t *ts = NULL;
  int i = 0;

  if(t->ts_video && pkt->pkt_componentindex == t->ts_video->sindex) {
    ts = t->ts_video;
  } else if (t->ts_audio && pkt->pkt_componentindex == t->ts_audio->sindex) {
    ts = t->ts_audio;
  }

  if(ts) {
    transcoder_stream_convert(ts, pkt);
    return;
  }

  // Look for passthrough streams
  for(i=0; i<t->pt_count; i++) {
    if(t->pt_streams[i].sindex == pkt->pkt_componentindex) {
      transcoder_stream_passthrough(&t->pt_streams[i], pkt);
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

      t->pt_streams[pt_index].target = t->t_output;

      // Use same index for both source and target, globalheaders seems to need it.
      t->pt_streams[pt_index].sindex = ssc_src->ssc_index;
      t->pt_streams[pt_index].tindex = ssc_src->ssc_index;

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

      tvhlog(LOG_INFO, "transcode", "%d:%s ==> %d:PASSTHROUGH", 
	     t->pt_streams[pt_index].sindex,
	     streaming_component_type2txt(ssc_src->ssc_type),
	     t->pt_streams[pt_index].tindex);

    } else if (!t->ts_audio && SCT_ISAUDIO(ssc_src->ssc_type) && SCT_ISAUDIO(t->atype)) {
      transcoder_stream_t *ts = transcoder_stream_create(ssc_src->ssc_type, t->atype);
      if(!ts)
	continue;

      ts->target = t->t_output;

      // Use same index for both source and target, globalheaders seems to need it.
      ts->sindex = ssc_src->ssc_index;
      ts->tindex = ssc_src->ssc_index;

      ssc->ssc_index    = ts->tindex;
      ssc->ssc_type     = ts->ttype;
      ssc->ssc_sri      = ssc_src->ssc_sri;
      ssc->ssc_channels = MIN(2, ssc_src->ssc_channels);
      memcpy(ssc->ssc_lang, ssc_src->ssc_lang, 4);

      tvhlog(LOG_INFO, "transcode", "%d:%s ==> %d:%s", 
	     ts->sindex,
	     streaming_component_type2txt(ts->stype),
	     ts->tindex,
	     streaming_component_type2txt(ts->ttype));

      j++;
      t->ts_audio = ts;

    } else if (!t->ts_video && SCT_ISVIDEO(ssc_src->ssc_type) && SCT_ISVIDEO(t->vtype)) {
      transcoder_stream_t *ts = transcoder_stream_create(ssc_src->ssc_type, t->vtype);
      if(!ts)
	continue;

      ts->target = t->t_output;

      // Use same index for both source and target, globalheaders seems to need it.
      ts->sindex = ssc_src->ssc_index;
      ts->tindex = ssc_src->ssc_index;

      ssc->ssc_index         = ts->tindex;
      ssc->ssc_type          = ts->ttype;
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
	     ts->sindex,
	     streaming_component_type2txt(ts->stype),
	     ssc_src->ssc_width,
	     ssc_src->ssc_height,
	     ts->tindex,
	     streaming_component_type2txt(ts->ttype),
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
    transcoder_stream_destroy(t->ts_audio);
    t->ts_audio = NULL;
  }

  if(t->ts_video) {
    transcoder_stream_destroy(t->ts_video);
    t->ts_video = NULL;
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
