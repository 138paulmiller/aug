
#define AUG_IMPLEMENTATION
#include <aug.h>


#define AUG_DEFINE_ARRAY(name, type)                                                      \
typedef struct name                                                                       \
{                                                                                         \
	type* buffer;                                                                         \
	size_t capacity;                                                                      \
	size_t length;                                                                        \
} name;                                                                                   \
name name##_new(size_t size)                                                              \
{                                                                                         \
    name array;                                                                           \
    array.length = 0;                                                                     \
    array.capacity = size;                                                                \
    array.buffer = (type*)AUG_ALLOC(sizeof(type) * array.capacity);                       \
    return array;                                                                         \
}                                                                                         \
void name##_delete(name* array)                                                           \
{                                                                                         \
    AUG_FREE(array->buffer);                                                              \
    array->buffer = NULL;                                                                 \
}                                                                                         \
void name##_resize(name* array, size_t size)                                              \
{                                                                                         \
    array->capacity = size;                                                               \
    array->buffer = (type*)AUG_REALLOC(array->buffer, sizeof(type)* array->capacity);     \
}                                                                                         \
void name##_push(name* array, type data)                                                  \
{                                                                                         \
    if (array->length+1 >= array->capacity)                                               \
        name##_resize(array, 2 * array->capacity);                                        \
    array->buffer[array->length++] = data;                                                \
}                                                                                         \
type* name##_pop(name* array)                                                             \
{                                                                                         \
    return array->length > 0 ? &array->buffer[--array->length] : NULL;                    \
}                                                                                         \
type* name##_at(const name* array, size_t index)                                          \
{                                                                                         \
    return index >= 0 && index < array->length ? &array->buffer[index] : NULL;            \
}                                                                                         \
type* name##_back(const name* array)                                                      \
{                                                                                         \
    return array->length > 0 ? &array->buffer[array->length - 1] : NULL;                  \
}

int print_value(aug_value value)
{
	switch (value.type)
	{
	case AUG_NONE: printf("none"); break;
	case AUG_BOOL: printf("%s", value.b ? "true" : "false"); break;
	case AUG_CHAR: printf("%c", value.c); break;
	case AUG_INT: printf("%d", value.i); break;
	case AUG_FLOAT: printf("%0.3f", value.f); break;
	case AUG_STRING: printf("%s", value.str->buffer); break;
	case AUG_ARRAY:
		printf("[ ");
        for( size_t i = 0; i < value.array->length; ++i)
            print_value(*aug_array_at(value.array, i)) && printf(" ");
		printf("]");
		break;
	}
    return 0;
}

aug_value print(int argc, aug_value* args)
{
	for( int i = 0; i < argc; ++i)
		print_value(args[i]);
	printf("\n");
	return aug_none();
}

int main(int argc, char** argv)
{
	aug_vm* vm = aug_startup(NULL);
	aug_register(vm, "print", print);
	aug_execute(vm, argv[1]);
	aug_shutdown(vm);
	return 0;
}
