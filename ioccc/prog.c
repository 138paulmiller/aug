#define AUG_STACK_SIZE (1024 * 16)
#define AUG_EXTENSION_SIZE 64
#define AUG_APPROX_THRESHOLD 0.0000001
#define AUG_ALLOC(type) (type*)(malloc(sizeof(type)))
#define AUG_ALLOC_ARRAY(type, count) (type*)(malloc(sizeof(type)*count))
#define AUG_REALLOC_ARRAY(ptr, type, count) (type*)(realloc(ptr, sizeof(type)*count))
#define AUG_FREE(ptr) free(ptr)
#define AUG_FREE_ARRAY(ptr) free(ptr)

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct OBJ
{
    char type;
    union 
    {
        bool b;
        int i; 
        char c;
        float f;
        struct STR* str;
        struct aug_array* array;
    };
} OBJ;

typedef struct STR { char *buffer; int ref_count; size_t capacity; size_t length; } STR;

const char* TYPES[] = { "bool", "char", "int", "float", "string", "array", "none" };
STR* STR_new(size_t size) 
{
	STR* string = AUG_ALLOC(STR);
	string->ref_count = 1;
	string->length = 0;
	string->capacity = size;
	string->buffer = AUG_ALLOC_ARRAY(char, string->capacity);
	return string;
}
STR* STR_create(const char* bytes) 
{
	STR* string = AUG_ALLOC(STR);
	string->ref_count = 1;
	string->length = strlen(bytes);
	string->capacity = string->length + 1;
	string->buffer = AUG_ALLOC_ARRAY(char, string->capacity);
    strcpy(string->buffer, bytes);
	return string;
}
void STR_reserve(STR* string, size_t size) 
{
	string->capacity = size;
    string->buffer = AUG_REALLOC_ARRAY(string->buffer, char, string->capacity);
}
void STR_push(STR* string, char c) 
{
	if(string->length + 1 >= string->capacity) 
        STR_reserve(string, 2 * string->capacity);
    string->buffer[string->length++] = c;
    string->buffer[string->length] = '\0';
}

char STR_pop(STR* string) 
{
	return string->length > 0 ? string->buffer[--string->length] : -1;
}

char STR_at(const STR* string, size_t index) 
{
	return index < string->length ? string->buffer[index] : -1;
}

char STR_back(const STR* string) 
{
	return string->length > 0 ? string->buffer[string->length-1] : -1;
}
bool STR_compare(const STR* a, const STR* b) 
{
	if(a == NULL || b == NULL || a->length != b->length)
        return 0; 
    return strncmp(a->buffer, b->buffer, a->length) == 0;
}
bool STR_compare_bytes(const STR* a, const char* bytes) 
{
    if(bytes == NULL)
        return a->buffer == NULL;

    size_t len = strlen(bytes);
    if(len != a->length)
        return 0;

    size_t i;
    for(i = 0; i < a->length; ++i)
    {
        if(a->buffer[i] != bytes[i])
            return 0;

    }
    return true;
}
void STR_incref(STR* string) 
{
    if(string != NULL)
	    string->ref_count++;
}
STR* STR_decref(STR* string) 
{
	if(string != NULL && --string->ref_count == 0)
    {
        AUG_FREE_ARRAY(string->buffer);
        AUG_FREE(string);
        return NULL;
    }
    return string;
}
typedef struct aug_array { OBJ* buffer; int ref_count; size_t capacity; size_t length; } aug_array;
aug_array* aug_array_new(size_t size)
{                
	aug_array* array = AUG_ALLOC(aug_array);
	array->ref_count = 1;   
	array->length = 0;      
	array->capacity = size; 
	array->buffer = AUG_ALLOC_ARRAY(OBJ, array->capacity );
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
	array->capacity = size; array->buffer = AUG_REALLOC_ARRAY(array->buffer, OBJ, array->capacity);
}
OBJ* aug_array_push(aug_array* array)  
{
	if(array->length + 1 >= array->capacity)   
        aug_array_resize(array, 2 * array->capacity);     
    return &array->buffer[array->length++];     
}
OBJ* aug_array_pop(aug_array* array)
{
	return array->length > 0 ? &array->buffer[--array->length] : NULL; 
}
OBJ* aug_array_at(const aug_array* array, size_t index) 
{
	return index < array->length ? &array->buffer[index] : NULL;       
}
OBJ* aug_array_back(const aug_array* array)             
{
	return array->length > 0 ? &array->buffer[array->length-1] : NULL; 
}
typedef struct bin { void** buffer; size_t capacity; size_t length; } bin;
bin bin_new(size_t size)
{ 
	bin container;
	container.length = 0;
	container.capacity = size; 
	container.buffer = AUG_ALLOC_ARRAY(void*, container.capacity);
	return container;
}
void bin_delete(bin* container)
{
    AUG_FREE_ARRAY(container->buffer); container->buffer = NULL;
}
void bin_push(bin* container, void* data)  
{
	if(container->length + 1 >= container->capacity) 
    {
        container->capacity *= 2; 
        container->buffer = AUG_REALLOC_ARRAY(container->buffer, void*, container->capacity);
    }
    container->buffer[container->length++] = data;
}
void* bin_pop(bin* container)
{
	return container->length > 0 ? container->buffer[--container->length] : NULL; 
}
void* bin_at(const bin* container, size_t index) 
{
	return index >= 0 && index < container->length ? container->buffer[index] : NULL;
}
void* bin_back(const bin* container)
{
	return container->length > 0 ? container->buffer[container->length-1] : NULL; 
}
typedef struct aug_symbol { STR* name; char scope; char type; int offset; int argc; } aug_symbol;
typedef struct aug_symtable {aug_symbol* buffer; int ref_count; size_t capacity; size_t length; } aug_symtable;
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
    if(symtable) ++symtable->ref_count;
}

aug_symtable* aug_symtable_decref(aug_symtable* symtable)
{    
    if(symtable != NULL && --symtable->ref_count == 0)
    {
        size_t i;
        for(i = 0; i < symtable->length; ++i)
        {
            aug_symbol symbol = symtable->buffer[i];
            STR_decref(symbol.name);
        }
        AUG_FREE_ARRAY(symtable->buffer);
        AUG_FREE(symtable);
        return NULL;
    }
    return symtable;
}
void  aug_symtable_reserve(aug_symtable* symtable, size_t size)
{
	symtable->capacity = size; symtable->buffer = AUG_REALLOC_ARRAY(symtable->buffer, aug_symbol, symtable->capacity);
}

aug_symbol aug_symtable_get(aug_symtable* symtable, STR* name)
{
    size_t i;
    for(i = 0; i < symtable->length; ++i)
    {
        aug_symbol symbol = symtable->buffer[i];
        if(STR_compare(symbol.name, name))
            return symbol;
    }
    aug_symbol symbol;
    symbol.offset = -1;
    symbol.type = 0;
    symbol.argc = 0;
    return symbol;
}

bool aug_symtable_set(aug_symtable* symtable, aug_symbol symbol)
{
    aug_symbol existing_symbol = aug_symtable_get(symtable, symbol.name);
    if(existing_symbol.type != 0) return 0;
    if(symtable->length + 1 >= symtable->capacity)  aug_symtable_reserve(symtable, 2 * symtable->capacity);
    aug_symbol new_symbol = symbol;
    STR_incref(new_symbol.name);
    symtable->buffer[symtable->length++] = new_symbol;
    return true;
}
OBJ aug_none();
bool aug_get_bool(const OBJ* value);
typedef struct aug_frame { int base_index; int stack_index; bool func_call; int arg_count; const char* pc;  } aug_frame;
typedef OBJ (aug_ext)(int argc, const OBJ*);
typedef struct VM { char* pc; char* exe; OBJ stack[AUG_STACK_SIZE]; int stack_index; int base_index; aug_ext* exts[AUG_EXTENSION_SIZE];       STR* ext_names[AUG_EXTENSION_SIZE];  int ext_count;     } VM;

#define LOG(...) fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")
#define LOG_INPUT(input, pos, ...) \
if(input->valid && !!(input->valid = 0))                            \
    aug_input_error_hint(input, pos);       \
    LOG(__VA_ARGS__);              \

#define AUG_INPUT_ERROR(input, ...) LOG_INPUT(input, aug_input_prev_pos(input), __VA_ARGS__);

typedef struct aug_pos { size_t filepos; size_t linepos; size_t line; size_t col; }aug_pos;
typedef struct aug_input { FILE* file; STR* filename; bool valid; size_t track_pos; size_t pos_buffer_index; aug_pos pos_buffer[4]; char c; }aug_input;

void aug_input_error_hint(aug_input* input, const aug_pos* pos)
{
    int curr_pos = ftell(input->file);
    fseek(input->file, pos->linepos, SEEK_SET);
    char buffer[4096];
    size_t n = 0;
    char c = fgetc(input->file);
    while(isspace(c))
        c = fgetc(input->file);
    while(c != EOF && c != '\n' && n < (int)(sizeof(buffer) / sizeof(buffer[0]) - 1))
    {
        buffer[n++] = c;
        c = fgetc(input->file);
    }
    buffer[n] = '\0';

    LOG("Error %s:(%d,%d) ", input->filename->buffer, pos->line + 1, pos->col + 1);
    LOG("%s", buffer);
    if(pos->col < n-1)
    {
        size_t i;
        for(i = 0; i < pos->col; ++i)
            buffer[i] = ' ';
        buffer[pos->col] = '^';
        buffer[pos->col+1] = '\0';
    }
    LOG("%s", buffer);
    fseek(input->file, curr_pos, SEEK_SET);
}

aug_pos* aug_input_pos(aug_input* input)
{
    return &input->pos_buffer[input->pos_buffer_index];
}

aug_pos* aug_input_prev_pos(aug_input* input)
{
    input->pos_buffer_index--;
    if(input->pos_buffer_index < 0)
        input->pos_buffer_index = (sizeof(input->pos_buffer) / sizeof(input->pos_buffer[0])) - 1;
    return aug_input_pos(input);
}

aug_pos* aug_input_next_pos(aug_input* input)
{
    input->pos_buffer_index = (input->pos_buffer_index + 1) % (sizeof(input->pos_buffer) / sizeof(input->pos_buffer[0]));
    return aug_input_pos(input);
}

char aug_input_get(aug_input* input)
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

char aug_input_peek(aug_input* input)
{
    char c = fgetc(input->file);
    ungetc(c, input->file);
    return c;
}

void aug_input_unget(aug_input* input)
{
    ungetc(input->c, input->file);
}

