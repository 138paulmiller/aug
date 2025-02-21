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
    aug.h - single file embeddable script engine. Self-contained bytecode compiler and virtual machine 

    * To use exactly one source file must contain the following 
        #define AUG_IMPLEMENTATION
        #include "aug.h"

    Todo: 
    -  API to set/get values from script instance, like aug_call
    -  Better runtime error handling!
        - Create a map from vm operation to source line. have ast keep file line info 
        - Create a map from bytecode addr to source code position. In aug_vm log error, add this. 
        - Must support multiple intput sources. MOdify the raw input reference, use file index values for mapping to the source file
        - Create an error type to allow extensions for forward errors to VM
        - Create a symtable datatype. Create uniform API for IR and VM to compile in IR pass and as well as serialize/deserialize from debug file
    - Create Programs for loading/unloading compiled code. 
        - This will compile script code to a string literal and create a standalone c/c++ app that boots vm and executes bytecode 
        - The compiled scripts will also dump symtables to allow aug_call*s
    - Implement value references to allow iterators to modify the element
    - Implement match statements, like switch statements that ease "if else if" chains 
    - Implement wildcard, glob matching for strings
    - Implement range semantics
        - add support to push range objects onto stack [0..n]. Creating an iterable from these will only store beginning, end, and step amount. 
    - Implement objects 
        - Custom type registration. create accessors from field offset. allow users to define struct. 
    - Create default methods for built-in types
        - Add, remove, push, pop, find, sort
    - Serialize Bytecode to external file. Execute compiled bytecode from file
    - Serialize debug symbols to file. Link from bytecode to source file.
        - add source file and line to debug symbols
    - File, Directory, Vector, Matrix primitive types 
        - create default language bindings to sdl, fileio, win32 etc...
    - Operator overloading? 
    - Built-in math and utility functions. Perhaps registered as the default ? For math, directly use opcodes
    - Syntax to allow multiple var defines in aug_parse_stmt_define_var. Create a Define_Var_List AST node, seperate by comma
    - Implement method calls. 
        - Syntax, parse expr, then dot, then expr excluding literals.
        - Semantics, in AST method call push the object, then the params, then call func
                     in AST method def, load object, then the params. Find all vars that are fields to the object in prepass. Add to symtable under 
    - Short circuit evaluation with and/or statements. Push one on, check if true, then push next, check it true
    - For using libs, create a new LIB symbol in the symtable. Have each call prefixed with the lib name, then in IR pass open lib and check if func exists ? 
    - Allow importing compiled bytecode. Serialize symtable to bytecode, on use, deserialize the symtable and append the bytecode to the current IR.

    Issue:
    - Retaining script lib state via lib_extensions map is a bit hacky. 
        - Perhaps create an array for lib_extensions. Have the script load/unload extension to/from VM on startup/shutdown 

    Note:
    - Locally defined functions can be supported, but will require closures to be implemented. See the aug_parse_stmt(aug_lexer* lexer, bool is_block)
        - To define closures, pass all the reference variables on the stack and re-add to the functions local symtable. Adjust the decstack well
*/
#ifndef __AUG_HEADER__
#define __AUG_HEADER__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AUG_DEBUG
#define AUG_DEBUG 0
#endif//AUG_DEBUG

// Size of the virtual machine's value stack 
#ifndef AUG_STACK_SIZE
#define AUG_STACK_SIZE (1024 * 16)
#endif//AUG_STACK_SIZE

// Threshold of the nearly equal operator
#ifndef AUG_APPROX_THRESHOLD
#define AUG_APPROX_THRESHOLD 0.0000001
#endif//AUG_APPROX_THRESHOLD 

#ifndef AUG_ALLOW_NO_SEMICOLON
#define AUG_ALLOW_NO_SEMICOLON true
#endif//AUG_ALLOW_NO_SEMICOLON

#ifndef AUG_ALLOW_SINGLE_STMT_BLOCK
#define AUG_ALLOW_SINGLE_STMT_BLOCK true
#endif//AUG_ALLOW_SINGLE_STMT_BLOCK

#ifndef AUG_REGISTER_LIB_FUNC
#define AUG_REGISTER_LIB_FUNC "aug_register_lib"
#endif//AUG_REGISTER_LIB_FUNC

#if defined(__cplusplus) && defined(_MSC_VER)
#define AUG_STDCALL __stdcall
#elif defined(__cplusplus) && defined(__GNUC__)
#define AUG_STDCALL extern "C" //__attribute__((visibility("default")))
#else 
#define AUG_STDCALL
#endif 

#if defined(_MSC_VER)
#define AUG_LIB void __declspec(dllexport) AUG_STDCALL
#elif defined(__GNUC__)
#define AUG_LIB void AUG_STDCALL
#else
#define AUG_LIB
#pragma warning Potentially Unsupported compiler. Unknown dynamic link import/export semantics.
#endif

#include <limits.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#if _WIN32
#include <Windows.h>
#elif __linux
#include <dlfcn.h>
#endif

// Data structures
typedef struct aug_vm aug_vm;
typedef struct aug_value aug_value;
typedef struct aug_hashtable aug_hashtable;
typedef struct aug_container aug_container;

// String data type value
typedef struct aug_string
{
	char *buffer;
	int ref_count;
	size_t capacity;
	size_t length;
} aug_string;

// Array data type value
typedef struct aug_array
{
	aug_value* buffer;
	int ref_count;
	size_t capacity;
	size_t length;
} aug_array;

typedef struct aug_map_bucket aug_map_bucket;

// Associative Array data type value
typedef struct aug_map
{
    aug_map_bucket* buckets;
    size_t capacity;
    size_t count;
    size_t ref_count;
} aug_map;

// Range is a tuple [from,to) 
typedef struct aug_range
{
    int from, to;
    size_t ref_count;
} aug_range;

// TODO: Class data types value
typedef struct aug_object
{
    int ref_count;
    // Value Hash map aug_string -> aug_value
    //aug_std_array<aug_attribute> attribs;
} aug_object;

// All support engine type values
typedef enum aug_type
{
    AUG_BOOL,
    AUG_CHAR,
    AUG_INT, 
    AUG_FLOAT, 
    AUG_STRING, 
    AUG_ARRAY,
    AUG_MAP,
    AUG_RANGE,
    AUG_OBJECT,
    AUG_FUNCTION,
    AUG_ITERATOR,
    AUG_USERDATA,
    AUG_NONE,
} aug_type;

typedef struct aug_iterator
{
	aug_value* iterable;
	aug_value* index;
    size_t ref_count;
} aug_iterator;

// Values instance 
typedef struct aug_value
{
    aug_type type;
    union 
    {
        bool b;             // AUG_BOOL
        int i;              // AUG_INT/AUG_FUNCTION
        char c;             // AUG_CHAR
        float f;            // AUG_FLOAT
        aug_string* str;    // AUG_STRING
        aug_array* array;   // AUG_ARRAY
        aug_map* map;       // AUG_MAP
        aug_iterator* it;   // AUG_ITERATOR
        aug_range* range;   // AUG_RANGE
        aug_object* obj;    // AUG_OBJECT
        void* userdata;     // AUG_USERDATA (custom data type for users)
    };
} aug_value;

// Represents a "compiled" script
typedef struct aug_script
{
    aug_hashtable* globals;         // global defined symbols
    char* bytecode;                 // raw pointer to instruction set
    aug_array* stack_state;         // current state of the stack before and after execution 
    aug_hashtable* lib_extensions;  // lib loaded extensions

    aug_container* markers;   // aug_trace_marker info for error handling
} aug_script;

// Calling frames are used to access parameters and local variables from the stack within a calling context
typedef struct aug_frame
{
    int base_index;
    int stack_index;

    // For function call frames, the following fields will be used. Otherwise, the frame is used only for local scope 
    bool func_call;
    int arg_count;
    const char* instruction; 
} aug_frame;

typedef void(aug_error_func)(const char* /*msg*/);
typedef aug_value /*return*/(aug_extension_func)(int argc, aug_value* /*args*/);

typedef struct aug_extension
{
    aug_extension_func* func;
} aug_extension;

// Handle to a loaded extension lib
typedef void* aug_lib_handle;

// Type signature for external library entry points
typedef void (*aug_register_lib_func)(aug_vm* /*vm*/);

// Running instance of the virtual machine
typedef struct aug_vm
{
    const char* exec_filepath;
    aug_error_func* error_func;
    bool valid;
    bool running;

    aug_hashtable* extensions; // func_name->aug_extension. all globally registered extensions. available to all scripts
    aug_container* libs;       // aug_lib_handle. loaded library handles for the registered extensions
    aug_container* markers;    // weak pointer to script's aug_trace_markers
    aug_hashtable* lib_extensions;  // Weak pointer to script loaded libs

    // Script runtime context state

    const char* instruction;      // Index pointer to current bytecode being executed
    const char* last_instruction; // Weak pointer to bytecode last bytecode executed
    const char* bytecode;         // Weak pointer to script bytecode 
    aug_value stack[AUG_STACK_SIZE];
    int stack_index; // Current position on stack (ESP)
    int base_index;  // Current frame stack offset (EBP)
    int arg_count;   // Current argument count expected when entering a call frame

#if AUG_DEBUG
    void (*debug_post_instruction)(aug_vm* /*vm*/, int /*opcode*/);
#endif

} aug_vm;

typedef struct aug_vm_exec_state
{
    const char* bytecode;
    const char* instruction;
    const char* last_instruction;
    int base_index;
    aug_array* stack_state;         // current state of the stack before and after execution 
    aug_hashtable* lib_extensions;  
    aug_container* markers;   
} aug_vm_exec_state;

// VM API ----------------------------------------- VM API ---------------------------------------------------- VM API//

// VM Must call both startup before using the VM. When done, must call shutdown.
aug_vm* aug_startup(aug_error_func* on_error);
void aug_shutdown(aug_vm* vm);

// Extensions are native functions that can be called from scripts. Use aug_register/aug_unregister to manage extensions
void aug_register(aug_vm* vm, const char* func_name, aug_extension_func* func);
void aug_unregister(aug_vm* vm, const char* func_name);

// Will reboot the VM to execute the standalone script or code
void aug_execute(aug_vm* vm, const char* filename);

aug_value aug_eval(aug_vm* vm, const char* code);

// Compiles, executes and loads script into globals into memory from file
aug_script* aug_load(aug_vm* vm, const char* filename);

// Unload the script globals from the VM and memory
void aug_unload(aug_vm* vm, aug_script* script);

// Used to call global functions within the script
aug_value aug_call(aug_vm* vm, aug_script* script, const char* func_name);
aug_value aug_call_args(aug_vm* vm, aug_script* script, const char* func_name, int argc, aug_value* args);

void aug_save_state(aug_vm* vm, aug_vm_exec_state* exec_state);
void aug_load_state(aug_vm* vm, aug_vm_exec_state* exec_state);

#if AUG_DEBUG
const char* aug_opcode_label(uint8_t opcode);
const char* aug_ast_label(uint8_t ast_type);
#endif 

// Value API ------------------------------------- Value API ------------------------------------------------ Value API//
aug_value aug_none();
bool aug_to_bool(const aug_value* value);
int aug_to_int(const aug_value* value);
float aug_to_float(const aug_value* value);
bool aug_compare(aug_value* a, aug_value* b);
const char* aug_type_label(const aug_value* value);
void aug_decref(aug_value* value);
void aug_incref(aug_value* value);

aug_value aug_create_bool(bool data);
aug_value aug_create_int(int data);
aug_value aug_create_char(char data);
aug_value aug_create_float(float data);
aug_value aug_create_string(const char* data);
aug_value aug_create_array();
aug_value aug_create_map();
aug_value aug_create_user_data(void* data);

// String API------------------------------------ String API ----------------------------------------------- String API//
aug_string* aug_string_new(size_t size);
aug_string* aug_string_create(const char* bytes);
void aug_string_incref(aug_string* string);
aug_string* aug_string_decref(aug_string* string);
void aug_string_resize(aug_string* string, size_t size);
void aug_string_push(aug_string* string, char c);
char aug_string_pop(aug_string* string);
void aug_string_append(aug_string* a, const aug_string* b);
void aug_string_append_bytes(aug_string* string, const char* bytes, size_t len);
char aug_string_at(const aug_string* string, size_t index);
bool aug_string_set(const aug_string* string, size_t index, char c);
char aug_string_back(const aug_string* string);
bool aug_string_compare(const aug_string* a, const aug_string* b);
bool aug_string_compare_bytes(const aug_string* a, const char* bytes);

// Array API --------------------------------------- Array API --------------------------------------------- Array API//
aug_array* aug_array_new(size_t size);
void  aug_array_incref(aug_array* array);
aug_array* aug_array_decref(aug_array* array);
void aug_array_reserve(aug_array* array, size_t size);
void aug_array_resize(aug_array* array, size_t size);
aug_value* aug_array_push(aug_array* array);
aug_value* aug_array_pop(aug_array* array);
aug_value* aug_array_at(const aug_array* array, size_t index);
bool aug_array_set(const aug_array* array, size_t index, aug_value* value);
aug_value* aug_array_back(const aug_array* array);
bool aug_array_compare(const aug_array* a, const aug_array* b);
void aug_array_append(aug_array* array, aug_value* value);
void aug_array_remove(aug_array* array, size_t index);
aug_array* aug_array_copy(aug_array* array);

// Map API ------------------------------------------ Map API ------------------------------------------------- Map API//
aug_map* aug_map_new(size_t size);
void aug_map_incref(aug_map* map);
aug_map* aug_map_decref(aug_map* map);
bool aug_map_insert(aug_map* map, aug_value* key, aug_value* value);
bool aug_map_insert_or_update(aug_map* map, aug_value* key, aug_value* value);
bool aug_map_remove(aug_map* map, aug_value* key);
aug_value* aug_map_get(aug_map* map, aug_value* key);

typedef void(aug_map_iterator)(const aug_value* /*key*/, aug_value* /*value*/, void* /*user_data*/);
void aug_map_foreach(aug_map* /*map*/, aug_map_iterator* /*iterator*/, void* /*user_data*/);

// Iterator API ------------------------------------- Iterator API ----------------------------------------- Iterator API//
aug_iterator* aug_iterator_new(aug_value* iterable);
aug_iterator* aug_iterator_decref(aug_iterator* iterator);
void aug_iterator_incref(aug_iterator* iterator);
bool aug_iterator_next(aug_iterator* iterator);
bool aug_iterator_get(aug_iterator* iterator, aug_value* out_element);

// Range API ----------------------------------------- Range API --------------------------------------------- Range API//
aug_range* aug_range_new(int from, int to);
aug_range* aug_range_decref(aug_range* range);
void aug_range_incref(aug_range* range);

#ifdef __cplusplus
}
#endif

#endif //__AUG_HEADER__

// IMPLEMENTATION ============================= IMPLEMENTATION  ======================================= IMPLEMENTATION //

#if defined(AUG_IMPLEMENTATION)

