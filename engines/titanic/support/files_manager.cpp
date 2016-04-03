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

#include "common/file.h"
#include "titanic/support/files_manager.h"
#include "titanic/game_manager.h"

namespace Titanic {

CFilesManager::CFilesManager() : _gameManager(nullptr), 
		_assetsPath("Assets"), _exeResources(nullptr), _field0(0),
		_drive(-1), _field18(0), _field1C(0), _field3C(0) {
}

CFilesManager::~CFilesManager() {
	delete _exeResources;
}

bool CFilesManager::fileExists(const CString &name) {
	Common::File f;
	return f.exists(name);
}

bool CFilesManager::scanForFile(const CString &name) {
	if (name.empty())
		return false;

	CString filename = name;
	filename.toLowercase();
	
	if (filename[0] == 'y' || filename[0] == 'z')
		return true;
	else if (filename[0] < 'a' || filename[0] > 'c')
		return false;

	CString fname = filename;
	int idx = fname.indexOf('#');
	if (idx >= 0) {
		fname = fname.left(idx);
		fname += ".st";
	}

	if (_gameManager)
		_gameManager->viewChange();

	// The original had a bunch of code here handling determining
	// which asset path, if any, the filename was present for,
	// and storing the "active asset path" it was found on.
	// This is redundant for ScummVM, which takes care of the paths
	return fileExists(fname);
}

void CFilesManager::loadDrive() {
	assert(_drive == -1);
	resetView();
}

void CFilesManager::debug(CScreenManager *screenManager) {
	warning("TODO: CFilesManager::debug");
}

void CFilesManager::resetView() {
	if (_gameManager) {
		_gameManager->_gameState.setMode(GSMODE_SELECTED);
		_gameManager->initBounds();
	}
}

void CFilesManager::fn4(const CString &name) {
	warning("TODO: CFilesManager::fn4");
}

void CFilesManager::fn5(const CString &name) {
	warning("TODO: CFilesManager::fn5");
}

Common::SeekableReadStream *CFilesManager::getResource(
	Common::WinResourceID area, Common::WinResourceID name) {
	if (!_exeResources) {
		_exeResources = new Common::PEResources();
		_exeResources->loadFromEXE("st.exe");
	}
	
	return _exeResources->getResource(area, name);
}

} // End of namespace Titanic
