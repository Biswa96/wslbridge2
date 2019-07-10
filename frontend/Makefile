STRIP ?= strip

all : ../out/wslbridge.exe

../out/wslbridge.exe : wslbridge.cc LocalSock.cpp ../common/SocketIo.cc ../common/SocketIo.h
	mkdir -p ../out
	$(CXX) -std=c++11 -fno-exceptions \
		-static -static-libgcc -static-libstdc++ \
		-D_GNU_SOURCE -D_WIN32_WINNT=0x0600 -DUNICODE -D_UNICODE \
		-Wall -O2 $< LocalSock.cpp ../common/SocketIo.cc -o $@
	$(STRIP) $@

clean:
	rm -f ../out/wslbridge.exe
