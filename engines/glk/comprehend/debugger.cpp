/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "glk/comprehend/debugger.h"
#include "glk/comprehend/comprehend.h"
#include "glk/comprehend/pics.h"

namespace Glk {
namespace Comprehend {

Debugger *g_debugger;

Debugger::Debugger() : Glk::Debugger() {
	g_debugger = this;
	registerCmd("dump", WRAP_METHOD(Debugger, cmdDump));
	registerCmd("floodfills", WRAP_METHOD(Debugger, cmdFloodfills));
	registerCmd("room", WRAP_METHOD(Debugger, cmdRoom));
	registerCmd("itemroom", WRAP_METHOD(Debugger, cmdItemRoom));
}

Debugger::~Debugger() {
	g_debugger = nullptr;
}

void Debugger::print(const char *fmt, ...) {
	va_list argp;
	va_start(argp, fmt);
	Common::String msg = Common::String::vformat(fmt, argp);
	va_end(argp);

	debugPrintf("%s", msg.c_str());
}

bool Debugger::cmdDump(int argc, const char **argv) {
	Common::String param = (argc == 2) ? argv[1] : "";
	ComprehendGame *game = g_comprehend->_game;

	if (!dumpGameData(game, param))
		debugPrintf("Unknown dump option\n");

	return true;
}

bool Debugger::cmdFloodfills(int argc, const char **argv) {
	if (argc == 2 && !strcmp(argv[1], "off")) {
		g_comprehend->_drawFlags |= IMAGEF_NO_FLOODFILL;
		debugPrintf("Floodfills are off\n");
	} else {
		g_comprehend->_drawFlags &= ~IMAGEF_NO_FLOODFILL;
		debugPrintf("Floodfills are on\n");
	}

	return true;
}

bool Debugger::cmdRoom(int argc, const char **argv) {
	ComprehendGame *game = g_comprehend->getGame();

	if (argc == 1) {
		debugPrintf("Current room = %d\n", game->_currentRoom);
		return true;
	} else {
		game->move_to(strToInt(argv[1]));
		game->update_graphics();
		return false;
	}
}

bool Debugger::cmdItemRoom(int argc, const char **argv) {
	ComprehendGame *game = g_comprehend->getGame();

	if (argc == 1) {
		debugPrintf("itemroom <item> [<room>]\n");
	} else {
		Item *item = game->get_item(strToInt(argv[1]));

		if (argc == 2) {
			debugPrintf("Item room = %d\n", item->_room);
		} else {
			int room = strToInt(argv[2]);
			if (room == 0)
				room = game->_currentRoom;
			bool visibleChange = item->_room == game->_currentRoom ||
				room == game->_currentRoom;

			item->_room = room;

			if (visibleChange) {
				game->_updateFlags |= UPDATE_GRAPHICS_ITEMS;
				game->update_graphics();
			}

			return false;
		}
	}

	return true;
}

} // namespace Comprehend
} // namespace Glk
