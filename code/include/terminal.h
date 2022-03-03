#pragma once
#include <stddef.h>
/**
 *
 */
int term_open(const char *termpath);

void term_close(int ttys);

int term_write(int ttys, const char *msg, size_t msglen);
