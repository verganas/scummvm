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
#include "common/system.h"
#include "common/savefile.h"
#include "bagel/boflib/file.h"
#include "bagel/boflib/debug.h"
#include "bagel/boflib/log.h"

namespace Bagel {

#define CHUNK_SIZE 0x00007FFF

CBofFile::CBofFile() {
	m_szFileName[0] = '\0';
}

CBofFile::CBofFile(const char *pszFileName, uint32 lFlags) {
	m_szFileName[0] = '\0';
	Assert(pszFileName != nullptr);

	// Open now?
	if (pszFileName != nullptr) {
		open(pszFileName, lFlags);
	}
}

CBofFile::~CBofFile() {
	Assert(IsValidObject(this));

	Close();
}

ErrorCode CBofFile::create(const char *pszFileName, uint32 lFlags) {
	Assert(IsValidObject(this));
	Assert(pszFileName != nullptr);
	Assert(strlen(pszFileName) < MAX_DIRPATH);
	Assert(*pszFileName != '\0');

	// Can't create a read-only file
	Assert(!(lFlags & CBF_READONLY));

	m_lFlags = lFlags;

	// Remember this files name
	Common::strcpy_s(m_szFileName, pszFileName);

	// Create the file
	Common::OutSaveFile *save = g_system->getSavefileManager()->openForSaving(pszFileName, false);
	if (save != nullptr) {
		_stream = new SaveReadWriteStream(save);

		if (g_pDebugOptions != nullptr && g_pDebugOptions->m_bShowIO) {
			LogInfo(BuildString("Creating file '%s'", m_szFileName));
		}

	} else {
		ReportError(ERR_FOPEN, "Unable to create %s", m_szFileName);
	}

	return _errCode;
}

ErrorCode CBofFile::open(const char *pszFileName, uint32 lFlags) {
	Assert(IsValidObject(this));
	Assert(pszFileName != nullptr);
	Assert(strlen(pszFileName) < MAX_DIRPATH);
	Assert(*pszFileName != '\0');

	// Can't open for both Text and Binary modes
	Assert(!((lFlags & CBF_TEXT) && (lFlags & CBF_BINARY)));

	// Can't overwrite a readonly file
	Assert(!((lFlags & CBF_READONLY) && (lFlags & CBF_OVERWRITE)));

	// Can't create a new file to be readonly (there would be nothing to read!)
	Assert(!((lFlags & CBF_READONLY) && (lFlags & CBF_CREATE)));

	// Keep a copy of these flags
	m_lFlags = lFlags;

	if (_stream)
		return _errCode;

	if ((lFlags & CBF_CREATE) && ((lFlags & CBF_SAVEFILE) ||
			!Common::File::exists(pszFileName))) {
		create(pszFileName, lFlags);

	} else {
		// Remember this files' name
		Common::strcpy_s(m_szFileName, pszFileName);

		if (lFlags & CBF_SAVEFILE) {
			_stream = g_system->getSavefileManager()->openForLoading(pszFileName);

			if (!_stream)
				ReportError(ERR_FOPEN, "Could not open %s", pszFileName);

		} else {
			Common::File *f = new Common::File();

			if (f->open(pszFileName)) {
				_stream = f;

				if (g_pDebugOptions != nullptr && g_pDebugOptions->m_bShowIO) {
					LogInfo(BuildString("Opened file '%s'", m_szFileName));
				}
			} else {
				delete f;
				ReportError(ERR_FOPEN, "Could not open %s", pszFileName);
			}
		}
	}

	return _errCode;
}

void CBofFile::Close() {
	Assert(IsValidObject(this));

	if (_stream != nullptr) {
		if (g_pDebugOptions != nullptr && g_pDebugOptions->m_bShowIO) {
			LogInfo(BuildString("Closed file '%s'", m_szFileName));
		}

		delete _stream;
		_stream = nullptr;
	}
}

ErrorCode CBofFile::Read(void *pDestBuf, int32 lBytes) {
	Assert(IsValidObject(this));
	Assert(pDestBuf != nullptr);
	Assert(lBytes >= 0);

	Common::SeekableReadStream *rs = dynamic_cast<Common::SeekableReadStream *>(_stream);
	assert(rs);

	if (!ErrorOccurred()) {
		if (rs != nullptr) {
			byte *pBuf = (byte *)pDestBuf;

			while (lBytes > 0) {
				int nLength = (int)MIN(lBytes, (int32)CHUNK_SIZE);
				lBytes -= CHUNK_SIZE;

				if ((int)rs->read(pBuf, nLength) != nLength) {
					ReportError(ERR_FREAD, "Unable to read %d bytes from %s", nLength, m_szFileName);
				}

				pBuf += nLength;
			}

		} else {
			error("Attempt to read from a file that is not open for reading: %s", m_szFileName);
		}
	}

	return _errCode;
}

ErrorCode CBofFile::Write(const void *pSrcBuf, int32 lBytes) {
	Assert(IsValidObject(this));

	Common::WriteStream *ws = dynamic_cast<Common::WriteStream *>(_stream);

	if (ws != nullptr) {
		// As long as this file is not set for readonly, then write the buffer
		if (!(m_lFlags & CBF_READONLY)) {
			const byte *pBuf = (const byte *)pSrcBuf;

			while (lBytes > 0) {
				int nLength = (int)MIN(lBytes, (int32)CHUNK_SIZE);
				lBytes -= CHUNK_SIZE;

				if ((int)ws->write(pBuf, nLength) != nLength) {
					ReportError(ERR_FWRITE, "Unable to write %d bytes to %s", nLength, m_szFileName);
				}

				pBuf += nLength;
			}

			// Flush this file's buffer back out right now
			Commit();

		} else {
			LogWarning(BuildString("Attempted to write to the READONLY file '%s'", m_szFileName));
		}

	} else {
		LogWarning("Attempt to write to a file that is not open");
	}

	return _errCode;
}

ErrorCode CBofFile::setPosition(uint32 lPos) {
	Assert(IsValidObject(this));

	// Only supports files up to 2Gig
	Assert(lPos < 0x80000000);

	Common::SeekableReadStream *rs = dynamic_cast<Common::SeekableReadStream *>(_stream);
	Common::SeekableWriteStream *ws = dynamic_cast<Common::SeekableWriteStream *>(_stream);

	if (rs) {
		if (!rs->seek(lPos))
			ReportError(ERR_FSEEK, "Unable to seek to %ld", lPos);
	}
	if (ws) {
		if (!ws->seek(lPos))
			ReportError(ERR_FSEEK, "Unable to seek to %ld", lPos);
	}

	return _errCode;
}

uint32 CBofFile::getPosition() {
	Assert(IsValidObject(this));

	Common::SeekableReadStream *rs = dynamic_cast<Common::SeekableReadStream *>(_stream);
	Common::SeekableWriteStream *ws = dynamic_cast<Common::SeekableWriteStream *>(_stream);

	if (rs)
		return rs->pos();
	if (ws)
		return ws->pos();

	error("getPosition on closed file");
}

ErrorCode CBofFile::seekToEnd() {
	Assert(IsValidObject(this));

	Common::SeekableReadStream *rs = dynamic_cast<Common::SeekableReadStream *>(_stream);
	Common::SeekableWriteStream *ws = dynamic_cast<Common::SeekableWriteStream *>(_stream);

	if (rs)
		rs->seek(0, SEEK_END);
	else if (ws)
		ws->seek(0, SEEK_END);
	else
		error("Seek in closed file");

	return _errCode;
}

ErrorCode CBofFile::SetLength(uint32 /*lNewLength*/) {
	Assert(IsValidObject(this));

	LogWarning("CBofFile::SetLength() is not yet supported");

	return _errCode;
}

uint32 CBofFile::GetLength() {
	Assert(IsValidObject(this));

	Common::SeekableReadStream *rs = dynamic_cast<Common::SeekableReadStream *>(_stream);
	Common::SeekableWriteStream *ws = dynamic_cast<Common::SeekableWriteStream *>(_stream);

	if (rs)
		return rs->size();
	if (ws)
		return ws->size();

	error("GetLength in closed file");
}

void CBofFile::Commit() {
	Assert(IsValidObject(this));

	Common::SeekableWriteStream *ws = dynamic_cast<Common::SeekableWriteStream *>(_stream);
	if (ws)
		ws->finalize();
}

} // namespace Bagel
