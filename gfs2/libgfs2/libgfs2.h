#ifndef __LIBGFS2_DOT_H__
#define __LIBGFS2_DOT_H__

#include <features.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <endian.h>
#include <byteswap.h>

#include <linux/gfs2_ondisk.h>
#include "osi_list.h"

__BEGIN_DECLS

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#if __BYTE_ORDER == __BIG_ENDIAN

#define be16_to_cpu(x) (x)
#define be32_to_cpu(x) (x)
#define be64_to_cpu(x) (x)

#define cpu_to_be16(x) (x)
#define cpu_to_be32(x) (x)
#define cpu_to_be64(x) (x)

#define le16_to_cpu(x) (bswap_16((x)))
#define le32_to_cpu(x) (bswap_32((x)))
#define le64_to_cpu(x) (bswap_64((x)))

#define cpu_to_le16(x) (bswap_16((x)))
#define cpu_to_le32(x) (bswap_32((x)))
#define cpu_to_le64(x) (bswap_64((x)))

#endif  /*  __BYTE_ORDER == __BIG_ENDIAN  */


#if __BYTE_ORDER == __LITTLE_ENDIAN

#define be16_to_cpu(x) (bswap_16((x)))
#define be32_to_cpu(x) (bswap_32((x)))
#define be64_to_cpu(x) (bswap_64((x)))

#define cpu_to_be16(x) (bswap_16((x)))
#define cpu_to_be32(x) (bswap_32((x)))
#define cpu_to_be64(x) (bswap_64((x))) 

#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define le64_to_cpu(x) (x)

#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)

#endif  /*  __BYTE_ORDER == __LITTLE_ENDIAN  */

static __inline__ __attribute__((noreturn)) void die(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "%s: ", __FILE__);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(-1);
}

struct device {
	uint64_t start;
	uint64_t length;
	uint32_t rgf_flags;
};

struct gfs2_bitmap
{
	uint32_t   bi_offset;  /* The offset in the buffer of the first byte */
	uint32_t   bi_start;   /* The position of the first byte in this block */
	uint32_t   bi_len;     /* The number of bytes in this block */
};

struct rgrp_list {
	osi_list_t list;
	uint64_t start;	   /* The offset of the beginning of this resource group */
	uint64_t length;	/* The length of this resource group */
	uint32_t rgf_flags;

	struct gfs2_rindex ri;
	struct gfs2_rgrp rg;
	struct gfs2_bitmap *bits;
	struct gfs2_buffer_head **bh;
};

struct gfs2_buffer_head {
	osi_list_t b_list;
	osi_list_t b_hash;
	osi_list_t b_altlist; /* alternate list */

	unsigned int b_count;
	uint64_t b_blocknr;
	char *b_data;

	int b_changed;
};

struct dup_blocks {
	osi_list_t list;
	uint64_t block_no;
	osi_list_t ref_inode_list;
};

struct special_blocks {
	osi_list_t list;
	uint64_t block;
};

struct gfs2_sbd;
struct gfs2_inode {
	struct gfs2_dinode i_di;
	struct gfs2_buffer_head *i_bh;
	struct gfs2_sbd *i_sbd;
};

#define BUF_HASH_SHIFT       (13)    /* # hash buckets = 8K */
#define BUF_HASH_SIZE        (1 << BUF_HASH_SHIFT)
#define BUF_HASH_MASK        (BUF_HASH_SIZE - 1)

/* FIXME not sure that i want to keep a record of the inodes or the
 * contents of them, or both ... if I need to write back to them, it
 * would be easier to hold the inode as well  */
struct per_node
{
	struct gfs2_inode *inum;
	struct gfs2_inum_range inum_range;
	struct gfs2_inode *statfs;
	struct gfs2_statfs_change statfs_change;
	struct gfs2_inode *unlinked;
	struct gfs2_inode *quota;
	struct gfs2_quota_change quota_change;
};

struct master_dir
{
	struct gfs2_inode *inum;
	uint64_t next_inum;
	struct gfs2_inode *statfs;
	struct gfs2_statfs_change statfs_change;

