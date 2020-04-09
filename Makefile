.PHONY: all

all: ec gcc g++ clang clang++ c++

ec: exec_compiler.cpp
	g++ -std=c++17 exec_compiler.cpp -o ec -ggdb -Wall

.PHONY: Makefile

%: ec
	ln -s ec "$@"


.PHONY: clean

clean:
	find ./ -type l -lname ec -exec rm -fv "{}" \;
	rm -fv ec
