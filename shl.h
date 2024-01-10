#ifndef __SHL_HEADER__
#define __SHL_HEADER__
/*
    Curb Programming Language
        Single File parser, bytecode compiler, and virtual machine for the Curb programming
    Author: https://github.com/138paulmiller

    Use 
        `#define SHL_IMPLEMENTATION`
        `#include "shl.h"`

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

        args := expr 
              | expr , args
              | NULL

        value := NAME 
               | func_call 
               | NUMBER 
               | STRING 
               | ( expr )
 
        stmt_expr := expr ;
    
        stmt_assign := NAME = expr ;
    
        stmt_if := IF block 
                |  IF block ELSE block 
                |  IF block ELSE stmt_if
    
        stmt_while := WHILE expr { stmts }

        stmt_func_def := FUNC NAME (  block 

    Todo: 
    - Semantic Pass - check variable, function, field names, check binary/unary ops
    - VM - Serialize Bytecode to external file. Execute compiled bytecode from file
    - VM - Serialize debug map to file. Link from bytecode to source file. 
    - VM - Print Stack trace on error. Link back to source file if running uncompiled bytecode
    - Type - Support boolean types
    - Native - Support return value in function callbacks
    - Convert to C
    
    Bugs:
    - VM - Semantic pass to determine if a function call needs to discard the return value (i.e. return not push value on stack)
*/

#include <functional>
#include <list>
#include <string>
#include <vector>
#include <unordered_map>

struct shl_value;
struct shl_object;

using shl_string = std::string;

template <class type>
using shl_array = std::vector<type>;

template <class key, class type>
using shl_map = std::unordered_map<key, type>;

typedef void(shl_error_callback)(const char* /*msg*/);
typedef void(shl_function_callback)(const shl_array<shl_value*>& /*args*/);

enum shl_type
{
    SHL_BOOL,
    SHL_INT, 
    SHL_FLOAT, 
    SHL_STRING, 
    SHL_OBJECT,
    SHL_NONE
};

const char* shl_type_labels[] = 
{
    "bool", "int", "float", "string", "object", "none"
};

struct shl_value
{
    shl_type type;
    union 
    {
        bool b;
        int i; 
        float f;
        const char* str;
        shl_object* obj;
    };
};

struct shl_attribute
{
    shl_string id;
    shl_value value;
};

struct shl_object
{
    shl_array<shl_attribute> attribs;
};

struct shl_environment
{
    shl_map<shl_string, shl_function_callback*> functions;
    shl_error_callback* error_callback = nullptr;
};

void shl_register(shl_environment& env, const char* function_id, shl_function_callback* callback); //TODO parse the func types ? 
void shl_unregister(shl_environment& env, const char* function_id);

void shl_execute(shl_environment& env, const char* filename);
void shl_evaluate(shl_environment& env, const char* code);

shl_string shl_value_to_string(const shl_value* value);
bool shl_value_to_bool(const shl_value* value);

inline bool shl_value_set_bool(shl_value* value,  bool data);
inline bool shl_value_set_int(shl_value* value,   int data);
inline bool shl_value_set_float(shl_value* value, float data);

inline bool shl_value_add(shl_value* result, shl_value* lhs, shl_value* rhs);
inline bool shl_value_sub(shl_value* result, shl_value* lhs, shl_value* rhs);
inline bool shl_value_mul(shl_value* result, shl_value* lhs, shl_value* rhs);
inline bool shl_value_div(shl_value* result, shl_value* lhs, shl_value* rhs);
inline bool shl_value_pow(shl_value* result, shl_value* lhs, shl_value* rhs);
inline bool shl_value_mod(shl_value* result, shl_value* lhs, shl_value* rhs);
inline bool shl_value_lt(shl_value* result, shl_value* lhs, shl_value* rhs);
inline bool shl_value_lte(shl_value* result, shl_value* lhs, shl_value* rhs);
inline bool shl_value_gt(shl_value* result, shl_value* lhs, shl_value* rhs);
inline bool shl_value_gte(shl_value* result, shl_value* lhs, shl_value* rhs);
inline bool shl_value_eq(shl_value* result, shl_value* lhs, shl_value* rhs);
inline bool shl_value_neq(shl_value* result, shl_value* lhs, shl_value* rhs);
inline bool shl_value_approxeq(shl_value* result, shl_value* lhs, shl_value* rhs);

#endif //__SHL_HEADER__

// -------------------------------------- Implementation Details ---------------------------------------// 

#if defined(SHL_IMPLEMENTATION)

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>

#define SHL_DEBUG 1
#define SHL_VM_DEBUG_TOP 0

#ifndef SHL_VM_APPROX_THRESHOLD
    #define SHL_VM_APPROX_THRESHOLD 0.0000001
#endif//SHL_VM_APPROX_THRESHOLD 

#ifndef SHL_VM_OPERAND_LEN
    #define SHL_VM_OPERAND_LEN 8
#endif//SHL_TOKEN_BUFFER_LEN 

#ifndef SHL_TOKEN_BUFFER_LEN
    #define SHL_TOKEN_BUFFER_LEN 32
#endif//SHL_TOKEN_BUFFER_LEN 

#ifndef SHL_BYTECODE_CHUNK_SIZE
    #define SHL_BYTECODE_CHUNK_SIZE (1024 * 2)
#endif//SHL_BYTECODE_CHUNK_SIZE

#ifndef SHL_STACK_SIZE
    #define SHL_STACK_SIZE (4096 * 4)
#endif//SHL_STACK_SIZE

#ifndef SHL_COMMENT_SYMBOL
    #define SHL_COMMENT_SYMBOL '#'
#endif//SHL_COMMENT_SYMBOL 

#ifndef SHL_LOG_PRELUDE
    #define SHL_LOG_PRELUDE "[SHL]"
#endif//SHL_LOG_PRELUDE 

#ifdef SHL_LOG_VERBOSE
    #ifdef WIN32
        #define SHL_LOG_TRACE() fprintf(stderr, "[%s:%d]", __FUNCTION__, __LINE__);
    #else
        #define SHL_LOG_TRACE() fprintf(stderr, "[%s:%d]", __func__, __LINE__);
    #endif  
#else 
    #define SHL_LOG_TRACE()   
#endif

#define SHL_LOG_ERROR(env, ...)                        \
{                                                      \
    SHL_LOG_TRACE()                                    \
    fprintf(stderr, SHL_LOG_PRELUDE "Error: ");        \
    fprintf(stderr, __VA_ARGS__);                      \
    fprintf(stderr, "\n");                             \
                                                                            \
    char buffer[4096];                                                      \
    int len = snprintf(buffer, sizeof(buffer), SHL_LOG_PRELUDE "Error: ");  \
    len = snprintf(buffer + len, sizeof(buffer) - len, __VA_ARGS__);        \
    if(env.error_callback) env.error_callback(buffer);                      \
}

// -------------------------------------- Lexer  ---------------------------------------// 
// Static token details
struct shl_token_detail
{
    const char* label;    // the string representation, used for visualization and debugging
    char prec;            // if the token is an operator, this is the precedence (note: higher values take precendece)
    int  argc;            // if the token is an operator, this is the number of operands
    bool capture;         // if non-zero, the token will contain the source string value (i.e. integer and string literals)
    const char* keyword;  // if non-null, the token must match the provided keyword
};

#define SHL_TOKEN_LIST                                \
    /* State */                                       \
    SHL_TOKEN(NONE,           0, 0, 0, nullptr)       \
    SHL_TOKEN(ERR,	          0, 0, 0, nullptr)       \
    SHL_TOKEN(END,            0, 0, 0, nullptr)       \
    /* Symbols */			                          \
    SHL_TOKEN(DOT,            0, 0, 0, nullptr)       \
    SHL_TOKEN(COMMA,          0, 0, 0, nullptr)       \
    SHL_TOKEN(COLON,          0, 0, 0, nullptr)       \
    SHL_TOKEN(SEMICOLON,      0, 0, 0, nullptr)       \
    SHL_TOKEN(LPAREN,         0, 0, 0, nullptr)       \
    SHL_TOKEN(RPAREN,         0, 0, 0, nullptr)       \
    SHL_TOKEN(LBRACKET,       0, 0, 0, nullptr)       \
    SHL_TOKEN(RBRACKET,       0, 0, 0, nullptr)       \
    SHL_TOKEN(LBRACE,         0, 0, 0, nullptr)       \
    SHL_TOKEN(RBRACE,         0, 0, 0, nullptr)       \
    /* Operators - Arithmetic */                      \
    SHL_TOKEN(AND,            1, 2, 0, nullptr)       \
    SHL_TOKEN(OR,             1, 2, 0, nullptr)       \
    SHL_TOKEN(ADD,            2, 2, 0, nullptr)       \
    SHL_TOKEN(ADD_EQ,         1, 2, 0, nullptr)       \
    SHL_TOKEN(SUB,            2, 2, 0, nullptr)       \
    SHL_TOKEN(SUB_EQ,         1, 2, 0, nullptr)       \
    SHL_TOKEN(MUL,            3, 2, 0, nullptr)       \
    SHL_TOKEN(MUL_EQ,         1, 2, 0, nullptr)       \
    SHL_TOKEN(DIV,            3, 2, 0, nullptr)       \
    SHL_TOKEN(DIV_EQ,         1, 2, 0, nullptr)       \
    SHL_TOKEN(POW,            3, 2, 0, nullptr)       \
    SHL_TOKEN(POW_EQ,         1, 2, 0, nullptr)       \
    SHL_TOKEN(MOD,            3, 2, 0, nullptr)       \
    SHL_TOKEN(MOD_EQ,         1, 2, 0, nullptr)       \
    /* Operators - Boolean */                         \
    SHL_TOKEN(EQ,             1, 2, 0, nullptr)       \
    SHL_TOKEN(LT,             2, 2, 0, nullptr)       \
    SHL_TOKEN(GT,             2, 2, 0, nullptr)       \
    SHL_TOKEN(LT_EQ,          2, 2, 0, nullptr)       \
    SHL_TOKEN(GT_EQ,          2, 2, 0, nullptr)       \
    SHL_TOKEN(NOT,            3, 1, 0, nullptr)       \
    SHL_TOKEN(NOT_EQ,         2, 2, 0, nullptr)       \
    SHL_TOKEN(APPROX_EQ,      1, 2, 0, nullptr)       \
    /* Literals */                                    \
    SHL_TOKEN(DECIMAL,        0, 0, 1, nullptr)       \
    SHL_TOKEN(HEXIDECIMAL,    0, 0, 1, nullptr)       \
    SHL_TOKEN(BINARY,         0, 0, 1, nullptr)       \
    SHL_TOKEN(FLOAT,          0, 0, 1, nullptr)       \
    SHL_TOKEN(STRING,         0, 0, 1, nullptr)       \
    /* Misc */                                        \
    SHL_TOKEN(NAME,           0, 0, 1, nullptr)       \
    SHL_TOKEN(ASSIGN,         0, 0, 0, nullptr)       \
    /* Keywords */                                    \
    SHL_TOKEN(IF,             0, 0, 0, "if")          \
    SHL_TOKEN(ELSE,           0, 0, 0, "else")        \
    SHL_TOKEN(IN,             0, 0, 0, "in")          \
    SHL_TOKEN(FOR,            0, 0, 0, "for")         \
    SHL_TOKEN(WHILE,          0, 0, 0, "while")       \
    SHL_TOKEN(FUNC,           0, 0, 0, "func")        \
    SHL_TOKEN(RETURN,         0, 0, 0, "return")      \
    SHL_TOKEN(TRUE,           0, 0, 0, "true")        \
    SHL_TOKEN(FALSE,          0, 0, 0, "false")

