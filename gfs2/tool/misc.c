#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>

#define __user
#include <linux/gfs2_ondisk.h>
#include <sys/mount.h>

#include "libgfs2.h"
#include "gfs2_tool.h"
#include "iflags.h"

#define SYS_BASE "/sys/fs/gfs2" /* FIXME: Look in /proc/mounts to find this */

/**
 * do_freeze - freeze a GFS2 filesystem
 * @argc:
 * @argv:
 *
 */

void
do_freeze(int argc, char **argv)
{
	char *command = argv[optind - 1];
	char *name;

	if (optind == argc)
		die("Usage: gfs2_tool %s <mountpoint>\n", command);

	name = mp2fsname(argv[optind]);

	if (strcmp(command, "freeze") == 0) {
		if (set_sysfs(name, "freeze", "1")) {
			fprintf(stderr, "Error writing to sysfs freeze file: %s\n",
					strerror(errno));
			exit(-1);
		}
	} else if (strcmp(command, "unfreeze") == 0) {
		if (set_sysfs(name, "freeze", "0")) {
			fprintf(stderr, "Error writing to sysfs freeze file: %s\n",
					strerror(errno));
			exit(-1);
		}
	}

	sync();
}

/**
 * print_lockdump -
 * @argc:
 * @argv:
 *
 */

void
print_lockdump(int argc, char **argv)
{
	char path[PATH_MAX];
	char *name, line[PATH_MAX];
	char *debugfs;
	FILE *file;
	int rc = -1;

	/* See if debugfs is mounted, and if not, mount it. */
	debugfs = find_debugfs_mount();
	if (!debugfs) {
		debugfs = malloc(PATH_MAX);
		if (!debugfs)
			die("Can't allocate memory for debugfs.\n");

		memset(debugfs, 0, PATH_MAX);
		sprintf(debugfs, "/tmp/debugfs.XXXXXX");

		if (!mkdtemp(debugfs)) {
			fprintf(stderr,
				"Can't create %s mount point.\n",
				debugfs);
			free(debugfs);
			exit(-1);
		}

		rc = mount("none", debugfs, "debugfs", 0, NULL);
		if (rc) {
			fprintf(stderr,
				"Can't mount debugfs.  "
				"Maybe your kernel doesn't support it.\n");
				free(debugfs);
				exit(-1);
		}
	}
	name = mp2fsname(argv[optind]);
	if (name) {
		sprintf(path, "%s/gfs2/%s/glocks", debugfs, name);
		free(name);
		file = fopen(path, "rt");
		if (file) {
			while (fgets(line, PATH_MAX, file)) {
				printf("%s", line);
			}
			fclose(file);
		} else {
			fprintf(stderr, "Can't open %s: %s\n", path,
				strerror(errno));
		}
	} else {
		fprintf(stderr, "Unable to locate sysfs for mount point %s.\n",
			argv[optind]);
	}
	/* Check if we mounted the debugfs and if so, unmount it. */
	if (!rc) {
		umount(debugfs);
		rmdir(debugfs);
	}
	free(debugfs);
}

/**
 * margs -
 * @argc:
 * @argv:
 *
 */

void
margs(int argc, char **argv)
{
	die("margs not implemented\n");
}

/**
 * print_flags - print the flags in a dinode's di_flags field
 * @di: the dinode structure
 *
 */

static void
print_flags(struct gfs2_dinode *di)
{
	if (di->di_flags) {
		printf("Flags:\n");
		if (di->di_flags & GFS2_DIF_JDATA)
			printf("  jdata\n");
		if (di->di_flags & GFS2_DIF_EXHASH)
			printf("  exhash\n");
		if (di->di_flags & GFS2_DIF_EA_INDIRECT)
			printf("  ea_indirect\n");
		if (di->di_flags & GFS2_DIF_IMMUTABLE)
			printf("  immutable\n");
		if (di->di_flags & GFS2_DIF_APPENDONLY)
			printf("  appendonly\n");
		if (di->di_flags & GFS2_DIF_NOATIME)
			printf("  noatime\n");
		if (di->di_flags & GFS2_DIF_SYNC)
			printf("  sync\n");
		if (di->di_flags & GFS2_DIF_TRUNC_IN_PROG)
			printf("  trunc_in_prog\n");
	}
}

/*
 * Use FS_XXX_FL flags defined in <linux/fs.h> which correspond to
 * GFS2_DIF_XXX
 */
