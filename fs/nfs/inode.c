/*
 *  linux/fs/nfs/inode.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs inode and superblock handling functions
 *
 *  Modularised by Alan Cox <Alan.Cox@linux.org>, while hacking some
 *  experimental NFS changes. Modularisation taken straight from SYS5 fs.
 *
 *  Change to nfs_read_super() to permit NFS mounts to multi-homed hosts.
 *  J.S.Peatfield@damtp.cam.ac.uk
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/unistd.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/stats.h>
#include <linux/nfs_fs.h>
#include <linux/lockd/bind.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#define NFSDBG_FACILITY		NFSDBG_VFS
#define NFS_PARANOIA 1

extern void nfs_invalidate_dircache_sb(struct super_block *);
extern int  check_failed_request(struct inode *);

static void nfs_read_inode(struct inode *);
static void nfs_put_inode(struct inode *);
static void nfs_delete_inode(struct inode *);
static int  nfs_notify_change(struct inode *, struct iattr *);
static void nfs_put_super(struct super_block *);
static int  nfs_statfs(struct super_block *, struct statfs *, int);

static struct super_operations nfs_sops = { 
	nfs_read_inode,		/* read inode */
	NULL,			/* write inode */
	nfs_put_inode,		/* put inode */
	nfs_delete_inode,	/* delete inode */
	nfs_notify_change,	/* notify change */
	nfs_put_super,		/* put superblock */
	NULL,			/* write superblock */
	nfs_statfs,		/* stat filesystem */
	NULL
};

struct rpc_stat			nfs_rpcstat = { &nfs_program };

/*
 * The "read_inode" function doesn't actually do anything:
 * the real data is filled in later in nfs_fhget. Here we
 * just mark the cache times invalid, and zero out i_mode
 * (the latter makes "nfs_refresh_inode" do the right thing
 * wrt pipe inodes)
 */
static void
nfs_read_inode(struct inode * inode)
{
	inode->i_blksize = inode->i_sb->s_blocksize;
	inode->i_mode = 0;
	inode->i_rdev = 0;
	inode->i_op = NULL;
	NFS_CACHEINV(inode);
	NFS_ATTRTIMEO(inode) = NFS_MINATTRTIMEO(inode);
}

static void
nfs_put_inode(struct inode * inode)
{
	dprintk("NFS: put_inode(%x/%ld)\n", inode->i_dev, inode->i_ino);
	/*
	 * We want to get rid of unused inodes ...
	 */
	if (inode->i_count == 1)
		inode->i_nlink = 0;
}

static void
nfs_delete_inode(struct inode * inode)
{
	int failed;

	dprintk("NFS: delete_inode(%x/%ld)\n", inode->i_dev, inode->i_ino);
	/*
	 * Flush out any pending write requests ...
	 */
	if (NFS_WRITEBACK(inode) != NULL) {
		unsigned long timeout = jiffies + 5*HZ;
		printk("NFS: inode %ld, invalidating pending RPC requests\n",
			inode->i_ino);
		nfs_invalidate_pages(inode);
		while (NFS_WRITEBACK(inode) != NULL && jiffies < timeout) {
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + HZ/10;
			schedule();
		}
		current->state = TASK_RUNNING;
		if (NFS_WRITEBACK(inode) != NULL)
			printk("NFS: Arghhh, stuck RPC requests!\n");
	}

	failed = check_failed_request(inode);
	if (failed)
		printk("NFS: inode %ld had %d failed requests\n",
			inode->i_ino, failed);
	clear_inode(inode);
}

void
nfs_put_super(struct super_block *sb)
{
	struct nfs_server *server = &sb->u.nfs_sb.s_server;
	struct rpc_clnt	*rpc;

	/*
	 * Lock the super block while we bring down the daemons.
	 */
	lock_super(sb);
	if ((rpc = server->client) != NULL)
		rpc_shutdown_client(rpc);

	if (!(server->flags & NFS_MOUNT_NONLM))
		lockd_down();	/* release rpc.lockd */
	rpciod_down();		/* release rpciod */
	/*
	 * Invalidate the dircache for this superblock.
	 */
	nfs_invalidate_dircache_sb(sb);

	sb->s_dev = 0;
	unlock_super(sb);
	MOD_DEC_USE_COUNT;
}

/*
 * Compute and set NFS server blocksize
 */
