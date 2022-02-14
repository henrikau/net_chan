#pragma once
#include <stdio.h>

FILE * tb_open(void);
void tb_close(FILE *tracefd);
void tb_tag(FILE *tracefd, const char *line);
