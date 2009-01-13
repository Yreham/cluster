#include <sys/types.h>
#include <sys/un.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>

#include <linux/dlmconstants.h>
#include "libdlm.h"
#include "libdlmcontrol.h"
#include "copyright.cf"

#define LKM_IVMODE -1

#define OP_JOIN				1
#define OP_LEAVE			2
#define OP_JOINLEAVE			3
#define OP_LIST				4
#define OP_DEADLOCK_CHECK		5
#define OP_DUMP				6
#define OP_PLOCKS			7
#define OP_LOCKDUMP			8
#define OP_LOCKDEBUG			9

static char *prog_name;
static char *lsname;
static int operation;
static int opt_ind;
static int ls_all_nodes = 0;
static int opt_dir = 0;
static int opt_excl = 0;
static int opt_fs = 0;
static int dump_mstcpy = 0;
static mode_t create_mode = 0600;
static int verbose;
static int wide;
static int summarize;

#define MAX_LS 128
#define MAX_NODES 128

/* from linux/fs/dlm/dlm_internal.h */
#define DLM_LKSTS_WAITING       1
#define DLM_LKSTS_GRANTED       2
#define DLM_LKSTS_CONVERT       3

struct dlmc_lockspace lss[MAX_LS];
struct dlmc_node nodes[MAX_NODES];

struct rinfo {
	int print_granted;
	int print_convert;
	int print_waiting;
	int print_lookup;
	int namelen;
	int nodeid;
	int lvb;
	unsigned int lkb_count;
	unsigned int lkb_granted;
	unsigned int lkb_convert;
	unsigned int lkb_waiting;
	unsigned int lkb_lookup;
	unsigned int lkb_wait_msg;
	unsigned int lkb_master_copy;
	unsigned int lkb_local_copy;
	unsigned int lkb_process_copy;
};

struct summary {
	unsigned int rsb_total;
	unsigned int rsb_with_lvb;
	unsigned int rsb_no_locks;
	unsigned int rsb_lookup;
	unsigned int rsb_master;
	unsigned int rsb_local;
	unsigned int rsb_nodeid_error;
	unsigned int lkb_count;
	unsigned int lkb_granted;
	unsigned int lkb_convert;
	unsigned int lkb_waiting;
	unsigned int lkb_lookup;
	unsigned int lkb_wait_msg;
	unsigned int lkb_master_copy;
	unsigned int lkb_local_copy;
	unsigned int lkb_process_copy;
};

char *mode_str(int mode)
{
	switch (mode) {
	case -1:
		return "IV";
	case LKM_NLMODE:
		return "NL";
	case LKM_CRMODE:
		return "CR";
	case LKM_CWMODE:
		return "CW";
	case LKM_PRMODE:
		return "PR";
	case LKM_PWMODE:
		return "PW";
	case LKM_EXMODE:
		return "EX";
	}
	return "??";
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("dlm_tool [options] [join | leave | lockdump | lockdebug |\n"
	       "                    ls | dump | plocks | deadlock_check]\n");
	printf("\n");
	printf("Options:\n");
	printf("  -n               Show all node information in ls\n");
	printf("  -d <n>           Resource directory off/on (0/1) in join, default 0\n");
	printf("  -e <n>           Exclusive create off/on (0/1) in join, default 0\n");
	printf("  -f <n>           FS memory allocation off/on (0/1) in join, default 0\n");
	printf("  -m <mode>        Permission mode for lockspace device (octal), default 0600\n");
	printf("  -M               Print MSTCPY locks in lockdump\n"
	       "                   (remote locks that are locally mastered)\n");
	printf("  -s               Summary following lockdebug output\n");
	printf("                   (experimental, format not fixed)\n");
	printf("  -v               Verbose lockdebug output\n");
	printf("  -w               Wide lockdebug output\n");
	printf("  -h               Print this help, then exit\n");
	printf("  -V               Print program version information, then exit\n");
	printf("\n");
}