static unsigned int 
get_flag_from_name(char *name)
{
	if (strncmp(name, "jdata", 5) == 0)
		return FS_JOURNAL_DATA_FL;
	else if (strncmp(name, "exhash", 6) == 0)
		return FS_INDEX_FL;
	else if (strncmp(name, "immutable", 9) == 0)
		return FS_IMMUTABLE_FL;
	else if (strncmp(name, "appendonly", 10) == 0)
		return FS_APPEND_FL;
	else if (strncmp(name, "noatime", 7) == 0)
		return FS_NOATIME_FL;
	else if (strncmp(name, "sync", 4) == 0)
		return FS_SYNC_FL;
	else 
		return 0;
}

/**
 * set_flag - set or clear flags in some dinodes
 * @argc:
 * @argv:
 *
 */
void
set_flag(int argc, char **argv)
{
	struct gfs2_dinode di;
	char *flstr;
	int fd, error, set;
	unsigned int newflags = 0;
	unsigned int flag;
	if (optind == argc) {
		di.di_flags = 0xFFFFFFFF;
		print_flags(&di);
		return;
	}
	
	set = (strcmp(argv[optind -1], "setflag") == 0) ? 1 : 0;
	flstr = argv[optind++];
	if (!(flag = get_flag_from_name(flstr)))
		die("unrecognized flag %s\n", argv[optind -1]);
	
	for (; optind < argc; optind++) {
		fd = open(argv[optind], O_RDONLY);
		if (fd < 0)
			die("can't open %s: %s\n", argv[optind], strerror(errno));
		/* first get the existing flags on the file */
		error = ioctl(fd, FS_IOC_GETFLAGS, &newflags);
		if (error)
			die("can't get flags on %s: %s\n", 
			    argv[optind], strerror(errno));
		newflags = set ? newflags | flag : newflags & ~flag;
		/* new flags */
		error = ioctl(fd, FS_IOC_SETFLAGS, &newflags);
		if (error)
			die("can't set flags on %s: %s\n", 
			    argv[optind], strerror(errno));
		close(fd);
	}
}

/**
 * print_args -
 * @argc:
 * @argv:
 *
 */


void
print_args(int argc, char **argv)
{
	char *fs;
	DIR *d;
	struct dirent *de;
	char path[PATH_MAX];
	struct gfs2_sbd sbd;

	if (optind == argc)
		die("Usage: gfs2_tool getargs <mountpoint>\n");

	sbd.path_name = argv[optind];
	if (check_for_gfs2(&sbd)) {
		if (errno == EINVAL)
			fprintf(stderr, "Not a valid GFS2 mount point: %s\n",
					sbd.path_name);
		else
			fprintf(stderr, "%s\n", strerror(errno));
		exit(-1);
	}
	fs = mp2fsname(argv[optind]);

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX - 1, "%s/%s/args/", SYS_BASE, fs);

	d = opendir(path);
	if (!d)
		die("can't open %s: %s\n", path, strerror(errno));

	while((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;

		snprintf(path, PATH_MAX - 1, "args/%s", de->d_name);
		printf("%s %s\n", de->d_name, get_sysfs(fs, path));
	}

	closedir(d);
	
}

/**
 * print_journals - print out the file system journal information
 * @argc:
 * @argv:
 *
 */

