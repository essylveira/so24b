#include "ulist.h"
#include <stdio.h>
#include <stdlib.h>

waiting_t *waiting_alloc(process_t *waiting, process_t *to_be_waited) {
    waiting_t *wt = calloc(1, sizeof(waiting_t));

    wt->waiting = waiting;
    wt->to_be_waited = to_be_waited;

    return wt;
}

wlist_t *wlist_alloc() {
    wlist_t *wlst = calloc(1, sizeof(wlist_t));
    return wlst;
}

void wlist_insert(wlist_t *wlst, waiting_t *wt) {

    waiting_t *curr = wlst->head;

    while (curr && curr->next) {
        curr = curr->next;
    }

    if (curr) {
        curr->next = wt;
    } else {
        wlst->head = wt;
    }
}

void wlist_solve(wlist_t *wlst, process_t *to_be_waited) {

    waiting_t *prev = NULL;
    waiting_t *curr = wlst->head;

    while (curr) {
        if (curr->to_be_waited == to_be_waited) {
            process_set_state(curr->waiting, ready);
            if (prev) {
                prev->next = curr->next;
                curr = prev->next;
            } else {
                curr = NULL;
                wlst->head = NULL;
            }
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
}