#define OPTION_STRING "MhVnd:m:e:f:vws"

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;
	int need_lsname;
	char modebuf[8];

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {
		case 'd':
			opt_dir = atoi(optarg);
			break;

		case 'e':
			opt_excl = atoi(optarg);
			break;

		case 'f':
			opt_fs = atoi(optarg);
			break;

		case 'm':
			memset(modebuf, 0, sizeof(modebuf));
			snprintf(modebuf, 8, "%s", optarg);
			sscanf(modebuf, "%o", &create_mode);
			break;

		case 'M':
			dump_mstcpy = 1;
			break;

		case 'n':
			ls_all_nodes = 1;
			break;

		case 's':
			summarize = 1;
			break;

		case 'v':
			verbose = 1;
			break;

		case 'w':
			wide = 1;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("%s %s (built %s %s)\n",
				prog_name, RELEASE_VERSION, __DATE__, __TIME__);
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
			fprintf(stderr, "unknown option: %c\n", optchar);
			exit(EXIT_FAILURE);
			break;
		};
	}

	need_lsname = 1;

	while (optind < argc) {

		/*
		 * libdlm
		 */

		if (!strncmp(argv[optind], "join", 4) &&
		    (strlen(argv[optind]) == 4)) {
			operation = OP_JOIN;
			opt_ind = optind + 1;
			break;
		} else if (!strncmp(argv[optind], "leave", 5) &&
			   (strlen(argv[optind]) == 5)) {
			operation = OP_LEAVE;
			opt_ind = optind + 1;
			break;
		} else if (!strncmp(argv[optind], "joinleave", 9) &&
			   (strlen(argv[optind]) == 9)) {
			operation = OP_JOINLEAVE;
			opt_ind = optind + 1;
			break;
		}

		/*
		 * libdlmcontrol
		 */

		else if (!strncmp(argv[optind], "ls", 2) &&
			   (strlen(argv[optind]) == 2)) {
			operation = OP_LIST;
			opt_ind = optind + 1;
			need_lsname = 0;
			break;
		} else if (!strncmp(argv[optind], "deadlock_check", 14) &&
			   (strlen(argv[optind]) == 14)) {
			operation = OP_DEADLOCK_CHECK;
			opt_ind = optind + 1;
			break;
		} else if (!strncmp(argv[optind], "dump", 4) &&
			   (strlen(argv[optind]) == 4)) {
			operation = OP_DUMP;
			opt_ind = optind + 1;
			need_lsname = 0;
			break;
		} else if (!strncmp(argv[optind], "plocks", 6) &&
			   (strlen(argv[optind]) == 6)) {
			operation = OP_PLOCKS;
			opt_ind = optind + 1;
			break;
		}

		/*
		 * debugfs
		 */

		else if (!strncmp(argv[optind], "lockdump", 8) &&
			   (strlen(argv[optind]) == 8)) {
			operation = OP_LOCKDUMP;
			opt_ind = optind + 1;
			break;
		} else if (!strncmp(argv[optind], "lockdebug", 9) &&
			   (strlen(argv[optind]) == 9)) {
			operation = OP_LOCKDEBUG;
			opt_ind = optind + 1;
			break;
		}
		optind++;
	}

	if (!operation || !opt_ind) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	if (optind < argc - 1)
		lsname = argv[opt_ind];
	else if (need_lsname) {
		fprintf(stderr, "lockspace name required\n");
		exit(EXIT_FAILURE);
	}
}

static int do_write(int fd, void *buf, size_t count)
{
	int rv, off = 0;

 retry:
	rv = write(fd, buf + off, count);
	if (rv == -1 && errno == EINTR)
		goto retry;
	if (rv < 0)
		return rv;

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
}