// Token identifier. 
enum shl_token_id : uint8_t 
{
#define SHL_TOKEN(id, ...) SHL_TOKEN_##id,
    SHL_TOKEN_LIST
#undef SHL_TOKEN
    SHL_TOKEN_COUNT
};

// All token type info. Types map from id to type info
static shl_token_detail shl_token_details[(int)SHL_TOKEN_COUNT] = 
{
#define SHL_TOKEN(id, ...) { #id, __VA_ARGS__},
    SHL_TOKEN_LIST
#undef SHL_TOKEN
};

#undef SHL_TOKEN_LIST

// Token instance
struct shl_token
{
    shl_token_id id;
    const shl_token_detail* detail; 
    shl_string data;
    int line;
    int col;
};

// Lexer state
struct shl_lexer
{
    shl_token prev;
    shl_token curr;
    shl_token next;

    std::istream* input = nullptr;
    shl_string inputname;

    int line, col;
    int prev_line, prev_col;
    std::streampos pos_start;

    char token_buffer[SHL_TOKEN_BUFFER_LEN];
};

#define SHL_LEXER_ERROR(env, lexer, token, ...)              \
{                                                            \
    token.id = SHL_TOKEN_ERR;                                \
    SHL_LOG_ERROR(env, "%s(%d,%d):",                         \
        lexer.inputname.c_str(), lexer.line+1, lexer.col+1); \
    SHL_LOG_ERROR(env, __VA_ARGS__);                         \
}

char shl_lexer_get(shl_lexer& lexer)
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

char shl_lexer_peek(shl_lexer& lexer)
{
    return lexer.input->peek();
}

char shl_lexer_unget(shl_lexer& lexer)
{
    lexer.input->unget();

    lexer.line = lexer.prev_line;
    lexer.col = lexer.prev_col;

    return shl_lexer_peek(lexer);
}

void shl_lexer_start_tracking(shl_lexer& lexer)
{
    lexer.pos_start = lexer.input->tellg();
}

void shl_lexer_end_tracking(shl_lexer& lexer, shl_string& s)
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

bool shl_lexer_tokenize_string(shl_environment& env, shl_lexer& lexer, shl_token& token)
{
    char c = shl_lexer_get(lexer);
    if (c != '\"')
    {
        shl_lexer_unget(lexer);
        return false;
    }

    token.id = SHL_TOKEN_STRING;
    token.data.clear();

    c = shl_lexer_get(lexer);

    while (c != '\"')
    {
        if (c == EOF)
        {
            SHL_LEXER_ERROR(env, lexer, token, "string literal missing closing \"");
            return false;
        }

        if (c == '\\')
        {
            // handle escaped chars
            c = shl_lexer_get(lexer);
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
                SHL_LEXER_ERROR(env, lexer, token, "invalid escape character \\%c", c);
                while (c != '\"')
                {
                    if (c == EOF)
                        break;
                    c = shl_lexer_get(lexer);
                }
                return false;
            }
        }
        else
        {
            token.data.push_back(c);
        }

        c = shl_lexer_get(lexer);
    }

    return true;
}

bool shl_lexer_tokenize_symbol(shl_environment& env, shl_lexer& lexer, shl_token& token)
{
    (void)env;

    shl_token_id id = SHL_TOKEN_ERR;

    char c = shl_lexer_get(lexer);
    switch (c)
    {
    case '.': id = SHL_TOKEN_DOT;       break;
    case ',': id = SHL_TOKEN_COMMA;     break;
    case ':': id = SHL_TOKEN_COLON;     break;
    case ';': id = SHL_TOKEN_SEMICOLON; break;
    case '(': id = SHL_TOKEN_LPAREN;    break;
    case ')': id = SHL_TOKEN_RPAREN;    break;
    case '[': id = SHL_TOKEN_LBRACKET;  break;
    case ']': id = SHL_TOKEN_RBRACKET;  break;
    case '{': id = SHL_TOKEN_LBRACE;    break;
    case '}': id = SHL_TOKEN_RBRACE;    break;
    case '&': id = SHL_TOKEN_AND;       break;
    case '|': id = SHL_TOKEN_OR;        break;
    case '+':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_ADD_EQ;
        else
            id = SHL_TOKEN_ADD;
        break;
    case '-':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_SUB_EQ;
        else
            id = SHL_TOKEN_SUB;
        break;
    case '*':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_MUL_EQ;
        else
            id = SHL_TOKEN_MUL;
        break;
    case '/':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_DIV_EQ;
        else
            id = SHL_TOKEN_DIV;
        break;
    case '^':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_POW_EQ;
        else
            id = SHL_TOKEN_POW;
        break;
    case '%':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_MOD_EQ;
        else
            id = SHL_TOKEN_MOD;
        break;
    case '<':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_LT_EQ;
        else
            id = SHL_TOKEN_LT;
        break;
    case '>':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_GT_EQ;
        else
            id = SHL_TOKEN_GT;
        break;
    case '=':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_EQ;
        else
            id = SHL_TOKEN_ASSIGN;
        break;
    case '!':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_NOT_EQ;
        else
            id = SHL_TOKEN_NOT;
        break;
    case '~':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_APPROX_EQ;
        break;
    }

    if (id == SHL_TOKEN_ERR)
    {
        shl_lexer_unget(lexer);
        return false;
    }

    token.id = id;
    return true;
}

bool shl_lexer_tokenize_name(shl_environment& env, shl_lexer& lexer, shl_token& token)
{
    (void)env;

    shl_lexer_start_tracking(lexer);

    char c = shl_lexer_get(lexer);
    if (c != '_' && !isalpha(c))
    {
        shl_lexer_unget(lexer);
        return false;
    }
    
    while (c == '_' || isalnum(c))
        c = shl_lexer_get(lexer);
    shl_lexer_unget(lexer);

    shl_string name;
    shl_lexer_end_tracking(lexer, name);

    // find token id for keyword
    shl_token_id id = SHL_TOKEN_NAME;
    for (size_t i = 0; i < (size_t)SHL_TOKEN_COUNT; ++i)
    {
        const shl_token_detail& detail = shl_token_details[i];
        if (detail.keyword && detail.keyword == name)
        {
            id = (shl_token_id)i;
            break;
        }
    }
    
    token.id = id;
    token.data = std::move(name);

    return true;
}