aug_input* aug_input_open(const char* filename)
{
    FILE* file = fopen(filename, "r");
    if(file == NULL)
    {
        LOG("Input failed to open file %s", filename);
        return NULL;
    }

    aug_input* input = AUG_ALLOC(aug_input);
    input->valid = true;
    input->file = file;
    input->filename = STR_create(filename);
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
    input->filename = STR_decref(input->filename);
    if(input->file != NULL)
        fclose(input->file);

    AUG_FREE(input);
}

void aug_input_start_tracking(aug_input* input)
{
    input->track_pos = ftell(input->file);
}

STR* aug_input_end_tracking(aug_input* input)
{
    const size_t pos_end = ftell(input->file);
    const size_t len = (pos_end - input->track_pos);
    STR* string = STR_new(len+1);
    string->length = len;
    string->buffer[len] = '\0';
    fseek(input->file, input->track_pos, SEEK_SET);
    (void)fread(string->buffer, sizeof(char), len, input->file);
    fseek(input->file, pos_end, SEEK_SET);
    return string;
}
typedef struct TOK_detail
{
    const char* label;    // the string representation, used for visualization and debugging
    char prec;            // if the token is an operator, this is the precedence (note: higher values take precendece)
    int  argc;            // if the token is an operator, this is the number of arguments
    bool capture;         // if non-zero, the token will contain the source string value (i.e. integer and string literals)
    const char* keyword;  // if non-null, the token must match the provided keyword
} TOK_detail;

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
    AUG_TOKEN(FALSE,          0, 0, 0, "0")

// Token identifier. 
typedef enum TOK_id
{
#define AUG_TOKEN(id, ...) AUG_TOKEN_##id,
    AUG_TOKEN_LIST
#undef AUG_TOKEN
    AUG_TOKEN_COUNT
} TOK_id;

// All token type info. Types map from id to type info
static TOK_detail TOK_details[(int)AUG_TOKEN_COUNT] = 
{
#define AUG_TOKEN(id, ...) { #id, __VA_ARGS__},
    AUG_TOKEN_LIST
#undef AUG_TOKEN
};

#undef AUG_TOKEN_LIST

// Token instance
typedef struct  TOK
{
    TOK_id id;
    const TOK_detail* detail; 
    STR* data;
    aug_pos pos;
} TOK;

// Lexer state
typedef struct aug_lexer
{
    aug_input* input;

    TOK curr;
    TOK next;

    char comment_symbol;
} aug_lexer;

TOK TOK_new()
{
    TOK token;
    token.id = AUG_TOKEN_NONE;
    token.detail = &TOK_details[(int)token.id];
    token.data = NULL;
    return token;
}

void TOK_reset(TOK* token)
{
    STR_decref(token->data);
    *token = TOK_new();
}

TOK TOK_copy(TOK token)
{
    TOK new_token = token;
    STR_incref(new_token.data);
    return new_token;
}

aug_lexer* aug_lexer_new(aug_input* input)
{
    aug_lexer* lexer = AUG_ALLOC(aug_lexer);
    lexer->input = input;
    lexer->comment_symbol = '#';

    lexer->curr = TOK_new();
    lexer->next = TOK_new();

    return lexer;
}

void aug_lexer_delete(aug_lexer* lexer)
{
    TOK_reset(&lexer->curr);
    TOK_reset(&lexer->next);

    AUG_FREE(lexer);
}

bool aug_lexer_tokenize_char(aug_lexer* lexer, TOK* token)
{
    char c = aug_input_get(lexer->input);
    token->id = AUG_TOKEN_CHAR;
    token->data = STR_new(1);

    c = aug_input_get(lexer->input);
    if(c != '\'')
    {
        STR_push(token->data, c);
        c = aug_input_get(lexer->input); // eat 
    }
    else
        STR_push(token->data, 0);

    if(c != '\'')
    {
        token->data = STR_decref(token->data);
        AUG_INPUT_ERROR(lexer->input, "char literal missing closing \"");
        return 0;
    }
    return true;
}

bool aug_lexer_tokenize_string(aug_lexer* lexer, TOK* token)
{
    char c = aug_input_get(lexer->input);

    token->id = AUG_TOKEN_STRING;
    token->data = STR_new(4);

    c = aug_input_get(lexer->input);

    while(c != '\"')
    {
        if(c == EOF)
        {
            token->data = STR_decref(token->data);
            AUG_INPUT_ERROR(lexer->input, "string literal missing closing \"");
            return 0;
        }

        if(c == '\\')
        {
            // handle escaped chars
            c = aug_input_get(lexer->input);
            switch(c)
            {
            case '\'': 
                STR_push(token->data, '\'');
                break;
            case '\"':
                STR_push(token->data, '\"');
                break;
            case '\\':
                STR_push(token->data, '\\');
                break;
            case '0': //Null
                STR_push(token->data, 0x0);
                break;
            case 'a': //Alert beep
                STR_push(token->data, 0x07);
                break;
            case 'b': // Backspace
                STR_push(token->data, 0x08);
                break;
            case 'f': // Page break
                STR_push(token->data, 0x0C);
                break;
            case 'n': // Newline
                STR_push(token->data, 0x0A);
                break;
            case 'r': // Carriage return
                STR_push(token->data, 0x0D);
                break;
            case 't': // Tab (Horizontal)
                STR_push(token->data, 0x09);
                break;
            case 'v': // Tab (Vertical)
                STR_push(token->data, 0x0B);
                break;
            default:
                token->data = STR_decref(token->data);
                AUG_INPUT_ERROR(lexer->input, "invalid escape character \\%c", c);
                return 0;
            }
        }
        else
        {
            STR_push(token->data, c);
        }

        c = aug_input_get(lexer->input);
    }

    return true;
}

bool aug_lexer_tokenize_symbol(aug_lexer* lexer, TOK* token)
{
    TOK_id id = AUG_TOKEN_NONE;

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
        return 0;
    }

    token->id = id;
    return true;
}

bool aug_lexer_tokenize_name(aug_lexer* lexer, TOK* token)
{
    aug_input_start_tracking(lexer->input);

    char c = aug_input_get(lexer->input);
    if(c != '_' && !isalpha(c))
    {
        aug_input_unget(lexer->input);
        return 0;
    }
    
    while(c == '_' || isalnum(c))
        c = aug_input_get(lexer->input);
    aug_input_unget(lexer->input);

    token->id = AUG_TOKEN_NAME;
    token->data = aug_input_end_tracking(lexer->input);

    // find token id for keyword
    for(size_t i = 0; i < (size_t)AUG_TOKEN_COUNT; ++i)
    {
        if(STR_compare_bytes(token->data, TOK_details[i].keyword))
        {
            token->id = (TOK_id)i;
            token->data = STR_decref(token->data); // keyword is static, free token data
            break;
        }
    }
    
    return true;
}

bool aug_lexer_tokenize_number(aug_lexer* lexer, TOK* token)
{    
    aug_input_start_tracking(lexer->input);

    char c = aug_input_get(lexer->input);
    if(c != '.' && !isdigit(c) && c != '+' && c != '-')
    {
        aug_input_unget(lexer->input);
        return 0;
    } 

    TOK_id id = AUG_TOKEN_NONE;

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
                return 0;
            }
            c = aug_input_get(lexer->input);
        }

        if(c == '.')
            id = AUG_TOKEN_FLOAT;
        else 
            id = AUG_TOKEN_INT;

        bool dot = 0;
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
        token->data = STR_decref(token->data);
        return 0;
    }

    return true;
}

