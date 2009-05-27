#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>
#include <libintl.h>
#define _(String) gettext(String)

#define __user
#include <linux/gfs2_ondisk.h>

#include "gfs2_tool.h"
#include "libgfs2.h"

/**
 * anthropomorphize - make a uint64_t number more human
 */
static const char *anthropomorphize(unsigned long long inhuman_value)
{
	const char *symbols = " KMGTPE";
	int i;
	unsigned long long val = inhuman_value;
	static char out_val[32];

	memset(out_val, 0, sizeof(out_val));
	for (i = 0; i < 6 && val > 1024; i++)
		val /= 1024;
	sprintf(out_val, "%llu%c", val, symbols[i]);
	return out_val;
}

/**
 * printit - parse out and print values according to the output type
 */
static void printit(unsigned long long block_size, const char *label,
		    unsigned long long total, unsigned long long used,
		    unsigned long long freespace, unsigned int percentage)
{
	switch (output_type) {
	case OUTPUT_BLOCKS:
		printf("  %-15s%-15llu%-15llu%-15llu%u%%\n",
		       label, total, used, freespace, percentage);
		break;
	case OUTPUT_K:
		printf("  %-15s%-15llu%-15llu%-15llu%u%%\n",
		       label, (total * block_size) / 1024,
		       (used * block_size) / 1024, (freespace * block_size) / 1024,
		       percentage);
		break;
	case OUTPUT_HUMAN:
		/* Need to do three separate printfs here because function
		   anthropomorphize re-uses the same static space. */
		printf("  %-15s%-15s", label,
		       anthropomorphize(total * block_size));
		printf("%-15s", anthropomorphize(used * block_size));
		printf("%-15s%u%%\n", anthropomorphize(freespace * block_size),
		       percentage);
		break;
	}
}

/**
 * do_df_one - print out information about one filesystem
 * @path: the path to the filesystem
 *
 */

