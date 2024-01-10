#define SHL_IMPLEMENTATION
#define SHL_LOG_VERBOSE
#include <shl.h>

#ifdef  _WIN32
#include "windows.h"
#define _CRTDBG_MAP_ALLOC //to get more details
#endif //_WIN32

namespace
{
	void test_lexer(const char* filename)
	{
		printf("Tokens \n");

		shl_environment env;
		shl_lexer lexer;
		shl_lexer_open_file(env, lexer, filename);
		while (shl_lexer_move(env, lexer) && lexer.curr.id != SHL_TOKEN_END)
		{
			//printf("\tPREV: %s (%s)%d:%d\n", lexer.prev.detail->label, lexer.prev.data.c_str(), lexer.prev.line, lexer.prev.col);
			printf("\tCURR: %s (%s) %d:%d\n", lexer.curr.detail->label, lexer.curr.data.c_str(), lexer.curr.line, lexer.curr.col);
			//printf("\tNEXT: %s (%s)%d:%d\n", lexer.next.detail->label, lexer.next.data.c_str(), lexer.next.line, lexer.next.col);
			printf("\n");
		}
		shl_lexer_close(lexer);

		printf("End Tokenizing File: %s\n", filename);
	}

	void test_ast_print_tree(shl_ast* node, std::string prefix = "", bool is_leaf = false)
	{
		static const char* space = "  ";// char(192);
		static const char* pipe = "| ";// char(192);
		static const char* pipe_junction = "|-";//char(195);
		static const char* pipe_end = "\\-";//char(179);

		printf("%s", prefix.c_str());

		if(is_leaf)
		{
			printf("%s", pipe_end);
			prefix += space;
		}
		else 
		{
			printf("%s", pipe_junction);
			prefix += pipe;
		}

		const shl_token& token = node->token;
		const shl_array<shl_ast*>& children = node->children;

		switch(node->id)
		{
			case SHL_AST_ROOT:
				assert(children.size() == 1);
				printf("AST");
				test_ast_print_tree(children[0], "\n", true);
				printf("\n");
				break;
			case SHL_AST_BLOCK:
				printf("BLOCK");
				for (size_t i = 0; i < children.size(); ++i)
					test_ast_print_tree(children[i], prefix, i == children.size()-1);
				break;
			case SHL_AST_STMT_ASSIGN:
				assert(children.size()==1);
				printf("ASSIGN:%s", token.data.c_str());
				test_ast_print_tree(children[0], prefix, true);
				break;
			case SHL_AST_STMT_IF:
				printf("IF:%s", token.data.c_str());
				test_ast_print_tree(children[0], prefix, true);
				break;
			case SHL_AST_STMT_IF_ELSE:
				printf("IF:%s", token.data.c_str());
				test_ast_print_tree(children[0], prefix, false);
				printf("ELSE:%s", token.data.c_str());
				test_ast_print_tree(children[1], prefix, true);
				break;
			case SHL_AST_STMT_WHILE:
				printf("WHILE");
				for (size_t i = 0; i < children.size(); ++i)
					test_ast_print_tree(children[i], prefix, i == children.size()-1);
				break;
			case SHL_AST_UNARY_OP:
				assert(children.size() == 1);
				printf("%s", token.detail->label);
				test_ast_print_tree(children[0], prefix, true);
				break;
			case SHL_AST_BINARY_OP:
				assert(children.size() == 2);
				printf("%s", token.detail->label);
				test_ast_print_tree(children[0], prefix, false);
				test_ast_print_tree(children[1], prefix, true);
				break;

			case SHL_AST_FUNC_CALL:
				printf("FUNCCALL:%s", token.data.c_str());
				for (size_t i = 0; i < children.size(); ++i)
					test_ast_print_tree(children[i], prefix, i == children.size()-1);
				break;
			case SHL_AST_VARIABLE:
			case SHL_AST_LITERAL:
				printf("%s", token.data.c_str());
				break;
			case SHL_AST_FUNC_DEF:
				assert(children.size() == 2);
				printf("FUNCDEF:%s", token.data.c_str());
				for (size_t i = 0; i < children.size(); ++i)
					test_ast_print_tree(children[i], prefix, i == children.size() - 1);
				break;
			case SHL_AST_PARAM_LIST:
				printf("PARAMS:%s", token.data.c_str());
				for (size_t i = 0; i < children.size(); ++i)
					test_ast_print_tree(children[i], prefix, i == children.size() - 1);
				break;
			case SHL_AST_PARAM:
				printf("%s", token.data.c_str());
				break;
			case SHL_AST_RETURN:
				printf("RETURN");
				if(children.size() == 1)
				test_ast_print_tree(children[0], prefix, true);
				break;
		}
	}

