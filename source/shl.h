#ifndef __SHL_HEADER__
#define __SHL_HEADER__
/*
    Single Header Language 
    Author: 138paulmiller
*/

/*
    TODO: 
    1. Parsing: Handle Unary Operator expression parsing 
    2. Semantic Pass: check variable, function, field names, check binary/unary ops
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

#ifndef SHL_MAX_TOKEN_LENGTH
    #define SHL_MAX_TOKEN_LENGTH 128
#endif//SHL_MAX_TOKEN_LENGTH 

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
#define SHL_TOKEN_LIST                        \
    /* State */                               \
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
enum class shl_token_id : uint8_t 
{
#define SHL_TOKEN(id, ...) id,
    SHL_TOKEN_LIST
#undef SHL_TOKEN
    COUNT
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
static shl_token_detail shl_token_details[(int)shl_token_id::COUNT] = 
{
    #define SHL_TOKEN(id, ...) { #id, __VA_ARGS__},
        SHL_TOKEN_LIST
    #undef SHL_TOKEN
};

// Token instance
struct shl_token
{
    shl_token_id id;
    const shl_token_detail* detail; 
    std::string data;
    int line;
    int col;
};

#undef TOKEN_LIST

class shl_lexer
{
    shl_token _prev;
    shl_token _curr;
    shl_token _next;

    std::istream* _input;
    std::string _inputname;

    int _line, _col;
    int _prev_line, _prev_col;
    std::streampos _pos_start;

public:
    shl_lexer();
    ~shl_lexer();

    bool open(const char* line);
    bool open_file(const char* filename);
    void close();

    bool move();   //move to next token position

    inline int line() { return _line; }
    inline int col() { return _col; }
    inline std::string inputname() { return _inputname; }
    inline const shl_token& prev() { return _prev; }
    inline const shl_token& curr() {return _curr;}    
    inline const shl_token& next() {return _next;}

private:
    char _get();
    char _unget();
    char _peek();

    void _start_tracking();
    void _end_tracking(std::string& s);

