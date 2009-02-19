/* pass1 checks inodes for format & type, duplicate blocks, & incorrect
 * block count.
 *
 * It builds up tables that contains the state of each block (free,
 * block in use, metadata type, etc), as well as bad blocks and
 * duplicate blocks.  (See block_list.[ch] for more info)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <inttypes.h>

#include "libgfs2.h"
#include "fsck.h"
#include "util.h"
#include "link.h"
#include "metawalk.h"

struct block_count {
	uint64_t indir_count;
	uint64_t data_count;
	uint64_t ea_count;
};

static int leaf(struct gfs2_inode *ip, uint64_t block,
		struct gfs2_buffer_head *bh, void *private);
static int check_metalist(struct gfs2_inode *ip, uint64_t block,
			  struct gfs2_buffer_head **bh, void *private);
static int check_data(struct gfs2_inode *ip, uint64_t block, void *private);
static int check_eattr_indir(struct gfs2_inode *ip, uint64_t indirect,
			     uint64_t parent, struct gfs2_buffer_head **bh,
			     enum update_flags *want_updated, void *private);
static int check_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
			    uint64_t parent, struct gfs2_buffer_head **bh,
			    enum update_flags *want_updated, void *private);
static int check_eattr_entries(struct gfs2_inode *ip,
			       struct gfs2_buffer_head *leaf_bh,
			       struct gfs2_ea_header *ea_hdr,
			       struct gfs2_ea_header *ea_hdr_prev,
			       void *private);
static int check_extended_leaf_eattr(struct gfs2_inode *ip, uint64_t *data_ptr,
				     struct gfs2_buffer_head *leaf_bh,
				     struct gfs2_ea_header *ea_hdr,
				     struct gfs2_ea_header *ea_hdr_prev,
				     enum update_flags *want_updated,
				     void *private);
static int finish_eattr_indir(struct gfs2_inode *ip, int indir_ok,
			      enum update_flags *want_updated, void *private);

struct metawalk_fxns pass1_fxns = {
	.private = NULL,
	.check_leaf = leaf,
	.check_metalist = check_metalist,
	.check_data = check_data,
	.check_eattr_indir = check_eattr_indir,
	.check_eattr_leaf = check_eattr_leaf,
	.check_dentry = NULL,
	.check_eattr_entry = check_eattr_entries,
	.check_eattr_extentry = check_extended_leaf_eattr,
	.finish_eattr_indir = finish_eattr_indir,
};

static int leaf(struct gfs2_inode *ip, uint64_t block,
		struct gfs2_buffer_head *bh, void *private)
{
	struct block_count *bc = (struct block_count *) private;

	log_debug("\tLeaf block at %15" PRIu64 " (0x%" PRIx64 ")\n",
			  block, block);
	gfs2_block_set(ip->i_sbd, bl, block, gfs2_leaf_blk);
	bc->indir_count++;
	return 0;
}

static int check_metalist(struct gfs2_inode *ip, uint64_t block,
			  struct gfs2_buffer_head **bh, void *private)
{
	struct gfs2_block_query q = {0};
	int found_dup = 0;
	struct gfs2_buffer_head *nbh;
	struct block_count *bc = (struct block_count *)private;

	*bh = NULL;

	if (gfs2_check_range(ip->i_sbd, block)){ /* blk outside of FS */
		gfs2_block_set(ip->i_sbd, bl, ip->i_di.di_num.no_addr,
			       gfs2_bad_block);
		log_debug("Bad indirect block pointer (out of range).\n");

		return 1;
	}
	if(gfs2_block_check(ip->i_sbd, bl, block, &q)) {
		stack;
		return -1;
	}
	if(q.block_type != gfs2_block_free) {
		log_debug("Found duplicate block in indirect block -"
				  " was marked %d\n", q.block_type);
		gfs2_block_mark(ip->i_sbd, bl, block, gfs2_dup_block);
		found_dup = 1;
	}
	nbh = bread(&ip->i_sbd->buf_list, block);

	if (gfs2_check_meta(nbh, GFS2_METATYPE_IN)){
		log_debug("Bad indirect block pointer "
				  "(points to something that is not an indirect block).\n");
		if(!found_dup) {
			gfs2_block_set(ip->i_sbd, bl, block, gfs2_meta_inval);
			brelse(nbh, not_updated);
			return 1;
		}
	}else  /* blk check ok */
		*bh = nbh;

	log_debug("Setting %" PRIu64 " (0x%" PRIx64 ") to indirect block.\n",
			  block, block);
	gfs2_block_set(ip->i_sbd, bl, block, gfs2_indir_blk);
	bc->indir_count++;

	return 0;
}

