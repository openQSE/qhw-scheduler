#ifndef QHW_LIST_H
#define QHW_LIST_H

#include <stddef.h>

struct qhw_list_node {
	struct qhw_list_node *prev;
	struct qhw_list_node *next;
};

void qhw_list_init(struct qhw_list_node *head);
int qhw_list_empty(const struct qhw_list_node *head);
void qhw_list_push_back(
	struct qhw_list_node *head,
	struct qhw_list_node *node);
struct qhw_list_node *qhw_list_pop_front(struct qhw_list_node *head);
void qhw_list_remove(struct qhw_list_node *node);

#define qhw_container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

#endif