	struct gfs2_rindex rindex;
	struct gfs2_inode *qinode;
	struct gfs2_quota quotas;

	struct gfs2_inode       *jiinode;
	struct gfs2_inode       *riinode;
	struct gfs2_inode       *rooti;
	struct gfs2_inode       *pinode;
	
	struct gfs2_inode **journal;      /* Array of journals */
	uint32_t journals;                /* Journal count */
	struct per_node *pn;              /* Array of per_node entries */
};

struct buf_list {
	unsigned int num_bufs;
	unsigned int spills;
	uint32_t limit;
	osi_list_t list;
	struct gfs2_sbd *sbp;
	osi_list_t buf_hash[BUF_HASH_SIZE];
};

struct gfs2_sbd {
	struct gfs2_sb sd_sb;    /* a copy of the ondisk structure */
	char lockproto[GFS2_LOCKNAME_LEN];
	char locktable[GFS2_LOCKNAME_LEN];

	unsigned int bsize;	     /* The block size of the FS (in bytes) */
	unsigned int jsize;	     /* Size of journals (in MB) */
	unsigned int rgsize;     /* Size of resource groups (in MB) */
	unsigned int utsize;     /* Size of unlinked tag files (in MB) */
	unsigned int qcsize;     /* Size of quota change files (in MB) */

	int debug;
	int quiet;
	int expert;
	int override;

	char device_name[PATH_MAX];
	char *path_name;

	/* Constants */

	uint32_t sd_fsb2bb;
	uint32_t sd_fsb2bb_shift;
	uint32_t sd_diptrs;
	uint32_t sd_inptrs;
	uint32_t sd_jbsize;
	uint32_t sd_hash_bsize;
	uint32_t sd_hash_bsize_shift;
	uint32_t sd_hash_ptrs;
	uint32_t sd_max_dirres;
	uint32_t sd_max_height;
	uint64_t sd_heightsize[GFS2_MAX_META_HEIGHT];
	uint32_t sd_max_jheight;
	uint64_t sd_jheightsize[GFS2_MAX_META_HEIGHT];

	/* Not specified on the command line, but... */

	int64_t time;

	struct device device;
	uint64_t device_size;

	int device_fd;
	int path_fd;

	uint64_t sb_addr;

	uint64_t orig_fssize;
	uint64_t fssize;
	uint64_t blks_total;
	uint64_t blks_alloced;
	uint64_t dinodes_alloced;

	uint64_t orig_rgrps;
	uint64_t rgrps;
	uint64_t new_rgrps;
	osi_list_t rglist;

	unsigned int orig_journals;

	struct buf_list buf_list;   /* transient buffer list */
	struct buf_list nvbuf_list; /* non-volatile buffer list */

	struct gfs2_inode *master_dir;
	struct master_dir md;

	unsigned int writes;
	int metafs_fd;
	char metafs_path[PATH_MAX]; /* where metafs is mounted */
	struct special_blocks bad_blocks;
	struct dup_blocks dup_blocks;
	struct special_blocks eattr_blocks;
};

struct metapath {
	unsigned int mp_list[GFS2_MAX_META_HEIGHT];
};


#define GFS2_DEFAULT_BSIZE          (4096)
#define GFS2_DEFAULT_JSIZE          (128)
#define GFS2_DEFAULT_RGSIZE         (256)
#define GFS2_DEFAULT_UTSIZE         (1)
#define GFS2_DEFAULT_QCSIZE         (1)
#define GFS2_DEFAULT_LOCKPROTO      "lock_dlm"
#define GFS2_MIN_GROW_SIZE          (10)
#define GFS2_EXCESSIVE_RGS          (10000)

#define DATA (1)
#define META (2)
#define DINODE (3)

#define NOT_UPDATED (0)
#define UPDATED (1)

/* A bit of explanation is in order: */
/* updated flag means the buffer was updated from THIS function before */
/*         brelse was called. */
/* not_updated flag means the buffer may or may not have been updated  */
/*         by a function called within this one, but it wasn't updated */
/*         by this function. */
enum update_flags {
	not_updated = NOT_UPDATED,
	updated = UPDATED
};

