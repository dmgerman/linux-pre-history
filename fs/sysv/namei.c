/*
 *  linux/fs/sysv/namei.c
 *
 *  minix/namei.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/namei.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/namei.c
 *  Copyright (C) 1993  Bruno Haible
 */
/*
  7 Dec 1997 - updated to use dentries by Krzysztof G. Baranowski
	       <kgb@manjak.knm.org.pl>
 */
	 

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysv_fs.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>

/* compare strings: name[0..len-1] (not zero-terminated) and
 * buffer[0..] (filled with zeroes up to buffer[0..maxlen-1])
 */
static inline int namecompare(int len, int maxlen,
	const char * name, const char * buffer)
{
	if (len > maxlen)
		return 0;
	if (len < maxlen && buffer[len])
		return 0;
	return !memcmp(name, buffer, len);
}

/*
 * ok, we cannot use strncmp, as the name is not in our data space. [Now it is!]
 * Thus we'll have to use sysv_match. No big problem. Match also makes
 * some sanity tests.
 *
 * NOTE! unlike strncmp, sysv_match returns 1 for success, 0 for failure.
 */
static int sysv_match(int len, const char * name, struct sysv_dir_entry * de)
{
	if (!de->inode || len > SYSV_NAMELEN)
		return 0;
	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
	if (!len && (de->name[0]=='.') && (de->name[1]=='\0'))
		return 1;
	return namecompare(len,SYSV_NAMELEN,name,de->name);
}

/*
 *	sysv_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 */
static struct buffer_head * sysv_find_entry(struct inode * dir,
	const char * name, int namelen, struct sysv_dir_entry ** res_dir)
{
	struct super_block * sb;
	unsigned long pos, block, offset; /* pos = block * block_size + offset */
	struct buffer_head * bh;

	*res_dir = NULL;
	if (!dir)
		return NULL;
	sb = dir->i_sb;
	if (namelen > SYSV_NAMELEN)
		if (sb->sv_truncate)
			namelen = SYSV_NAMELEN;
		else
			return NULL;
	bh = NULL;
	pos = block = offset = 0;
	while (pos < dir->i_size) {
		if (!bh) {
			bh = sysv_file_bread(dir,block,0);
			if (!bh) {
				/* offset = 0; */ block++;
				pos += sb->sv_block_size;
				continue;
			}
		}
		if (sysv_match(namelen, name,
			       *res_dir = (struct sysv_dir_entry *) (bh->b_data + offset) ))
			return bh;
		pos += SYSV_DIRSIZE;
		offset += SYSV_DIRSIZE;
		if (offset < sb->sv_block_size)
			continue;
		brelse(bh);
		bh = NULL;
		offset = 0; block++;
	}
	brelse(bh);
	*res_dir = NULL;
	return NULL;
}

int sysv_lookup(struct inode * dir, struct dentry * dentry)
{
	int ino;
	struct inode * inode = NULL;
	struct sysv_dir_entry * de;
	struct buffer_head * bh;

	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		return -ENOENT;
	}
	if (!(bh = sysv_find_entry(dir, dentry->d_name.name, 
				   dentry->d_name.len, &de))) {
		return -ENOENT;
	}
	ino = de->inode;
	brelse(bh);
	inode = iget(dir->i_sb,ino);
	
	if (!inode) 
		return -EACCES;
	d_add(dentry, inode);
	return 0;
}

