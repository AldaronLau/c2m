#include <stdio.h>

// RWOPS support ( includes thread & timer support )
#define _GNU_SOURCE
#include <sys/mman.h>
// Timer
#include "../SDL2-c2m/src/timer/windows/SDL_systimer.c"
#include "../SDL2-c2m/src/timer/unix/SDL_systimer.c"
#include "../SDL2-c2m/src/timer/haiku/SDL_systimer.c"
#include "../SDL2-c2m/src/timer/psp/SDL_systimer.c"

//
#include "../SDL2-c2m/src/SDL_hints.c"
#include "../SDL2-c2m/src/atomic/SDL_atomic.c"
#include "../SDL2-c2m/src/atomic/SDL_spinlock.c"

#include "../SDL2-c2m/src/thread/psp/SDL_syscond.c"
#include "../SDL2-c2m/src/thread/psp/SDL_sysmutex.c"
#include "../SDL2-c2m/src/thread/psp/SDL_syssem.c"
#include "../SDL2-c2m/src/thread/psp/SDL_systhread.c"

#include "../SDL2-c2m/src/thread/pthread/SDL_syscond.c"
#include "../SDL2-c2m/src/thread/pthread/SDL_sysmutex.c"
#include "../SDL2-c2m/src/thread/pthread/SDL_syssem.c"
#include "../SDL2-c2m/src/thread/pthread/SDL_systhread.c"
#include "../SDL2-c2m/src/thread/pthread/SDL_systls.c"

#include "../SDL2-c2m/src/thread/windows/SDL_sysmutex.c"
#include "../SDL2-c2m/src/thread/windows/SDL_syssem.c"
#include "../SDL2-c2m/src/thread/windows/SDL_systhread.c"
#include "../SDL2-c2m/src/thread/windows/SDL_systls.c"

//
#include "../SDL2-c2m/src/thread/SDL_thread.c"
#include "../SDL2-c2m/src/SDL_log.c"
#include "../SDL2-c2m/src/SDL_error.c"
#include "../SDL2-c2m/src/stdlib/SDL_getenv.c"
#include "../SDL2-c2m/src/stdlib/SDL_stdlib.c"
#include "../SDL2-c2m/src/stdlib/SDL_string.c"
#include "../SDL2-c2m/src/stdlib/SDL_malloc.c"
#include "../SDL2-c2m/src/file/SDL_rwops.c"

// String support ( includes Clump Array )
#include "c2m_string.c"
// Clump List
//#include "../clump/src/pool.c"
//#include "../clump/src/list.c"

enum {
	TYPE_STRING,
	TYPE_UBYTE,
	TYPE_SBYTE,
	TYPE_USHORT,
	TYPE_SSHORT,
	TYPE_UINT32,
	TYPE_SINT32,
	TYPE_UINT64,
	TYPE_SINT64,
	TYPE_FLOAT32,
	TYPE_FLOAT64,
	TYPE_POINTER,
	TYPE_INTEGER,
}c2m_type_t;

typedef struct{
	char* module; // 24 + null byte
	char* function;
	void* next;
}c2m_func_t;

typedef struct{
	char* name;
	char* version;
	char* creator;
	char* library;
	struct cl_array* main;
	struct cl_array* functions;
	struct cl_array* libfuncs;
	struct cl_array* varnames;
	uint8_t in_main;
	uint8_t in_func;
	uint8_t return_success;
	uint32_t goto_count;
	uint32_t block_count;
	struct{
		uint8_t stdlib;
		uint8_t stdio;
		uint8_t clump;
		uint8_t sdl;
		uint8_t sdl_window;
		uint8_t sdl_audio;
	}libreq;
	c2m_func_t* imports;
}c2m_t;

static void c2m_abort(const char* reason) {
	printf("Aborting because: \"%s\"\n", reason);
	exit(1);
}

void c2m_skip_whitespace(int32_t* i, const char* string) {
	char value;

	value = string[*i];
	while(value == ' ' || value == '\t') {
		(*i) = (*i) + 1;
		value = string[*i];
	}
}

// Returns 1 if not what expected.
static uint8_t c2m_expect(int32_t* i, const char* string, char* what) {
	if(strncmp(&string[*i], what, strlen(what))) {
		return 1;
	}

	*i += strlen(what);
	return 0;
}

