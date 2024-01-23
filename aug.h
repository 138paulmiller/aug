/* MIT License

Copyright (c) 2024 Paul Miller (https://github.com/138paulmiller)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE. */

/*
    AUG - embeddable script engine

    * aug.h - single file implementation, self-contained lexer, parser, bytecode compiler, and virtual machine 

    * To use exactly one source file must contain the following 
        #define AUG_IMPLEMENTATION
        #include "aug.h"

    Todo: 
    - Implement for loops
    - Serialize Bytecode to external file. Execute compiled bytecode from file
    - Serialize debug map to file. Link from bytecode to source file. 
    - Opcodes for iterators, values references etc...
    - Create bespoke operations array. 
    - Create bespoke hashmap symtable.
    - Implement objects 
*/
#ifdef __cplusplus
extern "C" {
#endif

#ifndef __AUG_HEADER__
#define __AUG_HEADER__

#define AUG_DEBUG_VM 0 // for console debug

// Size of the virtual machine's value stack 
#ifndef AUG_STACK_SIZE
#define AUG_STACK_SIZE (1024 * 16)
#endif//AUG_STACK_SIZE

// MAx number of user-provided external functions
#ifndef AUG_EXTENSION_SIZE
#define AUG_EXTENSION_SIZE 64
#endif//AUG_EXTENSION_SIZE

// Threshold of the nearly equal operator
#ifndef AUG_APPROX_THRESHOLD
#define AUG_APPROX_THRESHOLD 0.0000001
#endif//AUG_APPROX_THRESHOLD 

#ifndef AUG_ALLOC
#define AUG_ALLOC(type) (type*)(malloc(sizeof(type)))
#endif//AUG_ALLOC

#ifndef AUG_ALLOC_ARRAY
#define AUG_ALLOC_ARRAY(type, count) (type*)(malloc(sizeof(type)*count))
#endif//AUG_ALLOC_ARRAY

#ifndef AUG_REALLOC_ARRAY
#define AUG_REALLOC_ARRAY(ptr, type, count) (type*)(realloc(ptr, sizeof(type)*count))
#endif//AUG_REALLOC_ARRAY

#ifndef AUG_FREE
#define AUG_FREE(ptr) free(ptr)
#endif//AUG_FREE

#ifndef AUG_FREE_ARRAY
#define AUG_FREE_ARRAY(ptr) free(ptr)
#endif//AUG_FREE_ARRAY

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Data structures
typedef struct aug_value aug_value;

typedef struct aug_string
{
	char *buffer;
	int ref_count;
	size_t capacity;
	size_t length;
} aug_string;

aug_string* aug_string_new(size_t size);
aug_string* aug_string_create(const char* bytes);
void aug_string_incref(aug_string* string);
aug_string* aug_string_decref(aug_string* string);
void aug_string_reserve(aug_string* string, size_t size);
void aug_string_push(aug_string* string, char c);
char aug_string_pop(aug_string* string);
char aug_string_at(const aug_string* string, size_t index);
char aug_string_back(const aug_string* string);
bool aug_string_compare(const aug_string* a, const aug_string* b);
bool aug_string_compare_bytes(const aug_string* a, const char* bytes);

typedef struct aug_array
{
	aug_value* buffer;
	int ref_count;
	size_t capacity;
	size_t length;
} aug_array;

aug_array* aug_array_new(size_t size);
void  aug_array_incref(aug_array* array);
aug_array* aug_array_decref(aug_array* array);
void  aug_array_resize(aug_array* array, size_t size);
bool aug_array_index_valid(aug_array* array, size_t index);
aug_value* aug_array_push(aug_array* array);
aug_value* aug_array_pop(aug_array* array);
aug_value* aug_array_at(const aug_array* array, size_t index);
aug_value* aug_array_back(const aug_array* array);


// Object instance
typedef struct aug_object
{
    int ref_count;
    // Value Hash map aug_string -> aug_value
    //aug_std_array<aug_attribute> attribs;
} aug_object;

// Value Types
typedef enum aug_value_type
{
    AUG_BOOL,
    AUG_CHAR,
    AUG_INT, 
    AUG_FLOAT, 
    AUG_STRING, 
    AUG_ARRAY,
    AUG_OBJECT,
    AUG_NONE,
} aug_value_type;

#if defined(AUG_IMPLEMENTATION)
const char* aug_value_type_labels[] =
{
    "bool", "char", "int", "float", "string", "array", "object", "none"
};
static_assert(sizeof(aug_value_type_labels) / sizeof(aug_value_type_labels[0]) == (int)AUG_NONE + 1, "Type labels must be up to date with enum");
#endif //AUG_IMPLEMENTATION

// Values instance 
typedef struct aug_value
{
    aug_value_type type;
    union 
    {
        bool b;
        int i; 
        char c;
        float f;
        aug_string* str;
        aug_object* obj;
        aug_array* array;
    };
} aug_value;

aug_value aug_none();
bool aug_get_bool(const aug_value* value);
int aug_get_int(const aug_value* value);
float aug_get_float(const aug_value* value);
aug_value aug_from_bool(bool data);
aug_value aug_from_int(int data);
aug_value aug_from_char(char data);
aug_value aug_from_float(float data);
aug_value aug_from_string(const char* data);

// Symbol types
typedef enum aug_symbol_type
{
    AUG_SYM_NONE,
    AUG_SYM_VAR,
    AUG_SYM_FUNC,
} aug_symbol_type;

typedef enum aug_symbol_scope
{
    AUG_SYM_SCOPE_LOCAL,
    AUG_SYM_SCOPE_GLOBAL,
    AUG_SYM_SCOPE_PARAM,
} aug_symbol_scope;

// Script symbols 
typedef struct aug_symbol
{
    aug_string* name;
    aug_symbol_scope scope;
    aug_symbol_type type;
    // Functions - offset is the bytecode address, argc is the number of expected params
    // Variables - offset is the stack offset from the base index
    int offset;
    int argc;
} aug_symbol;

typedef struct aug_symtable
{
	aug_symbol* buffer;
	int ref_count;
	size_t capacity;
	size_t length;
} aug_symtable;

aug_symtable* aug_symtable_new(size_t size);
void aug_symtable_incref(aug_symtable* symtable);
aug_symtable* aug_symtable_decref(aug_symtable* symtable);
void  aug_symtable_reserve(aug_symtable* symtable, size_t size);
bool aug_symtable_set(aug_symtable* symtable, aug_symbol symbol);
aug_symbol aug_symtable_get(aug_symtable* symtable, aug_string* name);
aug_symbol aug_symtable_get_bytes(aug_symtable* symtable, const char* name);

// Represents a "compiled" script
typedef struct aug_script
{
    aug_symtable* globals;
    char* bytecode;
    size_t bytecode_size;

    // If the script
    aug_array* stack_state;
} aug_script;

// Calling frames are used to preserve and access parameters and local variables from the stack within a calling context
typedef struct aug_frame
{
    int base_index;
    int stack_index;
    // For function frames, there will be a return instruction and arg count. Otherwise, the frame is used for local scope 

    bool func_call;
    int arg_count;
    const char* instruction; 
} aug_frame;

typedef void(aug_error_function)(const char* /*msg*/);
typedef aug_value /*return*/ (aug_extension)(int argc, const aug_value* /*args*/);

// Running instance of the virtual machine
typedef struct aug_vm
{
    aug_error_function* error_callback;
    bool valid;

    const char* instruction;
    const char* bytecode;
 
    aug_value stack[AUG_STACK_SIZE];
    int stack_index;
    int base_index;

    // Extensions are external functions are native functions that can be called from scripts   
    // This external function map contains the user's registered functions. 
    // Use aug_register/aug_unregister to modify these fields
    aug_extension* extensions[AUG_EXTENSION_SIZE];      
    aug_string* extension_names[AUG_EXTENSION_SIZE]; 
    int extension_count;

    // TODO: debug symtable from addr to func name / variable offsets
} aug_vm;

// VM Must call both startup before using the VM. When done, must call shutdown.
aug_vm* aug_startup(aug_error_function* on_error);
void aug_shutdown(aug_vm* vm);

// Extend the script functions via external functions. 
// NOTE: changing the registered functions will require a script recompilation. Can not guarantee the external function call will work. 
void aug_register(aug_vm* vm, const char* func_name, aug_extension* extension);
void aug_unregister(aug_vm* vm, const char* func_name);

// Will reboot the VM to execute the standalone script or code
void aug_execute(aug_vm* vm, const char* filename);
void aug_evaluate(aug_vm* vm, const char* code);

// Script
bool aug_compile(aug_vm* vm, aug_script* script, const char* filename);

aug_script* aug_script_new();
void aug_script_delete(aug_script* script);

void aug_load(aug_vm* vm, aug_script* script);
void aug_unload(aug_vm* vm, aug_script* script);

aug_value aug_call(aug_vm* vm, aug_script* script, const char* func_name);
aug_value aug_call_args(aug_vm* vm, aug_script* script, const char* func_name, int argc, aug_value* args);

#ifdef __cplusplus
}
#endif

#endif //__AUG_HEADER__

// -------------------------------------- Implementation Details ---------------------------------------// 

#if defined(AUG_IMPLEMENTATION)

