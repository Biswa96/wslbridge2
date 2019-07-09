STRIP ?= strip

all : ../out/wslbridge-backend

../out/wslbridge-backend : wslbridge-backend.cc ../common/SocketIo.cc ../common/SocketIo.h
	mkdir -p ../out
	$(CXX) -std=c++11 -fno-exceptions \
		-static-libgcc -static-libstdc++ \
		-D_GNU_SOURCE \
		-Wall -O2 $< ../common/SocketIo.cc -o $@ -lutil -pthread
	$(STRIP) $@

clean:
	rm -f ../out/wslbridge-backend
