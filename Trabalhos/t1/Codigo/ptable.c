#include "ptable.h"
#include "irq.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

struct process {
    unsigned int pid;
    int PC, A, X, erro, complemento, modo;
    pstate st;
    process_t *next;
};

struct ptable {
    process_t *running;
    process_t *head;
};

process_t *process_create(int PC, int A, int X, int erro, int complemento,
                          int modo) {
    process_t *proc = malloc(sizeof(process_t));
    assert(proc != NULL);

    static unsigned int pid = 0;
    proc->pid = pid++;

    proc->st = ready;

    proc->PC = PC;
    proc->A = A;
    proc->X = X;
    proc->erro = erro;
    proc->complemento = complemento;
    proc->modo = modo;

    proc->next = NULL;

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

void process_set_state(process_t *proc, pstate st) {
    proc->st = st;
}

unsigned int process_pid(process_t *proc) { return proc->pid; }

ptable_t *ptable_create() {
    ptable_t *ptbl = malloc(sizeof(ptable_t));
    assert(ptbl != NULL);

    ptbl->running = NULL;
    ptbl->head = NULL;

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
        printf("%d %p\n", process_pid(curr), curr);
        curr = curr->next;
    }
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

// Esta função assume que `proc` está em `ptbl`.
// Criar uma função para excluir totalmente o processo.
void ptable_remove_process(ptable_t *ptbl, process_t *proc) {
    process_t *prev = NULL;
    process_t *curr = ptbl->head;

    while (curr != proc) {
        prev = curr;
        curr = curr->next;
    }

    if (prev) {
        prev->next = curr->next;
    } else {
        ptbl->head = NULL;
    }

    proc->next = NULL;
}

process_t *ptable_find(ptable_t *ptbl, unsigned int pid) {
    process_t *curr = ptbl->head;

    while (curr && curr->pid != pid) {
        curr = curr->next;
    }

    return curr;
}