void
print_journals(int argc, char **argv)
{
	struct gfs2_sbd sbd;
	DIR *jindex;
	struct dirent *journal;
	char jindex_name[PATH_MAX], jname[PATH_MAX];
	int jcount;
	struct stat statbuf;

	memset(&sbd, 0, sizeof(struct gfs2_sbd));
	sbd.bsize = GFS2_DEFAULT_BSIZE;
	sbd.rgsize = -1;
	sbd.jsize = GFS2_DEFAULT_JSIZE;
	sbd.qcsize = GFS2_DEFAULT_QCSIZE;
	sbd.md.journals = 1;

	sbd.path_name = argv[optind];
	sbd.path_fd = open(sbd.path_name, O_RDONLY);
	if (sbd.path_fd < 0)
		die("can't open root directory %s: %s\n",
		    sbd.path_name, strerror(errno));
	if (check_for_gfs2(&sbd)) {
		if (errno == EINVAL)
			fprintf(stderr, "Not a valid GFS2 mount point: %s\n",
					sbd.path_name);
		else
			fprintf(stderr, "%s\n", strerror(errno));
		exit(-1);
	}
	sbd.device_fd = open(sbd.device_name, O_RDONLY);
	if (sbd.device_fd < 0)
		die("can't open device %s: %s\n",
		    sbd.device_name, strerror(errno));

	if (mount_gfs2_meta(&sbd)) {
		fprintf(stderr, "Error mounting GFS2 metafs: %s\n",
			strerror(errno));
		exit(-1);
	}

	sprintf(jindex_name, "%s/jindex", sbd.metafs_path);
	jindex = opendir(jindex_name);
	if (!jindex) {
		die("Can't open %s\n", jindex_name);
	} else {
		jcount = 0;
		while ((journal = readdir(jindex))) {
			if (journal->d_name[0] == '.')
				continue;
			sprintf(jname, "%s/%s", jindex_name, journal->d_name);
			if (stat(jname, &statbuf)) {
				statbuf.st_size = 0;
				perror(jname);
			}
			jcount++;
			printf("%s - %lluMB\n", journal->d_name,
			       (unsigned long long)statbuf.st_size / 1048576);
		}

		printf("%d journal(s) found.\n", jcount);
		closedir(jindex);
	}
	cleanup_metafs(&sbd);
	close(sbd.device_fd);
	close(sbd.path_fd);
}

/**
 * print_list - print the list of mounted filesystems
 *
 */

#define LIST_SIZE 1048576

void
print_list(void)
{
	char path[PATH_MAX];
	char s_id[PATH_MAX];
	char *list, *p;
	int rv, fd, x = 0, total = 0;
	DIR *d;
	struct dirent *de;

	list = malloc(LIST_SIZE);
	if (!list)
		die("out of memory\n");

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s", SYS_BASE);

	d = opendir(path);
	if (!d)
		die("can't open %s: %s\n", SYS_BASE, strerror(errno));

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/id", SYS_BASE, de->d_name);

		fd = open(path, O_RDONLY);
		if (fd < 0)
			die("can't open %s: %s\n", path, strerror(errno));

		memset(s_id, 0, PATH_MAX);

		rv = read(fd, s_id, sizeof(s_id));
		if (rv < 0)
			die("can't read %s: %s\n", path, strerror(errno));

		close(fd);

		p = strstr(s_id, "\n");
		if (p)
			*p = '\0';

		total += strlen(s_id) + strlen(de->d_name) + 2;
		if (total > LIST_SIZE)
			break;

		x += sprintf(list + x, "%s %s\n", s_id, de->d_name);

	}

	closedir(d);
	printf("%s", list);
	free(list);
}

/**
 * do_shrink - shrink the inode cache for a filesystem
 * @argc:
 * @argv:
 *
 */

void
do_shrink(int argc, char **argv)
{
	char *fs;
	struct gfs2_sbd sbd;

	if (optind == argc)
		die("Usage: gfs2_tool shrink <mountpoint>\n");

	sbd.path_name = argv[optind];
	if (check_for_gfs2(&sbd)) {
		if (errno == EINVAL)
			fprintf(stderr, "Not a valid GFS2 mount point: %s\n",
					sbd.path_name);
		else
			fprintf(stderr, "%s\n", strerror(errno));
		exit(-1);
	}
	fs = mp2fsname(argv[optind]);
	
	if (set_sysfs(fs, "shrink", "1")) {
		fprintf(stderr, "Error writing to sysfs shrink file: %s\n",
				strerror(errno));
		exit(-1);
	}
}

/**
 * do_withdraw - withdraw a GFS2 filesystem
 * @argc:
 * @argv:
 *
 */

void
do_withdraw(int argc, char **argv)
{
	char *name;
	struct gfs2_sbd sbd;

	if (optind == argc)
		die("Usage: gfs2_tool withdraw <mountpoint>\n");

	sbd.path_name = argv[optind];
	if (check_for_gfs2(&sbd)) {
		if (errno == EINVAL)
			fprintf(stderr, "Not a valid GFS2 mount point: %s\n",
					sbd.path_name);
		else
			fprintf(stderr, "%s\n", strerror(errno));
		exit(-1);
	}
	name = mp2fsname(argv[optind]);

	if (set_sysfs(name, "withdraw", "1")) {
		fprintf(stderr, "Error writing to sysfs withdraw file: %s\n",
				strerror(errno));
		exit(-1);
	}
}

