#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/mount.h>
#include <linux/types.h>
#include <sys/file.h>
#include <dirent.h>
#include <linux/kdev_t.h>
#include <sys/sysmacros.h>

#include "libgfs2.h"

#define PAGE_SIZE (4096)
#define SYS_BASE "/sys/fs/gfs2" /* FIXME: Look in /proc/mounts to find this */
#define DIV_RU(x, y) (((x) + (y) - 1) / (y))

static char sysfs_buf[PAGE_SIZE];

uint32_t compute_heightsize(struct gfs2_sbd *sdp, uint64_t *heightsize,
			    uint32_t bsize1, int diptrs, int inptrs)
{
	int x;

	heightsize[0] = sdp->bsize - sizeof(struct gfs2_dinode);
	heightsize[1] = bsize1 * diptrs;
	for (x = 2;; x++) {
		uint64_t space, d;
		uint32_t m;

		space = heightsize[x - 1] * inptrs;
		m = space % inptrs;
		d = space / inptrs;

		if (d != heightsize[x - 1] || m)
			break;
		heightsize[x] = space;
	}
	if (x > GFS2_MAX_META_HEIGHT)
		die("bad constants (1)\n");
	return x;
}

void compute_constants(struct gfs2_sbd *sdp)
{
	uint32_t hash_blocks, ind_blocks, leaf_blocks;
	uint32_t tmp_blocks;

	sdp->md.next_inum = 1;

	sdp->sd_sb.sb_bsize_shift = ffs(sdp->bsize) - 1;
	sdp->sb_addr = GFS2_SB_ADDR * GFS2_BASIC_BLOCK / sdp->bsize;

	sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift -
		GFS2_BASIC_BLOCK_SHIFT;
	sdp->sd_fsb2bb = 1 << sdp->sd_fsb2bb_shift;
	sdp->sd_diptrs = (sdp->bsize - sizeof(struct gfs2_dinode)) /
		sizeof(uint64_t);
	sdp->sd_inptrs = (sdp->bsize - sizeof(struct gfs2_meta_header)) /
		sizeof(uint64_t);
	sdp->sd_jbsize = sdp->bsize - sizeof(struct gfs2_meta_header);
	sdp->sd_hash_bsize = sdp->bsize / 2;
	sdp->sd_hash_bsize_shift = sdp->sd_sb.sb_bsize_shift - 1;
	sdp->sd_hash_ptrs = sdp->sd_hash_bsize / sizeof(uint64_t);

	/*  Compute maximum reservation required to add a entry to a directory  */

	hash_blocks = DIV_RU(sizeof(uint64_t) * (1 << GFS2_DIR_MAX_DEPTH),
			     sdp->sd_jbsize);

	ind_blocks = 0;
	for (tmp_blocks = hash_blocks; tmp_blocks > sdp->sd_diptrs;) {
		tmp_blocks = DIV_RU(tmp_blocks, sdp->sd_inptrs);
		ind_blocks += tmp_blocks;
	}

	leaf_blocks = 2 + GFS2_DIR_MAX_DEPTH;

	sdp->sd_max_dirres = hash_blocks + ind_blocks + leaf_blocks;

	sdp->sd_max_height = compute_heightsize(sdp, sdp->sd_heightsize,
						sdp->bsize, sdp->sd_diptrs,
						sdp->sd_inptrs);
	sdp->sd_max_jheight = compute_heightsize(sdp, sdp->sd_jheightsize,
						 sdp->sd_jbsize,
						 sdp->sd_diptrs,
						 sdp->sd_inptrs);
}

int check_for_gfs2(struct gfs2_sbd *sdp)
{
	FILE *fp;
	char buffer[PATH_MAX];
	char fstype[80];
	int fsdump, fspass, ret;
	char fspath[PATH_MAX];
	char fsoptions[PATH_MAX];
	char *realname;

	realname = realpath(sdp->path_name, NULL);
	if (!realname) {
		return -1;
	}
	fp = fopen("/proc/mounts", "r");
	if (fp == NULL) {
		free(realname);
		return -1;
	}
	while ((fgets(buffer, PATH_MAX - 1, fp)) != NULL) {
		buffer[PATH_MAX - 1] = 0;

		if (strstr(buffer, "0") == 0)
			continue;

		if ((ret = sscanf(buffer, "%s %s %s %s %d %d",
				  sdp->device_name, fspath, 
				  fstype, fsoptions, &fsdump, &fspass)) != 6) 
			continue;

		if (strcmp(fstype, "gfs2") != 0)
			continue;

		/* Check if they specified the device instead of mnt point */
		if (strcmp(sdp->device_name, realname) == 0)
			strcpy(sdp->path_name, fspath); /* fix it */
		else if (strcmp(fspath, realname) != 0)
			continue;

		fclose(fp);
		free(realname);
		if (strncmp(sdp->device_name, "/dev/loop", 9) == 0) {
			errno = EINVAL;
			return -1;
		}

		return 0;
	}
	free(realname);
	fclose(fp);
	errno = EINVAL;
	return -1;
}

static void lock_for_admin(struct gfs2_sbd *sdp)
{
	int error;

	if (sdp->debug)
		printf("\nTrying to get admin lock...\n");

	sdp->metafs_fd = open(sdp->metafs_path, O_RDONLY | O_NOFOLLOW);
	if (sdp->metafs_fd < 0)
		die("can't open %s: %s\n",
		    sdp->metafs_path, strerror(errno));
	
	error = flock(sdp->metafs_fd, LOCK_EX);
	if (error)
		die("can't flock %s: %s\n", sdp->metafs_path, strerror(errno));
	if (sdp->debug)
		printf("Got it.\n");
}