static char *flag_str(uint32_t flags)
{
	static char join_flags[128];

	memset(join_flags, 0, sizeof(join_flags));

	strcat(join_flags, "flags ");

	if (flags & DLM_LSFL_NODIR)
		strcat(join_flags, "NODIR ");

	if (flags & DLM_LSFL_NEWEXCL)
		strcat(join_flags, "NEWEXCL ");

	if (flags & DLM_LSFL_FS)
		strcat(join_flags, "FS ");

	return join_flags;
}

void do_join(char *name)
{
	dlm_lshandle_t *dh;
	uint32_t flags = 0;

	if (!opt_dir)
		flags |= DLM_LSFL_NODIR;

	if (opt_excl)
		flags |= DLM_LSFL_NEWEXCL;

	if (opt_fs)
		flags |= DLM_LSFL_FS;

	printf("Joining lockspace \"%s\" permission %o %s\n",
	       name, create_mode, flags ? flag_str(flags) : "");
	fflush(stdout);

	dh = dlm_new_lockspace(name, create_mode, flags);
	if (!dh) {
		fprintf(stderr, "dlm_new_lockspace %s error %d\n",
			name, errno);
		exit(-1);
	}

	dlm_close_lockspace(dh);
	/* there's no autofree so the ls should stay around */
	printf("done\n");
}

void do_leave(char *name)
{
	dlm_lshandle_t *dh;

	printf("Leaving lockspace \"%s\"\n", name);
	fflush(stdout);

	dh = dlm_open_lockspace(name);
	if (!dh) {
		fprintf(stderr, "dlm_open_lockspace %s error %p %d\n",
			name, dh, errno);
		exit(-1);
	}

	dlm_release_lockspace(name, dh, 1);
	printf("done\n");
}

char *pr_master(int nodeid, uint32_t first_lkid)
{
	static char buf[64];

	memset(buf, 0, sizeof(buf));

	if (nodeid > 0)
		sprintf(buf, "Local %d", nodeid);
	else if (!nodeid)
		sprintf(buf, "Master");
	else if (nodeid == -1)
		sprintf(buf, "Lookup lkid %08x", first_lkid);

	return buf;
}

char *pr_recovery(uint32_t flags, int root_list, int recover_list,
		  int recover_locks_count)
{
	static char buf[128];

	memset(buf, 0, sizeof(buf));

	if (flags || root_list || recover_list || recover_locks_count)
		sprintf(buf, "flags %08x root %d recover %d locks_count %d",
			flags, root_list, recover_list, recover_locks_count);

	return buf;
}

void print_rsb(char *line, struct rinfo *ri)
{
	char type[4], namefmt[4], *p;
	char addr[64];
	int rv, nodeid, root_list, recover_list, recover_locks_count, namelen;
	uint32_t first_lkid, flags;

	rv = sscanf(line, "%s %s %d %u %u %d %d %u %u %s",
		    type,
		    addr,
		    &nodeid,
		    &first_lkid,
		    &flags,
		    &root_list,
		    &recover_list,
		    &recover_locks_count,
		    &namelen,
		    namefmt);

	if (rv != 10)
		goto fail;

	/* used for lkb prints */
	ri->nodeid = nodeid;

	ri->namelen = namelen;
	
	p = strchr(line, '\n');
	if (!p)
		goto fail;
	*p = '\0';

	p = strstr(line, namefmt);
	if (!p)
		goto fail;
	p += 4;

	strcat(addr, " ");

	if (!strncmp(namefmt, "str", 3))
		printf("Resource len %2d  \"%s\"\n", namelen, p);
	else if (!strncmp(namefmt, "hex", 3))
		printf("Resource len %2d hex %s\n", namelen, p);
	else
		goto fail;

	printf("%-16s %s\n",
		pr_master(nodeid, first_lkid),
		pr_recovery(flags, root_list, recover_list, recover_locks_count));
	return;

 fail:
	fprintf(stderr, "print_rsb error rv %d line \"%s\"\n", rv, line);
}