bool shl_lexer_tokenize_number(shl_environment& env, shl_lexer& lexer, shl_token& token)
{    
    shl_lexer_start_tracking(lexer);

    char c = shl_lexer_get(lexer);
    if (c != '.' && !isdigit(c) && c != '+' && c != '-')
    {
        shl_lexer_unget(lexer);
        return false;
    } 

    shl_token_id id = SHL_TOKEN_ERR;

    if (c == '0' && shl_lexer_peek(lexer) == 'x')
    {
        id = SHL_TOKEN_HEXIDECIMAL;

        c = shl_lexer_get(lexer); //eat 'x'
        c = shl_lexer_get(lexer);

        while (isalnum(c))
        {
            if (!isdigit(c) && !((c >='a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                id = SHL_TOKEN_ERR;

            c = shl_lexer_get(lexer);
        }
        shl_lexer_unget(lexer);
    }
    else if (c == '0' && shl_lexer_peek(lexer) == 'b')
    {
        id = SHL_TOKEN_BINARY;

        c = shl_lexer_get(lexer); // eat 'b'
        c = shl_lexer_get(lexer);

        while (isdigit(c))
        {
            if(c != '0' && c != '1')
                id = SHL_TOKEN_ERR;

            c = shl_lexer_get(lexer);
        }
        shl_lexer_unget(lexer);
    }
    else
    {
        if (c == '+' || c == '-')
        {
            c = shl_lexer_peek(lexer);
            if (c != '.' && !isdigit(c))
            {
                shl_lexer_unget(lexer);
                return false;
            }
            c = shl_lexer_get(lexer);
        }

        if (c == '.')
            id = SHL_TOKEN_FLOAT;
        else 
            id = SHL_TOKEN_DECIMAL;

        bool dot = false;
        while (c == '.' || isdigit(c))
        {
            if (c == '.')
            {
                if (dot)
                    id = SHL_TOKEN_ERR;
                else
                    id = SHL_TOKEN_FLOAT;

                dot = true;
            }
            c = shl_lexer_get(lexer);
        }
        shl_lexer_unget(lexer);
    }

    shl_string str;
    shl_lexer_end_tracking(lexer, str);

    if(id == SHL_TOKEN_ERR)
    {
        SHL_LEXER_ERROR(env, lexer, token, "invalid numeric format %s", str.c_str());
        return false;
    }
    
    token.id = id;
    token.data = std::move(str);
    return true;
}

shl_token shl_lexer_tokenize(shl_environment& env, shl_lexer& lexer)
{
    shl_token token;

    // if file is not open, or already at then end. return invalid token
    if (lexer.input == nullptr)
    {
        token.id = SHL_TOKEN_NONE;
        token.detail = &shl_token_details[(int)token.id];
        return token;
    }

    if (shl_lexer_peek(lexer) == EOF)
    {
        token.id = SHL_TOKEN_END;
        token.detail = &shl_token_details[(int)token.id];
        return token;
    }

    char c = shl_lexer_peek(lexer);

    // skip whitespace
    if (isspace(c))
    {
        while (isspace(c))
            c = shl_lexer_get(lexer);
        shl_lexer_unget(lexer);
    }

    // skip comments
    while(c == SHL_COMMENT_SYMBOL)
    {        
        while (c != EOF)
        {
            c = shl_lexer_get(lexer);

            if (c == '\n')
            {
                c = shl_lexer_peek(lexer);
                break;
            }
        }

        // skip whitespace
        if (isspace(c))
        {
            while (isspace(c))
                c = shl_lexer_get(lexer);
            shl_lexer_unget(lexer);
        }
    }

    // handle eof
    if (c == EOF)
    {
        token.id = SHL_TOKEN_END;
        token.detail = &shl_token_details[(int)token.id];
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
            case SHL_TOKEN_BINARY:
            case SHL_TOKEN_HEXIDECIMAL:
            case SHL_TOKEN_FLOAT:
            case SHL_TOKEN_DECIMAL:
                allow_sign = false;
                break;
            default:
                break;
        }
        if (!allow_sign || !shl_lexer_tokenize_number(env, lexer, token))
            shl_lexer_tokenize_symbol(env, lexer, token);
        break;
    }
    case '\"':
        shl_lexer_tokenize_string(env, lexer, token);
        break;
    default:
        if (shl_lexer_tokenize_symbol(env, lexer, token))
            break;
        if(shl_lexer_tokenize_name(env, lexer, token))
            break;
        if (shl_lexer_tokenize_number(env, lexer, token))
            break;

        SHL_LEXER_ERROR(env, lexer, token, "invalid character %c", c);
        break;
    }

    token.detail = &shl_token_details[(int)token.id];
    return token;
}

bool shl_lexer_move(shl_environment& env, shl_lexer& lexer)
{
    if (lexer.next.id == SHL_TOKEN_NONE)
        lexer.next = shl_lexer_tokenize(env, lexer);        

    lexer.prev = lexer.curr; 
    lexer.curr = lexer.next;
    lexer.next = shl_lexer_tokenize(env, lexer);

    return lexer.curr.id != SHL_TOKEN_NONE;
}

void shl_lexer_close(shl_lexer& lexer)
{
    if (lexer.input)
        delete lexer.input;

    lexer.input = nullptr;
    lexer.line = 0;
    lexer.col = 0;
    lexer.prev_line = 0;
    lexer.prev_col = 0;
    lexer.pos_start = 0;

    lexer.prev = shl_token();
    lexer.curr = shl_token();
    lexer.next = shl_token();
}

bool shl_lexer_open(shl_environment& env, shl_lexer& lexer, const char* code)
{
    shl_lexer_close(lexer);

    std::istringstream* iss = new std::istringstream(code, std::fstream::in);
    if (iss == nullptr || !iss->good())
    {
        SHL_LOG_ERROR(env,"Lexer failed to open code");
        if(iss) 
            delete iss;
        return false;
    }

    lexer.input = iss;
    lexer.inputname = "code";

    return shl_lexer_move(env, lexer);
}

bool shl_lexer_open_file(shl_environment& env, shl_lexer& lexer, const char* filename)
{
    shl_lexer_close(lexer);

    std::fstream* file = new std::fstream(filename, std::fstream::in);
    if (file == nullptr || !file->is_open())
    {
        SHL_LOG_ERROR(env,"Lexer failed to open file %s", filename);
        if (file)
            delete file;
        return false;
    }

    lexer.input = file;
    lexer.inputname = filename;

    return shl_lexer_move(env, lexer);
}

// -------------------------------------- Parser / Abstract Syntax Tree ---------------------------------------// 
#define SHL_PARSE_ERROR(env, lexer,  ...)\
{\
    SHL_LOG_ERROR(env, "Syntax error %s(%d,%d)", lexer.inputname.c_str(), lexer.line+1, lexer.col+1);\
    SHL_LOG_ERROR(env, __VA_ARGS__);\
}

enum shl_ast_id : uint8_t
{
    SHL_AST_ROOT,
    SHL_AST_BLOCK, 
    SHL_AST_STMT_ASSIGN, 
    SHL_AST_STMT_IF,
    SHL_AST_STMT_IF_ELSE,
    SHL_AST_STMT_WHILE,
    SHL_AST_LITERAL, 
    SHL_AST_VARIABLE, 
    SHL_AST_UNARY_OP, 
    SHL_AST_BINARY_OP, 
    SHL_AST_FUNC_CALL,
    SHL_AST_FUNC_DEF, 
    SHL_AST_PARAM_LIST,
    SHL_AST_PARAM,
    SHL_AST_RETURN,
};

struct shl_ast
{
    shl_ast_id id;
    shl_token token;
    shl_array<shl_ast*> children;
};

shl_ast* shl_parse_value(shl_environment& env, shl_lexer& lexer); 
shl_ast* shl_parse_block(shl_environment& env, shl_lexer& lexer);

shl_ast* shl_ast_new(shl_ast_id id, const shl_token& token = shl_token())
{
    shl_ast* node = new shl_ast();
    node->id = id;
    node->token = token;
    return node;
}

void shl_ast_delete(shl_ast* node)
{
    if (node == nullptr)
        return;
    for(shl_ast* child : node->children)
        shl_ast_delete(child);
    delete node;
}

bool shl_parse_expr_pop(shl_environment& env, shl_lexer& lexer, shl_array<shl_token>& op_stack, shl_array<shl_ast*>& expr_stack)
{
    shl_token next_op = op_stack.back();
    op_stack.pop_back();

    const int op_argc = (size_t)next_op.detail->argc;
    assert(op_argc == 1 || op_argc == 2); // Only supported operator types

    if(expr_stack.size() < (size_t)op_argc)
    {
        while(expr_stack.size() > 0)
        {
            shl_ast_delete(expr_stack.back());
            expr_stack.pop_back();
        }
        SHL_PARSE_ERROR(env, lexer,  "Invalid number of arguments to operator %s", next_op.detail->label);
        return false;
    }

    // Push binary op onto stack
    shl_ast_id id = (op_argc == 2) ? SHL_AST_BINARY_OP : SHL_AST_UNARY_OP;
    shl_ast* binaryop = shl_ast_new(id, next_op);
    binaryop->children.resize(op_argc);

    for(int i = 0; i < op_argc; ++i)
    {
        shl_ast* expr = expr_stack.back();
        expr_stack.pop_back();
        binaryop->children[(op_argc-1) - i] = expr; // add in reverse
    }

    expr_stack.push_back(binaryop);
    return true;
}

shl_ast* shl_parse_expr(shl_environment& env, shl_lexer& lexer)
{
    // Shunting yard algorithm
    shl_array<shl_token> op_stack;
    shl_array<shl_ast*> expr_stack;
    
    while(lexer.curr.id != SHL_TOKEN_SEMICOLON)
    {
        shl_token op = lexer.curr;
        if(op.detail->prec > 0)
        {
            // left associate by default (for right, <= becomes <)
            while(op_stack.size() && op_stack.back().detail->prec >= op.detail->prec)
            {
                if(!shl_parse_expr_pop(env, lexer,  op_stack, expr_stack))
                    return nullptr;
            }

            op_stack.push_back(op);
            shl_lexer_move(env, lexer);
        }
        else
        {
            shl_ast* value = shl_parse_value(env, lexer);
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
        if(!shl_parse_expr_pop(env, lexer,  op_stack, expr_stack))
            return nullptr;
    }

    // Not a valid expression. Either malformed or missing semicolon 
    if(expr_stack.size() == 0 || expr_stack.size() > 1)
    {
        while(expr_stack.size() > 0)
        {
            shl_ast_delete(expr_stack.back());
            expr_stack.pop_back();
        }
        SHL_PARSE_ERROR(env, lexer,  "Invalid expression syntax");
        return nullptr;
    }

    return expr_stack.back();
}

shl_ast* shl_parse_funccall(shl_environment& env, shl_lexer& lexer)
{
    shl_token name_token = lexer.curr;
    if (name_token.id != SHL_TOKEN_NAME)
        return nullptr;

    if (lexer.next.id != SHL_TOKEN_LPAREN)
        return nullptr;

    shl_lexer_move(env, lexer); // eat NAME
    shl_lexer_move(env, lexer); // eat LPAREN

    shl_array<shl_ast*> args;
    if (shl_ast* expr = shl_parse_expr(env, lexer))
    {
        args.push_back(expr);

        while (expr && lexer.curr.id == SHL_TOKEN_COMMA)
        {
            shl_lexer_move(env, lexer); // eat COMMA

            if((expr = shl_parse_expr(env, lexer)))
                args.push_back(expr);
        }
    }

    if (lexer.curr.id != SHL_TOKEN_RPAREN)
    {
        SHL_PARSE_ERROR(env, lexer,  "Function call missing closing parentheses");
        for(shl_ast* arg : args)
            delete arg;
        return nullptr;
    }

    shl_lexer_move(env, lexer); // eat RPAREN

    shl_ast* funccall = shl_ast_new(SHL_AST_FUNC_CALL, name_token);
    funccall->children = std::move(args);
    return funccall;
}

shl_ast* shl_parse_value(shl_environment& env, shl_lexer& lexer)
{
    shl_token token = lexer.curr;
    switch (token.id)
    {
    case SHL_TOKEN_NAME:
    {   
        if (shl_ast* funccall = shl_parse_funccall(env, lexer))
            return funccall;

        shl_lexer_move(env, lexer);
        shl_ast* var = shl_ast_new(SHL_AST_VARIABLE, token);
        return var;
    }
    case SHL_TOKEN_DECIMAL:
    case SHL_TOKEN_HEXIDECIMAL:
    case SHL_TOKEN_BINARY:
    case SHL_TOKEN_FLOAT:
    case SHL_TOKEN_STRING:
    case SHL_TOKEN_TRUE:
    case SHL_TOKEN_FALSE:
    {
        shl_lexer_move(env, lexer);
        shl_ast* literal = shl_ast_new(SHL_AST_LITERAL, token);
        literal->id = SHL_AST_LITERAL;
        literal->token = token;
        return literal;
    }
    case SHL_TOKEN_LPAREN:
    {
        shl_lexer_move(env, lexer); // eat LPAREN
        shl_ast* expr = shl_parse_expr(env, lexer);
        if (lexer.curr.id != SHL_TOKEN_RPAREN)
        {
            SHL_PARSE_ERROR(env, lexer,  "Expression missing closing parentheses");
            shl_ast_delete(expr);    
            return nullptr;
        }
        shl_lexer_move(env, lexer); // eat RPAREN
        return expr;
    }
    default: 
    break;
    }
    return nullptr;
}

shl_ast* shl_parse_stmt_expr(shl_environment& env, shl_lexer& lexer)
{
    shl_ast* stmt_expr = shl_parse_expr(env, lexer);
    if (stmt_expr == nullptr)
        return nullptr;
    
    if (lexer.curr.id != SHL_TOKEN_SEMICOLON)
    {
        shl_ast_delete(stmt_expr);
        SHL_PARSE_ERROR(env, lexer, "Missing semicolon at end of expression");
        return nullptr;
    }

    shl_lexer_move(env, lexer); // eat SEMICOLON
    
    return stmt_expr;
}

shl_ast* shl_parse_stmt_assign(shl_environment& env, shl_lexer& lexer)
{
    shl_token name_token = lexer.curr;
    shl_token eq_token = lexer.next;
    if (name_token.id != SHL_TOKEN_NAME || eq_token.id != SHL_TOKEN_ASSIGN)
        return nullptr;

    shl_lexer_move(env, lexer); // eat NAME
    shl_lexer_move(env, lexer); // eat ASSIGN

    shl_ast* expr = shl_parse_expr(env, lexer);
    if (expr == nullptr)
    {
        SHL_PARSE_ERROR(env, lexer, "Assignment expected expression after \"=\"");
        return nullptr;
    }

    if (lexer.curr.id != SHL_TOKEN_SEMICOLON)
    {
        shl_ast_delete(expr);
        SHL_PARSE_ERROR(env, lexer, "Missing semicolon at end of expression");
        return nullptr;
    }

    shl_lexer_move(env, lexer); // eat SEMICOLON

    shl_ast* stmt_assign = shl_ast_new(SHL_AST_STMT_ASSIGN, name_token);
    stmt_assign->children.push_back(expr);
    return stmt_assign;
}

shl_ast* shl_parse_stmt_if(shl_environment& env, shl_lexer& lexer)
{
    if (lexer.curr.id != SHL_TOKEN_IF)
        return nullptr;

    shl_lexer_move(env, lexer); // eat IF

    shl_ast* expr = shl_parse_expr(env, lexer);
    if(expr == nullptr)
    {
        SHL_PARSE_ERROR(env, lexer, "If statement missing expression");
        return nullptr;      
    }

    shl_ast* block = shl_parse_block(env, lexer);
    if (block == nullptr)
    {
        shl_ast_delete(expr);
        SHL_PARSE_ERROR(env, lexer, "If statement missing block");
        return nullptr;
    }

    // Parse else 
    if (lexer.curr.id == SHL_TOKEN_ELSE)
    {
        shl_lexer_move(env, lexer); // eat ELSE

        shl_ast* if_else_stmt = shl_ast_new(SHL_AST_STMT_IF_ELSE);
        if_else_stmt->children.push_back(expr);
        if_else_stmt->children.push_back(block);

        // Handling else if becomes else { if ... }
        if (lexer.curr.id == SHL_TOKEN_IF)
        {
            shl_ast* trailing_if_stmt = shl_parse_stmt_if(env, lexer);
            if (trailing_if_stmt == nullptr)
            {
                shl_ast_delete(if_else_stmt);
                return nullptr;
            }
            if_else_stmt->children.push_back(trailing_if_stmt);
        }
        else
        {
            shl_ast* else_block = shl_parse_block(env, lexer);
            if (else_block == nullptr)
            {
                shl_ast_delete(if_else_stmt);
                SHL_PARSE_ERROR(env, lexer, "If Else statement missing block");
                return nullptr;
            }
            if_else_stmt->children.push_back(else_block);
        }

        return if_else_stmt;
    }

    shl_ast* if_stmt = shl_ast_new(SHL_AST_STMT_IF);
    if_stmt->children.push_back(expr);
    if_stmt->children.push_back(block);
    return if_stmt;
}

shl_ast* shl_parse_stmt_while(shl_environment& env, shl_lexer& lexer)
{
    if (lexer.curr.id != SHL_TOKEN_WHILE)
        return nullptr;

    shl_lexer_move(env, lexer); // eat WHILE

    shl_ast* expr = shl_parse_expr(env, lexer);
    if (expr == nullptr)
    {
        SHL_PARSE_ERROR(env, lexer, "While statement missing expression");
        return nullptr;
    }

    shl_ast* block = shl_parse_block(env, lexer);
    if (block == nullptr)
    {
        shl_ast_delete(expr);
        SHL_PARSE_ERROR(env, lexer, "While statement missing block");
        return nullptr;
    }

    shl_ast* while_stmt = shl_ast_new(SHL_AST_STMT_WHILE);
    while_stmt->children.push_back(expr);
    while_stmt->children.push_back(block);

    return while_stmt;
}

shl_ast* shl_parse_param_list(shl_environment& env, shl_lexer& lexer)
{
    if (lexer.curr.id != SHL_TOKEN_LPAREN)
    {
        SHL_PARSE_ERROR(env, lexer, "Missing opening parentheses in function parameter list");
        return nullptr;
    }

    shl_lexer_move(env, lexer); // eat LPAREN

    shl_ast* param_list = shl_ast_new(SHL_AST_PARAM_LIST);

    if (lexer.curr.id == SHL_TOKEN_NAME)
    {
        shl_ast* param = shl_ast_new(SHL_AST_PARAM);
        param->token = lexer.curr;
        param_list->children.push_back(param);

        shl_lexer_move(env, lexer); // eat NAME

        while (lexer.curr.id == SHL_TOKEN_COMMA)
        {
            shl_lexer_move(env, lexer); // eat COMMA

            if (lexer.curr.id != SHL_TOKEN_NAME)
            {
                SHL_PARSE_ERROR(env, lexer, "Invalid function parameter. Expected parameter name");
                shl_ast_delete(param_list);
                return nullptr;
            }

            shl_ast* param = shl_ast_new(SHL_AST_PARAM);
            param->token = lexer.curr;
            param_list->children.push_back(param);

            shl_lexer_move(env, lexer); // eat NAME
        }
    }

    if (lexer.curr.id != SHL_TOKEN_RPAREN)
    {
        SHL_PARSE_ERROR(env, lexer, "Missing closing parentheses in function parameter list");
        shl_ast_delete(param_list);
        return nullptr;
    }

    shl_lexer_move(env, lexer); // eat RPAREN

    return param_list;
}

shl_ast* shl_parse_stmt_func(shl_environment& env, shl_lexer& lexer)
{
    if (lexer.curr.id != SHL_TOKEN_FUNC)
        return nullptr;

    shl_lexer_move(env, lexer); // eat FUNC

    if (lexer.curr.id != SHL_TOKEN_NAME)
    {
        SHL_PARSE_ERROR(env, lexer, "Missing name in function definition");
        return nullptr;
    }

    const shl_token func_name = lexer.curr;

    shl_lexer_move(env, lexer); // eat NAME

    shl_ast* param_list = shl_parse_param_list(env, lexer);
    if (param_list == nullptr)
        return nullptr;

    shl_ast* block = shl_parse_block(env, lexer);
    if (block == nullptr)
        return nullptr;

    shl_ast* func_def = shl_ast_new(SHL_AST_FUNC_DEF);
    func_def->token = func_name;
    func_def->children.push_back(param_list);
    func_def->children.push_back(block);

    return func_def;
}

shl_ast* shl_parse_stmt_return(shl_environment& env, shl_lexer& lexer)
{
    if (lexer.curr.id != SHL_TOKEN_RETURN)
        return nullptr;

    shl_lexer_move(env, lexer); // eat RETURN

    shl_ast* return_stmt = shl_ast_new(SHL_AST_RETURN);

    shl_ast* expr = shl_parse_expr(env, lexer);
    if (expr != nullptr)
        return_stmt->children.push_back(expr);

    if (lexer.curr.id != SHL_TOKEN_SEMICOLON)
    {
        shl_ast_delete(return_stmt);
        SHL_PARSE_ERROR(env, lexer, "Missing semicolon at end of expression");
        return nullptr;
    }

    shl_lexer_move(env, lexer); // eat SEMICOLON

    return return_stmt;
}

shl_ast* shl_parse_stmt(shl_environment& env, shl_lexer& lexer)
{
    //TODO: assignment, funcdef etc..
    // Default, epxression parsing. 
    shl_ast* stmt = nullptr;

    switch (lexer.curr.id)
    {
    case SHL_TOKEN_NAME:
        stmt = shl_parse_stmt_assign(env, lexer);
        if(!stmt)
            stmt = shl_parse_stmt_expr(env, lexer);
        break;
    case SHL_TOKEN_IF:
        stmt = shl_parse_stmt_if(env, lexer);
        break;
    case SHL_TOKEN_WHILE:
        stmt = shl_parse_stmt_while(env, lexer);
        break;
    case SHL_TOKEN_FUNC:
        stmt = shl_parse_stmt_func(env, lexer);
        break;
    case SHL_TOKEN_RETURN:
        stmt = shl_parse_stmt_return(env, lexer);
        break;
    default:
        stmt = shl_parse_stmt_expr(env, lexer);
        break;
    }

    return stmt;
}

shl_ast* shl_parse_block(shl_environment& env, shl_lexer& lexer)
{
    if (lexer.curr.id != SHL_TOKEN_LBRACE)
    {
        SHL_PARSE_ERROR(env, lexer, "Block missing opening \"{\"");
        return nullptr;
    }
    shl_lexer_move(env, lexer); // eat LBRACE

    shl_array<shl_ast*> stmts;
    while(shl_ast* stmt = shl_parse_stmt(env, lexer))
        stmts.push_back(stmt);

    if(stmts.size() == 0)
        return nullptr;

    shl_ast* block = shl_ast_new(SHL_AST_BLOCK);
    block->children = std::move(stmts);


    if (lexer.curr.id != SHL_TOKEN_RBRACE)
    {
        SHL_PARSE_ERROR(env, lexer, "Block missing closing \"}\"");
        shl_ast_delete(block);
        return nullptr;
    }
    shl_lexer_move(env, lexer); // eat RBRACE

    return block;
}

shl_ast* shl_parse_root(shl_environment& env, shl_lexer& lexer)
{
    shl_ast* block = shl_ast_new(SHL_AST_BLOCK);
    while (shl_ast* stmt = shl_parse_stmt(env, lexer))
        block->children.push_back(stmt);

    if (block->children.size() == 0)
    {
        shl_ast_delete(block);
        return nullptr;
    }

    shl_ast* root = new shl_ast();
    root->id = SHL_AST_ROOT;
    root->children.push_back(block);
    return root;
}

shl_ast* shl_parse(shl_environment& env, const char* code)
{
    shl_lexer lexer;
    if(!shl_lexer_open(env, lexer, code))
        return nullptr;

    shl_ast* root = shl_parse_root(env, lexer);
    shl_lexer_close(lexer);

    return root;
}

shl_ast* shl_parse_file(shl_environment& env, const char* filename)
{
    shl_lexer lexer;
    if(!shl_lexer_open_file(env, lexer, filename))
        return nullptr;

    shl_ast* root = shl_parse_root(env, lexer);
    shl_lexer_close(lexer);

    return root;
}

// -------------------------------------- OPCODE -----------------------------------------// 

#define SHL_OPCODE_LIST        \
	SHL_OPCODE(EXIT)           \
	SHL_OPCODE(NO_OP)          \
	SHL_OPCODE(NEXT_CHUNK)     \
	SHL_OPCODE(PUSH_BOOL)      \
	SHL_OPCODE(PUSH_INT)       \
	SHL_OPCODE(PUSH_FLOAT)     \
	SHL_OPCODE(PUSH_STRING)    \
	SHL_OPCODE(PUSH_NONE)      \
	SHL_OPCODE(PUSH_LOCAL)     \
	SHL_OPCODE(LOAD_LOCAL)     \
	SHL_OPCODE(POP)            \
	SHL_OPCODE(ADD)            \
	SHL_OPCODE(SUB)            \
	SHL_OPCODE(MUL)            \
	SHL_OPCODE(DIV)            \
	SHL_OPCODE(POW)            \
	SHL_OPCODE(MOD)            \
	SHL_OPCODE(AND)            \
	SHL_OPCODE(OR)             \
	SHL_OPCODE(XOR)            \
	SHL_OPCODE(NOT)            \
	SHL_OPCODE(NEG)            \
	SHL_OPCODE(CMP)            \
	SHL_OPCODE(ABS)            \
	SHL_OPCODE(SIN)            \
	SHL_OPCODE(COS)            \
	SHL_OPCODE(ATAN)           \
	SHL_OPCODE(LN)             \
	SHL_OPCODE(SQRT)           \
	SHL_OPCODE(INC)            \
	SHL_OPCODE(DEC)            \
    SHL_OPCODE(LT)             \
	SHL_OPCODE(LTE)            \
	SHL_OPCODE(EQ)             \
	SHL_OPCODE(NEQ)            \
	SHL_OPCODE(APPROXEQ)       \
	SHL_OPCODE(GT)             \
	SHL_OPCODE(GTE)            \
	SHL_OPCODE(JUMP)           \
	SHL_OPCODE(JUMP_ZERO)      \
	SHL_OPCODE(JUMP_NZERO)     \
	SHL_OPCODE(ENTER)          \
	SHL_OPCODE(RETURN)         \
	SHL_OPCODE(CALL_EXT)       \
	SHL_OPCODE(ASSERT)         \
	SHL_OPCODE(ASSERT_POSITIVE)\
	SHL_OPCODE(ASSERT_BOUND)


// Special condition in bytecode
#define SHL_OPCODE_INVALID_ADDR -1

enum shl_opcode : uint8_t
{ 
#define SHL_OPCODE(opcode) SHL_OPCODE_##opcode,
	SHL_OPCODE_LIST
#undef SHL_OPCODE
    SHL_OPCODE_COUNT
};
static_assert(SHL_OPCODE_COUNT < 255, "SHL Opcode count too large. This will affect bytecode instruction set");

static const char* shl_opcode_labels[] =
{
#define SHL_OPCODE(opcode) #opcode,
	SHL_OPCODE_LIST
#undef SHL_OPCODE
}; 

#undef SHL_OPCODE_LIST

// -------------------------------------- IR --------------------------------------------// 

enum shl_ir_operand_type
{
    // type if operand is constant or literal
    SHL_IR_OPERAND_NONE = 0,
    SHL_IR_OPERAND_BOOL,
    SHL_IR_OPERAND_INT,
    SHL_IR_OPERAND_FLOAT,  
    SHL_IR_OPERAND_BYTES,
};

struct shl_ir_operand
{
    union
    {
        bool b;
        int i;
        float f;
        const char* str; // NOTE: weak pointer to token data
        char bytes[SHL_VM_OPERAND_LEN]; // NOTE: weak pointer to token data
    } data;

    shl_ir_operand_type type = SHL_IR_OPERAND_NONE;
};

struct shl_ir_operation
{
    shl_opcode opcode;
    shl_array<shl_ir_operand> operands; //optional parameter. will be encoded in following bytes
   
#if SHL_DEBUG
    size_t bytecode_offset;
#endif //SHL_DEBUG
};

struct shl_ir_symtable
{
    shl_map<shl_string, int> name_addr_map;
};

struct shl_ir_block
{
    shl_ir_symtable symtable;
    size_t local_offset; // used to track local variable memory on stack
};

// All the blocks within a compilation/translation unit (i.e. file, code literal)
struct shl_ir_module
{
    shl_array<shl_ir_operation> operations;
};

struct shl_ir
{		
    shl_ir_module module;
    shl_array<shl_ir_block> block_stack;
    int label_count = 0;
    size_t bytecode_count = 0;
};

inline void shl_ir_push_block(shl_ir& ir, shl_string label)
{
    shl_ir_block block;
    block.local_offset = 0;
    ir.block_stack.push_back(block);
}

inline void shl_ir_pop_block(shl_ir& ir)
{
    assert(ir.block_stack.size() > 0);
    ir.block_stack.pop_back();
}

inline shl_ir_block& shl_ir_top_block(shl_ir& ir)
{
    assert(ir.block_stack.size() > 0);
    return ir.block_stack.back();
}

inline size_t shl_ir_operand_size(const shl_ir_operand& operand)
{
    switch (operand.type)
    {
    case SHL_IR_OPERAND_NONE:
        return 0;
    case SHL_IR_OPERAND_BOOL:
        return sizeof(operand.data.b);
    case SHL_IR_OPERAND_INT:
        return sizeof(operand.data.i);
    case SHL_IR_OPERAND_FLOAT:
        return sizeof(operand.data.f);
    case SHL_IR_OPERAND_BYTES:
        return strlen(operand.data.str) + 1; // +1 for null term
    }
    return 0;
}

inline size_t shl_ir_operation_size(const shl_ir_operation& operation)
{
    size_t size = sizeof(operation.opcode);
    for(const shl_ir_operand& operand : operation.operands)
    {
        size += shl_ir_operand_size(operand);
    }
    return size;
}

inline size_t shl_ir_add_operation(shl_ir& ir, shl_opcode opcode, const shl_array<shl_ir_operand>& operands)
{
    shl_ir_operation operation;
    operation.opcode = opcode;
    operation.operands = operands;
#if SHL_DEBUG
    operation.bytecode_offset = ir.bytecode_count;
#endif //SHL_DEBUG
    ir.bytecode_count += shl_ir_operation_size(operation);

    ir.module.operations.push_back(std::move(operation));
    return ir.module.operations.size()-1;
}

inline size_t shl_ir_add_operation(shl_ir& ir, shl_opcode opcode, const shl_ir_operand& operand)
{
    shl_ir_operation operation;
    operation.opcode = opcode;
    operation.operands.push_back(operand);
#if SHL_DEBUG
    operation.bytecode_offset = ir.bytecode_count;
#endif //SHL_DEBUG
    ir.bytecode_count += shl_ir_operation_size(operation);

    ir.module.operations.push_back(std::move(operation));
    return ir.module.operations.size()-1;
}

inline size_t shl_ir_add_operation(shl_ir& ir, shl_opcode opcode)
{
    shl_ir_operand operand;
    return shl_ir_add_operation(ir, opcode, operand);
}

inline shl_ir_operation& shl_ir_get_operation(shl_ir& ir, size_t operation_index)
{
    assert(ir.block_stack.size());
    assert(operation_index < ir.module.operations.size());
    return ir.module.operations.at(operation_index);
}

inline shl_ir_operand shl_ir_operand_from_bool(bool data)
{
    shl_ir_operand operand;
    operand.type = SHL_IR_OPERAND_BOOL;
    operand.data.b = data;
    return operand;
}

inline shl_ir_operand shl_ir_operand_from_int(int data)
{
    shl_ir_operand operand;
    operand.type = SHL_IR_OPERAND_INT;
    operand.data.i = data;
    return operand;
}

inline shl_ir_operand shl_ir_operand_from_float(float data)
{
    shl_ir_operand operand;
    operand.type = SHL_IR_OPERAND_FLOAT;
    operand.data.f = data;
    return operand;
}

inline shl_ir_operand shl_ir_operand_from_str(const char* data)
{
    shl_ir_operand operand;
    operand.type = SHL_IR_OPERAND_BYTES;
    operand.data.str = data;
    return operand;
}

inline int shl_ir_set_name_addr(shl_ir& ir, const shl_string& name)
{
    shl_ir_block& block = shl_ir_top_block(ir);
    int offset = block.local_offset++;
    block.symtable.name_addr_map[name] = offset;
    return offset;
}

inline int shl_ir_get_name_addr(shl_ir& ir, const shl_string& name)
{
    for (int i = ir.block_stack.size()-1; i >=0; --i)
    {
        shl_ir_block& block = ir.block_stack.at(i);
        if (block.symtable.name_addr_map.count(name))
            return block.symtable.name_addr_map[name];
    }
    return SHL_OPCODE_INVALID_ADDR;
}

// -------------------------------------- Virtual Machine / Bytecode ----------------------------------------------// 
struct shl_vm_frame
{
    int arg_count; // number of arguments pushed onto frame. will be popped.
    // VM resume state
    int stack_offset = -1;
    const char* instruction = nullptr;
};

struct shl_vm
{
    const char* instruction;
    const char* bytecode;
 
    shl_value stack[SHL_STACK_SIZE]; 
    int stack_offset;

    // Address to return to when Ret is called. Pops all local from stack
    shl_array<shl_vm_frame> frame_stack;
};

// Used to convert values to/from bytes for constant values
union shl_vm_bytecode_value
{
    bool b;
    int i;
    float f;
    unsigned char bytes[SHL_VM_OPERAND_LEN];
};

#define SHL_VM_ERROR(env, vm, ...)     \
{                                      \
    SHL_LOG_ERROR(env, __VA_ARGS__);   \
    vm.instruction = nullptr;          \
}

#define SHL_VM_UNOP_ERROR(env, vm, arg, op)          \
{                                                    \
    if (arg != nullptr)                              \
        SHL_VM_ERROR(env, vm, "%s %s not defined",   \
            op,                                      \
            shl_type_labels[(int)arg->type]);        \
}

#define SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, op)     \
{                                                     \
    if (lhs != nullptr && rhs != nullptr)             \
        SHL_VM_ERROR(env, vm, "%s %s %s not defined", \
            shl_type_labels[(int)lhs->type],          \
            op,                                       \
            shl_type_labels[(int)rhs->type]);         \
}

inline shl_value* shl_vm_top(shl_environment& env, shl_vm& vm)
{
    return &vm.stack[vm.stack_offset];
}

inline shl_value* shl_vm_push(shl_environment& env, shl_vm& vm)
{
    ++vm.stack_offset;
    if(vm.stack_offset >= SHL_STACK_SIZE)   
    {                                              
        if(vm.instruction)
            SHL_VM_ERROR(env, vm, "Stack overflow");      
        return nullptr;                           
    }

    return shl_vm_top(env, vm);
}

inline shl_value* shl_vm_pop(shl_environment& env, shl_vm& vm)
{        
    if(vm.stack_offset < 0)   
    {   
        if(vm.instruction)
            SHL_VM_ERROR(env, vm, "Stack underflow");      
        return nullptr;                           
    }

    shl_value* top = shl_vm_top(env, vm);
    --vm.stack_offset;
    return top;
}

inline void shl_vm_push_frame(shl_environment& env, shl_vm& vm, int arg_count, int ret_addr)
{
    shl_vm_frame frame;
    frame.arg_count = arg_count;
    frame.stack_offset = vm.stack_offset;
    frame.instruction = vm.bytecode + ret_addr;
    vm.frame_stack.push_back(frame);
}

inline void shl_vm_pop_frame(shl_environment& env, shl_vm& vm)
{
    shl_vm_frame frame = vm.frame_stack.back();
    vm.frame_stack.pop_back();

    vm.stack_offset = frame.stack_offset - frame.arg_count;
    vm.instruction = frame.instruction;
    // TODO: other than the assignment above. pop until stack offset == frame stack offset
    //while (vm.stack_offset > vm.frame_stack.back())
    //{
    //    // TODO: Free all objects in frame
    // --vm.stack_offset;
    //}
}

inline shl_value* shl_vm_get(shl_environment& env, shl_vm& vm, int stack_offset)
{
    int offset = stack_offset;
    if (vm.frame_stack.size() > 0)
    {
        const shl_vm_frame& frame = vm.frame_stack.back();
        if (frame.stack_offset > 0)
            offset = frame.stack_offset - stack_offset;
    }

    if (offset < 0)
    {
        if (vm.instruction)
            SHL_VM_ERROR(env, vm, "Stack undeflow");
        return nullptr;
    }
    return &vm.stack[offset];
}

inline int shl_vm_read_bool(shl_vm& vm)
{
    shl_vm_bytecode_value bytecode_value;
    for (size_t i = 0; i < sizeof(bytecode_value.b); ++i)
        bytecode_value.bytes[i] = *(vm.instruction++);
    return bytecode_value.b;
}

inline int shl_vm_read_int(shl_vm& vm)
{
    shl_vm_bytecode_value bytecode_value;
    for (size_t i = 0; i < sizeof(bytecode_value.i); ++i)
        bytecode_value.bytes[i] = *(vm.instruction++);
    return bytecode_value.i;
}

inline float shl_vm_read_float(shl_vm& vm)
{
    shl_vm_bytecode_value bytecode_value;
    for (size_t i = 0; i < sizeof(bytecode_value.f); ++i)
        bytecode_value.bytes[i] = *(vm.instruction++);
    return bytecode_value.f;
}

inline const char* shl_vm_read_bytes(shl_vm& vm)
{
    size_t len = 1; // include null terminating
    while (*(vm.instruction++))
        len++;
    return vm.instruction - len;
}

void shl_vm_execute(shl_environment& env, shl_vm& vm, const shl_array<char>& bytecode)
{
    vm.bytecode = &bytecode[0];
    vm.instruction = vm.bytecode;
    vm.stack_offset = -1;

    if (vm.bytecode == nullptr)
        return;

    while(vm.instruction)
    {
#if SHL_VM_DEBUG_TOP
        printf("[%d] %s\n", vm.instruction - vm.bytecode, shl_opcode_labels[(*vm.instruction)]);
#endif

        shl_opcode opcode = (shl_opcode) (*vm.instruction);
        ++vm.instruction;

        switch(opcode)
        {
            case SHL_OPCODE_EXIT:
                vm.instruction = nullptr;
                break;
            case SHL_OPCODE_NO_OP:
                break;
            case SHL_OPCODE_PUSH_NONE:
            {
                shl_value* value = shl_vm_push(env, vm);
                if (value == nullptr)
                    break;
                value->type = SHL_NONE;
                break;
            }
            case SHL_OPCODE_PUSH_BOOL:
            {
                shl_value* value = shl_vm_push(env, vm);
                if (value == nullptr)
                    break;
                value->type = SHL_BOOL;
                value->b = shl_vm_read_bool(vm);
                break;
            }
            case SHL_OPCODE_PUSH_INT:   
            {
                shl_value* value = shl_vm_push(env, vm);
                if(value == nullptr) 
                    break;
                value->type = SHL_INT;
                value->i = shl_vm_read_int(vm);
                break;
            }
            case SHL_OPCODE_PUSH_FLOAT:
            {
                shl_value* value = shl_vm_push(env, vm);
                if(value == nullptr) 
                    break;
                value->type = SHL_FLOAT;
                value->f = shl_vm_read_float(vm);
                break;
            }                                  
            case SHL_OPCODE_PUSH_STRING:
            {
                shl_value* value = shl_vm_push(env, vm);
                if(value == nullptr) 
                    break;
                value->type = SHL_STRING;
                // TODO: Duplicate string ? When popping string from stack, clean up allocated string ?  
                value->str = shl_vm_read_bytes(vm);
                break;
            }
            case SHL_OPCODE_PUSH_LOCAL:
            {
                const int address = shl_vm_read_int(vm);
                shl_value* top = shl_vm_push(env, vm);
                shl_value* local = shl_vm_get(env, vm, address);
                if (top !=  nullptr || local != nullptr)
                    *top = *local;

                break;
            }
            case SHL_OPCODE_LOAD_LOCAL:
            {
                const int address = shl_vm_read_int(vm);                
                shl_value* top = shl_vm_pop(env, vm);

                // If local address was not supplied, i.e. not defined, push new instance onto stack
                shl_value* local;
                if(address == SHL_OPCODE_INVALID_ADDR)
                    local = shl_vm_push(env, vm);
                else
                    local = shl_vm_get(env, vm, address);
                    
                if (top != nullptr && local != nullptr)
                    *local = *top;

                break;
            }
            case SHL_OPCODE_ENTER:
            {
                const int arg_count = shl_vm_read_int(vm);
                const int ret_addr = shl_vm_read_int(vm);
                shl_vm_push_frame(env, vm, arg_count, ret_addr);
                break;
            }
            case SHL_OPCODE_RETURN:
            {
                shl_value* ret_value = shl_vm_top(env, vm);
                shl_vm_pop_frame(env, vm);

                shl_value* top = shl_vm_push(env, vm);
                if (ret_value != nullptr && top != nullptr)
                    *top = *ret_value;
                break;
            }
            case SHL_OPCODE_CALL_EXT:
            {
                const int arg_count = shl_vm_read_int(vm);
                const char* func_name = shl_vm_read_bytes(vm);;

                shl_array<shl_value*> args;
                args.resize(arg_count);
                for(int i = arg_count - 1; i >= 0; --i)
                {
                    args[i] = shl_vm_pop(env, vm);
                    if (args[i] == nullptr)
                        SHL_VM_ERROR(env, vm, "Function Call %s Recieved %d arguments, expected %d", func_name, arg_count - i, arg_count);
                }

                if(args.size() == arg_count && env.functions.count(func_name))
                    env.functions.at(func_name)(args);

                break;
            }
            case SHL_OPCODE_ADD:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);    
                if (!shl_value_add(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, "+");

                break;
            }
            case SHL_OPCODE_SUB:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);    
                if (!shl_value_sub(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, "-");
                break;
            }
            case SHL_OPCODE_MUL:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);    
                if (!shl_value_mul(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, "*");
                break;
            }
            case SHL_OPCODE_DIV:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);    
                if(!shl_value_div(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, "/");
                break;
            }
            case SHL_OPCODE_POW:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);
                if (!shl_value_pow(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, "^");
                break;
            }
            case SHL_OPCODE_MOD:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);
                if (!shl_value_mod(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, "%%");
                break;
            }
            case SHL_OPCODE_NOT:
            {
                shl_value* arg = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);
                if (!shl_value_set_bool(target, !shl_value_to_bool(arg)))
                    SHL_VM_UNOP_ERROR(env, vm, arg, "!");
                break;
            }
            case SHL_OPCODE_LT:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);
                if (!shl_value_lt(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, "<");
                break;
            }
            case SHL_OPCODE_LTE:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);
                if (!shl_value_lte(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, "<=");
                break;
            }
            case SHL_OPCODE_GT:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);
                if (!shl_value_gt(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, ">");
                break;
            }
            case SHL_OPCODE_GTE:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);
                if (!shl_value_gte(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, ">=");
                break;
            }
            case SHL_OPCODE_EQ:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);
                if (!shl_value_eq(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, "==");
                break;
            }
            case SHL_OPCODE_NEQ:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);
                if (!shl_value_neq(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, "!=");
                break;
            }
            case SHL_OPCODE_APPROXEQ:
            {
                shl_value* lhs = shl_vm_pop(env, vm);
                shl_value* rhs = shl_vm_pop(env, vm);
                shl_value* target = shl_vm_push(env, vm);
                if (!shl_value_approxeq(target, lhs, rhs))
                    SHL_VM_BINOP_ERROR(env, vm, lhs, rhs, "~=");
                break;
            }
            case SHL_OPCODE_JUMP:
            {
                const int instruction_offset = shl_vm_read_int(vm);
                vm.instruction = vm.bytecode + instruction_offset;
                break;
            }
            case SHL_OPCODE_JUMP_NZERO:
            {
                const int instruction_offset = shl_vm_read_int(vm);

                // need to check if the chunk can be jumped to
                shl_value* value = shl_vm_pop(env, vm);
                if (shl_value_to_bool(value) != 0)
                    vm.instruction = vm.bytecode + instruction_offset;
                break;
            }
            case SHL_OPCODE_JUMP_ZERO:
            {
                const int instruction_offset = shl_vm_read_int(vm);

                // need to check if the chunk can be jumped to
                shl_value* value = shl_vm_pop(env, vm);
                if (shl_value_to_bool(value) == 0)
                    vm.instruction = vm.bytecode + instruction_offset;
                break;
            }
            default:
                // UNSUPPORTED!!!!
            break;
        }
    }
}

