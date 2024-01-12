#ifndef __CURB_HEADER__
#define __CURB_HEADER__
/*
    Curb Programming Language
        Single File parser, bytecode compiler, and virtual machine for the Curb programming language
    Author: https://github.com/138paulmiller

    Use 
        `#define CURB_IMPLEMENTATION`
        `#include "curb.h"`

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
    - VM - Serialize Bytecode to external file. Execute compiled bytecode from file
    - VM - Serialize debug map to file. Link from bytecode to source file. 
    - VM - Print Stack trace on error. Link back to source file if running uncompiled bytecode
    - Native - Support return value in function callbacks
    - Semantic Pass - resolve IR pass any potential naming, field,  or return issues     
    - Convert to C
    Bugs:
    - VM - Semantic pass to determine if a function call needs to discard the return value (i.e. return not push value on stack)
*/

#include <functional>
#include <list>
#include <string>
#include <vector>
#include <unordered_map>

struct curb_value;
struct curb_object;

using curb_string = std::string;

template <class type>
using curb_array = std::vector<type>;

template <class key, class type>
using curb_map = std::unordered_map<key, type>;

typedef void(curb_error_callback)(const char* /*msg*/);
typedef void(curb_function_callback)(curb_value* /*return_value*/, const curb_array<curb_value*>& /*args*/);

enum curb_type
{
    CURB_BOOL,
    CURB_INT, 
    CURB_FLOAT, 
    CURB_STRING, 
    CURB_OBJECT,
    CURB_NONE
};

struct curb_value
{
    curb_type type;
    union 
    {
        bool b;
        int i; 
        float f;
        const char* str;
        curb_object* obj;
    };
};

struct curb_attribute
{
    curb_string id;
    curb_value value;
};

struct curb_object
{
    curb_array<curb_attribute> attribs;
};

enum curb_symbol_type
{
    CURB_IR_SYM_NONE,
    CURB_IR_SYM_VAR,
    CURB_IR_SYM_FUNC,
};

struct curb_symbol
{
    curb_symbol_type type;
    // Functions - this is the bytecode address.
    // Variables - this is the local frames stack offset.
    int offset;
};

struct curb_symtable
{
    curb_map<curb_string, curb_symbol> symbols;
};

struct curb_script
{
    bool valid;
    curb_symtable global_symtable;
    curb_array<char> bytecode;
};

struct curb_environment
{
    curb_map<curb_string, curb_function_callback*> external_functions;
    curb_error_callback* error_callback = nullptr;
};

void curb_register(curb_environment& env, const char* func_name, curb_function_callback* callback);
void curb_unregister(curb_environment& env, const char* func_name);

void curb_execute(curb_environment& env, const char* filename);
void curb_evaluate(curb_environment& env, const char* code);

bool curb_compile(curb_environment& env, curb_script& script, const char* filename);
curb_value curb_call(curb_environment& env, curb_script& script, const char* func_name, const curb_array<curb_value>& args);

curb_string curb_to_string(const curb_value* value);
bool curb_to_bool(const curb_value* value);

curb_value curb_bool(bool data);
curb_value curb_int(int data);
curb_value curb_float(float data);

#endif //__CURB_HEADER__

// -------------------------------------- Implementation Details ---------------------------------------// 

#if defined(CURB_IMPLEMENTATION)

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>

#define CURB_DEBUG 1

#ifndef CURB_VM_APPROX_THRESHOLD
    #define CURB_VM_APPROX_THRESHOLD 0.0000001
#endif//CURB_VM_APPROX_THRESHOLD 

#ifndef CURB_VM_OPERAND_LEN
    #define CURB_VM_OPERAND_LEN 8
#endif//CURB_TOKEN_BUFFER_LEN 

#ifndef CURB_TOKEN_BUFFER_LEN
    #define CURB_TOKEN_BUFFER_LEN 32
#endif//CURB_TOKEN_BUFFER_LEN 

#ifndef CURB_BYTECODE_CHUNK_SIZE
    #define CURB_BYTECODE_CHUNK_SIZE (1024 * 2)
#endif//CURB_BYTECODE_CHUNK_SIZE

#ifndef CURB_STACK_SIZE
    #define CURB_STACK_SIZE (4096 * 4)
#endif//CURB_STACK_SIZE

#ifndef CURB_COMMENT_SYMBOL
    #define CURB_COMMENT_SYMBOL '#'
#endif//CURB_COMMENT_SYMBOL 

#ifndef CURB_LOG_PRELUDE
    #define CURB_LOG_PRELUDE "[CURB]"
#endif//CURB_LOG_PRELUDE 

#ifdef CURB_LOG_VERBOSE
    #ifdef WIN32
        #define CURB_LOG_TRACE() fprintf(stderr, "[%s:%d]", __FUNCTION__, __LINE__);
    #else
        #define CURB_LOG_TRACE() fprintf(stderr, "[%s:%d]", __func__, __LINE__);
    #endif  
#else 
    #define CURB_LOG_TRACE()   
#endif

#define CURB_LOG_ERROR(env, ...)                        \
{                                                      \
    CURB_LOG_TRACE()                                    \
    fprintf(stderr, CURB_LOG_PRELUDE "Error: ");        \
    fprintf(stderr, __VA_ARGS__);                      \
    fprintf(stderr, "\n");                             \
                                                                            \
    char buffer[4096];                                                      \
    int len = snprintf(buffer, sizeof(buffer), CURB_LOG_PRELUDE "Error: ");  \
    len = snprintf(buffer + len, sizeof(buffer) - len, __VA_ARGS__);        \
    if(env.error_callback) env.error_callback(buffer);                      \
}

// -------------------------------------- Lexer  ---------------------------------------// 
// Static token details
struct curb_token_detail
{
    const char* label;    // the string representation, used for visualization and debugging
    char prec;            // if the token is an operator, this is the precedence (note: higher values take precendece)
    int  argc;            // if the token is an operator, this is the number of operands
    bool capture;         // if non-zero, the token will contain the source string value (i.e. integer and string literals)
    const char* keyword;  // if non-null, the token must match the provided keyword
};

#define CURB_TOKEN_LIST                                \
    /* State */                                        \
    CURB_TOKEN(NONE,           0, 0, 0, nullptr)       \
    CURB_TOKEN(ERR,	          0, 0, 0, nullptr)        \
    CURB_TOKEN(END,            0, 0, 0, nullptr)       \
    /* Symbols */			                           \
    CURB_TOKEN(DOT,            0, 0, 0, nullptr)       \
    CURB_TOKEN(COMMA,          0, 0, 0, nullptr)       \
    CURB_TOKEN(COLON,          0, 0, 0, nullptr)       \
    CURB_TOKEN(SEMICOLON,      0, 0, 0, nullptr)       \
    CURB_TOKEN(LPAREN,         0, 0, 0, nullptr)       \
    CURB_TOKEN(RPAREN,         0, 0, 0, nullptr)       \
    CURB_TOKEN(LBRACKET,       0, 0, 0, nullptr)       \
    CURB_TOKEN(RBRACKET,       0, 0, 0, nullptr)       \
    CURB_TOKEN(LBRACE,         0, 0, 0, nullptr)       \
    CURB_TOKEN(RBRACE,         0, 0, 0, nullptr)       \
    /* Operators - Arithmetic */                       \
    CURB_TOKEN(AND,            1, 2, 0, nullptr)       \
    CURB_TOKEN(OR,             1, 2, 0, nullptr)       \
    CURB_TOKEN(ADD,            2, 2, 0, nullptr)       \
    CURB_TOKEN(ADD_EQ,         1, 2, 0, nullptr)       \
    CURB_TOKEN(SUB,            2, 2, 0, nullptr)       \
    CURB_TOKEN(SUB_EQ,         1, 2, 0, nullptr)       \
    CURB_TOKEN(MUL,            3, 2, 0, nullptr)       \
    CURB_TOKEN(MUL_EQ,         1, 2, 0, nullptr)       \
    CURB_TOKEN(DIV,            3, 2, 0, nullptr)       \
    CURB_TOKEN(DIV_EQ,         1, 2, 0, nullptr)       \
    CURB_TOKEN(POW,            3, 2, 0, nullptr)       \
    CURB_TOKEN(POW_EQ,         1, 2, 0, nullptr)       \
    CURB_TOKEN(MOD,            3, 2, 0, nullptr)       \
    CURB_TOKEN(MOD_EQ,         1, 2, 0, nullptr)       \
    /* Operators - Boolean */                          \
    CURB_TOKEN(EQ,             1, 2, 0, nullptr)       \
    CURB_TOKEN(LT,             2, 2, 0, nullptr)       \
    CURB_TOKEN(GT,             2, 2, 0, nullptr)       \
    CURB_TOKEN(LT_EQ,          2, 2, 0, nullptr)       \
    CURB_TOKEN(GT_EQ,          2, 2, 0, nullptr)       \
    CURB_TOKEN(NOT,            3, 1, 0, nullptr)       \
    CURB_TOKEN(NOT_EQ,         2, 2, 0, nullptr)       \
    CURB_TOKEN(APPROX_EQ,      1, 2, 0, nullptr)       \
    /* Literals */                                     \
    CURB_TOKEN(DECIMAL,        0, 0, 1, nullptr)       \
    CURB_TOKEN(HEXIDECIMAL,    0, 0, 1, nullptr)       \
    CURB_TOKEN(BINARY,         0, 0, 1, nullptr)       \
    CURB_TOKEN(FLOAT,          0, 0, 1, nullptr)       \
    CURB_TOKEN(STRING,         0, 0, 1, nullptr)       \
    /* Misc */                                         \
    CURB_TOKEN(NAME,           0, 0, 1, nullptr)       \
    CURB_TOKEN(ASSIGN,         0, 0, 0, nullptr)       \
    /* Keywords */                                     \
    CURB_TOKEN(IF,             0, 0, 0, "if")          \
    CURB_TOKEN(ELSE,           0, 0, 0, "else")        \
    CURB_TOKEN(IN,             0, 0, 0, "in")          \
    CURB_TOKEN(FOR,            0, 0, 0, "for")         \
    CURB_TOKEN(WHILE,          0, 0, 0, "while")       \
    CURB_TOKEN(VAR,            0, 0, 0, "var")         \
    CURB_TOKEN(FUNC,           0, 0, 0, "func")        \
    CURB_TOKEN(RETURN,         0, 0, 0, "return")      \
    CURB_TOKEN(TRUE,           0, 0, 0, "true")        \
    CURB_TOKEN(FALSE,          0, 0, 0, "false")

// Token identifier. 
enum curb_token_id : uint8_t 
{
#define CURB_TOKEN(id, ...) CURB_TOKEN_##id,
    CURB_TOKEN_LIST
#undef CURB_TOKEN
    CURB_TOKEN_COUNT
};

// All token type info. Types map from id to type info
static curb_token_detail curb_token_details[(int)CURB_TOKEN_COUNT] = 
{
#define CURB_TOKEN(id, ...) { #id, __VA_ARGS__},
    CURB_TOKEN_LIST
#undef CURB_TOKEN
};

#undef CURB_TOKEN_LIST

// Token instance
struct curb_token
{
    curb_token_id id;
    const curb_token_detail* detail; 
    curb_string data;
    int line;
    int col;
};

// Lexer state
struct curb_lexer
{
    curb_token prev;
    curb_token curr;
    curb_token next;

    std::istream* input = nullptr;
    curb_string inputname;

    int line, col;
    int prev_line, prev_col;
    std::streampos pos_start;

    char token_buffer[CURB_TOKEN_BUFFER_LEN];
};

#define CURB_LEXER_ERROR(env, lexer, token, ...)              \
{                                                            \
    token.id = CURB_TOKEN_ERR;                                \
    CURB_LOG_ERROR(env, "%s(%d,%d):",                         \
        lexer.inputname.c_str(), lexer.line+1, lexer.col+1); \
    CURB_LOG_ERROR(env, __VA_ARGS__);                         \
}

char curb_lexer_get(curb_lexer& lexer)
{
    const char c = lexer.input->get();
    
    lexer.prev_line = lexer.line;
    lexer.prev_col = lexer.col;

    ++lexer.col;

    if (c == '\n')
    {
        ++lexer.line;
        lexer.col = 0;
    }
    return c;
}

char curb_lexer_peek(curb_lexer& lexer)
{
    return lexer.input->peek();
}

