CC=gcc
CXX=g++

main: main.cpp  parser.hpp parser.cpp
	$(CXX) -g -Wall main.cpp -o main

