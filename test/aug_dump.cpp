
#ifdef  _WIN32
// compile by using: cl /EHsc /W4 /D_DEBUG /MDd
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif  //_WIN32

#define AUG_IMPLEMENTATION
#define AUG_LOG_VERBOSE
#define AUG_DEBUG
#include <aug.h>
#include <string>

void dump_token(aug_token* token)
{
	printf("%ld:%ld %s\t%s\n", 
		token->pos.line, token->pos.col,
		token->detail ? token->detail->label : "",
		token->data ? token->data->buffer : "" 
	);
}

void dump_lexer(const char* filename)
{
	return;
	printf("Tokens \n");

	aug_input* input = aug_input_open(filename, nullptr);

	aug_lexer* lexer = aug_lexer_new(input);
	while (lexer && aug_lexer_move(lexer) && lexer->curr.id != AUG_TOKEN_END)
	{
		dump_token(&lexer->curr);
	}

	aug_lexer_delete(lexer);
	aug_input_close(input);

	printf("End Tokenizing File: %s\n", filename);
}

void dump_ast_tree(const aug_ast* node, std::string prefix, bool is_leaf)
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

	aug_token token = node->token;
	aug_ast** children = node->children;
	const int children_size = node->children_size;

	printf("[%d]", node->id);

	switch(node->id)
	{
		case AUG_AST_ROOT:
			printf("AST\n");
			for (int i = 0; i < children_size; ++i)
				dump_ast_tree(children[i], prefix, i == children_size-1);
			break;
		case AUG_AST_BLOCK:
			printf("BLOCK\n");
			for (int i = 0; i < children_size; ++i)
				dump_ast_tree(children[i], prefix, i == children_size-1);
			break;
		case AUG_AST_STMT_DEFINE_VAR:
			printf("DEFINE: %s\n", token.data ?  token.data->buffer : "(null)");
			if(children_size==1)
				dump_ast_tree(children[0], prefix, true);
			break;
		case AUG_AST_STMT_ASSIGN_VAR:
			assert(children_size == 1);
			printf("ASSIGN: %s\n", token.data ?  token.data->buffer : "(null)");
			dump_ast_tree(children[0], prefix, false);
			break;
		case AUG_AST_STMT_EXPR:
			printf("EXPR:\n");
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
			for (int i = 0; i < children_size; ++i)
				dump_ast_tree(children[i], prefix, i == children_size-1);
			break;
		case AUG_AST_UNARY_OP:
			assert(children_size == 1);
			printf("%s\n", token.detail->label);
			dump_ast_tree(children[0], prefix, true);
			break;
		case AUG_AST_BINARY_OP:
			assert(children_size == 2);
			printf("%s\n", token.detail->label);
			dump_ast_tree(children[0], prefix, false);
			dump_ast_tree(children[1], prefix, true);
			break;
		case AUG_AST_FUNC_CALL:
			printf("FUNCCALL: %s\n", token.data ?  token.data->buffer : "(null)");
			for (int i = 0; i < children_size; ++i)
				dump_ast_tree(children[i], prefix, i == children_size-1);
			break;
		case AUG_AST_STMT_DEFINE_FUNC:
			assert(children_size == 2);
			printf("FUNCDEF: %s\n",token.data ?  token.data->buffer : "(null)");
			for (int i = 0; i < children_size; ++i)
				dump_ast_tree(children[i], prefix, i == children_size - 1);
			break;
		case AUG_AST_PARAM_LIST:
			printf("PARAMS\n");
			for (int i = 0; i < children_size; ++i)
				dump_ast_tree(children[i], prefix, i == children_size - 1);
			break;
		case AUG_AST_RETURN:
			printf("RETURN\n");
			if(children_size == 1)
			dump_ast_tree(children[0], prefix, true);
			break;
		case AUG_AST_ARRAY:
			printf("ARRAY\n");
			for (int i = 0; i < children_size; ++i)
				dump_ast_tree(children[i], prefix, i == children_size - 1);
			break;
		case AUG_AST_ELEMENT:
			printf("ELEMENT\n");
			for (int i = 0; i < children_size; ++i)
				dump_ast_tree(children[i], prefix, i == children_size - 1);
			break;
		case AUG_AST_PARAM:
		case AUG_AST_VARIABLE:
		case AUG_AST_LITERAL:
			if(token.id == AUG_TOKEN_TRUE)
				printf("true\n");
			else if(token.id == AUG_TOKEN_FALSE)
				printf("false\n");
			else
				printf("%s\n", token.data->buffer);
			break;
	}
}

void dump_ast(aug_ast* root)
{
	if (root == nullptr)
		return;
	dump_ast_tree(root, "", false);
}

void dump_ir(aug_ir* ir)
{
	printf("Bytecode\n");

    size_t i;
   	for(i = 0; i < ir->operations.length; ++i)
    {
		aug_ir_operation* operation = aug_ir_operation_array_at(&ir->operations, i);


		printf("%d\t\t%s", (int)operation->bytecode_offset, aug_opcode_labels[(int)operation->opcode]);
		aug_ir_operand operand = operation->operand;
		switch (operand.type)
		{
		case AUG_IR_OPERAND_BOOL:
			printf(" %s", operand.data.b ? "true" : "false");
			break;
		case AUG_IR_OPERAND_CHAR:
			printf(" %c", operand.data.c);
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

#ifdef AUG_DEBUG_SYMBOLS
		aug_debug_symbol debug_symbol = aug_debug_symbols_get(ir->debug_symbols, (int)operation->bytecode_offset);
		if (debug_symbol.symbol.name) printf("(%s)", debug_symbol.symbol.name->buffer);
#endif //AUG_DEBUG_SYMBOLS
		printf("\n");
	}
}

void aug_dump_file(aug_vm* vm, const char* filename)
{
	printf("----------%s Dump------------\n", filename);

	dump_lexer(filename);

	aug_input* input = aug_input_open(filename, vm->error_callback);
	aug_ast* root = aug_parse(input);

	dump_ast(root);

	// Generate IR
	aug_ir* ir = aug_ir_new(input);
	aug_ast_to_ir(vm, root, ir);

	dump_ir(ir);

	aug_ast_delete(root);
	aug_ir_delete(ir);
	aug_input_close(input);
}
