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
			printf("\t%s (%s)%d:%d\n", lexer.curr.detail->label, lexer.curr.data.c_str(), lexer.curr.line, lexer.curr.col);
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
			case SHL_AST_ASSIGN:
				assert(children.size()==1);
				printf("ASSIGN:%s", token.data.c_str());
				test_ast_print_tree(children[0], prefix, true);
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
			break;
			case SHL_AST_PARAM:
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
		for(const shl_ir_block& block : module.blocks)
		{
			printf("\t%s:\n", block.label.c_str());
			for(const shl_ir_operation& operation : block.operations)
			{
				printf("\t\t%s", shl_opcode_labels[(int)operation.opcode]);
				for(shl_ir_operand operand : operation.operands)
					printf(" %s", operand.data.c_str());
				printf("\n");
			}
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

		shl_ir ir;
		shl_ast_to_ir(env, root, ir);
		shl_ast_delete(root);

		test_ir(ir.module);

		shl_execute(env, filename);
	}
}

void print(const shl_list<shl_value*>& args)
{
	for (shl_value* arg : args)
	{
		switch (arg->type)
		{
		case SHL_INT:
			printf("%ld", arg->i);
			break;
		case SHL_FLOAT:
			printf("%f", arg->f);
			break;
		case SHL_STRING:
			printf("%s", arg->str);
			break;
		case SHL_OBJECT:
			printf("object");
			break;
		}
	}

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

	for(int i = 1; i < argc; ++i)
	{
		if(argv[i] && strcmp(argv[i], "--test") == 0)
		{
			if (++i >= argc)
			{
				printf("shl_test: --test parameter expected filename!");
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

			shl_execute(env, argv[i]);
		}
	}
	return 0;
}