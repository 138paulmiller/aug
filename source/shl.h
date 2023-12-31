#ifndef __SHL_HEADER__
#define __SHL_HEADER__
/*
    Single Header Language 
    Author: 138paulmiller

    Todo: 
    - Parsing - statement, assignment, object, func dec
    - Semantic Pass- check variable, function, field names, check binary/unary ops
    - Framework - Add filewatchers to recompile if source file changed.
    - VM - Execute compiled bytecode from file
*/

#include <functional>
#include <list>
#include <string>
#include <vector>
#include <unordered_map>

using shl_string = std::string;

template <class type>
using shl_list = std::list<type>;

template <class type>
using shl_array = std::vector<type>;

template <class key, class type>
using shl_map = std::unordered_map<key, type>;

using shl_error_callback = std::function<void(const char* /*msg*/)>;
using shl_function_callback = std::function<void(const shl_list<class shl_value*>& /*args*/)>;

enum shl_type
{
    SHL_INT, 
    SHL_FLOAT, 
    SHL_STRING, 
    SHL_BOOL, 
    SHL_OBJECT
};

struct shl_value
{
    shl_type type;
    union 
    {
        long i; 
        double f;
        const char* str;
        class shl_object* obj;
    };
};

struct shl_object
{
    shl_map<const char*, shl_value> map;
};

struct shl_environment
{
    // user defined functions
    shl_map<const char*, shl_function_callback> function_callbacks;
    shl_error_callback error_callback;
};

//void shl_register(shl_environment& environment, const char* function_signature, shl_function_callback callback); //TODO parse the func types ? 
//void shl_unregister(shl_environment& environment, const char* function_signature);

//void shl_compile(shl_environment& environment, const char* filename, const char* compiled_filename);

void shl_execute(shl_environment& env, const char* filename);
void shl_evaluate(shl_environment& env, const char* code);

#endif //__SHL_HEADER__

#if defined(SHL_IMPLEMENTATION)

#include <cassert>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>

#ifndef SHL_TOKEN_BUFFER_LEN
    #define SHL_TOKEN_BUFFER_LEN 128
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
    char buffer[2048];                                                      \
    int len = snprintf(buffer, sizeof(buffer), SHL_LOG_PRELUDE "Error: ");  \
    len = snprintf(buffer + len, sizeof(buffer) - len, __VA_ARGS__);        \
    env.error_callback(buffer);                                             \
}

// -------------------------------------- Lexer  ---------------------------------------// 
#define SHL_TOKEN_LIST                           \
    /* State */                                  \
    SHL_TOKEN(NONE,           0, 0, 0, NULL)     \
    SHL_TOKEN(ERR,	          0, 0, 1, NULL)     \
    SHL_TOKEN(END,            0, 0, 1, NULL)     \
    /* Symbols */			                     \
    SHL_TOKEN(DOT,            0, 0, 0, NULL)     \
    SHL_TOKEN(COMMA,          0, 0, 0, NULL)     \
    SHL_TOKEN(COLON,          0, 0, 0, NULL)     \
    SHL_TOKEN(SEMICOLON,      0, 0, 0, NULL)     \
    SHL_TOKEN(LPAREN,         0, 0, 0, NULL)     \
    SHL_TOKEN(RPAREN,         0, 0, 0, NULL)     \
    SHL_TOKEN(LBRACKET,       0, 0, 0, NULL)     \
    SHL_TOKEN(RBRACKET,       0, 0, 0, NULL)     \
    SHL_TOKEN(LBRACE,         0, 0, 0, NULL)     \
    SHL_TOKEN(RBRACE,         0, 0, 0, NULL)     \
    /* Operators */                              \
    SHL_TOKEN(AND,            1, 2, 0, NULL)     \
    SHL_TOKEN(OR,             1, 2, 0, NULL)     \
    SHL_TOKEN(ADD,            2, 2, 0, NULL)     \
    SHL_TOKEN(ADD_EQUAL,      1, 2, 0, NULL)     \
    SHL_TOKEN(SUB,            2, 2, 0, NULL)     \
    SHL_TOKEN(SUB_EQUAL,      1, 2, 0, NULL)     \
    SHL_TOKEN(MUL,            3, 2, 0, NULL)     \
    SHL_TOKEN(MUL_EQUAL,      1, 2, 0, NULL)     \
    SHL_TOKEN(DIV,            3, 2, 0, NULL)     \
    SHL_TOKEN(DIV_EQUAL,      1, 2, 0, NULL)     \
    SHL_TOKEN(POW,            3, 2, 0, NULL)     \
    SHL_TOKEN(POW_EQUAL,      1, 2, 0, NULL)     \
    SHL_TOKEN(LESS,           2, 2, 0, NULL)     \
    SHL_TOKEN(GREATER,        2, 2, 0, NULL)     \
    SHL_TOKEN(LESS_EQUAL,     1, 2, 0, NULL)     \
    SHL_TOKEN(GREATER_EQUAL,  1, 2, 0, NULL)     \
    SHL_TOKEN(ASSIGN,         1, 2, 0, NULL)     \
    SHL_TOKEN(EQUAL,          2, 2, 0, NULL)     \
    SHL_TOKEN(NOT,            3, 1, 0, NULL)     \
    SHL_TOKEN(NOT_EQUAL,      3, 2, 0, NULL)     \
    /* Literals */                               \
    SHL_TOKEN(DECIMAL,        0, 0, 1, NULL)     \
    SHL_TOKEN(HEXIDECIMAL,    0, 0, 1, NULL)     \
    SHL_TOKEN(BINARY,         0, 0, 1, NULL)     \
    SHL_TOKEN(FLOAT,          0, 0, 1, NULL)     \
    SHL_TOKEN(STRING,         0, 0, 1, NULL)     \
    /* Labels */                                 \
    SHL_TOKEN(ID,             0, 0, 1, NULL)     \
    /* Keywords */                               \
    SHL_TOKEN(IF,             0, 0, 1, "if")     \
    SHL_TOKEN(IN,             0, 0, 1, "in")     \
    SHL_TOKEN(FOR,            0, 0, 1, "for")    \
    SHL_TOKEN(WHILE,          0, 0, 1, "while")  \
    SHL_TOKEN(FUNC,           0, 0, 1, "func")   \
    SHL_TOKEN(TRUE,           0, 0, 1, "true")   \
    SHL_TOKEN(FALSE,          0, 0, 1, "false")

