CC ?= cc
CFLAGS ?= -O2 -Wall

all: sender receiver

sender: sender.cpp
	g++ -std=c++14 -pthread -O2 sender.cpp -o sender

receiver: receiver.cpp
	g++ -std=c++14 -pthread -O2 receiver.cpp -o receiver

clean:
	rm -f sender receiver
