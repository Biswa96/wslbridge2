# This file is part of wslbridge2 project
# Licensed under the GNU General Public License version 3
# Copyright (C) 2019 Biswapriyo Nath

# Makefile for wslbridge2 rawpty

STRIP ?= strip
NAME = rawpty.exe
BINDIR = ../bin
CFLAGS = -D_GNU_SOURCE -fno-exceptions -O2 -std=c++11 -Wall
LFLAGS = -pthread

ifdef RELEASE
LFLAGS += -static -static-libgcc -static-libstdc++
endif

BINS = \
$(BINDIR)/rawpty.obj \
$(BINDIR)/SocketIo.obj \
$(BINDIR)/TerminalState.obj

all : $(BINDIR) $(NAME)

$(NAME) : $(BINS)
	$(CXX) $^ $(LFLAGS) -o $(BINDIR)/$@
	$(STRIP) $(BINDIR)/$@

$(BINDIR)/rawpty.obj : rawpty.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR)/SocketIo.obj : ../wslbridge/SocketIo.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR)/TerminalState.obj : ../wslbridge/TerminalState.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR) :
	mkdir -p $(BINDIR)

clean :
	rm -f $(BINDIR)/$(NAME)
