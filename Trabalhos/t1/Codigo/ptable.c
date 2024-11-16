#include "ptable.h"
#include "irq.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

struct process {
    int pid;
    int PC, A, X, erro, complemento, modo;
    int quantum;
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
    proc->prio = 0.5;
    proc->modo = usuario;
    proc->st = ready;

    return proc;
}

void process_free(process_t *proc) { free(proc); }

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
           proc->PC, proc->A, proc->X, proc->erro, proc->complemento,
           proc->modo);
    printf("%s\n", proc->st ? "ready" : "blocked");
    printf("%p\n", proc->next);
}

pstate process_state(process_t *proc) { return proc->st; }

void process_set_state(process_t *proc, pstate st) { proc->st = st; }

int process_pid(process_t *proc) { return proc->pid; }

int process_PC(process_t *proc) { return proc->PC; }

int process_X(process_t *proc) { return proc->X; }

int process_A(process_t *proc) { return proc->A; }

err_t process_erro(process_t *proc) { return proc->erro; }

cpu_modo_t process_modo(process_t *proc) { return proc->modo; }

int process_complemento(process_t *proc) { return proc->complemento; }

int process_quantum(process_t *proc) { return proc->quantum; }

process_t *process_next(process_t *proc) {
    return proc->next;
}

pendency_t process_pendency(process_t *proc) {
    return proc->pendency;
}

void process_set_PC(process_t *proc, int PC) { proc->PC = PC; }

void process_set_X(process_t *proc, int X) { proc->X = X; }

void process_set_A(process_t *proc, int A) { proc->A = A; }

void process_set_erro(process_t *proc, err_t erro) { proc->erro = erro; }

void process_set_modo(process_t *proc, cpu_modo_t modo) { proc->modo = modo; }

void process_set_complemento(process_t *proc, int complemento) {
    proc->complemento = complemento;
}

void process_set_pendency(process_t *proc, pendency_t pendency) {
    proc->pendency = pendency;
}

void process_dec_quantum(process_t *proc) { proc->quantum--; }

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
        printf("%d %p %s", process_pid(curr), curr, curr->st ? "ready" : "blocked");
        if (curr->pendency == read) {
            printf("read\n");
        } else if (curr->pendency == write) {
            printf("write\n");
        }
        curr = curr->next;
    }
}

void ptable_fprintf(ptable_t *ptbl, FILE *fp) {

    process_t *curr = ptbl->head;

    fprintf(fp, "\n\nrunning: %p\n", ptable_running_process(ptbl));

    while (curr) {
        fprintf(fp, "%d %p %s", process_pid(curr), curr, curr->st ? "ready" : "blocked");
        if (curr->pendency == read) {
            fprintf(fp, "read\n");
        } else if (curr->pendency == write) {
            fprintf(fp, "write\n");
        }
        curr = curr->next;
    }

    fprintf(fp, "\n\n");
}

process_t *ptable_running_process(ptable_t *ptbl) { return ptbl->running; }

process_t *ptable_head(ptable_t *ptbl) { return ptbl->head; }

void ptable_set_running_process(ptable_t *ptbl, process_t *proc) {
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
        ptbl->running = NULL;
    }

    proc->next = NULL;
}

process_t *ptable_next_ready_process_to_head(ptable_t *ptbl) {

    process_t *prev = NULL;
    process_t *curr = ptbl->head;

    while (curr && curr->st != ready) {
        prev = curr;
        curr = curr->next;
    }

    if (!curr) {
        return NULL;
    }

    if (prev && curr) {
        prev->next = curr->next;
        curr->next = ptbl->head;
        ptbl->head = curr;
    }

    return curr;
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

void ptable_preemptive_move(ptable_t *ptbl) {

    process_t *curr = ptbl->running;

    if (!curr) {
        return;
    }

    if (curr && curr->quantum == 0) {
        curr->quantum = QUANTUM;
        ptable_move_to_end(ptbl);
    }
}
