
# AUG

Highly extendable scripting engine. Designed to augment existing applications.

## Usage

### Include

**aug.h** implementation is insipried by the [stb](https://github.com/nothings/stb) style libraries. 

At least one source file must contain the implementation details for this project. 
Meaning at least on c/cpp files must contain the following

```c
#define AUG_IMPLEMENTATION
#include "aug.h"
```

### Virtual Machine

1. Initialize the Aug Virtual Machine (VM). To do this, first startup the VM

```c
aug_vm* vm = aug_startup(NULL);
```

An optional error messaging handler can be provided as such

```c
void on_error(const char* msg)
{
    fprintf(stderr, "[ERROR]%s\t\n", msg);
}
aug_vm* vm = aug_startup(on_error);
```

2. When finished using, cleanup the memory and state 

```c
aug_shutdown(vm);
```

### Executing script

1. To execute an aug script, simply call **aug_execute**.

Here is an example of the minimal use case. Boot a VM instance, compiles, loads the script into the VM, executes, then shutsdown.

```c
aug_vm* vm = aug_startup(NULL);
aug_execute(vm, "path/to/file");
aug_shutdown(vm);
```

## Interoperability

#### C FFI

The C foreign function interface for this project makes use of function *extensions*, which are essentially registered function pointers.
These extension functions are in place to extend the scripting capabilities by providing interoperability between the aug scripts and your native application. 
To register these extensions, users must first define and implement a thin wrapper function in C. 
These external functions expect a function signature similar to the C **main** function signature (see code below).
To bind your local function to a script function you must use the ***aug_register** function.

```c
aug_value function(int argc, aug_value* args) { ... }
aug_register(vm, "function", external_function);
```
argc - the number of values passed into the function
args - array of argument values passed into the function
return -  value returned back to the script calling context

#### AUG FFI 

Inverse to the C FFI, this project provides a mechanism for calling script functions from C.
To do this, the scripts must first be loaded into the VM. This will execute the global statements of the script and retain the state of the into the script. 
Then, at a later point in time, users can call script define functions, that may or may not modify the global state of the script. 

1. Load the script

Loading scripts from external files can be done using the **aug_load** function. This function will return an instance of the script.
Scripts retain their global state, and will keep references to any globals values until they are unloaded using **aug_unload**

Note: This example sssumes the VM has already been booted via **aug_startup**.

```c
aug_script* script = aug_load(vm, "path/to/file.aug");
...
aug_unload(script);
```

2. Call the script functions

Note: This example assumes the VM has already been booted via **aug_startup**.
Note: This example assumes that the *file.aug* contains function defintions for *Setup()* and *Update(delta)*

Once you have a loaded script instance, you can call functions defined in the script using both **aug_call** and **aug_call_args**.

```c
aug_script* script = aug_load(vm. "path/to/file.aug");

aug_call(vm, script, "Setup");

aug_value args[] = { aug_create_float(100) };
aug_call_args(vm, script, "Update", 1, &args[0]);

aug_unload(script);

```

### Interop Example 

A complete example for executing the first 30 integers of the fibonacci series
This example makes use of most of the libraries FFI features, allowing users to define and call functions in both languages.

**fib.aug**

```c
func fibonacci(n) {
	var a = 0;
	var b = 1;
	var c = 0;
	var sum = 0;
	var count = n;
	while count > 0 {
		count = count - 1;
		a = b;
		b = sum;
		sum = a + b;
		print(sum);
	}
}
```

**main.c**

```c
aug_value print(int argc, aug_value* args)
{
	if(argc == 1)
	{
		aug_value value = args[0];
		if(value.type == AUG_INT)
			printf("%d", value.i);
	}                  
}

int main(int argc, char** argv)
{
	aug_vm* vm = aug_startup(NULL);
	aug_register(vm, "print", print);
	aug_script* script = aug_load(vm, "fib.aug");
	aug_value args[] = { aug_create_int(30) };
	aug_call_args(vm, script, "fib", 1, args);
	aug_unload(script);
	aug_shutdown(vm);
 	return 0;
}
```

Note: If these scripts are intended to have a long lifecycle, keeping a handle on the script and VM will allow users to contiuously update and execute the script. 
Something like this:

```c
	aug_vm* vm = aug_startup(NULL);
	aug_register(vm, "print", print);
	aug_script* script = aug_load(vm, "entity.aug");
	bool running = true;
	while(running)
	{
		aug_value args[] = { aug_create_int(time(NULL)) };
		aug_call_args(vm, script, "update", 1, args);
		...
	}
	aug_unload(script);
	aug_shutdown(vm);
```

## Libraries

There is a special keyword, **use** that allows users to load precompiled libraries into the aug runtime. 
In the script calling `use example;` will search for a dynamic library name example. As of now, it will look for the library in the applications working directory. 

To create the example lib, users must include the aug header and define a function that registers all the lib extensions. 
AUG expect the library function entry point to be named aug_register_lib. Below is standard example of a library that will be loaded by the **use** statement.

```c
#include "aug.h"
AUG_LIBCALL void aug_register_lib(aug_vm* vm)
{
	aug_register(vm, "lib_function", lib_function);
	...
}
```
Compiling this is OS specific, for an example library used by the test utility see https://github.com/138paulmiller/aug/test/lib

### Demo

- For examples of use cases see the test cases [here] https://github.com/138paulmiller/aug/test
- For a working demo of misc demos, see https://github.com/138paulmiller/aug_demo

### Syntax

```
NAME    : \w+[\w\d_]+

BINOP   : + - * / < > ^ % == ~=  
         += -= *= /= <= >= ^= %= 
         and or

NUMBER  : [-+]?\d* 
        | [-+]?\d*.\d*
        | 0b[01]+ 
        | 0x[0-9a-fA-F]+

STRING  : ".*"

CHAR    : '.'

BOOL    : true 
        | false

block   : { stmts }

params  : NAME  params
        | , NAME params
        | NULL

stmt    : expr ;
        | VAR NAME = expr ;
        | WHILE expr { stmts }
        | FUNC NAME ( params ) block
        | IF block 
        | IF block ELSE block 
        | IF block ELSE cond

stmts   : stmt stmts
        | NULL

expr    : value 
        | expr BINOP expr 
        | UNOP expr

func_call : NAME ( args )

args    : expr args
        | , expr args
        | NULL

value   : NAME 
        | func_call 
        | NUMBER 
        | STRING 
        | CHAR 
        | BOOL
        | ( expr )
        | [ args ]
```
