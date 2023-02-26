CC = gcc
CFLAGS = -Wall -ggdb -Wextra
TARGET = proja

all: proja

proja: checksum.c packet_parser.c config.c tunif.c router.c
	$(CC) $(CFLAGS) checksum.c packet_parser.c config.c tunif.c router.c -o $(TARGET)

clean:
	rm *.o	$(TARGET)
