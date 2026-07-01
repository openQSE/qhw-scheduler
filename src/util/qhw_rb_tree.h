#ifndef QHW_RB_TREE_H
#define QHW_RB_TREE_H

#include <stdint.h>

struct qhw_rb_node {
	uint64_t key;
	void *value;
	struct qhw_rb_node *left;
	struct qhw_rb_node *right;
	struct qhw_rb_node *parent;
	int red;
};

struct qhw_rb_tree {
	struct qhw_rb_node *root;
};

void qhw_rb_tree_init(struct qhw_rb_tree *tree);
struct qhw_rb_node *qhw_rb_tree_find(
	struct qhw_rb_tree *tree,
	uint64_t key);
int qhw_rb_tree_insert(
	struct qhw_rb_tree *tree,
	struct qhw_rb_node *node);
struct qhw_rb_node *qhw_rb_tree_first(struct qhw_rb_tree *tree);
struct qhw_rb_node *qhw_rb_tree_next(struct qhw_rb_node *node);

#endif

