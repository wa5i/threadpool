objects = test.o threadpool.o
CC = gcc

all: $(objects)
	$(CC) -Wall -g -o test $(objects) -lpthread

$(objects): %.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f test *.o