/* bitmap.c */
struct gfs2_bmap {
        uint64_t size;
        uint64_t mapsize;
        int chunksize;
        int chunks_per_byte;
        char *map;
};

/* block_list.c */
#define FREE	        (0x0)  /*   0000 */
#define BLOCK_IN_USE    (0x1)  /*   0001 */
#define DIR_INDIR_BLK   (0x2)  /*   0010 */
#define DIR_INODE       (0x3)  /*   0011 */
#define FILE_INODE      (0x4)  /*   0100 */
#define LNK_INODE       (0x5)
#define BLK_INODE       (0x6)
#define CHR_INODE       (0x7)
#define FIFO_INODE      (0x8)
#define SOCK_INODE      (0x9)
#define DIR_LEAF_INODE  (0xA)  /*   1010 */
#define JOURNAL_BLK     (0xB)  /*   1011 */
#define OTHER_META      (0xC)  /*   1100 */
#define EATTR_META      (0xD)  /*   1101 */
#define UNUSED1         (0xE)  /*   1110 */
#define INVALID_META    (0xF)  /*   1111 */

/* Must be kept in sync with mark_to_bitmap array in block_list.c */
enum gfs2_mark_block {
	gfs2_block_free = FREE,
	gfs2_block_used = BLOCK_IN_USE,
	gfs2_indir_blk = DIR_INDIR_BLK,
	gfs2_inode_dir = DIR_INODE,
	gfs2_inode_file = FILE_INODE,
	gfs2_inode_lnk = LNK_INODE,
	gfs2_inode_blk = BLK_INODE,
	gfs2_inode_chr = CHR_INODE,
	gfs2_inode_fifo = FIFO_INODE,
	gfs2_inode_sock = SOCK_INODE,
	gfs2_leaf_blk = DIR_LEAF_INODE,
	gfs2_journal_blk = JOURNAL_BLK,
	gfs2_meta_other = OTHER_META,
	gfs2_meta_eattr = EATTR_META,
	gfs2_meta_unused = UNUSED1,
	gfs2_meta_inval = INVALID_META,
	gfs2_bad_block,      /* Contains at least one bad block */
	gfs2_dup_block,      /* Contains at least one duplicate block */
	gfs2_eattr_block,    /* Contains an eattr */
};

struct gfs2_block_query {
        uint8_t block_type;
        uint8_t bad_block;
        uint8_t dup_block;
        uint8_t eattr_block;
};

struct gfs2_gbmap {
        struct gfs2_bmap group_map;
        struct gfs2_bmap bad_map;
        struct gfs2_bmap dup_map;
        struct gfs2_bmap eattr_map;
};

union gfs2_block_lists {
        struct gfs2_gbmap gbmap;
};

/* bitmap implementation */
struct gfs2_block_list {
        union gfs2_block_lists list;
};

extern struct gfs2_block_list *gfs2_block_list_create(struct gfs2_sbd *sdp,
					       uint64_t size,
					       uint64_t *addl_mem_needed);
extern struct special_blocks *blockfind(struct special_blocks *blist, uint64_t num);
extern void gfs2_special_set(struct special_blocks *blocklist, uint64_t block);
extern void gfs2_special_free(struct special_blocks *blist);
extern int gfs2_block_mark(struct gfs2_sbd *sdp, struct gfs2_block_list *il,
	 		   uint64_t block, enum gfs2_mark_block mark);
extern int gfs2_block_set(struct gfs2_sbd *sdp, struct gfs2_block_list *il,
			  uint64_t block, enum gfs2_mark_block mark);
extern int gfs2_block_clear(struct gfs2_sbd *sdp, struct gfs2_block_list *il,
			    uint64_t block, enum gfs2_mark_block m);
extern int gfs2_block_check(struct gfs2_sbd *sdp, struct gfs2_block_list *il,
			    uint64_t block, struct gfs2_block_query *val);
extern void *gfs2_block_list_destroy(struct gfs2_sbd *sdp,
				     struct gfs2_block_list *il);

