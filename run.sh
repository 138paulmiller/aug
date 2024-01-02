#/bin/sh
valgrind  --leak-check=full --tool=memcheck ./build/shl_test --test examples/math
#valgrind --tool=memcheck ./build/shl_test --test examples/math0 --test examples/math1 --test examples/math2