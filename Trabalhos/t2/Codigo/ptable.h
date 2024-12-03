#ifndef PTABLE_H
#define PTABLE_H

#include "cpu.h"
#include "err.h"
#include "memoria.h"
#include <stdio.h>

#define QUANTUM 5

typedef enum pendency { none, read, write } pendency_t;

typedef enum pstate { blocked, ready, running } pstate;

typedef struct process process_t;
typedef struct ptable ptable_t;
typedef struct log log_t;

struct log {
    int process_created;
    int total_time;
    int time_blocked;

    int number_interruptions[5]; // so_le, so_escr, so_cria_proc, so_mata_proc,
                                 // so_espera_proc
    int number_preemptions;

    int process_created_at[4];
    int process_killed_at[4];

    int process_time[4]; // init, p1, p2, p3

    int number_preemptions_process[4]; // p1, p2, p3

    int number_states_process[4][3]; // p1, p2, p3 x ready, blocked, running

    int process_state_time[4][3]; // p1, p2, p3 x ready, blocked, running

    int mean_time[4]; // init, p1, p2, p3
};

extern log_t logs;

process_t *process_create();
void process_free(process_t *proc);

void process_save_registers(process_t *proc, mem_t *mem);
void process_load_registers(process_t *proc, mem_t *mem);

void process_printf(process_t *proc);
pstate process_state(process_t *proc);
void process_set_state(process_t *proc, pstate st);

process_t *ptable_running_process(ptable_t *ptbl);
process_t *ptable_head(ptable_t *ptbl);

int process_pid(process_t *proc);
int process_PC(process_t *proc);
int process_X(process_t *proc);
int process_A(process_t *proc);

process_t *process_next(process_t *proc);
pendency_t process_pendency(process_t *proc);

void process_set_PC(process_t *proc, int PC);
void process_set_A(process_t *proc, int A);
void process_set_pendency(process_t *proc, pendency_t pendency);
void process_dec_quantum(process_t *proc);

float process_prio(process_t *proc);

ptable_t *ptable_create();
void ptable_free(ptable_t *ptbl);

void ptable_printf(ptable_t *ptbl);
void ptable_fprintf(ptable_t *ptbl, FILE *fp);

void ptable_set_running_process(ptable_t *ptbl, process_t *proc);

void ptable_insert_process(ptable_t *ptbl, process_t *proc);
void ptable_remove_process(ptable_t *ptbl, process_t *proc);

process_t *ptable_find(ptable_t *ptbl, int pid);
void ptable_check_waiting(ptable_t *ptbl);

void ptable_move_to_end(ptable_t *ptbl);

void ptable_standard_mode(ptable_t *ptbl, bool mode);
void ptable_preemptive_mode(ptable_t *ptbl);
void ptable_priority_mode(ptable_t *ptbl);
void ptable_sort_by_priority(ptable_t *ptbl);
bool ptable_idle(ptable_t *ptbl);
void ptable_update_times(ptable_t *ptbl);

#endif // PTABLE_H
