/*
 *	AX.25 release 036
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	AX.25 028a	Jonathan(G4KLX)	New state machine based on SDL diagrams.
 *	AX.25 028b	Jonathan(G4KLX)	Extracted AX25 control block from the
 *					sock structure.
 *	AX.25 029	Alan(GW4PTS)	Switched to KA9Q constant names.
 *	AX.25 031	Joerg(DL1BKE)	Added DAMA support
 *	AX.25 032	Joerg(DL1BKE)	Fixed DAMA timeout bug
 *	AX.25 033	Jonathan(G4KLX)	Modularisation functions.
 *	AX.25 035	Frederic(F1OAT)	Support for pseudo-digipeating.
 *	AX.25 036	Jonathan(G4KLX)	Split from ax25_timer.c.
 */

#include <linux/config.h>
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

void ax25_std_timer(ax25_cb *ax25)
{
	switch (ax25->state) {
		case AX25_STATE_0:
			/* Magic here: If we listen() and a new link dies before it
			   is accepted() it isn't 'dead' so doesn't get removed. */
			if (ax25->sk == NULL || ax25->sk->destroy || (ax25->sk->state == TCP_LISTEN && ax25->sk->dead)) {
				del_timer(&ax25->timer);
				ax25_destroy_socket(ax25);
				return;
			}
			break;

		case AX25_STATE_3:
		case AX25_STATE_4:
			/*
			 * Check the state of the receive buffer.
			 */
			if (ax25->sk != NULL) {
				if (atomic_read(&ax25->sk->rmem_alloc) < (ax25->sk->rcvbuf / 2) &&
				    (ax25->condition & AX25_COND_OWN_RX_BUSY)) {
					ax25->condition &= ~AX25_COND_OWN_RX_BUSY;
					ax25->condition &= ~AX25_COND_ACK_PENDING;
					ax25_send_control(ax25, AX25_RR, AX25_POLLOFF, AX25_RESPONSE);
					break;
				}
			}
			/*
			 * Check for frames to transmit.
			 */
			ax25_kick(ax25);
			break;

		default:
			break;
	}

	if (ax25->t2timer > 0 && --ax25->t2timer == 0) {
		if (ax25->state == AX25_STATE_3 || ax25->state == AX25_STATE_4) {
			if (ax25->condition & AX25_COND_ACK_PENDING) {
				ax25->condition &= ~AX25_COND_ACK_PENDING;
				ax25_std_timeout_response(ax25);
			}
		}
	}

	if (ax25->t3timer > 0 && --ax25->t3timer == 0) {
		if (ax25->state == AX25_STATE_3) {
			ax25->n2count = 0;
			ax25_std_transmit_enquiry(ax25);
			ax25->state   = AX25_STATE_4;
		}
		ax25->t3timer = ax25->t3;
	}

	if (ax25->idletimer > 0 && --ax25->idletimer == 0) {
		/* dl1bke 960228: close the connection when IDLE expires */
		/* 		  similar to DAMA T3 timeout but with    */
		/* 		  a "clean" disconnect of the connection */

		ax25_clear_queues(ax25);

		ax25->n2count = 0;
		ax25->t3timer = 0;
		ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
		ax25->state   = AX25_STATE_2;
		ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);

		if (ax25->sk != NULL) {
			ax25->sk->state     = TCP_CLOSE;
			ax25->sk->err       = 0;
			ax25->sk->shutdown |= SEND_SHUTDOWN;
			if (!ax25->sk->dead)
				ax25->sk->state_change(ax25->sk);
			ax25->sk->dead      = 1;
			ax25->sk->destroy   = 1;
		}
	}

	if (ax25->t1timer == 0 || --ax25->t1timer > 0) {
		ax25_set_timer(ax25);
		return;
	}

	switch (ax25->state) {
		case AX25_STATE_1: 
			if (ax25->n2count == ax25->n2) {
				if (ax25->modulus == AX25_MODULUS) {
					ax25_link_failed(&ax25->dest_addr, ax25->ax25_dev->dev);
					ax25_clear_queues(ax25);
					ax25->state = AX25_STATE_0;
					if (ax25->sk != NULL) {
						ax25->sk->state     = TCP_CLOSE;
						ax25->sk->err       = ETIMEDOUT;
						ax25->sk->shutdown |= SEND_SHUTDOWN;
						if (!ax25->sk->dead)
							ax25->sk->state_change(ax25->sk);
						ax25->sk->dead      = 1;
					}
				} else {
					ax25->modulus = AX25_MODULUS;
					ax25->window  = ax25->ax25_dev->values[AX25_VALUES_WINDOW];
					ax25->n2count = 0;
					ax25_send_control(ax25, AX25_SABM, AX25_POLLON, AX25_COMMAND);
				}
			} else {
				ax25->n2count++;
				if (ax25->modulus == AX25_MODULUS)
					ax25_send_control(ax25, AX25_SABM, AX25_POLLON, AX25_COMMAND);
				else
					ax25_send_control(ax25, AX25_SABME, AX25_POLLON, AX25_COMMAND);
			}
			break;

		case AX25_STATE_2:
			if (ax25->n2count == ax25->n2) {
				ax25_link_failed(&ax25->dest_addr, ax25->ax25_dev->dev);
				ax25_clear_queues(ax25);
				ax25->state = AX25_STATE_0;
				ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);

				if (ax25->sk != NULL) {
					ax25->sk->state     = TCP_CLOSE;
					ax25->sk->err       = ETIMEDOUT;
					ax25->sk->shutdown |= SEND_SHUTDOWN;
					if (!ax25->sk->dead)
						ax25->sk->state_change(ax25->sk);
					ax25->sk->dead      = 1;
				}
			} else {
				ax25->n2count++;
				ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
			}
			break;

		case AX25_STATE_3: 
			ax25->n2count = 1;
			ax25_std_transmit_enquiry(ax25);
			ax25->state   = AX25_STATE_4;
			break;

		case AX25_STATE_4:
			if (ax25->n2count == ax25->n2) {
				ax25_link_failed(&ax25->dest_addr, ax25->ax25_dev->dev);
				ax25_clear_queues(ax25);
				ax25_send_control(ax25, AX25_DM, AX25_POLLON, AX25_RESPONSE);
				ax25->state = AX25_STATE_0;
				if (ax25->sk != NULL) {
					SOCK_DEBUG(ax25->sk, "AX.25 link Failure\n");
					ax25->sk->state     = TCP_CLOSE;
					ax25->sk->err       = ETIMEDOUT;
					ax25->sk->shutdown |= SEND_SHUTDOWN;
					if (!ax25->sk->dead)
						ax25->sk->state_change(ax25->sk);
					ax25->sk->dead      = 1;
				}
			} else {
				ax25->n2count++;
				ax25_std_transmit_enquiry(ax25);
			}
			break;
	}

	ax25->t1timer = ax25->t1 = ax25_calculate_t1(ax25);

	ax25_set_timer(ax25);
}

#endif
