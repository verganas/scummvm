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

#include "common/file.h"

#include "freescape/freescape.h"
#include "freescape/games/castle/castle.h"
#include "freescape/language/8bitDetokeniser.h"

namespace Freescape {

void CastleEngine::initZX() {
	_viewArea = Common::Rect(64, 36, 256, 148);
}

Graphics::Surface *CastleEngine::loadFrames(Common::SeekableReadStream *file, int pos, int numFrames, uint32 back) {
	Graphics::Surface *surface = new Graphics::Surface();
	file->seek(pos);
	int16 width = file->readByte();
	int16 height = file->readByte();
	surface->create(width * 8, height, _gfx->_texturePixelFormat);

	/*byte mask =*/ file->readByte();

	uint8 r, g, b;
	_gfx->readFromPalette(7, r, g, b);
	uint32 white = _gfx->_texturePixelFormat.ARGBToColor(0xFF, r, g, b);

	surface->fillRect(Common::Rect(0, 0, width * 8, height), white);
	/*int frameSize =*/ file->readUint16LE();

	for (int i = 0; i < width * height; i++) {
		byte color = file->readByte();
		for (int n = 0; n < 8; n++) {
			int y = i / width;
			int x = (i % width) * 8 + (7 - n);
			if ((color & (1 << n)))
				surface->setPixel(x, y, back);
		}
	}
	return surface;
}

void CastleEngine::loadAssetsZXFullGame() {
	Common::File file;
	uint8 r, g, b;

	file.open("castlemaster.zx.title");
	if (file.isOpen()) {
		_title = loadAndCenterScrImage(&file);
	} else
		error("Unable to find castlemaster.zx.title");

	file.close();
	file.open("castlemaster.zx.border");
	if (file.isOpen()) {
		_border = loadAndCenterScrImage(&file);
	} else
		error("Unable to find castlemaster.zx.border");
	file.close();

	file.open("castlemaster.zx.data");
	if (!file.isOpen())
		error("Failed to open castlemaster.zx.data");

	//loadMessagesFixedSize(&file, 0x4bc + 1, 16, 27);
	loadFonts(&file, 0x1219, _font);
	loadMessagesVariableSize(&file, 0x4bd, 71);

    load8bitBinary(&file, 0x6a3b, 16);
	loadSpeakerFxZX(&file, 0xc91, 0xccd);

	loadColorPalette();
	_gfx->readFromPalette(2, r, g, b);
	uint32 red = _gfx->_texturePixelFormat.ARGBToColor(0xFF, r, g, b);
	_keysFrame = loadFrames(&file, 0xdf7, 1, red);

	for (auto &it : _areaMap) {
		it._value->addStructure(_areaMap[255]);
		for (int16 id = 136; id < 140; id++) {
			it._value->addObjectFromArea(id, _areaMap[255]);
		}

		for (int16 id = 214; id < 228; id++) {
			it._value->addObjectFromArea(id, _areaMap[255]);
		}
	}

	_areaMap[1]->addFloor();
	_areaMap[2]->addFloor();
}

void CastleEngine::drawZXUI(Graphics::Surface *surface) {
	uint32 color = 5;
	uint8 r, g, b;

	_gfx->readFromPalette(color, r, g, b);
	uint32 front = _gfx->_texturePixelFormat.ARGBToColor(0xFF, r, g, b);

	color = 0;
	_gfx->readFromPalette(color, r, g, b);
	uint32 black = _gfx->_texturePixelFormat.ARGBToColor(0xFF, r, g, b);

	Common::Rect backRect(123, 179, 242 + 5, 188);
	surface->fillRect(backRect, black);

	Common::String message;
	int deadline;
	getLatestMessages(message, deadline);
	if (deadline <= _countdown) {
		drawStringInSurface(message, 120, 179, front, black, surface);
		_temporaryMessages.push_back(message);
		_temporaryMessageDeadlines.push_back(deadline);
	} else
		drawStringInSurface(_currentArea->_name, 120, 179, front, black, surface);

	for (int k = 0; k < _numberKeys; k++) {
		surface->copyRectToSurface((const Graphics::Surface)*_keysFrame, 99 - k * 4, 177, Common::Rect(0, 0, 6, 11));
	}

	//drawEnergyMeter(surface);
}

} // End of namespace Freescape
