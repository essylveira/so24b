// so.c
// sistema operacional
// simulador de computador
// so24b

// INCLUDES {{{1
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

// CONSTANTES E TIPOS {{{1
// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50 // em instruções executadas

struct so_t {
    cpu_t *cpu;
    mem_t *mem;
    es_t *es;
    console_t *console;
    bool erro_interno;
    // tabela de processos, processo corrente, pendências, etc
    ptable_t *ptbl;
    wlist_t *wlst;
    FILE *fp;
};

// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares
// carrega o programa contido no arquivo na memória do processador; retorna end.
// inicial
static int so_carrega_programa(so_t *self, char *nome_do_executavel);
// copia para str da memória do processador, até copiar um 0 (retorna true) ou
// tam bytes
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender);

// CRIAÇÃO {{{1

so_t *so_cria(cpu_t *cpu, mem_t *mem, es_t *es, console_t *console) {
    so_t *self = malloc(sizeof(*self));
    if (self == NULL)
        return NULL;

    self->cpu = cpu;
    self->mem = mem;
    self->es = es;
    self->console = console;
    self->erro_interno = false;

    // quando a CPU executar uma instrução CHAMAC, deve chamar a função
    //   so_trata_interrupcao, com primeiro argumento um ptr para o SO
    cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

    // coloca o tratador de interrupção na memória
    // quando a CPU aceita uma interrupção, passa para modo supervisor,
    //   salva seu estado à partir do endereço 0, e desvia para o endereço
    //   IRQ_END_TRATADOR
    // colocamos no endereço IRQ_END_TRATADOR o programa de tratamento
    //   de interrupção (escrito em asm). esse programa deve conter a
    //   instrução CHAMAC, que vai chamar so_trata_interrupcao (como
    //   foi definido acima)
    int ender = so_carrega_programa(self, "trata_int.maq");
    if (ender != IRQ_END_TRATADOR) {
        // console_printf(
        //     "SO: problema na carga do programa de tratamento de
        //     interrupção");
        self->erro_interno = true;
    }

    // programa o relógio para gerar uma interrupção após INTERVALO_INTERRUPCAO
    if (es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO) !=
        ERR_OK) {
        // console_printf("SO: problema na programação do timer");
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

// TRATAMENTO DE INTERRUPÇÃO {{{1

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_irq(so_t *self, int irq);
static void so_trata_pendencias(so_t *self);
static void so_escalona(so_t *self);
static int so_despacha(so_t *self);

// função a ser chamada pela CPU quando executa a instrução CHAMAC, no tratador
// de
//   interrupção em assembly
// essa é a única forma de entrada no SO depois da inicialização
// na inicialização do SO, a CPU foi programada para chamar esta função para
// executar
//   a instrução CHAMAC
// a instrução CHAMAC só deve ser executada pelo tratador de interrupção
//
// o primeiro argumento é um ponteiro para o SO, o segundo é a identificação
//   da interrupção
// o valor retornado por esta função é colocado no registrador A, e pode ser
//   testado pelo código que está após o CHAMAC. No tratador de interrupção em
//   assembly esse valor é usado para decidir se a CPU deve retornar da
//   interrupção (e executar o código de usuário) ou executar PARA e ficar
//   suspensa até receber outra interrupção
static int so_trata_interrupcao(void *argC, int reg_A) {
    so_t *self = argC;
    irq_t irq = reg_A;
    // esse print polui bastante, recomendo tirar quando estiver com mais
    // confiança
    // console_printf("SO: recebi IRQ %d (%s)", irq, irq_nome(irq));
    // salva o estado da cpu no descritor do processo que foi interrompido
    so_salva_estado_da_cpu(self);
    // faz o atendimento da interrupção
    so_trata_irq(self, irq);
    // faz o processamento independente da interrupção
    so_trata_pendencias(self);
    // escolhe o próximo processo a executar
    so_escalona(self);
    // recupera o estado do processo escolhido
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

    ptable_preemptive_move(self->ptbl);
    ptable_next_ready_process_to_head(self->ptbl);

    console_printf("%d\n", ptable_count(self->ptbl));
    
}

static int so_despacha(so_t *self) {
    // se houver processo corrente, coloca o estado desse processo onde ele
    //   será recuperado pela CPU (em IRQ_END_*) e retorna 0, senão retorna 1
    // o valor retornado será o valor de retorno de CHAMAC

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

// TRATAMENTO DE UMA IRQ {{{1

// funções auxiliares para tratar cada tipo de interrupção
static void so_trata_irq_reset(so_t *self);
static void so_trata_irq_chamada_sistema(so_t *self);
static void so_trata_irq_err_cpu(so_t *self);
static void so_trata_irq_relogio(so_t *self);
static void so_trata_irq_desconhecida(so_t *self, int irq);

static void so_trata_irq(so_t *self, int irq) {
    // verifica o tipo de interrupção que está acontecendo, e atende de acordo
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

// interrupção gerada uma única vez, quando a CPU inicializa
static void so_trata_irq_reset(so_t *self) {
    // deveria criar um processo para o init, e inicializar o estado do
    //   processador para esse processo com os registradores zerados, exceto
    //   o PC e o modo.
    // como não tem suporte a processos, está carregando os valores dos
    //   registradores diretamente para a memória, de onde a CPU vai carregar
    //   para os seus registradores quando executar a instrução RETI

    // coloca o programa init na memória
    int ender = so_carrega_programa(self, "init.maq");
    if (ender != 100) {
        // console_printf("SO: problema na carga do programa inicial");
        self->erro_interno = true;
        return;
    }

    process_t *proc = process_create();
    process_set_PC(proc, ender);

    ptable_insert_process(self->ptbl, proc);

    ptable_set_running_process(self->ptbl, proc);

    process_load_registers(proc, self->mem);

    // // altera o PC para o endereço de carga
    // mem_escreve(self->mem, IRQ_END_PC, ender);
    // // passa o processador para modo usuário
    // mem_escreve(self->mem, IRQ_END_modo, usuario);
}

// interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t *self) {
    // Ocorreu um erro interno na CPU
    // O erro está codificado em IRQ_END_erro
    // Em geral, causa a morte do processo que causou o erro
    // Ainda não temos processos, causa a parada da CPU
    int err_int;
    // t1: com suporte a processos, deveria pegar o valor do registrador erro
    //   no descritor do processo corrente, e reagir de acordo com esse erro
    //   (em geral, matando o processo)
    mem_le(self->mem, IRQ_END_erro, &err_int);
    err_t err = err_int;
    console_printf("SO: IRQ não tratada -- erro na CPU: %s", err_nome(err));
    self->erro_interno = true;
}

// interrupção gerada quando o timer expira
static void so_trata_irq_relogio(so_t *self) {
    // rearma o interruptor do relógio e reinicializa o timer para a próxima
    // interrupção
    err_t e1, e2;
    // desliga o sinalizador de interrupção
    e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0);
    e2 = es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO);

    if (e1 != ERR_OK || e2 != ERR_OK) {
        // console_printf("SO: problema da reinicialização do timer");
        self->erro_interno = true;
    }
    // t1: deveria tratar a interrupção
    //   por exemplo, decrementa o quantum do processo corrente, quando se tem
    //   um escalonador com quantum
    // console_printf("SO: interrupção do relógio (não tratada)");
    process_t *running = ptable_running_process(self->ptbl);
    if (running) {
        process_dec_quantum(running);
    }
}