static unsigned int
nfs_block_size(unsigned int bsize, unsigned char *nrbitsp)
{
	if (bsize < 1024)
		bsize = NFS_DEF_FILE_IO_BUFFER_SIZE;
	else if (bsize >= NFS_MAX_FILE_IO_BUFFER_SIZE)
		bsize = NFS_MAX_FILE_IO_BUFFER_SIZE;

	/* make sure blocksize is a power of two */
	if ((bsize & (bsize - 1)) || nrbitsp) {
		unsigned int	nrbits;

		for (nrbits = 31; nrbits && !(bsize & (1 << nrbits)); nrbits--)
			;
		bsize = 1 << nrbits;
		if (nrbitsp)
			*nrbitsp = nrbits;
		if (bsize < NFS_DEF_FILE_IO_BUFFER_SIZE)
			bsize = NFS_DEF_FILE_IO_BUFFER_SIZE;
	}

	return bsize;
}

/*
 * The way this works is that the mount process passes a structure
 * in the data argument which contains the server's IP address
 * and the root file handle obtained from the server's mount
 * daemon. We stash these away in the private superblock fields.
 */
struct super_block *
nfs_read_super(struct super_block *sb, void *raw_data, int silent)
{
	struct nfs_mount_data	*data = (struct nfs_mount_data *) raw_data;
	struct sockaddr_in	srvaddr;
	struct nfs_server	*server;
	struct rpc_timeout	timeparms;
	struct rpc_xprt		*xprt;
	struct rpc_clnt		*clnt;
	unsigned int		authflavor;
	int			tcp;
	kdev_t			dev = sb->s_dev;
	struct inode		*root_inode;

	MOD_INC_USE_COUNT;
	if (!data)
		goto out_miss_args;

	if (data->version != NFS_MOUNT_VERSION) {
		printk("nfs warning: mount version %s than kernel\n",
			data->version < NFS_MOUNT_VERSION ? "older" : "newer");
		if (data->version < 2)
			data->namlen = 0;
		if (data->version < 3)
			data->bsize  = 0;
	}

	/* We now require that the mount process passes the remote address */
	memcpy(&srvaddr, &data->addr, sizeof(srvaddr));
	if (srvaddr.sin_addr.s_addr == INADDR_ANY)
		goto out_no_remote;

	lock_super(sb);

	sb->s_magic      = NFS_SUPER_MAGIC;
	sb->s_dev        = dev;
	sb->s_op         = &nfs_sops;
	sb->s_blocksize  = nfs_block_size(data->bsize, &sb->s_blocksize_bits);
	sb->u.nfs_sb.s_root = data->root;
	server           = &sb->u.nfs_sb.s_server;
	server->rsize    = nfs_block_size(data->rsize, NULL);
	server->wsize    = nfs_block_size(data->wsize, NULL);
	server->flags    = data->flags;
	server->acregmin = data->acregmin*HZ;
	server->acregmax = data->acregmax*HZ;
	server->acdirmin = data->acdirmin*HZ;
	server->acdirmax = data->acdirmax*HZ;
	strcpy(server->hostname, data->hostname);

	/* Which protocol do we use? */
	tcp   = (data->flags & NFS_MOUNT_TCP);

	/* Initialize timeout values */
	timeparms.to_initval = data->timeo * HZ / 10;
	timeparms.to_retries = data->retrans;
	timeparms.to_maxval  = tcp? RPC_MAX_TCP_TIMEOUT : RPC_MAX_UDP_TIMEOUT;
	timeparms.to_exponential = 1;

	/* Choose authentication flavor */
	if (data->flags & NFS_MOUNT_SECURE) {
		authflavor = RPC_AUTH_DES;
	} else if (data->flags & NFS_MOUNT_KERBEROS) {
		authflavor = RPC_AUTH_KRB;
	} else {
		authflavor = RPC_AUTH_UNIX;
	}

	/* Now create transport and client */
	xprt = xprt_create_proto(tcp? IPPROTO_TCP : IPPROTO_UDP,
						&srvaddr, &timeparms);
	if (xprt == NULL)
		goto out_no_xprt;

	clnt = rpc_create_client(xprt, server->hostname, &nfs_program,
						NFS_VERSION, authflavor);
	if (clnt == NULL)
		goto out_no_client;

	clnt->cl_intr     = (data->flags & NFS_MOUNT_INTR)? 1 : 0;
	clnt->cl_softrtry = (data->flags & NFS_MOUNT_SOFT)? 1 : 0;
	clnt->cl_chatty   = 1;
	server->client    = clnt;

	/* Fire up rpciod if not yet running */
#ifdef RPCIOD_RESULT
	if (rpciod_up())
		goto out_no_iod;
#else
	rpciod_up();
#endif

	/*
	 * Keep the super block locked while we try to get 
	 * the root fh attributes.
	 */
	root_inode = nfs_fhget(sb, &data->root, NULL);
	if (!root_inode)
		goto out_no_root;
	sb->s_root = d_alloc_root(root_inode, NULL);
	if (!sb->s_root)
		goto out_no_root;
	/* We're airborne */
	unlock_super(sb);

	/* Check whether to start the lockd process */
	if (!(server->flags & NFS_MOUNT_NONLM))
		lockd_up();
	return sb;

	/* Yargs. It didn't work out. */
out_no_root:
	printk("nfs_read_super: get root inode failed\n");
	iput(root_inode);
	rpciod_down();
#ifdef RPCIOD_RESULT
	goto out_shutdown;

out_no_iod:
	printk("nfs_read_super: couldn't start rpciod!\n");
out_shutdown:
#endif
	rpc_shutdown_client(server->client);
	goto out_unlock;

out_no_client:
	printk("NFS: cannot create RPC client.\n");
	xprt_destroy(xprt);
	goto out_unlock;

out_no_xprt:
	printk("NFS: cannot create RPC transport.\n");
out_unlock:
	unlock_super(sb);
	goto out_fail;

out_no_remote:
	printk("NFS: mount program didn't pass remote address!\n");
	goto out_fail;

out_miss_args:
	printk("nfs_read_super: missing data argument\n");

out_fail:
	sb->s_dev = 0;
	MOD_DEC_USE_COUNT;
	return NULL;
}

