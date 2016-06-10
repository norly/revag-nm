/*
 * Copyright 2015 Max Staudt
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


static int base_id = 0x420;
static int my_id = 0x1a;
static int next_id = 0x19;  //myid

static int ignore_counter = 10;

static void on_nm_frame(int socket)
{
  struct can_frame frame;
  struct sockaddr_can addr;
  int ret;
  socklen_t len;

  ret = recvfrom(socket, &frame, sizeof(struct can_frame), 0,
                  (struct sockaddr *)&addr, &len);
  if (ret < 0) {
    perror("recvfrom");
    exit(1);
  }

  if (frame.can_id >> 5 == 0x420 >> 5) {
    printf("Received NM frame from %03x\n", frame.can_id);
  }

  if (frame.can_id == base_id + my_id) {
    printf("Skipping my own NM frame.\n");
    return;
  }

  switch (frame.data[1]) {
    case 01:
      if (frame.data[0] == my_id) {
        struct can_frame txframe = {.can_id = base_id + next_id,
                                    .can_dlc = 8,
                                    .data = {next_id, 01, 00, 00, 00, 00, 00, 00},
                                   };
	ssize_t tmp = write(socket, &txframe, sizeof(txframe));
	if (tmp != sizeof(txframe)) {
		perror("write socket");
		//return 1;
	}
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
	ssize_t tmp = write(socket, &txframe, sizeof(txframe));
	if (tmp != sizeof(txframe)) {
		perror("write socket");
		//return 1;
	}
      }
      break;
  }
}





static int net_init(char *ifname)
{
        int s;
	int recv_own_msgs;
	struct sockaddr_can addr;
	struct ifreq ifr;

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) {
		perror("socket");
		exit(1);
	}

	memset(&ifr.ifr_name, 0, sizeof(ifr.ifr_name));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
		perror("SIOCGIFINDEX");
		exit(1);
	}

	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 0;
	}

	recv_own_msgs = 0; /* 0 = disabled (default), 1 = enabled */
	setsockopt(s, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
			&recv_own_msgs, sizeof(recv_own_msgs));

	return s;
}

int main(int argc, char **argv)
{
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
			on_nm_frame(s);
			continue;
		}
	}

	return 0;
}
