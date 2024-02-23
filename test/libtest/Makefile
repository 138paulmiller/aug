OUT_DIR = lib
CC = gcc
CFLAGS = -I../.. -Wall -Werror
TARGETS = std

.PHONY: $(TARGETS)

all: $(TARGETS)
	echo "Done"

clean: 
	rm -r $(OUT_DIR)

$(OUT_DIR):
	mkdir $(OUT_DIR)

$(TARGETS) : $(OUT_DIR) 
	# compile to position independant object file
	$(CC) $(CFLAGS) -fpic -o $(OUT_DIR)/$@.o -c $@.c

	# Create a shared library from an object file
	$(CC) $(CFLAGS) -shared -o $(OUT_DIR)/$@.so $(OUT_DIR)/$@.o 