static void
do_df_one(char *path)
{
	unsigned int percentage;
	unsigned int journals;
	uint64_t rgrps;
	char *fs;
	int statfs_fd;
	struct gfs2_sbd sbd;
	char buf[GFS2_DEFAULT_BSIZE], statfs_fn[PATH_MAX];
	struct gfs2_statfs_change sc;

	memset(&sbd, 0, sizeof(struct gfs2_sbd));
	sbd.path_name = path;
	if (check_for_gfs2(&sbd)) {
		if (errno == EINVAL)
			fprintf(stderr, _("Not a valid GFS2 mount point: %s\n"),
					sbd.path_name);
		else
			fprintf(stderr, "%s\n", strerror(errno));
		exit(-1);
	}
	fs = mp2fsname(sbd.path_name);
	if (!fs) {
		fprintf(stderr, _("Couldn't find GFS2 filesystem mounted at %s\n"),
				sbd.path_name);
		exit(-1);
	}

	sbd.device_fd = open(sbd.device_name, O_RDONLY);
	if (sbd.device_fd < 0)
		die( _("can't open %s: %s\n"), path, strerror(errno));

	sbd.bsize = GFS2_DEFAULT_BSIZE;
	sbd.jsize = GFS2_DEFAULT_JSIZE;
	sbd.rgsize = GFS2_DEFAULT_RGSIZE;
	sbd.utsize = GFS2_DEFAULT_UTSIZE;
	sbd.qcsize = GFS2_DEFAULT_QCSIZE;
	osi_list_init(&sbd.rglist);
	init_buf_list(&sbd, &sbd.buf_list, 128 << 20);
	init_buf_list(&sbd, &sbd.nvbuf_list, 0xffffffff);

	if (lseek(sbd.device_fd, 0x10 * sbd.bsize, SEEK_SET) !=
	    0x10 * sbd.bsize) {
		fprintf(stderr, _("bad seek: %s from %s:%d: superblock\n"),
			strerror(errno), __FUNCTION__, __LINE__);
		exit(-1);
	}
	if (read(sbd.device_fd, buf, sbd.bsize) != sbd.bsize) {
		fprintf(stderr, _("bad read: %s from %s:%d: superblock\n"),
			strerror(errno), __FUNCTION__, __LINE__);
		exit(-1);
	}

	gfs2_sb_in(&sbd.sd_sb, buf); /* parse it out into the sb structure */
	sbd.bsize = sbd.sd_sb.sb_bsize;
	if (compute_constants(&sbd)) {
		fprintf(stderr, "Bad constants (1)\n");
		exit(-1);
	}

	sbd.master_dir = gfs2_load_inode(&sbd,
					 sbd.sd_sb.sb_master_dir.no_addr);

	gfs2_lookupi(sbd.master_dir, "rindex", 6, &sbd.md.riinode);
	gfs2_lookupi(sbd.master_dir, "jindex", 6, &sbd.md.jiinode);
	close(sbd.device_fd);

	journals = sbd.md.jiinode->i_di.di_entries - 2;

	rgrps = sbd.md.riinode->i_di.di_size;
	if (rgrps % sizeof(struct gfs2_rindex))
		die("bad rindex size\n");
	rgrps /= sizeof(struct gfs2_rindex);

	printf("%s:\n", path);
	printf( _("  SB lock proto = \"%s\"\n"), sbd.sd_sb.sb_lockproto);
	printf( _("  SB lock table = \"%s\"\n"), sbd.sd_sb.sb_locktable);
	printf( _("  SB ondisk format = %u\n"), sbd.sd_sb.sb_fs_format);
	printf( _("  SB multihost format = %u\n"), sbd.sd_sb.sb_multihost_format);
	printf( _("  Block size = %u\n"), sbd.sd_sb.sb_bsize);
	printf( _("  Journals = %u\n"), journals);
	printf( _("  Resource Groups = %"PRIu64"\n"), rgrps);
	printf( _("  Journal number = %s\n"), get_sysfs(fs, "lock_module/jid"));

	/* Read the master statfs file */
	if (mount_gfs2_meta(&sbd)) {
		fprintf(stderr, _("Error mounting GFS2 metafs: %s\n"),
			strerror(errno));
		exit(-1);
	}

	sprintf(statfs_fn, "%s/statfs", sbd.metafs_path);
	statfs_fd = open(statfs_fn, O_RDONLY);
	if (read(statfs_fd, buf, sizeof(struct gfs2_statfs_change)) !=
	    sizeof(struct gfs2_statfs_change)) {
		fprintf(stderr, _("bad read: %s from %s:%d: superblock\n"),
			strerror(errno), __FUNCTION__, __LINE__);
		exit(-1);
	}
	gfs2_statfs_change_in(&sc, (char *)&buf);

	close(statfs_fd);

	cleanup_metafs(&sbd);

	printf("\n");
	switch (output_type) {
	case OUTPUT_BLOCKS:
		printf("  %-15s%-15s%-15s%-15s%-15s\n", "Type", "Total Blocks",
		       "Used Blocks", "Free Blocks", "use%");
		break;
	case OUTPUT_K:
		printf("  %-15s%-15s%-15s%-15s%-15s\n", "Type", "Total K",
		       "Used K", "Free K", "use%");
		break;
	case OUTPUT_HUMAN:
		printf("  %-15s%-15s%-15s%-15s%-15s\n", "Type", "Total",
		       "Used", "Free", "use%");
		break;
	}
	printf("  ------------------------------------------------------------------------\n");

	percentage = sc.sc_total ?
		(100.0 * (sc.sc_total - sc.sc_free)) / sc.sc_total + 0.5 : 0;
	printit(sbd.sd_sb.sb_bsize, "data", sc.sc_total,
		sc.sc_total - sc.sc_free, sc.sc_free, percentage);

	percentage = (sc.sc_dinodes + sc.sc_free) ?
		(100.0 * sc.sc_dinodes / (sc.sc_dinodes + sc.sc_free)) + 0.5 :
		0;
	printit(sbd.sd_sb.sb_bsize, "inodes", sc.sc_dinodes + sc.sc_free,
		sc.sc_dinodes, sc.sc_free, percentage);
}


/**
 * print_df - print out information about filesystems
 * @argc:
 * @argv:
 *
 */

void
print_df(int argc, char **argv)
{
	if (optind < argc) {
		char buf[PATH_MAX];

		if (!realpath(argv[optind], buf))
			die( _("can't determine real path: %s\n"), strerror(errno));

		do_df_one(buf);

		return;
	}

	{
		FILE *file;
		char buf[256], device[256], path[256], type[256];
		int first = TRUE;

		file = fopen("/proc/mounts", "r");
		if (!file)
			die( _("can't open /proc/mounts: %s\n"), strerror(errno));

		while (fgets(buf, 256, file)) {
			if (sscanf(buf, "%s %s %s", device, path, type) != 3)
				continue;
			if (strcmp(type, "gfs2") != 0)
				continue;

			if (first)
				first = FALSE;
			else
				printf("\n");

			do_df_one(path);
		}

		fclose(file);
	}
}
