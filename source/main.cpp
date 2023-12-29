#define SHL_IMPLEMENTATION
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
	std::cout << "Tokenizing" << filename << "\n";
	shl_lexer lexer;
	lexer.open_file(filename);
	while (lexer.move())
	{
		//std::cout << "\t\tprev ";
		//lexer_print_token(lexer.prev());

		std::cout << "\t\tcurr ";
		lexer_print_token(lexer.curr());

		//std::cout << "\t\tnext ";
		//lexer_print_token(lexer.next());

		std::cout << "\n";

		if (lexer.curr().id == shl_token_id::NONE)
			break;
	}
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

	switch(node->type)
	{
		case shl_ast::ROOT:
			std::cout << "ROOT";
			assert(children.size()==1);
			ast_print_tree(children[0], "\n", true);
			std::cout << "\n";
		break;
		case shl_ast::BLOCK:
			std::cout << "BLOCK";
			for (size_t i = 0; i < children.size(); ++i)
				ast_print_tree(children[i], prefix, i == children.size()-1);
		break;
		case shl_ast::ASSIGN:
			std::cout << "ASSIGN:" << token.data;
			assert(children.size()==1);
			ast_print_tree(children[0], prefix, true);
		break;
		case shl_ast::UNARY_OP:
			std::cout << token.detail->label;
			assert(children.size()==1);
			ast_print_tree(children[0], prefix, true);
		break;
		case shl_ast::BINARY_OP:
			std::cout << token.detail->label;
			assert(children.size()==2);
			ast_print_tree(children[0], prefix, false);
			ast_print_tree(children[1], prefix, true);
		break;

		case shl_ast::FUNC_CALL:
			std::cout << "FUNCCALL:" << token.data;
			for (size_t i = 0; i < children.size(); ++i)
				ast_print_tree(children[i], prefix, i == children.size()-1);
		break;
		case shl_ast::VARIABLE:
		case shl_ast::LITERAL:
			std::cout << token.data;
		break;
		case shl_ast::FUNC_DEF:
		break;
		case shl_ast::PARAM:
		break;
	}
}

void ir_print(const shl_ir_module* module)
{
	std::cout << "IR\n";
	for(const shl_ir_block& block : module->blocks)
	{
		std::cout << block.label << ":\n";
		for(const shl_ir_operation& operation : block.operations)
		{
			std::cout << '\t' << shl_opcode_labels[(int)operation.opcode];
			if(operation.operand.valid())
				std::cout << ' ' << operation.operand.data;
			std::cout << '\n';
		}
	}
}

void run_test(const char* filename)
{
	lexer_test_file(filename);

	shl_ast* root = shl_parse_file(filename);
	if(root == nullptr)
	{
		std::cerr << "Failed to generate AST";
		return;
	}

	std::cout << "AST\n";
	ast_print_tree(root);

	shl_ir_builder builder;
	shl_ast_to_ir(root, &builder);
	delete root;

	shl_ir_module* module = builder.get_module();
	if(module == nullptr)
	{
		std::cerr << "Failed to generate IR";
		return;
	}

	ir_print(module);
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