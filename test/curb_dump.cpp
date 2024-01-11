#define CURB_IMPLEMENTATION
#define CURB_LOG_VERBOSE
#include <curb.h>

void dump_lexer(const char* filename)
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

void dump_ast_tree(curb_ast* node, std::string prefix, bool is_leaf)
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
			printf("AST\n");
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size() - 1);
			break;
		case CURB_AST_BLOCK:
			printf("BLOCK\n");
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size()-1);
			break;
		case CURB_AST_STMT_ASSIGN:
			assert(children.size()==1);
			printf("ASSIGN:%s\n", token.data.c_str());
			dump_ast_tree(children[0], prefix, true);
			break;
		case CURB_AST_STMT_EXPR:
			dump_ast_tree(children[0], prefix, true);
			break;
		case CURB_AST_STMT_IF:
			printf("IF:%s\n", token.data.c_str());
			dump_ast_tree(children[0], prefix, true);
			break;
		case CURB_AST_STMT_IF_ELSE:
			printf("IF:%s\n", token.data.c_str());
			dump_ast_tree(children[0], prefix, false);
			printf("ELSE:%s\n", token.data.c_str());
			dump_ast_tree(children[1], prefix, true);
			break;
		case CURB_AST_STMT_WHILE:
			printf("WHILE\n");
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size()-1);
			break;
		case CURB_AST_UNARY_OP:
			assert(children.size() == 1);
			printf("%s\n", token.detail->label);
			dump_ast_tree(children[0], prefix, true);
			break;
		case CURB_AST_BINARY_OP:
			assert(children.size() == 2);
			printf("%s\n", token.detail->label);
			dump_ast_tree(children[0], prefix, false);
			dump_ast_tree(children[1], prefix, true);
			break;
		case CURB_AST_FUNC_CALL:
			printf("FUNCCALL:%s\n", token.data.c_str());
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size()-1);
			break;
		case CURB_AST_FUNC_DEF:
			assert(children.size() == 2);
			printf("FUNCDEF:%s\n", token.data.c_str());
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size() - 1);
			break;
		case CURB_AST_PARAM_LIST:
			printf("PARAMS:%s\n", token.data.c_str());
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size() - 1);
			break;
		case CURB_AST_RETURN:
			printf("RETURN\n");
			if(children.size() == 1)
			dump_ast_tree(children[0], prefix, true);
			break;
		case CURB_AST_PARAM:
		case CURB_AST_VARIABLE:
		case CURB_AST_LITERAL:
			printf("%s\n", token.data.c_str());
			break;
	}
}

void dump_ast(curb_ast* root)
{
	dump_ast_tree(root, "", false);
}

void dump_bytecode(const curb_ir& ir)
{
	printf("Bytecode\n");
	for(const curb_ir_operation& operation : ir.operations)
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

void curb_dump_file(curb_environment env, const char* filename)
{
	printf("----------%s------------\n", filename);

	//dump_lexer(filename);

	curb_ast* root = curb_parse_file(env, filename);
	if (root == nullptr)
		return;

	dump_ast(root);

	// Generate IR
	curb_ast_to_ir_context context;
	context.valid = true;
	context.env = env;

	curb_ir ir;
	curb_ast_to_ir(context, root, ir);

	dump_bytecode(ir);

	// Cleanup
	curb_ast_delete(root);
}
