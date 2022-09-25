CC=gcc
CXX=g++

main: main.cpp parser.tab.cpp parser.hpp parser.cpp
	$(CXX) main.cpp -ly -o main

parser.tab.cpp: parser.ypp parser.hpp
	bison --debug parser.ypp