static int
nfs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	int error;
	struct nfs_fsinfo res;
	struct statfs tmp;

	error = nfs_proc_statfs(&sb->u.nfs_sb.s_server, &sb->u.nfs_sb.s_root,
		&res);
	if (error) {
		printk("nfs_statfs: statfs error = %d\n", -error);
		res.bsize = res.blocks = res.bfree = res.bavail = 0;
	}
	tmp.f_type = NFS_SUPER_MAGIC;
	tmp.f_bsize = res.bsize;
	tmp.f_blocks = res.blocks;
	tmp.f_bfree = res.bfree;
	tmp.f_bavail = res.bavail;
	tmp.f_files = 0;
	tmp.f_ffree = 0;
	tmp.f_namelen = NAME_MAX;
	return copy_to_user(buf, &tmp, bufsiz) ? -EFAULT : 0;
}

/*
 * This is our own version of iget that looks up inodes by file handle
 * instead of inode number.  We use this technique instead of using
 * the vfs read_inode function because there is no way to pass the
 * file handle or current attributes into the read_inode function.
 * We just have to be careful not to subvert iget's special handling
 * of mount points.
 */
struct inode *
nfs_fhget(struct super_block *sb, struct nfs_fh *fhandle,
				  struct nfs_fattr *fattr)
{
	struct nfs_fattr newfattr;
	int error;
	struct inode *inode;

	if (!sb) {
		printk("nfs_fhget: super block is NULL\n");
		return NULL;
	}
	if (!fattr) {
		error = nfs_proc_getattr(&sb->u.nfs_sb.s_server, fhandle,
			&newfattr);
		if (error) {
			printk("nfs_fhget: getattr error = %d\n", -error);
			return NULL;
		}
		fattr = &newfattr;
	}
	if (!(inode = iget(sb, fattr->fileid))) {
		printk("nfs_fhget: iget failed\n");
		return NULL;
	}
#ifdef NFS_PARANOIA
if (inode->i_dev != sb->s_dev)
printk("nfs_fhget: impossible\n");
#endif

	if (inode->i_ino != fattr->fileid) {
		printk("nfs_fhget: unexpected inode from iget\n");
		return inode;
	}

	/*
	 * Check whether the mode has been set, as we only want to
	 * do this once. (We don't allow inodes to change types.)
	 */
	if (inode->i_mode == 0) {
		inode->i_mode = fattr->mode;
		if (S_ISREG(inode->i_mode))
			inode->i_op = &nfs_file_inode_operations;
		else if (S_ISDIR(inode->i_mode))
			inode->i_op = &nfs_dir_inode_operations;
		else if (S_ISLNK(inode->i_mode))
			inode->i_op = &nfs_symlink_inode_operations;
		else if (S_ISCHR(inode->i_mode)) {
			inode->i_op = &chrdev_inode_operations;
			inode->i_rdev = to_kdev_t(fattr->rdev);
		} else if (S_ISBLK(inode->i_mode)) {
			inode->i_op = &blkdev_inode_operations;
			inode->i_rdev = to_kdev_t(fattr->rdev);
		} else if (S_ISFIFO(inode->i_mode))
			init_fifo(inode);
		else
			inode->i_op = NULL;
		/*
		 * Preset the size and mtime, as there's no need
		 * to invalidate the caches.
		 */ 
		inode->i_size  = fattr->size;
		inode->i_mtime = fattr->mtime.seconds;
		NFS_OLDMTIME(inode) = fattr->mtime.seconds;
	}
	*NFS_FH(inode) = *fhandle;
	nfs_refresh_inode(inode, fattr);
	dprintk("NFS: fhget(%x/%ld ct=%d)\n",
		inode->i_dev, inode->i_ino,
		inode->i_count);

