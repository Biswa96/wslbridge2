# Makefile for wslbridge2 frontend

STRIP ?= strip
NAME = wslbridge2.exe
BINDIR = ../bin
CFLAGS = -D_GNU_SOURCE -fno-exceptions -O2 -std=c++11 -Wall
LFLAGS = -static -static-libgcc -static-libstdc++
BINS = \
$(BINDIR)/LocalSock.obj \
$(BINDIR)/SocketIo.obj \
$(BINDIR)/TerminalState.obj \
$(BINDIR)/wslbridge.obj

# Windows header specific options
CCOPT = -D_WIN32_WINNT=0x0600 -DUNICODE -D_UNICODE

all : $(BINDIR) $(NAME)

$(NAME) : $(BINS)
	$(CXX) $^ $(LFLAGS) -o $(BINDIR)/$@
	$(STRIP) $(BINDIR)/$@

$(BINDIR)/LocalSock.obj : LocalSock.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR)/SocketIo.obj : ../common/SocketIo.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR)/TerminalState.obj : TerminalState.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR)/wslbridge.obj : wslbridge.cpp
	$(CXX) -c $(CFLAGS) $(CCOPT) $< -o $@

$(BINDIR) :
	mkdir -p $(BINDIR)

clean :
	rm -f $(BINDIR)/$(NAME)