#ifdef __cplusplus
extern "C" {
#endif

#include <ctype.h>
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

// --------------------------------------- Generic Containers -----------------------------------------//

typedef struct aug_container
{
	void** buffer;
	size_t capacity;
	size_t length;
} aug_container;

aug_container aug_container_new(size_t size);
void aug_container_delete(aug_container* array);
void  aug_container_reserve(aug_container* array, size_t size);
void aug_container_push(aug_container* array, void* data);
void* aug_container_pop(aug_container* array);
void* aug_container_at(const aug_container* array, size_t index);
void* aug_container_back(const aug_container* array);

// --------------------------------------- Input/Logging ---------------------------------------//

void aug_log_error_internal(aug_error_function* error_callback, const char* format, ...)
{
    // TODO: make thread safe
    static char log_buffer[4096];

    va_list args;
    va_start(args, format);
    if(error_callback)
    {
        vsnprintf(log_buffer, sizeof(log_buffer), format, args);
        error_callback(log_buffer);
    }
    else
    {
#if defined(AUG_LOG_VERBOSE)
        vprintf(format, args);
#endif //defined(AUG_LOG_VERBOSE)
    }
    va_end(args);
}

#define AUG_LOG_ERROR(error_callback, ...)   \
    aug_log_error_internal(error_callback, __VA_ARGS__);

#define AUG_INPUT_ERROR_AT(input, pos, ...) \
if(input->valid)                            \
{                                           \
    input->valid = false;                   \
    aug_input_error_hint(input, pos);       \
    AUG_LOG_ERROR(input->error_callback,    \
        __VA_ARGS__);                       \
}

#define AUG_INPUT_ERROR(input, ...) \
    AUG_INPUT_ERROR_AT(input,       \
        aug_input_prev_pos(input),  \
        __VA_ARGS__);

typedef struct aug_pos
{
    size_t filepos;
    size_t linepos;
    size_t line;
    size_t col;
}aug_pos;

typedef struct aug_input
{
    FILE* file;
    aug_string* filename;
    aug_error_function* error_callback;
    bool valid;

    size_t track_pos;
    size_t pos_buffer_index;
    aug_pos pos_buffer[4];

    char c;
}aug_input;

inline void aug_input_error_hint(aug_input* input, const aug_pos* pos)
{
    assert(input != NULL && input->file != NULL);
    
    // save state
    int curr_pos = ftell(input->file);

    // go to line
    fseek(input->file, pos->linepos, SEEK_SET);

    char buffer[4096];
    size_t n = 0;

    char c = fgetc(input->file);
    // skip leading whitespace
    while(isspace(c))
        c = fgetc(input->file);

    while(c != EOF && c != '\n' && n < (int)(sizeof(buffer) / sizeof(buffer[0]) - 1))
    {
        buffer[n++] = c;
        c = fgetc(input->file);
    }
    buffer[n] = '\0';

    AUG_LOG_ERROR(input->error_callback, "Error %s:(%d,%d) ",
        input->filename->buffer, pos->line + 1, pos->col + 1);

    AUG_LOG_ERROR(input->error_callback, "%s", buffer);

    // Draw arrow to the error if within buffer
    if(pos->col < n-1)
    {
        size_t i;
        for(i = 0; i < pos->col; ++i)
            buffer[i] = ' ';
        buffer[pos->col] = '^';
        buffer[pos->col+1] = '\0';
    }

    AUG_LOG_ERROR(input->error_callback, "%s", buffer);

    // restore state
    fseek(input->file, curr_pos, SEEK_SET);
}

inline aug_pos* aug_input_pos(aug_input* input)
{
    return &input->pos_buffer[input->pos_buffer_index];
}

inline aug_pos* aug_input_prev_pos(aug_input* input)
{
    assert(input != NULL);
    input->pos_buffer_index--;
    if(input->pos_buffer_index < 0)
        input->pos_buffer_index = (sizeof(input->pos_buffer) / sizeof(input->pos_buffer[0])) - 1;
    return aug_input_pos(input);
}

inline aug_pos* aug_input_next_pos(aug_input* input)
{
    assert(input != NULL);
    input->pos_buffer_index = (input->pos_buffer_index + 1) % (sizeof(input->pos_buffer) / sizeof(input->pos_buffer[0]));
    return aug_input_pos(input);
}

inline char aug_input_get(aug_input* input)
{
    if(input == NULL || input->file == NULL)
        return -1;

    input->c = fgetc(input->file);

    aug_pos* pos = aug_input_pos(input);
    aug_pos* next_pos = aug_input_next_pos(input);

    next_pos->line = pos->line;
    next_pos->col = pos->col + 1;
    next_pos->linepos = pos->linepos;
    next_pos->filepos = ftell(input->file);

    if(input->c == '\n')
    {
        next_pos->col = pos->line + 1;
        next_pos->line = pos->line;
        next_pos->linepos = ftell(input->file);
    }
    return input->c;
}

inline char aug_input_peek(aug_input* input)
{
    assert(input != NULL && input->file != NULL);
    char c = fgetc(input->file);
    ungetc(c, input->file);
    return c;
}

inline void aug_input_unget(aug_input* input)
{
    assert(input != NULL && input->file != NULL);
    ungetc(input->c, input->file);
}

aug_input* aug_input_open(const char* filename, aug_error_function* error_callback)
{
    FILE* file = fopen(filename, "r");
    if(file == NULL)
    {
        AUG_LOG_ERROR(error_callback, "Input failed to open file %s", filename);
        return NULL;
    }

    aug_input* input = AUG_ALLOC(aug_input);
    input->valid = true;
    input->error_callback = error_callback;
    input->file = file;
    input->filename = aug_string_create(filename);
    input->pos_buffer_index = 0;
    input->track_pos = 0;
    input->c = -1;

    aug_pos* pos = aug_input_pos(input);
    pos->col = 0;
    pos->line = 0;
    pos->filepos = pos->linepos = ftell(input->file);

    return input;
}

void aug_input_close(aug_input* input)
{
    input->filename = aug_string_decref(input->filename);
    if(input->file != NULL)
        fclose(input->file);

    AUG_FREE(input);
}

inline void aug_input_start_tracking(aug_input* input)
{
    assert(input != NULL && input->file != NULL);
    input->track_pos = ftell(input->file);
}

inline aug_string* aug_input_end_tracking(aug_input* input)
{
    assert(input != NULL && input->file != NULL);

    const size_t pos_end = ftell(input->file);
    const size_t len = (pos_end - input->track_pos);

    aug_string* string = aug_string_new(len+1);
    string->length = len;
    string->buffer[len] = '\0';

    fseek(input->file, input->track_pos, SEEK_SET);
    size_t count = fread(string->buffer, sizeof(char), len, input->file);
    fseek(input->file, pos_end, SEEK_SET);
    if(count != len)
    {
        AUG_INPUT_ERROR(input, "Failed to read %d bytes! %s", len, input->filename->buffer);
        fseek(input->file, pos_end, SEEK_END);
    }

    return string;
}

// -------------------------------------- Lexer  ---------------------------------------// 
// Static token details
typedef struct aug_token_detail
{
    const char* label;    // the string representation, used for visualization and debugging
    char prec;            // if the token is an operator, this is the precedence (note: higher values take precendece)
    int  argc;            // if the token is an operator, this is the number of arguments
    bool capture;         // if non-zero, the token will contain the source string value (i.e. integer and string literals)
    const char* keyword;  // if non-null, the token must match the provided keyword
} aug_token_detail;

#define AUG_TOKEN_LIST                             \
    /* State */                                    \
    AUG_TOKEN(NONE,           0, 0, 0, NULL)       \
    AUG_TOKEN(END,            0, 0, 0, NULL)       \
    /* Symbols */		                           \
    AUG_TOKEN(DOT,            0, 0, 0, NULL)       \
    AUG_TOKEN(COMMA,          0, 0, 0, NULL)       \
    AUG_TOKEN(COLON,          0, 0, 0, NULL)       \
    AUG_TOKEN(SEMICOLON,      0, 0, 0, NULL)       \
    AUG_TOKEN(LPAREN,         0, 0, 0, NULL)       \
    AUG_TOKEN(RPAREN,         0, 0, 0, NULL)       \
    AUG_TOKEN(LBRACKET,       0, 0, 0, NULL)       \
    AUG_TOKEN(RBRACKET,       0, 0, 0, NULL)       \
    AUG_TOKEN(LBRACE,         0, 0, 0, NULL)       \
    AUG_TOKEN(RBRACE,         0, 0, 0, NULL)       \
    /* Operators - Arithmetic */                   \
    AUG_TOKEN(ADD,            2, 2, 0, NULL)       \
    AUG_TOKEN(SUB,            2, 2, 0, NULL)       \
    AUG_TOKEN(MUL,            3, 2, 0, NULL)       \
    AUG_TOKEN(DIV,            3, 2, 0, NULL)       \
    AUG_TOKEN(POW,            3, 2, 0, NULL)       \
    AUG_TOKEN(MOD,            3, 2, 0, NULL)       \
    AUG_TOKEN(AND,            1, 2, 0, "and")      \
    AUG_TOKEN(OR,             1, 2, 0, "or")       \
    /* Operators - Boolean */                      \
    AUG_TOKEN(EQ,             1, 2, 0, NULL)       \
    AUG_TOKEN(LT,             2, 2, 0, NULL)       \
    AUG_TOKEN(GT,             2, 2, 0, NULL)       \
    AUG_TOKEN(LT_EQ,          2, 2, 0, NULL)       \
    AUG_TOKEN(GT_EQ,          2, 2, 0, NULL)       \
    AUG_TOKEN(NOT,            3, 1, 0, NULL)       \
    AUG_TOKEN(NOT_EQ,         2, 2, 0, NULL)       \
    AUG_TOKEN(APPROX_EQ,      1, 2, 0, NULL)       \
    /* Literals */                                 \
    AUG_TOKEN(INT,            0, 0, 1, NULL)       \
    AUG_TOKEN(HEX,            0, 0, 1, NULL)       \
    AUG_TOKEN(BINARY,         0, 0, 1, NULL)       \
    AUG_TOKEN(FLOAT,          0, 0, 1, NULL)       \
    AUG_TOKEN(CHAR,           0, 0, 1, NULL)       \
    AUG_TOKEN(STRING,         0, 0, 1, NULL)       \
    /* Variable/Symbol */                          \
    AUG_TOKEN(NAME,           0, 0, 1, NULL)       \
    AUG_TOKEN(ASSIGN,         0, 0, 0, NULL)       \
    AUG_TOKEN(ADD_ASSIGN,     0, 0, 0, NULL)       \
    AUG_TOKEN(SUB_ASSIGN,     0, 0, 0, NULL)       \
    AUG_TOKEN(MUL_ASSIGN,     0, 0, 0, NULL)       \
    AUG_TOKEN(DIV_ASSIGN,     0, 0, 0, NULL)       \
    AUG_TOKEN(MOD_ASSIGN,     0, 0, 0, NULL)       \
    AUG_TOKEN(POW_ASSIGN,     0, 0, 0, NULL)       \
    /* Keywords */                                 \
    AUG_TOKEN(IF,             0, 0, 0, "if")       \
    AUG_TOKEN(ELSE,           0, 0, 0, "else")     \
    AUG_TOKEN(IN,             0, 0, 0, "in")       \
    AUG_TOKEN(FOR,            0, 0, 0, "for")      \
    AUG_TOKEN(WHILE,          0, 0, 0, "while")    \
    AUG_TOKEN(VAR,            0, 0, 0, "var")      \
    AUG_TOKEN(FUNC,           0, 0, 0, "func")     \
    AUG_TOKEN(RETURN,         0, 0, 0, "return")   \
    AUG_TOKEN(TRUE,           0, 0, 0, "true")     \
    AUG_TOKEN(FALSE,          0, 0, 0, "false")

// Token identifier. 
typedef enum aug_token_id
{
#define AUG_TOKEN(id, ...) AUG_TOKEN_##id,
    AUG_TOKEN_LIST
#undef AUG_TOKEN
    AUG_TOKEN_COUNT
} aug_token_id;

// All token type info. Types map from id to type info
static aug_token_detail aug_token_details[(int)AUG_TOKEN_COUNT] = 
{
#define AUG_TOKEN(id, ...) { #id, __VA_ARGS__},
    AUG_TOKEN_LIST
#undef AUG_TOKEN
};

#undef AUG_TOKEN_LIST

// Token instance
typedef struct  aug_token
{
    aug_token_id id;
    const aug_token_detail* detail; 
    aug_string* data;
    aug_pos pos;
} aug_token;

// Lexer state
typedef struct aug_lexer
{
    aug_input* input;

    aug_token curr;
    aug_token next;

    char comment_symbol;
} aug_lexer;

aug_token aug_token_new()
{
    aug_token token;
    token.id = AUG_TOKEN_NONE;
    token.detail = &aug_token_details[(int)token.id];
    token.data = NULL;
    return token;
}

void aug_token_reset(aug_token* token)
{
    aug_string_decref(token->data);
    *token = aug_token_new();
}

aug_token aug_token_copy(aug_token token)
{
    aug_token new_token = token;
    aug_string_incref(new_token.data);
    return new_token;
}

aug_lexer* aug_lexer_new(aug_input* input)
{
    aug_lexer* lexer = AUG_ALLOC(aug_lexer);
    lexer->input = input;
    lexer->comment_symbol = '#';

    lexer->curr = aug_token_new();
    lexer->next = aug_token_new();

    return lexer;
}

void aug_lexer_delete(aug_lexer* lexer)
{
    aug_token_reset(&lexer->curr);
    aug_token_reset(&lexer->next);

    AUG_FREE(lexer);
}

inline bool aug_lexer_tokenize_char(aug_lexer* lexer, aug_token* token)
{
    char c = aug_input_get(lexer->input);
    assert(c == '\'');

    token->id = AUG_TOKEN_CHAR;
    token->data = aug_string_new(1);

    c = aug_input_get(lexer->input);
    if(c != '\'')
    {
        aug_string_push(token->data, c);
        c = aug_input_get(lexer->input); // eat 
    }
    else
        aug_string_push(token->data, 0);

    if(c != '\'')
    {
        token->data = aug_string_decref(token->data);
        AUG_INPUT_ERROR(lexer->input, "char literal missing closing \"");
        return false;
    }
    return true;
}

inline bool aug_lexer_tokenize_string(aug_lexer* lexer, aug_token* token)
{
    char c = aug_input_get(lexer->input);
    assert(c == '\"');

    token->id = AUG_TOKEN_STRING;
    token->data = aug_string_new(4);

    c = aug_input_get(lexer->input);

    while(c != '\"')
    {
        if(c == EOF)
        {
            token->data = aug_string_decref(token->data);
            AUG_INPUT_ERROR(lexer->input, "string literal missing closing \"");
            return false;
        }

        if(c == '\\')
        {
            // handle escaped chars
            c = aug_input_get(lexer->input);
            switch(c)
            {
            case '\'': 
                aug_string_push(token->data, '\'');
                break;
            case '\"':
                aug_string_push(token->data, '\"');
                break;
            case '\\':
                aug_string_push(token->data, '\\');
                break;
            case '0': //Null
                aug_string_push(token->data, 0x0);
                break;
            case 'a': //Alert beep
                aug_string_push(token->data, 0x07);
                break;
            case 'b': // Backspace
                aug_string_push(token->data, 0x08);
                break;
            case 'f': // Page break
                aug_string_push(token->data, 0x0C);
                break;
            case 'n': // Newline
                aug_string_push(token->data, 0x0A);
                break;
            case 'r': // Carriage return
                aug_string_push(token->data, 0x0D);
                break;
            case 't': // Tab (Horizontal)
                aug_string_push(token->data, 0x09);
                break;
            case 'v': // Tab (Vertical)
                aug_string_push(token->data, 0x0B);
                break;
            default:
                token->data = aug_string_decref(token->data);
                AUG_INPUT_ERROR(lexer->input, "invalid escape character \\%c", c);
                return false;
            }
        }
        else
        {
            aug_string_push(token->data, c);
        }

        c = aug_input_get(lexer->input);
    }

    return true;
}

inline bool aug_lexer_tokenize_symbol(aug_lexer* lexer, aug_token* token)
{
    aug_token_id id = AUG_TOKEN_NONE;

    char c = aug_input_get(lexer->input);
    switch(c)
    {
    case '.': id = AUG_TOKEN_DOT;       break;
    case ',': id = AUG_TOKEN_COMMA;     break;
    case ':': id = AUG_TOKEN_COLON;     break;
    case ';': id = AUG_TOKEN_SEMICOLON; break;
    case '(': id = AUG_TOKEN_LPAREN;    break;
    case ')': id = AUG_TOKEN_RPAREN;    break;
    case '[': id = AUG_TOKEN_LBRACKET;  break;
    case ']': id = AUG_TOKEN_RBRACKET;  break;
    case '{': id = AUG_TOKEN_LBRACE;    break;
    case '}': id = AUG_TOKEN_RBRACE;    break;
    case '+':
        if(aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_ADD_ASSIGN;
        else
            id = AUG_TOKEN_ADD;
        break;
    case '-':
        if(aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_SUB_ASSIGN;
        else
            id = AUG_TOKEN_SUB;
        break;
    case '*':
        if(aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_MUL_ASSIGN;
        else
            id = AUG_TOKEN_MUL;
        break;
    case '/':
        if(aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_DIV_ASSIGN;
        else
            id = AUG_TOKEN_DIV;
        break;
    case '^':
        if(aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_POW_ASSIGN;
        else
            id = AUG_TOKEN_POW;
        break;
    case '%':
        if(aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_MOD_ASSIGN;
        else
            id = AUG_TOKEN_MOD;
        break;
    case '<':
        if(aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_LT_EQ;
        else
            id = AUG_TOKEN_LT;
        break;
    case '>':
        if(aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_GT_EQ;
        else
            id = AUG_TOKEN_GT;
        break;
    case '=':
        if(aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_EQ;
        else
            id = AUG_TOKEN_ASSIGN;
        break;
    case '!':
        if(aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_NOT_EQ;
        else
            id = AUG_TOKEN_NOT;
        break;
    case '~':
        if(aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_APPROX_EQ;
        break;
    }

    if(id == AUG_TOKEN_NONE)
    {
        aug_input_unget(lexer->input);
        return false;
    }

    token->id = id;
    return true;
}

inline bool aug_lexer_tokenize_name(aug_lexer* lexer, aug_token* token)
{
    aug_input_start_tracking(lexer->input);

    char c = aug_input_get(lexer->input);
    if(c != '_' && !isalpha(c))
    {
        aug_input_unget(lexer->input);
        return false;
    }
    
    while(c == '_' || isalnum(c))
        c = aug_input_get(lexer->input);
    aug_input_unget(lexer->input);

    token->id = AUG_TOKEN_NAME;
    token->data = aug_input_end_tracking(lexer->input);

    // find token id for keyword
    for(size_t i = 0; i < (size_t)AUG_TOKEN_COUNT; ++i)
    {
        if(aug_string_compare_bytes(token->data, aug_token_details[i].keyword))
        {
            token->id = (aug_token_id)i;
            token->data = aug_string_decref(token->data); // keyword is static, free token data
            break;
        }
    }
    
    return true;
}

bool aug_lexer_tokenize_number(aug_lexer* lexer, aug_token* token)
{    
    aug_input_start_tracking(lexer->input);

    char c = aug_input_get(lexer->input);
    if(c != '.' && !isdigit(c) && c != '+' && c != '-')
    {
        aug_input_unget(lexer->input);
        return false;
    } 

    aug_token_id id = AUG_TOKEN_NONE;

    if(c == '0' && aug_input_peek(lexer->input) == 'x')
    {
        id = AUG_TOKEN_HEX;

        c = aug_input_get(lexer->input); //eat 'x'
        c = aug_input_get(lexer->input);

        while(isalnum(c))
        {
            if(!isdigit(c) && !((c >='a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                id = AUG_TOKEN_NONE;

            c = aug_input_get(lexer->input);
        }
        aug_input_unget(lexer->input);
    }
    else if(c == '0' && aug_input_peek(lexer->input) == 'b')
    {
        id = AUG_TOKEN_BINARY;

        c = aug_input_get(lexer->input); // eat 'b'
        c = aug_input_get(lexer->input);

        while(isdigit(c))
        {
            if(c != '0' && c != '1')
                id = AUG_TOKEN_NONE;

            c = aug_input_get(lexer->input);
        }
        aug_input_unget(lexer->input);
    }
    else
    {
        if(c == '+' || c == '-')
        {
            c = aug_input_peek(lexer->input);
            if(c != '.' && !isdigit(c))
            {
                aug_input_unget(lexer->input);
                return false;
            }
            c = aug_input_get(lexer->input);
        }

        if(c == '.')
            id = AUG_TOKEN_FLOAT;
        else 
            id = AUG_TOKEN_INT;

        bool dot = false;
        while(c == '.' || isdigit(c))
        {
            if(c == '.')
            {
                if(dot)
                    id = AUG_TOKEN_NONE;
                else
                    id = AUG_TOKEN_FLOAT;

                dot = true;
            }
            c = aug_input_get(lexer->input);
        }
        aug_input_unget(lexer->input);
    }

    token->id = id;
    token->data = aug_input_end_tracking(lexer->input);

    if(id == AUG_TOKEN_NONE)
    {
        AUG_INPUT_ERROR(lexer->input, "invalid numeric format %s", token->data->buffer);
        token->data = aug_string_decref(token->data);
        return false;
    }

    return true;
}

aug_token aug_lexer_tokenize(aug_lexer* lexer)
{
    aug_token token = aug_token_new();

    // if file is not open, or already at then end. return invalid token
    if(lexer->input == NULL || !lexer->input->valid)
        return token;

    if(aug_input_peek(lexer->input) == EOF)
    {
        token.id = AUG_TOKEN_END;
        token.detail = &aug_token_details[(int)token.id];
        return token;
    }

    char c = aug_input_peek(lexer->input);

    // skip whitespace
    if(isspace(c))
    {
        while(isspace(c))
            c = aug_input_get(lexer->input);
        aug_input_unget(lexer->input);
    }

    // skip comments
    while(c == lexer->comment_symbol)
    {        
        while(c != EOF)
        {
            c = aug_input_get(lexer->input);

            if(c == '\n')
            {
                c = aug_input_peek(lexer->input);
                break;
            }
        }

        // skip whitespace
        if(isspace(c))
        {
            while(isspace(c))
                c = aug_input_get(lexer->input);
            aug_input_unget(lexer->input);
        }
    }

    // handle eof
    if(c == EOF)
    {
        token.id = AUG_TOKEN_END;
        token.detail = &aug_token_details[(int)token.id];
        return token;
    }

    token.pos = *aug_input_pos(lexer->input);

    switch (c)
    {
    case '.': 
    case '+':
    case '-':
    {
        // To prevent contention with sign as an operator, if the proceeding token is a number or name (variable)
        // treat sign as an operator, else, treat as the number's sign
        bool allow_sign = true;
        switch(lexer->curr.id)
        {
            case AUG_TOKEN_NAME:
            case AUG_TOKEN_BINARY:
            case AUG_TOKEN_HEX:
            case AUG_TOKEN_FLOAT:
            case AUG_TOKEN_INT:
                allow_sign = false;
                break;
            default:
                break;
        }
        if(!allow_sign || !aug_lexer_tokenize_number(lexer, &token))
            aug_lexer_tokenize_symbol(lexer, &token);
        break;
    }
    case '\"':
        aug_lexer_tokenize_string(lexer, &token);
        break;
    case '\'':
        aug_lexer_tokenize_char(lexer, &token);
        break;
    default:
        if(aug_lexer_tokenize_name(lexer, &token))
            break;
        if(aug_lexer_tokenize_number(lexer, &token))
            break;
    
        if(aug_lexer_tokenize_symbol(lexer, &token))
            break;
        AUG_INPUT_ERROR(lexer->input, "invalid character %c", c);
        break;
    }

    token.detail = &aug_token_details[(int)token.id];
    return token;
}

bool aug_lexer_move(aug_lexer* lexer)
{
    if(lexer == NULL)
        return false;

    if(lexer->next.id == AUG_TOKEN_NONE)
        lexer->next = aug_lexer_tokenize(lexer);        


    aug_token_reset(&lexer->curr);

    lexer->curr = lexer->next;
    lexer->next = aug_lexer_tokenize(lexer);

    return lexer->curr.id != AUG_TOKEN_NONE;
}

// -------------------------------------- Parser / Abstract Syntax Tree ---------------------------------------// 

typedef enum aug_ast_id
{
    AUG_AST_ROOT,
    AUG_AST_BLOCK, 
    AUG_AST_STMT_EXPR,
    AUG_AST_STMT_DEFINE_VAR,
    AUG_AST_STMT_ASSIGN_VAR,
    AUG_AST_STMT_IF,
    AUG_AST_STMT_IF_ELSE,
    AUG_AST_STMT_WHILE,
    AUG_AST_LITERAL, 
    AUG_AST_VARIABLE, 
    AUG_AST_ARRAY,
    AUG_AST_ELEMENT,
    AUG_AST_UNARY_OP, 
    AUG_AST_BINARY_OP, 
    AUG_AST_FUNC_CALL,
    AUG_AST_FUNC_DEF, 
    AUG_AST_PARAM_LIST,
    AUG_AST_PARAM,
    AUG_AST_RETURN,
} aug_ast_id;

typedef struct aug_ast
{
    aug_ast_id id;
    aug_token token;
    struct aug_ast** children;
    int children_size;
    int children_capacity;
} aug_ast;

aug_ast* aug_parse_value(aug_lexer* lexer); 
aug_ast* aug_parse_block(aug_lexer* lexer);

inline aug_ast* aug_ast_new(aug_ast_id id, aug_token token)
{
    aug_ast* node = AUG_ALLOC(aug_ast);
    node->id = id;
    node->token = token;
    node->children = NULL;
    node->children_size = 0;
    node->children_capacity = 0;
    return node;
}

inline void aug_ast_delete(aug_ast* node)
{
    if(node == NULL)
        return;
    if(node->children)
    {
        int i;
        for(i = 0; i < node->children_size; ++i)
            aug_ast_delete(node->children[i]);
        AUG_FREE_ARRAY(node->children);
    }
    aug_token_reset(&node->token);
    AUG_FREE(node);
}

inline void aug_ast_resize(aug_ast* node, int size)
{    
    node->children_capacity = size == 0 ? 1 : size;
    node->children = AUG_REALLOC_ARRAY(node->children, aug_ast*, node->children_capacity);
    node->children_size = size;
}

inline void aug_ast_add(aug_ast* node, aug_ast* child)
{
    if(node->children_size + 1 >= node->children_capacity)
    {
        node->children_capacity = node->children_capacity == 0 ? 1 : node->children_capacity * 2;
        node->children = AUG_REALLOC_ARRAY(node->children, aug_ast*, node->children_capacity);
    }
    node->children[node->children_size++] = child;
}

inline bool aug_parse_expr_pop(aug_lexer* lexer, aug_container* op_stack, aug_container* expr_stack)
{
    //op_stack : aug_token*
    //expr_stack : aug_ast*
    
    aug_token* next_op = (aug_token*)aug_container_pop(op_stack);

    const int op_argc = next_op->detail->argc;
    assert(op_argc == 1 || op_argc == 2); // Only supported operator types

    if(expr_stack->length < (size_t)op_argc)
    {
        while(expr_stack->length > 0)
        {
            aug_ast* expr = (aug_ast*)aug_container_pop(expr_stack);
            aug_ast_delete(expr);
        }
        AUG_INPUT_ERROR(lexer->input, "Invalid number of arguments to operator %s", next_op->detail->label);
        
        AUG_FREE(next_op);
        return false;
    }

    // Push binary op onto stack
    aug_ast_id id = (op_argc == 2) ? AUG_AST_BINARY_OP : AUG_AST_UNARY_OP;
    aug_ast* binaryop = aug_ast_new(id, *next_op);
    aug_ast_resize(binaryop, op_argc);
    
    int i;
    for(i = 0; i < op_argc; ++i)
    {
        aug_ast* expr = (aug_ast*)aug_container_pop(expr_stack);
        binaryop->children[(op_argc-1) - i] = expr; // add in reverse
    }

    aug_container_push(expr_stack, binaryop); 
    AUG_FREE(next_op);
    return true;
}

inline void aug_parse_expr_stack_cleanup(aug_container* op_stack, aug_container* expr_stack)
{
    while(op_stack->length > 0)
    {
        aug_token* token = (aug_token*) aug_container_pop(op_stack);
        AUG_FREE(token);
    }
    while(expr_stack->length > 0)
    {
        aug_ast* expr = (aug_ast*) aug_container_pop(expr_stack);
        aug_ast_delete(expr);
    }
    aug_container_delete(op_stack);
    aug_container_delete(expr_stack);
}

inline aug_ast* aug_parse_expr(aug_lexer* lexer)
{
    // Shunting yard algorithm
    aug_container op_stack = aug_container_new(1);
    aug_container expr_stack = aug_container_new(1);
    while(lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        aug_token op = lexer->curr;

        if(op.detail->prec > 0)
        {
            // left associate by default (for right, <= becomes <)
            while(op_stack.length)
            {
                aug_token* next_op = (aug_token*)aug_container_back(&op_stack);

                if(next_op->detail->prec < op.detail->prec)
                    break;
                if(!aug_parse_expr_pop(lexer, &op_stack, &expr_stack))
                    return NULL;
            }
            aug_token* new_op = AUG_ALLOC(aug_token);
            *new_op = op;
            
            aug_container_push(&op_stack, new_op);
            aug_lexer_move(lexer);
        }
        else
        {
            aug_ast* value = aug_parse_value(lexer);
            if(value == NULL)
                break;

            aug_container_push(&expr_stack, value);
        }
    }

    // Not an expression
    if(op_stack.length == 0 && expr_stack.length == 0)
    {
        aug_container_delete(&op_stack);
        aug_container_delete(&expr_stack);
        return NULL;
    }

    while(op_stack.length)
    {
        if(!aug_parse_expr_pop(lexer, &op_stack, &expr_stack))
        {
            aug_parse_expr_stack_cleanup(&op_stack, &expr_stack);
            return NULL;
        }
    }

    // Not a valid expression. Either malformed or missing semicolon 
    if(expr_stack.length == 0 || expr_stack.length > 1)
    {
        aug_parse_expr_stack_cleanup(&op_stack, &expr_stack);
        AUG_INPUT_ERROR(lexer->input, "Invalid expression syntax");
        return NULL;
    }

    aug_ast* expr = (aug_ast*) aug_container_back(&expr_stack);
    aug_container_delete(&op_stack);
    aug_container_delete(&expr_stack);
    return expr;
}

inline aug_ast* aug_parse_funccall(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_NAME)
        return NULL;

    if(lexer->next.id != AUG_TOKEN_LPAREN)
        return NULL;

    aug_token name_token = aug_token_copy(lexer->curr);

    aug_lexer_move(lexer); // eat NAME
    aug_lexer_move(lexer); // eat LPAREN

    aug_ast* funccall = aug_ast_new(AUG_AST_FUNC_CALL, name_token);
    aug_ast* expr = aug_parse_expr(lexer);
    if(expr != NULL)
    {
        aug_ast_add(funccall, expr);

        while(expr != NULL && lexer->curr.id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            expr = aug_parse_expr(lexer);
            if(expr != NULL)
                aug_ast_add(funccall, expr);
        }
    }

    if(lexer->curr.id != AUG_TOKEN_RPAREN)
    {
        aug_ast_delete(funccall);
        AUG_INPUT_ERROR(lexer->input, "Function call missing closing parentheses");
        return NULL;
    }

    aug_lexer_move(lexer); // eat RPAREN
    return funccall;
}

inline aug_ast* aug_parse_array(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_LBRACKET)
        return NULL;

    aug_lexer_move(lexer); // eat LBRACKET

    aug_ast* array = aug_ast_new(AUG_AST_ARRAY, aug_token_new());
    aug_ast* expr = aug_parse_expr(lexer);
    if(expr != NULL)
    {
        aug_ast_add(array, expr);

        while(expr != NULL && lexer->curr.id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            expr = aug_parse_expr(lexer);
            if(expr != NULL)
                aug_ast_add(array, expr);
        }
    }

    if(lexer->curr.id != AUG_TOKEN_RBRACKET)
    {
        aug_ast_delete(array);
        AUG_INPUT_ERROR(lexer->input, "List missing closing bracket");
        return NULL;
    }

    aug_lexer_move(lexer); // eat RBRACKET
    return array;
}

inline aug_ast* aug_parse_get_element(aug_lexer* lexer)
{
    if(lexer->next.id != AUG_TOKEN_LBRACKET)
        return NULL;

    aug_token name_token = aug_token_copy(lexer->curr);

    aug_lexer_move(lexer); // eat NAME
    aug_lexer_move(lexer); // eat LBRACKET

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        AUG_INPUT_ERROR(lexer->input, "Index operator missing index value");
        return NULL;
    }

    aug_ast* container = aug_ast_new(AUG_AST_VARIABLE, name_token);
    
    aug_ast* element = aug_ast_new(AUG_AST_ELEMENT, aug_token_new());
    aug_ast_resize(element, 2);
    element->children[0] = container;
    element->children[1] = expr;

    if(lexer->curr.id != AUG_TOKEN_RBRACKET)
    {
        aug_ast_delete(element);
        AUG_INPUT_ERROR(lexer->input, "Index operator missing closing bracket");
        return NULL;
    }

    aug_lexer_move(lexer); // eat RBRACKET
    return element;
}

inline aug_ast* aug_parse_value(aug_lexer* lexer)
{
    aug_ast* value = NULL;
    switch (lexer->curr.id)
    {
    case AUG_TOKEN_INT:
    case AUG_TOKEN_HEX:
    case AUG_TOKEN_BINARY:
    case AUG_TOKEN_FLOAT:
    case AUG_TOKEN_STRING:
    case AUG_TOKEN_CHAR:
    case AUG_TOKEN_TRUE:
    case AUG_TOKEN_FALSE:
    {
        aug_token token = aug_token_copy(lexer->curr);
        value = aug_ast_new(AUG_AST_LITERAL, token);

        aug_lexer_move(lexer);
        break;
    }
    case AUG_TOKEN_NAME:
    {   
        // try parse funccall
        value = aug_parse_funccall(lexer);
        if(value != NULL)
            break;

        // try parse index of variable
        value = aug_parse_get_element(lexer);
        if(value != NULL)
            break;

        // consume token. return variable node
        aug_token token = aug_token_copy(lexer->curr);
        value = aug_ast_new(AUG_AST_VARIABLE, token);

        aug_lexer_move(lexer); // eat name
        break;
    }
    case AUG_TOKEN_LBRACKET:
    {
        value = aug_parse_array(lexer);
        break;
    }
    case AUG_TOKEN_LPAREN:
    {
        aug_lexer_move(lexer); // eat LPAREN
        value = aug_parse_expr(lexer);
        if(lexer->curr.id == AUG_TOKEN_RPAREN)
        {
            aug_lexer_move(lexer); // eat RPAREN
        }
        else
        {
            AUG_INPUT_ERROR(lexer->input, "Expression missing closing parentheses");
            aug_ast_delete(value);    
            value = NULL;
        }
        break;
    }
    default: break;
    }
    return value;
}

aug_ast* aug_parse_stmt_expr(aug_lexer* lexer)
{
    aug_ast* expr = aug_parse_expr(lexer);
    if(expr == NULL)
        return NULL;
    
    if(lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        aug_ast_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "Missing semicolon at end of expression");
        return NULL;
    }

    aug_lexer_move(lexer); // eat SEMICOLON
    
    aug_ast* stmt_expr = aug_ast_new(AUG_AST_STMT_EXPR, aug_token_new());
    aug_ast_add(stmt_expr, expr);
    return stmt_expr;
}

aug_ast* aug_parse_stmt_define_var(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_VAR)
        return NULL;

    aug_lexer_move(lexer); // eat VAR

    if(lexer->curr.id != AUG_TOKEN_NAME)
    {
        AUG_INPUT_ERROR(lexer->input,  "Variable assignment expected name");
        return NULL;
    }

    aug_token name_token = aug_token_copy(lexer->curr);
    aug_lexer_move(lexer); // eat NAME

    if(lexer->curr.id == AUG_TOKEN_SEMICOLON)
    {
        aug_lexer_move(lexer); // eat SEMICOLON

        aug_ast* stmt_define = aug_ast_new(AUG_AST_STMT_DEFINE_VAR, name_token);
        return stmt_define;
    }

    if(lexer->curr.id != AUG_TOKEN_ASSIGN)
    {
        aug_token_reset(&name_token);
        AUG_INPUT_ERROR(lexer->input,  "Variable assignment expected \"=\" or ;");
        return NULL;
    }

    aug_lexer_move(lexer); // eat ASSIGN

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        aug_token_reset(&name_token);
        AUG_INPUT_ERROR(lexer->input,  "Variable assignment expected expression after \"=\"");
        return NULL;
    }
    if(lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        aug_token_reset(&name_token);
        aug_ast_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "Variable assignment missing semicolon at end of expression");
        return NULL;
    }

    aug_lexer_move(lexer); // eat SEMICOLON

    aug_ast* stmt_define = aug_ast_new(AUG_AST_STMT_DEFINE_VAR, name_token);
    aug_ast_add(stmt_define, expr);
    return stmt_define;
}

aug_ast* aug_parse_stmt_assign_var(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_NAME)
        return NULL;

    aug_token eq_token = lexer->next;
    aug_token op_token = aug_token_new();

    switch(eq_token.id)
    {
    case AUG_TOKEN_ASSIGN:     op_token.id = AUG_TOKEN_NONE; break;
    case AUG_TOKEN_ADD_ASSIGN: op_token.id = AUG_TOKEN_ADD; break;
    case AUG_TOKEN_SUB_ASSIGN: op_token.id = AUG_TOKEN_SUB; break;
    case AUG_TOKEN_MUL_ASSIGN: op_token.id = AUG_TOKEN_MUL; break;
    case AUG_TOKEN_DIV_ASSIGN: op_token.id = AUG_TOKEN_DIV; break;
    case AUG_TOKEN_MOD_ASSIGN: op_token.id = AUG_TOKEN_MOD; break;
    case AUG_TOKEN_POW_ASSIGN: op_token.id = AUG_TOKEN_POW; break;
    default: return NULL;
    }

    aug_token name_token = aug_token_copy(lexer->curr);
    aug_lexer_move(lexer); // eat NAME
    aug_lexer_move(lexer); // eat ASSIGN

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        aug_token_reset(&name_token);
        AUG_INPUT_ERROR(lexer->input,  "Assignment expected expression after \"=\"");
        return NULL;
    }

    if(lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        aug_token_reset(&name_token);
        aug_ast_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "Missing semicolon at end of expression");
        return NULL;
    }

    aug_lexer_move(lexer); // eat SEMICOLON

    if(op_token.id != AUG_TOKEN_NONE)
    {
        // setup detail
        op_token.detail = &aug_token_details[(int)op_token.id];

        // Create name + expr
        aug_token expr_name_token = aug_token_copy(name_token);
        aug_ast* binaryop = aug_ast_new(AUG_AST_BINARY_OP, op_token);
        aug_ast* value = aug_ast_new(AUG_AST_VARIABLE, expr_name_token);

        // add in reverse order
        aug_ast_resize(binaryop, 2);
        binaryop->children[0] = value;
        binaryop->children[1] = expr;

        aug_ast* stmt_assign = aug_ast_new(AUG_AST_STMT_ASSIGN_VAR, name_token);
        aug_ast_add(stmt_assign, binaryop);
        return stmt_assign;
    }

    aug_ast* stmt_assign = aug_ast_new(AUG_AST_STMT_ASSIGN_VAR, name_token);
    aug_ast_add(stmt_assign, expr);
    return stmt_assign;
}

aug_ast* aug_parse_stmt_if(aug_lexer* lexer);

aug_ast* aug_parse_stmt_if_else(aug_lexer* lexer, aug_ast* expr, aug_ast* block)
{
    aug_lexer_move(lexer); // eat ELSE

    aug_ast* if_else_stmt = aug_ast_new(AUG_AST_STMT_IF_ELSE, aug_token_new());
    aug_ast_resize(if_else_stmt, 3);
    if_else_stmt->children[0] = expr;
    if_else_stmt->children[1] = block;


    // Handling else if becomes else { if ... }
    if(lexer->curr.id == AUG_TOKEN_IF)
    {
        aug_ast* trailing_if_stmt = aug_parse_stmt_if(lexer);
        if(trailing_if_stmt == NULL)
        {
            aug_ast_delete(if_else_stmt);
            return NULL;
        }
        if_else_stmt->children[2] = trailing_if_stmt;
    }
    else
    {
        aug_ast* else_block = aug_parse_block(lexer);
        if(else_block == NULL)
        {
            aug_ast_delete(if_else_stmt);
            AUG_INPUT_ERROR(lexer->input,  "If Else statement missing block");
            return NULL;
        }
        if_else_stmt->children[2] = else_block;
    }

    return if_else_stmt;
}

aug_ast* aug_parse_stmt_if(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_IF)
        return NULL;

    aug_lexer_move(lexer); // eat IF

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        AUG_INPUT_ERROR(lexer->input,  "If statement missing expression");
        return NULL;      
    }

    aug_ast* block = aug_parse_block(lexer);
    if(block == NULL)
    {
        aug_ast_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "If statement missing block");
        return NULL;
    }

    // Parse else 
    if(lexer->curr.id == AUG_TOKEN_ELSE)
        return aug_parse_stmt_if_else(lexer, expr, block);

    aug_ast* if_stmt = aug_ast_new(AUG_AST_STMT_IF, aug_token_new());
    aug_ast_resize(if_stmt, 2);
    if_stmt->children[0] = expr;
    if_stmt->children[1] = block;
    return if_stmt;
}

aug_ast* aug_parse_stmt_while(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_WHILE)
        return NULL;

    aug_lexer_move(lexer); // eat WHILE

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        AUG_INPUT_ERROR(lexer->input,  "While statement missing expression");
        return NULL;
    }

    aug_ast* block = aug_parse_block(lexer);
    if(block == NULL)
    {
        aug_ast_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "While statement missing block");
        return NULL;
    }

    aug_ast* while_stmt = aug_ast_new(AUG_AST_STMT_WHILE, aug_token_new());
    aug_ast_resize(while_stmt, 2);
    while_stmt->children[0] = expr;
    while_stmt->children[1] = block;
    return while_stmt;
}

