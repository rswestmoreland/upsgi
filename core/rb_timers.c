/*

        upsgi rbtree implementation based on nginx

        Copyright (C) Igor Sysoev
        Copyright (C) Nginx, Inc.
        Copyright (C) Unbit S.a.s.

        The red-black tree code is based on the algorithm described in
        the "Introduction to Algorithms" by Cormen, Leiserson and Rivest.
*/

#include "upsgi.h"

#define upsgi_rbt_red(node)               ((node)->color = 1)
#define upsgi_rbt_black(node)             ((node)->color = 0)
#define upsgi_rbt_is_red(node)            ((node)->color)
#define upsgi_rbt_is_black(node)          (!upsgi_rbt_is_red(node))
#define upsgi_rbt_copy_color(n1, n2)      (n1->color = n2->color)


struct upsgi_rbtree *upsgi_init_rb_timer() {

	struct upsgi_rbtree *tree = upsgi_calloc(sizeof(struct upsgi_rbtree));
	struct upsgi_rb_timer *sentinel = upsgi_calloc(sizeof(struct upsgi_rb_timer));
	// no need to set it black, calloc already did it
	//upsgi_rbt_black(sentinel);
	tree->root = sentinel;
	tree->sentinel = sentinel;

	return tree;
}

struct upsgi_rb_timer *upsgi_min_rb_timer(struct upsgi_rbtree *tree, struct upsgi_rb_timer *node) {

	if (!node)
		node = tree->root;
	struct upsgi_rb_timer *sentinel = tree->sentinel;

	if (tree->root == sentinel) return NULL;

	while (node->left != sentinel) {
		node = node->left;
	}

	return node;
}

static void upsgi_rbtree_lr(struct upsgi_rb_timer **root, struct upsgi_rb_timer *sentinel, struct upsgi_rb_timer *node) {

	struct upsgi_rb_timer *temp;

	temp = node->right;
	node->right = temp->left;

	if (temp->left != sentinel) {
		temp->left->parent = node;
	}

	temp->parent = node->parent;

	if (node == *root) {
		*root = temp;

	}
	else if (node == node->parent->left) {
		node->parent->left = temp;

	}
	else {
		node->parent->right = temp;
	}

	temp->left = node;
	node->parent = temp;
}


static void upsgi_rbtree_rr(struct upsgi_rb_timer **root, struct upsgi_rb_timer *sentinel, struct upsgi_rb_timer *node) {

	struct upsgi_rb_timer *temp;

	temp = node->left;
	node->left = temp->right;

	if (temp->right != sentinel) {
		temp->right->parent = node;
	}

	temp->parent = node->parent;

	if (node == *root) {
		*root = temp;

	}
	else if (node == node->parent->right) {
		node->parent->right = temp;

	}
	else {
		node->parent->left = temp;
	}

	temp->right = node;
	node->parent = temp;
}


static void upsgi_rbt_add(struct upsgi_rb_timer *temp, struct upsgi_rb_timer *node, struct upsgi_rb_timer *sentinel) {
        struct upsgi_rb_timer **p;

        for (;;) {

                p = (node->value < temp->value) ? &temp->left : &temp->right;
                if (*p == sentinel)
                        break;
                temp = *p;
        }

        *p = node;
        node->parent = temp;
        node->left = sentinel;
        node->right = sentinel;
        upsgi_rbt_red(node);
}


struct upsgi_rb_timer *upsgi_add_rb_timer(struct upsgi_rbtree *tree, uint64_t value, void *data) {

	struct upsgi_rb_timer *node = upsgi_malloc(sizeof(struct upsgi_rb_timer));
	struct upsgi_rb_timer *new_node = node;
	node->value = value;
	node->data = data;

	struct upsgi_rb_timer *temp = NULL;

	/* a binary tree insert */

	struct upsgi_rb_timer **root = &tree->root;
	struct upsgi_rb_timer *sentinel = tree->sentinel;

	if (*root == sentinel) {
		node->parent = NULL;
		node->left = sentinel;
		node->right = sentinel;
		upsgi_rbt_black(node);
		*root = node;
		return new_node;
	}

	upsgi_rbt_add(*root, node, sentinel);

	/* re-balance tree */

