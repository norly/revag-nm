#ifndef __REVAG_NM_H__
#define __REVAG_NM_H__

#include <sys/time.h>


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


enum timer_reason {
	NM_TIMER_NOW,
	NM_TIMER_NORMAL,
	NM_TIMER_AWOL,
	NM_TIMER_LIMPHOME,
};

struct NM_Main {
	unsigned max_nodes;
	struct NM_Node *nodes;

	NM_ID my_id;
	canid_t can_base;

	struct timeval tv;
	enum timer_reason timer_reason;

	/* How many times have we been alone when we reset? */
	int lonely_resets;
};




/* OSEK/VDX NM: T_Typ
 *
 * This timeout is ~50 ms in:
 *  - 0x19 (RCD 310, Bosch)
 *
 * and ~45ms in:
 *  - 0x0b Instrument cluster?
 *  - 0x15 MDI
 *  - 0x1A Phone
 */
#define NM_USECS_NORMAL_TURN 50000


/* OSEK/VDX NM: T_Max
 *
 * This timeout is 140 ms in:
 *  - 0x19 (RCD 310, Bosch)
 */
#define NM_USECS_NODE_AWOL 140000


/* OSEK/VDX NM: T_Error
 *
 * This timeout is 500 ms in:
 *  - 0x19 (RCD 310, Bosch)
 */
#define NM_USECS_LIMPHOME 500000


#endif /* __REVAG_NM_H__ */
