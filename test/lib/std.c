#define AUG_IMPLEMENTATION
#include <aug.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

static aug_vm* s_std_vm;

aug_value aug_std_random(int argc, aug_value* args)
{
	assert(argc == 0 || argc == 1 || argc == 2);

	int x;
	if(argc == 1)
		x = rand() % aug_to_int(args+0);
	else if(argc == 2)
	{
		const int lower = aug_to_int(args+0);
		const int upper = aug_to_int(args+1);
		x = rand() % (upper - lower + 1) + lower;
	}
	else
		x = rand();
	return aug_create_int(x);
}

void aug_std_print_value(const aug_value value);

void aug_std_print_map_pair(const aug_value* key, aug_value* value, void* user_data)
{
	printf("\n\t");
	aug_std_print_value(*key);
	printf(" : ");
	aug_std_print_value(*value);
}

void aug_std_print_value(const aug_value value)
{
	switch (value.type)
	{
	case AUG_NONE:
		printf("none");
		break;
	case AUG_BOOL:
		printf("%s", value.b ? "true" : "false");
		break;
	case AUG_CHAR:
		printf("%c", value.c);
		break;
	case AUG_INT:
		printf("%d", value.i);
		break;
	case AUG_FLOAT:
		printf("%0.3f", value.f);
		break;
	case AUG_STRING:
		printf("%s", value.str->buffer);
		break;
	case AUG_OBJECT:
		printf("object");
		break;
	case AUG_FUNCTION:
		printf("function %d", value.i);
		break;
	case AUG_ARRAY:
	{
		printf("[");
		for( size_t i = 0; i < value.array->length; ++i)
		{
			printf(" ");
			const aug_value* entry = aug_array_at(value.array, i);
			aug_std_print_value(*entry);
			if(entry->type == AUG_ARRAY) printf("\n");		
		}
		printf(" ]");
		break;
	}
	case AUG_MAP:
	{		
		printf("{");
		aug_map_foreach(value.map, aug_std_print_map_pair, NULL);
		printf("\n}");

		break;
	}
	default: break;
	}
}

aug_value aug_std_print(int argc, aug_value* args)
{
	for( int i = 0; i < argc; ++i)
		aug_std_print_value(args[i]);

	printf("\n");
	return aug_none();
}

aug_value aug_std_to_string(int argc, aug_value* args)
{
	assert(argc == 1);

	aug_value value = args[0];
   	char out[1024];
    switch (value.type)
    {
    case AUG_NONE:
    	return aug_none();
    case AUG_BOOL:
        snprintf(out, sizeof(out), "%s", value.b ? "true" : "false");
        break;
	case AUG_CHAR:
        snprintf(out, sizeof(out), "%c", value.c);
        break;
    case AUG_INT:
        snprintf(out, sizeof(out), "%d", value.i);
        break;
    case AUG_FLOAT:
        snprintf(out, sizeof(out), "%0.3f", value.f);
        break;
    case AUG_STRING:
        snprintf(out, sizeof(out), "%s", value.str->buffer);
        break;
    default:
    	return aug_none();
    }
	return aug_create_string(out);
}

aug_value aug_std_get(int argc, aug_value* args)
{
	assert(argc == 2);
	assert(args[0].type == AUG_MAP);

	aug_value* elem = aug_map_get(args[0].map, args + 1);
	if(elem == NULL)
		return aug_none();
	aug_incref(elem);
	return *elem;
}

aug_value aug_std_exists(int argc, aug_value* args)
{
	assert(argc == 2);
	assert(args[0].type == AUG_MAP);

	aug_value* elem = aug_map_get(args[0].map, args + 1);
	return aug_create_bool(elem != NULL);
}

aug_value aug_std_concat(int argc, aug_value* args)
{
	assert (argc >= 0);

	aug_value value = aug_create_string("");
	for (int i = 0; i < argc; ++i)
	{
		aug_value* arg = args + i;
		switch(arg->type)
		{
			case AUG_CHAR:
				aug_string_push(value.str, arg->c);
				break;
			case AUG_STRING:
				aug_string_append(value.str, arg->str);
				break;
			default: break;
		}
	}
	return value;
}

aug_value aug_std_split(int argc, aug_value* args)
{
	assert(argc == 2);
	assert(args[0].type == AUG_STRING && args[1].type == AUG_STRING);

	aug_value value = aug_create_array();
	aug_string* str = args[0].str;
	aug_string* delim = args[1].str;
	aug_value line = aug_create_string("");
	for (size_t i = 0; i < str->length; ++i)
	{
		char c = str->buffer[i];
		if( c == delim->buffer[0])
		{
			size_t j = 0;
			while(j < str->length &&  j < delim->length)
			{
				if(str->buffer[i+j] != delim->buffer[j])
					break;
				++j;
			}

			if(j == delim->length)
			{				
				aug_array_append(value.array, &line);
				line = aug_create_string("");
				i += (j - 1); // -1 to account for the ++i in the for loop 
			}
		}
		else
		{
			aug_string_push(line.str, c);
		}
	}
	
	aug_array_append(value.array, &line);
	return value;
}