static uint8_t c2m_checknum(int32_t* i, const char* string, char** dest) {
	char temp[256];
	uint8_t howmany = 0;
	if(string[*i] >= '0' && string[*i] <= '9') {
		while(string[*i] >= '0' && string[*i] <= '9') {
			temp[howmany] = string[*i];
			howmany++;
			if(howmany == 255) c2m_abort("Maxed out number length");
			*i = *i + 1;
		}
		printf("Malloc....\n");
		*dest = malloc(howmany + 1);
		printf("Malloc done....\n");
		memcpy(*dest, temp, howmany);
		(*dest)[howmany] = 0;
		return 0;
	}
	return 1;
}

static uint32_t c2m_count(int32_t* i, const char* string, char what) {
	int count = 0;

	for(int j = *i; j < strlen(string); j++) {
		if(string[j] == what)
			return j - *i;
		if(string[j] == '\n' || string[j] == '\0')
			return 0;
	}
//	printf("Expected character \"%c\":", what);
//	printf("%s\n", &string[*i]);
	return 0;
}

static inline
void c2m_read(int32_t* i, const char* string, char* dest, uint32_t len) {
	memcpy(dest, &string[*i], len), dest[len] = 0;
	*i += len;
}

static uint8_t
c2m_process_value(int32_t* i, const char* string, char** dest, uint8_t* type) {
	// Probably more whitespace
	c2m_skip_whitespace(i, string);
	// Check for type
	if(c2m_expect(i, string, "\"") == 0) {
		*type = TYPE_STRING;
		// String Declaration
		uint32_t len = c2m_count(i, string, '\"');
		if(len == 0) c2m_abort("closing double quote is missing");
		*dest = malloc(len + 1);
		c2m_read(i, string, *dest, len);
		if(c2m_expect(i, string, "\"\n")) {
			c2m_expect(i, string, "\"");
			c2m_skip_whitespace(i, string);
			if(c2m_expect(i, string, "+") == 0) {
				uint8_t type2 = 0;
				char* dest2 = NULL;
				if(c2m_process_value(i, string, &dest2, &type2))
					c2m_abort("Unrecognized value");
				printf("Processed append onto string....");
				printf("O: %s\n", dest2);
				if(type2 == TYPE_INTEGER) {
					*dest = realloc(*dest,
						len + strlen(dest2) + 1);
					memcpy(*dest + len, dest2, strlen(dest2));
				}
			}
		}
	}else if(c2m_expect(i, string, "TRUE") == 0) {
		*type = TYPE_UBYTE;
		// Byte declaration: 1
		*dest = malloc(2);
		(*dest)[0] = '1', (*dest)[1] = 0;
		c2m_expect(i, string, "\n");
	}else if(c2m_expect(i, string, "FALSE") == 0) {
		*type = TYPE_UBYTE;
		// Byte declaration: 0
		*dest = malloc(2);
		(*dest)[0] = '0', (*dest)[1] = 0;
		c2m_expect(i, string, "\n");
	}else if(c2m_checknum(i, string, dest) == 0) {
		*type = TYPE_INTEGER;
		c2m_skip_whitespace(i, string);
		printf("processed number %s", *dest);
		if(string[*i] != '\0') {
			printf("ERROR: %d %c", string[*i], string[*i]);
			c2m_abort("Not null after integer!");
		}
	}else{
		const char* what = &string[*i];
		*dest = malloc(strlen(what) + 1);
		memcpy(*dest, what, strlen(what) + 1);
		*i = *i + strlen(what);
		c2m_expect(i, string, "\n");
//		(*dest)[0] = '0', (*dest)[strlen(what)] = 0;
//		printf("not proper at all %s\n", &string[*i]);
		// Not a proper declaration.
//		return 1;
	}
	return 0;
}

/*
 * Returns 1 if not a variable declaration.
*/
static uint8_t c2m_declare_var(int32_t* i, const char* string, char** dest) {
	// After name probably whitespace
	c2m_skip_whitespace(i, string);
	// Check for equals
	if(c2m_expect(i, string, "=")) return 1;
	//
	uint8_t type = 0;
	return c2m_process_value(i, string, dest, &type);
}

