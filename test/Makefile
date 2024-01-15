
OUT_DIR = ../build
TARGET = $(OUT_DIR)/aug_test
SRC = $(wildcard *.cpp)
CC = g++
CFLAGS = -Wall -O3
LIBS = -I../ -std=c++11

all: $(OUT_DIR) $(TARGET)
	echo "Done"

clean: 
	rm -f $(TARGET)

$(OUT_DIR):
	mkdir $(OUT_DIR)

$(TARGET): $(SRC)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)