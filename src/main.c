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

void c2m_abort(const char* reason) {
	printf("Aborting because: \"%s\"....\n", reason);
	exit(1);
}

void get_config(void) {
	SDL_RWops *file;
	uint64_t size;
	char* filecontents;

	if((file = SDL_RWFromFile("c2m.config", "rb")) == NULL) {
		c2m_abort("No c2m.config found!");
	}
	size = SDL_RWsize(file);
	filecontents = malloc(size + 1);
	SDL_RWread(file, filecontents, size, 1);
	printf("output = %s", filecontents);
	free(filecontents);
}

int main(int argc, char* argv[]) {
	printf("Compiling....\n");
	get_config();
	printf("Compiled!\n");
}
