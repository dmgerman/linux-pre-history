/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/root.c
 *
 *  Copyright 1997 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/modversions.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/malloc.h>
#include <linux/ioctl.h>
#include <linux/auto_fs.h>

static int autofs_root_readdir(struct inode *,struct file *,void *,filldir_t);
static int autofs_root_lookup(struct inode *,const char *,int,struct inode **);
static int autofs_root_symlink(struct inode *,const char *,int,const char *);
static int autofs_root_unlink(struct inode *,const char *,int);
static int autofs_root_rmdir(struct inode *,const char *,int);
static int autofs_root_mkdir(struct inode *,const char *,int,int);
static int autofs_root_ioctl(struct inode *, struct file *,unsigned int,unsigned long);

static struct file_operations autofs_root_operations = {
        NULL,                   /* lseek */
        NULL,                   /* read */
        NULL,                   /* write */
        autofs_root_readdir,    /* readdir */
        NULL,                   /* select */
        autofs_root_ioctl,	/* ioctl */
        NULL,                   /* mmap */
        NULL,                   /* open */
        NULL,                   /* release */
        NULL                    /* fsync */
};

struct inode_operations autofs_root_inode_operations = {
        &autofs_root_operations, /* file operations */
        NULL,                   /* create */
        autofs_root_lookup,     /* lookup */
        NULL,                   /* link */
        autofs_root_unlink,     /* unlink */
        autofs_root_symlink,    /* symlink */
        autofs_root_mkdir,      /* mkdir */
        autofs_root_rmdir,      /* rmdir */
        NULL,                   /* mknod */
        NULL,                   /* rename */
        NULL,                   /* readlink */
        NULL,                   /* follow_link */
        NULL,                   /* readpage */
        NULL,                   /* writepage */
        NULL,                   /* bmap */
        NULL,                   /* truncate */
        NULL                    /* permission */
};