void mount_gfs2_meta(struct gfs2_sbd *sdp)
{
	int ret;

	memset(sdp->metafs_path, 0, PATH_MAX);
	snprintf(sdp->metafs_path, PATH_MAX - 1, "/tmp/.gfs2meta.XXXXXX");

	if(!mkdtemp(sdp->metafs_path))
		die("Couldn't create %s : %s\n", sdp->metafs_path,
		    strerror(errno));

	ret = mount(sdp->path_name, sdp->metafs_path, "gfs2meta", 0, NULL);
	if (ret) {
		rmdir(sdp->metafs_path);
		die("Couldn't mount %s : %s\n", sdp->metafs_path,
		    strerror(errno));
	}
	lock_for_admin(sdp);
}

void cleanup_metafs(struct gfs2_sbd *sdp)
{
	int ret;

	if (sdp->metafs_fd <= 0)
		return;

	fsync(sdp->metafs_fd);
	close(sdp->metafs_fd);
	ret = umount(sdp->metafs_path);
	if (ret)
		fprintf(stderr, "Couldn't unmount %s : %s\n",
			sdp->metafs_path, strerror(errno));
	else
		rmdir(sdp->metafs_path);
}

static char *__get_sysfs(char *fsname, char *filename)
{
	char path[PATH_MAX];
	int fd, rv;

	memset(path, 0, PATH_MAX);
	memset(sysfs_buf, 0, PAGE_SIZE);
	snprintf(path, PATH_MAX - 1, "%s/%s/%s", SYS_BASE, fsname, filename);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		die("can't open %s: %s\n", path, strerror(errno));
	rv = read(fd, sysfs_buf, PAGE_SIZE);
	if (rv < 0)
		die("can't read from %s: %s\n", path, strerror(errno));

	close(fd);
	return sysfs_buf;
}

char *get_sysfs(char *fsname, char *filename)
{
	char *p = strchr(__get_sysfs(fsname, filename), '\n');
	if (p)
		*p = '\0';
	return sysfs_buf;
}

unsigned int get_sysfs_uint(char *fsname, char *filename)
{
	unsigned int x;
	sscanf(__get_sysfs(fsname, filename), "%u", &x);
	return x;
}

int set_sysfs(char *fsname, char *filename, char *val)
{
	char path[PATH_MAX];
	int fd, rv, len;

	len = strlen(val) + 1;
	if (len > PAGE_SIZE) {
		errno = EINVAL;
		return -1;
	}

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX - 1, "%s/%s/%s", SYS_BASE, fsname, filename);

	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;

	rv = write(fd, val, len);
	if (rv != len) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

/**
 * get_list - Get the list of GFS2 filesystems
 *
 * Returns: a NULL terminated string
 */

#define LIST_SIZE 1048576

char *get_list(void)
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

	return list;
}

char *find_debugfs_mount(void)
{
	FILE *file;
	char line[PATH_MAX];
	char device[PATH_MAX], type[PATH_MAX];
	char *path;

	file = fopen("/proc/mounts", "rt");
	if (!file)
		die("can't open /proc/mounts: %s\n", strerror(errno));

	path = malloc(PATH_MAX);
	if (!path)
		die("Can't allocate memory for debugfs.\n");
	while (fgets(line, PATH_MAX, file)) {

		if (sscanf(line, "%s %s %s", device, path, type) != 3)
			continue;
		if (!strcmp(type, "debugfs")) {
			fclose(file);
			return path;
		}
	}

	free(path);
	fclose(file);
	return NULL;
}

/**
 * mp2fsname - Find the name for a filesystem given its mountpoint
 *
 * We do this by iterating through gfs2 dirs in /sys/fs/gfs2/ looking for
 * one where the "id" attribute matches the device id returned by stat for
 * the mount point.  The reason we go through all this is simple: the
 * kernel's sysfs is named after the VFS s_id, not the device name.
 * So it's perfectly legal to do something like this to simulate user
 * conditions without the proper hardware:
 *    # rm /dev/sdb1
 *    # mkdir /dev/cciss
 *    # mknod /dev/cciss/c0d0p1 b 8 17
 *    # mount -tgfs2 /dev/cciss/c0d0p1 /mnt/gfs2
 *    # gfs2_tool gettune /mnt/gfs2
 * In this example the tuning variables are in a directory named after the
 * VFS s_id, which in this case would be /sys/fs/gfs2/sdb1/
 *
 * Returns: the fsname
 */

char *mp2fsname(char *mp)
{
	char device_id[PATH_MAX], *fsname = NULL;
	struct stat statbuf;
	DIR *d;
	struct dirent *de;

	if (stat(mp, &statbuf))
		return NULL;

	memset(device_id, 0, sizeof(device_id));
	sprintf(device_id, "%i:%i", major(statbuf.st_dev),
		minor(statbuf.st_dev));

	d = opendir(SYS_BASE);
	if (!d)
		die("can't open %s: %s\n", SYS_BASE, strerror(errno));

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;

		if (strcmp(get_sysfs(de->d_name, "id"), device_id) == 0) {
			fsname = strdup(de->d_name);
			break;
		}
	}

	closedir(d);

	return fsname;
}