#ifdef __cplusplus
extern "C" {
#endif

#include <ctype.h>
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

#if defined(_WIN32) && defined(__STDC_WANT_SECURE_LIB__)
#define AUG_SECURE
#endif

#ifndef AUG_ALLOC
#define AUG_ALLOC(size) malloc(size)
#endif//AUG_ALLOC

#ifndef AUG_FREE
#define AUG_FREE(ptr) free(ptr)
#endif//AUG_FREE

#ifndef AUG_REALLOC
#define AUG_REALLOC(ptr, size) realloc(ptr, size)
#endif//AUG_REALLOC

// CONTAINER ====================================   CONTAINER   ============================================ CONTAINER // 

// Generic resizeable array data structure that allocates bytes. 
// Supports for type specific casting to modify and access.

typedef struct aug_container
{
    char* buffer;  
	size_t capacity;
	size_t length;  
	size_t ref_count;  
    size_t element_size;
} aug_container;

aug_container* aug_container_new(size_t capacity, size_t element_size)    
{
    aug_container* container = (aug_container*)AUG_ALLOC(sizeof(aug_container));   
    container->length = 0;    
    container->ref_count = 1;   
    container->capacity = capacity;   
    container->element_size = element_size; 
    container->buffer = (char*)AUG_ALLOC(element_size * container->capacity);
    return container;   
}        

void aug_container_incref(aug_container* container)   
{
    if(container != NULL)
        ++container->ref_count;
}

aug_container* aug_container_decref(aug_container* container)   
{
    if(container != NULL && --container->ref_count == 0)
    {
        AUG_FREE(container->buffer); 
        AUG_FREE(container);
        return NULL;    
    }
    return container;
}

void aug_container_resize(aug_container* container, size_t size)    
{
    container->capacity = size;    
    container->buffer = (char*)AUG_REALLOC(container->buffer, container->element_size * container->capacity); 
}

char* aug_container_push(aug_container* container)    
{
    if (container->length+1 >= container->capacity) 
        aug_container_resize(container, 2 * container->capacity);
    return &container->buffer[container->length++ * container->element_size];    
}

char* aug_container_pop(aug_container* container)   
{        
    return container->length > 0 ? &container->buffer[(--container->length) * container->element_size] : NULL;    
}        

char* aug_container_at(const aug_container* container, size_t index)   
{        
    return index < container->length ? &container->buffer[index * container->element_size] : NULL;    
}        

char* aug_container_back(const aug_container* container)   
{        
    return container->length > 0 ? &container->buffer[(container->length - 1) * container->element_size] : NULL;    
}

#define aug_container_new_type(type, size) \
	aug_container_new(size, sizeof(type))

#define aug_container_push_type(type, container, data) \
	*((type*)aug_container_push(container)) = data

#define aug_container_pop_type(type, container) \
	*((type*)aug_container_pop(container))

#define aug_container_at_type(type, container, index) \
	*((type*)aug_container_at(container, index))

#define aug_container_ptr_type(type, container, index) \
	((type*)aug_container_at(container, index))

#define aug_container_back_type(type, container) \
	*((type*)aug_container_back(container))

// HASHTABLE ====================================== HASHTABLE =============================================== HASHTABLE//

#ifndef AUG_HASHTABLE_SIZE_DEFAULT
#define AUG_HASHTABLE_SIZE_DEFAULT 1
#endif//AUG_HASHTABLE_SIZE_DEFAULT

#ifndef AUG_HASHTABLE_BUCKET_SIZE_DEFAULT
#define AUG_HASHTABLE_BUCKET_SIZE_DEFAULT 1
#endif//AUG_HASHTABLE_BUCKET_SIZE_DEFAULT

typedef size_t(aug_hashtable_hash)(const char* /*str*/);
typedef void(aug_hashtable_free)(uint8_t* /*data*/);
typedef void(aug_hashtable_iterator)(uint8_t* /*data*/, void* /*user_data*/);

typedef struct aug_hashtable_bucket
{
    char** key_buffer;
    uint8_t* data_buffer;
    size_t capacity;
} aug_hashtable_bucket;

typedef struct aug_hashtable
{
	aug_hashtable_bucket* buckets;
	size_t capacity;
	size_t count;
    size_t ref_count;
	size_t element_size;

    aug_hashtable_hash * hash_func;
    aug_hashtable_free * free_func;
} aug_hashtable;

void aug_hashtable_bucket_init(aug_hashtable* map, size_t size)
{
    size_t i;
    for (i = 0; i < map->capacity; ++i)
    {
        aug_hashtable_bucket* bucket = &map->buckets[i];
        bucket->capacity = size;
        bucket->key_buffer = (char**)AUG_ALLOC(sizeof(char*) * size);
        bucket->data_buffer = (uint8_t*)AUG_ALLOC(sizeof(uint8_t) * map->element_size * size);
        memset(bucket->key_buffer, 0, sizeof(char*) * size);
    }
}

uint8_t* aug_hashtable_bucket_insert(aug_hashtable* map, aug_hashtable_bucket* bucket, const char* key)
{
    size_t i;
    for (i = 0; i < bucket->capacity; ++i)
    {
        if (bucket->key_buffer[i] == NULL)
            break;

        if (strcmp(bucket->key_buffer[i], key) == 0)
            return NULL;
    }

    if (i >= bucket->capacity)
    {
        size_t new_size = 2 * bucket->capacity;
        bucket->key_buffer = (char**)AUG_REALLOC(bucket->key_buffer, sizeof(char*) * new_size);
        bucket->data_buffer = (uint8_t*)AUG_REALLOC(bucket->data_buffer, map->element_size * new_size);

        size_t j; // init new entries to null 
        for (j = bucket->capacity; j < new_size; ++j)
            bucket->key_buffer[j] = NULL;
        bucket->capacity = new_size;
    }

    if (bucket->key_buffer[i] != NULL)
        AUG_FREE(bucket->key_buffer[i]);

    ++map->count;

    const size_t buff_size = strlen(key) + 1;
    bucket->key_buffer[i] = (char*)AUG_ALLOC(buff_size);
#ifdef AUG_SECURE
    strcpy_s(bucket->key_buffer[i], buff_size, key);
#else
    strcpy(bucket->key_buffer[i], key);
#endif
    return &bucket->data_buffer[i * map->element_size];
}

aug_hashtable* aug_hashtable_new(size_t size, size_t element_size, aug_hashtable_hash* hash, aug_hashtable_free* free)
{
    assert(hash != NULL);

    aug_hashtable* map = (aug_hashtable*)AUG_ALLOC(sizeof(aug_hashtable)); 
    map->capacity = size;
    map->element_size = element_size;
    map->hash_func = hash;
    map->free_func = free;
    map->ref_count = 1;
    map->count = 0;
    map->buckets = (aug_hashtable_bucket*)AUG_ALLOC(sizeof(aug_hashtable_bucket) * size);
    aug_hashtable_bucket_init(map, AUG_HASHTABLE_BUCKET_SIZE_DEFAULT);
    return map;
}

void aug_hashtable_resize(aug_hashtable* map, size_t size)
{
    size_t old_size = map->capacity;
    aug_hashtable_bucket* old_buckets = map->buckets;

    map->capacity = size;
    map->buckets = (aug_hashtable_bucket*)AUG_ALLOC(sizeof(aug_hashtable_bucket) * map->capacity);
    aug_hashtable_bucket_init(map, AUG_HASHTABLE_BUCKET_SIZE_DEFAULT);

    // reindex all values, copy over raw data 
    size_t i, j;
    for (i = 0; i < old_size; ++i)
    {
        aug_hashtable_bucket* old_bucket = &old_buckets[i];
        for (j = 0; j < old_bucket->capacity; ++j)
        {
            if (old_bucket->key_buffer[j] != NULL)
            {
                char* key = old_bucket->key_buffer[j];
                uint8_t* data = &old_bucket->data_buffer[j * map->element_size];
                size_t hash = map->hash_func(key);

                aug_hashtable_bucket* new_bucket = &map->buckets[hash % map->capacity];
                uint8_t* new_data = aug_hashtable_bucket_insert(map, new_bucket, key);
                assert(new_data != NULL);
                size_t e;
                for (e = 0; e < map->element_size; ++e)
                    new_data[e] = data[e];

                AUG_FREE(old_bucket->key_buffer[j]);
            }
        }

        AUG_FREE(old_bucket->data_buffer);
        old_bucket->data_buffer = NULL;
        AUG_FREE(old_bucket->key_buffer);
        old_bucket->key_buffer = NULL;
    }

    AUG_FREE(old_buckets);
}

void aug_hashtable_incref(aug_hashtable* map)
{
    if(map)
        ++map->ref_count;
}

aug_hashtable* aug_hashtable_decref(aug_hashtable* map)
{
    if(map && --map->ref_count == 0)
    {
        size_t i;
        for(i = 0; i < map->capacity; ++i)
        {
            aug_hashtable_bucket* bucket = &map->buckets[i];
            size_t j;
            for (j = 0; j < bucket->capacity; ++j)
            {
                if (bucket->key_buffer[j] != NULL)
                {
                    if (map->free_func != NULL)
                        map->free_func(&bucket->data_buffer[j * map->element_size]);
                    AUG_FREE(bucket->key_buffer[j]);
                }
            }
            AUG_FREE(bucket->data_buffer);
            AUG_FREE(bucket->key_buffer);
        }
        AUG_FREE(map->buckets);
        AUG_FREE(map);
        return NULL;
    }
    return map;
}

uint8_t* aug_hashtable_create(aug_hashtable* map, const char* key)
{
    size_t hash = map->hash_func(key);
    aug_hashtable_bucket* bucket = &map->buckets[hash % map->capacity];
    // If a bucket is larger than the map capacity, reindex all entries
    if (bucket->capacity > map->capacity)
    {
        aug_hashtable_resize(map, map->capacity * 2);
        bucket = &map->buckets[hash % map->capacity];
    }

    uint8_t* data = aug_hashtable_bucket_insert(map, bucket, key);
    if(data != NULL)
        ++map->count;
    return data;
}

bool aug_hashtable_remove(aug_hashtable* map, const char* key)
{
    const size_t find_hash = map->hash_func(key);
    aug_hashtable_bucket* bucket = &map->buckets[find_hash % map->capacity];
    size_t i;
    for(i = 0; i  < bucket->capacity; ++i)
    {
        const char* check_key = bucket->key_buffer[i];
        if(check_key != NULL && strcmp(key, check_key) == 0)
        {
            AUG_FREE(bucket->key_buffer[i]);
            bucket->key_buffer[i] = NULL;
            if(map->free_func)
                map->free_func(&bucket->data_buffer[i * map->element_size]);
            return true;
        }
    }
    return false;
}

uint8_t* aug_hashtable_get(aug_hashtable* map, const char* key)
{
    const size_t find_hash = map->hash_func(key);
    aug_hashtable_bucket* bucket = &map->buckets[find_hash % map->capacity];
    size_t i;
    for(i = 0; i  < bucket->capacity; ++i)
    {
        const char* check_key = bucket->key_buffer[i];
        if (check_key != NULL && strcmp(key, check_key) == 0)
            return &bucket->data_buffer[i * map->element_size];
    }
    return NULL;
}

void aug_hashtable_foreach(aug_hashtable* map, aug_hashtable_iterator* iterator, void* user_data)
{
    size_t i, j;
    for(i = 0; i < map->capacity; ++i)
    {
        aug_hashtable_bucket* bucket = &map->buckets[i];
        for(j = 0; j < bucket->capacity; ++j)
        {        
            if(bucket->key_buffer[j] != NULL)
                iterator(&bucket->data_buffer[j * map->element_size], user_data);
        }
    }
}

size_t aug_hashtable_hash_default(const char* str)
{
    size_t hash = 5381; // DJB2 hash
    while(*str)
        hash = ((hash << 5) + hash) + (size_t)(*str++);
    return hash;
}

#define aug_hashtable_new_type(type)\
    aug_hashtable_new(AUG_HASHTABLE_SIZE_DEFAULT, sizeof(type), aug_hashtable_hash_default, NULL)

#define aug_hashtable_insert_type(type, map, key)\
    ((type*)aug_hashtable_create(map, key))

#define aug_hashtable_ptr_type(type, map, key)\
    ((type*)aug_hashtable_get(map, key))

// LOGGING =====================================   LOGGING   ================================================= LOGGING // 

void aug_log_error_internal(aug_error_func* error_func, const char* format, va_list args)
{
    if(error_func)
    {
        // TODO: make thread safe
        static char log_buffer[4096];
        vsnprintf(log_buffer, sizeof(log_buffer), format, args);
        error_func(log_buffer);
    }
#if defined(AUG_LOG_VERBOSE)
    else
    {
        vprintf(format, args);
    }
#endif //defined(AUG_LOG_VERBOSE)
}

void aug_log_error(aug_error_func* error_func, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    aug_log_error_internal(error_func, format, args);
    va_end(args);
}

// INPUT ========================================   INPUT   ===================================================== INPUT // 

typedef struct aug_pos
{
    int filepos;
    int linepos;
    size_t line;
    size_t col;

    int c;
}aug_pos;

static const int AUG_INPUT_POS_BUFFER_SIZE = 2;

typedef struct aug_input
{
    FILE* file;
    const char* str;

    bool valid;
    aug_string* filename;
    int track_pos;
    int pos_buffer_index;
    aug_pos pos_buffer[2]; //store, prev, curr

    aug_error_func* error_func;
}aug_input;

static inline aug_pos* aug_input_pos(aug_input* input)
{
    return &input->pos_buffer[input->pos_buffer_index];
}

static inline aug_pos* aug_input_get_pos(aug_input* input, int dir)
{
    assert(input != NULL);
    const int len = ((int)input->pos_buffer_index + dir) % AUG_INPUT_POS_BUFFER_SIZE;
    if (len < 0) 
        input->pos_buffer_index = AUG_INPUT_POS_BUFFER_SIZE - input->pos_buffer_index;
    return &input->pos_buffer[len];
}

static inline aug_pos* aug_input_move_pos(aug_input* input, int dir)
{
    assert(input != NULL);
    input->pos_buffer_index = (input->pos_buffer_index + dir) % AUG_INPUT_POS_BUFFER_SIZE;
    // TODO: Fix ? Always false 
    if (input->pos_buffer_index < 0) 
        input->pos_buffer_index = AUG_INPUT_POS_BUFFER_SIZE + input->pos_buffer_index;
    return &input->pos_buffer[input->pos_buffer_index];
}

static inline char aug_input_get(aug_input* input)
{
    if(input == NULL)
        return -1;

    int c;
    if(input->file != NULL)
    {
        c = fgetc(input->file);
    }
    else 
    {
        aug_pos* pos = aug_input_pos(input);
        c = input->str[pos->filepos];
        if(c == '\0')
            return -1;
    }

    aug_pos* pos = aug_input_pos(input);
    aug_pos* next_pos = aug_input_move_pos(input, 1);
    next_pos->c = c;
    next_pos->line = pos->line;
    next_pos->col = pos->col + 1;
    next_pos->linepos = pos->linepos;
    next_pos->filepos = pos->filepos + 1;

    if(c == '\n')
    {
        next_pos->col = 0;
        next_pos->line = pos->line + 1;
        next_pos->linepos = next_pos->filepos;
    }
    return (char)c;
}

static inline char aug_input_peek(aug_input* input)
{
    assert(input != NULL);
    if(input->file != NULL)
    {
        int c = fgetc(input->file);
        ungetc(c, input->file);
        return (char)c;
    }

    aug_pos* pos = aug_input_pos(input);
    int c = input->str[pos->filepos];
    return (char)c;
}

static inline void aug_input_unget(aug_input* input)
{
    assert(input != NULL);
    aug_pos* pos = aug_input_pos(input);
    if(input->file != NULL)
        ungetc(pos->c, input->file);
    aug_input_move_pos(input, -1);
}

aug_input* aug_input_open(const char* filename, aug_error_func* error_func)
{
#ifdef AUG_SECURE
    FILE* file;
    fopen_s(&file, filename, "r+");
#else
    FILE* file = fopen(filename, "r+");
#endif //AUG_SECURE
    if(file == NULL)
    {
        aug_log_error(error_func, "Input failed to open file %s", filename);
        return NULL;
    }

    aug_input* input = (aug_input*)AUG_ALLOC(sizeof(aug_input));
    input->error_func = error_func;
    input->file = file;
    input->str = NULL;
    input->valid = true;
    input->filename = aug_string_create(filename);
    input->pos_buffer_index = 0;
    input->track_pos = 0;

    aug_pos* pos = aug_input_pos(input);
    pos->col = 0;
    pos->line = 0;
    pos->c = -1;
    pos->filepos = pos->linepos = 0;

    return input;
}

aug_input* aug_input_open_code(const char* code, aug_error_func* error_func)
{
    aug_input* input = (aug_input*)AUG_ALLOC(sizeof(aug_input));
    input->error_func = error_func;
    input->file = NULL;
    input->str = code;
    input->valid = true;
    input->filename = aug_string_create("stdin");
    input->pos_buffer_index = 0;
    input->track_pos = 0;

    aug_pos* pos = aug_input_pos(input);
    pos->col = 0;
    pos->line = 0;
    pos->c = -1;
    pos->filepos = pos->linepos = 0;

    return input;
}

void aug_input_close(aug_input* input)
{
    if(input == NULL) 
        return;

    input->filename = aug_string_decref(input->filename);
    if(input->file != NULL)
        fclose(input->file);

    AUG_FREE(input);
}

static inline void aug_input_start_tracking(aug_input* input)
{
    assert(input != NULL);
    aug_pos* pos = aug_input_pos(input);
    input->track_pos = pos->filepos;
}

static inline aug_string* aug_input_end_tracking(aug_input* input)
{
    assert(input != NULL);
    aug_pos* pos = aug_input_pos(input);

    assert(pos->filepos != -1 && pos->filepos > input->track_pos);

    const int pos_end = pos->filepos;
    const size_t len = (size_t)(pos_end - input->track_pos);

    aug_string* string = aug_string_new(len + 1);
    string->length = len;
    string->buffer[len] = '\0';

    if(input->file != NULL)
    {        
        fseek(input->file, input->track_pos, SEEK_SET);
        size_t count = fread(string->buffer, sizeof(char), len, input->file);
        fseek(input->file, pos_end, SEEK_SET);
        if(count != len)
        {
            aug_log_error(input->error_func, "Failed to read %d bytes! %s", len, input->filename->buffer);
            fseek(input->file, pos_end, SEEK_END);
        }
    }
    else 
    {
        assert(len < INT_MAX);
        int i;
        for(i = 0; i < (int)len; ++i)
            string->buffer[i] = input->str[i + input->track_pos]; 
    }
    return string;
}

static inline void aug_log_input_error_hint(aug_input* input, const aug_pos* pos)
{
    assert(input != NULL);

    if(input->file == NULL)
        return;

    // save state
    int curr_pos = (int)ftell(input->file);

    // go to line
    fseek(input->file, (int)pos->linepos, SEEK_SET);

    // skip leading whitespace
    size_t ws_skipped = 0;
    int c = fgetc(input->file);
    while (isspace(c) && ++ws_skipped)
        c = fgetc(input->file);

    aug_log_error(input->error_func, "Error %s:(%d,%d) ", 
        input->filename->buffer, pos->line + 1, pos->col + 1);

    // Draw line
    char buffer[4096];
    size_t n = 0;
    while (c != EOF && c != '\n' && n < (sizeof(buffer) - 1))
    {
        buffer[n++] = (char)c;
        c = fgetc(input->file);
    }
    buffer[n] = '\0';

    aug_log_error(input->error_func, "%s", buffer);

    // Draw arrow to the error if within buffer
    size_t tok_col = pos->col - ws_skipped;
    if (tok_col != 0 && tok_col < n - 1)
    {
        size_t i;
        for (i = 0; i < tok_col; ++i)
            buffer[i] = ' ';
        buffer[tok_col] = '^';
        buffer[tok_col + 1] = '\0';
        aug_log_error(input->error_func, "%s", buffer);
    }

    // restore state
    fseek(input->file, curr_pos, SEEK_SET);
}

void aug_log_input_error(aug_input* input, const char* format, ...)
{
    aug_log_input_error_hint(input, aug_input_get_pos(input, -1));

    va_list args;
    va_start(args, format);
    aug_log_error_internal(input->error_func, format, args);
    va_end(args);
}

void aug_log_input_error_at(aug_input* input, const aug_pos* pos, const char* format, ...)
{
    aug_log_input_error_hint(input, pos);

    va_list args;
    va_start(args, format);
    aug_log_error_internal(input->error_func, format, args);
    va_end(args);
}

// TOKENS ========================================   TOKENS   ================================================== TOKENS // 

// Static token details
typedef struct aug_token_detail
{
    const char* label;    // the string representation, used for visualization and debugging
    char prec;            // if the token is an operator, this is the precedence (higher values take precendece)
    int  argc;            // if the token is an operator, this is the number of arguments
    bool capture;         // if non-zero, the token will contain the source string value (integer and string literals)
    const char* keyword;  // if non-null, the token must match the provided keyword
} aug_token_detail;

#define AUG_TOKEN_LIST                             \
    /* State */                                    \
    AUG_TOKEN(INVALID,        0, 0, 0, NULL)       \
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
    AUG_TOKEN(ADD,            6, 2, 0, NULL)       \
    AUG_TOKEN(SUB,            6, 2, 0, NULL)       \
    AUG_TOKEN(MUL,            7, 2, 0, NULL)       \
    AUG_TOKEN(DIV,            7, 2, 0, NULL)       \
    AUG_TOKEN(POW,            7, 2, 0, NULL)       \
    AUG_TOKEN(MOD,            7, 2, 0, NULL)       \
    /* Operators - Boolean */                      \
    AUG_TOKEN(AND,            3, 2, 0, "and")      \
    AUG_TOKEN(OR,             3, 2, 0, "or")       \
    AUG_TOKEN(NOT,            8, 1, 0, NULL)       \
    AUG_TOKEN(NOT_EQ,         4, 2, 0, NULL)       \
    AUG_TOKEN(APPROX_EQ,      4, 2, 0, NULL)       \
    AUG_TOKEN(EQ,             4, 2, 0, NULL)       \
    AUG_TOKEN(LT,             5, 2, 0, NULL)       \
    AUG_TOKEN(GT,             5, 2, 0, NULL)       \
    AUG_TOKEN(LT_EQ,          5, 2, 0, NULL)       \
    AUG_TOKEN(GT_EQ,          5, 2, 0, NULL)       \
    /* Operators - Assignment */                   \
    AUG_TOKEN(ASSIGN,         1, 2, 0, NULL)       \
    AUG_TOKEN(ADD_ASSIGN,     1, 2, 0, NULL)       \
    AUG_TOKEN(SUB_ASSIGN,     1, 2, 0, NULL)       \
    AUG_TOKEN(MUL_ASSIGN,     1, 2, 0, NULL)       \
    AUG_TOKEN(DIV_ASSIGN,     1, 2, 0, NULL)       \
    AUG_TOKEN(MOD_ASSIGN,     1, 2, 0, NULL)       \
    AUG_TOKEN(POW_ASSIGN,     1, 2, 0, NULL)       \
    /* Literals */                                 \
    AUG_TOKEN(INT,            0, 0, 1, NULL)       \
    AUG_TOKEN(HEX,            0, 0, 1, NULL)       \
    AUG_TOKEN(BINARY,         0, 0, 1, NULL)       \
    AUG_TOKEN(FLOAT,          0, 0, 1, NULL)       \
    AUG_TOKEN(CHAR,           0, 0, 1, NULL)       \
    AUG_TOKEN(STRING,         0, 0, 1, NULL)       \
    /* Variable/Symbol */                          \
    AUG_TOKEN(NAME,           0, 0, 1, NULL)       \
    /* Keywords */                                 \
    AUG_TOKEN(IF,             0, 0, 0, "if")       \
    AUG_TOKEN(ELSE,           0, 0, 0, "else")     \
    AUG_TOKEN(IN,             0, 0, 0, "in")       \
    AUG_TOKEN(FOR,            0, 0, 0, "for")      \
    AUG_TOKEN(WHILE,          0, 0, 0, "while")    \
    AUG_TOKEN(VAR,            0, 0, 0, "var")      \
    AUG_TOKEN(FUNC,           0, 0, 0, "func")     \
    AUG_TOKEN(RETURN,         0, 0, 0, "return")   \
    AUG_TOKEN(BREAK,          0, 0, 0, "break")    \
    AUG_TOKEN(CONTINUE,       0, 0, 0, "continue") \
    AUG_TOKEN(TRUE,           0, 0, 0, "true")     \
    AUG_TOKEN(FALSE,          0, 0, 0, "false")    \
    AUG_TOKEN(NONE,           0, 0, 0, "none")     \
    AUG_TOKEN(IMPORT,         0, 0, 0, "import")   \
    AUG_TOKEN(EXPORT,         0, 0, 0, "export")

// Token identifier. 
typedef enum aug_token_id
{
#define AUG_TOKEN(type, ...) AUG_TOKEN_##type,
    AUG_TOKEN_LIST
#undef AUG_TOKEN
    AUG_TOKEN_COUNT
} aug_token_id;

// All token type info. Types map from type to type info
aug_token_detail aug_token_details[(int)AUG_TOKEN_COUNT] = 
{
#define AUG_TOKEN(type, ...) { #type, __VA_ARGS__},
    AUG_TOKEN_LIST
#undef AUG_TOKEN
};

#undef AUG_TOKEN_LIST

// Token instance
typedef struct aug_token
{
    aug_token_id id;
    const aug_token_detail* detail; 
    aug_string* data;
    aug_pos pos;

} aug_token;

// LEXER ==========================================   LEXER   =================================================== LEXER // 
#define AUG_LEXER_TOKEN_BUFFER_SIZE 4

// Lexer state
typedef struct aug_lexer
{
    aug_input* input;

    aug_token tokens[AUG_LEXER_TOKEN_BUFFER_SIZE];
    int at_index;
    int tokenize_index; //next token index to be tokenized
    char comment_symbol;
} aug_lexer;

aug_token aug_token_new()
{
    aug_token token;
    token.id = AUG_TOKEN_INVALID;
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

bool aug_token_is_assign_op(aug_token token)
{
    switch(token.id)
    {

    case AUG_TOKEN_ASSIGN: 
    case AUG_TOKEN_ADD_ASSIGN: 
    case AUG_TOKEN_SUB_ASSIGN: 
    case AUG_TOKEN_MUL_ASSIGN: 
    case AUG_TOKEN_DIV_ASSIGN: 
    case AUG_TOKEN_MOD_ASSIGN: 
    case AUG_TOKEN_POW_ASSIGN: 
        return true;
    default: break;
    }
    return false;    
}

aug_lexer* aug_lexer_new(aug_input* input)
{
    aug_lexer* lexer = (aug_lexer*)AUG_ALLOC(sizeof(aug_lexer));
    lexer->input = input;
    lexer->comment_symbol = '#';
    lexer->at_index = -1;
    lexer->tokenize_index = 0;
    for (int i = 0; i < AUG_LEXER_TOKEN_BUFFER_SIZE; ++i)
        lexer->tokens[i] = aug_token_new();

    return lexer;
}

void aug_lexer_delete(aug_lexer* lexer)
{
    for(int i = 0; i < AUG_LEXER_TOKEN_BUFFER_SIZE; ++i)
        aug_token_reset(&lexer->tokens[i]);
    AUG_FREE(lexer);
}

aug_token aug_lexer_curr(aug_lexer* lexer)
{
    return lexer->tokens[lexer->at_index];
}

aug_token aug_lexer_next(aug_lexer* lexer)
{
    return lexer->tokens[(lexer->at_index + 1) % AUG_LEXER_TOKEN_BUFFER_SIZE];
}

aug_token aug_lexer_last_tokenized(aug_lexer* lexer)
{
    return lexer->tokens[lexer->tokenize_index > 0 ? lexer->tokenize_index - 1 : AUG_LEXER_TOKEN_BUFFER_SIZE - 1];
}

bool aug_lexer_tokenize_char(aug_lexer* lexer, aug_token* token)
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
        aug_log_input_error(lexer->input, "char literal missing closing \"");
        return false;
    }
    return true;
}

bool aug_lexer_tokenize_string(aug_lexer* lexer, aug_token* token)
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
            aug_log_input_error(lexer->input, "string literal missing closing \"");
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
                aug_log_input_error(lexer->input, "invalid escape character \\%c", c);
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

bool aug_lexer_tokenize_symbol(aug_lexer* lexer, aug_token* token)
{
    aug_token_id id = AUG_TOKEN_INVALID;

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

    if(id == AUG_TOKEN_INVALID)
    {
        aug_input_unget(lexer->input);
        return false;
    }

    token->id = id;
    return true;
}