/*
 *	sysv_add_entry()
 *
 * adds a file entry to the specified directory, returning a possible
 * error value if it fails.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
static int sysv_add_entry(struct inode * dir,
	const char * name, int namelen,
	struct buffer_head ** res_buf,
	struct sysv_dir_entry ** res_dir)
{
	struct super_block * sb;
	int i;
	unsigned long pos, block, offset; /* pos = block * block_size + offset */
	struct buffer_head * bh;
	struct sysv_dir_entry * de;

	*res_buf = NULL;
	*res_dir = NULL;
	if (!dir)
		return -ENOENT;
	sb = dir->i_sb;
	if (namelen > SYSV_NAMELEN)
		if (sb->sv_truncate)
			namelen = SYSV_NAMELEN;
		else
			return -ENAMETOOLONG;
	if (!namelen)
		return -ENOENT;
	bh = NULL;
	pos = block = offset = 0;
	while (1) {
		if (!bh) {
			bh = sysv_file_bread(dir,block,1);
			if (!bh)
				return -ENOSPC;
		}
		de = (struct sysv_dir_entry *) (bh->b_data + offset);
		pos += SYSV_DIRSIZE;
		offset += SYSV_DIRSIZE;
		if (pos > dir->i_size) {
			de->inode = 0;
			dir->i_size = pos;
			mark_inode_dirty(dir);
		}
		if (de->inode) {
			if (namecompare(namelen, SYSV_NAMELEN, name, de->name)) {
				brelse(bh);
				return -EEXIST;
			}
		} else {
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
			mark_inode_dirty(dir);
			for (i = 0; i < SYSV_NAMELEN ; i++)
				de->name[i] = (i < namelen) ? name[i] : 0;
			mark_buffer_dirty(bh, 1);
			*res_dir = de;
			break;
		}
		if (offset < sb->sv_block_size)
			continue;
		brelse(bh);
		bh = NULL;
		offset = 0; block++;
	}
	*res_buf = bh;
	return 0;
}

int sysv_create(struct inode * dir, struct dentry * dentry, int mode)
{
	int error;
	struct inode * inode;
	struct buffer_head * bh;
	struct sysv_dir_entry * de;

	if (!dir)
		return -ENOENT;
	inode = sysv_new_inode(dir);
	if (!inode) 
		return -ENOSPC;
	inode->i_op = &sysv_file_inode_operations;
	inode->i_mode = mode;
	mark_inode_dirty(inode);
	error = sysv_add_entry(dir,dentry->d_name.name,
			       dentry->d_name.len, &bh, &de);
	if (error) {
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput(inode);
		return error;
	}
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	d_instantiate(dentry, inode);
	return 0;
}

int sysv_mknod(struct inode * dir, struct dentry * dentry, int mode, int rdev)
{
	int error;
	struct inode * inode;
	struct buffer_head * bh;
	struct sysv_dir_entry * de;

	if (!dir)
		return -ENOENT;
	bh = sysv_find_entry(dir,dentry->d_name.name,
			     dentry->d_name.len,&de);
	if (bh) {
		brelse(bh);
		return -EEXIST;
	}
	inode = sysv_new_inode(dir);
	if (!inode)
		return -ENOSPC;
	inode->i_uid = current->fsuid;
	inode->i_mode = mode;
	inode->i_op = NULL;
	if (S_ISREG(inode->i_mode))
		inode->i_op = &sysv_file_inode_operations;
	else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &sysv_dir_inode_operations;
		if (dir->i_mode & S_ISGID)
			inode->i_mode |= S_ISGID;
	}
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &sysv_symlink_inode_operations;
	else if (S_ISCHR(inode->i_mode))
		inode->i_op = &chrdev_inode_operations;
	else if (S_ISBLK(inode->i_mode))
		inode->i_op = &blkdev_inode_operations;
	else if (S_ISFIFO(inode->i_mode))
		init_fifo(inode);
	if (S_ISBLK(mode) || S_ISCHR(mode))
		inode->i_rdev = to_kdev_t(rdev);
	mark_inode_dirty(inode);
	error = sysv_add_entry(dir, dentry->d_name.name, dentry->d_name.len, &bh, &de);
	if (error) {
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput(inode);
		return error;
	}
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	d_instantiate(dentry, inode);
	return 0;
}

