/*
 * NETLINK      Kernel-user communication protocol.
 *
 * 		Authors:	Alan Cox <alan@cymru.net>
 * 				Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 * 
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/fcntl.h>
#include <linux/termios.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <asm/uaccess.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <net/scm.h>

#define Nprintk(a...)

#if defined(CONFIG_NETLINK_DEV) || defined(CONFIG_NETLINK_DEV_MODULE)
#define NL_EMULATE_DEV
#endif

static struct sock *nl_table[MAX_LINKS];
static atomic_t nl_table_lock[MAX_LINKS];
static struct wait_queue *nl_table_wait;

#ifdef NL_EMULATE_DEV
static struct socket *netlink_kernel[MAX_LINKS];
#endif

static int netlink_dump(struct sock *sk);
static void netlink_destroy_callback(struct netlink_callback *cb);

extern __inline__ void
netlink_wait_on_table(int protocol)
{
	while (atomic_read(&nl_table_lock[protocol]))
		sleep_on(&nl_table_wait);
}

extern __inline__ void
netlink_lock_table(int protocol)
{
	atomic_inc(&nl_table_lock[protocol]);
}

extern __inline__ void
netlink_unlock_table(int protocol, int wakeup)
{
#if 0
	/* F...g gcc does not eat it! */

	if (atomic_dec_and_test(&nl_table_lock[protocol]) && wakeup)
		wake_up(&nl_table_wait);
#else
	atomic_dec(&nl_table_lock[protocol]);
	if (atomic_read(&nl_table_lock[protocol]) && wakeup)
		wake_up(&nl_table_wait);
#endif
}

static __inline__ void netlink_lock(struct sock *sk)
{
	atomic_inc(&sk->protinfo.af_netlink.locks);
}

static __inline__ void netlink_unlock(struct sock *sk)
{
	atomic_dec(&sk->protinfo.af_netlink.locks);
}

static __inline__ int netlink_locked(struct sock *sk)
{
	return atomic_read(&sk->protinfo.af_netlink.locks);
}

static __inline__ struct sock *netlink_lookup(int protocol, pid_t pid)
{
	struct sock *sk;

	for (sk=nl_table[protocol]; sk; sk=sk->next) {
		if (sk->protinfo.af_netlink.pid == pid) {
			netlink_lock(sk);
			return sk;
		}
	}

	return NULL;
}

extern struct proto_ops netlink_ops;

static void netlink_insert(struct sock *sk)
{
	cli();
	sk->next = nl_table[sk->protocol];
	nl_table[sk->protocol] = sk;
	sti();
}

static void netlink_remove(struct sock *sk)
{
	struct sock **skp;
	for (skp = &nl_table[sk->protocol]; *skp; skp = &((*skp)->next)) {
		if (*skp == sk) {
			*skp = sk->next;
			return;
		}
	}
}

static int netlink_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	sock->state = SS_UNCONNECTED;

	if (sock->type != SOCK_RAW && sock->type != SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;

	if (protocol<0 || protocol >= MAX_LINKS)
		return -EPROTONOSUPPORT;

	sock->ops = &netlink_ops;

	sk = sk_alloc(AF_NETLINK, GFP_KERNEL);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock,sk);
	sk->destruct = NULL;
	
	sk->mtu=4096;
	sk->protocol=protocol;
	return 0;
}

static void netlink_destroy_timer(unsigned long data)
{
	struct sock *sk=(struct sock *)data;

	if (!netlink_locked(sk) && !atomic_read(&sk->wmem_alloc)
	    && !atomic_read(&sk->rmem_alloc)) {
		sk_free(sk);
		return;
	}
	
	sk->timer.expires=jiffies+10*HZ;
	add_timer(&sk->timer);
	printk(KERN_DEBUG "netlink sk destroy delayed\n");
}