// -------------------------------------- Passes ------------------------------------------------// 
bool shl_pass_semantic_check(const shl_ast* node)
{
    if(node == nullptr)
        return false;
    return true;
}

// -------------------------------------- Transformations ---------------------------------------// 
struct shl_ast_to_ir_context
{
    bool valid;
    shl_environment env;
    shl_map<shl_string, int> function_addr;
};

void shl_ast_to_ir(shl_ast_to_ir_context& context, const shl_ast* node, shl_ir& ir)
{
    if(node == nullptr|| !context.valid)
        return;

    const shl_token& token = node->token;
    const shl_array<shl_ast*>& children = node->children;

    switch(node->id)
    {
        case SHL_AST_ROOT:
        {
            assert(children.size() == 1);  // START [0] EXIT
            shl_ast_to_ir(context, children[0], ir);

            // End block, default exit
            shl_ir_push_block(ir, "");
            shl_ir_add_operation(ir, SHL_OPCODE_EXIT);
            shl_ir_pop_block(ir);
            break;
        }
        case SHL_AST_BLOCK: 
        {
            shl_ir_push_block(ir, "");
            for (shl_ast* stmt : children)
                shl_ast_to_ir(context, stmt, ir);
            shl_ir_pop_block(ir);
            break;
        }
        case SHL_AST_LITERAL:
        {
            switch(token.id)
            {
                case SHL_TOKEN_DECIMAL:
                {
                    const int data = strtol(token.data.c_str(), NULL, 10);
                    const shl_ir_operand& operand = shl_ir_operand_from_int(data);
                    shl_ir_add_operation(ir, SHL_OPCODE_PUSH_INT, operand);
                    break;
                }
                case SHL_TOKEN_HEXIDECIMAL:
                {
                    const unsigned int data = strtoul(token.data.c_str(), NULL, 16);
                    const shl_ir_operand& operand = shl_ir_operand_from_int(data);
                    shl_ir_add_operation(ir, SHL_OPCODE_PUSH_INT, operand);
                    break;
                }
                case SHL_TOKEN_BINARY:
                {
                    const unsigned int data = strtoul(token.data.c_str(), NULL, 2);
                    const shl_ir_operand& operand = shl_ir_operand_from_int(data);
                    shl_ir_add_operation(ir, SHL_OPCODE_PUSH_INT, operand);
                    break;
                }
                case SHL_TOKEN_FLOAT:       
                {
                    const float data = strtof(token.data.c_str(), NULL);
                    const shl_ir_operand& operand = shl_ir_operand_from_float(data);
                    shl_ir_add_operation(ir, SHL_OPCODE_PUSH_FLOAT, operand);
                    break;
                }
                case SHL_TOKEN_STRING:      
                {
                    const shl_ir_operand& operand = shl_ir_operand_from_str(token.data.c_str());
                    shl_ir_add_operation(ir, SHL_OPCODE_PUSH_STRING, operand);
                    break;
                }
                case SHL_TOKEN_TRUE:
                {
                    const shl_ir_operand& operand = shl_ir_operand_from_bool(true);
                    shl_ir_add_operation(ir, SHL_OPCODE_PUSH_BOOL, operand);
                    break;
                }
                case SHL_TOKEN_FALSE:
                {
                    const shl_ir_operand& operand = shl_ir_operand_from_bool(false);
                    shl_ir_add_operation(ir, SHL_OPCODE_PUSH_BOOL, operand);
                    break;
                }
                default: 
                break;
            }
            break;
        }
        case SHL_AST_VARIABLE:
        {
            const int offset = shl_ir_get_name_addr(ir, token.data);
            if (offset == -1)
            {
                context.valid = false;
                SHL_LOG_ERROR(context.env, "Variable %s not defined in current block", token.data.c_str());
                break;
            }

            // TODO: determine if variable is local or global
            const shl_ir_operand& address_operand = shl_ir_operand_from_int(offset);
            shl_ir_add_operation(ir, SHL_OPCODE_PUSH_LOCAL, address_operand);
            break;
        }
        case SHL_AST_UNARY_OP:
        {
            assert(children.size() == 1); // token [0]

            shl_ast_to_ir(context, children[0], ir);

            switch(token.id)
            {
                case SHL_TOKEN_NOT: shl_ir_add_operation(ir, SHL_OPCODE_NOT); break;
                default: break;
            }
            break;
        }
        case SHL_AST_BINARY_OP:
        {
            assert(children.size() == 2); // [0] token [1]

            shl_ast_to_ir(context, children[1], ir); // RHS
            shl_ast_to_ir(context, children[0], ir); // LHS

            switch(token.id)
            {
                case SHL_TOKEN_ADD:    shl_ir_add_operation(ir, SHL_OPCODE_ADD); break;
                case SHL_TOKEN_SUB:    shl_ir_add_operation(ir, SHL_OPCODE_SUB); break;
                case SHL_TOKEN_MUL:    shl_ir_add_operation(ir, SHL_OPCODE_MUL); break;
                case SHL_TOKEN_DIV:    shl_ir_add_operation(ir, SHL_OPCODE_DIV); break;
                case SHL_TOKEN_MOD:    shl_ir_add_operation(ir, SHL_OPCODE_MOD); break;
                case SHL_TOKEN_POW:    shl_ir_add_operation(ir, SHL_OPCODE_POW); break;
                case SHL_TOKEN_LT:     shl_ir_add_operation(ir, SHL_OPCODE_LT);  break;
                case SHL_TOKEN_LT_EQ:  shl_ir_add_operation(ir, SHL_OPCODE_LTE); break;
                case SHL_TOKEN_GT:     shl_ir_add_operation(ir, SHL_OPCODE_GT);  break;
                case SHL_TOKEN_GT_EQ:  shl_ir_add_operation(ir, SHL_OPCODE_GTE); break;
                case SHL_TOKEN_EQ:     shl_ir_add_operation(ir, SHL_OPCODE_EQ);  break;
                case SHL_TOKEN_NOT_EQ: shl_ir_add_operation(ir, SHL_OPCODE_NEQ); break;
                case SHL_TOKEN_APPROX_EQ: shl_ir_add_operation(ir, SHL_OPCODE_APPROXEQ); break;
                default: break;
            }
            break;
        }
        case SHL_AST_FUNC_CALL:
        {
            const char* func_name = token.data.c_str();
            const int arg_count = children.size();

            if (context.function_addr.count(func_name))
            {
                for (const shl_ast* arg : children)
                    shl_ast_to_ir(context, arg, ir);

                shl_array<shl_ir_operand> stub_operands;
                stub_operands.push_back(shl_ir_operand_from_int(0));
                stub_operands.push_back(shl_ir_operand_from_int(0));
                const size_t enter = shl_ir_add_operation(ir, SHL_OPCODE_ENTER, stub_operands);

                int function_offset = context.function_addr[func_name];
                const shl_ir_operand& func_jmp_operand = shl_ir_operand_from_int(function_offset);

                shl_array<shl_ir_operand> operands;
                operands.push_back(func_jmp_operand);
                shl_ir_add_operation(ir, SHL_OPCODE_JUMP, operands);

                const int return_addr = ir.bytecode_count;

                shl_ir_operation& enter_operation = shl_ir_get_operation(ir, enter);
                enter_operation.operands[0] = shl_ir_operand_from_int(arg_count);
                enter_operation.operands[1] = shl_ir_operand_from_int(return_addr);
            }
            else if(context.env.functions.count(func_name))
            {
                for (const shl_ast* arg : children)
                    shl_ast_to_ir(context, arg, ir);

                const shl_ir_operand& arg_count_operand = shl_ir_operand_from_int(arg_count);
                const shl_ir_operand& func_name_operand = shl_ir_operand_from_str(func_name);

                shl_array<shl_ir_operand> operands;
                operands.push_back(arg_count_operand);
                operands.push_back(func_name_operand);
                shl_ir_add_operation(ir, SHL_OPCODE_CALL_EXT, operands);
            }
            else
            {
                context.valid = false;
                SHL_LOG_ERROR(context.env, "Function %s not defined", func_name);
            }
            break;
        }
        case SHL_AST_STMT_ASSIGN:
        {
            assert(children.size() == 1); // token = [0]

            if (children.size() == 1)
                shl_ast_to_ir(context, children[0], ir);

            int offset = shl_ir_get_name_addr(ir, token.data);
            if (offset == SHL_OPCODE_INVALID_ADDR)
                shl_ir_set_name_addr(ir, token.data);

            const shl_ir_operand& address_operand = shl_ir_operand_from_int(offset);
            shl_ir_add_operation(ir, SHL_OPCODE_LOAD_LOCAL, address_operand);

            break;
        }
        case SHL_AST_STMT_IF:
        {
            assert(children.size() == 2); //if ([0]) {[1]}

            const shl_ir_operand stub_operand = shl_ir_operand_from_int(0);

            // Evaluate expression. 
            shl_ast_to_ir(context, children[0], ir);

            //Jump to end if false
            const size_t end_block_jmp = shl_ir_add_operation(ir, SHL_OPCODE_JUMP_ZERO, stub_operand);

            // True block
            shl_ast_to_ir(context, children[1], ir);
            const size_t end_block_addr = ir.bytecode_count;

            // Fixup stubbed block offsets
            shl_ir_operation& end_block_jmp_operation = shl_ir_get_operation(ir, end_block_jmp);
            end_block_jmp_operation.operands[0] = shl_ir_operand_from_int(end_block_addr);
            
            break;
        }
        case SHL_AST_STMT_IF_ELSE:
        {
            assert(children.size() == 3); //if ([0]) {[1]} else {[2]}

            const shl_ir_operand stub_operand = shl_ir_operand_from_int(0);

            // Evaluate expression. 
            shl_ast_to_ir(context, children[0], ir);

            //Jump to else if false
            const size_t else_block_jmp = shl_ir_add_operation(ir, SHL_OPCODE_JUMP_ZERO, stub_operand);

            // True block
            shl_ast_to_ir(context, children[1], ir);
                
            //Jump to end after true
            const size_t end_block_jmp = shl_ir_add_operation(ir, SHL_OPCODE_JUMP, stub_operand);
            const size_t else_block_addr = ir.bytecode_count;

            // Else block
            shl_ast_to_ir(context, children[2], ir);

            // Tag end address
            const size_t end_block_addr = ir.bytecode_count;

            // Fixup stubbed block offsets
            shl_ir_operation& else_block_jmp_operation = shl_ir_get_operation(ir, else_block_jmp);
            else_block_jmp_operation.operands[0] = shl_ir_operand_from_int(else_block_addr);

            shl_ir_operation& end_block_jmp_operation = shl_ir_get_operation(ir, end_block_jmp);
            end_block_jmp_operation.operands[0] = shl_ir_operand_from_int(end_block_addr);
            
            break;
        }
        case SHL_AST_STMT_WHILE:
        {
            assert(children.size() == 2); //while ([0]) {[1]}

            const shl_ir_operand stub_operand = shl_ir_operand_from_int(0);

            const shl_ir_operand& begin_block_operand = shl_ir_operand_from_int(ir.bytecode_count);

            // Evaluate expression. 
            shl_ast_to_ir(context, children[0], ir);

            //Jump to end if false
            const size_t end_block_jmp = shl_ir_add_operation(ir, SHL_OPCODE_JUMP_ZERO, stub_operand);

            // Loop block
            shl_ast_to_ir(context, children[1], ir);

            // Jump back to beginning, expr evaluation 
            shl_ir_add_operation(ir, SHL_OPCODE_JUMP, begin_block_operand);

            // Tag end address
            const size_t end_block_addr = ir.bytecode_count;

            // Fixup stubbed block offsets
            shl_ir_operation& end_block_jmp_operation = shl_ir_get_operation(ir, end_block_jmp);
            end_block_jmp_operation.operands[0] = shl_ir_operand_from_int(end_block_addr);
            
            break;
        }
        case SHL_AST_PARAM:
        {
            // Force new position for the parameter. load stack into new address
            int offset = SHL_OPCODE_INVALID_ADDR;
            shl_ir_set_name_addr(ir, token.data);

            const shl_ir_operand& address_operand = shl_ir_operand_from_int(SHL_OPCODE_INVALID_ADDR);
            shl_ir_add_operation(ir, SHL_OPCODE_LOAD_LOCAL, address_operand);
            break;
        }
        case SHL_AST_PARAM_LIST:
        {            
            for (const shl_ast* param : children)
                shl_ast_to_ir(context, param, ir);
            break;
        }
        case SHL_AST_RETURN:
        {
            if(children.size() == 1) //return [0];
            {
                shl_ast_to_ir(context, children[0], ir);
                shl_ir_add_operation(ir, SHL_OPCODE_RETURN);
            }
            else
            {
                shl_ir_add_operation(ir, SHL_OPCODE_PUSH_NONE);
                shl_ir_add_operation(ir, SHL_OPCODE_RETURN);
            }
            break;
        }
        case SHL_AST_FUNC_DEF:
        {
            assert(children.size() == 2); //func token [0] {[1]};

            // Jump over the func def
            const shl_ir_operand stub_operand = shl_ir_operand_from_int(0);
            const size_t end_block_jmp = shl_ir_add_operation(ir, SHL_OPCODE_JUMP, stub_operand);

            const char* func_name = token.data.c_str();

            const size_t func_addr = ir.bytecode_count;
            if (context.function_addr.count(func_name) != 0)
            {
                context.valid = false;
                SHL_LOG_ERROR(context.env, "Function %s already defined", func_name);
                break;
            }
            context.function_addr[func_name] = func_addr;

            shl_ir_push_block(ir, func_name);

            shl_ast_to_ir(context, children[0], ir);
            shl_ast_to_ir(context, children[1], ir);
            
            // Ensure there is a return
            if (ir.module.operations.at(ir.module.operations.size() - 1).opcode != SHL_OPCODE_RETURN)
            {
                shl_ir_add_operation(ir, SHL_OPCODE_PUSH_NONE);
                shl_ir_add_operation(ir, SHL_OPCODE_RETURN);
            }
    
            shl_ir_pop_block(ir);

            const size_t end_block_addr = ir.bytecode_count;

            // fixup jump operand
            shl_ir_operation& end_block_jmp_operation = shl_ir_get_operation(ir, end_block_jmp);
            end_block_jmp_operation.operands[0] = shl_ir_operand_from_int(end_block_addr);

            break;
        }
        default:
            assert(0);
            break;
    }
}

