CFLAGS=-Wall -Wextra -Werror -O2
TARGETS=ipv4_chat

.PHONY: all clean

all: $(TARGETS)

clean:
	rm -rf *.o $(TARGETS)

ipv4_chat: src/main.c src/ipv4chat_utils.c
	gcc $(CFLAGS) -pthread -o ipv4_chat src/main.c src/ipv4chat_utils.c

install:
	sudo apt update
	sudo apt install build-essential