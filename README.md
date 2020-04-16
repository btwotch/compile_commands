# compile_commands

compile_commands hooks into the system, creates a mount namespace and logs invocations of clang, clang++, gcc, g++, ...

It is using mount namespaces.

## Usage

Compile:
```
make
```

Use:
```
./ec bash
cd <dir of sourcecode>
make
exit
```
now you should have a compile_commands.json in your current directory

#### Technical Details
can be found here: https://btwotch.wordpress.com/2020/04/10/compile_commands-json-independent-from-cmake/
