// so.c
// sistema operacional
// simulador de computador
// so24b

#include "so.h"
#include "dispositivos.h"
#include "irq.h"
#include "programa.h"
#include "ptable.h"
#include "ulist.h"


#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


#define INTERVALO_INTERRUPCAO 50


struct so_t {
   cpu_t *cpu;
   mem_t *mem;
   es_t *es;
   console_t *console;
   bool erro_interno;
  
   ptable_t *ptbl;
   wlist_t *wlst;
   FILE *fp;
};

static int so_trata_interrupcao(void *argC, int reg_A);
static int so_carrega_programa(so_t *self, char *nome_do_executavel);
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender);

so_t *so_cria(cpu_t *cpu, mem_t *mem, es_t *es, console_t *console) {
   so_t *self = malloc(sizeof(*self));
   if (self == NULL)
       return NULL;


   self->cpu = cpu;
   self->mem = mem;
   self->es = es;
   self->console = console;
   self->erro_interno = false;


   cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);


   int ender = so_carrega_programa(self, "trata_int.maq");
   if (ender != IRQ_END_TRATADOR) {
       self->erro_interno = true;
   }


   if (es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO) !=
       ERR_OK) {
       self->erro_interno = true;
   }


   self->ptbl = ptable_create();
   self->wlst = wlist_alloc();


   self->fp = fopen("logs.txt", "w");


   return self;
}


void so_destroi(so_t *self) {
   cpu_define_chamaC(self->cpu, NULL, NULL);
   ptable_free(self->ptbl);


   fclose(self->fp);
   free(self);
}


static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_irq(so_t *self, int irq);
static void so_trata_pendencias(so_t *self);
static void so_escalona(so_t *self);
static int so_despacha(so_t *self);


static int so_trata_interrupcao(void *argC, int reg_A) {
   so_t *self = argC;
   irq_t irq = reg_A;


   so_salva_estado_da_cpu(self);


   so_trata_irq(self, irq);


   so_trata_pendencias(self);


   so_escalona(self);


   return so_despacha(self);
}


static void so_salva_estado_da_cpu(so_t *self) {


   process_t *running = ptable_running_process(self->ptbl);


   if (running) {
       process_save_registers(running, self->mem);
   }
}


void so_resolve_read(so_t *self, process_t *proc) {


   dispositivo_id_t check_disp = process_pid(proc) * 4 + D_TERM_A_TECLADO_OK;
   dispositivo_id_t access_disp = process_pid(proc) * 4 + D_TERM_A_TECLADO;


   int available;


   if (es_le(self->es, check_disp, &available) != ERR_OK) {
       self->erro_interno = true;
       return;
   }


   if (!available) {
       return;
   }


   int data;
   if (es_le(self->es, access_disp, &data) != ERR_OK) {
       self->erro_interno = true;
       return;
   }


   process_set_A(proc, data);


   process_set_pendency(proc, none);
   process_set_state(proc, ready);
}


void so_resolve_write(so_t *self, process_t *proc) {


   dispositivo_id_t check_disp = process_pid(proc) * 4 + D_TERM_A_TELA_OK;
   dispositivo_id_t access_disp = process_pid(proc) * 4 + D_TERM_A_TELA;


   int available;


   if (es_le(self->es, check_disp, &available) != ERR_OK) {
       self->erro_interno = true;
       return;
   }


   if (!available) {
       return;
   }


   int data = process_X(proc);


   if (es_escreve(self->es, access_disp, data) != ERR_OK) {
       self->erro_interno = true;
       return;
   }


   process_set_A(proc, 0);


   process_set_pendency(proc, none);
   process_set_state(proc, ready);
}


static void so_trata_pendencias(so_t *self) {


   process_t *curr = ptable_head(self->ptbl);


   while (curr) {


       pendency_t pendency = process_pendency(curr);


       if (pendency == read) {
           so_resolve_read(self, curr);
       } else if (pendency == write) {
           so_resolve_write(self, curr);
       }


       curr = process_next(curr);
   }
}


static void so_escalona(so_t *self) {


   ptable_preemptive_mode(self->ptbl);
   ptable_next_ready_process_to_head(self->ptbl);


   console_printf("%d\n", ptable_count(self->ptbl));
  
}


static int so_despacha(so_t *self) {


   process_t *running = ptable_running_process(self->ptbl);


   if (self->erro_interno || !running) {
       return 1;
   }


   if (process_state(running) == blocked || process_pendency(running) != none) {
       console_printf("This should not happen.\n");
   }


   process_load_registers(running, self->mem);


   return 0;
}


static void so_trata_irq_reset(so_t *self);
static void so_trata_irq_chamada_sistema(so_t *self);
static void so_trata_irq_err_cpu(so_t *self);
static void so_trata_irq_relogio(so_t *self);
static void so_trata_irq_desconhecida(so_t *self, int irq);


static void so_trata_irq(so_t *self, int irq) {
   switch (irq) {
   case IRQ_RESET:
       so_trata_irq_reset(self);
       break;
   case IRQ_SISTEMA:
       so_trata_irq_chamada_sistema(self);
       break;
   case IRQ_ERR_CPU:
       so_trata_irq_err_cpu(self);
       break;
   case IRQ_RELOGIO:
       so_trata_irq_relogio(self);
       break;
   default:
       so_trata_irq_desconhecida(self, irq);
   }
}


static void so_trata_irq_reset(so_t *self) {
   int ender = so_carrega_programa(self, "init.maq");
   if (ender != 100) {
       self->erro_interno = true;
       return;
   }


   process_t *proc = process_create();
   process_set_PC(proc, ender);


   ptable_insert_process(self->ptbl, proc);


   ptable_set_running_process(self->ptbl, proc);


   process_load_registers(proc, self->mem);
}