void c2m_gconfig(c2m_t* c2m) {
	SDL_RWops *file;

	if((file = SDL_RWFromFile("c2m.config", "rb")) == NULL) {
		c2m_abort("No c2m.config found!");
	}

	uint64_t size = SDL_RWsize(file);
	char filecontents[size + 1];

	SDL_RWread(file, filecontents, size, 1);
	for(int32_t i = 0; i < size;) {
		if(strncmp(&filecontents[i], "name", 4) == 0) {
			i += 4;
			if(c2m_declare_var(&i, filecontents, &c2m->name))
				c2m_abort("Improper variable declaration.");
		}else if(strncmp(&filecontents[i], "version", 7) == 0) {
			i += 7;
			if(c2m_declare_var(&i, filecontents, &c2m->version))
				c2m_abort("Improper variable declaration.");
		}else if(strncmp(&filecontents[i], "creator", 7) == 0) {
			i += 7;
			if(c2m_declare_var(&i, filecontents, &c2m->creator))
				c2m_abort("Improper variable declaration.");
		}else if(strncmp(&filecontents[i], "library", 7) == 0) {
			i += 7;
			if(c2m_declare_var(&i, filecontents, &c2m->library))
				c2m_abort("Improper variable declaration.");
		}else{
			break;
		}
	}
//	printf("name = '%s'\n", c2m->name);
//	printf("version = '%s'\n", c2m->version);
//	printf("creator = '%s'\n", c2m->creator);
//	printf("library = '%s'\n", c2m->library);
}

static inline void c2m_output(SDL_RWops *output, const char* string) {
	if(SDL_RWwrite(output, string, 1, strlen(string)) != strlen(string)) {
		c2m_abort("Failed to write");
	}
}

void c2m_modular_func_call(c2m_t* c2m, int32_t* i, const char* string,
	struct cl_array* mof)
{
	c2m_skip_whitespace(i, string);
	uint32_t len = c2m_count(i, string, '.');
	if(len == 0) {
		printf("Where = %s", &string[*i]);
		c2m_abort("no module function separator");
	}
	char* module_name = malloc(len);
	c2m_read(i, string, module_name, len);
	c2m_expect(i, string, ".");
	uint32_t len2 = c2m_count(i, string, '(');
	if(len == 0) c2m_abort("need an opening parenthesis");
	char* function_name = malloc(len2);
	c2m_read(i, string, function_name, len2);
//	c2m_expect(i, string, "\n");
	fputs("Import module: ", stdout);
	fputs(module_name, stdout);
	fputs(" & Function: ", stdout);
	fputs(function_name, stdout);
	fputs("\n", stdout);
	uint8_t found_module = 0;
	// Search for module
	c2m_func_t** current = &c2m->imports;
	while(1) {
		if(*current == NULL) {
			*current = malloc(sizeof(c2m_func_t));
			(*current)->module = module_name;
			(*current)->function = function_name;
			(*current)->next = NULL;
			break;
		}
		if(strcmp((*current)->module, module_name) == 0 &&
		   strcmp((*current)->function, function_name) == 0)
		{
			free(module_name);
			free(function_name);
			module_name = (*current)->module;
			function_name = (*current)->function;
			break;
		}
		current = (void*)&((*current)->next);
	}
	c2m_string_append(mof, module_name);
	c2m_string_append(mof, "__");
	c2m_string_append(mof, function_name);
	c2m_string_append(mof, "(");
	if(c2m_expect(i, string, "("))
		c2m_abort("No opening parenthesis after function call");
	
	uint32_t len3 = c2m_count(i, string, ')');
	if(len == 0) c2m_abort("No closing parenthesis for fn call");
	char* parameter = malloc(len3);
	c2m_read(i, string, parameter, len3);
	uint8_t add_comma = 0;
	
	for(int32_t k = 0; k < len3;) {
		char* returnv = NULL;
		uint8_t type = 0;
		c2m_process_value(&k, parameter, &returnv, &type);
		if(add_comma) {
			c2m_string_append(mof, ",");
		}
		if(type == TYPE_STRING) {
			c2m_string_append(mof, "\"");
			c2m_string_append(mof, returnv);
			c2m_string_append(mof, "\"");
		}else{
			c2m_abort("Unsupported type");
		}
		free(returnv);
		add_comma = 1;
	}
	c2m_string_append(mof, ");\n");
	if(c2m_expect(i, string, ")\n"))
		c2m_abort("Missing newline after function call");
}