// foi gerada uma interrupção para a qual o SO não está preparado
static void so_trata_irq_desconhecida(so_t *self, int irq) {
    // console_printf("SO: não sei tratar IRQ %d (%s)", irq, irq_nome(irq));
    self->erro_interno = true;
}

// CHAMADAS DE SISTEMA {{{1

// funções auxiliares para cada chamada de sistema
static void so_chamada_le(so_t *self);
static void so_chamada_escr(so_t *self);
static void so_chamada_cria_proc(so_t *self);
static void so_chamada_mata_proc(so_t *self);
static void so_chamada_espera_proc(so_t *self);

static void so_trata_irq_chamada_sistema(so_t *self) {
    // a identificação da chamada está no registrador A
    // com processos, o reg A tá no descritor do processo corrente

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

        // deveria matar o processo
        ptable_remove_process(self->ptbl, running);
        process_free(running);

        self->erro_interno = true;
    }
}

// implementação da chamada se sistema SO_LE
// faz a leitura de um dado da entrada corrente do processo, coloca o dado no
// reg A
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

// implementação da chamada se sistema SO_ESCR
// escreve o valor do reg X na saída corrente do processo
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

// implementação da chamada se sistema SO_CRIA_PROC
// cria um processo
static void so_chamada_cria_proc(so_t *self) {
    // T1: deveria criar um novo processo
    // em X está o endereço onde está o nome do arquivo

    process_t *running = ptable_running_process(self->ptbl);

    char nome[100];

    // deveria ler o X do descritor do processo criador
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

    // deveria escrever no PC do descritor do processo criado
    process_set_PC(created, mem_address);

    ptable_insert_process(self->ptbl, created);

    // Deveria escrever -1 (se erro) ou o PID do processo criado (se OK) no
    // reg A do processo que pediu a criação
    process_set_A(running, process_pid(created));
}

// implementação da chamada se sistema SO_MATA_PROC
// mata o processo com pid X (ou o processo corrente se X é 0)
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

// implementação da chamada se sistema SO_ESPERA_PROC
// espera o fim do processo com pid X
static void so_chamada_espera_proc(so_t *self) {
    // T1: deveria bloquear o processo se for o caso (e desbloquear na morte
    // do esperado) ainda sem suporte a processos, retorna erro -1

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

// CARGA DE PROGRAMA {{{1

// carrega o programa na memória
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, char *nome_do_executavel) {
    // programa para executar na nossa CPU
    programa_t *prog = prog_cria(nome_do_executavel);
    if (prog == NULL) {
        // console_printf("Erro na leitura do programa '%s'\n",
        //                nome_do_executavel);
        return -1;
    }

    int end_ini = prog_end_carga(prog);
    int end_fim = end_ini + prog_tamanho(prog);

    for (int end = end_ini; end < end_fim; end++) {
        if (mem_escreve(self->mem, end, prog_dado(prog, end)) != ERR_OK) {
            // console_printf("Erro na carga da memória, endereco %d\n", end);
            return -1;
        }
    }

    prog_destroi(prog);
    // console_printf("SO: carga de '%s' em %d-%d", nome_do_executavel, end_ini,
    //                end_fim);
    return end_ini;
}

// ACESSO À MEMÓRIA DOS PROCESSOS {{{1

// copia uma string da memória do simulador para o vetor str.
// retorna false se erro (string maior que vetor, valor não char na memória,
//   erro de acesso à memória)
// T1: deveria verificar se a memória pertence ao processo
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
    // estourou o tamanho de str
    return false;
}

// vim: foldmethod=marker
