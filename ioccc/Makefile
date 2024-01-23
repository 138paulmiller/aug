
TARGET = prog
SRC = prog.c
CC = gcc

all: $(TARGET)
	echo "Done"

clean: 
	rm -f $(TARGET)

$(TARGET): $(SRC)
	$(CC) -g -o $@ $^ -Wall -O3 -I../