/* buf.c */
extern void init_buf_list(struct gfs2_sbd *sdp, struct buf_list *bl, uint32_t limit);
extern struct gfs2_buffer_head *bfind(struct buf_list *bl, uint64_t num);
extern struct gfs2_buffer_head *__bget_generic(struct buf_list *bl,
					       uint64_t num, int find_existing,
					       int read_disk, int line,
					       const char *caller);
extern struct gfs2_buffer_head *__bget(struct buf_list *bl, uint64_t num,
				       int line, const char *caller);
extern struct gfs2_buffer_head *__bread(struct buf_list *bl, uint64_t num,
					int line, const char *caller);
extern struct gfs2_buffer_head *bhold(struct gfs2_buffer_head *bh);
extern void brelse(struct gfs2_buffer_head *bh, enum update_flags is_updated);
extern void __bsync(struct buf_list *bl, int line, const char *caller);
extern void __bcommit(struct buf_list *bl, int line, const char *caller);

#define bget_generic(bl, num, find, read) __bget_generic(bl, num, find, read, \
							 __LINE__, \
							 __FUNCTION__)
#define bget(bl, num) __bget(bl, num, __LINE__, __FUNCTION__)
#define bread(bl, num) __bread(bl, num, __LINE__, __FUNCTION__)
#define bsync(bl) do { __bsync(bl, __LINE__, __FUNCTION__); } while(0)
#define bcommit(bl) do { __bcommit(bl, __LINE__, __FUNCTION__); } while(0)

/* device_geometry.c */
extern int device_geometry(struct gfs2_sbd *sdp);
extern int fix_device_geometry(struct gfs2_sbd *sdp);

/* fs_bits.c */
#define BFITNOENT (0xFFFFFFFF)

/* functions with blk #'s that are buffer relative */
extern uint32_t gfs2_bitcount(unsigned char *buffer, unsigned int buflen,
			      unsigned char state);
extern uint32_t gfs2_bitfit(unsigned char *buffer, unsigned int buflen,
			    uint32_t goal, unsigned char old_state);

/* functions with blk #'s that are rgrp relative */
extern uint32_t gfs2_blkalloc_internal(struct rgrp_list *rgd, uint32_t goal,
				       unsigned char old_state,
				       unsigned char new_state, int do_it);
extern int gfs2_check_range(struct gfs2_sbd *sdp, uint64_t blkno);

/* functions with blk #'s that are file system relative */
extern int gfs2_set_bitmap(struct gfs2_sbd *sdp, uint64_t blkno, int state);

/* fs_geometry.c */
extern void rgblocks2bitblocks(unsigned int bsize, uint32_t *rgblocks,
			       uint32_t *bitblocks);
extern void compute_rgrp_layout(struct gfs2_sbd *sdp, int rgsize_specified);
extern void build_rgrps(struct gfs2_sbd *sdp, int write);

/* fs_ops.c */
#define IS_LEAF     (1)
#define IS_DINODE   (2)

extern struct metapath *find_metapath(struct gfs2_inode *ip, uint64_t block);
extern void lookup_block(struct gfs2_inode *ip, struct gfs2_buffer_head *bh,
			 unsigned int height, struct metapath *mp,
			 int create, int *new, uint64_t *block);
extern struct gfs2_inode *inode_get(struct gfs2_sbd *sdp,
				    struct gfs2_buffer_head *bh);
extern void inode_put(struct gfs2_inode *ip, enum update_flags updated);
extern uint64_t data_alloc(struct gfs2_inode *ip);
extern uint64_t meta_alloc(struct gfs2_inode *ip);
extern uint64_t dinode_alloc(struct gfs2_sbd *sdp);
extern int gfs2_readi(struct gfs2_inode *ip, void *buf, uint64_t offset,
		      unsigned int size);
extern int gfs2_writei(struct gfs2_inode *ip, void *buf, uint64_t offset,
		       unsigned int size);
extern struct gfs2_buffer_head *get_file_buf(struct gfs2_inode *ip,
					     uint64_t lbn, int prealloc);