static void so_trata_irq_err_cpu(so_t *self) {
   int err_int;


   mem_le(self->mem, IRQ_END_erro, &err_int);
   err_t err = err_int;
   console_printf("SO: IRQ não tratada -- erro na CPU: %s", err_nome(err));
   self->erro_interno = true;
}


static void so_trata_irq_relogio(so_t *self) {
  
   err_t e1, e2;
  
   e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0);
   e2 = es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO);


   if (e1 != ERR_OK || e2 != ERR_OK) {
       self->erro_interno = true;
   }
  
   process_t *running = ptable_running_process(self->ptbl);
   if (running) {
       process_dec_quantum(running);
   }
}


static void so_trata_irq_desconhecida(so_t *self, int irq) {
   self->erro_interno = true;
}


static void so_chamada_le(so_t *self);
static void so_chamada_escr(so_t *self);
static void so_chamada_cria_proc(so_t *self);
static void so_chamada_mata_proc(so_t *self);
static void so_chamada_espera_proc(so_t *self);


static void so_trata_irq_chamada_sistema(so_t *self) {


   process_t *running = ptable_running_process(self->ptbl);


   int id_chamada = process_A(running);


   switch (id_chamada) {
   case SO_LE:
       so_chamada_le(self);
       break;
   case SO_ESCR:
       so_chamada_escr(self);
       break;
   case SO_CRIA_PROC:
       so_chamada_cria_proc(self);
       break;
   case SO_MATA_PROC:
       so_chamada_mata_proc(self);
       break;
   case SO_ESPERA_PROC:
       so_chamada_espera_proc(self);
       break;
   default:
       console_printf("SO: chamada de sistema desconhecida (%d)", id_chamada);


       ptable_remove_process(self->ptbl, running);
       process_free(running);


       self->erro_interno = true;
   }
}


static void so_chamada_le(so_t *self) {


   process_t *running = ptable_running_process(self->ptbl);


   int teclado = 4 * process_pid(running) + D_TERM_A_TECLADO;
   int teclado_ok = 4 * process_pid(running) + D_TERM_A_TECLADO_OK;


   int available;
   if (es_le(self->es, teclado_ok, &available) != ERR_OK) {
       self->erro_interno = true;
       return;
   }


   if (!available) {
       process_set_state(running, blocked);
       process_set_pendency(running, read);
       return;
   }


   int data;
   if (es_le(self->es, teclado, &data) != ERR_OK) {
       self->erro_interno = true;
       return;
   }


   process_set_A(running, data);
}


static void so_chamada_escr(so_t *self) {


   process_t *running = ptable_running_process(self->ptbl);


   int tela = 4 * process_pid(running) + D_TERM_A_TELA;
   int tela_ok = 4 * process_pid(running) + D_TERM_A_TELA_OK;


   int available;
   if (es_le(self->es, tela_ok, &available) != ERR_OK) {
       self->erro_interno = true;
       return;
   }


   if (!available) {
       process_set_state(running, blocked);
       process_set_pendency(running, write);
       return;
   }


   int data = process_X(running);


   if (es_escreve(self->es, tela, data) != ERR_OK) {
       self->erro_interno = true;
       return;
   }


   process_set_A(running, 0);
}


static void so_chamada_cria_proc(so_t *self) {


   process_t *running = ptable_running_process(self->ptbl);


   char nome[100];


   int asm_address = process_X(running);


   if (!copia_str_da_mem(100, nome, self->mem, asm_address)) {
       process_set_A(running, -1);
       return;
   }


   int mem_address = so_carrega_programa(self, nome);


   if (mem_address <= 0) {
       process_set_A(running, -1);
       return;
   }


   process_t *created = process_create();


   process_set_PC(created, mem_address);


   ptable_insert_process(self->ptbl, created);


   process_set_A(running, process_pid(created));
}


static void so_chamada_mata_proc(so_t *self) {


   process_t *running = ptable_running_process(self->ptbl);


   int pid = process_X(running);


   process_t *found = pid == 0 ? running : ptable_find(self->ptbl, pid);


   if (found) {
       ptable_remove_process(self->ptbl, found);
       wlist_solve(self->wlst, found);


       if (pid == 0) {
           ptable_set_running_process(self->ptbl, NULL);
       }


       process_set_state(found, ready);
   }
}


static void so_chamada_espera_proc(so_t *self) {


   process_t *running = ptable_running_process(self->ptbl);


   int pid = process_X(running);


   process_t *found = ptable_find(self->ptbl, pid);


   if (found) {
       waiting_t *wt = waiting_alloc(running, found);
       wlist_insert(self->wlst, wt);
       process_set_state(running, blocked);
   } else {
       process_set_state(running, ready);
   }
}


static int so_carrega_programa(so_t *self, char *nome_do_executavel) {


   programa_t *prog = prog_cria(nome_do_executavel);
   if (prog == NULL) {
       return -1;
   }


   int end_ini = prog_end_carga(prog);
   int end_fim = end_ini + prog_tamanho(prog);


   for (int end = end_ini; end < end_fim; end++) {
       if (mem_escreve(self->mem, end, prog_dado(prog, end)) != ERR_OK) {
           return -1;
       }
   }


   prog_destroi(prog);
   return end_ini;
}


static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender) {
   for (int indice_str = 0; indice_str < tam; indice_str++) {
       int caractere;
       if (mem_le(mem, ender + indice_str, &caractere) != ERR_OK) {
           return false;
       }
       if (caractere < 0 || caractere > 255) {
           return false;
       }
       str[indice_str] = caractere;
       if (caractere == 0) {
           return true;
       }
   }


   return false;
}