bool aug_lexer_tokenize_name(aug_lexer* lexer, aug_token* token)
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

    aug_token_id id = AUG_TOKEN_INVALID;

    if(c == '0' && aug_input_peek(lexer->input) == 'x')
    {
        id = AUG_TOKEN_HEX;

        c = aug_input_get(lexer->input); //eat 'x'
        c = aug_input_get(lexer->input);

        while(isalnum(c))
        {
            if(!isdigit(c) && !((c >='a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                id = AUG_TOKEN_INVALID;

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
                id = AUG_TOKEN_INVALID;

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
                    id = AUG_TOKEN_INVALID;
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

    if(id == AUG_TOKEN_INVALID)
    {
        aug_log_input_error(lexer->input, "invalid numeric format %s", token->data->buffer);
        token->data = aug_string_decref(token->data);
        return false;
    }

    return true;
}

aug_token aug_lexer_tokenize(aug_lexer* lexer)
{
    aug_token token = aug_token_new();

    // if file is not open, or already at then end. return invalid token
    if(lexer->input == NULL)
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
        switch(aug_lexer_last_tokenized(lexer).id)
        {
            case AUG_TOKEN_NAME:
            case AUG_TOKEN_BINARY:
            case AUG_TOKEN_HEX:
            case AUG_TOKEN_FLOAT:
            case AUG_TOKEN_INT:
            case AUG_TOKEN_RPAREN:
            case AUG_TOKEN_RBRACE:
            case AUG_TOKEN_RBRACKET:
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
        aug_log_input_error(lexer->input, "invalid character %c", c);
        break;
    }

    token.detail = &aug_token_details[(int)token.id];
    return token;
}

bool aug_lexer_move(aug_lexer* lexer)
{
    if(lexer == NULL)
        return false;

    if (lexer->at_index == -1)
    {
        lexer->at_index = 0;
        lexer->tokenize_index = 1;

        lexer->tokens[lexer->at_index] = aug_lexer_tokenize(lexer);
        lexer->tokens[lexer->tokenize_index] = aug_lexer_tokenize(lexer);
        return aug_lexer_curr(lexer).id != AUG_TOKEN_INVALID;
    }

    lexer->at_index = (lexer->at_index + 1) % AUG_LEXER_TOKEN_BUFFER_SIZE;

    // to avoid the circular buffer from overstepping. next index time to catch up
    if (lexer->at_index > lexer->tokenize_index)
    {
        return aug_lexer_curr(lexer).id != AUG_TOKEN_INVALID;
    }

    lexer->tokenize_index = (lexer->tokenize_index + 1) % AUG_LEXER_TOKEN_BUFFER_SIZE;
    aug_token_reset(&lexer->tokens[lexer->tokenize_index]);
    lexer->tokens[lexer->tokenize_index] = aug_lexer_tokenize(lexer);
    return aug_lexer_curr(lexer).id != AUG_TOKEN_INVALID;
}

bool aug_lexer_undo(aug_lexer* lexer)
{
    if (lexer == NULL)
        return false;

    lexer->at_index = lexer->at_index > 0 ? lexer->at_index - 1 : AUG_LEXER_TOKEN_BUFFER_SIZE - 1;
    assert(lexer->at_index != lexer->tokenize_index);
    return aug_lexer_curr(lexer).id != AUG_TOKEN_INVALID;
}

// AST ================================================   AST   =================================================== AST // 

typedef uint8_t aug_ast_type;

#define AUG_AST_TYPE_LIST            \
    AUG_AST_TYPE(ROOT)               \
    AUG_AST_TYPE(BLOCK)              \
    AUG_AST_TYPE(STMT_EXPR)          \
    AUG_AST_TYPE(STMT_DEFINE_VAR)    \
    AUG_AST_TYPE(STMT_IF)            \
    AUG_AST_TYPE(STMT_IF_ELSE)       \
    AUG_AST_TYPE(STMT_WHILE)         \
    AUG_AST_TYPE(STMT_FOR)           \
    AUG_AST_TYPE(LITERAL)            \
    AUG_AST_TYPE(VARIABLE)           \
    AUG_AST_TYPE(ARRAY)              \
    AUG_AST_TYPE(MAP)                \
    AUG_AST_TYPE(MAP_PAIR)           \
    AUG_AST_TYPE(ELEMENT)            \
    AUG_AST_TYPE(FIELD)              \
    AUG_AST_TYPE(RANGE)              \
    AUG_AST_TYPE(UNARY_OP)           \
    AUG_AST_TYPE(BINARY_OP)          \
    AUG_AST_TYPE(DISCARD)            \
    AUG_AST_TYPE(FUNC_CALL)          \
    AUG_AST_TYPE(FUNC_CALL_UNNAMED)  \
    AUG_AST_TYPE(STMT_DEFINE_FUNC)   \
    AUG_AST_TYPE(PARAM_LIST)         \
    AUG_AST_TYPE(PARAM)              \
    AUG_AST_TYPE(RETURN)             \
    AUG_AST_TYPE(BREAK)              \
    AUG_AST_TYPE(CONTINUE)           \
    AUG_AST_TYPE(IMPORT_SCRIPT)      \
    AUG_AST_TYPE(IMPORT_LIB)

enum aug_ast_types
{ 
#define AUG_AST_TYPE(type) AUG_AST_##type,
    AUG_AST_TYPE_LIST
#undef AUG_AST_TYPE
    AUG_AST_COUNT
};

#if AUG_DEBUG

const char* aug_ast_labels[] =
{
#define AUG_AST_TYPE(opcode) #opcode,
    AUG_AST_TYPE_LIST
#undef AUG_AST_TYPE
}; 

const char* aug_ast_label(uint8_t type)
{
    return aug_ast_labels[(int)type];
}

#endif //AUG_DEBUG
#undef AUG_AST_TYPE_LIST

typedef struct aug_ast
{
    aug_ast_type type;
    aug_token token;
    struct aug_ast** children;
    int children_size;
    int children_capacity;
} aug_ast;

aug_ast* aug_ast_new(aug_ast_type type, aug_token token)
{
    aug_ast* node = (aug_ast*)AUG_ALLOC(sizeof(aug_ast));
    node->type = type;
    node->token = token;
    node->children = NULL;
    node->children_size = 0;
    node->children_capacity = 0;
    return node;
}

static inline void aug_ast_delete(aug_ast* node)
{
    if(node == NULL)
        return;
    if(node->children)
    {
        int i;
        for(i = 0; i < node->children_size; ++i)
            aug_ast_delete(node->children[i]);
        AUG_FREE(node->children);
    }
    if(node->token.data)
    aug_token_reset(&node->token);
    AUG_FREE(node);
}

static inline void aug_ast_resize(aug_ast* node, int size)
{    
    node->children_capacity = size == 0 ? 1 : size;
    node->children = (aug_ast**)AUG_REALLOC(node->children, sizeof(aug_ast*) * (size_t)node->children_capacity);
    node->children_size = size;
}

static inline void aug_ast_add(aug_ast* node, aug_ast* child)
{
    if(node->children_size + 1 >= node->children_capacity)
    {
        node->children_capacity = node->children_capacity == 0 ? 1 : node->children_capacity * 2;
        node->children = (aug_ast**)AUG_REALLOC(node->children, sizeof(aug_ast*) * (size_t)node->children_capacity);
    }
    node->children[node->children_size++] = child;
}

aug_token aug_ast_token_empty(aug_lexer* lexer)
{
    aug_token token = aug_token_new();
    token.pos = aug_lexer_curr(lexer).pos; //retains token position
    return token;
}

// PARSER =============================================   PARSER   ============================================= PARSER // 

aug_ast* aug_parse_value(aug_lexer* lexer);
aug_ast* aug_parse_block(aug_lexer* lexer);

static inline bool aug_parse_expr_pop(aug_lexer* lexer, aug_container* op_stack, aug_container* expr_stack)
{
    aug_token next_op = aug_container_pop_type(aug_token, op_stack);

    const int op_argc = next_op.detail->argc;
    assert(op_argc == 1 || op_argc == 2); // Only supported operator types

    if(expr_stack->length < (size_t)op_argc)
    {
        aug_log_input_error(lexer->input, "Invalid number of arguments to operator %s. Expected %ld, received %d", next_op.detail->label, op_argc, expr_stack->length);
        while(expr_stack->length > 0)
        {
            aug_ast* expr = aug_container_pop_type(aug_ast*, expr_stack);
            aug_ast_delete(expr);
        }
        return false;
    }

    // Push binary op onto stack
    aug_ast_type id = (op_argc == 2) ? AUG_AST_BINARY_OP : AUG_AST_UNARY_OP;
    aug_ast* binaryop = aug_ast_new(id, next_op);
    aug_ast_resize(binaryop, op_argc);
    
    int i;
    for(i = 0; i < op_argc; ++i)
    {
        // Not, length is checked above, guaranteed to be valid pointer
        aug_ast* expr = aug_container_pop_type(aug_ast*, expr_stack);
        binaryop->children[(op_argc-1) - i] = expr; // add in reverse
    }

    aug_container_push_type(aug_ast*, expr_stack, binaryop);
    return true;
}

static inline void aug_parse_expr_stack_cleanup(aug_container* op_stack, aug_container* expr_stack)
{
    while(expr_stack->length > 0)
    {
        aug_ast* expr = aug_container_pop_type(aug_ast*, expr_stack);
        aug_ast_delete(expr);
    }
    aug_container_decref(expr_stack);
    aug_container_decref(op_stack);
}

aug_ast* aug_parse_expr(aug_lexer* lexer)
{
    // Shunting yard algorithm
    aug_container* op_stack = aug_container_new_type(aug_token, 1); 
    aug_container* expr_stack = aug_container_new_type(aug_ast*, 1); 

    bool expect_value = true;
    while(aug_lexer_curr(lexer).detail->prec > 0 || expect_value)
    {
        aug_token op = aug_lexer_curr(lexer);
        if(op.detail->prec > 0)
        {
            expect_value = true;

            // left associate by default (for right, <= becomes <)
            while(op_stack->length)
            {
                aug_token next_op = aug_container_back_type(aug_token, op_stack);
                if(next_op.detail->prec < op.detail->prec)
                    break;
                if(!aug_parse_expr_pop(lexer, op_stack, expr_stack))
                    return NULL;
            }
            aug_container_push_type(aug_token, op_stack, op);
            aug_lexer_move(lexer);
        }
        else 
        {            
            expect_value = false;
            aug_ast* value = aug_parse_value(lexer);
            if(value == NULL)
                break;

            aug_container_push_type(aug_ast*, expr_stack, value);
        }
    }

    // Not an expression
    if(op_stack->length == 0 && expr_stack->length == 0)
    {
        aug_container_decref(op_stack);
        aug_container_decref(expr_stack);
        return NULL;
    }

    while(op_stack->length)
    {
        if(!aug_parse_expr_pop(lexer, op_stack, expr_stack))
        {
            aug_parse_expr_stack_cleanup(op_stack, expr_stack);
            return NULL;
        }
    }

    // Not a valid expression. Either malformed or missing semicolon 
    if(expr_stack->length == 0 || expr_stack->length > 1)
    {
        aug_parse_expr_stack_cleanup(op_stack, expr_stack);
        aug_log_input_error(lexer->input, "Invalid expression syntax");
        return NULL;
    }

    aug_ast* expr = aug_container_back_type(aug_ast*, expr_stack);
    aug_container_decref(op_stack);
    aug_container_decref(expr_stack);
    return expr;
}

aug_ast* aug_parse_funccall(aug_lexer* lexer, aug_ast* value)
{
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_LPAREN)
        return NULL;

    aug_lexer_move(lexer); // eat LPAREN

    // If value is a variable, then have this be a standard function call. 
    // Otherwise, will be an unnamed call from stack. This allows func calls from elements, and returns values
    aug_ast* funccall = NULL;
    if(value->type == AUG_AST_VARIABLE)
    {
        // pass token to func call and reset variable token. converts variable into func call
        funccall = aug_ast_new(AUG_AST_FUNC_CALL, value->token);
        value->token = aug_token_new();
        aug_ast_delete(value);
    }
    else
    {
        funccall = aug_ast_new(AUG_AST_FUNC_CALL_UNNAMED, aug_ast_token_empty(lexer));
        aug_ast_add(funccall, value); //NOTE: first child is the function value
    }
    
    aug_ast* expr = aug_parse_expr(lexer);
    if(expr != NULL)
    {
        aug_ast_add(funccall, expr);

        while(expr != NULL && aug_lexer_curr(lexer).id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            expr = aug_parse_expr(lexer);
            if(expr != NULL)
                aug_ast_add(funccall, expr);
        }
    }

    if(aug_lexer_curr(lexer).id != AUG_TOKEN_RPAREN)
    {
        aug_ast_delete(funccall);
        aug_log_input_error(lexer->input, "Function call missing closing parentheses");
        return NULL;
    }

    aug_lexer_move(lexer); // eat RPAREN
    return funccall;
}

aug_ast* aug_parse_array(aug_lexer* lexer)
{
    // [ expr, ... ]
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_LBRACKET)
        return NULL;

    aug_lexer_move(lexer); // eat LBRACKET


    aug_ast* array = aug_ast_new(AUG_AST_ARRAY, aug_ast_token_empty(lexer));

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr != NULL)
    {
        aug_ast_add(array, expr);

        while(expr != NULL && aug_lexer_curr(lexer).id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            if (aug_lexer_curr(lexer).id == AUG_TOKEN_RBRACKET)
                break;

            expr = aug_parse_expr(lexer);
            if(expr != NULL)
                aug_ast_add(array, expr);
        }
    }

    if(aug_lexer_curr(lexer).id != AUG_TOKEN_RBRACKET)
    {
        aug_ast_delete(array);
        aug_log_input_error(lexer->input, "Array missing closing bracket");
        return NULL;
    }

    aug_lexer_move(lexer); // eat RBRACKET
    return array;
}

bool aug_parse_is_key(aug_token token)
{
    switch (token.id)
    {
    case AUG_TOKEN_INT:
    case AUG_TOKEN_HEX:
    case AUG_TOKEN_BINARY:
    case AUG_TOKEN_FLOAT:
    case AUG_TOKEN_STRING:
    case AUG_TOKEN_CHAR:
    case AUG_TOKEN_TRUE:
    case AUG_TOKEN_FALSE:
        return true;
    default: break;
    }
    return false;
}

aug_ast* aug_parse_key(aug_lexer* lexer)
{
    if(aug_parse_is_key(aug_lexer_curr(lexer)))
    {
        aug_token token = aug_token_copy(aug_lexer_curr(lexer));
        aug_ast* value = aug_ast_new(AUG_AST_LITERAL, token);
        aug_lexer_move(lexer);
        return value;
    }

    aug_log_input_error(lexer->input, "Invalid key type. Expected literal value");
    return NULL;
}

aug_ast* aug_parse_map_pair(aug_lexer* lexer)
{
    // key : value
    aug_ast* key = aug_parse_key(lexer);
    if (key == NULL)
        return NULL;

    if (aug_lexer_curr(lexer).id != AUG_TOKEN_COLON)
    {
        aug_ast_delete(key);
        aug_log_input_error(lexer->input, "Key value expected : after key");
        return NULL;
    }
    aug_lexer_move(lexer); // eat :
    
    aug_ast* expr = aug_parse_expr(lexer);
    if (expr == NULL)
    {
        aug_ast_delete(key);
        aug_log_input_error(lexer->input, "Key value expected value after :");
        return NULL;
    }

    aug_ast* keyvalue = aug_ast_new(AUG_AST_MAP_PAIR, aug_ast_token_empty(lexer));
    aug_ast_add(keyvalue, key);
    aug_ast_add(keyvalue, expr);
    return keyvalue;
}

aug_ast* aug_parse_map(aug_lexer* lexer)
{
    // { key : value } NOTE: key only int/string 
    if (aug_lexer_curr(lexer).id != AUG_TOKEN_LBRACE)
        return NULL;

    aug_lexer_move(lexer); // eat LBRACE

    if (aug_lexer_curr(lexer).id == AUG_TOKEN_RBRACE)
    {
        aug_lexer_move(lexer); // eat RBRACE
        return aug_ast_new(AUG_AST_MAP, aug_ast_token_empty(lexer));
    }

    // must have key : comma. Otherwise, not a map literal
    if (!aug_parse_is_key(aug_lexer_curr(lexer)) || aug_lexer_next(lexer).id != AUG_TOKEN_COLON)
    {
        aug_lexer_undo(lexer);
        return NULL;
    }

    aug_ast* map = aug_ast_new(AUG_AST_MAP, aug_ast_token_empty(lexer));
    aug_ast* pair = aug_parse_map_pair(lexer);
    if (pair != NULL )
    {
        aug_ast_add(map, pair);

        while (pair != NULL && aug_lexer_curr(lexer).id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            if (aug_lexer_curr(lexer).id == AUG_TOKEN_RBRACE)
                break;

            pair = aug_parse_map_pair(lexer);
            if (pair != NULL)
                aug_ast_add(map, pair);
        }
    }

    if (aug_lexer_curr(lexer).id != AUG_TOKEN_RBRACE)
    {
        aug_ast_delete(map);
        aug_log_input_error(lexer->input, "Map missing closing }");
        return NULL;
    }

    aug_lexer_move(lexer); // eat RBRACE
    return map;
}

aug_ast* aug_parse_element(aug_lexer* lexer, aug_ast* container)
{
    // [ expr ]
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_LBRACKET)
        return NULL;

    aug_lexer_move(lexer); // eat LBRACKET

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        aug_log_input_error(lexer->input, "Index operator missing index value");
        return NULL;
    }

    aug_ast* element = aug_ast_new(AUG_AST_ELEMENT, aug_ast_token_empty(lexer));

    aug_ast_resize(element, 2);
    element->children[0] = expr;
    element->children[1] = container;

    if(aug_lexer_curr(lexer).id != AUG_TOKEN_RBRACKET)
    {
        aug_ast_delete(element);
        aug_log_input_error(lexer->input, "Index operator missing closing ]");
        return NULL;
    }

    aug_lexer_move(lexer); // eat RBRACKET
    
    return element;
}

aug_ast* aug_parse_field(aug_lexer* lexer, aug_ast* container)
{
    // . NAME
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_DOT)
        return NULL;

    aug_lexer_move(lexer); // eat DOT

    if(aug_lexer_curr(lexer).id != AUG_TOKEN_NAME)
    {
        aug_log_input_error(lexer->input, "Dot operator missing field name");
        return NULL;
    }

    aug_ast* name = aug_ast_new(AUG_AST_VARIABLE, aug_ast_token_empty(lexer));

    aug_lexer_move(lexer); // eat NAME

    aug_ast* field = aug_ast_new(AUG_AST_FIELD, aug_ast_token_empty(lexer));

    aug_ast_resize(field, 2);
    field->children[0] = name;
    field->children[1] = container;

    return field;
}


aug_ast* aug_parse_value(aug_lexer* lexer)
{
    aug_ast* value = NULL;
    switch (aug_lexer_curr(lexer).id)
    {
    case AUG_TOKEN_INT:
    case AUG_TOKEN_HEX:
    case AUG_TOKEN_BINARY:
    case AUG_TOKEN_FLOAT:
    case AUG_TOKEN_STRING:
    case AUG_TOKEN_CHAR:
    case AUG_TOKEN_TRUE:
    case AUG_TOKEN_FALSE:
    case AUG_TOKEN_NONE:
    {
        aug_token token = aug_token_copy(aug_lexer_curr(lexer));
        value = aug_ast_new(AUG_AST_LITERAL, token);

        aug_lexer_move(lexer);
        break;
    }
    case AUG_TOKEN_NAME:
    {
        // consume token. return variable node
        aug_token token = aug_token_copy(aug_lexer_curr(lexer));
        value = aug_ast_new(AUG_AST_VARIABLE, token);

        aug_lexer_move(lexer); // eat name
        break;
    }
    case AUG_TOKEN_LBRACKET:
    {
        value = aug_parse_array(lexer);
        break;
    }
    case AUG_TOKEN_LBRACE:
    {
        value = aug_parse_map(lexer);
        break;
    }
    case AUG_TOKEN_LPAREN:
    {
        aug_lexer_move(lexer); // eat LPAREN
        value = aug_parse_expr(lexer);
        if (aug_lexer_curr(lexer).id == AUG_TOKEN_RPAREN)
        {
            aug_lexer_move(lexer); // eat RPAREN
        }
        else
        {
            aug_log_input_error(lexer->input, "Expression missing closing parentheses");
            aug_ast_delete(value);
            value = NULL;
        }
        break;
    }
    default: break;
    }

    // Parse the qualifying operations, func call (), element [], and dot .
    while(value)
    {
        // try parse funccall
        aug_ast* funccall = aug_parse_funccall(lexer, value);
        if (funccall != NULL)
        {
            value = funccall;
            continue;
        }

        // try parse element of variable
        aug_ast* element = aug_parse_element(lexer, value);
        if (element != NULL)
        {
            value = element;
            continue;
        }

        // try parse field of variable
        aug_ast* field = aug_parse_field(lexer, value);
        if (field != NULL)
        {
            value = field;
            continue;
        }

        break;
    }

    return value;
}

bool aug_parse_stmt_semicolon(aug_lexer* lexer)
{
    if(aug_lexer_curr(lexer).id == AUG_TOKEN_SEMICOLON)
    {
        aug_lexer_move(lexer); // eat SEMICOLON
        return true;
    }
#if AUG_ALLOW_NO_SEMICOLON
    return true;
#else
    return false;
#endif //AUG_ALLOW_NO_SEMICOLON
}

aug_ast* aug_parse_stmt_expr(aug_lexer* lexer)
{
    aug_ast* expr = aug_parse_expr(lexer);
    if(expr == NULL)
        return NULL;
    
    aug_ast* stmt_expr = aug_ast_new(AUG_AST_STMT_EXPR, aug_ast_token_empty(lexer));
    aug_ast_add(stmt_expr, expr);

    // If expression is a non-assignment, discard from the stack
    if(expr->type != AUG_AST_BINARY_OP || !aug_token_is_assign_op(expr->token))
        aug_ast_add(stmt_expr, aug_ast_new(AUG_AST_DISCARD, aug_ast_token_empty(lexer)));

    if(!aug_parse_stmt_semicolon(lexer))
    {
        aug_ast_delete(expr);
        aug_log_input_error(lexer->input,  "Missing semicolon at end of expression");
        return NULL;
    }
    return stmt_expr;
}

aug_ast* aug_parse_stmt_define_var(aug_lexer* lexer)
{
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_VAR)
        return NULL;

    aug_lexer_move(lexer); // eat VAR

    if(aug_lexer_curr(lexer).id != AUG_TOKEN_NAME)
    {
        aug_log_input_error(lexer->input,  "Variable assignment expected name");
        return NULL;
    }

    aug_token name_token = aug_token_copy(aug_lexer_curr(lexer));
    aug_lexer_move(lexer); // eat NAME

    if(aug_lexer_curr(lexer).id != AUG_TOKEN_ASSIGN)
    {
        if(aug_parse_stmt_semicolon(lexer))
        {
            aug_ast* stmt_define = aug_ast_new(AUG_AST_STMT_DEFINE_VAR, name_token);
            return stmt_define;
        }

        aug_token_reset(&name_token);
        aug_log_input_error(lexer->input,  "Variable assignment expected \"=\" or ;");
        return NULL;
    }
    aug_lexer_move(lexer); // eat ASSIGN

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        aug_token_reset(&name_token);
        aug_log_input_error(lexer->input,  "Variable assignment expected expression after \"=\"");
        return NULL;
    }

    if(!aug_parse_stmt_semicolon(lexer))
    {
        aug_token_reset(&name_token);
        aug_ast_delete(expr);
        aug_log_input_error(lexer->input,  "Variable assignment missing semicolon at end of expression");
        return NULL;
    }

    aug_ast* stmt_define = aug_ast_new(AUG_AST_STMT_DEFINE_VAR, name_token);
    aug_ast_add(stmt_define, expr);
    return stmt_define;
}

aug_ast* aug_parse_stmt_if(aug_lexer* lexer);

aug_ast* aug_parse_stmt_if_else(aug_lexer* lexer, aug_ast* expr, aug_ast* block)
{
    aug_lexer_move(lexer); // eat ELSE

    aug_ast* if_else_stmt = aug_ast_new(AUG_AST_STMT_IF_ELSE, aug_ast_token_empty(lexer));
    aug_ast_resize(if_else_stmt, 3);
    if_else_stmt->children[0] = expr;
    if_else_stmt->children[1] = block;

    // Handling else if becomes else { if ... }
    if(aug_lexer_curr(lexer).id == AUG_TOKEN_IF)
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
            aug_log_input_error(lexer->input,  "If Else statement missing block");
            return NULL;
        }
        if_else_stmt->children[2] = else_block;
    }

    return if_else_stmt;
}

aug_ast* aug_parse_stmt_if(aug_lexer* lexer)
{
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_IF)
        return NULL;

    aug_lexer_move(lexer); // eat IF

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        aug_log_input_error(lexer->input,  "If statement missing expression");
        return NULL;      
    }

    aug_ast* block = aug_parse_block(lexer);
    if(block == NULL)
    {
        aug_ast_delete(expr);
        aug_log_input_error(lexer->input,  "If statement missing block");
        return NULL;
    }

    // Parse else 
    if(aug_lexer_curr(lexer).id == AUG_TOKEN_ELSE)
        return aug_parse_stmt_if_else(lexer, expr, block);

    aug_ast* if_stmt = aug_ast_new(AUG_AST_STMT_IF, aug_ast_token_empty(lexer));
    aug_ast_resize(if_stmt, 2);
    if_stmt->children[0] = expr;
    if_stmt->children[1] = block;
    return if_stmt;
}

aug_ast* aug_parse_stmt_while(aug_lexer* lexer)
{
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_WHILE)
        return NULL;

    aug_lexer_move(lexer); // eat WHILE

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr == NULL)
    {
        aug_log_input_error(lexer->input,  "While loop missing expression");
        return NULL;
    }

    aug_ast* block = aug_parse_block(lexer);
    if(block == NULL)
    {
        aug_ast_delete(expr);
        aug_log_input_error(lexer->input,  "While loop missing block");
        return NULL;
    }

    aug_ast* while_stmt = aug_ast_new(AUG_AST_STMT_WHILE, aug_ast_token_empty(lexer));
    aug_ast_resize(while_stmt, 2);
    while_stmt->children[0] = expr;
    while_stmt->children[1] = block;
    return while_stmt;
}

aug_ast* aug_parse_for_range(aug_lexer* lexer)
{
    aug_ast* from_expr = aug_parse_expr(lexer);
    if(from_expr == NULL)
        return NULL;

    // if not a range object, return just iterable expr
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_COLON)
        return from_expr;

    aug_lexer_move(lexer); // eat COLON

    aug_ast* to_expr = aug_parse_expr(lexer);
    if(to_expr == NULL)
    {
        aug_ast_delete(from_expr);
        aug_log_input_error(lexer->input, "Range missing from from value");
        return NULL;
    }

    aug_ast* range = aug_ast_new(AUG_AST_RANGE, aug_ast_token_empty(lexer));

    aug_ast_resize(range, 2);
    range->children[0] = from_expr;
    range->children[1] = to_expr;
    return range;
}

aug_ast* aug_parse_stmt_for(aug_lexer* lexer)
{
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_FOR)
        return NULL;

    aug_lexer_move(lexer); // eat FOR

    if(aug_lexer_curr(lexer).id != AUG_TOKEN_NAME)
    {
        aug_log_input_error(lexer->input,  "For loop expected variable name");
        return NULL;
    }

    // consume token. return variable node
    aug_token token = aug_token_copy(aug_lexer_curr(lexer));
    aug_ast* var = aug_ast_new(AUG_AST_VARIABLE, token);
    aug_lexer_move(lexer); // eat NAME

    if(aug_lexer_curr(lexer).id != AUG_TOKEN_IN)
    {
        aug_ast_delete(var);
        aug_log_input_error(lexer->input,  "For loop expected in after variable name");
        return NULL;
    }
    aug_lexer_move(lexer); // eat IN

    aug_ast* expr = aug_parse_for_range(lexer);
    if(expr == NULL)
    {
        aug_ast_delete(var);
        aug_log_input_error(lexer->input,  "For loop missing expression");
        return NULL;
    }

    aug_ast* block = aug_parse_block(lexer);
    if(block == NULL)
    {
        aug_ast_delete(var);
        aug_ast_delete(expr);
        aug_log_input_error(lexer->input,  "For loop missing block");
        return NULL;
    }

    aug_ast* for_stmt = aug_ast_new(AUG_AST_STMT_FOR, aug_ast_token_empty(lexer));
    aug_ast_resize(for_stmt, 3);
    for_stmt->children[0] = var;
    for_stmt->children[1] = expr;
    for_stmt->children[2] = block;
    return for_stmt;
}

aug_ast* aug_parse_param_list(aug_lexer* lexer)
{
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_LPAREN)
    {
        aug_log_input_error(lexer->input,  "Missing opening parentheses in function parameter list");
        return NULL;
    }

    aug_lexer_move(lexer); // eat LPAREN

    aug_ast* param_list = aug_ast_new(AUG_AST_PARAM_LIST, aug_ast_token_empty(lexer));
    if(aug_lexer_curr(lexer).id == AUG_TOKEN_NAME)
    {
        aug_ast* param = aug_ast_new(AUG_AST_PARAM, aug_token_copy(aug_lexer_curr(lexer)));
        aug_ast_add(param_list, param);

        aug_lexer_move(lexer); // eat NAME

        while(aug_lexer_curr(lexer).id == AUG_TOKEN_COMMA)
        {
            aug_lexer_move(lexer); // eat COMMA

            if(aug_lexer_curr(lexer).id != AUG_TOKEN_NAME)
            {
                aug_log_input_error(lexer->input,  "Invalid function parameter. Expected parameter name");
                aug_ast_delete(param_list);
                return NULL;
            }

            param = aug_ast_new(AUG_AST_PARAM, aug_token_copy(aug_lexer_curr(lexer)));
            aug_ast_add(param_list, param);

            aug_lexer_move(lexer); // eat NAME
        }
    }

    if(aug_lexer_curr(lexer).id != AUG_TOKEN_RPAREN)
    {
        aug_log_input_error(lexer->input,  "Missing closing parentheses in function parameter list");
        aug_ast_delete(param_list);
        return NULL;
    }

    aug_lexer_move(lexer); // eat RPAREN

    return param_list;
}

aug_ast* aug_parse_stmt_func(aug_lexer* lexer)
{
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_FUNC)
        return NULL;

    aug_lexer_move(lexer); // eat FUNC

    if(aug_lexer_curr(lexer).id != AUG_TOKEN_NAME)
    {
        aug_log_input_error(lexer->input,  "Missing name in function definition");
        return NULL;
    }

    aug_token func_name_token = aug_token_copy(aug_lexer_curr(lexer));

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

    aug_ast* func_def = aug_ast_new(AUG_AST_STMT_DEFINE_FUNC, func_name_token);
    aug_ast_resize(func_def, 2);
    func_def->children[0] = param_list;
    func_def->children[1] = block;
    return func_def;
}

