/*
 *  Electronic Program Guide - External Scraping Interface
 *  Copyright (C) 2014 John Törnblom
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

#include <pthread.h>

#include "tvheadend.h"
#include "epgscrape.h"
#include "htsmsg.h"
#include "htsmsg_json.h"
#include "channels.h"
#include "spawn.h"
#include "notify.h"
#include "prop.h"
#include "settings.h"


/*************************************
 * Datatypes
 ************************************/
TAILQ_HEAD(scrape_io_queue, scrape_io);

typedef struct scrape_io {
  TAILQ_ENTRY(scrape_io) sio_link;
  htsmsg_t *sio_input;
  htsmsg_t *sio_output;
  time_t    sio_created;
  uint32_t  sio_epg_id;
} scrape_io_t;

typedef struct scrape_config {
  uint32_t sc_enabled;
  char*    sc_exec;
} scrape_config_t;


/************************************
 * Static variables
 ************************************/
static struct scrape_io_queue scrape_queue;
static        pthread_mutex_t scrape_mutex;
static        pthread_cond_t  scrape_cond;
static        pthread_t       scrape_thread;
static        scrape_config_t scrape_config;


/************************************
 * Configuration options
 ************************************/
static const property_t  scrape_props[] = {
  {
    .type   = PT_BOOL,
    .id     = "enabled",
    .name   = "Enabled",
    .off    = offsetof(scrape_config_t, sc_enabled),
  },
  {
    .type   = PT_STR,
    .id     = "exec",
    .name   = "Path to external executable",
    .off    = offsetof(scrape_config_t, sc_exec),
  },
  {}
};


/*
 * Enqueue a broadcast object for scraping
 */
void
epgscrape_enqueue_broadcast(epg_broadcast_t *ebc) {
  scrape_io_t *sio;
  epg_episode_t *ee;
  epg_genre_t *eg;
  epg_episode_num_t epnum;
  const char *lang;
  const char *str;

  lock_assert(&global_lock);

  //Check if we are allready scraping the event, or 
  //if we allready have finished scraping it.
  if(!scrape_config.sc_enabled || ebc->_scraping || ebc->scraped)
    return;

  lang = NULL;
  ee = ebc->episode;
  eg = ee ? LIST_FIRST(&ee->genre) : NULL;
  memset(&epnum, 0, sizeof(epnum));

  sio = calloc(1, sizeof(scrape_io_t));
  sio->sio_input   = htsmsg_create_map();
  sio->sio_created = dispatch_clock;
  sio->sio_epg_id  = ebc->id;

  /* Timestamps */
  htsmsg_add_s64(sio->sio_input, "start",   ebc->start);
  htsmsg_add_s64(sio->sio_input, "stop",    ebc->stop);
  htsmsg_add_s64(sio->sio_input, "scraped", ebc->scraped);
  htsmsg_add_s64(sio->sio_input, "updated", ebc->updated);

  /* Source info */
  if ((str = channel_get_name(ebc->channel)))
    htsmsg_add_str(sio->sio_input, "channel_name", str);

  /* Meta data */
  if ((str = epg_broadcast_get_title(ebc, lang)))
    htsmsg_add_str(sio->sio_input, "title", str);
  if ((str = epg_broadcast_get_description(ebc, lang)))
    htsmsg_add_str(sio->sio_input, "description", str);
  if ((str = epg_broadcast_get_summary(ebc, lang)))
    htsmsg_add_str(sio->sio_input, "summary", str);
  if (eg)
    htsmsg_add_u32(sio->sio_input, "content_type", eg->code);

  pthread_mutex_lock(&scrape_mutex);
  TAILQ_INSERT_TAIL(&scrape_queue, sio, sio_link);
  pthread_cond_signal(&scrape_cond);
  pthread_mutex_unlock(&scrape_mutex);
}


/*
 * Process stdout from the external scraper
 */