void print_lvb(char *line)
{
	char lvb[1024];
	char type[4];
	int i, c, rv, lvblen;
	uint32_t lvbseq;

	memset(lvb, 0, 1024);

	rv = sscanf(line, "%s %u %d %[0-9A-Fa-f ]", type, &lvbseq, &lvblen, lvb);

	if (rv != 4) {
		fprintf(stderr, "print_lvb error rv %d line \"%s\"\n", rv, line);
		return;
	}

	printf("LVB len %d seq %u\n", lvblen, lvbseq);

	for (c = 0, i = 0; ; i++) {
		printf("%c", lvb[i]);
		if (lvb[i] != ' ')
			c++;
		if (!wide && lvb[i] == ' ' && !(c % 32))
			printf("\n");
		if (c == (lvblen * 2))
			break;
	}
	printf("\n");
}

struct lkb {
	uint64_t xid, timestamp, time_bast;
	uint32_t id, remid, exflags, flags, lvbseq;
	int nodeid, ownpid, status, grmode, rqmode, highbast, rsb_lookup, wait_type;
};

char *pr_grmode(struct lkb *lkb)
{
	if (lkb->status == DLM_LKSTS_GRANTED || lkb->status == DLM_LKSTS_CONVERT)
		return mode_str(lkb->grmode);
	else if (lkb->status == DLM_LKSTS_WAITING || lkb->rsb_lookup)
		return "--";
	else
		return "XX";
}

char *pr_rqmode(struct lkb *lkb)
{
	static char buf[5];

	memset(buf, 0, sizeof(buf));

	if (lkb->status == DLM_LKSTS_GRANTED) {
		return "    ";
	} else if (lkb->status == DLM_LKSTS_CONVERT ||
		   lkb->status == DLM_LKSTS_WAITING ||
		   lkb->rsb_lookup) {
		sprintf(buf, "(%s)", mode_str(lkb->rqmode));
		return buf;
	} else {
		return "(XX)";
	}
}

char *pr_remote(struct lkb *lkb, struct rinfo *ri)
{
	static char buf[64];

	memset(buf, 0, sizeof(buf));

	if (!lkb->nodeid) {
		return "                    ";
	} else if (lkb->nodeid != ri->nodeid) {
		sprintf(buf, "Remote: %3d %08x", lkb->nodeid, lkb->remid);
		return buf;
	} else {
		sprintf(buf, "Master: %3d %08x", lkb->nodeid, lkb->remid);
		return buf;
	}
}

char *pr_wait(struct lkb *lkb)
{
	static char buf[16];

	memset(buf, 0, sizeof(buf));

	if (!lkb->wait_type) {
		return "        ";
	} else {
		sprintf(buf, " wait %02d", lkb->wait_type);
		return buf;
	}
}

char *pr_verbose(struct lkb *lkb)
{
	static char buf[128];

	memset(buf, 0, sizeof(buf));

	sprintf(buf, "time %016llu flags %08x %08x bast %d %llu",
		(unsigned long long)lkb->timestamp,
		lkb->exflags, lkb->flags, lkb->highbast,
		(unsigned long long)lkb->time_bast);

	return buf;
}