extern struct gfs2_buffer_head *init_dinode(struct gfs2_sbd *sdp,
					    struct gfs2_inum *inum,
					    unsigned int mode, uint32_t flags,
					    struct gfs2_inum *parent);
extern struct gfs2_inode *createi(struct gfs2_inode *dip, const char *filename,
				  unsigned int mode, uint32_t flags);
extern void dirent2_del(struct gfs2_inode *dip, struct gfs2_buffer_head *bh,
			struct gfs2_dirent *prev, struct gfs2_dirent *cur);
extern struct gfs2_inode *gfs2_load_inode(struct gfs2_sbd *sbp, uint64_t block);
extern int gfs2_lookupi(struct gfs2_inode *dip, const char *filename, int len,
			struct gfs2_inode **ipp);
extern void dir_add(struct gfs2_inode *dip, const char *filename, int len,
		    struct gfs2_inum *inum, unsigned int type);
extern int gfs2_dirent_del(struct gfs2_inode *dip, struct gfs2_buffer_head *bh,
			   const char *filename, int filename_len);
extern void block_map(struct gfs2_inode *ip, uint64_t lblock, int *new,
		      uint64_t *dblock, uint32_t *extlen, int prealloc,
		      enum update_flags if_changed);
extern void gfs2_get_leaf_nr(struct gfs2_inode *dip, uint32_t index,
			     uint64_t *leaf_out);
extern void gfs2_put_leaf_nr(struct gfs2_inode *dip, uint32_t inx, uint64_t leaf_out);
extern void gfs2_free_block(struct gfs2_sbd *sdp, uint64_t block);
extern int gfs2_freedi(struct gfs2_sbd *sdp, uint64_t block);
extern int gfs2_get_leaf(struct gfs2_inode *dip, uint64_t leaf_no,
			 struct gfs2_buffer_head **bhp);
extern int gfs2_dirent_first(struct gfs2_inode *dip,
			     struct gfs2_buffer_head *bh,
			     struct gfs2_dirent **dent);
extern int gfs2_dirent_next(struct gfs2_inode *dip, struct gfs2_buffer_head *bh,
			    struct gfs2_dirent **dent);
extern void build_height(struct gfs2_inode *ip, int height);
extern void unstuff_dinode(struct gfs2_inode *ip);
extern unsigned int calc_tree_height(struct gfs2_inode *ip, uint64_t size);
extern int write_journal(struct gfs2_sbd *sdp, struct gfs2_inode *ip,
			 unsigned int j, unsigned int blocks);

/**
 * device_size - figure out a device's size
 * @fd: the file descriptor of a device
 * @bytes: the number of bytes the device holds
 *
 * Returns: -1 on error (with errno set), 0 on success (with @bytes set)
 */

extern int device_size(int fd, uint64_t *bytes);

/* gfs1.c - GFS1 backward compatibility functions */
struct gfs_indirect {
	struct gfs2_meta_header in_header;

	char in_reserved[64];
};

struct gfs_dinode {
	struct gfs2_meta_header di_header;

	struct gfs2_inum di_num; /* formal inode # and block address */

	uint32_t di_mode;	/* mode of file */
	uint32_t di_uid;	/* owner's user id */
	uint32_t di_gid;	/* owner's group id */
	uint32_t di_nlink;	/* number (qty) of links to this file */
	uint64_t di_size;	/* number (qty) of bytes in file */
	uint64_t di_blocks;	/* number (qty) of blocks in file */
	int64_t di_atime;	/* time last accessed */
	int64_t di_mtime;	/* time last modified */
	int64_t di_ctime;	/* time last changed */

	/*  Non-zero only for character or block device nodes  */
	uint32_t di_major;	/* device major number */
	uint32_t di_minor;	/* device minor number */

	/*  Block allocation strategy  */
	uint64_t di_rgrp;	/* dinode rgrp block number */
	uint64_t di_goal_rgrp;	/* rgrp to alloc from next */
	uint32_t di_goal_dblk;	/* data block goal */
	uint32_t di_goal_mblk;	/* metadata block goal */