char curb_lexer_unget(curb_lexer& lexer)
{
    lexer.input->unget();

    lexer.line = lexer.prev_line;
    lexer.col = lexer.prev_col;

    return curb_lexer_peek(lexer);
}

void curb_lexer_start_tracking(curb_lexer& lexer)
{
    lexer.pos_start = lexer.input->tellg();
}

void curb_lexer_end_tracking(curb_lexer& lexer, curb_string& s)
{
    const std::fstream::iostate state = lexer.input->rdstate();
    lexer.input->clear();

    const std::streampos pos_end = lexer.input->tellg();
    const std::streamoff len = (pos_end - lexer.pos_start);
    
    lexer.token_buffer[0] = '\0';
    lexer.input->seekg(lexer.pos_start);
    lexer.input->read(lexer.token_buffer, len);
    lexer.input->seekg(pos_end);
    lexer.input->clear(state);

    s.assign(lexer.token_buffer, (size_t)len);
}

bool curb_lexer_tokenize_string(curb_environment& env, curb_lexer& lexer, curb_token& token)
{
    char c = curb_lexer_get(lexer);
    if (c != '\"')
    {
        curb_lexer_unget(lexer);
        return false;
    }

    token.id = CURB_TOKEN_STRING;
    token.data.clear();

    c = curb_lexer_get(lexer);

    while (c != '\"')
    {
        if (c == EOF)
        {
            CURB_LEXER_ERROR(env, lexer, token, "string literal missing closing \"");
            return false;
        }

        if (c == '\\')
        {
            // handle escaped chars
            c = curb_lexer_get(lexer);
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
                CURB_LEXER_ERROR(env, lexer, token, "invalid escape character \\%c", c);
                while (c != '\"')
                {
                    if (c == EOF)
                        break;
                    c = curb_lexer_get(lexer);
                }
                return false;
            }
        }
        else
        {
            token.data.push_back(c);
        }

        c = curb_lexer_get(lexer);
    }

    return true;
}

bool curb_lexer_tokenize_symbol(curb_environment& env, curb_lexer& lexer, curb_token& token)
{
    (void)env;

    curb_token_id id = CURB_TOKEN_ERR;

    char c = curb_lexer_get(lexer);
    switch (c)
    {
    case '.': id = CURB_TOKEN_DOT;       break;
    case ',': id = CURB_TOKEN_COMMA;     break;
    case ':': id = CURB_TOKEN_COLON;     break;
    case ';': id = CURB_TOKEN_SEMICOLON; break;
    case '(': id = CURB_TOKEN_LPAREN;    break;
    case ')': id = CURB_TOKEN_RPAREN;    break;
    case '[': id = CURB_TOKEN_LBRACKET;  break;
    case ']': id = CURB_TOKEN_RBRACKET;  break;
    case '{': id = CURB_TOKEN_LBRACE;    break;
    case '}': id = CURB_TOKEN_RBRACE;    break;
    case '&': id = CURB_TOKEN_AND;       break;
    case '|': id = CURB_TOKEN_OR;        break;
    case '+':
        if (curb_lexer_peek(lexer) == '=' && curb_lexer_get(lexer))
            id = CURB_TOKEN_ADD_EQ;
        else
            id = CURB_TOKEN_ADD;
        break;
    case '-':
        if (curb_lexer_peek(lexer) == '=' && curb_lexer_get(lexer))
            id = CURB_TOKEN_SUB_EQ;
        else
            id = CURB_TOKEN_SUB;
        break;
    case '*':
        if (curb_lexer_peek(lexer) == '=' && curb_lexer_get(lexer))
            id = CURB_TOKEN_MUL_EQ;
        else
            id = CURB_TOKEN_MUL;
        break;
    case '/':
        if (curb_lexer_peek(lexer) == '=' && curb_lexer_get(lexer))
            id = CURB_TOKEN_DIV_EQ;
        else
            id = CURB_TOKEN_DIV;
        break;
    case '^':
        if (curb_lexer_peek(lexer) == '=' && curb_lexer_get(lexer))
            id = CURB_TOKEN_POW_EQ;
        else
            id = CURB_TOKEN_POW;
        break;
    case '%':
        if (curb_lexer_peek(lexer) == '=' && curb_lexer_get(lexer))
            id = CURB_TOKEN_MOD_EQ;
        else
            id = CURB_TOKEN_MOD;
        break;
    case '<':
        if (curb_lexer_peek(lexer) == '=' && curb_lexer_get(lexer))
            id = CURB_TOKEN_LT_EQ;
        else
            id = CURB_TOKEN_LT;
        break;
    case '>':
        if (curb_lexer_peek(lexer) == '=' && curb_lexer_get(lexer))
            id = CURB_TOKEN_GT_EQ;
        else
            id = CURB_TOKEN_GT;
        break;
    case '=':
        if (curb_lexer_peek(lexer) == '=' && curb_lexer_get(lexer))
            id = CURB_TOKEN_EQ;
        else
            id = CURB_TOKEN_ASSIGN;
        break;
    case '!':
        if (curb_lexer_peek(lexer) == '=' && curb_lexer_get(lexer))
            id = CURB_TOKEN_NOT_EQ;
        else
            id = CURB_TOKEN_NOT;
        break;
    case '~':
        if (curb_lexer_peek(lexer) == '=' && curb_lexer_get(lexer))
            id = CURB_TOKEN_APPROX_EQ;
        break;
    }

    if (id == CURB_TOKEN_ERR)
    {
        curb_lexer_unget(lexer);
        return false;
    }

    token.id = id;
    return true;
}

bool curb_lexer_tokenize_name(curb_environment& env, curb_lexer& lexer, curb_token& token)
{
    (void)env;

    curb_lexer_start_tracking(lexer);

    char c = curb_lexer_get(lexer);
    if (c != '_' && !isalpha(c))
    {
        curb_lexer_unget(lexer);
        return false;
    }
    
    while (c == '_' || isalnum(c))
        c = curb_lexer_get(lexer);
    curb_lexer_unget(lexer);

    curb_string name;
    curb_lexer_end_tracking(lexer, name);

    // find token id for keyword
    curb_token_id id = CURB_TOKEN_NAME;
    for (size_t i = 0; i < (size_t)CURB_TOKEN_COUNT; ++i)
    {
        const curb_token_detail& detail = curb_token_details[i];
        if (detail.keyword && detail.keyword == name)
        {
            id = (curb_token_id)i;
            break;
        }
    }
    
    token.id = id;
    token.data = std::move(name);

    return true;
}