TOK aug_lexer_tokenize(aug_lexer* lexer)
{
    TOK token = TOK_new();

    // if file is not open, or already at then end. return invalid token
    if(lexer->input == NULL || !lexer->input->valid)
        return token;

    if(aug_input_peek(lexer->input) == EOF)
    {
        token.id = AUG_TOKEN_END;
        token.detail = &TOK_details[(int)token.id];
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
        token.detail = &TOK_details[(int)token.id];
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
                allow_sign = 0;
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

    token.detail = &TOK_details[(int)token.id];
    return token;
}

bool aug_lexer_move(aug_lexer* lexer)
{
    if(lexer == NULL)
        return 0;

    if(lexer->next.id == AUG_TOKEN_NONE)
        lexer->next = aug_lexer_tokenize(lexer);        


    TOK_reset(&lexer->curr);

    lexer->curr = lexer->next;
    lexer->next = aug_lexer_tokenize(lexer);

    return lexer->curr.id != AUG_TOKEN_NONE;
}

// -------------------------------------- Parser / Abstract Syntax Tree ---------------------------------------// 

typedef enum AST_id
{
    AUG_AST_ROOT = 0,
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
} AST_id;

typedef struct AST
{
    AST_id id;
    TOK token;
    struct AST** children;
    int children_size;
    int children_capacity;
} AST;

AST* aug_parse_value(aug_lexer* lexer); 
AST* aug_parse_block(aug_lexer* lexer);

AST* AST_new(AST_id id, TOK token)
{
    AST* node = AUG_ALLOC(AST);
    node->id = id;
    node->token = token;
    node->children = NULL;
    node->children_size = 0;
    node->children_capacity = 0;
    return node;
}

void AST_delete(AST* node)
{
    if(node == NULL)
        return;
    if(node->children)
    {
        int i;
        for(i = 0; i < node->children_size; ++i)
            AST_delete(node->children[i]);
        AUG_FREE_ARRAY(node->children);
    }
    TOK_reset(&node->token);
    AUG_FREE(node);
}

void AST_resize(AST* node, int size)
{    
    node->children_capacity = size == 0 ? 1 : size;
    node->children = AUG_REALLOC_ARRAY(node->children, AST*, node->children_capacity);
    node->children_size = size;
}

void AST_add(AST* node, AST* child)
{
    if(node->children_size + 1 >= node->children_capacity)
    {
        node->children_capacity = node->children_capacity == 0 ? 1 : node->children_capacity * 2;
        node->children = AUG_REALLOC_ARRAY(node->children, AST*, node->children_capacity);
    }
    node->children[node->children_size++] = child;
}

bool aug_parse_expr_pop(aug_lexer* lexer, bin* op_stack, bin* expr_stack)
{
    //op_stack : TOK*
    //expr_stack : AST*
    
    TOK* next_op = (TOK*)bin_pop(op_stack);

    const int op_argc = next_op->detail->argc;
    

    if(expr_stack->length < (size_t)op_argc)
    {
        while(expr_stack->length > 0)
        {
            AST* expr = (AST*)bin_pop(expr_stack);
            AST_delete(expr);
        }
        AUG_INPUT_ERROR(lexer->input, "Invalid number of arguments to operator %s", next_op->detail->label);
        
        AUG_FREE(next_op);
        return 0;
    }

    // Push binary op onto stack
    AST_id id = (op_argc == 2) ? AUG_AST_BINARY_OP : AUG_AST_UNARY_OP;
    AST* binaryop = AST_new(id, *next_op);
    AST_resize(binaryop, op_argc);
    
    int i;
    for(i = 0; i < op_argc; ++i)
    {
        AST* expr = (AST*)bin_pop(expr_stack);
        binaryop->children[(op_argc-1) - i] = expr; // add in reverse
    }

    bin_push(expr_stack, binaryop); 
    AUG_FREE(next_op);
    return true;
}

void aug_parse_expr_stack_cleanup(bin* op_stack, bin* expr_stack)
{
    while(op_stack->length > 0)
    {
        TOK* token = (TOK*) bin_pop(op_stack);
        AUG_FREE(token);
    }
    while(expr_stack->length > 0)
    {
        AST* expr = (AST*) bin_pop(expr_stack);
        AST_delete(expr);
    }
    bin_delete(op_stack);
    bin_delete(expr_stack);
}

AST* aug_parse_expr(aug_lexer* lexer)
{
    // Shunting yard algorithm
    bin op_stack = bin_new(1);
    bin expr_stack = bin_new(1);
    while(lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        TOK op = lexer->curr;

        if(op.detail->prec > 0)
        {
            // left associate by default (for right, <= becomes <)
            while(op_stack.length)
            {
                TOK* next_op = (TOK*)bin_back(&op_stack);

                if(next_op->detail->prec < op.detail->prec)
                    break;
                if(!aug_parse_expr_pop(lexer, &op_stack, &expr_stack))
                    return NULL;
            }
            TOK* new_op = AUG_ALLOC(TOK);
            *new_op = op;
            
            bin_push(&op_stack, new_op);
            aug_lexer_move(lexer);
        }
        else
        {
            AST* value = aug_parse_value(lexer);
            if(value == NULL)
                break;

            bin_push(&expr_stack, value);
        }
    }

    // Not an expression
    if(op_stack.length == 0 && expr_stack.length == 0)
    {
        bin_delete(&op_stack);
        bin_delete(&expr_stack);
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

    AST* expr = (AST*) bin_back(&expr_stack);
    bin_delete(&op_stack);
    bin_delete(&expr_stack);
    return expr;
}

AST* aug_parse_funccall(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_NAME)
        return NULL;

    if(lexer->next.id != AUG_TOKEN_LPAREN)
        return NULL;

    TOK name_token = TOK_copy(lexer->curr);

    aug_lexer_move(lexer); // eat NAME
    aug_lexer_move(lexer); // eat LPAREN

    AST* funccall = AST_new(AUG_AST_FUNC_CALL, name_token);
    AST* expr = aug_parse_expr(lexer);
    if(expr != NULL)
    {
        AST_add(funccall, expr);

        while(expr != NULL && lexer->curr.id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            expr = aug_parse_expr(lexer);
            if(expr != NULL)
                AST_add(funccall, expr);
        }
    }

    if(lexer->curr.id != AUG_TOKEN_RPAREN)
    {
        AST_delete(funccall);
        AUG_INPUT_ERROR(lexer->input, "Function call missing closing parentheses");
        return NULL;
    }

    aug_lexer_move(lexer); // eat RPAREN
    return funccall;
}

AST* aug_parse_array(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_LBRACKET)
        return NULL;

    aug_lexer_move(lexer); // eat LBRACKET

    AST* array = AST_new(AUG_AST_ARRAY, TOK_new());
    AST* expr = aug_parse_expr(lexer);
    if(expr != NULL)
    {
        AST_add(array, expr);

        while(expr != NULL && lexer->curr.id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            expr = aug_parse_expr(lexer);
            if(expr != NULL)
                AST_add(array, expr);
        }
    }

    if(lexer->curr.id != AUG_TOKEN_RBRACKET)
    {
        AST_delete(array);
        AUG_INPUT_ERROR(lexer->input, "List missing closing bracket");
        return NULL;
    }

    aug_lexer_move(lexer); // eat RBRACKET
    return array;
}

AST* aug_parse_get_element(aug_lexer* lexer)
{
    if(lexer->next.id != AUG_TOKEN_LBRACKET)
        return NULL;

    TOK name_token = TOK_copy(lexer->curr);

    aug_lexer_move(lexer); // eat NAME
    aug_lexer_move(lexer); // eat LBRACKET

    AST* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        AUG_INPUT_ERROR(lexer->input, "Index operator missing index value");
        return NULL;
    }

    AST* container = AST_new(AUG_AST_VARIABLE, name_token);
    
    AST* element = AST_new(AUG_AST_ELEMENT, TOK_new());
    AST_resize(element, 2);
    element->children[0] = container;
    element->children[1] = expr;

    if(lexer->curr.id != AUG_TOKEN_RBRACKET)
    {
        AST_delete(element);
        AUG_INPUT_ERROR(lexer->input, "Index operator missing closing bracket");
        return NULL;
    }

    aug_lexer_move(lexer); // eat RBRACKET
    return element;
}

AST* aug_parse_value(aug_lexer* lexer)
{
    AST* value = NULL;
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
        TOK token = TOK_copy(lexer->curr);
        value = AST_new(AUG_AST_LITERAL, token);

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
        TOK token = TOK_copy(lexer->curr);
        value = AST_new(AUG_AST_VARIABLE, token);

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
            AST_delete(value);    
            value = NULL;
        }
        break;
    }
    default: break;
    }
    return value;
}

AST* aug_parse_stmt_expr(aug_lexer* lexer)
{
    AST* expr = aug_parse_expr(lexer);
    if(expr == NULL)
        return NULL;
    
    if(lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        AST_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "Missing semicolon at end of expression");
        return NULL;
    }

    aug_lexer_move(lexer); // eat SEMICOLON
    
    AST* stmt_expr = AST_new(AUG_AST_STMT_EXPR, TOK_new());
    AST_add(stmt_expr, expr);
    return stmt_expr;
}

AST* aug_parse_stmt_define_var(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_VAR)
        return NULL;

    aug_lexer_move(lexer); // eat VAR

    if(lexer->curr.id != AUG_TOKEN_NAME)
    {
        AUG_INPUT_ERROR(lexer->input,  "Variable assignment expected name");
        return NULL;
    }

    TOK name_token = TOK_copy(lexer->curr);
    aug_lexer_move(lexer); // eat NAME

    if(lexer->curr.id == AUG_TOKEN_SEMICOLON)
    {
        aug_lexer_move(lexer); // eat SEMICOLON

        AST* stmt_define = AST_new(AUG_AST_STMT_DEFINE_VAR, name_token);
        return stmt_define;
    }

    if(lexer->curr.id != AUG_TOKEN_ASSIGN)
    {
        TOK_reset(&name_token);
        AUG_INPUT_ERROR(lexer->input,  "Variable assignment expected \"=\" or ;");
        return NULL;
    }

    aug_lexer_move(lexer); // eat ASSIGN

    AST* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        TOK_reset(&name_token);
        AUG_INPUT_ERROR(lexer->input,  "Variable assignment expected expression after \"=\"");
        return NULL;
    }
    if(lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        TOK_reset(&name_token);
        AST_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "Variable assignment missing semicolon at end of expression");
        return NULL;
    }

    aug_lexer_move(lexer); // eat SEMICOLON

    AST* stmt_define = AST_new(AUG_AST_STMT_DEFINE_VAR, name_token);
    AST_add(stmt_define, expr);
    return stmt_define;
}

AST* aug_parse_stmt_assign_var(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_NAME)
        return NULL;

    TOK eq_token = lexer->next;
    TOK op_token = TOK_new();

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

    TOK name_token = TOK_copy(lexer->curr);
    aug_lexer_move(lexer); // eat NAME
    aug_lexer_move(lexer); // eat ASSIGN

    AST* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        TOK_reset(&name_token);
        AUG_INPUT_ERROR(lexer->input,  "Assignment expected expression after \"=\"");
        return NULL;
    }

    if(lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        TOK_reset(&name_token);
        AST_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "Missing semicolon at end of expression");
        return NULL;
    }

    aug_lexer_move(lexer); // eat SEMICOLON

    if(op_token.id != AUG_TOKEN_NONE)
    {
        // setup detail
        op_token.detail = &TOK_details[(int)op_token.id];

        // Create name + expr
        TOK expr_name_token = TOK_copy(name_token);
        AST* binaryop = AST_new(AUG_AST_BINARY_OP, op_token);
        AST* value = AST_new(AUG_AST_VARIABLE, expr_name_token);

        // add in reverse order
        AST_resize(binaryop, 2);
        binaryop->children[0] = value;
        binaryop->children[1] = expr;

        AST* stmt_assign = AST_new(AUG_AST_STMT_ASSIGN_VAR, name_token);
        AST_add(stmt_assign, binaryop);
        return stmt_assign;
    }

    AST* stmt_assign = AST_new(AUG_AST_STMT_ASSIGN_VAR, name_token);
    AST_add(stmt_assign, expr);
    return stmt_assign;
}

AST* aug_parse_stmt_if(aug_lexer* lexer);

AST* aug_parse_stmt_if_else(aug_lexer* lexer, AST* expr, AST* block)
{
    aug_lexer_move(lexer); // eat ELSE

    AST* if_else_stmt = AST_new(AUG_AST_STMT_IF_ELSE, TOK_new());
    AST_resize(if_else_stmt, 3);
    if_else_stmt->children[0] = expr;
    if_else_stmt->children[1] = block;


    // Handling else if becomes else { if ... }
    if(lexer->curr.id == AUG_TOKEN_IF)
    {
        AST* trailing_if_stmt = aug_parse_stmt_if(lexer);
        if(trailing_if_stmt == NULL)
        {
            AST_delete(if_else_stmt);
            return NULL;
        }
        if_else_stmt->children[2] = trailing_if_stmt;
    }
    else
    {
        AST* else_block = aug_parse_block(lexer);
        if(else_block == NULL)
        {
            AST_delete(if_else_stmt);
            AUG_INPUT_ERROR(lexer->input,  "If Else statement missing block");
            return NULL;
        }
        if_else_stmt->children[2] = else_block;
    }

    return if_else_stmt;
}

AST* aug_parse_stmt_if(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_IF)
        return NULL;

    aug_lexer_move(lexer); // eat IF

    AST* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        AUG_INPUT_ERROR(lexer->input,  "If statement missing expression");
        return NULL;      
    }

    AST* block = aug_parse_block(lexer);
    if(block == NULL)
    {
        AST_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "If statement missing block");
        return NULL;
    }

    // Parse else 
    if(lexer->curr.id == AUG_TOKEN_ELSE)
        return aug_parse_stmt_if_else(lexer, expr, block);

    AST* if_stmt = AST_new(AUG_AST_STMT_IF, TOK_new());
    AST_resize(if_stmt, 2);
    if_stmt->children[0] = expr;
    if_stmt->children[1] = block;
    return if_stmt;
}

