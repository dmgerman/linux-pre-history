/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		AF_INET protocol family socket handler.
 *
 * Version:	@(#)af_inet.c	(from sock.c) 1.0.17	06/02/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Alan Cox, <A.Cox@swansea.ac.uk>
 *
 * Changes (see also sock.c)
 *
 *		A.N.Kuznetsov	:	Socket death error in accept().
 *		John Richardson :	Fix non blocking error in connect()
 *					so sockets that fail to connect
 *					don't return -EINPROGRESS.
 *		Alan Cox	:	Asynchronous I/O support
 *		Alan Cox	:	Keep correct socket pointer on sock structures
 *					when accept() ed
 *		Alan Cox	:	Semantics of SO_LINGER aren't state moved
 *					to close when you look carefully. With
 *					this fixed and the accept bug fixed 
 *					some RPC stuff seems happier.
 *		Niibe Yutaka	:	4.4BSD style write async I/O
 *		Alan Cox, 
 *		Tony Gale 	:	Fixed reuse semantics.
 *		Alan Cox	:	bind() shouldn't abort existing but dead
 *					sockets. Stops FTP netin:.. I hope.
 *		Alan Cox	:	bind() works correctly for RAW sockets. Note
 *					that FreeBSD at least was broken in this respect
 *					so be careful with compatibility tests...
 *		Alan Cox	:	routing cache support
 *		Alan Cox	:	memzero the socket structure for compactness.
 *		Matt Day	:	nonblock connect error handler
 *		Alan Cox	:	Allow large numbers of pending sockets
 *					(eg for big web sites), but only if
 *					specifically application requested.
 *		Alan Cox	:	New buffering throughout IP. Used dumbly.
 *		Alan Cox	:	New buffering now used smartly.
 *		Alan Cox	:	BSD rather than common sense interpretation of
 *					listen.
 *		Germano Caronni	:	Assorted small races.
 *		Alan Cox	:	sendmsg/recvmsg basic support.
 *		Alan Cox	:	Only sendmsg/recvmsg now supported.
 *		Alan Cox	:	Locked down bind (see security list).
 *		Alan Cox	:	Loosened bind a little.
 *		Mike McLagan	:	ADD/DEL DLCI Ioctls
 *	Willy Konynenberg	:	Transparent proxying support.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/rarp.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/raw.h>
#include <net/icmp.h>
#include <net/inet_common.h>
#include <linux/ip_fw.h>
#ifdef CONFIG_IP_MASQUERADE
#include <net/ip_masq.h>
#endif
#ifdef CONFIG_IP_ALIAS
#include <net/ip_alias.h>
#endif
#ifdef CONFIG_BRIDGE
#include <net/br.h>
#endif
#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif
#ifdef CONFIG_NET_RADIO
#include <linux/wireless.h>
#endif	/* CONFIG_NET_RADIO */

#define min(a,b)	((a)<(b)?(a):(b))

extern struct proto packet_prot;
extern int raw_get_info(char *, char **, off_t, int, int);
extern int snmp_get_info(char *, char **, off_t, int, int);
extern int afinet_get_info(char *, char **, off_t, int, int);
extern int tcp_get_info(char *, char **, off_t, int, int);
extern int udp_get_info(char *, char **, off_t, int, int);


struct sock * tcp_sock_array[SOCK_ARRAY_SIZE];
struct sock * udp_sock_array[SOCK_ARRAY_SIZE];
struct sock * raw_sock_array[SOCK_ARRAY_SIZE];

#ifdef CONFIG_DLCI
extern int dlci_ioctl(unsigned int, void*);
#endif

#ifdef CONFIG_DLCI_MODULE
int (*dlci_ioctl_hook)(unsigned int, void *) = NULL;
#endif

int (*rarp_ioctl_hook)(unsigned int,void*) = NULL;

/*
 *	See if a socket number is in use.
 */
 
static int sk_inuse(struct proto *prot, int num)
{
	struct sock *sk;

	for(sk = prot->sock_array[num & (SOCK_ARRAY_SIZE -1 )];
		sk != NULL;  sk=sk->next) 
	{
		if (sk->num == num) 
			return(1);
	}
	return(0);
}


/*
 *	Pick a new socket number
 */

unsigned short get_new_socknum(struct proto *prot, unsigned short base)
{
	static int start=0;

	/*
	 * Used to cycle through the port numbers so the
	 * chances of a confused connection drop.
	 */
	 
	int i, j;
	int best = 0;
	int size = 32767; /* a big num. */
	struct sock *sk;

	if (base == 0) 
		base = PROT_SOCK+1+(start & 1023);
	if (base <= PROT_SOCK) 
	{
		base += PROT_SOCK+(start & 1023);
	}

	/*
	 *	Now look through the entire array and try to find an empty ptr. 
	 */
	 
	for(i=0; i < SOCK_ARRAY_SIZE; i++) 
	{
		j = 0;
		sk = prot->sock_array[(i+base+1) &(SOCK_ARRAY_SIZE -1)];
		while(sk != NULL) 
		{
			sk = sk->next;
			j++;
		}
		if (j == 0) 
		{
			start =(i+1+start )&1023;
			return(i+base+1);
		}
		if (j < size) 
		{
			best = i;
			size = j;
		}
	}

	/* Now make sure the one we want is not in use. */

	while(sk_inuse(prot, base +best+1)) 
	{
		best += SOCK_ARRAY_SIZE;
	}
	return(best+base+1);
}

/*
 *	Add a socket into the socket tables by number.
 */

void inet_put_sock(unsigned short num, struct sock *sk)
{
	struct sock **skp, *tmp;
	int mask;
	unsigned long flags;
	
	if(sk->type==SOCK_PACKET)
		return;

	sk->num = num;
	sk->next = NULL;
	num = num &(SOCK_ARRAY_SIZE -1);

	/* 
	 *	We can't have an interrupt re-enter here. 
	 */
	 
	save_flags(flags);
	cli();

	sk->prot->inuse += 1;
	if (sk->prot->highestinuse < sk->prot->inuse)
		sk->prot->highestinuse = sk->prot->inuse;

	if (sk->prot->sock_array[num] == NULL) 
	{
		sk->prot->sock_array[num] = sk;
		restore_flags(flags);
		return;
	}
	
	restore_flags(flags);
	for(mask = 0xff000000; mask != 0xffffffff; mask = (mask >> 8) | mask) 
	{
		if ((mask & sk->rcv_saddr) &&
		    (mask & sk->rcv_saddr) != (mask & 0xffffffff)) 
		{
			mask = mask << 8;
			break;
		}
	}

	/*
	 * add the socket to the sock_array[]..
	 */
	skp = sk->prot->sock_array + num;
	cli();
	while ((tmp = *skp) != NULL) {
		if (!(tmp->rcv_saddr & mask))
			break;
		skp = &tmp->next;
	}
	sk->next = tmp;
	*skp = sk;
	sti();
}

/*
 *	Remove a socket from the socket tables.
 */

void inet_remove_sock(struct sock *sk1)
{
	struct sock **p;
	unsigned long flags;

	if (sk1->type==SOCK_PACKET)
		return;
		
	if (!sk1->prot) 
	{
		NETDEBUG(printk("sock.c: remove_sock: sk1->prot == NULL\n"));
		return;
	}

	/* We can't have this changing out from under us. */
	save_flags(flags);
	cli();
	
	p=&(sk1->prot->sock_array[sk1->num & (SOCK_ARRAY_SIZE -1)]);
	
	while(*p!=NULL)
	{
		if(*p==sk1)
		{
			sk1->prot->inuse--;
			*p=sk1->next;
			break;
		}
		p=&((*p)->next);
	}
	restore_flags(flags);
}

/*
 *	Destroy an AF_INET socket
 */
 
void destroy_sock(struct sock *sk)
{
	struct sk_buff *skb;

	lock_sock(sk);			/* just to be safe. */

  	/*
  	 *	Now we can no longer get new packets or once the
  	 *	timers are killed, send them.
  	 */
  	 
  	net_delete_timer(sk);

	if (sk->prot->destroy)
		sk->prot->destroy(sk);

  	/*
  	 *	Clean up the read buffer.
  	 */

	while((skb=skb_dequeue(&sk->receive_queue))!=NULL) 
	{
		/*
		 * This will take care of closing sockets that were
		 * listening and didn't accept everything.
		 */
		if (skb->sk != NULL && skb->sk != sk) 
		{
			IS_SKB(skb);
			skb->sk->prot->close(skb->sk, 0);
		}
		IS_SKB(skb);
		kfree_skb(skb, FREE_READ);
	}

  	/*
  	 *	Clean up the error queue.
  	 */

	while((skb=skb_dequeue(&sk->error_queue))!=NULL) 
	{
		IS_SKB(skb);
		kfree_skb(skb, FREE_READ);
	}

  	/*
  	 *	Now the backlog. 
  	 */
  	 
  	while((skb=skb_dequeue(&sk->back_log))!=NULL) 
  	{
		IS_SKB(skb);
		kfree_skb(skb, FREE_READ);
	}

	/*
	 *	Now if it has a half accepted/ closed socket. 
	 */
	 
	if (sk->pair) 
	{
		sk->pair->prot->close(sk->pair, 0);
		sk->pair = NULL;
  	}

	/*
	 * Now if everything is gone we can free the socket
	 * structure, otherwise we need to keep it around until
	 * everything is gone.
	 */

	if (sk->rmem_alloc == 0 && sk->wmem_alloc == 0) 
	{
/*
 *	It is wrong! We MUST unlink socket from socket table
 *	even earlier, than it used to be.
 *	F.e. TCP socket must be unlinked at the moment, when
 *	it goes to TCP_CLOSE. --ANK
 */
	    	inet_remove_sock(sk);

		if(sk->opt)
			kfree(sk->opt);
		dst_release(sk->dst_cache);
		/*
		 *	This one is pure paranoia. I'll take it out
		 *	later once I know the bug is buried.
		 */
		tcp_cache_zap();
		sk_free(sk);
	} 
	else 
	{
		/* this should never happen. */
		/* actually it can if an ack has just been sent. */
		/* 
		 * It's more normal than that...
		 * It can happen because a skb is still in the device queues
		 * [PR]
		 */
		  
		printk("Socket destroy delayed (r=%d w=%d)\n",
 			sk->rmem_alloc, sk->wmem_alloc);

		sk->destroy = 1;
		sk->ack_backlog = 0;
		release_sock(sk);
		net_reset_timer(sk, TIME_DESTROY, SOCK_DESTROY_TIME);
  	}
}

/*
 *	The routines beyond this point handle the behaviour of an AF_INET
 *	socket object. Mostly it punts to the subprotocols of IP to do
 *	the work.
 */
 

/*
 *	Set socket options on an inet socket.
 */
 
int inet_setsockopt(struct socket *sock, int level, int optname,
		    char *optval, int optlen)
{
	struct sock *sk=sock->sk;
	if (sk->prot->setsockopt==NULL)
		return(-EOPNOTSUPP);
	return sk->prot->setsockopt(sk,level,optname,optval,optlen);
}

/*
 *	Get a socket option on an AF_INET socket.
 *
 *	FIX: POSIX 1003.1g is very ambiguous here. It states that
 *	asynchronous errors should be reported by getsockopt. We assume
 *	this means if you specify SO_ERROR (otherwise whats the point of it).
 */

int inet_getsockopt(struct socket *sock, int level, int optname,
		    char *optval, int *optlen)
{
	struct sock *sk=sock->sk;
	if (sk->prot->getsockopt==NULL)
		return(-EOPNOTSUPP);
	return sk->prot->getsockopt(sk,level,optname,optval,optlen);
}

/*
 *	Automatically bind an unbound socket.
 */

static int inet_autobind(struct sock *sk)
{
	/* We may need to bind the socket. */
	if (sk->num == 0) 
	{
		sk->num = get_new_socknum(sk->prot, 0);
		if (sk->num == 0) 
			return(-EAGAIN);
		udp_cache_zap();
		tcp_cache_zap();
		inet_put_sock(sk->num, sk);
		sk->dummy_th.source = ntohs(sk->num);
	}
	return 0;
}

/*
 *	Move a socket into listening state.
 */
 
int inet_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;

	if (sock->state != SS_UNCONNECTED || sock->type != SOCK_STREAM)
		return(-EINVAL);

	if (inet_autobind(sk)!=0)
		return -EAGAIN;

	/* We might as well re use these. */ 
	/*
	 * note that the backlog is "unsigned char", so truncate it
	 * somewhere. We might as well truncate it to what everybody
	 * else does..
	 * Now truncate to 128 not 5. 
	 */
	if ((unsigned) backlog == 0)	/* BSDism */
		backlog = 1;
	if ((unsigned) backlog > SOMAXCONN)
		backlog = SOMAXCONN;
	sk->max_ack_backlog = backlog;
	if (sk->state != TCP_LISTEN)
	{
		sk->ack_backlog = 0;
		sk->state = TCP_LISTEN;
	}
	sk->socket->flags |= SO_ACCEPTCON;
	return(0);
}