void c2m_infunc(c2m_t* c2m, int32_t* i, const char* string, struct cl_array* a){
	c2m_skip_whitespace(i, string);
	if(c2m_expect(i, string, "while") == 0) {
		c2m->goto_count++;
		uint32_t add = c2m->goto_count;

		c2m_string_append(a, "C2M_WHILE");
		while(add) {
			char lnum = '0' + (add % 10);
			add /= 10;
			char dest[2];
			dest[0] = lnum;
			dest[1] = '\0';
			c2m_string_append(a, dest);
		}
		c2m_string_append(a, ":\n");
		c2m->block_count++;

		c2m_skip_whitespace(i, string);
		if(c2m_expect(i, string, "{\n")) {
			c2m_abort("Missing bracket + newline for while loop.");
		}
	}else if(c2m_expect(i, string, "exit\n") == 0) {
		c2m->libreq.stdlib = 1;
		c2m_string_append(a, "exit(0);");
	}else if(c2m_expect(i, string, "fail\n") == 0) {
		c2m->libreq.stdlib = 1;
		c2m_string_append(a, "exit(1);");
	}else if(c2m_expect(i, string, "}") == 0) {
		printf("CLOSE BRACKET;\n");
		if(c2m->block_count) {
			c2m_string_append(a, "goto ");
		// TODO: Repeated code in while
		uint32_t add = c2m->goto_count;

		c2m_string_append(a, "C2M_WHILE");
		while(add) {
			char lnum = '0' + (add % 10);
			add /= 10;
			char dest[2];
			dest[0] = lnum;
			dest[1] = '\0';
			c2m_string_append(a, dest);
		}
		//
			c2m_string_append(a, ";\n");
			c2m->block_count--;
		}else{
			c2m->in_func = 0;
			c2m_string_append(a, "}\n");
		}
	}else if(c2m_expect(i, string, "\n") == 0) {
	}else if(c2m_expect(i, string, "int32_t") == 0) {
		// check for C function call
		uint32_t len = c2m_count(i, string, '\n');
		char call[len + 1];

		c2m_string_append(a, "int32_t");
		c2m_read(i, string, call, len), call[len] = '\0';
		c2m_string_append(a, call);
		c2m_string_append(a, ";\n");
	}else{
		// check for C function call
		uint32_t len = c2m_count(i, string, ';');
		char call[len + 1];

		if(len == 0) {
			// C-- function call
			c2m_modular_func_call(c2m, i, string, a);
			return;
		}

		c2m_read(i, string, call, len), call[len] = '\0';
		c2m_string_append(a, call);
		c2m_string_append(a, ";\n");
		if(c2m_expect(i, string, ";\n")) {
			c2m_abort("Missing newline for c function call");
		}
	}
}

static uint8_t
c2m_process_var(int32_t* i, const char* string, struct cl_array* a) {
	c2m_skip_whitespace(i, string);
	if(c2m_expect(i, string, "string_t") == 0) {
		uint32_t len = c2m_count(i, string, ',');
		if(len) {
			char par_name[len + 1];
			
			c2m_read(i, string, par_name, len), par_name[len] = '\0';
			c2m_string_append(a, "char* ");
			c2m_string_append(a, par_name);
			c2m_string_append(a, ",");
		}else{
			c2m_string_append(a, "char* ");
			c2m_string_append(a, &(string[*i]));
			c2m_string_append(a, "){\n");
			return 0;
		}
	}else{
		fputs("Unknown Variable Type: ", stdout);
		fputs(&string[*i], stdout);
		fputs("\n", stdout);
		c2m_abort("Unknown type");
	}
	return 1;
}