	uint32_t di_flags;	/* GFS_DIF_... */

	/*  struct gfs_rindex, struct gfs_jindex, or struct gfs_dirent */
	uint32_t di_payload_format;  /* GFS_FORMAT_... */
	uint16_t di_type;	/* GFS_FILE_... type of file */
	uint16_t di_height;	/* height of metadata (0 == stuffed) */
	uint32_t di_incarn;	/* incarnation (unused, see gfs_meta_header) */
	uint16_t di_pad;

	/*  These only apply to directories  */
	uint16_t di_depth;	/* Number of bits in the table */
	uint32_t di_entries;	/* The # (qty) of entries in the directory */

	/*  This formed an on-disk chain of unused dinodes  */
	struct gfs2_inum di_next_unused;  /* used in old versions only */

	uint64_t di_eattr;	/* extended attribute block number */

	char di_reserved[56];
};

extern void gfs1_lookup_block(struct gfs2_inode *ip,
			      struct gfs2_buffer_head *bh,
			      unsigned int height, struct metapath *mp,
			      int create, int *new, uint64_t *block);
extern void gfs1_block_map(struct gfs2_inode *ip, uint64_t lblock, int *new,
			   uint64_t *dblock, uint32_t *extlen, int prealloc);
extern int gfs1_readi(struct gfs2_inode *ip, void *buf, uint64_t offset,
		      unsigned int size);
extern int gfs1_rindex_read(struct gfs2_sbd *sdp, int fd, int *count1);
extern int gfs1_ri_update(struct gfs2_sbd *sdp, int fd, int *rgcount);
extern struct gfs2_inode *gfs_inode_get(struct gfs2_sbd *sdp,
					struct gfs2_buffer_head *bh);

/* gfs2_log.c */
struct gfs2_options {
	char *device;
	int yes:1;
	int no:1;
	int query:1;
};

#define MSG_DEBUG       7
#define MSG_INFO        6
#define MSG_NOTICE      5
#define MSG_WARN        4
#define MSG_ERROR       3
#define MSG_CRITICAL    2
#define MSG_NULL        1