/*
 *	Create an inet socket.
 *
 *	FIXME: Gcc would generate much better code if we set the parameters
 *	up in in-memory structure order. Gcc68K even more so
 */

static int inet_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	struct proto *prot;
	int err;

	sock->state = SS_UNCONNECTED;
	sk = sk_alloc(GFP_KERNEL);
	if (sk == NULL) 
		return(-ENOBUFS);

	/*
	 *	Note for tcp that also wiped the dummy_th block for us.
	 */
	switch (sock->type) 
	{
		case SOCK_STREAM:
		case SOCK_SEQPACKET:
			if (protocol && protocol != IPPROTO_TCP) 
			{
				sk_free(sk);
				return(-EPROTONOSUPPORT);
			}
			protocol = IPPROTO_TCP;
			sk->no_check = TCP_NO_CHECK;
			if (ipv4_config.no_pmtu_disc)
				sk->ip_pmtudisc = IP_PMTUDISC_DONT;
			else
				sk->ip_pmtudisc = IP_PMTUDISC_WANT;
			prot = &tcp_prot;
			sock->ops = &inet_stream_ops;
			break;

		case SOCK_DGRAM:
			if (protocol && protocol != IPPROTO_UDP) 
			{
				sk_free(sk);
				return(-EPROTONOSUPPORT);
			}
			protocol = IPPROTO_UDP;
			sk->no_check = UDP_NO_CHECK;
			sk->ip_pmtudisc = IP_PMTUDISC_DONT;
			prot=&udp_prot;
			sock->ops = &inet_dgram_ops;
			break;
      
		case SOCK_RAW:
			if (!suser()) 
			{
				sk_free(sk);
				return(-EPERM);
			}
			if (!protocol) 
			{
				sk_free(sk);
				return(-EPROTONOSUPPORT);
			}
			prot = &raw_prot;
			sk->reuse = 1;
			sk->ip_pmtudisc = IP_PMTUDISC_DONT;
			sk->num = protocol;
			sock->ops = &inet_dgram_ops;
			break;

		case SOCK_PACKET:
			if (!suser()) 
			{
				sk_free(sk);
				return(-EPERM);
			}
			if (!protocol) 
			{
				sk_free(sk);
				return(-EPROTONOSUPPORT);
			}
			prot = &packet_prot;
			sk->reuse = 1;
			sk->ip_pmtudisc = IP_PMTUDISC_DONT;
			sk->num = protocol;
			sock->ops = &inet_dgram_ops;
			break;

		default:
			sk_free(sk);
			return(-ESOCKTNOSUPPORT);
	}

	sock_init_data(sock,sk);
	
	sk->zapped=0;
