
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

#ifndef BAGEL_SPACEBAR_NAV_WINDOW_H
#define BAGEL_SPACEBAR_NAV_WINDOW_H

#include "bagel/boflib/gui/window.h"
#include "bagel/baglib/storage_dev_win.h"
#include "bagel/boflib/gui/text_box.h"
#include "bagel/boflib/gui/window.h"
#include "bagel/boflib/app.h"
#include "bagel/boflib/gui/button.h"
#include "bagel/boflib/gfx/sprite.h"
#include "bagel/boflib/sound.h"

namespace Bagel {
namespace SpaceBar {

typedef struct {
	const CHAR *m_pszCargo;
	INT Weight;
	BOOL m_bUsed;
} NCARGO;

typedef struct {
	const CHAR *Name;
	INT Left;
	INT Top;
	INT Right;
	INT Bottom;
	NCARGO cargo;

} NPLANET;


class CNavWindow : public CBagStorageDevWnd {
public:
	CNavWindow();

#if BOF_DEBUG
	virtual ~CNavWindow();
#endif

	virtual ERROR_CODE  Attach();
	virtual ERROR_CODE  Detach();

	virtual VOID OnBofButton(CBofObject *pButton, INT nState);

protected:
	VOID CalcFuel(DOUBLE hf);

	virtual VOID OnLButtonDown(UINT nFlags, CBofPoint *pPoint, void * = nullptr);
	//virtual VOID OnLButtonDblClk(UINT nFlags, CBofPoint *pPoint);
	virtual VOID OnKeyHit(ULONG lKey, ULONG lRepCount);
	virtual VOID OnTimer(UINT tId);
	//virtual VOID OnClose() { Quit(); }
	virtual VOID OnPaint(CBofRect *pRect);
	virtual VOID OnMainLoop();

	// split up the onmousedown method
	VOID RefreshData();     // added func to just paint text

	VOID OnPinna();
	VOID OnHpoctyl();
	VOID OnArmpit();
	VOID OnYzore();
	VOID OnBulbus();
	VOID OnDingle();
	VOID OnKarkas();
	VOID OnFruufnia();
	VOID OnWilbur();
	VOID OnMcKelvey();
	VOID OnMedge();
	VOID OnWall();
	VOID OnWoo();
	VOID OnRoman();
	VOID OnWeed();
	VOID OnBok();
	VOID OnPizer();
	VOID OnBackwater();
	VOID OnUranus();
	VOID OnToejam();
	VOID OnHellhole();
	VOID OnPustule();
	VOID OnSwamprock();
	VOID OnSlug();
	VOID OnMaggotsnest();
	VOID OnPeggleboz();
	VOID OnArcheroids();
	VOID OnPackRat();
	VOID OnBeacon();
	VOID OnMankala();
	VOID OnLife();
	VOID OnFuge();
	VOID OnGarfunkel();
	VOID OnBattlefish();
	VOID OnNoVacancy();

	// Data
	//
	INT m_level;
	INT m_fuel;
	INT m_cargo;
	INT m_ship;
	CBofRect *m_pPinna;
	CBofRect *m_pHpoctyl;
	CBofRect *m_pArmpit;
	CBofRect *m_pYzore;
	CBofRect *m_pBulbus;
	CBofRect *m_pDingle;
	CBofRect *m_pKarkas;
	CBofRect *m_pFruufnia;
	CBofBmpButton *m_pButtons[2];
	CBofPalette *m_pPal;
	CBofPalette *m_pOldPal;
	CBofSprite *m_pMap;
	CBofSprite *m_pCurLoc;
	//      CBofSprite *m_pLevelDone;
	CBofBitmap *m_pNewMap;
	CBofBitmap *bmptwo;
	CBofRect *m_pCurPos;
	CBofString *m_pPortName;
	CBofRect *m_pWilbur;
	CBofRect *m_pMcKelvey;
	CBofRect *m_pMedge;
	CBofRect *m_pWall;
	CBofRect *m_pWoo;
	CBofRect *m_pRoman;
	CBofRect *m_pWeed;
	CBofRect *m_pBok;
	CBofRect *m_pPizer;
	CBofRect *m_pBackwater;
	CBofRect *m_pUranus;
	CBofRect *m_pToejam;
	CBofRect *m_pHellhole;
	CBofRect *m_pPustule;
	CBofRect *m_pSwamprock;
	CBofRect *m_pSlug;
	CBofRect *m_pMaggotsnest;
	CBofRect *m_pPeggleboz;
	CBofRect *m_pArcheroids;
	CBofRect *m_pPackRat;
	CBofRect *m_pBeacon;
	CBofRect *m_pMankala;
	CBofRect *m_pLife;
	CBofRect *m_pFuge;
	CBofRect *m_pGarfunkel;
	CBofRect *m_pBattlefish;
	CBofRect *m_pNoVacancy;

	NPLANET *m_pLevel;
	BOOL m_bNavAttached;
};

} // namespace SpaceBar
} // namespace Bagel

#endif