void print_lkb(char *line, struct rinfo *ri)
{
	struct lkb lkb;
	char type[4];
	int rv;

	rv = sscanf(line, "%s %x %d %x %u %llu %x %x %d %d %d %d %d %d %u %llu %llu",
		    type,
		    &lkb.id,
		    &lkb.nodeid,
		    &lkb.remid,
		    &lkb.ownpid,
		    &lkb.xid,
		    &lkb.exflags,
		    &lkb.flags,
		    &lkb.status,
		    &lkb.grmode,
		    &lkb.rqmode,
		    &lkb.highbast,
		    &lkb.rsb_lookup,
		    &lkb.wait_type,
		    &lkb.lvbseq,
		    &lkb.timestamp,
		    &lkb.time_bast);

	ri->lkb_count++;

	if (lkb.status == DLM_LKSTS_GRANTED) {
	       	if (!ri->print_granted++)
			printf("Granted\n");
		ri->lkb_granted++;
	}
	if (lkb.status == DLM_LKSTS_CONVERT) {
		if (!ri->print_convert++)
			printf("Convert\n");
		ri->lkb_convert++;
	}
	if (lkb.status == DLM_LKSTS_WAITING) {
	       	if (!ri->print_waiting++)
			printf("Waiting\n");
		ri->lkb_waiting++;
	}
	if (lkb.rsb_lookup) {
	       	if (!ri->print_lookup++)
			printf("Lookup\n");
		ri->lkb_lookup++;
	}

	if (lkb.wait_type)
		ri->lkb_wait_msg++;

	if (!ri->nodeid) {
		if (lkb.nodeid)
			ri->lkb_master_copy++;
		else
			ri->lkb_local_copy++;
	} else {
		ri->lkb_process_copy++;
	}

	printf("%08x %s %s %s %s %s\n",
	       lkb.id, pr_grmode(&lkb), pr_rqmode(&lkb),
	       pr_remote(&lkb, ri), pr_wait(&lkb),
	       (verbose && wide) ? pr_verbose(&lkb) : "");

	if (verbose && !wide)
		printf("%s\n", pr_verbose(&lkb));
}

static void clear_rinfo(struct rinfo *ri)
{
	memset(ri, 0, sizeof(struct rinfo));
	ri->nodeid = -9;
}

static void count_rinfo(struct summary *s, struct rinfo *ri)
{
	/* the first time called */
	if (!ri->namelen)
		return;

	s->rsb_total++;

	if (ri->lvb)
		s->rsb_with_lvb++;

	if (!ri->lkb_count) {
		s->rsb_no_locks++;
		printf("no locks\n");
	}

	if (!ri->nodeid)
		s->rsb_master++;
	else if (ri->nodeid == -1)
		s->rsb_lookup++;
	else if (ri->nodeid > 0)
		s->rsb_local++;
	else
		s->rsb_nodeid_error++;

	s->lkb_count        += ri->lkb_count;
	s->lkb_granted      += ri->lkb_granted;
	s->lkb_convert      += ri->lkb_convert;
	s->lkb_waiting      += ri->lkb_waiting;
	s->lkb_lookup       += ri->lkb_lookup;
	s->lkb_wait_msg     += ri->lkb_wait_msg;
	s->lkb_master_copy  += ri->lkb_master_copy;
	s->lkb_local_copy   += ri->lkb_local_copy;
	s->lkb_process_copy += ri->lkb_process_copy;
}

static void print_summary(struct summary *s)
{
	printf("rsb\n");
	printf("  total         %u\n", s->rsb_total);
	printf("  master        %u\n", s->rsb_master);
	printf("  remote master %u\n", s->rsb_local);
	printf("  lookup master %u\n", s->rsb_lookup);
	printf("  with lvb      %u\n", s->rsb_with_lvb);
	printf("  with no locks %u\n", s->rsb_no_locks);
	printf("  nodeid error  %u\n", s->rsb_nodeid_error);
	printf("\n");

	printf("lkb\n");
	printf("  total         %u\n", s->lkb_count);
	printf("  granted       %u\n", s->lkb_granted);
	printf("  convert       %u\n", s->lkb_convert);
	printf("  waiting       %u\n", s->lkb_waiting);
	printf("  local copy    %u\n", s->lkb_local_copy);
	printf("  master copy   %u\n", s->lkb_master_copy);
	printf("  process copy  %u\n", s->lkb_process_copy);
	printf("  rsb lookup    %u\n", s->lkb_lookup);
	printf("  wait message  %u\n", s->lkb_wait_msg);
}

#define LOCK_LINE_MAX 1024

