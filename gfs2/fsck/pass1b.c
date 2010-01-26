#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libintl.h>
#define _(String) gettext(String)

#include "libgfs2.h"
#include "fsck.h"
#include "osi_list.h"
#include "util.h"
#include "metawalk.h"
#include "inode_hash.h"

struct fxn_info {
	uint64_t block;
	int found;
	int ea_only;    /* The only dups were found in EAs */
};

struct dup_handler {
	struct duptree *b;
	struct inode_with_dups *id;
	int ref_inode_count;
	int ref_count;
};

static inline void inc_if_found(uint64_t block, int not_ea, void *private) {
	struct fxn_info *fi = (struct fxn_info *) private;
	if(block == fi->block) {
		(fi->found)++;
		if(not_ea)
			fi->ea_only = 0;
	}
}

static int check_metalist(struct gfs2_inode *ip, uint64_t block,
			  struct gfs2_buffer_head **bh, void *private)
{
	inc_if_found(block, 1, private);

	return 0;
}

static int check_data(struct gfs2_inode *ip, uint64_t block, void *private)
{
	inc_if_found(block, 1, private);

	return 0;
}

static int check_eattr_indir(struct gfs2_inode *ip, uint64_t block,
			     uint64_t parent, struct gfs2_buffer_head **bh,
			     void *private)
{
	struct gfs2_sbd *sbp = ip->i_sbd;
	struct gfs2_buffer_head *indir_bh = NULL;

	inc_if_found(block, 0, private);
	indir_bh = bread(sbp, block);
	*bh = indir_bh;

	return 0;
}

static int check_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
			    uint64_t parent, struct gfs2_buffer_head **bh,
			    void *private)
{
	struct gfs2_sbd *sbp = ip->i_sbd;
	struct gfs2_buffer_head *leaf_bh = NULL;

	inc_if_found(block, 0, private);
	leaf_bh = bread(sbp, block);

	*bh = leaf_bh;
	return 0;
}

static int check_eattr_entry(struct gfs2_inode *ip,
			     struct gfs2_buffer_head *leaf_bh,
			     struct gfs2_ea_header *ea_hdr,
			     struct gfs2_ea_header *ea_hdr_prev, void *private)
{
	return 0;
}

static int check_eattr_extentry(struct gfs2_inode *ip, uint64_t *ea_data_ptr,
				struct gfs2_buffer_head *leaf_bh,
				struct gfs2_ea_header *ea_hdr,
				struct gfs2_ea_header *ea_hdr_prev,
				void *private)
{
	uint64_t block = be64_to_cpu(*ea_data_ptr);

	inc_if_found(block, 0, private);

	return 0;
}

static int find_dentry(struct gfs2_inode *ip, struct gfs2_dirent *de,
		       struct gfs2_dirent *prev,
		       struct gfs2_buffer_head *bh, char *filename,
		       uint16_t *count, void *priv)
{
	struct osi_node *n;
	osi_list_t *tmp2;
	struct duptree *b;
	struct inode_with_dups *id;
	struct gfs2_leaf leaf;

	for (n = osi_first(&dup_blocks); n; n = osi_next(n)) {
		b = (struct duptree *)n;
		osi_list_foreach(tmp2, &b->ref_inode_list) {
			id = osi_list_entry(tmp2, struct inode_with_dups,
					    list);
			if(id->name)
				/* We can only have one parent of
				 * inodes that contain duplicate
				 * blocks... */
				continue;
			if(id->block_no == de->de_inum.no_addr) {
				id->name = strdup(filename);
				id->parent = ip->i_di.di_num.no_addr;
				log_debug( _("Duplicate block %llu (0x%llx"
					  ") is in file or directory %llu"
					  " (0x%llx) named %s\n"),
					  (unsigned long long)id->block_no,
					  (unsigned long long)id->block_no,
					  (unsigned long long)
					  ip->i_di.di_num.no_addr,
					  (unsigned long long)
					  ip->i_di.di_num.no_addr,
					  filename);
				/* If there are duplicates of
				 * duplicates, I guess we'll miss them
				 * here */
				break;
			}
		}
	}
	/* Return the number of leaf entries so metawalk doesn't flag this
	   leaf as having none. */
	gfs2_leaf_in(&leaf, bh);
	*count = leaf.lf_entries;
	return 0;
}

static int clear_dup_metalist(struct gfs2_inode *ip, uint64_t block,
			      struct gfs2_buffer_head **bh, void *private)
{
	struct dup_handler *dh = (struct dup_handler *) private;

