/*
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 */

#include <linuxmt/types.h>
#include <linuxmt/sched.h>
#include <linuxmt/minix_fs.h>
#include <linuxmt/locks.h>
#include <linuxmt/kernel.h>
#include <linuxmt/mm.h>
#include <linuxmt/string.h>
#include <linuxmt/stat.h>
#include <linuxmt/debug.h>

#include <arch/system.h>
#include <arch/segment.h>
#include <arch/bitops.h>

/* Static functions in this file */

static unsigned short map_iblock(register struct inode *,block_t,block_t,int);
static unsigned short map_izone(register struct inode *,block_t,int);
static void minix_commit_super(register struct super_block *);
static struct buffer_head *minix_update_inode(register struct inode *);

/* Function definitions */

void minix_put_inode(register struct inode *inode)
{
    if (inode->i_nlink)
	return;
    inode->i_size = 0;
    minix_truncate(inode);
    minix_free_inode(inode);
}

static void minix_commit_super(register struct super_block *sb)
{
    mark_buffer_dirty(sb->u.minix_sb.s_sbh, 1);
    sb->s_dirt = 0;
}

void minix_write_super(register struct super_block *sb)
{
    if (!(sb->s_flags & MS_RDONLY)) {
	register struct minix_super_block *ms = sb->u.minix_sb.s_ms;

	if (ms->s_state & MINIX_VALID_FS)
	    ms->s_state &= ~MINIX_VALID_FS;
	minix_commit_super(sb);
    }
    sb->s_dirt = 0;
}

void minix_put_super(register struct super_block *sb)
{
    register struct minix_sb_info *ms = &sb->u.minix_sb;
    int i;

    lock_super(sb);
    if (!(sb->s_flags & MS_RDONLY)) {
	ms->s_ms->s_state = ms->s_mount_state;
	mark_buffer_dirty(ms->s_sbh, 1);
    }
    sb->s_dev = 0;
    for (i = 0; i < MINIX_I_MAP_SLOTS; i++)
	brelse(ms->s_imap[i]);
    for (i = 0; i < MINIX_Z_MAP_SLOTS; i++)
	brelse(ms->s_zmap[i]);
    brelse(ms->s_sbh);
    unlock_super(sb);
}

int minix_remount(register struct super_block *sb, int *flags, char *data)
{
    register struct minix_sb_info *ms = &sb->u.minix_sb;

    if ((*flags & MS_RDONLY) != (sb->s_flags & MS_RDONLY)) {
	if (*flags & MS_RDONLY) {

	    if (	  (ms->s_ms->s_state & MINIX_VALID_FS)
		      || !(ms->s_mount_state & MINIX_VALID_FS))
		return 0;
	    /* Mounting a rw partition read-only. */
	    ms->s_mount_state = sb->u.minix_sb.s_mount_state;
	    mark_buffer_dirty(ms->s_sbh, 1);
	    sb->s_dirt = 1;
	    minix_commit_super(sb);

	} else {

	    /* Mount a partition which is read-only, read-write. */
	    ms->s_mount_state = ms->s_ms->s_state;
	    ms->s_ms->s_state &= ~MINIX_VALID_FS;
	    mark_buffer_dirty(ms->s_sbh, 1);
	    sb->s_dirt = 1;

	}
	if (!(ms->s_mount_state & MINIX_VALID_FS))
	    printk("MINIX-fs warning: remounting unchecked fs, running fsck is recommended.\n");
	else if ((ms->s_mount_state & MINIX_ERROR_FS))
	    printk("MINIX-fs warning: remounting fs with errors, running fsck is recommended.\n");
    }
    return 0;
}

static struct super_operations minix_sops = {
    minix_read_inode,
#ifdef BLOAT_FS
    NULL,
#endif
    minix_write_inode,
    minix_put_inode,
    minix_put_super,
    minix_write_super,
#ifdef BLOAT_FS
    minix_statfs,
#endif
    minix_remount
};

struct super_block *minix_read_super(register struct super_block *s, char *data, int silent)
{
    struct buffer_head *bh;
    register struct minix_sb_info *msb;
    block_t block, i;
    kdev_t dev = s->s_dev;