bool curb_lexer_tokenize_number(curb_environment& env, curb_lexer& lexer, curb_token& token)
{    
    curb_lexer_start_tracking(lexer);

    char c = curb_lexer_get(lexer);
    if (c != '.' && !isdigit(c) && c != '+' && c != '-')
    {
        curb_lexer_unget(lexer);
        return false;
    } 

    curb_token_id id = CURB_TOKEN_ERR;

    if (c == '0' && curb_lexer_peek(lexer) == 'x')
    {
        id = CURB_TOKEN_HEXIDECIMAL;

        c = curb_lexer_get(lexer); //eat 'x'
        c = curb_lexer_get(lexer);

        while (isalnum(c))
        {
            if (!isdigit(c) && !((c >='a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                id = CURB_TOKEN_ERR;

            c = curb_lexer_get(lexer);
        }
        curb_lexer_unget(lexer);
    }
    else if (c == '0' && curb_lexer_peek(lexer) == 'b')
    {
        id = CURB_TOKEN_BINARY;

        c = curb_lexer_get(lexer); // eat 'b'
        c = curb_lexer_get(lexer);

        while (isdigit(c))
        {
            if(c != '0' && c != '1')
                id = CURB_TOKEN_ERR;

            c = curb_lexer_get(lexer);
        }
        curb_lexer_unget(lexer);
    }
    else
    {
        if (c == '+' || c == '-')
        {
            c = curb_lexer_peek(lexer);
            if (c != '.' && !isdigit(c))
            {
                curb_lexer_unget(lexer);
                return false;
            }
            c = curb_lexer_get(lexer);
        }

        if (c == '.')
            id = CURB_TOKEN_FLOAT;
        else 
            id = CURB_TOKEN_DECIMAL;

        bool dot = false;
        while (c == '.' || isdigit(c))
        {
            if (c == '.')
            {
                if (dot)
                    id = CURB_TOKEN_ERR;
                else
                    id = CURB_TOKEN_FLOAT;

                dot = true;
            }
            c = curb_lexer_get(lexer);
        }
        curb_lexer_unget(lexer);
    }

    curb_string str;
    curb_lexer_end_tracking(lexer, str);

    if(id == CURB_TOKEN_ERR)
    {
        CURB_LEXER_ERROR(env, lexer, token, "invalid numeric format %s", str.c_str());
        return false;
    }
    
    token.id = id;
    token.data = std::move(str);
    return true;
}

curb_token curb_lexer_tokenize(curb_environment& env, curb_lexer& lexer)
{
    curb_token token;

    // if file is not open, or already at then end. return invalid token
    if (lexer.input == nullptr)
    {
        token.id = CURB_TOKEN_NONE;
        token.detail = &curb_token_details[(int)token.id];
        return token;
    }

    if (curb_lexer_peek(lexer) == EOF)
    {
        token.id = CURB_TOKEN_END;
        token.detail = &curb_token_details[(int)token.id];
        return token;
    }

    char c = curb_lexer_peek(lexer);

    // skip whitespace
    if (isspace(c))
    {
        while (isspace(c))
            c = curb_lexer_get(lexer);
        curb_lexer_unget(lexer);
    }

    // skip comments
    while(c == CURB_COMMENT_SYMBOL)
    {        
        while (c != EOF)
        {
            c = curb_lexer_get(lexer);

            if (c == '\n')
            {
                c = curb_lexer_peek(lexer);
                break;
            }
        }

        // skip whitespace
        if (isspace(c))
        {
            while (isspace(c))
                c = curb_lexer_get(lexer);
            curb_lexer_unget(lexer);
        }
    }

    // handle eof
    if (c == EOF)
    {
        token.id = CURB_TOKEN_END;
        token.detail = &curb_token_details[(int)token.id];
        return token;
    }

    token.col = lexer.col;
    token.line = lexer.line;

    switch (c)
    {
    case '.': 
    case '+':
    case '-':
    {
        // To prevent contention with sign as an operator, if the proceeding token is a number
        // treat sign as an operator, else, treat as the number's sign
        bool allow_sign = true;
        switch(lexer.curr.id)
        {
            case CURB_TOKEN_NAME:
            case CURB_TOKEN_BINARY:
            case CURB_TOKEN_HEXIDECIMAL:
            case CURB_TOKEN_FLOAT:
            case CURB_TOKEN_DECIMAL:
                allow_sign = false;
                break;
            default:
                break;
        }
        if (!allow_sign || !curb_lexer_tokenize_number(env, lexer, token))
            curb_lexer_tokenize_symbol(env, lexer, token);
        break;
    }
    case '\"':
        curb_lexer_tokenize_string(env, lexer, token);
        break;
    default:
        if (curb_lexer_tokenize_symbol(env, lexer, token))
            break;
        if(curb_lexer_tokenize_name(env, lexer, token))
            break;
        if (curb_lexer_tokenize_number(env, lexer, token))
            break;

        CURB_LEXER_ERROR(env, lexer, token, "invalid character %c", c);
        break;
    }

    token.detail = &curb_token_details[(int)token.id];
    return token;
}

bool curb_lexer_move(curb_environment& env, curb_lexer& lexer)
{
    if (lexer.next.id == CURB_TOKEN_NONE)
        lexer.next = curb_lexer_tokenize(env, lexer);        

    lexer.prev = lexer.curr; 
    lexer.curr = lexer.next;
    lexer.next = curb_lexer_tokenize(env, lexer);

    return lexer.curr.id != CURB_TOKEN_NONE;
}

void curb_lexer_close(curb_lexer& lexer)
{
    if (lexer.input)
        delete lexer.input;

    lexer.input = nullptr;
    lexer.line = 0;
    lexer.col = 0;
    lexer.prev_line = 0;
    lexer.prev_col = 0;
    lexer.pos_start = 0;

    lexer.prev = curb_token();
    lexer.curr = curb_token();
    lexer.next = curb_token();
}

bool curb_lexer_open(curb_environment& env, curb_lexer& lexer, const char* code)
{
    curb_lexer_close(lexer);

    std::istringstream* iss = new std::istringstream(code, std::fstream::in);
    if (iss == nullptr || !iss->good())
    {
        CURB_LOG_ERROR(env,"Lexer failed to open code");
        if(iss) 
            delete iss;
        return false;
    }

    lexer.input = iss;
    lexer.inputname = "code";

    return curb_lexer_move(env, lexer);
}

bool curb_lexer_open_file(curb_environment& env, curb_lexer& lexer, const char* filename)
{
    curb_lexer_close(lexer);

    std::fstream* file = new std::fstream(filename, std::fstream::in);
    if (file == nullptr || !file->is_open())
    {
        CURB_LOG_ERROR(env,"Lexer failed to open file %s", filename);
        if (file)
            delete file;
        return false;
    }

    lexer.input = file;
    lexer.inputname = filename;

    return curb_lexer_move(env, lexer);
}

// -------------------------------------- Parser / Abstract Syntax Tree ---------------------------------------// 
#define CURB_PARSE_ERROR(env, lexer,  ...)\
{\
    CURB_LOG_ERROR(env, "Syntax error %s(%d,%d)", lexer.inputname.c_str(), lexer.line+1, lexer.col+1);\
    CURB_LOG_ERROR(env, __VA_ARGS__);\
}

enum curb_ast_id : uint8_t
{
    CURB_AST_ROOT,
    CURB_AST_BLOCK, 
    CURB_AST_STMT_EXPR,
    CURB_AST_STMT_VAR,
    CURB_AST_STMT_ASSIGN,
    CURB_AST_STMT_IF,
    CURB_AST_STMT_IF_ELSE,
    CURB_AST_STMT_WHILE,
    CURB_AST_LITERAL, 
    CURB_AST_VARIABLE, 
    CURB_AST_UNARY_OP, 
    CURB_AST_BINARY_OP, 
    CURB_AST_FUNC_CALL,
    CURB_AST_FUNC_DEF, 
    CURB_AST_PARAM_LIST,
    CURB_AST_PARAM,
    CURB_AST_RETURN,
};

struct curb_ast
{
    curb_ast_id id;
    curb_token token;
    curb_array<curb_ast*> children;
};

curb_ast* curb_parse_value(curb_environment& env, curb_lexer& lexer); 
curb_ast* curb_parse_block(curb_environment& env, curb_lexer& lexer);

curb_ast* curb_ast_new(curb_ast_id id, const curb_token& token = curb_token())
{
    curb_ast* node = new curb_ast();
    node->id = id;
    node->token = token;
    return node;
}

void curb_ast_delete(curb_ast* node)
{
    if (node == nullptr)
        return;
    for(curb_ast* child : node->children)
        curb_ast_delete(child);
    delete node;
}

bool curb_parse_expr_pop(curb_environment& env, curb_lexer& lexer, curb_array<curb_token>& op_stack, curb_array<curb_ast*>& expr_stack)
{
    curb_token next_op = op_stack.back();
    op_stack.pop_back();

    const int op_argc = (size_t)next_op.detail->argc;
    assert(op_argc == 1 || op_argc == 2); // Only supported operator types

    if(expr_stack.size() < (size_t)op_argc)
    {
        while(expr_stack.size() > 0)
        {
            curb_ast_delete(expr_stack.back());
            expr_stack.pop_back();
        }
        CURB_PARSE_ERROR(env, lexer,  "Invalid number of arguments to operator %s", next_op.detail->label);
        return false;
    }

    // Push binary op onto stack
    curb_ast_id id = (op_argc == 2) ? CURB_AST_BINARY_OP : CURB_AST_UNARY_OP;
    curb_ast* binaryop = curb_ast_new(id, next_op);
    binaryop->children.resize(op_argc);

    for(int i = 0; i < op_argc; ++i)
    {
        curb_ast* expr = expr_stack.back();
        expr_stack.pop_back();
        binaryop->children[(op_argc-1) - i] = expr; // add in reverse
    }

    expr_stack.push_back(binaryop);
    return true;
}

curb_ast* curb_parse_expr(curb_environment& env, curb_lexer& lexer)
{
    // Shunting yard algorithm
    curb_array<curb_token> op_stack;
    curb_array<curb_ast*> expr_stack;
    
    while(lexer.curr.id != CURB_TOKEN_SEMICOLON)
    {
        curb_token op = lexer.curr;
        if(op.detail->prec > 0)
        {
            // left associate by default (for right, <= becomes <)
            while(op_stack.size() && op_stack.back().detail->prec >= op.detail->prec)
            {
                if(!curb_parse_expr_pop(env, lexer,  op_stack, expr_stack))
                    return nullptr;
            }

            op_stack.push_back(op);
            curb_lexer_move(env, lexer);
        }
        else
        {
            curb_ast* value = curb_parse_value(env, lexer);
            if(value == nullptr)
                break;
            expr_stack.push_back(value);
        }
    }

    // Not an expression
    if(op_stack.size() == 0 && expr_stack.size() == 0)
        return nullptr;

    while(op_stack.size())
    {
        if(!curb_parse_expr_pop(env, lexer,  op_stack, expr_stack))
            return nullptr;
    }

    // Not a valid expression. Either malformed or missing semicolon 
    if(expr_stack.size() == 0 || expr_stack.size() > 1)
    {
        while(expr_stack.size() > 0)
        {
            curb_ast_delete(expr_stack.back());
            expr_stack.pop_back();
        }
        CURB_PARSE_ERROR(env, lexer,  "Invalid expression syntax");
        return nullptr;
    }

    return expr_stack.back();
}

curb_ast* curb_parse_funccall(curb_environment& env, curb_lexer& lexer)
{
    curb_token name_token = lexer.curr;
    if (name_token.id != CURB_TOKEN_NAME)
        return nullptr;

    if (lexer.next.id != CURB_TOKEN_LPAREN)
        return nullptr;

    curb_lexer_move(env, lexer); // eat NAME
    curb_lexer_move(env, lexer); // eat LPAREN

    curb_array<curb_ast*> args;
    if (curb_ast* expr = curb_parse_expr(env, lexer))
    {
        args.push_back(expr);

        while (expr && lexer.curr.id == CURB_TOKEN_COMMA)
        {
            curb_lexer_move(env, lexer); // eat COMMA

            if((expr = curb_parse_expr(env, lexer)))
                args.push_back(expr);
        }
    }

    if (lexer.curr.id != CURB_TOKEN_RPAREN)
    {
        CURB_PARSE_ERROR(env, lexer,  "Function call missing closing parentheses");
        for(curb_ast* arg : args)
            delete arg;
        return nullptr;
    }

    curb_lexer_move(env, lexer); // eat RPAREN

    curb_ast* funccall = curb_ast_new(CURB_AST_FUNC_CALL, name_token);
    funccall->children = std::move(args);
    return funccall;
}

curb_ast* curb_parse_value(curb_environment& env, curb_lexer& lexer)
{
    curb_token token = lexer.curr;
    switch (token.id)
    {
    case CURB_TOKEN_NAME:
    {   
        if (curb_ast* funccall = curb_parse_funccall(env, lexer))
            return funccall;

        curb_lexer_move(env, lexer);
        curb_ast* var = curb_ast_new(CURB_AST_VARIABLE, token);
        return var;
    }
    case CURB_TOKEN_DECIMAL:
    case CURB_TOKEN_HEXIDECIMAL:
    case CURB_TOKEN_BINARY:
    case CURB_TOKEN_FLOAT:
    case CURB_TOKEN_STRING:
    case CURB_TOKEN_TRUE:
    case CURB_TOKEN_FALSE:
    {
        curb_lexer_move(env, lexer);
        curb_ast* literal = curb_ast_new(CURB_AST_LITERAL, token);
        literal->id = CURB_AST_LITERAL;
        literal->token = token;
        return literal;
    }
    case CURB_TOKEN_LPAREN:
    {
        curb_lexer_move(env, lexer); // eat LPAREN
        curb_ast* expr = curb_parse_expr(env, lexer);
        if (lexer.curr.id != CURB_TOKEN_RPAREN)
        {
            CURB_PARSE_ERROR(env, lexer,  "Expression missing closing parentheses");
            curb_ast_delete(expr);    
            return nullptr;
        }
        curb_lexer_move(env, lexer); // eat RPAREN
        return expr;
    }
    default: 
    break;
    }
    return nullptr;
}

curb_ast* curb_parse_stmt_expr(curb_environment& env, curb_lexer& lexer)
{
    curb_ast* expr = curb_parse_expr(env, lexer);
    if (expr == nullptr)
        return nullptr;
    
    if (lexer.curr.id != CURB_TOKEN_SEMICOLON)
    {
        curb_ast_delete(expr);
        CURB_PARSE_ERROR(env, lexer, "Missing semicolon at end of expression");
        return nullptr;
    }

    curb_lexer_move(env, lexer); // eat SEMICOLON
    
    curb_ast* stmt_expr = curb_ast_new(CURB_AST_STMT_EXPR);
    stmt_expr->children.push_back(expr);
    return stmt_expr;
}

curb_ast* curb_parse_stmt_var(curb_environment& env, curb_lexer& lexer)
{
    if (lexer.curr.id != CURB_TOKEN_VAR)
        return nullptr;

    curb_lexer_move(env, lexer); // eat VAR

    curb_token name_token = lexer.curr;
    if (name_token.id != CURB_TOKEN_NAME)
    {
        CURB_PARSE_ERROR(env, lexer, "Variable assignment expected name");
        return nullptr;
    }
    curb_lexer_move(env, lexer); // eat NAME

    if (lexer.curr.id == CURB_TOKEN_SEMICOLON)
    {
        curb_lexer_move(env, lexer); // eat SEMICOLON

        curb_ast* stmt_assign = curb_ast_new(CURB_AST_STMT_VAR, name_token);
        return stmt_assign;
    }

    if (lexer.curr.id != CURB_TOKEN_ASSIGN)
    {
        CURB_PARSE_ERROR(env, lexer, "Variable assignment expected \"=\" or ;");
        return nullptr;
    }

    curb_lexer_move(env, lexer); // eat ASSIGN

    curb_ast* expr = curb_parse_expr(env, lexer);
    if (expr == nullptr)
    {
        CURB_PARSE_ERROR(env, lexer, "Variable assignment expected expression after \"=\"");
        return nullptr;
    }

    if (lexer.curr.id != CURB_TOKEN_SEMICOLON)
    {
        curb_ast_delete(expr);
        CURB_PARSE_ERROR(env, lexer, "Variable assignment missing semicolon at end of expression");
        return nullptr;
    }

    curb_lexer_move(env, lexer); // eat SEMICOLON

    curb_ast* stmt_assign = curb_ast_new(CURB_AST_STMT_VAR, name_token);
    stmt_assign->children.push_back(expr);
    return stmt_assign;
}

curb_ast* curb_parse_stmt_assign(curb_environment& env, curb_lexer& lexer)
{
    curb_token name_token = lexer.curr;
    curb_token eq_token = lexer.next;
    if (name_token.id != CURB_TOKEN_NAME || eq_token.id != CURB_TOKEN_ASSIGN)
        return nullptr;

    curb_lexer_move(env, lexer); // eat NAME
    curb_lexer_move(env, lexer); // eat ASSIGN

    curb_ast* expr = curb_parse_expr(env, lexer);
    if (expr == nullptr)
    {
        CURB_PARSE_ERROR(env, lexer, "Assignment expected expression after \"=\"");
        return nullptr;
    }

    if (lexer.curr.id != CURB_TOKEN_SEMICOLON)
    {
        curb_ast_delete(expr);
        CURB_PARSE_ERROR(env, lexer, "Missing semicolon at end of expression");
        return nullptr;
    }

    curb_lexer_move(env, lexer); // eat SEMICOLON

    curb_ast* stmt_assign = curb_ast_new(CURB_AST_STMT_ASSIGN, name_token);
    stmt_assign->children.push_back(expr);
    return stmt_assign;
}

curb_ast* curb_parse_stmt_if(curb_environment& env, curb_lexer& lexer)
{
    if (lexer.curr.id != CURB_TOKEN_IF)
        return nullptr;

    curb_lexer_move(env, lexer); // eat IF

    curb_ast* expr = curb_parse_expr(env, lexer);
    if(expr == nullptr)
    {
        CURB_PARSE_ERROR(env, lexer, "If statement missing expression");
        return nullptr;      
    }

    curb_ast* block = curb_parse_block(env, lexer);
    if (block == nullptr)
    {
        curb_ast_delete(expr);
        CURB_PARSE_ERROR(env, lexer, "If statement missing block");
        return nullptr;
    }

    // Parse else 
    if (lexer.curr.id == CURB_TOKEN_ELSE)
    {
        curb_lexer_move(env, lexer); // eat ELSE

        curb_ast* if_else_stmt = curb_ast_new(CURB_AST_STMT_IF_ELSE);
        if_else_stmt->children.push_back(expr);
        if_else_stmt->children.push_back(block);

        // Handling else if becomes else { if ... }
        if (lexer.curr.id == CURB_TOKEN_IF)
        {
            curb_ast* trailing_if_stmt = curb_parse_stmt_if(env, lexer);
            if (trailing_if_stmt == nullptr)
            {
                curb_ast_delete(if_else_stmt);
                return nullptr;
            }
            if_else_stmt->children.push_back(trailing_if_stmt);
        }
        else
        {
            curb_ast* else_block = curb_parse_block(env, lexer);
            if (else_block == nullptr)
            {
                curb_ast_delete(if_else_stmt);
                CURB_PARSE_ERROR(env, lexer, "If Else statement missing block");
                return nullptr;
            }
            if_else_stmt->children.push_back(else_block);
        }

        return if_else_stmt;
    }

    curb_ast* if_stmt = curb_ast_new(CURB_AST_STMT_IF);
    if_stmt->children.push_back(expr);
    if_stmt->children.push_back(block);
    return if_stmt;
}

curb_ast* curb_parse_stmt_while(curb_environment& env, curb_lexer& lexer)
{
    if (lexer.curr.id != CURB_TOKEN_WHILE)
        return nullptr;

    curb_lexer_move(env, lexer); // eat WHILE

    curb_ast* expr = curb_parse_expr(env, lexer);
    if (expr == nullptr)
    {
        CURB_PARSE_ERROR(env, lexer, "While statement missing expression");
        return nullptr;
    }

    curb_ast* block = curb_parse_block(env, lexer);
    if (block == nullptr)
    {
        curb_ast_delete(expr);
        CURB_PARSE_ERROR(env, lexer, "While statement missing block");
        return nullptr;
    }

    curb_ast* while_stmt = curb_ast_new(CURB_AST_STMT_WHILE);
    while_stmt->children.push_back(expr);
    while_stmt->children.push_back(block);

    return while_stmt;
}

curb_ast* curb_parse_param_list(curb_environment& env, curb_lexer& lexer)
{
    if (lexer.curr.id != CURB_TOKEN_LPAREN)
    {
        CURB_PARSE_ERROR(env, lexer, "Missing opening parentheses in function parameter list");
        return nullptr;
    }

    curb_lexer_move(env, lexer); // eat LPAREN

    curb_ast* param_list = curb_ast_new(CURB_AST_PARAM_LIST);

    if (lexer.curr.id == CURB_TOKEN_NAME)
    {
        curb_ast* param = curb_ast_new(CURB_AST_PARAM);
        param->token = lexer.curr;
        param_list->children.push_back(param);

        curb_lexer_move(env, lexer); // eat NAME

        while (lexer.curr.id == CURB_TOKEN_COMMA)
        {
            curb_lexer_move(env, lexer); // eat COMMA

            if (lexer.curr.id != CURB_TOKEN_NAME)
            {
                CURB_PARSE_ERROR(env, lexer, "Invalid function parameter. Expected parameter name");
                curb_ast_delete(param_list);
                return nullptr;
            }

            curb_ast* param = curb_ast_new(CURB_AST_PARAM);
            param->token = lexer.curr;
            param_list->children.push_back(param);

            curb_lexer_move(env, lexer); // eat NAME
        }
    }

    if (lexer.curr.id != CURB_TOKEN_RPAREN)
    {
        CURB_PARSE_ERROR(env, lexer, "Missing closing parentheses in function parameter list");
        curb_ast_delete(param_list);
        return nullptr;
    }

    curb_lexer_move(env, lexer); // eat RPAREN

    return param_list;
}

curb_ast* curb_parse_stmt_func(curb_environment& env, curb_lexer& lexer)
{
    if (lexer.curr.id != CURB_TOKEN_FUNC)
        return nullptr;

    curb_lexer_move(env, lexer); // eat FUNC

    if (lexer.curr.id != CURB_TOKEN_NAME)
    {
        CURB_PARSE_ERROR(env, lexer, "Missing name in function definition");
        return nullptr;
    }

    const curb_token func_name = lexer.curr;

    curb_lexer_move(env, lexer); // eat NAME

    curb_ast* param_list = curb_parse_param_list(env, lexer);
    if (param_list == nullptr)
        return nullptr;

    curb_ast* block = curb_parse_block(env, lexer);
    if (block == nullptr)
        return nullptr;

    curb_ast* func_def = curb_ast_new(CURB_AST_FUNC_DEF);
    func_def->token = func_name;
    func_def->children.push_back(param_list);
    func_def->children.push_back(block);

    return func_def;
}

curb_ast* curb_parse_stmt_return(curb_environment& env, curb_lexer& lexer)
{
    if (lexer.curr.id != CURB_TOKEN_RETURN)
        return nullptr;

    curb_lexer_move(env, lexer); // eat RETURN

    curb_ast* return_stmt = curb_ast_new(CURB_AST_RETURN);

    curb_ast* expr = curb_parse_expr(env, lexer);
    if (expr != nullptr)
        return_stmt->children.push_back(expr);

    if (lexer.curr.id != CURB_TOKEN_SEMICOLON)
    {
        curb_ast_delete(return_stmt);
        CURB_PARSE_ERROR(env, lexer, "Missing semicolon at end of expression");
        return nullptr;
    }

    curb_lexer_move(env, lexer); // eat SEMICOLON

    return return_stmt;
}

curb_ast* curb_parse_stmt(curb_environment& env, curb_lexer& lexer)
{
    //TODO: assignment, funcdef etc..
    // Default, epxression parsing. 
    curb_ast* stmt = nullptr;

    switch (lexer.curr.id)
    {
    case CURB_TOKEN_NAME:
        stmt = curb_parse_stmt_assign(env, lexer);
        if(!stmt)
            stmt = curb_parse_stmt_expr(env, lexer);
        break;
    case CURB_TOKEN_IF:
        stmt = curb_parse_stmt_if(env, lexer);
        break;
    case CURB_TOKEN_WHILE:
        stmt = curb_parse_stmt_while(env, lexer);
        break;
    case CURB_TOKEN_VAR:
        stmt = curb_parse_stmt_var(env, lexer);
        break;
    case CURB_TOKEN_FUNC:
        stmt = curb_parse_stmt_func(env, lexer);
        break;
    case CURB_TOKEN_RETURN:
        stmt = curb_parse_stmt_return(env, lexer);
        break;
    default:
        stmt = curb_parse_stmt_expr(env, lexer);
        break;
    }

    return stmt;
}

curb_ast* curb_parse_block(curb_environment& env, curb_lexer& lexer)
{
    if (lexer.curr.id != CURB_TOKEN_LBRACE)
    {
        CURB_PARSE_ERROR(env, lexer, "Block missing opening \"{\"");
        return nullptr;
    }
    curb_lexer_move(env, lexer); // eat LBRACE

    curb_array<curb_ast*> stmts;
    while(curb_ast* stmt = curb_parse_stmt(env, lexer))
        stmts.push_back(stmt);

    if(stmts.size() == 0)
        return nullptr;

    curb_ast* block = curb_ast_new(CURB_AST_BLOCK);
    block->children = std::move(stmts);


    if (lexer.curr.id != CURB_TOKEN_RBRACE)
    {
        CURB_PARSE_ERROR(env, lexer, "Block missing closing \"}\"");
        curb_ast_delete(block);
        return nullptr;
    }
    curb_lexer_move(env, lexer); // eat RBRACE

    return block;
}

curb_ast* curb_parse_root(curb_environment& env, curb_lexer& lexer)
{
    curb_ast* root = curb_ast_new(CURB_AST_ROOT);
    while (curb_ast* stmt = curb_parse_stmt(env, lexer))
        root->children.push_back(stmt);

    if (root->children.size() == 0)
    {
        curb_ast_delete(root);
        return nullptr;
    }
    return root;
}

curb_ast* curb_parse(curb_environment& env, const char* code)
{
    curb_lexer lexer;
    if(!curb_lexer_open(env, lexer, code))
        return nullptr;

    curb_ast* root = curb_parse_root(env, lexer);
    curb_lexer_close(lexer);

    return root;
}

curb_ast* curb_parse_file(curb_environment& env, const char* filename)
{
    curb_lexer lexer;
    if(!curb_lexer_open_file(env, lexer, filename))
        return nullptr;

    curb_ast* root = curb_parse_root(env, lexer);
    curb_lexer_close(lexer);

    return root;
}

// -------------------------------------- OPCODE -----------------------------------------// 

#define CURB_OPCODE_LIST           \
	CURB_OPCODE(EXIT)              \
	CURB_OPCODE(NO_OP)             \
	CURB_OPCODE(POP)               \
	CURB_OPCODE(PUSH_NONE)         \
	CURB_OPCODE(PUSH_BOOL)         \
	CURB_OPCODE(PUSH_INT)          \
	CURB_OPCODE(PUSH_FLOAT)        \
	CURB_OPCODE(PUSH_STRING)       \
	CURB_OPCODE(PUSH_LOCAL)        \
	CURB_OPCODE(LOAD_LOCAL)        \
	CURB_OPCODE(ADD)               \
	CURB_OPCODE(SUB)               \
	CURB_OPCODE(MUL)               \
	CURB_OPCODE(DIV)               \
	CURB_OPCODE(POW)               \
	CURB_OPCODE(MOD)               \
	CURB_OPCODE(AND)               \
	CURB_OPCODE(OR)                \
	CURB_OPCODE(XOR)               \
	CURB_OPCODE(NOT)               \
	CURB_OPCODE(NEG)               \
	CURB_OPCODE(CMP)               \
	CURB_OPCODE(ABS)               \
	CURB_OPCODE(SIN)               \
	CURB_OPCODE(COS)               \
	CURB_OPCODE(ATAN)              \
	CURB_OPCODE(LN)                \
	CURB_OPCODE(SQRT)              \
	CURB_OPCODE(INC)               \
	CURB_OPCODE(DEC)               \
    CURB_OPCODE(LT)                \
	CURB_OPCODE(LTE)               \
	CURB_OPCODE(EQ)                \
	CURB_OPCODE(NEQ)               \
	CURB_OPCODE(APPROXEQ)          \
	CURB_OPCODE(GT)                \
	CURB_OPCODE(GTE)               \
	CURB_OPCODE(JUMP)              \
	CURB_OPCODE(JUMP_ZERO)         \
	CURB_OPCODE(JUMP_NZERO)        \
	CURB_OPCODE(RETURN)            \
	CURB_OPCODE(CALL)              \
	CURB_OPCODE(CALL_EXT)          \
    CURB_OPCODE(PUSH_SCOPE)         \
    CURB_OPCODE(POP_SCOPE)          \


// Special value used in bytecode to denote an invalid vm offset
#define CURB_OPCODE_INVALID -1

enum curb_opcode : uint8_t
{ 
#define CURB_OPCODE(opcode) CURB_OPCODE_##opcode,
	CURB_OPCODE_LIST
#undef CURB_OPCODE
    CURB_OPCODE_COUNT
};
static_assert(CURB_OPCODE_COUNT < 255, "CURB Opcode count too large. This will affect bytecode instruction set");

static const char* curb_opcode_labels[] =
{
#define CURB_OPCODE(opcode) #opcode,
	CURB_OPCODE_LIST
#undef CURB_OPCODE
}; 

#undef CURB_OPCODE_LIST

// -------------------------------------- IR --------------------------------------------// 

enum curb_ir_operand_type
{
    // type if operand is constant or literal
    CURB_IR_OPERAND_NONE = 0,
    CURB_IR_OPERAND_BOOL,
    CURB_IR_OPERAND_INT,
    CURB_IR_OPERAND_FLOAT,  
    CURB_IR_OPERAND_BYTES,
};

struct curb_ir_operand
{
    union
    {
        bool b;
        int i;
        float f;
        const char* str; // NOTE: weak pointer to token data
        char bytes[CURB_VM_OPERAND_LEN]; // NOTE: weak pointer to token data
    } data;

    curb_ir_operand_type type = CURB_IR_OPERAND_NONE;
};

struct curb_ir_operation
{
    curb_opcode opcode;
    curb_array<curb_ir_operand> operands; //optional parameter. will be encoded in following bytes
   
#if CURB_DEBUG
    size_t bytecode_offset;
#endif //CURB_DEBUG
};

// All the blocks within a compilation/translation unit (i.e. file, code literal)
struct curb_ir
{		
    curb_array<curb_ir_operation> operations;
    size_t bytecode_offset = 0;
    
    curb_array<curb_symtable> symtable_stack;
    curb_array<size_t> frame_stack;

    int label_count = 0;
};

inline size_t curb_ir_operand_size(const curb_ir_operand& operand)
{
    switch (operand.type)
    {
    case CURB_IR_OPERAND_NONE:
        return 0;
    case CURB_IR_OPERAND_BOOL:
        return sizeof(operand.data.b);
    case CURB_IR_OPERAND_INT:
        return sizeof(operand.data.i);
    case CURB_IR_OPERAND_FLOAT:
        return sizeof(operand.data.f);
    case CURB_IR_OPERAND_BYTES:
        return strlen(operand.data.str) + 1; // +1 for null term
    }
    return 0;
}

inline size_t curb_ir_operation_size(const curb_ir_operation& operation)
{
    size_t size = sizeof(operation.opcode);
    for(const curb_ir_operand& operand : operation.operands)
    {
        size += curb_ir_operand_size(operand);
    }
    return size;
}

inline size_t curb_ir_add_operation(curb_ir& ir, curb_opcode opcode, const curb_array<curb_ir_operand>& operands)
{
    curb_ir_operation operation;
    operation.opcode = opcode;
    operation.operands = operands;
#if CURB_DEBUG
    operation.bytecode_offset = ir.bytecode_offset;
#endif //CURB_DEBUG
    ir.bytecode_offset += curb_ir_operation_size(operation);

    ir.operations.push_back(std::move(operation));
    return ir.operations.size()-1;
}

inline size_t curb_ir_add_operation(curb_ir& ir, curb_opcode opcode, const curb_ir_operand& operand)
{
    curb_ir_operation operation;
    operation.opcode = opcode;
    operation.operands.push_back(operand);
#if CURB_DEBUG
    operation.bytecode_offset = ir.bytecode_offset;
#endif //CURB_DEBUG

    ir.bytecode_offset += curb_ir_operation_size(operation);
    ir.operations.push_back(std::move(operation));
    return ir.operations.size()-1;
}

inline size_t curb_ir_add_operation(curb_ir& ir, curb_opcode opcode)
{
    curb_ir_operand operand;
    return curb_ir_add_operation(ir, opcode, operand);
}

inline curb_ir_operation& curb_ir_get_operation(curb_ir& ir, size_t operation_index)
{
    assert(operation_index < ir.operations.size());
    return ir.operations.at(operation_index);
}

inline curb_ir_operand curb_ir_operand_from_bool(bool data)
{
    curb_ir_operand operand;
    operand.type = CURB_IR_OPERAND_BOOL;
    operand.data.b = data;
    return operand;
}

inline curb_ir_operand curb_ir_operand_from_int(int data)
{
    curb_ir_operand operand;
    operand.type = CURB_IR_OPERAND_INT;
    operand.data.i = data;
    return operand;
}

inline curb_ir_operand curb_ir_operand_from_float(float data)
{
    curb_ir_operand operand;
    operand.type = CURB_IR_OPERAND_FLOAT;
    operand.data.f = data;
    return operand;
}

inline curb_ir_operand curb_ir_operand_from_str(const char* data)
{
    curb_ir_operand operand;
    operand.type = CURB_IR_OPERAND_BYTES;
    operand.data.str = data;
    return operand;
}

inline bool curb_ir_set_var(curb_ir& ir, const curb_string& name)
{
    assert(ir.symtable_stack.size() > 0); 
    assert(ir.frame_stack.size() > 0);

    curb_symtable& symtable = ir.symtable_stack.back();
    const int offset = ir.frame_stack.back()++;

    if (symtable.symbols.count(name) != 0)
        return false;

    curb_symbol sym;
    sym.type = CURB_IR_SYM_VAR;
    sym.offset = offset;

    symtable.symbols[name] = sym;
    return true;
}

inline bool curb_ir_set_func(curb_ir& ir, const curb_string& name)
{
    assert(ir.symtable_stack.size() > 0);

    curb_symtable& symtable = ir.symtable_stack.back();
    int offset = ir.bytecode_offset;

    if (symtable.symbols.count(name) != 0)
        return false;

    curb_symbol sym;
    sym.type = CURB_IR_SYM_FUNC;
    sym.offset = offset;

    symtable.symbols[name] = sym;
}

inline curb_symbol curb_ir_get_symbol(curb_ir& ir, const curb_string& name)
{
    for (int i = ir.symtable_stack.size() - 1; i >= 0; --i)
    {
        curb_symtable& symtable = ir.symtable_stack.at(i);
        if (symtable.symbols.count(name))
            return symtable.symbols[name];
    }

    curb_symbol sym;
    sym.offset = CURB_OPCODE_INVALID;
    sym.type = CURB_IR_SYM_NONE;
    return sym;
}

inline curb_symbol curb_ir_get_symbol_local(curb_ir& ir, const curb_string& name)
{
    assert(ir.symtable_stack.size() > 0);

    curb_symtable& symtable = ir.symtable_stack.back();
    if (symtable.symbols.count(name))
        return symtable.symbols[name];

    curb_symbol sym;
    sym.offset = CURB_OPCODE_INVALID;
    sym.type = CURB_IR_SYM_NONE;
    return sym;
}

inline void curb_ir_push_block(curb_ir& ir)
{
    curb_symtable symtable;
    ir.symtable_stack.push_back(symtable);
    
    if(ir.frame_stack.size())
        ir.frame_stack.push_back(ir.frame_stack.back());

    curb_ir_add_operation(ir, CURB_OPCODE_PUSH_SCOPE);
}

inline void curb_ir_pop_block(curb_ir& ir)
{
    assert(ir.symtable_stack.size() > 0);
    ir.symtable_stack.pop_back();

    if (ir.frame_stack.size())
        ir.frame_stack.pop_back();

    curb_ir_add_operation(ir, CURB_OPCODE_POP_SCOPE);
}

inline int curb_ir_frame(curb_ir& ir)
{
    if (ir.frame_stack.size() > 0)
        return ir.frame_stack.back();
    return 0;
}

inline void curb_ir_push_frame(curb_ir& ir, int arg_count)
{
    curb_symtable symtable;
    ir.symtable_stack.push_back(symtable);

    const int stack_offset = curb_ir_frame(ir) - arg_count;
    ir.frame_stack.push_back(stack_offset);
}

inline void curb_ir_pop_frame(curb_ir& ir)
{
    assert(ir.symtable_stack.size() > 0);
    ir.symtable_stack.pop_back();

    assert(ir.frame_stack.size() > 0);
    ir.frame_stack.pop_back();
}

// --------------------------------------- Value Operations -------------------------------------------------------//
const char* curb_type_labels[] =
{
    "bool", "int", "float", "string", "object", "none"
};

static_assert(sizeof(curb_type_labels) / sizeof(curb_type_labels[0]) == (int)CURB_NONE + 1, "Type labels must be up to date with enum");

inline bool curb_set_bool(curb_value* value, bool data)
{
    if (value == nullptr)
        return false;
    value->type = CURB_BOOL;
    value->b = data;
    return true;
}

inline bool curb_set_int(curb_value* value, int data)
{
    if (value == nullptr)
        return false;
    value->type = CURB_INT;
    value->i = data;
    return true;
}

inline bool curb_set_float(curb_value* value, float data)
{
    if (value == nullptr)
        return false;
    value->type = CURB_FLOAT;
    value->f = data;
    return true;
}

#define CURB_DEFINE_BINOP(result, lhs, rhs,                      \
    int_int_case,                                               \
    int_float_case,                                             \
    float_int_case,                                             \
    float_float_case,                                           \
    bool_bool_case                                              \
)                                                               \
{                                                               \
    if(result == nullptr || lhs == nullptr || rhs == nullptr )  \
        return false;                                           \
    switch(lhs->type)                                           \
    {                                                           \
        case CURB_INT:                                           \
            switch(rhs->type)                                   \
            {                                                   \
                case CURB_INT:   int_int_case;                   \
                case CURB_FLOAT: int_float_case;                 \
                default: break;                                 \
            }                                                   \
            break;                                              \
        case CURB_FLOAT:                                         \
           switch(rhs->type)                                    \
            {                                                   \
                case CURB_INT:   float_int_case;                 \
                case CURB_FLOAT: float_float_case;               \
                default: break;                                 \
            }                                                   \
            break;                                              \
        case CURB_BOOL:                                          \
            switch (rhs->type)                                  \
            {                                                   \
                case CURB_BOOL: bool_bool_case;                  \
                default: break;                                 \
            }                                                   \
            break;                                              \
        default: break;                                         \
    }                                                           \
}

inline bool curb_add(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_int(result, lhs->i + rhs->i),
        return curb_set_float(result, lhs->i + rhs->f),
        return curb_set_float(result, lhs->f + rhs->i),
        return curb_set_float(result, lhs->f + rhs->f),
        return false
    )
        return false;
}

inline bool curb_sub(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_int(result, lhs->i - rhs->i),
        return curb_set_float(result, lhs->i - rhs->f),
        return curb_set_float(result, lhs->f - rhs->i),
        return curb_set_float(result, lhs->f - rhs->f),
        return false
    )
        return false;
}