void shl_ir_to_bytecode(shl_ir& ir, shl_array<char>& bytecode)
{  
    bytecode.resize(ir.bytecode_count, SHL_OPCODE_NO_OP) ;

    char* instruction = &bytecode[0];

    for(const shl_ir_operation& operation : ir.module.operations)
    {
        *instruction = (char)operation.opcode;
        ++instruction;    

        for(const shl_ir_operand& operand : operation.operands)
        {
            switch (operand.type)
            {
            case SHL_IR_OPERAND_NONE:
                break;
            case SHL_IR_OPERAND_BOOL:
            case SHL_IR_OPERAND_INT:
            case SHL_IR_OPERAND_FLOAT:
                for (size_t i = 0; i < shl_ir_operand_size(operand); ++i)
                    *(instruction++) = operand.data.bytes[i];
                break;
            case SHL_IR_OPERAND_BYTES:
                for (size_t i = 0; i < strlen(operand.data.str); ++i)
                    *(instruction++) = operand.data.str[i];

                *(instruction++) = 0; // null terminate
                break;
            }
        }
    }
}

// -------------------------------------- API ------ ---------------------------------------// 

bool shl_compile_ast(shl_environment& env, shl_ast* root, shl_array<char>& bytecode)
{
    bool success = shl_pass_semantic_check(root);
    if (!success)
        return false;

    // Generate IR
    shl_ast_to_ir_context context;
    context.valid = true;
    context.env = env;

    shl_ir ir;
    shl_ast_to_ir(context, root, ir);

    // Load bytecode into VM
    shl_ir_to_bytecode(ir, bytecode);

    return context.valid;
}

