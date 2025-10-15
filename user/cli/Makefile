# cli/Makefile
CXX = g++
CXXFLAGS = -O2 -Wall -std=c++17

TARGET = main
SRC = main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

