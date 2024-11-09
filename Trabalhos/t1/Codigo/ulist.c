#include "ulist.h"
#include "dispositivos.h"
#include <stdlib.h>


unresolved_t *unresolved_alloc(process_t *proc, dispositivo_id_t check,
                               dispositivo_id_t access, disp_kind_t disp_kind) {
    unresolved_t *ur = malloc(sizeof(unresolved_t));

    ur->proc = proc;
    ur->check_disp = check;
    ur->access_disp = access;
    ur->disp_kind = disp_kind;

    return ur;
}

process_t *unresolved_proc(unresolved_t *ur) {
    return ur->proc;
}

dispositivo_id_t unresolved_check_disp(unresolved_t *ur) {
    return ur->check_disp;
}

dispositivo_id_t unresolved_access_disp(unresolved_t *ur) {
    return ur->access_disp;
}

disp_kind_t unresolved_disp_kind(unresolved_t *ur) {
    return ur->disp_kind;
}

ulist_t *ulist_alloc() {

    ulist_t *ulst = calloc(1, sizeof(ulist_t));

    return ulst;
}

void ulist_insert(ulist_t *ulst, unresolved_t *ur) {

    unresolved_t *curr = ulst->head;

    while (curr && curr->next) {
        curr = curr->next;
    }

    if (curr) {
        curr->next = ur;
    } else {
        ulst->head = ur;
    }
}

void ulist_remove(ulist_t *ulst, unresolved_t *ur) {
    
    unresolved_t *prev = NULL;
    unresolved_t *curr = ulst->head;

    while (curr) {
        prev = curr;
        curr = curr->next;
    }

    if (prev) {
        prev->next = 0;
    } else {
    }
}
