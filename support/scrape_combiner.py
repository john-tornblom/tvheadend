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
A small pyton script that combines the imdb scraper for movies with
the comhem scraper for series information
'''

import sys
import json
import scrape_imdb
import scrape_comhem


if __name__ == "__main__":
    msg = sys.stdin.read()
    obj = json.loads(msg)
    res = None

    content_group = content_type = None
    if 'content_type' in obj:
        content_type  = obj['content_type']
        content_group = (content_type >> 4) & 0xf
    
    if content_group == 1: # A movie
        res = scrape_imdb.handle_message(msg)
    else:
        res = scrape_comhem.handle_message(msg)

    if res: print(res)

