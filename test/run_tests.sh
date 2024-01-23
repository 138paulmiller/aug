#/bin/sh
clear 
make clean 
make

valgrind  --main-stacksize=1048576 --tool=memcheck --leak-check=full \
../build/aug_test \
--test ../examples/test_binops         \
--test ../examples/test_fib            \
--test ../examples/test_fib_recursive  \
--test ../examples/test_func           \
--test ../examples/test_func_local     \
--test ../examples/test_stackoverflow  \
--test ../examples/test_array          \
--test_native ../examples/test_native  \
--test_game ../examples/test_game  \
