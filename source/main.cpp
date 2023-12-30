#define SHL_IMPLEMENTATION
#define SHL_LOG_VERBOSE
#include "shl.h"

#ifdef  _WIN32
#include "windows.h"
#define _CRTDBG_MAP_ALLOC //to get more details
#endif //_WIN32

void lexer_print_token(const shl_token& token)
{
	std::cout << token.detail->label << "(" << token.data << ") " << token.line << ':' << token.col;
}

void lexer_test_file(const char* filename)
{
	std::cout << "Tokenizing\t" << filename << "\n";
	shl_lexer lexer;
	shl_lexer_open_file(lexer, filename);
	while (shl_lexer_move(lexer))
	{
		std::cout << "\tcurr ";
		lexer_print_token(lexer.curr);
		std::cout << "\n";

		if (lexer.curr.id == SHL_TOKEN_END)
			break;
	}
    shl_lexer_close(lexer);

	std::cout << "Done\n";
}

// -------------------------- parser -------------------------------------------
void ast_print_tree(shl_ast* node, std::string prefix = "", bool is_leaf = false)
{
	static const char* space = "  ";// char(192);
	static const char* pipe = "| ";// char(192);
	static const char* pipe_junction = "|-";//char(195);
	static const char* pipe_end = "\\-";//char(179);

	std::cout << prefix; 
	if(is_leaf)
	{
		std::cout << pipe_end;
		prefix += space;
	}
	else 
	{
		std::cout << pipe_junction;
		prefix += pipe;
	}

	const shl_token& token = node->token;
    const shl_array<shl_ast*>& children = node->children;

	switch(node->id)
	{
		case SHL_AST_ROOT:
			std::cout << "ROOT";
			assert(children.size()==1);
			ast_print_tree(children[0], "\n", true);
			std::cout << "\n";
		break;
		case SHL_AST_BLOCK:
			std::cout << "BLOCK";
			for (size_t i = 0; i < children.size(); ++i)
				ast_print_tree(children[i], prefix, i == children.size()-1);
		break;
		case SHL_AST_ASSIGN:
			std::cout << "ASSIGN:" << token.data;
			assert(children.size()==1);
			ast_print_tree(children[0], prefix, true);
		break;
		case SHL_AST_UNARY_OP:
			std::cout << token.detail->label;
			assert(children.size()==1);
			ast_print_tree(children[0], prefix, true);
		break;
		case SHL_AST_BINARY_OP:
			std::cout << token.detail->label;
			assert(children.size()==2);
			ast_print_tree(children[0], prefix, false);
			ast_print_tree(children[1], prefix, true);
		break;

		case SHL_AST_FUNC_CALL:
			std::cout << "FUNCCALL:" << token.data;
			for (size_t i = 0; i < children.size(); ++i)
				ast_print_tree(children[i], prefix, i == children.size()-1);
		break;
		case SHL_AST_VARIABLE:
		case SHL_AST_LITERAL:
			std::cout << token.data;
		break;
		case SHL_AST_FUNC_DEF:
		break;
		case SHL_AST_PARAM:
		break;
	}
}

void ir_print(const shl_ir_module& module)
{
	std::cout << "IR\n";
	for(const shl_ir_block& block : module.blocks)
	{
		std::cout << block.label << ":\n";
		for(const shl_ir_operation& operation : block.operations)
		{
			std::cout << '\t' << shl_opcode_labels[(int)operation.opcode];
			std::cout << ' ' << operation.operand.data;
			std::cout << '\n';
		}
	}
}

void run_test(const char* filename)
{
	std::cout << "----------" << filename << "------------\n";

	//lexer_test_file(filename);

	shl_ast* root = shl_parse_file(filename);
	if(root == nullptr)
	{
		std::cerr << "Failed to generate AST";
		return;
	}

	std::cout << "AST\n";
	ast_print_tree(root);

	shl_ir ir;
	shl_ast_to_ir(root, ir);
	shl_ast_delete(root);

	ir_print(ir.module);
}

int main(int argc, char** argv)
{
#ifdef  _WIN32
	_CrtMemState start_memstate;
	_CrtMemCheckpoint(&start_memstate); //take a snapshot
#endif //_WIN32

	for(int i = 1; i < argc; ++i)
	{
		if(argv[i] && strcmp(argv[i], "--test") == 0)
		{
			++i;
			if(argv[i] == nullptr)
			{
				std::cerr << "shl_test: -test param expected filename!";
				return -1;
			}
			run_test(argv[i]);
		}
	}

#ifdef  _WIN32
	_CrtMemState end_memstate, diff_memstate;
	_CrtMemCheckpoint(&end_memstate); //take a snapshot 
	if (_CrtMemDifference(&diff_memstate, &start_memstate, &end_memstate)) // if there is a difference
	{
		std::cout << "!!! MEMORY LEAK !!! Check debug output!";

		OutputDebugString(L"-----------_CrtMemDumpStatistics ---------");
		_CrtMemDumpStatistics(&diff_memstate);
		OutputDebugString(L"-----------_CrtMemDumpAllObjectsSince ---------");
		_CrtMemDumpAllObjectsSince(&start_memstate);
		OutputDebugString(L"-----------_CrtDumpMemoryLeaks ---------");
		_CrtDumpMemoryLeaks();
	}
#endif //_WIN32

	return 0;
}