	return inode;
}

int
nfs_notify_change(struct inode *inode, struct iattr *attr)
{
	struct nfs_sattr sattr;
	struct nfs_fattr fattr;
	int error;

	/*
	 * Make sure the inode is up-to-date.
	 */
	error = nfs_revalidate(inode);
	if (error) {
#ifdef NFS_PARANOIA
printk("nfs_notify_change: revalidate failed, error=%d\n", error);
#endif
		goto out;
	}

	sattr.mode = (u32) -1;
	if (attr->ia_valid & ATTR_MODE) 
		sattr.mode = attr->ia_mode;

	sattr.uid = (u32) -1;
	if (attr->ia_valid & ATTR_UID)
		sattr.uid = attr->ia_uid;

	sattr.gid = (u32) -1;
	if (attr->ia_valid & ATTR_GID)
		sattr.gid = attr->ia_gid;

	sattr.size = (u32) -1;
	if ((attr->ia_valid & ATTR_SIZE) && S_ISREG(inode->i_mode))
		sattr.size = attr->ia_size;

	sattr.mtime.seconds = sattr.mtime.useconds = (u32) -1;
	if (attr->ia_valid & ATTR_MTIME) {
		sattr.mtime.seconds = attr->ia_mtime;
		sattr.mtime.useconds = 0;
	}

	sattr.atime.seconds = sattr.atime.useconds = (u32) -1;
	if (attr->ia_valid & ATTR_ATIME) {
		sattr.atime.seconds = attr->ia_atime;
		sattr.atime.useconds = 0;
	}

	error = nfs_proc_setattr(NFS_SERVER(inode), NFS_FH(inode),
				&sattr, &fattr);
	if (error)
		goto out;
	/*
	 * If we changed the size or mtime, update the inode
	 * now to avoid invalidating the page cache.
	 */
	if (sattr.size != (u32) -1) {
		if (sattr.size != fattr.size)
			printk("nfs_notify_change: sattr=%d, fattr=%d??\n",
				sattr.size, fattr.size);
		nfs_truncate_dirty_pages(inode, sattr.size);
		inode->i_size  = sattr.size;
		inode->i_mtime = fattr.mtime.seconds;
	}
	if (sattr.mtime.seconds != (u32) -1)
		inode->i_mtime = fattr.mtime.seconds;
	error = nfs_refresh_inode(inode, &fattr);
out:
	return error;
}

/*
 * Externally visible revalidation function
 */
int
nfs_revalidate(struct inode *inode)
{
	return nfs_revalidate_inode(NFS_SERVER(inode), inode);
}

/*
 * This function is called whenever some part of NFS notices that
 * the cached attributes have to be refreshed.
 */
int
_nfs_revalidate_inode(struct nfs_server *server, struct inode *inode)
{
	struct nfs_fattr fattr;
	int		 status = 0;

	if (jiffies - NFS_READTIME(inode) < NFS_ATTRTIMEO(inode))
		goto out;

	dfprintk(PAGECACHE, "NFS: revalidating %x/%ld inode\n",
			inode->i_dev, inode->i_ino);
	status = nfs_proc_getattr(server, NFS_FH(inode), &fattr);
	if (status) {
#ifdef NFS_PARANOIA
printk("nfs_revalidate_inode: getattr failed, error=%d\n", status);
#endif
		goto done;
	}

	status = nfs_refresh_inode(inode, &fattr);
	if (status)
		goto done;
	if (fattr.mtime.seconds == NFS_OLDMTIME(inode)) {
		/* Update attrtimeo value */
		if ((NFS_ATTRTIMEO(inode) <<= 1) > NFS_MAXATTRTIMEO(inode))
			NFS_ATTRTIMEO(inode) = NFS_MAXATTRTIMEO(inode);
	}
	NFS_OLDMTIME(inode) = fattr.mtime.seconds;

done:
	dfprintk(PAGECACHE,
		"NFS: inode %x/%ld revalidation complete (status %d).\n",
				inode->i_dev, inode->i_ino, status);
out:
	return status;
}

