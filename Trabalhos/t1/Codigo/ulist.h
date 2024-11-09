#include "dispositivos.h"
#include "ptable.h"
#include "so.h"
#include <stdbool.h>

typedef struct unresolved unresolved_t;
typedef struct ulist ulist_t;

typedef enum disp_kind {
    read,
    write
} disp_kind_t;

typedef struct unresolved {
    process_t *proc;
    dispositivo_id_t check_disp;
    dispositivo_id_t access_disp;
    disp_kind_t disp_kind;
    unresolved_t *next;
} unresolved_t;

typedef struct ulist {
    unresolved_t *head;
} ulist_t;


unresolved_t *unresolved_alloc(process_t *proc, dispositivo_id_t check_disp,
                               dispositivo_id_t access_disp, disp_kind_t should_read);
process_t *unresolved_proc(unresolved_t *ur);
dispositivo_id_t unresolved_check_disp(unresolved_t *ur);
dispositivo_id_t unresolved_access_disp(unresolved_t *ur);
disp_kind_t unresolved_disp_kind(unresolved_t *ur);

ulist_t *ulist_alloc();
void ulist_insert(ulist_t *ulst, unresolved_t *ur);
unresolved_t *ulist_next(ulist_t *ulst);
