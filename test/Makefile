OUT_DIR = ../build
TARGET = $(OUT_DIR)/aug_test
SRC = $(wildcard *.cpp)
CC = g++
CFLAGS = -Wall -O3
LIBS = -I../ -std=c++11
LD =
DEBUG=0

all: $(OUT_DIR) $(TARGET)
	echo "Done"

clean: 
	rm -f $(TARGET)

test: clean all
	clear;
	valgrind  --main-stacksize=1048576 --tool=memcheck --leak-check=full \
	../build/aug_test --test_all $(shell ls ../examples/test_*)\
	 --test_native ../examples/test_native 

$(OUT_DIR):
	mkdir $(OUT_DIR)

$(TARGET): $(SRC)
	$(CC) -DAUG_DEBUG=$(DEBUG) -g -o $@ $^ $(CFLAGS) $(LIBS) $(LD)