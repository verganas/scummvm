/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "mm/mm1/views/Tavern.h"
#include "mm/mm1/globals.h"

namespace MM {
namespace MM1 {
namespace Views {

Tavern::Tavern() : TextView("Tavern") {
	_bounds = getLineBounds(17, 24);
}

bool Tavern::msgFocus(const FocusMessage &msg) {
	g_events->msgBusiness(BusinessMessage(4));
	return true;
}

bool Tavern::msgKeypress(const KeypressMessage &msg) {
	return true;
}

void Tavern::draw() {

}

} // namespace Views
} // namespace MM1
} // namespace MM