static int check_data(struct gfs2_inode *ip, uint64_t block, void *private)
{
	struct gfs2_block_query q = {0};
	struct block_count *bc = (struct block_count *) private;

	if (gfs2_check_range(ip->i_sbd, block)) {
		log_err( "Bad data block pointer (out of range)\n");
		/* Mark the owner of this block with the bad_block
		 * designator so we know to check it for out of range blocks later */
		gfs2_block_set(ip->i_sbd, bl, ip->i_di.di_num.no_addr,
			       gfs2_bad_block);
		return 1;
	}
	if(gfs2_block_check(ip->i_sbd, bl, block, &q)) {
		stack;
		return -1;
	}
	if(q.block_type != gfs2_block_free) {
		log_debug("Found duplicate block at %" PRIu64 " (0x%"PRIx64 ")\n",
				  block, block);
		gfs2_block_mark(ip->i_sbd, bl, block, gfs2_dup_block);
		bc->data_count++;
		return 1;
	}
	log_debug("Setting %" PRIu64 " (0x%" PRIx64 ") to data block\n", block,
			  block);
	gfs2_block_set(ip->i_sbd, bl, block, gfs2_block_used);
	bc->data_count++;
	return 0;
}

/* clear_eas - clear the extended attributes for an inode
 *
 * @ip       - in core inode pointer
 * @bc       - pointer to a block count structure
 * block     - the block that had the problem
 * duplicate - if this is a duplicate block, don't set it "free"
 * emsg      - what to tell the user about the eas being checked
 * Returns: 1 if the EA is fixed, else 0 if it was not fixed.
 */
static int clear_eas(struct gfs2_inode *ip, struct block_count *bc,
		     uint64_t block, int duplicate,
		     enum update_flags *want_updated, const char *emsg)
{
	struct gfs2_sbd *sdp = ip->i_sbd;

	*want_updated = not_updated;
	log_err("Inode #%llu (0x%llx): %s",
		(unsigned long long)ip->i_di.di_num.no_addr,
		(unsigned long long)ip->i_di.di_num.no_addr, emsg);
	if (block)
		log_err(" at block #%" PRIu64 " (0x%" PRIx64 ")",
			block, block);
	log_err(".\n");
	errors_found++;
	if ((errors_corrected += query(&opts, "Clear the bad EA? (y/n) "))) {
		if (block == 0)
			block = ip->i_di.di_eattr;
		gfs2_block_clear(sdp, bl, block, gfs2_eattr_block);
		if (!duplicate) {
			gfs2_block_clear(sdp, bl, block, gfs2_indir_blk);
			gfs2_block_set(sdp, bl, block, gfs2_block_free);
			gfs2_set_bitmap(sdp, block, GFS2_BLKST_FREE);
		}
		ip->i_di.di_flags &= ~GFS2_DIF_EA_INDIRECT;
		if (block == ip->i_di.di_eattr)
			ip->i_di.di_eattr = 0;
		bc->ea_count = 0;
		ip->i_di.di_blocks = 1 + bc->indir_count + bc->data_count;
		gfs2_dinode_out(&ip->i_di, ip->i_bh->b_data);
		*want_updated = updated;
		return 1;
	} else {
		log_err("The bad EA was not fixed.\n");
		bc->ea_count++;
		return 0;
	}
}