static int netlink_release(struct socket *sock, struct socket *peer)
{
	struct sock *sk = sock->sk;

	if (!sk)
		return 0;

	/* Wait on table before removing socket */
	netlink_wait_on_table(sk->protocol);
	netlink_remove(sk);

	if (sk->protinfo.af_netlink.cb) {
		netlink_unlock(sk);
		sk->protinfo.af_netlink.cb->done(sk->protinfo.af_netlink.cb);
		netlink_destroy_callback(sk->protinfo.af_netlink.cb);
		sk->protinfo.af_netlink.cb = NULL;
	}

	/* OK. Socket is unlinked, and, therefore,
	   no new packets will arrive */
	sk->state_change(sk);
	sk->dead = 1;

	skb_queue_purge(&sk->receive_queue);
	skb_queue_purge(&sk->write_queue);

	/* IMPORTANT! It is the major unpleasant feature of this
	   transport (and AF_UNIX datagram, when it will be repaired).
	   
	   Someone could wait on our sock->wait now.
	   We cannot release socket until waiter will remove yourself
	   from wait queue. I choose the most conservetive way of solving
	   the problem.

	   We waked up this queue above, so that we need only to wait
	   when the readers release us.
	 */

	while (netlink_locked(sk)) {
		current->counter = 0;
		schedule();
	}

	if (sk->socket)	{
		sk->socket = NULL;
		sock->sk = NULL;
	}

	if (atomic_read(&sk->rmem_alloc) || atomic_read(&sk->wmem_alloc)) {
		sk->timer.data=(unsigned long)sk;
		sk->timer.expires=jiffies+HZ;
		sk->timer.function=netlink_destroy_timer;
		add_timer(&sk->timer);
		printk(KERN_DEBUG "impossible 333\n");
		return 0;
	}

	sk_free(sk);
	return 0;
}

static int netlink_autobind(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct sock *osk;

	netlink_wait_on_table(sk->protocol);

	sk->protinfo.af_netlink.groups = 0;
	sk->protinfo.af_netlink.pid = current->pid;

retry:
	for (osk=nl_table[sk->protocol]; osk; osk=osk->next) {
		if (osk->protinfo.af_netlink.pid == sk->protinfo.af_netlink.pid) {
			/* Bind collision, search negative pid values. */
			if (sk->protinfo.af_netlink.pid > 0)
				sk->protinfo.af_netlink.pid = -4096;
			sk->protinfo.af_netlink.pid--;
			goto retry;
		}
	}

	netlink_insert(sk);
	return 0;
}

static int netlink_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct sock *osk;
	struct sockaddr_nl *nladdr=(struct sockaddr_nl *)addr;
	
	if (nladdr->nl_family != AF_NETLINK)
		return -EINVAL;

	/* Only superuser is allowed to listen multicasts */
	if (nladdr->nl_groups && !suser())
		return -EPERM;

	if (sk->protinfo.af_netlink.pid) {
		if (nladdr->nl_pid != sk->protinfo.af_netlink.pid)
			return -EINVAL;
		sk->protinfo.af_netlink.groups = nladdr->nl_groups;
		return 0;
	}

	if (nladdr->nl_pid == 0) {
		netlink_autobind(sock);
		sk->protinfo.af_netlink.groups = nladdr->nl_groups;
		return 0;
	}

	netlink_wait_on_table(sk->protocol);

	for (osk=nl_table[sk->protocol]; osk; osk=osk->next) {
		if (osk->protinfo.af_netlink.pid == nladdr->nl_pid)
			return -EADDRINUSE;
	}

	sk->protinfo.af_netlink.pid = nladdr->nl_pid;
	sk->protinfo.af_netlink.groups = nladdr->nl_groups;
	netlink_insert(sk);
	return 0;
}

static int netlink_connect(struct socket *sock, struct sockaddr *addr,
			   int alen, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_nl *nladdr=(struct sockaddr_nl*)addr;

	if (addr->sa_family == AF_UNSPEC)
	{
		sk->protinfo.af_netlink.dst_pid = 0;
		sk->protinfo.af_netlink.dst_groups = 0;
		return 0;
	}
	if (addr->sa_family != AF_NETLINK)
		return -EINVAL;

	/* Only superuser is allowed to send multicasts */
	if (!suser() && nladdr->nl_groups)
		return -EPERM;

	sk->protinfo.af_netlink.dst_pid = nladdr->nl_pid;
	sk->protinfo.af_netlink.dst_groups = nladdr->nl_groups;

	if (!sk->protinfo.af_netlink.pid)
		netlink_autobind(sock);
	return 0;
}