	if(dh->ref_count == 1)
		return 1;
	if(block == dh->b->block) {
		log_err( _("Found duplicate reference in inode \"%s\" at "
			   "block #%llu (0x%llx) to block #%llu (0x%llx)\n"),
			 dh->id->name ? dh->id->name : _("unknown name"),
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)block, (unsigned long long)block);
		log_err( _("Inode %s is in directory %"PRIu64" (0x%" PRIx64 ")\n"),
			 dh->id->name ? dh->id->name : "", dh->id->parent,
			 dh->id->parent);
		/* Setting the block to invalid means the inode is
		 * cleared in pass2 */
		fsck_blockmap_set(ip, ip->i_di.di_num.no_addr,
				  _("inode with duplicate"), gfs2_meta_inval);
	}
	return 0;
}

static int clear_dup_data(struct gfs2_inode *ip, uint64_t block, void *private)
{
	return clear_dup_metalist(ip, block, NULL, private);
}

static int clear_dup_eattr_indir(struct gfs2_inode *ip, uint64_t block,
				 uint64_t parent, struct gfs2_buffer_head **bh,
				 void *private)
{
	struct dup_handler *dh = (struct dup_handler *) private;
	/* Can't use fxns from eattr.c since we need to check the ref
	 * count */
	*bh = NULL;
	if(dh->ref_count == 1)
		return 1;
	if(block == dh->b->block) {
		log_err( _("Found dup in inode \"%s\" with address #%llu"
			" (0x%llx) with block #%llu (0x%llx)\n"),
			dh->id->name ? dh->id->name : _("unknown name"),
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)block,
			(unsigned long long)block);
		log_err( _("Inode %s is in directory %" PRIu64 " (0x%" PRIx64 ")\n"),
				dh->id->name ? dh->id->name : "",
				dh->id->parent, dh->id->parent);
		fsck_blockmap_set(ip, ip->i_di.di_eattr,
				  _("indirect eattr"), gfs2_meta_inval);
	}

	return 0;
}

static int clear_dup_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
				uint64_t parent, struct gfs2_buffer_head **bh,
				void *private)
{
	struct dup_handler *dh = (struct dup_handler *) private;

	if(dh->ref_count == 1)
		return 1;
	if(block == dh->b->block) {
		log_err( _("Found dup in inode \"%s\" with address #%llu"
			" (0x%llx) with block #%llu (0x%llx)\n"),
			dh->id->name ? dh->id->name : _("unknown name"),
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)block,
			(unsigned long long)block);
		log_err( _("Inode %s is in directory %" PRIu64 " (0x%" PRIx64 ")\n"),
				dh->id->name ? dh->id->name : "",
				dh->id->parent, dh->id->parent);
		/* mark the main eattr block invalid */
		fsck_blockmap_set(ip, ip->i_di.di_eattr,
				  _("indirect eattr leaf"), gfs2_meta_inval);
	}

	return 0;
}

static int clear_eattr_entry (struct gfs2_inode *ip,
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
		max_ptrs = (be32_to_cpu(ea_hdr->ea_data_len) + avail_size - 1) /
			avail_size;

		if(max_ptrs > ea_hdr->ea_num_ptrs)
			return 1;
		else {
			log_debug( _("  Pointers Required: %d\n  Pointers Reported: %d\n"),
					  max_ptrs, ea_hdr->ea_num_ptrs);
		}
	}
	return 0;
}

static int clear_eattr_extentry(struct gfs2_inode *ip, uint64_t *ea_data_ptr,
				struct gfs2_buffer_head *leaf_bh,
				struct gfs2_ea_header *ea_hdr,
				struct gfs2_ea_header *ea_hdr_prev,
				void *private)
{
	uint64_t block = be64_to_cpu(*ea_data_ptr);
	struct dup_handler *dh = (struct dup_handler *) private;

	if(dh->ref_count == 1)
		return 1;
	if(block == dh->b->block) {
		log_err( _("Found dup in inode \"%s\" with address #%llu"
			" (0x%llx) with block #%llu (0x%llx)\n"),
			dh->id->name ? dh->id->name : _("unknown name"),
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)block, (unsigned long long)block);
		log_err( _("Inode %s is in directory %" PRIu64 " (0x%" PRIx64 ")\n"),
				dh->id->name ? dh->id->name : "",
				dh->id->parent, dh->id->parent);
		/* mark the main eattr block invalid */
		fsck_blockmap_set(ip, ip->i_di.di_eattr,
				  _("extended eattr leaf"), gfs2_meta_inval);
	}

	return 0;

}

