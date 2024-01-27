#/bin/sh
clear 
rm -r ../build/
mkdir ../build
gcc -g -o ../build/augc main.c -I../ -lm 
valgrind --main-stacksize=1048576 --tool=memcheck --leak-check=full \
../build/augc                   \
../examples/test                \
../examples/test_fib            \
../examples/test_fib_recursive  \
../examples/test_binops         \
../examples/test_func           \
../examples/test_func_callbacks \
../examples/test_func_local     \
../examples/test_stackoverflow  \
../examples/test_array          \