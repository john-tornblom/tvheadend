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
Scraper for the EIT description broadcasted by the Swedish cable tv
operator Comhem.
'''


import sys
import re
import json


sample_input = '''
{
"description": "Brittisk komediserie från 2008. Rick Spleen är en cynisk och \
misantropisk man vars liv kantas av besvikelser och pinsamheter. Dessutom har hans \
karriär som komiker inte alls gått som han tänkt sig. Skådespelare: Jack \
Dee(Rick Spleen), Raquel Cassidy(Mel), Sean Power(Marty), Tony Gardner(Michael), \
Anna Crilly(Magda), Antonia Campbell-Hughes(Sam), Rasmus Hardiker(Ben), William \
Hoyland(Ambrose), Christopher Godwin(Colin). 2013. Säsong 3. Del 1 av 7."
}
'''


def handle_message(msg):
    obj = json.loads(msg)
    season = None
    episode = None
    episode_count = None

    # Try to parse tv show information
    try:
        p = re.compile(r'\. (Säsong (\d+)\. )?(Del (\d+))( av (\d+))?\.$')
        g = p.search(obj['description'].encode('utf-8')).groups()
        if g[1]: season = int(g[1])
        if g[3]: episode = int(g[3])
        if g[5]: episode_count = int(g[5])
    except Exception as e:
        return

    res = dict()
    res['episode'] = dict()

    if season:
        res['episode']['season_number']  = season
        
    if episode:
        res['episode']['episode_number']  = episode

    if episode_count:
        res['episode']['episode_count'] = episode_count

    msg = json.dumps(res)
    print(msg)


if __name__ == "__main__":
    handle_message(sys.stdin.read())
    #handle_message(sample_input)