#ifdef CONFIG_TCP_NAGLE_OFF
	sk->nonagle = 1;
#endif  
	sk->family = AF_INET;
	sk->protocol = protocol;

	sk->prot = prot;
	sk->backlog_rcv = prot->backlog_rcv;

	sk->timer.data = (unsigned long)sk;
	sk->timer.function = &net_timer;

	sk->ip_ttl=ip_statistics.IpDefaultTTL;

	if(sk->type==SOCK_RAW && protocol==IPPROTO_RAW)
		sk->ip_hdrincl=1;
	else
		sk->ip_hdrincl=0;

	sk->ip_mc_loop=1;
	sk->ip_mc_ttl=1;
	sk->ip_mc_index=0;
	sk->ip_mc_list=NULL;
	
	/*
	 *	Speed up by setting some standard state for the dummy_th
	 *	if TCP uses it (maybe move to tcp_init later)
	 */
  	
	if (sk->num) 
	{
		/*
		 * It assumes that any protocol which allows
		 * the user to assign a number at socket
		 * creation time automatically
		 * shares.
		 */
		inet_put_sock(sk->num, sk);
		sk->dummy_th.source = ntohs(sk->num);
	}

	if (sk->prot->init) 
	{
		err = sk->prot->init(sk);
		if (err != 0) 
		{
			destroy_sock(sk);
			return(err);
		}
	}
	return(0);
}


/*
 *	Duplicate a socket.
 */
 
static int inet_dup(struct socket *newsock, struct socket *oldsock)
{
	return inet_create(newsock, oldsock->sk->protocol);
}

/*
 *	The peer socket should always be NULL (or else). When we call this
 *	function we are destroying the object and from then on nobody
 *	should refer to it.
 */
 
int inet_release(struct socket *sock, struct socket *peersock)
{
	struct sock *sk = sock->sk;
	unsigned long timeout;

	if (sk==NULL)
		return 0;

	if (sock->state != SS_UNCONNECTED)
		sock->state = SS_DISCONNECTING;

	sk->state_change(sk);

	/* Start closing the connection.  This may take a while. */

	/* Applications forget to leave groups before exiting */
	ip_mc_drop_socket(sk);

	/*
	 * If linger is set, we don't return until the close
	 * is complete.  Otherwise we return immediately. The
	 * actually closing is done the same either way.
	 *
	 * If the close is due to the process exiting, we never
	 * linger..
	 */
	timeout = 0;
	if (sk->linger)
	{
		timeout = ~0UL;
		if (!sk->lingertime)
			timeout = jiffies + HZ*sk->lingertime;
	}
	if (current->flags & PF_EXITING)
		timeout = 0;

	sock->sk = NULL;
	sk->socket = NULL;

	sk->prot->close(sk, timeout);
	return(0);
}


