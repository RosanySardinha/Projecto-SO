#include <stdio.h>
#include <stdlib.h>
#include "sthread.h"
#include "sthread_user.h"

#define NUM_CAIXAS 3
#define TOTAL_CLIENTES 6
#define TEMPO_PADRAO_ATENDIMENTO 2

typedef struct {
    sthread_mon_t mon;
    int clientes_na_fila;
    int id;
} caixa_t;

static caixa_t caixas[NUM_CAIXAS];

/* 
 * Rotina obrigatória: Atender (Chamada pelo Empregado)
 * Bloqueia a tarefa do empregado durante o intervalo de tempo.
 */
void Atender(int TempoAtendimento) {
    sthread_user_sleep(TempoAtendimento);
}

/* 
 * Rotina obrigatória: SerAtendido (Chamada pelo Cliente)
 * Bloqueia a tarefa do cliente durante o intervalo de tempo.
 */
void SerAtendido(int TempoAtendimento) {
    sthread_user_sleep(TempoAtendimento);
}

/* 
 * Função Auxiliar do Empregado: ProximoCliente
 * Verifica se há clientes na fila ou tenta roubar de outra fila se estiver vazia.
 */
int ProximoCliente(int fila) {
    int idx = fila - 1;
    sthread_user_monitor_enter(caixas[idx].mon);

    // Se a fila local estiver vazia, tenta ajudar outra fila cheia
    if (caixas[idx].clientes_na_fila == 0) {
        for (int i = 0; i < NUM_CAIXAS; i++) {
            if (i != idx && caixas[i].clientes_na_fila > 1) {
                sthread_user_monitor_enter(caixas[i].mon);
                caixas[i].clientes_na_fila--;
                printf("[Caixa %d] Ajudando Caixa %d. Puxou 1 cliente.\n", fila, caixas[i].id);
                sthread_user_monitor_exit(caixas[i].mon);
                
                caixas[idx].clientes_na_fila++;
                break;
            }
        }

        // Se mesmo assim continuar vazia, o empregado bloqueia-se à espera
        while (caixas[idx].clientes_na_fila == 0) {
            printf("[Caixa %d] Sem clientes. Empregado à espera...\n", fila);
            sthread_user_monitor_wait(caixas[idx].mon);
        }
    }

    caixas[idx].clientes_na_fila--;
    printf("[Caixa %d] Empregado começou a atender um cliente.\n", fila);
    sthread_user_monitor_exit(caixas[idx].mon);

    return TEMPO_PADRAO_ATENDIMENTO;
}

/* 
 * Função Auxiliar do Cliente: EscolherFila
 * Encontra a menor fila disponível e insere o cliente nela de forma segura.
 */
void EscolherFila(int TempoAtendimento, int idCliente) {
    int melhor_caixa = 0;
    int menor_fila = 999;
    
    for (int i = 0; i < NUM_CAIXAS; i++) {
        if (caixas[i].clientes_na_fila < menor_fila) {
            menor_fila = caixas[i].clientes_na_fila;
            melhor_caixa = i;
        }
    }

    sthread_user_monitor_enter(caixas[melhor_caixa].mon);
    printf("[Cliente %d] Escolheu a fila do Caixa %d (Total na fila: %d).\n", idCliente, caixas[melhor_caixa].id, caixas[melhor_caixa].clientes_na_fila + 1);
    caixas[melhor_caixa].clientes_na_fila++;

    sthread_user_monitor_signal(caixas[melhor_caixa].mon);
    sthread_user_monitor_exit(caixas[melhor_caixa].mon);
}

/* 
 * ROTINA OFICIAL: Cliente (conforme imagem)
 */
void* Cliente(void *arg) {
    int idCliente = *(int*)arg;
    free(arg);

    // Simula tempo antes de ir para a fila
    sthread_user_sleep(rand() % 3);

    int TempoAtendimento = TEMPO_PADRAO_ATENDIMENTO;
    
    EscolherFila(TempoAtendimento, idCliente);
    SerAtendido(TempoAtendimento);

    printf("[Cliente %d] Foi atendido e saiu do supermercado.\n", idCliente);
    return NULL;
}

/* 
 * ROTINA OFICIAL: Empregado (conforme imagem)
 */
void* Empregado(void *arg) {
    int fila = *(int*)arg;
    int TempoAtendimento;

    while (1) { // while (TRUE)
        TempoAtendimento = ProximoCliente(fila);
        Atender(TempoAtendimento);
        printf("[Caixa %d] Concluiu o atendimento.\n", fila);
    }
    return NULL;
}

int main() {
    sthread_user_init();
    printf("=== SIMULAÇÃO UAN: MODELO CONCORRENTE OFICIAL ===\n");

    int caixa_ids[NUM_CAIXAS];
    for (int i = 0; i < NUM_CAIXAS; i++) {
        caixas[i].id = i + 1;
        caixas[i].clientes_na_fila = 0;
        caixas[i].mon = sthread_user_monitor_init();
        caixa_ids[i] = caixas[i].id;
        
        // Cria o Empregado com a prioridade padrão 5 vinda do novo sthread_user_create
        sthread_user_create((sthread_start_func_t)Empregado, &caixa_ids[i], 5);
    }

    sthread_t clientes[TOTAL_CLIENTES];
    for (int i = 0; i < TOTAL_CLIENTES; i++) {
        int *id = malloc(sizeof(int));
        *id = i + 1;
        clientes[i] = sthread_user_create((sthread_start_func_t)Cliente, id, 5);
    }

    for (int i = 0; i < TOTAL_CLIENTES; i++) {
        sthread_user_join(clientes[i], NULL);
    }

    sthread_user_sleep(3);
    printf("=== TODOS OS CLIENTES FORAM PROCESSADOS ===\n");

    for (int i = 0; i < NUM_CAIXAS; i++) {
        sthread_user_monitor_free(caixas[i].mon);
    }
    return 0;
}
