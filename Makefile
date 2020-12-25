# This file is part of wslbridge2 project.
# Licensed under the terms of the GNU General Public License v3 or later.
# Copyright (C) 2019-2020 Biswapriyo Nath.

# Root Makefile for wslbridge2 project
# For static linked binaries use `make RELEASE=1` command

all:
ifeq ($(OS), Windows_NT)
	cd src; $(MAKE) -f Makefile.frontend
else
	cd src; $(MAKE) -f Makefile.backend
endif

clean:
	rm -rf bin/*