void shl_register(shl_environment& env, const char* function_id, shl_function_callback *callback)
{
    env.functions[function_id] = callback;
}

void shl_unregister(shl_environment& env, const char* function_id)
{
    env.functions.erase(function_id);
}

void shl_execute(shl_environment& env, const char* filename)
{
    // Parse file
    shl_ast* root = shl_parse_file(env, filename);
    if (root == nullptr)
        return;

    // Load bytecode into VM
    shl_array<char> bytecode;
    shl_compile_ast(env, root, bytecode);

    // Cleanup 
    shl_ast_delete(root);

    shl_vm vm;
    shl_vm_execute(env, vm, bytecode);

    // TODO: should this return top of stack to user?
}

void shl_evaluate(shl_environment& env, const char* code)
{
    // Parse file
    shl_ast* root = shl_parse(env, code);
    if (root == nullptr)
        return;

    // Load bytecode into VM
    shl_array<char> bytecode;
    shl_compile_ast(env, root, bytecode);

    // Cleanup 
    shl_ast_delete(root);

    shl_vm vm;
    shl_vm_execute(env, vm, bytecode);

    // TODO: should this return top of stack to user?
}

// --------------------------------------------- Values ---------------------------------------------------------// 

inline bool shl_value_set_bool(shl_value* value, bool data)
{
    if (value == nullptr)
        return false;
    value->type = SHL_BOOL;
    value->b = data;
    return true;
}

