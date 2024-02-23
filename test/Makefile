OUT_DIR = ../build
TARGET = $(OUT_DIR)/aug_test
SRC = $(wildcard *.cpp)
CC = g++
CFLAGS = -Wall -O3
LIBS = -I../ -std=c++11
LINK = -rdynamic -Wl,-rpath,../build
DEBUG=0

all: $(OUT_DIR) $(TARGET)
	echo "Done"

clean: 
	rm -r $(OUT_DIR)

test: lib clean all
	clear;
	valgrind  --main-stacksize=1048576 --tool=memcheck --leak-check=full \
	../build/aug_test --test_all $(shell ls ../examples/test_*)\
	 --test_native ../examples/test_native 

lib:
	cp -rf ./libtest/lib/. ../build

$(OUT_DIR):
	mkdir $(OUT_DIR)

$(TARGET): $(SRC)
	$(CC) $(LINK) -DAUG_DEBUG=$(DEBUG) -g -o $@ $^ $(CFLAGS) $(LIBS)