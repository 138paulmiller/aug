#define AUG_IMPLEMENTATION
#define AUG_LOG_VERBOSE
#define AUG_DEBUG
#include <aug.h>

void dump_lexer(const char* filename)
{
	printf("Tokens \n");

	aug_input* input = aug_input_open(filename, nullptr, true);

	aug_lexer* lexer = aug_lexer_new(input);
	while (lexer && aug_lexer_move(lexer) && lexer->curr.id != AUG_TOKEN_END)
	{
		printf("\tCURR: %s (%s) %d:%d\n", lexer->curr.detail->label, lexer->curr.data.c_str(), lexer->curr.pos.line, lexer->curr.pos.col);
	}

	aug_lexer_delete(lexer);
	aug_input_close(input);

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
		case AUG_AST_STMT_DEFINE_VAR:
			printf("DEFINE:%s\n", token.data.c_str());
			if(children.size()==1)
				dump_ast_tree(children[0], prefix, true);
			break;
		case AUG_AST_STMT_ASSIGN_VAR:
			assert(children.size() == 1);
			printf("ASSIGN%s\n",token.data.c_str() );
			dump_ast_tree(children[0], prefix, false);
			break;
		case AUG_AST_STMT_EXPR:
			printf("EXPR:%s\n", token.data.c_str());
			dump_ast_tree(children[0], prefix, true);
			break;
		case AUG_AST_STMT_IF:
			printf("IF\n");
			dump_ast_tree(children[0], prefix, false);
			dump_ast_tree(children[1], prefix, true);
			break;
		case AUG_AST_STMT_IF_ELSE:
			printf("IF\n");
			dump_ast_tree(children[0], prefix, false);
			dump_ast_tree(children[1], prefix, true);
			printf("ELSE\n");
			dump_ast_tree(children[2], prefix, true);
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
		case AUG_AST_ARRAY:
			printf("ARRAY\n");
			for (size_t i = 0; i < children.size(); ++i)
				dump_ast_tree(children[i], prefix, i == children.size() - 1);
			break;
		case AUG_AST_ELEMENT:
			printf("ELEMENT\n");
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
	if (root == nullptr)
		return;
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

void aug_dump_file(aug_vm vm, const char* filename)
{
	printf("----------%s------------\n", filename);

	//dump_lexer(filename);

	aug_input* input = aug_input_open(filename, vm.error_callback, true);
	aug_ast* root = aug_parse(vm, input);

	dump_ast(root);

	// Generate IR
	aug_ir ir;
    aug_ir_init(ir, input);
	aug_ast_to_ir(vm, root, ir);

	dump_bytecode(ir);

	// Cleanup
	aug_ast_delete(root);
	aug_input_close(input);
}
