# This file is part of wslbridge2 project.
# Licensed under the terms of the GNU General Public License v3 or later.
# Copyright (C) 2019 Biswapriyo Nath.

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
$(BINDIR)/common.obj \
$(BINDIR)/rawpty.obj \
$(BINDIR)/TerminalState.obj

all : $(BINDIR) $(NAME)

$(NAME) : $(BINS)
	$(CXX) $^ $(LFLAGS) -o $(BINDIR)/$@
	$(STRIP) $(BINDIR)/$@

$(BINDIR)/common.obj : ../src/common.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR)/rawpty.obj : rawpty.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR)/TerminalState.obj : ../src/TerminalState.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR) :
	mkdir -p $(BINDIR)

clean :
	rm -f $(BINDIR)/$(NAME)
