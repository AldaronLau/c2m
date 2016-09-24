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

typedef struct{
	char* name;
	char* version;
	char* creator;
	char* library;
}c2m_t;

void c2m_skip_whitespace(int32_t* i, const char* string) {
	char value;

	value = string[*i];
	while(value == ' ' || value == '\t') {
		(*i) = (*i) + 1;
		value = string[*i];
	}
}

// Returns 1 if not what expected.
uint8_t c2m_expect(int32_t* i, const char* string, char* what) {
	if(strncmp(&string[*i], what, strlen(what))) {
		return 1;
	}

	*i += strlen(what);
	return 0;
}

void c2m_abort(const char* reason) {
	printf("Aborting because: \"%s\"....\n", reason);
	exit(1);
}

uint32_t c2m_count(int32_t* i, const char* string, char what) {
	int count = 0;

	for(int j = *i; j < strlen(string); j++) {
		if(string[j] == what)
			return j - *i;
	}
	printf("Expected character \"%c\":", what);
	c2m_abort("Character not found");
	return 0;
}

static inline
void c2m_read(int32_t* i, const char* string, char* dest, uint32_t len) {
	memcpy(dest, &string[*i], len);
	*i += len;
}

/*
 * Returns 1 if not a variable declaration.
*/
uint8_t c2m_declare_var(int32_t* i, const char* string, char** dest) {
	// After name probably whitespace
	c2m_skip_whitespace(i, string);
	// Check for equals
	if(c2m_expect(i, string, "="))
		return 1;
	// Probably more whitespace
	c2m_skip_whitespace(i, string);
	// Check for type
	if(c2m_expect(i, string, "\"") == 0) {
		// String Declaration
		uint32_t len = c2m_count(i, string, '\"');
		*dest = malloc(len + 1);
		c2m_read(i, string, *dest, len);
		c2m_expect(i, string, "\"\n");
	}else if(c2m_expect(i, string, "true") == 0) {
		// Byte declaration: 1
		*dest = malloc(4 + 1);
		memcpy(*dest, "true", 4);
		c2m_expect(i, string, "\"\n");
	}else if(c2m_expect(i, string, "false") == 0) {
		// Byte declaration: 0
		*dest = malloc(5 + 1);
		memcpy(*dest, "false", 5);
		c2m_expect(i, string, "\"\n");
	}else{
		printf("not proper at all %s\n", &string[*i]);
		// Not a proper declaration.
		return 1;
	}
	return 0;
}

void get_config(c2m_t* c2m) {
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
	printf("name = '%s'\n", c2m->name);
	printf("version = '%s'\n", c2m->version);
	printf("creator = '%s'\n", c2m->creator);
	printf("library = '%s'\n", c2m->library);
}

int main(int argc, char* argv[]) {
	c2m_t c2m;
	printf("Compiling....\n");
	get_config(&c2m);
	printf("Compiled!\n");
}