inline aug_ast* aug_parse_param_list(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_LPAREN)
    {
        AUG_INPUT_ERROR(lexer->input,  "Missing opening parentheses in function parameter list");
        return NULL;
    }

    aug_lexer_move(lexer); // eat LPAREN

    aug_ast* param_list = aug_ast_new(AUG_AST_PARAM_LIST, aug_token_new());
    if(lexer->curr.id == AUG_TOKEN_NAME)
    {
        aug_ast* param = aug_ast_new(AUG_AST_PARAM, aug_token_copy(lexer->curr));
        aug_ast_add(param_list, param);

        aug_lexer_move(lexer); // eat NAME

        while(lexer->curr.id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            if(lexer->curr.id != AUG_TOKEN_NAME)
            {
                AUG_INPUT_ERROR(lexer->input,  "Invalid function parameter. Expected parameter name");
                aug_ast_delete(param_list);
                return NULL;
            }

            aug_ast* param = aug_ast_new(AUG_AST_PARAM, aug_token_copy(lexer->curr));
            aug_ast_add(param_list, param);

            aug_lexer_move(lexer); // eat NAME
        }
    }

    if(lexer->curr.id != AUG_TOKEN_RPAREN)
    {
        AUG_INPUT_ERROR(lexer->input,  "Missing closing parentheses in function parameter list");
        aug_ast_delete(param_list);
        return NULL;
    }

    aug_lexer_move(lexer); // eat RPAREN

    return param_list;
}