aug_value aug_std_append(int argc, aug_value* args)
{
	assert (argc > 0);

	aug_value value = args[0];
	for (int i = 1; i < argc; ++i)
	{
		aug_value* arg = args + i;

		switch(value.type)
		{
		case AUG_ARRAY:
			aug_array_append(value.array, arg);
			break;
		case AUG_STRING:
		{
			switch(arg->type)
			{
				case AUG_CHAR:
					aug_string_push(value.str, arg->c);
					break;
				case AUG_STRING:
					aug_string_append(value.str, arg->str);
					break;
				default: break;
			}
			break;
		}
		default: break;
		} 
	}
	return aug_none();
}

aug_value aug_std_remove(int argc, aug_value* args)
{
	assert(argc == 2);
	assert(args[0].type == AUG_ARRAY);

	aug_value value = args[0];
	aug_value index = args[1];
	aug_array_remove(value.array, aug_to_int(&index));
	return aug_none();
}

aug_value aug_std_front(int argc, aug_value* args)
{
	assert(argc == 1);
	assert(args[0].type == AUG_ARRAY);

	aug_value value = args[0];
	aug_value* element = aug_array_at(value.array, 0);
	aug_incref(element);
	if(element != NULL)
		return *element;
	return aug_none();
}

aug_value aug_std_back(int argc, aug_value* args)
{
	assert(argc == 1);
	assert(args[0].type == AUG_ARRAY);

	aug_value value = args[0];
	aug_value* element = aug_array_at(value.array, value.array->length - 1);
	aug_incref(element);
	if(element != NULL)
		return *element;
	return aug_none();
}

aug_value aug_std_length(int argc, aug_value* args)
{
	assert(argc == 1);

	aug_value value = args[0];
	switch (value.type)
	{
	case AUG_STRING:
		return aug_create_int(value.str->length);
	case AUG_ARRAY:
		return aug_create_int(value.array->length);
	case AUG_MAP:
		return aug_create_int(value.map->count);
	default: break;
	}
	return aug_none();
}

aug_value aug_std_contains(int argc, aug_value* args)
{
	assert(!((argc != 2 || argc != 4) && args[0].type != AUG_ARRAY));

	aug_value value = args[0];
	aug_value arg = args[1];

	size_t start = 0;
	size_t end = value.array->length;

	if(argc == 4)
	{		
		if(args[2].type != AUG_INT || args[3].type != AUG_INT)
			return aug_none();
		start = aug_to_int(args + 2);
		end = aug_to_int(args + 3);
	}

	for (size_t i = start; i < end; ++i){
		aug_value* element = aug_array_at(value.array, i);
		if(aug_compare(&arg, element))
			return aug_create_bool(true);
	}
	return aug_create_bool(false);
}

aug_value aug_std_snap(int argc, aug_value* args)
{
	assert(argc == 2);

	int x = aug_to_int(args + 0);
	int grid = aug_to_int(args + 1);
	return aug_create_int(floor(x / grid) * grid);
}

aug_value aug_std_floor(int argc, aug_value* args)
{
	assert(argc == 1);

	float x = aug_to_float(args);
	return aug_create_int(floor(x));
}


aug_value aug_std_swap(int argc, aug_value* args)
{
	assert(argc == 2);

	aug_value temp = args[0];
	args[0] = args[1];
	args[1] = temp;
	return aug_none();
}

void core_basedir(char* path, const char* file)
{
    int i = 0;
    while(file && *file != '\0')
        path[i++] = *file++;
    
    while(i >= 0 && path[i] != '/' && path[i] != '\\')
        --i;
    ++i;

    path[i] = '\0';
}

void core_makepath(char* path, const char* base, const char* file)
{
    int i = 0;
    while(base && *base != '\0')
        path[i++] = *base++;
    
    while(i >= 0 && path[i] != '/' && path[i] != '\\')
        --i;
    ++i;
    
    while(file && *file != '\0')
        path[i++] = *file++;
    path[i] = '\0';
}

aug_value aug_std_exec(int argc, aug_value* args)
{
	assert(argc == 1);
	assert(args[0].type == AUG_STRING);

	char syspath[1024] = {0};
	core_basedir(syspath, s_std_vm->exec_filepath);
	core_makepath(syspath, syspath, args[0].str->buffer);

	aug_vm_exec_state exec_state;
	aug_save_state(s_std_vm, &exec_state);
	aug_execute(s_std_vm, syspath);
    aug_load_state(s_std_vm, &exec_state);
	return aug_none();
}

AUG_LIB aug_register_lib(aug_vm* vm)
{
	s_std_vm = vm;
	aug_register(vm, "exec",      aug_std_exec      );
	aug_register(vm, "snap",      aug_std_snap      );
	aug_register(vm, "swap",      aug_std_swap      );
	aug_register(vm, "floor",     aug_std_floor     );
	aug_register(vm, "random",    aug_std_random    );
	aug_register(vm, "print",     aug_std_print     );
	aug_register(vm, "get",       aug_std_get       );
	aug_register(vm, "exists",    aug_std_exists    );
	aug_register(vm, "to_string", aug_std_to_string );
	aug_register(vm, "concat",    aug_std_concat    );
	aug_register(vm, "append",    aug_std_append    );
	aug_register(vm, "remove",    aug_std_remove    );
	aug_register(vm, "front",     aug_std_front     );
	aug_register(vm, "back",      aug_std_back      );
	aug_register(vm, "length",    aug_std_length    ) ;
	aug_register(vm, "contains",  aug_std_contains  );
	aug_register(vm, "split",     aug_std_split     );
}

#ifdef __cplusplus
}
#endif

