#/bin/sh
clear 
make clean 
make
valgrind  --track-origins=yes --leak-check=full --tool=memcheck \
../build/aug_test --verbose --dump ../examples/test_game --test_game ../examples/test_game

#../build/aug_test --verbose --dump ../examples/test_func --test ../examples/test_func
