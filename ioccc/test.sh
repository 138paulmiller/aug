#/bin/sh
clear 
make clean 
make
valgrind  --main-stacksize=1048576 --tool=memcheck --leak-check=full \
./prog source
