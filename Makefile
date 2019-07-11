# Root Makefile for wslbridge2 project

all:
ifeq ($(OS), Windows_NT)
	cd frontend; $(MAKE)
else
	cd backend; $(MAKE)
endif

clean:
	rm -rf bin/*