aug_ast* aug_parse_stmt_return(aug_lexer* lexer)
{
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_RETURN)
        return NULL;

    aug_lexer_move(lexer); // eat RETURN

    aug_ast* return_stmt = aug_ast_new(AUG_AST_RETURN, aug_ast_token_empty(lexer));

#if AUG_ALLOW_NO_SEMICOLON
    // Special condition for empty return and no semicolon. Prevent assignment operations from being the return expr. leave as none
    if(aug_lexer_curr(lexer).id == AUG_TOKEN_NAME)
    {
        if(aug_token_is_assign_op(aug_lexer_next(lexer)))
            return return_stmt;
    }
#endif

    aug_ast* expr = aug_parse_expr(lexer);
    if(expr != NULL)
        aug_ast_add(return_stmt, expr);

    if(!aug_parse_stmt_semicolon(lexer))
    {
        aug_ast_delete(return_stmt);
        aug_log_input_error(lexer->input,  "Missing semicolon at end of return statement");
        return NULL;
    }

    return return_stmt;
}

aug_ast* aug_parse_stmt_break(aug_lexer* lexer)
{
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_BREAK)
        return NULL;

    // copy over token to allow error details
    aug_ast* break_stmt = aug_ast_new(AUG_AST_BREAK, aug_token_copy(aug_lexer_curr(lexer)));
    aug_lexer_move(lexer); // eat BREAK

    if(!aug_parse_stmt_semicolon(lexer))
    {
        aug_ast_delete(break_stmt);
        aug_log_input_error(lexer->input,  "Missing semicolon at end of break statement");
        return NULL;
    }

    return break_stmt;
}

aug_ast* aug_parse_stmt_continue(aug_lexer* lexer)
{
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_CONTINUE)
        return NULL;

    // copy over token to allow error details
    aug_ast* continue_stmt = aug_ast_new(AUG_AST_CONTINUE, aug_token_copy(aug_lexer_curr(lexer)));
    aug_lexer_move(lexer); // eat CONTINUE

    if(!aug_parse_stmt_semicolon(lexer))
    {
        aug_ast_delete(continue_stmt);
        aug_log_input_error(lexer->input,  "Missing semicolon at end of continue statement");
        return NULL;
    }

    return continue_stmt;
}

aug_ast* aug_parse_stmt_import(aug_lexer* lexer)
{
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_IMPORT)
        return NULL;

    aug_lexer_move(lexer); // eat IMPORT

    aug_ast* import_stmt = NULL;

    aug_token token = aug_lexer_curr(lexer);
    switch(token.id)
    {
        case AUG_TOKEN_STRING:
            import_stmt = aug_ast_new(AUG_AST_IMPORT_SCRIPT, aug_token_copy(token));
            aug_lexer_move(lexer); // eat TOKEN 
            break;   
        case AUG_TOKEN_NAME:
            import_stmt = aug_ast_new(AUG_AST_IMPORT_LIB, aug_token_copy(token));
            aug_lexer_move(lexer); // eat TOKEN 
            break;
        default: 
            aug_ast_delete(import_stmt);
            aug_log_input_error(lexer->input,  "Use statement expected library name or script path");
            break;
    }

    if(import_stmt == NULL)
        return NULL;

    if(!aug_parse_stmt_semicolon(lexer))
    {
        aug_ast_delete(import_stmt);
        aug_log_input_error(lexer->input,  "Missing semicolon at end of use statement");
        return NULL;
    }

    return import_stmt;
}


aug_ast* aug_parse_stmt(aug_lexer* lexer, bool is_block)
{
    aug_ast* stmt = NULL;
    switch (aug_lexer_curr(lexer).id)
    {
    //case AUG_TOKEN_NAME:
        // TODO: create an assign element
        //stmt = aug_parse_stmt_assign(lexer);
        //if(stmt != NULL)
        //    break;
        //stmt = aug_parse_stmt_expr(lexer);
        //break;
    case AUG_TOKEN_IF:
        stmt = aug_parse_stmt_if(lexer);
        break;
    case AUG_TOKEN_WHILE:
        stmt = aug_parse_stmt_while(lexer);
        break;
    case AUG_TOKEN_FOR:
        stmt = aug_parse_stmt_for(lexer);
        break;
    case AUG_TOKEN_VAR:
        stmt = aug_parse_stmt_define_var(lexer);
        break;
    case AUG_TOKEN_RETURN:
        stmt = aug_parse_stmt_return(lexer);
        break;
    case AUG_TOKEN_BREAK:
        stmt = aug_parse_stmt_break(lexer);
        break;
    case AUG_TOKEN_CONTINUE:
        stmt = aug_parse_stmt_continue(lexer);
        break;
    case AUG_TOKEN_IMPORT:
        if(!is_block)
            stmt = aug_parse_stmt_import(lexer);
        break;
    case AUG_TOKEN_FUNC:
        if(!is_block)
            stmt = aug_parse_stmt_func(lexer);
        else
            aug_log_input_error(lexer->input,  "Unexpected function defintion");
        break;
    default:
        stmt = aug_parse_stmt_expr(lexer);
        break;
    }
    return stmt;
}

aug_ast* aug_parse_block(aug_lexer* lexer)
{
    if(aug_lexer_curr(lexer).id != AUG_TOKEN_LBRACE)
    {
#if AUG_ALLOW_SINGLE_STMT_BLOCK
        aug_ast* block = aug_ast_new(AUG_AST_BLOCK, aug_ast_token_empty(lexer));    
        aug_ast* stmt = aug_parse_stmt(lexer, true);
        aug_ast_add(block, stmt);
        return block;
#else 
        // if no brace, parse single block
        aug_log_input_error(lexer->input,  "Block missing opening \"{\"");
        return NULL;
#endif // AUG_ALLOW_SINGLE_STMT_BLOCK
    }
    aug_lexer_move(lexer); // eat LBRACE

    aug_ast* block = aug_ast_new(AUG_AST_BLOCK, aug_ast_token_empty(lexer));    
    aug_ast* stmt = aug_parse_stmt(lexer, true);
    while(stmt)
    {
        aug_ast_add(block, stmt);
        stmt = aug_parse_stmt(lexer, true);
    }   

    if(aug_lexer_curr(lexer).id != AUG_TOKEN_RBRACE)
    {
        aug_log_input_error(lexer->input,  "Block missing closing \"}\"");
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

    aug_ast* root = aug_ast_new(AUG_AST_ROOT, aug_ast_token_empty(lexer));
    aug_ast* stmt = aug_parse_stmt(lexer, false);
    while(stmt)
    {
        aug_ast_add(root, stmt);
        stmt = aug_parse_stmt(lexer, false);
    }   

    if(root->children_size == 0)
    {
        aug_ast_delete(root);
        return NULL;
    }
    return root;
}

aug_ast* aug_parse(aug_input* input)
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

// OPCODE =============================================   OPCODE   ============================================= OPCODE // 
typedef uint8_t aug_opcode;

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
	AUG_OPCODE(PUSH_MAP)          \
	AUG_OPCODE(PUSH_FUNC)         \
	AUG_OPCODE(PUSH_LOCAL)        \
	AUG_OPCODE(PUSH_GLOBAL)       \
	AUG_OPCODE(PUSH_ELEMENT)      \
	AUG_OPCODE(PUSH_ITERATOR)     \
    AUG_OPCODE(PUSH_RANGE)        \
    AUG_OPCODE(LOAD_LOCAL)        \
	AUG_OPCODE(LOAD_GLOBAL)       \
	AUG_OPCODE(LOAD_ELEMENT)      \
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
	AUG_OPCODE(ITERATE)           \
    AUG_OPCODE(CALL_FRAME)        \
    AUG_OPCODE(ARG_COUNT)         \
	AUG_OPCODE(CALL)              \
	AUG_OPCODE(CALL_TOP)          \
	AUG_OPCODE(CALL_LOCAL)        \
	AUG_OPCODE(CALL_GLOBAL)       \
	AUG_OPCODE(CALL_EXT)          \
	AUG_OPCODE(ENTER_FUNC)        \
	AUG_OPCODE(RETURN_FUNC)       \
    AUG_OPCODE(IMPORT_LIB)        

enum aug_opcodes
{ 
#define AUG_OPCODE(opcode) AUG_OPCODE_##opcode,
	AUG_OPCODE_LIST
#undef AUG_OPCODE
    AUG_OPCODE_COUNT
};

#if AUG_DEBUG
const char* aug_opcode_labels[] =
{
#define AUG_OPCODE(opcode) #opcode,
	AUG_OPCODE_LIST
#undef AUG_OPCODE
}; 

const char* aug_opcode_label(uint8_t opcode)
{
    return aug_opcode_labels[(int)opcode];
}

#endif //AUG_DEBUG
#undef AUG_OPCODE_LIST

// Special value used in bytecode to denote an invalid vm offset
#define AUG_OPCODE_INVALID -1
// Values pushes onto stack to track function calls. (return address, calling base index)  
#define AUG_CALL_FRAME_STACK_SIZE 2

// Symbols ================================================= Symbols ============================================== Symbols //

// Internally managed script symbol types used by IR symtable and the VM debug symbols

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


// Tracing ==============================================Tracing ================================================ Tracing // 

// Script symbols source info
typedef struct aug_trace_marker
{
    int bytecode_addr;
    aug_string* symbol_name;
  
    aug_string* filename;
    aug_pos pos;

    // NOTE: should contain filename to open input and move position to
} aug_trace_marker;

// IR ===================================================   IR   ======================================================= IR // 

typedef enum aug_ir_operand_type
{
    AUG_IR_OPERAND_NONE = 0,
    AUG_IR_OPERAND_BOOL,
    AUG_IR_OPERAND_CHAR,
    AUG_IR_OPERAND_INT,
    AUG_IR_OPERAND_FLOAT,  
    AUG_IR_OPERAND_BYTES,  
    AUG_IR_OPERAND_SYMBOL, //Translates from sym name (str) to offset (int)
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

        char* str; // NOTE: copied from token data, released in generate bytecode pass
    } data;

    aug_ir_operand_type type;
} aug_ir_operand;


typedef struct aug_ir_operation
{
    aug_opcode opcode;
    aug_ir_operand operand; //optional parameter. will be encoded in following bytes
    int bytecode_offset;
} aug_ir_operation;

// The symbol stack state for a variable scope .
typedef struct aug_ir_scope
{
    int base_index;
    int stack_offset;
    aug_hashtable* symtable;
} aug_ir_scope;

// The scope and vm stack state for the function body frames.
typedef struct aug_ir_frame
{
    int base_index;
    int arg_count;
    aug_container* scope_stack; //type aug_ir_scope
} aug_ir_frame;

typedef struct aug_ir_loop
{
    int bytecode_begin;     // the beginning of the loop
    size_t end_jump_operation; // used to jump over block on end condition
    aug_container* break_operations; // size_t use to cache break operations for later fixup
} aug_ir_loop;

// All the blocks within a compilation/translation unit (i.e. file, code literal)
typedef struct aug_ir
{		
    // Top is the current frame.
    aug_container* frame_stack; // type aug_ir_frame

    // Top is the current loop address.
    aug_container* loop_stack; // type aug_ir_loop

    // Generated data
    aug_container* operations; // type aug_ir_operation
    int bytecode_offset;
    
    // Assigned to the outer-most frame's symbol table. 
    // This field is initialized after generation, so not available during the generation pass. 
    aug_hashtable* globals;
    bool valid;

    // Debug table, index from bytecode addr
    aug_container* markers;

} aug_ir;

static inline aug_ir* aug_ir_new()
{
    aug_ir* ir = (aug_ir*)AUG_ALLOC(sizeof(aug_ir));
    ir->valid = true;
    ir->bytecode_offset = 0;
    ir->frame_stack =  aug_container_new_type(aug_ir_frame, 1);
    ir->loop_stack =  aug_container_new_type(aug_ir_loop, 1);
    ir->operations = aug_container_new_type(aug_ir_operation, 1);
    ir->markers = aug_container_new_type(aug_trace_marker, 1);
    
    ir->globals = NULL; // initialized in ast to ir pass
    return ir;
}

static inline void aug_ir_delete(aug_ir* ir)
{
    ir->globals = aug_hashtable_decref(ir->globals);
    ir->frame_stack = aug_container_decref(ir->frame_stack);
    ir->loop_stack = aug_container_decref(ir->loop_stack);

    if(ir->markers->ref_count == 1)
    {
        for(size_t i = 0; i < ir->markers->length; ++i)
        {
            aug_trace_marker* marker = aug_container_ptr_type(aug_trace_marker, ir->markers, i);
            marker->symbol_name = aug_string_decref(marker->symbol_name);
            marker->filename = aug_string_decref(marker->filename);
        }
    }
    ir->markers = aug_container_decref(ir->markers);
 
    for(size_t i = 0; i < ir->operations->length; ++i)
    {
        aug_ir_operation operation = aug_container_at_type(aug_ir_operation, ir->operations, i);
        aug_ir_operand operand = operation.operand;
        // free any allocated operand data
        switch (operand.type)
        {
        case AUG_IR_OPERAND_BYTES:
            AUG_FREE(operand.data.str);
            break;
        default:
            break;
        }
    }

    ir->operations = aug_container_decref(ir->operations);

    AUG_FREE(ir);
}

static inline size_t aug_ir_operand_size(aug_ir_operand operand)
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
    case AUG_IR_OPERAND_SYMBOL:
        return sizeof(operand.data.i); 
    }
    return 0;
}

static inline int aug_ir_operation_size(aug_ir_operation operation)
{
    int size = (int)sizeof(aug_opcode);
    size += (int)aug_ir_operand_size(operation.operand);
    return size;
}

static inline size_t aug_ir_add_operation_arg(aug_ir* ir, aug_opcode opcode, aug_ir_operand operand)
{
    assert(ir->operations != NULL);
    aug_ir_operation operation;
    operation.opcode = opcode;
    operation.operand = operand;
    operation.bytecode_offset = ir->bytecode_offset;

    ir->bytecode_offset += aug_ir_operation_size(operation);
    aug_container_push_type(aug_ir_operation, ir->operations, operation);
    return ir->operations->length-1;
}

static inline size_t aug_ir_add_operation(aug_ir* ir, aug_opcode opcode)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_NONE;
    return aug_ir_add_operation_arg(ir, opcode, operand);
}

static inline aug_ir_operation* aug_ir_last_operation(aug_ir* ir)
{
    assert(ir->operations != NULL && ir->operations->length > 0);
    return aug_container_ptr_type(aug_ir_operation, ir->operations, ir->operations->length - 1);
}

static inline aug_ir_operation* aug_ir_get_operation(aug_ir* ir, size_t operation_index)
{   
    assert(ir->operations != NULL);
    assert(operation_index < ir->operations->length);
    return aug_container_ptr_type(aug_ir_operation, ir->operations, operation_index);
}

static inline aug_ir_operand aug_ir_operand_from_bool(bool data)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_BOOL;
    operand.data.b = data;
    return operand;
}

static inline aug_ir_operand aug_ir_operand_from_char(char data)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_CHAR;
    operand.data.c = data;
    return operand;
}

static inline aug_ir_operand aug_ir_operand_from_int(int data)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_INT;
    operand.data.i = data;
    return operand;
}

static inline aug_ir_operand aug_ir_operand_from_float(float data)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_FLOAT;
    operand.data.f = data;
    return operand;
}

static inline aug_ir_operand aug_ir_operand_from_str(const aug_string* data)
{
    aug_ir_operand operand;
    operand.type = AUG_IR_OPERAND_BYTES;
    operand.data.str = (char*)AUG_ALLOC(sizeof(char)*data->length+1);
#ifdef AUG_SECURE
    strcpy_s(operand.data.str, data->length+1, data->buffer);
#else
    strcpy(operand.data.str, data->buffer);
#endif
    return operand;
}

static inline aug_ir_operand aug_ir_operand_from_symbol(const aug_symbol symbol)
{
    aug_ir_operand operand;
    if(symbol.scope == AUG_SYM_SCOPE_GLOBAL)
    {
        operand.type = AUG_IR_OPERAND_SYMBOL;
        operand.data.str = symbol.name->buffer;
    }
    else
    {
        operand.type = AUG_IR_OPERAND_INT;
        operand.data.i = symbol.offset;
    }
    return operand;
}

static inline aug_ir_frame* aug_ir_current_frame(aug_ir* ir)
{
    assert(ir != NULL && ir->frame_stack != NULL);
    assert(ir->frame_stack->length > 0);
    return aug_container_ptr_type(aug_ir_frame, ir->frame_stack, ir->frame_stack->length - 1);
}

static inline aug_ir_scope* aug_ir_current_scope(aug_ir* ir)
{
    aug_ir_frame* frame = aug_ir_current_frame(ir);
    assert(frame != NULL && frame->scope_stack != NULL);
    assert(frame->scope_stack->length > 0);
    return aug_container_ptr_type(aug_ir_scope, frame->scope_stack, frame->scope_stack->length - 1);
}

static inline bool aug_ir_current_scope_is_global(aug_ir* ir)
{
    aug_ir_frame* frame = aug_ir_current_frame(ir);
    if(ir->frame_stack->length == 1 && frame->scope_stack->length == 1)
        return true;
    return false;
}

static inline int aug_ir_current_scope_local_offset(aug_ir* ir)
{
    const aug_ir_scope* scope = aug_ir_current_scope(ir);
    return scope->stack_offset - scope->base_index;
}

static inline int aug_ir_calling_offset(aug_ir* ir)
{
    aug_ir_scope* scope = aug_ir_current_scope(ir);
    aug_ir_frame* frame = aug_ir_current_frame(ir);
    return (scope->stack_offset - frame->base_index) + frame->arg_count;
}

static inline int aug_ir_current_frame_local_offset(aug_ir* ir, int offset, int frame_delta)
{
    const aug_ir_frame* local_frame = aug_ir_current_frame(ir);
    //If this symbol is a local in an outer frame, calculating the delta
    // must account for frame size to account for frame stack values (ret addr and base index) 
    return offset - local_frame->base_index - frame_delta * AUG_CALL_FRAME_STACK_SIZE;
}

static inline void aug_ir_symtable_free(uint8_t* data)
{
    aug_symbol* symbol_ptr = (aug_symbol*)data;
    if(symbol_ptr != NULL)
        aug_string_decref(symbol_ptr->name);
}

static inline void aug_ir_push_frame(aug_ir* ir, int arg_count)
{
    aug_ir_frame frame;
    frame.arg_count = arg_count;

    if(ir->frame_stack->length > 0)
    {
        const aug_ir_scope* scope = aug_ir_current_scope(ir);
        frame.base_index = scope->stack_offset;
    }
    else
    {
        frame.base_index = 0;
    }

    aug_ir_scope scope;
    scope.base_index = frame.base_index;
    scope.stack_offset = frame.base_index;
    scope.symtable = aug_hashtable_new_type(aug_symbol);
    scope.symtable->free_func = aug_ir_symtable_free;

    frame.scope_stack = aug_container_new_type(aug_ir_scope, 1);
    aug_container_push_type(aug_ir_scope, frame.scope_stack, scope);
    aug_container_push_type(aug_ir_frame, ir->frame_stack, frame);
}

static inline void aug_ir_pop_frame(aug_ir* ir)
{
    if(ir->frame_stack->length == 1)
    {
        aug_ir_scope* scope = aug_ir_current_scope(ir);
        ir->globals = scope->symtable;
        scope->symtable = NULL; // Move to globals
    }
    
    aug_ir_frame frame = aug_container_pop_type(aug_ir_frame, ir->frame_stack);

    for(size_t i = 0; i < frame.scope_stack->length; ++i)
    {
        aug_ir_scope scope = aug_container_at_type(aug_ir_scope, frame.scope_stack, i);
        scope.symtable = aug_hashtable_decref(scope.symtable);
    }
    frame.scope_stack = aug_container_decref(frame.scope_stack);
}

static inline void aug_ir_push_scope(aug_ir* ir)
{
    const aug_ir_scope* current_scope = aug_ir_current_scope(ir);
    aug_ir_frame* frame = aug_ir_current_frame(ir);

    aug_ir_scope scope;
    scope.base_index = current_scope->stack_offset;
    scope.stack_offset = current_scope->stack_offset;
    scope.symtable = aug_hashtable_new_type(aug_symbol);
    scope.symtable->free_func = aug_ir_symtable_free;
    aug_container_push_type(aug_ir_scope, frame->scope_stack, scope);
}

static inline void aug_ir_pop_scope(aug_ir* ir)
{
    const aug_ir_operand delta = aug_ir_operand_from_int(aug_ir_current_scope_local_offset(ir));
    aug_ir_add_operation_arg(ir, AUG_OPCODE_POP, delta);

    aug_ir_frame* frame = aug_ir_current_frame(ir);
    aug_ir_scope scope = aug_container_pop_type(aug_ir_scope, frame->scope_stack);
    scope.symtable = aug_hashtable_decref(scope.symtable);
}

static inline void aug_ir_begin_loop(aug_ir* ir)
{
    aug_ir_loop loop;
    loop.bytecode_begin = ir->bytecode_offset;
    loop.end_jump_operation = 0;
    loop.break_operations = aug_container_new_type(size_t, 1);
    aug_container_push_type(aug_ir_loop, ir->loop_stack, loop);
}

static inline void aug_ir_check_loop(aug_ir* ir)
{
    aug_container* loop_stack = ir->loop_stack;
    aug_ir_loop* loop = aug_container_ptr_type(aug_ir_loop, loop_stack, loop_stack->length-1);

    const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);
    loop->end_jump_operation = aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP_ZERO, stub_operand);
}

static inline bool aug_ir_continue_loop(aug_ir* ir)
{
    aug_container* loop_stack = ir->loop_stack;
    aug_ir_loop* loop = aug_container_ptr_type(aug_ir_loop, loop_stack, loop_stack->length-1);
    if(loop == NULL)
        return false;

    const aug_ir_operand begin_addr_operand = aug_ir_operand_from_int(loop->bytecode_begin);
    aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP, begin_addr_operand);
    return true;
}

static inline bool aug_ir_break_loop(aug_ir* ir)
{
    aug_container* loop_stack = ir->loop_stack;
    aug_ir_loop* loop = aug_container_ptr_type(aug_ir_loop, loop_stack, loop_stack->length-1);
    if(loop == NULL)
        return false;

    const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);
    size_t break_operation = aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP, stub_operand);
    aug_container_push_type(size_t, loop->break_operations, break_operation);
    return true;
}

static inline void aug_ir_end_loop(aug_ir* ir)
{
    aug_ir_loop loop = aug_container_pop_type(aug_ir_loop, ir->loop_stack);
    
    // Close the loop, jump back to beginning
    const aug_ir_operand begin_addr_operand = aug_ir_operand_from_int(loop.bytecode_begin);
    aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP, begin_addr_operand);

    // Fixup stubbed block offsets
    const int end_block_addr = ir->bytecode_offset;

    aug_ir_operation* operation = aug_ir_get_operation(ir, loop.end_jump_operation);
    assert(operation != NULL);
    operation->operand = aug_ir_operand_from_int(end_block_addr);

    for(size_t i = 0; i < loop.break_operations->length; ++i)
    {
        aug_ir_operation* operation = aug_ir_get_operation(ir, aug_container_at_type(size_t, loop.break_operations, i));
        assert(operation != NULL);
        operation->operand = aug_ir_operand_from_int(end_block_addr);
    }

    loop.break_operations = aug_container_decref(loop.break_operations);
}

static inline void aug_ir_mark_symbol(aug_ir* ir, aug_symbol symbol)
{
    aug_trace_marker marker;
    marker.symbol_name = symbol.name;
    marker.filename = NULL;
    marker.bytecode_addr = ir->bytecode_offset;
    aug_container_push_type(aug_trace_marker, ir->markers, marker);

    aug_string_incref(marker.symbol_name);
}

static inline void aug_ir_mark_source(aug_ir* ir, aug_string* filename, aug_pos pos)
{
    aug_trace_marker marker;
    marker.pos = pos;
    marker.symbol_name = NULL;
    marker.filename = filename;
    marker.bytecode_addr = ir->bytecode_offset;
    aug_container_push_type(aug_trace_marker, ir->markers, marker);

    aug_string_incref(filename);
}

static inline bool aug_ir_set_var(aug_ir* ir, aug_string* var_name)
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

    aug_symbol* symbol_ptr = aug_hashtable_insert_type(aug_symbol, scope->symtable, symbol.name->buffer);
    if(symbol_ptr == NULL)
        return false;

    *symbol_ptr = symbol;
    aug_string_incref(symbol_ptr->name);
    return true;
}