aug_ast* aug_parse_stmt_func(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_FUNC)
        return NULL;

    aug_lexer_move(lexer); // eat FUNC

    if(lexer->curr.id != AUG_TOKEN_NAME)
    {
        AUG_INPUT_ERROR(lexer->input,  "Missing name in function definition");
        return NULL;
    }

    aug_token func_name_token = aug_token_copy(lexer->curr);

    aug_lexer_move(lexer); // eat NAME

    aug_ast* param_list = aug_parse_param_list(lexer);
    if(param_list == NULL)
    {
        aug_token_reset(&func_name_token);
        return NULL;
    }

    aug_ast* block = aug_parse_block(lexer);
    if(block == NULL)
    {
        aug_token_reset(&func_name_token);
        aug_ast_delete(param_list);
        return NULL;
    }

    aug_ast* func_def = aug_ast_new(AUG_AST_FUNC_DEF, func_name_token);
    aug_ast_resize(func_def, 2);
    func_def->children[0] = param_list;
    func_def->children[1] = block;
    return func_def;
}

aug_ast* aug_parse_stmt_return(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_RETURN)
        return NULL;

    aug_lexer_move(lexer); // eat RETURN

    aug_ast* return_stmt = aug_ast_new(AUG_AST_RETURN, aug_token_new());

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr != NULL)
        aug_ast_add(return_stmt, expr);

    if(lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        aug_ast_delete(return_stmt);
        AUG_INPUT_ERROR(lexer->input,  "Missing semicolon at end of expression");
        return NULL;
    }

    aug_lexer_move(lexer); // eat SEMICOLON

    return return_stmt;
}

inline aug_ast* aug_parse_stmt(aug_lexer* lexer)
{
    //TODO: assignment, funcdef etc..
    // Default, epxression parsing. 
    aug_ast* stmt = NULL;

    switch (lexer->curr.id)
    {
    case AUG_TOKEN_NAME:
        // TODO: create an assign element
        stmt = aug_parse_stmt_assign_var(lexer);
        if(stmt != NULL)
            break;
        stmt = aug_parse_stmt_expr(lexer);
        break;
    case AUG_TOKEN_IF:
        stmt = aug_parse_stmt_if(lexer);
        break;
    case AUG_TOKEN_WHILE:
        stmt = aug_parse_stmt_while(lexer);
        break;
    case AUG_TOKEN_VAR:
        stmt = aug_parse_stmt_define_var(lexer);
        break;
    case AUG_TOKEN_FUNC:
        stmt = aug_parse_stmt_func(lexer);
        break;
    case AUG_TOKEN_RETURN:
        stmt = aug_parse_stmt_return(lexer);
        break;
    default:
        stmt = aug_parse_stmt_expr(lexer);
        break;
    }

    return stmt;
}

aug_ast* aug_parse_block(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_LBRACE)
    {
        AUG_INPUT_ERROR(lexer->input,  "Block missing opening \"{\"");
        return NULL;
    }
    aug_lexer_move(lexer); // eat LBRACE

    aug_ast* block = aug_ast_new(AUG_AST_BLOCK, aug_token_new());
    while(aug_ast* stmt = aug_parse_stmt(lexer))
        aug_ast_add(block, stmt);

    if(lexer->curr.id != AUG_TOKEN_RBRACE)
    {
        AUG_INPUT_ERROR(lexer->input,  "Block missing closing \"}\"");
        aug_ast_delete(block);
        return NULL;
    }
    aug_lexer_move(lexer); // eat RBRACE

    return block;
}

aug_ast* aug_parse_root(aug_lexer* lexer)
{
    if(lexer == NULL)
        return NULL;

    aug_lexer_move(lexer); // move to first token

    aug_ast* root = aug_ast_new(AUG_AST_ROOT, aug_token_new());
    while(aug_ast* stmt = aug_parse_stmt(lexer))
        aug_ast_add(root, stmt);

    if(root->children_size == 0)
    {
        aug_ast_delete(root);
        return NULL;
    }
    return root;
}

/*
Syntax:
    block := { stmts }

    stmt := stmt_expr
            | stmt_assign
            | stmt_expr
            | stmt_while
            | stmt_func_def

    stmts := stmt stmts
            | NULL

    expr := value 
            | expr BINOP expr 
            | UNOP expr

    func_call := NAME ( args )

    args := expr args
            | , expr args
            | NULL

    value := NAME 
            | func_call 
            | NUMBER 
            | STRING 
            | ( expr )
            | [ args ]

    stmt_expr := expr ;

    stmt_assign := VAR NAME = expr ;

    stmt_if := IF block 
            |  IF block ELSE block 
            |  IF block ELSE stmt_if

    stmt_while := WHILE expr { stmts }

    params := NAME  params
            | , NAME params
            | NULL

    stmt_func_def := FUNC NAME ( params ) block 
*/


aug_ast* aug_parse(aug_vm* vm, aug_input* input)
{
    if(input == NULL)
        return NULL;

    aug_lexer* lexer = aug_lexer_new(input);
    if(lexer == NULL)
        return NULL;

    aug_ast* root = aug_parse_root(lexer);
    aug_lexer_delete(lexer);
    return root;
}

// -------------------------------------- OPCODE -----------------------------------------// 

#define AUG_OPCODE_LIST           \
	AUG_OPCODE(EXIT)              \
	AUG_OPCODE(NO_OP)             \
	AUG_OPCODE(POP)               \
	AUG_OPCODE(PUSH_NONE)         \
	AUG_OPCODE(PUSH_BOOL)         \
	AUG_OPCODE(PUSH_INT)          \
	AUG_OPCODE(PUSH_CHAR)         \
	AUG_OPCODE(PUSH_FLOAT)        \
	AUG_OPCODE(PUSH_STRING)       \
	AUG_OPCODE(PUSH_ARRAY)        \
	AUG_OPCODE(PUSH_LOCAL)        \
	AUG_OPCODE(PUSH_GLOBAL)       \
	AUG_OPCODE(PUSH_ELEMENT)      \
    AUG_OPCODE(PUSH_CALL_FRAME)   \
    AUG_OPCODE(LOAD_LOCAL)        \
	AUG_OPCODE(LOAD_GLOBAL)       \
	AUG_OPCODE(ADD)               \
	AUG_OPCODE(SUB)               \
	AUG_OPCODE(MUL)               \
	AUG_OPCODE(DIV)               \
	AUG_OPCODE(POW)               \
	AUG_OPCODE(MOD)               \
	AUG_OPCODE(AND)               \
	AUG_OPCODE(OR)                \
	AUG_OPCODE(XOR)               \
	AUG_OPCODE(NOT)               \
	AUG_OPCODE(NEG)               \
	AUG_OPCODE(CMP)               \
	AUG_OPCODE(ABS)               \
	AUG_OPCODE(SIN)               \
	AUG_OPCODE(COS)               \
	AUG_OPCODE(ATAN)              \
	AUG_OPCODE(LN)                \
	AUG_OPCODE(SQRT)              \
	AUG_OPCODE(INC)               \
	AUG_OPCODE(DEC)               \
    AUG_OPCODE(LT)                \
	AUG_OPCODE(LTE)               \
	AUG_OPCODE(EQ)                \
	AUG_OPCODE(NEQ)               \
	AUG_OPCODE(APPROXEQ)          \
	AUG_OPCODE(GT)                \
	AUG_OPCODE(GTE)               \
	AUG_OPCODE(JUMP)              \
	AUG_OPCODE(JUMP_ZERO)         \
	AUG_OPCODE(JUMP_NZERO)        \
	AUG_OPCODE(RETURN)            \
	AUG_OPCODE(CALL)              \
	AUG_OPCODE(CALL_EXT)          \
    AUG_OPCODE(DEC_STACK)         \


// Special value used in bytecode to denote an invalid vm offset
#define AUG_OPCODE_INVALID -1

typedef enum aug_opcode : uint8_t
{ 
#define AUG_OPCODE(opcode) AUG_OPCODE_##opcode,
	AUG_OPCODE_LIST
#undef AUG_OPCODE
    AUG_OPCODE_COUNT
}aug_opcode;

static_assert(AUG_OPCODE_COUNT < 255, "AUG Opcode count too large. This will affect bytecode instruction set");

static const char* aug_opcode_labels[] =
{
#define AUG_OPCODE(opcode) #opcode,
	AUG_OPCODE_LIST
#undef AUG_OPCODE
}; 

#undef AUG_OPCODE_LIST

// -------------------------------------- IR --------------------------------------------// 

typedef enum aug_ir_operand_type : uint8_t
{
    // type if operand is constant or literal
    AUG_IR_OPERAND_NONE = 0,
    AUG_IR_OPERAND_BOOL,
    AUG_IR_OPERAND_CHAR,
    AUG_IR_OPERAND_INT,
    AUG_IR_OPERAND_FLOAT,  
    AUG_IR_OPERAND_BYTES,
} aug_ir_operand_type;

typedef struct aug_ir_operand
{
    union
    {
        bool b;
        int i;
        char c;
        float f;
        char bytes[sizeof(float)]; //Used to access raw byte data to bool, float and int types

        const char* str; // NOTE: weak pointer to token data
    } data;

    aug_ir_operand_type type;
} aug_ir_operand;

static_assert(sizeof(float) >= sizeof(int), "Ensure bytes array has enough space to contain both int and float data types");

typedef struct aug_ir_operation
{
    aug_opcode opcode;
    aug_ir_operand operand; //optional parameter. will be encoded in following bytes
    size_t bytecode_offset;
} aug_ir_operation;

typedef struct aug_ir_scope
{
    int base_index;
    int stack_offset;
    aug_symtable* symtable;
} aug_ir_scope;

typedef struct aug_ir_frame
{
    int base_index;
    int arg_count;
    aug_container scope_stack; //aug_ir_scope
} aug_ir_frame;

// All the blocks within a compilation/translation unit (i.e. file, code literal)
typedef struct aug_ir
{		
    aug_input* input; // weak ref to source file/code

    // Transient IR data
    aug_container frame_stack; //aug_ir_frame
    int label_count;

    // Generated data
    aug_container operations; //aug_ir_operation
    size_t bytecode_offset;
    
    // Assigned to the outer-most frame's symbol table. 
    // This field is initialized after generation, so not available during the generation pass. 
    aug_symtable* globals;
    bool valid;
} aug_ir;

inline aug_ir* aug_ir_new(aug_input* input)
{
    aug_ir* ir = AUG_ALLOC(aug_ir);
    ir->valid = true;
    ir->input = input;
    ir->label_count = 0;
    ir->bytecode_offset = 0;
    ir->frame_stack =  aug_container_new(1);
    ir->operations = aug_container_new(1);
    return ir;
}

inline void aug_ir_delete(aug_ir* ir)
{
    size_t i;
    for(i = 0; i < ir->operations.length; ++i)
    {
        aug_ir_operation* operation = (aug_ir_operation*)aug_container_at(&ir->operations, i);
        AUG_FREE(operation);
    }
    aug_container_delete(&ir->operations);
    aug_container_delete(&ir->frame_stack);
    aug_symtable_decref(ir->globals);
    AUG_FREE(ir);
}

inline size_t aug_ir_operand_size(aug_ir_operand operand)
{
    switch (operand.type)
    {
    case AUG_IR_OPERAND_NONE:
        return 0;
    case AUG_IR_OPERAND_BOOL:
        return sizeof(operand.data.b);
    case AUG_IR_OPERAND_CHAR:
        return sizeof(operand.data.c);
    case AUG_IR_OPERAND_INT:
        return sizeof(operand.data.i);
    case AUG_IR_OPERAND_FLOAT:
        return sizeof(operand.data.f);
    case AUG_IR_OPERAND_BYTES:
        return strlen(operand.data.str) + 1; // +1 for null term
    }
    return 0;
}

inline size_t aug_ir_operation_size(const aug_ir_operation* operation)
{
    if(operation == 0)
        return 0;
    size_t size = sizeof(operation->opcode);
    size += aug_ir_operand_size(operation->operand);
    return size;
}

inline size_t aug_ir_add_operation_arg(aug_ir*ir, aug_opcode opcode, aug_ir_operand operand)
{
    aug_ir_operation* operation = AUG_ALLOC(aug_ir_operation);
    operation->opcode = opcode;
    operation->operand = operand;
    operation->bytecode_offset = ir->bytecode_offset;

    ir->bytecode_offset += aug_ir_operation_size(operation);
    aug_container_push(&ir->operations, operation);
    return ir->operations.length-1;
}

inline size_t aug_ir_add_operation(aug_ir*ir, aug_opcode opcode)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_NONE;
    return aug_ir_add_operation_arg(ir, opcode, operand);
}

inline aug_ir_operation* aug_ir_last_operation(aug_ir*ir)
{
    assert(ir->operations.length > 0);
    return (aug_ir_operation*)aug_container_at(&ir->operations, ir->operations.length - 1);
}

inline aug_ir_operation* aug_ir_get_operation(aug_ir*ir, size_t operation_index)
{
    assert(operation_index < ir->operations.length);
    return (aug_ir_operation*)aug_container_at(&ir->operations, operation_index);
}

inline aug_ir_operand aug_ir_operand_from_bool(bool data)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_BOOL;
    operand.data.b = data;
    return operand;
}

inline aug_ir_operand aug_ir_operand_from_char(char data)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_CHAR;
    operand.data.c = data;
    return operand;
}

inline aug_ir_operand aug_ir_operand_from_int(int data)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_INT;
    operand.data.i = data;
    return operand;
}

inline aug_ir_operand aug_ir_operand_from_float(float data)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_FLOAT;
    operand.data.f = data;
    return operand;
}

inline aug_ir_operand aug_ir_operand_from_str(const char* data)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_BYTES;
    operand.data.str = data;
    return operand;
}

inline aug_ir_frame* aug_ir_current_frame(aug_ir*ir)
{
    assert(ir->frame_stack.length > 0);
    return (aug_ir_frame*)aug_container_back(&ir->frame_stack);
}

inline aug_ir_scope* aug_ir_current_scope(aug_ir*ir)
{
    aug_ir_frame* frame = aug_ir_current_frame(ir);
    assert(frame->scope_stack.length > 0);
    return (aug_ir_scope*)aug_container_back(&frame->scope_stack);
}

inline bool aug_ir_current_scope_is_global(aug_ir*ir)
{
    aug_ir_frame* frame = aug_ir_current_frame(ir);
    if(ir->frame_stack.length == 1 && frame->scope_stack.length == 1)
        return true;
    return false;
}

inline int aug_ir_current_scope_local_offset(aug_ir*ir)
{
    const aug_ir_scope* scope = aug_ir_current_scope(ir);
    return scope->stack_offset - scope->base_index;
}

inline int aug_ir_calling_offset(aug_ir*ir)
{
    aug_ir_scope* scope = aug_ir_current_scope(ir);
    aug_ir_frame* frame = aug_ir_current_frame(ir);
    return (scope->stack_offset - frame->base_index) + frame->arg_count;
}

inline void aug_ir_push_frame(aug_ir*ir, int arg_count)
{
    aug_ir_frame* frame = AUG_ALLOC(aug_ir_frame);
    frame->arg_count = arg_count;

    if(ir->frame_stack.length > 0)
    {
        const aug_ir_scope* scope = aug_ir_current_scope(ir);
        frame->base_index = scope->stack_offset;
    }
    else
    {
        frame->base_index = 0;
    }

    aug_ir_scope* scope = AUG_ALLOC(aug_ir_scope);
    scope->base_index = frame->base_index;
    scope->stack_offset = frame->base_index;
    scope->symtable = aug_symtable_new(1);
    
    frame->scope_stack = aug_container_new(1);
    aug_container_push(&frame->scope_stack, scope);
    aug_container_push(&ir->frame_stack, frame);
}