inline bool curb_mul(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_int(result, lhs->i * rhs->i),
        return curb_set_float(result, lhs->i * rhs->f),
        return curb_set_float(result, lhs->f * rhs->i),
        return curb_set_float(result, lhs->f * rhs->f),
        return false
    )
        return false;
}

inline bool curb_div(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_float(result, (float)lhs->i / rhs->i),
        return curb_set_float(result, lhs->i / rhs->f),
        return curb_set_float(result, lhs->f / rhs->i),
        return curb_set_float(result, lhs->f / rhs->f),
        return false
    )
        return false;
}

inline bool curb_pow(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_int(result, (int)powf((float)lhs->i, (float)rhs->i)),
        return curb_set_float(result, powf((float)lhs->i, rhs->f)),
        return curb_set_float(result, powf(lhs->f, (float)rhs->i)),
        return curb_set_float(result, powf(lhs->f, rhs->f)),
        return false
    )
        return false;
}

inline bool curb_mod(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_int(result, lhs->i % rhs->i),
        return curb_set_float(result, (float)fmod(lhs->i, rhs->f)),
        return curb_set_float(result, (float)fmod(lhs->f, rhs->i)),
        return curb_set_float(result, (float)fmod(lhs->f, rhs->f)),
        return false
    )
        return false;
}

