// Residual - Virtual machine to run LucasArts' 3D adventure games
// Copyright (C) 2003-2004 The ScummVM-Residual Team (www.scummvm.org)
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

#ifndef COSTUME_H
#define COSTUME_H

#include <string>
#include "matrix4.h"

class TextSplitter;
class Actor;

class Costume {
public:
	Costume(const char *filename, const char *data, int len, Costume *prevCost);

	~Costume();

	const char *filename() const { return _fname.c_str(); }

	void playChore(int num) { _chores[num].play(); }
	void playChoreLooping(int num) { _chores[num].playLooping(); }
	void setChoreLooping(int num, bool val) { _chores[num].setLooping(val); }
	void stopChore(int num) { _chores[num].stop(); }
	void stopChores();
	int isChoring(int num, bool excludeLooping);
	int isChoring(bool excludeLooping);

	void setHead(int joint1, int joint2, int joint3, float maxRoll, float maxPitch, float maxYaw);

	void update();
	void setupTextures();
	void draw();
	void setPosRotate(Vector3d pos, float pitch, float yaw, float roll);

	class Component {
	public:
		Component(Component *parent, int parentID);

		virtual void setMatrix(Matrix4 matrix) { };
		virtual void init() { }
		virtual void setKey(int /* val */) { }
		virtual void update() { }
		virtual void setupTexture() { }
		virtual void draw() { }
		virtual void reset() { }
		virtual ~Component() { }

	protected:
		int _parentID;
		Component *_parent, *_child, *_sibling;
		Matrix4 _matrix;
		void setParent(Component *newParent);

		friend class Costume;
	};

private:
	Component *loadComponent(char tag[4], Component *parent, int parentID, const char *name);

	std::string _fname;

	int _numComponents;
	Component **_components;

	struct TrackKey {
		int time, value;
	};

	struct ChoreTrack {
		int compID;
		int numKeys;
		TrackKey *keys;
	};

	struct Head {
		int joint1;
		int joint2;
		int joint3;
		float maxRoll;
		float maxPitch;
		float maxYaw;
	} _head;

	class Chore {
	public:
		void load(Costume *owner, TextSplitter &ts);
		void play();
		void playLooping();
		void setLooping(bool val) { _looping = val; }
		void stop();
		void update();

	private:
		Costume *_owner;

		int _length;
		int _numTracks;
		ChoreTrack *_tracks;
		char _name[32];

		bool _hasPlayed, _playing, _looping;
		int _currTime;

		void setKeys(int startTime, int stopTime);

		friend class Costume;
	};

	int _numChores;
	Chore *_chores;
	Matrix4 _matrix;
};

#endif
