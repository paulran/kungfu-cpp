CC = g++
CFLAGS = -g
INCLUDES = -I../../3rdparty/tabulate/include

all: test_tabulate

test_tabulate: test_tabulate.cpp
	$(CC) $(CFLAGS) -o test_tabulate.out test_tabulate.cpp $(INCLUDES) $(LDFLAGS)

clean:
	rm -f test_tabulate.out

.PHONY: all test_tabulate clean
