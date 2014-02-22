#!/usr/bin/env python2
# encoding: utf-8
# Copyright (C) 2014 John Törnblom
#
# This file is part of tvheadend.
#
# tvheadend is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# tvheadend is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with tvheadend.  If not, see <http://www.gnu.org/licenses/>.


'''
Scraper that uses the title, description and content type of an EPG 
event to match a movie at IMDb with a given title and prodiction year.

Requires https://github.com/alberanid/imdbpy (the one included in ubuntu 
13.10 is out of date and does not work due to changes to the IMDb website).

'''


import sys
import re
import json
import imdb


sample_input = '''
{
    "title": "Horrible bosses",
    "content_type": 20,
    "description": "Amerikansk komedi från 2011. De tre vännerna Nick..."
}
'''


def get_movie_info(imdb_id):
    res = dict()
    i = imdb.IMDb()
    movie = i.get_movie(imdb_id)

    # Not sure why, but the full-size cover url is not amongst the dict keys
    try:
        res['image'] = movie['full-size cover url']
    except:
        pass

    for k, v in movie.iteritems():
        if not v: continue

        if k == 'title':
            res['title'] = v

        elif k == 'plot outline':
            res['summary'] = v

        elif k == 'plot':
            res['description'] = v[0]

        elif k == 'full-size cover url':
            res['image'] = v

        elif k == 'cover url' and 'image' not in res:
            res['image'] = v

        elif k == 'rating':
            res['star_rating'] = v * 10

    return res


def search_movie(title, year=None):
    i = imdb.IMDb()

    results = i.search_movie(title)
    for res in results:
        r = range(res['year'] - 1, res['year'] + 2)
        if year and not (year in r): continue

        return get_movie_info(res.movieID)

    # We might reach here if the year didn't match
    return get_movie_info(results[0])


def handle_message(msg):
    obj = json.loads(msg)

    # Try to parse the production year
    year = None
    try:
        p = re.compile(r'(\d\d\d\d)')
        g = p.search(obj['description']).groups()
        year =  int(g[0])
    except:
        pass

    content_group = content_type = None
    if 'content_type' in obj:
        content_type  = obj['content_type']
        content_group = (content_type >> 4) & 0xf
    
    # Not a movie
    if content_group != 1:
        return

    # Adult movie, usually not listed
    if content_type == 24:
        return

    res = dict()
    res['episode'] = search_movie(obj['title'], year)
    return json.dumps(res)


if __name__ == "__main__":
    res = handle_message(sys.stdin.read())
    #res = handle_message(sample_input)

    if res: print(res)
    
