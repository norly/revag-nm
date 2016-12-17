/*
 * Copyright 2015-2016 Max Staudt
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License 2 as published
 * by the Free Software Foundation.
 */

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


enum {
	/* OSEK/VDX NM Level 0 */

	NM_MAIN_OFF      = 0x00,
	NM_MAIN_ON       = 0x01,
	NM_MAIN_LOGIN    = 0x02,
	NM_MAIN_LIMPHOME = 0x04,
	NM_MAIN_MASK     = 0x0F,

	NM_SLEEP_CANCEL  = 0x00,
	NM_SLEEP_REQUEST = 0x10,
	NM_SLEEP_ACK     = 0x20,
	NM_SLEEP_MASK    = 0xF0,
};

typedef unsigned char NM_ID;
typedef unsigned char NM_State;

struct NM_Node {
	NM_ID next;
	NM_State state;
};

struct NM_Main {
	unsigned max_nodes;
	struct NM_Node *nodes;
};




static void can_tx(int socket, struct can_frame *frame)
{
	ssize_t ret;

	ret = write(socket, frame, sizeof(*frame));
	if (ret != sizeof(*frame)) {
		perror("write to CAN socket");
		exit(1);
	}
}



static char* nm_main_to_string(NM_State state)
{
	switch(state & NM_MAIN_MASK) {
		case NM_MAIN_OFF:
			return "Off";
		case NM_MAIN_ON:
			return "On/Ready";
		case NM_MAIN_LOGIN:
			return "Login";
		case NM_MAIN_LIMPHOME:
			return "Limp home";
		default:
			return "Unknown?";
	}
}

static char* nm_sleep_to_string(NM_State state)
{
	switch(state & NM_SLEEP_MASK) {
		case NM_SLEEP_CANCEL:
			return "No";
		case NM_SLEEP_REQUEST:
			return "Request";
		case NM_SLEEP_ACK:
			return "Acknowledged";
		default:
			return "Unknown?";
	}
}



static void nm_dump_all(struct NM_Main *nm)
{
	unsigned id;

	printf("\n");
	printf("Current system state:\n");
	printf("\n");

	for (id = 0; id < nm->max_nodes; id++) {
		struct NM_Node *node = &nm->nodes[id];

		if (node->state & NM_MAIN_MASK) {
			printf("Active node %02x:\n", id);
			printf("  Next:  %02x\n", node->next);
			printf("  Main:  %s\n", nm_main_to_string(node->state));
			printf("  Sleep: %s\n", nm_sleep_to_string(node->state));
			printf("\n");
		}
	}
}



static void nm_handle_can_frame(struct NM_Main *nm, struct can_frame *frame)
{
	NM_ID id;
	NM_ID next;
	NM_State state;

	//printf("Received CAN frame from CAN ID %03x\n", frame->can_id);

	if (frame->can_dlc < 2) {
		printf("Skipping short frame from CAN ID %03x\n", frame->can_id);
		return;
	}


	if ((frame->can_id & ~0x1f) != 0x420) {
		printf("Skipping non-NM from CAN ID %03x\n", frame->can_id);
		return;
	}

	printf("Received NM frame from CAN ID %03x\n", frame->can_id);

	id = frame->can_id & 0x1f;
	next = frame->data[0];
	state = frame->data[1];

	nm->nodes[id].next = next;
	nm->nodes[id].state = state;

	nm_dump_all(nm);

	/*
	switch (state) {
		case 01:
			if (frame.data[0] == my_id) {
				struct can_frame txframe = {.can_id = base_id + next_id,
							    .can_dlc = 8,
							    .data = {next_id, 01, 00, 00, 00, 00, 00, 00},
							   };
				can_tx(socket, &txframe);
			}
		break;
		case 02:
			if (ignore_counter > 0) {
				ignore_counter--;
				break;
			}
			if (next_id <= my_id
				  ? frame.can_id - base_id < next_id
				  : next_id == my_id || frame.can_id - base_id < next_id) {
				next_id = frame.can_id - base_id;

				struct can_frame txframe = {.can_id = base_id + my_id,
							    .can_dlc = 8,
							    .data = {my_id, 02, 01, 04, 00, 04, 00, 00},
							   };
				can_tx(socket, &txframe);
			}
		break;
	}
	*/
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
	struct NM_Node nodes[32] = {{0}};
	struct NM_Main nm = {.max_nodes = 32, .nodes = nodes};
  	fd_set rdfs;
	int s;

	if (argc != 2) {
		printf("syntax: %s IFNAME\n", argv[0]);
		exit(1);
	}

	s = net_init(argv[1]);

	while (1) {

		FD_ZERO(&rdfs);

		FD_SET(s, &rdfs);

		if (select(s+1, &rdfs, NULL, NULL, NULL) < 0) {
			perror("select");
			return 1;
		}

		if (FD_ISSET(s, &rdfs)) {
			struct can_frame frame;
			ssize_t ret;

			ret = read(s, &frame, sizeof(frame));
			if (ret < 0) {
				perror("recvfrom CAN socket");
				exit(1);
			}

			nm_handle_can_frame(&nm, &frame);
			continue;
		}
	}

	return 0;
}