static int netlink_getname(struct socket *sock, struct sockaddr *addr, int *addr_len, int peer)
{
	struct sock *sk = sock->sk;
	struct sockaddr_nl *nladdr=(struct sockaddr_nl *)addr;
	
	nladdr->nl_family = AF_NETLINK;
	*addr_len = sizeof(*nladdr);

	if (peer) {
		nladdr->nl_pid = sk->protinfo.af_netlink.dst_pid;
		nladdr->nl_groups = sk->protinfo.af_netlink.dst_groups;
	} else {
		nladdr->nl_pid = sk->protinfo.af_netlink.pid;
		nladdr->nl_groups = sk->protinfo.af_netlink.groups;
	}
	return 0;
}

int netlink_unicast(struct sock *ssk, struct sk_buff *skb, pid_t pid, int nonblock)
{
	struct sock *sk;
	int len = skb->len;
	int protocol = ssk->protocol;

retry:
	for (sk = nl_table[protocol]; sk; sk = sk->next) {
		if (sk->protinfo.af_netlink.pid != pid)
				continue;

		netlink_lock(sk);

#ifdef NL_EMULATE_DEV
		if (sk->protinfo.af_netlink.handler) {
			len = sk->protinfo.af_netlink.handler(protocol, skb);
			netlink_unlock(sk);
			return len;
		}
#endif

		cli();
		if (atomic_read(&sk->rmem_alloc) > sk->rcvbuf) {
			if (nonblock) {
				sti();
				netlink_unlock(sk);
				kfree_skb(skb, 0);
				return -EAGAIN;
			}
			interruptible_sleep_on(sk->sleep);
			netlink_unlock(sk);
			sti();

			if (signal_pending(current)) {
				kfree_skb(skb, 0);
				return -ERESTARTSYS;
			}
			goto retry;
		}
		sti();
Nprintk("unicast_deliver %d\n", skb->len);
		skb_orphan(skb);
		skb_set_owner_r(skb, sk);
		skb_queue_tail(&sk->receive_queue, skb);
		sk->data_ready(sk, len);
		netlink_unlock(sk);
		return len;
	}
	kfree_skb(skb, 0);
	return -ECONNREFUSED;
}

static __inline__ int netlink_broadcast_deliver(struct sock *sk, struct sk_buff *skb)
{
#ifdef NL_EMULATE_DEV
	if (sk->protinfo.af_netlink.handler) {
		sk->protinfo.af_netlink.handler(sk->protocol, skb);
		return 0;
	} else
#endif
	if (atomic_read(&sk->rmem_alloc) <= sk->rcvbuf) {
Nprintk("broadcast_deliver %d\n", skb->len);
                skb_orphan(skb);
		skb_set_owner_r(skb, sk);
		skb_queue_tail(&sk->receive_queue, skb);
		sk->data_ready(sk, skb->len);
		return 0;
	}
	return -1;
}

void netlink_broadcast(struct sock *ssk, struct sk_buff *skb, pid_t pid,
		       unsigned group, int allocation)
{
	struct sock *sk;
	struct sk_buff *skb2 = NULL;
	int protocol = ssk->protocol;
	int failure = 0;

	/* While we sleep in clone, do not allow to change socket list */

	netlink_lock_table(protocol);

	for (sk = nl_table[protocol]; sk; sk = sk->next) {
		if (ssk == sk)
			continue;

		if (sk->protinfo.af_netlink.pid == pid ||
		    !(sk->protinfo.af_netlink.groups&group))
			continue;

		if (failure) {
			sk->err = -ENOBUFS;
			sk->state_change(sk);
			continue;
		}

		netlink_lock(sk);
		if (skb2 == NULL) {
			if (atomic_read(&skb->users) != 1) {
				skb2 = skb_clone(skb, allocation);
			} else {
				skb2 = skb;
				atomic_inc(&skb->users);
			}
		}
		if (skb2 == NULL) {
			sk->err = -ENOBUFS;
			sk->state_change(sk);
			/* Clone failed. Notify ALL listeners. */
			failure = 1;
		} else if (netlink_broadcast_deliver(sk, skb2)) {
			sk->err = -ENOBUFS;
			sk->state_change(sk);
		} else
			skb2 = NULL;
		netlink_unlock(sk);
	}

	netlink_unlock_table(protocol, allocation == GFP_KERNEL);

	if (skb2)
		kfree_skb(skb2, 0);
	kfree_skb(skb, 0);
}