inline bool curb_lt(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_bool(result, lhs->i < rhs->i),
        return curb_set_bool(result, lhs->i < rhs->f),
        return curb_set_bool(result, lhs->f < rhs->i),
        return curb_set_bool(result, lhs->f < rhs->f),
        return false
    )
        return false;
}

inline bool curb_lte(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_bool(result, lhs->i <= rhs->i),
        return curb_set_bool(result, lhs->i <= rhs->f),
        return curb_set_bool(result, lhs->f <= rhs->i),
        return curb_set_bool(result, lhs->f <= rhs->f),
        return false
    )
        return false;
}

inline bool curb_gt(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_bool(result, lhs->i > rhs->i),
        return curb_set_bool(result, lhs->i > rhs->f),
        return curb_set_bool(result, lhs->f > rhs->i),
        return curb_set_bool(result, lhs->f > rhs->f),
        return false
    )
        return false;
}

inline bool curb_gte(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_bool(result, lhs->i >= rhs->i),
        return curb_set_bool(result, lhs->i >= rhs->f),
        return curb_set_bool(result, lhs->f >= rhs->i),
        return curb_set_bool(result, lhs->f >= rhs->f),
        return false
    )
        return false;
}

inline bool curb_eq(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    // TODO: add a special case for object equivalence (per field)
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_bool(result, lhs->i == rhs->i),
        return curb_set_bool(result, lhs->i == rhs->f),
        return curb_set_bool(result, lhs->f == rhs->i),
        return curb_set_bool(result, lhs->f == rhs->f),
        return curb_set_bool(result, lhs->b == rhs->b)
    )
        return false;
}

