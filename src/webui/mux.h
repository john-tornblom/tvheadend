/*
 *  tvheadend, muxing of packets with libavformat
 *  Copyright (C) 2012 John Törnblom
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
 *  along with this program.  If not, see <htmlui://www.gnu.org/licenses/>.
 */


#ifndef MUX_H_
#define MUX_H_

#include <libavformat/avformat.h>
#include <libavformat/avio.h>

#include "tvheadend.h"
#include "service.h"
#include "channels.h"
#include "epg.h"

typedef enum {
  MC_UNKNOWN = 0,
  MC_MATROSKA = 1,
  MC_MPEGTS = 2,
  MC_WEBM = 3
} mux_container_type_t;

typedef struct mux {
  int fd; // File descriptor for socket
  AVFormatContext *oc; // Output format
  AVIOContext *pb; // Buffer for muxer
  int errors; //number of weite errors on socket
  mux_container_type_t mux_type;
} mux_t;

const char *mux_container_type2txt(mux_container_type_t mc);
mux_container_type_t mux_container_txt2type(const char *str);

mux_t*
mux_create(int fd,
	   const struct streaming_start *ss,
	   const channel_t *ch,
	   mux_container_type_t mc);

int mux_write_pkt(mux_t *mux, struct th_pkt *pkt);
int mux_write_meta(mux_t *mux, event_t *e);

int mux_close(mux_t *mux);
void mux_destroy(mux_t *mux);

void mux_init(void);

#endif