static void
c2m_import(c2m_t* c2m, int32_t* i, const char* string, const char* mod,
	const char* function)
{
	if(c2m_expect(i, string, "import stdio") == 0) {
		c2m->libreq.stdio = 1;
	}else if(c2m_expect(i, string, "import stdlib") == 0) {
		c2m->libreq.stdlib = 1;
	}else if(c2m_expect(i, string, "import clump") == 0) {
		c2m->libreq.clump = 1;
	}else if(c2m_expect(i, string, "import sdl") == 0) {
		c2m->libreq.sdl = 1;
	}else if(c2m_expect(i, string, "import sdl_window") == 0) {
		c2m->libreq.sdl_window = 1;
	}else if(c2m_expect(i, string, "import sdl_audio") == 0) {
		c2m->libreq.sdl_audio = 1;
	}else if(c2m_expect(i, string, "\n") == 0) {
	}else{
		uint32_t len = c2m_count(i, string, '(');
		if(len == 0) {
			printf("ERROR @: %s", &(string[*i]));
			c2m_abort("opening parenthesis missing");
		}
		char func_name[len + 1];

		c2m_read(i, string, func_name, len), func_name[len] = '\0';
		if(strcmp(func_name, function)) {
			while(string[*i] != '}') *i = *i + 1;
			c2m_expect(i, string, "}\n");
			return;
		}
		printf("Open function %s\n", func_name);
		c2m_expect(i, string, "(");
		c2m_skip_whitespace(i, string);

		uint32_t len2 = c2m_count(i, string, ')');
		if(len2 == 0) c2m_abort("closing parenthesis missing");
		char parameters[len2 + 1];
		c2m_read(i, string, parameters, len2), parameters[len2] = '\0';
		c2m_expect(i, string, ")");
		c2m_skip_whitespace(i, string);
		c2m_expect(i, string, "{\n");

		c2m_string_append(c2m->libfuncs, "static void ");
		c2m_string_append(c2m->libfuncs, mod);
		c2m_string_append(c2m->libfuncs, "__");
		c2m_string_append(c2m->libfuncs, func_name);
		c2m_string_append(c2m->libfuncs, "(");

		int j = 0;
		while(c2m_process_var(&j, parameters, c2m->libfuncs));

//		printf("AFTER VAR: %s", &(string[*i]));
		c2m->in_func = 1;
		while(*i < strlen(string) && c2m->in_func) {
			c2m_infunc(c2m, i, string, c2m->libfuncs);
		}
	}
}

void c2m_loop(c2m_t* c2m, int32_t* i, const char* string) {
	if(c2m_expect(i, string, "//") == 0) {
		while(string[*i] != '\n' && string[*i] != '\0') *i = *i + 1;
	}
	if(c2m->in_main) {
		// Skip indentation.
		c2m_skip_whitespace(i, string);
		if(c2m_expect(i, string, "exit\n}") == 0) {
			c2m->in_main = 0;
		}else if(c2m_expect(i, string, "fail\n}") == 0) {
			c2m->return_success = 0;
			c2m->in_main = 0;
		}else if(c2m_expect(i, string, "}") == 0) {
			printf("MAIN / CLOSE BRACKET;\n");
			if(c2m->block_count) {
				c2m_string_append(c2m->main, "goto ");
				// TODO: Repeated code in while
				uint32_t add = c2m->goto_count;

				c2m_string_append(c2m->main, "C2M_WHILE");
				while(add) {
					char lnum = '0' + (add % 10);
					add /= 10;
					char dest[2];
					dest[0] = lnum;
					dest[1] = '\0';
					c2m_string_append(c2m->main, dest);
				}
				//
				c2m_string_append(c2m->main, ";\n");
				c2m->block_count--;
			}else{
				c2m->in_main = 0;
			}
		}else{
			c2m_infunc(c2m, i, string, c2m->main);
		}
	}else if(c2m->in_func) {
		c2m_infunc(c2m, i, string, c2m->functions);
	}else if(c2m_expect(i, string, "main(") == 0) {
		c2m_skip_whitespace(i, string);
		if(c2m_expect(i, string, "list_t args")) {
			c2m_abort("Expected \"list_t args\" after \"main(\"");
		}
		c2m_skip_whitespace(i, string);
		if(c2m_expect(i, string, ")")) {
			c2m_abort("Expected \")\" after \"list_t args\"");
		}
		c2m_skip_whitespace(i, string);
		if(c2m_expect(i, string, "{\n")) {
			c2m_abort("Expected \"{\\n\" after \")\"");
		}
		c2m->in_main = 1;
	}else if(c2m_expect(i, string, "\n") == 0) {
	}else{
		c2m_skip_whitespace(i, string);
		printf("Error: %s", &(string[*i]));
		printf("Error: %d", string[*i]);
		c2m_abort("Unable to process text");
	}
}

