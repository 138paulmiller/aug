#define AUG_LOG_VERBOSE
#include <aug.h>

#include <string.h>

void aug_dump_file(aug_environment env, const char* filename);

struct aug_tester
{
	int passed = 0;
	int total = 0;
	int verbose = 0;
	bool dump;
	aug_string filename;

	aug_environment env;
	
	void begin(aug_environment test_env, const char* file)
	{
		env = test_env;
		filename = file;
		passed = 0;
		total = 0;

		if (dump)
			aug_dump_file(env, file);

		if (verbose)
			printf("[TEST]\t%s\n", filename.c_str());
	}

	void end()
	{
		if (verbose && total > 0)
			printf("[TEST]\tEnded. Passed %d / %d\n", passed, total);
		else
			printf("[%s]\t%s\n", passed == total ? "PASS" : "FAIL", filename.c_str());
	}

	void verify(bool success, const aug_string& message)
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

aug_tester tester;


aug_string to_string(const aug_value* value)
{
    if (value == nullptr)
        return "null";

    char out[1024];
    int len = 0;
    switch (value->type)
    {
    case AUG_NONE:
        return "none";
    case AUG_BOOL:
        len = snprintf(out, sizeof(out), "%s", value->b ? "true" : "false");
        break;
    case AUG_INT:
        len = snprintf(out, sizeof(out), "%d", value->i);
        break;
    case AUG_FLOAT:
        len = snprintf(out, sizeof(out), "%f", value->f);
        break;
    case AUG_STRING:
        len = snprintf(out, sizeof(out), "%s", value->str);
        break;
    case AUG_OBJECT:
        return "object";
    case AUG_LIST:
    {
        aug_string str;
        aug_list_node* node = value->list->front; 
        while(node)
        {
            str += to_string(node->value);
            node = node->next;
        }
        return str;
    }
    }
    return aug_string(out, len);
}

void print(const aug_value* arg)
{
	if (arg == nullptr)
		return;

	switch (arg->type)
	{
	case AUG_NONE:
		printf("none");
		break;
	case AUG_BOOL:
		printf("%s", arg->b ? "true" : "false");
		break;
	case AUG_INT:
		printf("%d", arg->i);
		break;
	case AUG_FLOAT:
		printf("%0.3f", arg->f);
		break;
	case AUG_STRING:
		printf("%s", arg->str);
		break;
	case AUG_OBJECT:
		printf("object");
		break;
	case AUG_LIST:
	{
		printf("[");
		if(arg->list)
		{
			aug_list_node* node = arg->list->front; 
			while(node)
			{
				printf(" ");
				print(node->value);
				node = node->next;
			}
		}
		printf("]");
		break;
	}
	}
}

void print(aug_value* return_value, const aug_array<aug_value*>& args)
{
	for (aug_value* arg : args)
	{
		print(arg);
	}

	printf("\n");
}

void expect(aug_value* return_value, const aug_array<aug_value*>& args)
{
	if (args.size() == 0)
		return;

	bool success = aug_to_bool(args[0]);
	aug_string message;
	for( size_t i = 1; i < args.size(); ++i)
		message += to_string(args[i]);
	
	tester.verify(success, message);
}

void aug_test_native(const char* filename)
{
	aug_script script;
	aug_compile(tester.env, script, filename);

	{	
		aug_array<aug_value> args;
		args.push_back(aug_from_int(5));
		//args.push_back(aug_from_int(30));

		aug_value value = aug_call(tester.env, script, "fibonacci", args);
		
		bool success = value.i == 5;
		//bool success = value.i == 832040;
		const aug_string message = "fibonacci = " + to_string(&value);
		tester.verify(success, message);
	}
	{
		aug_array<aug_value> args;
		args.push_back(aug_from_int(10000));

		aug_value value = aug_call(tester.env, script, "count", args);
		
		bool success = value.i == 10000;
		const aug_string message = "count = " + to_string(&value);
		tester.verify(success, message);
	}

}

aug_environment aug_test_env()
{
	aug_environment env;
	aug_register(env, "print", print);
	aug_register(env, "expect", expect);
	return env;
}

#ifdef  _WIN32

#include "windows.h"
#define _CRTDBG_MAP_ALLOC //to get more details

struct win32_memorycheck_scope
{
	_CrtMemState start_memstate;

	win32_memorycheck_scope()
	{
		_CrtMemCheckpoint(&start_memstate); //take a snapshot
	}
	
	~win32_memorycheck_scope()
	{
		_CrtMemState end_memstate, diff_memstate;
		_CrtMemCheckpoint(&end_memstate); //take a snapshot 
		if (_CrtMemDifference(&diff_memstate, &start_memstate, &end_memstate)) // if there is a difference
		{
			OutputDebugString(L"!!! MEMORY LEAK !!! Check debug output!");
			OutputDebugString(L"-----------_CrtMemDumpStatistics ---------");
			_CrtMemDumpStatistics(&diff_memstate);
			OutputDebugString(L"-----------_CrtMemDumpAllObjectsSince ---------");
			_CrtMemDumpAllObjectsSince(&start_memstate);
			OutputDebugString(L"-----------_CrtDumpMemoryLeaks ---------");
			_CrtDumpMemoryLeaks();
		}
	}
};

#endif //_WIN32

int aug_test(int argc, char** argv)
{
#ifdef  _WIN32
	win32_memorycheck_scope memcheck;
#endif //_WIN32

	for(int i = 1; i < argc; ++i)
	{
		if (argv[i] && strcmp(argv[i], "--verbose") == 0)
		{
			tester.verbose = 1;
		}
		else if(argv[i] && strcmp(argv[i], "--dump") == 0)
		{
			tester.dump = true;
		}
		else if (argv[i] && strcmp(argv[i], "--test") == 0)
		{
			if (++i >= argc)
			{
				printf("aug_test: --exec parameter expected filename!");
				return -1;
			}
			
			tester.begin(aug_test_env(), argv[i]);

			aug_execute(tester.env, argv[i]);

			tester.end();
		}
		else if (argv[i] && strcmp(argv[i], "--test_native") == 0)
		{
			if (++i >= argc)
			{
				printf("aug_test: --test_native parameter expected filename!");
				return -1;
			}

			tester.begin(aug_test_env(), argv[i]);

			aug_test_native(argv[i]);

			tester.end();
		}
	}

	return 0;
}