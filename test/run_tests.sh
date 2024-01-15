#/bin/sh
clear 
make clean 
make
valgrind  --track-origins=yes --leak-check=full --tool=memcheck ../build/shl_test ../build/aug_test --dump ../examples/test --test ../examples/test

valgrind  --tool=memcheck --leak-check=full ../build/aug_test  --verbose \
--test ../examples/test_binops         \
--test ../examples/test_fib            \
--test ../examples/test_fib_recursive  \
--test ../examples/test_func           \
--test ../examples/test_func_local     \
--test ../examples/test_native         