static int autofs_root_readdir(struct inode *inode, struct file *filp,
			       void *dirent, filldir_t filldir)
{
	struct autofs_dir_ent *ent;
	struct autofs_dirhash *dirhash;
	off_t onr, nr;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -ENOTDIR;

	dirhash = &((struct autofs_sb_info *)inode->i_sb->u.generic_sbp)->dirhash;
	nr = filp->f_pos;

	switch(nr)
	{
	case 0:
		if (filldir(dirent, ".", 1, nr, inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	case 1:
		if (filldir(dirent, "..", 2, nr, inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	default:
		while ( onr = nr, ent = autofs_hash_enum(dirhash,&nr) ) {
			if (filldir(dirent,ent->name,ent->len,onr,ent->ino) < 0)
				return 0;
			filp->f_pos = nr;
		}
		break;
	}

	return 0;
}

static int autofs_root_lookup(struct inode *dir, const char *name, int len,
			      struct inode **result)
{
	struct autofs_sb_info *sbi;
	struct autofs_dir_ent *ent;
	struct inode *res;
	autofs_hash_t hash;
	int status, oz_mode;

	DPRINTK(("autofs_root_lookup: name = "));
	autofs_say(name,len);

	*result = NULL;
	if (!dir)
		return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOTDIR;
	}

	/* Handle special cases: . and ..; since this is a root directory,
	   they both point to the inode itself */
	*result = dir;
	if (!len)
		return 0;
	if (name[0] == '.') {
		if (len == 1)
			return 0;
		if (name[1] == '.' && len == 2)
			return 0;
	}

	*result = res = NULL;
	sbi = (struct autofs_sb_info *) dir->i_sb->u.generic_sbp;

	hash = autofs_hash(name,len);

	oz_mode = autofs_oz_mode(sbi);
	DPRINTK(("autofs_lookup: pid = %u, pgrp = %u, catatonic = %d, oz_mode = %d\n", current->pid, current->pgrp, sbi->catatonic, oz_mode));

	do {
		while ( !(ent = autofs_hash_lookup(&sbi->dirhash,hash,name,len)) ) {
			DPRINTK(("lookup failed, pid = %u, pgrp = %u\n", current->pid, current->pgrp));
			
			if ( oz_mode ) {
				iput(dir);
				return -ENOENT;
			} else {
				status = autofs_wait(sbi,hash,name,len);
				DPRINTK(("autofs_wait returned %d\n", status));
				if ( status ) {
					iput(dir);
					return status;
				}
			}
		}

		DPRINTK(("lookup successful, inode = %08x\n", (unsigned int)ent->ino));

		if (!(res = iget(dir->i_sb,ent->ino))) {
			printk("autofs: iget returned null!\n");
			iput(dir);
			return -EACCES;
		}
		
		if ( !oz_mode && S_ISDIR(res->i_mode) && res->i_sb == dir->i_sb ) {
			/* Not a mount point yet, call 1-800-DAEMON */
			DPRINTK(("autofs: waiting on non-mountpoint dir, inode = %lu, pid = %u, pgrp = %u\n", res->i_ino, current->pid, current->pgrp));
			iput(res);
			res = NULL;
			status = autofs_wait(sbi,hash,name,len);
			if ( status ) {
				iput(dir);
				return status;
			}
		}
	} while(!res);
		
	*result = res;
	iput(dir);
	return 0;
}

static int autofs_root_symlink(struct inode *dir, const char *name, int len, const char *symname)
{
	struct autofs_sb_info *sbi = (struct autofs_sb_info *) dir->i_sb->u.generic_sbp;
	struct autofs_dirhash *dh = &sbi->dirhash;
	autofs_hash_t hash = autofs_hash(name,len);
	struct autofs_dir_ent *ent;
	unsigned int n;
	int slsize;
	struct autofs_symlink *sl;

	DPRINTK(("autofs_root_symlink: %s <- ", symname));
	autofs_say(name,len);

	iput(dir);
	
	if ( !autofs_oz_mode(sbi) )
		return -EPERM;

	if ( autofs_hash_lookup(dh,hash,name,len) )
		return -EEXIST;

	n = find_first_zero_bit(sbi->symlink_bitmap,AUTOFS_MAX_SYMLINKS);
	if ( n >= AUTOFS_MAX_SYMLINKS )
		return -ENOSPC;

	set_bit(n,sbi->symlink_bitmap);
	sl = &sbi->symlink[n];
	sl->len = strlen(symname);
	sl->data = kmalloc(slsize = sl->len+1, GFP_KERNEL);
	if ( !sl->data ) {
		clear_bit(n,sbi->symlink_bitmap);
		return -ENOSPC;
	}
	ent = kmalloc(sizeof(struct autofs_dir_ent), GFP_KERNEL);
	if ( !ent ) {
		kfree(sl->data);
		clear_bit(n,sbi->symlink_bitmap);
		return -ENOSPC;
	}
	ent->name = kmalloc(len, GFP_KERNEL);
	if ( !ent->name ) {
		kfree(sl->data);
		kfree(ent);
		clear_bit(n,sbi->symlink_bitmap);
		return -ENOSPC;
	}
	memcpy(sl->data,symname,slsize);
	sl->mtime = CURRENT_TIME;

	ent->ino = AUTOFS_FIRST_SYMLINK + n;
	ent->hash = hash;
	memcpy(ent->name,name,ent->len = len);
	ent->expiry = END_OF_TIME;

	autofs_hash_insert(dh,ent);

	return 0;
}

static int autofs_root_unlink(struct inode *dir, const char *name, int len)
{
	struct autofs_sb_info *sbi = (struct autofs_sb_info *) dir->i_sb->u.generic_sbp;
	struct autofs_dirhash *dh = &sbi->dirhash;
	autofs_hash_t hash = autofs_hash(name,len);
	struct autofs_dir_ent *ent;
	unsigned int n;

	if ( !autofs_oz_mode(sbi) )
		return -EPERM;

	ent = autofs_hash_lookup(dh,hash,name,len);
	if ( !ent )
		return -ENOENT;
	n = ent->ino - AUTOFS_FIRST_SYMLINK;
	if ( n >= AUTOFS_MAX_SYMLINKS || !test_bit(n,sbi->symlink_bitmap) )
		return -EINVAL;	/* Not a symlink inode, can't unlink */
	autofs_hash_delete(ent);
	clear_bit(n,sbi->symlink_bitmap);
	kfree(sbi->symlink[n].data);
	
	return 0;
}

static int autofs_root_rmdir(struct inode *dir, const char *name, int len)
{
	struct autofs_sb_info *sbi = (struct autofs_sb_info *) dir->i_sb->u.generic_sbp;
	struct autofs_dirhash *dh = &sbi->dirhash;
	autofs_hash_t hash = autofs_hash(name,len);
	struct autofs_dir_ent *ent;

	if ( !autofs_oz_mode(sbi) ) {
		iput(dir);
		return -EPERM;
	}
	ent = autofs_hash_lookup(dh,hash,name,len);
	if ( !ent ) {
		iput(dir);
		return -ENOENT;
	}
	if ( (unsigned int)ent->ino < AUTOFS_FIRST_DIR_INO ) {
		iput(dir);
		return -ENOTDIR; /* Not a directory */
	}
	autofs_hash_delete(ent);
	dir->i_nlink--;
	iput(dir);

	return 0;
}

static int autofs_root_mkdir(struct inode *dir, const char *name, int len, int mode)
{
	struct autofs_sb_info *sbi = (struct autofs_sb_info *) dir->i_sb->u.generic_sbp;
	struct autofs_dirhash *dh = &sbi->dirhash;
	autofs_hash_t hash = autofs_hash(name,len);
	struct autofs_dir_ent *ent;

	if ( !autofs_oz_mode(sbi) ) {
		iput(dir);
		return -EPERM;
	}
	ent = autofs_hash_lookup(dh,hash,name,len);
	if ( ent ) {
		iput(dir);
		return -EEXIST;
	}
	if ( sbi->next_dir_ino < AUTOFS_FIRST_DIR_INO ) {
		printk("autofs: Out of inode numbers -- what the heck did you do??\n");
		iput(dir);
		return -ENOSPC;
	}
	ent = kmalloc(sizeof(struct autofs_dir_ent), GFP_KERNEL);
	if ( !ent ) {
		iput(dir);
		return -ENOSPC;
	}
	ent->name = kmalloc(len, GFP_KERNEL);
	if ( !ent->name ) {
		kfree(ent);
		iput(dir);
		return -ENOSPC;
	}
	ent->hash = hash;
	memcpy(ent->name, name, ent->len = len);
	ent->ino = sbi->next_dir_ino++;
	ent->expiry = END_OF_TIME;
	autofs_hash_insert(dh,ent);
	dir->i_nlink++;
	iput(dir);

	return 0;
}

/*
 * ioctl()'s on the root directory is the chief method for the daemon to
 * generate kernel reactions
 */
static int autofs_root_ioctl(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg)
{
	struct autofs_sb_info *sbi = (struct autofs_sb_info *)inode->i_sb->u.generic_sbp;

	DPRINTK(("autofs_ioctl: cmd = %04x, arg = 0x%08lx, sbi = %p, pgrp = %u\n",cmd,arg,sbi,current->pgrp));

	switch(cmd) {
	case AUTOFS_IOC_READY:	/* Wait queue: go ahead and retry */
		if ( !autofs_oz_mode(sbi) && !fsuser() )
			return -EPERM;
		return autofs_wait_release(sbi,arg,0);
	case AUTOFS_IOC_FAIL:	/* Wait queue: fail with ENOENT */
		/* Optional: add to failure cache */
		if ( !autofs_oz_mode(sbi) && !fsuser() )
			return -EPERM;
		return autofs_wait_release(sbi,arg,-ENOENT);
	case AUTOFS_IOC_CATATONIC: /* Enter catatonic mode (daemon shutdown) */
		if ( !autofs_oz_mode(sbi) && !fsuser() )
			return -EPERM;
		autofs_catatonic_mode(sbi);
		return 0;
	default:
		return -ENOTTY;	/* Should this be ENOSYS? */
	}
}