    if (sizeof(struct minix_inode) != 32)
	panic("bad inode size");
    lock_super(s);
    if (!(bh = bread(dev, (block_t) 1))) {
	s->s_dev = 0;
	unlock_super(s);
	printk("minix: unable to read sb\n");
	return NULL;
    }
    map_buffer(bh);
    {
	/* Localise register variable */
	register struct minix_super_block *ms;

	msb = &s->u.minix_sb;
	msb->s_ms = ms = (struct minix_super_block *) bh->b_data;
	msb->s_sbh = bh;
	msb->s_mount_state = ms->s_state;
	msb->s_ninodes = ms->s_ninodes;
	msb->s_imap_blocks = ms->s_imap_blocks;
	msb->s_zmap_blocks = ms->s_zmap_blocks;
	msb->s_firstdatazone = ms->s_firstdatazone;
	msb->s_log_zone_size = ms->s_log_zone_size;
	msb->s_max_size = ms->s_max_size;

#ifdef BLOAT_FS
	s->s_magic = ms->s_magic;
#endif

	if (ms->s_magic == MINIX_SUPER_MAGIC) {
	    msb->s_version = MINIX_V1;
	    msb->s_nzones = s->u.minix_sb.s_ms->s_nzones;
	    msb->s_dirsize = 16;
	    msb->s_namelen = 14;
	} else {
	    s->s_dev = 0;
	    unlock_super(s);
	    unmap_brelse(bh);
	    if (silent) {
		debug1("VFS: dev %s is not minixfs.\n", kdevname(dev));
	    } else
		printk("VFS: dev %s is not minixfs.\n", kdevname(dev));
	    return NULL;
	}
    }
    for (i = 0; i < MINIX_I_MAP_SLOTS; i++)
	msb->s_imap[i] = NULL;
    for (i = 0; i < MINIX_Z_MAP_SLOTS; i++)
	msb->s_zmap[i] = NULL;
    block = 2;

    /*
     *      FIXME:: We cant keep these in memory on an 8086, need to change
     *      the code to fetch/release each time we get a block.
     */
    for (i = 0; i < msb->s_imap_blocks; i++)
	if ((msb->s_imap[i] = bread(dev, block)) != NULL)
	    block++;
	else
	    break;
    for (i = 0; i < msb->s_zmap_blocks; i++)
	if ((msb->s_zmap[i] = bread(dev, block)) != NULL)
	    block++;
	else
	    break;
    if (block != 2 + msb->s_imap_blocks + msb->s_zmap_blocks) {
	for (i = 0; i < MINIX_I_MAP_SLOTS; i++)
	    brelse(msb->s_imap[i]);
	for (i = 0; i < MINIX_Z_MAP_SLOTS; i++)
	    brelse(msb->s_zmap[i]);
	s->s_dev = 0;
	unlock_super(s);
	unmap_brelse(bh);
	printk("minix: bad superblock or bitmaps\n");
	return NULL;
    }
    (void) set_bit(0, msb->s_imap[0]->b_data);
    (void) set_bit(0, msb->s_zmap[0]->b_data);
    unlock_super(s);

    /* set up enough so that it can read an inode */
    s->s_dev = dev;
    s->s_op = &minix_sops;
    s->s_mounted = iget(s, (ino_t) MINIX_ROOT_INO);
    if (!s->s_mounted) {
	s->s_dev = 0;
	unmap_brelse(bh);
	printk("minix: get root inode failed\n");
	return NULL;
    }
    if (!(s->s_flags & MS_RDONLY)) {
	msb->s_ms->s_state &= ~MINIX_VALID_FS;
	mark_buffer_dirty(bh, 1);
	s->s_dirt = 1;
    }
    if (!(msb->s_mount_state & MINIX_VALID_FS))
	printk("MINIX-fs: mounting unchecked file system, running fsck is recommended.\n");
    else if (msb->s_mount_state & MINIX_ERROR_FS)
	printk("MINIX-fs: mounting file system with errors, running fsck is recommended.\n");
    unmap_buffer(bh);
    return s;
}

#ifdef BLOAT_FS