void do_lockdebug(char *name)
{
	struct summary summary;
	struct rinfo info;
	FILE *file;
	char path[PATH_MAX];
	char line[LOCK_LINE_MAX];
	int old = 0;

	snprintf(path, PATH_MAX, "/sys/kernel/debug/dlm/%s_all", name);

	file = fopen(path, "r");
	if (!file) {
		snprintf(path, PATH_MAX, "/sys/kernel/debug/dlm/%s", name);
		file = fopen(path, "r");
		if (!file) {
			fprintf(stderr, "can't open %s: %s\n", path, strerror(errno));
			return;
		}
		old = 1;
	}

	memset(&summary, 0, sizeof(struct summary));
	memset(&info, 0, sizeof(struct rinfo));

	while (fgets(line, LOCK_LINE_MAX, file)) {

		if (old)
			goto raw;

		if (!strncmp(line, "version", 7))
			continue;

		if (!strncmp(line, "rsb", 3)) {
			count_rinfo(&summary, &info);
			clear_rinfo(&info);
			printf("\n");
			print_rsb(line, &info);
			continue;
		}
		
		if (!strncmp(line, "lvb", 3)) {
			print_lvb(line);
			info.lvb = 1;
			continue;
		}
		
		if (!strncmp(line, "lkb", 3)) {
			print_lkb(line, &info);
			continue;
		}
 raw:
		printf("%s", line);
	}
	fclose(file);

	if (summarize) {
		printf("\n");
		print_summary(&summary);
	}
}

void parse_r_name(char *line, char *name)
{
	char *p;
	int i = 0;
	int begin = 0;

	for (p = line; ; p++) {
		if (*p == '"') {
			if (begin)
				break;
			begin = 1;
			continue;
		}
		if (begin)
			name[i++] = *p;
	}
}

void do_lockdump(char *name)
{
	FILE *file;
	char path[PATH_MAX];
	char line[LOCK_LINE_MAX];
	char r_name[65];
	int r_nodeid;
	int r_len;
	int rv;
	unsigned int time;
	unsigned long long xid;
	uint32_t	id;
	int		nodeid;
	uint32_t	remid;
	int		ownpid;
	uint32_t	exflags;
	uint32_t	flags;
	int8_t		status;
	int8_t		grmode;
	int8_t		rqmode;

	snprintf(path, PATH_MAX, "/sys/kernel/debug/dlm/%s_locks", name);

	file = fopen(path, "r");
	if (!file) {
		fprintf(stderr, "can't open %s: %s\n", path, strerror(errno));
		return;
	}

	/* skip the header on the first line */
	if(!fgets(line, LOCK_LINE_MAX, file)) {
		fprintf(stderr, "can't read %s: %s\n", path, strerror(errno));
		return;
	}

	while (fgets(line, LOCK_LINE_MAX, file)) {
		rv = sscanf(line, "%x %d %x %u %llu %x %x %hhd %hhd %hhd %u %d %d",
		       &id,
		       &nodeid,
		       &remid,
		       &ownpid,
		       &xid,
		       &exflags,
		       &flags,
		       &status,
		       &grmode,
		       &rqmode,
		       &time,
		       &r_nodeid,
		       &r_len);

		if (rv != 13) {
			fprintf(stderr, "invalid debugfs line %d: %s\n",
				rv, line);
			return;
		}

		memset(r_name, 0, sizeof(r_name));
		parse_r_name(line, r_name);

		/* don't print MSTCPY locks without -M */
		if (!r_nodeid && nodeid) {
			if (!dump_mstcpy)
				continue;
			printf("id %08x gr %s rq %s pid %u MSTCPY %d \"%s\"\n",
				id, mode_str(grmode), mode_str(rqmode),
				ownpid, nodeid, r_name);
			continue;
		}

		/* A hack because dlm-kernel doesn't set rqmode back to IV when
		   a NOQUEUE convert fails, which means in a lockdump it looks
		   like a granted lock is still converting since rqmode is not
		   IV.  (does it make sense to include status in the output,
		   e.g. G,C,W?) */

		if (status == DLM_LKSTS_GRANTED)
			rqmode = LKM_IVMODE;

		printf("id %08x gr %s rq %s pid %u master %d \"%s\"\n",
			id, mode_str(grmode), mode_str(rqmode),
			ownpid, nodeid, r_name);
	}

	fclose(file);
}