static inline bool aug_ir_set_param(aug_ir* ir, aug_string* param_name)
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

    aug_symbol* symbol_ptr = aug_hashtable_insert_type(aug_symbol, scope->symtable, symbol.name->buffer);
    if(symbol_ptr == NULL)
        return false;

    *symbol_ptr = symbol;

    aug_string_incref(symbol_ptr->name);
    return true;
}

static inline bool aug_ir_set_func(aug_ir* ir, aug_string* func_name, int param_count, bool update)
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

    aug_symbol* symbol_ptr = aug_hashtable_insert_type(aug_symbol, scope->symtable, symbol.name->buffer);
    if(symbol_ptr == NULL)
    {
        if(update)
            symbol_ptr = aug_hashtable_ptr_type(aug_symbol, scope->symtable, func_name->buffer);
        else
            return false;
    }

    *symbol_ptr = symbol;
    if(!update)
        aug_string_incref(symbol_ptr->name);
    return true;
}

static inline aug_symbol aug_ir_get_symbol(aug_ir* ir, aug_string* name)
{
    for(int i = (int)ir->frame_stack->length - 1; i >= 0; --i)
    {
        aug_ir_frame frame = aug_container_at_type(aug_ir_frame, ir->frame_stack, (size_t)i);
        for(int j = (int)frame.scope_stack->length - 1; j >= 0; --j)
        {
            aug_ir_scope scope = aug_container_at_type(aug_ir_scope, frame.scope_stack, (size_t)j);
            aug_symbol* symbol_ptr = aug_hashtable_ptr_type(aug_symbol, scope.symtable, name->buffer);
            if (symbol_ptr != NULL && symbol_ptr->type != AUG_SYM_NONE)
                return *symbol_ptr;
        }
    }

    aug_symbol sym;
    sym.type = AUG_SYM_NONE;
    return sym;
}

static inline aug_symbol aug_ir_get_symbol_relative(aug_ir* ir, aug_string* name)
{
    for(int i = (int)ir->frame_stack->length - 1; i >= 0; --i)
    {
        aug_ir_frame frame = aug_container_at_type(aug_ir_frame, ir->frame_stack, (size_t)i);
        for(int j = (int)frame.scope_stack->length - 1; j >= 0; --j)
        {
            aug_ir_scope scope = aug_container_at_type(aug_ir_scope, frame.scope_stack, (size_t)j);
            aug_symbol* symbol_ptr = aug_hashtable_ptr_type(aug_symbol, scope.symtable, name->buffer);
            if (symbol_ptr != NULL && symbol_ptr->type != AUG_SYM_NONE)
            {
                aug_symbol symbol = *symbol_ptr;
                switch (symbol.scope)
                {
                case AUG_SYM_SCOPE_GLOBAL:
                    break;
                case AUG_SYM_SCOPE_PARAM:
                {
                    const aug_ir_frame* local_frame = aug_ir_current_frame(ir);
                    symbol.offset = symbol.offset - local_frame->base_index;
                    break;
                }
                case AUG_SYM_SCOPE_LOCAL:
                {
                    const int frame_delta = (int)(ir->frame_stack->length-1) - i;
                    symbol.offset = aug_ir_current_frame_local_offset(ir, symbol.offset, frame_delta);
                    break;
                }
                }
                return symbol;
            }
        }
    }

    aug_symbol sym;
    sym.type = AUG_SYM_NONE;
    return sym;
}

static inline aug_symbol aug_ir_get_symbol_local(aug_ir* ir, aug_string* name)
{
    aug_ir_scope* scope = aug_ir_current_scope(ir);
    aug_symbol* symbol_ptr =  aug_hashtable_ptr_type(aug_symbol, scope->symtable, name->buffer);
    if(symbol_ptr != NULL)
        return *symbol_ptr;

    aug_symbol sym;
    sym.type = AUG_SYM_NONE;
    return sym;
}

// VALUE ===============================================   VALUE   ============================================= VALUE // 

static inline bool aug_set_bool(aug_value* value, bool data)
{
    if(value == NULL)
        return false;
    value->type = AUG_BOOL;
    value->b = data;
    return true;
}

static inline bool aug_set_int(aug_value* value, int data)
{
    if(value == NULL)
        return false;
    value->type = AUG_INT;
    value->i = data;
    return true;
}

static inline bool aug_set_char(aug_value* value, char data)
{
    if(value == NULL)
        return false;
    value->type = AUG_CHAR;
    value->c = data;
    return true;
}

static inline bool aug_set_float(aug_value* value, float data)
{
    if(value == NULL)
        return false;
    value->type = AUG_FLOAT;
    value->f = data;
    return true;
}

static inline bool aug_set_string(aug_value* value, const char* data)
{
    if(value == NULL)
        return false;

    value->type = AUG_STRING;
    value->str = aug_string_create(data);
    return true;
}

static inline bool aug_set_array(aug_value* value)
{
    if(value == NULL)
        return false;

    value->type = AUG_ARRAY;
    value->array = aug_array_new(1);
    return true;
}

static inline bool aug_set_map(aug_value* value)
{
    if(value == NULL)
        return false;

    value->type = AUG_MAP;
    value->map = aug_map_new(1);
    return true;
}

static inline bool aug_set_func(aug_value* value, int data)
{
    if(value == NULL)
        return false;
    value->type = AUG_FUNCTION;
    value->i = data;
    return true;
}

static inline bool aug_set_iterator(aug_value* value, aug_value* iterable)
{
    if(value == NULL)
        return false;

    value->type = AUG_ITERATOR;
    value->it = aug_iterator_new(iterable);
    return value->it != NULL;
}

static inline bool aug_set_range(aug_value* value, aug_value* from, aug_value* to)
{
    if(value == NULL)
        return false;

    if(to == NULL || to->type != AUG_INT || from == NULL || from->type != AUG_INT)
        return false;


    value->type = AUG_RANGE;
    value->range = aug_range_new(from->i, to->i);
    return value->range != NULL;
}

static inline bool aug_iterate(aug_value* value, aug_value* out_element)
{
    if(value == NULL || value->type != AUG_ITERATOR)
        return false;
    aug_iterator_next(value->it);
    return aug_iterator_get(value->it, out_element);
}

aug_value aug_none()
{
    aug_value value;
    value.type = AUG_NONE;
    return value;
}

aug_value aug_create_array()
{
    aug_value value;
    aug_set_array(&value);
    return value;
}

aug_value aug_create_map()
{
    aug_value value;
    aug_set_map(&value);
    return value;
}

bool aug_to_bool(const aug_value* value)
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
    case AUG_ARRAY:
        return value->array != NULL;
    case AUG_MAP:
        return value->map != NULL;
    case AUG_OBJECT:
        return value->obj != NULL;
    case AUG_FUNCTION:
        return value->i != 0;
    case AUG_ITERATOR:
        return value->it != NULL && value->it->index != NULL;
    case AUG_RANGE:
        return value->range != NULL;
    case AUG_USERDATA:
        return value->userdata != NULL; 
    }
    return false;
}

int aug_to_int(const aug_value* value)
{    
    if(value == NULL)
        return 0;

    switch (value->type)
    {
    case AUG_BOOL:
        return value->b ? 1 : 0;
    case AUG_INT:
        return value->i;
    case AUG_CHAR:
        return (int)value->c;
    case AUG_FLOAT:
        return (int)value->f;
    default:
        return 0;
    }
    return 0;
}

float aug_to_float(const aug_value* value)
{
    if(value == NULL)
    	return 0.0f;

    switch (value->type)
    { 
    case AUG_BOOL:
        return value->b ? 1.0f : 0.0f;
    case AUG_INT:
        return (float)value->i;
    case AUG_CHAR:
        return (float)value->c;
    case AUG_FLOAT:
        return value->f;
    default:
        return 0.0f;
    }
    return 0.0f;
}

bool aug_compare(aug_value* a, aug_value* b)
{
    if(a->type != b->type)
        return false;
    
    switch (a->type)
    {
    case AUG_NONE:
        return true;
    case AUG_FUNCTION:
        return a->i == b->i;
    case AUG_BOOL:
        return a->b == b->b;
    case AUG_INT:
        return a->i == b->i;
    case AUG_CHAR:
        return a->c == b->c;
    case AUG_FLOAT:
        return (float)fabs(a->f - b->f) < AUG_APPROX_THRESHOLD;
    case AUG_STRING:
        return aug_string_compare(a->str, b->str);
    case AUG_ARRAY:
        return aug_array_compare(a->array, b->array);
    case AUG_RANGE:
        return false; //should not be user accesible to compare
    case AUG_MAP:
        assert(0); //TODO
        return false;
    case AUG_OBJECT:
        assert(0); //TODO
        return false;
    case AUG_ITERATOR:
        return false; //should not be user accesible to compare
    case AUG_USERDATA:
        return a->userdata == b->userdata;
    }
    return false;
}

const char* aug_type_label(const aug_value* value)
{
    if (value == NULL) return "null";
    switch (value->type)
    {
    case AUG_NONE:      return "none";
    case AUG_BOOL:      return "bool";
    case AUG_INT:       return "int";
    case AUG_CHAR:      return "char";
    case AUG_FLOAT:     return "float";
    case AUG_STRING:    return "string";
    case AUG_ARRAY:     return "array";
    case AUG_OBJECT:    return "object";
    case AUG_MAP:       return "map";
    case AUG_FUNCTION:  return "function";
    case AUG_RANGE:     return "range";
    case AUG_ITERATOR:  return "iterator";
    case AUG_USERDATA:  return "custom";
    }
    return NULL;
}

void aug_decref(aug_value* value)
{
    if(value == NULL)
        return;

    switch (value->type)
    {
    case AUG_STRING:
        value->str = aug_string_decref(value->str);
        break;
    case AUG_ARRAY:
        value->array = aug_array_decref(value->array);
        break;
    case AUG_MAP:
        value->map = aug_map_decref(value->map);
        break;
    case AUG_ITERATOR:
        value->it = aug_iterator_decref(value->it);
        break;
    case AUG_RANGE:
        value->range = aug_range_decref(value->range);
        break;
    case AUG_OBJECT:
        if(value->obj && --value->obj->ref_count <= 0)
            AUG_FREE(value->obj);
        break;
    default:
        break;
    }

    *value = aug_none();
}

void aug_incref(aug_value* value)
{
    if(value == NULL)
        return;

    switch (value->type)
    {
    case AUG_STRING:
        aug_string_incref(value->str);
        break;
    case AUG_ARRAY:
        aug_array_incref(value->array);
        break;
    case AUG_MAP:
        aug_map_incref(value->map);
        break;
    case AUG_ITERATOR:
        aug_iterator_incref(value->it);
        break;
    case AUG_RANGE:
        aug_range_incref(value->range);
        break;
    case AUG_OBJECT:
        assert(value->obj);
        ++value->obj->ref_count;
        break;
    default:
        break;
    }
}

static inline void aug_assign(aug_value* to, aug_value* from)
{
    if(from == NULL || to == NULL)
        return;

    aug_decref(to);
    *to = *from;
    aug_incref(to);   
}

static inline void aug_move(aug_value* to, aug_value* from)
{
    if(from == NULL || to == NULL)
        return;

    aug_decref(to);
    *to = *from;
    *from = aug_none();
}

static inline bool aug_get_element(aug_value* value, aug_value* index, aug_value* element_out)
{
    assert(element_out != NULL);
    *element_out = aug_none();

    if(value == NULL || index == NULL)
        return false;

    switch (value->type)
    {
    case AUG_STRING:
    {
        size_t i = (size_t)aug_to_int(index);
        char c = aug_string_at(value->str, i);
        if(c == -1)
            return false;
        *element_out = aug_create_char(c);
        return true;
    }
    case AUG_ARRAY:
    {
        size_t i = (size_t)aug_to_int(index);
        aug_value* element = aug_array_at(value->array, i);
        if(element == NULL)
            return false;
        *element_out = *element;
        return true;
    }
    case AUG_MAP:
    {
        aug_value* element = aug_map_get(value->map, index);
        if(element == NULL)
            return true; // Allow maps to return non if index is invalid

        *element_out = *element;
        return true;
    }
    case AUG_RANGE:
    {
        if(index->type != AUG_INT)
            return false;

        int i = aug_to_int(index);
        if(i < value->range->from || i >= value->range->to)
            return false;

        *element_out = aug_create_int(i);
        return true;
    }
    default:
        break;
    }
    return false;
}

static inline bool aug_set_element(aug_value* value, aug_value* index, aug_value* element)
{
    if(value == NULL || index == NULL || element == NULL)
        return false;

    switch (value->type)
    {
    case AUG_STRING:
    {
        size_t i = (size_t)aug_to_int(index);
        if(element->type == AUG_CHAR)
            return aug_string_set(value->str, i, element->c);
        return false;
    }
    case AUG_ARRAY:
    {
        size_t i = (size_t)aug_to_int(index);
        return aug_array_set(value->array, i, element);
    }
    case AUG_MAP:
    {
        return aug_map_insert_or_update(value->map, index, element);
    }
    default:
        break;
    }
    return false;
}

// TODO: Function table?
// Define a plain-old-data binary operation
#define AUG_DEFINE_BINOP_POD(result, lhs, rhs,                  \
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

static inline bool aug_add(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_int(result, lhs->i + rhs->i),
        return aug_set_float(result, (float)lhs->i + rhs->f),
        return aug_set_float(result, lhs->f + (float)rhs->i),
        return aug_set_float(result, lhs->f + rhs->f),
        return aug_set_char(result, lhs->c + rhs->c),
        return false
    );
    return false;
}

static inline bool aug_sub(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_int(result, lhs->i - rhs->i),
        return aug_set_float(result, (float)lhs->i - rhs->f),
        return aug_set_float(result, lhs->f - (float)rhs->i),
        return aug_set_float(result, lhs->f - rhs->f),
        return aug_set_char(result, lhs->c - rhs->c),
        return false
    );
    return false;
}

static inline bool aug_mul(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_int(result, lhs->i * rhs->i),
        return aug_set_float(result, (float)lhs->i * rhs->f),
        return aug_set_float(result, lhs->f * (float)rhs->i),
        return aug_set_float(result, lhs->f * rhs->f),
        return aug_set_char(result, lhs->c * rhs->c),
        return false
    );
    return false;
}

static inline bool aug_div(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_float(result, (float)lhs->i / (float)rhs->i),
        return aug_set_float(result, (float)lhs->i / rhs->f),
        return aug_set_float(result, lhs->f / (float)rhs->i),
        return aug_set_float(result, lhs->f / rhs->f),
        return aug_set_char(result, lhs->c / rhs->c),
        return false
    );
    return false;
}

static inline bool aug_pow(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_int(result, (int)powf((float)lhs->i, (float)rhs->i)),
        return aug_set_float(result, powf((float)lhs->i, rhs->f)),
        return aug_set_float(result, powf(lhs->f, (float)rhs->i)),
        return aug_set_float(result, powf(lhs->f, rhs->f)),
        return false,
        return false
    );
    return false;
}

static inline bool aug_mod(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_int(result, lhs->i % rhs->i),
        return aug_set_float(result, (float)fmod((float)lhs->i, rhs->f)),
        return aug_set_float(result, (float)fmod(lhs->f, (float)rhs->i)),
        return aug_set_float(result, (float)fmod(lhs->f, rhs->f)),
        return false,
        return false
    );
    return false;
}

static inline bool aug_lt(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_bool(result, lhs->i < rhs->i),
        return aug_set_bool(result, (float)lhs->i < rhs->f),
        return aug_set_bool(result, lhs->f < (float)rhs->i),
        return aug_set_bool(result, lhs->f < rhs->f),
        return aug_set_bool(result, lhs->c < rhs->c),
        return false
    );
    return false;
}

static inline bool aug_lte(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_bool(result, lhs->i <= rhs->i),
        return aug_set_bool(result, (float)lhs->i <= rhs->f),
        return aug_set_bool(result, lhs->f <= (float)rhs->i),
        return aug_set_bool(result, lhs->f <= rhs->f),
        return aug_set_bool(result, lhs->c <= rhs->c),
        return false
    );
    return false;
}

static inline bool aug_gt(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_bool(result, lhs->i > rhs->i),
        return aug_set_bool(result, (float)lhs->i > rhs->f),
        return aug_set_bool(result, lhs->f > (float)rhs->i),
        return aug_set_bool(result, lhs->f > rhs->f),
        return aug_set_bool(result, lhs->c > rhs->c),
        return false
    );
    return false;
}

static inline bool aug_gte(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_bool(result, lhs->i >= rhs->i),
        return aug_set_bool(result, (float)lhs->i >= rhs->f),
        return aug_set_bool(result, lhs->f >= (float)rhs->i),
        return aug_set_bool(result, lhs->f >= rhs->f),
        return aug_set_bool(result, lhs->c >= rhs->c),
        return false
    );
    return false;
}

static inline bool aug_eq(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_bool(result, lhs->i == rhs->i),
        return aug_set_bool(result, (float)lhs->i == rhs->f),
        return aug_set_bool(result, lhs->f == (float)rhs->i),
        return aug_set_bool(result, lhs->f == rhs->f),
        return aug_set_bool(result, lhs->c == rhs->c),
        return aug_set_bool(result, lhs->b == rhs->b)
    );

    if (lhs->type == AUG_NONE || rhs->type == AUG_NONE)
        return aug_set_bool(result, lhs->type == rhs->type);

    if (lhs->type != rhs->type)
        return false;

    switch (lhs->type)
    {
    case AUG_STRING: return aug_set_bool(result, aug_string_compare(lhs->str, rhs->str));
    case AUG_ARRAY: return aug_set_bool(result, aug_array_compare(lhs->array, rhs->array));
    default: break;
    }
    return false;
}

static inline bool aug_neq(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_bool(result, lhs->i != rhs->i),
        return aug_set_bool(result, (float)lhs->i != rhs->f),
        return aug_set_bool(result, lhs->f != (float)rhs->i),
        return aug_set_bool(result, lhs->f != rhs->f),
        return aug_set_bool(result, lhs->c != rhs->c),
        return aug_set_bool(result, lhs->b != rhs->b)
    );

    if (lhs->type == AUG_NONE || rhs->type == AUG_NONE)
        return aug_set_bool(result, lhs->type != rhs->type);

    if (lhs->type != rhs->type)
        return false;

    switch (lhs->type)
    {
    case AUG_STRING: return aug_set_bool(result, !aug_string_compare(lhs->str, rhs->str));
    case AUG_ARRAY: return aug_set_bool(result, !aug_array_compare(lhs->array, rhs->array));
    default: break;
    }
    return false;
}

