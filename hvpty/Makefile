# This file is part of wslbridge2 project
# Licensed under the GNU General Public License version 3
# Copyright (C) 2019 Biswapriyo Nath

# Makefile for hvpty files

BINDIR = ../bin
CFLAGS = -D_GNU_SOURCE -m64 -O2 -Wall
WIN_LIB = -lws2_32 -lole32 -pthread
WIN_SRC = \
GetConsoleHandle.S GetVmId.cpp hvpty_win.cpp \
../common/SocketIo.cpp \
../frontend/TerminalState.cpp

all:
ifeq ($(OS), Windows_NT)
	$(CXX) $(CFLAGS) $(WIN_SRC) $(WIN_LIB) -o $(BINDIR)/hvpty.exe
else
	$(CC) $(CFLAGS) hvpty_wsl.c -lutil -o $(BINDIR)/hvpty-backend
endif

clean:
	rm -rf $(BINDIR)/*