static char *dlmc_lf_str(uint32_t flags)
{
	static char str[128];
	int i = 0;

	memset(str, 0, sizeof(str));

	if (flags & DLMC_LF_SAVE_PLOCKS) {
		i++;
		strcat(str, "save_plock");
	}
	if (flags & DLMC_LF_NEED_PLOCKS) {
		strcat(str, i++ ? "," : "");
		strcat(str, "need_plock");
	}
	if (flags & DLMC_LF_FS_REGISTERED) {
		strcat(str, i++ ? "," : "");
		strcat(str, "fs_reg");
	}
	if (flags & DLMC_LF_KERNEL_STOPPED) {
		strcat(str, i++ ? "," : "");
		strcat(str, "kern_stop");
	}
	if (flags & DLMC_LF_LEAVING) {
		strcat(str, i++ ? "," : "");
		strcat(str, "leave");
	}
	if (flags & DLMC_LF_JOINING) {
		strcat(str, i++ ? "," : "");
		strcat(str, "join");
	}

	return str;
}

static char *nf_check_str(uint32_t flags)
{
	if (flags & DLMC_NF_CHECK_FENCING)
		return "fence";

	if (flags & DLMC_NF_CHECK_QUORUM)
		return "quorum";

	if (flags & DLMC_NF_CHECK_FS)
		return "fs";

	return "none";
}

static char *condition_str(int cond)
{
	switch (cond) {
	case 0:
		return "";
	case 1:
		return "fencing";
	case 2:
		return "quorum";
	case 3:
		return "fs";
	case 4:
		return "pending";
	default:
		return "unknown";
	}
}

static int node_compare(const void *va, const void *vb)
{
	const struct dlmc_node *a = va;
	const struct dlmc_node *b = vb;

	return a->nodeid - b->nodeid;
}

static void show_nodeids(int count, struct dlmc_node *nodes)
{
	struct dlmc_node *n = nodes;
	int i;

	for (i = 0; i < count; i++) {
		printf("%d ", n->nodeid);
		n++;
	}
	printf("\n");
}

static void show_ls(struct dlmc_lockspace *ls)
{
	int rv, node_count;

	printf("name          %s\n", ls->name);
	printf("id            0x%08x\n", ls->global_id);
	printf("flags         0x%08x %s\n",
		ls->flags, dlmc_lf_str(ls->flags));
	printf("change        member %d joined %d remove %d failed %d seq %d,%d\n",
		ls->cg_prev.member_count, ls->cg_prev.joined_count,
		ls->cg_prev.remove_count, ls->cg_prev.failed_count,
		ls->cg_prev.combined_seq, ls->cg_prev.seq);

	node_count = 0;
	memset(&nodes, 0, sizeof(nodes));
	rv = dlmc_lockspace_nodes(ls->name, DLMC_NODES_MEMBERS,
				  MAX_NODES, &node_count, nodes);
	if (rv < 0) {
		printf("members       error\n");
		goto next;
	}
	qsort(nodes, node_count, sizeof(struct dlmc_node), node_compare);

	printf("members       ");
	show_nodeids(node_count, nodes);

 next:
	if (!ls->cg_next.seq)
		return;

	printf("new change    member %d joined %d remove %d failed %d seq %d,%d\n",
		ls->cg_next.member_count, ls->cg_next.joined_count,
		ls->cg_next.remove_count, ls->cg_next.failed_count,
	        ls->cg_next.combined_seq, ls->cg_next.seq);

	printf("new status    wait_messages %d wait_condition %d %s\n",
		ls->cg_next.wait_messages, ls->cg_next.wait_condition,
		condition_str(ls->cg_next.wait_condition));

	node_count = 0;
	memset(&nodes, 0, sizeof(nodes));
	rv = dlmc_lockspace_nodes(ls->name, DLMC_NODES_NEXT,
				  MAX_NODES, &node_count, nodes);
	if (rv < 0) {
		printf("new members   error\n");
		return;
	}
	qsort(nodes, node_count, sizeof(struct dlmc_node), node_compare);

	printf("new members   ");
	show_nodeids(node_count, nodes);
}