void netlink_set_err(struct sock *ssk, pid_t pid, unsigned group, int code)
{
	struct sock *sk;
	int protocol = ssk->protocol;

Nprintk("seterr");
	for (sk = nl_table[protocol]; sk; sk = sk->next) {
		if (ssk == sk)
			continue;

		if (sk->protinfo.af_netlink.pid == pid ||
		    !(sk->protinfo.af_netlink.groups&group))
			continue;

		sk->err = -code;
		sk->state_change(sk);
	}
}

static int netlink_sendmsg(struct socket *sock, struct msghdr *msg, int len,
			   struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sockaddr_nl *addr=msg->msg_name;
	pid_t dst_pid;
	unsigned dst_groups;
	struct sk_buff *skb;
	int err;

	if (msg->msg_flags&MSG_OOB)
		return -EOPNOTSUPP;

	if (msg->msg_flags&~MSG_DONTWAIT) {
		printk("1 %08x\n", msg->msg_flags);
		return -EINVAL;
	}

	if (msg->msg_namelen) {
		if (addr->nl_family != AF_NETLINK) {
			printk("2 %08x\n", addr->nl_family);
			return -EINVAL;
		}
		dst_pid = addr->nl_pid;
		dst_groups = addr->nl_groups;
		if (dst_groups && !suser())
			return -EPERM;
	} else {
		dst_pid = sk->protinfo.af_netlink.dst_pid;
		dst_groups = sk->protinfo.af_netlink.dst_groups;
	}


	if (!sk->protinfo.af_netlink.pid)
		netlink_autobind(sock);

	skb = sock_wmalloc(sk, len, 0, GFP_KERNEL);
	if (skb==NULL)
		return -ENOBUFS;

	NETLINK_CB(skb).pid = sk->protinfo.af_netlink.pid;
	NETLINK_CB(skb).groups = sk->protinfo.af_netlink.groups;
	NETLINK_CB(skb).dst_pid = dst_pid;
	NETLINK_CB(skb).dst_groups = dst_groups;
	memcpy(NETLINK_CREDS(skb), &scm->creds, sizeof(struct ucred));
	memcpy_fromiovec(skb_put(skb,len), msg->msg_iov, len);

	if (dst_groups) {
		atomic_inc(&skb->users);
		netlink_broadcast(sk, skb, dst_pid, dst_groups, GFP_KERNEL);
	}
	err = netlink_unicast(sk, skb, dst_pid, msg->msg_flags&MSG_DONTWAIT);
	if (err < 0) {
		printk("3\n");
	}
	return err;
}

static int netlink_recvmsg(struct socket *sock, struct msghdr *msg, int len,
			   int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	int noblock = flags&MSG_DONTWAIT;
	int copied;
	struct sk_buff *skb;
	int err;

	if (flags&(MSG_OOB|MSG_PEEK))
		return -EOPNOTSUPP;

	err = -sock_error(sk);
	if (err)
		return err;

	skb = skb_recv_datagram(sk,flags,noblock,&err);
	if (skb==NULL)
 		return err;

	msg->msg_namelen = 0;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	skb->h.raw = skb->data;
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	if (msg->msg_name) {
		struct sockaddr_nl *addr = (struct sockaddr_nl*)msg->msg_name;
		addr->nl_family = AF_NETLINK;
		addr->nl_pid	= NETLINK_CB(skb).pid;
		addr->nl_groups	= NETLINK_CB(skb).dst_groups;
		msg->msg_namelen = sizeof(*addr);
	}

	scm->creds = *NETLINK_CREDS(skb);
	skb_free_datagram(sk, skb);

	if (sk->protinfo.af_netlink.cb
	    && atomic_read(&sk->rmem_alloc) <= sk->rcvbuf/2)
		netlink_dump(sk);
	return err ? err : copied;
}

