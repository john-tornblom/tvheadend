/*
 *  API - EPG scraping related calls
 *
 *  Copyright (C) 2013 Adam Sutton
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; withm even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "tvheadend.h"
#include "channels.h"
#include "access.h"
#include "api.h"
#include "epgscrape.h"


static int
api_epgscrape_load
  ( void *opaque, const char *op, htsmsg_t *args, htsmsg_t **resp )
{
  htsmsg_t *l;
  pthread_mutex_lock(&global_lock);
  *resp = htsmsg_create_map();
  l     = htsmsg_create_list();
  htsmsg_add_msg(l, NULL, epgscrape_get_config());
  htsmsg_add_msg(*resp, "entries", l);
  pthread_mutex_unlock(&global_lock);
  return 0;
}

static int
api_epgscrape_save
  ( void *opaque, const char *op, htsmsg_t *args, htsmsg_t **resp )
{
  pthread_mutex_lock(&global_lock);
  if (epgscrape_set_config(args))
    epgscrape_save();
  pthread_mutex_unlock(&global_lock);
  *resp = htsmsg_create_map();
  htsmsg_add_u32(*resp, "success", 1);
  return 0;
}

void
api_epgscrape_init ( void )
{
  static api_hook_t ah[] = {
    { "epgscrape/config/load", ACCESS_ADMIN, api_epgscrape_load, NULL },
    { "epgscrape/config/save", ACCESS_ADMIN, api_epgscrape_save, NULL },
    { NULL },
  };

  api_register_all(ah);
}