static inline bool aug_approxeq(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    AUG_DEFINE_BINOP_POD(result, lhs, rhs,
        return aug_set_bool(result, lhs->i == rhs->i),
        return aug_set_bool(result, (float)fabs((float)lhs->i - rhs->f) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(result, (float)fabs(lhs->f - (float)rhs->i) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(result, (float)fabs(lhs->f - rhs->f) < AUG_APPROX_THRESHOLD),
        return aug_set_bool(result, lhs->c == rhs->c),
        return aug_set_bool(result, lhs->b == rhs->b)
    );
    return false;
}

static inline bool aug_and(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    return aug_set_bool(result, aug_to_bool(lhs) && aug_to_bool(rhs));
}

static inline bool aug_or(aug_value* result, aug_value* lhs, aug_value* rhs)
{
    return aug_set_bool(result, aug_to_bool(lhs) || aug_to_bool(rhs));
}

static inline bool aug_not(aug_value* result, aug_value* arg)
{
    return aug_set_bool(result, !aug_to_bool(arg));
}

#undef AUG_DEFINE_BINOP_POD

// VM  =====================================================  VM  ================================================== VM // 

// Used to convert values to/from bytes for constant values
typedef union aug_vm_bytecode_value
{
    bool b;
    int i;
    char c;
    float f;
    char bytes[sizeof(float)]; //Used to access raw byte data to bool, float and int types
} aug_vm_bytecode_value;

aug_trace_marker* aug_vm_get_marker(aug_vm* vm)
{
    const int addr = (int)(vm->last_instruction - vm->bytecode);
    // TODO: index by address for faster lookup. Not priority as this will only occur on VM error 
    for(size_t i = 0; i < vm->markers->length; ++i)
    {
        aug_trace_marker* marker = aug_container_ptr_type(aug_trace_marker, vm->markers, i);
        if(marker->bytecode_addr == addr && marker->symbol_name == NULL)
            return marker;
    }
    return NULL;
}

aug_string* aug_vm_get_marker_symbol(aug_vm* vm)
{
    const int addr = (int)(vm->last_instruction - vm->bytecode);
    // TODO: index by address for faster lookup. Not priority as this will only occur on VM error 
    for(size_t i = 0; i < vm->markers->length; ++i)
    {
        aug_trace_marker marker = aug_container_at_type(aug_trace_marker, vm->markers, i);
        if(marker.bytecode_addr == addr && marker.symbol_name != NULL)
            return marker.symbol_name;
    }
    return NULL;
}

void aug_log_vm_warn(aug_vm* vm, const char* format, ...)
{
    // Log the source code, or debug symbol if it exists
    aug_trace_marker* marker = aug_vm_get_marker(vm);
    if(marker != NULL && marker->filename != NULL)
    {
        aug_input* input = aug_input_open(marker->filename->buffer, vm->error_func);
        aug_log_input_error_hint(input, &marker->pos);
        aug_input_close(input);
    }

    va_list args;
    va_start(args, format);
    aug_log_error_internal(vm->error_func, format, args);
    va_end(args);
}


void aug_log_vm_error(aug_vm* vm, const char* format, ...)
{
    // Do not cascade multiple errors
    if (vm->instruction == NULL)
        return;
    vm->instruction = NULL;

    aug_trace_marker* marker = aug_vm_get_marker(vm);
    if(marker != NULL && marker->filename != NULL)
    {
        aug_input* input = aug_input_open(marker->filename->buffer, vm->error_func);
        aug_log_input_error_hint(input, &marker->pos);
        aug_input_close(input);
    }

    va_list args;
    va_start(args, format);
    aug_log_error_internal(vm->error_func, format, args);
    va_end(args);
}

static inline aug_value* aug_vm_top(aug_vm* vm)
{
    return &vm->stack[vm->stack_index -1];
}

static inline aug_value* aug_vm_push(aug_vm* vm)
{
    if(vm->stack_index >= AUG_STACK_SIZE)
    {                                              
        //aug_log_vm_error(vm, "Stack overflow");      
        return NULL;                           
    }
    aug_value* top = &vm->stack[vm->stack_index++];
    return top;
}

static inline aug_value* aug_vm_pop(aug_vm* vm)
{
    aug_value* top = aug_vm_top(vm);
    --vm->stack_index;
    return top;
}

static inline aug_value* aug_vm_get_global(aug_vm* vm, int stack_offset)
{
    if(stack_offset < 0)
    {
        //aug_log_vm_error(vm, "Stack underflow");
        return NULL;
    }
    else if(stack_offset >= AUG_STACK_SIZE)
    {
        //aug_log_vm_error(vm, "Stack overflow");
        return NULL;
    }

    return &vm->stack[stack_offset];
}

static inline void aug_vm_push_call_frame(aug_vm* vm, int return_addr)
{
    // NOTE: pushing 2 values onto stack. if this changes, must modify AUG_CALL_FRAME_STACK_SIZE 
    //       note that the call also pushes the argument count
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

static inline aug_value* aug_vm_get_local(aug_vm* vm, int stack_offset)
{
    return aug_vm_get_global(vm, vm->base_index + stack_offset);
}

static inline int aug_vm_read_bool(aug_vm* vm)
{
    aug_vm_bytecode_value bytecode_value;
    for(size_t i = 0; i < sizeof(bytecode_value.b); ++i)
        bytecode_value.bytes[i] = *(vm->instruction++);
    return bytecode_value.b;
}

static inline int aug_vm_read_int(aug_vm* vm)
{
    aug_vm_bytecode_value bytecode_value;
    for(size_t i = 0; i < sizeof(bytecode_value.i); ++i)
        bytecode_value.bytes[i] = *(vm->instruction++);
    return bytecode_value.i;
}

static inline char aug_vm_read_char(aug_vm* vm)
{
    aug_vm_bytecode_value bytecode_value;
    for(size_t i = 0; i < sizeof(bytecode_value.c); ++i)
        bytecode_value.bytes[i] = *(vm->instruction++);
    return bytecode_value.c;
}

static inline float aug_vm_read_float(aug_vm* vm)
{
    aug_vm_bytecode_value bytecode_value;
    for(size_t i = 0; i < sizeof(bytecode_value.f); ++i)
        bytecode_value.bytes[i] = *(vm->instruction++);
    return bytecode_value.f;
}

static inline const char* aug_vm_read_bytes(aug_vm* vm)
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
    vm->arg_count = 0;
    vm->valid = false; 
    vm->running = false; 
}

void aug_vm_shutdown(aug_vm* vm)
{
    vm->running = false; 

    //Cleanup stack values. Free any outstanding values
    while(vm->stack_index > 0)
        aug_decref(aug_vm_pop(vm));

    // Ensure that stack has returned to beginning state
    if(vm->stack_index != 0)
        aug_log_error(vm->error_func, "Virtual machine shutdown error. Invalid stack state");
}

void aug_vm_load_script(aug_vm* vm, const aug_script* script)
{
    if(vm == NULL || script == NULL)
        return;

    if(script->bytecode == NULL)
        vm->bytecode = NULL;
    else
        vm->bytecode = script->bytecode;
    
    vm->instruction = vm->bytecode;
    vm->valid = (vm->bytecode != NULL);
    vm->markers = script->markers; //NOTE weak ref
    vm->lib_extensions = script->lib_extensions;

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
    if(vm == NULL || script == NULL)
        return;

    vm->instruction = vm->bytecode = NULL;
    while (vm->stack_index > 0)
    {
        aug_value* top = aug_vm_pop(vm);
        aug_decref(top);
    }

    // Unload the script state
    if(script->stack_state != NULL)
    {
        for(size_t i = 0; i < script->stack_state->length; ++i)
        {   
            aug_value* value = aug_array_at(script->stack_state, i);
            aug_decref(value);
        }
        script->stack_state = aug_array_decref(script->stack_state);
    }
}

void aug_vm_save_script(aug_vm* vm, aug_script* script)
{
    if(vm == NULL || script == NULL)
        return;

    // Move ownership
    script->lib_extensions = vm->lib_extensions;
    vm->lib_extensions = NULL;

    // reset script stack state to match vm
    script->stack_state = aug_array_decref(script->stack_state);
    if (vm->stack_index > 0)
    {
        script->stack_state = aug_array_new(1);

        aug_array_reserve(script->stack_state, (size_t)(vm->stack_index));
        script->stack_state->length = (size_t)(vm->stack_index);

        while(vm->stack_index > 0)
        {
            aug_value* top = aug_vm_pop(vm);
            aug_value* element = aug_array_at(script->stack_state, (size_t)(vm->stack_index));
            *element = aug_none();
            aug_assign(element, top);
        }
    }
}

void aug_vm_lib_load(aug_vm* vm, const char* libname)
{
#if _WIN32
    char libpath[1024];
    snprintf(libpath, sizeof(libpath), "%s.dll", libname); //seach locally

    HINSTANCE handle = LoadLibraryA(libpath);
    if (handle == NULL)
    {
        aug_log_error(vm->error_func, "Failed to open library %s", libname);
        return;
    }

    // resolve function address here
    aug_register_lib_func register_lib = (aug_register_lib_func)GetProcAddress(handle, AUG_REGISTER_LIB_FUNC);
    if (register_lib == NULL)
    {
        aug_log_error(vm->error_func, "Library %s failed to setup", libname);
        FreeLibrary(handle);
        return;
    }
#elif __linux
    char libpath[1024];
    snprintf(libpath, sizeof(libpath), "./%s.so", libname); //seach locally

    void* handle = dlopen (libpath, RTLD_LAZY | RTLD_GLOBAL);
    if (handle == NULL) 
    {
        char* error = dlerror();
        aug_log_error(vm->error_func, "Failed to open library %s. %s", libname, error);
        return;
    }

    aug_register_lib_func register_lib = (aug_register_lib_func)dlsym(handle, AUG_REGISTER_LIB_FUNC);
    char* error = dlerror();
    if (error != NULL)
    {
        aug_log_error(vm->error_func, "Library %s failed to setup. %s", libname, error);
        dlclose(handle);
        return;
    }
#endif

    register_lib(vm);
    aug_container_push_type(aug_lib_handle, vm->libs, (aug_lib_handle)handle);
}

void aug_vm_lib_unload(aug_lib_handle handle)
{
    if(handle == 0)
        return;
#if _WIN32
    FreeLibrary((HINSTANCE)handle);
#elif __linux
    dlclose((void*)handle);
#endif
}

#define AUG_OPCODE_UNOP(opfunc, str)                                                \
{                                                                                   \
    aug_value* arg = aug_vm_pop(vm);                                                \
    aug_value target = aug_none();                                                  \
    if (!opfunc(&target, arg))                                                      \
        aug_log_vm_error(vm, "%s %s not defined", str, aug_type_label(arg));        \
    aug_decref(arg);                                                                \
    aug_move(aug_vm_push(vm), &target);                                             \
    break;                                                                          \
}

#define AUG_OPCODE_BINOP(opfunc, str)                                                                       \
{                                                                                                           \
    aug_value* rhs = aug_vm_pop(vm);                                                                        \
    aug_value* lhs = aug_vm_pop(vm);                                                                        \
    aug_value target = aug_none();                                                                          \
    if (!opfunc(&target, lhs, rhs))                                                                         \
        aug_log_vm_error(vm, "%s %s %s not defined", aug_type_label(lhs), str, aug_type_label(rhs));        \
    aug_decref(lhs);                                                                                        \
    aug_decref(rhs);                                                                                        \
    aug_move(aug_vm_push(vm), &target);                                                                     \
    break;                                                                                                  \
}

void aug_vm_execute(aug_vm* vm)
{
    if(vm == NULL)
        return;

    vm->running = true; 
    while(vm->instruction)
    {
        vm->last_instruction = vm->instruction;

        aug_opcode opcode = (aug_opcode)(*vm->instruction++);
        switch(opcode)
        {
            case AUG_OPCODE_ADD:      AUG_OPCODE_BINOP(aug_add, "+");
            case AUG_OPCODE_SUB:      AUG_OPCODE_BINOP(aug_sub, "-");
            case AUG_OPCODE_MUL:      AUG_OPCODE_BINOP(aug_mul, "*");
            case AUG_OPCODE_DIV:      AUG_OPCODE_BINOP(aug_div, "/");
            case AUG_OPCODE_POW:      AUG_OPCODE_BINOP(aug_pow, "^");
            case AUG_OPCODE_MOD:      AUG_OPCODE_BINOP(aug_mod, "%");
            case AUG_OPCODE_AND:      AUG_OPCODE_BINOP(aug_and, "and");
            case AUG_OPCODE_OR:       AUG_OPCODE_BINOP(aug_or,  "or");
            case AUG_OPCODE_LT:       AUG_OPCODE_BINOP(aug_lt,  "<");
            case AUG_OPCODE_LTE:      AUG_OPCODE_BINOP(aug_lte, "<=");
            case AUG_OPCODE_GT:       AUG_OPCODE_BINOP(aug_gt,  ">");
            case AUG_OPCODE_GTE:      AUG_OPCODE_BINOP(aug_gte, ">=");
            case AUG_OPCODE_EQ:       AUG_OPCODE_BINOP(aug_eq,  "==");
            case AUG_OPCODE_NEQ:      AUG_OPCODE_BINOP(aug_neq, "!=");
            case AUG_OPCODE_APPROXEQ: AUG_OPCODE_BINOP(aug_approxeq, "~=");
            case AUG_OPCODE_NOT:      AUG_OPCODE_UNOP(aug_not, "!");
            case AUG_OPCODE_NO_OP:
                break;
            case AUG_OPCODE_EXIT:
            {
                vm->instruction = NULL;
                break;
            }
            case AUG_OPCODE_POP:
            {
                int delta = aug_vm_read_int(vm);
                while(--delta >= 0)
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
                while(--count >= 0)
                {
                    aug_value* arg = aug_vm_pop(vm);
                    aug_value* element = aug_array_push(value.array);
                    if(element != NULL) 
                    {
                        *element = aug_none();
                        aug_move(element, arg);
                    }
                }

                aug_value* top = aug_vm_push(vm);          
                aug_move(top, &value);
                break;
            }
           case AUG_OPCODE_PUSH_MAP:
           {
               aug_value value;
               aug_set_map(&value);

               int count = aug_vm_read_int(vm);
               while (--count >= 0)
               {
                   aug_value* arg_value = aug_vm_pop(vm);
                   aug_value* arg_key = aug_vm_pop(vm);
                   aug_map_insert(value.map, arg_key, arg_value);
                   aug_decref(arg_key);
                   aug_decref(arg_value);
               }

               aug_value* top = aug_vm_push(vm);
               aug_move(top, &value);
               break;
           }
            case AUG_OPCODE_PUSH_FUNC:   
            {
                int func_addr = aug_vm_read_int(vm);
                aug_value* value = aug_vm_push(vm);
                if(value == NULL) 
                    break;
                aug_set_func(value, func_addr);
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
                aug_value* container = aug_vm_pop(vm);
                aug_value* index = aug_vm_pop(vm);

                aug_value value = aug_none();
                if(!aug_get_element(container, index, &value))
                    aug_log_vm_error(vm, "Index out of range error"); // TODO: more descriptive
                aug_decref(container);
                aug_decref(index);

                aug_value* top = aug_vm_push(vm);
                aug_assign(top, &value);
                break;
            }
            case AUG_OPCODE_PUSH_RANGE:
            {
                aug_value* to = aug_vm_pop(vm);
                aug_value* from = aug_vm_pop(vm);

                aug_value value;
                if(!aug_set_range(&value, from, to))    
                    aug_log_vm_error(vm, "Could not create a range from type %s to %s", aug_type_label(from), aug_type_label(to)); // TODO: more descriptive
                aug_decref(to);
                aug_decref(from);

                aug_value* top = aug_vm_push(vm);
                aug_move(top, &value);
                break;
            }
            case AUG_OPCODE_PUSH_ITERATOR:
            {
                aug_value* iterable = aug_vm_pop(vm);

                // Create new iterator, move into iterable slot. Iterator retains pointer to iterable
                aug_value value;
                if(!aug_set_iterator(&value, iterable))    
                    aug_log_vm_error(vm, "Type %s is not an iterable", aug_type_label(iterable)); // TODO: more descriptive

                aug_value* top = aug_vm_push(vm);
                aug_move(top, &value);
                break;
            }
            case AUG_OPCODE_ITERATE:
            {
                const int stack_offset = aug_vm_read_int(vm);
                aug_value* iterator = aug_vm_get_local(vm, stack_offset);

                aug_value element;
                bool success = aug_iterate(iterator, &element);

                if(success)
                {
                    aug_value* value = aug_vm_push(vm);
                    if(value != NULL)
                        aug_assign(value, &element);
                }

                aug_value* condition = aug_vm_push(vm);
                if(condition != NULL)
                    aug_set_bool(condition, success);
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
                aug_value* global = aug_vm_get_global(vm, stack_offset);
                aug_value* top = aug_vm_pop(vm);
                aug_move(global, top);
                break;
            }
            case AUG_OPCODE_LOAD_ELEMENT:
            {
                aug_value* container = aug_vm_pop(vm);
                aug_value* index = aug_vm_pop(vm);
                aug_value* value = aug_vm_pop(vm);
                if(!aug_set_element(container, index, value))    
                    aug_log_vm_error(vm, "Index out of range error"); // TODO: more descriptive
                aug_decref(container);
                aug_decref(index);
                aug_decref(value);
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
                aug_value* cond = aug_vm_pop(vm);
                if(aug_to_bool(cond) != 0)
                    vm->instruction = vm->bytecode + instruction_offset;
                aug_decref(cond);
                break;
            }
            case AUG_OPCODE_JUMP_ZERO:
            {
                const int instruction_offset = aug_vm_read_int(vm);
                aug_value* cond = aug_vm_pop(vm);
                if(aug_to_bool(cond) == 0)
                    vm->instruction = vm->bytecode + instruction_offset;
                aug_decref(cond);
                break;
            }
            case AUG_OPCODE_CALL_FRAME:
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
            case AUG_OPCODE_CALL_TOP:
            {
                aug_value* top = aug_vm_pop(vm);
                if(top == NULL || top->type != AUG_FUNCTION)
                {
                    aug_string* symbol = aug_vm_get_marker_symbol(vm);
                    aug_log_vm_error(vm, "Unnamed value %s is not a function", 
                        symbol ? symbol->buffer : "(anonymous)");
                    break;
                }

                const int func_addr = top->i;
                vm->instruction = vm->bytecode + func_addr;
                vm->base_index = vm->stack_index;
                break;
            }
            case AUG_OPCODE_CALL_LOCAL:
            {
                const int stack_offset = aug_vm_read_int(vm);
                aug_value* local = aug_vm_get_local(vm, stack_offset);
                if(local == NULL || local->type != AUG_FUNCTION)
                {
                    aug_string* symbol = aug_vm_get_marker_symbol(vm);
                    aug_log_vm_error(vm, "Local variable %s can not a function", 
                        symbol ? symbol->buffer : "(anonymous)");
                    break;
                }

                const int func_addr = local->i;
                vm->instruction = vm->bytecode + func_addr;
                vm->base_index = vm->stack_index;
                break;
            }
            case AUG_OPCODE_CALL_GLOBAL:
            {
                const int stack_offset = aug_vm_read_int(vm);
                aug_value* global = aug_vm_get_global(vm, stack_offset);
                if(global == NULL || global->type != AUG_FUNCTION)
                {                    
                    aug_string* symbol = aug_vm_get_marker_symbol(vm);
                    aug_log_vm_error(vm, "Global variable %s can not a function", 
                        symbol ? symbol->buffer : "(anonymous)");
                    break;
                }

                vm->instruction = vm->bytecode + global->i;
                vm->base_index = vm->stack_index;
                break;
            }
            case AUG_OPCODE_CALL_EXT:
            {
                aug_value* func_name_value = aug_vm_pop(vm);
                if(func_name_value == NULL || func_name_value->type != AUG_STRING)
                {
                    aug_decref(func_name_value);
                    aug_log_vm_error(vm, "Extension function call expected function name to be pushed on stack");
                    break;
                }

                const char* func_name = func_name_value->str->buffer;

                // TODO: room for optimization. use a single extension map ? Load/Unload lib extensions on script load/unload may have better amortized runtime
                aug_extension* extension = aug_hashtable_ptr_type(aug_extension, vm->lib_extensions, func_name);
                if(extension == NULL )
                {                    
                    extension = aug_hashtable_ptr_type(aug_extension, vm->extensions, func_name);
                    if(extension == NULL || extension->func == NULL)
                    {
                        aug_log_vm_error(vm, "Extension function %s not registered", func_name_value->str->buffer);
                        aug_decref(func_name_value);
                        break;
                    }
                }

                // Gather arguments
                const int arg_count = aug_vm_read_int(vm);
                aug_value* args = (aug_value*)AUG_ALLOC(sizeof(aug_value) * (size_t)arg_count);
                for(int i = arg_count - 1; i >= 0; --i)
                {
                    aug_value* arg = aug_vm_pop(vm);
                    if(arg != NULL)
                    {
                        aug_value value = aug_none();
                        aug_move(&value, arg);
                        args[arg_count - i - 1] = value;
                    }
                }

                // Call the function, Move return value on to top of stack
                aug_value ret_value = extension->func(arg_count, args);

                // Cleanup argumentsl
                aug_decref(func_name_value);
                for (int i = 0; i < arg_count; ++i)
                    aug_decref(&args[i]);
                AUG_FREE(args);
                
                // Return on top
                aug_value* top = aug_vm_push(vm);
                if(top)
                    aug_move(top, &ret_value);
                break;
            }
            case AUG_OPCODE_ARG_COUNT:
            {                
                vm->arg_count = aug_vm_read_int(vm);
                break;
            }
            case AUG_OPCODE_ENTER_FUNC:
            {                
                const int param_count = aug_vm_read_int(vm);
                if (vm->arg_count != param_count)
                {
                    aug_string* symbol = aug_vm_get_marker_symbol(vm);
                    aug_log_vm_error(vm, "Incorrect number of arguments passed to %s. Received %d expected %d ", 
                        symbol ? symbol->buffer : "anonymous", vm->arg_count, param_count);
                    break;
                }
                break;
            }
            case AUG_OPCODE_RETURN_FUNC:
            {
                // get func return value
                aug_value* ret_value = aug_vm_pop(vm);
                
                // Free locals
                const int delta = aug_vm_read_int(vm);
                for(int i = 0; i < delta; ++i)
                    aug_decref(aug_vm_pop(vm));
                
                // Restore base index
                aug_value* ret_base = aug_vm_pop(vm);
                if(ret_base == NULL)
                {                    
                    aug_log_vm_error(vm, "Calling frame setup incorrectly. Stack missing stack base");
                    break;
                }

                vm->base_index = ret_base->i;
                aug_decref(ret_base);

                // jump to return instruction
                aug_value* ret_addr = aug_vm_pop(vm);
                if(ret_addr == NULL)
                {                    
                    aug_log_vm_error(vm, "Calling frame setup incorrectly. Stack missing return address");
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
                    aug_move(top, ret_value);

                break;
            }
            case AUG_OPCODE_IMPORT_LIB:
            {
                aug_vm_lib_load(vm, aug_vm_read_bytes(vm));
                break;
            }
            default:
                assert(0);
            break;
        }

#if AUG_DEBUG
        if(vm->debug_post_instruction)
            vm->debug_post_instruction(vm, opcode);
#endif // AUG_DEBUG
    }

    vm->running = false; 
}

aug_value aug_vm_execute_from_frame(aug_vm* vm, int func_addr, int argc, aug_value* args)
{
    // Manually set expected call frame
    aug_vm_push_call_frame(vm, AUG_OPCODE_INVALID);

    vm->arg_count = argc; // setup expected argument count 

    // Jump to function call
    vm->instruction = vm->bytecode + func_addr;

    for(int i = 0; i < argc; ++i)
    {
        aug_value* value = aug_vm_push(vm);
        if(value)
            *value = args[i];
    }

    vm->base_index = vm->stack_index;
    aug_vm_execute(vm);

    aug_value ret_value = aug_none();
    if(vm->stack_index >= 1)
    {
        // If stack is valid, get the pushed value
        aug_value* top = aug_vm_pop(vm);
        if(top)
            ret_value = *top;
    }
    return ret_value;
}

// COMPILER ============================================== COMPILER ========================================== COMPILER // 

void aug_generate_ir_pass(const aug_ast* node, aug_ir* ir, aug_input* input);

void aug_generate_ir_prepass(const aug_ast* node, aug_ir* ir, aug_input* input)
{
    // This is a statement prepass. Will gather globals and handle use calls
    if(node == NULL || !ir->valid || input == NULL)
        return;

    const aug_token token = node->token;
    aug_string* token_data = token.data; 
    aug_ast** children = node->children;
    const int children_size = node->children_size;

    switch(node->type)
    {
        case AUG_AST_ROOT:
        case AUG_AST_BLOCK: 
        {
            for(int i = 0; i < children_size; ++ i)
                aug_generate_ir_prepass(children[i], ir, input);
            break;
        }
        case AUG_AST_STMT_DEFINE_FUNC: 
        {
            assert(token_data != NULL && children_size == 2); //func token [0] {[1]};

            aug_ast* params = children[0];
            assert(params && params->type == AUG_AST_PARAM_LIST);
            const int param_count = params->children_size;

            if(!aug_ir_set_func(ir, token_data, param_count, false))
            {
                ir->valid = false;
                aug_log_input_error_at(input, &token.pos, "Global %s already defined", token_data->buffer);
                break;
            }
            break;
        }
        case AUG_AST_IMPORT_LIB:
        {
            const aug_ir_operand operand = aug_ir_operand_from_str(token_data);
            aug_ir_add_operation_arg(ir, AUG_OPCODE_IMPORT_LIB, operand);
            break;
        } 
        case AUG_AST_IMPORT_SCRIPT:
        {
            // TODO: open and parse script, should be relative to the current input. (i.e. trunk filename, add relative path and get absolute?) 
            aug_string* imported_filename = aug_string_create(input->filename->buffer);
            size_t dir_pos = imported_filename->length;
            while(--dir_pos != 0)
            {
                if(imported_filename->buffer[dir_pos] == '/' || imported_filename->buffer[dir_pos] == '\\')
                {
                    ++dir_pos;
                    break;
                }
            }

            for(size_t i = 0; i <= token_data->length; ++i) // <= note include null term
            {
                if(i+dir_pos >= imported_filename->length)
                    aug_string_push(imported_filename, token_data->buffer[i]);
                else
                    aug_string_set(imported_filename, i+dir_pos, token_data->buffer[i]);
            }

            // Parse file
            aug_input* imported_input = aug_input_open(imported_filename->buffer, input->error_func);
            aug_ast* root = aug_parse(imported_input);
            if (root == NULL)
            {
                aug_string_decref(imported_filename);
                aug_input_close(imported_input);
                aug_log_input_error_at(input, &token.pos, "Failed to use script %s", token_data->buffer);
                break;
            }

            // Generate IR / 
            // TODO: fix memory leak error with symbols being improperly tracked when freed from ast and held by the user IR
            aug_generate_ir_pass(root, ir, imported_input);

            aug_string_decref(imported_filename);
            aug_input_close(imported_input);
            aug_ast_delete(root);
            break;  
        } 
        default: 
            break;
    }
}

void aug_generate_ir_pass(const aug_ast* node, aug_ir* ir, aug_input* input)
{
    if(node == NULL || !ir->valid)
        return;

    const aug_token token = node->token;
    aug_string* token_data = token.data; 
    aug_ast** children = node->children;
    const int children_size = node->children_size;

    switch(node->type)
    {
        case AUG_AST_ROOT:
        {
            // gather globals, uses etc..
            aug_generate_ir_prepass(node, ir, input);

            for(int i = 0; i < children_size; ++ i)
                aug_generate_ir_pass(children[i], ir, input);
            break;
        }
        case AUG_AST_BLOCK: 
        {
            for(int i = 0; i < children_size; ++ i)
                aug_generate_ir_pass(children[i], ir, input);

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
                    const aug_ir_operand operand = aug_ir_operand_from_char(data);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_CHAR, operand);
                    break;
                }
                case AUG_TOKEN_INT:
                {
                    assert(token_data && token_data->length > 0);
                    const int data = (int)(strtol(token_data->buffer, NULL, 10));
                    const aug_ir_operand operand = aug_ir_operand_from_int(data);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_INT, operand);
                    break;
                }
                case AUG_TOKEN_HEX:
                {
                    assert(token_data && token_data->length > 2 && token_data->buffer[1] == 'x'); 
                    const int data = (int)(strtoul(token_data->buffer + 2, NULL, 16)); // +2 skip 0x 
                    const aug_ir_operand operand = aug_ir_operand_from_int(data);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_INT, operand);
                    break;
                }
                case AUG_TOKEN_BINARY:
                {
                    assert(token_data && token_data->length > 2 && token_data->buffer[1] == 'b');
                    const int data = (int)(strtoul(token_data->buffer + 2, NULL, 2));// +2 skip 0b 
                    const aug_ir_operand operand = aug_ir_operand_from_int(data);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_INT, operand);
                    break;
                }
                case AUG_TOKEN_FLOAT:
                {
                    assert(token_data && token_data->length > 0);
                    const float data = strtof(token_data->buffer, NULL);
                    const aug_ir_operand operand = aug_ir_operand_from_float(data);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_FLOAT, operand);
                    break;
                }
                case AUG_TOKEN_STRING:
                {
                    const aug_ir_operand operand = aug_ir_operand_from_str(token_data);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_STRING, operand);
                    break;
                }
                case AUG_TOKEN_TRUE:
                {
                    const aug_ir_operand operand = aug_ir_operand_from_bool(true);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_BOOL, operand);
                    break;
                }
                case AUG_TOKEN_FALSE:
                {
                    const aug_ir_operand operand = aug_ir_operand_from_bool(false);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_BOOL, operand);
                    break;
                }
                case AUG_TOKEN_NONE:
                {
                    aug_ir_add_operation(ir, AUG_OPCODE_PUSH_NONE);
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

            const aug_symbol symbol = aug_ir_get_symbol_relative(ir, token_data);
            if(symbol.type == AUG_SYM_NONE)
            {
                ir->valid = false;
                aug_log_input_error_at(input, &token.pos, "Variable %s not defined in current block", 
                    token_data->buffer);
                return;
            }

            aug_ir_mark_symbol(ir, symbol);

            if(symbol.type == AUG_SYM_FUNC)
            {
                //TODO: create function type
                const aug_ir_operand address_operand = aug_ir_operand_from_symbol(symbol);
                aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_FUNC, address_operand);
                break;
            }

            const aug_ir_operand address_operand = aug_ir_operand_from_symbol(symbol);
            if(symbol.scope == AUG_SYM_SCOPE_GLOBAL)
                aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_GLOBAL, address_operand);
            else // if local or param
                aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_LOCAL, address_operand);
            break;
        }
        case AUG_AST_UNARY_OP:
        {
            assert(children_size == 1); // token [0]

            aug_generate_ir_pass(children[0], ir, input);

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

            aug_token_id id = token.id;
            if(id != AUG_TOKEN_ASSIGN) // special condition, assignment handles lhs via the addr/element
                aug_generate_ir_pass(children[0], ir, input); // LHS
            aug_generate_ir_pass(children[1], ir, input); // RHS

            aug_ir_mark_source(ir, input->filename, token.pos);

            switch (token.id)
            {
                case AUG_TOKEN_ADD:        aug_ir_add_operation(ir, AUG_OPCODE_ADD);      break;
                case AUG_TOKEN_SUB:        aug_ir_add_operation(ir, AUG_OPCODE_SUB);      break;
                case AUG_TOKEN_MUL:        aug_ir_add_operation(ir, AUG_OPCODE_MUL);      break;
                case AUG_TOKEN_DIV:        aug_ir_add_operation(ir, AUG_OPCODE_DIV);      break;
                case AUG_TOKEN_MOD:        aug_ir_add_operation(ir, AUG_OPCODE_MOD);      break;
                case AUG_TOKEN_POW:        aug_ir_add_operation(ir, AUG_OPCODE_POW);      break;
                case AUG_TOKEN_AND:        aug_ir_add_operation(ir, AUG_OPCODE_AND);      break;
                case AUG_TOKEN_OR:         aug_ir_add_operation(ir, AUG_OPCODE_OR);       break;
                case AUG_TOKEN_LT:         aug_ir_add_operation(ir, AUG_OPCODE_LT);       break;
                case AUG_TOKEN_LT_EQ:      aug_ir_add_operation(ir, AUG_OPCODE_LTE);      break;
                case AUG_TOKEN_GT:         aug_ir_add_operation(ir, AUG_OPCODE_GT);       break;
                case AUG_TOKEN_GT_EQ:      aug_ir_add_operation(ir, AUG_OPCODE_GTE);      break;
                case AUG_TOKEN_EQ:         aug_ir_add_operation(ir, AUG_OPCODE_EQ);       break;
                case AUG_TOKEN_NOT_EQ:     aug_ir_add_operation(ir, AUG_OPCODE_NEQ);      break;
                case AUG_TOKEN_APPROX_EQ:  aug_ir_add_operation(ir, AUG_OPCODE_APPROXEQ); break;
                case AUG_TOKEN_ADD_ASSIGN: aug_ir_add_operation(ir, AUG_OPCODE_ADD); id = AUG_TOKEN_ASSIGN; break;
                case AUG_TOKEN_SUB_ASSIGN: aug_ir_add_operation(ir, AUG_OPCODE_SUB); id = AUG_TOKEN_ASSIGN; break;
                case AUG_TOKEN_MUL_ASSIGN: aug_ir_add_operation(ir, AUG_OPCODE_MUL); id = AUG_TOKEN_ASSIGN; break;
                case AUG_TOKEN_DIV_ASSIGN: aug_ir_add_operation(ir, AUG_OPCODE_DIV); id = AUG_TOKEN_ASSIGN; break;
                case AUG_TOKEN_MOD_ASSIGN: aug_ir_add_operation(ir, AUG_OPCODE_MOD); id = AUG_TOKEN_ASSIGN; break;
                case AUG_TOKEN_POW_ASSIGN: aug_ir_add_operation(ir, AUG_OPCODE_POW); id = AUG_TOKEN_ASSIGN; break;
                default: break;
            }

            if(id == AUG_TOKEN_ASSIGN)
            {
                aug_ast* var_node = children[0];

                // LHS must be an element of variable
                if(var_node->type != AUG_AST_VARIABLE && var_node->type != AUG_AST_ELEMENT)
                {
                    ir->valid = false;
                    aug_log_input_error_at(input, &token.pos, "Left hand of assignment must be a variable or element");
                    break;
                }

                if(var_node->type == AUG_AST_VARIABLE)
                {
                    aug_string* var_token_data = var_node->token.data;
                    assert(var_token_data);

                    const aug_symbol symbol = aug_ir_get_symbol_relative(ir, var_token_data);
                    if(symbol.type == AUG_SYM_NONE)
                    {
                        ir->valid = false;
                        aug_log_input_error_at(input, &token.pos, "Variable %s not defined", var_token_data->buffer);
                        break;
                    }
                    if(symbol.type == AUG_SYM_FUNC)
                    {
                        ir->valid = false;
                        aug_log_input_error_at(input, &token.pos, "Can not assign function %s to a value", var_token_data->buffer);
                        break;
                    }

                    aug_ir_mark_symbol(ir, symbol);

                    const aug_ir_operand address_operand = aug_ir_operand_from_symbol(symbol);
                    if(symbol.scope == AUG_SYM_SCOPE_GLOBAL)
                        aug_ir_add_operation_arg(ir, AUG_OPCODE_LOAD_GLOBAL, address_operand);
                    else // if local or param
                        aug_ir_add_operation_arg(ir, AUG_OPCODE_LOAD_LOCAL, address_operand);
                }
                else if(var_node->type == AUG_AST_ELEMENT)
                {
                    assert(var_node->children_size == 2); // 0[1]
                    aug_generate_ir_pass(var_node->children[0], ir, input); // push index expr
                    aug_generate_ir_pass(var_node->children[1], ir, input); // push container                    
                    
                    aug_ir_mark_source(ir, input->filename, token.pos);
                    aug_ir_add_operation(ir, AUG_OPCODE_LOAD_ELEMENT);
                }
            }
            break;
        }
        case AUG_AST_ARRAY:
        {
            for(int i = children_size - 1; i >= 0; --i)
                aug_generate_ir_pass(children[i], ir, input);

            const aug_ir_operand count_operand = aug_ir_operand_from_int(children_size);
            aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_ARRAY, count_operand);
            break;
        }
        case AUG_AST_MAP:
        {
            int i;
            for (i = children_size - 1; i >= 0; --i)
                aug_generate_ir_pass(children[i], ir, input);

            const aug_ir_operand count_operand = aug_ir_operand_from_int(children_size);
            aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_MAP, count_operand);
            break;
        }
        case AUG_AST_MAP_PAIR:
        {
            assert(children_size == 2); // 0[1]
            aug_generate_ir_pass(children[0], ir, input); // push key
            aug_generate_ir_pass(children[1], ir, input); // push value
            break;
        }
        case AUG_AST_ELEMENT:
        {
            assert(children_size == 2); // 0[1]
            aug_generate_ir_pass(children[0], ir, input); // push index expr
            aug_generate_ir_pass(children[1], ir, input); // push container

            aug_ir_mark_source(ir, input->filename, token.pos);
            aug_ir_add_operation(ir, AUG_OPCODE_PUSH_ELEMENT);
            break;
        }
        case AUG_AST_RANGE:
        {
            assert(children_size == 2); // 0[1]
            aug_generate_ir_pass(children[0], ir, input); // push from 
            aug_generate_ir_pass(children[1], ir, input); // push to

            aug_ir_mark_source(ir, input->filename, token.pos);
            aug_ir_add_operation(ir, AUG_OPCODE_PUSH_RANGE);
            break;
        }
        case AUG_AST_STMT_EXPR:
        {
            int i;
            for( i = 0; i < children_size; ++i)
                aug_generate_ir_pass(children[i], ir, input);
            break;
        }
        case AUG_AST_DISCARD:
        {
            // if evaluating an input string, do not discard global return values. These may be returned to user
            if(input->str != NULL)
            {
                bool global = aug_ir_current_scope_is_global(ir); 
                if(!global)
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_POP, aug_ir_operand_from_int(1));
            }
            else //executing a file, discard all unused return values
            {
                aug_ir_add_operation_arg(ir, AUG_OPCODE_POP, aug_ir_operand_from_int(1));
            }
            break;
        }
        case AUG_AST_STMT_DEFINE_VAR:
        {
            assert(token_data != NULL);

            if(children_size == 1) // token = [0]
                aug_generate_ir_pass(children[0], ir, input);
            else
                aug_ir_add_operation(ir, AUG_OPCODE_PUSH_NONE);

            // Get variable in the current block. If it exists, error out. If it does not exist, set in table
            aug_symbol symbol = aug_ir_get_symbol_local(ir, token_data);
            if(symbol.type != AUG_SYM_NONE)
            {
                ir->valid = false;
                aug_log_input_error_at(input, &token.pos, "Variable %s already defined in block", token_data->buffer);
                break;
            }

            aug_ir_set_var(ir, token_data);
            break;
        }
        case AUG_AST_STMT_IF:
        {
            assert(children_size == 2); //if([0]) {[1]}

            // Evaluate expression. 
            aug_generate_ir_pass(children[0], ir, input);

            //Jump to end if false
            const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);
            const size_t end_block_jmp = aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP_ZERO, stub_operand);

            // True block
            aug_ir_push_scope(ir);
            aug_generate_ir_pass(children[1], ir, input);
            aug_ir_pop_scope(ir);

            const int end_block_addr = ir->bytecode_offset;

            // Fixup stubbed block offsets
            aug_ir_get_operation(ir, end_block_jmp)->operand = aug_ir_operand_from_int(end_block_addr);

            break;
        }
        case AUG_AST_STMT_IF_ELSE:
        {
            assert(children_size == 3); //if([0]) {[1]} else {[2]}

            // Evaluate expression. 
            aug_generate_ir_pass(children[0], ir, input);

            //Jump to else if false
            const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);
            const size_t else_block_jmp = aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP_ZERO, stub_operand);

            // True block
            aug_ir_push_scope(ir);
            aug_generate_ir_pass(children[1], ir, input);
            aug_ir_pop_scope(ir);

            //Jump to end after true
            const size_t end_block_jmp = aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP, stub_operand);
            const int else_block_addr = ir->bytecode_offset;

            // Else block
            aug_ir_push_scope(ir);
            aug_generate_ir_pass(children[2], ir, input);
            aug_ir_pop_scope(ir);

            // Tag end address
            const int end_block_addr = ir->bytecode_offset;

            // Fixup stubbed block offsets
            aug_ir_get_operation(ir, else_block_jmp)->operand = aug_ir_operand_from_int(else_block_addr);
            aug_ir_get_operation(ir, end_block_jmp)->operand = aug_ir_operand_from_int(end_block_addr);

            break;
        }
        case AUG_AST_STMT_WHILE:
        {
            assert(children_size == 2); //while [0] {[1]}

            aug_ir_begin_loop(ir);

            // Evaluate expression. 
            aug_generate_ir_pass(children[0], ir, input);
            aug_ir_check_loop(ir);

            // Loop block
            aug_ir_push_scope(ir);
            aug_generate_ir_pass(children[1], ir, input);
            aug_ir_pop_scope(ir);

            aug_ir_end_loop(ir);
            break;
        }
        case AUG_AST_STMT_FOR:
        {
            assert(children_size == 3); //for [0] in [1] {[2]}
            assert(children[0] && children[0]->type == AUG_AST_VARIABLE);

            // Evaluate and initialize iterable expression. 
            aug_ir_scope* scope = aug_ir_current_scope(ir);
            int it_offset = scope->stack_offset++;
            it_offset = aug_ir_current_frame_local_offset(ir, it_offset, 0);
            aug_generate_ir_pass(children[1], ir, input);

            aug_ir_mark_source(ir, input->filename, token.pos);
            aug_ir_add_operation(ir, AUG_OPCODE_PUSH_ITERATOR);

            // Top of the loop.
            aug_ir_begin_loop(ir);

            aug_ir_push_scope(ir);

            // initialize the variable
            aug_string* var_token_data = children[0]->token.data; 
            aug_ir_set_var(ir, var_token_data);
            aug_ir_add_operation(ir, AUG_OPCODE_PUSH_NONE); // initialize the slot for the var

            aug_symbol var_symbol = aug_ir_get_symbol_relative(ir, var_token_data);

            //Move the iterator, and check the loop
            aug_ir_add_operation_arg(ir, AUG_OPCODE_ITERATE, aug_ir_operand_from_int(it_offset));
            aug_ir_check_loop(ir);

            // If success, the iterator pushes the element value, load this into local
            aug_ir_add_operation_arg(ir, AUG_OPCODE_LOAD_LOCAL, aug_ir_operand_from_symbol(var_symbol));

            // Loop block
            aug_generate_ir_pass(children[2], ir, input);

            aug_ir_pop_scope(ir);

            aug_ir_end_loop(ir);

            // pop the temporary iterator and iteration temp slot to restore stack (2)
            aug_ir_add_operation_arg(ir, AUG_OPCODE_POP, aug_ir_operand_from_int(2));

            scope = aug_ir_current_scope(ir);
            scope->stack_offset--; // remove iterator from stack
            break;
        }
        case AUG_AST_BREAK:
        {
            if(!aug_ir_break_loop(ir))
            {
                ir->valid = false;
                aug_log_input_error_at(input, &token.pos, "Break statement must be inside loop");
            }
            break;
        }
        case AUG_AST_CONTINUE:
        {
            if(!aug_ir_continue_loop(ir))
            {
                ir->valid = false;
                aug_log_input_error_at(input, &token.pos, "Continue statement must be inside loop");
            }
            break;
        }
        case AUG_AST_FUNC_CALL:
        {
            assert(token_data != NULL); // func name is token data
            const int arg_count = children_size;

            const aug_symbol symbol = aug_ir_get_symbol(ir, token_data);
            if(symbol.type != AUG_SYM_NONE)
            {
                // If the symbol is a user defined function, check arg count.
                if(symbol.type == AUG_SYM_FUNC && symbol.argc != arg_count)
                {
                    ir->valid = false;
                    aug_log_input_error_at(input, &token.pos, "Function Call %s passed %d arguments, expected %d", 
                        token_data->buffer, arg_count, symbol.argc);
                }
                else
                {
                    // offset to account for the pushed base
                    size_t push_frame = aug_ir_add_operation_arg(ir, AUG_OPCODE_CALL_FRAME, aug_ir_operand_from_int(0));

                    // Push arguments onto stack
                    int i;
                    for(i = 0; i < children_size; ++ i)
                        aug_generate_ir_pass(children[i], ir, input);

                    // TODO: Push arg count, and have the function body check that arg count matches in the vm runtime
                    aug_ir_operand arg_count = aug_ir_operand_from_int(children_size);
                    aug_ir_add_operation_arg(ir, AUG_OPCODE_ARG_COUNT, arg_count);
                                        
                    if(symbol.type == AUG_SYM_FUNC)  
                    {
                        aug_ir_mark_symbol(ir, symbol);

                        aug_ir_operand address_operand = aug_ir_operand_from_symbol(symbol);
                        aug_ir_add_operation_arg(ir, AUG_OPCODE_CALL, address_operand);
                    }
                    if(symbol.type == AUG_SYM_VAR)
                    {
                        const aug_symbol var_symbol = aug_ir_get_symbol_relative(ir, token_data);
                        aug_ir_mark_symbol(ir, var_symbol);

                        aug_ir_operand address_operand = aug_ir_operand_from_symbol(var_symbol);
                        if(var_symbol.scope == AUG_SYM_SCOPE_GLOBAL)
                            aug_ir_add_operation_arg(ir, AUG_OPCODE_CALL_GLOBAL, address_operand);
                        else // if local or param
                            aug_ir_add_operation_arg(ir, AUG_OPCODE_CALL_LOCAL, address_operand);
                    }
                    
                    // fixup the return address to after the call
                    aug_ir_get_operation(ir, push_frame)->operand = aug_ir_operand_from_int(ir->bytecode_offset);
                }
                break;
            }

            for(int i = arg_count - 1; i >= 0; --i)
                aug_generate_ir_pass(children[i], ir, input);

            // push func name then call ext
            aug_ir_add_operation_arg(ir, AUG_OPCODE_PUSH_STRING, aug_ir_operand_from_str(token_data));
            aug_ir_add_operation_arg(ir, AUG_OPCODE_CALL_EXT, aug_ir_operand_from_int(arg_count));
            break;
        }
        case AUG_AST_FUNC_CALL_UNNAMED:
        {
            assert(children_size >= 1); 
            const int arg_count = children_size - 1; // off by one since first child is the value to push onto the stack to be called

            // offset to account for the pushed base
            size_t push_frame = aug_ir_add_operation_arg(ir, AUG_OPCODE_CALL_FRAME, aug_ir_operand_from_int(0));

            // Push arguments onto stack
            for(int i = 1; i < children_size; ++i)
                aug_generate_ir_pass(children[i], ir, input);

            // First arg is the value of the function to call 
            aug_generate_ir_pass(children[0], ir, input);

            // TODO: Push arg count, and have the function body check that arg count matches in the vm runtime
            aug_ir_add_operation_arg(ir, AUG_OPCODE_ARG_COUNT, aug_ir_operand_from_int(arg_count));
            aug_ir_add_operation(ir, AUG_OPCODE_CALL_TOP);

            // fixup the return address to after the call
            aug_ir_get_operation(ir, push_frame)->operand = aug_ir_operand_from_int(ir->bytecode_offset);

            break;
        }
        case AUG_AST_RETURN:
        {
            const aug_ir_operand offset = aug_ir_operand_from_int(aug_ir_calling_offset(ir));
            if(children_size == 1) //return [0];
            {
                aug_generate_ir_pass(children[0], ir, input);
                aug_ir_add_operation_arg(ir, AUG_OPCODE_RETURN_FUNC, offset);
            }
            else
            {
                aug_ir_add_operation(ir, AUG_OPCODE_PUSH_NONE);
                aug_ir_add_operation_arg(ir, AUG_OPCODE_RETURN_FUNC, offset);
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
            for(int i = 0; i < children_size; ++ i)
                aug_generate_ir_pass(children[i], ir, input);
            break;
        }
        case AUG_AST_STMT_DEFINE_FUNC:
        {
            assert(token_data != NULL && children_size == 2); //func token [0] {[1]};

            // Jump over the func def
            const aug_ir_operand stub_operand = aug_ir_operand_from_int(0);
            const size_t end_block_jmp = aug_ir_add_operation_arg(ir, AUG_OPCODE_JUMP, stub_operand);

            aug_ast* params = children[0];
            assert(params && params->type == AUG_AST_PARAM_LIST);
            const int param_count = params->children_size;

            // Gather global pass handles global symbol table. The offset however is incorrect, this pass will fixup the function offset  
            bool update = aug_ir_current_scope_is_global(ir); 

            // Try to set function in symbol table. Func name is token
            if(!aug_ir_set_func(ir, token_data, param_count, update))
            {
                ir->valid = false;
                aug_log_input_error_at(input, &token.pos, "Function %s already defined", token_data->buffer);
                break;
            }

            // Parameter frame
            aug_ir_push_scope(ir);
            aug_generate_ir_pass(children[0], ir, input);

            aug_ir_mark_symbol(ir, aug_ir_get_symbol(ir, token_data));

            // TODO: Argument count check
            aug_ir_operand param_count_arg = aug_ir_operand_from_int(param_count);
            aug_ir_add_operation_arg(ir, AUG_OPCODE_ENTER_FUNC, param_count_arg);

            // Function block frame
            aug_ir_push_frame(ir, param_count);
            aug_generate_ir_pass(children[1], ir, input);

            // Ensure there is a return
            if(aug_ir_last_operation(ir)->opcode != AUG_OPCODE_RETURN_FUNC)
            {
                const aug_ir_operand offset = aug_ir_operand_from_int(aug_ir_calling_offset(ir));
                aug_ir_add_operation(ir, AUG_OPCODE_PUSH_NONE);
                aug_ir_add_operation_arg(ir, AUG_OPCODE_RETURN_FUNC, offset);
            }

            aug_ir_pop_frame(ir);
            aug_ir_pop_scope(ir);

            // fixup jump operand
            const int end_block_addr = ir->bytecode_offset;
            aug_ir_get_operation(ir, end_block_jmp)->operand = aug_ir_operand_from_int(end_block_addr);

            break;
        }
        default:
            break;
    }
}

