CC = g++
CFLAGS = -g
INCLUDES = -I../../3rdparty/rxcpp/Rx/v2/src

all: test_rxcpp

test_rxcpp: test_rxcpp.cpp
	$(CC) $(CFLAGS) -o test_rxcpp.out test_rxcpp.cpp $(INCLUDES) $(LDFLAGS)

clean:
	rm -f test_rxcpp.out

.PHONY: all test_rxcpp clean
