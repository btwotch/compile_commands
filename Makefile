.PHONY: all

ec: exec_compiler.cpp
	g++ -std=c++17 exec_compiler.cpp -o ec -ggdb -Wall

.PHONY: clean

clean:
	rm -fv ec