void minix_statfs(register struct super_block *sb, struct statfs *buf, size_t bufsiz)
{
    register struct minix_sb_info *msb = &sb->u.minix_sb;
    struct statfs tmp;

    tmp.f_type = sb->s_magic;
    tmp.f_bsize = BLOCK_SIZE;
    tmp->f_blocks = (msb->s_nzones - msb->s_firstdatazone)
				<< msb->s_log_zone_size;
    tmp.f_bfree = minix_count_free_blocks(sb);
    tmp.f_bavail = tmp.f_bfree;
    tmp.f_files = (long) msb->s_ninodes;
    tmp.f_ffree = minix_count_free_inodes(sb);
    tmp.f_namelen = msb->s_namelen;
    memcpy(buf, &tmp, bufsiz);
}

#endif

/*  Adapted from Linux 0.12's inode.c.  _bmap() is a big function, I know 
 *
 *  Rewritten 2001 by Alan Cox based on newer kernel code + my own plans
 */

static unsigned short map_izone(register struct inode *inode, block_t block, int create)
{
    register block_t *i_zone = inode->i_zone;

    if (create && !i_zone[block])
	if ((i_zone[block] = minix_new_block(inode->i_sb))) {
	    inode->i_ctime = CURRENT_TIME;
	    inode->i_dirt = 1;
	}
    return i_zone[block];
}

static unsigned short map_iblock(register struct inode *inode, block_t i, block_t block, int create)
{
    register struct buffer_head *bh = bread(inode->i_dev, i);

    if (!bh)
	return 0;
    map_buffer(bh);
    i = ((block_t *)(bh->b_data))[block];
    if (create && !i) {
	if ((i = minix_new_block(inode->i_sb))) {
	    ((block_t *) (bh->b_data))[block] = i;
	    bh->b_dirty = 1;
	}
    }
    unmap_brelse(bh);
    return i;
}

unsigned short minix_bmap(register struct inode *inode, block_t block,
			  int create)
{
    register block_t i;

#if 0
/*	I do not understand what this bit means, it cannot be this big, it is
 *	a short. If this was a long it would make sense. We need to check for
 *	overruns in the block num elsewhere.. FIXME
 */

    if (block > (7 + 512 + 512 * 512))
	panic("minix_bmap: block (%d) too big", block);
#endif

    if (block < 7)
	return map_izone(inode, block, create);
    block -= 7;
    if (block < 512) {
	i = map_izone(inode, 7, create);
	goto map1;
    }

    /*
     *		Double indirection country. Do two block remaps
     */

    block -= 512;

    i = map_izone(inode, 8, create);
    if (!i)
	return 0;

    /* Two layer indirection */
    i = map_iblock(inode, i, block >> 9, create);

  map1:
    /*		Do the final indirect block check and read
     */

    if (!i)
	return 0;

    /*		Ok now load the second indirect block
     */
    return map_iblock(inode, i, block & 511, create);
}

struct buffer_head *minix_getblk(register struct inode *inode, block_t block, int create)
{
    struct buffer_head *bh = NULL;
    block_t blknum = minix_bmap(inode, block, create);

    if (blknum)
	bh = getblk(inode->i_dev, blknum);

    return bh;
}

struct buffer_head *minix_bread(struct inode *inode, block_t block, int create)
{
    register struct buffer_head *bh = minix_getblk(inode, block, create);

    if (!bh)
	return NULL;
    return readbuf(bh);
}

/*
 *	Set the ops on a minix inode
 */

void minix_set_ops(struct inode *inode)
{
    if (S_ISREG(inode->i_mode))
	inode->i_op = &minix_file_inode_operations;
    else if (S_ISDIR(inode->i_mode))
	inode->i_op = &minix_dir_inode_operations;
    else if (S_ISLNK(inode->i_mode))
	inode->i_op = &minix_symlink_inode_operations;
    else if (S_ISCHR(inode->i_mode))
	inode->i_op = &chrdev_inode_operations;
    else if (S_ISBLK(inode->i_mode))
	inode->i_op = &blkdev_inode_operations;

#ifdef NOT_YET
    else if (S_ISFIFO(inode->i_mode))
	init_fifo(inode);
#endif

}

