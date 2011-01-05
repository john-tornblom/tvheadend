/*
 *  OpenTV EPG parsing for radio channels
 *  Copyright (C) 2010 sb1066
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

#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include "tvheadend.h"
#include "channels.h"
#include "epg.h"
#include "otv.h"

#define OTV_DESC_SONG_ID      0
#define OTV_DESC_STATION_NAME 1
#define OTV_DESC_SONG_LENGTH  2
#define OTV_DESC_UNKNOWN1     3
#define OTV_DESC_UNKNOWN2     4
#define OTV_DESC_SONG_NAME    5
#define OTV_DESC_SONG_ARTIST  6
#define OTV_DESC_UNKNOWN3     7
#define OTV_DESC_ALBUM_NAME   8
#define OTV_DESC_ALBUM_LABEL  9
#define OTV_DESC_ALBUM_YEAR   10
#define OTV_DESC_UNKNOWN4     11

LIST_HEAD(otv_desc_list, otv_desc);
static struct otv_desc_list otv_desclist;

typedef struct otv_desc {
  LIST_ENTRY(otv_desc) otv_link;
  channel_t *otv_ch;
  int otv_state;
  time_t otv_created;
  int otv_event_id;
   
  int otv_duration;
  char* otv_station;
  char* otv_artist;
  char* otv_song;
  char* otv_album;
  char* otv_label;
  int otv_year;
} otv_desc_t;

static pthread_mutex_t otv_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  otv_cond;

/**
 *
 */
static void
otv_desc_destroy(otv_desc_t *d)
{
  if(!d)
    return;

  if(d->otv_station)
    free(d->otv_station);

  if(d->otv_artist)
    free(d->otv_artist);
  
  if(d->otv_song)
    free(d->otv_song);
  
  if(d->otv_album)
    free(d->otv_album);
  
  if(d->otv_label)
    free(d->otv_label);
  
  free(d);
}

/**
 *
 */
static otv_desc_t
*otv_desc_parse(int event_id, const uint8_t *data, int len)
{
  otv_desc_t *d, *tmp;
  char *ptr, *ptr_end;
  char *checksum;
  char buf[len+1];
  int i;

  lock_assert(&otv_mutex);
  
  if(!index((const char*)data, '|'))
    return NULL;

  LIST_FOREACH(d, &otv_desclist, otv_link)
     if(d->otv_event_id == event_id)
        return NULL;
  
  d = calloc(1, sizeof(otv_desc_t));
  memset(d, 0, sizeof(otv_desc_t));
  memcpy(buf, data, len);
  buf[len] = '\0';

  d->otv_event_id = event_id;
  d->otv_created = time(NULL);
  
  i = 0;
  ptr = buf;
  while (ptr < buf+len) {
    ptr_end = index(ptr, '|');
    if(ptr_end)
      *ptr_end = '\0';
    
    switch (i) {
    case OTV_DESC_STATION_NAME:
       d->otv_station = strdup(ptr);
      break;
    case OTV_DESC_SONG_ID:
      //NOP, use event_id instead
      break;
    case OTV_DESC_SONG_LENGTH:
      d->otv_duration = atoi(ptr);
      break;
    case OTV_DESC_SONG_NAME:
       d->otv_song = strdup(ptr);
      break;
    case OTV_DESC_SONG_ARTIST:
       d->otv_artist = strdup(ptr);
      break;
    case OTV_DESC_ALBUM_NAME:
       d->otv_album = strdup(ptr);
      break;
    case OTV_DESC_ALBUM_LABEL:
       d->otv_label = strdup(ptr);
      break;
    case OTV_DESC_ALBUM_YEAR:
       d->otv_year = atoi(ptr);
      break;
    case OTV_DESC_UNKNOWN4:
      checksum = ptr;
      break;
    default:
      break;
    }
    ptr += strlen(ptr)+1;
    i++;
  }

  //See if we have a previus event for this channel
  LIST_FOREACH(tmp, &otv_desclist, otv_link)
     if(tmp->otv_ch && !strcmp(d->otv_station, tmp->otv_ch->ch_name))
        d->otv_state = 1;
  
  LIST_INSERT_HEAD(&otv_desclist, d, otv_link);
  pthread_cond_signal(&otv_cond);
  
  return d;
}

/**
 *
 */
static void *
otv_thread(void *aux)
{
  otv_desc_t *d, *next;
  time_t now;
  event_t *ev;
  int upd = 0;
  char buf[512];
  
  pthread_mutex_lock(&global_lock);
  
  while(1) {
    pthread_cond_wait(&otv_cond, &global_lock);

    now = time(NULL);

    pthread_mutex_lock(&otv_mutex);
    
    for(d = LIST_FIRST(&otv_desclist); d != NULL; d = next) {
       next = LIST_NEXT(d, otv_link);
       
       if(d->otv_created + d->otv_duration < now) {
          LIST_REMOVE(d, otv_link);
          otv_desc_destroy(d);
          continue;
       }

       d->otv_ch = channel_find_by_name(d->otv_station, 0, 0);
       if(!d->otv_ch)
          continue;

       //0: first time this channel has seen a song. time is out of sync so don't send it
       //1: the song just started, send it as an epg
       //2: the song has been transmitted, ignore it
       if(d->otv_state == 1) {
          d->otv_state = 2;

          ev = epg_event_create(d->otv_ch,
                                now,
                                now + d->otv_duration,
                                -1,
                                NULL);

          memset(buf, 0, sizeof(buf));
          strcat(buf, d->otv_artist);
          strcat(buf, " - ");
          strcat(buf, d->otv_song);
          
          upd = epg_event_set_title(ev, buf);

          memset(buf, 0, sizeof(buf));
          strcat(buf, "Album: ");
          strcat(buf, d->otv_album);
          upd |= epg_event_set_desc(ev, buf);

          if(upd)
             epg_event_updated(ev);
       }
    }
    pthread_mutex_unlock(&otv_mutex);
  }

  return NULL;
}

/**
 * OTV data
 *
 * Originally found on greek radio channels at 16E, but also present
 * on radio channels from the swedish cable provider Comhem.
 */
void
otv_input(service_t *t, elementary_stream_t *st, const uint8_t *data,
          int len, int start, int error)
{
  int desc_len, event_id;
  
  if(!start || len < 25)
    return;
  
  event_id = (data[13] << 24 ) |
    (data[14] << 16 ) |
    (data[15] << 8) |
    (data[16] << 0 );
  
  desc_len = data[24];
  
  if(len < desc_len+24)
    return;

  pthread_mutex_lock(&otv_mutex);
  otv_desc_parse(event_id, data+25, desc_len);
  pthread_mutex_unlock(&otv_mutex);
}

/**
 *
 */
void
otv_init(void)
{
  pthread_t ptid;
  pthread_attr_t attr;

  pthread_cond_init(&otv_cond, NULL);
  LIST_INIT(&otv_desclist);
  
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
 
  pthread_create(&ptid, &attr, otv_thread, NULL);
}
