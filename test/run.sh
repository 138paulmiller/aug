#/bin/sh
clear 
make clean 
make
valgrind  --track-origins=yes --leak-check=full --tool=memcheck \
../build/aug_test --verbose --dump ../examples/test_list --test ../examples/test_list
