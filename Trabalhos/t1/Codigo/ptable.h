#ifndef PTABLE_H 
#define PTABLE_H 

#include "memoria.h"
#include "cpu.h"
#include "err.h"

typedef enum pstate {
    blocked,
    ready
} pstate;

typedef struct process process_t;
typedef struct ptable ptable_t;

process_t *process_create();
void process_free(process_t *proc);

void process_save_registers(process_t *proc, mem_t *mem);
void process_load_registers(process_t *proc, mem_t *mem);
void process_printf(process_t *proc);

pstate process_state(process_t *proc);
void process_set_state(process_t *proc, pstate st);

int process_pid(process_t *proc);

int process_PC(process_t *proc);
int process_X(process_t *proc);
int process_A(process_t *proc);
err_t process_erro(process_t *proc);
int process_complemento(process_t *proc);
cpu_modo_t process_modo(process_t *proc);

void process_set_PC(process_t *proc, int PC);
void process_set_X(process_t *proc, int X);
void process_set_A(process_t *proc, int A);
void process_set_erro(process_t *proc, err_t erro);
void process_set_complemento(process_t *proc, int complemento);
void process_set_modo(process_t *proc, cpu_modo_t modo);

ptable_t *ptable_create();
void ptable_free(ptable_t *ptbl);
process_t *ptable_running_process(ptable_t *ptbl);
process_t *ptable_head(ptable_t *ptbl);
void ptable_printf(ptable_t *ptbl);
void ptable_set_running_process(ptable_t *ptbl, process_t *proc);
void ptable_insert_process(ptable_t *ptbl, process_t *proc);
void ptable_remove_process(ptable_t *ptbl, process_t *proc);
process_t *ptable_next_ready_process_to_head(ptable_t *ptbl);
process_t *ptable_find(ptable_t *ptbl, unsigned int pid);

#endif // PTABLE_H
