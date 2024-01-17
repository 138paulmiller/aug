#define AUG_LOG_VERBOSE
#include <aug.h>

#include <string.h>

void aug_dump_file(aug_environment env, const char* filename);

struct aug_tester;
typedef void(aug_tester_func)(aug_environment&);

struct aug_tester
{
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
	}

	static void shutdown()
	{
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
			printf("[TEST]\t%s\n", filename.c_str());
	}

	void run(aug_environment& env, aug_tester_func* func = nullptr)
	{
		if (dump)
			aug_dump_file(env, filename.c_str());
		 
		if (func != nullptr)
			func(env);
		else
			aug_execute(env, filename.c_str());
	}

	void end()
	{
		if (verbose)
			printf("[TEST]\tEnded. Passed %d / %d\n", passed, total);
		else
			printf("[%s]\t%s\n", total > 0 && passed == total ? "PASS" : "FAIL", filename.c_str());
	}

	void verify(bool success, const std::string& message)
	{
		++total;

		if (success)
			++passed;

		if (verbose)
		{
			printf("[%s]\t", success ? "PASS" : "FAIL");

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
    case AUG_INT:
        len = snprintf(out, sizeof(out), "%d", value.i);
        break;
    case AUG_FLOAT:
        len = snprintf(out, sizeof(out), "%f", value.f);
        break;
    case AUG_STRING:
        len = snprintf(out, sizeof(out), "%s", value.str->c_str());
        break;
    case AUG_OBJECT:
        return "object";
    case AUG_LIST:
    {
        std::string str = "[ ";
		if(value.list)
		{
			for(const aug_value& entry : value.list->data)
			{
				str += to_string(entry);
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
	case AUG_INT:
		printf("%d", value.i);
		break;
	case AUG_FLOAT:
		printf("%0.3f", value.f);
		break;
	case AUG_STRING:
		printf("%s", value.str->c_str());
		break;
	case AUG_OBJECT:
		printf("object");
		break;
	case AUG_LIST:
	{
		printf("[ ");
		if(value.list)
		{
			for(const aug_value& entry : value.list->data)
			{
				print(entry);
				printf(" ");
			}
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
		// INVALID TYPE!
		return 0.0f;
	case AUG_INT:
		return (float)value.i;
	case AUG_FLOAT:
		type = AUG_FLOAT;
		return value.f;
	case AUG_LIST:
	{
		float  total = 0;
		if(value.list)
		{
			for(const aug_value& entry : value.list->data)
				total += sum(entry, type);
		}
		return total;
	}
	}
	return 0.0f;
}

aug_value sum(const aug_std_array<aug_value>& args)
{
	aug_value_type type = AUG_INT;
	float total = 0.0;
	for (const aug_value& arg : args)
		total += sum(arg, type);

	if(type == AUG_FLOAT)
		return  aug_from_float(total);
	else if(type == AUG_INT)
		return aug_from_int((int)total);
	return aug_none();
}

aug_value print(const aug_std_array<aug_value>& args)
{
	for (const aug_value& arg : args)
	{
		print(arg);
	}

	printf("\n");

	return aug_none();
}

aug_value expect(const aug_std_array<aug_value>& args)
{
	if (args.size() == 0)
		return aug_none();

	bool success = aug_to_bool(&args[0]);
	std::string message;
	for( size_t i = 1; i < args.size(); ++i)
		message += to_string(args[i]);
	
	aug_tester::get().verify(success, message);

	return aug_none();
}

void aug_test_native(aug_environment& env)
{
	aug_script script;
	aug_compile(env, script, aug_tester::get().filename.c_str());

	{	
		aug_std_array<aug_value> args;
		args.push_back(aug_from_int(5));
		//args.push_back(aug_from_int(30));

		aug_value value = aug_call(env, script, "fibonacci", args);
		
		bool success = value.i == 5;
		//bool success = value.i == 832040;
		const std::string message = "fibonacci = " + to_string(value);
		aug_tester::get().verify(success, message);
	}
	{
		const int n = 5000;
		aug_std_array<aug_value> args;
		args.push_back(aug_from_int(n));

		aug_value value = aug_call(env, script, "count", args);
		
		bool success = value.i == n;
		const std::string message = "count = " + to_string(value);
		aug_tester::get().verify(success, message);
	}
}


int aug_test(int argc, char** argv)
{
	aug_environment env;
	aug_startup(env, nullptr);
	aug_register(env, "print", print);
	aug_register(env, "expect", expect);
	aug_register(env, "sum", sum);

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
			aug_tester::get().run(env);
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
			aug_tester::get().run(env, aug_test_native);
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
				aug_tester::get().run(env);
				aug_tester::get().end();
			}
		}
	}
	aug_tester::shutdown();

	aug_shutdown(env);
	return 0;
}