inline void aug_ir_pop_frame(aug_ir*ir)
{
    if(ir->frame_stack.length == 1)
    {
        aug_ir_scope* scope = aug_ir_current_scope(ir);
        ir->globals = scope->symtable;
        scope->symtable = NULL; // Move to globals
    }
    
    aug_ir_frame* frame = (aug_ir_frame*)aug_container_pop(&ir->frame_stack);

    size_t i;
    for(i = 0; i < frame->scope_stack.length; ++i)
    {
        aug_ir_scope* scope = (aug_ir_scope*)aug_container_at(&frame->scope_stack, i);
        aug_symtable_decref(scope->symtable);
        AUG_FREE(scope);
    }
    aug_container_delete(&frame->scope_stack);
    AUG_FREE(frame);
}

inline void aug_ir_push_scope(aug_ir*ir)
{
    const aug_ir_scope* current_scope = aug_ir_current_scope(ir);
    aug_ir_scope* scope = AUG_ALLOC(aug_ir_scope);
    scope->base_index = current_scope->stack_offset;
    scope->stack_offset = current_scope->stack_offset;
    scope->symtable = aug_symtable_new(1);

    aug_ir_frame* frame = aug_ir_current_frame(ir);
    aug_container_push(&frame->scope_stack, scope);
}

inline void aug_ir_pop_scope(aug_ir*ir)
{
    const aug_ir_operand delta = aug_ir_operand_from_int(aug_ir_current_scope_local_offset(ir));
    aug_ir_add_operation_arg(ir, AUG_OPCODE_DEC_STACK, delta);

    aug_ir_frame* frame = aug_ir_current_frame(ir);
    aug_ir_scope* scope = (aug_ir_scope*)aug_container_pop(&frame->scope_stack);
    aug_symtable_decref(scope->symtable);
    AUG_FREE(scope);
}

inline bool aug_ir_set_var(aug_ir*ir, aug_string* var_name)
{
    aug_ir_scope* scope = aug_ir_current_scope(ir);
    const int offset = scope->stack_offset++;

    aug_symbol symbol;
    symbol.name = var_name;
    symbol.type = AUG_SYM_VAR;
    symbol.offset = offset;
    symbol.argc = 0;
    
    if(aug_ir_current_scope_is_global(ir))
        symbol.scope = AUG_SYM_SCOPE_GLOBAL;
    else
        symbol.scope = AUG_SYM_SCOPE_LOCAL;


    return aug_symtable_set(scope->symtable, symbol);
}

inline bool aug_ir_set_param(aug_ir*ir, aug_string* param_name)
{
    aug_ir_scope* scope = aug_ir_current_scope(ir);
    const int offset = scope->stack_offset++;

    aug_symbol symbol;
    symbol.name = param_name;
    symbol.type = AUG_SYM_VAR;
    symbol.offset = offset;
    symbol.argc = 0;
    
    if(aug_ir_current_scope_is_global(ir))
        symbol.scope = AUG_SYM_SCOPE_GLOBAL;
    else
        symbol.scope = AUG_SYM_SCOPE_PARAM;

    return aug_symtable_set(scope->symtable, symbol);
}

inline bool aug_ir_set_func(aug_ir*ir, aug_string* func_name, int param_count)
{
    aug_ir_scope* scope = aug_ir_current_scope(ir);
    const int offset = ir->bytecode_offset;

    aug_symbol symbol;
    symbol.name = func_name;
    symbol.type = AUG_SYM_FUNC;
    symbol.offset = offset;
    symbol.argc = param_count;

    if(aug_ir_current_scope_is_global(ir))
        symbol.scope = AUG_SYM_SCOPE_GLOBAL;
    else
        symbol.scope = AUG_SYM_SCOPE_LOCAL;

    return aug_symtable_set(scope->symtable, symbol);
}

inline aug_symbol aug_ir_get_symbol(aug_ir*ir, aug_string* name)
{
    int i,j;
    for(i = ir->frame_stack.length - 1; i >= 0; --i)
    {
        aug_ir_frame* frame = (aug_ir_frame*) aug_container_at(&ir->frame_stack, i);
        for(j = frame->scope_stack.length - 1; j >= 0; --j)
        {
            aug_ir_scope* scope = (aug_ir_scope*) aug_container_at(&frame->scope_stack, j);
            aug_symbol symbol = aug_symtable_get(scope->symtable, name);
            if(symbol.type != AUG_SYM_NONE)
                return symbol;
        }
    }

    aug_symbol sym;
    sym.offset = AUG_OPCODE_INVALID;
    sym.type = AUG_SYM_NONE;
    sym.argc = 0;
    return sym;
}

inline aug_symbol aug_ir_symbol_relative(aug_ir*ir, aug_string* name)
{
    int i,j;
    for(i = ir->frame_stack.length - 1; i >= 0; --i)
    {
        aug_ir_frame* frame = (aug_ir_frame*) aug_container_at(&ir->frame_stack, i);
        for(j = frame->scope_stack.length - 1; j >= 0; --j)
        {
            aug_ir_scope* scope = (aug_ir_scope*) aug_container_at(&frame->scope_stack, j);
            aug_symbol symbol = aug_symtable_get(scope->symtable, name);
            if(symbol.type != AUG_SYM_NONE)
            {
                switch (symbol.scope)
                {
                case AUG_SYM_SCOPE_GLOBAL:
                    break;
                case AUG_SYM_SCOPE_PARAM:
                {
                    const aug_ir_frame* frame = aug_ir_current_frame(ir);
                    symbol.offset = symbol.offset - frame->base_index;
                    break;
                }
                case AUG_SYM_SCOPE_LOCAL:
                {
                    const aug_ir_frame* frame = aug_ir_current_frame(ir);
                    //If this variable is a local variable in an outer frame. Offset by 2 (ret addr and base index) for each frame delta
                    int frame_delta = (ir->frame_stack.length-1) - i;
                    symbol.offset = symbol.offset - frame->base_index - frame_delta * 2;
                    break;
                }
                }
                return symbol;
            }
        }
    }

    aug_symbol sym;
    sym.offset = AUG_OPCODE_INVALID;
    sym.type = AUG_SYM_NONE;
    sym.argc = 0;
    return sym;
}

inline aug_symbol aug_ir_get_symbol_local(aug_ir*ir, aug_string* name)
{
    aug_ir_scope* scope = aug_ir_current_scope(ir);
    return aug_symtable_get(scope->symtable, name);
}

// --------------------------------------- Value Operations -------------------------------------------------------//
inline bool aug_set_bool(aug_value* value, bool data)
{
    if(value == NULL)
        return false;
    value->type = AUG_BOOL;
    value->b = data;
    return true;
}

inline bool aug_set_int(aug_value* value, int data)
{
    if(value == NULL)
        return false;
    value->type = AUG_INT;
    value->i = data;
    return true;
}

inline bool aug_set_char(aug_value* value, char data)
{
    if(value == NULL)
        return false;
    value->type = AUG_CHAR;
    value->c = data;
    return true;
}

inline bool aug_set_float(aug_value* value, float data)
{
    if(value == NULL)
        return false;
    value->type = AUG_FLOAT;
    value->f = data;
    return true;
}

inline bool aug_set_string(aug_value* value, const char* data)
{
    if(value == NULL)
        return false;

    value->type = AUG_STRING;
    value->str = aug_string_create(data);
    return true;
}

inline bool aug_set_array(aug_value* value)
{
    if(value == NULL)
        return false;

    value->type = AUG_ARRAY;
    value->array = aug_array_new(1);
    return true;
}

aug_value aug_none()
{
    aug_value value;
    value.type = AUG_NONE;
    return value;
}

bool aug_get_bool(const aug_value* value)
{
    if(value == NULL)
        return false;

    switch (value->type)
    {
    case AUG_NONE:
        return false;
    case AUG_BOOL:
        return value->b;
    case AUG_INT:
        return value->i != 0;
    case AUG_CHAR:
        return value->c != 0;
    case AUG_FLOAT:
        return value->f != 0.0f;
    case AUG_STRING:
        return value->str != NULL;
    case AUG_OBJECT:
        return value->obj != NULL;
    case AUG_ARRAY:
        return value->array != NULL;
    }
    return false;
}

int aug_get_int(const aug_value* value)
{    
    if(value == NULL)
        return false;

    switch (value->type)
    {
    case AUG_NONE:
    case AUG_STRING:
    case AUG_OBJECT:
    case AUG_ARRAY:
        return 0;
    case AUG_BOOL:
        return value->b ? 1 : 0;
    case AUG_INT:
        return value->i;
    case AUG_CHAR:
        return (int)value->c;
    case AUG_FLOAT:
        return (int)value->f;
    }
    return 0;
}

float aug_get_float(const aug_value* value)
{
    if(value == NULL)
        return false;

    switch (value->type)
    {
    case AUG_NONE:
    case AUG_STRING:
    case AUG_OBJECT:
    case AUG_ARRAY:
       return 0.0f;
    case AUG_BOOL:
        return value->b ? 1.0f : 0.0f;
    case AUG_INT:
        return (float)value->i;
    case AUG_CHAR:
        return (float)value->c;
    case AUG_FLOAT:
        return value->f;
    }
    return 0.0f;
}

inline void aug_decref(aug_value* value)
{
    if(value == nullptr)
        return;

    switch (value->type)
    {
    case AUG_NONE:
    case AUG_BOOL:
    case AUG_INT:
    case AUG_CHAR:
    case AUG_FLOAT:
        break;
    case AUG_STRING:
        value->str = aug_string_decref(value->str);
        break;
    case AUG_ARRAY:
        if(value->array)
        {
            for(size_t i = 0; i < value->array->length; ++i)
            {
                aug_decref(aug_array_at(value->array, i));
            }
            value->array = aug_array_decref(value->array);
        }
        break;
    case AUG_OBJECT:
        if(value->obj && --value->obj->ref_count <= 0)
        {
            value->type = AUG_NONE;
            AUG_FREE(value->obj);
        }
        break;
    }
}

inline void aug_incref(aug_value* value)
{
    if(value == nullptr)
        return;

    switch (value->type)
    {
    case AUG_STRING:
        aug_string_incref(value->str);
        break;
    case AUG_ARRAY:
        aug_array_incref(value->array);
        break;
    case AUG_OBJECT:
        assert(value->obj);
        ++value->obj->ref_count;
        break;
    default:
        break;
    }
}

inline void aug_assign(aug_value* to, aug_value* from)
{
    if(from == NULL || to == NULL)
        return;

    aug_decref(to);
    *to = *from;
    aug_incref(to);
}

inline void aug_move(aug_value* to, aug_value* from)
{
    if(from == NULL || to == NULL)
        return;

    aug_decref(to);
    *to = *from;
    *from = aug_none();
}

inline bool aug_get_element(aug_value* container, aug_value* index, aug_value* element)
{
    if(container == NULL || index == NULL || element == NULL)
        return false;

    int i = aug_get_int(index);
    if(i < 0)
        return false;

    switch (container->type)
    {
    case AUG_STRING:
    {
        *element = aug_from_char(aug_string_at(container->str, i));
        return true;
    }
    case AUG_ARRAY:
    {
        aug_value* value = aug_array_at(container->array, i);
        if(value)
        {
            *element = *value;
            return true;
        }    
         
    }
    default:
        break;
    }
    *element = aug_none();
    return false;
}

#define AUG_DEFINE_BINOP(result, lhs, rhs,                      \
    int_int_case,                                               \
    int_float_case,                                             \
    float_int_case,                                             \
    float_float_case,                                           \
    char_char_case,                                             \
    bool_bool_case                                              \
)                                                               \
{                                                               \
    if(result == NULL || lhs == NULL || rhs == NULL )           \
        return false;                                           \
    switch(lhs->type)                                           \
    {                                                           \
        case AUG_INT:                                           \
            switch(rhs->type)                                   \
            {                                                   \
                case AUG_INT:   int_int_case;                   \
                case AUG_FLOAT: int_float_case;                 \
                default: break;                                 \
            }                                                   \
            break;                                              \
        case AUG_FLOAT:                                         \
           switch(rhs->type)                                    \
            {                                                   \
                case AUG_INT:   float_int_case;                 \
                case AUG_FLOAT: float_float_case;               \
                default: break;                                 \
            }                                                   \
            break;                                              \
        case AUG_CHAR:                                          \
            switch (rhs->type)                                  \
            {                                                   \
                case AUG_CHAR: char_char_case;                  \
                default: break;                                 \
            }                                                   \
            break;                                              \
        case AUG_BOOL:                                          \
            switch (rhs->type)                                  \
            {                                                   \
                case AUG_BOOL: bool_bool_case;                  \
                default: break;                                 \
            }                                                   \
            break;                                              \
        default: break;                                         \
    }                                                           \
}

inline bool aug_add(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_int(result, lhs->i + rhs->i),
        return aug_set_float(result, lhs->i + rhs->f),
        return aug_set_float(result, lhs->f + rhs->i),
        return aug_set_float(result, lhs->f + rhs->f),
        return aug_set_char(result, lhs->c + rhs->c),
        return false
    )
    return false;
}

inline bool aug_sub(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_int(result, lhs->i - rhs->i),
        return aug_set_float(result, lhs->i - rhs->f),
        return aug_set_float(result, lhs->f - rhs->i),
        return aug_set_float(result, lhs->f - rhs->f),
        return aug_set_char(result, lhs->c - rhs->c),
        return false
    )
    return false;
}

inline bool aug_mul(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_int(result, lhs->i * rhs->i),
        return aug_set_float(result, lhs->i * rhs->f),
        return aug_set_float(result, lhs->f * rhs->i),
        return aug_set_float(result, lhs->f * rhs->f),
        return aug_set_char(result, lhs->c * rhs->c),
        return false
    )
    return false;
}

inline bool aug_div(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_float(result, (float)lhs->i / rhs->i),
        return aug_set_float(result, lhs->i / rhs->f),
        return aug_set_float(result, lhs->f / rhs->i),
        return aug_set_float(result, lhs->f / rhs->f),
        return aug_set_char(result, lhs->c / rhs->c),
        return false
    )
    return false;
}

inline bool aug_pow(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_int(result, (int)powf((float)lhs->i, (float)rhs->i)),
        return aug_set_float(result, powf((float)lhs->i, rhs->f)),
        return aug_set_float(result, powf(lhs->f, (float)rhs->i)),
        return aug_set_float(result, powf(lhs->f, rhs->f)),
        return false,
        return false
    )
    return false;
}

inline bool aug_mod(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_int(result, lhs->i % rhs->i),
        return aug_set_float(result, (float)fmod(lhs->i, rhs->f)),
        return aug_set_float(result, (float)fmod(lhs->f, rhs->i)),
        return aug_set_float(result, (float)fmod(lhs->f, rhs->f)),
        return false,
        return false
    )
    return false;
}

inline bool aug_lt(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_bool(result, lhs->i < rhs->i),
        return aug_set_bool(result, lhs->i < rhs->f),
        return aug_set_bool(result, lhs->f < rhs->i),
        return aug_set_bool(result, lhs->f < rhs->f),
        return aug_set_bool(result, lhs->c < rhs->c),
        return false
    )
    return false;
}

inline bool aug_lte(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_bool(result, lhs->i <= rhs->i),
        return aug_set_bool(result, lhs->i <= rhs->f),
        return aug_set_bool(result, lhs->f <= rhs->i),
        return aug_set_bool(result, lhs->f <= rhs->f),
        return aug_set_bool(result, lhs->c <= rhs->c),
        return false
    )
    return false;
}

inline bool aug_gt(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_bool(result, lhs->i > rhs->i),
        return aug_set_bool(result, lhs->i > rhs->f),
        return aug_set_bool(result, lhs->f > rhs->i),
        return aug_set_bool(result, lhs->f > rhs->f),
        return aug_set_bool(result, lhs->c > rhs->c),
        return false
    )
    return false;
}

inline bool aug_gte(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_bool(result, lhs->i >= rhs->i),
        return aug_set_bool(result, lhs->i >= rhs->f),
        return aug_set_bool(result, lhs->f >= rhs->i),
        return aug_set_bool(result, lhs->f >= rhs->f),
        return aug_set_bool(result, lhs->c >= rhs->c),
        return false
    )
    return false;
}

inline bool aug_eq(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    // TODO: add a special case for object equivalence (per field)
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_bool(result, lhs->i == rhs->i),
        return aug_set_bool(result, lhs->i == rhs->f),
        return aug_set_bool(result, lhs->f == rhs->i),
        return aug_set_bool(result, lhs->f == rhs->f),
        return aug_set_bool(result, lhs->c == rhs->c),
        return aug_set_bool(result, lhs->b == rhs->b)
    )
    return false;
}