inline bool curb_neq(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    // TODO: add a special case for object equivalence (per field)
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_bool(result, lhs->i != rhs->i),
        return curb_set_bool(result, lhs->i != rhs->f),
        return curb_set_bool(result, lhs->f != rhs->i),
        return curb_set_bool(result, lhs->f != rhs->f),
        return curb_set_bool(result, lhs->b != rhs->b)
    )
        return false;
}

inline bool curb_approxeq(curb_value* result, curb_value* lhs, curb_value* rhs)
{
    CURB_DEFINE_BINOP(result, lhs, rhs,
        return curb_set_bool(result, abs(lhs->i - rhs->i) < CURB_VM_APPROX_THRESHOLD),
        return curb_set_bool(result, abs(lhs->i - rhs->f) < CURB_VM_APPROX_THRESHOLD),
        return curb_set_bool(result, abs(lhs->f - rhs->i) < CURB_VM_APPROX_THRESHOLD),
        return curb_set_bool(result, abs(lhs->f - rhs->f) < CURB_VM_APPROX_THRESHOLD),
        return curb_set_bool(result, lhs->b == rhs->b)
    )
        return false;
}

#undef CURB_DEFINE_BINOP

// -------------------------------------- Virtual Machine / Bytecode ----------------------------------------------// 

// Calling frames are used to preserve and access parameters and local variables from the stack within a colling context
struct curb_vm_frame
{
    int arg_count;                 // number of arguments pushed onto frame. will be popped.
    int base_index;                // current stack offset of frame
    curb_array<int> scope_stack;
    const char* return_instruction;
};

struct curb_vm
{
    const char* instruction;
    const char* bytecode;
 
    curb_value stack[CURB_STACK_SIZE]; 
    int stack_offset;

    curb_array<curb_vm_frame> frame_stack;
    // TODO: debug to map from addr to func name / variable
};

// Used to convert values to/from bytes for constant values
union curb_vm_bytecode_value
{
    bool b;
    int i;
    float f;
    unsigned char bytes[CURB_VM_OPERAND_LEN];
};

#define CURB_VM_ERROR(env, vm, ...)     \
{                                      \
    CURB_LOG_ERROR(env, __VA_ARGS__);   \
    vm.instruction = nullptr;          \
}

#define CURB_VM_UNOP_ERROR(env, vm, arg, op)          \
{                                                    \
    if (arg != nullptr)                              \
        CURB_VM_ERROR(env, vm, "%s %s not defined",   \
            op,                                      \
            curb_type_labels[(int)arg->type]);        \
}

#define CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, op)     \
{                                                     \
    if (lhs != nullptr && rhs != nullptr)             \
        CURB_VM_ERROR(env, vm, "%s %s %s not defined", \
            curb_type_labels[(int)lhs->type],          \
            op,                                       \
            curb_type_labels[(int)rhs->type]);         \
}

inline curb_value* curb_vm_top(curb_environment& env, curb_vm& vm)
{
    return &vm.stack[vm.stack_offset -1];
}

inline curb_value* curb_vm_push(curb_environment& env, curb_vm& vm)
{
    if(vm.stack_offset >= CURB_STACK_SIZE)
    {                                              
        if(vm.instruction)
            CURB_VM_ERROR(env, vm, "Stack overflow");      
        return nullptr;                           
    }
    return &vm.stack[vm.stack_offset++];
}

inline curb_value* curb_vm_pop(curb_environment& env, curb_vm& vm)
{
    curb_value* top = curb_vm_top(env, vm);
    --vm.stack_offset;
    return top;
}

inline int curb_vm_frame_base(curb_environment& env, curb_vm& vm)
{
    const curb_vm_frame& frame = vm.frame_stack.back();
    return frame.base_index;
}

inline void curb_vm_push_frame(curb_environment& env, curb_vm& vm, const char* return_instruction, int arg_count)
{
    curb_vm_frame frame;
    frame.return_instruction = return_instruction;
    frame.arg_count = arg_count;
    frame.base_index = vm.stack_offset;

    vm.frame_stack.push_back(frame);
}

inline void curb_vm_pop_frame(curb_environment& env, curb_vm& vm)
{
    curb_vm_frame frame = vm.frame_stack.back();
    vm.instruction = frame.return_instruction;
    vm.stack_offset = frame.base_index - frame.arg_count;

    vm.frame_stack.pop_back();
}

// Pushing and popping the stack index for the current frame is used to enter and exit variable scopes. restores that stack index 
inline void curb_vm_push_scope(curb_environment& env, curb_vm& vm)
{
    curb_vm_frame& frame = vm.frame_stack.back();
    frame.scope_stack.push_back(vm.stack_offset);
}

inline void curb_vm_pop_scope(curb_environment& env, curb_vm& vm)
{
    curb_vm_frame& frame = vm.frame_stack.back();
    vm.stack_offset = frame.scope_stack.back();
    frame.scope_stack.pop_back();
}

inline curb_value* curb_vm_get(curb_environment& env, curb_vm& vm, int stack_offset)
{
    const int offset = curb_vm_frame_base(env, vm) + stack_offset;
    if (offset < 0)
    {
        if (vm.instruction)
            CURB_VM_ERROR(env, vm, "Stack underflow");
        return nullptr;
    }
    else if (offset >= vm.stack_offset)
    {
        if (vm.instruction)
            CURB_VM_ERROR(env, vm, "Stack overflow");
        return nullptr;
    }

    return &vm.stack[offset];
}

inline int curb_vm_read_bool(curb_vm& vm)
{
    curb_vm_bytecode_value bytecode_value;
    for (size_t i = 0; i < sizeof(bytecode_value.b); ++i)
        bytecode_value.bytes[i] = *(vm.instruction++);
    return bytecode_value.b;
}

inline int curb_vm_read_int(curb_vm& vm)
{
    curb_vm_bytecode_value bytecode_value;
    for (size_t i = 0; i < sizeof(bytecode_value.i); ++i)
        bytecode_value.bytes[i] = *(vm.instruction++);
    return bytecode_value.i;
}

inline float curb_vm_read_float(curb_vm& vm)
{
    curb_vm_bytecode_value bytecode_value;
    for (size_t i = 0; i < sizeof(bytecode_value.f); ++i)
        bytecode_value.bytes[i] = *(vm.instruction++);
    return bytecode_value.f;
}

inline const char* curb_vm_read_bytes(curb_vm& vm)
{
    size_t len = 1; // include null terminating
    while (*(vm.instruction++))
        len++;
    return vm.instruction - len;
}

void curb_vm_startup(curb_environment& env, curb_vm& vm, const curb_script& script)
{
    if (script.bytecode.size() == 0)
        return;

    vm.bytecode = &script.bytecode[0];
    vm.instruction = vm.bytecode;
    vm.stack_offset = 0;

    if (vm.bytecode == nullptr)
        return;

    curb_vm_push_frame(env, vm, nullptr, 0);
}

void curb_vm_shutdown(curb_environment& env, curb_vm& vm)
{
    curb_vm_pop_frame(env, vm);

    // Ensure that stack has returned to beginning state
    assert(vm.stack_offset == 0);
}

void curb_vm_execute(curb_environment& env, curb_vm& vm)
{
    while(vm.instruction)
    {
#if  0
        printf("[%d] %s\n", vm.instruction - vm.bytecode, curb_opcode_labels[(*vm.instruction)]);
#endif
        curb_opcode opcode = (curb_opcode) (*vm.instruction);
        ++vm.instruction;

        switch(opcode)
        {
            case CURB_OPCODE_NO_OP:
                break;
            case CURB_OPCODE_EXIT:
            {
                vm.instruction = nullptr;
                break;
            }
            case CURB_OPCODE_POP:
            {
                curb_vm_pop(env, vm);
                break;
            }
            case CURB_OPCODE_PUSH_NONE:
            {
                curb_value* value = curb_vm_push(env, vm);
                if (value == nullptr)
                    break;
                value->type = CURB_NONE;
                break;
            }
            case CURB_OPCODE_PUSH_BOOL:
            {
                curb_value* value = curb_vm_push(env, vm);
                if (value == nullptr)
                    break;
                value->type = CURB_BOOL;
                value->b = curb_vm_read_bool(vm);
                break;
            }
            case CURB_OPCODE_PUSH_INT:   
            {
                curb_value* value = curb_vm_push(env, vm);
                if(value == nullptr) 
                    break;
                value->type = CURB_INT;
                value->i = curb_vm_read_int(vm);
                break;
            }
            case CURB_OPCODE_PUSH_FLOAT:
            {
                curb_value* value = curb_vm_push(env, vm);
                if(value == nullptr) 
                    break;
                value->type = CURB_FLOAT;
                value->f = curb_vm_read_float(vm);
                break;
            }                                  
            case CURB_OPCODE_PUSH_STRING:
            {
                curb_value* value = curb_vm_push(env, vm);
                if(value == nullptr) 
                    break;
                value->type = CURB_STRING;
                value->str = curb_vm_read_bytes(vm); // TODO: Duplicate string ? When popping string from stack, clean up allocated string ?  Otherwise, this string can not be modified
                break;
            }
            case CURB_OPCODE_PUSH_LOCAL:
            {
                const int address = curb_vm_read_int(vm);
                curb_value* top = curb_vm_push(env, vm);
                curb_value* local = curb_vm_get(env, vm, address);
                if (top !=  nullptr || local != nullptr)
                    *top = *local;
                break;
            }
            case CURB_OPCODE_LOAD_LOCAL:
            {
                const int stack_offset = curb_vm_read_int(vm);
                curb_value* top = curb_vm_pop(env, vm);
                curb_value* local = curb_vm_get(env, vm, stack_offset);
                if (top != nullptr && local != nullptr)
                    *local = *top;
                break;
            }
            case CURB_OPCODE_ADD:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);    
                if (!curb_add(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "+");
                break;
            }
            case CURB_OPCODE_SUB:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);    
                if (!curb_sub(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "-");
                break;
            }
            case CURB_OPCODE_MUL:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);    
                if (!curb_mul(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "*");
                break;
            }
            case CURB_OPCODE_DIV:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);    
                if(!curb_div(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "/");
                break;
            }
            case CURB_OPCODE_POW:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);
                if (!curb_pow(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "^");
                break;
            }
            case CURB_OPCODE_MOD:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);
                if (!curb_mod(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "%%");
                break;
            }
            case CURB_OPCODE_NOT:
            {
                curb_value* arg = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);
                if (!curb_set_bool(target, !curb_to_bool(arg)))
                    CURB_VM_UNOP_ERROR(env, vm, arg, "!");
                break;
            }
            case CURB_OPCODE_AND:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);
                if (!curb_set_bool(target, curb_to_bool(lhs) && curb_to_bool(rhs)))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "&");
                break;
            }
            case CURB_OPCODE_OR:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);
                if (!curb_set_bool(target, curb_to_bool(lhs) || curb_to_bool(rhs)))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "|");
                break;
            }
            case CURB_OPCODE_LT:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);
                if (!curb_lt(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "<");
                break;
            }
            case CURB_OPCODE_LTE:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);
                if (!curb_lte(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "<=");
                break;
            }
            case CURB_OPCODE_GT:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);
                if (!curb_gt(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, ">");
                break;
            }
            case CURB_OPCODE_GTE:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);
                if (!curb_gte(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, ">=");
                break;
            }
            case CURB_OPCODE_EQ:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);
                if (!curb_eq(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "==");
                break;
            }
            case CURB_OPCODE_NEQ:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);
                if (!curb_neq(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "!=");
                break;
            }
            case CURB_OPCODE_APPROXEQ:
            {
                curb_value* lhs = curb_vm_pop(env, vm);
                curb_value* rhs = curb_vm_pop(env, vm);
                curb_value* target = curb_vm_push(env, vm);
                if (!curb_approxeq(target, lhs, rhs))
                    CURB_VM_BINOP_ERROR(env, vm, lhs, rhs, "~=");
                break;
            }
            case CURB_OPCODE_JUMP:
            {
                const int instruction_offset = curb_vm_read_int(vm);
                vm.instruction = vm.bytecode + instruction_offset;
                break;
            }
            case CURB_OPCODE_JUMP_NZERO:
            {
                const int instruction_offset = curb_vm_read_int(vm);
                curb_value* value = curb_vm_pop(env, vm);
                if (curb_to_bool(value) != 0)
                    vm.instruction = vm.bytecode + instruction_offset;
                break;
            }
            case CURB_OPCODE_JUMP_ZERO:
            {
                const int instruction_offset = curb_vm_read_int(vm);
                curb_value* value = curb_vm_pop(env, vm);
                if (curb_to_bool(value) == 0)
                    vm.instruction = vm.bytecode + instruction_offset;
                break;
            }
            case CURB_OPCODE_PUSH_SCOPE:
            {
                curb_vm_push_scope(env, vm);
                break;
            }
            case CURB_OPCODE_POP_SCOPE:
            {
                curb_vm_pop_scope(env, vm);
                break;
            }
            case CURB_OPCODE_CALL:
            {
                const int arg_count = curb_vm_read_int(vm);
                curb_vm_push_frame(env, vm, vm.instruction, arg_count);

                const int func_addr = curb_vm_read_int(vm);
                vm.instruction = vm.bytecode + func_addr;
                break;
            }
            case CURB_OPCODE_RETURN:
            {
                curb_value* ret_value = curb_vm_top(env, vm);
                curb_vm_pop_frame(env, vm);

                curb_value* top = curb_vm_push(env, vm);
                if (ret_value != nullptr && top != nullptr)
                    *top = *ret_value;
                break;
            }
            case CURB_OPCODE_CALL_EXT:
            {
                const int arg_count = curb_vm_read_int(vm);
                const char* func_name = curb_vm_read_bytes(vm);;

                curb_array<curb_value*> args;
                args.resize(arg_count);
                for (int i = arg_count - 1; i >= 0; --i)
                {
                    curb_value* arg = curb_vm_pop(env, vm);
                    if (arg != nullptr)
                        args[i] = arg;
                }

                if ((int)args.size() != arg_count)
                {
                    CURB_VM_ERROR(env, vm, "Function Call %s Recieved %d arguments, expected %d", func_name, args.size(), arg_count);
                    break;
                }

                if (env.external_functions.count(func_name) == 0)
                {
                    CURB_VM_ERROR(env, vm, "External Function Call %s not registered", func_name);
                    break;
                }

                curb_value ret_value;
                ret_value.type = CURB_NONE;
                env.external_functions.at(func_name)(&ret_value, args);
                
                curb_value* top = curb_vm_push(env, vm);
                if (top)
                    *top = ret_value;
                break;
            }
            default:
                // UNSUPPORTED!!!!
            break;
        }
    }
}

