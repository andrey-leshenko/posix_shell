CXX=g++

main: main.cpp
	$(CXX) -std=c++17 -g -Wall main.cpp -o main

.PHONY: test
test: main
	./test.py

