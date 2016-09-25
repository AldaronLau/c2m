#include "../clump/src/array.c"

static inline struct cl_array *c2m_string_create(const char* initial_value) {
	return cl_array_create(1, initial_value ? strlen(initial_value) : 0);
}

static inline void c2m_string_append(struct cl_array *arr, const char* what) {
	cl_array_pop(arr);
	for(int i = 0; i < strlen(what); i++) {
		char* value = cl_array_add(arr);
		*value = what[i];
	}
	char* value = cl_array_add(arr);
	*value = '\0';
}

static inline void c2m_string_destroy(struct cl_array *arr) {
	cl_array_destroy(arr);
}
