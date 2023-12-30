#ifndef __SHL_HEADER__
#define __SHL_HEADER__
/*
    Single Header Language 
    Author: 138paulmiller
*/

/*
    TODO: 
    - Semantic Pass: check variable, function, field names, check binary/unary ops
*/

//void shl_register(const char* function_signature, shl_callback callback);
//void shl_unregister(const char* function_signature);

//void shl_compile(const char* filename, const char* compiled_filename);
void shl_evaluate(const char* filename, const char* compiled_filename);

#endif //__SHL_HEADER__

#if defined(SHL_IMPLEMENTATION)

#ifdef _WIN32
#include <windows.h>
#endif 

#include <cassert>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <list>
#include <memory>
#include <vector>


#ifndef __SHL_FUNCTION_NAME__
    #ifdef WIN32
        #define __SHL_FUNCTION_NAME__  __FUNCTION__  
    #else
        #define __SHL_FUNCTION_NAME__  __func__ 
    #endif
#endif

#ifndef SHL_TOKEN_BUFFER_LEN
    #define SHL_TOKEN_BUFFER_LEN 128
#endif//SHL_TOKEN_BUFFER_LEN 

#ifndef SHL_COMMENT_SYMBOL
    #define SHL_COMMENT_SYMBOL '#'
#endif//SHL_COMMENT_SYMBOL 

#ifndef SHL_LOG_PRELUDE
    #define SHL_LOG_PRELUDE "[SHL]"
#endif//SHL_LOG_PRELUDE 

#define shl_log_error(...)\
{\
    fprintf(stderr, "[%s:%d]", __SHL_FUNCTION_NAME__, __LINE__);\
    fprintf(stderr, SHL_LOG_PRELUDE "Error: ");\
    fprintf(stderr, __VA_ARGS__);\
    fprintf(stderr, "\n");\
}

// -------------------------------------- Core  ---------------------------------------//
template <class type>
using shl_list = std::list<type>;

template <class type>
using shl_array = std::vector<type>;

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
    char prec;        // if the token is an operator, this is the precedence (note: higher values take precendece)
    int  argc;       // if the token is an operator, this is the number of operands
    bool capture;     // if non-zero, the token will contain the source string value (i.e. integer and string literals)
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
    std::string data;
    int line;
    int col;
};

struct shl_lexer
{
    shl_token prev;
    shl_token curr;
    shl_token next;

    std::istream* input = nullptr;
    std::string inputname;

    int line, col;
    int prev_line, prev_col;
    std::streampos pos_start;
};

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

void shl_lexer_end_tracking(shl_lexer& lexer, std::string& s)
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

void shl_lexer_tokenize_error(shl_lexer& lexer, shl_token& token, const char* format, ...)
{
    char buffer[2048];
    int len = snprintf(buffer, 2048, "%s(%d,%d):", lexer.inputname.c_str(), lexer.line, lexer.col);

    va_list argptr;
    va_start(argptr, format);
    len += vsnprintf(buffer+len, 2048, format, argptr);
    va_end(argptr);

    token.id = SHL_TOKEN_ERR;
    token.data.copy(buffer, len);
}

bool shl_lexer_tokenize_string(shl_lexer& lexer, shl_token& token)
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
            shl_lexer_tokenize_error(lexer, token, "string literal missing closing \"");
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
                shl_lexer_tokenize_error(lexer, token, "invalid escape character \\%c", c);
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

bool shl_lexer_tokenize_symbol(shl_lexer& lexer, shl_token& token)
{
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

bool shl_lexer_tokenize_label(shl_lexer& lexer, shl_token& token)
{
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

    std::string label;
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

bool shl_lexer_tokenize_number(shl_lexer& lexer, shl_token& token)
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

    std::string str;
    shl_lexer_end_tracking(lexer, str);

    if(id == SHL_TOKEN_ERR)
    {
        shl_lexer_tokenize_error(lexer, token, "invalid numeric format %s", str.c_str());
        return false;
    }
    
    token.id = id;
    token.data = std::move(str);
    return true;
}

shl_token shl_lexer_tokenize(shl_lexer& lexer)
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
        if (!shl_lexer_tokenize_number(lexer, token))
            shl_lexer_tokenize_symbol(lexer, token);
        break;
    case '\"':
        shl_lexer_tokenize_string(lexer, token);
        break;
    default:
        if (shl_lexer_tokenize_symbol(lexer, token))
            break;
        if(shl_lexer_tokenize_label(lexer, token))
            break;
        if (shl_lexer_tokenize_number(lexer, token))
            break;

        shl_lexer_tokenize_error(lexer, token, "invalid character % c", c);
        break;
    }

    token.detail = &shl_token_details[(int)token.id];
    return token;
}