static int inet_bind(struct socket *sock, struct sockaddr *uaddr,
		     int addr_len)
{
	struct sockaddr_in *addr=(struct sockaddr_in *)uaddr;
	struct sock *sk=sock->sk, *sk2;
	unsigned short snum = 0 /* Stoopid compiler.. this IS ok */;
	int chk_addr_ret;

	/*
	 *	If the socket has its own bind function then use it.
	 */
	 
	if(sk->prot->bind)
		return sk->prot->bind(sk,uaddr, addr_len);
		
	/* check this error. */
	if (sk->state != TCP_CLOSE)
		return(-EINVAL);
	if(addr_len<sizeof(struct sockaddr_in))
		return -EINVAL;
		
	if (sock->type != SOCK_RAW)
	{
		if (sk->num != 0) 
			return(-EINVAL);

		snum = ntohs(addr->sin_port);
		
#ifdef CONFIG_IP_MASQUERADE
		/*
		 *	The kernel masquerader needs some ports
		 */		
		if(snum>=PORT_MASQ_BEGIN && snum<=PORT_MASQ_END)
			return -EADDRINUSE;
#endif		 

		if (snum == 0) 
			snum = get_new_socknum(sk->prot, 0);
		if (snum < PROT_SOCK && !suser()) 
			return(-EACCES);
	}
	
	chk_addr_ret = __ip_chk_addr(addr->sin_addr.s_addr);
#ifdef CONFIG_IP_TRANSPARENT_PROXY
	/*
	 * Superuser may bind to any address to allow transparent proxying.
	 */
	if (addr->sin_addr.s_addr != 0 && chk_addr_ret != IS_MYADDR && chk_addr_ret != IS_MULTICAST && chk_addr_ret != IS_BROADCAST && !suser())
#else
	if (addr->sin_addr.s_addr != 0 && chk_addr_ret != IS_MYADDR && chk_addr_ret != IS_MULTICAST && chk_addr_ret != IS_BROADCAST)
#endif
		return(-EADDRNOTAVAIL);	/* Source address MUST be ours! */

#ifndef CONFIG_IP_TRANSPARENT_PROXY
	/*
	 * Am I just thick or is this test really always true after the one
	 * above?  Just taking the test out appears to be the easiest way to
	 * make binds to remote addresses for transparent proxying work.
	 */
	if (chk_addr_ret || addr->sin_addr.s_addr == 0)
	{
#endif
		/*
		 *      We keep a pair of addresses. rcv_saddr is the one
		 *      used by get_sock_*(), and saddr is used for transmit.
		 *
		 *      In the BSD API these are the same except where it
		 *      would be illegal to use them (multicast/broadcast) in
		 *      which case the sending device address is used.
		 */
		sk->rcv_saddr = addr->sin_addr.s_addr;
		if(chk_addr_ret==IS_MULTICAST||chk_addr_ret==IS_BROADCAST)
			sk->saddr = 0;  /* Use device */
		else
			sk->saddr = addr->sin_addr.s_addr;
#ifndef CONFIG_IP_TRANSPARENT_PROXY
	}
#endif
	if (sock->type != SOCK_RAW)
	{
		/* Make sure we are allowed to bind here. */
		cli();
		for(sk2 = sk->prot->sock_array[snum & (SOCK_ARRAY_SIZE -1)];
					sk2 != NULL; sk2 = sk2->next) 
		{
			/*
			 *	Hash collision or real match ?
			 */
			 
			if (sk2->num != snum) 
				continue;
				
			/*
			 *	Either bind on the port is wildcard means
			 *	they will overlap and thus be in error
			 */			
			 
			if (!sk2->rcv_saddr || !sk->rcv_saddr)
			{
				/*
				 *	Allow only if both are setting reuse.
				 */
				if(sk2->reuse && sk->reuse && sk2->state!=TCP_LISTEN)
					continue;
				sti();
				return(-EADDRINUSE);
			}

			/*
			 *	Two binds match ?
			 */

			if (sk2->rcv_saddr != sk->rcv_saddr) 
				continue;
			/*
			 *	Reusable port ?
			 */

			if (!sk->reuse)
			{
				sti();
				return(-EADDRINUSE);
			}
			
			/*
			 *	Reuse ?
			 */
			 
			if (!sk2->reuse || sk2->state==TCP_LISTEN)
			{
				sti();
				return(-EADDRINUSE);
			}
		}
		sti();

		inet_remove_sock(sk);
		if (sock->type==SOCK_DGRAM)
			udp_cache_zap();
		if (sock->type==SOCK_STREAM)
			tcp_cache_zap();
		inet_put_sock(snum, sk);
		sk->dummy_th.source = ntohs(sk->num);
		sk->daddr = 0;
		sk->dummy_th.dest = 0;
	}
	dst_release(sk->dst_cache);
	sk->dst_cache=NULL;
	return(0);
}

int inet_dgram_connect(struct socket *sock, struct sockaddr * uaddr,
		       int addr_len, int flags)
{
	struct sock *sk=sock->sk;
	int err;

	if (inet_autobind(sk)!=0)
		return(-EAGAIN);
	if (sk->prot->connect == NULL) 
		return(-EOPNOTSUPP);
	err = sk->prot->connect(sk, (struct sockaddr *)uaddr, addr_len);
	if (err < 0) 
		return(err);
	return(0);
}

/*
 *	Connect to a remote host. There is regrettably still a little
 *	TCP 'magic' in here.
 */
 
