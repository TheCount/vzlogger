/**
 * Generic linked list
 *
 * @package vzlogger
 * @copyright Copyright (c) 2011, The volkszaehler.org project
 * @license http://www.gnu.org/licenses/gpl.txt GNU Public License
 * @author Steffen Vogel <info@steffenvogel.de>
 */
/*
 * This file is part of volkzaehler.org
 *
 * volkzaehler.org is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * volkzaehler.org is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with volkszaehler.org. If not, see <http://www.gnu.org/licenses/>.
 */
 
#ifndef _LIST_H_
#define _LIST_H_

#include <stdlib.h>

#define CONCAT2(a, b) a ## b
#define CONCAT(a, b) CONCAT2(a, b)
#define UNIQUE(prefix) CONCAT(__ ## prefix ## _, __LINE__ )

#define foreach(list, value, type) \
	__list_item_t *UNIQUE(it) = (list).head; \
	for( \
		type * (value) = UNIQUE(it)->data; \
		({ \
			if (UNIQUE(it)) { \
				(value) = UNIQUE(it)->data; \
			} \
			; UNIQUE(it) != NULL; \
		}); \
		UNIQUE(it) = UNIQUE(it)->next \
	) \

typedef struct __list_item {
	void *data;
	struct __list_item *prev;
	struct __list_item *next;
} __list_item_t;

typedef struct {
	size_t size;
	__list_item_t *head;
	__list_item_t *tail;
} list_t;

inline void list_init(list_t *list) {
	list->size = 0;
	list->head = list->tail = NULL;
}

inline size_t list_push(list_t *list, void *data) {
	__list_item_t *new = malloc(sizeof(__list_item_t));

	if (new == NULL) return -1; /* cannot allocate memory */

	new->data = data;
	new->prev = list->tail;
	new->next = NULL;

	if (list->tail == NULL) {
		list->head = new;
	}
	else {
		list->tail->next = new;
	}

	list->tail = new;
	list->size = list->size + 1;

	return list->size;
}

inline void * list_pop(list_t *list) {
	__list_item_t *old = list->tail;

	if (old == NULL) {
		return NULL;
	}

	void *data = old->data;

	list->tail = old->prev;
	list->size--;

	free(old);

	return data;
}

inline void list_free(list_t *list) {
	while (list->head != NULL) {
		__list_item_t *old = list->head;
		list->head = old->next;

		free(old->data);
		free(old);
	}

	list_init(list);
}

#endif /* _LIST_H_ */
