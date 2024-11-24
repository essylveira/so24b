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
   int t_exec;
   float prio;
   pstate st;
   process_t *next;
   pendency_t pendency;
};


struct ptable {
   process_t *running;
   process_t *head;
};

struct log{
    int process_created;
    int total_time;
    int time_blocked;

    int number_interruptions[3]; // read, write, clock
    int number_preemptions;

    int process_time[3]; // p1, p2, p3

    int number_preemptions_process[3]; // p1, p2, p3
    
    int number_states_process[3][3]; // p1, p2, p3 x ready, blocked, running

    int process_state_time[3][3]; // p1, p2, p3 x ready, blocked, running

    int mean_time[3]; // p1, p2, p3
};

log_t *log_create() {

   log_t *log = calloc(1, sizeof(ptable_t));

   return log;
}

void log_free(log_t *log) { free(log); }

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
   if (st == blocked) {
       proc->prio += (float)( proc->t_exec / QUANTUM);
   }
}


process_t *ptable_running_process(ptable_t *ptbl) { return ptbl->running; }


process_t *ptable_head(ptable_t *ptbl) { return ptbl->head; }




int process_pid(process_t *proc) { return proc->pid; }


int process_PC(process_t *proc) { return proc->PC; }


int process_X(process_t *proc) { return proc->X; }


int process_A(process_t *proc) { return proc->A; }


process_t *process_next(process_t *proc) {
   return proc->next;
}


pendency_t process_pendency(process_t *proc) {
   return proc->pendency;
}


void process_set_PC(process_t *proc, int PC) { proc->PC = PC; }


void process_set_A(process_t *proc, int A) { proc->A = A; }


void process_set_pendency(process_t *proc, pendency_t pendency) {
   proc->pendency = pendency;
}


void process_dec_quantum(process_t *proc) {
   proc->quantum--;
   proc->t_exec++;
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
       printf("%d %p %s", process_pid(curr), curr, curr->st ? "ready" : "blocked");
       if (curr->pendency == read) {
           printf(" read\n");
       } else if (curr->pendency == write) {
           printf(" write\n");
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


void ptable_set_running_process(ptable_t *ptbl, process_t *proc) {
   if (proc) {
       proc->quantum = QUANTUM;
       proc->t_exec = 0;
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
       ptable_set_running_process(ptbl,NULL);
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


int ptable_count(ptable_t *ptbl) {
   process_t *curr = ptbl->head;
   int i = 0;
   while (curr) {
       i++;
       curr = curr->next;
   }
   return i;
}


process_t *ptable_next_ready_process_to_head(ptable_t *ptbl) {


   process_t *prev = NULL;
   process_t *curr = ptbl->head;


   while (curr && curr->st != ready) {


       prev = curr;
       curr = curr->next;
   }


   if (prev && curr) {
       prev->next = curr->next;
       curr->next = ptbl->head;
       ptbl->head = curr;
   }


   if (ptbl->running != curr) {
       ptable_set_running_process(ptbl,curr);
   }

   return curr;
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


void ptable_standard_mode(ptable_t *ptbl) {

    ptable_next_ready_process_to_head(ptbl);

}

void ptable_preemptive_mode(ptable_t *ptbl) {

   process_t *curr = ptbl->running;

   if (!curr) {
       return;
   }

   if (curr && curr->quantum == 0) {
       ptable_move_to_end(ptbl);
   }

   ptable_next_ready_process_to_head(ptbl);

}

void ptable_priority_mode(ptable_t *ptbl) {
    
    process_t *curr = ptbl->running;

   if (!curr) {
       return;
   }

   if (curr && curr->quantum == 0) {
       curr->prio += (float)(curr->t_exec / QUANTUM);
       ptable_move_to_end(ptbl);
   }

   ptable_next_ready_process_to_head(ptbl);

}