int inet_stream_connect(struct socket *sock, struct sockaddr * uaddr,
			int addr_len, int flags)
{
	struct sock *sk=sock->sk;
	int err;

	switch (sock->state)
	{
		case SS_UNCONNECTED:
			/* This is ok... continue with connect */
			break;
		case SS_CONNECTED:
			/* Socket is already connected */
			return -EISCONN;
		case SS_CONNECTING:
			/* Not yet connected... we will check this. */
		
			/*
			 *	FIXME:  for all protocols what happens if you start
			 *	an async connect fork and both children connect. Clean
			 *	this up in the protocols!
			 */
			break;
		default:
			return(-EINVAL);
	}

	if (sock->state == SS_CONNECTING && tcp_connected(sk->state))
	{
		sock->state = SS_CONNECTED;
		/* Connection completing after a connect/EINPROGRESS/select/connect */
		return 0;	/* Rock and roll */
	}

	if (sock->state == SS_CONNECTING && sk->protocol == IPPROTO_TCP && (flags & O_NONBLOCK))
	{
		if(sk->err!=0)
			return sock_error(sk);
		return -EALREADY;	/* Connecting is currently in progress */
  	}
	if (sock->state != SS_CONNECTING) 
	{
		/* We may need to bind the socket. */
		if (inet_autobind(sk)!=0)
			return(-EAGAIN);
		if (sk->prot->connect == NULL) 
			return(-EOPNOTSUPP);
		err = sk->prot->connect(sk, uaddr, addr_len);
		if (err < 0) 
			return(err);
  		sock->state = SS_CONNECTING;
	}
	
	if (sk->state > TCP_FIN_WAIT2 && sock->state==SS_CONNECTING)
	{
		sock->state=SS_UNCONNECTED;
		return sock_error(sk);
	}

	if (sk->state != TCP_ESTABLISHED && (flags & O_NONBLOCK)) 
	  	return (-EINPROGRESS);

	cli(); /* avoid the race condition */
	while(sk->state == TCP_SYN_SENT || sk->state == TCP_SYN_RECV) 
	{
		interruptible_sleep_on(sk->sleep);
		if (current->signal & ~current->blocked) 
		{
			sti();
			return(-ERESTARTSYS);
		}
		/* This fixes a nasty in the tcp/ip code. There is a hideous hassle with
		   icmp error packets wanting to close a tcp or udp socket. */
		if (sk->err && sk->protocol == IPPROTO_TCP)
		{
			sock->state = SS_UNCONNECTED;
			sti();
			return sock_error(sk); /* set by tcp_err() */
		}
	}
	sti();
	sock->state = SS_CONNECTED;

	if (sk->state != TCP_ESTABLISHED && sk->err) 
	{
		sock->state = SS_UNCONNECTED;
		return sock_error(sk);
	}
	return(0);
}

/*
 *	Accept a pending connection. The TCP layer now gives BSD semantics.
 */

int inet_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk1 = sock->sk;
	struct sock *newsk = newsock->sk;
	struct sock *sk2;
	int err;

	if (sock->state != SS_UNCONNECTED)
		return -EINVAL;
	if (!(sock->flags & SO_ACCEPTCON)) 
		return -EINVAL;
	if (sk1->prot->accept == NULL) 
		return -EOPNOTSUPP;

	/*
	 *	Restore the state if we have been interrupted, and then returned. 
	 */
	 
	if (sk1->pair != NULL ) 
	{
		sk2 = sk1->pair;
		sk1->pair = NULL;
	} 
	else
	{
		sk2 = sk1->prot->accept(sk1,flags);
		if (sk2 == NULL) 
			return sock_error(sk1);
	}

	/*
	 *	We've been passed an extra socket.
	 *	We need to free it up because the tcp module creates
	 *	its own when it accepts one.
	 */

	sk2->sleep = newsk->sleep;

	newsock->sk = sk2;
	sk2->socket = newsock;
	newsk->socket = NULL;

	if (flags & O_NONBLOCK)
	{
		destroy_sock(newsk);
		return(0);
	}

	cli(); /* avoid the race. */
	while (sk2->state == TCP_SYN_RECV) 
	{
		interruptible_sleep_on(sk2->sleep);
		if (current->signal & ~current->blocked) 
		{
			sti();
			sk1->pair = sk2;
			sk2->sleep = NULL;
			sk2->socket = NULL;

			newsock->sk = newsk;
			newsk->socket = newsock;
			return -ERESTARTSYS;
		}
	}
	sti();

	if (sk2->state != TCP_ESTABLISHED && sk2->err > 0) 
	{
		err = sock_error(sk2);
		sk2->sleep = NULL;
		sk2->socket = NULL;
		destroy_sock(sk2);

		newsock->sk = newsk;
		newsk->socket = newsock;

		return err;
	}
	if (sk2->state == TCP_CLOSE)
	{
		sk2->sleep = NULL;
		sk2->socket = NULL;
		destroy_sock(sk2);

		newsock->sk = newsk;
		newsk->socket = newsock;

		return -ECONNABORTED;
	}

	destroy_sock(newsk);
	newsock->state = SS_CONNECTED;
	return(0);
}


/*
 *	This does both peername and sockname.
 */
 
static int inet_getname(struct socket *sock, struct sockaddr *uaddr,
		 int *uaddr_len, int peer)
{
	struct sock *sk=sock->sk;
	struct sockaddr_in *sin=(struct sockaddr_in *)uaddr;
  
	sin->sin_family = AF_INET;
	if (peer) 
	{
		if (!tcp_connected(sk->state)) 
			return(-ENOTCONN);
		sin->sin_port = sk->dummy_th.dest;
		sin->sin_addr.s_addr = sk->daddr;
	} 
	else 
	{
		__u32 addr = sk->rcv_saddr;
		if (!addr)
			addr = sk->saddr;
		sin->sin_port = sk->dummy_th.source;
		sin->sin_addr.s_addr = addr;
	}
	*uaddr_len = sizeof(*sin);
	return(0);
}