#define print_log(iif, priority, format...)     \
do { print_fsck_log(iif, priority, __FILE__, __LINE__, ## format); } while(0)

#define log_debug(format...) \
do { print_log(0, MSG_DEBUG, format); } while(0)
#define log_info(format...) \
do { print_log(0, MSG_INFO, format); } while(0)

#define log_notice(format...) \
do { print_log(0, MSG_NOTICE, format); } while(0)

#define log_warn(format...) \
do { print_log(0, MSG_WARN, format); } while(0)

#define log_err(format...) \
do { print_log(0, MSG_ERROR, format); } while(0)

#define log_crit(format...) \
do { print_log(0, MSG_CRITICAL, format); } while(0)

#define stack log_debug("<backtrace> - %s()\n", __func__)

#define log_at_debug(format...)         \
do { print_log(1, MSG_DEBUG, format); } while(0)

#define log_at_info(format...) \
do { print_log(1, MSG_INFO, format); } while(0)

#define log_at_notice(format...) \
do { print_log(1, MSG_NOTICE, format); } while(0)

#define log_at_warn(format...) \
do { print_log(1, MSG_WARN, format); } while(0)

#define log_at_err(format...) \
do { print_log(1, MSG_ERROR, format); } while(0)

#define log_at_crit(format...) \
do { print_log(1, MSG_CRITICAL, format); } while(0)

extern void increase_verbosity(void);
extern void decrease_verbosity(void);
extern void print_fsck_log(int iif, int priority, const char *file, int line,
			   const char *format, ...)
	__attribute__((format(printf,5,6)));
extern char generic_interrupt(const char *caller, const char *where,
			      const char *progress, const char *question,
			      const char *answers);
extern int gfs2_query(int *setonabort, struct gfs2_options *opts,
		      const char *format, ...)
	__attribute__((format(printf,3,4)));

/* misc.c */

extern int compute_heightsize(struct gfs2_sbd *sdp, uint64_t *heightsize,
		uint32_t *maxheight, uint32_t bsize1, int diptrs, int inptrs);
extern int compute_constants(struct gfs2_sbd *sdp);
extern int find_gfs2_meta(struct gfs2_sbd *sdp);
extern int dir_exists(const char *dir);
extern int check_for_gfs2(struct gfs2_sbd *sdp);
extern int mount_gfs2_meta(struct gfs2_sbd *sdp);
extern void cleanup_metafs(struct gfs2_sbd *sdp);
extern char *find_debugfs_mount(void);
extern char *mp2fsname(char *mp);
extern char *get_sysfs(const char *fsname, const char *filename);
extern int get_sysfs_uint(const char *fsname, const char *filename, unsigned int *val);
extern int set_sysfs(const char *fsname, const char *filename, const char *val);
extern int is_fsname(char *name);

/* recovery.c */
extern void gfs2_replay_incr_blk(struct gfs2_inode *ip, unsigned int *blk);
extern int gfs2_replay_read_block(struct gfs2_inode *ip, unsigned int blk,
				  struct gfs2_buffer_head **bh);
extern int gfs2_revoke_add(struct gfs2_sbd *sdp, uint64_t blkno, unsigned int where);
extern int gfs2_revoke_check(struct gfs2_sbd *sdp, uint64_t blkno,
			     unsigned int where);
extern void gfs2_revoke_clean(struct gfs2_sbd *sdp);
extern int get_log_header(struct gfs2_inode *ip, unsigned int blk,
			  struct gfs2_log_header *head);
extern int find_good_lh(struct gfs2_inode *ip, unsigned int *blk,
			struct gfs2_log_header *head);
extern int jhead_scan(struct gfs2_inode *ip, struct gfs2_log_header *head);
extern int gfs2_find_jhead(struct gfs2_inode *ip, struct gfs2_log_header *head);
extern int clean_journal(struct gfs2_inode *ip, struct gfs2_log_header *head);

/* rgrp.c */
extern int gfs2_compute_bitstructs(struct gfs2_sbd *sdp, struct rgrp_list *rgd);
extern struct rgrp_list *gfs2_blk2rgrpd(struct gfs2_sbd *sdp, uint64_t blk);
extern uint64_t gfs2_rgrp_read(struct gfs2_sbd *sdp, struct rgrp_list *rgd);
extern void gfs2_rgrp_relse(struct rgrp_list *rgd, enum update_flags updated);
extern void gfs2_rgrp_free(osi_list_t *rglist, enum update_flags updated);

/* structures.c */
extern int build_master(struct gfs2_sbd *sdp);
extern void build_sb(struct gfs2_sbd *sdp, const unsigned char *uuid);
extern int build_jindex(struct gfs2_sbd *sdp);
extern int build_per_node(struct gfs2_sbd *sdp);
extern int build_inum(struct gfs2_sbd *sdp);
extern int build_statfs(struct gfs2_sbd *sdp);
extern int build_rindex(struct gfs2_sbd *sdp);
extern int build_quota(struct gfs2_sbd *sdp);
extern int build_root(struct gfs2_sbd *sdp);
extern int do_init(struct gfs2_sbd *sdp);
extern int gfs2_check_meta(struct gfs2_buffer_head *bh, int type);
extern int gfs2_next_rg_meta(struct rgrp_list *rgd, uint64_t *block, int first);
extern int gfs2_next_rg_metatype(struct gfs2_sbd *sdp, struct rgrp_list *rgd,
				 uint64_t *block, uint32_t type, int first);
/* super.c */
extern int check_sb(struct gfs2_sb *sb);
extern int read_sb(struct gfs2_sbd *sdp);
extern int ji_update(struct gfs2_sbd *sdp);
extern int rindex_read(struct gfs2_sbd *sdp, int fd, int *count1);
extern int ri_update(struct gfs2_sbd *sdp, int fd, int *rgcount);
extern int write_sb(struct gfs2_sbd *sdp);

/* ondisk.c */
extern uint32_t gfs2_disk_hash(const char *data, int len);
extern const char *str_uuid(const unsigned char *uuid);
extern void gfs2_print_uuid(const unsigned char *uuid);
extern void print_it(const char *label, const char *fmt, const char *fmt2, ...)
	__attribute__((format(printf,2,4)));

/* Translation functions */

extern void gfs2_inum_in(struct gfs2_inum *no, char *buf);
extern void gfs2_inum_out(struct gfs2_inum *no, char *buf);
extern void gfs2_meta_header_in(struct gfs2_meta_header *mh, char *buf);
extern void gfs2_meta_header_out(struct gfs2_meta_header *mh, char *buf);
extern void gfs2_sb_in(struct gfs2_sb *sb, char *buf);
extern void gfs2_sb_out(struct gfs2_sb *sb, char *buf);
extern void gfs2_rindex_in(struct gfs2_rindex *ri, char *buf);
extern void gfs2_rindex_out(struct gfs2_rindex *ri, char *buf);
extern void gfs2_rgrp_in(struct gfs2_rgrp *rg, char *buf);
extern void gfs2_rgrp_out(struct gfs2_rgrp *rg, char *buf);
extern void gfs2_quota_in(struct gfs2_quota *qu, char *buf);
extern void gfs2_quota_out(struct gfs2_quota *qu, char *buf);
extern void gfs2_dinode_in(struct gfs2_dinode *di, char *buf);
extern void gfs2_dinode_out(struct gfs2_dinode *di, char *buf);
extern void gfs2_dirent_in(struct gfs2_dirent *de, char *buf);
extern void gfs2_dirent_out(struct gfs2_dirent *de, char *buf);
extern void gfs2_leaf_in(struct gfs2_leaf *lf, char *buf);
extern void gfs2_leaf_out(struct gfs2_leaf *lf, char *buf);
extern void gfs2_ea_header_in(struct gfs2_ea_header *ea, char *buf);
extern void gfs2_ea_header_out(struct gfs2_ea_header *ea, char *buf);
extern void gfs2_log_header_in(struct gfs2_log_header *lh, char *buf);
extern void gfs2_log_header_out(struct gfs2_log_header *lh, char *buf);
extern void gfs2_log_descriptor_in(struct gfs2_log_descriptor *ld, char *buf);
extern void gfs2_log_descriptor_out(struct gfs2_log_descriptor *ld, char *buf);
extern void gfs2_inum_range_in(struct gfs2_inum_range *ir, char *buf);
extern void gfs2_inum_range_out(struct gfs2_inum_range *ir, char *buf);
extern void gfs2_statfs_change_in(struct gfs2_statfs_change *sc, char *buf);
extern void gfs2_statfs_change_out(struct gfs2_statfs_change *sc, char *buf);
extern void gfs2_quota_change_in(struct gfs2_quota_change *qc, char *buf);
extern void gfs2_quota_change_out(struct gfs2_quota_change *qc, char *buf);

/* Printing functions */

extern void gfs2_inum_print(struct gfs2_inum *no);
extern void gfs2_meta_header_print(struct gfs2_meta_header *mh);
extern void gfs2_sb_print(struct gfs2_sb *sb);
extern void gfs2_rindex_print(struct gfs2_rindex *ri);
extern void gfs2_rgrp_print(struct gfs2_rgrp *rg);
extern void gfs2_quota_print(struct gfs2_quota *qu);
extern void gfs2_dinode_print(struct gfs2_dinode *di);
extern void gfs2_dirent_print(struct gfs2_dirent *de, char *name);
extern void gfs2_leaf_print(struct gfs2_leaf *lf);
extern void gfs2_ea_header_print(struct gfs2_ea_header *ea, char *name);
extern void gfs2_log_header_print(struct gfs2_log_header *lh);
extern void gfs2_log_descriptor_print(struct gfs2_log_descriptor *ld);
extern void gfs2_inum_range_print(struct gfs2_inum_range *ir);
extern void gfs2_statfs_change_print(struct gfs2_statfs_change *sc);
extern void gfs2_quota_change_print(struct gfs2_quota_change *qc);

__END_DECLS

#endif /* __LIBGFS2_DOT_H__ */