void c2m_compile(c2m_t* c2m) {
	fputs("Compiling ", stdout);
	fputs(c2m->name, stdout);
	fputs(" version ", stdout);
	fputs(c2m->version, stdout);
	fputs("\n", stdout);
	SDL_RWops *output;
	SDL_RWops *input;

	if((output = SDL_RWFromFile("main.c", "w+")) == NULL) {
		c2m_abort("couldn't create output file");
	}
	if((input = SDL_RWFromFile("src/main.c2m", "rb")) == NULL) {
		c2m_abort("couldn't open input file");
	}
	uint64_t size = SDL_RWsize(input);
	char filecontents[size + 1];

	SDL_RWread(input, filecontents, size, 1);
	SDL_RWclose(input);

	c2m->functions = c2m_string_create(NULL);
	c2m->libfuncs = c2m_string_create(NULL);
	c2m->main = c2m_string_create(NULL);
	c2m->varnames = c2m_string_create(NULL);
	c2m->in_main = 0;
	c2m->in_func = 0;
	c2m->return_success = 1;
	c2m->imports = NULL;
	c2m->libreq.stdio = 0;
	c2m->libreq.stdlib = 0;
	c2m->libreq.clump = 0;
	c2m->libreq.sdl = 0;
	c2m->libreq.sdl_window = 0;
	c2m->libreq.sdl_audio = 0;
	c2m->goto_count = 0;
	c2m->block_count = 0;
	for(int32_t i = 0; i < size;) {
		c2m_loop(c2m, &i, filecontents);
	}
	c2m_func_t* current = c2m->imports;
	while(1) {
		if(current == NULL) {
			break;
		}
		struct cl_array * filename = c2m_string_create(NULL);
		c2m_string_append(filename, "lib/");
		c2m_string_append(filename, current->module);
		c2m_string_append(filename, ".c2m");
		fputs("Opening ", stdout);
		fputs(filename->store, stdout);
		fputs("\n", stdout);
		if((input = SDL_RWFromFile(filename->store, "rb")) == NULL) {
			c2m_abort("couldn't open input file");
		}
		uint64_t size = SDL_RWsize(input);
		char libcontents[size + 1];
		SDL_RWread(input, libcontents, size, 1);
		SDL_RWclose(input);
		for(int32_t i = 0; i < size;) {
			c2m_import(c2m, &i, libcontents, current->module, current->function);
		}
		current = current->next;
	}
	// Include requirements from C
	c2m_output(output, "#include <stdint.h>\n"); // No matter what 32-64 compat
	if(c2m->libreq.stdio) c2m_output(output, "#include <stdio.h>\n");
	if(c2m->libreq.stdlib) c2m_output(output, "#include <stdlib.h>\n");
	if(c2m->libreq.clump) c2m_output(output, "#include <c2m_clump.c>\n");
	if(c2m->libreq.sdl) c2m_output(output, "#include <c2m_sdl.c>\n");
	if(c2m->libreq.sdl_window) c2m_output(output, "#include <c2m_window.c>\n");
	if(c2m->libreq.sdl_audio) c2m_output(output, "#include <c2m_audio.c>\n");
	// Functions
	if(c2m->functions->n_items) c2m_output(output, c2m->functions->store);
	if(c2m->libfuncs->n_items) c2m_output(output, c2m->libfuncs->store);
	c2m_output(output, "int main(int argc, char* argv[]){\n");
	c2m_output(output, c2m->main->store);
	c2m_output(output, c2m->return_success ?
		"return 0; }\n" : "return 1; }\n");
	SDL_RWclose(output);
	fputs("Stage 2\n", stdout);
	struct cl_array* clang_command = c2m_string_create(NULL);
	c2m_string_append(clang_command, "clang -O3 main.c -o ");
	c2m_string_append(clang_command, c2m->name);
	system(clang_command->store);
	fputs("Compiled\n", stdout);
}

int main(int argc, char* argv[]) {
	c2m_t c2m;
	c2m_gconfig(&c2m);
	c2m_compile(&c2m);
}