inline bool aug_neq(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    // TODO: add a special case for object equivalence (per field)
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_bool(result, lhs->i != rhs->i),
        return aug_set_bool(result, lhs->i != rhs->f),
        return aug_set_bool(result, lhs->f != rhs->i),
        return aug_set_bool(result, lhs->f != rhs->f),
        return aug_set_bool(result, lhs->c != rhs->c),
        return aug_set_bool(result, lhs->b != rhs->b)
    )
    return false;
}

inline bool aug_approxeq(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_bool(result, lhs->i == rhs->i),
        return aug_set_bool(result, abs(lhs->i - rhs->f) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(result, abs(lhs->f - rhs->i) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(result, abs(lhs->f - rhs->f) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(result, lhs->c == rhs->c),
        return aug_set_bool(result, lhs->b == rhs->b)
    )
    return false;
}

inline bool aug_and(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    return aug_set_bool(result, aug_get_bool(lhs) && aug_get_bool(rhs));
}

inline bool aug_or(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    return aug_set_bool(result, aug_get_bool(lhs) || aug_get_bool(rhs));
}

#undef AUG_DEFINE_BINOP

// -------------------------------------- Virtual Machine / Bytecode ----------------------------------------------// 

// Used to convert values to/from bytes for constant values
union aug_vm_bytecode_value
{
    bool b;
    int i;
    char c;
    float f;
    unsigned char bytes[sizeof(float)]; //Used to access raw byte data to bool, float and int types
};

static_assert(sizeof(float) >= sizeof(int), "Ensure bytes array has enough space to contain both int and float data types");

#define AUG_VM_ERROR(vm, ...)                         \
{                                                     \
    AUG_LOG_ERROR(vm->error_callback, __VA_ARGS__);    \
    vm->valid = false;                                 \
    vm->instruction = NULL;                            \
}

#define AUG_VM_UNOP_ERROR(vm, arg, op)                              \
{                                                                   \
    AUG_VM_ERROR(vm, "%s %s not defined",                           \
        op,                                                         \
        arg ? aug_value_type_labels[(int)arg->type] : "(null)");    \
}

#define AUG_VM_BINOP_ERROR(vm, lhs, rhs, op)                        \
{                                                                   \
    AUG_VM_ERROR(vm, "%s %s %s not defined",                        \
        lhs ? aug_value_type_labels[(int)lhs->type] : "(null)",     \
        op,                                                         \
        rhs ? aug_value_type_labels[(int)rhs->type] : "(null)")     \
}

inline aug_value* aug_vm_top(aug_vm* vm)
{
    return &vm->stack[vm->stack_index -1];
}

inline aug_value* aug_vm_push(aug_vm* vm)
{
    if(vm->stack_index >= AUG_STACK_SIZE)
    {                                              
        if(vm->valid)
            AUG_VM_ERROR(vm, "Stack overflow");      
        return NULL;                           
    }
    aug_value* top = &vm->stack[vm->stack_index++];
    return top;
}

inline aug_value* aug_vm_pop(aug_vm* vm)
{
    aug_value* top = aug_vm_top(vm);
    --vm->stack_index;
    return top;
}

inline aug_value* aug_vm_get_global(aug_vm* vm, int stack_offset)
{
    if(stack_offset < 0)
    {
        if(vm->instruction)
            AUG_VM_ERROR(vm, "Stack underflow");
        return NULL;
    }
    else if(stack_offset >= AUG_STACK_SIZE)
    {
        if(vm->instruction)
            AUG_VM_ERROR(vm, "Stack overflow");
        return NULL;
    }

    return &vm->stack[stack_offset];
}

inline void aug_vm_push_call_frame(aug_vm* vm, int return_addr)
{
    aug_value* ret_value = aug_vm_push(vm);
    if(ret_value == NULL)
        return;
    ret_value->type = AUG_INT;
    ret_value->i = return_addr;

    aug_value* base_value = aug_vm_push(vm);
    if(base_value == NULL)
        return;
    base_value->type = AUG_INT;
    base_value->i = vm->base_index;    
}

inline aug_value* aug_vm_get_local(aug_vm* vm, int stack_offset)
{
    return aug_vm_get_global(vm, vm->base_index + stack_offset);
}

inline int aug_vm_read_bool(aug_vm* vm)
{
    aug_vm_bytecode_value bytecode_value;
    for(size_t i = 0; i < sizeof(bytecode_value.b); ++i)
        bytecode_value.bytes[i] = *(vm->instruction++);
    return bytecode_value.b;
}

inline int aug_vm_read_int(aug_vm* vm)
{
    aug_vm_bytecode_value bytecode_value;
    for(size_t i = 0; i < sizeof(bytecode_value.i); ++i)
        bytecode_value.bytes[i] = *(vm->instruction++);
    return bytecode_value.i;
}

inline char aug_vm_read_char(aug_vm* vm)
{
    aug_vm_bytecode_value bytecode_value;
    for(size_t i = 0; i < sizeof(bytecode_value.c); ++i)
        bytecode_value.bytes[i] = *(vm->instruction++);
    return bytecode_value.c;
}

inline float aug_vm_read_float(aug_vm* vm)
{
    aug_vm_bytecode_value bytecode_value;
    for(size_t i = 0; i < sizeof(bytecode_value.f); ++i)
        bytecode_value.bytes[i] = *(vm->instruction++);
    return bytecode_value.f;
}

inline const char* aug_vm_read_bytes(aug_vm* vm)
{
    size_t len = 1; // include null terminating
    while(*(vm->instruction++))
        len++;
    return vm->instruction - len;
}

void aug_vm_startup(aug_vm* vm)
{
    vm->bytecode = NULL;
    vm->instruction = NULL;
    vm->stack_index = 0;
    vm->base_index = 0;
    vm->valid = false; 
}

void aug_vm_shutdown(aug_vm* vm)
{
    //Cleanup stack values. Free any outstanding values
    while(vm->stack_index > 0)
    {
        aug_decref(aug_vm_pop(vm));
    }

    // Ensure that stack has returned to beginning state
    if(vm->stack_index != 0)
        AUG_LOG_ERROR(vm->error_callback, "Virtual machine shutdown error. Invalid stack state");
}

void aug_vm_load_script(aug_vm* vm, const aug_script* script)
{
    if(vm == NULL || script == NULL)
        return;

    if(script->bytecode == NULL && script->bytecode_size == 0)
        vm->bytecode = NULL;
    else
        vm->bytecode = script->bytecode;
    
    vm->instruction = vm->bytecode;
    vm->valid = (vm->bytecode != NULL);

    // Load the script state
    if(script->stack_state != NULL)
    {
        for(size_t i = 0; i < script->stack_state->length; ++i)
        {
            aug_value* top = aug_vm_push(vm);
            if(top)
                *top = *aug_array_at(script->stack_state, i);
        }
    }
}

void aug_vm_unload_script(aug_vm* vm, aug_script* script)
{
    if(script == NULL)
        return;

    // Unload the script state
    if(script->stack_state != NULL)
    {
        for(size_t i = 0; i < script->stack_state->length; ++i)
        {   
            aug_value* value = aug_array_at(script->stack_state, i);
            aug_decref(value);
        }
    }
}

void aug_vm_save_script(aug_vm* vm, aug_script* script)
{
    if(vm == NULL || script == NULL)
        return;

    script->stack_state = aug_array_decref(script->stack_state);
    if(vm->stack_index > 0)
        script->stack_state = aug_array_new(1);

    while(vm->stack_index > 0)
    {
        aug_value* top = aug_vm_pop(vm);
        aug_value* element = aug_array_push(script->stack_state);
        *element = aug_none();
        aug_assign(element, top);
    }
}

void aug_vm_execute(aug_vm* vm)
{
    if(vm == NULL)
        return;

    while(vm->instruction)
    {
        aug_opcode opcode = (aug_opcode) (*vm->instruction);
        ++vm->instruction;

        switch(opcode)
        {
            case AUG_OPCODE_NO_OP:
                break;
            case AUG_OPCODE_EXIT:
            {
                vm->instruction = NULL;
                break;
            }
            case AUG_OPCODE_POP:
            {
                aug_decref(aug_vm_pop(vm));
                break;
            }
            case AUG_OPCODE_PUSH_NONE:
            {
                aug_value* value = aug_vm_push(vm);
                if(value == NULL)
                    break;
                value->type = AUG_NONE;
                break;
            }
            case AUG_OPCODE_PUSH_BOOL:
            {
                aug_value* value = aug_vm_push(vm);
                if(value == NULL)
                    break;
                aug_set_bool(value, aug_vm_read_bool(vm));
                break;
            }
            case AUG_OPCODE_PUSH_INT:   
            {
                aug_value* value = aug_vm_push(vm);
                if(value == NULL) 
                    break;
                aug_set_int(value, aug_vm_read_int(vm));
                break;
            }
            case AUG_OPCODE_PUSH_CHAR:   
            {
                aug_value* value = aug_vm_push(vm);
                if(value == NULL) 
                    break;
                aug_set_char(value, aug_vm_read_char(vm));
                break;
            }
            case AUG_OPCODE_PUSH_FLOAT:
            {
                aug_value* value = aug_vm_push(vm);
                if(value == NULL) 
                    break;
                aug_set_float(value, aug_vm_read_float(vm));
                break;
            }                                  
            case AUG_OPCODE_PUSH_STRING:
            {
                aug_value value;
                aug_set_string(&value, aug_vm_read_bytes(vm));

                aug_value* top = aug_vm_push(vm);                
                aug_move(top, &value);
                break;
            }
           case AUG_OPCODE_PUSH_ARRAY:
            {
                aug_value value;
                aug_set_array(&value);

                int count = aug_vm_read_int(vm);
                while(count-- > 0)
                {
                    aug_value* element = aug_array_push(value.array);
                    if(element != NULL) 
                    {
                        *element = aug_none();
                        aug_move(element, aug_vm_pop(vm));
                    }
                }

                aug_value* top = aug_vm_push(vm);          
                aug_move(top, &value);
                break;
            }
            case AUG_OPCODE_PUSH_LOCAL:
            {
                const int stack_offset = aug_vm_read_int(vm);
                aug_value* local = aug_vm_get_local(vm, stack_offset);

                aug_value* top = aug_vm_push(vm);
                aug_assign(top, local);
                break;
            }
            case AUG_OPCODE_PUSH_GLOBAL:
            {
                const int stack_offset = aug_vm_read_int(vm);
                aug_value* local = aug_vm_get_global(vm, stack_offset);

                aug_value* top = aug_vm_push(vm);
                aug_assign(top, local);
                break;
            }
            case AUG_OPCODE_PUSH_ELEMENT:
            {
                // Pop the list, then expression
                aug_value* index_expr = aug_vm_pop(vm);
                aug_value* container = aug_vm_pop(vm);

                aug_value value;
                if(!aug_get_element(container, index_expr, &value))    
                {
                    AUG_VM_ERROR(vm, "Index error"); // TODO: more descriptive
                    break;  
                }

                aug_value* top = aug_vm_push(vm);
                aug_move(top, &value);

                aug_decref(container);
                aug_decref(index_expr);
                break;
            }
            case AUG_OPCODE_LOAD_LOCAL:
            {
                const int stack_offset = aug_vm_read_int(vm);
                aug_value* local = aug_vm_get_local(vm, stack_offset);

                aug_value* top = aug_vm_pop(vm);
                aug_move(local, top);
                break;
            }
            case AUG_OPCODE_LOAD_GLOBAL:
            {
                const int stack_offset = aug_vm_read_int(vm);
                aug_value* local = aug_vm_get_global(vm, stack_offset);

                aug_value* top = aug_vm_pop(vm);
                aug_move(local, top);
                break;
            }            
            case AUG_OPCODE_ADD:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);    
                if(!aug_add(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "+");
                break;
            }
            case AUG_OPCODE_SUB:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);    
                if(!aug_sub(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "-");
                break;
            }
            case AUG_OPCODE_MUL:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);    
                if(!aug_mul(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "*");
                break;
            }
            case AUG_OPCODE_DIV:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);    
                if(!aug_div(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "/");
                break;
            }
            case AUG_OPCODE_POW:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if(!aug_pow(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "^");
                break;
            }
            case AUG_OPCODE_MOD:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if(!aug_mod(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "%%");
                break;
            }
            case AUG_OPCODE_NOT:
            {
                aug_value* arg = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if(!aug_set_bool(target, !aug_get_bool(arg)))
                    AUG_VM_UNOP_ERROR(vm, arg, "!");
                break;
            }
            case AUG_OPCODE_AND:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if(!aug_and(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "&");
                break;
            }
            case AUG_OPCODE_OR:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if(!aug_or(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "|");
                break;
            }
            case AUG_OPCODE_LT:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if(!aug_lt(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "<");
                break;
            }
            case AUG_OPCODE_LTE:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if(!aug_lte(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "<=");
                break;
            }
            case AUG_OPCODE_GT:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if(!aug_gt(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, ">");
                break;
            }
            case AUG_OPCODE_GTE:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if(!aug_gte(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, ">=");
                break;
            }
            case AUG_OPCODE_EQ:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if(!aug_eq(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "==");
                break;
            }
            case AUG_OPCODE_NEQ:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if(!aug_neq(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "!=");
                break;
            }
            case AUG_OPCODE_APPROXEQ:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if(!aug_approxeq(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "~=");
                break;
            }
            case AUG_OPCODE_JUMP:
            {
                const int instruction_offset = aug_vm_read_int(vm);
                vm->instruction = vm->bytecode + instruction_offset;
                break;
            }
            case AUG_OPCODE_JUMP_NZERO:
            {
                const int instruction_offset = aug_vm_read_int(vm);
                aug_value* top = aug_vm_pop(vm);
                if(aug_get_bool(top) != 0)
                    vm->instruction = vm->bytecode + instruction_offset;
                aug_decref(top);
                break;
            }
            case AUG_OPCODE_JUMP_ZERO:
            {
                const int instruction_offset = aug_vm_read_int(vm);
                aug_value* top = aug_vm_pop(vm);
                if(aug_get_bool(top) == 0)
                    vm->instruction = vm->bytecode + instruction_offset;
                aug_decref(top);
                break;
            }
            case AUG_OPCODE_DEC_STACK:
            {
                const int delta = aug_vm_read_int(vm);
                int i;
                for(i = 0; i < delta; ++i)
                    aug_decref(aug_vm_pop(vm));
                break;
            }
            case AUG_OPCODE_PUSH_CALL_FRAME:
            {
                const int ret_addr = aug_vm_read_int(vm);
                aug_vm_push_call_frame(vm, ret_addr);
                break;
            }
            case AUG_OPCODE_CALL:
            {
                const int func_addr = aug_vm_read_int(vm);
                vm->instruction = vm->bytecode + func_addr;
                vm->base_index = vm->stack_index;
                break;
            }
            case AUG_OPCODE_RETURN:
            {
                // get func return value
                aug_value* ret_value = aug_vm_pop(vm);
                
                // Free locals
                const int delta = aug_vm_read_int(vm);
                int i;
                for(i = 0; i < delta; ++i)
                    aug_decref(aug_vm_pop(vm));
                
                // Restore base index
                aug_value* ret_base = aug_vm_pop(vm);
                if(ret_base == NULL)
                {                    
                    AUG_VM_ERROR(vm, "Calling frame setup incorrectly. Stack missing stack base");
                    break;
                }

                vm->base_index = ret_base->i;
                aug_decref(ret_base);

                // jump to return instruction
                aug_value* ret_addr = aug_vm_pop(vm);
                if(ret_addr == NULL)
                {                    
                    AUG_VM_ERROR(vm, "Calling frame setup incorrectly. Stack missing return address");
                    break;
                }
                
                if(ret_addr->i == AUG_OPCODE_INVALID)
                    vm->instruction = NULL;
                else
                    vm->instruction = vm->bytecode + ret_addr->i;
                aug_decref(ret_addr);

                // push return value back onto stack, for callee
                aug_value* top = aug_vm_push(vm);
                if(ret_value != NULL && top != NULL)
                    *top = *ret_value;

                break;
            }
            case AUG_OPCODE_CALL_EXT:
            {
                aug_value* func_index_value = aug_vm_pop(vm);
                if(func_index_value == NULL || func_index_value->type != AUG_INT)
                {
                    aug_decref(func_index_value);

                    AUG_VM_ERROR(vm, "External Function Call expected function index to be pushed on stack");
                    break;                    
                }

                const int arg_count = aug_vm_read_int(vm);                
                const int func_index = func_index_value->i;

                // Gather arguments
                aug_value* args = AUG_ALLOC_ARRAY(aug_value, arg_count);
                int i;
                for(i = arg_count - 1; i >= 0; --i)
                {
                    aug_value* arg = aug_vm_pop(vm);
                    if(arg != NULL)
                    {
                        aug_value value = aug_none();
                        aug_move(&value, arg);
                        args[arg_count - i - 1] = value;
                    }
                }
          
                // Check function call
                if(func_index >= 0 && func_index < AUG_EXTENSION_SIZE && vm->extensions[func_index] != NULL)
                {
                    // Call the external function. Move return value on to top of stack
                    aug_value ret_value = vm->extensions[func_index](arg_count, args);
                    aug_value* top = aug_vm_push(vm);
                    if(top)
                        aug_move(top, &ret_value);
                }
                else
                {
                    AUG_VM_ERROR(vm, "External Function Called at index %d not registered", func_index);
                }

                // Cleanup arguments
                aug_decref(func_index_value);
                for(i = 0; i < arg_count; ++i)
                    aug_decref(&args[i]);
                AUG_FREE_ARRAY(args);
                break;
            }
            default:
                // UNSUPPORTED!!!!
            break;
        }