// Token identifier. 
enum shl_token_id : uint8_t 
{
#define SHL_TOKEN(id, ...) SHL_TOKEN_##id,
    SHL_TOKEN_LIST
#undef SHL_TOKEN
    SHL_TOKEN_COUNT
};

// Static token details
struct shl_token_detail
{
    const char* label;    // the string representation, used for visualization and debugging
    char prec;            // if the token is an operator, this is the precedence (note: higher values take precendece)
    int  argc;            // if the token is an operator, this is the number of operands
    bool capture;         // if non-zero, the token will contain the source string value (i.e. integer and string literals)
    const char* keyword;  // if non-null, the token must match the provided keyword
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
};

#define SHL_LEXER_ERROR(env, lexer, token, ...)          \
{                                                        \
    token.id = SHL_TOKEN_ERR;                            \
                                                         \
    SHL_LOG_ERROR(env, "%s(%d,%d):",                     \
        lexer.inputname.c_str(), lexer.line, lexer.col); \
    SHL_LOG_ERROR(env, __VA_ARGS__);                     \
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
    const int len = pos_end - lexer.pos_start;

    static char buffer[SHL_TOKEN_BUFFER_LEN];

    lexer.input->seekg(lexer.pos_start);
    lexer.input->read(buffer, len);
    lexer.input->seekg(pos_end);
    lexer.input->clear(state);

    s.assign(buffer, len);
}