/*
 *	We export these functions to other modules. They provide a 
 *	complete set of kernel non-blocking support for message
 *	queueing.
 */

struct sock *
netlink_kernel_create(int unit, void (*input)(struct sock *sk, int len))
{
	struct socket *sock;
	struct sock *sk;

	if (unit<0 || unit>=MAX_LINKS)
		return NULL;

	if (!(sock = sock_alloc())) 
		return NULL;

	sock->type = SOCK_RAW;

	if (netlink_create(sock, unit) < 0) {
		sock_release(sock);
		return NULL;
	}
	sk = sock->sk;
	if (input)
		sk->data_ready = input;

	netlink_insert(sk);
	return sk;
}

static void netlink_destroy_callback(struct netlink_callback *cb)
{
	if (cb->skb)
		kfree_skb(cb->skb, 0);
	kfree(cb);
}

/*
 * It looks a bit ugly.
 * It would be better to create kernel thread.
 */

static int netlink_dump(struct sock *sk)
{
	struct netlink_callback *cb;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int len;
	
	skb = sock_rmalloc(sk, NLMSG_GOODSIZE, 0, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;
	
	cb = sk->protinfo.af_netlink.cb;

	len = cb->dump(skb, cb);
	
	if (len > 0) {
		skb_queue_tail(&sk->receive_queue, skb);
		sk->data_ready(sk, len);
		return 0;
	}

	nlh = __nlmsg_put(skb, NETLINK_CB(cb->skb).pid, cb->nlh->nlmsg_seq, NLMSG_DONE, sizeof(int));
	nlh->nlmsg_flags |= NLM_F_MULTI;
	memcpy(NLMSG_DATA(nlh), &len, sizeof(len));
	skb_queue_tail(&sk->receive_queue, skb);
	sk->data_ready(sk, skb->len);
	
	cb->done(cb);
	sk->protinfo.af_netlink.cb = NULL;
	netlink_destroy_callback(cb);
	netlink_unlock(sk);
	return 0;
}

int netlink_dump_start(struct sock *ssk, struct sk_buff *skb,
		       struct nlmsghdr *nlh,
		       int (*dump)(struct sk_buff *skb, struct netlink_callback*),
		       int (*done)(struct netlink_callback*))
{
	struct netlink_callback *cb;
	struct sock *sk;

	cb = kmalloc(sizeof(*cb), GFP_KERNEL);
	if (cb == NULL)
		return -ENOBUFS;

	memset(cb, 0, sizeof(*cb));
	cb->dump = dump;
	cb->done = done;
	cb->nlh = nlh;
	atomic_inc(&skb->users);
	cb->skb = skb;

	sk = netlink_lookup(ssk->protocol, NETLINK_CB(skb).pid);
	if (sk == NULL) {
		netlink_destroy_callback(cb);
		return -ECONNREFUSED;
	}
	/* A dump is in progress... */
	if (sk->protinfo.af_netlink.cb) {
		netlink_destroy_callback(cb);
		netlink_unlock(sk);
		return -EBUSY;
	}
	sk->protinfo.af_netlink.cb = cb;
	netlink_dump(sk);
	return 0;
}

void netlink_ack(struct sk_buff *in_skb, struct nlmsghdr *nlh, int err)
{
	struct sk_buff *skb;
	struct nlmsghdr *rep;
	struct nlmsgerr *errmsg;
	int size;

	if (err == 0)
		size = NLMSG_SPACE(sizeof(struct nlmsgerr));
	else
		size = NLMSG_SPACE(4 + nlh->nlmsg_len);

	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return;
	
	rep = __nlmsg_put(skb, NETLINK_CB(in_skb).pid, nlh->nlmsg_seq,
			  NLMSG_ERROR, sizeof(struct nlmsgerr));
	errmsg = NLMSG_DATA(rep);
	errmsg->error = err;
	memcpy(&errmsg->msg, nlh, err ? nlh->nlmsg_len : sizeof(struct nlmsghdr));
	netlink_unicast(in_skb->sk, skb, NETLINK_CB(in_skb).pid, MSG_DONTWAIT);
}


#ifdef NL_EMULATE_DEV
/*
 *	Backward compatibility.
 */	
 
int netlink_attach(int unit, int (*function)(int, struct sk_buff *skb))
{
	struct sock *sk = netlink_kernel_create(unit, NULL);
	if (sk == NULL)
		return -ENOBUFS;
	sk->protinfo.af_netlink.handler = function;
	netlink_kernel[unit] = sk->socket;
	return 0;
}

void netlink_detach(int unit)
{
	struct socket *sock = netlink_kernel[unit];
	netlink_kernel[unit] = NULL;
	sock_release(sock);
}

int netlink_post(int unit, struct sk_buff *skb)
{
	if (netlink_kernel[unit]) {
		netlink_broadcast(netlink_kernel[unit]->sk, skb, 0, ~0, GFP_ATOMIC);
		return 0;
	}
	return -EUNATCH;;
}

EXPORT_SYMBOL(netlink_attach);
EXPORT_SYMBOL(netlink_detach);
EXPORT_SYMBOL(netlink_post);

#endif

#if 0

/* What a pity... It was good code, but at the moment it
   results in unnecessary complications.
 */

/*
 *	"High" level netlink interface. (ANK)
 *	
 *	Features:
 *		- standard message format.
 *		- pseudo-reliable delivery. Messages can be still lost, but
 *		  user level will know that they were lost and can
 *		  recover (f.e. gated could reread FIB and device list)
 *		- messages are batched.
 */

/*
 *	Try to deliver queued messages.
 */

static void nlmsg_delayed_flush(struct sock *sk)
{
	nlmsg_flush(sk, GFP_ATOMIC);
}

static void nlmsg_flush(struct sock *sk, int allocation)
{
	struct sk_buff *skb;
	unsigned long flags;

	save_flags(flags);
	cli();
	while ((skb=skb_dequeue(&sk->write_queue)) != NULL) {
		if (skb->users != 1) {
			skb_queue_head(&sk->write_queue, skb);
			break;
		}
		restore_flags(flags);
		netlink_broadcast(sk, skb, 0, NETLINK_CB(skb).dst_groups, allocation);
		cli();
	}
	start_bh_atomic();
	restore_flags(flags);
	if (skb) {
		if (sk->timer.function)
			del_timer(&sk->timer)
		sk->timer.expires = jiffies + (sk->protinfo.af_netlink.delay ? : HZ/2);
		sk->timer.function = (void (*)(unsigned long))nlmsg_delayed_flush;
		sk->timer.data = (unsigned long)sk;
		add_timer(&sk->timer);
	}
	end_bh_atomic();
}

/*
 *	Allocate room for new message. If it is impossible, return NULL.
 */

void *nlmsg_broadcast(struct sock *sk, struct sk_buff **skbp,
		      unsigned long type, int len,
		      unsigned groups, int allocation)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int	rlen;
	unsigned long flags;

	rlen = NLMSG_SPACE(len);

	save_flags(flags);
	cli();
	skb = sk->write_queue.tail;
	if (skb == sk->write_queue.head)
		skb = NULL;
	if (skb == NULL || skb_tailroom(skb) < rlen || NETLINK_CB(skb).dst_groups != groups) {
		restore_flags(flags);

		if (skb)
			nlmsg_flush(sk, allocation);

		skb = sock_wmalloc(rlen > NLMSG_GOODSIZE ? rlen : NLMSG_GOODSIZE,
				   sk, 0, allocation);

		if (skb==NULL) {
			printk (KERN_WARNING "nlmsg at unit %d overrunned\n", sk->protocol);
			return NULL;
		}

		NETLINK_CB(skb).dst_groups = groups;
		cli();
		skb_queue_tail(&sk->write_queue, skb);
	}
	atomic_inc(&skb->users);
	restore_flags(flags);

	nlh = (struct nlmsghdr*)skb_put(skb, rlen);
	nlh->nlmsg_type = type;
	nlh->nlmsg_len = NLMSG_LENGTH(len);
	nlh->nlmsg_seq = 0;
	nlh->nlmsg_pid = 0;
	*skbp = skb;
	return nlh->nlmsg_data;
}