static int check_eattr_indir(struct gfs2_inode *ip, uint64_t indirect,
			     uint64_t parent, struct gfs2_buffer_head **bh,
			     enum update_flags *want_updated, void *private)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	int ret = 0;
	struct gfs2_block_query q = {0};
	struct block_count *bc = (struct block_count *) private;

	/* This inode contains an eattr - it may be invalid, but the
	 * eattr attributes points to a non-zero block */
	if(gfs2_check_range(sdp, indirect)) {
		/*log_warn("EA indirect block #%"PRIu64" is out of range.\n",
			indirect);
			gfs2_block_set(sdp, bl, parent, bad_block);*/
		/* Doesn't help to mark this here - this gets checked
		 * in pass1c */
		return 1;
	}
	if(gfs2_block_check(sdp, bl, indirect, &q)) {
		stack;
		return -1;
	}

	/* Special duplicate processing:  If we have an EA block,
	   check if it really is an EA.  If it is, let duplicate
	   handling sort it out.  If it isn't, clear it but don't
	   count it as a duplicate. */
	*bh = bread(&sdp->buf_list, indirect);
	if(gfs2_check_meta(*bh, GFS2_METATYPE_IN)) {
		if(q.block_type != gfs2_block_free) { /* Duplicate? */
			if (!clear_eas(ip, bc, indirect, 1, want_updated,
				       "Bad indirect EA duplicate found"))
				gfs2_block_set(sdp, bl, indirect,
					       gfs2_dup_block);
			return 1;
		}
		clear_eas(ip, bc, indirect, 0, want_updated,
			  "EA indirect block has incorrect type");
		return 1;
	}
	if(q.block_type != gfs2_block_free) { /* Duplicate? */
		log_err("Inode #%llu (0x%llx): Duplicate EA indirect "
			"block found at #%llu (0x%llx).\n",
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)indirect,
			(unsigned long long)indirect);
		gfs2_block_set(sdp, bl, indirect, gfs2_dup_block);
		bc->ea_count++;
		ret = 1;
	} else {
		log_debug("Setting #%" PRIu64 " (0x%" PRIx64
			  ") to indirect EA block\n", indirect, indirect);
		gfs2_block_set(sdp, bl, indirect, gfs2_indir_blk);
		bc->ea_count++;
	}
	return ret;
}

static int finish_eattr_indir(struct gfs2_inode *ip, int indir_ok,
			      enum update_flags *want_updated, void *private)
{
	if (indir_ok) {
		log_debug("Marking inode #%llu (0x%llx) with eattr block\n",
			  (unsigned long long)ip->i_di.di_num.no_addr,
			  (unsigned long long)ip->i_di.di_num.no_addr);
		/* Mark the inode as having an eattr in the block map
		   so pass1c can check it. */
		gfs2_block_mark(ip->i_sbd, bl, ip->i_di.di_num.no_addr,
				gfs2_eattr_block);
		return 0;
	}
	clear_eas(ip, (struct block_count *)private, 0, 0, want_updated,
		  "has unrecoverable indirect EA errors");
	return 0;
}

/**
 * check_extended_leaf_eattr
 * @ip
 * @el_blk: block number of the extended leaf
 *
 * An EA leaf block can contain EA's with pointers to blocks
 * where the data for that EA is kept.  Those blocks still
 * have the gfs2 meta header of type GFS2_METATYPE_EA
 *
 * Returns: 0 if correct[able], -1 if removal is needed
 */
static int check_extended_leaf_eattr(struct gfs2_inode *ip, uint64_t *data_ptr,
				     struct gfs2_buffer_head *leaf_bh,
				     struct gfs2_ea_header *ea_hdr,
				     struct gfs2_ea_header *ea_hdr_prev,
				     enum update_flags *want_updated,
				     void *private)
{
	struct gfs2_buffer_head *el_buf;
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_block_query q;
	uint64_t el_blk = be64_to_cpu(*data_ptr);
	struct block_count *bc = (struct block_count *) private;
	int ret = 0;

	if(gfs2_check_range(sdp, el_blk)){
		log_err("Inode #%llu (0x%llx): EA extended "
			"leaf block #%llu (0x%llx) is out of range.\n",
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)el_blk,
			(unsigned long long)el_blk);
		gfs2_block_set(sdp, bl, ip->i_di.di_eattr, gfs2_bad_block);
		return 1;
	}

	if(gfs2_block_check(sdp, bl, el_blk, &q)) {
		stack;
		return -1;
	}
	el_buf = bread(&sdp->buf_list, el_blk);

	/* Special duplicate processing:  If we have an EA block,
	   check if it really is an EA.  If it is, let duplicate
	   handling sort it out.  If it isn't, clear it but don't
	   count it as a duplicate. */
	if(gfs2_check_meta(el_buf, GFS2_METATYPE_ED)) {
		if(q.block_type != gfs2_block_free) /* Duplicate? */
			clear_eas(ip, bc, el_blk, 1, want_updated,
				  "has bad extended EA duplicate");
		else
			clear_eas(ip, bc, el_blk, 0, want_updated,
				  "EA extended leaf block has incorrect type");
		ret = 1;
	} else { /* If this looks like an EA */
		if(q.block_type != gfs2_block_free) { /* Duplicate? */
			log_debug("Duplicate block found at #%" PRIu64
				  " (0x%" PRIx64 ").\n",
				  el_blk, el_blk);
			gfs2_block_set(sdp, bl, el_blk, gfs2_dup_block);
			bc->ea_count++;
			ret = 1;
		} else {
			log_debug("Setting block #%" PRIu64
				  " (0x%" PRIx64 ") to eattr block\n",
				  el_blk, el_blk);
			gfs2_block_set(sdp, bl, el_blk, gfs2_meta_eattr);
			bc->ea_count++;
		}
	}

	brelse(el_buf, not_updated);
	return ret;
}