static int
epgscrape_process_result(scrape_io_t *sio)
{
  int update;
  epg_broadcast_t *ebc;
  epg_brand_t *eb;
  epg_season_t *es;
  epg_episode_t *ee;
  epg_episode_num_t en;
  htsmsg_t *sub;
  const char *lang;
  const char *str;
  uint32_t u32;
  int64_t s64;

  if(!sio->sio_output)
    return 0;

  scopedgloballock();

  htsmsg_print(sio->sio_output);

  if(!(ebc = epg_broadcast_find_by_id(sio->sio_epg_id, NULL)))
    return 0;

  ebc->scraped   = dispatch_clock;
  ebc->_scraping = 0;

  ee = ebc->episode;
  eb = ee->brand;
  es = ee->season;

  if(!ee)
    return 0;

  update = 0;
  lang = htsmsg_get_str(sio->sio_output, "language");

  memset(&en, 0, sizeof(en));
  epg_episode_get_epnum(ee, &en);

  if ((sub = htsmsg_get_map(sio->sio_output, "brand"))) {
    if((str = htsmsg_get_str(sub, "title"))) {
      if(eb) update |= epg_brand_set_title(eb, str, lang, NULL);
      update |= epg_episode_set_title(ee, str, lang, NULL);
    }

    if((str = htsmsg_get_str(sub, "summary")) && eb)
      update |= epg_brand_set_summary(eb, str, lang, NULL);

    if((u32 = htsmsg_get_u32_or_default(sub, "season_count", 0))) {
      if(eb) update |= epg_brand_set_season_count(eb, u32, NULL);
      en.s_cnt = u32;
    }

    if((str = htsmsg_get_str(sub, "image")) && eb)
      update |= epg_brand_set_image(eb, str, NULL);
  }

  if((sub = htsmsg_get_map(sio->sio_output, "season"))) {
    
    if((str = htsmsg_get_str(sub, "summary")) && es)
      update |= epg_season_set_summary(es, str, lang, NULL);
    
    if((u32 = htsmsg_get_u32_or_default(sub, "season_number", 0))) {
      if(es) update |= epg_season_set_number(es, u32, NULL);
      en.s_num = u32;
    }

    if((u32 = htsmsg_get_u32_or_default(sub, "episode_count", 0))) {
      if(es) update |= epg_season_set_episode_count(es, u32, NULL);
      en.e_cnt = u32;
    }

    if((str = htsmsg_get_str(sub, "image")) && es)
      update |= epg_season_set_image(es, str, NULL);
  }

  if((sub = htsmsg_get_map(sio->sio_output, "episode"))) {

    if((str = htsmsg_get_str(sub, "title")))
      update |= epg_episode_set_title(ee, str, lang, NULL);

    if((str = htsmsg_get_str(sub, "subtitle")))
      update |= epg_episode_set_subtitle(ee, str, lang, NULL);

    if((str = htsmsg_get_str(sub, "summary")))
      update |= epg_episode_set_summary(ee, str, lang, NULL);

    if((str = htsmsg_get_str(sub, "description")))
      update |= epg_episode_set_description(ee, str, lang, NULL);

    if((str = htsmsg_get_str(sub, "image")))
      update |= epg_episode_set_image(ee, str, NULL);

    if((u32 = htsmsg_get_u32_or_default(sub, "age_rating", 0)))
      update |= epg_episode_set_age_rating(ee, u32, NULL);

    if((u32 = htsmsg_get_u32_or_default(sub, "star_rating", 0)))
      update |= epg_episode_set_star_rating(ee, u32, NULL);

    if((s64 = htsmsg_get_s64_or_default(sub, "first_aired", 0)))
      update |= epg_episode_set_first_aired(ee, s64, NULL);

    if((u32 = htsmsg_get_u32_or_default(sub, "episode_number", 0)))
      en.e_num = u32;

    if((u32 = htsmsg_get_u32_or_default(sub, "episode_count", 0)))
      en.e_cnt = u32;

    if((u32 = htsmsg_get_u32_or_default(sub, "season_number", 0)))
      en.s_num = u32;

    if((u32 = htsmsg_get_u32_or_default(sub, "season_count", 0)))
      en.s_cnt = u32;

    if((u32 = htsmsg_get_u32_or_default(sub, "part_number", 0)))
      en.p_num = u32;

    if((u32 = htsmsg_get_u32_or_default(sub, "part_count", 0)))
      en.p_cnt = u32;
  }

  update |= epg_episode_set_epnum(ee, &en, NULL);

  return update;
}


/*
 * Queue consuming thread that spawn scraping jobs
 */
static void*
epgscrape_consumer_thread(void *aux)
{
  scrape_io_t *sio;
  char *outbuf, *inbuf;

  pthread_mutex_lock(&scrape_mutex);

  while(1) {
    if(!scrape_config.sc_enabled ||
       !(sio = TAILQ_FIRST(&scrape_queue))) {
      pthread_cond_wait(&scrape_cond, &scrape_mutex);
      continue;
    }
  
    TAILQ_REMOVE(&scrape_queue, sio, sio_link);

    pthread_mutex_unlock(&scrape_mutex);

    inbuf = htsmsg_json_serialize_to_str(sio->sio_input, 0);
    if(scrape_config.sc_exec &&
       spawn_and_store_stdout(scrape_config.sc_exec, NULL, 
			      inbuf, &outbuf) > 0)
      sio->sio_output = htsmsg_json_deserialize(outbuf);

    if(epgscrape_process_result(sio))
      epg_updated();

    if(sio->sio_input)
      htsmsg_destroy(sio->sio_input);

    if(sio->sio_output)
      htsmsg_destroy(sio->sio_output);

    usleep(100000);
    pthread_mutex_lock(&scrape_mutex);
  }

  return NULL;
}


/*
 * Get configuration
 */
htsmsg_t*
epgscrape_get_config(void)
{
  htsmsg_t *m = htsmsg_create_map();
  prop_read_values(&scrape_config, scrape_props, m, 0, NULL);
  return m;
}


/*
 * Set configuration
 */
int
epgscrape_set_config(htsmsg_t *m)
{
  int save;

  pthread_mutex_lock(&scrape_mutex);
  save = prop_write_values(&scrape_config, scrape_props, m, 0, NULL);

  if(save)
    pthread_cond_signal(&scrape_cond);
  pthread_mutex_unlock(&scrape_mutex);

  return save;
}


/*
 * Save configuration to disk
 */
void
epgscrape_save(void)
{
  htsmsg_t *m = epgscrape_get_config();
  hts_settings_save(m, "scrape/config");
  notify_reload("scrape");
}


/*
 * Init global variables, and start the scraping thread
 */
void
epgscrape_init(void)
{
  htsmsg_t *m;

  pthread_mutex_init(&scrape_mutex, NULL);
  pthread_cond_init(&scrape_cond, NULL);
  TAILQ_INIT(&scrape_queue);
  memset(&scrape_config, 0, sizeof(scrape_config_t));

  if((m = hts_settings_load("scrape/config"))) {
    epgscrape_set_config(m);
    htsmsg_destroy(m);
  }

  tvhthread_create(&scrape_thread, NULL, epgscrape_consumer_thread, 
		   NULL, 0);
}

