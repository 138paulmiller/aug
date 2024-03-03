#define AUG_IMPLEMENTATION
#define AUG_LOG_VERBOSE
#include <aug.h>
#include "dump.inl"

#include <string.h>

struct aug_tester;
typedef void(aug_tester_func)(aug_vm*);

#define STDOUT_RED     "\x1b[31m" 
#define STDOUT_GREEN   "\x1b[32m"
#define STDOUT_YELLOW  "\x1b[33m"
#define STDOUT_BLUE    "\x1b[34m"
#define STDOUT_CLEAR   "\x1b[0m"

typedef struct aug_tester
{
    const char* filename;
    int session_passed;
    int session_total;

    int passed;
    int total;
    bool verbose;
    bool dump;
} aug_tester;
aug_tester s_tester;

static void test_startup()
{
    s_tester.session_passed = 0;
    s_tester.session_total = 0;
}

static void test_shutdown()
{
    bool success = s_tester.session_total > 0 && s_tester.session_passed == s_tester.session_total;
    const char* msg = success ? STDOUT_GREEN "PASS" : STDOUT_RED "FAIL";
    printf("[%s%s]\t Session Ended. Passed %d / %d\n", msg, STDOUT_CLEAR, s_tester.session_passed, s_tester.session_total);
}

void test_run(const char* filename, aug_vm* vm, aug_tester_func* func)
{
    // Begin Test
    s_tester.filename = filename;
    s_tester.passed = 0;
    s_tester.total = 0;

    if (s_tester.verbose)
        printf("%s%s%s\n", STDOUT_YELLOW, s_tester.filename, STDOUT_CLEAR);

#if AUG_DEBUG
    if (s_tester.dump)
        aug_dump_file(vm, s_tester.filename);
#endif 

    // Run test
    if (func != NULL)
        func(vm);
    else
        aug_execute(vm, s_tester.filename);
    
    if(!vm->valid)
        s_tester.passed = 0;

    // End test
    bool success = s_tester.total > 0 && s_tester.passed == s_tester.total;
    if (success)
        ++s_tester.session_passed;
    ++s_tester.session_total;

    if(s_tester.verbose)
        printf("%s%s: Passed %d / %d%s\n", STDOUT_YELLOW, s_tester.filename, s_tester.passed, s_tester.total, STDOUT_CLEAR);
    else 
        printf("[%s%s]\t%s", success ? STDOUT_GREEN "PASS" : STDOUT_RED "FAIL", STDOUT_CLEAR, s_tester.filename);
    printf("%s\n", s_tester.filename);
}

void test_verify(bool success, aug_string* message)
{
    ++s_tester.total;

    if (success)
        ++s_tester.passed;

    if (s_tester.verbose)
    {
        printf("[%s%s]\t", success ? STDOUT_GREEN "PASS" : STDOUT_RED "FAIL", STDOUT_CLEAR);

        if (message)
            printf("%s", message->buffer);
        printf("\n");
    }
}

aug_string* to_string(const aug_value* value);

void to_string_map_pair(const aug_value* key, aug_value* value, void* user_data)
{
    aug_string* str = (aug_string*)user_data;
    aug_string* key_str = to_string(key);
    aug_string* value_str = to_string(value);
    aug_string_append_bytes(str, "\n\t", 2);
    aug_string_append(str, key_str);
    aug_string_append_bytes(str, " : ", 3);
    aug_string_append(str, value_str);
    aug_string_decref(key_str);
    aug_string_decref(value_str);
}

aug_string* to_string(const aug_value* value)
{
    char out[1024];
    switch (value->type)
    {
    case AUG_NONE:
        return aug_string_create("none");
    case AUG_BOOL:
        snprintf(out, sizeof(out), "%s", value->b ? "true" : "false");
        break;
    case AUG_CHAR:
        snprintf(out, sizeof(out), "%c", value->c);
        break;
    case AUG_INT:
        snprintf(out, sizeof(out), "%d", value->i);
        break;
    case AUG_FLOAT:
        snprintf(out, sizeof(out), "%f", value->f);
        break;
    case AUG_STRING:
        snprintf(out, sizeof(out), "%s", value->str->buffer);
        break;
    case AUG_FUNCTION:
        snprintf(out, sizeof(out), "function %d", value->i);
        break;
    case AUG_OBJECT:
        return aug_string_create("object");
    case AUG_ITERATOR:
        return aug_string_create("iterator");
    case AUG_ARRAY:
    {
        aug_string* str = aug_string_create("[");
        if(value->array)
        {
            for( size_t i = 0; i < value->array->length; ++i)
            {
                const aug_value* element = aug_array_at(value->array, i);
                aug_string* element_str = to_string(element);
                aug_string_append(str, element_str);
                aug_string_decref(element_str);
                if( i != value->array->length - 1)
                    aug_string_append_bytes(str, ",", 1);
            }
        }
        aug_string_append_bytes(str, "]", 1);
        return str;
    }
    case AUG_MAP:
    {
        aug_string* str = aug_string_create("{");
        aug_map_foreach(value->map, to_string_map_pair, &str);
        aug_string_append_bytes(str, "\n}", 2);
        return str;
    }
    default: return aug_string_create("");
    }
    return aug_string_create(out);
}

float sum_value(const aug_value* value, aug_type* type)
{
    switch (value->type)
    {
    case AUG_NONE:
    case AUG_BOOL:
    case AUG_STRING:
    case AUG_OBJECT:
    case AUG_FUNCTION:
        return 0.0f;
    case AUG_INT:
        return (float)value->i;
    case AUG_CHAR:
        return (float)value->c;
    case AUG_FLOAT:
        *type = AUG_FLOAT;
        return value->f;
    case AUG_ARRAY:
    {
        float total = 0;
        if(value->array)
        {
            for( size_t i = 0; i < value->array->length; ++i)
            {
                const aug_value* entry = aug_array_at(value->array, i);
                total += sum_value(entry, type);
            }
        }
        return total;
    }
    default: 
        return 0;
    }
    return 0.0f;
}

