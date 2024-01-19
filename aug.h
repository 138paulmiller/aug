
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

    * syntax:
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

    Todo: 
    - Syntax - Implement for loops
    - VM - Serialize Bytecode to external file. Execute compiled bytecode from file
    - VM - Serialize debug map to file. Link from bytecode to source file. 
    - VM - Print Stack trace on error. Link back to source file if running uncompiled bytecode
    - Native - Support return value in function callbacks
    - Semantic Pass - resolve IR pass any potential naming, field,  or return issues     
    - Convert to C 
    -   Reimplement primary data structures, expose custom allocator/deallocator in vmironment
    - Pushing string literals should not create copy. Create a flag to determine if in memory bytes

    Issues:
        - Accessing variables outside the current calling frame (function scope) causes issues. The 2 return and base index params.
            Handle this via global ?  
        - VM Calling functions should retain global state in between vm sessions. To resolve this, create new API for loading VM for a given script. 
            This will exec the script ,and retain state. then can call each new script function. Also save state to script
*/

#ifndef __AUG_HEADER__
#define __AUG_HEADER__

#define AUG_DEBUG_VM 0 // for console debug

// Max size of the virtual machine stack
#ifndef AUG_STACK_SIZE
#define AUG_STACK_SIZE (1024 * 16)
#endif//AUG_STACK_SIZE

#ifndef AUG_APPROX_THRESHOLD
#define AUG_APPROX_THRESHOLD 0.0000001
#endif//AUG_APPROX_THRESHOLD 

// Max number of characters per symbol name
#ifndef AUG_TOKEN_BUFFER_SIZE
#define AUG_TOKEN_BUFFER_SIZE 32
#endif//AUG_TOKEN_BUFFER_SIZE

#ifndef AUG_NEW
#define AUG_NEW(type) new type
#endif//AUG_NEW

#ifndef AUG_NEW_ARRAY
#define AUG_NEW_ARRAY(type, count) new type [count]
#endif//AUG_NEW_ARRAY

#ifndef AUG_DELETE
#define AUG_DELETE(ptr) delete ptr
#endif//AUG_DELETE

#ifndef AUG_DELETE_ARRAY
#define AUG_DELETE_ARRAY(ptr) delete [] ptr
#endif//AUG_DELETE_ARRAY


#include <string>
#include <vector>
#include <list>
#include <unordered_map>

// Data structures
struct aug_value;
struct aug_object;
struct aug_symbol;

using aug_std_string = std::string;

template <class type>
using aug_std_list = std::list<type>;

template <class type>
using aug_std_array = std::vector<type>;

template <class key, class type>
using aug_std_map = std::unordered_map<key, type>;

typedef void(aug_error_callback)(const char* /*msg*/);
typedef aug_value /*return_value*/ (aug_function_callback)(const aug_std_array<aug_value>& /*args*/);

// Value Types
enum aug_value_type : char
{
    AUG_BOOL,
    AUG_INT, 
    AUG_FLOAT, 
    AUG_STRING, 
    AUG_OBJECT,
    AUG_LIST,
    AUG_NONE
};

#if defined(AUG_IMPLEMENTATION)
const char* aug_value_type_labels[] =
{
    "bool", "int", "float", "string", "object", "list", "none"
};
static_assert(sizeof(aug_value_type_labels) / sizeof(aug_value_type_labels[0]) == (int)AUG_NONE + 1, "Type labels must be up to date with enum");
#endif //AUG_IMPLEMENTATION

struct aug_list
{
    int ref_count;

    // TODO: replace with custom class
    aug_std_list<aug_value> data;
};

struct aug_string : public aug_std_string
{
    int ref_count;
};

// Values instance 
struct aug_value
{
    aug_value_type type;
    union 
    {
        bool b;
        int i; 
        float f;
        aug_string* str;
        aug_object* obj;
        aug_list* list;
    };
};

// Object attributes
struct aug_attribute
{
    aug_string id;
    aug_value value;
};

// Object instance
struct aug_object
{
    int ref_count;

    aug_std_array<aug_attribute> attribs;
};

// Symbol types
enum aug_symbol_type
{
    AUG_SYM_NONE,
    AUG_SYM_VAR,
    AUG_SYM_FUNC,
};

enum aug_symbol_scope
{
    AUG_SYM_SCOPE_LOCAL,
    AUG_SYM_SCOPE_GLOBAL,
    AUG_SYM_SCOPE_PARAM,
};

// Script symbols 
struct aug_symbol
{
    aug_symbol_scope scope;
    aug_symbol_type type;
    // Functions - offset is the bytecode address, argc is the number of expected params
    // Variables - offset is the stack offset from the base index
    int offset;
    int argc;
};

using aug_symtable = aug_std_map<aug_std_string, aug_symbol>;

// Represents a "compiled" script
struct aug_script
{
    aug_symtable globals;
    aug_std_array<char> bytecode;
    bool valid;
};

// Calling frames are used to preserve and access parameters and local variables from the stack within a calling context
struct aug_frame
{
    int base_index;
    int stack_index;
    // For function frames, there will be a return instruction and arg count. Otherwise, the frame is used for local scope 

    bool func_call;
    int arg_count;
    const char* instruction; 
};

// Running instance of the virtual machine
struct aug_vm
{
    aug_error_callback* error_callback;
    bool valid;

    const char* instruction;
    const char* bytecode;
 
    aug_value* stack;
    int stack_index;
    int base_index;

    // External functions are native functions that can be called from scripts   
    // This external function map contains the user's registered functions. 
    // Use aug_register/aug_unregister to modify these fields
    aug_std_array<aug_function_callback*> external_functions;
    aug_std_array<aug_std_string>         external_function_names;

    // TODO: debug symtable from addr to func name / variable offsets
};

void aug_startup(aug_vm& vm, aug_error_callback* error_callback);
void aug_shutdown(aug_vm& vm);

// NOTE: changing the registered functions will require a script recompilation. Can not guarantee the external function call will work. 
void aug_register(aug_vm& vm, const char* func_name, aug_function_callback* callback);
void aug_unregister(aug_vm& vm, const char* func_name);

void aug_execute(aug_vm& vm, const char* filename);
void aug_evaluate(aug_vm& vm, const char* code);

bool aug_compile(aug_vm& vm, aug_script& script, const char* filename);
void aug_boot(aug_vm& vm, aug_script& script);

aug_value aug_call(aug_vm& vm, aug_script& script, const char* func_name);
aug_value aug_call(aug_vm& vm, aug_script& script, const char* func_name, const aug_std_array<aug_value>& args);

aug_value aug_none();
bool aug_get_bool(const aug_value& value);
int aug_get_int(const aug_value& value);
float aug_get_float(const aug_value& value);

void aug_delete(aug_value* value);

aug_value aug_from_bool(bool data);
aug_value aug_from_int(int data);
aug_value aug_from_float(float data);
aug_value aug_from_string(const char* data);

#endif //__AUG_HEADER__

// -------------------------------------- Implementation Details ---------------------------------------// 

#if defined(AUG_IMPLEMENTATION)

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

#include <fstream>
#include <iostream>
#include <sstream>

// --------------------------------------- Input/Logging ---------------------------------------//

