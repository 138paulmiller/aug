#/bin/sh
clear 
make clean 
make

valgrind  --main-stacksize=1048576 --tool=memcheck --leak-check=full \
../build/aug_test --dump --test ../examples/test