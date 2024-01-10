#/bin/sh
clear 
make clean 
make
valgrind  --leak-check=full --tool=memcheck ../build/shl_test ../build/shl_test --dump ../examples/test --exec ../examples/test
#../build/shl_test --dump ../examples/test --exec ../examples/test