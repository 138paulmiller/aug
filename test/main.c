
#define AUG_IMPLEMENTATION
#include <aug.h>

bool verbose = false;
int tests_passed = 0;
int tests_total = 0;

#define STDOUT_RED    "\x1b[31m" 
#define STDOUT_GREEN  "\x1b[32m"
#define STDOUT_YELLOW "\x1b[33m"
#define STDOUT_BLUE   "\x1b[34m"
#define STDOUT_CLEAR  "\x1b[0m"

void print_value(aug_value value)
{
	switch (value.type)
	{
	case AUG_NONE:
		printf("none");
		break;
	case AUG_BOOL:
		printf("%s", value.b ? "true" : "false");
		break;
	case AUG_CHAR:
		printf("%c", value.c);
		break;
	case AUG_INT:
		printf("%d", value.i);
		break;
	case AUG_FLOAT:
		printf("%0.3f", value.f);
		break;
	case AUG_STRING:
		printf("%s", value.str->buffer);
		break;
	case AUG_OBJECT:
		printf("object");
		break;
	case AUG_FUNCTION:
		printf("function %d", value.i);
		break;
	case AUG_ARRAY:
	{
		printf("[ ");
		for( size_t i = 0; i < value.array->length; ++i)
		{
			const aug_value* entry = aug_array_at(value.array, i);
			print_value(*entry);
			printf(" ");
		}
		printf("]");
		break;
	}
	}
}

float sum_value(aug_value value, aug_value_type* type)
{
	switch (value.type)
	{
	case AUG_NONE:
	case AUG_BOOL:
	case AUG_STRING:
	case AUG_OBJECT:
	case AUG_FUNCTION:
		return 0.0f;
	case AUG_INT:
		return (float)value.i;
	case AUG_CHAR:
		return (float)value.c;
	case AUG_FLOAT:
		*type = AUG_FLOAT;
		return value.f;
	case AUG_ARRAY:
	{
		float  total = 0;
		if(value.array)
		{
			for( size_t i = 0; i < value.array->length; ++i)
			{
				const aug_value* entry = aug_array_at(value.array, i);
				total += sum_value(*entry, type);
			}
		}
		return total;
	}
	}
	return 0.0f;
}

aug_value sum(int argc, aug_value* args)
{
	aug_value_type type = AUG_INT;
	float total = 0.0;
	for( int i = 0; i < argc; ++i)
		total += sum_value(args[i], &type);

	if(type == AUG_FLOAT)
		return  aug_create_float(total);
	else if(type == AUG_INT)
		return aug_create_int((int)total);
	return aug_none();
}

aug_value append(int argc, aug_value* args)
{
	if (argc == 0)
		aug_none();
	if (argc == 1)
		return args[0];

	aug_value value = aug_create_array();
	for (int i = 0; i < argc; ++i)
	{
		aug_value arg = args[i];
		if (arg.type == AUG_ARRAY)
		{
			for (size_t j = 0; j < arg.array->length; ++j)
			{
				const aug_value* entry = aug_array_at(arg.array, j);
				aug_value* element = aug_array_push(value.array);
				if (entry != NULL && element != NULL)
					*element = *entry;
			}
		}
		else
		{
			aug_value* element = aug_array_push(value.array);
			if (element != NULL)
				*element = arg;
		}
	}
	return value;
}

aug_value print(int argc, aug_value* args)
{
	for( int i = 0; i < argc; ++i)
		print_value(args[i]);

	printf("\n");

	return aug_none();
}

aug_value expect(int argc, aug_value* args)
{
	if (argc == 0)
		return aug_none();

	bool success = aug_get_bool(&args[0]);
	++tests_total;
    if (success)
        ++tests_passed;

    if (verbose)
    {
        printf("[%s%s]\t", success ? STDOUT_GREEN "PASS" : STDOUT_RED "FAIL", STDOUT_CLEAR);
        for( int i = 1; i < argc; ++i)
            print_value(args[i]);
        printf("\n");
    }

	return aug_none();
}

int main(int argc, char** argv)
{
	aug_vm* vm = aug_startup(NULL);
	aug_register(vm, "print", print);
	aug_register(vm, "expect", expect);
	aug_register(vm, "sum", sum);
	aug_register(vm, "append", append);

	for(int i = 1; i < argc; ++i)
	{
		if (argv[i] && strcmp(argv[i], "--verbose") == 0)
        {
			verbose = 1;
            break;
        }
        const char* filename = argv[i];
        tests_total = 0;
        tests_passed = 0;

        aug_execute(vm, filename);

        bool success = tests_total > 0 && tests_passed == tests_total;
        if (verbose)
            printf("%s%s: Passed %d / %d%s\n", STDOUT_YELLOW, filename, tests_passed, tests_total, STDOUT_CLEAR);
        else 
            printf("[%s%s]\t%s", success ? STDOUT_GREEN "PASS" : STDOUT_RED "FAIL", STDOUT_CLEAR, filename);
        printf("%s\n", filename);
    }

	aug_shutdown(vm);
	return 0;
}
