CC = g++
CFLAGS = -g
INCLUDES = -I. -I../../3rdparty/nlohmann/single_include -I../../3rdparty/nng/include -I../../3rdparty/spdlog/include
LDFLAGS = -L../../build/3rdparty/nng -lnng

all: nng_server nng_client

nng_server: nng_server.cpp socket.cpp
	$(CC) $(CFLAGS) -o nng_server.out nng_server.cpp socket.cpp $(INCLUDES) $(LDFLAGS)

nng_client: nng_client.cpp socket.cpp
	$(CC) $(CFLAGS) -o nng_client.out nng_client.cpp socket.cpp $(INCLUDES) $(LDFLAGS)

clean:
	rm -f nng_server.out nng_client.out

.PHONY: all nng_server nng_client clean
