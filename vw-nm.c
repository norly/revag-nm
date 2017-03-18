/*
 * Copyright 2015-2016 Max Staudt
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License 2 as published
 * by the Free Software Foundation.
 */

#include <assert.h>

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <endian.h>
#include <sys/time.h>


#include "vw-nm.h"
#include "vw-nm-tools.h"



static int nm_is_rx_frame_valid(struct NM_Main *nm, struct can_frame *frame)
{
	if (frame->can_dlc < 2) {
		printf("Skipping short frame from CAN ID %03x\n", frame->can_id);
		return 0;
	}

	if ((frame->can_id & ~(nm->max_nodes - 1)) != nm->can_base) {
		printf("Skipping non-NM from CAN ID %03x\n", frame->can_id);
		return 0;
	}

	return 1;
}




static void nm_update_my_next_id(struct NM_Main *nm) {
	unsigned id = nm->my_id;

	do {
		NM_State state;

		id++;
		if (id >= nm->max_nodes) {
			id = 0;
		}

		state = nm->nodes[id].state & NM_MAIN_MASK;

		if (state == NM_MAIN_ON || state == NM_MAIN_LOGIN) {
			/* We skip limp home nodes */
			nm->nodes[nm->my_id].next = id;
			break;
		}
	} while (id != nm->my_id);

	if (nm->nodes[nm->my_id].next == nm->my_id) {
		/* Uh oh, we're the only one left. */

		/* TODO */
		nm->nodes[nm->my_id].state = NM_MAIN_LOGIN;

		/* TODO: Timeout 140ms (RCD 310) */
	}
}



static void nm_handle_can_frame(struct NM_Main *nm, struct can_frame *frame)
{
	NM_ID sender, next;
	NM_State state;

	/* Is this a valid frame within our logical network? */
	if (!nm_is_rx_frame_valid(nm, frame)) {
		return;
	}

	printf("Received NM frame from CAN ID %03x\n", frame->can_id);


	/* Parse sender, its perceived successor, and its state */
	sender = frame->can_id & (nm->max_nodes - 1);
	next = frame->data[0];
	state = frame->data[1];

	/* TODO: Validate state, it needs to be within the enum */

	/* Skip our own frames */
	if (sender == nm->my_id) {
		return;
	}

	nm->nodes[sender].next = next;
	nm->nodes[sender].state = state;

	switch (state & NM_MAIN_MASK) {
		case NM_MAIN_ON:
			if (next == nm->nodes[nm->my_id].next
				&& nm->nodes[nm->my_id].next != nm->my_id) {
				/* sender doesn't know we exist */

				nm->nodes[nm->my_id].state = NM_MAIN_LOGIN;

				nm->tv.tv_sec = 0;
				nm->tv.tv_usec = 0;
				/* IMPORTANT: The caller needs to check for
				 * timeouts first, i.e. no other NM frames
				 * are received until our correcting login
				 * has been sent.
				 */
			} else if (next == nm->nodes[nm->my_id].next) {
				/* where (nm->nodes[nm->my_id].next == nm->my_id) */

				/* It can happen when:
				 *  - our sent frames don't go anywhere
				 *  - we just logged in and immediately
				 *    afterwards another ECU sent a regular
				 *    NM frame.
				 */

				/* Let's handle this just like a LOGIN, since
				 * we're learning about a new device.
				 * See case NM_MAIN_LOGIN below for details.
				 */

				nm_update_my_next_id(nm);
				nm->nodes[nm->my_id].state = NM_MAIN_ON;
			} else if (next == nm->my_id) {
				/* It's our turn.
				 * Reset the timeout so anyone we missed
				 * can send its login frame to correct us.
				 */
				nm->tv.tv_sec = 0;
				nm->tv.tv_usec = NM_USECS_MY_TURN;
			} else {
				/* We just got some random ON message.
				 * Reset the timer looking out for broken
				 * connectivity.
				 */
				nm->tv.tv_sec = 0;
				nm->tv.tv_usec = NM_USECS_NODE_AWOL;
			}
			break;
		case NM_MAIN_LOGIN:
			/* Note: sender != nm->my_id */

			nm_update_my_next_id(nm);

			/* We're not alone anymore, so let's change state. */
			nm->nodes[nm->my_id].state = NM_MAIN_ON;

			/* We don't reset the timeout when somebody logs in,
			 * i.e. (next == sender).
			 * Instead, we'll simply include them in the next
			 * round. */

			/* Actually, when a login is done as a correction,
			 * we do reset the timeout.
			 */
			if (next != sender) {
				nm->tv.tv_sec = 0;
				nm->tv.tv_usec = NM_USECS_NODE_AWOL;
			}
			break;
		case NM_MAIN_LIMPHOME:
			nm_update_my_next_id(nm);
	}

	nm_dump_all(nm);
}






