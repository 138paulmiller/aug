
OUT_DIR = ../build
TARGET = $(OUT_DIR)/shl_test
SRC = $(wildcard *.cpp)
CC = g++
CFLAGS = -Wall
LIBS = -I../ -std=c++17

all: $(OUT_DIR) $(TARGET)
	echo "Done"

monolith : $(TARGET)_monolith
	echo "Done"

clean: 
	rm -f $(TARGET)

$(TARGET)_monolith: $(TARGET)_monolith.cpp
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

$(OUT_DIR):
	mkdir $(OUT_DIR)

$(TARGET): $(SRC)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)