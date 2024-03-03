#if AUG_DEBUG

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

    aug_input* input = aug_input_open(filename, NULL);

    aug_lexer* lexer = aug_lexer_new(input);
    while (lexer && aug_lexer_move(lexer) && aug_lexer_curr(lexer).id != AUG_TOKEN_END)
    {
        aug_token curr = aug_lexer_curr(lexer);
        dump_token(&curr);
    }

    aug_lexer_delete(lexer);
    aug_input_close(input);

    printf("End Tokenizing File: %s\n", filename);
}

void dump_ast_tree(const aug_ast* node, aug_string* prev_prefix, bool is_leaf)
{
    if(node == NULL)
        return;

    static const char* space = "  ";// char(192);
    static const char* pipe = "| ";// char(192);
    static const char* pipe_junction = "|-";//char(195);
    static const char* pipe_end = "\\-";//char(179);

    aug_string* prefix = aug_string_create(prev_prefix->buffer);
    printf("%s", prefix->buffer);

    if(is_leaf)
    {
        printf("%s", pipe_end);
        aug_string_append_bytes(prefix, space, strlen(pipe));
    }
    else 
    {
        printf("%s", pipe_junction);
        aug_string_append_bytes(prefix, pipe, strlen(pipe));
    }

    aug_token token = node->token;
    aug_ast** children = node->children;
    const int children_size = node->children_size;

    printf("%s", aug_ast_type_label(node->type));
    if(token.data != NULL)
        printf(": %s", token.data->buffer);
    printf("\n");
    for (int i = 0; i < children_size; ++i)
        dump_ast_tree(children[i], prefix, i == children_size - 1);
    aug_string_decref(prefix);
}

void dump_ast(aug_ast* root)
{
    if (root == NULL)
        return;
    aug_string* prefix = aug_string_create("");
    dump_ast_tree(root, prefix, false);
    aug_string_decref(prefix);
}

void dump_ir(aug_ir* ir)
{
    assert(ir && ir->operations);
    printf("Bytecode\n");

    size_t i;
    for(i = 0; i < ir->operations->length; ++i)
    {
        aug_ir_operation operation = aug_container_at_type(aug_ir_operation, ir->operations, i);

        printf("%d\t\t%s", (int)operation.bytecode_offset, aug_opcode_label(operation.opcode));
        aug_ir_operand operand = operation.operand;
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
        case AUG_IR_OPERAND_SYMBOL:
        {
            aug_symbol* symbol = aug_hashtable_ptr_type(aug_symbol, ir->globals, operand.data.str);
            assert(symbol);
            printf(" %d:%s", symbol->offset, operand.data.str);
            break;
        }
        case AUG_IR_OPERAND_NONE:
            break;
        }

        int addr = (int)operation.bytecode_offset;
        size_t i;
        // TODO: index by address for faster lookup. Not priority as this will only occur on VM error 
        for (i = 0; i < ir->debug_symbols->length; ++i)
        {
            aug_debug_symbol debug_symbol = aug_container_at_type(aug_debug_symbol, ir->debug_symbols, i);
            if (debug_symbol.bytecode_addr == addr)
            {
                if (debug_symbol.symbol.name) printf("(%s)", debug_symbol.symbol.name->buffer);
                break;
            };
        }
        printf("\n");
    }
}

void aug_dump_file(aug_vm* vm, const char* filename)
{
    printf("----------%s Dump------------\n", filename);

    dump_lexer(filename);

    aug_input* input = aug_input_open(filename, vm->error_func);
    if(input == NULL)
        return;
    aug_ast* root = aug_parse(input);
    if(root == NULL)
        return;

    dump_ast(root);

    // Generate IR

       // Generate IR
    aug_ir* ir = aug_generate_ir(vm, input, root);
    dump_ir(ir);

    aug_ast_delete(root);
    aug_ir_delete(ir);
    aug_input_close(input);
}
#endif //AUG_DEBUG