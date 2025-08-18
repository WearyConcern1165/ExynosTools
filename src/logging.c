#include "logging.h"
#include <stdio.h>
#include <stdlib.h>

static FILE* g_xeno_log_file = NULL;

void xeno_logging_init(void) {
	const char* path = getenv("EXYNOSTOOLS_LOG_PATH");
	if (path && *path) {
		FILE* f = fopen(path, "a");
		if (f) {
			g_xeno_log_file = f;
			setvbuf(g_xeno_log_file, NULL, _IOLBF, 0);
		}
	}
}

FILE* xeno_log_stream(void) {
	return g_xeno_log_file ? g_xeno_log_file : stderr;
}

