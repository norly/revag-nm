static char* nm_main_to_string(NM_State state)
{
	switch(state & NM_MAIN_MASK) {
		case NM_MAIN_OFF:
			return "Off";
		case NM_MAIN_ON:
			return "Ready";
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
	printf(" Node | next | Main      | Sleep\n");
	printf("----------------------------------------\n");

	for (id = 0; id < nm->max_nodes; id++) {
		struct NM_Node *node = &nm->nodes[id];

		if (node->state & NM_MAIN_MASK) {
			printf("  %02x     %02x    %9s   %s\n",
				id,
				node->next,
				nm_main_to_string(node->state),
				nm_sleep_to_string(node->state));

		}
	}

	printf("\n");
}






static void can_tx(int socket, struct can_frame *frame)
{
	ssize_t ret;

	ret = write(socket, frame, sizeof(*frame));
	if (ret != sizeof(*frame)) {
		perror("write to CAN socket");
		exit(1);
	}
}








static struct NM_Main* nm_alloc(unsigned node_bits, NM_ID my_id, canid_t can_base)
{
	struct NM_Main *nm;

	if (node_bits < 1 || node_bits > 6) {
		return NULL;
	}

	nm = malloc(sizeof(*nm));
	if (!nm) {
		return NULL;
	}

	nm->max_nodes = 1 << node_bits;
	nm->nodes = malloc(nm->max_nodes * sizeof(*nm->nodes));
	if (!nm->nodes) {
		free(nm);
		return NULL;
	}

	nm->my_id = my_id;
	nm->can_base = can_base;

	nm->nodes[nm->my_id].next = nm->my_id;
	nm->nodes[nm->my_id].state = NM_MAIN_LOGIN;

	nm->tv.tv_sec = 0;
	nm->tv.tv_usec = NM_USECS_OTHER_TURN;

	return nm;
}




static void nm_free(struct NM_Main *nm)
{
	free(nm->nodes);
	free(nm);
}