bool shl_lexer_tokenize_string(shl_environment& env, shl_lexer& lexer, shl_token& token)
{
    shl_lexer_start_tracking(lexer);

    char c = shl_lexer_get(lexer);
    if (c != '\"')
    {
        shl_lexer_unget(lexer);
        return false;
    }

    do
    {
        c = shl_lexer_get(lexer);
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
            case '\'': case '\"': case '\\':
            case '0': case 'a': case 'b': case 'f': case 'n': case 'r': case 't': case 'v':
                c = shl_lexer_get(lexer);
                break;
            default:
            {
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
        }
    } while (c != '\"');

    token.id = SHL_TOKEN_STRING;
    shl_lexer_end_tracking(lexer, token.data);

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
    case '*':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_MUL_EQUAL;
        else
            id = SHL_TOKEN_MUL;
        break;
    case '/':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_DIV_EQUAL;
        else
            id = SHL_TOKEN_DIV;
        break;
    case '^':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_POW_EQUAL;
        else
            id = SHL_TOKEN_POW;
        break;
    case '<':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_LESS_EQUAL;
        else
            id = SHL_TOKEN_LESS;
        break;
    case '>':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_GREATER_EQUAL;
        else
            id = SHL_TOKEN_GREATER;
        break;
    case '=':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_GREATER_EQUAL;
        else
            id = SHL_TOKEN_ASSIGN;
        break;
    case '!':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_NOT_EQUAL;
        else
            id = SHL_TOKEN_NOT;
        break;
    case '+':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_ADD_EQUAL;
        else
            id = SHL_TOKEN_ADD;
        break;
    case '-':
        if (shl_lexer_peek(lexer) == '=' && shl_lexer_get(lexer))
            id = SHL_TOKEN_SUB_EQUAL;
        else
            id = SHL_TOKEN_SUB;
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

bool shl_lexer_tokenize_label(shl_environment& env, shl_lexer& lexer, shl_token& token)
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

    shl_string label;
    shl_lexer_end_tracking(lexer, label);

    // find token id for keyword
    shl_token_id id = SHL_TOKEN_ID;
    for (int i = 0; i < (int)SHL_TOKEN_COUNT; ++i)
    {
        const shl_token_detail& detail = shl_token_details[i];
        if (detail.keyword && detail.keyword == label)
        {
            id = (shl_token_id)i;
            break;
        }
    }
    
    token.id = id;
    token.data = std::move(label);

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

        c = shl_lexer_get(lexer); //eat x
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

        c = shl_lexer_get(lexer); // eat b
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
    if (c == SHL_COMMENT_SYMBOL)
    {
        while (c != EOF && c != '\n')
            c = shl_lexer_get(lexer);
        if (c == '\n')
            c = shl_lexer_peek(lexer);
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
        if (!shl_lexer_tokenize_number(env, lexer, token))
            shl_lexer_tokenize_symbol(env, lexer, token);
        break;
    case '\"':
        shl_lexer_tokenize_string(env, lexer, token);
        break;
    default:
        if (shl_lexer_tokenize_symbol(env, lexer, token))
            break;
        if(shl_lexer_tokenize_label(env, lexer, token))
            break;
        if (shl_lexer_tokenize_number(env, lexer, token))
            break;

        SHL_LEXER_ERROR(env, lexer, token, "invalid character % c", c);
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
        return false;
    }

    lexer.input = file;
    lexer.inputname = filename;

    return shl_lexer_move(env, lexer);
}

// -------------------------------------- Parser ---------------------------------------// 

enum shl_ast_id : uint8_t
{
    SHL_AST_ROOT,
    SHL_AST_BLOCK, 
    SHL_AST_ASSIGN, 
    SHL_AST_LITERAL, 
    SHL_AST_VARIABLE, 
    SHL_AST_UNARY_OP, 
    SHL_AST_BINARY_OP, 
    SHL_AST_FUNC_CALL,
    SHL_AST_FUNC_DEF, 
    SHL_AST_PARAM
};

struct shl_ast
{
    shl_ast_id id;
    shl_token token;
    shl_array<shl_ast*> children;
};

shl_ast* shl_ast_new(shl_ast_id id, const shl_token& token = shl_token())
{
    shl_ast* node = new shl_ast();
    node->id = id;
    node->token = token;
    return node;
}

void shl_ast_delete(shl_ast* node)
{
    for(shl_ast* child : node->children)
        shl_ast_delete(child);
    delete node;
}

#define SHL_PARSE_ERROR(env, lexer,  ...)\
{\
    SHL_LOG_ERROR(env, "Syntax error %s(%d,%d)", lexer.inputname.c_str(), lexer.line, lexer.col);\
    SHL_LOG_ERROR(env, __VA_ARGS__);\
}

bool shl_parse_expr_pop(shl_environment& env, shl_lexer& lexer, shl_list<shl_token>&op_stack, shl_list<shl_ast*>& expr_stack)
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

shl_ast* shl_parse_value(shl_environment& env, shl_lexer& lexer); 