    shl_token _tokenize();
    bool _tokenize_string(shl_token& token);
    bool _tokenize_symbol(shl_token& token);
    bool _tokenize_label(shl_token& token);
    bool _tokenize_number(shl_token& token);
    void _tokenize_error(shl_token& token, const char* format, ...);
};

shl_lexer::shl_lexer()
    : _input(nullptr)
    , _line(0)
    , _col(0)
{
}

shl_lexer::~shl_lexer()
{
    close();
}

void shl_lexer::close()
{
    if (_input)
        delete _input;

    _input = nullptr;
    _line = 0;
    _col = 0;
    _prev_line = 0;
    _prev_col = 0;
    _pos_start = 0;

    _prev = shl_token();
    _curr = shl_token();
    _next = shl_token();
}

bool shl_lexer::open(const char* code)
{
    close();

    std::istringstream* iss = new std::istringstream(code, std::fstream::in);
    if (iss == nullptr || !iss->good())
    {
        shl_log_error("Lexer failed to open code");
        return false;
    }

    _input = iss;
    _inputname = "code";

    return true;
}

bool shl_lexer::open_file(const char* filename)
{
    close();

    std::fstream* file = new std::fstream(filename, std::fstream::in);
    if (file == nullptr || !file->is_open())
    {
        shl_log_error("Lexer failed to open file %s", filename);
        return false;
    }

    _input = file;
    _inputname = filename;
    return true;
}

bool shl_lexer::move()
{
    if (_next.id == shl_token_id::NONE)
        _next = _tokenize();        

    _prev = _curr; 
    _curr = next();
    _next = _tokenize();

    return _curr.id != shl_token_id::NONE;
}

char shl_lexer::_get()
{
    const char c = _input->get();
    
    _prev_line = _line;
    _prev_col = _col;

    ++_col;

    if (c == '\n')
    {
        ++_line;
        _col = 0;
    }
    return c;
}

char shl_lexer::_peek()
{
    return _input->peek();
}

char shl_lexer::_unget()
{
    _input->unget();

    _line = _prev_line;
    _col = _prev_col;

    return _peek();
}

void shl_lexer::_start_tracking()
{
    _pos_start = _input->tellg();
}

void shl_lexer::_end_tracking(std::string& s)
{
    const std::fstream::iostate state = _input->rdstate();
    _input->clear();

    const std::streampos pos_end = _input->tellg();
    const int len = pos_end - _pos_start;

    static char buffer[SHL_MAX_TOKEN_LENGTH];

    _input->seekg(_pos_start);
    _input->read(buffer, len);
    _input->seekg(pos_end);
    _input->clear(state);

    s.assign(buffer, len);
}

shl_token shl_lexer::_tokenize()
{
    shl_token token;

    // if file is not open, or already at then end. return invalid token
    if (_input == nullptr || _peek() == EOF)
    {
        token.id = shl_token_id::NONE;
        token.detail = &shl_token_details[(int)token.id];
        return token;
    }

    char c = _peek();

    // skip whitespace
    if (isspace(c))
    {
        while (isspace(c))
            c = _get();
        _unget();
    }

    // skip comments
    if (c == SHL_COMMENT_SYMBOL)
    {
        while (c != EOF && c != '\n')
            c = _get();
        if (c == '\n')
            c = _peek();
    }

    // handle eof
    if (c == EOF)
    {
        token.id = shl_token_id::END;
        token.detail = &shl_token_details[(int)token.id];
        return token;
    }

    token.col = _col;
    token.line = _line;

    switch (c)
    {
    case '.': 
    case '+':
    case '-':
        if (!_tokenize_number(token))
            _tokenize_symbol(token);
        break;
    case '\"':
        _tokenize_string(token);
        break;
    default:
        if (_tokenize_symbol(token))
            break;
        if(_tokenize_label(token))
            break;
        if (_tokenize_number(token))
            break;

        _tokenize_error(token, "invalid character % c", c);
        break;
    }

    token.detail = &shl_token_details[(int)token.id];
    return token;
}

bool shl_lexer::_tokenize_string(shl_token& token)
{
    _start_tracking();

    char c = _get();
    if (c != '\"')
    {
        _unget();
        return false;
    }

    do
    {
        c = _get();
        if (c == EOF)
        {
            _tokenize_error(token, "string literal missing closing \"");
            return false;
        }

        if (c == '\\')
        {
            // handle escaped chars
            c = _get();
            switch (c)
            {
            case '\'': case '\"': case '\\':
            case '0': case 'a': case 'b': case 'f': case 'n': case 'r': case 't': case 'v':
                c = _get();
                break;
            default:
            {
                _tokenize_error(token, "invalid escape character \\%c", c);
                while (c != '\"')
                {
                    if (c == EOF)
                        break;
                    c = _get();
                }
                return false;
            }
            }
        }
    } while (c != '\"');

    token.id = shl_token_id::STRING;
    _end_tracking(token.data);

    return true;
}

bool shl_lexer::_tokenize_symbol(shl_token& token)
{
    shl_token_id id = shl_token_id::ERR;

    char c = _get();
    switch (c)
    {
    case '.': id = shl_token_id::DOT;       break;
    case ',': id = shl_token_id::COMMA;     break;
    case ':': id = shl_token_id::COLON;     break;
    case ';': id = shl_token_id::SEMICOLON; break;
    case '(': id = shl_token_id::LPAREN;    break;
    case ')': id = shl_token_id::RPAREN;    break;
    case '[': id = shl_token_id::LBRACKET;  break;
    case ']': id = shl_token_id::RBRACKET;  break;
    case '{': id = shl_token_id::LBRACE;    break;
    case '}': id = shl_token_id::RBRACE;    break;
    case '&': id = shl_token_id::AND;       break;
    case '|': id = shl_token_id::OR;        break;
    case '*':
        if (_peek() == '=' && _get())
            id = shl_token_id::MUL_EQUAL;
        else
            id = shl_token_id::MUL;
        break;
    case '/':
        if (_peek() == '=' && _get())
            id = shl_token_id::DIV_EQUAL;
        else
            id = shl_token_id::DIV;
        break;
    case '^':
        if (_peek() == '=' && _get())
            id = shl_token_id::POW_EQUAL;
        else
            id = shl_token_id::POW;
        break;
    case '<':
        if (_peek() == '=' && _get())
            id = shl_token_id::LESS_EQUAL;
        else
            id = shl_token_id::LESS;
        break;
    case '>':
        if (_peek() == '=' && _get())
            id = shl_token_id::GREATER_EQUAL;
        else
            id = shl_token_id::GREATER;
        break;
    case '=':
        if (_peek() == '=' && _get())
            id = shl_token_id::GREATER_EQUAL;
        else
            id = shl_token_id::ASSIGN;
        break;
    case '!':
        if (_peek() == '=' && _get())
            id = shl_token_id::NOT_EQUAL;
        else
            id = shl_token_id::NOT;
        break;
    case '+':
        if (_peek() == '=' && _get())
            id = shl_token_id::ADD_EQUAL;
        else
            id = shl_token_id::ADD;
        break;
    case '-':
        if (_peek() == '=' && _get())
            id = shl_token_id::SUB_EQUAL;
        else
            id = shl_token_id::SUB;
        break;
    }

    if (id == shl_token_id::ERR)
    {
        _unget();
        return false;
    }

    token.id = id;
    return true;
}

bool shl_lexer::_tokenize_label(shl_token& token)
{
    _start_tracking();

    char c = _get();
    if (c != '_' && !isalpha(c))
    {
        _unget();
        return false;
    }
    
    while (c == '_' || isalnum(c))
        c = _get();
    _unget();

    std::string label;
    _end_tracking(label);

    // find token id for keyword
    shl_token_id id = shl_token_id::ID;
    for (int i = 0; i < (int)shl_token_id::COUNT; ++i)
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

bool shl_lexer::_tokenize_number(shl_token& token)
{
    _start_tracking();

    char c = _get();
    if (c != '.' && !isdigit(c) && c != '+' && c != '-')
    {
        _unget();
        return false;
    }

    shl_token_id id = shl_token_id::ERR;

    if (c == '0' && _peek() == 'x')
    {
        id = shl_token_id::HEXIDECIMAL;

        c = _get(); //eat x
        c = _get();

        while (isalnum(c))
        {
            if (!isdigit(c) && !((c >='a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                id = shl_token_id::ERR;

            c = _get();
        }
        _unget();
    }
    else if (c == '0' && _peek() == 'b')
    {
        id = shl_token_id::BINARY;

        c = _get(); // eat b
        c = _get();

        while (isdigit(c))
        {
            if(c != '0' && c != '1')
                id = shl_token_id::ERR;

            c = _get();
        }
        _unget();
    }
    else
    {
        if (c == '+' || c == '-')
        {
            c = _peek();
            if (c != '.' && !isdigit(c))
            {
                _unget();
                return false;
            }
            c = _get();
        }

        if (c == '.')
            id = shl_token_id::FLOAT;
        else 
            id = shl_token_id::DECIMAL;

        bool dot = false;
        while (c == '.' || isdigit(c))
        {
            if (c == '.')
            {
                if (dot)
                    id = shl_token_id::ERR;
                else
                    id = shl_token_id::FLOAT;

                dot = true;
            }
            c = _get();
        }
        _unget();
    }

    std::string str;
    _end_tracking(str);

    if(id == shl_token_id::ERR)
    {
        _tokenize_error(token, "invalid numeric format %s", str.c_str());
        return false;
    }
    
    token.id = id;
    token.data = std::move(str);
    return true;
}

void shl_lexer::_tokenize_error(shl_token& token, const char* format, ...)
{
    char buffer[2048];
    int len = snprintf(buffer, 2048, "%s(%d,%d):", _inputname.c_str(), _line, _col);

    va_list argptr;
    va_start(argptr, format);
    len += vsnprintf(buffer+len, 2048, format, argptr);
    va_end(argptr);

    token.id = shl_token_id::ERR;
    token.data.copy(buffer, len);
}

// -------------------------------------- Parser ---------------------------------------// 
class shl_ast
{
public:
    enum type : uint8_t
    {
        ROOT, BLOCK, 
        ASSIGN, 
        LITERAL, 
        VARIABLE, 
        UNARY_OP, 
        BINARY_OP, 
        FUNC_CALL,
        FUNC_DEF, 
        PARAM
    };

    ~shl_ast() 
    {
        for(shl_ast* child : children)
            delete child;
    }

    shl_token token;
    type type;
    shl_array<shl_ast*> children;
};


#define shl_parse_error(lexer, ...)\
{\
    shl_log_error("Syntax error %s(%d,%d)", lexer.inputname().c_str(), lexer.line(), lexer.col());\
    shl_log_error(__VA_ARGS__);\
}

bool shl_parse_expr_pop(shl_lexer& lexer, shl_list<shl_token>&op_stack, shl_list<shl_ast*>& expr_stack)
{
    shl_token next_op = op_stack.back();
    op_stack.pop_back();

    const int op_argc = (size_t)next_op.detail->argc;
    assert(op_argc == 1 || op_argc == 2); // Only supported operator types

    if(expr_stack.size() < (size_t)op_argc)
    {
        while(expr_stack.size() > 0)
        {
            delete expr_stack.back();
            expr_stack.pop_back();
        }
        shl_parse_error(lexer, "Invalid number of arguments to operator %s", next_op.detail->label);
        return false;
    }

    // Push binary op onto stack
    shl_ast* binaryop = new shl_ast();
    binaryop->type = (op_argc == 2) ? shl_ast::BINARY_OP : shl_ast::UNARY_OP;
    binaryop->token = next_op;
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
    
    while(lexer.curr().id != shl_token_id::SEMICOLON)
    {
        shl_token op = lexer.curr();
        if(op.detail->prec > 0)
        {
            // left associate by default (for right, <= becomes <)
            while(op_stack.size() && op_stack.back().detail->prec >= op.detail->prec)
            {
                if(!shl_parse_expr_pop(lexer, op_stack, expr_stack))
                    return nullptr;
            }

            op_stack.push_back(op);
            lexer.move();
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
        if(!shl_parse_expr_pop(lexer, op_stack, expr_stack))
            return nullptr;
    }

    // Not a valid expression. Either malformed or missing semicolon 
    if(expr_stack.size() == 0 || expr_stack.size() > 1)
    {
        while(expr_stack.size() > 0)
        {
            delete expr_stack.back();
            expr_stack.pop_back();
        }
        shl_parse_error(lexer, "Invalid expression syntax");
        return nullptr;
    }

    return expr_stack.back();
}

shl_ast* shl_parse_funccall(shl_lexer& lexer)
{
    shl_token idtoken = lexer.curr();
    if (idtoken.id != shl_token_id::ID)
        return nullptr;

    if (lexer.next().id != shl_token_id::LPAREN)
        return nullptr;

    lexer.move(); // eat id
    lexer.move(); // eat (

    shl_array<shl_ast*> args;
    if (shl_ast* expr = shl_parse_expr(lexer))
    {
        args.push_back(expr);

        while (expr && lexer.curr().id == shl_token_id::COMMA)
        {
            lexer.move(); // eat ,

            if((expr = shl_parse_expr(lexer)))
                args.push_back(expr);
        }
    }

    if (lexer.curr().id != shl_token_id::RPAREN)
    {
        shl_parse_error(lexer, "Function call missing closing parentheses");
        for(shl_ast* arg : args)
            delete arg;
        return nullptr;
    }
    lexer.move(); // eat )

    shl_ast* funccall = new shl_ast();
    funccall->type = shl_ast::FUNC_CALL;
    funccall->token = idtoken;
    funccall->children = std::move(args);
    return funccall;
}

shl_ast* shl_parse_value(shl_lexer& lexer)
{

    shl_token token = lexer.curr();
    switch (token.id)
    {
    case shl_token_id::ID:
    {   
        if (shl_ast* funccall = shl_parse_funccall(lexer))
            return funccall;

        lexer.move();
        shl_ast* var = new shl_ast();
        var->type = shl_ast::VARIABLE;
        var->token = token;
        return var;
    }
    case shl_token_id::DECIMAL:
    case shl_token_id::HEXIDECIMAL:
    case shl_token_id::BINARY:
    case shl_token_id::FLOAT:
    case shl_token_id::STRING:
    {
        lexer.move();
        shl_ast* literal = new shl_ast();
        literal->type = shl_ast::LITERAL;
        literal->token = token;
        return literal;
    }
    case shl_token_id::LPAREN:
    {
        lexer.move(); // eat the LPAREN
        shl_ast* expr = shl_parse_expr(lexer);
        if (lexer.curr().id != shl_token_id::RPAREN)
        {
            shl_parse_error(lexer, "Expression missing closing parentheses");
            delete expr;    
            return nullptr;
        }
        lexer.move(); // eat the RPAREN
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
        if(lexer.curr().id != shl_token_id::SEMICOLON)
        {
            delete expr;
            shl_parse_error(lexer, "Missing semicolon at end of expression");
            return nullptr;
        }
        lexer.move(); // eat ;
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

    shl_ast* block = new shl_ast();
    block->type = shl_ast::BLOCK;
    block->children = std::move(stmts);
    return block;
}

shl_ast* shl_parse(const char* code)
{
    shl_lexer lexer;
    lexer.open(code);
    lexer.move();

    shl_ast* block = shl_parse_block(lexer);
    if(block == nullptr)
        return nullptr;

    shl_ast* root = new shl_ast();
    root->type = shl_ast::ROOT;
    root->children = { std::move(block) };
    return root;
}

shl_ast* shl_parse_file(const char* filename)
{
    shl_lexer lexer;
    lexer.open_file(filename);
    lexer.move();

    shl_ast* block = shl_parse_block(lexer);
    if(block == nullptr)
        return nullptr;

    shl_ast* root = new shl_ast();
    root->type = shl_ast::ROOT;
    root->children = { std::move(block) };
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

class shl_ir_operand
{
public:
    shl_ir_operand() : flags(0) {}
    bool valid() const { return flags != 0; }
    bool check_flag(shl_ir_flags bit) const { return (flags & bit) != 0;}
    void set_flag(shl_ir_flags bit) { flags |= bit; }

    std::string data;
    uint16_t flags;
};

class shl_ir_operation
{
public:
    shl_opcode opcode;
    shl_ir_operand operand; //optional parameter. will be encoded in following bytes
};

class shl_ir_block
{
public:
    std::string label;
    shl_list<shl_ir_operation> operations;
};

class shl_ir_module
{
public:
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

    switch(node->type)
    {
        case shl_ast::ROOT:
            assert(children.size() == 1);
            ir->new_module();
            shl_ast_to_ir(children[0], ir);
            break;
        case shl_ast::BLOCK: 
            ir->push_block("");
            for (shl_ast* stmt : children)
                shl_ast_to_ir(stmt, ir);
            ir->pop_block();
            break;
        case shl_ast::ASSIGN:
            //TODO
            assert(0);
            break;
        case shl_ast::LITERAL:
        {
            shl_ir_operand operand;
            operand.data = token.data;
            operand.set_flag(shl_ir_flags::CONST);
            switch(token.id)
            {
                case shl_token_id::DECIMAL:     operand.set_flag(shl_ir_flags::DECIMAL);     break;
                case shl_token_id::HEXIDECIMAL: operand.set_flag(shl_ir_flags::HEXIDECIMAL); break;
                case shl_token_id::BINARY:      operand.set_flag(shl_ir_flags::BINARY);      break;
                case shl_token_id::FLOAT:       operand.set_flag(shl_ir_flags::FLOAT);       break;
                case shl_token_id::STRING:      operand.set_flag(shl_ir_flags::STRING);      break;
                default: break;
            }
            ir->add_operation(shl_opcode::PUSH, operand);
            break;
        }
        case shl_ast::VARIABLE:
        {
            shl_ir_operand operand;
            operand.data = token.data;
            operand.set_flag(shl_ir_flags::LOCAL);
            ir->add_operation(shl_opcode::PUSH, operand);
            break;
        }
        case shl_ast::UNARY_OP:
        {
            assert(children.size() == 1);
            shl_ast_to_ir(children[0], ir);

            switch(token.id)
            {
                case shl_token_id::NOT: ir->add_operation(shl_opcode::NOT); break;
                default: break;
            }
            break;
        }
        case shl_ast::BINARY_OP:
        {
            assert(children.size() == 2);
            shl_ast_to_ir(children[1], ir); // RHS
            shl_ast_to_ir(children[0], ir); // LHS

            switch(token.id)
            {
                case shl_token_id::ADD: ir->add_operation(shl_opcode::ADD); break;
                case shl_token_id::SUB: ir->add_operation(shl_opcode::SUB); break;
                case shl_token_id::MUL: ir->add_operation(shl_opcode::MUL); break;
                case shl_token_id::DIV: ir->add_operation(shl_opcode::DIV); break;
                default: break;
            }
            break;
        }
        case shl_ast::FUNC_CALL:
        {
            for(const shl_ast* arg : children)
                shl_ast_to_ir(arg, ir);

            shl_ir_operand operand;
            operand.data = token.data;
            operand.set_flag(shl_ir_flags::STRING);
            ir->add_operation(shl_opcode::CALL, operand);
            break;
        }
        case shl_ast::FUNC_DEF:
        {
            assert(0);
            break;
        }
        case shl_ast::PARAM:
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