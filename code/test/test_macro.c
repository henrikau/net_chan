#include <stdio.h>
#include "unity.h"
#include "helper.h"
#include "test_net_fifo.h"
#include <timedc_avtp.h>

static void test_macro_nf_read(void)
{

}

int main(int argc, char *argv[])
{
	UNITY_BEGIN();
	nf_set_nic("lo");

	RUN_TEST(test_macro_nf_read);

	return UNITY_END();
}
