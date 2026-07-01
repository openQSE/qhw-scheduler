#include "qhw_list.h"

void qhw_list_init(struct qhw_list_node *head)
{
	head->next = head;
	head->prev = head;
}

int qhw_list_empty(const struct qhw_list_node *head)
{
	return head->next == head;
}

void qhw_list_push_back(
	struct qhw_list_node *head,
	struct qhw_list_node *node)
{
	node->prev = head->prev;
	node->next = head;
	head->prev->next = node;
	head->prev = node;
}

struct qhw_list_node *qhw_list_pop_front(struct qhw_list_node *head)
{
	struct qhw_list_node *node;

	if (qhw_list_empty(head)) {
		return NULL;
	}

	node = head->next;
	qhw_list_remove(node);
	return node;
}

void qhw_list_remove(struct qhw_list_node *node)
{
	node->prev->next = node->next;
	node->next->prev = node->prev;
	node->next = NULL;
	node->prev = NULL;
}

