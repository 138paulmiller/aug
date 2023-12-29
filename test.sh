clear 
make clean 
make; 
valgrind --tool=memcheck ./build/shl_test --test examples/math 