#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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

typedef struct {
    int id_cliente;
    int qtd_itens;
} ArgsCliente;

static caixa_t caixas[NUM_CAIXAS];
static int finalizar = 0; // Flag pra encerrar empregados

void Atender(int TempoAtendimento) {
    sthread_user_sleep(TempoAtendimento);
}

void SerAtendido(int TempoAtendimento) {
    sthread_user_sleep(TempoAtendimento);
}

int ProximoCliente(int fila) {
    int idx = fila - 1;
    sthread_user_monitor_enter(caixas[idx].mon);

    if (caixas[idx].clientes_na_fila == 0) {
        for (int i = 0; i < NUM_CAIXAS; i++) {
            if (i!= idx && caixas[i].clientes_na_fila > 1) {
                sthread_user_monitor_enter(caixas[i].mon);
                caixas[i].clientes_na_fila--;
                printf("[Caixa %d] Ajudando Caixa %d. Puxou 1 cliente.\n", fila, caixas[i].id);
                sthread_user_monitor_exit(caixas[i].mon);

                caixas[idx].clientes_na_fila++;
                break;
            }
        }

        while (caixas[idx].clientes_na_fila == 0 &&!finalizar) {
            printf("[Caixa %d] Sem clientes. Empregado à espera...\n", fila);
            sthread_user_monitor_wait(caixas[idx].mon);
        }

        if (finalizar) {
            sthread_user_monitor_exit(caixas[idx].mon);
            return -1; // Sinaliza pra thread sair
        }
    }

    caixas[idx].clientes_na_fila--;
    printf("[Caixa %d] Empregado começou a atender um cliente.\n", fila);
    sthread_user_monitor_exit(caixas[idx].mon);

    return TEMPO_PADRAO_ATENDIMENTO;
}

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
    printf("[Cliente %d] Escolheu a fila do Caixa %d (Total na fila: %d).\n",
           idCliente, caixas[melhor_caixa].id, caixas[melhor_caixa].clientes_na_fila + 1);
    caixas[melhor_caixa].clientes_na_fila++;

    sthread_user_monitor_signal(caixas[melhor_caixa].mon);
    sthread_user_monitor_exit(caixas[melhor_caixa].mon);
}

void* Cliente(void *arg) {
    ArgsCliente args = (ArgsCliente)arg;
    int idCliente = args->id_cliente;
    free(arg);

    sthread_user_sleep(rand() % 3); // Chegada aleatória
    int TempoAtendimento = TEMPO_PADRAO_ATENDIMENTO;

    EscolherFila(TempoAtendimento, idCliente);
    SerAtendido(TempoAtendimento);

    printf("[Cliente %d] Foi atendido e saiu do supermercado.\n", idCliente);
    return NULL;
}

void* Empregado(void *arg) {
    int fila = (int)arg;
    free(arg);
    int TempoAtendimento;

    while (1) {
        TempoAtendimento = ProximoCliente(fila);
        if (TempoAtendimento == -1) break; // Encerra se recebeu sinal de finalizar
        Atender(TempoAtendimento);
        printf("[Caixa %d] Concluiu o atendimento.\n", fila);
    }
    printf("[Caixa %d] Encerrando expediente.\n", fila);
    return NULL;
}

int main() {
    srand(time(NULL));
    sthread_user_init();
    printf("=== SIMULACAO UAN: MODELO CONCORRENTE OFICIAL ===\n");

    // 1. Inicializa os caixas e monitores
    for (int i = 0; i < NUM_CAIXAS; i++) {
        caixas[i].id = i + 1;
        caixas[i].clientes_na_fila = 0;
        caixas[i].mon = sthread_user_monitor_init();
    }

    // 2. CRIA CLIENTES PRIMEIRO - isso evita segfault na sthread
    sthread_t clientes[TOTAL_CLIENTES];
    for (int i = 0; i < TOTAL_CLIENTES; i++) {
        ArgsCliente *args_cli = malloc(sizeof(ArgsCliente));
        args_cli->id_cliente = i + 1;
        args_cli->qtd_itens = 5;
        clientes[i] = sthread_user_create((sthread_start_func_t)Cliente, args_cli);
    }

    // 3. CRIA EMPREGADOS DEPOIS
    sthread_t empregados[NUM_CAIXAS];
    for (int i = 0; i < NUM_CAIXAS; i++) {
        int *id_caixa = malloc(sizeof(int));
        *id_caixa = caixas[i].id;
        empregados[i] = sthread_user_create((sthread_start_func_t)Empregado, id_caixa);
    }

    // 4. Espera todos os clientes terminarem
    for (int i = 0; i < TOTAL_CLIENTES; i++) {
        sthread_user_join(clientes[i], NULL);
    }

    printf("=== TODOS OS CLIENTES FORAM PROCESSADOS ===\n");

    // 5. Avisa os empregados pra parar
    finalizar = 1;
    for (int i = 0; i < NUM_CAIXAS; i++) {
        sthread_user_monitor_enter(caixas[i].mon);
        sthread_user_monitor_signal(caixas[i].mon); // Acorda quem tá em wait
        sthread_user_monitor_exit(caixas[i].mon);
    }

    // 6. Espera empregados terminarem
    for (int i = 0; i < NUM_CAIXAS; i++) {
        sthread_user_join(empregados[i], NULL);
    }

    // 7. Libera recursos
    for (int i = 0; i < NUM_CAIXAS; i++) {
        sthread_user_monitor_free(caixas[i].mon);
    }

    sthread_user_exit();
    return 0;
}
