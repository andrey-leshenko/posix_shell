CXX=g++

# sudo apt install libreadline-dev

main: main.cpp
	$(CXX) -std=c++17 -g -Wall main.cpp -o main -lreadline

.PHONY: test
test: main
	./test.py

