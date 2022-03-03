#include <timedc_args.h>
#include <timedc_avtp.h>

error_t parser(int key, char *arg, struct argp_state *state)
{
      switch (key) {
      case 'i':
	      nf_set_nic(arg);
	      break;
      case 'l':
	      nf_set_logfile(arg);
	      break;
      case 'L':
	      nf_log_delay();
	      break;
      case 's':
	      nf_set_hmap_size(atoi(arg));
	      break;
      case 'S':
	      nf_use_srp();
	      break;
      case 't':
	      nf_use_ftrace();
	      break;
      case 'T':
	      nf_use_termtag(arg);
	      break;
      case 'v':
	      nf_verbose();
	      break;
       }

       return 0;
}