curb_value curb_vm_execute_function(curb_environment& env, curb_vm& vm, curb_script& script, const char* func_name, const curb_array<curb_value>& args)
{
    curb_value ret_value;
    ret_value.type = CURB_NONE;

    if (script.global_symtable.symbols.count(func_name) == 0)
    {
        CURB_LOG_ERROR(env, "Function name not defined %s", func_name);
        return ret_value;
    }

    const curb_symbol& symbol = script.global_symtable.symbols[func_name];
    if (symbol.type != CURB_IR_SYM_FUNC)
    {
        CURB_LOG_ERROR(env, "%s is not defined as a function", func_name);
        return ret_value;
    }

    // Jump to function call

    const size_t arg_count = args.size();
    vm.instruction = vm.bytecode + symbol.offset;

    for (size_t i = 0; i < arg_count; ++i)
    {
        curb_value* value = curb_vm_push(env, vm);
        if (value)
            *value = args[i];
    }

    curb_vm_push_frame(env, vm, nullptr, arg_count);
    curb_vm_execute(env, vm);

    curb_value* top = curb_vm_pop(env, vm);
    if (top)
        ret_value = *top;

    return ret_value;
}

// -------------------------------------- Passes ------------------------------------------------// 
bool curb_pass_semantic_check(const curb_ast* node)
{
    if(node == nullptr)
        return false;
    return true;
}

// -------------------------------------- Transformations ---------------------------------------// 
struct curb_ast_to_ir_context
{
    bool valid;
    curb_environment env;
};

void curb_ast_to_ir(curb_ast_to_ir_context& context, const curb_ast* node, curb_ir& ir)
{
    if(node == nullptr|| !context.valid)
        return;

    const curb_token& token = node->token;
    const curb_array<curb_ast*>& children = node->children;

    switch(node->id)
    {
        case CURB_AST_ROOT:
        {
            curb_ir_push_block(ir); // push a global block. This will be returned

            for (curb_ast* stmt : children)
                curb_ast_to_ir(context, stmt, ir);

            // End block, default exit
            curb_ir_add_operation(ir, CURB_OPCODE_EXIT);
            break;
        }
        case CURB_AST_BLOCK: 
        {
            for (curb_ast* stmt : children)
                curb_ast_to_ir(context, stmt, ir);
            break;
        }
        case CURB_AST_LITERAL:
        {
            switch(token.id)
            {
                case CURB_TOKEN_DECIMAL:
                {
                    const int data = strtol(token.data.c_str(), NULL, 10);
                    const curb_ir_operand& operand = curb_ir_operand_from_int(data);
                    curb_ir_add_operation(ir, CURB_OPCODE_PUSH_INT, operand);
                    break;
                }
                case CURB_TOKEN_HEXIDECIMAL:
                {
                    const unsigned int data = strtoul(token.data.c_str(), NULL, 16);
                    const curb_ir_operand& operand = curb_ir_operand_from_int(data);
                    curb_ir_add_operation(ir, CURB_OPCODE_PUSH_INT, operand);
                    break;
                }
                case CURB_TOKEN_BINARY:
                {
                    const unsigned int data = strtoul(token.data.c_str(), NULL, 2);
                    const curb_ir_operand& operand = curb_ir_operand_from_int(data);
                    curb_ir_add_operation(ir, CURB_OPCODE_PUSH_INT, operand);
                    break;
                }
                case CURB_TOKEN_FLOAT:       
                {
                    const float data = strtof(token.data.c_str(), NULL);
                    const curb_ir_operand& operand = curb_ir_operand_from_float(data);
                    curb_ir_add_operation(ir, CURB_OPCODE_PUSH_FLOAT, operand);
                    break;
                }
                case CURB_TOKEN_STRING:      
                {
                    const curb_ir_operand& operand = curb_ir_operand_from_str(token.data.c_str());
                    curb_ir_add_operation(ir, CURB_OPCODE_PUSH_STRING, operand);
                    break;
                }
                case CURB_TOKEN_TRUE:
                {
                    const curb_ir_operand& operand = curb_ir_operand_from_bool(true);
                    curb_ir_add_operation(ir, CURB_OPCODE_PUSH_BOOL, operand);
                    break;
                }
                case CURB_TOKEN_FALSE:
                {
                    const curb_ir_operand& operand = curb_ir_operand_from_bool(false);
                    curb_ir_add_operation(ir, CURB_OPCODE_PUSH_BOOL, operand);
                    break;
                }
                default: 
                break;
            }
            break;
        }
        case CURB_AST_VARIABLE:
        {
            const curb_symbol& symbol = curb_ir_get_symbol(ir, token.data);
            if (symbol.type == CURB_IR_SYM_NONE)
            {
                context.valid = false;
                CURB_LOG_ERROR(context.env, "Variable %s not defined in current block", token.data.c_str());
                break;
            }

            if (symbol.type == CURB_IR_SYM_FUNC)
            {
                context.valid = false;
                CURB_LOG_ERROR(context.env, "Function %s can not be used as a variable", token.data.c_str());
                break;
            }

            // TODO: determine if variable is local or global
            const curb_ir_operand& address_operand = curb_ir_operand_from_int(symbol.offset);
            curb_ir_add_operation(ir, CURB_OPCODE_PUSH_LOCAL, address_operand);
            break;
        }
        case CURB_AST_UNARY_OP:
        {
            assert(children.size() == 1); // token [0]

            curb_ast_to_ir(context, children[0], ir);

            switch(token.id)
            {
                case CURB_TOKEN_NOT: curb_ir_add_operation(ir, CURB_OPCODE_NOT); break;
                default: break;
            }
            break;
        }
        case CURB_AST_BINARY_OP:
        {
            assert(children.size() == 2); // [0] token [1]

            curb_ast_to_ir(context, children[1], ir); // RHS
            curb_ast_to_ir(context, children[0], ir); // LHS

            switch(token.id)
            {
                case CURB_TOKEN_ADD:    curb_ir_add_operation(ir, CURB_OPCODE_ADD); break;
                case CURB_TOKEN_SUB:    curb_ir_add_operation(ir, CURB_OPCODE_SUB); break;
                case CURB_TOKEN_MUL:    curb_ir_add_operation(ir, CURB_OPCODE_MUL); break;
                case CURB_TOKEN_DIV:    curb_ir_add_operation(ir, CURB_OPCODE_DIV); break;
                case CURB_TOKEN_MOD:    curb_ir_add_operation(ir, CURB_OPCODE_MOD); break;
                case CURB_TOKEN_POW:    curb_ir_add_operation(ir, CURB_OPCODE_POW); break;
                case CURB_TOKEN_AND:    curb_ir_add_operation(ir, CURB_OPCODE_AND); break;
                case CURB_TOKEN_OR:     curb_ir_add_operation(ir, CURB_OPCODE_OR); break;
                case CURB_TOKEN_LT:     curb_ir_add_operation(ir, CURB_OPCODE_LT);  break;
                case CURB_TOKEN_LT_EQ:  curb_ir_add_operation(ir, CURB_OPCODE_LTE); break;
                case CURB_TOKEN_GT:     curb_ir_add_operation(ir, CURB_OPCODE_GT);  break;
                case CURB_TOKEN_GT_EQ:  curb_ir_add_operation(ir, CURB_OPCODE_GTE); break;
                case CURB_TOKEN_EQ:     curb_ir_add_operation(ir, CURB_OPCODE_EQ);  break;
                case CURB_TOKEN_NOT_EQ: curb_ir_add_operation(ir, CURB_OPCODE_NEQ); break;
                case CURB_TOKEN_APPROX_EQ: curb_ir_add_operation(ir, CURB_OPCODE_APPROXEQ); break;
                default: break;
            }
            break;
        }
        case CURB_AST_FUNC_CALL:
        {
            const char* func_name = token.data.c_str();
            const int arg_count = children.size();
            const curb_symbol& symbol = curb_ir_get_symbol(ir, func_name);
            if (symbol.type == CURB_IR_SYM_FUNC)
            {
                for (const curb_ast* arg : children)
                    curb_ast_to_ir(context, arg, ir);

                const curb_ir_operand& arg_count_operand = curb_ir_operand_from_int(arg_count);
                const curb_ir_operand& func_jmp_operand = curb_ir_operand_from_int(symbol.offset);

                curb_array<curb_ir_operand> operands;
                operands.push_back(arg_count_operand);
                operands.push_back(func_jmp_operand);
                curb_ir_add_operation(ir, CURB_OPCODE_CALL, operands);
            }
            else if (symbol.type == CURB_IR_SYM_VAR)
            {
                context.valid = false;
                CURB_LOG_ERROR(context.env, "Can not call variable %s as a function", func_name);
            }
            else if(context.env.external_functions.count(func_name))
            {
                for (const curb_ast* arg : children)
                    curb_ast_to_ir(context, arg, ir);

                const curb_ir_operand& arg_count_operand = curb_ir_operand_from_int(arg_count);
                const curb_ir_operand& func_name_operand = curb_ir_operand_from_str(func_name);

                curb_array<curb_ir_operand> operands;
                operands.push_back(arg_count_operand);
                operands.push_back(func_name_operand);
                curb_ir_add_operation(ir, CURB_OPCODE_CALL_EXT, operands);
            }
            else
            {
                context.valid = false;
                CURB_LOG_ERROR(context.env, "Function %s not defined", func_name);
            }
            break;
        }
        case CURB_AST_STMT_EXPR:
        {
            if (children.size() == 1)
            {
                curb_ast_to_ir(context, children[0], ir);
                // discard the top
                curb_ir_add_operation(ir, CURB_OPCODE_POP);
            }
            break;
        }
        case CURB_AST_STMT_ASSIGN:
        {
            if (children.size() == 1) // token = [0]
                curb_ast_to_ir(context, children[0], ir);

            const curb_symbol symbol = curb_ir_get_symbol(ir, token.data);
            if (symbol.offset == CURB_OPCODE_INVALID)
            {
                context.valid = false;
                CURB_LOG_ERROR(context.env, "Variable %s not defined", token.data.c_str());
                break;
            }

            const curb_ir_operand& address_operand = curb_ir_operand_from_int(symbol.offset);
            curb_ir_add_operation(ir, CURB_OPCODE_LOAD_LOCAL, address_operand);

            break;
        }
        case CURB_AST_STMT_VAR:
        {
            if (children.size() == 1) // token = [0]
                curb_ast_to_ir(context, children[0], ir);

            const curb_symbol symbol = curb_ir_get_symbol_local(ir, token.data);
            if (symbol.offset != CURB_OPCODE_INVALID)
            {
                context.valid = false;
                CURB_LOG_ERROR(context.env, "Variable %s already defined", token.data.c_str());
                break;
            }
            
            curb_ir_set_var(ir, token.data);

            break;
        }
        case CURB_AST_STMT_IF:
        {
            assert(children.size() == 2); //if ([0]) {[1]}

            const curb_ir_operand stub_operand = curb_ir_operand_from_int(0);

            // Evaluate expression. 
            curb_ast_to_ir(context, children[0], ir);

            //Jump to end if false
            const size_t end_block_jmp = curb_ir_add_operation(ir, CURB_OPCODE_JUMP_ZERO, stub_operand);

            // True block
            curb_ir_push_block(ir);
            curb_ast_to_ir(context, children[1], ir);
            curb_ir_pop_block(ir);

            const size_t end_block_addr = ir.bytecode_offset;

            // Fixup stubbed block offsets
            curb_ir_operation& end_block_jmp_operation = curb_ir_get_operation(ir, end_block_jmp);
            end_block_jmp_operation.operands[0] = curb_ir_operand_from_int(end_block_addr);
            
            break;
        }
        case CURB_AST_STMT_IF_ELSE:
        {
            assert(children.size() == 3); //if ([0]) {[1]} else {[2]}

            const curb_ir_operand stub_operand = curb_ir_operand_from_int(0);

            // Evaluate expression. 
            curb_ast_to_ir(context, children[0], ir);

            //Jump to else if false
            const size_t else_block_jmp = curb_ir_add_operation(ir, CURB_OPCODE_JUMP_ZERO, stub_operand);

            // True block
            curb_ir_push_block(ir);
            curb_ast_to_ir(context, children[1], ir);
            curb_ir_pop_block(ir);

                
            //Jump to end after true
            const size_t end_block_jmp = curb_ir_add_operation(ir, CURB_OPCODE_JUMP, stub_operand);
            const size_t else_block_addr = ir.bytecode_offset;

            // Else block
            curb_ir_push_block(ir);
            curb_ast_to_ir(context, children[2], ir);
            curb_ir_pop_block(ir);

            // Tag end address
            const size_t end_block_addr = ir.bytecode_offset;

            // Fixup stubbed block offsets
            curb_ir_operation& else_block_jmp_operation = curb_ir_get_operation(ir, else_block_jmp);
            else_block_jmp_operation.operands[0] = curb_ir_operand_from_int(else_block_addr);

            curb_ir_operation& end_block_jmp_operation = curb_ir_get_operation(ir, end_block_jmp);
            end_block_jmp_operation.operands[0] = curb_ir_operand_from_int(end_block_addr);
            
            break;
        }
        case CURB_AST_STMT_WHILE:
        {
            assert(children.size() == 2); //while ([0]) {[1]}

            const curb_ir_operand stub_operand = curb_ir_operand_from_int(0);

            const curb_ir_operand& begin_block_operand = curb_ir_operand_from_int(ir.bytecode_offset);

            // Evaluate expression. 
            curb_ast_to_ir(context, children[0], ir);

            //Jump to end if false
            const size_t end_block_jmp = curb_ir_add_operation(ir, CURB_OPCODE_JUMP_ZERO, stub_operand);

            // Loop block
            curb_ir_push_block(ir);
            curb_ast_to_ir(context, children[1], ir);
            curb_ir_pop_block(ir);

            // Jump back to beginning, expr evaluation 
            curb_ir_add_operation(ir, CURB_OPCODE_JUMP, begin_block_operand);

            // Tag end address
            const size_t end_block_addr = ir.bytecode_offset;

            // Fixup stubbed block offsets
            curb_ir_operation& end_block_jmp_operation = curb_ir_get_operation(ir, end_block_jmp);
            end_block_jmp_operation.operands[0] = curb_ir_operand_from_int(end_block_addr);
            
            break;
        }
        case CURB_AST_PARAM:
        {
            curb_ir_set_var(ir, token.data);
            break;
        }
        case CURB_AST_PARAM_LIST:
        {                
            // start stack offset at beginning of parameter (this will be nagative relative to the current frame)
            for (const curb_ast* param : children)
                curb_ast_to_ir(context, param, ir);
            break;
        }
        case CURB_AST_RETURN:
        {
            if(children.size() == 1) //return [0];
            {
                curb_ast_to_ir(context, children[0], ir);
                curb_ir_add_operation(ir, CURB_OPCODE_RETURN);
            }
            else
            {
                curb_ir_add_operation(ir, CURB_OPCODE_PUSH_NONE);
                curb_ir_add_operation(ir, CURB_OPCODE_RETURN);
            }
            break;
        }
        case CURB_AST_FUNC_DEF:
        {
            assert(children.size() == 2); //func token [0] {[1]};

            // Jump over the func def
            const curb_ir_operand stub_operand = curb_ir_operand_from_int(0);
            const size_t end_block_jmp = curb_ir_add_operation(ir, CURB_OPCODE_JUMP, stub_operand);

            const char* func_name = token.data.c_str();
            if (!curb_ir_set_func(ir, func_name))
            {
                context.valid = false;
                CURB_LOG_ERROR(context.env, "Function %s already defined", func_name);
                break;
            }

            curb_ast* params = children[0];
            assert( params && params->id == CURB_AST_PARAM_LIST);

            curb_ir_push_frame(ir, params->children.size());
            curb_ast_to_ir(context, children[0], ir);
            curb_ast_to_ir(context, children[1], ir);
            curb_ir_pop_frame(ir);

            // Ensure there is a return
            if (ir.operations.at(ir.operations.size() - 1).opcode != CURB_OPCODE_RETURN)
            {
                curb_ir_add_operation(ir, CURB_OPCODE_PUSH_NONE);
                curb_ir_add_operation(ir, CURB_OPCODE_RETURN);
            }

            const size_t end_block_addr = ir.bytecode_offset;

            // fixup jump operand
            curb_ir_operation& end_block_jmp_operation = curb_ir_get_operation(ir, end_block_jmp);
            end_block_jmp_operation.operands[0] = curb_ir_operand_from_int(end_block_addr);

            break;
        }
        default:
            assert(0);
            break;
    }
}

