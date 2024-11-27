#include "dispositivos.h"
#include "ptable.h"
#include "so.h"
#include <stdbool.h>

extern log_t logs;

typedef struct waiting waiting_t;
typedef struct wlist wlist_t;

struct waiting {
    process_t *waiting;
    process_t *to_be_waited;
    waiting_t *next;
};

struct wlist {
    waiting_t *head;
};

waiting_t *waiting_alloc(process_t *waiting, process_t *to_be_waited);
wlist_t *wlist_alloc();
void wlist_insert(wlist_t *wlst, waiting_t *wt);
void wlist_solve(wlist_t *wlst, process_t *to_be_waited);
