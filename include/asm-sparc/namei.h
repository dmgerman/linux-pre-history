/* $Id: namei.h,v 1.10 1997/09/24 16:20:36 jj Exp $
 * linux/include/asm-sparc/namei.h
 *
 * Routines to handle famous /usr/gnemul/s*.
 * Included from linux/fs/namei.c
 */

#ifndef __SPARC_NAMEI_H
#define __SPARC_NAMEI_H

#define SPARC_BSD_EMUL "usr/gnemul/sunos/"
#define SPARC_SOL_EMUL "usr/gnemul/solaris/"

static inline struct dentry *
__sparc_lookup_dentry(const char *name, int follow_link)
{
	struct dentry *base;
	char *emul;

	switch (current->personality) {
	case PER_BSD:
		emul = SPARC_BSD_EMUL; break;
	case PER_SVR4:
		emul = SPARC_SOL_EMUL; break;
	default:
		return NULL;
	}

	base = lookup_dentry (emul, dget (current->fs->root), 1);
			
	if (IS_ERR (base)) return NULL;
	
	base = lookup_dentry (name, base, follow_link);
	
	if (IS_ERR (base)) return NULL;
	
	if (!base->d_inode) {
		struct dentry *fromroot;
		
		fromroot = lookup_dentry (name, dget (current->fs->root), follow_link);
		
		if (IS_ERR (fromroot)) return base;
		
		if (fromroot->d_inode) {
			dput(base);
			return fromroot;
		}
		
		dput(fromroot);
	}
	
	return base;
}

#define __prefix_lookup_dentry(name, follow_link)				\
	if (current->personality) {						\
		dentry = __sparc_lookup_dentry (name, follow_link);		\
		if (dentry) return dentry;					\
	}

#endif /* __SPARC_NAMEI_H */
