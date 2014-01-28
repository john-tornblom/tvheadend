/*
 *  Electronic Program Guide - External Scraping Interface
 *  Copyright (C) 2012 2014 John Törnblom
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

#ifndef EPG_SCRAPE_H
#define EPG_SCRAPE_H

#include "epg.h"
#include "htsmsg.h"

void epgscrape_enqueue_broadcast(epg_broadcast_t *ebc);

int       epgscrape_set_config(htsmsg_t *m);
htsmsg_t* epgscrape_get_config(void);

void epgscrape_save(void);
void epgscrape_init(void);


#endif /* EPG_SCRAPE_H */