bool shl_lexer_move(shl_lexer& lexer)
{
    if (lexer.next.id == SHL_TOKEN_NONE)
        lexer.next = shl_lexer_tokenize(lexer);        

    lexer.prev = lexer.curr; 
    lexer.curr = lexer.next;
    lexer.next = shl_lexer_tokenize(lexer);

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

bool shl_lexer_open(shl_lexer& lexer, const char* code)
{
    shl_lexer_close(lexer);

    std::istringstream* iss = new std::istringstream(code, std::fstream::in);
    if (iss == nullptr || !iss->good())
    {
        shl_log_error("Lexer failed to open code");
        return false;
    }

    lexer.input = iss;
    lexer.inputname = "code";

    return shl_lexer_move(lexer);
}

bool shl_lexer_open_file(shl_lexer& lexer, const char* filename)
{
    shl_lexer_close(lexer);

    std::fstream* file = new std::fstream(filename, std::fstream::in);
    if (file == nullptr || !file->is_open())
    {
        shl_log_error("Lexer failed to open file %s", filename);
        return false;
    }

    lexer.input = file;
    lexer.inputname = filename;

    return shl_lexer_move(lexer);
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

#define shl_parse_error(lexer,  ...)\
{\
    shl_log_error("Syntax error %s(%d,%d)", lexer.inputname.c_str(), lexer.line, lexer.col);\
    shl_log_error(__VA_ARGS__);\
}

bool shl_parse_expr_pop(shl_lexer& lexer,  shl_list<shl_token>&op_stack, shl_list<shl_ast*>& expr_stack)
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
        shl_parse_error(lexer,  "Invalid number of arguments to operator %s", next_op.detail->label);
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

shl_ast* shl_parse_value(shl_lexer& lexer); 

shl_ast* shl_parse_expr(shl_lexer& lexer)
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
                if(!shl_parse_expr_pop(lexer,  op_stack, expr_stack))
                    return nullptr;
            }

            op_stack.push_back(op);
            shl_lexer_move(lexer);
        }
        else
        {
            shl_ast* value = shl_parse_value(lexer);
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
        if(!shl_parse_expr_pop(lexer,  op_stack, expr_stack))
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
        shl_parse_error(lexer,  "Invalid expression syntax");
        return nullptr;
    }

    return expr_stack.back();
}

shl_ast* shl_parse_funccall(shl_lexer& lexer)
{
    shl_token idtoken = lexer.curr;
    if (idtoken.id != SHL_TOKEN_ID)
        return nullptr;

    if (lexer.next.id != SHL_TOKEN_LPAREN)
        return nullptr;

    shl_lexer_move(lexer); // eat id
    shl_lexer_move(lexer); // eat (

    shl_array<shl_ast*> args;
    if (shl_ast* expr = shl_parse_expr(lexer))
    {
        args.push_back(expr);

        while (expr && lexer.curr.id == SHL_TOKEN_COMMA)
        {
            shl_lexer_move(lexer); // eat ,

            if((expr = shl_parse_expr(lexer)))
                args.push_back(expr);
        }
    }

    if (lexer.curr.id != SHL_TOKEN_RPAREN)
    {
        shl_parse_error(lexer,  "Function call missing closing parentheses");
        for(shl_ast* arg : args)
            delete arg;
        return nullptr;
    }

    shl_lexer_move(lexer); // eat )

    shl_ast* funccall = shl_ast_new(SHL_AST_FUNC_CALL, idtoken);
    funccall->children = std::move(args);
    return funccall;
}

shl_ast* shl_parse_value(shl_lexer& lexer)
{
    shl_token token = lexer.curr;
    switch (token.id)
    {
    case SHL_TOKEN_ID:
    {   
        if (shl_ast* funccall = shl_parse_funccall(lexer))
            return funccall;

        shl_lexer_move(lexer);
        shl_ast* var = shl_ast_new(SHL_AST_VARIABLE, token);
        return var;
    }
    case SHL_TOKEN_DECIMAL:
    case SHL_TOKEN_HEXIDECIMAL:
    case SHL_TOKEN_BINARY:
    case SHL_TOKEN_FLOAT:
    case SHL_TOKEN_STRING:
    {
        shl_lexer_move(lexer);
        shl_ast* literal = shl_ast_new(SHL_AST_LITERAL, token);
        literal->id = SHL_AST_LITERAL;
        literal->token = token;
        return literal;
    }
    case SHL_TOKEN_LPAREN:
    {
        shl_lexer_move(lexer); // eat the LPAREN
        shl_ast* expr = shl_parse_expr(lexer);
        if (lexer.curr.id != SHL_TOKEN_RPAREN)
        {
            shl_parse_error(lexer,  "Expression missing closing parentheses");
            shl_ast_delete(expr);    
            return nullptr;
        }
        shl_lexer_move(lexer); // eat the RPAREN
        return expr;
    }
    default: 
    break;
    }
    return nullptr;
}