aug_value sum(int argc, aug_value* args)
{
    aug_type type = AUG_INT;
    float total = 0.0;
    for( int i = 0; i < argc; ++i)
        total += sum_value(args+i, &type);

    if(type == AUG_FLOAT)
        return  aug_create_float(total);
    else if(type == AUG_INT)
        return aug_create_int((int)total);
    return aug_none();
}

aug_value map_insert(int argc, aug_value* args)
{
    if (argc != 3)
        return aug_none();
    aug_value map = args[0];
    aug_value key = args[1];
    aug_value value = args[2];
    if(map.type != AUG_MAP)
        return aug_none();
    aug_map_insert(map.map, &key, &value);

    return aug_none();
}

aug_value expect(int argc, aug_value* args)
{
    if (argc == 0)
        return aug_none();

    bool success = aug_to_bool(&args[0]);

    aug_string* message = aug_string_create("");
    for( int i = 1; s_tester.verbose && i < argc; ++i)
    {
        aug_string* message_arg = to_string(args+i);
        aug_string_append(message, message_arg);
        aug_string_decref(message_arg);
    }
    
    test_verify(success, message);

    aug_string_decref(message);

    return aug_none();
}

void aug_test_native(aug_vm* vm)
{
    aug_script* script = aug_load(vm, s_tester.filename);
    {   
        aug_value args[1];
        args[0] = aug_create_int(5);

        aug_value value = aug_call_args(vm, script, "fibonacci", 1, &args[0]);

        bool success = value.i == 5;
        //bool success = value.i == 832040;
        aug_string* message = aug_string_create("fibonacci = ");
        aug_string* value_str = to_string(&value);
        aug_string_append(message, value_str);
        test_verify(success, message);

        aug_string_decref(value_str);
        aug_string_decref(message);
    }
    {
        const int n = 5000;
        aug_value args[1];
        args[0] = aug_create_int(n);

        aug_value value = aug_call_args(vm, script, "count", 1, &args[0]);
        
        bool success = value.i == n;
        aug_string* message = aug_string_create("count = ");
        aug_string* value_str = to_string(&value);
        aug_string_append(message, value_str);
        test_verify(success, message);

        aug_string_decref(value_str);
        aug_string_decref(message);
    }

    // unload the script state and restore vm
    aug_unload(vm, script);
}

void aug_test_gameloop(aug_vm* vm)
{   
    aug_script* script = aug_load(vm, s_tester.filename);

    const int test_count = 10;
    for(int i = 0; i < test_count; ++i)
    {
        aug_call(vm, script, "update");
    }

    aug_unload(vm, script);
}

void on_aug_error(const char* msg)
{
    fprintf(stderr, "[%sERROR%s]\t%s\t\n", STDOUT_RED, STDOUT_CLEAR, msg);
}

#if AUG_DEBUG
void on_aug_post_instruction_debug(aug_vm* vm, int opcode)
{
    printf("%ld:   %s\n", vm->instruction - vm->bytecode, aug_opcode_label(opcode));
    int i;
    for(i = 0; i < 10; ++i)
    {
        aug_value val = vm->stack[i];
        printf("%s %d: %s ", (vm->stack_index-1) == i ? ">" : " ", i, aug_type_label(&val));
        switch(val.type)
        {
            case AUG_INT:      printf("%d", val.i); break;
            case AUG_FLOAT:    printf("%f", val.f); break;
            case AUG_STRING:   printf("%s", val.str->buffer); break;
            case AUG_BOOL:     printf("%s", val.b ? "true" : "false"); break;
            case AUG_CHAR:     printf("%c", val.c); break;
            default: break;
        }
        printf("\n");
    }
    getchar();
}
#endif

int main(int argc, char**argv)
{
    aug_vm* vm = aug_startup(on_aug_error);

#if AUG_DEBUG
    vm->debug_post_instruction = on_aug_post_instruction_debug;
#endif

    aug_register(vm, "expect", expect);
    aug_register(vm, "sum", sum);
    test_startup();

    for(int i = 1; i < argc; ++i)
    {
        if (argv[i] && strcmp(argv[i], "--verbose") == 0)
        {
            s_tester.verbose = 1;
        }
        else if(argv[i] && strcmp(argv[i], "--dump") == 0)
        {
            s_tester.dump = true;
        }
        else if (argv[i] && strcmp(argv[i], "--test") == 0)
        {
            if (++i >= argc)
            {
                printf("aug_test: --exec parameter expected filename!");
                break;
            }
            test_run(argv[i], vm, NULL);
        }
        else if (argv[i] && strcmp(argv[i], "--test_native") == 0)
        {
            if (++i >= argc)
            {
                printf("aug_test: --test_native parameter expected filename!");
                break;
            }

            test_run(argv[i], vm, aug_test_native);
        }
        else if (argv[i] && strcmp(argv[i], "--test_game") == 0)
        {
            if (++i >= argc)
            {
                printf("aug_test: --test_game parameter expected filename!");
                break;
            }

            test_run(argv[i], vm, aug_test_gameloop);
        }
        else if (argv[i] && strcmp(argv[i], "--test_all") == 0)
        {
            if (++i >= argc)
            {
                printf("aug_test: --test_all parameter expected filename!");
                break;
            }

            while ( i < argc && strncmp(argv[i], "--", 2) != 0)
            {
                test_run(argv[i++], vm, NULL);
            }
        }
    }

    test_shutdown();
    aug_shutdown(vm);
    return 0;
}
