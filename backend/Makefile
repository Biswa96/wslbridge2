# Makefile for wslbridge2 backend

STRIP ?= strip
NAME = wslbridge-backend
BINDIR = ../bin
CFLAGS = -D_GNU_SOURCE -fno-exceptions -O2 -std=c++11 -Wall
LFLAGS = -lutil -pthread -static-libgcc -static-libstdc++
BINS = \
$(BINDIR)/wslbridge-backend.o \
$(BINDIR)/SocketIo.o

all : $(BINDIR) $(NAME)

$(NAME) : $(BINS)
	$(CXX) $^ $(LFLAGS) -o $(BINDIR)/$@
	$(STRIP) $(BINDIR)/$@

$(BINDIR)/wslbridge-backend.o : wslbridge-backend.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR)/SocketIo.o : ../common/SocketIo.cpp
	$(CXX) -c $(CFLAGS) $< -o $@

$(BINDIR) :
	mkdir -p $(BINDIR)

clean :
	rm -f $(BINDIR)/$(NAME)
