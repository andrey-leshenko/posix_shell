CC=gcc
CXX=g++

main: main.cpp parser.tab.cpp parser.hpp
	$(CXX) main.cpp -ly -o main

parser.tab.cpp: parser.ypp parser.hpp
	bison parser.ypp