shl_ast* shl_parse_expr(shl_environment& env, shl_lexer& lexer)
{
    // Shunting yard algorithm
    shl_list<shl_token> op_stack;
    shl_list<shl_ast*> expr_stack;
    
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
    shl_token idtoken = lexer.curr;
    if (idtoken.id != SHL_TOKEN_ID)
        return nullptr;

    if (lexer.next.id != SHL_TOKEN_LPAREN)
        return nullptr;

    shl_lexer_move(env, lexer); // eat id
    shl_lexer_move(env, lexer); // eat (

    shl_array<shl_ast*> args;
    if (shl_ast* expr = shl_parse_expr(env, lexer))
    {
        args.push_back(expr);

        while (expr && lexer.curr.id == SHL_TOKEN_COMMA)
        {
            shl_lexer_move(env, lexer); // eat ,

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

    shl_lexer_move(env, lexer); // eat )

    shl_ast* funccall = shl_ast_new(SHL_AST_FUNC_CALL, idtoken);
    funccall->children = std::move(args);
    return funccall;
}

shl_ast* shl_parse_value(shl_environment& env, shl_lexer& lexer)
{
    shl_token token = lexer.curr;
    switch (token.id)
    {
    case SHL_TOKEN_ID:
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
    {
        shl_lexer_move(env, lexer);
        shl_ast* literal = shl_ast_new(SHL_AST_LITERAL, token);
        literal->id = SHL_AST_LITERAL;
        literal->token = token;
        return literal;
    }
    case SHL_TOKEN_LPAREN:
    {
        shl_lexer_move(env, lexer); // eat the LPAREN
        shl_ast* expr = shl_parse_expr(env, lexer);
        if (lexer.curr.id != SHL_TOKEN_RPAREN)
        {
            SHL_PARSE_ERROR(env, lexer,  "Expression missing closing parentheses");
            shl_ast_delete(expr);    
            return nullptr;
        }
        shl_lexer_move(env, lexer); // eat the RPAREN
        return expr;
    }
    default: 
    break;
    }
    return nullptr;
}

shl_ast* shl_parse_stmt(shl_environment& env, shl_lexer& lexer)
{
    //TODO: assignment, funcdef etc..
    // Default, epxression parsing. 
    if (shl_ast* expr = shl_parse_expr(env, lexer))
    {
        if(lexer.curr.id != SHL_TOKEN_SEMICOLON)
        {
            shl_ast_delete(expr);    
            SHL_PARSE_ERROR(env, lexer,  "Missing semicolon at end of expression");
            return nullptr;
        }

        shl_lexer_move(env, lexer); // eat ;
        return expr;
    }
    return nullptr;
}

shl_ast* shl_parse_block(shl_environment& env, shl_lexer& lexer)
{
    shl_array<shl_ast*> stmts;
    while(shl_ast* stmt = shl_parse_stmt(env, lexer))
        stmts.push_back(stmt);

    if(stmts.size() == 0)
        return nullptr;

    shl_ast* block = shl_ast_new(SHL_AST_BLOCK);
    block->children = std::move(stmts);
    return block;
}

shl_ast* shl_parse_root(shl_environment& env, shl_lexer& lexer)
{
    shl_ast* block = shl_parse_block(env, lexer);
    if(block == nullptr)
        return nullptr;

    shl_ast* root = new shl_ast();
    root->id = SHL_AST_ROOT;
    root->children = { std::move(block) };
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
#define SHL_OPCODE_LIST\
	SHL_OPCODE(NO_OP)          \
	SHL_OPCODE(EXIT)           \
	SHL_OPCODE(NEXT_CHUNK)     \
	SHL_OPCODE(PUSH_INT)       \
	SHL_OPCODE(PUSH_FLOAT)     \
	SHL_OPCODE(PUSH_STRING)    \
	SHL_OPCODE(PUSH_LOCAL)     \
	SHL_OPCODE(PUSH_GLOBAL)    \
	SHL_OPCODE(POP)            \
	SHL_OPCODE(ADD)            \
	SHL_OPCODE(SUB)            \
	SHL_OPCODE(MUL)            \
	SHL_OPCODE(DIV)            \
	SHL_OPCODE(POW)            \
	SHL_OPCODE(REM)            \
	SHL_OPCODE(MOD)            \
	SHL_OPCODE(SHL)            \
	SHL_OPCODE(SHR)            \
	SHL_OPCODE(SAR)            \
	SHL_OPCODE(AND)            \
	SHL_OPCODE(OR)             \
	SHL_OPCODE(XOR)            \
	SHL_OPCODE(NOT)            \
	SHL_OPCODE(NEG)            \
	SHL_OPCODE(COMPARE)        \
	SHL_OPCODE(ABS)            \
	SHL_OPCODE(SIN)            \
	SHL_OPCODE(COS)            \
	SHL_OPCODE(ATAN)           \
	SHL_OPCODE(LN)             \
	SHL_OPCODE(SQRT)           \
	SHL_OPCODE(INC)            \
	SHL_OPCODE(DEC)            \
	SHL_OPCODE(LT)             \
	SHL_OPCODE(LE)             \
	SHL_OPCODE(EQ)             \
	SHL_OPCODE(NE)             \
	SHL_OPCODE(GE)             \
	SHL_OPCODE(GT)             \
	SHL_OPCODE(MEM_ADDR)       \
	SHL_OPCODE(MEM_GET)        \
	SHL_OPCODE(MEM_SET)        \
	SHL_OPCODE(MEM_INC)        \
	SHL_OPCODE(MEM_DEC)        \
	SHL_OPCODE(ELEM_ADDR)      \
	SHL_OPCODE(ELEM_GET)       \
	SHL_OPCODE(ELEM_SET)       \
	SHL_OPCODE(FIELD_ADDR)     \
	SHL_OPCODE(FIELD_GET)      \
	SHL_OPCODE(FIELD_SET)      \
	SHL_OPCODE(LABEL)          \
	SHL_OPCODE(JUMP)           \
	SHL_OPCODE(JUMP_ZERO)      \
	SHL_OPCODE(JUMP_NZERO)     \
	SHL_OPCODE(JUMP_LT)        \
	SHL_OPCODE(JUMP_LE)        \
	SHL_OPCODE(JUMP_EQ)        \
	SHL_OPCODE(JUMP_NE)        \
	SHL_OPCODE(JUMP_GE)        \
	SHL_OPCODE(JUMP_GT)        \
	SHL_OPCODE(MULADD)         \
	SHL_OPCODE(CALL)           \
	SHL_OPCODE(RET)            \
	SHL_OPCODE(ASSERT)         \
	SHL_OPCODE(ASSERT_POSITIVE)\
	SHL_OPCODE(ASSERT_BOUND)

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

enum shl_ir_operand_flags : uint8_t
{
    // type if operand is constant or literal
    SHL_IR_OPERAND_DECIMAL = 1 << 0,
    SHL_IR_OPERAND_HEXIDECIMAL = 1 << 1,
    SHL_IR_OPERAND_BINARY = 1 << 2,
    SHL_IR_OPERAND_FLOAT = 1 << 3, 
    SHL_IR_OPERAND_STRING = 1 << 4, 
};

struct shl_ir_operand
{
    shl_string data;
    uint8_t flags = 0;
};

struct shl_ir_operation
{
    shl_opcode opcode;
    shl_ir_operand operand; //optional parameter. will be encoded in following bytes
};

struct shl_ir_block
{
    shl_string label;
    shl_list<shl_ir_operation> operations;
};

// All the blocks within a compilation/translation unit (i.e. file, code literal)
struct shl_ir_module
{
    shl_list<shl_ir_block> blocks;
};

// IR Module Builder
struct shl_ir
{		
    shl_ir_module module;
    shl_list<shl_ir_block> block_stack; 
    int generated_label_count = 0;
};

void shl_ir_push_block(shl_ir& ir, shl_string label)
{
    if(label.size() == 0)
    {
        const shl_string& generated_label = "L" + std::to_string(ir.generated_label_count++);

        shl_ir_block block;
        block.label = shl_string(generated_label.c_str(), generated_label.size());
        ir.block_stack.push_back(block);
    }
    else
    {
        shl_ir_block block;
        block.label = label;
        ir.block_stack.push_back(block);
    }
}

void shl_ir_pop_block(shl_ir& ir)
{
    assert(ir.block_stack.size() > 0);
    ir.module.blocks.push_front(ir.block_stack.back());
    ir.block_stack.pop_back();
}

void shl_ir_add_operation(shl_ir& ir, shl_opcode opcode, const shl_ir_operand& operand)
{
    shl_ir_operation operation;
    operation.opcode = opcode;
    operation.operand = operand;
    ir.block_stack.back().operations.push_back(std::move(operation));
}

void shl_ir_add_operation(shl_ir& ir, shl_opcode opcode)
{
    shl_ir_operand operand;
    shl_ir_add_operation(ir, opcode, operand);
}

// -------------------------------------- Bytecode ----------------------------------------------// 
struct shl_bytecode_chunk
{
    char bytecode[SHL_BYTECODE_CHUNK_SIZE];
};

// Used to convert values to/from bytes for constant values
union shl_bytecode_value
{
    long i;
    double f;
    unsigned char bytes[16];
};

// -------------------------------------- VM ----------------------------------------------------// 
struct shl_vm
{
    // NOTE?: all globals are push on beginning portion of stack frame. All locals are relative addresses
    shl_value stack[SHL_STACK_SIZE]; 
    shl_array<shl_bytecode_chunk*> bytecode_chunks;

    char* instruction; 
    size_t stack_offset; 
    bool running;
};

#define SHL_VM_CHECK_STACK(env, vm, running, size) \
{                                                  \
    if(vm.stack_offset + size >= SHL_STACK_SIZE)   \
    {                                              \
        SHL_LOG_ERROR(env, "Stack overflow");      \
        running = false;                           \
    }                                              \
}

shl_value* shl_vm_push(shl_environment& env, shl_vm& vm)
{
    ++vm.stack_offset;
    if(vm.stack_offset >= SHL_STACK_SIZE)   
    {                                              
        if(vm.running)
        {
            SHL_LOG_ERROR(env, "Stack overflow");      
            vm.running = false;
        }  
        return nullptr;                           
    }

    shl_value* value = &vm.stack[vm.stack_offset];
    return value;
}

shl_value* shl_vm_pop(shl_environment& env, shl_vm& vm)
{        
    if(vm.stack_offset < 0)   
    {   
        if(vm.running)
        {
            SHL_LOG_ERROR(env, "Stack underflow");      
            vm.running = false;
        }                                           
        return nullptr;                           
    }

    shl_value* value = &vm.stack[vm.stack_offset];
    --vm.stack_offset;
    return value;
}


#define SHL_OPCODE_TO_BINOP(opcode, out, x, y) \
{                                                 \
    switch(opcode)                                \
    {                                             \
        case SHL_OPCODE_ADD:                      \
            out = x + y;                          \
            break;                                \
        case SHL_OPCODE_SUB:                      \
            out = x - y;                          \
            break;                                \
        case SHL_OPCODE_MUL:                      \
            out = x * y;                          \
            break;                                \
        case SHL_OPCODE_DIV:                      \
            out = x / y;                          \
            break;                                \
    }                                             \
}

void shl_vm_execute_binop(shl_environment& env, shl_vm& vm, shl_opcode opcode)
{
    shl_value* rhs = shl_vm_pop(env, vm);
    shl_value* lhs = shl_vm_pop(env, vm);
    shl_value* target = shl_vm_push(env, vm);
    if(rhs == nullptr || lhs == nullptr || target == nullptr)
        return;


    if(lhs->type == SHL_INT)
    {
        if(rhs->type == SHL_INT)
        {
            target->type == SHL_INT;
            SHL_OPCODE_TO_BINOP(opcode, target->i, lhs->i, rhs->i)
        }
        else if(rhs->type == SHL_FLOAT)
        {
            target->type == SHL_FLOAT;
            SHL_OPCODE_TO_BINOP(opcode, target->f, lhs->i, rhs->f)
        }
        else
        {
            //Undefined, should we support operator overloading? fallback to user?
            vm.running = false;
        }
    }
    else if(lhs->type == SHL_FLOAT)
    {
        if(rhs->type == SHL_INT)
        {
            target->type == SHL_FLOAT;
            SHL_OPCODE_TO_BINOP(opcode, target->f, lhs->f, rhs->i)
        }
        else if(rhs->type == SHL_FLOAT)
        {
            target->type == SHL_FLOAT;
            SHL_OPCODE_TO_BINOP(opcode, target->f, lhs->f, rhs->f)
        }
        else
        {
            //Undefined, should we support operator overloading? fallback to user?
            vm.running = false;
        }
    }
}

void shl_vm_execute(shl_environment& env, shl_vm& vm)
{
    if(vm.bytecode_chunks.size() == 0)
        return;

    shl_bytecode_chunk* chunk = vm.bytecode_chunks[0];
    vm.instruction = &chunk->bytecode[0];
    vm.stack_offset = -1;
    vm.running = true;

    shl_bytecode_value bytecode_value;

    while(vm.running)
    {
        shl_opcode opcode = (shl_opcode) (*vm.instruction);
        switch(opcode)
        {
            case SHL_OPCODE_EXIT:
                vm.running = false;
                break;
            case SHL_OPCODE_NO_OP:
                ++vm.instruction;
                break;
            case SHL_OPCODE_NEXT_CHUNK:
                ++chunk;
                vm.instruction = &chunk->bytecode[0];
                break;
            case SHL_OPCODE_PUSH_INT:   
                for(size_t i = 0; i < sizeof(bytecode_value.i); ++i)
                    bytecode_value.bytes[i] = *(++vm.instruction);

                if(shl_value* value = shl_vm_push(env, vm))
                {
                    value->type = SHL_INT;
                    value->i = bytecode_value.i;
                }
                break;
            case SHL_OPCODE_PUSH_FLOAT:                
                for(size_t i = 0; i < sizeof(bytecode_value.f); ++i)
                    bytecode_value.bytes[i] = *(++vm.instruction);
                    
                if(shl_value* value = shl_vm_push(env, vm))
                {
                    value->type = SHL_FLOAT;
                    value->f = bytecode_value.f;
                }
                break;

            case SHL_OPCODE_ADD:
            case SHL_OPCODE_SUB:
            case SHL_OPCODE_MUL:
            case SHL_OPCODE_DIV:
                shl_vm_execute_binop(env, vm, opcode);
                break;

        }
    }
}

void shl_vm_shutdown(shl_environment& env, shl_vm& vm)
{
    for( shl_bytecode_chunk* chunk : vm.bytecode_chunks)
        delete chunk;
}

// -------------------------------------- Passes ------------------------------------------------// 
bool shl_pass_semantic_check(const shl_ast* node)
{
    if(node == nullptr)
        return false;
    return true;
}

// -------------------------------------- Transformations ---------------------------------------// 
void shl_ast_to_ir(const shl_ast* node, shl_ir& ir)
{
    if(!shl_pass_semantic_check(node))
        return;

    const shl_token token = node->token;
    const shl_array<shl_ast*>& children = node->children;

    switch(node->id)
    {
        case SHL_AST_ROOT:
            assert(children.size() == 1);
            shl_ast_to_ir(children[0], ir);
            break;
        case SHL_AST_BLOCK: 
            shl_ir_push_block(ir, "");
            for (shl_ast* stmt : children)
                shl_ast_to_ir(stmt, ir);
            shl_ir_pop_block(ir);
            break;
        case SHL_AST_ASSIGN:
            //TODO
            assert(0);
            break;
        case SHL_AST_LITERAL:
        {
            shl_ir_operand operand;
            operand.data = token.data;
            switch(token.id)
            {
                case SHL_TOKEN_DECIMAL:     
                    operand.flags = SHL_IR_OPERAND_DECIMAL;     
                    shl_ir_add_operation(ir, SHL_OPCODE_PUSH_INT, operand);
                    break;
                case SHL_TOKEN_HEXIDECIMAL: 
                    operand.flags = SHL_IR_OPERAND_HEXIDECIMAL; 
                    shl_ir_add_operation(ir, SHL_OPCODE_PUSH_INT, operand);
                    break;
                case SHL_TOKEN_BINARY:      
                    operand.flags = SHL_IR_OPERAND_BINARY;
                    shl_ir_add_operation(ir, SHL_OPCODE_PUSH_INT, operand);
                    break;
                case SHL_TOKEN_FLOAT:       
                    operand.flags = SHL_IR_OPERAND_FLOAT;
                    shl_ir_add_operation(ir, SHL_OPCODE_PUSH_FLOAT, operand);
                    break;
                case SHL_TOKEN_STRING:      
                    operand.flags = SHL_IR_OPERAND_STRING;
                    shl_ir_add_operation(ir, SHL_OPCODE_PUSH_STRING, operand);
                    break;
                default: 
                break;
            }
            break;
        }
        case SHL_AST_VARIABLE:
        {
            shl_ir_operand operand;
            operand.data = token.data;
            // TODO: determine if variable is local or global
            shl_ir_add_operation(ir, SHL_OPCODE_PUSH_LOCAL, operand);
            break;
        }
        case SHL_AST_UNARY_OP:
        {
            assert(children.size() == 1);
            shl_ast_to_ir(children[0], ir);

            switch(token.id)
            {
                case SHL_TOKEN_NOT: shl_ir_add_operation(ir, SHL_OPCODE_NOT); break;
                default: break;
            }
            break;
        }
        case SHL_AST_BINARY_OP:
        {
            assert(children.size() == 2);
            shl_ast_to_ir(children[1], ir); // RHS
            shl_ast_to_ir(children[0], ir); // LHS

            switch(token.id)
            {
                case SHL_TOKEN_ADD: shl_ir_add_operation(ir, SHL_OPCODE_ADD); break;
                case SHL_TOKEN_SUB: shl_ir_add_operation(ir, SHL_OPCODE_SUB); break;
                case SHL_TOKEN_MUL: shl_ir_add_operation(ir, SHL_OPCODE_MUL); break;
                case SHL_TOKEN_DIV: shl_ir_add_operation(ir, SHL_OPCODE_DIV); break;
                default: break;
            }
            break;
        }
        case SHL_AST_FUNC_CALL:
        {
            for(const shl_ast* arg : children)
                shl_ast_to_ir(arg, ir);

            shl_ir_operand operand;
            operand.data = token.data;
            operand.flags = SHL_IR_OPERAND_STRING;
            shl_ir_add_operation(ir, SHL_OPCODE_CALL, operand);
            break;
        }
        case SHL_AST_FUNC_DEF:
        {
            assert(0);
            break;
        }
        case SHL_AST_PARAM:
        {
            assert(0);
            break;
        }
    }
}

int shl_bytecode_serialize(shl_array<char> bytes, int& chunk_offset, shl_array<shl_bytecode_chunk*>& bytecode_chunks)
{
    shl_bytecode_chunk* chunk = bytecode_chunks.back();
    if(chunk_offset + bytes.size() >= (SHL_BYTECODE_CHUNK_SIZE-1))
    {
        // pad remaining with NO_OPs
        while(chunk_offset < SHL_BYTECODE_CHUNK_SIZE-1)
        {
            chunk->bytecode[chunk_offset] = SHL_OPCODE_NO_OP;
            chunk_offset++;
        }
        chunk->bytecode[chunk_offset] = SHL_OPCODE_NEXT_CHUNK;
        chunk_offset = 0;
        bytecode_chunks.push_back(new shl_bytecode_chunk());
        chunk = bytecode_chunks.back();
    }

    for(char byte : bytes)
    {
        chunk->bytecode[chunk_offset] = byte;
        chunk_offset++; 
    }
  
    return bytes.size();
}

void shl_ir_to_bytecode(shl_ir& ir, shl_array<shl_bytecode_chunk*>& bytecode_chunks)
{
    int instruction_offset = 0;
    int chunk_offset = 0;
    shl_map<const char*, int> label_offset; 
    shl_bytecode_value value;

    bytecode_chunks.push_back(new shl_bytecode_chunk());

	for(const shl_ir_block& block : ir.module.blocks)
	{
        // TODO: handle JMP operations by translating from label to instr offet
		label_offset[block.label.c_str()] =  instruction_offset;
		for(const shl_ir_operation& operation : block.operations)
		{
            shl_array<char> bytes;
            bytes.push_back((char)operation.opcode);

            const shl_ir_operand& operand = operation.operand;
            if(operand.data.size())
            {
                if(operand.flags & SHL_IR_OPERAND_BINARY)
                {
                    value.i = strtoul(operand.data.c_str(), NULL, 2);
                    for(size_t i = 0; i < sizeof(value.i); ++i)
                        bytes.push_back(value.bytes[i]);
                }
                else if(operand.flags & SHL_IR_OPERAND_HEXIDECIMAL)
                {
                    value.i = strtoul(operand.data.c_str(), NULL, 16);
                    for(size_t i = 0; i < sizeof(value.i); ++i)
                        bytes.push_back(value.bytes[i]);
                }
                else if(operand.flags & SHL_IR_OPERAND_DECIMAL)
                {
                    // Note: Signed string to long call
                    value.i = strtol(operand.data.c_str(), NULL, 10);
                    for(size_t i = 0; i < sizeof(value.i); ++i)
                        bytes.push_back(value.bytes[i]);
                }
                else if(operand.flags & SHL_IR_OPERAND_FLOAT)
                {
                    value.f = strtof(operand.data.c_str(), NULL);
                    for(size_t i = 0; i < sizeof(value.f); ++i)
                        bytes.push_back(value.bytes[i]);
                }
                else if(operand.flags & SHL_IR_OPERAND_STRING)
                {
                    for(size_t i = 0; i < operand.data.size(); ++i)
                        bytes.push_back(operand.data[i]);
                    bytes.push_back(0);
                }
            }

            instruction_offset += shl_bytecode_serialize(bytes, chunk_offset, bytecode_chunks);
		}
	}


    shl_array<char> bytes;
    bytes.push_back(SHL_OPCODE_EXIT);
    instruction_offset += shl_bytecode_serialize(bytes, chunk_offset, bytecode_chunks);
}

// -------------------------------------- API ------ ---------------------------------------// 

void shl_execute(shl_environment& env, const char* filename)
{
    // Parse file
	shl_ast* root = shl_parse_file(env, filename);
	if(root == nullptr)
		return;

    // Generate IR
	shl_ir ir;
	shl_ast_to_ir(root, ir);

    // Load bytecode into VM
    shl_vm vm;
    shl_ir_to_bytecode(ir, vm.bytecode_chunks);

    // Run VM
    shl_vm_execute(env, vm);
}

void shl_evaluate(shl_environment& env, const char* code)
{
    //TODO: compile to byte code chunk, then pass to VM
}

#endif