/*
 * The minix V1 function to read an inode.
 */

void minix_read_inode(register struct inode *inode)
{
    struct buffer_head *bh;
    struct minix_inode *raw_inode;
    block_t block;
    ino_t ino = inode->i_ino;

    inode->i_op = NULL;
    inode->i_mode = 0;
    {
	/* Isolate register variable */
	register struct super_block *isb = inode->i_sb;
	register struct minix_sb_info *msb = &isb->u.minix_sb;

	if (!ino || ino > msb->s_ninodes) {
	    printk("Bad inode number on dev %s: %d is out of range\n",
		   kdevname(inode->i_dev), ino);
	    return;
	}
	block = msb->s_imap_blocks + 2 + msb->s_zmap_blocks
				   + (ino - 1) / MINIX_INODES_PER_BLOCK;
    }
    if (!(bh = bread(inode->i_dev, (block_t) block))) {
	printk("Major problem: unable to read inode from dev %s\n",
	       kdevname(inode->i_dev));
	return;
    }
    map_buffer(bh);
    raw_inode = ((struct minix_inode *) bh->b_data) +
		(ino - 1) % MINIX_INODES_PER_BLOCK;
    memcpy(inode, raw_inode, sizeof(struct minix_inode));
    inode->i_ctime = inode->i_atime = inode->i_mtime;

#ifdef BLOAT_FS
    inode->i_blocks = inode->i_blksize = 0;
#endif

    if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
	inode->i_rdev = to_kdev_t(raw_inode->i_zone[0]);
    else
	for (block = 0; block < 9; block++)
	    inode->i_zone[block] = raw_inode->i_zone[block];
    unmap_brelse(bh);
    minix_set_ops(inode);
}

/*
 * The minix V1 function to synchronize an inode.
 */

static struct buffer_head *minix_update_inode(register struct inode *inode)
{
    register struct buffer_head *bh;
    register struct minix_sb_info *ms = &inode->i_sb->u.minix_sb;
    struct minix_inode *raw_inode;
    block_t block;
    ino_t ino = inode->i_ino;

    if (!ino || ino > ms->s_ninodes) {
	printk("Bad inode number on dev %s: %d is out of range\n",
	       kdevname(inode->i_dev), ino);
	inode->i_dirt = 0;
	return 0;
    }
    block = ms->s_imap_blocks + 2 + ms->s_zmap_blocks
				  + (ino - 1) / MINIX_INODES_PER_BLOCK;

    if (!(bh = bread(inode->i_dev, (block_t) block))) {
	printk("unable to read i-node block\n");
	inode->i_dirt = 0;
	return 0;
    }
    map_buffer(bh);
    raw_inode = ((struct minix_inode *) bh->b_data) +
	(ino - 1) % MINIX_INODES_PER_BLOCK;
    memcpy(raw_inode, inode, sizeof(struct minix_inode));
    if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
	raw_inode->i_zone[0] = kdev_t_to_nr(inode->i_rdev);
    inode->i_dirt = 0;
    mark_buffer_dirty(bh, 1);
    unmap_buffer(bh);
    return bh;

}

void minix_write_inode(register struct inode *inode)
{
    register struct buffer_head *bh = minix_update_inode(inode);

    brelse(bh);
}

#ifdef BLOAT_FS

int minix_sync_inode(register struct inode *inode)
{
    struct buffer_head *bh = minix_update_inode(inode);
    int err = 0;

    if (bh && buffer_dirty(bh)) {
	ll_rw_blk(WRITE, bh);
	wait_on_buffer(bh);
	if (!buffer_uptodate(bh)) {
	    printk("IO error syncing minix inode [%s:%08lx]\n",
		   kdevname(inode->i_dev), inode->i_ino);
	    err = -1;
	}
    } else if (!bh)
	err = -1;
    brelse(bh);
    return err;
}

#endif

struct file_system_type minix_fs_type = {
    minix_read_super,
    "minix"
#ifdef BLOAT_FS
	   , 1
#endif
};

int init_minix_fs(void)
{
#if 0
    register_filesystem(&minix_fs_type);
#endif
    return 1;
}