	while (node != *root && upsgi_rbt_is_red(node->parent)) {

		if (node->parent == node->parent->parent->left) {
			temp = node->parent->parent->right;

			if (upsgi_rbt_is_red(temp)) {
				upsgi_rbt_black(node->parent);
				upsgi_rbt_black(temp);
				upsgi_rbt_red(node->parent->parent);
				node = node->parent->parent;

			}
			else {
				if (node == node->parent->right) {
					node = node->parent;
					upsgi_rbtree_lr(root, sentinel, node);
				}

				upsgi_rbt_black(node->parent);
				upsgi_rbt_red(node->parent->parent);
				upsgi_rbtree_rr(root, sentinel, node->parent->parent);
			}

		}
		else {
			temp = node->parent->parent->left;

			if (upsgi_rbt_is_red(temp)) {
				upsgi_rbt_black(node->parent);
				upsgi_rbt_black(temp);
				upsgi_rbt_red(node->parent->parent);
				node = node->parent->parent;

			}
			else {
				if (node == node->parent->left) {
					node = node->parent;
					upsgi_rbtree_rr(root, sentinel, node);
				}

				upsgi_rbt_black(node->parent);
				upsgi_rbt_red(node->parent->parent);
				upsgi_rbtree_lr(root, sentinel, node->parent->parent);
			}
		}
	}

	upsgi_rbt_black(*root);

	return new_node;
}

void upsgi_del_rb_timer(struct upsgi_rbtree *tree, struct upsgi_rb_timer *node) {
	uint8_t red;
	struct upsgi_rb_timer **root, *sentinel, *subst, *temp, *w;

	/* a binary tree delete */

	root = &tree->root;
	sentinel = tree->sentinel;

	if (node->left == sentinel) {
		temp = node->right;
		subst = node;

	}
	else if (node->right == sentinel) {
		temp = node->left;
		subst = node;

	}
	else {
		subst = upsgi_min_rb_timer(tree, node->right);

		if (subst->left != sentinel) {
			temp = subst->left;
		}
		else {
			temp = subst->right;
		}
	}

	if (subst == *root) {
		*root = temp;
		upsgi_rbt_black(temp);
		return;
	}

	red = upsgi_rbt_is_red(subst);

	if (subst == subst->parent->left) {
		subst->parent->left = temp;

	}
	else {
		subst->parent->right = temp;
	}

	if (subst == node) {

		temp->parent = subst->parent;

	}
	else {

		if (subst->parent == node) {
			temp->parent = subst;

		}
		else {
			temp->parent = subst->parent;
		}

		subst->left = node->left;
		subst->right = node->right;
		subst->parent = node->parent;
		upsgi_rbt_copy_color(subst, node);

		if (node == *root) {
			*root = subst;

		}
		else {
			if (node == node->parent->left) {
				node->parent->left = subst;
			}
			else {
				node->parent->right = subst;
			}
		}

		if (subst->left != sentinel) {
			subst->left->parent = subst;
		}

		if (subst->right != sentinel) {
			subst->right->parent = subst;
		}
	}

	if (red) {
		return;
	}

	/* a delete fixup */

	while (temp != *root && upsgi_rbt_is_black(temp)) {

		if (temp == temp->parent->left) {
			w = temp->parent->right;

			if (upsgi_rbt_is_red(w)) {
				upsgi_rbt_black(w);
				upsgi_rbt_red(temp->parent);
				upsgi_rbtree_lr(root, sentinel, temp->parent);
				w = temp->parent->right;
			}

			if (upsgi_rbt_is_black(w->left) && upsgi_rbt_is_black(w->right)) {
				upsgi_rbt_red(w);
				temp = temp->parent;

			}
			else {
				if (upsgi_rbt_is_black(w->right)) {
					upsgi_rbt_black(w->left);
					upsgi_rbt_red(w);
					upsgi_rbtree_rr(root, sentinel, w);
					w = temp->parent->right;
				}

				upsgi_rbt_copy_color(w, temp->parent);
				upsgi_rbt_black(temp->parent);
				upsgi_rbt_black(w->right);
				upsgi_rbtree_lr(root, sentinel, temp->parent);
				temp = *root;
			}

		}
		else {
			w = temp->parent->left;

			if (upsgi_rbt_is_red(w)) {
				upsgi_rbt_black(w);
				upsgi_rbt_red(temp->parent);
				upsgi_rbtree_rr(root, sentinel, temp->parent);
				w = temp->parent->left;
			}

			if (upsgi_rbt_is_black(w->left) && upsgi_rbt_is_black(w->right)) {
				upsgi_rbt_red(w);
				temp = temp->parent;

			}
			else {
				if (upsgi_rbt_is_black(w->left)) {
					upsgi_rbt_black(w->right);
					upsgi_rbt_red(w);
					upsgi_rbtree_lr(root, sentinel, w);
					w = temp->parent->left;
				}

				upsgi_rbt_copy_color(w, temp->parent);
				upsgi_rbt_black(temp->parent);
				upsgi_rbt_black(w->left);
				upsgi_rbtree_rr(root, sentinel, temp->parent);
				temp = *root;
			}
		}
	}

	upsgi_rbt_black(temp);
}
