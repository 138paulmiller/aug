#define AUG_IMPLEMENTATION
#define AUG_LOG_VERBOSE
#define AUG_DEBUG
#include <aug.h>

void dump_lexer(const char* filename)
{
	printf("Tokens \n");

	aug_environment env;
	aug_lexer lexer;
	aug_lexer_open_file(env, lexer, filename);
	while (aug_lexer_move(env, lexer) && lexer.curr.id != AUG_TOKEN_END)
	{
		//printf("\tPREV: %s (%s)%d:%d\n", lexer.prev.detail->label, lexer.prev.data.c_str(), lexer.prev.line, lexer.prev.col);
		printf("\tCURR: %s (%s) %d:%d\n", lexer.curr.detail->label, lexer.curr.data.c_str(), lexer.curr.line, lexer.curr.col);
		//printf("\tNEXT: %s (%s)%d:%d\n", lexer.next.detail->label, lexer.next.data.c_str(), lexer.next.line, lexer.next.col);
		printf("\n");
	}
	aug_lexer_close(lexer);

	printf("End Tokenizing File: %s\n", filename);
}

void dump_ast_tree(aug_ast* node, std::string prefix, bool is_leaf)
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

	const aug_token& token = node->token;
	const aug_std_array<aug_ast*>& children = node->children;

	switch(node->id)
	{
		case AUG_AST_ROOT:
			printf("AST\n");
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size() - 1);
			break;
		case AUG_AST_BLOCK:
			printf("BLOCK\n");
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size()-1);
			break;
		case AUG_AST_STMT_VAR:
			assert(children.size()==1);
			printf("VAR:%s\n", token.data.c_str());
			dump_ast_tree(children[0], prefix, true);
			break;
		case AUG_AST_STMT_ASSIGN:
			assert(children.size()==1);
			printf("ASSIGN:%s\n", token.data.c_str());
			dump_ast_tree(children[0], prefix, true);
			break;
		case AUG_AST_STMT_EXPR:
			printf("EXPR:%s\n", token.data.c_str());
			dump_ast_tree(children[0], prefix, true);
			break;
		case AUG_AST_STMT_IF:
			printf("IF:%s\n", token.data.c_str());
			dump_ast_tree(children[0], prefix, true);
			break;
		case AUG_AST_STMT_IF_ELSE:
			printf("IF:%s\n", token.data.c_str());
			dump_ast_tree(children[0], prefix, false);
			printf("ELSE:%s\n", token.data.c_str());
			dump_ast_tree(children[1], prefix, true);
			break;
		case AUG_AST_STMT_WHILE:
			printf("WHILE\n");
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size()-1);
			break;
		case AUG_AST_UNARY_OP:
			assert(children.size() == 1);
			printf("%s\n", token.detail->label);
			dump_ast_tree(children[0], prefix, true);
			break;
		case AUG_AST_BINARY_OP:
			assert(children.size() == 2);
			printf("%s\n", token.detail->label);
			dump_ast_tree(children[0], prefix, false);
			dump_ast_tree(children[1], prefix, true);
			break;
		case AUG_AST_FUNC_CALL:
			printf("FUNCCALL:%s\n", token.data.c_str());
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size()-1);
			break;
		case AUG_AST_FUNC_DEF:
			assert(children.size() == 2);
			printf("FUNCDEF:%s\n", token.data.c_str());
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size() - 1);
			break;
		case AUG_AST_PARAM_LIST:
			printf("PARAMS:%s\n", token.data.c_str());
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size() - 1);
			break;
		case AUG_AST_RETURN:
			printf("RETURN\n");
			if(children.size() == 1)
			dump_ast_tree(children[0], prefix, true);
			break;
		case AUG_AST_EXPR_LIST:
			printf("LIST\n");
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size() - 1);
			break;
		case AUG_AST_PARAM:
		case AUG_AST_VARIABLE:
		case AUG_AST_LITERAL:
			printf("%s\n", token.data.c_str());
			break;
	}
}

void dump_ast(aug_ast* root)
{
	dump_ast_tree(root, "", false);
}

void dump_bytecode(const aug_ir& ir)
{
	printf("Bytecode\n");
	for(const aug_ir_operation& operation : ir.operations)
	{
		printf("%d\t\t%s", (int)operation.bytecode_offset, aug_opcode_labels[(int)operation.opcode]);
		aug_ir_operand operand = operation.operand;
		switch (operand.type)
		{
		case AUG_IR_OPERAND_BOOL:
			printf(" %s", operand.data.b ? "true" : "false");
			break;
		case AUG_IR_OPERAND_INT:
			printf(" %d", operand.data.i);
			break;
		case AUG_IR_OPERAND_FLOAT:
			printf(" %f", operand.data.f);
			break;
		case AUG_IR_OPERAND_BYTES:
			printf(" %s", operand.data.str);
			break;
		case AUG_IR_OPERAND_NONE:
			break;
		}
		printf("\n");
	}
}

void aug_dump_file(aug_environment env, const char* filename)
{
	printf("----------%s------------\n", filename);

	//dump_lexer(filename);

	aug_ast* root = aug_parse_file(env, filename);
	if (root == nullptr)
		return;

	dump_ast(root);

	// Generate IR
	aug_ir ir;
    aug_ir_init(ir);
	aug_ast_to_ir(env, root, ir);

	dump_bytecode(ir);

	// Cleanup
	aug_ast_delete(root);
}