shl_ast* shl_parse_stmt(shl_lexer& lexer)
{
    //TODO: assignment, funcdef etc..
    // Default, epxression parsing. 
    if (shl_ast* expr = shl_parse_expr(lexer))
    {
        if(lexer.curr.id != SHL_TOKEN_SEMICOLON)
        {
            shl_ast_delete(expr);    
            shl_parse_error(lexer,  "Missing semicolon at end of expression");
            return nullptr;
        }

        shl_lexer_move(lexer); // eat ;
        return expr;
    }
    return nullptr;
}

shl_ast* shl_parse_block(shl_lexer& lexer)
{
    shl_array<shl_ast*> stmts;
    while(shl_ast* stmt = shl_parse_stmt(lexer))
        stmts.push_back(stmt);

    if(stmts.size() == 0)
        return nullptr;

    shl_ast* block = shl_ast_new(SHL_AST_BLOCK);
    block->children = std::move(stmts);
    return block;
}

shl_ast* shl_parse_root(shl_lexer& lexer)
{
    shl_ast* block = shl_parse_block(lexer);
    if(block == nullptr)
        return nullptr;

    shl_ast* root = new shl_ast();
    root->id = SHL_AST_ROOT;
    root->children = { std::move(block) };
    return root;
}

shl_ast* shl_parse(const char* code)
{
    shl_lexer lexer;
    if(!shl_lexer_open(lexer, code))
        return nullptr;

    shl_ast* root = shl_parse_root(lexer);
    shl_lexer_close(lexer);

    return root;
}

shl_ast* shl_parse_file(const char* filename)
{
    shl_lexer lexer;
    if(!shl_lexer_open_file(lexer, filename))
        return nullptr;

    shl_ast* root = shl_parse_root(lexer);
    shl_lexer_close(lexer);

    return root;
}

// -------------------------------------- IR ---------------------------------------// 
#define SHL_OPCODE_LIST\
	SHL_OPCODE(NO_OP)          \
	SHL_OPCODE(EXIT)           \
	SHL_OPCODE(PUSH)           \
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
	SHL_OPCODE(IJ)             \
	SHL_OPCODE(IJE)            \
	SHL_OPCODE(CALL)           \
	SHL_OPCODE(RET)            \
	SHL_OPCODE(ASSERT)         \
	SHL_OPCODE(ASSERT_POSITIVE)\
	SHL_OPCODE(ASSERT_BOUND)

enum class shl_opcode : uint8_t
{ 
	#define SHL_OPCODE(opcode) opcode,
	SHL_OPCODE_LIST
	#undef SHL_OPCODE
};

static const char* shl_opcode_labels[] =
{
	#define SHL_OPCODE(opcode) #opcode,
	SHL_OPCODE_LIST
	#undef SHL_OPCODE
}; 

#undef SHL_OPCODE_LIST

enum shl_ir_flags : uint16_t
{
    // scope
    CONST = 1, 
    LOCAL = 1 << 2,
    GLOBAL = 1 << 3,
    // if constant, type of literal
    DECIMAL = 1 << 4,
    HEXIDECIMAL = 1 << 5,
    BINARY = 1 << 6,
    FLOAT = 1 << 7, 
    STRING = 1 << 8, 
};

struct shl_ir_operand
{
    std::string data;
    uint16_t flags = 0;
};

struct shl_ir_operation
{
    shl_opcode opcode;
    shl_ir_operand operand; //optional parameter. will be encoded in following bytes
};

struct shl_ir_block
{
    std::string label;
    shl_list<shl_ir_operation> operations;
};

struct shl_ir_module
{
    shl_list<shl_ir_block> blocks;
};

class shl_ir_builder
{
public:
    ~shl_ir_builder() { delete _module; }

    void new_module();
    shl_ir_module* get_module() { return _module; }