void curb_ir_to_bytecode(curb_ir& ir, curb_array<char>& bytecode)
{  
    bytecode.resize(ir.bytecode_offset, CURB_OPCODE_NO_OP) ;

    char* instruction = &bytecode[0];

    for(const curb_ir_operation& operation : ir.operations)
    {
        *instruction = (char)operation.opcode;
        ++instruction;    

        for(const curb_ir_operand& operand : operation.operands)
        {
            switch (operand.type)
            {
            case CURB_IR_OPERAND_NONE:
                break;
            case CURB_IR_OPERAND_BOOL:
            case CURB_IR_OPERAND_INT:
            case CURB_IR_OPERAND_FLOAT:
                for (size_t i = 0; i < curb_ir_operand_size(operand); ++i)
                    *(instruction++) = operand.data.bytes[i];
                break;
            case CURB_IR_OPERAND_BYTES:
                for (size_t i = 0; i < strlen(operand.data.str); ++i)
                    *(instruction++) = operand.data.str[i];

                *(instruction++) = 0; // null terminate
                break;
            }
        }
    }
}

// -------------------------------------- API ------ ---------------------------------------// 

bool curb_compile_script(curb_environment& env, curb_ast* root, curb_script& script)
{
    script.bytecode.clear();
    script.global_symtable.symbols.clear();

    bool success = curb_pass_semantic_check(root);
    if (!success)
        return false;

    // Generate IR
    curb_ast_to_ir_context context;
    context.valid = true;
    context.env = env;

    curb_ir ir;
    curb_ast_to_ir(context, root, ir);

    if (!context.valid)
        return false;

    assert(ir.symtable_stack.size() == 1);
    script.global_symtable = ir.symtable_stack.back();

    // Load bytecode into VM
    curb_ir_to_bytecode(ir, script.bytecode);

    return context.valid;
}

void curb_register(curb_environment& env, const char* func_name, curb_function_callback *callback)
{
    env.external_functions[func_name] = callback;
}

void curb_unregister(curb_environment& env, const char* func_name)
{
    env.external_functions.erase(func_name);
}

void curb_execute(curb_environment& env, const char* filename)
{
    // Parse file
    curb_ast* root = curb_parse_file(env, filename);
    if (root == nullptr)
        return;

    curb_script script;
    const bool success = curb_compile_script(env, root, script);
    curb_ast_delete(root);

    if (!success)
        return;

    curb_vm vm;
    curb_vm_startup(env, vm, script);
    curb_vm_execute(env, vm);
    curb_vm_shutdown(env, vm);
}

void curb_evaluate(curb_environment& env, const char* code)
{
    // Parse file
    curb_ast* root = curb_parse(env, code);
    if (root == nullptr)
        return;

    // Load bytecode into VM
    curb_script script;
    const bool success = curb_compile_script(env, root, script);

    // Cleanup 
    curb_ast_delete(root);

    if (!success)
        return;

    curb_vm vm;
    curb_vm_startup(env, vm, script);
    curb_vm_execute(env, vm);
    curb_vm_shutdown(env, vm);
}

bool curb_compile(curb_environment& env, curb_script& script, const char* filename)
{
    script.valid = false;
    // Parse file
    curb_ast* root = curb_parse_file(env, filename);
    if (root == nullptr)
        return false;

    script.valid = curb_compile_script(env, root, script);
    curb_ast_delete(root);

    return script.valid;
}

curb_value curb_call(curb_environment& env, curb_script& script, const char* func_name, const curb_array<curb_value>& args)
{
    if (!script.valid)
    {
        curb_value ret_value;
        ret_value.type = CURB_NONE;
        return ret_value;
    }

    curb_vm vm;
    curb_vm_startup(env, vm, script);
    const curb_value& ret_value = curb_vm_execute_function(env, vm, script, func_name, args);
    curb_vm_shutdown(env, vm);
    return ret_value;
}

curb_value curb_bool(bool data)
{
    curb_value value;
    curb_set_bool(&value, data);
    return value;
}

curb_value curb_int(int data)
{
    curb_value value;
    curb_set_int(&value, data);
    return value;
}

curb_value curb_float(float data)
{
    curb_value value;
    curb_set_float(&value, data);
    return value;
}

curb_string curb_to_string(const curb_value* value)
{
    if (value == nullptr)
        return "null";

    char out[1024];
    int len = 0;
    switch (value->type)
    {
    case CURB_NONE:
        len = snprintf(out, sizeof(out), "%s", curb_type_labels[value->type]);
        break;
    case CURB_BOOL:
        len = snprintf(out, sizeof(out), "%s", value->b ? "true" : "false");
        break;
    case CURB_INT:
        len = snprintf(out, sizeof(out), "%d", value->i);
        break;
    case CURB_FLOAT:
        len = snprintf(out, sizeof(out), "%f", value->f);
        break;
    case CURB_STRING:
        len = snprintf(out, sizeof(out), "\"%s\"", value->str);
        break;
    case CURB_OBJECT:
        len = snprintf(out, sizeof(out), "%s", curb_type_labels[value->type]);
        break;
    }
    return curb_string(out, len);
}

bool curb_to_bool(const curb_value* value)
{
    if (value == nullptr)
        return false;

    switch (value->type)
    {
    case CURB_NONE:
        return false;
    case CURB_BOOL:
        return value->b;
    case CURB_INT:
        return value->i != 0;
    case CURB_FLOAT:
        return value->f != 0.0f;
    case CURB_STRING:
        return value->str != nullptr;
    case CURB_OBJECT:
        return value->obj != nullptr;
    }
    return false;
}

#endif