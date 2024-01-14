#/bin/sh
clear 
make clean 
make
valgrind  --track-origins=yes --leak-check=full --tool=memcheck ../build/shl_test ../build/shl_test --dump ../examples/test --test ../examples/test

../build/shl_test  \
--test ../examples/test_binops         \
--test ../examples/test_fib            \
--test ../examples/test_fib_recursive  \
--test ../examples/test_func           \
--test ../examples/test_func_local     \
--test ../examples/test_native         