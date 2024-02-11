#/bin/sh
clear 
rm -r ../build/
mkdir ../build
gcc -g -o ../build/augc main.c -I../ -lm 
valgrind --main-stacksize=1048576 --tool=memcheck --leak-check=full \
../build/augc $(ls ../examples/test_*)\