#if AUG_DEBUG_VM
        printf("OP:   %s\n", aug_opcode_labels[(int)opcode]);
        for(size_t i = 0; i < 16; ++i)
        {
            aug_value val = vm->stack[i];
            printf("%s %d: %s ", (vm->stack_index-1) == i ? ">" : " ", i, aug_value_type_labels[(int)val.type]);
            switch(val.type)
            {
                case AUG_INT: printf("%d", val.i); break;
                case AUG_FLOAT: printf("%f", val.f); break;
                case AUG_STRING: printf("%s", val.str->c_str()); break;
                case AUG_BOOL: printf("%s", val.b ? "true" : "false"); break;
                case AUG_CHAR: printf("%c", val.c); break;
                default: break;
            }
            printf("\n");
        }
        getchar();
#endif 
    }
}

aug_value aug_vm_execute_from_frame(aug_vm* vm, int func_addr, int argc, aug_value* args)
{
    // Manually set expected call frame
    aug_vm_push_call_frame(vm, AUG_OPCODE_INVALID);

    // Jump to function call
    vm->instruction = vm->bytecode + func_addr;

    int i;
    for(i = 0; i < argc; ++i)
    {
        aug_value* value = aug_vm_push(vm);
        if(value)
            *value = args[i];
    }

    vm->base_index = vm->stack_index;
    aug_vm_execute(vm);

    aug_value ret_value = aug_none();
    if(vm->stack_index > 1)
    {
        // If stack is valid, get the pushed value
        aug_value* top = aug_vm_pop(vm);
        if(top)
            ret_value = *top;
    }
    return ret_value;
}

// -------------------------------------- Passes ------------------------------------------------// 
bool aug_pass_semantic_check(const aug_ast* node)
{
    if(node == NULL)
        return false;
    return true;
}
// -------------------------------------- Transformations ---------------------------------------// 

void aug_ast_to_ir(aug_vm* vm, const aug_ast* node, aug_ir*ir)
{
    if(node == NULL || !ir->valid)
        return;

    const aug_token token = node->token;
    aug_string* token_data = token.data; 
    aug_ast** children = node->children;
    const int children_size = node->children_size;

    switch(node->id)
    {
        case AUG_AST_ROOT:
        {
            aug_ir_push_frame(ir, 0); // push a global frame

            int i;
            for(i = 0; i < children_size; ++ i)
                aug_ast_to_ir(vm, children[i], ir);

            aug_ir_add_operation(ir, AUG_OPCODE_EXIT);

            aug_ir_pop_frame(ir); // pop global frame
            break;
        }
        case AUG_AST_BLOCK: 
        {
            int i;
            for(i = 0; i < children_size; ++ i)
                aug_ast_to_ir(vm, children[i], ir);

            break;
        }
        case AUG_AST_LITERAL:
        {
            switch (token.id)
            {
                case AUG_TOKEN_CHAR:
                {
                    assert(token_data && token_data->length == 1);
                    const char data = token_data->buffer[0];
                    const aug_ir_operand& operand = aug_ir_operand_from_char(data);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_CHAR, operand);
                    break;
                }
                case AUG_TOKEN_INT:
                {
                    assert(token_data && token_data->length > 0);
                    const int data = strtol(token_data->buffer, NULL, 10);
                    const aug_ir_operand& operand = aug_ir_operand_from_int(data);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_INT, operand);
                    break;
                }
                case AUG_TOKEN_HEX:
                {
                    assert(token_data && token_data->length > 0);
                    const unsigned int data = strtoul(token_data->buffer, NULL, 16);
                    const aug_ir_operand& operand = aug_ir_operand_from_int(data);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_INT, operand);
                    break;
                }
                case AUG_TOKEN_BINARY:
                {
                    assert(token_data && token_data->length > 0);
                    const unsigned int data = strtoul(token_data->buffer, NULL, 2);
                    const aug_ir_operand& operand = aug_ir_operand_from_int(data);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_INT, operand);
                    break;
                }
                case AUG_TOKEN_FLOAT:
                {
                    assert(token_data && token_data->length > 0);
                    const float data = strtof(token_data->buffer, NULL);
                    const aug_ir_operand& operand = aug_ir_operand_from_float(data);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_FLOAT, operand);
                    break;
                }
                case AUG_TOKEN_STRING:
                {
                    const aug_ir_operand& operand = aug_ir_operand_from_str(token_data->buffer);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_STRING, operand);
                    break;
                }
                case AUG_TOKEN_TRUE:
                {
                    const aug_ir_operand& operand = aug_ir_operand_from_bool(true);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_BOOL, operand);
                    break;
                }
                case AUG_TOKEN_FALSE:
                {
                    const aug_ir_operand& operand = aug_ir_operand_from_bool(false);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_BOOL, operand);
                    break;
                }
                default:
                    assert(0);
                    break;
            }
            break;
        }
        case AUG_AST_VARIABLE:
        {
            assert(token_data != NULL);

            const aug_symbol& symbol = aug_ir_symbol_relative(ir, token_data);
            if(symbol.type == AUG_SYM_NONE)
            {
                ir->valid = false;
                AUG_INPUT_ERROR_AT(ir->input, &token.pos, "Variable %s not defined in current block", token_data->buffer);
                return;
            }

            if(symbol.type == AUG_SYM_FUNC)
            {
                ir->valid = false;
                AUG_INPUT_ERROR_AT(ir->input, &token.pos, "Function %s can not be used as a variable", token_data->buffer);
                return;
            }

            const aug_ir_operand& address_operand = aug_ir_operand_from_int(symbol.offset);
            if(symbol.scope == AUG_SYM_SCOPE_GLOBAL)
                aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_GLOBAL, address_operand);
            else
                aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_LOCAL, address_operand);
            break;
        }
        case AUG_AST_UNARY_OP:
        {
            assert(children_size == 1); // token [0]

            aug_ast_to_ir(vm, children[0], ir);

            switch (token.id)
            {
            case AUG_TOKEN_NOT: aug_ir_add_operation(ir, AUG_OPCODE_NOT); break;
            default:
                assert(0);
                break;
            }
            break;
        }
        case AUG_AST_BINARY_OP:
        {
            assert(children_size == 2); // [0] token [1]

            aug_ast_to_ir(vm, children[0], ir); // LHS
            aug_ast_to_ir(vm, children[1], ir); // RHS

            switch (token.id)
            {
                case AUG_TOKEN_ADD:       aug_ir_add_operation(ir, AUG_OPCODE_ADD);      break;
                case AUG_TOKEN_SUB:       aug_ir_add_operation(ir, AUG_OPCODE_SUB);      break;
                case AUG_TOKEN_MUL:       aug_ir_add_operation(ir, AUG_OPCODE_MUL);      break;
                case AUG_TOKEN_DIV:       aug_ir_add_operation(ir, AUG_OPCODE_DIV);      break;
                case AUG_TOKEN_MOD:       aug_ir_add_operation(ir, AUG_OPCODE_MOD);      break;
                case AUG_TOKEN_POW:       aug_ir_add_operation(ir, AUG_OPCODE_POW);      break;
                case AUG_TOKEN_AND:       aug_ir_add_operation(ir, AUG_OPCODE_AND);      break;
                case AUG_TOKEN_OR:        aug_ir_add_operation(ir, AUG_OPCODE_OR);       break;
                case AUG_TOKEN_LT:        aug_ir_add_operation(ir, AUG_OPCODE_LT);       break;
                case AUG_TOKEN_LT_EQ:     aug_ir_add_operation(ir, AUG_OPCODE_LTE);      break;
                case AUG_TOKEN_GT:        aug_ir_add_operation(ir, AUG_OPCODE_GT);       break;
                case AUG_TOKEN_GT_EQ:     aug_ir_add_operation(ir, AUG_OPCODE_GTE);      break;
                case AUG_TOKEN_EQ:        aug_ir_add_operation(ir, AUG_OPCODE_EQ);       break;
                case AUG_TOKEN_NOT_EQ:    aug_ir_add_operation(ir, AUG_OPCODE_NEQ);      break;
                case AUG_TOKEN_APPROX_EQ: aug_ir_add_operation(ir, AUG_OPCODE_APPROXEQ); break;
                default: break;
            }
            break;
        }
        case AUG_AST_ARRAY:
        {
            int i;
            for(i = children_size - 1; i >= 0; --i)
                aug_ast_to_ir(vm, children[i], ir);

            const aug_ir_operand& count_operand = aug_ir_operand_from_int(children_size);
            aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_ARRAY, count_operand);
            break;
        }
        case AUG_AST_ELEMENT:
        {
            assert(children_size == 2); // 0[1]
            aug_ast_to_ir(vm, children[0], ir); // push container var
            aug_ast_to_ir(vm, children[1], ir); // push index
            aug_ir_add_operation(ir, AUG_OPCODE_PUSH_ELEMENT);
            break;
        }
        case AUG_AST_STMT_EXPR:
        {
            if(children_size == 1)
            {
                aug_ast_to_ir(vm, children[0], ir);
                // discard the top if a non-assignment binop
                aug_ir_add_operation(ir, AUG_OPCODE_POP);
            }
            break;
        }
        case AUG_AST_STMT_ASSIGN_VAR:
        {
            assert(token_data != NULL);
            assert(children_size == 1); // token = [0]
            
            aug_ast_to_ir(vm, children[0], ir);

            const aug_symbol& symbol = aug_ir_symbol_relative(ir, token_data);
            if(symbol.type == AUG_SYM_NONE)
            {
                aug_ir_set_var(ir, token_data);
                return;
            }
            else if(symbol.type == AUG_SYM_FUNC)
            {
                ir->valid = false;
                AUG_INPUT_ERROR_AT(ir->input, &token.pos, "Can not assign function %s as a variable", token_data->buffer);
                return;
            }

            const aug_ir_operand& address_operand = aug_ir_operand_from_int(symbol.offset);
            if(symbol.scope == AUG_SYM_SCOPE_GLOBAL)
                aug_ir_add_operation_arg(ir, AUG_OPCODE_LOAD_GLOBAL, address_operand);
            else
                aug_ir_add_operation_arg(ir, AUG_OPCODE_LOAD_LOCAL, address_operand);
            break;
        }
        case AUG_AST_STMT_DEFINE_VAR:
        {
            if(children_size == 1) // token = [0]
                aug_ast_to_ir(vm, children[0], ir);
            else
                aug_ir_add_operation(ir, AUG_OPCODE_PUSH_NONE);

            const aug_symbol symbol = aug_ir_get_symbol_local(ir, token_data);
            if(symbol.offset != AUG_OPCODE_INVALID)
            {
                ir->valid = false;
                AUG_INPUT_ERROR_AT(ir->input, &token.pos, "Variable %s already defined in block", token_data->buffer);
                return;
            }

            aug_ir_set_var(ir, token_data);
        
            break;
        }
        case AUG_AST_STMT_IF:
        {
            assert(children_size == 2); //if([0]) {[1]}

            const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);

            // Evaluate expression. 
            aug_ast_to_ir(vm, children[0], ir);

            //Jump to end if false
            const size_t end_block_jmp = aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP_ZERO, stub_operand);

            // True block
            aug_ir_push_scope(ir);
            aug_ast_to_ir(vm, children[1], ir);
            aug_ir_pop_scope(ir);

            const size_t end_block_addr = ir->bytecode_offset;

            // Fixup stubbed block offsets
            aug_ir_get_operation(ir, end_block_jmp)->operand = aug_ir_operand_from_int(end_block_addr);

            break;
        }
        case AUG_AST_STMT_IF_ELSE:
        {
            assert(children_size == 3); //if([0]) {[1]} else {[2]}

            const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);

            // Evaluate expression. 
            aug_ast_to_ir(vm, children[0], ir);

            //Jump to else if false
            const size_t else_block_jmp = aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP_ZERO, stub_operand);

            // True block
            aug_ir_push_scope(ir);
            aug_ast_to_ir(vm, children[1], ir);
            aug_ir_pop_scope(ir);

            //Jump to end after true
            const size_t end_block_jmp = aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP, stub_operand);
            const size_t else_block_addr = ir->bytecode_offset;

            // Else block
            aug_ir_push_scope(ir);
            aug_ast_to_ir(vm, children[2], ir);
            aug_ir_pop_scope(ir);

            // Tag end address
            const size_t end_block_addr = ir->bytecode_offset;

            // Fixup stubbed block offsets
            aug_ir_get_operation(ir, else_block_jmp)->operand = aug_ir_operand_from_int(else_block_addr);
            aug_ir_get_operation(ir, end_block_jmp)->operand = aug_ir_operand_from_int(end_block_addr);

            break;
        }
        case AUG_AST_STMT_WHILE:
        {
            assert(children_size == 2); //while([0]) {[1]}

            const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);

            const aug_ir_operand& begin_block_operand = aug_ir_operand_from_int(ir->bytecode_offset);

            // Evaluate expression. 
            aug_ast_to_ir(vm, children[0], ir);

            //Jump to end if false
            const size_t end_block_jmp = aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP_ZERO, stub_operand);

            // Loop block
            aug_ir_push_scope(ir);
            aug_ast_to_ir(vm, children[1], ir);
            aug_ir_pop_scope(ir);

            // Jump back to beginning, expr evaluation 
            aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP, begin_block_operand);

            // Tag end address
            const size_t end_block_addr = ir->bytecode_offset;

            // Fixup stubbed block offsets
            aug_ir_get_operation(ir, end_block_jmp)->operand = aug_ir_operand_from_int(end_block_addr);
            break;
        }
        case AUG_AST_FUNC_CALL:
        {
            assert(token_data != NULL); // func name is token data
            const int arg_count = children_size;

            const aug_symbol& symbol = aug_ir_get_symbol(ir, token_data);
            if(symbol.type == AUG_SYM_VAR)
            {
                ir->valid = false;
                AUG_INPUT_ERROR_AT(ir->input, &token.pos, "Can not call variable %s as a function", token_data->buffer);
                break;
            }

            // If the symbol is a user defined function.
            if(symbol.type == AUG_SYM_FUNC)
            {
                if(symbol.argc != arg_count)
                {
                    ir->valid = false;
                    AUG_INPUT_ERROR_AT(ir->input, &token.pos, "Function Call %s passed %d arguments, expected %d", token_data->buffer, arg_count, symbol.argc);
                }
                else
                {
                    // offset to account for the pushed base
                    size_t push_frame = aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_CALL_FRAME, aug_ir_operand_from_int(0));

                    // push args
                    int i;
                    for(i = 0; i < children_size; ++ i)
                        aug_ast_to_ir(vm, children[i], ir);

                    const aug_ir_operand& func_addr = aug_ir_operand_from_int(symbol.offset); // func addr
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_CALL, func_addr);

                    // fixup the return address to after the call
                    aug_ir_get_operation(ir, push_frame)->operand = aug_ir_operand_from_int(ir->bytecode_offset);
                }
                break;
            }

            // Check if the symbol is a registered function
            int func_index = -1;
            int i;
            for(i = 0; i < AUG_EXTENSION_SIZE; ++i)
            {
                if(aug_string_compare(vm->extension_names[i], token_data) && vm->extensions[i] != NULL)
                {
                    func_index = i;
                    break;
                }
            }

            if(func_index == -1)
            {
                ir->valid = false;
                AUG_INPUT_ERROR_AT(ir->input, &token.pos, "Function %s not defined", token_data->buffer);
                break;
            }
            if(vm->extensions[func_index] == nullptr)
            {
                ir->valid = false;
                AUG_INPUT_ERROR_AT(ir->input, &token.pos, "External Function %s was not properly registered.", token_data->buffer);
                break;
            }

            for(i = arg_count - 1; i >= 0; --i)
                aug_ast_to_ir(vm, children[i], ir);

            const aug_ir_operand& func_index_operand = aug_ir_operand_from_int(func_index);
            aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_INT, func_index_operand);

            const aug_ir_operand& arg_count_operand = aug_ir_operand_from_int(arg_count);
            aug_ir_add_operation_arg(ir, AUG_OPCODE_CALL_EXT, arg_count_operand);
            break;
        }
        case AUG_AST_RETURN:
        {
            const aug_ir_operand& offset = aug_ir_operand_from_int(aug_ir_calling_offset(ir));
            if(children_size == 1) //return [0];
            {
                aug_ast_to_ir(vm, children[0], ir);
                aug_ir_add_operation_arg(ir, AUG_OPCODE_RETURN, offset);
            }
            else
            {
                aug_ir_add_operation(ir, AUG_OPCODE_PUSH_NONE);
                aug_ir_add_operation_arg(ir, AUG_OPCODE_RETURN, offset);
            }
            break;
        }
        case AUG_AST_PARAM:
        {
            assert(token_data != NULL);
            aug_ir_set_param(ir, token_data);
            break;
        }
        case AUG_AST_PARAM_LIST:
        {
            int i;
            for(i = 0; i < children_size; ++ i)
                aug_ast_to_ir(vm, children[i], ir);
            break;
        }
        case AUG_AST_FUNC_DEF:
        {
            assert(token_data != NULL); // func name is token
            assert(children_size == 2); //func token [0] {[1]};

            // Jump over the func def
            const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);
            const size_t end_block_jmp = aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP, stub_operand);

            aug_ast* params = children[0];
            assert(params && params->id == AUG_AST_PARAM_LIST);
            const int param_count = params->children_size;

            if(!aug_ir_set_func(ir, token_data, param_count))
            {
                ir->valid = false;
                AUG_INPUT_ERROR_AT(ir->input, &token.pos, "Function %s already defined", token_data->buffer);
                break;
            }

            // Parameter frame
            aug_ir_push_scope(ir);
            aug_ast_to_ir(vm, children[0], ir);

            // Function block frame
            aug_ir_push_frame(ir, param_count);
            aug_ast_to_ir(vm, children[1], ir);

            // Ensure there is a return
            if(aug_ir_last_operation(ir)->opcode != AUG_OPCODE_RETURN)
            {
                const aug_ir_operand& offset = aug_ir_operand_from_int(aug_ir_calling_offset(ir));
                aug_ir_add_operation(ir, AUG_OPCODE_PUSH_NONE);
                aug_ir_add_operation_arg(ir, AUG_OPCODE_RETURN, offset);
            }

            aug_ir_pop_frame(ir);
            aug_ir_pop_scope(ir);

            const size_t end_block_addr = ir->bytecode_offset;

            // fixup jump operand
            aug_ir_get_operation(ir, end_block_jmp)->operand = aug_ir_operand_from_int(end_block_addr);

            break;
        }
        default:
            assert(0);
            break;
    }
}

