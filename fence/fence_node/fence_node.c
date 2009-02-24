#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <liblogthread.h>

#include "libfence.h"
#include "libfenced.h"
#include "copyright.cf"

static char *prog_name;
static int verbose;

#define FL_SIZE 32
static struct fence_log log[FL_SIZE];
static int log_count;

#define OPTION_STRING "hvV"

#define die(fmt, args...) \
do \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt "\n", ##args); \
  exit(EXIT_FAILURE); \
} \
while (0)

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options] node_name\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -v    Show fence agent results, -vv for agent args\n");
	printf("  -h    Print this help, then exit\n");
	printf("  -V    Print program version information, then exit\n");
	printf("\n");
}

static char *fe_str(int r)
{
	switch (r) {
	case FE_AGENT_SUCCESS:
		return "success";
	case FE_AGENT_ERROR:
		return "error from agent";
	case FE_AGENT_FORK:
		return "error from fork";
	case FE_NO_CONFIG:
		return "error from ccs";
	case FE_NO_METHOD:
		return "error no method";
	case FE_NO_DEVICE:
		return "error no device";
	case FE_READ_AGENT:
		return "error config agent";
	case FE_READ_ARGS:
		return "error config args";
	case FE_READ_METHOD:
		return "error config method";
	case FE_READ_DEVICE:
		return "error config device";
	default:
		return "error unknown";
	}
}

int main(int argc, char *argv[])
{
	char *victim = NULL, *p;
	int cont = 1, optchar, error, rv, i;

	prog_name = argv[0];

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'v':
			verbose++;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("%s %s (built %s %s)\n", prog_name,
				RELEASE_VERSION, __DATE__, __TIME__);
			printf("%s\n", REDHAT_COPYRIGHT);
			exit(EXIT_SUCCESS);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = 0;
			break;

		default:
			die("unknown option: %c", optchar);
			break;
		};
	}

	while (optind < argc) {
		if (victim)
			die("unknown option %s", argv[optind]);
		victim = argv[optind];
		optind++;
	}

	if (!victim)
		die("no node name specified");

	memset(&log, 0, sizeof(log));
	log_count = 0;

	error = fence_node(victim, log, FL_SIZE, &log_count);

	logt_init("fence_node", LOG_MODE_OUTPUT_SYSLOG, SYSLOGFACILITY,
		  SYSLOGLEVEL, 0, NULL);

	if (!verbose)
		goto skip;

	if (log_count > FL_SIZE) {
		fprintf(stderr, "fence_node log overflow %d", log_count);
		log_count = FL_SIZE;
	}

	for (i = 0; i < log_count; i++) {
		fprintf(stderr, "fence %s dev %d.%d agent %s result: %s\n",
			victim, log[i].method_num, log[i].device_num,
			log[i].agent_name[0] ?  log[i].agent_name : "none",
			fe_str(log[i].error));

		if (verbose < 2)
			continue;

		p = strchr(log[i].agent_args, '\n');
		if (p)
			*p = '\0';
		fprintf(stderr, "agent args: %s\n", log[i].agent_args);
	}

 skip:
	if (error) {
		fprintf(stderr, "Fence of \"%s\" was unsuccessful\n", victim);
		logt_print(LOG_ERR, "Fence of \"%s\" was unsuccessful\n",
			   victim);
		rv = EXIT_FAILURE;
	} else {
		fprintf(stderr, "Fence of \"%s\" was successful\n", victim);
		logt_print(LOG_NOTICE, "Fence of \"%s\" was successful\n",
			   victim);
		rv = EXIT_SUCCESS;

		/* Tell fenced what we've done so that it can avoid fencing
		   this node again if the fence_node() rebooted it. */
		fenced_external(victim);
	}

	logt_exit();
	exit(rv);
}