int sysv_mkdir(struct inode * dir, struct dentry *dentry, int mode)
{
	int error;
	struct inode * inode;
	struct buffer_head * bh, *dir_block;
	struct sysv_dir_entry * de;

	if (!dir)
		return -EINVAL;
	bh = sysv_find_entry(dir, dentry->d_name.name,
                              dentry->d_name.len, &de);
	if (bh) {
		brelse(bh);
		return -EEXIST;
	}
	if (dir->i_nlink >= dir->i_sb->sv_link_max) {
		return -EMLINK;
	}
	inode = sysv_new_inode(dir);
	if (!inode)
		return -ENOSPC;
	inode->i_op = &sysv_dir_inode_operations;
	inode->i_size = 2 * SYSV_DIRSIZE;
	dir_block = sysv_file_bread(inode,0,1);
	if (!dir_block) {
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput(inode);
		return -ENOSPC;
	}
	de = (struct sysv_dir_entry *) (dir_block->b_data + 0*SYSV_DIRSIZE);
	de->inode = inode->i_ino;
	strcpy(de->name,"."); /* rest of de->name is zero, see sysv_new_block */
	de = (struct sysv_dir_entry *) (dir_block->b_data + 1*SYSV_DIRSIZE);
	de->inode = dir->i_ino;
	strcpy(de->name,".."); /* rest of de->name is zero, see sysv_new_block */
	inode->i_nlink = 2;
	mark_buffer_dirty(dir_block, 1);
	brelse(dir_block);
	inode->i_mode = S_IFDIR | (mode & 0777 & ~current->fs->umask);
	if (dir->i_mode & S_ISGID)
		inode->i_mode |= S_ISGID;
	mark_inode_dirty(inode);
	error = sysv_add_entry(dir, dentry->d_name.name,
                               dentry->d_name.len, &bh, &de);
 	if (error) {
		inode->i_nlink=0;
		iput(inode);
		return error;
	}
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh, 1);
	dir->i_nlink++;
	mark_inode_dirty(dir);
	brelse(bh);
        d_instantiate(dentry, inode);
 	return 0;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
static int empty_dir(struct inode * inode)
{
	struct super_block * sb;
	unsigned long pos, block, offset; /* pos = block * block_size + offset */
	struct buffer_head * bh;
	struct sysv_dir_entry * de;

	if (!inode)
		return 1;
	block = 0;
	bh = NULL;
	pos = offset = 2*SYSV_DIRSIZE;
	if (inode->i_size % SYSV_DIRSIZE)
		goto bad_dir;
	if (inode->i_size < pos)
		goto bad_dir;
	bh = sysv_file_bread(inode,0,0);
	if (!bh)
		goto bad_dir;
	de = (struct sysv_dir_entry *) (bh->b_data + 0*SYSV_DIRSIZE);
	if (!de->inode || strcmp(de->name,"."))
		goto bad_dir;
	de = (struct sysv_dir_entry *) (bh->b_data + 1*SYSV_DIRSIZE);
	if (!de->inode || strcmp(de->name,".."))
		goto bad_dir;
	sb = inode->i_sb;
	while (pos < inode->i_size) {
		if (!bh) {
			bh = sysv_file_bread(inode,block,0);
			if (!bh) {
				/* offset = 0; */ block++;
				pos += sb->sv_block_size;
				continue;
			}
		}
		de = (struct sysv_dir_entry *) (bh->b_data + offset);
		pos += SYSV_DIRSIZE;
		offset += SYSV_DIRSIZE;
		if (de->inode) {
			brelse(bh);
			return 0;
		}
		if (offset < sb->sv_block_size)
			continue;
		brelse(bh);
		bh = NULL;
		offset = 0; block++;
	}
	brelse(bh);
	return 1;
bad_dir:
	brelse(bh);
	printk("Bad directory on device %s\n",
	       kdevname(inode->i_dev));
	return 1;
}