static int check_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
			    uint64_t parent, struct gfs2_buffer_head **bh,
			    enum update_flags *want_updated, void *private)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_buffer_head *leaf_bh = NULL;
	int ret = 0;
	struct gfs2_block_query q = {0};
	struct block_count *bc = (struct block_count *) private;

	/* This inode contains an eattr - it may be invalid, but the
	 * eattr attributes points to a non-zero block */
	if (parent != ip->i_di.di_num.no_addr) { /* if parent isn't the inode */
		log_debug("Setting %" PRIu64 " (0x%" PRIx64 ") to eattr block\n",
				  parent, parent);
		gfs2_block_set(sdp, bl, parent, gfs2_eattr_block);
	}
	if(gfs2_check_range(sdp, block)){
		log_warn("Inode #%llu (0x%llx): EA leaf block #%llu (0x%llx"
			 ") is out of range.\n",
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)block, (unsigned long long)block);
		gfs2_block_set(sdp, bl, ip->i_di.di_eattr, gfs2_bad_block);
		ret = 1;
	}
	else if(gfs2_block_check(sdp, bl, block, &q)) {
		stack;
		return -1;
	}
	else {
		/* Special duplicate processing:  If we have an EA block,
		   check if it really is an EA.  If it is, let duplicate
		   handling sort it out.  If it isn't, clear it but don't
		   count it as a duplicate. */
		leaf_bh = bread(&sdp->buf_list, block);
		if(gfs2_check_meta(leaf_bh, GFS2_METATYPE_EA)) {
			if(q.block_type != gfs2_block_free) { /* Duplicate? */
				clear_eas(ip, bc, block, 1, want_updated,
					  "Bad EA duplicate found");
			} else {
				clear_eas(ip, bc, block, 0, want_updated,
					  "EA leaf block has incorrect type");
			}
			ret = 1;
			brelse(leaf_bh, not_updated);
		} else { /* If this looks like an EA */
			if(q.block_type != gfs2_block_free) { /* Duplicate? */
				log_debug("Duplicate block found at #%" PRIu64
					  " (0x%" PRIx64 ").\n",
					  block, block);
				gfs2_block_set(sdp, bl, block, gfs2_dup_block);
				bc->ea_count++;
				ret = 1;
				brelse(leaf_bh, not_updated);
			} else {
				log_debug("Setting block #%" PRIu64
					  " (0x%" PRIx64 ") to eattr block\n",
					  block, block);
				gfs2_block_set(sdp, bl, block,
					       gfs2_meta_eattr);
				bc->ea_count++;
			}
		}
	}
	if (!ret)
		*bh = leaf_bh;

	return ret;
}