/* Finds all references to duplicate blocks in the metadata */
static int find_block_ref(struct gfs2_sbd *sbp, uint64_t inode, struct duptree *b)
{
	struct gfs2_inode *ip;
	struct fxn_info myfi = {b->block, 0, 1};
	struct inode_with_dups *id = NULL;
	struct metawalk_fxns find_refs = {
		.private = (void*) &myfi,
		.check_leaf = NULL,
		.check_metalist = check_metalist,
		.check_data = check_data,
		.check_eattr_indir = check_eattr_indir,
		.check_eattr_leaf = check_eattr_leaf,
		.check_dentry = NULL,
		.check_eattr_entry = check_eattr_entry,
		.check_eattr_extentry = check_eattr_extentry,
	};

	ip = fsck_load_inode(sbp, inode); /* bread, inode_get */
	log_debug( _("Checking inode %" PRIu64 " (0x%" PRIx64 ")'s "
		     "metatree for references to block %" PRIu64 " (0x%" PRIx64
		     ")\n"), inode, inode, b->block, b->block);
	if(check_metatree(ip, &find_refs)) {
		stack;
		fsck_inode_put(&ip); /* out, brelse, free */
		return -1;
	}
	log_debug( _("Done checking metatree\n"));
	/* Check for ea references in the inode */
	if(check_inode_eattr(ip, &find_refs) < 0){
		stack;
		fsck_inode_put(&ip); /* out, brelse, free */
		return -1;
	}
	if (myfi.found) {
		if(!(id = malloc(sizeof(*id)))) {
			log_crit( _("Unable to allocate inode_with_dups structure\n"));
			return -1;
		}
		if(!(memset(id, 0, sizeof(*id)))) {
			log_crit( _("Unable to zero inode_with_dups structure\n"));
			return -1;
		}
		log_debug( _("Found %d entries with block %" PRIu64
				  " (0x%" PRIx64 ") in inode #%" PRIu64 " (0x%" PRIx64 ")\n"),
				  myfi.found, b->block, b->block, inode, inode);
		id->dup_count = myfi.found;
		id->block_no = inode;
		id->ea_only = myfi.ea_only;
		osi_list_add_prev(&id->list, &b->ref_inode_list);
	}
	fsck_inode_put(&ip); /* out, brelse, free */
	return 0;
}

static int handle_dup_blk(struct gfs2_sbd *sbp, struct duptree *b)
{
	osi_list_t *tmp;
	struct inode_with_dups *id;
	struct metawalk_fxns clear_dup_fxns = {
		.private = NULL,
		.check_leaf = NULL,
		.check_metalist = clear_dup_metalist,
		.check_data = clear_dup_data,
		.check_eattr_indir = clear_dup_eattr_indir,
		.check_eattr_leaf = clear_dup_eattr_leaf,
		.check_dentry = NULL,
		.check_eattr_entry = clear_eattr_entry,
		.check_eattr_extentry = clear_eattr_extentry,
	};
	struct gfs2_inode *ip;
	struct dup_handler dh = {0};

	osi_list_foreach(tmp, &b->ref_inode_list) {
		id = osi_list_entry(tmp, struct inode_with_dups, list);
		dh.ref_inode_count++;
		dh.ref_count += id->dup_count;
	}
	/* A single reference to the block implies a possible situation where
	   a data pointer points to a metadata block.  In other words, the
	   duplicate reference in the file system is (1) Metadata block X and
	   (2) A dinode reference such as a data pointer pointing to block X.
	   We can't really check for that in pass1 because user data might
	   just _look_ like metadata by coincidence, and at the time we're
	   checking, we might not have processed the referenced block.
	   Here in pass1b we're sure. */
	if (dh.ref_count == 1) {
		struct gfs2_buffer_head *bh;
		uint32_t cmagic;

		bh = bread(sbp, b->block);
		cmagic = ((struct gfs2_meta_header *)(bh->b_data))->mh_magic;
		brelse(bh);
		if (be32_to_cpu(cmagic) == GFS2_MAGIC) {
			tmp = b->ref_inode_list.next;
			id = osi_list_entry(tmp, struct inode_with_dups, list);
			log_warn( _("Inode %s (%lld/0x%llx) has a reference to"
				    " data block %llu (0x%llx), "
				    "but the block is really metadata.\n"),
				  id->name, (unsigned long long)id->block_no,
				  (unsigned long long)id->block_no,
				  (unsigned long long)b->block,
				  (unsigned long long)b->block);
			if (query( _("Clear the inode? (y/n) "))) {
				struct inode_info *ii;

				log_warn( _("Clearing inode %lld (0x%llx)...\n"),
					 (unsigned long long)id->block_no,
					 (unsigned long long)id->block_no);
				ip = fsck_load_inode(sbp, id->block_no);
				ii = inodetree_find(ip->i_di.di_num.no_addr);
				if (ii)
					inodetree_delete(ii);
				/* Setting the block to invalid means the inode
				   is cleared in pass2 */
				fsck_blockmap_set(ip, ip->i_di.di_num.no_addr,
						 _("inode with bad duplicate"),
						 gfs2_meta_inval);
				fsck_inode_put(&ip);
			} else {
				log_warn( _("The bad inode was not cleared."));
			}
			return 0;
		}
	}

	log_notice( _("Block %llu (0x%llx) has %d inodes referencing it"
		   " for a total of %d duplicate references\n"),
		   (unsigned long long)b->block, (unsigned long long)b->block,
		   dh.ref_inode_count, dh.ref_count);

	osi_list_foreach(tmp, &b->ref_inode_list) {
		id = osi_list_entry(tmp, struct inode_with_dups, list);
		log_warn( _("Inode %s (%lld/0x%llx) has %d reference(s) to "
			    "block %llu (0x%llx)\n"), id->name,
			  (unsigned long long)id->block_no,
			  (unsigned long long)id->block_no,
			  id->dup_count, (unsigned long long)b->block,
			  (unsigned long long)b->block);
	}
	osi_list_foreach(tmp, &b->ref_inode_list) {
		id = osi_list_entry(tmp, struct inode_with_dups, list);
		if (!(query( _("Okay to clear inode %lld (0x%llx)? (y/n) "),
			     (unsigned long long)id->block_no,
			     (unsigned long long)id->block_no))) {
			log_warn( _("The bad inode was not cleared...\n"));
			continue;
		}
		log_warn( _("Clearing inode %lld (0x%llx)...\n"),
			  (unsigned long long)id->block_no,
			  (unsigned long long)id->block_no);
		ip = fsck_load_inode(sbp, id->block_no);
		dh.b = b;
		dh.id = id;
		clear_dup_fxns.private = (void *) &dh;
		/* Clear the EAs for the inode first */
		check_inode_eattr(ip, &clear_dup_fxns);
		/* If the dup wasn't only in the EA, clear the inode */
		if(!id->ea_only)
			check_metatree(ip, &clear_dup_fxns);

		fsck_blockmap_set(ip, ip->i_di.di_num.no_addr,
				  _("inode with duplicate"), gfs2_meta_inval);
		fsck_inode_put(&ip); /* out, brelse, free */
		dh.ref_inode_count--;
		if(dh.ref_inode_count == 1)
			break;
		/* Inode is marked invalid and is removed in pass2 */
		/* FIXME: other option should be to duplicate the
		 * block for each duplicate and point the metadata at
		 * the cloned blocks */
	}
	return 0;

}