AST* aug_parse_stmt_while(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_WHILE)
        return NULL;

    aug_lexer_move(lexer); // eat WHILE

    AST* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        AUG_INPUT_ERROR(lexer->input,  "While statement missing expression");
        return NULL;
    }

    AST* block = aug_parse_block(lexer);
    if(block == NULL)
    {
        AST_delete(expr);
        AUG_INPUT_ERROR(lexer->input,  "While statement missing block");
        return NULL;
    }

    AST* while_stmt = AST_new(AUG_AST_STMT_WHILE, TOK_new());
    AST_resize(while_stmt, 2);
    while_stmt->children[0] = expr;
    while_stmt->children[1] = block;
    return while_stmt;
}

AST* aug_parse_param_list(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_LPAREN)
    {
        AUG_INPUT_ERROR(lexer->input,  "Missing opening parentheses in function parameter list");
        return NULL;
    }

    aug_lexer_move(lexer); // eat LPAREN

    AST* param_list = AST_new(AUG_AST_PARAM_LIST, TOK_new());
    if(lexer->curr.id == AUG_TOKEN_NAME)
    {
        AST* param = AST_new(AUG_AST_PARAM, TOK_copy(lexer->curr));
        AST_add(param_list, param);

        aug_lexer_move(lexer); // eat NAME

        while(lexer->curr.id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            if(lexer->curr.id != AUG_TOKEN_NAME)
            {
                AUG_INPUT_ERROR(lexer->input,  "Invalid function parameter. Expected parameter name");
                AST_delete(param_list);
                return NULL;
            }

            AST* param = AST_new(AUG_AST_PARAM, TOK_copy(lexer->curr));
            AST_add(param_list, param);

            aug_lexer_move(lexer); // eat NAME
        }
    }

    if(lexer->curr.id != AUG_TOKEN_RPAREN)
    {
        AUG_INPUT_ERROR(lexer->input,  "Missing closing parentheses in function parameter list");
        AST_delete(param_list);
        return NULL;
    }

    aug_lexer_move(lexer); // eat RPAREN

    return param_list;
}

AST* aug_parse_stmt_func(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_FUNC)
        return NULL;

    aug_lexer_move(lexer); // eat FUNC

    if(lexer->curr.id != AUG_TOKEN_NAME)
    {
        AUG_INPUT_ERROR(lexer->input,  "Missing name in function definition");
        return NULL;
    }

    TOK func_name_token = TOK_copy(lexer->curr);

    aug_lexer_move(lexer); // eat NAME

    AST* param_list = aug_parse_param_list(lexer);
    if(param_list == NULL)
    {
        TOK_reset(&func_name_token);
        return NULL;
    }

    AST* block = aug_parse_block(lexer);
    if(block == NULL)
    {
        TOK_reset(&func_name_token);
        AST_delete(param_list);
        return NULL;
    }

    AST* func_def = AST_new(AUG_AST_FUNC_DEF, func_name_token);
    AST_resize(func_def, 2);
    func_def->children[0] = param_list;
    func_def->children[1] = block;
    return func_def;
}

AST* aug_parse_stmt_return(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_RETURN)
        return NULL;

    aug_lexer_move(lexer); // eat RETURN

    AST* return_stmt = AST_new(AUG_AST_RETURN, TOK_new());

    AST* expr = aug_parse_expr(lexer);
    if(expr != NULL)
        AST_add(return_stmt, expr);

    if(lexer->curr.id != AUG_TOKEN_SEMICOLON)
    {
        AST_delete(return_stmt);
        AUG_INPUT_ERROR(lexer->input,  "Missing semicolon at end of expression");
        return NULL;
    }

    aug_lexer_move(lexer); // eat SEMICOLON

    return return_stmt;
}

AST* aug_parse_stmt(aug_lexer* lexer)
{
    switch (lexer->curr.id)
    {
    case AUG_TOKEN_NAME:
    {
        AST* stmt = aug_parse_stmt_assign_var(lexer);
        if(stmt != NULL)
            return stmt;
        return aug_parse_stmt_expr(lexer);
    }
    case AUG_TOKEN_IF:
        return aug_parse_stmt_if(lexer);
    case AUG_TOKEN_WHILE:
        return aug_parse_stmt_while(lexer);
    case AUG_TOKEN_VAR:
        return aug_parse_stmt_define_var(lexer);
    case AUG_TOKEN_FUNC:
        return aug_parse_stmt_func(lexer);
    case AUG_TOKEN_RETURN:
        return aug_parse_stmt_return(lexer);
    default:
        return aug_parse_stmt_expr(lexer);
    }
    return NULL;
}

AST* aug_parse_block(aug_lexer* lexer)
{
    if(lexer->curr.id != AUG_TOKEN_LBRACE)
    {
        AUG_INPUT_ERROR(lexer->input,  "Block missing opening \"{\"");
        return NULL;
    }
    aug_lexer_move(lexer); // eat LBRACE
    AST* block = AST_new(AUG_AST_BLOCK, TOK_new());    
    AST* stmt = aug_parse_stmt(lexer);
    while(stmt)
    {
        AST_add(block, stmt);
        stmt = aug_parse_stmt(lexer);
    }   
    if(lexer->curr.id != AUG_TOKEN_RBRACE)
    {
        AUG_INPUT_ERROR(lexer->input,  "Block missing closing \"}\"");
        AST_delete(block);
        return NULL;
    }
    aug_lexer_move(lexer); 
    return block;
}

AST* aug_parse_root(aug_lexer* lexer)
{
    if(lexer == NULL)
        return NULL;
    aug_lexer_move(lexer); // move to first token
    AST* root = AST_new(AUG_AST_ROOT, TOK_new());
    AST* stmt = aug_parse_stmt(lexer);
    while(stmt)
    {
        AST_add(root, stmt);
        stmt = aug_parse_stmt(lexer);
    }   
    if(root->children_size == 0)
    {
        AST_delete(root);
        return NULL;
    }
    return root;
}
AST* aug_parse(VM* vm, aug_input* input)
{
    if(input == NULL)
        return NULL;
    aug_lexer* lexer = aug_lexer_new(input);
    if(lexer == NULL)
        return NULL;
    AST* root = aug_parse_root(lexer);
    aug_lexer_delete(lexer);
    return root;
}
typedef struct IR_arg
{
    union
    {
        bool b;
        int i;
        char c;
        float f;
        char bytes[sizeof(float)];
        const char* str; 
    } data;
    char type;
} IR_arg;
typedef struct OP
{
    uint8_t opcode;
    IR_arg arg; 
    size_t exe_offset;
} OP;
typedef struct IR_scope
{
    int base_index;
    int stack_offset;
    aug_symtable* symtable;
} IR_scope;
typedef struct IR_frame
{
    int base_index;
    int arg_count;
    bin scope_stack;
} IR_frame;
typedef struct IR
{		
    aug_input* input; 
    bin frame_stack;
    int label_count;
    bin ops;
    size_t exe_offset;
    aug_symtable* globals;
    bool valid;
} IR;

IR* IR_new(aug_input* input)
{
    IR* ir = AUG_ALLOC(IR);
    ir->valid = true;
    ir->input = input;
    ir->label_count = 0;
    ir->exe_offset = 0;
    ir->frame_stack =  bin_new(1);
    ir->ops = bin_new(1);
    return ir;
}

void IR_delete(IR* ir)
{
    size_t i;
    for(i = 0; i < ir->ops.length; ++i)
    {
        OP* op = (OP*)bin_at(&ir->ops, i);
        AUG_FREE(op);
    }
    bin_delete(&ir->ops);
    bin_delete(&ir->frame_stack);
    aug_symtable_decref(ir->globals);
    AUG_FREE(ir);
}

size_t IR_arg_size(IR_arg arg)
{
    switch (arg.type)
    {
    case 0:
        return 0;
    case 1:
        return sizeof(arg.data.b);
    case 2:
        return sizeof(arg.data.c);
    case 3:
        return sizeof(arg.data.i);
    case 4:
        return sizeof(arg.data.f);
    case 5:
        return strlen(arg.data.str) + 1; // +1 for null term
    }
    return 0;
}

size_t OP_size(const OP* op)
{
    if(op == 0)
        return 0;
    size_t size = sizeof(uint8_t);
    size += IR_arg_size(op->arg);
    return size;
}

size_t IR_add_op_arg(IR*ir, uint8_t opcode, IR_arg arg)
{
    OP* op = AUG_ALLOC(OP);
    op->opcode = opcode;
    op->arg = arg;
    op->exe_offset = ir->exe_offset;

    ir->exe_offset += OP_size(op);
    bin_push(&ir->ops, op);
    return ir->ops.length-1;
}

size_t IR_add_op(IR*ir, uint8_t opcode)
{
    IR_arg arg;
    arg.type = 0;
    return IR_add_op_arg(ir, opcode, arg);
}

OP* IR_last_op(IR*ir)
{
    
    return (OP*)bin_at(&ir->ops, ir->ops.length - 1);
}

OP* IR_get_op(IR*ir, size_t op_index)
{
    
    return (OP*)bin_at(&ir->ops, op_index);
}

IR_arg IR_arg_from_bool(bool data)
{
    IR_arg arg;
    arg.type = 1;
    arg.data.b = data;
    return arg;
}

IR_arg IR_arg_from_char(char data)
{
    IR_arg arg;
    arg.type = 2;
    arg.data.c = data;
    return arg;
}

IR_arg IR_arg_from_int(int data)
{
    IR_arg arg;
    arg.type = 3;
    arg.data.i = data;
    return arg;
}

IR_arg IR_arg_from_float(float data)
{
    IR_arg arg;
    arg.type = 4;
    arg.data.f = data;
    return arg;
}

IR_arg IR_arg_from_str(const char* data)
{
    IR_arg arg;
    arg.type = 5;
    arg.data.str = data;
    return arg;
}

IR_frame* IR_current_frame(IR*ir)
{
    
    return (IR_frame*)bin_back(&ir->frame_stack);
}

IR_scope* IR_current_scope(IR*ir)
{
    IR_frame* frame = IR_current_frame(ir);
    
    return (IR_scope*)bin_back(&frame->scope_stack);
}

bool IR_current_scope_is_global(IR*ir)
{
    IR_frame* frame = IR_current_frame(ir);
    if(ir->frame_stack.length == 1 && frame->scope_stack.length == 1)
        return true;
    return 0;
}

int IR_current_scope_local_offset(IR*ir)
{
    const IR_scope* scope = IR_current_scope(ir);
    return scope->stack_offset - scope->base_index;
}

int IR_calling_offset(IR*ir)
{
    IR_scope* scope = IR_current_scope(ir);
    IR_frame* frame = IR_current_frame(ir);
    return (scope->stack_offset - frame->base_index) + frame->arg_count;
}

