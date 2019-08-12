# This file is part of wslbridge2 project
# Licensed under the GNU General Public License version 3
# Copyright (C) 2019 Biswapriyo Nath

# Root Makefile for wslbridge2 project

all:
ifeq ($(OS), Windows_NT)
	cd wslbridge; $(MAKE) -f Makefile.frontend
	cd rawpty; $(MAKE) -f Makefile
	cd hvpty; $(MAKE) -f Makefile.frontend
else
	cd wslbridge; $(MAKE) -f Makefile.backend
	cd hvpty; $(MAKE) -f Makefile.backend
endif

clean:
	rm -rf bin/*