aug_ir* aug_generate_ir(aug_ast* root, aug_input* input)
{
    if(root == NULL || input == NULL)
        return NULL;

    // Generate IR
    aug_ir* ir = aug_ir_new();

    aug_ir_push_frame(ir, 0);  // push global frame
    aug_generate_ir_pass(root, ir, input);
    
    // Signal to VM to exit
    aug_ir_add_operation(ir, AUG_OPCODE_EXIT);
    aug_ir_pop_frame(ir); // pop global frame
    return ir;
}

char* aug_generate_bytecode(aug_ir* ir)
{  
    assert(ir != NULL && ir->operations != NULL);

    if (!ir->valid)
        return NULL;

    assert(ir->bytecode_offset > 0);
    char* bytecode = (char*)AUG_ALLOC( sizeof(char) * (size_t)ir->bytecode_offset);
    char* instruction = bytecode;

    for(size_t i = 0; i < ir->operations->length; ++i)
    {
        aug_ir_operation operation = aug_container_at_type(aug_ir_operation, ir->operations, i);
        aug_ir_operand operand = operation.operand;

        // push operation opcode
        (*instruction++) = (char)operation.opcode;

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
            for(size_t i = 0; operand.data.str[i] != '\0'; ++i)
                *(instruction++) = operand.data.str[i];
            *(instruction++) = 0; // null terminate
            break;
        case AUG_IR_OPERAND_SYMBOL:
            aug_symbol* symbol = aug_hashtable_ptr_type(aug_symbol, ir->globals, operand.data.str);
            assert(symbol);
            operand.data.i = symbol->offset;
            for(size_t i = 0; i < aug_ir_operand_size(operand); ++i)
                *(instruction++) = operand.data.bytes[i];
            break;
        }
    }
    assert((int)(instruction - bytecode) == ir->bytecode_offset);
    return bytecode;
}

// STRING ================================================= STRING ============================================ STRING // 

aug_string* aug_string_new(size_t size) 
{
	aug_string* string = (aug_string*)AUG_ALLOC(sizeof(aug_string));
	string->ref_count = 1;
	string->length = 0;
	string->capacity = size;
	string->buffer = (char*)AUG_ALLOC(sizeof(char)*string->capacity);
	return string;
}

aug_string* aug_string_create(const char* bytes) 
{
	aug_string* string = (aug_string*)AUG_ALLOC(sizeof(aug_string));
	string->ref_count = 1;
	string->length = strlen(bytes);
	string->capacity = string->length + 1;
	string->buffer = (char*)AUG_ALLOC(sizeof(char)*string->capacity);

#ifdef AUG_SECURE
    strcpy_s(string->buffer, string->length+1, bytes);
#else
    strcpy(string->buffer, bytes);
#endif
    return string;
}

void aug_string_resize(aug_string* string, size_t size) 
{
	string->capacity = size;
    string->buffer = (char*)AUG_REALLOC(string->buffer, sizeof(char)*string->capacity);
}

void aug_string_push(aug_string* string, char c) 
{
	if(string->length + 1 >= string->capacity) 
        aug_string_resize(string, 2 * string->capacity);
    string->buffer[string->length++] = c;
    string->buffer[string->length] = '\0';
}

char aug_string_pop(aug_string* string) 
{
	return string->length > 0 ? string->buffer[--string->length] : -1;
}

void aug_string_append(aug_string* a, const aug_string* b)
{
    if(a != NULL && b != NULL && b->buffer != NULL)
        aug_string_append_bytes(a, b->buffer, b->length);
}

void aug_string_append_bytes(aug_string* string, const char* bytes, size_t len)
{
    if(string->length + len >= string->capacity) 
        aug_string_resize(string, string->capacity + len * 2);

    for(size_t i = 0; i < len; ++i)
        string->buffer[string->length++] = bytes[i];
    string->buffer[string->length] = '\0'; 
}

char aug_string_at(const aug_string* string, size_t index) 
{
	return index < string->length ? string->buffer[index] : -1;
}

bool aug_string_set(const aug_string* string, size_t index, char c) 
{
	if(index < string->length)
    {
        string->buffer[index] = c;
        return true;
    } 
    return false;
}

char aug_string_back(const aug_string* string) 
{
	return string->length > 0 ? string->buffer[string->length-1] : -1;
}

bool aug_string_compare(const aug_string* a, const aug_string* b) 
{
    if(a == NULL || b == NULL || a->length != b->length)
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

    for(size_t i = 0; i < a->length; ++i)
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
        AUG_FREE(string->buffer);
        AUG_FREE(string);
        return NULL;
    }
    return string;
}

// ARRAY ================================================== ARRAY ============================================== ARRAY // 

aug_array* aug_array_new(size_t size)
{                
	aug_array* array = (aug_array*)AUG_ALLOC(sizeof(aug_array));
	array->ref_count = 1;   
	array->length = 0;      
	array->capacity = size; 
	array->buffer = (aug_value*)AUG_ALLOC(sizeof(aug_value)*array->capacity);
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
        // If will be dereferenced, ensure children are as well
        for (size_t i = 0; i < array->length; ++i)
            aug_decref(aug_array_at(array, i));
        AUG_FREE(array->buffer);
        AUG_FREE(array);
        return NULL;
    }            
    return array;
}       