static int check_eattr_entries(struct gfs2_inode *ip,
			       struct gfs2_buffer_head *leaf_bh,
			       struct gfs2_ea_header *ea_hdr,
			       struct gfs2_ea_header *ea_hdr_prev,
			       void *private)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	char ea_name[256];

	if(!ea_hdr->ea_name_len){
		/* Skip this entry for now */
		return 1;
	}

	memset(ea_name, 0, sizeof(ea_name));
	strncpy(ea_name, (char *)ea_hdr + sizeof(struct gfs2_ea_header),
		ea_hdr->ea_name_len);

	if(!GFS2_EATYPE_VALID(ea_hdr->ea_type) &&
	   ((ea_hdr_prev) || (!ea_hdr_prev && ea_hdr->ea_type))){
		/* Skip invalid entry */
		return 1;
	}

	if(ea_hdr->ea_num_ptrs){
		uint32_t avail_size;
		int max_ptrs;

		avail_size = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_meta_header);
		max_ptrs = (be32_to_cpu(ea_hdr->ea_data_len)+avail_size-1)/avail_size;

		if(max_ptrs > ea_hdr->ea_num_ptrs) {
			return 1;
		} else {
			log_debug("  Pointers Required: %d\n  Pointers Reported: %d\n",
				  max_ptrs, ea_hdr->ea_num_ptrs);
		}
	}
	return 0;
}

int clear_metalist(struct gfs2_inode *ip, uint64_t block,
		   struct gfs2_buffer_head **bh, void *private)
{
	struct gfs2_block_query q = {0};

	*bh = NULL;

	if(gfs2_block_check(ip->i_sbd, bl, block, &q)) {
		stack;
		return -1;
	}
	if(!q.dup_block) {
		gfs2_block_set(ip->i_sbd, bl, block, gfs2_block_free);
		return 0;
	}
	return 0;
}

int clear_data(struct gfs2_inode *ip, uint64_t block, void *private)
{
	struct gfs2_block_query q = {0};

	if(gfs2_block_check(ip->i_sbd, bl, block, &q)) {
		stack;
		return -1;
	}
	if(!q.dup_block) {
		gfs2_block_set(ip->i_sbd, bl, block, gfs2_block_free);
		return 0;
	}
	return 0;

}

int clear_leaf(struct gfs2_inode *ip, uint64_t block,
	       struct gfs2_buffer_head *bh, void *private)
{
	struct gfs2_block_query q = {0};
	log_crit("Clearing leaf #%" PRIu64 " (0x%" PRIx64 ")\n", block, block);

	if(gfs2_block_check(ip->i_sbd, bl, block, &q)) {
		stack;
		return -1;
	}
	if(!q.dup_block) {
		log_crit("Setting leaf #%" PRIu64 " (0x%" PRIx64 ") invalid\n",
				 block, block);
		if(gfs2_block_set(ip->i_sbd, bl, block, gfs2_block_free)) {
			stack;
			return -1;
		}
		return 0;
	}
	return 0;

}

int add_to_dir_list(struct gfs2_sbd *sbp, uint64_t block)
{
	struct dir_info *di = NULL;
	struct dir_info *newdi;

	/* FIXME: This list should probably be a b-tree or
	 * something...but since most of the time we're going to be
	 * tacking the directory onto the end of the list, it doesn't
	 * matter too much */
	find_di(sbp, block, &di);
	if(di) {
		log_err("Attempting to add directory block #%" PRIu64
				" (0x%" PRIx64 ") which is already in list\n", block, block);
		return -1;
	}

	if(!(newdi = (struct dir_info *) malloc(sizeof(struct dir_info)))) {
		log_crit("Unable to allocate dir_info structure\n");
		return -1;
	}
	if(!memset(newdi, 0, sizeof(*newdi))) {
		log_crit("Error while zeroing dir_info structure\n");
		return -1;
	}

	newdi->dinode = block;
	dinode_hash_insert(dir_hash, block, newdi);
	return 0;
}

