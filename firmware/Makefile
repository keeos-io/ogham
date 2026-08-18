# -----------------------------------------------------------------------------
# Ogham — a dual-voice bytebeat synthesizer for Eurorack
#
# Author:     Steven Collins, 2026, Keeos.io
# Copyright:  (c) 2026 Steven Collins
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
#
# This file is part of the Ogham firmware. See LICENSE-firmware.txt at the
# repository root for the full licence text.
# https://github.com/keeos-io/ogham
# -----------------------------------------------------------------------------

# Ogham Eurorack Bytebeat Module (Daisy Seed Rev4)
TARGET = ogham_bytebeat

# Sources
CPP_SOURCES = \
	src/ogham_main.cpp \
	src/ogham_controls.cpp \
	src/ogham_audio_pipeline.cpp \
	src/ogham_cv_output.cpp \
	src/ogham_display.cpp \
	src/tm1637.cpp \
	src/bytebeat_engine.cpp \
	src/formulas.cpp \
	src/bpm_clock.cpp

# Library Locations
# Override if your libDaisy/DaisySP live elsewhere:
#   make LIB_DIR=/path/to/libs
LIB_DIR ?= ../lib
LIBDAISY_DIR = $(LIB_DIR)/libDaisy
DAISYSP_DIR = $(LIB_DIR)/DaisySP

# Include paths
C_INCLUDES += -Isrc

# Core location
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# Include libDaisy build system
include $(SYSTEM_FILES_DIR)/Makefile
