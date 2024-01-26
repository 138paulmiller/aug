#define AUG_LOG_VERBOSE
#include <aug.h>

#include <string>
#include <cstring>

void aug_dump_file(aug_vm* vm, const char* filename);

struct aug_tester;
typedef void(aug_tester_func)(aug_vm*);

#define STDOUT_RED  "\u001b[31m" 
#define STDOUT_GREEN  "\u001b[32m"
#define STDOUT_YELLOW  "\u001b[33m"
#define STDOUT_BLUE  "\u001b[34m"
#define STDOUT_CLEAR "\u001b[0m"

struct aug_tester
{
	int session_passed;
	int session_total;

	int passed = 0;
	int total = 0;
	int verbose = 0;
	bool dump;
	std::string filename;
	
private:
	static aug_tester* s_tester;
public:

	static void startup()
	{
		s_tester = new aug_tester();
		s_tester->session_passed = 0;
		s_tester->session_total = 0;
	}

	static void shutdown()
	{
		bool success = s_tester->session_total > 0 && s_tester->session_passed == s_tester->session_total;
		const char* msg = success ? STDOUT_GREEN "PASS" : STDOUT_RED "FAIL";
		printf("[%s%s]\t Session Ended. Passed %d / %d\n", msg, STDOUT_CLEAR, s_tester->session_passed, s_tester->session_total);
		delete s_tester;
		s_tester = NULL;
	}

	static aug_tester& get()
	{ 
		return *s_tester;
	}

	void begin(const char* file)
	{
		filename = file;
		passed = 0;
		total = 0;

		if (verbose)
			printf("%s%s%s\n", STDOUT_YELLOW, filename.c_str(), STDOUT_CLEAR);
	}

	void run(aug_vm* vm, aug_tester_func* func = nullptr)
	{

		if (dump)
			aug_dump_file(vm, filename.c_str());

		if (func != nullptr)
			func(vm);
		else
			aug_execute(vm, filename.c_str());
	}

	void end()
	{
		bool success = total > 0 && passed == total;
		if (success)
			++s_tester->session_passed;
		++session_total;

		if (verbose)
			printf("%s%s: Passed %d / %d%s\n", STDOUT_YELLOW, filename.c_str(), passed, total, STDOUT_CLEAR);
		else 
			printf("[%s%s]\t%s", success ? STDOUT_GREEN "PASS" : STDOUT_RED "FAIL", STDOUT_CLEAR, filename.c_str());
		printf("%s\n", filename.c_str());
	}

	void verify(bool success, const std::string& message)
	{
		++total;

		if (success)
			++passed;

		if (verbose)
		{
			printf("[%s%s]\t", success ? STDOUT_GREEN "PASS" : STDOUT_RED "FAIL", STDOUT_CLEAR);

			if (message.size())
				printf("%s", message.c_str());
			printf("\n");
		}
	}
};
aug_tester* aug_tester::s_tester;

std::string to_string(const aug_value& value)
{
    char out[1024];
    int len = 0;
    switch (value.type)
    {
    case AUG_NONE:
        return "none";
    case AUG_BOOL:
        len = snprintf(out, sizeof(out), "%s", value.b ? "true" : "false");
        break;
	case AUG_CHAR:
        len = snprintf(out, sizeof(out), "%c", value.c);
        break;
    case AUG_INT:
        len = snprintf(out, sizeof(out), "%d", value.i);
        break;
    case AUG_FLOAT:
        len = snprintf(out, sizeof(out), "%f", value.f);
        break;
    case AUG_STRING:
        len = snprintf(out, sizeof(out), "%s", value.str->buffer);
        break;
    case AUG_OBJECT:
        return "object";
    case AUG_ARRAY:
    {
        std::string str = "[ ";
		if(value.array)
		{
			for( size_t i = 0; i < value.array->length; ++i)
			{
				const aug_value* entry = aug_array_at(value.array, i);
				str += to_string(*entry);
				str += " ";
			}
		}
		str += "]";
        return str;
    }
    }
    return std::string(out, len);
}

void print(const aug_value& value)
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
	case AUG_ARRAY:
	{
		printf("[ ");
		for( size_t i = 0; i < value.array->length; ++i)
		{
			const aug_value* entry = aug_array_at(value.array, i);
			print(*entry);
			printf(" ");
		}
		printf("]");
		break;
	}
	}
}