inline bool  shl_value_set_int(shl_value* value, int data)
{
    if (value == nullptr)
        return false;
    value->type = SHL_INT;
    value->i = data;
    return true;
}

inline bool shl_value_set_float(shl_value* value, float data)
{
    if (value == nullptr)
        return false;
    value->type = SHL_FLOAT;
    value->f = data;
    return true;
}

shl_string shl_value_to_string(const shl_value* value)
{
    if(value == nullptr)
        return "null";

    char out[1024];
    int len = 0;
    switch(value->type)
    {
        case SHL_NONE:
            len = snprintf(out, sizeof(out), "%s", shl_type_labels[value->type]);
            break;
        case SHL_BOOL:
            len = snprintf(out, sizeof(out), "%s:%s", shl_type_labels[value->type], value->b ? "true" : "false");
            break;
        case SHL_INT:
            len = snprintf(out, sizeof(out), "%s:%d", shl_type_labels[value->type], value->i);
            break;
        case SHL_FLOAT:
            len = snprintf(out, sizeof(out), "%s:%f", shl_type_labels[value->type], value->f);
            break;
        case SHL_STRING:
            len = snprintf(out, sizeof(out), "%s:%s", shl_type_labels[value->type], value->str);
            break;
        case SHL_OBJECT:
            len = snprintf(out, sizeof(out), "%s", shl_type_labels[value->type]);
            break;
    }
    return shl_string(out, len);
}

