CXX = g++
CXXFLAGS = -O3 -std=c++17 -pthread -Wall -Wextra

all: sender receiver

sender: sender.cpp
	$(CXX) $(CXXFLAGS) sender.cpp -o sender

receiver: receiver.cpp
	$(CXX) $(CXXFLAGS) receiver.cpp -o receiver

clean:
	rm -f sender receiver