	void test_ast(shl_ast* root)
	{
		test_ast_print_tree(root, "", false);
	}

	void test_ir(const shl_ir_module& module)
	{
		printf("IR\n");
		for(const shl_ir_operation& operation : module.operations)
		{
			printf("%d\t\t%s", (int)operation.bytecode_offset, shl_opcode_labels[(int)operation.opcode]);
			for (shl_ir_operand operand : operation.operands)
			{
				switch (operand.type)
				{
				case SHL_IR_OPERAND_INT:
					printf(" %d", operand.data.i);
					break;
				case SHL_IR_OPERAND_FLOAT:
					printf(" %f", operand.data.f);
					break;
				case SHL_IR_OPERAND_BYTES:
					printf(" %s", operand.data.str);
					break;
				case SHL_IR_OPERAND_NONE:
					break;
				}
			}
			printf("\n");
		}
	}

	void test_execute(shl_environment env, const char* filename)
	{
		printf("----------%s------------\n", filename );

		//test_lexer(filename);

		shl_ast* root = shl_parse_file(env, filename);
		if(root == nullptr)
			return;

		test_ast_print_tree(root);

		// Generate IR
		shl_ast_to_ir_context context;
		context.valid = true;
		context.env = env;

		shl_ir ir;
		shl_ast_to_ir(context, root, ir);

		test_ir(ir.module);

		// Cleanup
		shl_ast_delete(root);
	}
}


void print(const shl_value* arg)
{
	if (arg == nullptr)
		return;

	switch (arg->type)
	{
	case SHL_BOOL:
		printf("%s", arg->b ? "true" : "false");
		break;
	case SHL_INT:
		printf("%d", arg->i);
		break;
	case SHL_FLOAT:
		printf("%0.3f", arg->f);
		break;
	case SHL_STRING:
		printf("%s", arg->str);
		break;
	case SHL_OBJECT:
		printf("object");
		break;
	}
}

void print(const shl_array<shl_value*>& args)
{
	for (shl_value* arg : args)
	{
		print(arg);
	}

	printf("\n");
}

static int test_passed_count = 0;
static int test_total = 0;

// condition, messages ...
void expect(const shl_array<shl_value*>& args)
{
	if (args.size() == 0)
		return;

	++test_total;

	if (shl_value_to_bool(args[0]))
	{
		printf("[PASSED]\t");
		++test_passed_count;
	}
	else
	{
		printf("[FAILED]\t");
	}

	for( size_t i = 1; i < args.size(); ++i)
		print(args[i]);

	printf("\n");
}

#ifdef  _WIN32

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

int shl_test(int argc, char** argv)
{
#ifdef  _WIN32
	win32_memorycheck_scope memcheck;
#endif //_WIN32

	shl_environment env;
	shl_register(env, "print", print);
	shl_register(env, "expect", expect);


	for(int i = 1; i < argc; ++i)
	{
		if(argv[i] && strcmp(argv[i], "--dump") == 0)
		{
			if (++i >= argc)
			{
				printf("shl_test: --generate parameter expected filename!");
				return -1;
			}

			test_execute(env, argv[i]);
		}
		else if (argv[i] && strcmp(argv[i], "--exec") == 0)
		{
			if (++i >= argc)
			{
				printf("shl_test: --exec parameter expected filename!");
				return -1;
			}

			printf("[TEST  ]\t%s\n", argv[i]);
			shl_execute(env, argv[i]);
			printf("[TEST  ]\tEnded. Passed %d / %d\n", test_passed_count, test_total);
		}
	}

	return 0;
}