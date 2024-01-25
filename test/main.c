
#define AUG_IMPLEMENTATION
#include <aug.h>

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