void IR_push_frame(IR*ir, int arg_count)
{
    IR_frame* frame = AUG_ALLOC(IR_frame);
    frame->arg_count = arg_count;

    if(ir->frame_stack.length > 0)
    {
        const IR_scope* scope = IR_current_scope(ir);
        frame->base_index = scope->stack_offset;
    }
    else
    {
        frame->base_index = 0;
    }

    IR_scope* scope = AUG_ALLOC(IR_scope);
    scope->base_index = frame->base_index;
    scope->stack_offset = frame->base_index;
    scope->symtable = aug_symtable_new(1);
    
    frame->scope_stack = bin_new(1);
    bin_push(&frame->scope_stack, scope);
    bin_push(&ir->frame_stack, frame);
}

void IR_pop_frame(IR*ir)
{
    if(ir->frame_stack.length == 1)
    {
        IR_scope* scope = IR_current_scope(ir);
        ir->globals = scope->symtable;
        scope->symtable = NULL; // Move to globals
    }
    
    IR_frame* frame = (IR_frame*)bin_pop(&ir->frame_stack);

    size_t i;
    for(i = 0; i < frame->scope_stack.length; ++i)
    {
        IR_scope* scope = (IR_scope*)bin_at(&frame->scope_stack, i);
        aug_symtable_decref(scope->symtable);
        AUG_FREE(scope);
    }
    bin_delete(&frame->scope_stack);
    AUG_FREE(frame);
}

void IR_push_scope(IR*ir)
{
    const IR_scope* current_scope = IR_current_scope(ir);
    IR_scope* scope = AUG_ALLOC(IR_scope);
    scope->base_index = current_scope->stack_offset;
    scope->stack_offset = current_scope->stack_offset;
    scope->symtable = aug_symtable_new(1);

    IR_frame* frame = IR_current_frame(ir);
    bin_push(&frame->scope_stack, scope);
}

void IR_pop_scope(IR*ir)
{
    const IR_arg delta = IR_arg_from_int(IR_current_scope_local_offset(ir));
    IR_add_op_arg(ir, 41, delta);

    IR_frame* frame = IR_current_frame(ir);
    IR_scope* scope = (IR_scope*)bin_pop(&frame->scope_stack);
    aug_symtable_decref(scope->symtable);
    AUG_FREE(scope);
}

bool IR_set_var(IR*ir, STR* var_name)
{
    IR_scope* scope = IR_current_scope(ir);
    const int offset = scope->stack_offset++;

    aug_symbol symbol;
    symbol.name = var_name;
    symbol.type = 1;
    symbol.offset = offset;
    symbol.argc = 0;
    
    if(IR_current_scope_is_global(ir))
        symbol.scope = 1;
    else
        symbol.scope = 0;


    return aug_symtable_set(scope->symtable, symbol);
}

bool IR_set_param(IR*ir, STR* param_name)
{
    IR_scope* scope = IR_current_scope(ir);
    const int offset = scope->stack_offset++;

    aug_symbol symbol;
    symbol.name = param_name;
    symbol.type = 1;
    symbol.offset = offset;
    symbol.argc = 0;
    
    if(IR_current_scope_is_global(ir))
        symbol.scope = 1;
    else
        symbol.scope = 2;

    return aug_symtable_set(scope->symtable, symbol);
}

bool IR_set_func(IR*ir, STR* func_name, int param_count)
{
    IR_scope* scope = IR_current_scope(ir);
    const int offset = ir->exe_offset;

    aug_symbol symbol;
    symbol.name = func_name;
    symbol.type = 2;
    symbol.offset = offset;
    symbol.argc = param_count;

    if(IR_current_scope_is_global(ir))
        symbol.scope = 1;
    else
        symbol.scope = 0;

    return aug_symtable_set(scope->symtable, symbol);
}

aug_symbol IR_get_symbol(IR*ir, STR* name)
{
    int i,j;
    for(i = ir->frame_stack.length - 1; i >= 0; --i)
    {
        IR_frame* frame = (IR_frame*) bin_at(&ir->frame_stack, i);
        for(j = frame->scope_stack.length - 1; j >= 0; --j)
        {
            IR_scope* scope = (IR_scope*) bin_at(&frame->scope_stack, j);
            aug_symbol symbol = aug_symtable_get(scope->symtable, name);
            if(symbol.type != 0)
                return symbol;
        }
    }

    aug_symbol sym;
    sym.offset = -1;
    sym.type = 0;
    sym.argc = 0;
    return sym;
}

