#define CURB_LOG_VERBOSE
#include <curb.h>

#include <string.h>

void curb_dump_file(curb_environment env, const char* filename);

struct curb_tester
{
	int passed = 0;
	int total = 0;
	int verbose = 0;
	bool dump;
	curb_string filename;

	curb_environment env;
	
	void begin(curb_environment test_env, const char* file)
	{
		env = test_env;
		filename = file;
		passed = 0;
		total = 0;

		if (dump)
			curb_dump_file(env, file);

		if (verbose)
			printf("[TEST]\t%s\n", filename.c_str());
	}

	void end()
	{
		if (verbose)
			printf("[TEST]\tEnded. Passed %d / %d\n", passed, total);
		else
			printf("[%s]\t%s\n", passed == total ? "PASS" : "FAIL", filename.c_str());
	}

	void verify(bool success, const curb_string& message)
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

curb_tester tester;

void print(const curb_value* arg)
{
	if (arg == nullptr)
		return;

	switch (arg->type)
	{
	case CURB_NONE:
		printf("none");
		break;
	case CURB_BOOL:
		printf("%s", arg->b ? "true" : "false");
		break;
	case CURB_INT:
		printf("%d", arg->i);
		break;
	case CURB_FLOAT:
		printf("%0.3f", arg->f);
		break;
	case CURB_STRING:
		printf("%s", arg->str);
		break;
	case CURB_OBJECT:
		printf("object");
		break;
	}
}

void print(curb_value* return_value, const curb_array<curb_value*>& args)
{
	for (curb_value* arg : args)
	{
		print(arg);
	}

	printf("\n");
}

void expect(curb_value* return_value, const curb_array<curb_value*>& args)
{
	if (args.size() == 0)
		return;

	bool success = curb_to_bool(args[0]);
	curb_string message;
	for( size_t i = 1; i < args.size(); ++i)
		message += curb_to_string(args[i]);
	
	tester.verify(success, message);
}

void curb_test_native(const char* filename)
{
	curb_script script;
	curb_compile(tester.env, script, filename);

	curb_array<curb_value> args;
	args.push_back(curb_int(5));
	//args.push_back(curb_int(30));

	curb_value value = curb_call(tester.env, script, "fibonacci", args);
	
	bool success = value.i == 5;
	//bool success = value.i == 832040;
	const curb_string message = "fibonacci = " + curb_to_string(&value);
	tester.verify(success, message);
}

curb_environment curb_test_env()
{
	curb_environment env;
	curb_register(env, "print", print);
	curb_register(env, "expect", expect);
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


int curb_test(int argc, char** argv)
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
				printf("curb_test: --exec parameter expected filename!");
				return -1;
			}
			
			tester.begin(curb_test_env(), argv[i]);

			curb_execute(tester.env, argv[i]);

			tester.end();
		}
		else if (argv[i] && strcmp(argv[i], "--test_native") == 0)
		{
			if (++i >= argc)
			{
				printf("curb_test: --test_native parameter expected filename!");
				return -1;
			}

			tester.begin(curb_test_env(), argv[i]);

			curb_test_native(argv[i]);

			tester.end();
		}
	}

	return 0;
}