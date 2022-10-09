CXX=g++

main: main.cpp
	$(CXX) -g -Wall main.cpp -o main

.PHONY: test
test: main
	./test.py

