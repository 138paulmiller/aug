OUT_DIR = build
TARGET = $(OUT_DIR)/aug_test
SRC = $(wildcard *.c)
CC = gcc
CFLAGS = -Wall -O3
LIBS = -I../ -std=c99 -lm
LINK = -rdynamic -Wl,-rpath,../build
DEBUG=0

all: $(OUT_DIR) $(TARGET)
	echo "Done"

clean: 
	rm -r $(OUT_DIR)

$(OUT_DIR):
	mkdir $(OUT_DIR)

$(TARGET): $(SRC)
	$(CC) $(LINK) -DAUG_DEBUG=$(DEBUG) -g -o $@ $^ $(CFLAGS) $(LIBS)