struct sk_buff* nlmsg_alloc(unsigned long type, int len,
			    unsigned long seq, unsigned long pid, int allocation)
{
	struct nlmsghdr	*nlh;
	struct sk_buff *skb;
	int		rlen;

	rlen = NLMSG_SPACE(len);

	skb = alloc_skb(rlen, allocation);
	if (skb==NULL)
		return NULL;

	nlh = (struct nlmsghdr*)skb_put(skb, rlen);
	nlh->nlmsg_type = type;
	nlh->nlmsg_len = NLMSG_LENGTH(len);
	nlh->nlmsg_seq = seq;
	nlh->nlmsg_pid = pid;
	return skb;
}

void nlmsg_release(struct sk_buff *skb)
{
	atomic_dec(skb->users);
}


/*
 *	Kick message queue.
 *	Two modes:
 *		- synchronous (delay==0). Messages are delivered immediately.
 *		- delayed. Do not deliver, but start delivery timer.
 */

void __nlmsg_transmit(struct sock *sk, int allocation)
{
	start_bh_atomic();
	if (!sk->protinfo.af_netlink.delay) {
		if (sk->timer.function) {
			del_timer(&sk->timer);
			sk->timer.function = NULL;
		}
		end_bh_atomic();
		nlmsg_flush(sk, allocation);
		return;
	}
	if (!sk->timer.function) {
		sk->timer.expires = jiffies + sk->protinfo.af_netlink.delay;
		sk->timer.function = (void (*)(unsigned long))nlmsg_delayed_flush;
		sk->timer.data = (unsigned long)sk;
		add_timer(&sk->timer);
	}
	end_bh_atomic();
}