static void nm_buildframe(struct NM_Main *nm, struct can_frame *frame)
{
	frame->can_id = nm->can_base + nm->my_id;
	frame->can_dlc = 2;
	frame->data[0] = nm->nodes[nm->my_id].next;
	frame->data[1] = nm->nodes[nm->my_id].state;
}




static void nm_timeout_callback(struct NM_Main *nm, struct can_frame *frame)
{
	nm->tv.tv_sec = 0;
	nm->tv.tv_usec = NM_USECS_NODE_AWOL;

	nm_buildframe(nm, frame);
}




static void nm_start(struct NM_Main *nm, struct can_frame *frame)
{
	nm->tv.tv_sec = 0;
	nm->tv.tv_usec = 50000;



	nm->nodes[nm->my_id].next = nm->my_id;
	nm->nodes[nm->my_id].state = NM_MAIN_LOGIN;

	nm_buildframe(nm, frame);
}




static int net_init(char *ifname)
{
        int s;
	int recv_own_msgs;
	struct sockaddr_can addr;
	struct ifreq ifr;
	struct can_filter fi;

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) {
		perror("socket");
		exit(1);
	}

	/* Convert interface name to index */
	memset(&ifr.ifr_name, 0, sizeof(ifr.ifr_name));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
		perror("SIOCGIFINDEX");
		exit(1);
	}

	/* Open the CAN interface */
	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 0;
	}

	recv_own_msgs = 1; /* 0 = disabled (default), 1 = enabled */
	setsockopt(s, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
			&recv_own_msgs, sizeof(recv_own_msgs));

	/* Handle only 32 NM IDs at CAN base ID 0x420 */
	fi.can_id   = 0x420;
	fi.can_mask = 0x7E0;

        setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &fi, sizeof(struct can_filter));

	return s;
}


int main(int argc, char **argv)
{
	struct NM_Main *nm;
  	fd_set rdfs;
	int s;

	if (argc != 2) {
		printf("syntax: %s IFNAME\n", argv[0]);
		return 1;
	}

	nm = nm_alloc(5, 0x0b, 0x420);
	if (!nm) {
		printf("Out of memory allocating NM struct.\n");
		return 1;
	}

	s = net_init(argv[1]);

	/* Stir up the hornet's nest */
	if (1) {
		struct can_frame frame;

		nm_start(nm, &frame);
		can_tx(s, &frame);
	}

	while (1) {
		int retval;

		FD_ZERO(&rdfs);
		FD_SET(s, &rdfs);

		retval = select(s+1, &rdfs, NULL, NULL, &nm->tv);
		/* We currently rely on Linux timeout behavior here,
		 * i.e. the timeout now reflects the remaining time */
		if (retval < 0) {
			perror("select");
			return 1;
		} else if (!retval) {
			/* Timeout, we NEED to check this first */
			struct can_frame frame;

			nm_timeout_callback(nm, &frame);
			can_tx(s, &frame);
		} else if (FD_ISSET(s, &rdfs)) {
			struct can_frame frame;
			ssize_t ret;

			ret = read(s, &frame, sizeof(frame));
			if (ret < 0) {
				perror("recvfrom CAN socket");
				return 1;
			}

			nm_handle_can_frame(nm, &frame);
			continue;
		}
	}

	nm_free(nm);

	return 0;
}