int inet_recvmsg(struct socket *sock, struct msghdr *msg, int size,
		 int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	int addr_len = 0;
	int err;
	
	if (sock->flags & SO_ACCEPTCON)
		return(-EINVAL);
	if (sk->prot->recvmsg == NULL) 
		return(-EOPNOTSUPP);
	if (sk->err)
		return sock_error(sk);
	/* We may need to bind the socket. */
	if (inet_autobind(sk)!=0)
		return(-EAGAIN);
	err = sk->prot->recvmsg(sk, msg, size, flags&MSG_DONTWAIT,
				flags&~MSG_DONTWAIT, &addr_len);
	if (err >= 0)
		msg->msg_namelen = addr_len;
	return err;
}


int inet_sendmsg(struct socket *sock, struct msghdr *msg, int size,
		 struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;

	if (sk->shutdown & SEND_SHUTDOWN) {
		send_sig(SIGPIPE, current, 1);
		return(-EPIPE);
	}
	if (sk->prot->sendmsg == NULL) 
		return(-EOPNOTSUPP);
	if(sk->err)
		return sock_error(sk);
	/* We may need to bind the socket. */
	if(inet_autobind(sk)!=0)
		return -EAGAIN;
	return sk->prot->sendmsg(sk, msg, size);
}


int inet_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;

	/*
	 * This should really check to make sure
	 * the socket is a TCP socket. (WHY AC...)
	 */
	how++; /* maps 0->1 has the advantage of making bit 1 rcvs and
		       1->2 bit 2 snds.
		       2->3 */
	if ((how & ~SHUTDOWN_MASK) || how==0)	/* MAXINT->0 */
		return(-EINVAL);
	if (sock->state == SS_CONNECTING && sk->state == TCP_ESTABLISHED)
		sock->state = SS_CONNECTED;
	if (!sk || !tcp_connected(sk->state)) 
		return(-ENOTCONN);
	sk->shutdown |= how;
	if (sk->prot->shutdown)
		sk->prot->shutdown(sk, how);
	return(0);
}


unsigned int inet_poll(struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;

	if (sk->prot->poll == NULL)
		return(0);
	return sk->prot->poll(sock, wait);
}

/*
 *	ioctl() calls you can issue on an INET socket. Most of these are
 *	device configuration and stuff and very rarely used. Some ioctls
 *	pass on to the socket itself.
 *
 *	NOTE: I like the idea of a module for the config stuff. ie ifconfig
 *	loads the devconfigure module does its configuring and unloads it.
 *	There's a good 20K of config code hanging around the kernel.
 */

static int inet_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	int err;
	int pid;

	switch(cmd) 
	{
		case FIOSETOWN:
		case SIOCSPGRP:
			err = get_user(pid, (int *) arg);
			if (err)
				return err; 
			if (current->pid != pid && current->pgrp != -pid && !suser())
				return -EPERM;
			sk->proc = pid;
			return(0);
		case FIOGETOWN:
		case SIOCGPGRP:
			return put_user(sk->proc, (int *)arg);
			return(0);			
		case SIOCGSTAMP:
			if(sk->stamp.tv_sec==0)
				return -ENOENT;
			err = copy_to_user((void *)arg,&sk->stamp,sizeof(struct timeval));
			if (err)
				err = -EFAULT;
			return err;
		case SIOCADDRT:
		case SIOCDELRT:
		case SIOCRTMSG:
			return(ip_rt_ioctl(cmd,(void *) arg));
		case SIOCDARP:
		case SIOCGARP:
		case SIOCSARP:
		case OLD_SIOCDARP:
		case OLD_SIOCGARP:
		case OLD_SIOCSARP:
			return(arp_ioctl(cmd,(void *) arg));
		case SIOCDRARP:
		case SIOCGRARP:
		case SIOCSRARP:
#ifdef CONFIG_KERNELD
			if (rarp_ioctl_hook == NULL)
				request_module("rarp");
#endif
			if (rarp_ioctl_hook != NULL)
				return(rarp_ioctl_hook(cmd,(void *) arg));
		case SIOCGIFADDR:
		case SIOCSIFADDR:
		case SIOCGIFBRDADDR:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
			return(devinet_ioctl(cmd,(void *) arg));
		case SIOCGIFCONF:
		case SIOCGIFFLAGS:
		case SIOCSIFFLAGS:
		case SIOCADDMULTI:
		case SIOCDELMULTI:
		case SIOCGIFMETRIC:
		case SIOCSIFMETRIC:
		case SIOCGIFMEM:
		case SIOCSIFMEM:
		case SIOCGIFMTU:
		case SIOCSIFMTU:
		case SIOCSIFLINK:
		case SIOCGIFHWADDR:
		case SIOCSIFHWADDR:
		case SIOCSIFMAP:
		case SIOCGIFMAP:
		case SIOCSIFSLAVE:
		case SIOCGIFSLAVE:
		case SIOGIFINDEX:
			return(dev_ioctl(cmd,(void *) arg));

		case SIOCGIFBR:
		case SIOCSIFBR:
#ifdef CONFIG_BRIDGE		
			return(br_ioctl(cmd,(void *) arg));
#else
			return -ENOPKG;
#endif						
			
		case SIOCADDDLCI:
		case SIOCDELDLCI:
#ifdef CONFIG_DLCI
			return(dlci_ioctl(cmd, (void *) arg));
#endif

#ifdef CONFIG_DLCI_MODULE

#ifdef CONFIG_KERNELD
			if (dlci_ioctl_hook == NULL)
				request_module("dlci");
#endif

			if (dlci_ioctl_hook)
				return((*dlci_ioctl_hook)(cmd, (void *) arg));
#endif
			return -ENOPKG;

		default:
			if ((cmd >= SIOCDEVPRIVATE) &&
			    (cmd <= (SIOCDEVPRIVATE + 15)))
				return(dev_ioctl(cmd,(void *) arg));

#ifdef CONFIG_NET_RADIO
			if((cmd >= SIOCIWFIRST) && (cmd <= SIOCIWLAST))
				return(dev_ioctl(cmd,(void *) arg));
#endif

			if (sk->prot->ioctl==NULL) 
				return(-EINVAL);
			return(sk->prot->ioctl(sk, cmd, arg));
	}
	/*NOTREACHED*/
	return(0);
}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
/*
 * Some routines for the for loop in get_sock which sometimes needs to walk
 * two linked lists in sequence.  Could use macros as well.
 * Does anyone know a nicer way to code this?
 */