#endif

#ifdef CONFIG_PROC_FS
static int netlink_read_proc(char *buffer, char **start, off_t offset,
			     int length, int *eof, void *data)
{
	off_t pos=0;
	off_t begin=0;
	int len=0;
	int i;
	struct sock *s;
	
	len+= sprintf(buffer,"sk       Eth Pid    Groups   "
		      "Rmem     Wmem     Dump     Locks\n");
	
	for (i=0; i<MAX_LINKS; i++) {
		for (s = nl_table[i]; s; s = s->next) {
			len+=sprintf(buffer+len,"%p %-3d %-6d %08x %-8d %-8d %p %d",
				     s,
				     s->protocol,
				     s->protinfo.af_netlink.pid,
				     s->protinfo.af_netlink.groups,
				     atomic_read(&s->rmem_alloc),
				     atomic_read(&s->wmem_alloc),
				     s->protinfo.af_netlink.cb,
				     atomic_read(&s->protinfo.af_netlink.locks)
				     );

			buffer[len++]='\n';
		
			pos=begin+len;
			if(pos<offset) {
				len=0;
				begin=pos;
			}
			if(pos>offset+length)
				goto done;
		}
	}
	*eof = 1;

done:
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;
	return len;
}
#endif

struct proto_ops netlink_ops = {
	AF_NETLINK,

	sock_no_dup,
	netlink_release,
	netlink_bind,
	netlink_connect,
	NULL,
	NULL,
	netlink_getname,
	datagram_poll,
	sock_no_ioctl,
	sock_no_listen,
	sock_no_shutdown,
	NULL,
	NULL,
	sock_no_fcntl,
	netlink_sendmsg,
	netlink_recvmsg
};

struct net_proto_family netlink_family_ops = {
	AF_NETLINK,
	netlink_create
};

void netlink_proto_init(struct net_proto *pro)
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *ent;
#endif
	struct sk_buff *dummy_skb;

	if (sizeof(struct netlink_skb_parms) > sizeof(dummy_skb->cb)) {
		printk(KERN_CRIT "netlink_proto_init: panic\n");
		return;
	}
	sock_register(&netlink_family_ops);
#ifdef CONFIG_PROC_FS
	ent = create_proc_entry("net/netlink", 0, 0);
	ent->read_proc = netlink_read_proc;
#endif
}
