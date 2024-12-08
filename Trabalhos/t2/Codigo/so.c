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
#include "tabpag.h"
#include "ulist.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

// CONSTANTES E TIPOS {{{1
// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50 // em instruções executadas

// Não tem processos nem memória virtual, mas é preciso usar a paginação,
//   pelo menos para implementar relocação, já que os programas estão sendo
//   todos montados para serem executados no endereço 0 e o endereço 0
//   físico é usado pelo hardware nas interrupções.
// Os programas estão sendo carregados no início de um quadro, e usam quantos
//   quadros forem necessárias. Para isso a variável quadro_livre contém
//   o número do primeiro quadro da memória principal que ainda não foi usado.
//   Na carga do processo, a tabela de páginas (deveria ter uma por processo,
//   mas não tem processo) é alterada para que o endereço virtual 0 resulte
//   no quadro onde o programa foi carregado. Com isso, o programa carregado
//   é acessível, mas o acesso ao anterior é perdido.

// t2: a interface de algumas funções que manipulam memória teve que ser alterada,
//   para incluir o processo ao qual elas se referem. Para isso, precisa de um
//   tipo para o processo. Neste código, não tem processos implementados, e não
//   tem um tipo para isso. Chutei o tipo int. Foi necessário também um valor para
//   representar a inexistência de um processo, coloquei -1. Altere para o seu
//   tipo, ou substitua os usos de processo_t e NENHUM_PROCESSO para o seu tipo.

#define DISK_TAM 1000
typedef mem_t disk_t;
int pos_livre = 0;


log_t logs;

struct so_t {
    cpu_t *cpu;
    mem_t *mem;
    mmu_t *mmu;
    es_t *es;
    console_t *console;
    bool erro_interno;

    // t1: tabela de processos, processo corrente, pendências, etc
    ptable_t *ptbl;
    wlist_t *wlst;
    log_t *log;
    bool finished;

    // primeiro quadro da memória que está livre (quadros anteriores estão ocupados)
    // t2: com memória virtual, o controle de memória livre e ocupada é mais
    //     completo que isso
    int quadro_livre;
    // uma tabela de páginas para poder usar a MMU
    // t2: com processos, não tem esta tabela global, tem que ter uma para
    //     cada processo
    disk_t *disk;

    FILE *prints;
};

// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares
// no t2, foi adicionado o 'processo' aos argumentos dessas funções
// carrega o programa na memória virtual de um processo; retorna end. inicial
static int so_carrega_programa(so_t *self, process_t *processo, char *nome_do_executavel);
// copia para str da memória do processo, até copiar um 0 (retorna true) ou tam bytes
static bool so_copia_str_do_processo(so_t *self, int tam, char str[tam], int end_virt, process_t *processo);

// CRIAÇÃO {{{1

so_t *so_cria(cpu_t *cpu, mem_t *mem, mmu_t *mmu, es_t *es, console_t *console) {
    so_t *self = malloc(sizeof(*self));
    assert(self != NULL);

    self->cpu = cpu;
    self->mem = mem;
    self->mmu = mmu;
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
    int ender = so_carrega_programa(self, NULL, "trata_int.maq");
    if (ender != IRQ_END_TRATADOR) {
        console_printf("SO: problema na carga do programa de tratamento de interrupção");
        self->erro_interno = true;
    }

    // programa o relógio para gerar uma interrupção após INTERVALO_INTERRUPCAO
    if (es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO) != ERR_OK) {
        console_printf("SO: problema na programação do timer");
        self->erro_interno = true;
    }

    self->disk = mem_cria(DISK_TAM);

    // t1
    self->ptbl = ptable_create();
    self->wlst = wlist_alloc();

    self->finished = false;

    self->prints = fopen("prints.txt", "w");

    // inicializa a tabela de páginas global, e entrega ela para a MMU
    // t2: com processos, essa tabela não existiria, teria uma por processo, que
    //     deve ser colocada na MMU quando o processo é despachado para execução
    // self->tabpag_global = tabpag_cria();
    // mmu_define_tabpag(self->mmu, self->tabpag_global);
    // define o primeiro quadro livre de memória como o seguinte àquele que
    //   contém o endereço 99 (as 100 primeiras posições de memória (pelo menos)
    //   não vão ser usadas por programas de usuário)
    // t2: o controle de memória livre deve ser mais aprimorado que isso
    self->quadro_livre = 99 / TAM_PAGINA + 1;
    return self;
}

void so_destroi(so_t *self) {
    cpu_define_chamaC(self->cpu, NULL, NULL);
    ptable_free(self->ptbl);
    fclose(self->prints);
    free(self);
}