void aug_array_reserve(aug_array* array, size_t size)    
{
	array->capacity = size; 
	array->buffer = (aug_value*)AUG_REALLOC(array->buffer, sizeof(aug_value)*array->capacity);
}

void aug_array_resize(aug_array* array, size_t size)    
{
    aug_array_reserve(array, size);
    array->length = array->capacity;
    for(size_t i = 0; i < array->length; ++i)
        array->buffer[i] = aug_none();
}
 
aug_value* aug_array_push(aug_array* array)  
{
	if(array->length + 1 >= array->capacity)
        aug_array_reserve(array, array->capacity * 2); 
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

bool aug_array_set(const aug_array* array, size_t index, aug_value* value) 
{
	if(index < array->length)
    {
        aug_assign(&array->buffer[index], value); 
        return true;      
    }   
    return false;      
}
 
aug_value* aug_array_back(const aug_array* array)             
{
	return array->length > 0 ? &array->buffer[array->length-1] : NULL; 
}

bool aug_array_compare(const aug_array* a, const aug_array* b)             
{
    if(a->length != b->length)
        return false;
    for(size_t i = 0; i < a->length; ++i)
    {
        if(!aug_compare(&a->buffer[i], &b->buffer[i]))
            return false;
    }
    return true;
}

void aug_array_append(aug_array* array, aug_value* value)  
{
    aug_value* new_value = aug_array_push(array);
    if(new_value == NULL)
        return;

    *new_value = aug_none(); 
    aug_array_set(array, array->length-1, value);
}

void aug_array_remove(aug_array* array, size_t index)  
{
    aug_value* value = aug_array_at(array, index); 

    if(value == NULL)
        return;

    aug_decref(value);
    for(size_t i = index; i < array->length-1; ++i)
        array->buffer[i] = array->buffer[i+1];
    array->length--;
}

aug_array* aug_array_copy(aug_array* array)
{
    aug_array* new_array = aug_array_new(array->length);
    for(size_t i = 0; i < array->length; ++i)
        aug_array_append(new_array, aug_array_at(array, i));
    return new_array;
}

// MAP ==================================================== MAP =================================================== MAP //

#ifndef AUG_MAP_SIZE_DEFAULT
#define AUG_MAP_SIZE_DEFAULT 1
#endif//AUG_MAP_SIZE_DEFAULT

#ifndef AUG_MAP_BUCKET_SIZE_DEFAULT
#define AUG_MAP_BUCKET_SIZE_DEFAULT 1
#endif//AUG_MAP_BUCKET_SIZE_DEFAULT

typedef struct aug_map_bucket
{
    aug_value* keys;
    aug_value* values;
    size_t capacity;
} aug_map_bucket;

bool aug_map_can_hash(const aug_value* value)
{
    switch(value->type)
    {
        case AUG_STRING:
        case AUG_INT:
            return true;
        default:
            return false;
    }
    return false;
}

size_t aug_map_hash(const aug_value* value)
{
    switch(value->type)
    {
        case AUG_STRING:
        {
            const char* bytes = value->str->buffer;
            size_t hash = 5381; // DJB2 hash
            while (*bytes)
                hash = ((hash << 5) + hash) + (size_t)(*bytes++);
            return hash;
        }
        case AUG_INT:
            return (size_t)value->i;
        default:
            return 0;
    }
    return 0;
}

void aug_map_bucket_init(aug_map* map, size_t size)
{
    for (size_t i = 0; i < map->capacity; ++i)
    {
        aug_map_bucket* bucket = &map->buckets[i];
        bucket->capacity = size;
        bucket->keys = (aug_value*)AUG_ALLOC(sizeof(aug_value) * size);
        bucket->values = (aug_value*)AUG_ALLOC(sizeof(aug_value) * size);
        for(size_t j = 0; j < bucket->capacity; ++j)
            bucket->keys[j] = aug_none();
    }
}

aug_value* aug_map_bucket_insert(aug_map* map, aug_map_bucket* bucket, aug_value* key)
{
    size_t i;
    for (i = 0; i < bucket->capacity; ++i)
    {
        if (bucket->keys[i].type == AUG_NONE)
            break;

        if (aug_compare(&bucket->keys[i], key))
            return NULL;
    }

    if (i >= bucket->capacity)
    {
        size_t new_size = 2 * bucket->capacity;
        bucket->keys = (aug_value*)AUG_REALLOC(bucket->keys, sizeof(aug_value) * new_size);
        bucket->values = (aug_value*)AUG_REALLOC(bucket->values, sizeof(aug_value) * new_size);

        size_t j; // init new entries to null 
        for (j = bucket->capacity; j < new_size; ++j)
            bucket->keys[j] = aug_none();
        bucket->capacity = new_size;
    }

    ++map->count;

    bucket->keys[i] = *key;
    aug_incref(&bucket->keys[i]);

    return &bucket->values[i];
}

aug_value* aug_map_bucket_insert_or_update(aug_map* map, aug_map_bucket* bucket, aug_value* key, aug_value* value)
{
    size_t i;
    for (i = 0; i < bucket->capacity; ++i)
    {
        if (bucket->keys[i].type == AUG_NONE)
            break;

        if (aug_compare(&bucket->keys[i], key))
        {
            aug_move(&bucket->values[i], value);
            return &bucket->values[i];
        }
        return NULL;
    }

    if (i >= bucket->capacity)
    {
        size_t new_size = 2 * bucket->capacity;
        bucket->keys = (aug_value*)AUG_REALLOC(bucket->keys, sizeof(aug_value) * new_size);
        bucket->values = (aug_value*)AUG_REALLOC(bucket->values, sizeof(aug_value) * new_size);

        size_t j; // init new entries to null 
        for (j = bucket->capacity; j < new_size; ++j)
            bucket->keys[j] = aug_none();
        bucket->capacity = new_size;
    }

    ++map->count;

    bucket->keys[i] = *key;
    aug_incref(&bucket->keys[i]);

    return &bucket->values[i];
}

aug_map* aug_map_new(size_t size)
{
    aug_map* map = (aug_map*)AUG_ALLOC(sizeof(aug_map));
    map->capacity = size;
    map->ref_count = 1;
    map->count = 0;
    map->buckets = (aug_map_bucket*)AUG_ALLOC(sizeof(aug_map_bucket) * size);
    aug_map_bucket_init(map, AUG_MAP_BUCKET_SIZE_DEFAULT);
    return map;
}

void aug_map_resize(aug_map* map, size_t size)
{
    size_t old_size = map->capacity;
    aug_map_bucket* old_buckets = map->buckets;

    map->capacity = size;
    map->buckets = (aug_map_bucket*)AUG_ALLOC(sizeof(aug_map_bucket) * map->capacity);
    aug_map_bucket_init(map, AUG_MAP_BUCKET_SIZE_DEFAULT);

    // reindex all values, copy over raw data 
    for (size_t i = 0; i < old_size; ++i)
    {
        aug_map_bucket* old_bucket = &old_buckets[i];
        for (size_t j = 0; j < old_bucket->capacity; ++j)
        {
            aug_value* key = &old_bucket->keys[j];
            if (old_bucket->keys[j].type != AUG_NONE)
            {
                aug_value* value = &old_bucket->values[j];
                size_t hash = aug_map_hash(key);

                aug_map_bucket* new_bucket = &map->buckets[hash % map->capacity];
                aug_value* new_value = aug_map_bucket_insert(map, new_bucket, key);
                assert(value != NULL);
                *new_value = *value;

                aug_decref(key);
            }
        }

        AUG_FREE(old_bucket->values);
        AUG_FREE(old_bucket->keys);
    }

    AUG_FREE(old_buckets);
}

void aug_map_incref(aug_map* map)
{
    if (map)
        ++map->ref_count;
}

aug_map* aug_map_decref(aug_map* map)
{
    if (map && --map->ref_count == 0)
    {
        for (size_t i = 0; i < map->capacity; ++i)
        {
            aug_map_bucket* bucket = &map->buckets[i];
            for (size_t j = 0; j < bucket->capacity; ++j)
            {
                if (bucket->keys[j].type != AUG_NONE)
                {
                    aug_decref(&bucket->values[j]);
                    aug_decref(&bucket->keys[j]);
                    bucket->keys[j] = aug_none();
                }
            }
            AUG_FREE(bucket->values);
            AUG_FREE(bucket->keys);
        }
        AUG_FREE(map->buckets);
        AUG_FREE(map);
        return NULL;
    }
    return map;
}

bool aug_map_insert(aug_map* map, aug_value* key, aug_value* data)
{
    if(!aug_map_can_hash(key) || data == NULL)
        return false;

    size_t hash = aug_map_hash(key);
    aug_map_bucket* bucket = &map->buckets[hash % map->capacity];
    // If a bucket is larger than the map capacity, reindex all entries
    if (bucket->capacity > map->capacity)
    {
        aug_map_resize(map, map->capacity * 2);
        bucket = &map->buckets[hash % map->capacity];
    }

    aug_value* entry = aug_map_bucket_insert(map, bucket, key);
    if (entry == NULL)
        return false;

    ++map->count;
    *entry = *data;
    aug_incref(entry);
    return true;
}

bool aug_map_remove(aug_map* map, aug_value* key)
{   
    if(!aug_map_can_hash(key))
        return false;
    
    size_t hash = aug_map_hash(key);
    aug_map_bucket* bucket = &map->buckets[hash % map->capacity];
    for (size_t i = 0; i < bucket->capacity; ++i)
    {
        if (aug_compare(&bucket->keys[i], key))
        {
            aug_decref(&bucket->keys[i]);
            aug_decref(&bucket->values[i]);
            bucket->keys[i] = aug_none();
            return true;
        }
    }
    return false;
}

aug_value* aug_map_get(aug_map* map, aug_value* key)
{
    if(!aug_map_can_hash(key))
        return NULL;

    size_t hash = aug_map_hash(key);
    aug_map_bucket* bucket = &map->buckets[hash % map->capacity];
    for (size_t i = 0; i < bucket->capacity; ++i)
    {
        if (aug_compare(&bucket->keys[i], key))
        {
            return &bucket->values[i];
        }
    }
    return NULL;
}

bool aug_map_insert_or_update(aug_map* map, aug_value* key, aug_value* data)
{
    // TODO: better solution, avoid duplicate traversal from initial lookup
    aug_value* entry = aug_map_get(map, key);
    if(entry != NULL)
    {
        aug_decref(entry);
        *entry = *data;
        aug_incref(entry);
        return true;
    }

    return aug_map_insert(map, key, data);
}

void aug_map_foreach(aug_map* map, aug_map_iterator* iterator, void* user_data)
{
    if (map == NULL)
        return;

    for (size_t i = 0; i < map->capacity; ++i)
    {
        aug_map_bucket* bucket = &map->buckets[i];
        for (size_t j = 0; j < bucket->capacity; ++j)
        {
            if (bucket->keys[j].type != AUG_NONE)
                iterator(&bucket->keys[j], &bucket->values[j], user_data);
        }
    }
}

aug_iterator* aug_iterator_new(aug_value* iterable)
{
    switch(iterable->type)
    {
        case AUG_INT:
        case AUG_STRING:
        case AUG_ARRAY:
        case AUG_RANGE:
            break;
        default:
            return NULL;
    }

    aug_iterator* iterator = (aug_iterator*)AUG_ALLOC(sizeof(aug_iterator));
    iterator->ref_count = 1;
    iterator->iterable = (aug_value*)AUG_ALLOC(sizeof(aug_value));
    *iterator->iterable = *iterable;
    aug_incref(iterable);
    iterator->index = NULL;
    return iterator;
}

aug_iterator* aug_iterator_decref(aug_iterator* iterator)
{
    if(iterator != NULL && --iterator->ref_count == 0)
    {
        if(iterator->index != NULL)
            AUG_FREE(iterator->index);
        
        aug_decref(iterator->iterable);
        AUG_FREE(iterator->iterable);
        AUG_FREE(iterator);
        return NULL;    
    }
    return iterator;
}

void aug_iterator_incref(aug_iterator* iterator)
{
    if(iterator != NULL)
        ++iterator->ref_count;
}

bool aug_iterator_next(aug_iterator* iterator)
{
    if(iterator == NULL || iterator->iterable == NULL)
        return false;

    int initial_index = 0;
    aug_value* index = iterator->index;
    aug_value* iterable = iterator->iterable;
    switch(iterable->type)
    {
        case AUG_INT:
        case AUG_STRING:
        case AUG_ARRAY:
            initial_index = 0;
            break;
        case AUG_RANGE:
            initial_index = iterable->range->from;
            break;
        default:
            return false;
    }

    // grab initial index
    if(iterator->index == NULL)
    {
        index = (aug_value*)AUG_ALLOC(sizeof(aug_value));
        *index = aug_create_int(initial_index);
    }
    else 
    {
        assert(index->type == AUG_INT);
        index->i += 1;
    }
    
    iterator->index = index;
    return true;
}

bool aug_iterator_get(aug_iterator* iterator, aug_value* out_element)
{
    if(iterator == NULL || iterator->iterable == NULL)
        return false;

    aug_value* index = iterator->index;
    aug_value* iterable = iterator->iterable;
    assert(index->type == AUG_INT);

    bool success = aug_get_element(iterable, index, out_element);
    if(!success)
    {
        if(iterable->type == AUG_INT && index->i <= iterable->i)
        {
            *out_element = *index;
            return true;
        }

        AUG_FREE(iterator->index);
        iterator->index = NULL;
        return false;
    }
    return true;
}

aug_range* aug_range_new(int from, int to)
{
    aug_range* range = (aug_range*)AUG_ALLOC(sizeof(aug_range));
    range->ref_count = 1;
    range->from = from;
    range->to = to;
    return range;
}

aug_range* aug_range_decref(aug_range* range)
{
    if(range != NULL && --range->ref_count == 0)
    {
        AUG_FREE(range);
        return NULL;    
    }
    return range;
}

void aug_range_incref(aug_range* range)
{
    if(range != NULL)
        ++range->ref_count;
}

// SCRIPT ================================================= SCRIPT ============================================= SCRIPT // 

aug_script* aug_script_new(aug_hashtable* globals, char* bytecode, aug_container* markers)
{
    aug_script* script = (aug_script*)AUG_ALLOC(sizeof(aug_script));
    script->stack_state = NULL;
    script->bytecode = bytecode;

    script->globals = globals;
    aug_hashtable_incref(script->globals);

    script->markers = markers;
    aug_container_incref(script->markers);

    script->lib_extensions = aug_hashtable_new_type(aug_extension);

    return script;
}

void aug_script_delete(aug_script* script)
{
    if (script == NULL)
        return;

    if (script->stack_state != NULL)
    {
        for (size_t i = 0; i < script->stack_state->length; ++i)
        {
            aug_value* value = aug_array_at(script->stack_state, i);
            aug_decref(value);
        }
    }
    script->globals = aug_hashtable_decref(script->globals);
    script->stack_state = aug_array_decref(script->stack_state);
    script->lib_extensions = aug_hashtable_decref(script->lib_extensions);

    if(script->markers->ref_count == 1)
    {
        for(size_t i = 0; i < script->markers->length; ++i)
        {
            aug_trace_marker* marker = aug_container_ptr_type(aug_trace_marker, script->markers, i);
            marker->symbol_name = aug_string_decref(marker->symbol_name);
            marker->filename = aug_string_decref(marker->filename);
        }
    }
    script->markers = aug_container_decref(script->markers);

    if (script->bytecode != NULL)
        AUG_FREE(script->bytecode);
    AUG_FREE(script);
}

// API ================================================= API ====================================================== API // 

aug_vm* aug_startup(aug_error_func* error_func)
{
    // If assert fails, Opcode count is too large. This will affect bytecode instruction set. If bumping aug_opcode type, ensure bytecode offsets are corrected
    assert(AUG_OPCODE_COUNT < 255);

    // If fails, Ensure enough bytes to contain both int and float data types when type punning
    // Check in the aug_ir_operand's union byte array and the aug_vm_bytecode_value's union byte array
    assert(sizeof(float) >= sizeof(int));

    aug_vm* vm = (aug_vm*)AUG_ALLOC(sizeof(aug_vm));

    for (size_t i = 0; i < AUG_STACK_SIZE; ++i)
        vm->stack[i] = aug_none();

    aug_vm_startup(vm);

    // Initialize global vm state, non script context sensitive
    vm->extensions = aug_hashtable_new_type(aug_extension);
    vm->libs = aug_container_new_type(aug_lib_handle, 1);
    vm->error_func = error_func;
    vm->exec_filepath = NULL;
#if AUG_DEBUG
    vm->debug_post_instruction = NULL;
#endif// AUG_DEBUG
    return vm;
}

void aug_shutdown(aug_vm* vm)
{
    // Deinitialize global vm state, non script context sensitive
    // Unregister all extensions and lib extensions
    for (size_t i = 0; i < vm->libs->length; ++i)
    {
        aug_lib_handle handle = aug_container_at_type(aug_lib_handle, vm->libs, i);
        aug_vm_lib_unload(handle);
    }

    vm->libs = aug_container_decref(vm->libs);
    vm->extensions = aug_hashtable_decref(vm->extensions);

    aug_vm_shutdown(vm);
    AUG_FREE(vm);
}

void aug_register(aug_vm* vm, const char* func_name, aug_extension_func* extension_func)
{
    if(vm->running && vm->lib_extensions != NULL)
    {
        if(aug_hashtable_get(vm->lib_extensions, func_name) != 0)
        {
            aug_log_vm_warn(vm, "Failed to register library extension Function %s. Already registered!", func_name);
            return;
        }

        aug_extension* extension = aug_hashtable_insert_type(aug_extension, vm->lib_extensions, func_name);
        if(extension != NULL)
            extension->func = extension_func;
        return;
    }

    if(aug_hashtable_get(vm->extensions, func_name) != 0)
    {
        aug_log_vm_warn(vm, "Failed to register library extension Function %s. Already registered!", func_name);
        return;
    }

    aug_extension* extension = aug_hashtable_insert_type(aug_extension, vm->extensions, func_name);
    if(extension != NULL)
        extension->func = extension_func;
}

void aug_unregister(aug_vm* vm, const char* func_name)
{
    if(vm->running && vm->lib_extensions != NULL)
    {        
        if(!aug_hashtable_remove(vm->lib_extensions, func_name))
            aug_log_vm_warn(vm, "Failed to unregister library extension Function %s. Not registered!", func_name);
        return;
    }

    if(!aug_hashtable_remove(vm->extensions, func_name))
        aug_log_vm_warn(vm, "Failed to unregister extension Function %s. Not registered!", func_name);
}

aug_script* aug_compile(aug_vm* vm, const char* filename)
{
    if(vm == NULL || filename == NULL)
        return NULL;

    aug_input* input = aug_input_open(filename, vm->error_func);
    aug_ast* root = aug_parse(input);
    if (root == NULL)
    {
        aug_input_close(input);
        return NULL;
    }

    aug_ir* ir = aug_generate_ir(root, input);
    char* bytecode = aug_generate_bytecode(ir);
    if(bytecode == NULL)
    {
        aug_ir_delete(ir);
        aug_ast_delete(root);
        aug_input_close(input);
        return NULL;
    }

    aug_script* script = aug_script_new(ir->globals, bytecode, ir->markers);
    
    aug_ir_delete(ir);
    aug_ast_delete(root);
    aug_input_close(input);

    return script;
}

aug_value aug_eval(aug_vm* vm, const char* code)
{    
    if(vm == NULL || code == NULL)
        return aug_none();

    aug_input* input = aug_input_open_code(code, vm->error_func);
    aug_ast* root = aug_parse(input);
    if (root == NULL)
    {
        aug_input_close(input);
        return aug_none();
    }

    aug_ir* ir = aug_generate_ir(root, input);
    char* bytecode = aug_generate_bytecode(ir);
    if(bytecode == NULL)
    {
        aug_ir_delete(ir);
        aug_ast_delete(root);
        aug_input_close(input);
        return aug_none();
    }

    aug_script* script = aug_script_new(ir->globals, bytecode, ir->markers);
    
    aug_ir_delete(ir);
    aug_ast_delete(root);
    aug_input_close(input);

    aug_vm_startup(vm);
    aug_vm_load_script(vm, script);
    aug_vm_execute(vm);

    aug_value* ret = aug_vm_pop(vm);

    aug_vm_shutdown(vm);
    aug_script_delete(script);
    return *ret;
}

void aug_execute(aug_vm* vm, const char* filename)
{
    aug_script* script = aug_compile(vm, filename);

    aug_vm_startup(vm);
    aug_vm_load_script(vm, script);
    aug_vm_execute(vm);
    aug_vm_shutdown(vm);

    aug_script_delete(script);
}

aug_script* aug_load(aug_vm* vm, const char* filename)
{
    // TODO: check file ext. If is a script, compile, else load bytecode
    aug_script* script = aug_compile(vm, filename);
    aug_vm_load_script(vm, script);
    aug_vm_execute(vm);
    aug_vm_save_script(vm, script);
    return script;
}

void aug_unload(aug_vm* vm, aug_script* script)
{
    aug_vm_unload_script(vm, script);
    aug_script_delete(script);
}

aug_value aug_call_args(aug_vm* vm, aug_script* script, const char* func_name, int argc, aug_value* args)
{
    if (vm == NULL || script == NULL)
        return aug_none();

    aug_value ret_value = aug_none();
    if (script->bytecode == NULL)
        return ret_value;

    aug_symbol* symbol_ptr = aug_hashtable_ptr_type(aug_symbol, script->globals, func_name);
    if (symbol_ptr == NULL || symbol_ptr->type == AUG_SYM_NONE)
    {
        aug_log_error(vm->error_func, "Function %s not defined", func_name);
        return ret_value;
    }

    aug_symbol symbol = *symbol_ptr; 
    switch (symbol.type)
    {
    case AUG_SYM_FUNC:
        break;
    case AUG_SYM_VAR:
        aug_log_error(vm->error_func, "Can not call variable %s a function", func_name);
        return ret_value;
    default:
        aug_log_error(vm->error_func, "Symbol %s not defined as a function", func_name);
        return ret_value;
    }

    if (symbol.argc != argc)
    {
        aug_log_error(vm->error_func, "Function %s passed %d arguments, expected %d", func_name, argc, symbol.argc);
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

void aug_save_state(aug_vm* vm, aug_vm_exec_state* exec_state)
{
    if (vm == NULL || exec_state == NULL)
        return;

    exec_state->bytecode = vm->bytecode;
    exec_state->base_index = vm->base_index;
    exec_state->instruction = vm->instruction;
    exec_state->last_instruction = vm->last_instruction;
    exec_state->markers = vm->markers; 
    exec_state->lib_extensions = vm->lib_extensions;

    // reset script stack state to match vm
    if (vm->stack_index > 0)
    {
        const size_t stack_size = (size_t)(vm->stack_index);
        exec_state->stack_state = aug_array_new(stack_size);
        exec_state->stack_state->length = stack_size;

        while (vm->stack_index > 0)
        {
            aug_value* top = aug_vm_pop(vm);
            aug_value* element = aug_array_at(exec_state->stack_state, (size_t)(vm->stack_index));
            aug_incref(top);
            *element = *top;
        }
    }
    else
        exec_state->stack_state = NULL;
}

void aug_load_state(aug_vm* vm, aug_vm_exec_state* exec_state)
{
    if (vm == NULL || exec_state == NULL)
        return;

    vm->bytecode = exec_state->bytecode;
    vm->base_index = exec_state->base_index;
    vm->instruction = exec_state->instruction;
    vm->last_instruction = exec_state->last_instruction;
    vm->markers = exec_state->markers;
    vm->lib_extensions = exec_state->lib_extensions;

    if (exec_state->stack_state != NULL)
    {
        for (size_t i = 0; i < exec_state->stack_state->length; ++i)
        {
            aug_value* top = aug_vm_push(vm);
            aug_value* element = aug_array_at(exec_state->stack_state, i);
            aug_decref(top);
            *top = *element;
        }
    }
    exec_state->stack_state = aug_array_decref(exec_state->stack_state);
}

aug_value aug_create_bool(bool data)
{
    aug_value value;
    aug_set_bool(&value, data);
    return value;
}

aug_value aug_create_int(int data)
{
    aug_value value;
    aug_set_int(&value, data);
    return value;
}

aug_value aug_create_char(char data)
{
    aug_value value;
    aug_set_char(&value, data);
    return value;
}

aug_value aug_create_float(float data)
{
    aug_value value;
    aug_set_float(&value, data);
    return value;
}

aug_value aug_create_string(const char* data)
{
    aug_value value;
    aug_set_string(&value, data);
    return value;
}

aug_value aug_create_user_data(void* userdata)
{
    aug_value value;
    value.type = AUG_USERDATA;
    value.userdata = userdata;
    return value;
}

#ifdef __cplusplus
} // extern C
#endif

#endif //AUG_IMPLEMENTATION
