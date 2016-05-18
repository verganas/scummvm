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

#ifndef BACKENDS_CLOUD_DROPBOX_STORAGE_H
#define BACKENDS_CLOUD_DROPBOX_STORAGE_H

#include "backends/cloud/storage.h"
#include "backends/cloud/manager.h"

namespace Cloud {
namespace Dropbox {

class DropboxStorage: public Cloud::Storage {
	static Common::String KEY, SECRET;

	Common::String _token, _uid;

	/** This private constructor is called from loadFromConfig(). */
	DropboxStorage(Common::String token, Common::String uid);

	static void getAccessToken(Common::String code);

public:	
	virtual ~DropboxStorage();

	virtual void listDirectory(Common::String path);
	virtual void syncSaves();
	virtual void printInfo();	
	/**
	* Load token and user id from configs and return DropboxStorage for those.	
	* @return pointer to the newly created DropboxStorage or 0 if some problem occured.
	*/
	static DropboxStorage *loadFromConfig();

	/**
	* Returns Dropbox auth link.
	*/
	static Common::String DropboxStorage::getAuthLink();

	/**
	* Show message with Dropbox auth instructions. (Temporary)
	*/
	static void authThroughConsole();
};

} //end of namespace Dropbox
} //end of namespace Cloud

#endif
