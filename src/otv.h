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

#ifndef OTV_H
#define OTV_H

#include "service.h"

void otv_input(service_t *t, elementary_stream_t *st, const uint8_t *data,
               int len, int start, int error);

void otv_init(void);

#endif /* OTV_H */
