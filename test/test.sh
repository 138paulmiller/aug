#/bin/sh
clear 

if [ "$1" == "build" ]; then
    make clean 
    make
    shift;
fi;

valgrind  --main-stacksize=1048576 --tool=memcheck --leak-check=full \
../build/aug_test --dump --test ../examples/$1