aug_symbol IR_symbol_relative(IR*ir, STR* name)
{
    int i,j;
    for(i = ir->frame_stack.length - 1; i >= 0; --i)
    {
        IR_frame* frame = (IR_frame*) bin_at(&ir->frame_stack, i);
        for(j = frame->scope_stack.length - 1; j >= 0; --j)
        {
            IR_scope* scope = (IR_scope*) bin_at(&frame->scope_stack, j);
            aug_symbol symbol = aug_symtable_get(scope->symtable, name);
            if(symbol.type != 0)
            {
                switch (symbol.scope)
                {
                case 1:
                    break;
                case 2:
                {
                    const IR_frame* frame = IR_current_frame(ir);
                    symbol.offset = symbol.offset - frame->base_index;
                    break;
                }
                case 0:
                {
                    const IR_frame* frame = IR_current_frame(ir);
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
    sym.offset = -1;
    sym.type = 0;
    sym.argc = 0;
    return sym;
}

aug_symbol IR_get_symbol_local(IR*ir, STR* name)
{
    IR_scope* scope = IR_current_scope(ir);
    return aug_symtable_get(scope->symtable, name);
}

// --------------------------------------- Value Operations -------------------------------------------------------//
bool aug_set_bool(OBJ* value, bool data)
{
    if(value == NULL)
        return 0;
    value->type = 0;
    value->b = data;
    return true;
}

bool aug_set_int(OBJ* value, int data)
{
    if(value == NULL)
        return 0;
    value->type = 2;
    value->i = data;
    return true;
}

bool aug_set_char(OBJ* value, char data)
{
    if(value == NULL)
        return 0;
    value->type = 1;
    value->c = data;
    return true;
}

bool aug_set_float(OBJ* value, float data)
{
    if(value == NULL)
        return 0;
    value->type = 3;
    value->f = data;
    return true;
}

bool aug_set_string(OBJ* value, const char* data)
{
    if(value == NULL)
        return 0;

    value->type = 4;
    value->str = STR_create(data);
    return true;
}

bool aug_set_array(OBJ* value)
{
    if(value == NULL)
        return 0;

    value->type = 5;
    value->array = aug_array_new(1);
    return true;
}

OBJ aug_none()
{
    OBJ value;
    value.type = 6;
    return value;
}

bool aug_get_bool(const OBJ* value)
{
    if(value == NULL)
        return 0;

    switch (value->type)
    {
    case 6:
        return 0;
    case 0:
        return value->b;
    case 2:
        return value->i != 0;
    case 1:
        return value->c != 0;
    case 3:
        return value->f != 0.0f;
    case 4:
        return value->str != NULL;
    case 5:
        return value->array != NULL;
    }
    return 0;
}

void aug_decref(OBJ* value)
{
    if(value == NULL)
        return;

    switch (value->type)
    {
    case 6:
    case 0:
    case 2:
    case 1:
    case 3:
        break;
    case 4:
        value->str = STR_decref(value->str);
        break;
    case 5:
        if(value->array)
        {
            for(size_t i = 0; i < value->array->length; ++i)
            {
                aug_decref(aug_array_at(value->array, i));
            }
            value->array = aug_array_decref(value->array);
        }
        break;
    }
}

void aug_incref(OBJ* value)
{
    if(value == NULL)
        return;

    switch (value->type)
    {
    case 4:
        STR_incref(value->str);
        break;
    case 5:
        aug_array_incref(value->array);
        break;
    default:
        break;
    }
}

void aug_assign(OBJ* to, OBJ* from)
{
    if(from == NULL || to == NULL)
        return;

    aug_decref(to);
    *to = *from;
    aug_incref(to);
}

void aug_move(OBJ* to, OBJ* from)
{
    if(from == NULL || to == NULL)
        return;

    aug_decref(to);
    *to = *from;
    *from = aug_none();
}

bool aug_get_element(OBJ* container, OBJ* index, OBJ* element)
{
    if(container == NULL || index == NULL || element == NULL)
        return 0;

    int i = (index && index->type == 2) ?  index->i : -1;

    if(i < 0)
        return 0;

    switch (container->type)
    {
    case 4:
    {
        aug_set_char(element, STR_at(container->str, i));
    return true;
    }
    case 5:
    {
        OBJ* value = aug_array_at(container->array, i);
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
    return 0;
}

#define AUG_DEFINE_BINOP(l,r,i_i,i_f,fi,ff,cc,bb)\
    if(!l || !r) return 0;\
    switch(l->type)                                           \
    {                                                           \
        case 2:switch(r->type){case 2: i_i;case 3:i_f;}break;\
        case 3:switch(r->type){case 2: fi;case 3: ff; }break;\
        case 1:switch(r->type){case 1: cc;}break;\ 
        case 0:switch(r->type){case 0: bb;}break;                                              \
    }                                                           \

bool aug_add(OBJ* t, OBJ* l, OBJ* r)
{
    AUG_DEFINE_BINOP(l, r,
        return aug_set_int(r, l->i + r->i),
        return aug_set_float(r, l->i + r->f),
        return aug_set_float(r, l->f + r->i),
        return aug_set_float(r, l->f + r->f),
        return aug_set_char(r, l->c + r->c),
        return 0
    )
    return 0;
}

bool aug_sub(OBJ* t, OBJ* l, OBJ* r)
{
    AUG_DEFINE_BINOP(l,r,
        return aug_set_int(r, l->i - r->i),
        return aug_set_float(r, l->i - r->f),
        return aug_set_float(r, l->f - r->i),
        return aug_set_float(r, l->f - r->f),
        return aug_set_char(r, l->c - r->c),
        return 0
    )
    return 0;
}

bool aug_mul(OBJ* t, OBJ* l, OBJ* r)
{
    AUG_DEFINE_BINOP(l,r,
        return aug_set_int(t, l->i * r->i),
        return aug_set_float(t, l->i * r->f),
        return aug_set_float(t, l->f * r->i),
        return aug_set_float(t, l->f * r->f),
        return aug_set_char(t, l->c * r->c),
        return 0
    )
    return 0;
}

bool aug_div(OBJ* t, OBJ* l, OBJ* r)
{
    AUG_DEFINE_BINOP(l,r,
        return aug_set_float(t, (float)l->i / r->i),
        return aug_set_float(t, l->i / r->f),
        return aug_set_float(t, l->f / r->i),
        return aug_set_float(t, l->f / r->f),
        return aug_set_char(t, l->c / r->c),
        return 0
    )
    return 0;
}

bool aug_pow(OBJ* t, OBJ* l, OBJ* r)
{
    AUG_DEFINE_BINOP(l,r,
        return aug_set_int(t, (int)powf((float)l->i, (float)r->i)),
        return aug_set_float(t, powf((float)l->i, r->f)),
        return aug_set_float(t, powf(l->f, (float)r->i)),
        return aug_set_float(t, powf(l->f, r->f)),
        return 0,
        return 0
    )
    return 0;
}

bool aug_mod(OBJ* t, OBJ* l, OBJ* r)
{
    AUG_DEFINE_BINOP(l,r,
        return aug_set_int(t, l->i % r->i),
        return aug_set_float(t, (float)fmod(l->i, r->f)),
        return aug_set_float(t, (float)fmod(l->f, r->i)),
        return aug_set_float(t, (float)fmod(l->f, r->f)),
        return 0,
        return 0
    )
    return 0;
}

bool aug_lt(OBJ* t, OBJ* l, OBJ* r)
{
    AUG_DEFINE_BINOP(l,r,
        return aug_set_bool(t, l->i < r->i),
        return aug_set_bool(t, l->i < r->f),
        return aug_set_bool(t, l->f < r->i),
        return aug_set_bool(t, l->f < r->f),
        return aug_set_bool(t, l->c < r->c),
        return 0
    )
    return 0;
}

bool aug_lte(OBJ* t, OBJ* l, OBJ* r)
{
    AUG_DEFINE_BINOP(l,r,
        return aug_set_bool(t, l->i <= r->i),
        return aug_set_bool(t, l->i <= r->f),
        return aug_set_bool(t, l->f <= r->i),
        return aug_set_bool(t, l->f <= r->f),
        return aug_set_bool(t, l->c <= r->c),
        return 0
    )
    return 0;
}

bool aug_gt(OBJ* t, OBJ* l, OBJ* r)
{
    AUG_DEFINE_BINOP(l,r,
        return aug_set_bool(t, l->i > r->i),
        return aug_set_bool(t, l->i > r->f),
        return aug_set_bool(t, l->f > r->i),
        return aug_set_bool(t, l->f > r->f),
        return aug_set_bool(t, l->c > r->c),
        return 0
    )
    return 0;
}

bool aug_gte(OBJ* t, OBJ* l, OBJ* r)
{
    AUG_DEFINE_BINOP(l,r,
        return aug_set_bool(t, l->i >= r->i),
        return aug_set_bool(t, l->i >= r->f),
        return aug_set_bool(t, l->f >= r->i),
        return aug_set_bool(t, l->f >= r->f),
        return aug_set_bool(t, l->c >= r->c),
        return 0
    )
    return 0;
}

bool aug_eq(OBJ* t, OBJ* l, OBJ* r)
{
    // TODO: add a special case for object equivalence (per field)
    AUG_DEFINE_BINOP(l,r,
        return aug_set_bool(t, l->i == r->i),
        return aug_set_bool(t, l->i == r->f),
        return aug_set_bool(t, l->f == r->i),
        return aug_set_bool(t, l->f == r->f),
        return aug_set_bool(t, l->c == r->c),
        return aug_set_bool(t, l->b == r->b)
    )
    return 0;
}

bool aug_neq(OBJ* t, OBJ* l, OBJ* r)
{
    // TODO: add a special case for object equivalence (per field)
    AUG_DEFINE_BINOP(l,r,
        return aug_set_bool(t, l->i != r->i),
        return aug_set_bool(t, l->i != r->f),
        return aug_set_bool(t, l->f != r->i),
        return aug_set_bool(t, l->f != r->f),
        return aug_set_bool(t, l->c != r->c),
        return aug_set_bool(t, l->b != r->b)
    )
    return 0;
}

bool aug_approxeq(OBJ* t, OBJ* l, OBJ* r)
{
    AUG_DEFINE_BINOP(l,r,
        return aug_set_bool(t, l->i == r->i),
        return aug_set_bool(t, abs(l->i - r->f) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(t, abs(l->f - r->i) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(t, abs(l->f - r->f) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(t, l->c == r->c),
        return aug_set_bool(t, l->b == r->b)
    )
    return 0;
}

bool aug_and(OBJ* t, OBJ* l, OBJ* r)
{
    return aug_set_bool(t, aug_get_bool(l) && aug_get_bool(r));
}

bool aug_or(OBJ* t, OBJ* l, OBJ* r)
{
    return aug_set_bool(t, aug_get_bool(l) || aug_get_bool(r));
}

typedef union VM_exe_value
{
    bool b;
    int i;
    char c;
    float f;
    unsigned char bytes[sizeof(float)]; 
} VM_exe_value;

#define AUG_VM_ERROR(vm, ...)  LOG( __VA_ARGS__);  vm->pc = NULL;

#define UNOP_ERR(vm, arg, op)                              \
{                                                                   \
    AUG_VM_ERROR(vm, "%s %s not defined",                           \
        op,                                                         \
        arg ? TYPES[(int)arg->type] : "(null)");    \
}

#define BINOP_ERR(vm, l, r, op)                        \
{                                                                   \
    AUG_VM_ERROR(vm, "%s %s %s not defined",                        \
        l ? TYPES[(int)l->type] : "(null)",     \
        op,                                                         \
        r ? TYPES[(int)r->type] : "(null)")     \
}

OBJ* VM_top(VM* vm)
{
    return &vm->stack[vm->stack_index -1];
}

OBJ* VM_push(VM* vm)
{
    if(vm->stack_index >= AUG_STACK_SIZE)
    {                                              
        if(vm->pc)
            AUG_VM_ERROR(vm, "Stack overflow");      
        return NULL;                           
    }
    OBJ* top = &vm->stack[vm->stack_index++];
    return top;
}

OBJ* VM_pop(VM* vm)
{
    return &vm->stack[--vm->stack_index];
}
void VM_push_call_frame(VM* vm, int return_addr)
{
    OBJ* ret_value = VM_push(vm);
    if(ret_value == NULL)
        return;
    ret_value->type = 2;
    ret_value->i = return_addr;

    OBJ* base_value = VM_push(vm);
    if(base_value == NULL)
        return;
    base_value->type = 2;
    base_value->i = vm->base_index;    
}
int VM_read_int(VM* vm)
{
    VM_exe_value exe_value;
    for(size_t i = 0; i < sizeof(exe_value.i); ++i)
        exe_value.bytes[i] = *(vm->pc++);
    return exe_value.i;
}
void VM_execute(VM* vm)
{
    vm->pc = vm->exe;
    while(vm->pc)
    {
        switch((uint8_t)(*vm->pc++))
        {
            case 1: break;
            case 0: vm->pc = NULL; break;
            case 2: aug_decref(VM_pop(vm)); break;
            case 3:
            {
                OBJ* value = VM_push(vm);
                if(value == NULL)
                    break;
                value->type = 6;
                break;
            }
            case 4:
            {
                OBJ* value = VM_push(vm);
                if(value == NULL)
                    break;
                VM_exe_value exe_value;
                for(size_t i = 0; i < sizeof(exe_value.b); ++i)
                    exe_value.bytes[i] = *(vm->pc++);
                aug_set_bool(value, exe_value.b);
                break;
            }
            case 5:   
            {
                OBJ* value = VM_push(vm);
                if(value == NULL) 
                    break;
                aug_set_int(value, VM_read_int(vm));
                break;
            }
            case 6:   
            {
                OBJ* value = VM_push(vm);
                if(value == NULL) 
                    break;

                VM_exe_value exe_value;
                for(size_t i = 0; i < sizeof(exe_value.c); ++i)
                    exe_value.bytes[i] = *(vm->pc++);
                aug_set_char(value, exe_value.c);
                break;
            }
            case 7:
            {
                OBJ* value = VM_push(vm);
                if(value == NULL) 
                    break;
                VM_exe_value exe_value;
                for(size_t i = 0; i < sizeof(exe_value.f); ++i)
                    exe_value.bytes[i] = *(vm->pc++);
                aug_set_float(value, exe_value.f);
                break;
            }                                  
            case 8:
            {
                size_t len = 1;  while(*(vm->pc++)) len++;
                OBJ value;
                aug_set_string(&value, vm->pc - len);
                OBJ* top = VM_push(vm);                
                aug_move(top, &value);
                break;
            }
           case 9:
            {
                OBJ value;
                aug_set_array(&value);
                int count = VM_read_int(vm);
                while(count-- > 0)
                {
                    OBJ* element = aug_array_push(value.array);
                    if(element != NULL) 
                    {
                        *element = aug_none();
                        aug_move(element, VM_pop(vm));
                    }
                }
                OBJ* top = VM_push(vm);          
                aug_move(top, &value);
                break;
            }
            case 10:
            {
                const int stack_offset = VM_read_int(vm);
                OBJ* local = &vm->stack[vm->base_index + stack_offset];
                OBJ* top = VM_push(vm);
                aug_assign(top, local);
                break;
            }
            case 11:
            {
                const int stack_offset = VM_read_int(vm);
                OBJ* local = &vm->stack[stack_offset];
                OBJ* top = VM_push(vm);
                aug_assign(top, local);
                break;
            }
            case 12:
            {
                OBJ* index_expr = VM_pop(vm);
                OBJ* container = VM_pop(vm);
                OBJ value;
                if(!aug_get_element(container, index_expr, &value))    
                {
                    AUG_VM_ERROR(vm, "Index error"); // TODO: more descriptive
                    break;  
                }
                OBJ* top = VM_push(vm);
                aug_move(top, &value);
                aug_decref(container);
                aug_decref(index_expr);
                break;
            }
            case 14:
            {
                const int stack_offset = VM_read_int(vm);
                OBJ* local = &vm->stack[vm->base_index + stack_offset];

                OBJ* top = VM_pop(vm);
                aug_move(local, top);
                break;
            }
            case 15:
            {
                const int stack_offset = VM_read_int(vm);
                OBJ* local = &vm->stack[stack_offset];

                OBJ* top = VM_pop(vm);
                aug_move(local, top);
                break;
            }            
            case 16:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);    
                if(!aug_add(t, l, r))
                    BINOP_ERR(vm, l, r, "+");
                break;
            }
            case 17:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);    
                if(!aug_sub(t, l, r))
                    BINOP_ERR(vm, l, r, "-");
                break;
            }
            case 18:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);    
                if(!aug_mul(t, l, r))
                    BINOP_ERR(vm, l, r, "*");
                break;
            }
            case 19:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);    
                if(!aug_div(t, l, r))
                    BINOP_ERR(vm, l, r, "/");
                break;
            }
            case 20:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);
                if(!aug_pow(t, l, r))
                    BINOP_ERR(vm, l, r, "^");
                break;
            }
            case 21:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);
                if(!aug_mod(t, l, r))
                    BINOP_ERR(vm, l, r, "%%");
                break;
            }
            case 25:
            {
                OBJ* arg = VM_pop(vm);
                OBJ* t = VM_push(vm);
                if(!aug_set_bool(t, !aug_get_bool(arg)))
                    UNOP_ERR(vm, arg, "!");
                break;
            }
            case 22:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);
                if(!aug_and(t, l, r))
                    BINOP_ERR(vm, l, r, "&");
                break;
            }
            case 23:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);
                if(!aug_or(t, l, r))
                    BINOP_ERR(vm, l, r, "|");
                break;
            }
            case 31:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);
                if(!aug_lt(t, l, r))
                    BINOP_ERR(vm, l, r, "<");
                break;
            }
            case 28:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);
                if(!aug_lte(t, l, r))
                    BINOP_ERR(vm, l, r, "<=");
                break;
            }
            case 34:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);
                if(!aug_gt(t, l, r))
                    BINOP_ERR(vm, l, r, ">");
                break;
            }
            case 30:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);
                if(!aug_gte(t, l, r))
                    BINOP_ERR(vm, l, r, ">=");
                break;
            }
            case 27:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);
                if(!aug_eq(t, l, r))
                    BINOP_ERR(vm, l, r, "==");
                break;
            }
            case 33:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);
                if(!aug_neq(t, l, r))
                    BINOP_ERR(vm, l, r, "!=");
                break;
            }
            case 29:
            {
                OBJ* r = VM_pop(vm);
                OBJ* l = VM_pop(vm);
                OBJ* t = VM_push(vm);
                if(!aug_approxeq(t, l, r))
                    BINOP_ERR(vm, l, r, "~=");
                break;
            }
            case 37:
            {
                const int pc_offset = VM_read_int(vm);
                vm->pc = vm->exe + pc_offset;
                break;
            }
            case 36:
            {
                const int pc_offset = VM_read_int(vm);
                OBJ* top = VM_pop(vm);
                if(aug_get_bool(top) != 0)
                    vm->pc = vm->exe + pc_offset;
                aug_decref(top);
                break;
            }
            case 35:
            {
                const int pc_offset = VM_read_int(vm);
                OBJ* top = VM_pop(vm);
                if(aug_get_bool(top) == 0)
                    vm->pc = vm->exe + pc_offset;
                aug_decref(top);
                break;
            }
            case 41:
            {
                const int delta = VM_read_int(vm);
                int i;
                for(i = 0; i < delta; ++i)
                    aug_decref(VM_pop(vm));
                break;
            }
            case 13:
            {
                const int ret_addr = VM_read_int(vm);
                VM_push_call_frame(vm, ret_addr);
                break;
            }
            case 40:
            {
                const int func_addr = VM_read_int(vm);
                vm->pc = vm->exe + func_addr;
                vm->base_index = vm->stack_index;
                break;
            }
            case 38:
            {
                OBJ* ret_value = VM_pop(vm);
                const int delta = VM_read_int(vm);
                int i;
                for(i = 0; i < delta; ++i)
                    aug_decref(VM_pop(vm));
                OBJ* ret_base = VM_pop(vm);
                if(ret_base == NULL)
                {                    
                    AUG_VM_ERROR(vm, "Calling frame setup incorrectly. Stack missing stack base");
                    break;
                }
                vm->base_index = ret_base->i;
                aug_decref(ret_base);
                OBJ* ret_addr = VM_pop(vm);
                if(ret_addr == NULL)
                {                    
                    AUG_VM_ERROR(vm, "Calling frame setup incorrectly. Stack missing return address");
                    break;
                }
                if(ret_addr->i == -1) vm->pc = NULL;
                else vm->pc = vm->exe + ret_addr->i;
                aug_decref(ret_addr);
                OBJ* top = VM_push(vm);
                if(ret_value != NULL && top != NULL)
                    *top = *ret_value;
                break;
            }
            case 39:
            {
                OBJ* func_index_value = VM_pop(vm);
                if(func_index_value == NULL || func_index_value->type != 2)
                {
                    aug_decref(func_index_value);

                    AUG_VM_ERROR(vm, "External Function Call expected function index to be pushed on stack");
                    break;                    
                }
                const int arg_count = VM_read_int(vm);                
                const int func_index = func_index_value->i;
                OBJ* args = AUG_ALLOC_ARRAY(OBJ, arg_count);
                int i;
                for(i = arg_count - 1; i >= 0; --i)
                {
                    OBJ* arg = VM_pop(vm);
                    if(arg != NULL)
                    {
                        OBJ value = aug_none();
                        aug_move(&value, arg);
                        args[arg_count - i - 1] = value;
                    }
                }
                if(func_index >= 0 && func_index < AUG_EXTENSION_SIZE && vm->exts[func_index] != NULL)
                {
                    // Call the external function. Move return value on to top of stack
                    OBJ ret_value = vm->exts[func_index](arg_count, args);
                    OBJ* top = VM_push(vm);
                    if(top)
                        aug_move(top, &ret_value);
                }
                else {
                    AUG_VM_ERROR(vm, "External Function Called at index %d not registered", func_index);
                }
                aug_decref(func_index_value);
                for(i = 0; i < arg_count; ++i)
                    aug_decref(&args[i]);
                AUG_FREE_ARRAY(args);
                break;
            }
        }
    }
}

