MODULE := engines/twp

MODULE_OBJS = \
	twp.o \
	console.o \
	metaengine.o \
	vm.o \
	ggpack.o \
	gfx.o \
	resmanager.o \
	spritesheet.o \
	room.o \
	lighting.o \
	font.o \
	sqgame.o \
	syslib.o \
	objlib.o \
	genlib.o \
	squtil.o \
	thread.o \
	rectf.o \
	scenegraph.o \
	object.o \
	ids.o \
	camera.o \
	actorlib.o \
	roomlib.o \
	soundlib.o \
	prefs.o \
	tsv.o \
	util.o \
	motor.o \
	yack.o \
	dialog.o \
	shaders.o \
	hud.o \
	lip.o \
	callback.o \
	graph.o \
	walkboxnode.o \
	actorswitcher.o \
	enginedialogtarget.o \
	audio.o \
	savegame.o \
	btea.o \
	time.o \
	dialogs.o \
	squirrel/sqapi.o \
	squirrel/sqbaselib.o \
	squirrel/sqfuncstate.o \
	squirrel/sqdebug.o \
	squirrel/sqlexer.o \
	squirrel/sqobject.o \
	squirrel/sqcompiler.o \
	squirrel/sqstate.o \
	squirrel/sqtable.o \
	squirrel/sqmem.o \
	squirrel/sqvm.o \
	squirrel/sqclass.o \
	squirrel/sqstdio.o \
	squirrel/sqstdmath.o \
	squirrel/sqstdstring.o \
	squirrel/sqstdstream.o \
	squirrel/sqstdblob.o \
	squirrel/sqstdrex.o \
	squirrel/sqstdaux.o \
	clipper/clipper.o \

ifdef USE_IMGUI
MODULE_OBJS += \
	debugtools.o \
	imgui/imgui.o \
	imgui/imgui_draw.o \
	imgui/imgui_widgets.o \
	imgui/imgui_tables.o \
	imgui_impl_opengl3_scummvm.o \
	imgui_impl_sdl2_scummvm.o
endif

# This module can be built as a plugin
ifeq ($(ENABLE_TWP), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