static __inline__ struct sock *secondlist(unsigned short hpnum, struct sock *s,
				int *pfirstpass, struct proto *prot)
{
	if (s == NULL && (*pfirstpass)-- )
		return prot->sock_array[hpnum & (SOCK_ARRAY_SIZE - 1)];
	else
		return s;
}
static __inline__ struct sock *get_sock_loop_init(unsigned short hnum,
			unsigned short hpnum, struct sock *s,
			int *pfirstpass, struct proto *prot)
{
	s = prot->sock_array[hnum & (SOCK_ARRAY_SIZE - 1)];
	return secondlist(hpnum, s, pfirstpass, prot);
}
static __inline__ struct sock *get_sock_loop_next(unsigned short hnum,
			unsigned short hpnum, struct sock *s,
			int *pfirstpass, struct proto *prot)
{
	s = s->next;
	return secondlist(hpnum, s, pfirstpass, prot);
}

struct sock *get_sock_proxy(struct proto *prot, unsigned short num,
				unsigned long raddr,
				unsigned short rnum, unsigned long laddr,
				unsigned long paddr, unsigned short pnum)
{
	struct sock *s = 0;
	struct sock *result = NULL;
	int badness = -1;
	unsigned short hnum;
	unsigned short hpnum;
	int firstpass = 1;

	hnum = ntohs(num);
	hpnum = ntohs(pnum);

	/*
	 * SOCK_ARRAY_SIZE must be a power of two.  This will work better
	 * than a prime unless 3 or more sockets end up using the same
	 * array entry.  This should not be a problem because most
	 * well known sockets don't overlap that much, and for
	 * the other ones, we can just be careful about picking our
	 * socket number when we choose an arbitrary one.
	 */

	for(s = get_sock_loop_init(hnum, hpnum, s, &firstpass, prot);
		s != NULL;
		s = get_sock_loop_next(hnum, hpnum, s, &firstpass, prot))
	{
		int score = 0;

		/* accept the addressed port or the redirect (proxy) port */
		if (s->num != hnum && s->num != hpnum)
			continue;

		if(s->dead && (s->state == TCP_CLOSE))
			continue;
		/* local address matches? */
		if (s->rcv_saddr) {
			/*
			 * If this is redirected traffic, it must either
			 * match on the redirected port/ip-address or on
			 * the actual destination, not on a mixture.
			 * There must be a simpler way to express this...
			 */
			if ((s->num != hpnum || s->rcv_saddr != paddr)
			    && (s->num != hnum || s->rcv_saddr != laddr))
				continue;
			score++;
		}
		/* remote address matches? */
		if (s->daddr) {
			if (s->daddr != raddr)
				continue;
			score++;
		}
		/* remote port matches? */
		if (s->dummy_th.dest) {
			if (s->dummy_th.dest != rnum)
				continue;
			score++;
		}
		/* perfect match? */
		if (score == 3 && s->num == hnum)
			return s;
		/* no, check if this is the best so far.. */
		if (score <= badness)
			continue;
		/* don't accept near matches on the actual destination
		 * port with IN_ADDR_ANY for redirected traffic, but do
		 * allow explicit remote address listens.  (disputable)
		 */
		if (s->num != hpnum && !s->rcv_saddr)
			continue;
		result = s;
		badness = score;
  	}
  	return result;
}
#endif

/*
 * This routine must find a socket given a TCP or UDP header.
 * Everything is assumed to be in net order.
 *
 * We give priority to more closely bound ports: if some socket
 * is bound to a particular foreign address, it will get the packet
 * rather than somebody listening to any address..
 */

struct sock *get_sock(struct proto *prot, unsigned short num,
				unsigned long raddr,
				unsigned short rnum, unsigned long laddr)
{
	struct sock *s = 0;
	struct sock *result = NULL;
	int badness = -1;
	unsigned short hnum;

	hnum = ntohs(num);

	/*
	 * SOCK_ARRAY_SIZE must be a power of two.  This will work better
	 * than a prime unless 3 or more sockets end up using the same
	 * array entry.  This should not be a problem because most
	 * well known sockets don't overlap that much, and for
	 * the other ones, we can just be careful about picking our
	 * socket number when we choose an arbitrary one.
	 */

	for(s = prot->sock_array[hnum & (SOCK_ARRAY_SIZE - 1)];
			s != NULL; s = s->next) 
	{
		int score = 0;

		if (s->num != hnum) 
			continue;

		if(s->dead && (s->state == TCP_CLOSE))
			continue;
		/* local address matches? */
		if (s->rcv_saddr) {
			if (s->rcv_saddr != laddr)
				continue;
			score++;
		}
		/* remote address matches? */
		if (s->daddr) {
			if (s->daddr != raddr)
				continue;
			score++;
		}
		/* remote port matches? */
		if (s->dummy_th.dest) {
			if (s->dummy_th.dest != rnum)
				continue;
			score++;
		}
		/* perfect match? */
		if (score == 3)
			return s;
		/* no, check if this is the best so far.. */
		if (score <= badness)
			continue;
		result = s;
		badness = score;
  	}
  	return result;
}


/*
 *	Deliver a datagram to raw sockets.
 */
 
struct sock *get_sock_raw(struct sock *sk, 
				unsigned short num,
				unsigned long raddr,
				unsigned long laddr)
{
	struct sock *s;

	s=sk;

	for(; s != NULL; s = s->next) 
	{
		if (s->num != num) 
			continue;
		if(s->dead && (s->state == TCP_CLOSE))
			continue;
		if(s->daddr && s->daddr!=raddr)
			continue;
 		if(s->rcv_saddr && s->rcv_saddr != laddr)
			continue;
		return(s);
  	}
  	return(NULL);
}

/*
 *	Deliver a datagram to broadcast/multicast sockets.
 */
 