int sysv_rmdir(struct inode * dir, struct dentry * dentry)
{
	int retval;
	struct inode * inode;
	struct buffer_head * bh;
	struct sysv_dir_entry * de;

	inode = NULL;
	bh = sysv_find_entry(dir, dentry->d_name.name,
                             dentry->d_name.len, &de);
	retval = -ENOENT;
	if (!bh)
		goto end_rmdir;
	retval = -EPERM;
	inode = dentry->d_inode;

        if ((dir->i_mode & S_ISVTX) && !fsuser() &&
            current->fsuid != inode->i_uid &&
            current->fsuid != dir->i_uid)
		goto end_rmdir;
	if (inode->i_dev != dir->i_dev)
		goto end_rmdir;
	if (inode == dir)	/* we may not delete ".", but "../dir" is ok */
		goto end_rmdir;
	if (!S_ISDIR(inode->i_mode)) {
		retval = -ENOTDIR;
		goto end_rmdir;
	}
	if (!empty_dir(inode)) {
		retval = -ENOTEMPTY;
		goto end_rmdir;
	}
	if (de->inode != inode->i_ino) {
		retval = -ENOENT;
		goto end_rmdir;
	}
	if (inode->i_count > 1) {
		retval = -EBUSY;
		goto end_rmdir;
	}
	if (inode->i_nlink != 2)
		printk("empty directory has nlink!=2 (%d)\n",inode->i_nlink);
	de->inode = 0;
	mark_buffer_dirty(bh, 1);
	inode->i_nlink=0;
	mark_inode_dirty(inode);
	dir->i_nlink--;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dir);
	d_delete(dentry);
	retval = 0;
end_rmdir:
	brelse(bh);
	return retval;
}

int sysv_unlink(struct inode * dir, struct dentry * dentry)
{
	int retval;
	struct inode * inode;
	struct buffer_head * bh;
	struct sysv_dir_entry * de;

repeat:
	retval = -ENOENT;
	inode = NULL;
	bh = sysv_find_entry(dir, dentry->d_name.name,
                             dentry->d_name.len, &de);
	if (!bh)
		goto end_unlink;
	inode = dentry->d_inode;

	retval = -EPERM;
	if (S_ISDIR(inode->i_mode))
		goto end_unlink;
	if (de->inode != inode->i_ino) {
		brelse(bh);
		current->counter = 0;
		schedule();
		goto repeat;
	}
	if ((dir->i_mode & S_ISVTX) && !fsuser() &&
	    current->fsuid != inode->i_uid &&
	    current->fsuid != dir->i_uid)
		goto end_unlink;
	if (de->inode != inode->i_ino) {
		retval = -ENOENT;
		goto end_unlink;
	}
	if (!inode->i_nlink) {
		printk("Deleting nonexistent file (%s:%lu), %d\n",
		        kdevname(inode->i_dev),
		       inode->i_ino, inode->i_nlink);
		inode->i_nlink=1;
	}
	de->inode = 0;
	mark_buffer_dirty(bh, 1);
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dir);
	inode->i_nlink--;
	inode->i_ctime = dir->i_ctime;
	mark_inode_dirty(inode);
	d_delete(dentry);
	retval = 0;
end_unlink:
	brelse(bh);
	return retval;
}

int sysv_symlink(struct inode * dir, struct dentry * dentry, 
		 const char * symname)
{
	struct sysv_dir_entry * de;
	struct inode * inode;
	struct buffer_head * name_block;
	char * name_block_data;
	struct super_block * sb;
	int i;
	char c;
	struct buffer_head * bh;

	if (!(inode = sysv_new_inode(dir)))
		return -ENOSPC;

	inode->i_mode = S_IFLNK | 0777;
	inode->i_op = &sysv_symlink_inode_operations;
	name_block = sysv_file_bread(inode,0,1);
	if (!name_block) {
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput(inode);
		return -ENOSPC;
	}
	sb = inode->i_sb;
	name_block_data = name_block->b_data;
	i = 0;
	while (i < sb->sv_block_size_1 && (c = *(symname++)))
		name_block_data[i++] = c;
	name_block_data[i] = 0;
	mark_buffer_dirty(name_block, 1);
	brelse(name_block);
	inode->i_size = i;
	mark_inode_dirty(inode);
	bh = sysv_find_entry(dir, dentry->d_name.name,
                             dentry->d_name.len, &de);
	if (bh) {
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput(inode);
		brelse(bh);
		return -EEXIST;
	}
	i = sysv_add_entry(dir, dentry->d_name.name,
                           dentry->d_name.len, &bh, &de);
	if (i) {
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput(inode);
		return i;
	}
	de->inode = inode->i_ino;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
        d_instantiate(dentry, inode);
	return 0;
}

