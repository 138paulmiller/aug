OUT_DIR = build
TARGET = $(OUT_DIR)/aug_test
SCRIPTS = scripts
SRC = $(wildcard *.c)
CC = gcc
CFLAGS = -ggdb -Werror -Wall -Wextra -Wdeprecated -Warray-bounds -Wcast-qual -Wstrict-aliasing -Woverflow -Wsign-conversion -Wconversion -Wbuiltin-macro-redefined -Wcast-align -Wdiscarded-array-qualifiers -Wdiscarded-qualifiers -Wimplicit -Wimplicit-function-declaration -Wimplicit-int
LIBS = -I../ -std=c99 -lm
LINK = -rdynamic -Wl,-rpath,../build
DEBUG=0

all: $(OUT_DIR) $(TARGET) pack
	echo "Done"

pack : $(OUT_DIR)
	cp -r $(SCRIPTS)/* $(OUT_DIR)

clean: 
	rm -r $(OUT_DIR)

$(OUT_DIR):
	mkdir $(OUT_DIR)

$(TARGET): $(SRC)
	$(CC) $(LINK) -DAUG_DEBUG=$(DEBUG) -g -o $@ $^ $(CFLAGS) $(LIBS)

install:
	sudo apt install valgrind linux-tools-generic
