#/bin/sh
clear 

if [ "$1" == "-b" ]; then
    make clean 
    make
    shift;
fi;

if [ "$1" == "-bd" ]; then
    make clean 
    make DEBUG=1
    shift;
fi;

valgrind  --main-stacksize=1048576 --tool=memcheck --leak-check=full \
../build/aug_test --dump --verbose --test ../examples/$1