int handle_di(struct gfs2_sbd *sdp, struct gfs2_buffer_head *bh,
			  uint64_t block)
{
	struct gfs2_block_query q = {0};
	struct gfs2_inode *ip;
	int error;
	struct block_count bc = {0};
	struct metawalk_fxns invalidate_metatree = {0};
	enum update_flags f;

	f = not_updated;
	invalidate_metatree.check_metalist = clear_metalist;
	invalidate_metatree.check_data = clear_data;
	invalidate_metatree.check_leaf = clear_leaf;

	ip = fsck_inode_get(sdp, bh);
	if (ip->i_di.di_num.no_addr != block) {
		log_err("Inode #%llu (0x%llx): Bad inode address found: %llu "
			"(0x%llx)\n", (unsigned long long)block,
			(unsigned long long)block,
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)ip->i_di.di_num.no_addr);
		errors_found++;
		if((errors_corrected +=
		    query(&opts, "Fix address in inode at block #%"
			  PRIu64 " (0x%" PRIx64 ")? (y/n) ", block, block))) {
			ip->i_di.di_num.no_addr = ip->i_di.di_num.no_formal_ino = block;
			gfs2_dinode_out(&ip->i_di, ip->i_bh->b_data);
			f = updated;
		} else
			log_err("Address in inode at block #%" PRIu64
				 " (0x%" PRIx64 ") not fixed\n", block, block);
	}

	if(gfs2_block_check(sdp, bl, block, &q)) {
		stack;
		fsck_inode_put(ip, f);
		return -1;
	}
	if(q.block_type != gfs2_block_free) {
		log_debug("Found duplicate block at #%" PRIu64 " (0x%" PRIx64 ")\n",
				  block, block);
		if(gfs2_block_mark(sdp, bl, block, gfs2_dup_block)) {
			stack;
			fsck_inode_put(ip, f);
			return -1;
		}
		fsck_inode_put(ip, f);
		return 0;
	}

	switch(ip->i_di.di_mode & S_IFMT) {

	case S_IFDIR:
		log_debug("Setting %" PRIu64 " (0x%" PRIx64 ") to directory inode.\n",
				  block, block);
		if(gfs2_block_set(sdp, bl, block, gfs2_inode_dir)) {
			stack;
			fsck_inode_put(ip, f);
			return -1;
		}
		if(add_to_dir_list(sdp, block)) {
			stack;
			fsck_inode_put(ip, f);
			return -1;
		}
		break;
	case S_IFREG:
		log_debug("Setting %" PRIu64 " (0x%" PRIx64 ") to file inode.\n",
				  block, block);
		if(gfs2_block_set(sdp, bl, block, gfs2_inode_file)) {
			stack;
			fsck_inode_put(ip, f);
			return -1;
		}
		break;
	case S_IFLNK:
		log_debug("Setting %" PRIu64 " (0x%" PRIx64 ") to symlink inode.\n",
				  block, block);
		if(gfs2_block_set(sdp, bl, block, gfs2_inode_lnk)) {
			stack;
			fsck_inode_put(ip, f);
			return -1;
		}
		break;
	case S_IFBLK:
		log_debug("Setting %" PRIu64 " (0x%" PRIx64 ") to block dev inode.\n",
				  block, block);
		if(gfs2_block_set(sdp, bl, block, gfs2_inode_blk)) {
			stack;
			fsck_inode_put(ip, f);
			return -1;
		}
		break;
	case S_IFCHR:
		log_debug("Setting %" PRIu64 " (0x%" PRIx64 ") to char dev inode.\n",
				  block, block);
		if(gfs2_block_set(sdp, bl, block, gfs2_inode_chr)) {
			stack;
			fsck_inode_put(ip, f);
			return -1;
		}
		break;
	case S_IFIFO:
		log_debug("Setting %" PRIu64 " (0x%" PRIx64 ") to fifo inode.\n",
				  block, block);
		if(gfs2_block_set(sdp, bl, block, gfs2_inode_fifo)) {
			stack;
			fsck_inode_put(ip, f);
			return -1;
		}
		break;
	case S_IFSOCK:
		log_debug("Setting %" PRIu64 " (0x%" PRIx64 ") to socket inode.\n",
				  block, block);
		if(gfs2_block_set(sdp, bl, block, gfs2_inode_sock)) {
			stack;
			fsck_inode_put(ip, f);
			return -1;
		}
		break;
	default:
		log_debug("Setting %" PRIu64 " (0x%" PRIx64 ") to invalid.\n",
				  block, block);
		if(gfs2_block_set(sdp, bl, block, gfs2_meta_inval)) {
			stack;
			fsck_inode_put(ip, f);
			return -1;
		}
		gfs2_set_bitmap(sdp, block, GFS2_BLKST_FREE);
		fsck_inode_put(ip, f);
		return 0;
	}
	if(set_link_count(ip->i_sbd, ip->i_di.di_num.no_addr, ip->i_di.di_nlink)) {
		stack;
		fsck_inode_put(ip, f);
		return -1;
	}

	/* FIXME: fix height and depth here - wasn't implemented in
	 * old fsck either, so no biggy... */
	if (ip->i_di.di_height < compute_height(sdp, ip->i_di.di_size)){
		log_warn("Dinode #%llu (0x%llx) has bad height  "
			 "Found %u, Expected >= %u\n",
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 ip->i_di.di_height,
			 compute_height(sdp, ip->i_di.di_size));
			/* once implemented, remove continue statement */
		log_warn("Marking inode invalid\n");
		if(gfs2_block_set(sdp, bl, block, gfs2_meta_inval)) {
			stack;
			fsck_inode_put(ip, f);
			return -1;
		}
		gfs2_set_bitmap(sdp, block, GFS2_BLKST_FREE);
		fsck_inode_put(ip, f);
		return 0;
	}

	if (S_ISDIR(ip->i_di.di_mode) &&
	    (ip->i_di.di_flags & GFS2_DIF_EXHASH)) {
		if (((1 << ip->i_di.di_depth) * sizeof(uint64_t)) != ip->i_di.di_size){
			log_warn("Directory dinode #%llu (0x%llx"
				 ") has bad depth.  Found %u, Expected %u\n",
				 (unsigned long long)ip->i_di.di_num.no_addr,
				 (unsigned long long)ip->i_di.di_num.no_addr,
				 ip->i_di.di_depth,
				 (1 >> (ip->i_di.di_size/sizeof(uint64_t))));
			/* once implemented, remove continue statement */
			log_warn("Marking inode invalid\n");
			if(gfs2_block_set(sdp, bl, block, gfs2_meta_inval)) {
				stack;
				fsck_inode_put(ip, f);
				return -1;
			}
			gfs2_set_bitmap(sdp, block, GFS2_BLKST_FREE);
			fsck_inode_put(ip, f);
			return 0;
		}
	}

	pass1_fxns.private = &bc;

	error = check_metatree(ip, &pass1_fxns);
	if(error < 0) {
		fsck_inode_put(ip, f);
		return 0;
	}
	if(error > 0) {
		log_warn("Marking inode #%llu (0x%llx) invalid\n",
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr);
		/* FIXME: Must set all leaves invalid as well */
		check_metatree(ip, &invalidate_metatree);
		gfs2_block_set(sdp, bl, ip->i_di.di_num.no_addr,
			       gfs2_meta_inval);
		gfs2_set_bitmap(sdp, ip->i_di.di_num.no_addr, GFS2_BLKST_FREE);
		fsck_inode_put(ip, f);
		return 0;
	}

	check_inode_eattr(ip, &f, &pass1_fxns);

	if (ip->i_di.di_blocks != 
		(1 + bc.indir_count + bc.data_count + bc.ea_count)) {
		log_err("Inode #%llu (0x%llx): Ondisk block count (%llu"
			") does not match what fsck found (%llu)\n",
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)ip->i_di.di_blocks,
			(unsigned long long)1 + bc.indir_count +
			bc.data_count + bc.ea_count);
		errors_found++;
		if ((errors_corrected +=
		     query(&opts, "Fix ondisk block count? (y/n) "))) {
			ip->i_di.di_blocks = 1 + bc.indir_count + bc.data_count +
				bc.ea_count;
			gfs2_dinode_out(&ip->i_di, ip->i_bh->b_data);
			f = updated;
		} else
			log_err("Bad block count for #%llu (0x%llx"
				") not fixed\n",
				(unsigned long long)ip->i_di.di_num.no_addr,
				(unsigned long long)ip->i_di.di_num.no_addr);
	}

	fsck_inode_put(ip, f);
	return 0;
}

