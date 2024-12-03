#include "ptable.h"
#include "console.h"
#include "irq.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

struct process {
    int pid;
    int PC, A, X, erro, complemento, modo;
    int quantum;
    float t_exec;
    float prio;
    pstate st;
    process_t *next;
    pendency_t pendency;
};

struct ptable {
    process_t *running;
    process_t *head;
};

process_t *process_create() {
    process_t *proc = calloc(1, sizeof(process_t));

    static int pid = 0;
    proc->pid = pid++;

    proc->quantum = QUANTUM;
    proc->t_exec = 0;
    proc->prio = 0.5;
    proc->modo = usuario;
    proc->st = ready;

    return proc;
}

void process_free(process_t *proc) {
    free(proc);
}

void process_save_registers(process_t *proc, mem_t *mem) {
    mem_le(mem, IRQ_END_PC, &proc->PC);
    mem_le(mem, IRQ_END_A, &proc->A);
    mem_le(mem, IRQ_END_X, &proc->X);
    mem_le(mem, IRQ_END_erro, &proc->erro);
    mem_le(mem, IRQ_END_complemento, &proc->complemento);
    mem_le(mem, IRQ_END_modo, &proc->modo);
}

void process_load_registers(process_t *proc, mem_t *mem) {
    mem_escreve(mem, IRQ_END_PC, proc->PC);
    mem_escreve(mem, IRQ_END_A, proc->A);
    mem_escreve(mem, IRQ_END_X, proc->X);
    mem_escreve(mem, IRQ_END_erro, proc->erro);
    mem_escreve(mem, IRQ_END_complemento, proc->complemento);
    mem_escreve(mem, IRQ_END_modo, proc->modo);
}

void process_printf(process_t *proc) {

    if (!proc) {
        printf("NULL\n");
        return;
    }

    printf("PID: %u\n", proc->pid);
    printf("PC: %d\nA: %d\nX: %d\nerro: %d\ncomplemento: %d\nmodo: %d\n",
           proc->PC,
           proc->A,
           proc->X,
           proc->erro,
           proc->complemento,
           proc->modo);
    printf("%s\n", proc->st ? "ready" : "blocked");
    printf("%p\n", proc->next);
}

pstate process_state(process_t *proc) {
    return proc->st;
}

void process_set_state(process_t *proc, pstate st) {
    proc->st = st;
    if (st == blocked) {
        // prio = (prio + t_exec/t_quantum) / 2
        proc->prio = (proc->prio + proc->t_exec / QUANTUM) / 2;
    }
    logs.number_states_process[proc->pid][st]++;
}

process_t *ptable_running_process(ptable_t *ptbl) {
    return ptbl->running;
}

process_t *ptable_head(ptable_t *ptbl) {
    return ptbl->head;
}

int process_pid(process_t *proc) {
    return proc->pid;
}

int process_PC(process_t *proc) {
    return proc->PC;
}

int process_X(process_t *proc) {
    return proc->X;
}

int process_A(process_t *proc) {
    return proc->A;
}

process_t *process_next(process_t *proc) {
    return proc->next;
}

pendency_t process_pendency(process_t *proc) {
    return proc->pendency;
}

void process_set_PC(process_t *proc, int PC) {
    proc->PC = PC;
}

void process_set_A(process_t *proc, int A) {
    proc->A = A;
}

void process_set_pendency(process_t *proc, pendency_t pendency) {
    proc->pendency = pendency;
}

void process_dec_quantum(process_t *proc) {
    proc->quantum--;
    proc->t_exec++;
}

float process_prio(process_t *proc) {
    return proc->prio;
}

ptable_t *ptable_create() {

    ptable_t *ptbl = calloc(1, sizeof(ptable_t));

    return ptbl;
}

void ptable_free(ptable_t *ptbl) {
    process_t *curr = ptbl->head;
    process_t *next;

    while (curr) {
        next = curr->next;
        process_free(curr);
        curr = next;
    }

    free(ptbl);
}

void ptable_printf(ptable_t *ptbl) {

    process_t *curr = ptbl->head;

    printf("running: %p\n", ptable_running_process(ptbl));

    while (curr) {
        printf("%d %p %s",
               process_pid(curr),
               curr,
               curr->st ? "ready" : "blocked");
        if (curr->pendency == read) {
            printf(" read\n");
        } else if (curr->pendency == write) {
            printf(" write\n");
        }
        curr = curr->next;
    }
}

void ptable_set_running_process(ptable_t *ptbl, process_t *proc) {
    if (proc) {
        proc->quantum = QUANTUM;
        proc->t_exec = 0;
        logs.number_states_process[proc->pid][running]++;
    }

    ptbl->running = proc;
}

void ptable_insert_process(ptable_t *ptbl, process_t *proc) {
    process_t *curr = ptbl->head;

    while (curr && curr->next) {
        curr = curr->next;
    }

    if (curr) {
        curr->next = proc;
    } else {
        ptbl->head = proc;
    }

    proc->next = NULL;
}