static int member_int(struct dlmc_node *n)
{
	if (n->flags & DLMC_NF_DISALLOWED)
		return -1;
	if (n->flags & DLMC_NF_MEMBER)
		return 1;
	return 0;
}

static void show_all_nodes(int count, struct dlmc_node *nodes)
{
	struct dlmc_node *n = nodes;
	int i;

	for (i = 0; i < count; i++) {
		printf("nodeid %d member %d failed %d start %d seq_add %u seq_rem %u check %s\n",
			n->nodeid,
			member_int(n),
			n->failed_reason,
			(n->flags & DLMC_NF_START) ? 1 : 0,
			n->added_seq,
			n->removed_seq,
			nf_check_str(n->flags));
		n++;
	}
}

static void do_list(char *name)
{
	struct dlmc_lockspace *ls;
	int node_count;
	int ls_count;
	int rv;
	int i;

	memset(lss, 0, sizeof(lss));

	if (name) {
		rv = dlmc_lockspace_info(name, lss);
		if (rv < 0)
			return;
		ls_count = 1;
	} else {
		rv = dlmc_lockspaces(MAX_LS, &ls_count, lss);
		if (rv < 0)
			return;
	}

	if (ls_count)
		printf("dlm lockspaces\n");

	for (i = 0; i < ls_count; i++) {
		ls = &lss[i];

		show_ls(ls);

		if (!ls_all_nodes)
			goto next;

		node_count = 0;
		memset(&nodes, 0, sizeof(nodes));

		rv = dlmc_lockspace_nodes(ls->name, DLMC_NODES_ALL,
					  MAX_NODES, &node_count, nodes);
		if (rv < 0) {
			printf("all nodes error %d %d\n", rv, errno);
			goto next;
		}

		qsort(nodes, node_count, sizeof(struct dlmc_node),node_compare);

		printf("all nodes\n");
		show_all_nodes(node_count, nodes);
 next:
		printf("\n");
	}
}

static void do_deadlock_check(char *name)
{
	dlmc_deadlock_check(name);
}

static void do_plocks(char *name)
{
	char buf[DLMC_DUMP_SIZE];

	memset(buf, 0, sizeof(buf));

	dlmc_dump_plocks(name, buf);

	do_write(STDOUT_FILENO, buf, strlen(buf));
}

static void do_dump(void)
{
	char buf[DLMC_DUMP_SIZE];

	memset(buf, 0, sizeof(buf));

	dlmc_dump_debug(buf);

	do_write(STDOUT_FILENO, buf, strlen(buf));
}

int main(int argc, char **argv)
{
	prog_name = argv[0];
	decode_arguments(argc, argv);

	switch (operation) {

	/* calls to libdlm; pass a command to dlm-kernel */

	case OP_JOIN:
		do_join(lsname);
		break;

	case OP_LEAVE:
		do_leave(lsname);
		break;

	case OP_JOINLEAVE:
		do_join(lsname);
		do_leave(lsname);
		break;

	/* calls to libdlmcontrol; pass a command/query to dlm_controld */

	case OP_LIST:
		do_list(lsname);
		break;

	case OP_DUMP:
		do_dump();
		break;

	case OP_PLOCKS:
		do_plocks(lsname);
		break;

	case OP_DEADLOCK_CHECK:
		do_deadlock_check(lsname);
		break;

	/* calls to read debugfs; query info from dlm-kernel */

	case OP_LOCKDUMP:
		do_lockdump(lsname);
		break;

	case OP_LOCKDEBUG:
		do_lockdebug(lsname);
		break;
	}
	return 0;
}