void AST_to_ir(VM* vm, const AST* node, IR*ir)
{
    if(node == NULL || !ir->valid)
        return;
    const TOK token = node->token;
    STR* token_data = token.data; 
    AST** children = node->children;
    const int children_size = node->children_size;
    int i;
    switch(node->id)
    {
        case AUG_AST_ROOT:
            IR_push_frame(ir, 0);
            for(i = 0; i < children_size; ++ i)
                AST_to_ir(vm, children[i], ir);
            IR_add_op(ir, 0); IR_pop_frame(ir); 
            break;
        case AUG_AST_BLOCK: 
            for(i = 0; i < children_size; ++ i)
                AST_to_ir(vm, children[i], ir);
            break;
        case AUG_AST_LITERAL:
        {
            switch (token.id)
            {
                case AUG_TOKEN_CHAR:
                {
                    const char data = token_data->buffer[0];
                    const IR_arg arg = IR_arg_from_char(data);
                    IR_add_op_arg(ir, 6, arg);
                    break;
                }
                case AUG_TOKEN_INT:
                {
                    const int data = strtol(token_data->buffer, NULL, 10);
                    const IR_arg arg = IR_arg_from_int(data);
                    IR_add_op_arg(ir, 5, arg);
                    break;
                }
                case AUG_TOKEN_HEX:
                {
                    const unsigned int data = strtoul(token_data->buffer, NULL, 16);
                    const IR_arg arg = IR_arg_from_int(data);
                    IR_add_op_arg(ir, 5, arg);
                    break;
                }
                case AUG_TOKEN_BINARY:
                {                    
                    const unsigned int data = strtoul(token_data->buffer, NULL, 2);
                    const IR_arg arg = IR_arg_from_int(data);
                    IR_add_op_arg(ir, 5, arg);
                    break;
                }
                case AUG_TOKEN_FLOAT:
                {
                    const float data = strtof(token_data->buffer, NULL);
                    const IR_arg arg = IR_arg_from_float(data);
                    IR_add_op_arg(ir, 7, arg);
                    break;
                }
                case AUG_TOKEN_STRING:
                {
                    const IR_arg arg = IR_arg_from_str(token_data->buffer);
                    IR_add_op_arg(ir, 8, arg);
                    break;
                }
                case AUG_TOKEN_TRUE:
                {
                    const IR_arg arg = IR_arg_from_bool(true);
                    IR_add_op_arg(ir, 4, arg);
                    break;
                }
                case AUG_TOKEN_FALSE:
                {
                    const IR_arg arg = IR_arg_from_bool(0);
                    IR_add_op_arg(ir, 4, arg);
                    break;
                }
                default:break;
            }
            break;
        }
        case AUG_AST_VARIABLE:
        {
            const aug_symbol symbol = IR_symbol_relative(ir, token_data);
            if(symbol.type == 0)
            {
                ir->valid = 0;
                LOG_INPUT(ir->input, &token.pos, "Variable %s not defined in current block", token_data->buffer);
                return;
            }
            if(symbol.type == 2)
            {
                ir->valid = 0;
                LOG_INPUT(ir->input, &token.pos, "Function %s can not be used as a variable", token_data->buffer);
                return;
            }
            const IR_arg address_arg = IR_arg_from_int(symbol.offset);
            if(symbol.scope == 1) IR_add_op_arg(ir, 11, address_arg);
            else IR_add_op_arg(ir, 10, address_arg);
            break;
        }
        case AUG_AST_UNARY_OP:
        {
            AST_to_ir(vm, children[0], ir);
            switch (token.id)
            {
            case AUG_TOKEN_NOT: IR_add_op(ir, 25); break;
            default: break;
            }
            break;
        }
        case AUG_AST_BINARY_OP:
        {
            AST_to_ir(vm, children[0], ir); // LHS
            AST_to_ir(vm, children[1], ir); // RHS
            switch (token.id)
            {
                case AUG_TOKEN_ADD:       IR_add_op(ir, 16);      break;
                case AUG_TOKEN_SUB:       IR_add_op(ir, 17);      break;
                case AUG_TOKEN_MUL:       IR_add_op(ir, 18);      break;
                case AUG_TOKEN_DIV:       IR_add_op(ir, 19);      break;
                case AUG_TOKEN_MOD:       IR_add_op(ir, 21);      break;
                case AUG_TOKEN_POW:       IR_add_op(ir, 20);      break;
                case AUG_TOKEN_AND:       IR_add_op(ir, 22);      break;
                case AUG_TOKEN_OR:        IR_add_op(ir, 23);       break;
                case AUG_TOKEN_LT:        IR_add_op(ir, 31);       break;
                case AUG_TOKEN_LT_EQ:     IR_add_op(ir, 28);      break;
                case AUG_TOKEN_GT:        IR_add_op(ir, 34);       break;
                case AUG_TOKEN_GT_EQ:     IR_add_op(ir, 30);      break;
                case AUG_TOKEN_EQ:        IR_add_op(ir, 27);       break;
                case AUG_TOKEN_NOT_EQ:    IR_add_op(ir, 33);      break;
                case AUG_TOKEN_APPROX_EQ: IR_add_op(ir, 29); break;
                default: break;
            }
            break;
        }
        case AUG_AST_ARRAY:
        {
            for(i = children_size - 1; i >= 0; --i)
                AST_to_ir(vm, children[i], ir);

            const IR_arg count_arg = IR_arg_from_int(children_size);
            IR_add_op_arg(ir, 9, count_arg);
            break;
        }
        case AUG_AST_ELEMENT:
        {
            AST_to_ir(vm, children[0], ir); // push container var
            AST_to_ir(vm, children[1], ir); // push index
            IR_add_op(ir, 12);
            break;
        }
        case AUG_AST_STMT_EXPR:
        {
            if(children_size == 1)
            {
                AST_to_ir(vm, children[0], ir);
                IR_add_op(ir, 2);
            }
            break;
        }
        case AUG_AST_STMT_ASSIGN_VAR:
        {
            AST_to_ir(vm, children[0], ir);
            const aug_symbol symbol = IR_symbol_relative(ir, token_data);
            if(symbol.type == 0)
            {
                IR_set_var(ir, token_data);
                return;
            }
            else if(symbol.type == 2)
            {
                ir->valid = 0;
                LOG_INPUT(ir->input, &token.pos, "Can not assign function %s as a variable", token_data->buffer);
                return;
            }
            const IR_arg address_arg = IR_arg_from_int(symbol.offset);
            if(symbol.scope == 1) IR_add_op_arg(ir, 15, address_arg);
            else IR_add_op_arg(ir, 14, address_arg);
            break;
        }
        case AUG_AST_STMT_DEFINE_VAR:
        {
            if(children_size == 1) AST_to_ir(vm, children[0], ir);
            else IR_add_op(ir, 3);
            const aug_symbol symbol = IR_get_symbol_local(ir, token_data);
            if(symbol.offset != -1)
            {
                ir->valid = 0;
                LOG_INPUT(ir->input, &token.pos, "Variable %s already defined in block", token_data->buffer);
                return;
            }
            IR_set_var(ir, token_data);
            break;
        }
        case AUG_AST_STMT_IF:
        {
            const IR_arg stub_arg = IR_arg_from_int(0);
            AST_to_ir(vm, children[0], ir);
            const size_t end_block_jmp = IR_add_op_arg(ir, 35, stub_arg);
            IR_push_scope(ir);
            AST_to_ir(vm, children[1], ir);
            IR_pop_scope(ir);
            const size_t end_block_addr = ir->exe_offset;
            IR_get_op(ir, end_block_jmp)->arg = IR_arg_from_int(end_block_addr);
            break;
        }
        case AUG_AST_STMT_IF_ELSE:
        {
            AST_to_ir(vm, children[0], ir);
            const size_t else_block_jmp = IR_add_op_arg(ir, 35, IR_arg_from_int(0));
            IR_push_scope(ir);
            AST_to_ir(vm, children[1], ir);
            IR_pop_scope(ir);
            const size_t end_block_jmp = IR_add_op_arg(ir, 37, IR_arg_from_int(0));
            const size_t else_block_addr = ir->exe_offset;
            IR_push_scope(ir);
            AST_to_ir(vm, children[2], ir);
            IR_pop_scope(ir);
            const size_t end_block_addr = ir->exe_offset;
            IR_get_op(ir, else_block_jmp)->arg = IR_arg_from_int(else_block_addr);
            IR_get_op(ir, end_block_jmp)->arg = IR_arg_from_int(end_block_addr);
            break;
        }
        case AUG_AST_STMT_WHILE:
        {
            const IR_arg begin_block_arg = IR_arg_from_int(ir->exe_offset);
            AST_to_ir(vm, children[0], ir);
            const size_t end_block_jmp = IR_add_op_arg(ir, 35, IR_arg_from_int(0));
            IR_push_scope(ir);
            AST_to_ir(vm, children[1], ir);
            IR_pop_scope(ir);
            IR_add_op_arg(ir, 37, begin_block_arg);
            const size_t end_block_addr = ir->exe_offset;
            IR_get_op(ir, end_block_jmp)->arg = IR_arg_from_int(end_block_addr);
            break;
        }
        case AUG_AST_FUNC_CALL:
        {
            const aug_symbol symbol = IR_get_symbol(ir, token_data);
            if(symbol.type == 1)
            {
                ir->valid = 0;
                LOG_INPUT(ir->input, &token.pos, "Can not call variable %s as a function", token_data->buffer);
                break;
            }

            if(symbol.type == 2)
            {
                if(symbol.argc != children_size)
                {
                    ir->valid = 0;
                    LOG_INPUT(ir->input, &token.pos, "Function Call %s passed %d arguments, expected %d", token_data->buffer, children_size, symbol.argc);
                    break;
                }

                size_t push_frame = IR_add_op_arg(ir, 13, IR_arg_from_int(0));
                for(i = 0; i < children_size; ++ i)
                    AST_to_ir(vm, children[i], ir);
                const IR_arg func_addr = IR_arg_from_int(symbol.offset); // func addr
                IR_add_op_arg(ir, 40, func_addr);
                IR_get_op(ir, push_frame)->arg = IR_arg_from_int(ir->exe_offset);
                break;
            }
            int func_index = -1;
            for(i = 0; i < AUG_EXTENSION_SIZE; ++i)
            {
                if(vm->exts[i] != NULL && STR_compare(vm->ext_names[i], token_data))
                {
                    func_index = i;
                    break;
                }
            }
            if(func_index == -1)
            {
                ir->valid = 0;
                LOG_INPUT(ir->input, &token.pos, "Function %s not defined", token_data->buffer);
                break;
            }
            for(i = children_size - 1; i >= 0; --i)
                AST_to_ir(vm, children[i], ir);

            IR_add_op_arg(ir, 5, IR_arg_from_int(func_index));
            IR_add_op_arg(ir, 39, IR_arg_from_int(children_size));
            break;
        }
        case AUG_AST_RETURN:
        {
            if(children_size == 1)  AST_to_ir(vm, children[0], ir);
            else IR_add_op(ir, 3);
            IR_add_op_arg(ir, 38, IR_arg_from_int(IR_calling_offset(ir)));
            break;
        }
        case AUG_AST_PARAM: IR_set_param(ir, token_data); break;
        case AUG_AST_PARAM_LIST:
        {
            int i;
            for(i = 0; i < children_size; ++ i)
                AST_to_ir(vm, children[i], ir);
            break;
        }
        case AUG_AST_FUNC_DEF:
        {
            const size_t end_block_jmp = IR_add_op_arg(ir, 37, IR_arg_from_int(0));
            AST* params = children[0];
            const int param_count = params->children_size;
            if(!IR_set_func(ir, token_data, param_count))
            {
                ir->valid = 0;
                LOG_INPUT(ir->input, &token.pos, "Function %s already defined", token_data->buffer);
                break;
            }
            IR_push_scope(ir);
            AST_to_ir(vm, children[0], ir);
            IR_push_frame(ir, param_count);
            AST_to_ir(vm, children[1], ir);
            if(IR_last_op(ir)->opcode != 38)
            {
                IR_add_op(ir, 3);
                IR_add_op_arg(ir, 38, IR_arg_from_int(IR_calling_offset(ir)));
            }
            IR_pop_frame(ir);
            IR_pop_scope(ir);
            IR_get_op(ir, end_block_jmp)->arg = IR_arg_from_int(ir->exe_offset);
            break;
        }
        default: break;
    }
}