bool shl_value_to_bool(const shl_value* value)
{
    if(value == nullptr)
        return false;

    switch(value->type)
    {
        case SHL_BOOL:
            return value->b;
        case SHL_INT:
            return value->i != 0;
        case SHL_FLOAT:
            return value->f != 0.0f;
        case SHL_STRING:
            return value->str != nullptr;
        case SHL_OBJECT:
            return value->obj != nullptr;
    }
    return false;
}

#define SHL_DEFINE_BINOP(result, lhs, rhs,                      \
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
        case SHL_INT:                                           \
            switch(rhs->type)                                   \
            {                                                   \
                case SHL_INT:   int_int_case;                   \
                case SHL_FLOAT: int_float_case;                 \
                default: break;                                 \
            }                                                   \
            break;                                              \
        case SHL_FLOAT:                                         \
           switch(rhs->type)                                    \
            {                                                   \
                case SHL_INT:   float_int_case;                 \
                case SHL_FLOAT: float_float_case;               \
                default: break;                                 \
            }                                                   \
            break;                                              \
        case SHL_BOOL:                                          \
            switch (rhs->type)                                  \
            {                                                   \
                case SHL_BOOL: bool_bool_case;                  \
                default: break;                                 \
            }                                                   \
            break;                                              \
        default: break;                                         \
    }                                                           \
}

inline bool shl_value_add(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_int  (result, lhs->i + rhs->i),
        return shl_value_set_float(result, lhs->i + rhs->f),
        return shl_value_set_float(result, lhs->f + rhs->i),
        return shl_value_set_float(result, lhs->f + rhs->f),
        return false
    )
   return false;
}

inline bool shl_value_sub(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_int  (result, lhs->i - rhs->i),
        return shl_value_set_float(result, lhs->i - rhs->f),
        return shl_value_set_float(result, lhs->f - rhs->i),
        return shl_value_set_float(result, lhs->f - rhs->f),
        return false
    )
    return false;
}

inline bool shl_value_mul(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_int  (result, lhs->i * rhs->i),
        return shl_value_set_float(result, lhs->i * rhs->f),
        return shl_value_set_float(result, lhs->f * rhs->i),
        return shl_value_set_float(result, lhs->f * rhs->f),
        return false
    )
    return false;
}

inline bool shl_value_div(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_float(result, (float)lhs->i / rhs->i),
        return shl_value_set_float(result,        lhs->i / rhs->f),
        return shl_value_set_float(result,        lhs->f / rhs->i),
        return shl_value_set_float(result,        lhs->f / rhs->f),
        return false
    )
     return false;
}

inline bool shl_value_pow(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_int(result,  (int)powf((float)lhs->i, (float)rhs->i)),
        return shl_value_set_float(result, powf((float) lhs->i, rhs->f)),
        return shl_value_set_float(result, powf(lhs->f, (float) rhs->i)),
        return shl_value_set_float(result, powf(lhs->f, rhs->f)),
        return false
    )
        return false;
}

inline bool shl_value_mod(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_int(result, lhs->i % rhs->i),
        return shl_value_set_float(result, (float) fmod(lhs->i, rhs->f)),
        return shl_value_set_float(result, (float) fmod(lhs->f, rhs->i)),
        return shl_value_set_float(result, (float) fmod(lhs->f, rhs->f)),
        return false
    )
        return false;
}

inline bool shl_value_lt(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_bool(result, lhs->i < rhs->i),
        return shl_value_set_bool(result, lhs->i < rhs->f),
        return shl_value_set_bool(result, lhs->f < rhs->i),
        return shl_value_set_bool(result, lhs->f < rhs->f),
        return false
    )
    return false;
}

inline bool shl_value_lte(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_bool(result, lhs->i <= rhs->i),
        return shl_value_set_bool(result, lhs->i <= rhs->f),
        return shl_value_set_bool(result, lhs->f <= rhs->i),
        return shl_value_set_bool(result, lhs->f <= rhs->f),
        return false
    )
    return false;
}

inline bool shl_value_gt(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_bool(result, lhs->i > rhs->i),
        return shl_value_set_bool(result, lhs->i > rhs->f),
        return shl_value_set_bool(result, lhs->f > rhs->i),
        return shl_value_set_bool(result, lhs->f > rhs->f),
        return false
    )
    return false;
}

inline bool shl_value_gte(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_bool(result, lhs->i >= rhs->i),
        return shl_value_set_bool(result, lhs->i >= rhs->f),
        return shl_value_set_bool(result, lhs->f >= rhs->i),
        return shl_value_set_bool(result, lhs->f >= rhs->f),
        return false
    )
    return false;
}

inline bool shl_value_eq(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    // TODO: add a special case for object equivalence (per field)
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_bool(result, lhs->i == rhs->i),
        return shl_value_set_bool(result, lhs->i == rhs->f),
        return shl_value_set_bool(result, lhs->f == rhs->i),
        return shl_value_set_bool(result, lhs->f == rhs->f),
        return shl_value_set_bool(result, lhs->b == rhs->b)
        )
    return false;
}

inline bool shl_value_neq(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    // TODO: add a special case for object equivalence (per field)
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_bool(result, lhs->i != rhs->i),
        return shl_value_set_bool(result, lhs->i != rhs->f),
        return shl_value_set_bool(result, lhs->f != rhs->i),
        return shl_value_set_bool(result, lhs->f != rhs->f),
        return shl_value_set_bool(result, lhs->b != rhs->b)
        )
    return false;
}

inline bool shl_value_approxeq(shl_value* result, shl_value* lhs, shl_value* rhs)
{
    SHL_DEFINE_BINOP(result, lhs, rhs,
        return shl_value_set_bool(result, abs(lhs->i - rhs->i) < SHL_VM_APPROX_THRESHOLD),
        return shl_value_set_bool(result, abs(lhs->i - rhs->f) < SHL_VM_APPROX_THRESHOLD),
        return shl_value_set_bool(result, abs(lhs->f - rhs->i) < SHL_VM_APPROX_THRESHOLD),
        return shl_value_set_bool(result, abs(lhs->f - rhs->f) < SHL_VM_APPROX_THRESHOLD),
        return shl_value_set_bool(result, lhs->b == rhs->b)
        )
        return false;
}

#undef SHL_DEFINE_BINOP

#endif