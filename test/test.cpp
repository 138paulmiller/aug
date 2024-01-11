#define CURB_IMPLEMENTATION
#define CURB_LOG_VERBOSE
#include <curb.h>

#ifdef  _WIN32
#include "windows.h"
#define _CRTDBG_MAP_ALLOC //to get more details
#endif //_WIN32

namespace
{
	void test_lexer(const char* filename)
	{
		printf("Tokens \n");

		curb_environment env;
		curb_lexer lexer;
		curb_lexer_open_file(env, lexer, filename);
		while (curb_lexer_move(env, lexer) && lexer.curr.id != CURB_TOKEN_END)
		{
			//printf("\tPREV: %s (%s)%d:%d\n", lexer.prev.detail->label, lexer.prev.data.c_str(), lexer.prev.line, lexer.prev.col);
			printf("\tCURR: %s (%s) %d:%d\n", lexer.curr.detail->label, lexer.curr.data.c_str(), lexer.curr.line, lexer.curr.col);
			//printf("\tNEXT: %s (%s)%d:%d\n", lexer.next.detail->label, lexer.next.data.c_str(), lexer.next.line, lexer.next.col);
			printf("\n");
		}
		curb_lexer_close(lexer);

		printf("End Tokenizing File: %s\n", filename);
	}

	void test_ast_print_tree(curb_ast* node, std::string prefix = "", bool is_leaf = false)
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

		const curb_token& token = node->token;
		const curb_array<curb_ast*>& children = node->children;

		switch(node->id)
		{
			case CURB_AST_ROOT:
				assert(children.size() == 1);
				printf("AST");
				test_ast_print_tree(children[0], "\n", true);
				printf("\n");
				break;
			case CURB_AST_BLOCK:
				printf("BLOCK");
				for (size_t i = 0; i < children.size(); ++i)
					test_ast_print_tree(children[i], prefix, i == children.size()-1);
				break;
			case CURB_AST_STMT_ASSIGN:
				assert(children.size()==1);
				printf("ASSIGN:%s", token.data.c_str());
				test_ast_print_tree(children[0], prefix, true);
				break;
			case CURB_AST_STMT_EXPR:
				test_ast_print_tree(children[0], prefix, true);
				break;
			case CURB_AST_STMT_IF:
				printf("IF:%s", token.data.c_str());
				test_ast_print_tree(children[0], prefix, true);
				break;
			case CURB_AST_STMT_IF_ELSE:
				printf("IF:%s", token.data.c_str());
				test_ast_print_tree(children[0], prefix, false);
				printf("ELSE:%s", token.data.c_str());
				test_ast_print_tree(children[1], prefix, true);
				break;
			case CURB_AST_STMT_WHILE:
				printf("WHILE");
				for (size_t i = 0; i < children.size(); ++i)
					test_ast_print_tree(children[i], prefix, i == children.size()-1);
				break;
			case CURB_AST_UNARY_OP:
				assert(children.size() == 1);
				printf("%s", token.detail->label);
				test_ast_print_tree(children[0], prefix, true);
				break;
			case CURB_AST_BINARY_OP:
				assert(children.size() == 2);
				printf("%s", token.detail->label);
				test_ast_print_tree(children[0], prefix, false);
				test_ast_print_tree(children[1], prefix, true);
				break;
			case CURB_AST_FUNC_CALL:
				printf("FUNCCALL:%s", token.data.c_str());
				for (size_t i = 0; i < children.size(); ++i)
					test_ast_print_tree(children[i], prefix, i == children.size()-1);
				break;
			case CURB_AST_FUNC_DEF:
				assert(children.size() == 2);
				printf("FUNCDEF:%s", token.data.c_str());
				for (size_t i = 0; i < children.size(); ++i)
					test_ast_print_tree(children[i], prefix, i == children.size() - 1);
				break;
			case CURB_AST_PARAM_LIST:
				printf("PARAMS:%s", token.data.c_str());
				for (size_t i = 0; i < children.size(); ++i)
					test_ast_print_tree(children[i], prefix, i == children.size() - 1);
				break;
			case CURB_AST_RETURN:
				printf("RETURN");
				if(children.size() == 1)
				test_ast_print_tree(children[0], prefix, true);
				break;
			case CURB_AST_PARAM:
			case CURB_AST_VARIABLE:
			case CURB_AST_LITERAL:
				printf("%s", token.data.c_str());
				break;
		}
	}

	void test_ast(curb_ast* root)
	{
		test_ast_print_tree(root, "", false);
	}

	void test_ir(const curb_ir_module& module)
	{
		printf("IR\n");
		for(const curb_ir_operation& operation : module.operations)
		{
			printf("%d\t\t%s", (int)operation.bytecode_offset, curb_opcode_labels[(int)operation.opcode]);
			for (curb_ir_operand operand : operation.operands)
			{
				switch (operand.type)
				{
				case CURB_IR_OPERAND_BOOL:
					printf(" %s", operand.data.b ? "true" : "false");
					break;
				case CURB_IR_OPERAND_INT:
					printf(" %d", operand.data.i);
					break;
				case CURB_IR_OPERAND_FLOAT:
					printf(" %f", operand.data.f);
					break;
				case CURB_IR_OPERAND_BYTES:
					printf(" %s", operand.data.str);
					break;
				case CURB_IR_OPERAND_NONE:
					break;
				}
			}
			printf("\n");
		}
	}

	void test_execute(curb_environment env, const char* filename)
	{
		printf("----------%s------------\n", filename );

		//test_lexer(filename);

		curb_ast* root = curb_parse_file(env, filename);
		if(root == nullptr)
			return;

		test_ast_print_tree(root);

		// Generate IR
		curb_ast_to_ir_context context;
		context.valid = true;
		context.env = env;

		curb_ir ir;
		curb_ast_to_ir(context, root, ir);

		test_ir(ir.module);

		// Cleanup
		curb_ast_delete(root);
	}
}


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

static int test_passed_count = 0;
static int test_total = 0;

// condition, messages ...
void expect(curb_value* return_value, const curb_array<curb_value*>& args)
{
	if (args.size() == 0)
		return;

	++test_total;

	if (curb_to_bool(args[0]))
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

int curb_test(int argc, char** argv)
{
#ifdef  _WIN32
	win32_memorycheck_scope memcheck;
#endif //_WIN32

	curb_environment env;
	curb_register(env, "print", print);
	curb_register(env, "expect", expect);


	for(int i = 1; i < argc; ++i)
	{
		if(argv[i] && strcmp(argv[i], "--dump") == 0)
		{
			if (++i >= argc)
			{
				printf("curb_test: --generate parameter expected filename!");
				return -1;
			}

			test_execute(env, argv[i]);
		}
		else if (argv[i] && strcmp(argv[i], "--exec") == 0)
		{
			if (++i >= argc)
			{
				printf("curb_test: --exec parameter expected filename!");
				return -1;
			}

			printf("[TEST  ]\t%s\n", argv[i]);
			curb_execute(env, argv[i]);
			printf("[TEST  ]\tEnded. Passed %d / %d\n", test_passed_count, test_total);
		}
	}

	return 0;
}