void aug_log_error_internal(aug_error_callback* error_callback, const char* format, ...)
{
    // TODO: make thread safe
    static char log_buffer[4096];

    va_list args;
    va_start(args, format);
    if (error_callback)
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

#define AUG_LOG_ERROR(error_callback, ...)\
    aug_log_error_internal(error_callback, __VA_ARGS__);

#define AUG_INPUT_ERROR_AT(input, pos, ...)\
if (input->valid)                          \
{                                          \
    input->valid = false;                  \
    aug_input_error_hint(input, pos);      \
    AUG_LOG_ERROR(input->error_callback,   \
        __VA_ARGS__);                      \
}

#define AUG_INPUT_ERROR(input, ...) \
    AUG_INPUT_ERROR_AT(input,       \
        aug_input_prev_pos(input),  \
        __VA_ARGS__);


struct aug_pos
{
    size_t filepos;
    size_t linepos;
    int line;
    int col;
};

struct aug_input
{
    bool is_file; // True if input source is a file. If false, file is string stream and filename is raw source code
    std::istream* file;
    aug_std_string filename;
    aug_error_callback* error_callback;
    bool valid;

    std::streampos track_pos;
    int pos_buffer_index;
    aug_pos pos_buffer[4];

    char token_buffer[AUG_TOKEN_BUFFER_SIZE];
};

inline aug_pos* aug_input_pos(aug_input* input)
{
    return &input->pos_buffer[input->pos_buffer_index];
}

inline aug_pos* aug_input_prev_pos(aug_input* input)
{
    assert(input != NULL);
    input->pos_buffer_index--;
    if (input->pos_buffer_index < 0)
        input->pos_buffer_index = (sizeof(input->pos_buffer) / sizeof(input->pos_buffer[0])) - 1;
    return aug_input_pos(input);
}

inline aug_pos* aug_input_next_pos(aug_input* input)
{
    assert(input != NULL);
    input->pos_buffer_index = (input->pos_buffer_index + 1) % (sizeof(input->pos_buffer) / sizeof(input->pos_buffer[0]));
    return aug_input_pos(input);
}

aug_input* aug_input_open(const char* filename_or_code, aug_error_callback* error_callback, bool is_file)
{
    std::istream* stream = NULL;
    if (is_file)
    {
        std::fstream* file = AUG_NEW(std::fstream(filename_or_code, std::fstream::in));
        if (file == NULL || !file->is_open())
        {
            AUG_LOG_ERROR(error_callback, "Input failed to open file %s", filename_or_code);
            if (file)
                AUG_DELETE(file);
            return NULL;
        }
        stream = file;
    }
    else
    {
        std::stringstream* iss = AUG_NEW(std::stringstream(filename_or_code, std::fstream::in));
        if (iss == NULL)
        {
            AUG_LOG_ERROR(error_callback, "Input failed to read code %s", filename_or_code);
            return NULL;
        }
        stream = iss;
    }

    aug_input* input = AUG_NEW(aug_input);
    input->valid = true;
    input->error_callback = error_callback;
    input->is_file = is_file;
    input->file = stream;
    input->filename = filename_or_code;
    input->pos_buffer_index = 0;
    input->track_pos = 0;

    aug_pos* pos = aug_input_pos(input);
    pos->col = 0;
    pos->line = 0;
    pos->filepos = pos->linepos = (size_t)input->file->tellg();

    return input;
}

void aug_input_close(aug_input* input)
{
    if (input->file != NULL)
        AUG_DELETE(input->file);

    AUG_DELETE(input);
}

inline char aug_input_get(aug_input* input)
{
    if (input == NULL || input->file == NULL)
        return -1;

    const char c = input->file->get();

    aug_pos* pos = aug_input_pos(input);
    aug_pos* next_pos = aug_input_next_pos(input);

    next_pos->line = pos->line;
    next_pos->col = pos->col + 1;
    next_pos->linepos = pos->linepos;
    next_pos->filepos = (size_t)input->file->tellg();

    if (c == '\n')
    {
        next_pos->col = pos->line + 1;
        next_pos->line = pos->line;
        next_pos->linepos = (size_t)input->file->tellg();
    }
    return c;
}

inline char aug_input_peek(aug_input* input)
{
    assert(input != NULL && input->file != NULL);
    return input->file->peek();
}

inline char aug_input_unget(aug_input* input)
{
    assert(input != NULL && input->file != NULL);
    input->file->unget();

    aug_input_prev_pos(input);

    return aug_input_peek(input);
}

inline void aug_input_start_tracking(aug_input* input)
{
    assert(input != NULL && input->file != NULL);
    input->track_pos = input->file->tellg();
}

inline bool aug_input_end_tracking(aug_input* input, aug_std_string& s)
{
    assert(input != NULL && input->file != NULL);
    const std::fstream::iostate state = input->file->rdstate();
    input->file->clear();

    const std::streampos pos_end = input->file->tellg();
    const std::streamoff len = (pos_end - input->track_pos);

    if (len >= AUG_TOKEN_BUFFER_SIZE)
    {
        AUG_LOG_ERROR(input->error_callback, "Token contains to many characters. Can not exceed %d", AUG_TOKEN_BUFFER_SIZE);
        input->file->clear(state);
        return false;
    }

    input->token_buffer[0] = '\0';
    input->file->seekg(input->track_pos);
    input->file->read(input->token_buffer, len);
    input->file->seekg(pos_end);
    input->file->clear(state);

    s.assign(input->token_buffer, (size_t)len);
    return true;
}

inline void aug_input_error_hint(aug_input* input, const aug_pos* pos)
{
    assert(input != NULL && input->file != NULL);
    
    // save state
    const std::fstream::iostate state = input->file->rdstate();
    input->file->clear();
    const std::streampos curr_pos = input->file->tellg();

    // go to line
    input->file->seekg(pos->linepos);

    char buffer[4096];
    int n = 0;

    char c = input->file->get();
    // skip leading whitespace
    while (isspace(c))
        c = input->file->get();

    while (c != EOF && c != '\n' && n < (int)(sizeof(buffer) / sizeof(buffer[0]) - 1))
    {
        buffer[n++] = c;
        c = input->file->get();
    }
    buffer[n] = '\0';

    AUG_LOG_ERROR(input->error_callback, "Error %s:(%d,%d) ",
        input->filename.c_str(), pos->line + 1, pos->col + 1);

    AUG_LOG_ERROR(input->error_callback, "%s", buffer);

    // Draw arrow to the error if within buffer
    if (pos->col < n-1)
    {
        for (int i = 0; i < pos->col; ++i)
            buffer[i] = ' ';
        buffer[pos->col] = '^';
        buffer[pos->col+1] = '\0';
    }

    AUG_LOG_ERROR(input->error_callback, "%s", buffer);

    // restore state
    input->file->seekg(curr_pos);
    input->file->clear(state);
}

// -------------------------------------- Lexer  ---------------------------------------// 
// Static token details
struct aug_token_detail
{
    const char* label;    // the string representation, used for visualization and debugging
    char prec;            // if the token is an operator, this is the precedence (note: higher values take precendece)
    int  argc;            // if the token is an operator, this is the number of arguments
    bool capture;         // if non-zero, the token will contain the source string value (i.e. integer and string literals)
    const char* keyword;  // if non-null, the token must match the provided keyword
};

#define AUG_TOKEN_LIST                             \
    /* State */                                    \
    AUG_TOKEN(NONE,           0, 0, 0, NULL)       \
    AUG_TOKEN(END,            0, 0, 0, NULL)       \
    /* Symbols */			                       \
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
    AUG_TOKEN(ADD_EQ,         1, 2, 0, NULL)       \
    AUG_TOKEN(SUB,            2, 2, 0, NULL)       \
    AUG_TOKEN(SUB_EQ,         1, 2, 0, NULL)       \
    AUG_TOKEN(MUL,            3, 2, 0, NULL)       \
    AUG_TOKEN(MUL_EQ,         1, 2, 0, NULL)       \
    AUG_TOKEN(DIV,            3, 2, 0, NULL)       \
    AUG_TOKEN(DIV_EQ,         1, 2, 0, NULL)       \
    AUG_TOKEN(POW,            3, 2, 0, NULL)       \
    AUG_TOKEN(POW_EQ,         1, 2, 0, NULL)       \
    AUG_TOKEN(MOD,            3, 2, 0, NULL)       \
    AUG_TOKEN(MOD_EQ,         1, 2, 0, NULL)       \
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
    AUG_TOKEN(DECIMAL,        0, 0, 1, NULL)       \
    AUG_TOKEN(HEXIDECIMAL,    0, 0, 1, NULL)       \
    AUG_TOKEN(BINARY,         0, 0, 1, NULL)       \
    AUG_TOKEN(FLOAT,          0, 0, 1, NULL)       \
    AUG_TOKEN(STRING,         0, 0, 1, NULL)       \
    /* Misc */                                     \
    AUG_TOKEN(NAME,           0, 0, 1, NULL)       \
    AUG_TOKEN(ASSIGN,         0, 0, 0, NULL)       \
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
enum aug_token_id : uint8_t 
{
#define AUG_TOKEN(id, ...) AUG_TOKEN_##id,
    AUG_TOKEN_LIST
#undef AUG_TOKEN
    AUG_TOKEN_COUNT
};

// All token type info. Types map from id to type info
static aug_token_detail aug_token_details[(int)AUG_TOKEN_COUNT] = 
{
#define AUG_TOKEN(id, ...) { #id, __VA_ARGS__},
    AUG_TOKEN_LIST
#undef AUG_TOKEN
};

#undef AUG_TOKEN_LIST

// Token instance
struct aug_token
{
    aug_token_id id;
    const aug_token_detail* detail; 
    aug_std_string data;
    aug_pos pos;
};

// Lexer state
struct aug_lexer
{
    aug_input* input;

    aug_token prev;
    aug_token curr;
    aug_token next;

    char comment_symbol;
};

aug_lexer* aug_lexer_new(aug_input* input)
{
    aug_lexer* lexer = AUG_NEW(aug_lexer);
    lexer->input = input;
    lexer->comment_symbol = '#';

    lexer->prev = aug_token();
    lexer->curr = aug_token();
    lexer->next = aug_token();

    return lexer;
}

void aug_lexer_delete(aug_lexer* lexer)
{
    AUG_DELETE(lexer);
}

inline bool aug_lexer_tokenize_string(aug_lexer* lexer, aug_token& token)
{
    char c = aug_input_get(lexer->input);
    if (c != '\"')
    {
        aug_input_unget(lexer->input);
        return false;
    }

    token.id = AUG_TOKEN_STRING;
    token.data.clear();

    c = aug_input_get(lexer->input);

    while (c != '\"')
    {
        if (c == EOF)
        {
            AUG_INPUT_ERROR(lexer->input, "string literal missing closing \"");
            return false;
        }

        if (c == '\\')
        {
            // handle escaped chars
            c = aug_input_get(lexer->input);
            switch (c)
            {
            case '\'': 
                token.data.push_back('\'');
                break;
            case '\"':
                token.data.push_back('\"');
                break;
            case '\\':
                token.data.push_back('\\');
                break;
            case '0': //Null
                token.data.push_back(0x0);
                break;
            case 'a': //Alert beep
                token.data.push_back(0x07);
                break;
            case 'b': // Backspace
                token.data.push_back(0x08);
                break;
            case 'f': // Page break
                token.data.push_back(0x0C);
                break;
            case 'n': // Newline
                token.data.push_back(0x0A);
                break;
            case 'r': // Carriage return
                token.data.push_back(0x0D);
                break;
            case 't': // Tab (Horizontal)
                token.data.push_back(0x09);
                break;
            case 'v': // Tab (Vertical)
                token.data.push_back(0x0B);
                break;
            default:
                AUG_INPUT_ERROR(lexer->input, "invalid escape character \\%c", c);
                while (c != '\"')
                {
                    if (c == EOF)
                        break;
                    c = aug_input_get(lexer->input);
                }
                return false;
            }
        }
        else
        {
            token.data.push_back(c);
        }

        c = aug_input_get(lexer->input);
    }

    return true;
}

inline bool aug_lexer_tokenize_symbol(aug_lexer* lexer, aug_token& token)
{
    aug_token_id id = AUG_TOKEN_NONE;

    char c = aug_input_get(lexer->input);
    switch (c)
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
        if (aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_ADD_EQ;
        else
            id = AUG_TOKEN_ADD;
        break;
    case '-':
        if (aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_SUB_EQ;
        else
            id = AUG_TOKEN_SUB;
        break;
    case '*':
        if (aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_MUL_EQ;
        else
            id = AUG_TOKEN_MUL;
        break;
    case '/':
        if (aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_DIV_EQ;
        else
            id = AUG_TOKEN_DIV;
        break;
    case '^':
        if (aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_POW_EQ;
        else
            id = AUG_TOKEN_POW;
        break;
    case '%':
        if (aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_MOD_EQ;
        else
            id = AUG_TOKEN_MOD;
        break;
    case '<':
        if (aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_LT_EQ;
        else
            id = AUG_TOKEN_LT;
        break;
    case '>':
        if (aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_GT_EQ;
        else
            id = AUG_TOKEN_GT;
        break;
    case '=':
        if (aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_EQ;
        else
            id = AUG_TOKEN_ASSIGN;
        break;
    case '!':
        if (aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_NOT_EQ;
        else
            id = AUG_TOKEN_NOT;
        break;
    case '~':
        if (aug_input_peek(lexer->input) == '=' && aug_input_get(lexer->input))
            id = AUG_TOKEN_APPROX_EQ;
        break;
    }

    if (id == AUG_TOKEN_NONE)
    {
        aug_input_unget(lexer->input);
        return false;
    }

    token.id = id;
    return true;
}

inline bool aug_lexer_tokenize_name(aug_lexer* lexer, aug_token& token)
{
    aug_input_start_tracking(lexer->input);

    char c = aug_input_get(lexer->input);
    if (c != '_' && !isalpha(c))
    {
        aug_input_unget(lexer->input);
        return false;
    }
    
    while (c == '_' || isalnum(c))
        c = aug_input_get(lexer->input);
    aug_input_unget(lexer->input);

    if (!aug_input_end_tracking(lexer->input, token.data))
        return false;

    token.id = AUG_TOKEN_NAME;

    // find token id for keyword
    for (size_t i = 0; i < (size_t)AUG_TOKEN_COUNT; ++i)
    {
        const aug_token_detail& detail = aug_token_details[i];
        if (detail.keyword && detail.keyword == token.data)
        {
            token.id = (aug_token_id)i;
            break;
        }
    }
    
    return true;
}

bool aug_lexer_tokenize_number(aug_lexer* lexer, aug_token& token)
{    
    aug_input_start_tracking(lexer->input);

    char c = aug_input_get(lexer->input);
    if (c != '.' && !isdigit(c) && c != '+' && c != '-')
    {
        aug_input_unget(lexer->input);
        return false;
    } 

    aug_token_id id = AUG_TOKEN_NONE;

    if (c == '0' && aug_input_peek(lexer->input) == 'x')
    {
        id = AUG_TOKEN_HEXIDECIMAL;

        c = aug_input_get(lexer->input); //eat 'x'
        c = aug_input_get(lexer->input);

        while (isalnum(c))
        {
            if (!isdigit(c) && !((c >='a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                id = AUG_TOKEN_NONE;

            c = aug_input_get(lexer->input);
        }
        aug_input_unget(lexer->input);
    }
    else if (c == '0' && aug_input_peek(lexer->input) == 'b')
    {
        id = AUG_TOKEN_BINARY;

        c = aug_input_get(lexer->input); // eat 'b'
        c = aug_input_get(lexer->input);

        while (isdigit(c))
        {
            if(c != '0' && c != '1')
                id = AUG_TOKEN_NONE;

            c = aug_input_get(lexer->input);
        }
        aug_input_unget(lexer->input);
    }
    else
    {
        if (c == '+' || c == '-')
        {
            c = aug_input_peek(lexer->input);
            if (c != '.' && !isdigit(c))
            {
                aug_input_unget(lexer->input);
                return false;
            }
            c = aug_input_get(lexer->input);
        }

        if (c == '.')
            id = AUG_TOKEN_FLOAT;
        else 
            id = AUG_TOKEN_DECIMAL;

        bool dot = false;
        while (c == '.' || isdigit(c))
        {
            if (c == '.')
            {
                if (dot)
                    id = AUG_TOKEN_NONE;
                else
                    id = AUG_TOKEN_FLOAT;

                dot = true;
            }
            c = aug_input_get(lexer->input);
        }
        aug_input_unget(lexer->input);
    }

    token.id = id;

    if(id == AUG_TOKEN_NONE)
    {
        AUG_INPUT_ERROR(lexer->input, "invalid numeric format %s", token.data.c_str());
        return false;
    }
    
    if (!aug_input_end_tracking(lexer->input, token.data))
        return false;

    return true;
}

aug_token aug_lexer_tokenize(aug_lexer* lexer)
{
    aug_token token;

    // if file is not open, or already at then end. return invalid token
    if (lexer->input == NULL || !lexer->input->valid)
    {
        token.id = AUG_TOKEN_NONE;
        token.detail = &aug_token_details[(int)token.id];
        return token;
    }

    if (aug_input_peek(lexer->input) == EOF)
    {
        token.id = AUG_TOKEN_END;
        token.detail = &aug_token_details[(int)token.id];
        return token;
    }

    char c = aug_input_peek(lexer->input);

    // skip whitespace
    if (isspace(c))
    {
        while (isspace(c))
            c = aug_input_get(lexer->input);
        aug_input_unget(lexer->input);
    }

    // skip comments
    while(c == lexer->comment_symbol)
    {        
        while (c != EOF)
        {
            c = aug_input_get(lexer->input);

            if (c == '\n')
            {
                c = aug_input_peek(lexer->input);
                break;
            }
        }

        // skip whitespace
        if (isspace(c))
        {
            while (isspace(c))
                c = aug_input_get(lexer->input);
            aug_input_unget(lexer->input);
        }
    }

    // handle eof
    if (c == EOF)
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
            case AUG_TOKEN_HEXIDECIMAL:
            case AUG_TOKEN_FLOAT:
            case AUG_TOKEN_DECIMAL:
                allow_sign = false;
                break;
            default:
                break;
        }
        if (!allow_sign || !aug_lexer_tokenize_number(lexer, token))
            aug_lexer_tokenize_symbol(lexer, token);
        break;
    }
    case '\"':
        aug_lexer_tokenize_string(lexer, token);
        break;
    default:
        if (aug_lexer_tokenize_symbol(lexer, token))
            break;
        if(aug_lexer_tokenize_name(lexer, token))
            break;
        if (aug_lexer_tokenize_number(lexer, token))
            break;

        AUG_INPUT_ERROR(lexer->input, "invalid character %c", c);
        break;
    }

    token.detail = &aug_token_details[(int)token.id];
    return token;
}

bool aug_lexer_move(aug_lexer* lexer)
{
    if (lexer == NULL)
        return false;

    if (lexer->next.id == AUG_TOKEN_NONE)
        lexer->next = aug_lexer_tokenize(lexer);        

    lexer->prev = lexer->curr; 
    lexer->curr = lexer->next;
    lexer->next = aug_lexer_tokenize(lexer);

    return lexer->curr.id != AUG_TOKEN_NONE;
}

// -------------------------------------- Parser / Abstract Syntax Tree ---------------------------------------// 

enum aug_ast_id : uint8_t
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
    AUG_AST_LIST,
    AUG_AST_UNARY_OP, 
    AUG_AST_BINARY_OP, 
    AUG_AST_FUNC_CALL,
    AUG_AST_FUNC_DEF, 
    AUG_AST_PARAM_LIST,
    AUG_AST_PARAM,
    AUG_AST_RETURN,
};

struct aug_ast
{
    aug_ast_id id;
    aug_token token;
    aug_std_array<aug_ast*> children;
};

aug_ast* aug_parse_value(aug_lexer* lexer); 
aug_ast* aug_parse_block(aug_lexer* lexer);

inline aug_ast* aug_ast_new(aug_ast_id id, const aug_token& token = aug_token())
{
    aug_ast* node = AUG_NEW(aug_ast);
    node->id = id;
    node->token = token;
    return node;
}

inline void aug_ast_delete(aug_ast* node)
{
    if (node == NULL)
        return;
    for(aug_ast* child : node->children)
        aug_ast_delete(child);
    delete node;
}

inline bool aug_parse_expr_pop(aug_lexer* lexer, aug_std_array<aug_token>& op_stack, aug_std_array<aug_ast*>& expr_stack)
{
    aug_token next_op = op_stack.back();
    op_stack.pop_back();

    const int op_argc = (size_t)next_op.detail->argc;
    assert(op_argc == 1 || op_argc == 2); // Only supported operator types

    if(expr_stack.size() < (size_t)op_argc)
    {
        while(expr_stack.size() > 0)
        {
            aug_ast_delete(expr_stack.back());
            expr_stack.pop_back();
        }
        AUG_INPUT_ERROR(lexer->input, "Invalid number of arguments to operator %s", next_op.detail->label);
        return false;
    }

    // Push binary op onto stack
    aug_ast_id id = (op_argc == 2) ? AUG_AST_BINARY_OP : AUG_AST_UNARY_OP;
    aug_ast* binaryop = aug_ast_new(id, next_op);
    binaryop->children.resize(op_argc);

    for(int i = 0; i < op_argc; ++i)
    {
        aug_ast* expr = expr_stack.back();
        expr_stack.pop_back();
        binaryop->children[(op_argc-1) - i] = expr; // add in reverse
    }

    expr_stack.push_back(binaryop);
    return true;
}

inline aug_ast* aug_parse_expr(aug_lexer* lexer)
{
    // Shunting yard algorithm
    aug_std_array<aug_token> op_stack;
    aug_std_array<aug_ast*> expr_stack;
    
    while(lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        aug_token op = lexer->curr;
        if(op.detail->prec > 0)
        {
            // left associate by default (for right, <= becomes <)
            while(op_stack.size() && op_stack.back().detail->prec >= op.detail->prec)
            {
                if(!aug_parse_expr_pop(lexer,  op_stack, expr_stack))
                    return NULL;
            }

            op_stack.push_back(op);
            aug_lexer_move(lexer);
        }
        else
        {
            aug_ast* value = aug_parse_value(lexer);
            if(value == NULL)
                break;
            expr_stack.push_back(value);
        }
    }

    // Not an expression
    if(op_stack.size() == 0 && expr_stack.size() == 0)
        return NULL;

    while(op_stack.size())
    {
        if(!aug_parse_expr_pop(lexer,  op_stack, expr_stack))
            return NULL;
    }

    // Not a valid expression. Either malformed or missing semicolon 
    if(expr_stack.size() == 0 || expr_stack.size() > 1)
    {
        while(expr_stack.size() > 0)
        {
            aug_ast_delete(expr_stack.back());
            expr_stack.pop_back();
        }
        AUG_INPUT_ERROR(lexer->input, "Invalid expression syntax");
        return NULL;
    }

    return expr_stack.back();
}

inline aug_ast* aug_parse_funccall(aug_lexer* lexer)
{
    aug_token name_token = lexer->curr;
    if (name_token.id != AUG_TOKEN_NAME)
        return NULL;

    if (lexer->next.id != AUG_TOKEN_LPAREN)
        return NULL;

    aug_lexer_move(lexer); // eat NAME
    aug_lexer_move(lexer); // eat LPAREN

    aug_ast* funccall = aug_ast_new(AUG_AST_FUNC_CALL, name_token);
    if (aug_ast* expr = aug_parse_expr(lexer))
    {
        funccall->children.push_back(expr);

        while (expr && lexer->curr.id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            if((expr = aug_parse_expr(lexer)))
                funccall->children.push_back(expr);
        }
    }

    if (lexer->curr.id != AUG_TOKEN_RPAREN)
    {
        aug_ast_delete(funccall);
        AUG_INPUT_ERROR(lexer->input, "Function call missing closing parentheses");
        return NULL;
    }

    aug_lexer_move(lexer); // eat RPAREN
    return funccall;
}

inline aug_ast* aug_parse_list(aug_lexer* lexer)
{
    aug_token name_token = lexer->curr;
    if (name_token.id != AUG_TOKEN_LBRACKET)
        return NULL;

    aug_lexer_move(lexer); // eat LBRACKET

    aug_ast* list = aug_ast_new(AUG_AST_LIST, name_token);
    if (aug_ast* expr = aug_parse_expr(lexer))
    {
        list->children.push_back(expr);

        while (expr && lexer->curr.id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            if((expr = aug_parse_expr(lexer)))
                list->children.push_back(expr);
        }
    }

    if (lexer->curr.id != AUG_TOKEN_RBRACKET)
    {
        aug_ast_delete(list);
        AUG_INPUT_ERROR(lexer->input, "List missing closing bracket");
        return NULL;
    }

    aug_lexer_move(lexer); // eat RBRACKET
    return list;
}

inline aug_ast* aug_parse_value(aug_lexer* lexer)
{
    aug_ast* value = NULL;
    const aug_token token = lexer->curr;
    switch (token.id)
    {
    case AUG_TOKEN_DECIMAL:
    case AUG_TOKEN_HEXIDECIMAL:
    case AUG_TOKEN_BINARY:
    case AUG_TOKEN_FLOAT:
    case AUG_TOKEN_STRING:
    case AUG_TOKEN_TRUE:
    case AUG_TOKEN_FALSE:
    {
        aug_lexer_move(lexer);
        value = aug_ast_new(AUG_AST_LITERAL, token);
        break;
    }
    case AUG_TOKEN_NAME:
    {   
        value = aug_parse_funccall(lexer);
        if (value == NULL)
        {
            aug_lexer_move(lexer);
            value = aug_ast_new(AUG_AST_VARIABLE, token);
        }
        break;
    }
    case AUG_TOKEN_LBRACKET:
    {
        value = aug_parse_list(lexer);
        break;
    }
    case AUG_TOKEN_LPAREN:
    {
        aug_lexer_move(lexer); // eat LPAREN
        value = aug_parse_expr(lexer);
        if (lexer->curr.id == AUG_TOKEN_RPAREN)
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
    if (expr == NULL)
        return NULL;
    
    if (lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        aug_ast_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "Missing semicolon at end of expression");
        return NULL;
    }

    aug_lexer_move(lexer); // eat SEMICOLON
    
    aug_ast* stmt_expr = aug_ast_new(AUG_AST_STMT_EXPR);
    stmt_expr->children.push_back(expr);
    return stmt_expr;
}

aug_ast* aug_parse_stmt_define_var(aug_lexer* lexer)
{
    if (lexer->curr.id != AUG_TOKEN_VAR)
        return NULL;

    aug_lexer_move(lexer); // eat VAR

    aug_token name_token = lexer->curr;
    if (name_token.id != AUG_TOKEN_NAME)
    {
        AUG_INPUT_ERROR(lexer->input,  "Variable assignment expected name");
        return NULL;
    }
    aug_lexer_move(lexer); // eat NAME

    if (lexer->curr.id == AUG_TOKEN_SEMICOLON)
    {
        aug_lexer_move(lexer); // eat SEMICOLON

        aug_ast* stmt_define = aug_ast_new(AUG_AST_STMT_DEFINE_VAR, name_token);
        return stmt_define;
    }

    if (lexer->curr.id != AUG_TOKEN_ASSIGN)
    {
        AUG_INPUT_ERROR(lexer->input,  "Variable assignment expected \"=\" or ;");
        return NULL;
    }

    aug_lexer_move(lexer); // eat ASSIGN

    aug_ast* expr = aug_parse_expr(lexer);
    if (expr == NULL)
    {
        AUG_INPUT_ERROR(lexer->input,  "Variable assignment expected expression after \"=\"");
        return NULL;
    }
    if (lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        aug_ast_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "Variable assignment missing semicolon at end of expression");
        return NULL;
    }

    aug_lexer_move(lexer); // eat SEMICOLON

    aug_ast* stmt_define = aug_ast_new(AUG_AST_STMT_DEFINE_VAR, name_token);
    stmt_define->children.push_back(expr);
    return stmt_define;
}

aug_ast* aug_parse_stmt_assign_var(aug_lexer* lexer)
{
    aug_token name_token = lexer->curr;
    aug_token eq_token = lexer->next;
    if (name_token.id != AUG_TOKEN_NAME || eq_token.id != AUG_TOKEN_ASSIGN)
        return NULL;

    aug_lexer_move(lexer); // eat NAME
    aug_lexer_move(lexer); // eat ASSIGN

    aug_ast* expr = aug_parse_expr(lexer);
    if (expr == NULL)
    {
        AUG_INPUT_ERROR(lexer->input,  "Assignment expected expression after \"=\"");
        return NULL;
    }

    if (lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        aug_ast_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "Missing semicolon at end of expression");
        return NULL;
    }

    aug_lexer_move(lexer); // eat SEMICOLON

    aug_ast* stmt_assign = aug_ast_new(AUG_AST_STMT_ASSIGN_VAR, name_token);
    stmt_assign->children.push_back(expr);
    return stmt_assign;
}

aug_ast* aug_parse_stmt_if(aug_lexer* lexer);

aug_ast* aug_parse_stmt_if_else(aug_lexer* lexer, aug_ast* expr, aug_ast* block)
{
    aug_lexer_move(lexer); // eat ELSE

    aug_ast* if_else_stmt = aug_ast_new(AUG_AST_STMT_IF_ELSE);
    if_else_stmt->children.push_back(expr);
    if_else_stmt->children.push_back(block);

    // Handling else if becomes else { if ... }
    if (lexer->curr.id == AUG_TOKEN_IF)
    {
        aug_ast* trailing_if_stmt = aug_parse_stmt_if(lexer);
        if (trailing_if_stmt == NULL)
        {
            aug_ast_delete(if_else_stmt);
            return NULL;
        }
        if_else_stmt->children.push_back(trailing_if_stmt);
    }
    else
    {
        aug_ast* else_block = aug_parse_block(lexer);
        if (else_block == NULL)
        {
            aug_ast_delete(if_else_stmt);
            AUG_INPUT_ERROR(lexer->input,  "If Else statement missing block");
            return NULL;
        }
        if_else_stmt->children.push_back(else_block);
    }

    return if_else_stmt;
}

aug_ast* aug_parse_stmt_if(aug_lexer* lexer)
{
    if (lexer->curr.id != AUG_TOKEN_IF)
        return NULL;

    aug_lexer_move(lexer); // eat IF

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        AUG_INPUT_ERROR(lexer->input,  "If statement missing expression");
        return NULL;      
    }

    aug_ast* block = aug_parse_block(lexer);
    if (block == NULL)
    {
        aug_ast_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "If statement missing block");
        return NULL;
    }

    // Parse else 
    if (lexer->curr.id == AUG_TOKEN_ELSE)
        return aug_parse_stmt_if_else(lexer, expr, block);

    aug_ast* if_stmt = aug_ast_new(AUG_AST_STMT_IF);
    if_stmt->children.push_back(expr);
    if_stmt->children.push_back(block);
    return if_stmt;
}

aug_ast* aug_parse_stmt_while(aug_lexer* lexer)
{
    if (lexer->curr.id != AUG_TOKEN_WHILE)
        return NULL;

    aug_lexer_move(lexer); // eat WHILE

    aug_ast* expr = aug_parse_expr(lexer);
    if (expr == NULL)
    {
        AUG_INPUT_ERROR(lexer->input,  "While statement missing expression");
        return NULL;
    }

    aug_ast* block = aug_parse_block(lexer);
    if (block == NULL)
    {
        aug_ast_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "While statement missing block");
        return NULL;
    }

    aug_ast* while_stmt = aug_ast_new(AUG_AST_STMT_WHILE);
    while_stmt->children.push_back(expr);
    while_stmt->children.push_back(block);

    return while_stmt;
}

inline aug_ast* aug_parse_param_list(aug_lexer* lexer)
{
    if (lexer->curr.id != AUG_TOKEN_LPAREN)
    {
        AUG_INPUT_ERROR(lexer->input,  "Missing opening parentheses in function parameter list");
        return NULL;
    }

    aug_lexer_move(lexer); // eat LPAREN

    aug_ast* param_list = aug_ast_new(AUG_AST_PARAM_LIST);
    if (lexer->curr.id == AUG_TOKEN_NAME)
    {
        aug_ast* param = aug_ast_new(AUG_AST_PARAM, lexer->curr);
        param_list->children.push_back(param);

        aug_lexer_move(lexer); // eat NAME

        while (lexer->curr.id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            if (lexer->curr.id != AUG_TOKEN_NAME)
            {
                AUG_INPUT_ERROR(lexer->input,  "Invalid function parameter. Expected parameter name");
                aug_ast_delete(param_list);
                return NULL;
            }

            aug_ast* param = aug_ast_new(AUG_AST_PARAM, lexer->curr);
            param_list->children.push_back(param);

            aug_lexer_move(lexer); // eat NAME
        }
    }

    if (lexer->curr.id != AUG_TOKEN_RPAREN)
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
    if (lexer->curr.id != AUG_TOKEN_FUNC)
        return NULL;

    aug_lexer_move(lexer); // eat FUNC

    if (lexer->curr.id != AUG_TOKEN_NAME)
    {
        AUG_INPUT_ERROR(lexer->input,  "Missing name in function definition");
        return NULL;
    }

    const aug_token func_name = lexer->curr;

    aug_lexer_move(lexer); // eat NAME

    aug_ast* param_list = aug_parse_param_list(lexer);
    if (param_list == NULL)
        return NULL;

    aug_ast* block = aug_parse_block(lexer);
    if (block == NULL)
    {
        aug_ast_delete(param_list);
        return NULL;
    }

    aug_ast* func_def = aug_ast_new(AUG_AST_FUNC_DEF, func_name);
    func_def->children.push_back(param_list);
    func_def->children.push_back(block);

    return func_def;
}

aug_ast* aug_parse_stmt_return(aug_lexer* lexer)
{
    if (lexer->curr.id != AUG_TOKEN_RETURN)
        return NULL;

    aug_lexer_move(lexer); // eat RETURN

    aug_ast* return_stmt = aug_ast_new(AUG_AST_RETURN);

    aug_ast* expr = aug_parse_expr(lexer);
    if (expr != NULL)
        return_stmt->children.push_back(expr);

    if (lexer->curr.id != AUG_TOKEN_SEMICOLON)
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
        stmt = aug_parse_stmt_assign_var(lexer);
        if(!stmt)
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
    if (lexer->curr.id != AUG_TOKEN_LBRACE)
    {
        AUG_INPUT_ERROR(lexer->input,  "Block missing opening \"{\"");
        return NULL;
    }
    aug_lexer_move(lexer); // eat LBRACE

    aug_ast* block = aug_ast_new(AUG_AST_BLOCK);
    while(aug_ast* stmt = aug_parse_stmt(lexer))
        block->children.push_back(stmt);

    if (lexer->curr.id != AUG_TOKEN_RBRACE)
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
    if (lexer == NULL)
        return NULL;

    aug_lexer_move(lexer); // move to first token

    aug_ast* root = aug_ast_new(AUG_AST_ROOT);
    while (aug_ast* stmt = aug_parse_stmt(lexer))
        root->children.push_back(stmt);

    if (root->children.size() == 0)
    {
        aug_ast_delete(root);
        return NULL;
    }
    return root;
}

aug_ast* aug_parse(aug_vm& vm, aug_input* input)
{
    if (input == NULL)
        return NULL;

    aug_lexer* lexer = aug_lexer_new(input);
    if (lexer == NULL)
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
	AUG_OPCODE(PUSH_FLOAT)        \
	AUG_OPCODE(PUSH_STRING)       \
	AUG_OPCODE(PUSH_LIST)         \
	AUG_OPCODE(PUSH_LOCAL)        \
	AUG_OPCODE(LOAD_LOCAL)        \
	AUG_OPCODE(PUSH_GLOBAL)       \
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
    AUG_OPCODE(PUSH_BASE)         \
    AUG_OPCODE(DEC_STACK)         \


// Special value used in bytecode to denote an invalid vm offset
#define AUG_OPCODE_INVALID -1

enum aug_opcode : uint8_t
{ 
#define AUG_OPCODE(opcode) AUG_OPCODE_##opcode,
	AUG_OPCODE_LIST
#undef AUG_OPCODE
    AUG_OPCODE_COUNT
};
static_assert(AUG_OPCODE_COUNT < 255, "AUG Opcode count too large. This will affect bytecode instruction set");

#ifdef AUG_DEBUG

static const char* aug_opcode_labels[] =
{
#define AUG_OPCODE(opcode) #opcode,
	AUG_OPCODE_LIST
#undef AUG_OPCODE
}; 

#endif// AUG_DEBUG

#undef AUG_OPCODE_LIST

// -------------------------------------- IR --------------------------------------------// 

enum aug_ir_operand_type
{
    // type if operand is constant or literal
    AUG_IR_OPERAND_NONE = 0,
    AUG_IR_OPERAND_BOOL,
    AUG_IR_OPERAND_INT,
    AUG_IR_OPERAND_FLOAT,  
    AUG_IR_OPERAND_BYTES,
};

struct aug_ir_operand
{
    union
    {
        bool b;
        int i;
        float f;
        char bytes[sizeof(float)]; //Used to access raw byte data to bool, float and int types

        const char* str; // NOTE: weak pointer to token data
    } data;

    aug_ir_operand_type type = AUG_IR_OPERAND_NONE;
};

static_assert(sizeof(float) >= sizeof(int), "Ensure bytes array has enough space to contain both int and float data types");

struct aug_ir_operation
{
    aug_opcode opcode;
    aug_ir_operand operand; //optional parameter. will be encoded in following bytes
   
#ifdef AUG_DEBUG
    size_t bytecode_offset;
#endif //AUG_DEBUG
};

struct aug_ir_scope
{
    int base_index;
    int stack_offset;
    aug_symtable symtable;
};

struct aug_ir_frame
{
    int base_index;
    int arg_count;
    aug_std_array< aug_ir_scope> scope_stack;
};

// All the blocks within a compilation/translation unit (i.e. file, code literal)
struct aug_ir
{		
    aug_input* input; // weak ref to source file/code

    // Transient IR data
    aug_std_array<aug_ir_frame> frame_stack;
    int label_count;

    // Generated data
    aug_std_array<aug_ir_operation> operations;
    size_t bytecode_offset;
    
    // Assigned to the outer-most frame's symbol table. 
    // This field is initialized after generation, so not available during the generation pass. 
    aug_symtable globals;
    bool valid;
};

inline void aug_ir_init(aug_ir& ir, aug_input* input)
{
    ir.valid = true;
    ir.input = input;
    ir.label_count = 0;
    ir.bytecode_offset = 0;
    ir.frame_stack.clear();
    ir.operations.clear();
}

inline size_t aug_ir_operand_size(const aug_ir_operand& operand)
{
    switch (operand.type)
    {
    case AUG_IR_OPERAND_NONE:
        return 0;
    case AUG_IR_OPERAND_BOOL:
        return sizeof(operand.data.b);
    case AUG_IR_OPERAND_INT:
        return sizeof(operand.data.i);
    case AUG_IR_OPERAND_FLOAT:
        return sizeof(operand.data.f);
    case AUG_IR_OPERAND_BYTES:
        return strlen(operand.data.str) + 1; // +1 for null term
    }
    return 0;
}

inline size_t aug_ir_operation_size(const aug_ir_operation& operation)
{
    size_t size = sizeof(operation.opcode);
    size += aug_ir_operand_size(operation.operand);
    return size;
}

inline size_t aug_ir_add_operation(aug_ir& ir, aug_opcode opcode, const aug_ir_operand& operand)
{
    aug_ir_operation operation;
    operation.opcode = opcode;
    operation.operand = operand;
#ifdef AUG_DEBUG
    operation.bytecode_offset = ir.bytecode_offset;
#endif //AUG_DEBUG

    ir.bytecode_offset += aug_ir_operation_size(operation);
    ir.operations.push_back(operation);
    return ir.operations.size()-1;
}

inline size_t aug_ir_add_operation(aug_ir& ir, aug_opcode opcode)
{
    aug_ir_operand operand;
    return aug_ir_add_operation(ir, opcode, operand);
}

inline aug_ir_operation& aug_ir_get_operation(aug_ir& ir, size_t operation_index)
{
    assert(operation_index < ir.operations.size());
    return ir.operations.at(operation_index);
}

inline aug_ir_operand aug_ir_operand_from_bool(bool data)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_BOOL;
    operand.data.b = data;
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

inline aug_ir_frame& aug_ir_current_frame(aug_ir& ir)
{
    assert(ir.frame_stack.size() > 0);
    return ir.frame_stack.back();
}

inline aug_ir_scope& aug_ir_current_scope(aug_ir& ir)
{
    aug_ir_frame& frame = aug_ir_current_frame(ir);
    assert(frame.scope_stack.size() > 0);
    return frame.scope_stack.back();
}

inline bool aug_ir_current_scope_is_global(aug_ir& ir)
{
    aug_ir_frame& frame = aug_ir_current_frame(ir);
    if(ir.frame_stack.size() == 1 && frame.scope_stack.size() == 1)
        return true;
    return false;
}

inline int aug_ir_current_scope_local_offset(aug_ir& ir)
{
    const aug_ir_scope& scope = aug_ir_current_scope(ir);
    return scope.stack_offset - scope.base_index;
}

inline int aug_ir_calling_offset(aug_ir& ir)
{
    const aug_ir_scope& scope = aug_ir_current_scope(ir);
    aug_ir_frame& frame = aug_ir_current_frame(ir);
    return (scope.stack_offset - frame.base_index) + frame.arg_count;
}

inline void aug_ir_push_frame(aug_ir& ir, int arg_count)
{
    aug_ir_frame frame;
    frame.arg_count = arg_count;

    if (ir.frame_stack.size() > 0)
    {
        const aug_ir_scope& scope = aug_ir_current_scope(ir);
        frame.base_index = scope.stack_offset;
    }
    else
    {
        frame.base_index = 0;
    }

    aug_ir_scope scope;
    scope.base_index = frame.base_index;
    scope.stack_offset = frame.base_index;
    frame.scope_stack.push_back(scope);

    ir.frame_stack.push_back(frame);
}

inline void aug_ir_pop_frame(aug_ir& ir)
{
    if(ir.frame_stack.size() == 1)
        ir.globals = aug_ir_current_scope(ir).symtable; // pass global frame symbols to ir
    ir.frame_stack.pop_back();
}

inline void aug_ir_push_scope(aug_ir& ir)
{
    const aug_ir_scope& current_scope = aug_ir_current_scope(ir);
    aug_ir_scope scope;
    scope.base_index = current_scope.stack_offset;
    scope.stack_offset = current_scope.stack_offset;

    aug_ir_frame& frame = aug_ir_current_frame(ir);
    frame.scope_stack.push_back(scope);
}

inline void aug_ir_pop_scope(aug_ir& ir)
{
    const aug_ir_operand delta = aug_ir_operand_from_int(aug_ir_current_scope_local_offset(ir));
    aug_ir_add_operation(ir, AUG_OPCODE_DEC_STACK, delta);

    aug_ir_frame& frame = aug_ir_current_frame(ir);
    frame.scope_stack.pop_back();
}

inline bool aug_ir_set_var(aug_ir& ir, const aug_std_string& name)
{
    aug_ir_scope& scope = aug_ir_current_scope(ir);
    const int offset = scope.stack_offset++;

    if (scope.symtable.count(name) != 0)
        return false;

    aug_symbol sym;
    sym.type = AUG_SYM_VAR;
    sym.offset = offset;
    sym.argc = 0;
    
    if (aug_ir_current_scope_is_global(ir))
        sym.scope = AUG_SYM_SCOPE_GLOBAL;
    else
        sym.scope = AUG_SYM_SCOPE_LOCAL;

    scope.symtable[name] = sym;
    return true;
}

inline bool aug_ir_set_param(aug_ir& ir, const aug_std_string& name)
{
    aug_ir_scope& scope = aug_ir_current_scope(ir);
    const int offset = scope.stack_offset++;

    if (scope.symtable.count(name) != 0)
        return false;

    aug_symbol sym;
    sym.type = AUG_SYM_VAR;
    sym.offset = offset;
    sym.argc = 0;
    
    if (aug_ir_current_scope_is_global(ir))
        sym.scope = AUG_SYM_SCOPE_GLOBAL;
    else
        sym.scope = AUG_SYM_SCOPE_PARAM;

    scope.symtable[name] = sym;
    return true;
}

inline bool aug_ir_set_func(aug_ir& ir, const aug_std_string& name, int param_count)
{
    aug_ir_scope& scope = aug_ir_current_scope(ir);
    const int offset = ir.bytecode_offset;
    if (scope.symtable.count(name) != 0)
        return false;

    aug_symbol sym;
    sym.type = AUG_SYM_FUNC;
    sym.offset = offset;
    sym.argc = param_count;

    if (aug_ir_current_scope_is_global(ir))
        sym.scope = AUG_SYM_SCOPE_GLOBAL;
    else
        sym.scope = AUG_SYM_SCOPE_LOCAL;

    scope.symtable[name] = sym;
    return true;
}

inline aug_symbol aug_ir_get_symbol(aug_ir& ir, const aug_std_string& name)
{
    for (int i = ir.frame_stack.size() - 1; i >= 0; --i)
    {
        const aug_ir_frame& frame = ir.frame_stack.at(i);
        for (int i = frame.scope_stack.size() - 1; i >= 0; --i)
        {
            const aug_ir_scope& scope = frame.scope_stack.at(i);
            const aug_symtable& symtable = scope.symtable;
            if (symtable.count(name))
                return symtable.at(name);
        }
    }

    aug_symbol sym;
    sym.offset = AUG_OPCODE_INVALID;
    sym.type = AUG_SYM_NONE;
    sym.argc = 0;
    return sym;
}

inline aug_symbol aug_ir_symbol_relative(aug_ir& ir, const aug_std_string& name)
{
    for (int i = ir.frame_stack.size() - 1; i >= 0; --i)
    {
        const aug_ir_frame& frame = ir.frame_stack.at(i);
        for (int j = frame.scope_stack.size() - 1; j >= 0; --j)
        {
            const aug_ir_scope& scope = frame.scope_stack.at(j);
            const aug_symtable& symtable = scope.symtable;
            if (symtable.count(name))
            {
                aug_symbol symbol = symtable.at(name);
                
                int relative_offset;
                switch (symbol.scope)
                {
                case AUG_SYM_SCOPE_GLOBAL:
                    relative_offset = symbol.offset;
                    break;
                case AUG_SYM_SCOPE_PARAM:
                {
                    const aug_ir_frame& frame = aug_ir_current_frame(ir);
                    symbol.offset = symbol.offset - frame.base_index;
                    break;
                }
                case AUG_SYM_SCOPE_LOCAL:
                {
                    const aug_ir_frame& frame = aug_ir_current_frame(ir);
                    //If this variable is a local variable in an outer frame. Offset by 2 (ret addr and base index) for each frame delta
                    int frame_delta = (ir.frame_stack.size()-1) - i;
                    symbol.offset = symbol.offset - frame.base_index - frame_delta * 2;
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

inline aug_symbol aug_ir_get_symbol_local(aug_ir& ir, const aug_std_string& name)
{
    const aug_ir_scope& scope = aug_ir_current_scope(ir);
    if (scope.symtable.count(name))
        return scope.symtable.at(name);

    aug_symbol sym;
    sym.offset = AUG_OPCODE_INVALID;
    sym.type = AUG_SYM_NONE;
    sym.argc = 0;
    return sym;
}

// --------------------------------------- Value Operations -------------------------------------------------------//
inline bool aug_set_bool(aug_value* value, bool data)
{
    if (value == NULL)
        return false;
    value->type = AUG_BOOL;
    value->b = data;
    return true;
}

inline bool aug_set_int(aug_value* value, int data)
{
    if (value == NULL)
        return false;
    value->type = AUG_INT;
    value->i = data;
    return true;
}

inline bool aug_set_float(aug_value* value, float data)
{
    if (value == NULL)
        return false;
    value->type = AUG_FLOAT;
    value->f = data;
    return true;
}

inline bool aug_set_string(aug_value* value, const char* data)
{
    if (value == NULL)
        return false;

    value->type = AUG_STRING;
    value->str = AUG_NEW(aug_string);
    value->str->ref_count = 1;
    value->str->assign(data, strlen(data));
    return true;
}

inline bool aug_set_list(aug_value* value)
{
    if (value == NULL)
        return false;

    value->type = AUG_LIST;
    value->list = AUG_NEW(aug_list);
    value->list->ref_count = 1; 
     return true;
}

aug_value aug_none()
{
    aug_value value;
    value.type = AUG_NONE;
    return value;
}

bool aug_is_true(const aug_value* value)
{
    if (value == NULL)
        return false;

    switch (value->type)
    {
    case AUG_NONE:
        return false;
    case AUG_BOOL:
        return value->b;
    case AUG_INT:
        return value->i != 0;
    case AUG_FLOAT:
        return value->f != 0.0f;
    case AUG_STRING:
        return value->str != NULL;
    case AUG_OBJECT:
        return value->obj != NULL;
    case AUG_LIST:
        return value->list != NULL;
    }
    return false;
}

bool aug_get_bool(const aug_value& value)
{
    switch (value.type)
    {
    case AUG_NONE:
        return false;
    case AUG_BOOL:
        return value.b;
    case AUG_INT:
        return value.i != 0;
    case AUG_FLOAT:
        return value.f != 0.0f;
    case AUG_STRING:
        return value.str != NULL;
    case AUG_OBJECT:
        return value.obj != NULL;
    case AUG_LIST:
        return value.list != NULL;
    }
    return false;
}

int aug_get_int(const aug_value& value)
{
    switch (value.type)
    {
    case AUG_NONE:
        return 0;
    case AUG_BOOL:
        return value.b ? 1 : 0;
    case AUG_INT:
        return value.i;
    case AUG_FLOAT:
        return (int)value.f;
    case AUG_STRING:
        return 0;
    case AUG_OBJECT:
        return 0;
    case AUG_LIST:
        return 0;
    }
    return 0;
}

float aug_get_float(const aug_value& value)
{
    switch (value.type)
    {
    case AUG_NONE:
        return 0.0f;
    case AUG_BOOL:
        return value.b ? 1.0f : 0.0f;
    case AUG_INT:
        return (float)value.i;
    case AUG_FLOAT:
        return value.f;
    case AUG_STRING:
        return 0.0f;
    case AUG_OBJECT:
        return 0.0f;
    case AUG_LIST:
        return 0.0f;
    }
    return 0.0f;
}

inline bool aug_move(aug_value* to, aug_value* from)
{
    if(from == NULL || to == NULL)
        return false;

    aug_delete(to);
    *to = *from;
    *from = aug_none();

    return true;
}

inline bool aug_assign(aug_value* to, aug_value* from)
{
    if(from == NULL || to == NULL)
        return false;

    aug_delete(to);
    *to = *from;

    switch (to->type)
    {
    case AUG_STRING:
        assert(to->str);
        ++to->str->ref_count;
        break;
    case AUG_OBJECT:
        assert(to->obj);
        ++to->obj->ref_count;
        break;
    case AUG_LIST:
        assert(to->list);
        ++to->list->ref_count;
        break;
    default:
        break;
    }
    return true;
}

inline void aug_delete(aug_value* value)
{
    if (value == NULL)
        return;

    switch (value->type)
    {
    case AUG_NONE:
    case AUG_BOOL:
    case AUG_INT:
    case AUG_FLOAT:
        break;
    case AUG_STRING:
        if(value->str && --value->str->ref_count <= 0)
        {
            value->type = AUG_NONE;
            AUG_DELETE(value->str);
        }
        break;
    case AUG_OBJECT:
        if(value->obj && --value->obj->ref_count <= 0)
        {
            value->type = AUG_NONE;
            AUG_DELETE(value->obj);
        }
        break;
    case AUG_LIST:
        if(value->list && --value->list->ref_count <= 0)
        {
            value->type = AUG_NONE;
            for(aug_value& entry : value->list->data)
                aug_delete(&entry);
            AUG_DELETE(value->list);
        }
        break;
    }
}

#define AUG_DEFINE_BINOP(result, lhs, rhs,                      \
    int_int_case,                                               \
    int_float_case,                                             \
    float_int_case,                                             \
    float_float_case,                                           \
    bool_bool_case                                              \
)                                                               \
{                                                               \
    if(result == NULL || lhs == NULL || rhs == NULL )  \
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
        return aug_set_bool(result, lhs->b != rhs->b)
    )
    return false;
}

inline bool aug_approxeq(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP(result, lhs, rhs,
        return aug_set_bool(result, abs(lhs->i - rhs->i) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(result, abs(lhs->i - rhs->f) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(result, abs(lhs->f - rhs->i) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(result, abs(lhs->f - rhs->f) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(result, lhs->b == rhs->b)
    )
    return false;
}

#undef AUG_DEFINE_BINOP

// -------------------------------------- Virtual Machine / Bytecode ----------------------------------------------// 

// Used to convert values to/from bytes for constant values
union aug_vm_bytecode_value
{
    bool b;
    int i;
    float f;
    unsigned char bytes[sizeof(float)]; //Used to access raw byte data to bool, float and int types
};

static_assert(sizeof(float) >= sizeof(int), "Ensure bytes array has enough space to contain both int and float data types");

#define AUG_VM_ERROR(vm, ...)                      \
{                                                  \
    AUG_LOG_ERROR(vm.error_callback, __VA_ARGS__); \
    vm.valid = false;                              \
    vm.instruction = NULL;                         \
}

#define AUG_VM_UNOP_ERROR(vm, arg, op)               \
{                                                    \
    if (arg != NULL)                                 \
        AUG_VM_ERROR(vm, "%s %s not defined",        \
            op,                                      \
            aug_value_type_labels[(int)arg->type]);  \
}

#define AUG_VM_BINOP_ERROR(vm, lhs, rhs, op)           \
{                                                      \
    if (lhs != NULL && rhs != NULL)                    \
        AUG_VM_ERROR(vm, "%s %s %s not defined",  \
            aug_value_type_labels[(int)lhs->type],     \
            op,                                        \
            aug_value_type_labels[(int)rhs->type]);    \
}

inline aug_value* aug_vm_top(aug_vm& vm)
{
    return &vm.stack[vm.stack_index -1];
}

inline aug_value* aug_vm_push(aug_vm& vm)
{
    if(vm.stack_index >= AUG_STACK_SIZE)
    {                                              
        if(vm.valid)
            AUG_VM_ERROR(vm, "Stack overflow");      
        return NULL;                           
    }
    aug_value* top = &vm.stack[vm.stack_index++];
    //*top = aug_none();
    return top;
}

inline aug_value* aug_vm_pop(aug_vm& vm)
{
    aug_value* top = aug_vm_top(vm);
    --vm.stack_index;
    return top;
}

inline void aug_vm_free(aug_value* value)
{
    aug_delete(value);
}

inline aug_value* aug_vm_get_global(aug_vm& vm, int stack_offset)
{
    if (stack_offset < 0)
    {
        if (vm.instruction)
            AUG_VM_ERROR(vm, "Stack underflow");
        return NULL;
    }
    else if (stack_offset >= AUG_STACK_SIZE)
    {
        if (vm.instruction)
            AUG_VM_ERROR(vm, "Stack overflow");
        return NULL;
    }

    return &vm.stack[stack_offset];
}

inline aug_value* aug_vm_get_local(aug_vm& vm, int stack_offset)
{
    return aug_vm_get_global(vm, vm.base_index + stack_offset);
}

inline int aug_vm_read_bool(aug_vm& vm)
{
    aug_vm_bytecode_value bytecode_value;
    for (size_t i = 0; i < sizeof(bytecode_value.b); ++i)
        bytecode_value.bytes[i] = *(vm.instruction++);
    return bytecode_value.b;
}

inline int aug_vm_read_int(aug_vm& vm)
{
    aug_vm_bytecode_value bytecode_value;
    for (size_t i = 0; i < sizeof(bytecode_value.i); ++i)
        bytecode_value.bytes[i] = *(vm.instruction++);
    return bytecode_value.i;
}

inline float aug_vm_read_float(aug_vm& vm)
{
    aug_vm_bytecode_value bytecode_value;
    for (size_t i = 0; i < sizeof(bytecode_value.f); ++i)
        bytecode_value.bytes[i] = *(vm.instruction++);
    return bytecode_value.f;
}

inline const char* aug_vm_read_bytes(aug_vm& vm)
{
    size_t len = 1; // include null terminating
    while (*(vm.instruction++))
        len++;
    return vm.instruction - len;
}

void aug_vm_startup(aug_vm& vm)
{
    vm.bytecode = NULL;
    vm.instruction = NULL;
    vm.stack_index = 0;
    vm.base_index = 0;
    vm.valid = false; 
}

void aug_vm_shutdown(aug_vm& vm)
{
    // Cleanup stack values. Free any outstanding values
    while(vm.stack_index > 0)
        aug_vm_free(aug_vm_pop(vm));

    // Ensure that stack has returned to beginning state
    if (vm.stack_index != 0)
        AUG_LOG_ERROR(vm.error_callback, "Virtual machine shutdown error. Invalid stack state");
}

void aug_vm_boot(aug_vm& vm, const aug_script& script)
{
    if (script.valid && script.bytecode.size() == 0)
        vm.bytecode = NULL;
    else
        vm.bytecode = &script.bytecode[0];
    
    vm.instruction = vm.bytecode;
    vm.valid = (vm.bytecode != NULL);
}

void aug_vm_execute(aug_vm& vm)
{
    while(vm.instruction)
    {
        aug_opcode opcode = (aug_opcode) (*vm.instruction);
        ++vm.instruction;

        switch(opcode)
        {
            case AUG_OPCODE_NO_OP:
                break;
            case AUG_OPCODE_EXIT:
            {
                vm.instruction = NULL;
                break;
            }
            case AUG_OPCODE_POP:
            {
                aug_vm_free(aug_vm_pop(vm));
                break;
            }
            case AUG_OPCODE_PUSH_NONE:
            {
                aug_value* value = aug_vm_push(vm);
                if (value == NULL)
                    break;
                value->type = AUG_NONE;
                break;
            }
            case AUG_OPCODE_PUSH_BOOL:
            {
                aug_value* value = aug_vm_push(vm);
                if (value == NULL)
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
           case AUG_OPCODE_PUSH_LIST:
            {
                aug_value value;
                aug_set_list(&value);

                int list_count = aug_vm_read_int(vm);
                while(list_count-- > 0)
                {
                    aug_value entry = aug_none();
                    aug_move(&entry, aug_vm_pop(vm));
                    value.list->data.push_back(entry);
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
            case AUG_OPCODE_LOAD_LOCAL:
            {
                const int stack_offset = aug_vm_read_int(vm);
                aug_value* local = aug_vm_get_local(vm, stack_offset);

                aug_value* top = aug_vm_pop(vm);
                aug_move(local, top);
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
                if (!aug_add(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "+");
                break;
            }
            case AUG_OPCODE_SUB:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);    
                if (!aug_sub(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "-");
                break;
            }
            case AUG_OPCODE_MUL:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);    
                if (!aug_mul(target, lhs, rhs))
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
                if (!aug_pow(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "^");
                break;
            }
            case AUG_OPCODE_MOD:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if (!aug_mod(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "%%");
                break;
            }
            case AUG_OPCODE_NOT:
            {
                aug_value* arg = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if (!aug_set_bool(target, !aug_is_true(arg)))
                    AUG_VM_UNOP_ERROR(vm, arg, "!");
                break;
            }
            case AUG_OPCODE_AND:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if (!aug_set_bool(target, aug_is_true(lhs) && aug_is_true(rhs)))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "&");
                break;
            }
            case AUG_OPCODE_OR:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if (!aug_set_bool(target, aug_is_true(lhs) || aug_is_true(rhs)))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "|");
                break;
            }
            case AUG_OPCODE_LT:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if (!aug_lt(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "<");
                break;
            }
            case AUG_OPCODE_LTE:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if (!aug_lte(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "<=");
                break;
            }
            case AUG_OPCODE_GT:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if (!aug_gt(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, ">");
                break;
            }
            case AUG_OPCODE_GTE:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if (!aug_gte(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, ">=");
                break;
            }
            case AUG_OPCODE_EQ:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if (!aug_eq(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "==");
                break;
            }
            case AUG_OPCODE_NEQ:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if (!aug_neq(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "!=");
                break;
            }
            case AUG_OPCODE_APPROXEQ:
            {
                aug_value* rhs = aug_vm_pop(vm);
                aug_value* lhs = aug_vm_pop(vm);
                aug_value* target = aug_vm_push(vm);
                if (!aug_approxeq(target, lhs, rhs))
                    AUG_VM_BINOP_ERROR(vm, lhs, rhs, "~=");
                break;
            }
            case AUG_OPCODE_JUMP:
            {
                const int instruction_offset = aug_vm_read_int(vm);
                vm.instruction = vm.bytecode + instruction_offset;
                break;
            }
            case AUG_OPCODE_JUMP_NZERO:
            {
                const int instruction_offset = aug_vm_read_int(vm);
                aug_value* top = aug_vm_pop(vm);
                if (aug_is_true(top) != 0)
                    vm.instruction = vm.bytecode + instruction_offset;
                aug_vm_free(top);
                break;
            }
            case AUG_OPCODE_JUMP_ZERO:
            {
                const int instruction_offset = aug_vm_read_int(vm);
                aug_value* top = aug_vm_pop(vm);
                if (aug_is_true(top) == 0)
                    vm.instruction = vm.bytecode + instruction_offset;
                aug_vm_free(top);
                break;
            }
            case AUG_OPCODE_PUSH_BASE:
            {              
                aug_value* value = aug_vm_push(vm);
                if (value == NULL)
                    break;
                value->type = AUG_INT;
                value->i = vm.base_index;
            break;
            }
            case AUG_OPCODE_DEC_STACK:
            {
                const int delta = aug_vm_read_int(vm);
                for(int i = 0; i < delta; ++i)
                    aug_vm_free(aug_vm_pop(vm));
                //vm.stack_index = vm.stack_index - delta;
                break;
            }
            case AUG_OPCODE_CALL:
            {
                const int func_addr = aug_vm_read_int(vm);
                vm.instruction = vm.bytecode + func_addr;
                vm.base_index = vm.stack_index;
                break;
            }
            case AUG_OPCODE_RETURN:
            {
                aug_value* ret_value = aug_vm_pop(vm);
                
                const int delta = aug_vm_read_int(vm);
                for(int i = 0; i < delta; ++i)
                    aug_vm_free(aug_vm_pop(vm));
                //vm.stack_index = vm.stack_index - delta;

                aug_value* ret_addr = aug_vm_pop(vm);
                if (ret_addr->i == AUG_OPCODE_INVALID)
                    vm.instruction = NULL;
                else
                    vm.instruction = vm.bytecode + ret_addr->i;
                aug_vm_free(ret_addr);

                aug_value* ret_base = aug_vm_pop(vm);
                vm.base_index = ret_base->i;
                aug_vm_free(ret_base);

                aug_value* top = aug_vm_push(vm);
                if (ret_value != NULL && top != NULL)
                    *top = *ret_value;

                break;
            }
            case AUG_OPCODE_CALL_EXT:
            {
                aug_value* func_index_value = aug_vm_pop(vm);
                if(func_index_value == NULL || func_index_value->type != AUG_INT)
                {
                    aug_vm_free(func_index_value);

                    AUG_VM_ERROR(vm, "External Function Call expected function index to be pushed on stack");
                    break;                    
                }

                const int arg_count = aug_vm_read_int(vm);                
                const int func_index = func_index_value->i;

                // Gather arguments
                aug_std_array<aug_value> args;
                args.reserve(arg_count);
                for (int i = arg_count - 1; i >= 0; --i)
                {
                    aug_value* arg = aug_vm_pop(vm);
                    if (arg != NULL)
                    {
                        aug_value value = aug_none();
                        aug_move(&value, arg);
                        args.push_back(value);
                    }
                }

                // Check function call
                if (func_index < 0 || func_index >= (int)vm.external_functions.size() || vm.external_functions[func_index] == NULL)
                {
                    AUG_VM_ERROR(vm, "External Function Called at index %d not registered", func_index);
                }
                else if ((int)args.size() != arg_count)
                {
                    const aug_std_string& func_name = vm.external_function_names[func_index];
                    AUG_VM_ERROR(vm, "External Function Call %s passed %d arguments, expected %d", func_name.c_str(), (int)args.size(), arg_count);
                }
                else
                {
                    // Call the external function. Move return value on to top of stack
                    aug_value ret_value = vm.external_functions[func_index](args);
                    aug_value* top = aug_vm_push(vm);
                    if (top)
                        aug_move(top, &ret_value);
                }

                // Cleanup arguments
                aug_vm_free(func_index_value);
                for (int i = 0; i < arg_count; ++i)
                    aug_vm_free(&args[i]);

                break;
            }
            default:
                // UNSUPPORTED!!!!
            break;
        }

#if AUG_DEBUG_VM
        printf("%s\n", aug_opcode_labels[(int)opcode]);

        for(int i = 0; i < 16; ++i)
        {
            aug_value val = vm.stack[i];
            printf("%s %d: %s ", (vm.stack_index-1) == i ? ">" : " ", i, aug_value_type_labels[(int)val.type]);
            switch(val.type)
            {
                case AUG_INT: printf("%d", val.i); break;
                case AUG_FLOAT: printf("%f", val.f); break;
                case AUG_STRING: printf("%s", val.str->c_str()); break;
                case AUG_BOOL: printf("%s", val.b ? "true" : "false"); break;
                default: break;
            }
            printf("\n");
        }
        getchar();
#endif 
    }
}

// -------------------------------------- Passes ------------------------------------------------// 
bool aug_pass_semantic_check(const aug_ast* node)
{
    if(node == NULL)
        return false;
    return true;
}
// -------------------------------------- Transformations ---------------------------------------// 

void aug_ast_to_ir(aug_vm& vm, const aug_ast* node, aug_ir& ir)
{
    if(node == NULL || !ir.valid)
        return;

    const aug_token& token = node->token;
    const aug_std_array<aug_ast*>& children = node->children;

    switch(node->id)
    {
        case AUG_AST_ROOT:
        {
            const aug_std_array<aug_ast*>& children = node->children;

            aug_ir_push_frame(ir, 0); // push a global frame

            for (aug_ast* stmt : children)
                aug_ast_to_ir(vm, stmt, ir);

            // Restore stack
            //const aug_ir_operand delta = aug_ir_operand_from_int(aug_ir_current_scope_local_offset(ir));
            //aug_ir_add_operation(ir, AUG_OPCODE_DEC_STACK, delta);
            aug_ir_add_operation(ir, AUG_OPCODE_EXIT);

            aug_ir_pop_frame(ir); // pop global frame
            break;
        }
        case AUG_AST_BLOCK: 
        {
            for (aug_ast* stmt : children)
                aug_ast_to_ir(vm, stmt, ir);
            break;
        }
        case AUG_AST_LITERAL:
        {
            const aug_token& token = node->token;
            switch (token.id)
            {
                case AUG_TOKEN_DECIMAL:
                {
                    const int data = strtol(token.data.c_str(), NULL, 10);
                    const aug_ir_operand& operand = aug_ir_operand_from_int(data);
                    aug_ir_add_operation(ir, AUG_OPCODE_PUSH_INT, operand);
                    break;
                }
                case AUG_TOKEN_HEXIDECIMAL:
                {
                    const unsigned int data = strtoul(token.data.c_str(), NULL, 16);
                    const aug_ir_operand& operand = aug_ir_operand_from_int(data);
                    aug_ir_add_operation(ir, AUG_OPCODE_PUSH_INT, operand);
                    break;
                }
                case AUG_TOKEN_BINARY:
                {
                    const unsigned int data = strtoul(token.data.c_str(), NULL, 2);
                    const aug_ir_operand& operand = aug_ir_operand_from_int(data);
                    aug_ir_add_operation(ir, AUG_OPCODE_PUSH_INT, operand);
                    break;
                }
                case AUG_TOKEN_FLOAT:
                {
                    const float data = strtof(token.data.c_str(), NULL);
                    const aug_ir_operand& operand = aug_ir_operand_from_float(data);
                    aug_ir_add_operation(ir, AUG_OPCODE_PUSH_FLOAT, operand);
                    break;
                }
                case AUG_TOKEN_STRING:
                {
                    const aug_ir_operand& operand = aug_ir_operand_from_str(token.data.c_str());
                    aug_ir_add_operation(ir, AUG_OPCODE_PUSH_STRING, operand);
                    break;
                }
                case AUG_TOKEN_TRUE:
                {
                    const aug_ir_operand& operand = aug_ir_operand_from_bool(true);
                    aug_ir_add_operation(ir, AUG_OPCODE_PUSH_BOOL, operand);
                    break;
                }
                case AUG_TOKEN_FALSE:
                {
                    const aug_ir_operand& operand = aug_ir_operand_from_bool(false);
                    aug_ir_add_operation(ir, AUG_OPCODE_PUSH_BOOL, operand);
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
            const aug_symbol& symbol = aug_ir_symbol_relative(ir, token.data);
            if (symbol.type == AUG_SYM_NONE)
            {
                ir.valid = false;
                AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Variable %s not defined in current block", token.data.c_str());
                return;
            }

            if (symbol.type == AUG_SYM_FUNC)
            {
                ir.valid = false;
                AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Function %s can not be used as a variable", token.data.c_str());
                return;
            }

            const aug_ir_operand& address_operand = aug_ir_operand_from_int(symbol.offset);
            if(symbol.scope == AUG_SYM_SCOPE_GLOBAL)
                aug_ir_add_operation(ir, AUG_OPCODE_PUSH_GLOBAL, address_operand);
            else
                aug_ir_add_operation(ir, AUG_OPCODE_PUSH_LOCAL, address_operand);
            break;
        }
        case AUG_AST_UNARY_OP:
        {
            assert(children.size() == 1); // token [0]

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
            assert(children.size() == 2); // [0] token [1]

            aug_ast_to_ir(vm, children[0], ir); // LHS
            aug_ast_to_ir(vm, children[1], ir); // RHS
            bool load_var = false;

            switch (token.id)
            {
                case AUG_TOKEN_ADD:    aug_ir_add_operation(ir, AUG_OPCODE_ADD); break;
                case AUG_TOKEN_SUB:    aug_ir_add_operation(ir, AUG_OPCODE_SUB); break;
                case AUG_TOKEN_MUL:    aug_ir_add_operation(ir, AUG_OPCODE_MUL); break;
                case AUG_TOKEN_DIV:    aug_ir_add_operation(ir, AUG_OPCODE_DIV); break;
                case AUG_TOKEN_MOD:    aug_ir_add_operation(ir, AUG_OPCODE_MOD); break;
                case AUG_TOKEN_POW:    aug_ir_add_operation(ir, AUG_OPCODE_POW); break;
                case AUG_TOKEN_AND:    aug_ir_add_operation(ir, AUG_OPCODE_AND); break;
                case AUG_TOKEN_OR:     aug_ir_add_operation(ir, AUG_OPCODE_OR); break;
                case AUG_TOKEN_LT:     aug_ir_add_operation(ir, AUG_OPCODE_LT);  break;
                case AUG_TOKEN_LT_EQ:  aug_ir_add_operation(ir, AUG_OPCODE_LTE); break;
                case AUG_TOKEN_GT:     aug_ir_add_operation(ir, AUG_OPCODE_GT);  break;
                case AUG_TOKEN_GT_EQ:  aug_ir_add_operation(ir, AUG_OPCODE_GTE); break;
                case AUG_TOKEN_EQ:     aug_ir_add_operation(ir, AUG_OPCODE_EQ);  break;
                case AUG_TOKEN_NOT_EQ: aug_ir_add_operation(ir, AUG_OPCODE_NEQ); break;
                case AUG_TOKEN_APPROX_EQ: aug_ir_add_operation(ir, AUG_OPCODE_APPROXEQ); break;
                case AUG_TOKEN_ADD_EQ:
                {
                    if (children[0] == NULL || children[0]->id != AUG_AST_VARIABLE)
                    {
                        ir.valid = false;
                        AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Left hand operand must be a variable");
                        break;
                    }

                    aug_ir_add_operation(ir, AUG_OPCODE_ADD);
                    load_var = true;
                    break;
                }
                case AUG_TOKEN_SUB_EQ:
                {
                    if (children[0] == NULL || children[0]->id != AUG_AST_VARIABLE)
                    {
                        ir.valid = false;
                        AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Left hand operand must be a variable");
                        break;
                    }

                    load_var = true;
                    aug_ir_add_operation(ir, AUG_OPCODE_SUB);
                    load_var = true;
                    break;
                }
                case AUG_TOKEN_MUL_EQ:
                {
                    if (children[0] == NULL || children[0]->id != AUG_AST_VARIABLE)
                    {
                        ir.valid = false;
                        AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Left hand operand must be a variable");
                        break;
                    }

                    aug_ir_add_operation(ir, AUG_OPCODE_MUL);
                    load_var = true;
                    break;
                }
                case AUG_TOKEN_DIV_EQ:
                {
                    if (children[0] == NULL || children[0]->id != AUG_AST_VARIABLE)
                    {
                        ir.valid = false;
                        AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Left hand operand must be a variable");
                        break;
                    }

                    aug_ir_add_operation(ir, AUG_OPCODE_DIV);
                    load_var = true;
                    break;
                }
                case AUG_TOKEN_MOD_EQ:
                {
                    if (children[0] == NULL || children[0]->id != AUG_AST_VARIABLE)
                    {
                        ir.valid = false;
                        AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Left hand operand must be a variable");
                        break;
                    }

                    aug_ir_add_operation(ir, AUG_OPCODE_MOD);
                    load_var = true;
                    break;
                }
                case AUG_TOKEN_POW_EQ:
                {
                    if (children[0] == NULL || children[0]->id != AUG_AST_VARIABLE)
                    {
                        ir.valid = false;
                        AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Left hand operand must be a variable");
                        break;
                    }

                    aug_ir_add_operation(ir, AUG_OPCODE_POW);
                    load_var = true;
                    break;
                }
                default:
                    assert(0);
                    break;
            }

            if(load_var)
            {
                const aug_symbol& symbol = aug_ir_symbol_relative(ir, children[0]->token.data); // previous LHS pass will handle and var semantic errors
                const aug_ir_operand& address_operand = aug_ir_operand_from_int(symbol.offset);
                if(symbol.scope == AUG_SYM_SCOPE_GLOBAL)
                    aug_ir_add_operation(ir, AUG_OPCODE_LOAD_GLOBAL, address_operand);
                else
                    aug_ir_add_operation(ir, AUG_OPCODE_LOAD_LOCAL, address_operand);
            }
            break;
        }
        case AUG_AST_LIST:
        {
            const int expr_count = children.size();
            for (int i = expr_count - 1; i >= 0; --i)
                aug_ast_to_ir(vm, children[i], ir);

            const aug_ir_operand& count_operand = aug_ir_operand_from_int(expr_count);
            aug_ir_add_operation(ir, AUG_OPCODE_PUSH_LIST, count_operand);
            break;
        }
        case AUG_AST_STMT_EXPR:
        {
            if (children.size() == 1)
            {
                aug_ast_to_ir(vm, children[0], ir);

                // discard the top if a non-assignment binop
                bool discard = true;
                if (children[0] && children[0]->id == AUG_AST_BINARY_OP)
                {
                    switch (children[0]->token.id)
                    {
                    case AUG_TOKEN_ADD_EQ:
                    case AUG_TOKEN_SUB_EQ:
                    case AUG_TOKEN_DIV_EQ:
                    case AUG_TOKEN_MUL_EQ:
                    case AUG_TOKEN_MOD_EQ:
                    case AUG_TOKEN_POW_EQ:
                        discard = false;
                        break;
                    default: break;
                    }
                }
                if (discard)
                    aug_ir_add_operation(ir, AUG_OPCODE_POP);
            }
            break;
        }
        case AUG_AST_STMT_ASSIGN_VAR:
        {
            if (children.size() == 1) // token = [0]
                aug_ast_to_ir(vm, children[0], ir);

            const aug_symbol& symbol = aug_ir_symbol_relative(ir, token.data);
            if (symbol.type == AUG_SYM_NONE)
            {
                aug_ir_set_var(ir, token.data);
                return;
            }
            else if (symbol.type == AUG_SYM_FUNC)
            {
                ir.valid = false;
                AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Can not assign function %s as a variable", token.data.c_str());
                return;
            }

            const aug_ir_operand& address_operand = aug_ir_operand_from_int(symbol.offset);
            if(symbol.scope == AUG_SYM_SCOPE_GLOBAL)
                aug_ir_add_operation(ir, AUG_OPCODE_LOAD_GLOBAL, address_operand);
            else
                aug_ir_add_operation(ir, AUG_OPCODE_LOAD_LOCAL, address_operand);
            break;
        }
        case AUG_AST_STMT_DEFINE_VAR:
        {
            if (children.size() == 1) // token = [0]
                aug_ast_to_ir(vm, children[0], ir);

            const aug_symbol symbol = aug_ir_get_symbol_local(ir, token.data);
            if (symbol.offset != AUG_OPCODE_INVALID)
            {
                ir.valid = false;
                AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Variable %s already defined in block", token.data.c_str());
                return;
            }

            aug_ir_set_var(ir, token.data);
        
            break;
        }
        case AUG_AST_STMT_IF:
        {
            assert(children.size() == 2); //if ([0]) {[1]}

            const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);

            // Evaluate expression. 
            aug_ast_to_ir(vm, children[0], ir);

            //Jump to end if false
            const size_t end_block_jmp = aug_ir_add_operation(ir, AUG_OPCODE_JUMP_ZERO, stub_operand);

            // True block
            aug_ir_push_scope(ir);
            aug_ast_to_ir(vm, children[1], ir);
            aug_ir_pop_scope(ir);

            const size_t end_block_addr = ir.bytecode_offset;

            // Fixup stubbed block offsets
            aug_ir_operation& end_block_jmp_operation = aug_ir_get_operation(ir, end_block_jmp);
            end_block_jmp_operation.operand = aug_ir_operand_from_int(end_block_addr);

            break;
        }
        case AUG_AST_STMT_IF_ELSE:
        {
            assert(children.size() == 3); //if ([0]) {[1]} else {[2]}

            const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);

            // Evaluate expression. 
            aug_ast_to_ir(vm, children[0], ir);

            //Jump to else if false
            const size_t else_block_jmp = aug_ir_add_operation(ir, AUG_OPCODE_JUMP_ZERO, stub_operand);

            // True block
            aug_ir_push_scope(ir);
            aug_ast_to_ir(vm, children[1], ir);
            aug_ir_pop_scope(ir);

            //Jump to end after true
            const size_t end_block_jmp = aug_ir_add_operation(ir, AUG_OPCODE_JUMP, stub_operand);
            const size_t else_block_addr = ir.bytecode_offset;

            // Else block
            aug_ir_push_scope(ir);
            aug_ast_to_ir(vm, children[2], ir);
            aug_ir_pop_scope(ir);

            // Tag end address
            const size_t end_block_addr = ir.bytecode_offset;

            // Fixup stubbed block offsets
            aug_ir_operation& else_block_jmp_operation = aug_ir_get_operation(ir, else_block_jmp);
            else_block_jmp_operation.operand = aug_ir_operand_from_int(else_block_addr);

            aug_ir_operation& end_block_jmp_operation = aug_ir_get_operation(ir, end_block_jmp);
            end_block_jmp_operation.operand = aug_ir_operand_from_int(end_block_addr);

            break;
        }
        case AUG_AST_STMT_WHILE:
        {
            assert(children.size() == 2); //while ([0]) {[1]}

            const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);

            const aug_ir_operand& begin_block_operand = aug_ir_operand_from_int(ir.bytecode_offset);

            // Evaluate expression. 
            aug_ast_to_ir(vm, children[0], ir);

            //Jump to end if false
            const size_t end_block_jmp = aug_ir_add_operation(ir, AUG_OPCODE_JUMP_ZERO, stub_operand);

            // Loop block
            aug_ir_push_scope(ir);
            aug_ast_to_ir(vm, children[1], ir);
            aug_ir_pop_scope(ir);

            // Jump back to beginning, expr evaluation 
            aug_ir_add_operation(ir, AUG_OPCODE_JUMP, begin_block_operand);

            // Tag end address
            const size_t end_block_addr = ir.bytecode_offset;

            // Fixup stubbed block offsets
            aug_ir_operation& end_block_jmp_operation = aug_ir_get_operation(ir, end_block_jmp);
            end_block_jmp_operation.operand = aug_ir_operand_from_int(end_block_addr);

            break;
        }
        case AUG_AST_FUNC_CALL:
        {
            const char* func_name = token.data.c_str();
            const int arg_count = children.size();
            const aug_symbol& symbol = aug_ir_get_symbol(ir, func_name);

            if (symbol.type == AUG_SYM_VAR)
            {
                ir.valid = false;
                AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Can not call variable %s as a function", func_name);
                break;
            }

            // If the symbol is a user defined function.
            if (symbol.type == AUG_SYM_FUNC)
            {
                if (symbol.argc != arg_count)
                {
                    ir.valid = false;
                    AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Function Call %s passed %d arguments, expected %d", func_name, arg_count, symbol.argc);
                }
                else
                {
                    // offset to account for the pushed base
                    aug_ir_add_operation(ir, AUG_OPCODE_PUSH_BASE);
                    size_t push_return_addr = aug_ir_add_operation(ir, AUG_OPCODE_PUSH_INT, aug_ir_operand_from_int(0));

                    for (const aug_ast* arg : children)
                        aug_ast_to_ir(vm, arg, ir);

                    const aug_ir_operand& func_addr = aug_ir_operand_from_int(symbol.offset); // func addr
                    aug_ir_add_operation(ir, AUG_OPCODE_CALL, func_addr);

                    // fixup the return address to after the call
                    aug_ir_get_operation(ir, push_return_addr).operand = aug_ir_operand_from_int(ir.bytecode_offset);
                }
                break;
            }

            // Check if the symbol is a registered function
            int func_index = -1;
            for (int i = 0; i < (int)vm.external_function_names.size(); ++i)
            {
                if (vm.external_function_names[i] == func_name)
                {
                    func_index = i;
                    break;
                }
            }

            if (func_index == -1)
            {
                ir.valid = false;
                AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Function %s not defined", func_name);
                break;
            }
            if (vm.external_functions[func_index] == nullptr)
            {
                ir.valid = false;
                AUG_INPUT_ERROR_AT(ir.input, &token.pos, "External Function %s was not properly registered.", func_name);
                break;
            }

            for (int i = arg_count - 1; i >= 0; --i)
                aug_ast_to_ir(vm, children[i], ir);

            const aug_ir_operand& func_index_operand = aug_ir_operand_from_int(func_index);
            aug_ir_add_operation(ir, AUG_OPCODE_PUSH_INT, func_index_operand);

            const aug_ir_operand& arg_count_operand = aug_ir_operand_from_int(arg_count);
            aug_ir_add_operation(ir, AUG_OPCODE_CALL_EXT, arg_count_operand);
            break;
        }
        case AUG_AST_RETURN:
        {
            const aug_ir_operand& offset = aug_ir_operand_from_int(aug_ir_calling_offset(ir));
            if (children.size() == 1) //return [0];
            {
                aug_ast_to_ir(vm, children[0], ir);
                aug_ir_add_operation(ir, AUG_OPCODE_RETURN, offset);
            }
            else
            {
                aug_ir_add_operation(ir, AUG_OPCODE_PUSH_NONE);
                aug_ir_add_operation(ir, AUG_OPCODE_RETURN, offset);
            }
            break;
        }
        case AUG_AST_PARAM:
        {
            aug_ir_set_param(ir, token.data);
            break;
        }
        case AUG_AST_PARAM_LIST:
        {
            // start stack offset at beginning of parameter (this will be nagative relative to the current frame)
            for (const aug_ast* param : children)
                aug_ast_to_ir(vm, param, ir);
            break;
        }
        case AUG_AST_FUNC_DEF:
        {
            assert(children.size() == 2); //func token [0] {[1]};

            // Jump over the func def
            const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);
            const size_t end_block_jmp = aug_ir_add_operation(ir, AUG_OPCODE_JUMP, stub_operand);

            const char* func_name = token.data.c_str();

            aug_ast* params = children[0];
            assert(params && params->id == AUG_AST_PARAM_LIST);
            const int param_count = params->children.size();

            if (!aug_ir_set_func(ir, func_name, param_count))
            {
                ir.valid = false;
                AUG_INPUT_ERROR_AT(ir.input, &token.pos, "Function %s already defined", func_name);
                break;
            }

            // Parameter frame
            aug_ir_push_scope(ir);
            aug_ast_to_ir(vm, children[0], ir);

            // Function block frame
            aug_ir_push_frame(ir, param_count);
            aug_ast_to_ir(vm, children[1], ir);

            // Ensure there is a return
            if (ir.operations.at(ir.operations.size() - 1).opcode != AUG_OPCODE_RETURN)
            {
                const aug_ir_operand& offset = aug_ir_operand_from_int(aug_ir_calling_offset(ir));
                aug_ir_add_operation(ir, AUG_OPCODE_PUSH_NONE);
                aug_ir_add_operation(ir, AUG_OPCODE_RETURN, offset);
            }

            aug_ir_pop_frame(ir);
            aug_ir_pop_scope(ir);

            const size_t end_block_addr = ir.bytecode_offset;

            // fixup jump operand
            aug_ir_operation& end_block_jmp_operation = aug_ir_get_operation(ir, end_block_jmp);
            end_block_jmp_operation.operand = aug_ir_operand_from_int(end_block_addr);

            break;
        }
        default:
            assert(0);
            break;
    }
}

void aug_ir_to_bytecode(aug_ir& ir, aug_std_array<char>& bytecode)
{  
    bytecode.resize(ir.bytecode_offset, AUG_OPCODE_NO_OP) ;

    char* instruction = &bytecode[0];

    for(const aug_ir_operation& operation : ir.operations)
    {
        *instruction = (char)operation.opcode;
        ++instruction;    

        const aug_ir_operand& operand = operation.operand;
        switch (operand.type)
        {
        case AUG_IR_OPERAND_NONE:
            break;
        case AUG_IR_OPERAND_BOOL:
        case AUG_IR_OPERAND_INT:
        case AUG_IR_OPERAND_FLOAT:
            for (size_t i = 0; i < aug_ir_operand_size(operand); ++i)
                *(instruction++) = operand.data.bytes[i];
            break;
        case AUG_IR_OPERAND_BYTES:
            for (size_t i = 0; i < strlen(operand.data.str); ++i)
                *(instruction++) = operand.data.str[i];

            *(instruction++) = 0; // null terminate
            break;
        }
    }
}

// -------------------------------------- API ---------------------------------------------// 

bool aug_compile_script(aug_vm& vm, aug_input* input, aug_ast* root, aug_script& script)
{
    if (root == NULL)
        return false;

    script.bytecode.clear();
    script.globals.clear();

    // Generate IR
    aug_ir ir;
    aug_ir_init(ir, input);
    aug_ast_to_ir(vm, root, ir);

    if (!ir.valid)
        return false;

    // Load script globals
    script.globals = ir.globals;

    // Load bytecode into VM
    aug_ir_to_bytecode(ir, script.bytecode);
    return true;
}

void aug_startup(aug_vm& vm, aug_error_callback* error_callback)
{
    vm.error_callback = NULL;
    vm.external_functions.clear();
    vm.external_function_names.clear();
    vm.stack = AUG_NEW_ARRAY(aug_value, AUG_STACK_SIZE);
    for(int i = 0; i < AUG_STACK_SIZE; ++i)
        vm.stack[i] = aug_none();

    // Initialize
    vm.error_callback = error_callback;
    aug_vm_startup(vm);
}

void aug_shutdown(aug_vm& vm)
{
    aug_vm_shutdown(vm);
    AUG_DELETE_ARRAY(vm.stack);
    vm.stack = nullptr;

    vm.error_callback = NULL;
    vm.external_functions.clear();
    vm.external_function_names.clear();
}

void aug_register(aug_vm& vm, const char* func_name, aug_function_callback *callback)
{
    for (int i = 0; i < (int)vm.external_functions.size(); ++i)
    {
        if (vm.external_functions[i] == NULL)
        {
            vm.external_functions[i] = callback;
            vm.external_function_names[i] = func_name;
            return;
        }
    }
    vm.external_functions.push_back(callback);
    vm.external_function_names.push_back(func_name);
}

void aug_unregister(aug_vm& vm, const char* func_name)
{
    for (int i = 0; i < (int)vm.external_functions.size(); ++i)
    {
        if (vm.external_function_names[i] == func_name)
        {
            vm.external_functions[i] = NULL;
            break;
        }
    }
}

void aug_execute(aug_vm& vm, const char* filename)
{
    aug_script script;
    bool success = aug_compile(vm, script, filename);
    if (!success)
        return;

    aug_vm_startup(vm);
    aug_vm_boot(vm, script);
    aug_vm_execute(vm);
    aug_vm_shutdown(vm);
}

void aug_evaluate(aug_vm& vm, const char* code)
{
    aug_input* input = aug_input_open(code, vm.error_callback, false);
    if (input == NULL)
        return;

    // Parse file
    aug_script script;
    aug_ast* root = aug_parse(vm, input);
    script.valid = aug_compile_script(vm, input, root, script);

    // Cleanup 
    aug_ast_delete(root);
    aug_input_close(input);

    if (!script.valid)
        return;

    aug_vm_startup(vm);
    aug_vm_boot(vm, script);
    aug_vm_execute(vm);
    aug_vm_shutdown(vm);
}

bool aug_compile(aug_vm& vm, aug_script& script, const char* filename)
{
    script.valid = false;

    aug_input* input = aug_input_open(filename, vm.error_callback, true);
    if (input == NULL)
        return NULL;

    // Parse file
    aug_ast* root = aug_parse(vm, input);
    script.valid = aug_compile_script(vm, input, root, script);
    
    // Cleanup 
    aug_ast_delete(root);
    aug_input_close(input);

    return script.valid;
}

void aug_boot(aug_vm& vm, aug_script& script)
{
    aug_vm_boot(vm, script);
    aug_vm_execute(vm);
}

aug_value aug_call(aug_vm& vm, aug_script& script, const char* func_name, const aug_std_array<aug_value>& args)
{
    aug_value ret_value = aug_none();
    if (!script.valid)
        return ret_value;

    if (script.globals.count(func_name) == 0)
    {
        AUG_LOG_ERROR(vm.error_callback, "Function %s not defined", func_name);
        return ret_value;
    }

    const aug_symbol& symbol = script.globals[func_name];
    switch (symbol.type)
    {
        case AUG_SYM_FUNC:
            break;
        case AUG_SYM_VAR:
        {
            AUG_LOG_ERROR(vm.error_callback, "Can not call variable %s a function", func_name);
            return ret_value;
        }
        default:
        {
            AUG_LOG_ERROR(vm.error_callback, "Symbol %s not defined as a function", func_name);
            return ret_value;
        }
    }
    if (symbol.argc != (int)args.size())
    {
        AUG_LOG_ERROR(vm.error_callback, "Function %s passed %d arguments, expected %d", func_name, (int)args.size(), symbol.argc);
        return ret_value;
    }

    aug_vm_boot(vm, script);

    // Manually set expected call frame
    // push base addr
    aug_value* base = aug_vm_push(vm);
    base->type = AUG_INT;
    base->i = vm.base_index;

    // push return address
    aug_value* return_address = aug_vm_push(vm);
    return_address->type = AUG_INT;
    return_address->i = AUG_OPCODE_INVALID;

    // Jump to function call
    vm.instruction = vm.bytecode + symbol.offset;

    const size_t arg_count = args.size();
    for (size_t i = 0; i < arg_count; ++i)
    {
        aug_value* value = aug_vm_push(vm);
        if (value)
            *value = args[i];
    }

    // Setup base index to be current stack index
    vm.base_index = vm.stack_index;

    aug_vm_execute(vm);

    // If stack is valid
    if (vm.stack_index > 0)
    {
        aug_value* top = aug_vm_pop(vm);
        if (top)
            ret_value = *top;
    }

    return ret_value;
}

aug_value aug_call(aug_vm& vm, aug_script& script, const char* func_name)
{
    const aug_std_array<aug_value> args;
    return aug_call(vm, script, func_name, args);
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

#endif