char* aug_ir_to_bytecode(aug_ir* ir)
{  
    char* bytecode = AUG_ALLOC_ARRAY(char, ir->bytecode_offset);
    char* instruction = bytecode;

    size_t i;
    for(i = 0; i < ir->operations.length; ++i)
    {
        aug_ir_operation* operation = (aug_ir_operation*)aug_container_at(&ir->operations, i);
        aug_ir_operand operand = operation->operand;

        // push operation opcode
        *(instruction++) = (char)operation->opcode;

        // push operation arguments
        switch (operand.type)
        {
        case AUG_IR_OPERAND_NONE:
            break;
        case AUG_IR_OPERAND_BOOL:
        case AUG_IR_OPERAND_CHAR:
        case AUG_IR_OPERAND_INT:
        case AUG_IR_OPERAND_FLOAT:
            for(size_t i = 0; i < aug_ir_operand_size(operand); ++i)
                *(instruction++) = operand.data.bytes[i];
            break;
        case AUG_IR_OPERAND_BYTES:
            for(size_t i = 0; i < strlen(operand.data.str); ++i)
                *(instruction++) = operand.data.str[i];
            *(instruction++) = 0; // null terminate
            break;
        }
    }
    return bytecode;
}

// -------------------------------------- API ---------------------------------------------// 
aug_script* aug_script_new()
{
    aug_script* script = AUG_ALLOC(aug_script);
    script->bytecode = NULL;
    script->stack_state = NULL;
    script->globals = NULL;
    return script;
}

void aug_script_delete(aug_script* script)
{
    if(script == NULL)
        return;

    if(script->stack_state != NULL)
    {
        for(size_t i = 0; i < script->stack_state->length; ++i)
        {
            aug_value* value = aug_array_at( script->stack_state, i );
            aug_decref(value);
        }
    }
    aug_symtable_decref(script->globals);
    script->stack_state = aug_array_decref(script->stack_state);

    if(script->bytecode != NULL)
        AUG_FREE_ARRAY(script->bytecode);
    AUG_FREE(script);
}

void aug_compile_script(aug_vm* vm, aug_input* input, aug_ast* root, aug_script* script)
{
    if(root == NULL || script == NULL)
        return;

    // Generate IR
    aug_ir* ir = aug_ir_new(input);
    aug_ast_to_ir(vm, root, ir);

    if(!ir->valid)
        return;
    
    // Load script globals
    script->globals = ir->globals;
    aug_symtable_incref(script->globals);

    // Load bytecode into VM
    script->bytecode = aug_ir_to_bytecode(ir);
    script->bytecode_size = ir->bytecode_offset;

    aug_ir_delete(ir);
}

aug_vm* aug_startup(aug_error_function* error_callback)
{
    aug_vm* vm = AUG_ALLOC(aug_vm);
    int i;
    for(i = 0; i < AUG_EXTENSION_SIZE; ++i)
    {
        vm->extensions[i] = NULL;
        vm->extension_names[i] = NULL;
    }

    for(i = 0; i < AUG_STACK_SIZE; ++i)
        vm->stack[i] = aug_none();

    // Initialize
    vm->error_callback = error_callback;
    aug_vm_startup(vm);
    return vm;
}

void aug_shutdown(aug_vm* vm)
{
    aug_vm_shutdown(vm);

    int i;
    for(i = 0; i < AUG_EXTENSION_SIZE; ++i)
    {
        vm->extensions[i] = NULL;
        vm->extension_names[i] = aug_string_decref(vm->extension_names[i]);
    }

    AUG_FREE(vm);
}

void aug_register(aug_vm* vm, const char* name, aug_extension *extension)
{
    int i;
    for(i = 0; i < AUG_EXTENSION_SIZE; ++i)
    {
        if(vm->extensions[i] == NULL)
        {
            vm->extensions[i] = extension;
            vm->extension_names[i] =  aug_string_create(name);
            break;
        }
    }
}

void aug_unregister(aug_vm* vm, const char* func_name)
{
    int i;
    for(i = 0; i < AUG_EXTENSION_SIZE; ++i)
    {
        if(aug_string_compare_bytes(vm->extension_names[i], func_name))
        {
            vm->extensions[i] = NULL;
            vm->extension_names[i] = aug_string_decref(vm->extension_names[i]);
            break;
        }
    }
}

void aug_execute(aug_vm* vm,const char* filename)
{
    aug_script* script = aug_script_new(); 
    aug_compile(vm, script, filename);

    aug_vm_startup(vm);
    aug_vm_load_script(vm, script);
    aug_vm_execute(vm);
    aug_vm_shutdown(vm);

    aug_script_delete(script);
}

bool aug_compile(aug_vm* vm, aug_script* script, const char* filename)
{
    if(script == NULL)
        return false;

    aug_input* input = aug_input_open(filename, vm->error_callback);
    if(input == NULL)
        return false;

    // Parse file
    aug_ast* root = aug_parse(vm, input);
    aug_compile_script(vm, input, root, script);
    
    // Cleanup 
    aug_ast_delete(root);
    aug_input_close(input);
    
    return true;
}

void aug_load(aug_vm* vm, aug_script* script)
{
    aug_vm_load_script(vm, script);
    aug_vm_execute(vm);
    aug_vm_save_script(vm, script);
}

void aug_unload(aug_vm* vm, aug_script* script)
{
    aug_vm_unload_script(vm, script);
}

aug_value aug_call_args(aug_vm* vm, aug_script* script, const char* func_name, int argc, aug_value* args)
{
    if(vm == NULL)
        return aug_none();

    aug_value ret_value = aug_none();
    if(script->bytecode == NULL)
        return ret_value;
    
    aug_symbol symbol = aug_symtable_get_bytes(script->globals, func_name);
    if(symbol.type == AUG_SYM_NONE)
    {
        AUG_LOG_ERROR(vm->error_callback, "Function %s not defined", func_name);
        return ret_value;
    }

    switch(symbol.type)
    {
        case AUG_SYM_FUNC:
            break;
        case AUG_SYM_VAR:
        {
            AUG_LOG_ERROR(vm->error_callback, "Can not call variable %s a function", func_name);
            return ret_value;
        }
        default:
        {
            AUG_LOG_ERROR(vm->error_callback, "Symbol %s not defined as a function", func_name);
            return ret_value;
        }
    }
    if(symbol.argc != argc)
    {
        AUG_LOG_ERROR(vm->error_callback, "Function %s passed %d arguments, expected %d", func_name, argc, symbol.argc);
        return ret_value;
    }

    aug_vm_startup(vm);
    aug_vm_load_script(vm, script);

    // Setup base index to be current stack index
    const int func_addr = symbol.offset;
    ret_value = aug_vm_execute_from_frame(vm, func_addr, argc, args);

    aug_vm_save_script(vm, script);
    aug_vm_shutdown(vm);

    return ret_value;
}

aug_value aug_call(aug_vm* vm, aug_script* script, const char* func_name)
{
    return aug_call_args(vm, script, func_name, 0, NULL);
}

aug_value aug_from_bool(bool data)
{
    aug_value value;
    aug_set_bool(&value, data);
    return value;
}

aug_value aug_from_int(int data)
{
    aug_value value;
    aug_set_int(&value, data);
    return value;
}

aug_value aug_from_char(char data)
{
    aug_value value;
    aug_set_char(&value, data);
    return value;
}

aug_value aug_from_float(float data)
{
    aug_value value;
    aug_set_float(&value, data);
    return value;
}

aug_value aug_from_string(const char* data)
{
    aug_value value;
    aug_set_string(&value, data);
    return value;
}

// ------------------------------- Data Structures --------------------------------//

aug_string* aug_string_new(size_t size) 
{
	aug_string* string = AUG_ALLOC(aug_string);
	string->ref_count = 1;
	string->length = 0;
	string->capacity = size;
	string->buffer = AUG_ALLOC_ARRAY(char, string->capacity);
	return string;
}

aug_string* aug_string_create(const char* bytes) 
{
	aug_string* string = AUG_ALLOC(aug_string);
	string->ref_count = 1;
	string->length = strlen(bytes);
	string->capacity = string->length + 1;
	string->buffer = AUG_ALLOC_ARRAY(char, string->capacity);
    strcpy(string->buffer, bytes);
	return string;
}

void aug_string_reserve(aug_string* string, size_t size) 
{
	string->capacity = size;
    string->buffer = AUG_REALLOC_ARRAY(string->buffer, char, string->capacity);
}

void aug_string_push(aug_string* string, char c) 
{
	if(string->length + 1 >= string->capacity) 
        aug_string_reserve(string, 2 * string->capacity);
    string->buffer[string->length++] = c;
    string->buffer[string->length] = '\0';
}

char aug_string_pop(aug_string* string) 
{
	return string->length > 0 ? string->buffer[--string->length] : -1;
}

char aug_string_at(const aug_string* string, size_t index) 
{
	return index < string->length ? string->buffer[index] : -1;
}

char aug_string_back(const aug_string* string) 
{
	return string->length > 0 ? string->buffer[string->length-1] : -1;
}

bool aug_string_compare(const aug_string* a, const aug_string* b) 
{
	if(a->length != b->length)
        return false; 
    return strncmp(a->buffer, b->buffer, a->length) == 0;
}

bool aug_string_compare_bytes(const aug_string* a, const char* bytes) 
{
    if(bytes == NULL)
        return a->buffer == NULL;

    size_t len = strlen(bytes);
    if(len != a->length)
        return false;

    size_t i;
    for(i = 0; i < a->length; ++i)
    {
        if(a->buffer[i] != bytes[i])
            return false;

    }
    return true;
}

void aug_string_incref(aug_string* string) 
{
    if(string != NULL)
	    string->ref_count++;
}

aug_string* aug_string_decref(aug_string* string) 
{
	if(string != NULL && --string->ref_count == 0)
    {
        AUG_FREE_ARRAY(string->buffer);
        AUG_FREE(string);
        return NULL;
    }
    return string;
}

// -------------------------------- Array ----------------------------------------------------//
                 
aug_array* aug_array_new(size_t size)
{                
	aug_array* array = AUG_ALLOC(aug_array);
	array->ref_count = 1;   
	array->length = 0;      
	array->capacity = size; 
	array->buffer = AUG_ALLOC_ARRAY(aug_value, array->capacity );
	return array;
}

void aug_array_incref(aug_array* array) 
{
    if(array != NULL)       
	    array->ref_count++; 
}
 
aug_array* aug_array_decref(aug_array* array)
{
	if(array != NULL && --array->ref_count == 0)
    {            
        AUG_FREE_ARRAY(array->buffer);
        AUG_FREE(array);
        return NULL;
    }            
    return array;
}       

bool aug_array_index_valid(aug_array* array, size_t index)    
{
    return index >= 0 && index < array->length;
}

void aug_array_resize(aug_array* array, size_t size)    
{
	array->capacity = size; 
	array->buffer = AUG_REALLOC_ARRAY(array->buffer, aug_value, array->capacity);
}
 
aug_value* aug_array_push(aug_array* array)  
{
	if(array->length + 1 >= array->capacity)   
        aug_array_resize(array, 2 * array->capacity);     
    return &array->buffer[array->length++];     
}
 
aug_value* aug_array_pop(aug_array* array)
{
	return array->length > 0 ? &array->buffer[--array->length] : NULL; 
}
 
aug_value* aug_array_at(const aug_array* array, size_t index) 
{
	return index < array->length ? &array->buffer[index] : NULL;       
}
 
aug_value* aug_array_back(const aug_array* array)             
{
	return array->length > 0 ? &array->buffer[array->length-1] : NULL; 
}

// ------------------------------- Generic Containers ------------------------------------//

aug_container aug_container_new(size_t size)
{ 
	aug_container container;
	container.length = 0;
	container.capacity = size; 
	container.buffer = AUG_ALLOC_ARRAY(void*, container.capacity);
	return container;
}
 
void aug_container_delete(aug_container* container)
{
    AUG_FREE_ARRAY(container->buffer);
    container->buffer = NULL;
}

void aug_container_reserve(aug_container* container, size_t size)    
{
	container->capacity = size; 
	container->buffer = AUG_REALLOC_ARRAY(container->buffer, void*, container->capacity);
}
 
void aug_container_push(aug_container* container, void* data)  
{
	if(container->length + 1 >= container->capacity) 
        aug_container_reserve(container, 2 * container->capacity);
    container->buffer[container->length++] = data;
}
 
void* aug_container_pop(aug_container* container)
{
	return container->length > 0 ? container->buffer[--container->length] : NULL; 
}
 
void* aug_container_at(const aug_container* container, size_t index) 
{
	return index >= 0 && index < container->length ? container->buffer[index] : NULL;
}
 
void* aug_container_back(const aug_container* container)
{
	return container->length > 0 ? container->buffer[container->length-1] : NULL; 
}

// ------------------------------- Symtable ------------------------------------------//
aug_symtable* aug_symtable_new(size_t size)
{
    aug_symtable* symtable = AUG_ALLOC(aug_symtable);
	symtable->length = 0;
	symtable->capacity = size; 
    symtable->ref_count = 1;
	symtable->buffer = AUG_ALLOC_ARRAY(aug_symbol, symtable->capacity);
	return symtable;
}

void aug_symtable_incref(aug_symtable* symtable)
{
    if(symtable)
        ++symtable->ref_count;
}

aug_symtable* aug_symtable_decref(aug_symtable* symtable)
{    
    if(symtable != NULL && --symtable->ref_count == 0)
    {
        size_t i;
        for(i = 0; i < symtable->length; ++i)
        {
            aug_symbol symbol = symtable->buffer[i];
            aug_string_decref(symbol.name);
        }
        AUG_FREE_ARRAY(symtable->buffer);
        AUG_FREE(symtable);
        return NULL;
    }
    return symtable;
}

void  aug_symtable_reserve(aug_symtable* symtable, size_t size)
{
	symtable->capacity = size; 
	symtable->buffer = AUG_REALLOC_ARRAY(symtable->buffer, aug_symbol, symtable->capacity);
}

bool aug_symtable_set(aug_symtable* symtable, aug_symbol symbol)
{
    aug_symbol existing_symbol = aug_symtable_get(symtable, symbol.name);
    if(existing_symbol.type != AUG_SYM_NONE)
        return false;

    if(symtable->length + 1 >= symtable->capacity) 
        aug_symtable_reserve(symtable, 2 * symtable->capacity);
    
    
    aug_symbol new_symbol = symbol;
    aug_string_incref(new_symbol.name);

    symtable->buffer[symtable->length++] = new_symbol;
    return true;
}

aug_symbol aug_symtable_get(aug_symtable* symtable, aug_string* name)
{
    size_t i;
    for(i = 0; i < symtable->length; ++i)
    {
        aug_symbol symbol = symtable->buffer[i];
        if(aug_string_compare(symbol.name, name))
            return symbol;
    }
    aug_symbol symbol;
    symbol.offset = AUG_OPCODE_INVALID;
    symbol.type = AUG_SYM_NONE;
    symbol.argc = 0;
    return symbol;
}

aug_symbol aug_symtable_get_bytes(aug_symtable* symtable, const char* name)
{
    size_t i;
    for(i = 0; i < symtable->length; ++i)
    {
        aug_symbol symbol = symtable->buffer[i];
        if(aug_string_compare_bytes(symbol.name, name))
            return symbol;
    }
    aug_symbol symbol;
    symbol.offset = AUG_OPCODE_INVALID;
    symbol.type = AUG_SYM_NONE;
    symbol.argc = 0;
    return symbol;
}

#ifdef __cplusplus
} // extern C
#endif

#endif //AUG_IMPLEMENTATION