
# AUG

Highly extendable scripting engine. Designed to augment existing applications.

## Usage

### Setup Virtual Machine

1. Initialize the Aug Virtual Machine(VM). To do this, first startup the VM
`aug_vm* vm = aug_startup(NULL);`

An optional error messaging handler can be provided as such
```
void on_error(const char* msg)
{
    fprintf(stderr, "[ERROR]%s\t\n", msg);
}
aug_vm* vm = aug_startup(on_error);
```
 
2. Register external functions, which will provided interoperability between the aug scripts and your application. 
External functions expect the following function signature, similar to the C **main** function signature.
To bind your local function to a script function you must use the ***aug_register** function.
```
aug_value external_function(int argc, aug_value* args) ...
aug_register(vm, "external_function", external_function);
```
argc - the number of values passed into the function
args - array of argument values passed into the function
return -  value returned back to the script calling context 


### Executing script

1. To execute an aug script, simply call **aug_execute**.
```
aug_execute(vm, "path/to/file");
```
2. A complete example for executing the first 30 integers of the fibonacci series

**fib.aug**
```
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
```
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
    aug_execute(vm, "fib.aug");
    aug_shutdown(vm);
    return 0;
}

```

### Calling script functions

3. Loading scripts from external files can be done using the **aug_load** function. This function will return an instance of the script.
   Scripts retain their global state, and will keep references to any globals values until they are unloaded using **aug_unload**

```
aug_script* script = aug_load("path/to/file.aug");
...
aug_unload(script);
```

 4. Once you have a loaded script instance, you can call functions defined in the script using both **aug_call** and **aug_call_args**.
```
aug_value args[] = { aug_create_float(100) };
aug_call(vm, script, "Setup");
...
aug_value args[] = { aug_create_float(100) };
aug_call_args(vm, script, "Update", 1, &args[0]);
```

### Examples

TODO: Sdl example

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
        |  IF block ELSE block 
        |  IF block ELSE cond

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
