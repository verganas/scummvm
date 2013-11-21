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

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "sci/sci.h"
#include "sci/engine/kernel.h"
#include "sci/engine/script.h"
#include "sci/engine/state.h"
#include "sci/engine/features.h"

#include "common/util.h"

namespace Sci {

// IMPORTANT:
// every patch entry needs the following:
//  - script number (pretty obvious)
//
//  - apply count
//     specifies the number of times a patch is supposed to get applied.
//     Most of the time, it should be 1.
//
//  - magicDWORD + magicOffset
//     please ALWAYS put 0 for those two. Both will get filled out at runtime by the patcher.
//
//  - signature data (is used to identify certain script code, that needs patching)
//     every signature needs to contain SIG_MAGICDWORD once.
//      The following 4 bytes after SIG_MAGICDWORD - which don't have to be fixed, you may for example
//      use SIG_SELECTOR16, will get used to quickly search for a partly match before verifying that
//      the whole signature actually matches. If it's not included, the script patcher will error() out
//      right when loading up the game.
//     If selector-IDs are included, please use SIG_SELECTOR16 + SIG_SELECTOR8 [1]. Simply
//      specify the selector that way, so that the patcher will search for the specific
//      selector instead of looking for a hardcoded value. Selectors may not be the same
//      between game versions.
//     For UINT16s either use SIG_UINT16 or SIG_SELECTOR16.
//      Macintosh versions of SCI games are using BE ordering instead of LE since SCI1.1 for UINT16s in scripts
//      By using those 2 commands, it's possible to make patches work for PC and Mac versions of the same game.
//     You may also skip bytes by using the SIG_ADDTOOFFSET command
//     Every signature data needs to get terminated using SIGNATURE_END
//
//  - patch data (is used for actually patching scripts)
//     When a match is found, the patch data will get applied.
//     Patch data is similar to signature data. Just use PATCH_SELECTOR16 + PATCH_SELECTOR8 [1]
//      for patching in selectors.
//     There are also patch specific commands.
//     Those are PATCH_GETORIGINALBYTE, which fetches a byte from the original script
//      and PATCH_GETORIGINALBYTEADJUST, which does the same but gets a second value
//      from the uint16 array and uses that value to adjust the original byte.
//     Every patch data needs to get terminated using PATCH_END
//
//  - and please always add a comment about why the patch was done and what's causing issues.
//     If possible make sure, that the patch works on localized (or just different) game versions
//      as well in case those need patching too.
//
// [1] - selectors need to get specified in selectorTable[] and ScriptPatcherSelectors-enum
//        before they can get used using the SIG_SELECTORx and PATCH_SELECTORx commands.
//        You have to use the exact same order in both the table and the enum, otherwise
//        it won't work.

#define SIG_END               0xFFFF
#define SIG_MISMATCH          0xFFFE
#define SIG_COMMANDMASK       0xF000
#define SIG_VALUEMASK         0x0FFF
#define SIG_BYTEMASK          0x00FF
#define SIG_MAGICDWORD        0xF000
#define SIG_ADDTOOFFSET       0xE000
#define SIG_SELECTOR16        0x9000
#define SIG_SELECTOR8         0x8000
#define SIG_UINT16            0x1000
#define SIG_BYTE              0x0000

#define PATCH_END                   SIG_END
#define PATCH_COMMANDMASK           SIG_COMMANDMASK
#define PATCH_VALUEMASK             SIG_VALUEMASK
#define PATCH_BYTEMASK              SIG_BYTEMASK
#define PATCH_ADDTOOFFSET           SIG_ADDTOOFFSET
#define PATCH_GETORIGINALBYTE       0xD000
#define PATCH_GETORIGINALBYTEADJUST 0xC000
#define PATCH_SELECTOR16            SIG_SELECTOR16
#define PATCH_SELECTOR8             SIG_SELECTOR8
#define PATCH_UINT16                SIG_UINT16
#define PATCH_BYTE                  SIG_BYTE

// defines maximum scratch area for getting original bytes from unpatched script data
#define PATCH_VALUELIMIT      4096

struct SciScriptPatcherEntry {
	bool active;
	uint16 scriptNr;
	const char *description;
	int16 applyCount;
	uint32 magicDWord;
	int magicOffset;
	const uint16 *signatureData;
	const uint16 *patchData;
};

#define SCI_SIGNATUREENTRY_TERMINATOR { false, 0, NULL, 0, 0, 0, NULL, NULL }

struct SciScriptPatcherSelector {
	const char *name;
	int16 id;
};

SciScriptPatcherSelector selectorTable[] = {
	{ "cycles",            -1, }, // system selector
	{ "seconds",           -1, }, // system selector
	{ "init",              -1, }, // system selector
	{ "dispose",           -1, }, // system selector
	{ "new",               -1, }, // system selector
	{ "curEvent",          -1, }, // system selector
	{ "disable",           -1, }, // system selector
	{ "show",              -1, }, // system selector
	{ "x",                 -1, }, // system selector
	{ "cel",               -1, }, // system selector
	{ "setMotion",         -1, }, // system selector
	{ "deskSarg",          -1, }, // Gabriel Knight
	{ "localize",          -1, }, // Freddy Pharkas
	{ "put",               -1, }, // Police Quest 1 VGA
	{ "solvePuzzle",       -1, }, // Quest For Glory 3
	{ "timesShownID",      -1, }, // Space Quest 1 VGA
	{ "startText",         -1, }, // King's Quest 6 CD / Laura Bow 2 CD for audio+text support
	{ "startAudio",        -1, }, // King's Quest 6 CD / Laura Bow 2 CD for audio+text support
	{ "modNum",            -1, }, // King's Quest 6 CD / Laura Bow 2 CD for audio+text support
	{ NULL,                -1 }
};

enum ScriptPatcherSelectors {
	SELECTOR_cycles = 0,
	SELECTOR_seconds,
	SELECTOR_init,
	SELECTOR_dispose,
	SELECTOR_new,
	SELECTOR_curEvent,
	SELECTOR_disable,
	SELECTOR_show,
	SELECTOR_x,
	SELECTOR_cel,
	SELECTOR_setMotion,
	SELECTOR_deskSarg,
	SELECTOR_localize,
	SELECTOR_put,
	SELECTOR_solvePuzzle,
	SELECTOR_timesShownID,
	SELECTOR_startText,
	SELECTOR_startAudio,
	SELECTOR_modNum
};

// ===========================================================================
// Conquests of Camelot
// At the bazaar in Jerusalem, it's possible to see a girl taking a shower.
//  If you get too close, you get warned by the father - if you don't get away,
//  he will kill you.
// Instead of walking there manually, it's also possible to enter "look window"
//  and ego will automatically walk to the window. It seems that this is something
//  that wasn't properly implemented, because instead of getting killed, you will
//  get an "Oops" message in Sierra SCI.
//
// This is caused by peepingTom in script 169 not getting properly initialized.
// peepingTom calls the object behind global b9h. This global variable is
//  properly initialized, when walking there manually (method fawaz::doit).
// When you instead walk there automatically (method fawaz::handleEvent), that
//  global isn't initialized, which then results in the Oops-message in Sierra SCI
//  and an error message in ScummVM/SCI.
//
// We fix the script by patching in a jump to the proper code inside fawaz::doit.
// Responsible method: fawaz::handleEvent
// Fixes bug #3614969
const uint16 camelotSignaturePeepingTom[] = {
	0x72, SIG_MAGICDWORD, SIG_UINT16 + 0x7e, 0x07, // lofsa fawaz <-- start of proper initializion code
	0xa1, 0xb9,                      // sag b9h
	SIG_ADDTOOFFSET +571,            // skip 571 bytes
	0x39, 0x7a,                      // pushi 7a <-- initialization code when walking automatically
	0x78,                            // push1
	0x7a,                            // push2
	0x38, SIG_UINT16 + 0xa9, 0x00,   // pushi 00a9 - script 169
	0x78,                            // push1
	0x43, 0x02, 0x04,                // call kScriptID
	0x36,                            // push
	0x81, 0x00,                      // lag 00
	0x4a, 0x06,                      // send 06
	0x32, SIG_UINT16 + 0x20, 0x05,   // jmp [end of fawaz::handleEvent]
	SIG_END
};

const uint16 camelotPatchPeepingTom[] = {
	PATCH_ADDTOOFFSET +576,
	0x32, PATCH_UINT16 + 0xbd, 0xfd, // jmp to fawaz::doit / properly init peepingTom code
	PATCH_END
};

//         script, description,                                            signature                   patch
SciScriptPatcherEntry camelotSignatures[] = {
	{  true,   62, "fix peepingTom Sierra bug",                    1, 0, 0, camelotSignaturePeepingTom, camelotPatchPeepingTom },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// stayAndHelp::changeState (0) is called when ego swims to the left or right
//  boundaries of room 660. Normally a textbox is supposed to get on screen
//  but the call is wrong, so not only do we get an error message the script
//  is also hanging because the cue won't get sent out
//  This also happens in sierra sci
// Applies to at least: PC-CD
// Responsible method: stayAndHelp::changeState
// Fixes bug: #3038387
const uint16 ecoquest1SignatureStayAndHelp[] = {
	0x3f, 0x01,                      // link 01
	0x87, 0x01,                      // lap param[1]
	0x65, 0x14,                      // aTop state
	0x36,                            // push
	0x3c,                            // dup
	0x35, 0x00,                      // ldi 00
	0x1a,                            // eq?
	0x31, 0x1c,                      // bnt [next state]
	0x76,                            // push0
	0x45, 0x01, 0x00,                // callb export1 from script 0 (switching control off)
	SIG_MAGICDWORD,
	0x38, SIG_UINT16 + 0x22, 0x01,   // pushi 0122
	0x78,                            // push1
	0x76,                            // push0
	0x81, 0x00,                      // lag global[0]
	0x4a, 0x06,                      // send 06 - call ego::setMotion(0)
	0x39, SIG_SELECTOR8 + SELECTOR_init, // pushi "init"
	0x39, 0x04,                      // pushi 04
	0x76,                            // push0
	0x76,                            // push0
	0x39, 0x17,                      // pushi 17
	0x7c,                            // pushSelf
	0x51, 0x82,                      // class EcoNarrator
	0x4a, 0x0c,                      // send 0c - call EcoNarrator::init(0, 0, 23, self) (BADLY BROKEN!)
	0x33,                            // jmp [end]
	SIG_END
};

const uint16 ecoquest1PatchStayAndHelp[] = {
	0x87, 0x01,                      // lap param[1]
	0x65, 0x14,                      // aTop state
	0x36,                            // push
	0x2f, 0x22,                      // bt [next state] (this optimization saves 6 bytes)
	0x39, 0x00,                      // pushi 0 (wasting 1 byte here)
	0x45, 0x01, 0x00,                // callb export1 from script 0 (switching control off)
	0x38, PATCH_UINT16 + 0x22, 0x01, // pushi 0122
	0x78,                            // push1
	0x76,                            // push0
	0x81, 0x00,                      // lag global[0]
	0x4a, 0x06,                      // send 06 - call ego::setMotion(0)
	0x39, PATCH_SELECTOR8 + SELECTOR_init, // pushi "init"
	0x39, 0x06,                      // pushi 06
	0x39, 0x02,                      // pushi 02 (additional 2 bytes)
	0x76,                            // push0
	0x76,                            // push0
	0x39, 0x17,                      // pushi 17
	0x7c,                            // pushSelf
	0x38, PATCH_UINT16 + 0x80, 0x02, // pushi 280 (additional 3 bytes)
	0x51, 0x82,                      // class EcoNarrator
	0x4a, 0x10,                      // send 10 - call EcoNarrator::init(2, 0, 0, 23, self, 640)
	PATCH_END
};

//          script, description,                                            signature                      patch
SciScriptPatcherEntry ecoquest1Signatures[] = {
	{  true,   660, "CD: bad messagebox and freeze",               1, 0, 0, ecoquest1SignatureStayAndHelp, ecoquest1PatchStayAndHelp },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// doMyThing::changeState (2) is supposed to remove the initial text on the
//  ecorder. This is done by reusing temp-space, that was filled on state 1.
//  this worked in sierra sci just by accident. In our sci, the temp space
//  is resetted every time, which means the previous text isn't available
//  anymore. We have to patch the code because of that - bug #3035386
const uint16 ecoquest2SignatureEcorder[] = {
	0x31, 0x22,                      // bnt [next state]
	0x39, 0x0a,                      // pushi 0a
	0x5b, 0x04, 0x1e,                // lea temp[1e]
	0x36,                            // push
	SIG_MAGICDWORD,
	0x39, 0x64,                      // pushi 64
	0x39, 0x7d,                      // pushi 7d
	0x39, 0x32,                      // pushi 32
	0x39, 0x66,                      // pushi 66
	0x39, 0x17,                      // pushi 17
	0x39, 0x69,                      // pushi 69
	0x38, PATCH_UINT16 + 0x31, 0x26, // pushi 2631
	0x39, 0x6a,                      // pushi 6a
	0x39, 0x64,                      // pushi 64
	0x43, 0x1b, 0x14,                // call kDisplay
	0x35, 0x0a,                      // ldi 0a
	0x65, 0x20,                      // aTop ticks
	0x33,                            // jmp [end]
	SIG_ADDTOOFFSET +1,              // [skip 1 byte]
	0x3c,                            // dup
	0x35, 0x03,                      // ldi 03
	0x1a,                            // eq?
	0x31,                            // bnt [end]
	SIG_END
};

const uint16 ecoquest2PatchEcorder[] = {
	0x2f, 0x02,                      // bt [to pushi 07]
	0x3a,                            // toss
	0x48,                            // ret
	0x38, PATCH_UINT16 + 0x07, 0x00, // pushi 07 (parameter count) (waste 1 byte)
	0x39, 0x0b,                      // push (FillBoxAny)
	0x39, 0x1d,                      // pushi 29d
	0x39, 0x73,                      // pushi 115d
	0x39, 0x5e,                      // pushi 94d
	0x38, PATCH_UINT16 + 0xd7, 0x00, // pushi 215d
	0x78,                            // push1 (visual screen)
	0x38, PATCH_UINT16 + 0x17, 0x00, // pushi 17 (color) (waste 1 byte)
	0x43, 0x6c, 0x0e,                // call kGraph
	0x38, PATCH_UINT16 + 0x05, 0x00, // pushi 05 (parameter count) (waste 1 byte)
	0x39, 0x0c,                      // pushi 12d (UpdateBox)
	0x39, 0x1d,                      // pushi 29d
	0x39, 0x73,                      // pushi 115d
	0x39, 0x5e,                      // pushi 94d
	0x38, PATCH_UINT16 + 0xd7, 0x00, // pushi 215d
	0x43, 0x6c, 0x0a,                // call kGraph
	PATCH_END
};

// ===========================================================================
// Same patch as above for the ecorder introduction. Fixes bug #3092115.
// Two workarounds are needed for this patch in workarounds.cpp (when calling
// kGraphFillBoxAny and kGraphUpdateBox), as there isn't enough space to patch
// the function otherwise.
const uint16 ecoquest2SignatureEcorderTutorial[] = {
	0x30, SIG_UINT16 + 0x23, 0x00,   // bnt [next state]
	0x39, 0x0a,                      // pushi 0a
	0x5b, 0x04, 0x1f,                // lea temp[1f]
	0x36,                            // push
	SIG_MAGICDWORD,
	0x39, 0x64,                      // pushi 64
	0x39, 0x7d,                      // pushi 7d
	0x39, 0x32,                      // pushi 32
	0x39, 0x66,                      // pushi 66
	0x39, 0x17,                      // pushi 17
	0x39, 0x69,                      // pushi 69
	0x38, SIG_UINT16 + 0x31, 0x26,   // pushi 2631
	0x39, 0x6a,                      // pushi 6a
	0x39, 0x64,                      // pushi 64
	0x43, 0x1b, 0x14,                // call kDisplay
	0x35, 0x1e,                      // ldi 1e
	0x65, 0x20,                      // aTop ticks
	0x32,                            // jmp [end]
	// 2 extra bytes, jmp offset
	SIG_END
};

const uint16 ecoquest2PatchEcorderTutorial[] = {
	0x31, 0x23,                      // bnt [next state] (save 1 byte)
	// The parameter count below should be 7, but we're out of bytes
	// to patch! A workaround has been added because of this
	0x78,                            // push1 (parameter count)
	//0x39, 0x07,                    // pushi 07 (parameter count)
	0x39, 0x0b,                      // push (FillBoxAny)
	0x39, 0x1d,                      // pushi 29d
	0x39, 0x73,                      // pushi 115d
	0x39, 0x5e,                      // pushi 94d
	0x38, PATCH_UINT16 + 0xd7, 0x00, // pushi 215d
	0x78,                            // push1 (visual screen)
	0x39, 0x17,                      // pushi 17 (color)
	0x43, 0x6c, 0x0e,                // call kGraph
	// The parameter count below should be 5, but we're out of bytes
	// to patch! A workaround has been added because of this
	0x78,                            // push1 (parameter count)
	//0x39, 0x05,                    // pushi 05 (parameter count)
	0x39, 0x0c,                      // pushi 12d (UpdateBox)
	0x39, 0x1d,                      // pushi 29d
	0x39, 0x73,                      // pushi 115d
	0x39, 0x5e,                      // pushi 94d
	0x38, PATCH_UINT16 + 0xd7, 0x00, // pushi 215d
	0x43, 0x6c, 0x0a,                // call kGraph
	// We are out of bytes to patch at this point,
	// so we skip 494 (0x1EE) bytes to reuse this code:
	// ldi 1e
	// aTop 20
	// jmp 030e (jump to end)
	0x32, PATCH_UINT16 + 0xee, 0x01, // skip 494 (0x1EE) bytes
	PATCH_END
};

//          script, description,                                            signature                          patch
SciScriptPatcherEntry ecoquest2Signatures[] = {
	{  true,    50, "initial text not removed on ecorder",         1, 0, 0, ecoquest2SignatureEcorder,         ecoquest2PatchEcorder },
	{  true,   333, "initial text not removed on ecorder tutorial",1, 0, 0, ecoquest2SignatureEcorderTutorial, ecoquest2PatchEcorderTutorial },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// EventHandler::handleEvent in Demo Quest has a bug, and it jumps to the
// wrong address when an incorrect word is typed, therefore leading to an
// infinite loop. This script bug was not apparent in SSCI, probably because
// event handling was slightly different there, so it was never discovered.
// Fixes bug #3038870.
const uint16 fanmadeSignatureInfiniteLoop[] = {
	0x38, SIG_UINT16 + 0x4c, 0x00,   // pushi 004c
	0x39, 0x00,                      // pushi 00
	0x87, 0x01,                      // lap 01
	0x4b, 0x04,                      // send 04
	SIG_MAGICDWORD,
	0x18,                            // not
	0x30, SIG_UINT16 + 0x2f, 0x00,   // bnt 002f  [06a5]	--> jmp ffbc  [0664] --> BUG! infinite loop
	SIG_END
};

const uint16 fanmadePatchInfiniteLoop[] = {
	PATCH_ADDTOOFFSET | +10,
	0x30, SIG_UINT16 + 0x32, 0x00,    // bnt 0032  [06a8] --> pushi 004c
	PATCH_END
};

//          script, description,                                            signature                     patch
SciScriptPatcherEntry fanmadeSignatures[] = {
	{  true,   999, "infinite loop on typo",                       1, 0, 0, fanmadeSignatureInfiniteLoop, fanmadePatchInfiniteLoop },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
//  script 0 of freddy pharkas/CD PointsSound::check waits for a signal and if
//   no signal received will call kDoSound(0xD) which is a dummy in sierra sci
//   and ScummVM and will use acc (which is not set by the dummy) to trigger
//   sound disposal. This somewhat worked in sierra sci, because the sample
//   was already playing in the sound driver. In our case we would also stop
//   the sample from playing, so we patch it out
//   The "score" code is already buggy and sets volume to 0 when playing
// Applies to at least: English PC-CD
// Responsible method: unknown
const uint16 freddypharkasSignatureScoreDisposal[] = {
	0x67, 0x32,                      // pTos 32 (selector theAudCount)
	0x78,                            // push1
	SIG_MAGICDWORD,
	0x39, 0x0d,                      // pushi 0d
	0x43, 0x75, 0x02,                // call kDoAudio
	0x1c,                            // ne?
	0x31,                            // bnt (-> to skip disposal)
	SIG_END
};

const uint16 freddypharkasPatchScoreDisposal[] = {
	0x34, PATCH_UINT16 + 0x00, 0x00, // ldi 0000
	0x34, PATCH_UINT16 + 0x00, 0x00, // ldi 0000
	0x34, PATCH_UINT16 + 0x00, 0x00, // ldi 0000
	PATCH_END
};

//  script 235 of freddy pharkas rm235::init and sEnterFrom500::changeState
//   disable icon 7+8 of iconbar (CD only). When picking up the canister after
//   placing it down, the scripts will disable all the other icons. This results
//   in IconBar::disable doing endless loops even in sierra sci, because there
//   is no enabled icon left. We remove disabling of icon 8 (which is help),
//   this fixes the issue.
// Applies to at least: English PC-CD
// Responsible method: rm235::init and sEnterFrom500::changeState
const uint16 freddypharkasSignatureCanisterHang[] = {
	0x38, SIG_SELECTOR16 + SELECTOR_disable, // pushi disable
	0x7a,                            // push2
	SIG_MAGICDWORD,
	0x39, 0x07,                      // pushi 07
	0x39, 0x08,                      // pushi 08
	0x81, 0x45,                      // lag 45
	0x4a, 0x08,                      // send 08 - call IconBar::disable(7, 8)
	SIG_END
};

const uint16 freddypharkasPatchCanisterHang[] = {
	PATCH_ADDTOOFFSET | +3,
	0x78,                            // push1
	PATCH_ADDTOOFFSET | +2,
	0x33, 0x00,                      // ldi 00 (waste 2 bytes)
	PATCH_ADDTOOFFSET | +3,
	0x06,                            // send 06 - call IconBar::disable(7)
	PATCH_END
};

//  script 215 of freddy pharkas lowerLadder::doit and highLadder::doit actually
//   process keyboard-presses when the ladder is on the screen in that room.
//   They strangely also call kGetEvent. Because the main User::doit also calls
//   kGetEvent, it's pure luck, where the event will hit. It's the same issue
//   as in QfG1VGA and if you turn dos-box to max cycles, and click around for
//   ego, sometimes clicks also won't get registered. Strangely it's not nearly
//   as bad as in our sci, but these differences may be caused by timing.
//   We just reuse the active event, thus removing the duplicate kGetEvent call.
// Applies to at least: English PC-CD, German Floppy, English Mac
// Responsible method: lowerLadder::doit and highLadder::doit
const uint16 freddypharkasSignatureLadderEvent[] = {
	0x39, SIG_MAGICDWORD,
	SIG_SELECTOR8 + SELECTOR_new,    // pushi new
	0x76,                            // push0
	0x38, SIG_SELECTOR16 + SELECTOR_curEvent, // pushi curEvent
	0x76,                            // push0
	0x81, 0x50,                      // lag global[50]
	0x4a, 0x04,                      // send 04 - read User::curEvent
	0x4a, 0x04,                      // send 04 - call curEvent::new
	0xa5, 0x00,                      // sat temp[0]
	0x38, SIG_SELECTOR16 + SELECTOR_localize,
	0x76,                            // push0
	0x4a, 0x04,                      // send 04 - call curEvent::localize
	SIG_END
};

const uint16 freddypharkasPatchLadderEvent[] = {
	0x34, 0x00, 0x00,                // ldi 0000 (waste 3 bytes, overwrites first 2 pushes)
	PATCH_ADDTOOFFSET | +8,
	0xa5, 0x00,                      // sat temp[0] (waste 2 bytes, overwrites 2nd send)
	PATCH_ADDTOOFFSET | +2,
	0x34, 0x00, 0x00,                // ldi 0000
	0x34, 0x00, 0x00,                // ldi 0000 (waste 6 bytes, overwrites last 3 opcodes)
	PATCH_END
};

// In the Macintosh version of Freddy Pharkas, kRespondsTo is broken for
// property selectors. They hacked the script to work around the issue,
// so we revert the script back to using the values of the DOS script.
// Applies to at least: English Mac
// Responsible method: unknown
const uint16 freddypharkasSignatureMacInventory[] = {
	SIG_MAGICDWORD,
	0x39, 0x23,                      // pushi 23
	0x39, 0x74,                      // pushi 74
	0x78,                            // push1
	0x38, SIG_UINT16 + 0x74, 0x01,   // pushi 0174 (on mac it's actually 0x01, 0x74)
	0x85, 0x15,                      // lat 15
	SIG_END
};

const uint16 freddypharkasPatchMacInventory[] = {
	0x39, 0x02,                      // pushi 02 (now matches the DOS version)
	PATCH_ADDTOOFFSET +23,
	0x39, 0x04,                      // pushi 04 (now matches the DOS version)
	PATCH_END
};

//          script, description,                                            signature                            patch
SciScriptPatcherEntry freddypharkasSignatures[] = {
	{  true,     0, "CD: score early disposal",                    1, 0, 0, freddypharkasSignatureScoreDisposal, freddypharkasPatchScoreDisposal },
	{  true,    15, "Mac: broken inventory",                       1, 0, 0, freddypharkasSignatureMacInventory,  freddypharkasPatchMacInventory },
	{  true,   235, "CD: canister pickup hang",                    3, 0, 0, freddypharkasSignatureCanisterHang,  freddypharkasPatchCanisterHang },
	{  true,   320, "ladder event issue",                          2, 0, 0, freddypharkasSignatureLadderEvent,   freddypharkasPatchLadderEvent },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// daySixBeignet::changeState (4) is called when the cop goes out and sets cycles to 220.
//  this is not enough time to get to the door, so we patch that to 23 seconds
// Applies to at least: English PC-CD, German PC-CD, English Mac
// Responsible method: daySixBeignet::changeState
const uint16 gk1SignatureDay6PoliceBeignet[] = {
	0x35, 0x04,                         // ldi 04
	0x1a,                               // eq?
	0x30, SIG_ADDTOOFFSET +2,           // bnt [next state check]
	0x38, SIG_SELECTOR16 + SELECTOR_dispose, // pushi dispose
	0x76,                               // push0
	0x72, SIG_ADDTOOFFSET +2,           // lofsa deskSarg
	0x4a, SIG_UINT16 + 0x04, 0x00,      // send 04
	SIG_MAGICDWORD,
	0x34, SIG_UINT16 + 0xdc, 0x00,      // ldi 220
	0x65, SIG_ADDTOOFFSET +1,           // aTop cycles (1a for PC, 1c for Mac)
	0x32,                               // jmp [end]
	SIG_END
};

const uint16 gk1PatchDay6PoliceBeignet[] = {
	PATCH_ADDTOOFFSET +16,
	0x34, PATCH_UINT16 + 0x17, 0x00,    // ldi 23
	0x65, PATCH_GETORIGINALBYTEADJUST +20, +2, // aTop seconds (1c for PC, 1e for Mac)
	PATCH_END
};

// sargSleeping::changeState (8) is called when the cop falls asleep and sets cycles to 220.
//  this is not enough time to get to the door, so we patch it to 42 seconds
// Applies to at least: English PC-CD, German PC-CD, English Mac
// Responsible method: sargSleeping::changeState
const uint16 gk1SignatureDay6PoliceSleep[] = {
	0x35, 0x08,                         // ldi 08
	0x1a,                               // eq?
	0x31, SIG_ADDTOOFFSET +1,           // bnt [next state check]
	SIG_MAGICDWORD,
	0x34, SIG_UINT16 + 0xdc, 0x00,      // ldi 220
	0x65, SIG_ADDTOOFFSET +1,           // aTop cycles (1a for PC, 1c for Mac)
	0x32,                               // jmp [end]
	0
};

const uint16 gk1PatchDay6PoliceSleep[] = {
	PATCH_ADDTOOFFSET +5,
	0x34, SIG_UINT16 + 0x2a, 0x00,      // ldi 42
	0x65, PATCH_GETORIGINALBYTEADJUST +9, +2, // aTop seconds (1c for PC, 1e for Mac)
	PATCH_END
};

// startOfDay5::changeState (20h) - when gabriel goes to the phone the script will hang
// Applies to at least: English PC-CD, German PC-CD, English Mac
// Responsible method: startOfDay5::changeState
const uint16 gk1SignatureDay5PhoneFreeze[] = {
	0x4a,
	SIG_MAGICDWORD, SIG_UINT16 + 0x0c, 0x00, // send 0c
	0x35, 0x03,                         // ldi 03
	0x65, SIG_ADDTOOFFSET +1,           // aTop cycles
	0x32, SIG_ADDTOOFFSET +2,           // jmp [end]
	0x3c,                               // dup
	0x35, 0x21,                         // ldi 21
	SIG_END
};

const uint16 gk1PatchDay5PhoneFreeze[] = {
	PATCH_ADDTOOFFSET +3,
	0x35, 0x06,                         // ldi 01
	0x65, PATCH_GETORIGINALBYTEADJUST +6, +6, // aTop ticks
	PATCH_END
};

// Floppy version: Interrogation::dispose() compares an object reference
// (stored in the view selector) with a number, leading to a crash (this kind
// of comparison was not used in SCI32). The view selector is used to store
// both a view number (in some cases), and a view reference (in other cases).
// In the floppy version, the checks are in the wrong order, so there is a
// comparison between a number an an object. In the CD version, the checks are
// in the correct order, thus the comparison is correct, thus we use the code
// from the CD version in the floppy one.
// Applies to at least: English Floppy
// Responsible method: Interrogation::dispose
// TODO: Check, if English Mac is affected too and if this patch applies
const uint16 gk1SignatureInterrogationBug[] = {
	SIG_MAGICDWORD,
	0x65, 0x4c,                      // aTop 4c
	0x67, 0x50,                      // pTos 50
	0x34, SIG_UINT16 + 0x10, 0x27,   // ldi 2710
	0x1e,                            // gt?
	0x31, 0x08,                      // bnt 08  [05a0]
	0x67, 0x50,                      // pTos 50
	0x34, SIG_UINT16 + 0x10, 0x27,   // ldi 2710
	0x04,                            // sub
	0x65, 0x50,                      // aTop 50
	0x63, 0x50,                      // pToa 50
	0x31, 0x15,                      // bnt 15  [05b9]
	0x39, 0x0e,                      // pushi 0e
	0x76,                            // push0
	0x4a, SIG_UINT16 + 0x04, 0x00,   // send 0004
	0xa5, 0x00,                      // sat 00
	0x38, SIG_SELECTOR16 + SELECTOR_dispose, // pushi dispose
	0x76,                            // push0
	0x63, 0x50,                      // pToa 50
	0x4a, SIG_UINT16 + 0x04, 0x00,   // send 0004
	0x85, 0x00,                      // lat 00
	0x65, 0x50,                      // aTop 50
	SIG_END
};

const uint16 gk1PatchInterrogationBug[] = {
	0x65, 0x4c,                      // aTop 4c
	0x63, 0x50,                      // pToa 50
	0x31, 0x15,                      // bnt 15  [05b9]
	0x39, 0x0e,                      // pushi 0e
	0x76,                            // push0
	0x4a, 0x04, 0x00,                // send 0004
	0xa5, 0x00,                      // sat 00
	0x38, SIG_SELECTOR16 + SELECTOR_dispose, // pushi dispose
	0x76,                            // push0
	0x63, 0x50,                      // pToa 50
	0x4a, 0x04, 0x00,                // send 0004
	0x85, 0x00,                      // lat 00
	0x65, 0x50,                      // aTop 50
	0x67, 0x50,                      // pTos 50
	0x34, PATCH_UINT16 + 0x10, 0x27, // ldi 2710
	0x1e,                            // gt?
	0x31, 0x08,                      // bnt 08  [05b9]
	0x67, 0x50,                      // pTos 50
	0x34, PATCH_UINT16 + 0x10, 0x27, // ldi 2710
	0x04,                            // sub
	0x65, 0x50,                      // aTop 50
	PATCH_END
};

//          script, description,                                            signature                     patch
SciScriptPatcherEntry gk1Signatures[] = {
	{  true,    51, "interrogation bug",                           1, 0, 0, gk1SignatureInterrogationBug, gk1PatchInterrogationBug },
	{  true,   212, "day 5 phone freeze",                          1, 0, 0, gk1SignatureDay5PhoneFreeze, gk1PatchDay5PhoneFreeze },
	{  true,   230, "day 6 police beignet timer issue",            1, 0, 0, gk1SignatureDay6PoliceBeignet, gk1PatchDay6PoliceBeignet },
	{  true,   230, "day 6 police sleep timer issue",              1, 0, 0, gk1SignatureDay6PoliceSleep, gk1PatchDay6PoliceSleep },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// at least during harpy scene export 29 of script 0 is called in kq5cd and
//  has an issue for those calls, where temp 3 won't get inititialized, but
//  is later used to set master volume. This issue makes sierra sci set
//  the volume to max. We fix the export, so volume won't get modified in
//  those cases.
const uint16 kq5SignatureCdHarpyVolume[] = {
	SIG_MAGICDWORD,
	0x80, SIG_UINT16 + 0x91, 0x01,   // lag global[191h]
	0x18,                            // not
	0x30, SIG_UINT16 + 0x2c, 0x00,   // bnt [jump further] (jumping, if global 191h is 1)
	0x35, 0x01,                      // ldi 01
	0xa0, SIG_UINT16 + 0x91, 0x01,   // sag global[191h] (setting global 191h to 1)
	0x38, SIG_UINT16 + 0x7b, 0x01,   // pushi 017b
	0x76,                            // push0
	0x81, 0x01,                      // lag global[1]
	0x4a, 0x04,                      // send 04 - read KQ5::masterVolume
	0xa5, 0x03,                      // sat temp[3] (store volume in temp 3)
	0x38, SIG_UINT16 + 0x7b, 0x01,   // pushi 017b
	0x76,                            // push0
	0x81, 0x01,                      // lag global[1]
	0x4a, 0x04,                      // send 04 - read KQ5::masterVolume
	0x36,                            // push
	0x35, 0x04,                      // ldi 04
	0x20,                            // ge? (followed by bnt)
	SIG_END
};

const uint16 kq5PatchCdHarpyVolume[] = {
	0x38, PATCH_UINT16 + 0x2f, 0x02, // pushi 022f (selector theVol) (3 new bytes)
	0x76,                            // push0 (1 new byte)
	0x51, 0x88,                      // class SpeakTimer (2 new bytes)
	0x4a, 0x04,                      // send 04 (2 new bytes) -> read SpeakTimer::theVol
	0xa5, 0x03,                      // sat temp[3] (2 new bytes) -> write to temp 3
	0x80, PATCH_UINT16 + 0x91, 0x01, // lag global[191h]
	// saving 1 byte due optimization
	0x2e, PATCH_UINT16 + 0x23, 0x00, // bt [jump further] (jumping, if global 191h is 1)
	0x35, 0x01,                      // ldi 01
	0xa0, PATCH_UINT16 + 0x91, 0x01, // sag global[191h] (setting global 191h to 1)
	0x38, PATCH_UINT16 + 0x7b, 0x01, // pushi 017b
	0x76,                            // push0
	0x81, 0x01,                      // lag global[1]
	0x4a, 0x04,                      // send 04 - read KQ5::masterVolume
	0xa5, 0x03,                      // sat temp[3] (store volume in temp 3)
	// saving 8 bytes due removing of duplicate code
	0x39, 0x04,                      // pushi 04 (saving 1 byte due swapping)
	0x22,                            // lt? (because we switched values)
	PATCH_END
};

// This is a heap patch, and it modifies the properties of an object, instead
// of patching script code.
//
// The witchCage object in script 200 is broken and claims to have 12
// variables instead of the 8 it should have because it is a Cage.
// Additionally its top,left,bottom,right properties are set to 0 rather
// than the right values. We fix the object by setting the right values.
// If they are all zero, this causes an impossible position check in
// witch::cantBeHere and an infinite loop when entering room 22 (bug #3034714).
//
// This bug is accidentally not triggered in SSCI because the invalid number
// of variables effectively hides witchCage::doit, causing this position check
// to be bypassed entirely.
// See also the warning+comment in Object::initBaseObject
const uint16 kq5SignatureWitchCageInit[] = {
	SIG_UINT16 + 0x00, 0x00,	// top
	SIG_UINT16 + 0x00, 0x00,	// left
	SIG_UINT16 + 0x00, 0x00,	// bottom
	SIG_UINT16 + 0x00, 0x00,	// right
	SIG_UINT16 + 0x00, 0x00,	// extra property #1
	SIG_MAGICDWORD,
	SIG_UINT16 + 0x7a, 0x00,	// extra property #2
	SIG_UINT16 + 0xc8, 0x00,	// extra property #3
	SIG_UINT16 + 0xa3, 0x00,	// extra property #4
	SIG_END
};

const uint16 kq5PatchWitchCageInit[] = {
	PATCH_UINT16 + 0x00, 0x00,	// top
	PATCH_UINT16 + 0x7a, 0x00,	// left
	PATCH_UINT16 + 0xc8, 0x00,	// bottom
	PATCH_UINT16 + 0xa3, 0x00,	// right
	PATCH_END
};


// In the final battle, the DOS version uses signals in the music to handle
// timing, while in the Windows version another method is used and the GM
// tracks do not contain these signals.
// The original kq5 interpreter used global 400 to distinguish between
// Windows (1) and DOS (0) versions.
// We replace the 4 relevant checks for global 400 by a fixed true when
// we use these GM tracks.
//
// Instead, we could have set global 400, but this has the possibly unwanted
// side effects of switching to black&white cursors (which also needs complex
// changes to GameFeatures::detectsetCursorType() ) and breaking savegame
// compatibilty between the DOS and Windows CD versions of KQ5.
// TODO: Investigate these side effects more closely.
const uint16 kq5SignatureWinGMSignals[] = {
	SIG_MAGICDWORD,
	0x80, SIG_UINT16 + 0x90, 0x01,   // lag 0x190
	0x18,                            // not
	0x30, SIG_UINT16 + 0x1b, 0x00,   // bnt +0x001B
	0x89, 0x57,                      // lsg 0x57
	SIG_END
};

const uint16 kq5PatchWinGMSignals[] = {
	0x34, PATCH_UINT16 + 0x01, 0x00, // ldi 0x0001
	PATCH_END
};

//          script, description,                                            signature                  patch
SciScriptPatcherEntry kq5Signatures[] = {
	{  true,     0, "CD: harpy volume change",                     1, 0, 0, kq5SignatureCdHarpyVolume, kq5PatchCdHarpyVolume },
	{  true,   200, "CD: witch cage init",                         1, 0, 0, kq5SignatureWitchCageInit, kq5PatchWitchCageInit },
	{ false,   124, "Win: GM Music signal checks",                 4, 0, 0, kq5SignatureWinGMSignals, kq5PatchWinGMSignals },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// When giving the milk bottle to one of the babies in the garden in KQ6 (room
// 480), script 481 starts a looping baby cry sound. However, that particular
// script also has an overriden check method (cryMusic::check). This method
// explicitly restarts the sound, even if it's set to be looped, thus the same
// sound is played twice, squelching all other sounds. We just rip the
// unnecessary cryMusic::check method out, thereby stopping the sound from
// constantly restarting (since it's being looped anyway), thus the normal
// game speech can work while the baby cry sound is heard. Fixes bug #3034579.
const uint16 kq6SignatureDuplicateBabyCry[] = {
	SIG_MAGICDWORD,
	0x83, 0x00,                      // lal 00
	0x31, 0x1e,                      // bnt 1e  [07f4]
	0x78,                            // push1
	0x39, 0x04,                      // pushi 04
	0x43, 0x75, 0x02,                // callk DoAudio[75] 02
	SIG_END
};

const uint16 kq6PatchDuplicateBabyCry[] = {
	0x48,                            // ret
	PATCH_END
};

// The inventory of King's Quest 6 is buggy. When it grows too large,
//  it will get split into 2 pages. Switching between those pages will
//  grow the stack, because it's calling itself per switch.
// Which means after a while ScummVM will bomb out because the stack frame
//  will be too large. This patch fixes the buggy script.
// Applies to at least: PC-CD, English PC floppy, German PC floppy, English Mac
// Responsible method: KqInv::showSelf
// Fixes bug: #3293954
const uint16 kq6SignatureInventoryStackFix[] = {
	0x67, 0x30,                         // pTos state
	0x34, SIG_UINT16 + 0x00, 0x20,      // ldi 2000
	0x12,                               // and
	0x18,                               // not
	0x31, 0x04,                         // bnt [not first refresh]
	0x35, 0x00,                         // ldi 00
	SIG_MAGICDWORD,
	0x65, 0x1e,                         // aTop curIcon
	0x67, 0x30,                         // pTos state
	0x34, SIG_UINT16 + 0xff, 0xdf,      // ldi dfff
	0x12,                               // and
	0x65, 0x30,                         // aTop state
	0x38, SIG_SELECTOR16 + SELECTOR_show, // pushi "show" ("show" is e1h for KQ6CD)
	0x78,                               // push1
	0x87, 0x00,                         // lap param[0]
	0x31, 0x04,                         // bnt [use global for show]
	0x87, 0x01,                         // lap param[1]
	0x33, 0x02,                         // jmp [use param for show]
	0x81, 0x00,                         // lag global[0]
	0x36,                               // push
	0x54, 0x06,                         // self 06 (KqInv::show)
	0x31, SIG_ADDTOOFFSET + 1,          // bnt [exit menu code] (0x08 for PC, 0x07 for mac)
	0x39, 0x39,                         // pushi 39
	0x76,                               // push0
	0x54, 0x04,                         // self 04 (KqInv::doit)
	SIG_END                             // followed by jmp (0x32 for PC, 0x33 for mac)
};

const uint16 kq6PatchInventoryStackFix[] = {
	0x67, 0x30,                         // pTos state
	0x3c,                               // dup (1 more byte, needed for patch)
	0x3c,                               // dup (1 more byte, saves 1 byte later)
	0x34, PATCH_UINT16 + 0x00, 0x20,    // ldi 2000
	0x12,                               // and
	0x2f, 0x02,                         // bt [not first refresh] - saves 3 bytes in total
	0x65, 0x1e,                         // aTop curIcon
	0x00,                               // neg (either 2000 or 0000 in acc, this will create dfff or ffff) - saves 2 bytes
	0x12,                               // and
	0x65, 0x30,                         // aTop state
	0x38,                               // pushi "show"
	PATCH_GETORIGINALBYTE +22,
	PATCH_GETORIGINALBYTE +23,
	0x78,                               // push1
	0x87, 0x00,                         // lap param[0]
	0x31, 0x04,                         // bnt [call show using global 0]
	0x8f, 0x01,                         // lsp param[1], save 1 byte total with lsg global[0] combined
	0x33, 0x02,                         // jmp [call show using param 1]
	0x89, 0x00,                         // lsg global[0], save 1 byte total, see above
	0x54, 0x06,                         // self 06 (call x::show)
	0x31,                               // bnt [menu exit code]
	PATCH_GETORIGINALBYTEADJUST +39, +6,// dynamic offset must be 0x0E for PC and 0x0D for mac
	0x34, PATCH_UINT16 + 0x00, 0x20,    // ldi 2000
	0x12,                               // and
	0x2f, 0x05,                         // bt [to return]
	0x39, 0x39,                         // pushi 39
	0x76,                               // push0
	0x54, 0x04,                         // self 04 (self::doit)
	0x48,                               // ret (saves 2 bytes for PC, 1 byte for mac)
	PATCH_END
};

// Audio + subtitles support - SHARED! - used for King's Quest 6 and Laura Bow 2
//  this patch gets enabled, when the user selects "both" in the ScummVM "Speech + Subtitles" menu
//  We currently use global 98d to hold a kMemory pointer.
// Patched method: Messager::sayNext / lb2Messager::sayNext (always use text branch)
const uint16 kq6laurabow2CDSignatureAudioTextSupport1[] = {
	0x89, 0x5a,                         // lsg global[5a]
	0x35, 0x02,                         // ldi 02
	0x12,                               // and
	SIG_MAGICDWORD,
	0x31, 0x13,                         // bnt [audio call]
	0x38, SIG_SELECTOR16 + SELECTOR_modNum, // pushi modNum
	SIG_END
};

const uint16 kq6laurabow2CDPatchAudioTextSupport1[] = {
	PATCH_ADDTOOFFSET +5,
	0x33, 0x13,                         // jmp [audio call]
	PATCH_END
};

// Patched method: Messager::sayNext / lb2Messager::sayNext (allocate audio memory)
const uint16 kq6laurabow2CDSignatureAudioTextSupport2[] = {
	0x7a,                               // push2
	0x78,                               // push1
	0x39, 0x0c,                         // pushi 0c
	0x43, SIG_MAGICDWORD, 0x72, 0x04,   // kMemory
	0xa5, 0xc9,                         // sat global[c9]
	SIG_END
};

const uint16 kq6laurabow2CDPatchAudioTextSupport2[] = {
	PATCH_ADDTOOFFSET +7,
	0xa1, 98,                           // sag global[98d]
	PATCH_END
};

// Patched method: Messager::sayNext / lb2Messager::sayNext (release audio memory)
const uint16 kq6laurabow2CDSignatureAudioTextSupport3[] = {
	0x7a,                               // push2
	0x39, 0x03,                         // pushi 03
	SIG_MAGICDWORD,
	0x8d, 0xc9,                         // lst temp[c9]
	0x43, 0x72, 0x04,                   // kMemory
	SIG_END
};

const uint16 kq6laurabow2CDPatchAudioTextSupport3[] = {
	PATCH_ADDTOOFFSET +3,
	0x89, 98,                           // lsg global[98d]
	PATCH_END
};

// Patched method: Narrator::say (use audio memory)
const uint16 kq6laurabow2CDSignatureAudioTextSupport4[] = {
	0x89, 0x5a,                         // lsg global[5a]
	0x35, 0x01,                         // ldi 01
	0x12,                               // and
	0x31, 0x08,                         // bnt [skip code]
	0x38, SIG_SELECTOR16 + SELECTOR_startText, // pushi startText
	0x78,                               // push1
	0x8f, 0x01,                         // lsp param[1]
	0x54, 0x06,                         // self 06
	0x89, 0x5a,                         // lsg global[5a]
	0x35, 0x02,                         // ldi 02
	0x12,                               // and
	0x31, 0x08,                         // bnt [skip code]
	SIG_MAGICDWORD,
	0x38, SIG_SELECTOR16 + SELECTOR_startAudio, // pushi startAudio
	0x78,                               // push1
	0x8f, 0x01,                         // lsp param[1]
	0x54, 0x06,                         // self 06
	SIG_END
};

const uint16 kq6laurabow2CDPatchAudioTextSupport4[] = {
	PATCH_ADDTOOFFSET +5,
	0x18,                               // not (never jump here)
	0x18,                               // not (never jump here)
	PATCH_ADDTOOFFSET +19,
	0x89, 98,                           // lsp global[98d]
	PATCH_END
};

// Patched method: Talker::display/Narrator::say (remove reset saved mouse cursor code)
//  code would screw over mouse cursor
const uint16 kq6laurabow2CDSignatureAudioTextSupport5[] = {
	SIG_MAGICDWORD,
	0x35, 0x00,                         // ldi 00
	0x65, 0x82,                         // aTop saveCursor
	SIG_END
};

const uint16 kq6laurabow2CDPatchAudioTextSupport5[] = {
	0x18, 0x18, 0x18, 0x18,             // waste bytes, do nothing
	PATCH_END
};

//          script, description,                                            signature                     patch
SciScriptPatcherEntry kq6Signatures[] = {
	{  true,   481, "duplicate baby cry",                          1, 0, 0, kq6SignatureDuplicateBabyCry, kq6PatchDuplicateBabyCry },
	{  true,   907, "inventory stack fix",                         1, 0, 0, kq6SignatureInventoryStackFix, kq6PatchInventoryStackFix },
	// King's Quest 6 and Laura Bow 2 share basic patches for audio + text support
	// *** King's Quest 6 audio + text support - CURRENTLY DISABLED ***
	//  TODO: fix window placement (currently part of the text windows go off-screen)
	//  TODO: fix hi-res portraits mode graphic glitches
	{ false,   924, "CD: audio + text support 1",                  1, 0, 0, kq6laurabow2CDSignatureAudioTextSupport1, kq6laurabow2CDPatchAudioTextSupport1 },
	{ false,   924, "CD: audio + text support 2",                  1, 0, 0, kq6laurabow2CDSignatureAudioTextSupport2, kq6laurabow2CDPatchAudioTextSupport2 },
	{ false,   924, "CD: audio + text support 3",                  1, 0, 0, kq6laurabow2CDSignatureAudioTextSupport3, kq6laurabow2CDPatchAudioTextSupport3 },
	{ false,   928, "CD: audio + text support 4",                  1, 0, 0, kq6laurabow2CDSignatureAudioTextSupport4, kq6laurabow2CDPatchAudioTextSupport4 },
	{ false,   928, "CD: audio + text support 5",                  2, 0, 0, kq6laurabow2CDSignatureAudioTextSupport5, kq6laurabow2CDPatchAudioTextSupport5 },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// Script 210 in the German version of Longbow handles the case where Robin
// hands out the scroll to Marion and then types his name using the hand code.
// The German version script contains a typo (probably a copy/paste error),
// and the function that is used to show each letter is called twice. The
// second time that the function is called, the second parameter passed to
// the function is undefined, thus kStrCat() that is called inside the function
// reads a random pointer and crashes. We patch all of the 5 function calls
// (one for each letter typed from "R", "O", "B", "I", "N") so that they are
// the same as the English version.
// Applies to at least: German floppy
// Responsible method: unknown
// Fixes bug: #3048054
const uint16 longbowSignatureShowHandCode[] = {
	0x78,                            // push1
	0x78,                            // push1
	0x72, SIG_ADDTOOFFSET +2,        // lofsa (letter, that was typed)
	0x36,                            // push
	0x40, SIG_ADDTOOFFSET +2,        // call
	0x02,                            // perform the call above with 2 parameters
	0x36,                            // push
	0x40, SIG_ADDTOOFFSET +2,        // call
	SIG_MAGICDWORD,
	0x02,                            // perform the call above with 2 parameters
	0x38, SIG_SELECTOR16 + SELECTOR_setMotion, // pushi "setMotion" (0x11c in Longbow German)
	0x39, SIG_SELECTOR8 + SELECTOR_x, // pushi "x" (0x04 in Longbow German)
	0x51, 0x1e,                      // class MoveTo
	SIG_END
};

const uint16 longbowPatchShowHandCode[] = {
	0x39, 0x01,                      // pushi 1 (combine the two push1's in one, like in the English version)
	PATCH_ADDTOOFFSET +3,            // leave the lofsa call untouched
	// The following will remove the duplicate call
	0x32, PATCH_UINT16 + 0x02, 0x00, // jmp 02 - skip 2 bytes (the remainder of the first call)
	0x48,                            // ret (dummy, should never be reached)
	0x48,                            // ret (dummy, should never be reached)
	PATCH_END
};

//          script, description,                                            signature                     patch
SciScriptPatcherEntry longbowSignatures[] = {
	{  true,   210, "hand code crash",                             5, 0, 0, longbowSignatureShowHandCode, longbowPatchShowHandCode },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// Leisure Suit Larry 2
// On the plane, Larry is able to wear the parachute. This grants 4 points.
// In early versions of LSL2, it was possible to get "unlimited" points by
//  simply wearing it multiple times.
// They fixed it in later versions by remembering, if the parachute was already
//  used before.
// But instead of adding it properly, it seems they hacked the script / forgot
//  to replace script 0 as well, which holds information about how many global
//  variables are allocated at the start of the game.
// The script tries to read an out-of-bounds global variable, which somewhat
//  "worked" in SSCI, but ScummVM/SCI doesn't allow that.
// That's why those points weren't granted here at all.
// We patch the script to use global 90, which seems to be unused in the whole game.
// Applies to at least: English floppy
// Responsible method: rm63Script::handleEvent
// Fixes bug: #3614419
const uint16 larry2SignatureWearParachutePoints[] = {
	0x35, 0x01,                      // ldi 01
	0xa1, SIG_MAGICDWORD, 0x8e,      // sag 8e
	0x80, SIG_UINT16 + 0xe0, 0x01,   // lag 1e0
	0x18,                            // not
	0x30, SIG_UINT16 + 0x0f, 0x00,   // bnt [don't give points]
	0x35, 0x01,                      // ldi 01
	0xa0, 0xe0, 0x01,                // sag 1e0
	SIG_END
};

const uint16 larry2PatchWearParachutePoints[] = {
	PATCH_ADDTOOFFSET +4,
	0x80, PATCH_UINT16 + 0x5a, 0x00, // lag 5a (global 90)
	PATCH_ADDTOOFFSET +6,
	0xa0, PATCH_UINT16 + 0x5a, 0x00, // sag 5a (global 90)
	PATCH_END
};

//          script, description,                                            signature                           patch
SciScriptPatcherEntry larry2Signatures[] = {
	{  true,    63, "plane: no points for wearing plane",          1, 0, 0, larry2SignatureWearParachutePoints, larry2PatchWearParachutePoints },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// Leisure Suit Larry 5
// In one of the conversations near the end (to be exact - room 380 and the text
//  about using champagne on Reverse Biaz - only used when you actually did that
//  in the game), the German text is too large, causing the textbox to get too large.
// Because of that the talking head of Patti is drawn over the textbox. A translation oversight.
// Applies to at least: German floppy
// Responsible method: none, position of talker object on screen needs to get modified
const uint16 larry5SignatureGermanEndingPattiTalker[] = {
	SIG_MAGICDWORD,
	SIG_UINT16 + 0x6e, 0x00,            // object pattiTalker::x (110)
	SIG_UINT16 + 0xb4, 0x00,            // object pattiTalker::y (180)
	SIG_ADDTOOFFSET + 469,              // verify that it's really the German version
	0x59, 0x6f, 0x75,                   // (object name) "You"
	0x23, 0x47, 0x44, 0x75,             // "#GDu"
	SIG_END
};

const uint16 larry5PatchGermanEndingPattiTalker[] = {
	PATCH_UINT16 + 0x5a, 0x00,          // change pattiTalker::x to 90
	PATCH_END
};

//          script, description,                                            signature                               patch
SciScriptPatcherEntry larry5Signatures[] = {
	{  true,   380, "German-only: Enlarge Patti Textbox",          1, 0, 0, larry5SignatureGermanEndingPattiTalker, larry5PatchGermanEndingPattiTalker },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// this is called on every death dialog. Problem is at least the german
//  version of lsl6 gets title text that is far too long for the
//  available temp space resulting in temp space corruption
//  This patch moves the title text around, so this overflow
//  doesn't happen anymore. We would otherwise get a crash
//  calling for invalid views (this happens of course also
//  in sierra sci)
// Applies to at least: German PC-CD
// Responsible method: unknown
const uint16 larry6SignatureDeathDialog[] = {
	SIG_MAGICDWORD,
	0x3e, SIG_UINT16 + 0x33, 0x01,   // link 0133 (offset 0x20)
	0x35, 0xff,                      // ldi ff
	0xa3, 0x00,                      // sal 00
	SIG_ADDTOOFFSET +680,            // [skip 680 bytes]
	0x8f, 0x01,                      // lsp 01 (offset 0x2cf)
	0x7a,                            // push2
	0x5a, SIG_UINT16 + 0x04, 0x00, SIG_UINT16 + 0x0e, 0x01, // lea 0004 010e
	0x36,                            // push
	0x43, 0x7c, 0x0e,                // kMessage[7c] 0e
	SIG_ADDTOOFFSET +90,             // [skip 90 bytes]
	0x38, SIG_UINT16 + 0xd6, 0x00,   // pushi 00d6 (offset 0x335)
	0x78,                            // push1
	0x5a, SIG_UINT16 + 0x04, 0x00, SIG_UINT16 + 0x0e, 0x01, // lea 0004 010e
	0x36,                            // push
	SIG_ADDTOOFFSET +76,             // [skip 76 bytes]
	0x38, SIG_UINT16 + 0xcd, 0x00,   // pushi 00cd (offset 0x38b)
	0x39, 0x03,                      // pushi 03
	0x5a, SIG_UINT16 + 0x04, 0x00, SIG_UINT16 + 0x0e, 0x01, // lea 0004 010e
	0x36,
	SIG_END
};

const uint16 larry6PatchDeathDialog[] = {
	0x3e, 0x00, 0x02,                // link 0200
	PATCH_ADDTOOFFSET +687,
	0x5a, PATCH_UINT16 + 0x04, 0x00, PATCH_UINT16 + 0x40, 0x01, // lea 0004 0140
	PATCH_ADDTOOFFSET +98,
	0x5a, PATCH_UINT16 + 0x04, 0x00, PATCH_UINT16 + 0x40, 0x01, // lea 0004 0140
	PATCH_ADDTOOFFSET +82,
	0x5a, PATCH_UINT16 + 0x04, 0x00, PATCH_UINT16 + 0x40, 0x01, // lea 0004 0140
	PATCH_END
};

//          script, description,                                            signature                   patch
SciScriptPatcherEntry larry6Signatures[] = {
	{  true,    82, "death dialog memory corruption",              1, 0, 0, larry6SignatureDeathDialog, larry6PatchDeathDialog },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// Laura Bow 2
// 
// Moving away the painting in the room with the hidden safe is problematic
//  for the CD version of the game. safePic::doVerb gets triggered by the mouse-click.
// This method sets local 0 as signal, which is only meant to get handled, when
//  the player clicks again to move the painting back. This signal is processed by
//  the room doit-script.
// That doit-script checks safePic::cel to be not equal 0 and would then skip over
//  the "close painting" trigger code. On very fast computers this script may
//  get called too early (which is the case when running under ScummVM and when
//  running the game using Sierra SCI in DOS-Box with cycles 15000) and thinks
//  that it's supposed to move the painting back. Which then results in the painting
//  getting moved to its original position immediately (which means it won't be possible
//  to access the safe behind it).
//
// We patch the script, so that we check for cel to be not equal 4 (the final cel) and
//  we also reset the safePic-signal immediately as well.
//
// In the floppy version Laura's coordinates are checked directly in rm560::doit
//  and as soon as she moves, the painting will automatically move to its original position.
//  This is not the case for the CD version of the game. The painting will only "move" back,
//  when the player actually exits the room and re-enters.
//
// Applies to at least: English PC-CD
// Responsible method: rm560::doit
// Fixes bug: #6460
const uint16 laurabow2CDSignaturePaintingClosing[] = {
	0x39, 0x04,                         // pushi 04 (cel)
	0x76,                               // push0
	SIG_MAGICDWORD,
	0x7a,                               // push2
	0x38, SIG_UINT16 + 0x31, 0x02,      // pushi 0231h (561)
	0x76,                               // push0
	0x43, 0x02, 0x04,                   // kScriptID (get export 0 of script 561)
	0x4a, 0x04,                         // send 04 (gets safePicture::cel)
	0x18,                               // not
	0x31, 0x21,                         // bnt [exit]
	0x38, SIG_UINT16 + 0x83, 0x02,      // pushi 0283h
	0x76,                               // push0
	0x7a,                               // push2
	0x39, 0x20,                         // pushi 20
	0x76,                               // push0
	0x43, 0x02, 0x04,                   // kScriptID (get export 0 of script 32)
	0x4a, 0x04,                         // send 04 (get sHeimlich::room)
	0x36,                               // push
	0x81, 0x0b,                         // lag global[b] (current room)
	0x1c,                               // ne?
	0x31, 0x0e,                         // bnt [exit]
	0x35, 0x00,                         // ldi 00
	0xa3, 0x00,                         // sal local[0] -> reset safePic signal
	SIG_END
};

const uint16 laurabow2CDPatchPaintingClosing[] = {
	PATCH_ADDTOOFFSET +2,
	0x3c,                               // dup (1 additional byte)
	0x76,                               // push0
	0x3c,                               // dup (1 additional byte)
	0xab, 0x00,                         // ssl local[0] -> reset safePic signal
	0x7a,                               // push2
	0x38, PATCH_UINT16 + 0x31, 0x02,    // pushi 0231h (561)
	0x76,                               // push0
	0x43, 0x02, 0x04,                   // kScriptID (get export 0 of script 561)
	0x4a, 0x04,                         // send 04 (gets safePicture::cel)
	0x1a,                               // eq?
	0x31, 0x1d,                         // bnt [exit]
	0x38, PATCH_UINT16 + 0x83, 0x02,    // pushi 0283h
	0x76,                               // push0
	0x7a,                               // push2
	0x39, 0x20,                         // pushi 20
	0x76,                               // push0
	0x43, 0x02, 0x04,                   // kScriptID (get export 0 of script 32)
	0x4a, 0x04,                         // send 04 (get sHeimlich::room)
	0x36,                               // push
	0x81, 0x0b,                         // lag global[b] (current room)
	0x1a,                               // eq? (2 opcodes changed, to save 2 bytes)
	0x2f, 0x0a,                         // bt [exit]
	PATCH_END
};

// In the CD version the system menu is disabled for certain rooms. LB2::handsOff is called,
//  when leaving the room (and in other cases as well). This method remembers the disabled
//  icons of the icon bar. In the new room LB2::handsOn will get called, which then enables
//  all icons, but also disabled the ones, that were disabled before.
//
// Because of this behaviour certain rooms, that should have the system menu enabled, have
//  it disabled, when entering those rooms from rooms, where the menu is supposed to be
//  disabled.
//
// We patch this by injecting code into LB2::newRoom (which is called right after a room change)
//  and reset the global variable there, that normally holds the disabled buttons.
//
// This patch may cause side-effects and it's difficult to test, because it affects every room
//  in the game. At least for the intro, the speakeasy and plenty of rooms in the beginning it
//  seems to work correctly.
//
// Applies to at least: English PC-CD
// Responsible method: LB2::newRoom, LB2::handsOff, LB2::handsOn
// Fixes bug: #6440
const uint16 laurabow2CDSignatureFixProblematicIconBar[] = {
	SIG_MAGICDWORD,
	0x38, SIG_UINT16 + 0xf1, 0x00,      // pushi 00f1 (disable) - hardcoded, we only want to patch the CD version
	0x76,                               // push0
	0x81, 0x45,                         // lag global[45]
	0x4a, 0x04,                         // send 04
	SIG_END
};

const uint16 laurabow2CDPatchFixProblematicIconBar[] = {
	0x35, 0x00,                      // ldi 00
	0xa1, 0x74,                      // sag 74h
	0x35, 0x00,                      // ldi 00 (waste bytes)
	0x35, 0x00,                      // ldi 00
	PATCH_END
};


//          script, description,                                            signature                                  patch
SciScriptPatcherEntry laurabow2Signatures[] = {
	{  true,   560, "CD: painting closing immediately",            1, 0, 0, laurabow2CDSignaturePaintingClosing,       laurabow2CDPatchPaintingClosing },
	{  true,     0, "CD: fix problematic icon bar",                1, 0, 0, laurabow2CDSignatureFixProblematicIconBar, laurabow2CDPatchFixProblematicIconBar },
	// King's Quest 6 and Laura Bow 2 share basic patches for audio + text support
	{ false,   924, "CD: audio + text support 1",                  1, 0, 0, kq6laurabow2CDSignatureAudioTextSupport1,  kq6laurabow2CDPatchAudioTextSupport1 },
	{ false,   924, "CD: audio + text support 2",                  1, 0, 0, kq6laurabow2CDSignatureAudioTextSupport2,  kq6laurabow2CDPatchAudioTextSupport2 },
	{ false,   924, "CD: audio + text support 3",                  1, 0, 0, kq6laurabow2CDSignatureAudioTextSupport3,  kq6laurabow2CDPatchAudioTextSupport3 },
	{ false,   928, "CD: audio + text support 4",                  1, 0, 0, kq6laurabow2CDSignatureAudioTextSupport4,  kq6laurabow2CDPatchAudioTextSupport4 },
	{ false,   928, "CD: audio + text support 5",                  2, 0, 0, kq6laurabow2CDSignatureAudioTextSupport5,  kq6laurabow2CDPatchAudioTextSupport5 },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// Mother Goose SCI1/SCI1.1
// MG::replay somewhat calculates the savedgame-id used when saving again
//  this doesn't work right and we remove the code completely.
//  We set the savedgame-id directly right after restoring in kRestoreGame.
const uint16 mothergoose256SignatureReplay[] = {
	0x36,                            // push
	0x35, SIG_MAGICDWORD, 0x20,      // ldi 20
	0x04,                            // sub
	0xa1, 0xb3,                      // sag global[b3]
	SIG_END
};

const uint16 mothergoose256PatchReplay[] = {
	0x34, PATCH_UINT16 + 0x00, 0x00, // ldi 0000 (dummy)
	0x34, PATCH_UINT16 + 0x00, 0x00, // ldi 0000 (dummy)
	PATCH_END
};

// when saving, it also checks if the savegame ID is below 13.
//  we change this to check if below 113 instead
const uint16 mothergoose256SignatureSaveLimit[] = {
	0x89, SIG_MAGICDWORD, 0xb3,      // lsg global[b3]
	0x35, 0x0d,                      // ldi 0d
	0x20,                            // ge?
	SIG_END
};

const uint16 mothergoose256PatchSaveLimit[] = {
	PATCH_ADDTOOFFSET | +2,
	0x35, 0x0d + SAVEGAMEID_OFFICIALRANGE_START, // ldi 113d
	PATCH_END
};

//          script, description,                                            signature                         patch
SciScriptPatcherEntry mothergoose256Signatures[] = {
	{  true,     0, "replay save issue",                           1, 0, 0, mothergoose256SignatureReplay,    mothergoose256PatchReplay },
	{  true,     0, "save limit dialog (SCI1.1)",                  1, 0, 0, mothergoose256SignatureSaveLimit, mothergoose256PatchSaveLimit },
	{  true,   994, "save limit dialog (SCI1)",                    1, 0, 0, mothergoose256SignatureSaveLimit, mothergoose256PatchSaveLimit },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// Police Quest 1 VGA
// When at the police station, you can put or get your gun from your locker.
// The script, that handles this, is buggy. It disposes the gun as soon as
//  you click, but then waits 2 seconds before it also closes the locker.
// Problem is that it's possible to click again, which then results in a
//  disposed object getting accessed. This happened to work by pure luck in
//  SSCI.
// This patch changes the code, so that the gun is actually given away
//  when the 2 seconds have passed and the locker got closed.
// Applies to at least: English floppy
// Responsible method: putGun::changeState (script 341)
// Fixes bug: #3036933 / #3303802
const uint16 pq1vgaSignaturePutGunInLockerBug[] = {
	0x35, 0x00,                      // ldi 00
	0x1a,                            // eq?
	0x31, 0x25,                      // bnt [next state check]
	SIG_ADDTOOFFSET +22,             // [skip 22 bytes]
	SIG_MAGICDWORD,
	0x38, SIG_SELECTOR16 + SELECTOR_put, // pushi "put"
	0x78,                            // push1
	0x76,                            // push0
	0x81, 0x00,                      // lag 00
	0x4a, 0x06,                      // send 06 - ego::put(0)
	0x35, 0x02,                      // ldi 02
	0x65, 0x1c,                      // aTop 1c (set timer to 2 seconds)
	0x33, 0x0e,                      // jmp [end of method]
	0x3c,                            // dup --- next state check target
	0x35, 0x01,                      // ldi 01
	0x1a,                            // eq?
	0x31, 0x08,                      // bnt [end of method]
	0x39, SIG_SELECTOR8 + SELECTOR_dispose, // pushi "dispose"
	0x76,                            // push0
	0x72, SIG_UINT16 + 0x88, 0x00,   // lofsa 0088
	0x4a, 0x04,                      // send 04 - locker::dispose
	SIG_END
};

const uint16 pq1vgaPatchPutGunInLockerBug[] = {
	PATCH_ADDTOOFFSET +3,
	0x31, 0x1c,                      // bnt [next state check]
	PATCH_ADDTOOFFSET +22,
	0x35, 0x02,                      // ldi 02
	0x65, 0x1c,                      // aTop 1c (set timer to 2 seconds)
	0x33, 0x17,                      // jmp [end of method]
	0x3c,                            // dup --- next state check target
	0x35, 0x01,                      // ldi 01
	0x1a,                            // eq?
	0x31, 0x11,                      // bnt [end of method]
	0x38, PATCH_SELECTOR16 + SELECTOR_put, // pushi "put"
	0x78,                            // push1
	0x76,                            // push0
	0x81, 0x00,                      // lag 00
	0x4a, 0x06,                      // send 06 - ego::put(0)
	PATCH_END
};

//          script, description,                                            signature                         patch
SciScriptPatcherEntry pq1vgaSignatures[] = {
	{  true,   341, "put gun in locker bug",                       1, 0, 0, pq1vgaSignaturePutGunInLockerBug, pq1vgaPatchPutGunInLockerBug },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
//  script 215 of qfg1vga pointBox::doit actually processes button-presses
//   during fighting with monsters. It strangely also calls kGetEvent. Because
//   the main User::doit also calls kGetEvent it's pure luck, where the event
//   will hit. It's the same issue as in freddy pharkas and if you turn dos-box
//   to max cycles, sometimes clicks also won't get registered. Strangely it's
//   not nearly as bad as in our sci, but these differences may be caused by
//   timing.
//   We just reuse the active event, thus removing the duplicate kGetEvent call.
// Applies to at least: English floppy
// Responsible method: pointBox::doit
const uint16 qfg1vgaSignatureFightEvents[] = {
	0x39, SIG_MAGICDWORD,
	SIG_SELECTOR8 + SELECTOR_new,       // pushi "new"
	0x76,                               // push0
	0x51, 0x07,                         // class Event
	0x4a, 0x04,                         // send 04 - call Event::new
	0xa5, 0x00,                         // sat temp[0]
	0x78,                               // push1
	0x76,                               // push0
	0x4a, 0x04,                         // send 04 - read Event::x
	0xa5, 0x03,                         // sat temp[3]
	0x76,                               // push0 (selector y)
	0x76,                               // push0
	0x85, 0x00,                         // lat temp[0]
	0x4a, 0x04,                         // send 04 - read Event::y
	0x36,                               // push
	0x35, 0x0a,                         // ldi 0a
	0x04,                               // sub (poor mans localization) ;-)
	SIG_END
};

const uint16 qfg1vgaPatchFightEvents[] = {
	0x38, PATCH_SELECTOR16 + SELECTOR_curEvent, // pushi 15a (selector curEvent)
	0x76,                            // push0
	0x81, 0x50,                      // lag global[50]
	0x4a, 0x04,                      // send 04 - read User::curEvent -> needs one byte more than previous code
	0xa5, 0x00,                      // sat temp[0]
	0x78,                            // push1
	0x76,                            // push0
	0x4a, 0x04,                      // send 04 - read Event::x
	0xa5, 0x03,                      // sat temp[3]
	0x76,                            // push0 (selector y)
	0x76,                            // push0
	0x85, 0x00,                      // lat temp[0]
	0x4a, 0x04,                      // send 04 - read Event::y
	0x39, 0x00,                      // pushi 00
	0x02,                            // add (waste 3 bytes) - we don't need localization, User::doit has already done it
	PATCH_END
};

// Script 814 of QFG1VGA is responsible for showing dialogs. However, the death
// screen message shown when the hero dies in room 64 (ghost room) is too large
// (254 chars long). Since the window header and main text are both stored in
// temp space, this is an issue, as the scripts read the window header, then the
// window text, which erases the window header text because of its length. To
// fix that, we allocate more temp space and move the pointer used for the
// window header a little bit, wherever it's used in script 814.
// Fixes bug #3568431.

// Patch 1: Increase temp space
const uint16 qfg1vgaSignatureTempSpace[] = {
	SIG_MAGICDWORD,
	0x3f, 0xba,                         // link 0xba
	0x87, 0x00,                         // lap 0
	SIG_END
};

const uint16 qfg1vgaPatchTempSpace[] = {
	0x3f, 0xca,                         // link 0xca
	PATCH_END
};

// Patch 2: Move the pointer used for the window header a little bit
const uint16 qfg1vgaSignatureDialogHeader[] = {
	SIG_MAGICDWORD,
	0x5b, 0x04, 0x80,                   // lea temp[0x80]
	0x36,                               // push
	SIG_END
};

const uint16 qfg1vgaPatchDialogHeader[] = {
	0x5b, 0x04, 0x90,                   // lea temp[0x90]
	PATCH_END
};

// When clicking on the crusher in room 331, Ego approaches him to talk to him,
// an action that is handled by moveToCrusher::changeState in script 331. The
// scripts set Ego to move close to the crusher, but when Ego is sneaking instead
// of walking, the target coordinates specified by script 331 are never reached,
// as Ego is making larger steps, and never reaches the required spot. This is an
// edge case that can occur when Ego is set to sneak. Normally, when clicking on
// the crusher, ego is supposed to move close to position 79, 165. We change it
// to 85, 165, which is not an edge case thus the freeze is avoided.
// Fixes bug #3585189.
const uint16 qfg1vgaSignatureMoveToCrusher[] = {
	SIG_MAGICDWORD,
	0x51, 0x1f,                         // class Motion
	0x36,                               // push
	0x39, 0x4f,                         // pushi 4f (79 - x)
	0x38, SIG_UINT16 + 0xa5, 0x00,      // pushi 00a5 (165 - y)
	0x7c,                               // pushSelf
	SIG_END
};

const uint16 qfg1vgaPatchMoveToCrusher[] = {
	PATCH_ADDTOOFFSET +3,
	0x39, 0x55,                         // pushi 55 (85 - x)
	PATCH_END
};

// Same pathfinding bug as above, where Ego is set to move to an impossible
// spot when sneaking. In GuardsTrumpet::changeState, we change the final
// location where Ego is moved from 111, 111 to 114, 114. Fixes bug #3604939.
const uint16 qfg1vgaSignatureMoveToCastleGate[] = {
	SIG_MAGICDWORD,
	0x51, 0x1f,                         // class MoveTo
	0x36,                               // push
	0x39, 0x6f,                         // pushi 6f (111 - x)
	0x3c,                               // dup (111 - y)
	0x7c,                               // pushSelf
	SIG_END
};

const uint16 qfg1vgaPatchMoveToCastleGate[] = {
	PATCH_ADDTOOFFSET +3,
	0x39, 0x72,                         // pushi 72 (114 - x)
	PATCH_END
};

// Typo in the original Sierra scripts
// Looking at a cheetaur resulted in a text about a Saurus Rex
// The code treats both monster types the same.
// Applies to at least: English floppy
// Responsible method: smallMonster::doVerb
// Fixes bug #3604943.
const uint16 qfg1vgaSignatureCheetaurDescription[] = {
	SIG_MAGICDWORD,
	0x34, SIG_UINT16 + 0xb8, 0x01,      // ldi 01b8
	0x1a,                               // eq?
	0x31, 0x16,                         // bnt 16
	0x38, SIG_UINT16 + 0x27, 0x01,      // pushi 0127
	0x39, 0x06,                         // pushi 06
	0x39, 0x03,                         // pushi 03
	0x78,                               // push1
	0x39, 0x12,                         // pushi 12 -> monster type Saurus Rex
	SIG_END
};

const uint16 qfg1vgaPatchCheetaurDescription[] = {
	PATCH_ADDTOOFFSET +14,
	0x39, 0x11,                         // pushi 11 -> monster type cheetaur
	PATCH_END
};

// In the "funny" room (Yorick's room) in QfG1 VGA, pulling the chain and
//  then pressing the button on the right side of the room results in
//  a broken game. This also happens in SSCI.
// Problem is that the Sierra programmers forgot to disable the door, that
//  gets opened by pulling the chain. So when ego falls down and then
//  rolls through the door, one method thinks that the player walks through
//  it and acts that way and the other method is still doing the roll animation.
// Local 5 of that room is a timer, that closes the door (object door11).
// Setting it to 1 during happyFace::changeState(0) stops door11::doit from
//  calling goTo6::init, so the whole issue is stopped from happening.
// Applies to at least: English floppy
// Responsible method: happyFace::changeState, door11::doit
// Fixes bug #3585793
const uint16 qfg1vgaSignatureFunnyRoomFix[] = {
	0x65, 0x14,                         // aTop 14 (state)
	0x36,                               // push
	0x3c,                               // dup
	0x35, 0x00,                         // ldi 00
	0x1a,                               // eq?
	0x30, SIG_UINT16 + 0x25, 0x00,      // bnt 0025 [-> next state]
	SIG_MAGICDWORD,
	0x35, 0x01,                         // ldi 01
	0xa3, 0x4e,                         // sal 4e
	SIG_END
};

const uint16 qfg1vgaPatchFunnyRoomFix[] = {
	PATCH_ADDTOOFFSET +3,
	0x2e, PATCH_UINT16 + 0x29, 0x00,    // bt 0029 [-> next state] - saves 4 bytes
	0x35, 0x01,                         // ldi 01
	0xa3, 0x4e,                         // sal 4e
	0xa3, 0x05,                         // sal 05 (sets local 5 to 1)
	0xa3, 0x05,                         // and again to make absolutely sure (actually to waste 2 bytes)
	PATCH_END
};

//          script, description,                                            signature                            patch
SciScriptPatcherEntry qfg1vgaSignatures[] = {
	{  true,   215, "fight event issue",                           1, 0, 0, qfg1vgaSignatureFightEvents,         qfg1vgaPatchFightEvents },
	{  true,   216, "weapon master event issue",                   1, 0, 0, qfg1vgaSignatureFightEvents,         qfg1vgaPatchFightEvents },
	{  true,   814, "window text temp space",                      1, 0, 0, qfg1vgaSignatureTempSpace,           qfg1vgaPatchTempSpace },
	{  true,   814, "dialog header offset",                        3, 0, 0, qfg1vgaSignatureDialogHeader,        qfg1vgaPatchDialogHeader },
	{  true,   331, "moving to crusher",                           1, 0, 0, qfg1vgaSignatureMoveToCrusher,       qfg1vgaPatchMoveToCrusher },
	{  true,    41, "moving to castle gate",                       1, 0, 0, qfg1vgaSignatureMoveToCastleGate,    qfg1vgaPatchMoveToCastleGate },
	{  true,   210, "cheetaur description fixed",                  1, 0, 0, qfg1vgaSignatureCheetaurDescription, qfg1vgaPatchCheetaurDescription },
	{  true,    96, "funny room script bug fixed",                 1, 0, 0, qfg1vgaSignatureFunnyRoomFix,        qfg1vgaPatchFunnyRoomFix },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// Script 944 in QFG2 contains the FileSelector system class, used in the
// character import screen. This gets incorrectly called constantly, whenever
// the user clicks on a button in order to refresh the file list. This was
// probably done because it would be easier to refresh the list whenever the
// user inserted a new floppy disk, or changed directory. The problem is that
// the script has a bug, and it invalidates the text of the entries in the
// list. This has a high probability of breaking, as the user could change the
// list very quickly, or the garbage collector could kick in and remove the
// deleted entries. We don't allow the user to change the directory, thus the
// contents of the file list are constant, so we can avoid the constant file
// and text entry refreshes whenever a button is pressed, and prevent possible
// crashes because of these constant quick object reallocations. Fixes bug
// #3037996.
const uint16 qfg2SignatureImportDialog[] = {
	0x63, SIG_MAGICDWORD, 0x20,         // pToa text
	0x30, SIG_UINT16 + 0x0b, 0x00,      // bnt [next state]
	0x7a,                               // push2
	0x39, 0x03,                         // pushi 03
	0x36,                               // push
	0x43, 0x72, 0x04,                   // callk Memory 4
	0x35, 0x00,                         // ldi 00
	0x65, 0x20,                         // aTop text
	SIG_END
};

const uint16 qfg2PatchImportDialog[] = {
	PATCH_ADDTOOFFSET +5,
	0x48,                               // ret
	PATCH_END
};

//          script, description,                                            signature                  patch
SciScriptPatcherEntry qfg2Signatures[] = {
	{  true,   944, "import dialog continuous calls",              1, 0, 0, qfg2SignatureImportDialog, qfg2PatchImportDialog },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// Patch for the import screen in QFG3, same as the one for QFG2 above
const uint16 qfg3SignatureImportDialog[] = {
	0x63, SIG_MAGICDWORD, 0x2a,         // pToa text
	0x31, 0x0b,                         // bnt [next state]
	0x7a,                               // push2
	0x39, 0x03,                         // pushi 03
	0x36,                               // push
	0x43, 0x72, 0x04,                   // callk Memory 4
	0x35, 0x00,                         // ldi 00
	0x65, 0x2a,                         // aTop text
	SIG_END
};

const uint16 qfg3PatchImportDialog[] = {
	PATCH_ADDTOOFFSET +4,
	0x48,                               // ret
	PATCH_END
};



// ===========================================================================
// Patch for the Woo dialog option in Uhura's conversation. Bug #3040722
// Problem: The Woo dialog option (0xffb5) is negative, and therefore
// treated as an option opening a submenu. This leads to uhuraTell::doChild
// being called, which calls hero::solvePuzzle and then proceeds with
// Teller::doChild to open the submenu. However, there is no actual submenu
// defined for option -75 since -75 does not show up in uhuraTell::keys.
// This will cause Teller::doChild to run out of bounds while scanning through
// uhuraTell::keys.
// Strategy: there is another conversation option in uhuraTell::doChild calling
// hero::solvePuzzle (0xfffc) which does a ret afterwards without going to
// Teller::doChild. We jump to this call of hero::solvePuzzle to get that same
// behaviour.
// Applies to at least: English, German, Italian, French, Spanish Floppy
// Responsible method: unknown
const uint16 qfg3SignatureWooDialog[] = {
	SIG_MAGICDWORD,
	0x67, 0x12,                         // pTos 12 (query)
	0x35, 0xb6,                         // ldi b6
	0x1a,                               // eq?
	0x2f, 0x05,                         // bt 05
	0x67, 0x12,                         // pTos 12 (query)
	0x35, 0x9b,                         // ldi 9b
	0x1a,                               // eq?
	0x31, 0x0c,                         // bnt 0c
	0x38, SIG_SELECTOR16 + SELECTOR_solvePuzzle, // pushi 0297
	0x7a,                               // push2
	0x38, SIG_UINT16 + 0x0c, 0x01,      // pushi 010c
	0x7a,                               // push2
	0x81, 0x00,                         // lag 00
	0x4a, 0x08,                         // send 08
	0x67, 0x12,                         // pTos 12 (query)
	0x35, 0xb5,                         // ldi b5
	SIG_END
};

const uint16 qfg3PatchWooDialog[] = {
	PATCH_ADDTOOFFSET +0x29,
	0x33, 0x11,                         // jmp to 0x6a2, the call to hero::solvePuzzle for 0xFFFC
	PATCH_END
};

//          script, description,                                            signature                  patch
SciScriptPatcherEntry qfg3Signatures[] = {
	{  true,   944, "import dialog continuous calls",              1, 0, 0, qfg3SignatureImportDialog, qfg3PatchImportDialog },
	{  true,   440, "dialog crash when asking about Woo",          1, 0, 0, qfg3SignatureWooDialog,    qfg3PatchWooDialog },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
//  script 298 of sq4/floppy has an issue. object "nest" uses another property
//   which isn't included in property count. We return 0 in that case, the game
//   adds it to nest::x. The problem is that the script also checks if x exceeds
//   we never reach that of course, so the pterodactyl-flight will go endlessly
//   we could either calculate property count differently somehow fixing this
//   but I think just patching it out is cleaner (bug #3037938)
const uint16 sq4FloppySignatureEndlessFlight[] = {
	0x39, 0x04,                         // pushi 04 (selector x)
	SIG_MAGICDWORD,
	0x78,                               // push1
	0x67, 0x08,                         // pTos 08 (property x)
	0x63, SIG_ADDTOOFFSET + 1,          // pToa (invalid property) - 44h for English floppy, 4ch for German floppy
	0x02,                               // add
	SIG_END
};

const uint16 sq4FloppyPatchEndlessFlight[] = {
	PATCH_ADDTOOFFSET +5,
	0x35, 0x03,                         // ldi 03 (which would be the content of the property)
	PATCH_END
};

// The scripts in SQ4CD support simultaneous playing of speech and subtitles,
// but this was not available as an option. The following two patches enable
// this functionality in the game's GUI options dialog.
// Patch 1: iconTextSwitch::show, called when the text options button is shown.
// This is patched to add the "Both" text resource (i.e. we end up with
// "Speech", "Text" and "Both")
const uint16 sq4CdSignatureTextOptionsButton[] = {
	SIG_MAGICDWORD,
	0x35, 0x01,                         // ldi 0x01
	0xa1, 0x53,                         // sag 0x53
	0x39, 0x03,                         // pushi 0x03
	0x78,                               // push1
	0x39, 0x09,                         // pushi 0x09
	0x54, 0x06,                         // self 0x06
	SIG_END
};

const uint16 sq4CdPatchTextOptionsButton[] = {
	PATCH_ADDTOOFFSET +7,
	0x39, 0x0b,                         // pushi 0x0b
	PATCH_END
};

// Patch 2: Adjust a check in babbleIcon::init, which handles the babble icon
// (e.g. the two guys from Andromeda) shown when dying/quitting.
// Fixes bug #3538418.
const uint16 sq4CdSignatureBabbleIcon[] = {
	SIG_MAGICDWORD,
	0x89, 0x5a,                         // lsg 5a
	0x35, 0x02,                         // ldi 02
	0x1a,                               // eq?
	0x31, 0x26,                         // bnt 26  [02a7]
	SIG_END
};

const uint16 sq4CdPatchBabbleIcon[] = {
	0x89, 0x5a,                         // lsg 5a
	0x35, 0x01,                         // ldi 01
	0x1a,                               // eq?
	0x2f, 0x26,                         // bt 26  [02a7]
	PATCH_END
};

// Patch 3: Add the ability to toggle among the three available options,
// when the text options button is clicked: "Speech", "Text" and "Both".
// Refer to the patch above for additional details.
// iconTextSwitch::doit (called when the text options button is clicked)
const uint16 sq4CdSignatureTextOptions[] = {
	SIG_MAGICDWORD,
	0x89, 0x5a,                         // lsg 0x5a (load global 90 to stack)
	0x3c,                               // dup
	0x35, 0x01,                         // ldi 0x01
	0x1a,                               // eq? (global 90 == 1)
	0x31, 0x06,                         // bnt 0x06 (0x0691)
	0x35, 0x02,                         // ldi 0x02
	0xa1, 0x5a,                         // sag 0x5a (save acc to global 90)
	0x33, 0x0a,                         // jmp 0x0a (0x69b)
	0x3c,                               // dup
	0x35, 0x02,                         // ldi 0x02
	0x1a,                               // eq? (global 90 == 2)
	0x31, 0x04,                         // bnt 0x04 (0x069b)
	0x35, 0x01,                         // ldi 0x01
	0xa1, 0x5a,                         // sag 0x5a (save acc to global 90)
	0x3a,                               // toss
	0x38, SIG_SELECTOR16 + SELECTOR_show, // pushi 0x00d9
	0x76,                               // push0
	0x54, 0x04,                         // self 0x04
	0x48,                               // ret
	SIG_END
};

const uint16 sq4CdPatchTextOptions[] = {
	0x89, 0x5a,                         // lsg 0x5a (load global 90 to stack)
	0x3c,                               // dup
	0x35, 0x03,                         // ldi 0x03 (acc = 3)
	0x1a,                               // eq? (global 90 == 3)
	0x2f, 0x07,                         // bt 0x07
	0x89, 0x5a,                         // lsg 0x5a (load global 90 to stack again)
	0x35, 0x01,                         // ldi 0x01 (acc = 1)
	0x02,                               // add: acc = global 90 (on stack) + 1 (previous acc value)
	0x33, 0x02,                         // jmp 0x02
	0x35, 0x01,                         // ldi 0x01 (reset acc to 1)
	0xa1, 0x5a,                         // sag 0x5a (save acc to global 90)
	0x33, 0x03,                         // jmp 0x03 (jump over the wasted bytes below)
	0x34, PATCH_UINT16 + 0x00, 0x00,    // ldi 0x0000 (waste 3 bytes)
	0x3a,                               // toss
	// (the rest of the code is the same)
	PATCH_END
};

//          script, description,                                            signature                        patch
SciScriptPatcherEntry sq4Signatures[] = {
	{  true,   298, "Floppy: endless flight",                      1, 0, 0, sq4FloppySignatureEndlessFlight, sq4FloppyPatchEndlessFlight },
	{  true,   818, "CD: Speech and subtitles option",             1, 0, 0, sq4CdSignatureTextOptions,       sq4CdPatchTextOptions },
	{  true,     0, "CD: Babble icon speech and subtitles fix",    1, 0, 0, sq4CdSignatureBabbleIcon,        sq4CdPatchBabbleIcon },
	{  true,   818, "CD: Speech and subtitles option button",      1, 0, 0, sq4CdSignatureTextOptionsButton, sq4CdPatchTextOptionsButton },
	SCI_SIGNATUREENTRY_TERMINATOR
};

// ===========================================================================
// When you leave Ulence Flats, another timepod is supposed to appear.
// On fast machines, that timepod appears fully immediately and then
//  starts to appear like it should be. That first appearance is caused
//  by the scripts setting an invalid cel number and the machine being
//  so fast that there is no time for another script to actually fix
//  the cel number. On slower machines, the cel number gets fixed
//  by the cycler and that's why only fast machines are affected.
//  The same issue happens in Sierra SCI.
// We simply set the correct starting cel number to fix the bug.
// Responsible method: robotIntoShip::changeState(9)
const uint16 sq1vgaSignatureUlenceFlatsTimepodGfxGlitch[] = {
	0x39, 
	SIG_MAGICDWORD, SIG_SELECTOR8 + SELECTOR_cel, // pushi "cel"
	0x78,                               // push1
	0x39, 0x0a,                         // pushi 0x0a (set ship::cel to 10)
	0x38, SIG_UINT16 + 0xa0, 0x00,      // pushi 0x00a0 (ship::setLoop)
	SIG_END
};

const uint16 sq1vgaPatchUlenceFlatsTimepodGfxGlitch[] = {
	PATCH_ADDTOOFFSET +3,
	0x39, 0x09,                         // pushi 0x09 (set ship::cel to 9)
	PATCH_END
};

const uint16 sq1vgaSignatureEgoShowsCard[] = {
	SIG_MAGICDWORD,
	0x38, SIG_SELECTOR16 + SELECTOR_timesShownID, // push "timesShownID"
	0x78,                               // push1
	0x38, SIG_SELECTOR16 + SELECTOR_timesShownID, // push "timesShownID"
	0x76,                               // push0
	0x51, 0x7c,                         // class DeltaurRegion
	0x4a, 0x04,                         // send 0x04 (get timesShownID)
	0x36,                               // push
	0x35, 0x01,                         // ldi 1
	0x02,                               // add
	0x36,                               // push
	0x51, 0x7c,                         // class DeltaurRegion
	0x4a, 0x06,                         // send 0x06 (set timesShownID)
	0x36,                               // push      (wrong, acc clobbered by class, above)
	0x35, 0x03,                         // ldi 0x03
	0x22,                               // lt?
	SIG_END
};

// Note that this script patch is merely a reordering of the
// instructions in the original script.
const uint16 sq1vgaPatchEgoShowsCard[] = {
	0x38, PATCH_SELECTOR16 + SELECTOR_timesShownID, // push "timesShownID"
	0x76,                               // push0
	0x51, 0x7c,                         // class DeltaurRegion
	0x4a, 0x04,                         // send 0x04 (get timesShownID)
	0x36,                               // push
	0x35, 0x01,                         // ldi 1
	0x02,                               // add
	0x36,                               // push (this push corresponds to the wrong one above)
	0x38, PATCH_SELECTOR16 + SELECTOR_timesShownID, // push "timesShownID"
	0x78,                               // push1
	0x36,                               // push
	0x51, 0x7c,                         // class DeltaurRegion
	0x4a, 0x06,                         // send 0x06 (set timesShownID)
	0x35, 0x03,                         // ldi 0x03
	0x22,                               // lt?
	PATCH_END
};


//          script, description,                                            signature                                   patch
SciScriptPatcherEntry sq1vgaSignatures[] = {
	{  true,    45, "Ulence Flats: timepod graphic glitch",        1, 0, 0, sq1vgaSignatureUlenceFlatsTimepodGfxGlitch, sq1vgaPatchUlenceFlatsTimepodGfxGlitch },
	{  true,    58, "Sarien armory droid zapping ego first time",  1, 0, 0, sq1vgaSignatureEgoShowsCard,                sq1vgaPatchEgoShowsCard },
	SCI_SIGNATUREENTRY_TERMINATOR};

// ===========================================================================
// The toolbox in sq5 is buggy. When you click on the upper part of the "put
//  in inventory"-button (some items only - for example the hole puncher - at the
//  upper left), points will get awarded correctly and the item will get put into
//  the player's inventory, but you will then get a "not here" message and the
//  item will also remain to be the current mouse cursor.
// The bug report also says that items may get lost. I wasn't able to reproduce
//  that part.
// This is caused by the mouse-click event getting reprocessed (which wouldn't
//  be a problem by itself) and during this reprocessing coordinates are not
//  processed the same as during the first click (script 226 includes a local
//  subroutine, which checks coordinates in a hardcoded way w/o port-adjustment).
// Because of this, the hotspot for the button is lower than it should be, which
//  then results in the game thinking that the user didn't click on the button
//  and also results in the previously mentioned message.
// This happened in Sierra SCI as well (of course).
// We fix it by combining state 0 + 1 of takeTool::changeState and so stopping
//  the event to get reprocessed. This was the only way possible, because everything
//  else is done in SCI system scripts and I don't want to touch those.
// Applies to at least: English/German/French PC floppy
// Responsible method: takeTool::changeState
// Fixes bug #6457
const uint16 sq5SignatureToolboxFix[] = {
	0x31, 0x13,                    // bnt [check for state 1]
	SIG_MAGICDWORD,
	0x38, SIG_UINT16 + 0xaa, 0x00, // pushi 00aa
	0x39, 0x05,                    // pushi 05
	0x39, 0x16,                    // pushi 16
	0x76,                          // push0
	0x39, 0x03,                    // pushi 03
	0x76,                          // push0
	0x7c,                          // pushSelf
	0x81, 0x5b,                    // lag 5b
	0x4a, 0x0e,                    // send 0e
	0x32, SIG_UINT16 + 0x88, 0x00, // jmp [end-of-method]
	0x3c,                          // dup
	0x35, 0x01,                    // ldi 01
	0x1a,                          // eq?
	0x31, 0x28,                    // bnt [check for state 2]
	SIG_END
};

const uint16 sq5PatchToolboxFix[] = {
	0x31, 0x41,                    // bnt [check for state 2]
	PATCH_ADDTOOFFSET +16,         // skip to jmp offset
	0x35, 0x01,                    // ldi 01
	0x65, 0x14,                    // aTop [state]
	0x36, 0x00, 0x00,              // ldi 0000 (waste 3 bytes)
	0x35, 0x00,                    // ldi 00 (waste 2 bytes)
	PATCH_END
};

//          script, description,                                            signature                        patch
SciScriptPatcherEntry sq5Signatures[] = {
	{  true,   226, "toolbox fix",                                 1, 0, 0, sq5SignatureToolboxFix,          sq5PatchToolboxFix },
	SCI_SIGNATUREENTRY_TERMINATOR
};


// will actually patch previously found signature area
void Script::patcherApplyPatch(const SciScriptPatcherEntry *patchEntry, byte *scriptData, const uint32 scriptSize, int32 signatureOffset, const bool isMacSci11) {
	const uint16 *patchData = patchEntry->patchData;
	byte orgData[PATCH_VALUELIMIT];
	int32 offset = signatureOffset;
	uint16 patchWord = *patchEntry->patchData;
	uint16 patchSelector = 0;

	// Copy over original bytes from script
	uint32 orgDataSize = scriptSize - offset;
	if (orgDataSize > PATCH_VALUELIMIT)
		orgDataSize = PATCH_VALUELIMIT;
	memcpy(&orgData, &scriptData[offset], orgDataSize);

	while (patchWord != PATCH_END) {
		uint16 patchCommand = patchWord & PATCH_COMMANDMASK;
		uint16 patchValue = patchWord & PATCH_VALUEMASK;
		switch (patchCommand) {
		case PATCH_ADDTOOFFSET: {
			// add value to offset
			offset += patchValue;
			break;
		}
		case PATCH_GETORIGINALBYTE: {
			// get original byte from script
			if (patchValue >= orgDataSize)
				error("Script-Patcher: can not get requested original byte from script");
			scriptData[offset] = orgData[patchValue];
			offset++;
			break;
		}
		case PATCH_GETORIGINALBYTEADJUST: {
			// get original byte from script and adjust it
			if (patchValue >= orgDataSize)
				error("Script-Patcher: can not get requested original byte from script");
			byte orgByte = orgData[patchValue];
			int16 adjustValue;
			patchData++; adjustValue = (int16)(*patchData);
			scriptData[offset] = orgByte + adjustValue;
			offset++;
			break;
		}
		case PATCH_UINT16:
		case PATCH_SELECTOR16: {
			byte byte1;
			byte byte2;
						
			switch (patchCommand) {
			case PATCH_UINT16: {
				byte1 = patchValue & PATCH_BYTEMASK;
				patchData++; patchWord = *patchData;
				if (patchWord & PATCH_COMMANDMASK)
					error("Script-Patcher: Patch inconsistent");
				byte2 = patchWord & PATCH_BYTEMASK;
				break;
			}
			case PATCH_SELECTOR16: {
				patchSelector = selectorTable[patchValue].id;
				byte1 = patchSelector & 0xFF;
				byte2 = patchSelector >> 8;
				break;
			}
			default:
				byte1 = 0; byte2 = 0;
			}
			if (!isMacSci11) {
				scriptData[offset++] = byte1;
				scriptData[offset++] = byte2;
			} else {
				// SCI1.1+ on macintosh had uint16s in script in BE-order
				scriptData[offset++] = byte2;
				scriptData[offset++] = byte1;
			}
			break;
		}
		case PATCH_SELECTOR8: {
			patchSelector = selectorTable[patchValue].id;
			if (patchSelector & 0xFF00)
				error("Script-Patcher: 8 bit selector required, game uses 16 bit selector");
			scriptData[offset] = patchSelector & 0xFF;
			offset++;
			break;
		}
		case PATCH_BYTE:
			scriptData[offset] = patchValue & PATCH_BYTEMASK;
			offset++;
		}
		patchData++;
		patchWord = *patchData;
	}
}

// will return -1 if no match was found, otherwise an offset to the start of the signature match
int32 Script::patcherFindSignature(const SciScriptPatcherEntry *patchEntry, const byte *scriptData, const uint32 scriptSize, const bool isMacSci11) {
	if (scriptSize < 4) // we need to find a DWORD, so less than 4 bytes is not okay
		return -1;

	const uint32 magicDWord = patchEntry->magicDWord; // is platform-specific BE/LE form, so that the later match will work
	const uint32 searchLimit = scriptSize - 3;
	uint32 DWordOffset = 0;
	// first search for the magic DWORD
	while (DWordOffset < searchLimit) {
		if (magicDWord == READ_UINT32(scriptData + DWordOffset)) {
			// magic DWORD found, check if actual signature matches
			uint32 offset = DWordOffset + patchEntry->magicOffset;
			uint32 byteOffset = offset;
			const uint16 *signatureData = patchEntry->signatureData;
			uint16 sigSelector = 0;

			uint16 sigWord = *signatureData;
			while (sigWord != SIG_END) {
				uint16 sigCommand = sigWord & SIG_COMMANDMASK;
				uint16 sigValue = sigWord & SIG_VALUEMASK;
				switch (sigCommand) {
				case SIG_ADDTOOFFSET: {
					// add value to offset
					byteOffset += sigValue;
					break;
				}
				case SIG_UINT16:
				case SIG_SELECTOR16: {
					if ((byteOffset + 1) < scriptSize) {
						byte byte1;
						byte byte2;

						switch (sigCommand) {
						case SIG_UINT16: {
							byte1 = sigValue & SIG_BYTEMASK;
							signatureData++; sigWord = *signatureData;
							if (sigWord & SIG_COMMANDMASK)
								error("Script-Patcher: signature inconsistent\nFaulty patch: '%s'", patchEntry->description);
							byte2 = sigWord & SIG_BYTEMASK;
							break;
						}
						case SIG_SELECTOR16: {
							sigSelector = selectorTable[sigValue].id;
							byte1 = sigSelector & 0xFF;
							byte2 = sigSelector >> 8;
							break;
						}
						default:
							byte1 = 0; byte2 = 0;
						}
						if (!isMacSci11) {
							if ((scriptData[byteOffset] != byte1) || (scriptData[byteOffset + 1] != byte2))
								sigWord = SIG_MISMATCH;
						} else {
							// SCI1.1+ on macintosh had uint16s in script in BE-order
							if ((scriptData[byteOffset] != byte2) || (scriptData[byteOffset + 1] != byte1))
								sigWord = SIG_MISMATCH;
						}
						byteOffset += 2;
					} else {
						sigWord = SIG_MISMATCH;
					}
					break;
				}
				case SIG_SELECTOR8: {
					if (byteOffset < scriptSize) {
						sigSelector = selectorTable[sigValue].id;
						if (sigSelector & 0xFF00)
							error("Script-Patcher: 8 bit selector required, game uses 16 bit selector\nFaulty patch: '%s'", patchEntry->description);
						if (scriptData[byteOffset] != (sigSelector & 0xFF))
							sigWord = SIG_MISMATCH;
						byteOffset++;
					} else {
						sigWord = SIG_MISMATCH; // out of bounds
					}
					break;
				}
				case SIG_BYTE:
					if (byteOffset < scriptSize) {
						if (scriptData[byteOffset] != sigWord)
							sigWord = SIG_MISMATCH;
						byteOffset++;
					} else {
						sigWord = SIG_MISMATCH; // out of bounds
					}
				}
				
				if (sigWord == SIG_MISMATCH)
					break;
				
				signatureData++;
				sigWord = *signatureData;
			}
			
			if (sigWord == SIG_END) // signature fully matched?
				return offset;
		}
		DWordOffset++;
	}
	// nothing found
	return -1;
}

// This method calculates the magic DWORD for each entry in the signature table
//  and it also initializes the selector table for selectors used in the signatures/patches of the current game
void Script::patcherInitSignature(SciScriptPatcherEntry *patchTable, bool isMacSci11) {
	SciScriptPatcherEntry *curEntry = patchTable;
	SciScriptPatcherSelector *curSelector = NULL;
	int step;
	int magicOffset;
	byte magicDWord[4];
	int magicDWordLeft = 0;
    const uint16 *curData;
    uint16 curWord;
    uint16 curCommand;
    uint32 curValue;
    byte byte1;
    byte byte2;

    while (curEntry->signatureData) {
		// process signature
		memset(magicDWord, 0, sizeof(magicDWord));

		for (step = 0; step < 2; step++) {
			switch (step) {
			case 0: curData = curEntry->signatureData; break;
			case 1: curData = curEntry->patchData; break;
			}
		
			curWord = *curData;
			magicOffset = 0;
			while (curWord != SIG_END) {
				curCommand = curWord & SIG_COMMANDMASK;
				curValue   = curWord & SIG_VALUEMASK;
				switch (curCommand) {
				case SIG_MAGICDWORD: {
					if (step == 0) {
						if ((curEntry->magicDWord) || (magicDWordLeft))
							error("Script-Patcher: Magic-DWORD specified multiple times in signature\nFaulty patch: '%s'", curEntry->description);
						magicDWordLeft = 4;
						curEntry->magicOffset = magicOffset;
					}
					break;
				}
				case SIG_ADDTOOFFSET: {
					magicOffset -= curValue;
					if (magicDWordLeft)
						error("Script-Patcher: Magic-DWORD contains AddToOffset command\nFaulty patch: '%s'", curEntry->description);
					break;
				}
				case SIG_UINT16:
				case SIG_SELECTOR16: {
					// UINT16 or 1
					switch (curCommand) {
					case SIG_UINT16: {
						curData++; curWord = *curData;
						if (curWord & SIG_COMMANDMASK)
							error("Script-Patcher: signature entry inconsistent\nFaulty patch: '%s'", curEntry->description);
						if (!isMacSci11) {
							byte1 = curValue;
							byte2 = curWord & SIG_BYTEMASK;
						} else {
							byte1 = curWord & SIG_BYTEMASK;
							byte2 = curValue;
						}
						break;
					}
					case SIG_SELECTOR16: {
						curSelector = &selectorTable[curValue];
						if (curSelector->id == -1)
							curSelector->id = g_sci->getKernel()->findSelector(curSelector->name);
						if (!isMacSci11) {
							byte1 = curSelector->id & 0x00FF;
							byte2 = curSelector->id >> 8;
						} else {
							byte1 = curSelector->id >> 8;
							byte2 = curSelector->id & 0x00FF;
						}
						break;
					}
					}
					magicOffset -= 2;
					if (magicDWordLeft) {
						// Remember current word for Magic DWORD
						magicDWord[4 - magicDWordLeft] = byte1;
						magicDWordLeft--;
						if (magicDWordLeft) {
							magicDWord[4 - magicDWordLeft] = byte2;
							magicDWordLeft--;
						}
						if (!magicDWordLeft) {
							curEntry->magicDWord = READ_LE_UINT32(magicDWord);
						}
					}
					break;
				}
				case SIG_BYTE:
				case SIG_SELECTOR8: {
					if (curCommand == SIG_SELECTOR8) {
						curSelector = &selectorTable[curValue];
						if (curSelector->id == -1) {
							curSelector->id = g_sci->getKernel()->findSelector(curSelector->name);
							if (curSelector->id != -1) {
								if (curSelector->id & 0xFF00)
									error("Script-Patcher: 8 bit selector required, game uses 16 bit selector\nFaulty patch: '%s'", curEntry->description);
							}
						}
						curValue = curSelector->id;
					}
					magicOffset--;
					if (magicDWordLeft) {
						// Remember current byte for Magic DWORD
						magicDWord[4 - magicDWordLeft] = (byte)curValue;
						magicDWordLeft--;
						if (!magicDWordLeft) {
							curEntry->magicDWord = READ_LE_UINT32(magicDWord);
						}
					}
				}
				}
				curData++;
				curWord = *curData;
			}
		}
		if (magicDWordLeft)
			error("Script-Patcher: Magic-DWORD beyond End-Of-Signature\nFaulty patch: '%s'", curEntry->description);
		if (!curEntry->magicDWord)
			error("Script-Patcher: Magic-DWORD not specified in signature\nFaulty patch: '%s'", curEntry->description);
    
		curEntry++;
    }
}

// This method enables certain patches
//  It's used for patches, which are not meant to get applied all the time
void Script::patcherEnablePatch(SciScriptPatcherEntry *patchTable, const char *searchDescription) {
	SciScriptPatcherEntry *curEntry = patchTable;
	int searchDescriptionLen = strlen( searchDescription );
	int matchCount = 0;
	
    while (curEntry->signatureData) {
		if (strncmp(curEntry->description, searchDescription, searchDescriptionLen) == 0) {
			// match found, enable patch
			curEntry->active = true;
			matchCount++;
		}
		curEntry++;
    }
    
    if (!matchCount)
		error("Script-Patcher: no patch found to enable");
}

void Script::patcherProcessScript(uint16 scriptNr, byte *scriptData, const uint32 scriptSize) {
	SciScriptPatcherEntry *signatureTable = NULL;
	const Sci::SciGameId gameId = g_sci->getGameId();

	switch (gameId) {
	case GID_CAMELOT:
		signatureTable = camelotSignatures;
		break;
	case GID_ECOQUEST:
		signatureTable = ecoquest1Signatures;
		break;
	case GID_ECOQUEST2:
		signatureTable = ecoquest2Signatures;
		break;
	case GID_FANMADE:
		signatureTable = fanmadeSignatures;
		break;
	case GID_FREDDYPHARKAS:
		signatureTable = freddypharkasSignatures;
		break;
	case GID_GK1:
		signatureTable = gk1Signatures;
		break;
	case GID_KQ5:
		// See the explanation in the kq5SignatureWinGMSignals comment
//		if (g_sci->_features->useAltWinGMSound())
//			signatureTable = kq5WinGMSignatures;
//		else
			signatureTable = kq5Signatures;
		break;
	case GID_KQ6:
		signatureTable = kq6Signatures;
		break;
	case GID_LAURABOW2:
		signatureTable = laurabow2Signatures;
		break;
	case GID_LONGBOW:
		signatureTable = longbowSignatures;
		break;
	case GID_LSL2:
		signatureTable = larry2Signatures;
		break;
	case GID_LSL5:
		signatureTable = larry5Signatures;
		break;
	case GID_LSL6:
		signatureTable = larry6Signatures;
		break;
	case GID_MOTHERGOOSE256:
		signatureTable = mothergoose256Signatures;
		break;
	case GID_PQ1:
		signatureTable = pq1vgaSignatures;
		break;
	case GID_QFG1VGA:
		signatureTable = qfg1vgaSignatures;
		break;
	case GID_QFG2:
		signatureTable = qfg2Signatures;
		break;
	case GID_QFG3:
		signatureTable = qfg3Signatures;
		break;
	case GID_SQ1:
		signatureTable = sq1vgaSignatures;
		break;
	case GID_SQ4:
		signatureTable = sq4Signatures;
		break;
	case GID_SQ5:
		signatureTable = sq5Signatures;
		break;
	default:
		break;
	}

	if (signatureTable) {
		bool isMacSci11 = (g_sci->getPlatform() == Common::kPlatformMacintosh && getSciVersion() >= SCI_VERSION_1_1);

		if (!signatureTable->magicDWord) {
			// Abort, in case selectors are not yet initialized (happens for games w/o selector-dictionary)
			if (!g_sci->getKernel()->selectorNamesAvailable())
				return;
		
			// signature table needs to get initialized (Magic DWORD set, selector table set)
			patcherInitSignature(signatureTable, isMacSci11);
			
			// Do additional game-specific initialization
			switch (gameId) {
			case GID_KQ5:
				if (g_sci->_features->useAltWinGMSound()) {
					// See the explanation in the kq5SignatureWinGMSignals comment
					patcherEnablePatch(signatureTable, "Win: GM Music signal checks");
				}
				break;
			case GID_LAURABOW2:
				if (g_sci->speechAndSubtitlesEnabled()) {
					// Enables Audio + subtitles patches for Laura Bow 2, when "Text and Speech: Both" is selected
					patcherEnablePatch(signatureTable, "CD: audio + text support");
				}
				break;
			default:
				break;
			}
		}
	
		while (signatureTable->signatureData) {
			if ( (scriptNr == signatureTable->scriptNr) && (signatureTable->active) ) {
				int32 foundOffset = 0;
				int16 applyCount = signatureTable->applyCount;
				do {
					foundOffset = patcherFindSignature(signatureTable, scriptData, scriptSize, isMacSci11);
					if (foundOffset != -1) {
						// found, so apply the patch
						debugC(kDebugLevelScriptPatcher, "Script-Patcher: '%s' on script %d offset %d", signatureTable->description, scriptNr, foundOffset);
						patcherApplyPatch(signatureTable, scriptData, scriptSize, foundOffset, isMacSci11);
					}
					applyCount--;
				} while ((foundOffset != -1) && (applyCount));
			}
			signatureTable++;
		}
	}
}

} // End of namespace Sci
