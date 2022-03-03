#include <terminal.h>

int term_open(const char *termpath)
{
	(void)termpath;
	return -1;
}

void term_close(int ttys)
{
	(void)ttys;
}

int term_write(int ttys, const char *msg, size_t msglen)
{
	(void)ttys;
	(void)msg;
	(void)msglen;
	return -1;
}
