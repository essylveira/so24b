#ifndef PTABLE_H 
#define PTABLE_H 

#include "memoria.h"

typedef enum pstate {
    blocked,
    ready
} pstate;

typedef struct process process_t;
typedef struct ptable ptable_t;

process_t *process_create(int PC, int A, int X, int erro, int complemento, int modo);
void process_free(process_t *proc);
void process_save_registers(process_t *proc, mem_t *mem);
void process_load_registers(process_t *proc, mem_t *mem);
void process_printf(process_t *proc);
pstate process_state(process_t *proc);
void process_set_state(process_t *proc, pstate st);
unsigned int process_pid(process_t *proc);

ptable_t *ptable_create();
void ptable_free(ptable_t *);
process_t *ptable_running_process(ptable_t *);
process_t *ptable_head(ptable_t *);
void ptable_printf(ptable_t *);
void ptable_set_running_process(ptable_t *, process_t *);
void ptable_insert_process(ptable_t *, process_t *);
void ptable_remove_process(ptable_t *, process_t *);
process_t *ptable_find(ptable_t *ptbl, unsigned int pid);

#endif // PTABLE_H
