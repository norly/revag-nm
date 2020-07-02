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










static void nm_set_timer_now(struct NM_Main *nm)
{
	nm->tv.tv_sec = 0;
	nm->tv.tv_usec = 0;
	nm->timer_reason = NM_TIMER_NOW;
}

static void nm_set_timer_normal(struct NM_Main *nm)
{
	nm->tv.tv_sec = 0;
	nm->tv.tv_usec = NM_USECS_NORMAL_TURN;
	nm->timer_reason = NM_TIMER_NORMAL;
}

static void nm_set_timer_awol(struct NM_Main *nm)
{
	nm->tv.tv_sec = 0;
	nm->tv.tv_usec = NM_USECS_NODE_AWOL;
	nm->timer_reason = NM_TIMER_AWOL;
}

static void nm_set_timer_limphome(struct NM_Main *nm)
{
	nm->tv.tv_sec = 0;
	nm->tv.tv_usec = NM_USECS_LIMPHOME;
	nm->timer_reason = NM_TIMER_LIMPHOME;
}






static void nm_reset(struct NM_Main *nm)
{
	unsigned id;

	if (nm->nodes[nm->my_id].next == nm->my_id) {
		nm->lonely_resets++;
	}

	for (id = 0; id < nm->max_nodes; id++) {
		nm->nodes[id].next = 0xff;
		nm->nodes[id].state = NM_MAIN_OFF;
	}

	nm->nodes[nm->my_id].next = nm->my_id;
	if (nm->lonely_resets >= 5) {
		printf("Limp home detected :(\n");

		nm->nodes[nm->my_id].state = NM_MAIN_LIMPHOME;
		nm_set_timer_limphome(nm);
	} else {
		nm->nodes[nm->my_id].state = NM_MAIN_LOGIN;
		nm_set_timer_now(nm);
	}
}


static void nm_initreset(struct NM_Main *nm)
{
	nm_reset(nm);

	nm->lonely_resets = 0;
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

	nm_initreset(nm);

	return nm;
}




static void nm_free(struct NM_Main *nm)
{
	free(nm->nodes);
	free(nm);
}
