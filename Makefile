# KeeperFX packaging Makefile — PACKAGING ONLY
#
# ⚠️  Compilation has moved to CMake + Docker.
#     To build the game:
#       cmake --preset windows-x86-release
#       cmake --build --preset windows-x86-release
#
# This Makefile handles PACKAGING only (creating the release 7zip archive,
# converting graphics, generating lang/sfx data).  It expects the game
# binaries to already be present at the CMake output path.
#
# Usage:
#   cmake --build --preset windows-x86-release   # build first
#   make package                                  # then package

EXEEXT = .exe

# Paths match the CMake windows-x86-release output
BUILD_PRESET = windows-x86-release
BUILD_DIR    = out/build/$(BUILD_PRESET)
BIN          = $(BUILD_DIR)/keeperfx$(EXEEXT)

RM    = rm -f
CP    = cp -f
MV    = mv -f
MKDIR = mkdir -p
ECHO  = @echo

BUILD_NUMBER ?= 0
GIT_BRANCH ?= $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
PACKAGE_SUFFIX ?= $(GIT_BRANCH)

CAMPAIGNS = $(patsubst campgns/%.cfg,%,$(wildcard campgns/*.cfg))
MAPPACKS   = $(patsubst levels/%.cfg,%,$(filter-out %/personal.cfg,$(wildcard levels/*.cfg)))
LANGS      = eng chi cht cze dut fre ger ita jpn kor lat pol rus spa swe

include build/make/version.mk

VER_STRING = $(VER_MAJOR).$(VER_MINOR).$(VER_RELEASE).$(BUILD_NUMBER) $(PACKAGE_SUFFIX)

include build/make/prebuilds.mk

PNGTOICO   = tools/png2ico/png2ico
PNGTORAW   = tools/pngpal2raw/bin/pngpal2raw
PNGTOBSPAL = tools/png2bestpal/bin/png2bestpal
POTONGDAT  = tools/po2ngdat/bin/po2ngdat
WAVTODAT   = tools/sndbanker/bin/sndbanker
RNC        = tools/rnctools/bin/rnc
DERNC      = tools/rnctools/bin/dernc

.PHONY: package clean-package

include build/make/tool_png2ico.mk
include build/make/tool_pngpal2raw.mk
include build/make/tool_png2bestpal.mk
include build/make/tool_po2ngdat.mk
include build/make/tool_sndbanker.mk
include build/make/tool_rnctools.mk

include build/make/pkg_lang.mk
include build/make/pkg_gfx.mk
include build/make/pkg_sfx.mk
include build/make/package.mk

export RM CP MKDIR MV ECHO