char* IR_to_exe(IR* ir)
{  
    if(ir->exe_offset == 0) return NULL;
    char* exe = AUG_ALLOC_ARRAY(char, ir->exe_offset);
    char* pc = exe;

    size_t i;
    for(i = 0; i < ir->ops.length; ++i)
    {
        OP* op = (OP*)bin_at(&ir->ops, i);
        (*pc++) = (uint8_t)op->opcode;
        switch (op->arg.type)
        {
        case 0: break;
        case 1: case 2: case 3: case 4: for(size_t i = 0; i < IR_arg_size(op->arg); ++i) *(pc++) = op->arg.data.bytes[i]; break;
        case 5: for(size_t i = 0; i < strlen(op->arg.data.str); ++i) *(pc++) = op->arg.data.str[i]; *(pc++) = 0; break;
        }
    }
    return exe;
}
VM* aug_startup()
{
    VM* vm = AUG_ALLOC(VM);
    memset(vm->exts, NULL, AUG_EXTENSION_SIZE * sizeof(*vm->exts));
    memset(vm->ext_names, NULL, AUG_EXTENSION_SIZE * sizeof(*vm->ext_names));
    int i; for(i = 0; i < AUG_STACK_SIZE; ++i) vm->stack[i] = aug_none();
    vm->stack_index = vm->base_index = 0;
    return vm;
}

void aug_shutdown(VM* vm)
{
     while(vm->stack_index > 0)
        aug_decref(VM_pop(vm));
    memset(vm->exts, NULL, AUG_EXTENSION_SIZE *  sizeof(*vm->exts));
    int i; for(i = 0; i < AUG_EXTENSION_SIZE; ++i) vm->ext_names[i] = STR_decref(vm->ext_names[i]);
    AUG_FREE(vm);
}

void aug_register(VM* vm, const char* name, aug_ext *ext)
{
    int i; for(i = 0; i < AUG_EXTENSION_SIZE; ++i)
    {
        if(vm->exts[i] == NULL && (vm->exts[i] = ext))
        {
            vm->ext_names[i] =  STR_create(name);
            break;
        }
    }
}

void aug_execute(VM* vm,const char* filename)
{
    aug_input* input = aug_input_open(filename);
    if(input == NULL) return;
    AST* root = aug_parse(vm, input);
    IR* ir = IR_new(input);
    AST_to_ir(vm, root, ir);
    vm->exe = IR_to_exe(ir);
    IR_delete(ir);
    AST_delete(root);
    aug_input_close(input);
    VM_execute(vm);
    AUG_FREE(vm->exe);
}
int print_value(OBJ value)
{
	switch (value.type)
	{
	case 6: printf("none"); break;
	case 0: printf("%s", value.b ? "true" : "0"); break;
	case 1: printf("%c", value.c); break;
	case 2: printf("%d", value.i); break;
	case 3: printf("%0.3f", value.f); break;
	case 4: printf("%s", value.str->buffer); break;
	case 5:
		printf("[ ");
        for( size_t i = 0; i < value.array->length; ++i)
            print_value(*aug_array_at(value.array, i)) && printf(" ");
		printf("]");
		break;
	}
    return 0;
}
OBJ print(int argc, const OBJ* args)
{
	for( int i = 0; i < argc; ++i)
		print_value(args[i]);
	printf("\n");
	return aug_none();
}

int main(int argc, char** argv)
{
	VM* vm = aug_startup();
	aug_register(vm, "print", print);
	aug_execute(vm, argv[1]);
	aug_shutdown(vm);
	return 0;
}