float sum(const aug_value& value, aug_value_type& type)
{
	switch (value.type)
	{
	case AUG_NONE:
	case AUG_BOOL:
	case AUG_STRING:
	case AUG_OBJECT:
		return 0.0f;
	case AUG_INT:
		return (float)value.i;
	case AUG_CHAR:
		return (float)value.c;
	case AUG_FLOAT:
		type = AUG_FLOAT;
		return value.f;
	case AUG_ARRAY:
	{
		float  total = 0;
		if(value.array)
		{
			for( size_t i = 0; i < value.array->length; ++i)
			{
				const aug_value* entry = aug_array_at(value.array, i);
				total += sum(*entry, type);
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
		total += sum(args[i], type);

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
		print(args[i]);

	printf("\n");

	return aug_none();
}

aug_value expect(int argc, aug_value* args)
{
	if (argc == 0)
		return aug_none();

	bool success = aug_get_bool(&args[0]);
	std::string message;
	for( int i = 1; i < argc; ++i)
		message += to_string(args[i]);
	
	aug_tester::get().verify(success, message);

	return aug_none();
}

void aug_test_native(aug_vm* vm)
{
	aug_script* script = aug_load(vm, aug_tester::get().filename.c_str());
	{	
		aug_value args[1];
		args[0] = aug_create_int(5);

		aug_value value = aug_call_args(vm, script, "fibonacci", 1, &args[0]);
		
		bool success = value.i == 5;
		//bool success = value.i == 832040;
		const std::string message = "fibonacci = " + to_string(value);
		aug_tester::get().verify(success, message);
	}
	{
		const int n = 5000;
		aug_value args[1];
		args[0] = aug_create_int(n);

		aug_value value = aug_call_args(vm, script, "count", 1, &args[0]);
		
		bool success = value.i == n;
		const std::string message = "count = " + to_string(value);
		aug_tester::get().verify(success, message);
	}

	// unload the script state and restore vm
	aug_unload(vm, script);
}

void aug_test_gameloop(aug_vm* vm)
{	
	aug_script* script = aug_load(vm, aug_tester::get().filename.c_str());

	const int test_count = 10;
	for(int i = 0; i < test_count; ++i)
	{
		aug_call(vm, script, "update");
	}

	aug_unload(vm, script);
}

void aug_error(const char* msg)
{
	fprintf(stderr, "[%sERROR%s]\t%s\t\n", STDOUT_RED, STDOUT_CLEAR, msg);
}

int aug_test(int argc, char** argv)
{
	aug_vm* vm = aug_startup(aug_error);
	
	aug_register(vm, "print", print);
	aug_register(vm, "expect", expect);
	aug_register(vm, "sum", sum);
	aug_register(vm, "append", append);

	aug_tester::startup();
	for(int i = 1; i < argc; ++i)
	{
		if (argv[i] && strcmp(argv[i], "--verbose") == 0)
		{
			aug_tester::get().verbose = 1;
		}
		else if(argv[i] && strcmp(argv[i], "--dump") == 0)
		{
			aug_tester::get().dump = true;
		}
		else if (argv[i] && strcmp(argv[i], "--test") == 0)
		{
			if (++i >= argc)
			{
				printf("aug_test: --exec parameter expected filename!");
				break;
			}
			aug_tester::get().begin(argv[i]);
			aug_tester::get().run(vm);
			aug_tester::get().end();
		}
		else if (argv[i] && strcmp(argv[i], "--test_native") == 0)
		{
			if (++i >= argc)
			{
				printf("aug_test: --test_native parameter expected filename!");
				break;
			}

			aug_tester::get().begin(argv[i]);
			aug_tester::get().run(vm, aug_test_native);
			aug_tester::get().end();
		}
		else if (argv[i] && strcmp(argv[i], "--test_game") == 0)
		{
			if (++i >= argc)
			{
				printf("aug_test: --test_game parameter expected filename!");
				break;
			}

			aug_tester::get().begin(argv[i]);
			aug_tester::get().run(vm, aug_test_gameloop);
			aug_tester::get().end();
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
				const char* arg = argv[i++];

				aug_tester::get().begin(arg);
				aug_tester::get().run(vm);
				aug_tester::get().end();
			}
		}
	}
	aug_tester::shutdown();

	aug_shutdown(vm);
	return 0;
}