// TRATAMENTO DE INTERRUPÇÃO {{{1

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_irq(so_t *self, int irq);
static void so_trata_pendencias(so_t *self);
static void so_escalona(so_t *self);
static int so_despacha(so_t *self);

// função a ser chamada pela CPU quando executa a instrução CHAMAC, no tratador de
//   interrupção em assembly
// essa é a única forma de entrada no SO depois da inicialização
// na inicialização do SO, a CPU foi programada para chamar esta função para executar
//   a instrução CHAMAC
// a instrução CHAMAC só deve ser executada pelo tratador de interrupção
//
// o primeiro argumento é um ponteiro para o SO, o segundo é a identificação
//   da interrupção
// o valor retornado por esta função é colocado no registrador A, e pode ser
//   testado pelo código que está após o CHAMAC. No tratador de interrupção em
//   assembly esse valor é usado para decidir se a CPU deve retornar da interrupção
//   (e executar o código de usuário) ou executar PARA e ficar suspensa até receber
//   outra interrupção
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

    // Contabilidade
    logs.time_blocked += ptable_idle(self->ptbl) ? 1 : 0;
    ptable_update_times(self->ptbl);
}

static void so_escalona(so_t *self) {
    // ptable_preemptive_mode(self->ptbl);

    ptable_priority_mode(self->ptbl);

    ptable_standard_mode(self->ptbl, true);
}

static int so_despacha(so_t *self) {
    process_t *running = ptable_running_process(self->ptbl);

    if (self->erro_interno || !running) {
        return 1;
    }

    process_load_registers(running, self->mem);
    mmu_define_tabpag(self->mmu, process_tabpag(running));

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
    // t1: deveria criar um processo para o init, e inicializar o estado do
    //   processador para esse processo com os registradores zerados, exceto
    //   o PC e o modo.
    // como não tem suporte a processos, está carregando os valores dos
    //   registradores diretamente para a memória, de onde a CPU vai carregar
    //   para os seus registradores quando executar a instrução RETI

    // coloca o programa "init" na memória
    // t2: deveria criar um processo, e programar a tabela de páginas dele **********
    process_t *proc = process_create(); // deveria inicializar um processo...
    ptable_insert_process(self->ptbl, proc);

    int ender = so_carrega_programa(self, proc, "init.maq");
    if (ender != 0) {
        console_printf("SO: problema na carga do programa inicial");
        self->erro_interno = true;
        return;
    }

    process_set_PC(proc, 0);
    process_set_modo(proc, usuario);

    ptable_set_running_process(self->ptbl, proc);
}

static void so_trata_err_pag_ausente(so_t *self) {

    console_printf("========");

    process_t *running = ptable_running_process(self->ptbl);
    tabpag_t *tabpag = process_tabpag(running);

    int virtual = process_complemento(running);

    int init = process_disk_init(running);

    tabpag_define_quadro(tabpag, virtual / TAM_PAGINA, self->quadro_livre);

    int init_virtual = (virtual / TAM_PAGINA) * TAM_PAGINA;

    for (int i = init_virtual; i < init_virtual + TAM_PAGINA; i++) {
        int valor;
        mem_le(self->disk, i + init, &valor); // endereço no disco + init
        mmu_escreve(self->mmu, i, valor, process_modo(running));
    }

    fprintf(self->prints, "VIRTUAL: %d, END: %d, PAGINA: %d, QUADRO: %d\n", virtual, virtual + init, virtual / TAM_PAGINA, self->quadro_livre);

    int tam = mem_tam(self->mem);
    fprintf(self->prints, "[");
    for (int i = 0; i < tam; i++) {
        fprintf(self->prints, " %04d ", i);
    }
    fprintf(self->prints, "]\n");
    fprintf(self->prints, "[");
    for (int i = 0; i < tam; i++) {
        int valor;
        mem_le(self->mem, i, &valor);
        fprintf(self->prints, " %04d ", valor);
    }
    fprintf(self->prints, "]");

    self->quadro_livre++;
}

// interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t *self) {

    int err_int;

    mem_le(self->mem, IRQ_END_erro, &err_int);
    err_t err = err_int;

    if (err == ERR_PAG_AUSENTE) {
        so_trata_err_pag_ausente(self);
    } else {
        console_printf("SO: IRQ não tratada -- erro na CPU: %s", err_nome(err));
        self->erro_interno = true;
    }
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

    if (ptable_head(self->ptbl) == NULL && !self->finished) {
        self->finished = true;

        FILE *fp = fopen("logs.txt", "w");

        fprintf(fp, "No de processos criados: %d\n", logs.process_created);

        es_le(self->es, D_RELOGIO_INSTRUCOES, &logs.total_time);
        fprintf(fp, "Tempo total de execução: %d\n", logs.total_time);

        fprintf(fp, "Tempo ocioso: %d\n", logs.time_blocked);

        fprintf(fp, "No de SO_LE: %d\n", logs.number_interruptions[0]);
        fprintf(fp, "No de SO_ESCR: %d\n", logs.number_interruptions[1]);
        fprintf(fp, "No de SO_CRIA_PROC: %d\n", logs.number_interruptions[2]);
        fprintf(fp, "No de SO_MATA_PROC: %d\n", logs.number_interruptions[3]);
        fprintf(fp, "No de SO_ESPERA_PROC: %d\n", logs.number_interruptions[4]);

        fprintf(fp, "No de preempções: %d\n", logs.number_preemptions);
        fprintf(fp, "\n");

        for (int i = 1; i < logs.process_created + 1; i++) {
            fprintf(fp, "Tempo de retorno PID %d: %d\n", i, logs.process_killed_at[i] - logs.process_created_at[i]);
        }
        fprintf(fp, "\n");

        for (int i = 1; i < logs.process_created + 1; i++) {
            fprintf(fp, "No de preempções PID %d: %d\n", i, logs.number_preemptions_process[i]);
        }
        fprintf(fp, "\n");

        for (int i = 1; i < logs.process_created + 1; i++) {
            fprintf(fp, "No de vezes que PID %d entrou no estado\n", i);
            fprintf(fp, "\tblocked: %d\n", logs.number_states_process[i][0]);
            fprintf(fp, "\tready: %d\n", logs.number_states_process[i][1]);
            fprintf(fp, "\trunning: %d\n", logs.number_states_process[i][2]);
        }
        fprintf(fp, "\n");

        for (int i = 1; i < logs.process_created + 1; i++) {
            fprintf(fp, "O tempo que PID %d ficou no estado\n", i);
            fprintf(fp, "\tblocked: %d\n", logs.process_state_time[i][0]);
            fprintf(fp, "\tready: %d\n", logs.process_state_time[i][1]);
            fprintf(fp, "\trunning: %d\n", logs.process_state_time[i][2]);
        }
        fprintf(fp, "\n");

        for (int i = 1; i < logs.process_created + 1; i++) {
            fprintf(fp,
                    "Tempo médio de resposta do PID %d: %f\n",
                    i,
                    logs.process_state_time[i][1] / (float)logs.number_states_process[i][1]);
        }
        fprintf(fp, "\n");

        fprintf(fp, "[");
        for (int i = 0; i < DISK_TAM; i++) {
            fprintf(fp, " %04d ", i);
        }
        fprintf(fp, "]\n");
        fprintf(fp, "[");
        for (int i = 0; i < DISK_TAM; i++) {
            int valor;
            mem_le(self->disk, i, &valor);
            fprintf(fp, " %04d ", valor);
        }
        fprintf(fp, "]");

        fclose(fp);
    }
}

// foi gerada uma interrupção para a qual o SO não está preparado
static void so_trata_irq_desconhecida(so_t *self, int irq) {
    console_printf("SO: não sei tratar IRQ %d (%s)", irq, irq_nome(irq));
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

    process_t *running = ptable_running_process(self->ptbl);

    int id_chamada = process_A(running);

    switch (id_chamada) {
    case SO_LE:
        so_chamada_le(self);
        logs.number_interruptions[0]++;
        break;
    case SO_ESCR:
        so_chamada_escr(self);
        logs.number_interruptions[1]++;
        break;
    case SO_CRIA_PROC:
        so_chamada_cria_proc(self);
        logs.number_interruptions[2]++;
        break;
    case SO_MATA_PROC:
        so_chamada_mata_proc(self);
        logs.number_interruptions[3]++;
        break;
    case SO_ESPERA_PROC:
        so_chamada_espera_proc(self);
        logs.number_interruptions[4]++;
        break;
    default:
        console_printf("SO: chamada de sistema desconhecida (%d)", id_chamada);

        ptable_remove_process(self->ptbl, running);
        process_free(running);

        self->erro_interno = true;
    }
}

// implementação da chamada se sistema SO_LE
// faz a leitura de um dado da entrada corrente do processo, coloca o dado no reg A
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

