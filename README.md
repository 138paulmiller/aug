
# AUG

Highly extensible and customizeable scripting language engine.


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