int scan_meta(struct gfs2_sbd *sdp, struct gfs2_buffer_head *bh,
			  uint64_t block)
{
	if (gfs2_check_meta(bh, 0)) {
		log_debug("Found invalid metadata at #%" PRIu64 " (0x%" PRIx64 ")\n",
				  block, block);
		if(gfs2_block_set(sdp, bl, block, gfs2_meta_inval)) {
			stack;
			return -1;
		}
		gfs2_set_bitmap(sdp, block, GFS2_BLKST_FREE);
		return 0;
	}

	log_debug("Checking metadata block #%" PRIu64 " (0x%" PRIx64 ")\n", block,
			  block);

	if (!gfs2_check_meta(bh, GFS2_METATYPE_DI)) {
		/* handle_di calls inode_get, then inode_put, which does brelse.   */
		/* In order to prevent brelse from getting the count off, hold it. */
		bhold(bh);
		if(handle_di(sdp, bh, block)) {
			stack;
			return -1;
		}
	}
	/* Ignore everything else - they should be hit by the handle_di step. */
	/* Don't check NONE either, because check_meta passes everything if   */
	/* GFS2_METATYPE_NONE is specified.                                   */
	/* Hopefully, other metadata types such as indirect blocks will be    */
	/* handled when the inode itself is processed, and if it's not, it    */
	/* should be caught in pass5.                                         */
	return 0;
}

