#ifndef _TRANSP_V6_H
#define _TRANSP_V6_H

#include <net/checksum.h>

/*
 *	IPv6 transport protocols
 */

#ifdef __KERNEL__

extern struct proto rawv6_prot;
extern struct proto udpv6_prot;
extern struct proto tcpv6_prot;

extern void				rawv6_init(void);
extern void				udpv6_init(void);
extern void				tcpv6_init(void);

extern int				udpv6_connect(struct sock *sk,
						      struct sockaddr *uaddr,
						      size_t addr_len);

extern int			datagram_recv_ctl(struct sock *sk,
						  struct msghdr *msg,
						  struct sk_buff *skb);

extern int			datagram_send_ctl(struct msghdr *msg,
						  struct device **src_dev,
						  struct in6_addr **src_addr,
						  struct ipv6_options *opt);

#define		LOOPBACK4_IPV6		__constant_htonl(0x7f000006)

/*
 *	address family specific functions
 */
extern struct tcp_func	ipv4_specific;

#endif

#endif