// implementação da chamada se sistema SO_CRIA_PROC
// cria um processo
static void so_chamada_cria_proc(so_t *self) {

    process_t *running = ptable_running_process(self->ptbl);

    char nome[100];
    if (!so_copia_str_do_processo(self, 100, nome, process_X(running), running)) {
        process_set_A(running, -1);
        return;
    }

    process_t *created = process_create();
    ptable_insert_process(self->ptbl, created);

    int mem_address = so_carrega_programa(self, created, nome);
    if (mem_address != 0) {
        process_set_A(running, -1);
        return;
    }

    process_set_PC(created, 0);
    process_set_A(running, process_pid(created));

    // Contabilidade
    logs.process_created++;
    es_le(self->es, D_RELOGIO_INSTRUCOES, &logs.process_created_at[process_pid(created)]);
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

        // Por quê?
        process_set_state(found, ready);
    }

    // Contabilidade
    es_le(self->es, D_RELOGIO_INSTRUCOES, &logs.process_killed_at[process_pid(found)]);
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

// CARGA DE PROGRAMA {{{1

// funções auxiliares
static int so_carrega_programa_na_memoria_fisica(so_t *self, programa_t *programa);
static int so_carrega_programa_na_memoria_virtual(so_t *self, programa_t *programa, process_t *processo);

// carrega o programa na memória de um processo ou na memória física se NENHUM_PROCESSO
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, process_t *processo, char *nome_do_executavel) {
    console_printf("SO: carga de '%s'", nome_do_executavel);

    programa_t *programa = prog_cria(nome_do_executavel);
    if (programa == NULL) {
        console_printf("Erro na leitura do programa '%s'\n", nome_do_executavel);
        return -1;
    }

    int end_carga;
    if (processo == NULL) {
        end_carga = so_carrega_programa_na_memoria_fisica(self, programa);
    } else { end_carga = so_carrega_programa_na_memoria_virtual(self, programa, processo);
    }

    prog_destroi(programa);
    return end_carga;
}

static int so_carrega_programa_na_memoria_fisica(so_t *self, programa_t *programa) {
    int end_ini = prog_end_carga(programa);
    int end_fim = end_ini + prog_tamanho(programa);

    for (int end = end_ini; end < end_fim; end++) {
        if (mem_escreve(self->mem, end, prog_dado(programa, end)) != ERR_OK) {
            console_printf("Erro na carga da memória, endereco %d\n", end);
            return -1;
        }
    }
    console_printf("carregado na memória física, %d-%d", end_ini, end_fim);
    return end_ini;
}

static int so_carrega_programa_na_memoria_virtual(so_t *self, programa_t *programa, process_t *proc) {

    // mmu_define_tabpag(self->mmu, process_tabpag(proc));
    //
    // int end_virt_ini = prog_end_carga(programa);
    // int end_virt_fim = end_virt_ini + prog_tamanho(programa) - 1;
    // int pagina_ini = end_virt_ini / TAM_PAGINA;
    // int pagina_fim = end_virt_fim / TAM_PAGINA;
    // int quadro_ini = self->quadro_livre;
    //
    // // mapeia as páginas nos quadros
    // int quadro = quadro_ini;
    // for (int pagina = pagina_ini; pagina <= pagina_fim; pagina++) {
    //     tabpag_define_quadro(process_tabpag(proc), pagina, quadro);
    //     quadro++;
    // }
    // self->quadro_livre = quadro;
    //
    // // carrega o programa na memória principal
    // int end_fis_ini = quadro_ini * TAM_PAGINA;
    // int end_fis = end_fis_ini;
    //
    // for (int end_virt = end_virt_ini; end_virt <= end_virt_fim; end_virt++) {
    //     mmu_escreve(self->mmu, end_virt, prog_dado(programa, end_virt), process_modo(proc));
    //     end_fis++;
    // }

    // Carrega dados no disco.
    
    process_set_disk(proc, pos_livre, prog_tamanho(programa));

    for (int i = 0; i < prog_tamanho(programa); i++) {
        mem_escreve(self->disk, pos_livre, prog_dado(programa, i));
        pos_livre++;
    }

    // console_printf("carregado na memória virtual V%d-%d F%d-%d", end_virt_ini, end_virt_fim, end_fis_ini, end_fis - 1);
    return 0;
}

// ACESSO À MEMÓRIA DOS PROCESSOS {{{1

// copia uma string da memória do processo para o vetor str.
// retorna false se erro (string maior que vetor, valor não char na memória,
//   erro de acesso à memória)
// O endereço é um endereço virtual de um processo.
// T2: Com memória virtual, cada valor do espaço de endereçamento do processo
//   pode estar em memória principal ou secundária (e tem que achar onde)
static bool so_copia_str_do_processo(so_t *self, int tam, char str[tam], int end_virt, process_t *processo) {
    if (processo == NULL)
        return false;
    for (int indice_str = 0; indice_str < tam; indice_str++) {
        int caractere;
        // não tem memória virtual implementada, posso usar a mmu para traduzir
        //   os endereços e acessar a memória
        if (mmu_le(self->mmu, end_virt + indice_str, &caractere, usuario) != ERR_OK) {
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