int sysv_link(struct inode * oldinode, struct inode * dir, 
	      struct dentry * dentry)
{
	int error;
	struct sysv_dir_entry * de;
	struct buffer_head * bh;

	if (S_ISDIR(oldinode->i_mode)) {
		return -EPERM;
	}
	if (oldinode->i_nlink >= oldinode->i_sb->sv_link_max) {
		return -EMLINK;
	}
	bh = sysv_find_entry(dir, dentry->d_name.name,
                             dentry->d_name.len, &de);
	if (bh) {
		brelse(bh);
		return -EEXIST;
	}
	error = sysv_add_entry(dir, dentry->d_name.name,
                               dentry->d_name.len, &bh, &de);
	if (error) {
		return error;
	}
	de->inode = oldinode->i_ino;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	oldinode->i_nlink++;
	oldinode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(oldinode);
        d_instantiate(dentry, oldinode);
	return 0;
}

/* return 1 if `new' is a subdir of `old' on the same device */
static int subdir(struct dentry * new_dentry, struct dentry * old_dentry)
{
	int result = 0;
	
        for (;;) {
                if (new_dentry != old_dentry) {
                        struct dentry * parent = new_dentry->d_parent;
                        if (parent == new_dentry)
                                break;
                        new_dentry = parent;
                        continue;
                }
                result = 1;
                break;
        }
        return result;
}

#define PARENT_INO(buffer) \
(((struct sysv_dir_entry *) ((buffer) + 1*SYSV_DIRSIZE))->inode)

/*
 * rename uses retrying to avoid race-conditions: at least they should be minimal.
 * it tries to allocate all the blocks, then sanity-checks, and if the sanity-
 * checks fail, it tries to restart itself again. Very practical - no changes
 * are done until we know everything works ok.. and then all the changes can be
 * done in one fell swoop when we have claimed all the buffers needed.
 *
 * Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 */
static int do_sysv_rename(struct inode * old_dir, struct dentry * old_dentry,
			  struct inode * new_dir, struct dentry * new_dentry)
{
	struct inode * old_inode, * new_inode;
	struct buffer_head * old_bh, * new_bh, * dir_bh;
	struct sysv_dir_entry * old_de, * new_de;
	int retval;

