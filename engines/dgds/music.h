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

#ifndef DGDS_MUSIC_H
#define DGDS_MUSIC_H

#include "audio/midiplayer.h"

namespace Dgds {

class DgdsMidiPlayer : public Audio::MidiPlayer {
public:
	DgdsMidiPlayer(bool isSfx);

	void play(byte *data, uint32 size);
	void stop();

private:
	/** like MidiPlyer::syncVolume, syncs sfx/music volume depending on isSfx */
	void syncVolumeForChannel();
	bool _isSfx;
};

} // End of namespace Dgds

#endif // DGDS_MUSIC_H