    void push_block(const std::string& label);
    void pop_block(); 

    void add_operation(shl_opcode opcode);
    void add_operation(shl_opcode opcode, const shl_ir_operand& param);

private:		
    shl_ir_module* _module = nullptr; 
    shl_list<shl_ir_block> _block_stack; 
    int _generated_label_count;
};

void shl_ir_builder::new_module()
{
    delete _module;
    _module = new shl_ir_module();
    _generated_label_count = 0;
    _block_stack.clear(); 
}

void shl_ir_builder::push_block(const std::string& label)
{
    if(label.size() == 0)
    {
        const std::string& generated_label = "L" + std::to_string(_generated_label_count++);

        shl_ir_block block;
        block.label = std::string(generated_label.c_str(), generated_label.size());
        _block_stack.push_back(block);
    }
    else
    {
        shl_ir_block block;
        block.label = label;
        _block_stack.push_back(block);
    }
}

void shl_ir_builder::pop_block()
{
    assert(_module != nullptr && _block_stack.size() > 0);
    _module->blocks.push_front(_block_stack.back());
    _block_stack.pop_back();
}

void shl_ir_builder::add_operation(shl_opcode opcode)
{
    shl_ir_operand operand;
    add_operation(opcode, operand);
}

void shl_ir_builder::add_operation(shl_opcode opcode, const shl_ir_operand& operand)
{
    shl_ir_operation operation;
    operation.opcode = opcode;
    operation.operand = operand;
    _block_stack.back().operations.push_back(std::move(operation));
}
// -------------------------------------- Passes ---------------------------------------// 

bool shl_pass_semantic_check(const shl_ast* node)
{
    if(node == nullptr)
        return false;
    return true;
}

// -------------------------------------- Transformations ---------------------------------------// 
void shl_ast_to_ir(const shl_ast* node, shl_ir_builder* ir)
{
    if(!shl_pass_semantic_check(node))
        return;

    const shl_token token = node->token;
    const shl_array<shl_ast*>& children = node->children;

    switch(node->id)
    {
        case SHL_AST_ROOT:
            assert(children.size() == 1);
            ir->new_module();
            shl_ast_to_ir(children[0], ir);
            break;
        case SHL_AST_BLOCK: 
            ir->push_block("");
            for (shl_ast* stmt : children)
                shl_ast_to_ir(stmt, ir);
            ir->pop_block();
            break;
        case SHL_AST_ASSIGN:
            //TODO
            assert(0);
            break;
        case SHL_AST_LITERAL:
        {
            shl_ir_operand operand;
            operand.data = token.data;
            operand.flags = shl_ir_flags::CONST;
            switch(token.id)
            {
                case SHL_TOKEN_DECIMAL:     operand.flags |= shl_ir_flags::DECIMAL;     break;
                case SHL_TOKEN_HEXIDECIMAL: operand.flags |= shl_ir_flags::HEXIDECIMAL; break;
                case SHL_TOKEN_BINARY:      operand.flags |= shl_ir_flags::BINARY;      break;
                case SHL_TOKEN_FLOAT:       operand.flags |= shl_ir_flags::FLOAT;       break;
                case SHL_TOKEN_STRING:      operand.flags |= shl_ir_flags::STRING;      break;
                default: break;
            }
            ir->add_operation(shl_opcode::PUSH, operand);
            break;
        }
        case SHL_AST_VARIABLE:
        {
            shl_ir_operand operand;
            operand.data = token.data;
            operand.flags = shl_ir_flags::LOCAL;
            ir->add_operation(shl_opcode::PUSH, operand);
            break;
        }
        case SHL_AST_UNARY_OP:
        {
            assert(children.size() == 1);
            shl_ast_to_ir(children[0], ir);

            switch(token.id)
            {
                case SHL_TOKEN_NOT: ir->add_operation(shl_opcode::NOT); break;
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
                case SHL_TOKEN_ADD: ir->add_operation(shl_opcode::ADD); break;
                case SHL_TOKEN_SUB: ir->add_operation(shl_opcode::SUB); break;
                case SHL_TOKEN_MUL: ir->add_operation(shl_opcode::MUL); break;
                case SHL_TOKEN_DIV: ir->add_operation(shl_opcode::DIV); break;
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
            operand.flags = shl_ir_flags::STRING;
            ir->add_operation(shl_opcode::CALL, operand);
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

void shl_evaluate(const char* filename, const char* compiled_filename)
{
    //TODO: compile to byte code chunk, then pass to VM
}

#endif