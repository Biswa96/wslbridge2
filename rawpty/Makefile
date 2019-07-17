# GNU GENERAL PUBLIC LICENSE Version 3 (GNU GPL v3)
# Copyright (c) 2019 Biswapriyo Nath

# Makefile for wslbridge2's RawPty

STRIP ?= strip
NAME = rawpty.exe
BINDIR = ../bin
CFLAGS = -D_GNU_SOURCE -fno-exceptions -O2 -std=c++11 -Wall
LFLAGS = -static -static-libgcc -static-libstdc++
BINS = \
$(BINDIR)/RawPty.obj \
$(BINDIR)/SocketIo.obj \
$(BINDIR)/TerminalState.obj

all : $(BINDIR) $(NAME)

$(NAME) : $(BINS)
	$(CXX) $^ $(LFLAGS) -o $(BINDIR)/$@
	$(STRIP) $(BINDIR)/$@

$(BINDIR)/RawPty.obj : RawPty.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR)/SocketIo.obj : ../common/SocketIo.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR)/TerminalState.obj : ../frontend/TerminalState.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR) :
	mkdir -p $(BINDIR)

clean :
	rm -f $(BINDIR)/*