	goto start_up;
try_again:
	brelse(old_bh);
	brelse(new_bh);
	brelse(dir_bh);
	iput(old_inode);
	iput(new_inode);
	current->counter = 0;
	schedule();
start_up:
	old_inode = new_inode = NULL;
	old_bh = new_bh = dir_bh = NULL;
	old_bh = sysv_find_entry(old_dir,old_dentry->d_name.name,
				old_dentry->d_name.len,&old_de);
	retval = -ENOENT;
	if (!old_bh)
		goto end_rename;
	old_inode = old_dentry->d_inode;/* don't cross mnt-points */
	retval = -EPERM;
	if ((old_dir->i_mode & S_ISVTX) && 
	    current->fsuid != old_inode->i_uid &&
	    current->fsuid != old_dir->i_uid && !fsuser())
		goto end_rename;
	new_inode = new_dentry->d_inode;
	new_bh = sysv_find_entry(new_dir,new_dentry->d_name.name,
				new_dentry->d_name.len,&new_de);
	if (new_bh) {
		if (!new_inode) {
			brelse(new_bh);
			new_bh = NULL;
		}
	}
	if (new_inode == old_inode) {
		retval = 0;
		goto end_rename;
	}
	if (new_inode && S_ISDIR(new_inode->i_mode)) {
		retval = -EISDIR;
		if (!S_ISDIR(old_inode->i_mode))
			goto end_rename;
		retval = -EINVAL;
		if (subdir(new_dentry, old_dentry))
			goto end_rename;
		retval = -ENOTEMPTY;
		if (!empty_dir(new_inode))
			goto end_rename;
		retval = -EBUSY;
		if (new_inode->i_count > 1)
			goto end_rename;
	}
	retval = -EPERM;
	if (new_inode && (new_dir->i_mode & S_ISVTX) && 
	    current->fsuid != new_inode->i_uid &&
	    current->fsuid != new_dir->i_uid && !fsuser())
		goto end_rename;
	if (S_ISDIR(old_inode->i_mode)) {
		retval = -ENOTDIR;
		if (new_inode && !S_ISDIR(new_inode->i_mode))
			goto end_rename;
		retval = -EINVAL;
		if (subdir(new_dentry, old_dentry))
			goto end_rename;
		retval = -EIO;
		dir_bh = sysv_file_bread(old_inode,0,0);
		if (!dir_bh)
			goto end_rename;
		if (PARENT_INO(dir_bh->b_data) != old_dir->i_ino)
			goto end_rename;
		retval = -EMLINK;
		if (!new_inode && new_dir->i_nlink >= new_dir->i_sb->sv_link_max)
			goto end_rename;
	}
	if (!new_bh) {
		retval = sysv_add_entry(new_dir,new_dentry->d_name.name,
					new_dentry->d_name.len,&new_bh,&new_de);
		if (retval)
			goto end_rename;
	}
/* sanity checking before doing the rename - avoid races */
	if (new_inode && (new_de->inode != new_inode->i_ino))
		goto try_again;
	if (new_de->inode && !new_inode)
		goto try_again;
	if (old_de->inode != old_inode->i_ino)
		goto try_again;
/* ok, that's it */
	old_de->inode = 0;
	new_de->inode = old_inode->i_ino;
	old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(old_dir);
	new_dir->i_ctime = new_dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(new_dir);
	if (new_inode) {
		new_inode->i_nlink--;
		new_inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(new_inode);
	}
	mark_buffer_dirty(old_bh, 1);
	mark_buffer_dirty(new_bh, 1);
	if (dir_bh) {
		PARENT_INO(dir_bh->b_data) = new_dir->i_ino;
		mark_buffer_dirty(dir_bh, 1);
		old_dir->i_nlink--;
		mark_inode_dirty(old_dir);
		if (new_inode) {
			new_inode->i_nlink--;
			mark_inode_dirty(new_inode);
		} else {
			new_dir->i_nlink++;
			mark_inode_dirty(new_dir);
		}
	}
	retval = 0;
end_rename:
	brelse(dir_bh);
	brelse(old_bh);
	brelse(new_bh);
	iput(old_inode);
	iput(new_inode);
	iput(old_dir);
	iput(new_dir);
	return retval;
}

/*
 * Ok, rename also locks out other renames, as they can change the parent of
 * a directory, and we don't want any races. Other races are checked for by
 * "do_rename()", which restarts if there are inconsistencies.
 *
 * Note that there is no race between different filesystems: it's only within
 * the same device that races occur: many renames can happen at once, as long
 * as they are on different partitions.
 */
int sysv_rename(struct inode * old_dir, struct dentry * old_dentry,
		struct inode * new_dir, struct dentry * new_dentry)
{
	static struct wait_queue * wait = NULL;
	static int lock = 0;
	int result;

	while (lock)
		sleep_on(&wait);
	lock = 1;
	result = do_sysv_rename(old_dir, old_dentry,
				new_dir, new_dentry);
	lock = 0;
	wake_up(&wait);
	return result;
}