void ptable_remove_process(ptable_t *ptbl, process_t *proc) {
    process_t *prev = NULL;
    process_t *curr = ptbl->head;

    while (curr != proc) {
        prev = curr;
        curr = curr->next;
    }

    if (!curr) {
        return;
    }

    if (prev) {
        prev->next = curr->next;
    } else {
        ptbl->head = curr->next;
        ptable_set_running_process(ptbl, NULL);
    }

    proc->next = NULL;
}

process_t *ptable_find(ptable_t *ptbl, int pid) {
    process_t *curr = ptbl->head;

    while (curr && curr->pid != pid) {
        curr = curr->next;
    }

    return curr;
}

// :|
void ptable_check_waiting(ptable_t *ptbl) {

    process_t *curr = ptbl->head;

    while (curr) {
        if (curr->st == blocked && curr->A == 9 &&
            !ptable_find(ptbl, curr->X)) {
            curr->st = ready;
        }

        curr = curr->next;
    }
}

void ptable_standard_mode(ptable_t *ptbl, bool mode) {

    process_t *prev = NULL;
    process_t *curr = ptbl->head;

    while (curr && curr->st != ready) {
        prev = curr;
        curr = curr->next;
    }

    if (prev && curr && !mode) {
        prev->next = curr->next;
        curr->next = ptbl->head;
        ptbl->head = curr;
    }

    if (ptbl->running && ptbl->running == curr && ptbl->running->quantum == 0) {
        ptbl->running->quantum = QUANTUM;
    }

    if (ptbl->running != curr) {
        ptable_set_running_process(ptbl, curr);
    }
}

void ptable_move_to_end(ptable_t *ptbl) {

    process_t *curr = ptbl->head;
    process_t *last = ptbl->head;

    if (!curr || !curr->next) {
        return;
    }

    while (last->next) {
        last = last->next;
    }

    ptbl->head = curr->next;

    last->next = curr;
    curr->next = NULL;
}

void ptable_preemptive_mode(ptable_t *ptbl) {

    process_t *curr = ptbl->running;

    if (!curr) {
        return;
    }

    if (curr && curr->quantum == 0) {
        logs.number_preemptions++;
        logs.number_preemptions_process[curr->pid]++;

        ptable_move_to_end(ptbl);
    }

    curr = ptbl->head;
    while (curr) {
        console_printf("pid: %d, quantum: %d, state: %d",
                       curr->pid,
                       curr->quantum,
                       curr->st);
        curr = curr->next;
    }
    console_printf("----------------");
}

static process_t *ptable_minimum_prio(ptable_t *ptbl) {

    if (!ptbl->head) {
        return NULL;
    }

    process_t *prev = NULL;
    process_t *curr = ptbl->head;

    process_t *minprev = NULL;
    process_t *minimum = ptbl->head;

    while (curr) {
        if (curr->prio < minimum->prio) {
            minprev = prev;
            minimum = curr;
        }

        prev = curr;
        curr = curr->next;
    }

    if (minprev) {
        minprev->next = minimum->next;
    } else {
        ptbl->head = minimum->next;
    }

    minimum->next = NULL;

    return minimum;
}

void ptable_sort_by_priority(ptable_t *ptbl) {

    process_t *head = ptable_minimum_prio(ptbl);
    process_t *curr = head;

    while (curr) {
        curr->next = ptable_minimum_prio(ptbl);
        curr = curr->next;
    }

    ptbl->head = head;
}

void ptable_priority_mode(ptable_t *ptbl) {

    process_t *curr = ptbl->running;

    if (!curr) {
        return;
    }

    if (curr && curr->quantum == 0) {
        logs.number_preemptions++;
        logs.number_preemptions_process[curr->pid]++;

        // prio = (prio + t_exec/t_quantum) / 2
        curr->prio = (curr->prio + curr->t_exec / QUANTUM) / 2.0;
    }

    ptable_sort_by_priority(ptbl);

    curr = ptbl->head;
    while (curr) {
        console_printf("pid: %d, prio: %f, quantum: %d, state: %d",
                       curr->pid,
                       curr->prio,
                       curr->quantum,
                       curr->st);
        curr = curr->next;
    }
    console_printf("----------------");
}

bool ptable_idle(ptable_t *ptbl) {

    process_t *curr = ptbl->head;

    while (curr) {
        if (curr->st != blocked) {
            return false;
        }
        curr = curr->next;
    }

    return true;
}

void ptable_update_times(ptable_t *ptbl) {

    process_t *curr = ptbl->head;

    while (curr) {
        if (curr == ptbl->running) {
            logs.process_state_time[curr->pid][2]++;
        } else {
            logs.process_state_time[curr->pid][curr->st]++;
        }
        curr = curr->next;
    }
}
