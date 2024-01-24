#/bin/sh
clear 

rm -rf ../build/
mkdir ../build
gcc -g -o ../build/augc main.c -I../ -lm 

valgrind --main-stacksize=1048576 --tool=memcheck --leak-check=full ../build/augc ../examples/test