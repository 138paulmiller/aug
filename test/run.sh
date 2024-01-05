#/bin/sh
clear 
make clean 
make
#valgrind  --leak-check=full --tool=memcheck ../build/shl_test --test ../examples/math
../build/shl_test --test ../examples/math0