/* Pass 1b handles finding the previous inode for a duplicate block
 * When found, store the inodes pointing to the duplicate block for
 * use in pass2 */
int pass1b(struct gfs2_sbd *sbp)
{
	struct duptree *b;
	uint64_t i;
	uint8_t q;
	struct osi_node *n;
	struct metawalk_fxns find_dirents = {0};
	int rc = FSCK_OK;
	find_dirents.check_dentry = &find_dentry;

	log_info( _("Looking for duplicate blocks...\n"));

	/* If there were no dups in the bitmap, we don't need to do anymore */
	if (dup_blocks.osi_node == NULL) {
		log_info( _("No duplicate blocks found\n"));
		return FSCK_OK;
	}

	/* Rescan the fs looking for pointers to blocks that are in
	 * the duplicate block map */
	log_info( _("Scanning filesystem for inodes containing duplicate blocks...\n"));
	log_debug( _("Filesystem has %"PRIu64" (0x%" PRIx64 ") blocks total\n"),
			  last_fs_block, last_fs_block);
	for(i = 0; i < last_fs_block; i += 1) {
		warm_fuzzy_stuff(i);
		if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
			goto out;
		log_debug( _("Scanning block %" PRIu64 " (0x%" PRIx64 ") for inodes\n"),
				  i, i);
		q = block_type(i);
		if((q == gfs2_inode_dir) ||
		   (q == gfs2_inode_file) ||
		   (q == gfs2_inode_lnk) ||
		   (q == gfs2_inode_blk) ||
		   (q == gfs2_inode_chr) ||
		   (q == gfs2_inode_fifo) ||
		   (q == gfs2_inode_sock)) {
			for (n = osi_first(&dup_blocks); n; n = osi_next(n)) {
				b = (struct duptree *)n;
				if(find_block_ref(sbp, i, b)) {
					stack;
					rc = FSCK_ERROR;
					goto out;
				}
			}
		}
		if(q == gfs2_inode_dir) {
			check_dir(sbp, i, &find_dirents);
		}
	}

	/* Fix dups here - it's going to slow things down a lot to fix
	 * it later */
	log_info( _("Handling duplicate blocks\n"));
out:
        for (n = osi_first(&dup_blocks); n; n = osi_next(n)) {
                b = (struct duptree *)n;
		if (!skip_this_pass && !rc) /* no error & not asked to skip the rest */
			handle_dup_blk(sbp, b);
		/* Do not attempt to free the dup_blocks list or its parts
		   here because any func that calls check_metatree needs
		   to check duplicate status based on this linked list.
		   This is especially true for pass2 where it may delete "bad"
		   inodes, and we can't delete an inode's indirect block if
		   it was a duplicate (therefore in use by another dinode). */
	}
	return rc;
}