/**
 * pass1 - walk through inodes and check inode state
 *
 * this walk can be done using root inode and depth first search,
 * watching for repeat inode numbers
 *
 * format & type
 * link count
 * duplicate blocks
 * bad blocks
 * inodes size
 * dir info
 */
int pass1(struct gfs2_sbd *sbp)
{
	struct gfs2_buffer_head *bh;
	osi_list_t *tmp;
	uint64_t block;
	struct rgrp_list *rgd;
	int first;
	uint64_t i;
	uint64_t blk_count;
	uint64_t offset;
	uint64_t rg_count = 0;

	/* FIXME: In the gfs fsck, we had to mark things like the
	 * journals and indices and such as 'other_meta' - in gfs2,
	 * the journals are files and are found in the normal file
	 * sweep - is there any metadata we need to mark here before
	 * the sweeps start that we won't find otherwise? */

	/* So, do we do a depth first search starting at the root
	 * inode, or use the rg bitmaps, or just read every fs block
	 * to find the inodes?  If we use the depth first search, why
	 * have pass3 at all - if we use the rg bitmaps, pass5 is at
	 * least partially invalidated - if we read every fs block,
	 * things will probably be intolerably slow.  The current fsck
	 * uses the rg bitmaps, so maybe that's the best way to start
	 * things - we can change the method later if necessary.
	 */

	for (tmp = sbp->rglist.next; tmp != &sbp->rglist;
	     tmp = tmp->next, rg_count++){
		log_info("Checking metadata in Resource Group #%" PRIu64 "\n",
				 rg_count);
		rgd = osi_list_entry(tmp, struct rgrp_list, list);
		if(gfs2_rgrp_read(sbp, rgd)){
			stack;
			return FSCK_ERROR;
		}
		log_debug("RG at %llu (0x%llx) is %u long\n",
			  (unsigned long long)rgd->ri.ri_addr,
			  (unsigned long long)rgd->ri.ri_addr,
			  rgd->ri.ri_length);
		for (i = 0; i < rgd->ri.ri_length; i++) {
			if(gfs2_block_set(sbp, bl, rgd->ri.ri_addr + i,
					  gfs2_meta_other)){
				stack;
				return FSCK_ERROR;
			}
		}

		offset = sizeof(struct gfs2_rgrp);
		blk_count = 1;
		first = 1;

		while (1) {
			/* "block" is relative to the entire file system */
			if (gfs2_next_rg_meta(rgd, &block, first))
				break;
			warm_fuzzy_stuff(block);
			if (fsck_abort) { /* if asked to abort */
				gfs2_rgrp_relse(rgd, not_updated);
				return FSCK_OK;
			}
			if (skip_this_pass) {
				printf("Skipping pass 1 is not a good idea.\n");
				skip_this_pass = FALSE;
				fflush(stdout);
			}
			bh = bread(&sbp->buf_list, block);

			if (scan_meta(sbp, bh, block)) {
				stack;
				brelse(bh, not_updated);
				gfs2_rgrp_relse(rgd, not_updated);
				return FSCK_ERROR;
			}
			brelse(bh, not_updated);
			first = 0;
		}
		gfs2_rgrp_relse(rgd, not_updated);
	}
	return FSCK_OK;
}