struct sock *get_sock_mcast(struct sock *sk, 
				unsigned short num,
				unsigned long raddr,
				unsigned short rnum, unsigned long laddr)
{
	struct sock *s;
	unsigned short hnum;

	hnum = ntohs(num);

	/*
	 * SOCK_ARRAY_SIZE must be a power of two.  This will work better
	 * than a prime unless 3 or more sockets end up using the same
	 * array entry.  This should not be a problem because most
	 * well known sockets don't overlap that much, and for
	 * the other ones, we can just be careful about picking our
	 * socket number when we choose an arbitrary one.
	 */
	
	s=sk;

	for(; s != NULL; s = s->next) 
	{
		if (s->num != hnum) 
			continue;
		if(s->dead && (s->state == TCP_CLOSE))
			continue;
		if(s->daddr && s->daddr!=raddr)
			continue;
		if (s->dummy_th.dest != rnum && s->dummy_th.dest != 0) 
			continue;
 		if(s->rcv_saddr  && s->rcv_saddr != laddr)
			continue;
		return(s);
  	}
  	return(NULL);
}


struct proto_ops inet_stream_ops = {
	AF_INET,

	inet_dup,
	inet_release,
	inet_bind,
	inet_stream_connect,
	NULL,
	inet_accept,
	inet_getname, 
	inet_poll,
	inet_ioctl,
	inet_listen,
	inet_shutdown,
	inet_setsockopt,
	inet_getsockopt,
	sock_no_fcntl,
	inet_sendmsg,
	inet_recvmsg
};

struct proto_ops inet_dgram_ops = {
	AF_INET,

	inet_dup,
	inet_release,
	inet_bind,
	inet_dgram_connect,
	NULL,
	NULL,
	inet_getname, 
	datagram_poll,
	inet_ioctl,
	NULL,
	inet_shutdown,
	inet_setsockopt,
	inet_getsockopt,
	sock_no_fcntl,
	inet_sendmsg,
	inet_recvmsg
};




struct net_proto_family inet_family_ops = {
	AF_INET,
	inet_create
};

extern unsigned long seq_offset;

#ifdef CONFIG_PROC_FS
#ifdef CONFIG_INET_RARP
static struct proc_dir_entry proc_net_rarp = {
	PROC_NET_RARP, 4, "rarp",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	rarp_get_info
};
#endif		/* RARP */
static struct proc_dir_entry proc_net_raw = {
	PROC_NET_RAW, 3, "raw",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	raw_get_info
};
static struct proc_dir_entry proc_net_snmp = {
	PROC_NET_SNMP, 4, "snmp",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	snmp_get_info
};
static struct proc_dir_entry proc_net_sockstat = {
	PROC_NET_SOCKSTAT, 8, "sockstat",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	afinet_get_info
};
static struct proc_dir_entry proc_net_tcp = {
	PROC_NET_TCP, 3, "tcp",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	tcp_get_info
};
static struct proc_dir_entry proc_net_udp = {
	PROC_NET_UDP, 3, "udp",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	udp_get_info
};
#endif		/* CONFIG_PROC_FS */


/*
 *	Called by socket.c on kernel startup.  
 */
 
void inet_proto_init(struct net_proto *pro)
{
	struct sk_buff *dummy_skb;
	struct inet_protocol *p;
	int i;


	printk("Swansea University Computer Society TCP/IP for NET3.037\n");

	if (sizeof(struct inet_skb_parm) > sizeof(dummy_skb->cb))
	{
		printk(KERN_CRIT "inet_proto_init: panic\n");
		return;
	}

	/*
	 *	Tell SOCKET that we are alive... 
	 */
   
  	(void) sock_register(&inet_family_ops);

  	seq_offset = CURRENT_TIME*250;

	/*
	 *	Add all the protocols. 
	 */
	 
	for(i = 0; i < SOCK_ARRAY_SIZE; i++) 
	{
		tcp_sock_array[i] = NULL;
		udp_sock_array[i] = NULL;
		raw_sock_array[i] = NULL;
  	}
	tcp_prot.inuse = 0;
	tcp_prot.highestinuse = 0;
	tcp_prot.sock_array = tcp_sock_array;
	udp_prot.inuse = 0;
	udp_prot.highestinuse = 0;
	udp_prot.sock_array = udp_sock_array;
	raw_prot.inuse = 0;
	raw_prot.highestinuse = 0;
	raw_prot.sock_array = raw_sock_array;

	printk("IP Protocols: ");
	for(p = inet_protocol_base; p != NULL;) 
	{
		struct inet_protocol *tmp = (struct inet_protocol *) p->next;
		inet_add_protocol(p);
		printk("%s%s",p->name,tmp?", ":"\n");
		p = tmp;
	}

	/*
	 *	Set the ARP module up
	 */
	arp_init();
  	/*
  	 *	Set the IP module up
  	 */
	ip_init();
	/*
	 *	Set the ICMP layer up
	 */
	icmp_init(&inet_family_ops);
	/*
	 *	Set the firewalling up
	 */
#if defined(CONFIG_IP_ACCT)||defined(CONFIG_IP_FIREWALL)
	ip_fw_init();
#endif
#ifdef CONFIG_IP_MASQUERADE
	ip_masq_init();
#endif

	/*
	 *	Initialise the multicast router
	 */
#if defined(CONFIG_IP_MROUTE)
	ip_mr_init();
#endif

	/*
	 *  Initialise AF_INET alias type (register net_alias_type)
	 */

#if defined(CONFIG_IP_ALIAS)
	ip_alias_init();
#endif

#ifdef CONFIG_INET_RARP
	rarp_ioctl_hook = rarp_ioctl;
#endif
	/*
	 *	Create all the /proc entries.
	 */

#ifdef CONFIG_PROC_FS
#ifdef CONFIG_INET_RARP
	proc_net_register(&proc_net_rarp);
#endif		/* RARP */
	proc_net_register(&proc_net_raw);
	proc_net_register(&proc_net_snmp);
	proc_net_register(&proc_net_sockstat);
	proc_net_register(&proc_net_tcp);
	proc_net_register(&proc_net_udp);
#endif		/* CONFIG_PROC_FS */
}
