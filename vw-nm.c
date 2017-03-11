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







static int nm_handle_can_frame(struct NM_Main *nm, struct can_frame *frame)
{
	NM_ID id;
	NM_ID next;
	NM_State state;
	int its_my_turn = 0;

	if (frame->can_dlc < 2) {
		printf("Skipping short frame from CAN ID %03x\n", frame->can_id);
		return 0;
	}


	if ((frame->can_id & ~(nm->max_nodes - 1)) != nm->can_base) {
		printf("Skipping non-NM from CAN ID %03x\n", frame->can_id);
		return 0;
	}

	printf("Received NM frame from CAN ID %03x\n", frame->can_id);

	id = frame->can_id & (nm->max_nodes - 1);
	next = frame->data[0];
	state = frame->data[1];

	nm->nodes[id].next = (state & NM_MAIN_MASK) == NM_MAIN_ON
				? next
				: 0xff;
	nm->nodes[id].state = state;

	switch (state & NM_MAIN_MASK) {
		case NM_MAIN_ON:
			if (next == nm->my_id) {
				its_my_turn = 1;
			}
			break;
		case NM_MAIN_LOGIN:
			if (id == nm->my_id) {
				break;
			}
			printf("Handling LOGIN\n");
			printf("Testing %x < %x\n", id, nm->nodes[nm->my_id].next);
			if (id < nm->nodes[nm->my_id].next) {
				nm->nodes[nm->my_id].next = id;
			}
			break;
	}

	nm_dump_all(nm);

	return its_my_turn;
}




static NM_ID nm_my_next_id(struct NM_Main *nm) {
	unsigned id;

	if (nm->max_nodes < 2
		|| (nm->nodes[nm->my_id].state & NM_MAIN_MASK) != NM_MAIN_ON) {
		assert(0);
	}

	id = nm->my_id;
	do {
		struct NM_Node *node;

		id++;
		if (id >= nm->max_nodes) {
			id = 0;
		}
		node = &nm->nodes[id];

		if ((node->state & NM_MAIN_MASK) == NM_MAIN_ON) {
			return id;
		}
	} while (id != nm->my_id);

	/* This is never reached */
	assert(0);
	return -1;
}




static void nm_timeout_callback(struct NM_Main *nm, struct can_frame *frame) {
	nm->nodes[nm->my_id].next = nm_my_next_id(nm);

	frame->can_id = nm->can_base + nm->my_id;
	frame->can_dlc = 2;
	frame->data[0] = nm->nodes[nm->my_id].next;
	frame->data[1] = NM_MAIN_ON;
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
	struct timeval tv, *next_tv = NULL;
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

	while (1) {
		int retval;

		FD_ZERO(&rdfs);
		FD_SET(s, &rdfs);

		retval = select(s+1, &rdfs, NULL, NULL, next_tv);
		/* We currently rely on Linux timeout behavior here,
		 * i.e. the timeout now reflects the remaining time */
		if (retval < 0) {
			perror("select");
			return 1;
		} else if (!retval) {
			/* Timeout */
			struct can_frame frame;

			nm_timeout_callback(nm, &frame);
			can_tx(s, &frame);

			next_tv = NULL;
		} else if (FD_ISSET(s, &rdfs)) {
			struct can_frame frame;
			ssize_t ret;

			ret = read(s, &frame, sizeof(frame));
			if (ret < 0) {
				perror("recvfrom CAN socket");
				return 1;
			}

			if (nm_handle_can_frame(nm, &frame)) {
				tv.tv_sec = 0;
				tv.tv_usec = 400000;
				next_tv = &tv;
			}
			continue;
		}
	}

	nm_free(nm);

	return 0;
}