/*
 * Many nfs protocol calls return the new file attributes after
 * an operation.  Here we update the inode to reflect the state
 * of the server's inode.
 *
 * This is a bit tricky because we have to make sure all dirty pages
 * have been sent off to the server before calling invalidate_inode_pages.
 * To make sure no other process adds more write requests while we try
 * our best to flush them, we make them sleep during the attribute refresh.
 *
 * A very similar scenario holds for the dir cache.
 */
int
nfs_refresh_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	int invalid = 0;
	int error = -EIO;

	dfprintk(VFS, "NFS: refresh_inode(%x/%ld ct=%d)\n",
		 inode->i_dev, inode->i_ino, inode->i_count);

	if (!inode || !fattr) {
		printk("nfs_refresh_inode: inode or fattr is NULL\n");
		goto out;
	}
	if (inode->i_ino != fattr->fileid) {
		printk("nfs_refresh_inode: inode number mismatch\n");
		goto out;
	}

	/*
	 * Make sure the inode's type hasn't changed.
	 */
	if ((inode->i_mode & S_IFMT) != (fattr->mode & S_IFMT))
		goto out_changed;

	/*
	 * If the size or mtime changed from outside, we want
	 * to invalidate the local caches immediately.
	 */
	if (inode->i_size != fattr->size) {
#ifdef NFS_DEBUG_VERBOSE
printk("NFS: size change on %x/%ld\n", inode->i_dev, inode->i_ino);
#endif
		invalid = 1;
	}
	if (inode->i_mtime != fattr->mtime.seconds) {
#ifdef NFS_DEBUG_VERBOSE
printk("NFS: mtime change on %x/%ld\n", inode->i_dev, inode->i_ino);
#endif
		invalid = 1;
	}

	inode->i_mode = fattr->mode;
	inode->i_nlink = fattr->nlink;
	inode->i_uid = fattr->uid;
	inode->i_gid = fattr->gid;

	inode->i_size = fattr->size;
	inode->i_blocks = fattr->blocks;
	inode->i_atime = fattr->atime.seconds;
	inode->i_mtime = fattr->mtime.seconds;
	inode->i_ctime = fattr->ctime.seconds;
	/*
	 * Update the read time so we don't revalidate too often.
	 */
	NFS_READTIME(inode) = jiffies;
	error = 0;
	if (invalid)
		goto out_invalid;
out:
	return error;

out_changed:
	/*
	 * Big trouble! The inode has become a different object.
	 */
#ifdef NFS_PARANOIA
printk("nfs_refresh_inode: inode %ld mode changed, %07o to %07o\n",
inode->i_ino, inode->i_mode, fattr->mode);
#endif
	fattr->mode = inode->i_mode; /* save mode */
	make_bad_inode(inode);
	inode->i_mode = fattr->mode; /* restore mode */
	/*
	 * No need to worry about unhashing the dentry, as the
	 * lookup validation will know that the inode is bad.
	 * (But we fall through to invalidate the caches.)
	 */

out_invalid:
	/*
	 * Invalidate the local caches
	 */
#ifdef NFS_DEBUG_VERBOSE
printk("nfs_refresh_inode: invalidating %ld pages\n", inode->i_nrpages);
#endif
	if (!S_ISDIR(inode->i_mode)) {
		/* This sends off all dirty pages off to the server.
		 * Note that this function must not sleep. */
		nfs_invalidate_pages(inode);
		invalidate_inode_pages(inode);
	} else
		nfs_invalidate_dircache(inode);
	NFS_CACHEINV(inode);
	NFS_ATTRTIMEO(inode) = NFS_MINATTRTIMEO(inode);
	goto out;
}

/*
 * File system information
 */
static struct file_system_type nfs_fs_type = {
	"nfs",
	0 /* FS_NO_DCACHE - this doesn't work right now*/,
	nfs_read_super,
	NULL
};

/*
 * Initialize NFS
 */
int
init_nfs_fs(void)
{
#ifdef CONFIG_PROC_FS
	rpc_register_sysctl();
	rpc_proc_init();
	rpc_proc_register(&nfs_rpcstat);
#endif
        return register_filesystem(&nfs_fs_type);
}

/*
 * Every kernel module contains stuff like this.
 */
#ifdef MODULE

EXPORT_NO_SYMBOLS;
/* Not quite true; I just maintain it */
MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");

int
init_module(void)
{
	return init_nfs_fs();
}

void
cleanup_module(void)
{
#ifdef CONFIG_PROC_FS
	rpc_proc_unregister("nfs");
#endif
	unregister_filesystem(&nfs_fs_type);
	nfs_free_dircache();
}
#endif
