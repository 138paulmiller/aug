#/bin/sh
clear 
make clean 
make

valgrind  --main-stacksize=1048576 --tool=memcheck --leak-check=full \
../build/aug_test --test_all $(ls ../examples/test_*)\
 --test_native ../examples/test_native 
