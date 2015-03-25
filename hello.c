#include <stdio.h>
#include <rs_queue.h>

int
main(int argc, char *argv[])
{
	rs__q_t *q = rs__q_init();
	if (!q) return 1;
	
	const int num = 7;
	int i;
	for (i = 0; i < num; i++) {
		rs__q_entry_t *e = rs__q_insert(q);
		if (!e) return 2;
		e->type = RS__Q_PACKET;
		e->data.packet.cmd_rc = i;
	}
	
	for (i = 0; i < num; i++) {
		rs__q_entry_t *e = rs__q_remove(q);
		if (!e) return 3;
	}
	
	if (rs__q_remove(q))
		return 4;
	
	rs__q_free(q);
	return 0;
}
