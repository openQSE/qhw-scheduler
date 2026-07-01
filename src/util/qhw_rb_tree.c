#include "qhw_rb_tree.h"

static void rotate_left(struct qhw_rb_tree *tree, struct qhw_rb_node *node)
{
	struct qhw_rb_node *right = node->right;

	node->right = right->left;
	if (right->left != 0) {
		right->left->parent = node;
	}
	right->parent = node->parent;
	if (node->parent == 0) {
		tree->root = right;
	} else if (node == node->parent->left) {
		node->parent->left = right;
	} else {
		node->parent->right = right;
	}
	right->left = node;
	node->parent = right;
}

static void rotate_right(struct qhw_rb_tree *tree, struct qhw_rb_node *node)
{
	struct qhw_rb_node *left = node->left;

	node->left = left->right;
	if (left->right != 0) {
		left->right->parent = node;
	}
	left->parent = node->parent;
	if (node->parent == 0) {
		tree->root = left;
	} else if (node == node->parent->right) {
		node->parent->right = left;
	} else {
		node->parent->left = left;
	}
	left->right = node;
	node->parent = left;
}

void qhw_rb_tree_init(struct qhw_rb_tree *tree)
{
	tree->root = 0;
}

struct qhw_rb_node *qhw_rb_tree_find(
	struct qhw_rb_tree *tree,
	uint64_t key)
{
	struct qhw_rb_node *node;

	if (tree == 0) {
		return 0;
	}

	node = tree->root;
	while (node != 0) {
		if (key == node->key) {
			return node;
		}
		node = key < node->key ? node->left : node->right;
	}

	return 0;
}

int qhw_rb_tree_insert(
	struct qhw_rb_tree *tree,
	struct qhw_rb_node *node)
{
	struct qhw_rb_node *parent = 0;
	struct qhw_rb_node **cursor;

	if (tree == 0 || node == 0) {
		return -1;
	}

	cursor = &tree->root;
	while (*cursor != 0) {
		parent = *cursor;
		if (node->key == parent->key) {
			return -1;
		}
		cursor = node->key < parent->key ?
			&parent->left : &parent->right;
	}

	node->left = 0;
	node->right = 0;
	node->parent = parent;
	node->red = 1;
	*cursor = node;

	while (node != tree->root && node->parent->red) {
		struct qhw_rb_node *grand = node->parent->parent;

		if (node->parent == grand->left) {
			struct qhw_rb_node *uncle = grand->right;

			if (uncle != 0 && uncle->red) {
				node->parent->red = 0;
				uncle->red = 0;
				grand->red = 1;
				node = grand;
			} else {
				if (node == node->parent->right) {
					node = node->parent;
					rotate_left(tree, node);
				}
				node->parent->red = 0;
				grand->red = 1;
				rotate_right(tree, grand);
			}
		} else {
			struct qhw_rb_node *uncle = grand->left;

			if (uncle != 0 && uncle->red) {
				node->parent->red = 0;
				uncle->red = 0;
				grand->red = 1;
				node = grand;
			} else {
				if (node == node->parent->left) {
					node = node->parent;
					rotate_right(tree, node);
				}
				node->parent->red = 0;
				grand->red = 1;
				rotate_left(tree, grand);
			}
		}
	}

	tree->root->red = 0;
	return 0;
}

struct qhw_rb_node *qhw_rb_tree_first(struct qhw_rb_tree *tree)
{
	struct qhw_rb_node *node;

	if (tree == 0) {
		return 0;
	}

	node = tree->root;
	if (node == 0) {
		return 0;
	}
	while (node->left != 0) {
		node = node->left;
	}

	return node;
}

struct qhw_rb_node *qhw_rb_tree_next(struct qhw_rb_node *node)
{
	if (node == 0) {
		return 0;
	}

	if (node->right != 0) {
		node = node->right;
		while (node->left != 0) {
			node = node->left;
		}
		return node;
	}

	while (node->parent != 0 && node == node->parent->right) {
		node = node->parent;
	}
	return node->parent;
}

