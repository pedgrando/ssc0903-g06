#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <math.h>

#ifdef _WIN32
// O Windows nao tem rand_r nativo, portanto definimos uma versao compativel
int rand_r(unsigned int *seedp) {
    *seedp = *seedp * 1103515245 + 12345;
    return (unsigned int)(*seedp / 65536) % 32768;
}
#endif

/* ============================================================
 * Tipos de dados e Estruturas
 * ============================================================ */

typedef struct {
    int linha;
    int coluna;
} Foco;

typedef struct {
    int passo;   /* passo de ativacao */
    int l1;      /* linha inicial */
    int c1;      /* coluna inicial */
    int l2;      /* linha final */
    int c2;      /* coluna final */
} Zona;

typedef struct {
    int L;                  /* numero de linhas da matriz */
    int C;                  /* numero de colunas da matriz */
    int P;                  /* numero maximo de passos */
    int T;                  /* numero de threads (ignorado na versao sequencial) */
    unsigned int seed;      /* semente para rand_r */
    int LIMIAR;             /* limiar de ignicao */

    int vento_linha;        /* componente vertical do vento (-1, 0, 1) */
    int vento_coluna;       /* componente horizontal do vento (-1, 0, 1) */
    int intensidade_vento;  /* intensidade do vento (0 a 5) */

    int F;                  /* quantidade de focos iniciais */
    int Z;                  /* quantidade de zonas de contencao */
    Foco *focos;            /* vetor de focos */
    Zona *zonas;            /* vetor de zonas */
} Config;

/* ============================================================
 * Variaveis Globais de Estado da Simulacao
 * ============================================================ */

unsigned char *cobertura = NULL;       /* Tipo de cobertura de cada celula (0: Agua, 1: Solo, 2: Rasteira, 3: Floresta) */
unsigned char *umidade = NULL;         /* Umidade de cada celula (0 a 100) */
unsigned char *estado_atual = NULL;    /* Estado atual (0: Nao comb., 1: Intacta, 2: Chamas, 3: Queimada, 4: Contencao) */
unsigned char *proximo_estado = NULL;  /* Estado futuro para o double-buffering */
int *tempo_atual = NULL;               /* Tempo restante de queima de cada celula */
int *proximo_tempo = NULL;             /* Tempo de queima futuro para o double-buffering */
int *ativacao = NULL;                  /* Menor passo de ativacao da zona (-1 se nenhuma) */
long long total_celulas = 0;           /* Total de celulas na grade (L * C) */

/* Matriz estatica 3x3 de pesos direcionais dos vizinhos de Moore pre-computados */
int pesos_vizinhos[3][3];

/* Estatisticas finais da simulacao */
int passos_executados = 0;
long long nao_combustiveis = 0;
long long intactas = 0;
long long em_chamas = 0;
long long queimadas = 0;
long long contencao = 0;
long long total_ignicoes = 0;
int pico_ignicoes_passo = -1;
int pico_ignicoes_qtd = 0;
double percentual_queimado = 0.0;
double percentual_protegido = 0.0;
double tempo_de_execucao = 0.0;

/* ============================================================
 * compara_focos
 * ============================================================
 * Funcao de comparacao utilizada pelo qsort para ordenar focos
 * por coordenadas (linha, coluna), facilitando a deteccao de
 * focos repetidos na validacao de entrada.
 */
static int compara_focos(const void *a, const void *b) {
    const Foco *fa = (const Foco *)a;
    const Foco *fb = (const Foco *)b;

    if (fa->linha != fb->linha) {
        return fa->linha - fb->linha;
    }
    return fa->coluna - fb->coluna;
}

/* ============================================================
 * le_entrada
 * ============================================================
 * Le o arquivo de configuracao da simulacao e valida todos os
 * parametros conforme os requisitos da especificacao:
 * dimensoes, passos, threads, limiar, direcao e intensidade do
 * vento, focos dentro dos limites sem duplicatas, e zonas validas.
 * Retorna 0 em caso de sucesso e 1 em caso de erro.
 */
int le_entrada(const char *caminho, Config *cfg) {
    FILE *fp = fopen(caminho, "r");
    if (fp == NULL) {
        fprintf(stderr, "Erro: nao foi possivel abrir o arquivo '%s'\n", caminho);
        return 1;
    }

    /* --- Primeira linha: configuracao geral --- */
    int L, C, P, T, LIMIAR;
    unsigned int seed;

    if (fscanf(fp, "%d %d %d %d %u %d", &L, &C, &P, &T, &seed, &LIMIAR) != 6) {
        fprintf(stderr, "Erro: primeira linha invalida\n");
        fclose(fp);
        return 1;
    }

    if (L <= 0 || C <= 0) {
        fprintf(stderr, "Erro: dimensoes da matriz devem ser positivas\n");
        fclose(fp);
        return 1;
    }
    if (P < 0) {
        fprintf(stderr, "Erro: numero de passos nao pode ser negativo\n");
        fclose(fp);
        return 1;
    }
    if (T <= 0) {
        fprintf(stderr, "Erro: numero de threads deve ser positivo\n");
        fclose(fp);
        return 1;
    }
    if (LIMIAR <= 0) {
        fprintf(stderr, "Erro: limiar de ignicao deve ser positivo\n");
        fclose(fp);
        return 1;
    }

    /* --- Segunda linha: configuracao do vento --- */
    int vento_linha, vento_coluna, intensidade;

    if (fscanf(fp, "%d %d %d", &vento_linha, &vento_coluna, &intensidade) != 3) {
        fprintf(stderr, "Erro: segunda linha invalida\n");
        fclose(fp);
        return 1;
    }

    if (vento_linha < -1 || vento_linha > 1 || vento_coluna < -1 || vento_coluna > 1) {
        fprintf(stderr, "Erro: componentes do vento devem estar entre -1 e 1\n");
        fclose(fp);
        return 1;
    }
    if (vento_linha == 0 && vento_coluna == 0) {
        fprintf(stderr, "Erro: direcao do vento (0,0) e invalida\n");
        fclose(fp);
        return 1;
    }
    if (intensidade < 0 || intensidade > 5) {
        fprintf(stderr, "Erro: intensidade do vento deve estar entre 0 e 5\n");
        fclose(fp);
        return 1;
    }

    /* --- Terceira linha: quantidade de focos e zonas --- */
    int F, Z;

    if (fscanf(fp, "%d %d", &F, &Z) != 2) {
        fprintf(stderr, "Erro: terceira linha invalida\n");
        fclose(fp);
        return 1;
    }
    if (F < 0 || Z < 0) {
        fprintf(stderr, "Erro: quantidades de focos e zonas nao podem ser negativas\n");
        fclose(fp);
        return 1;
    }

    /* --- Leitura dos focos iniciais --- */
    Foco *focos = NULL;
    if (F > 0) {
        focos = (Foco *)malloc(F * sizeof(Foco));
        if (focos == NULL) {
            fprintf(stderr, "Erro: falha na alocacao de memoria para focos\n");
            fclose(fp);
            return 1;
        }

        for (int i = 0; i < F; i++) {
            if (fscanf(fp, "%d %d", &focos[i].linha, &focos[i].coluna) != 2) {
                fprintf(stderr, "Erro: foco %d invalido\n", i + 1);
                free(focos);
                fclose(fp);
                return 1;
            }
            if (focos[i].linha < 0 || focos[i].linha >= L ||
                focos[i].coluna < 0 || focos[i].coluna >= C) {
                fprintf(stderr, "Erro: foco (%d, %d) fora da matriz\n",
                        focos[i].linha, focos[i].coluna);
                free(focos);
                fclose(fp);
                return 1;
            }
        }

        /* Verifica focos repetidos apos ordenar por (linha, coluna) */
        qsort(focos, F, sizeof(Foco), compara_focos);
        for (int i = 1; i < F; i++) {
            if (focos[i].linha == focos[i - 1].linha &&
                focos[i].coluna == focos[i - 1].coluna) {
                fprintf(stderr, "Erro: foco (%d, %d) repetido\n",
                        focos[i].linha, focos[i].coluna);
                free(focos);
                fclose(fp);
                return 1;
            }
        }
    }

    /* --- Leitura das zonas de contencao --- */
    Zona *zonas = NULL;
    if (Z > 0) {
        zonas = (Zona *)malloc(Z * sizeof(Zona));
        if (zonas == NULL) {
            fprintf(stderr, "Erro: falha na alocacao de memoria para zonas\n");
            free(focos);
            fclose(fp);
            return 1;
        }

        for (int i = 0; i < Z; i++) {
            if (fscanf(fp, "%d %d %d %d %d",
                       &zonas[i].passo,
                       &zonas[i].l1, &zonas[i].c1,
                       &zonas[i].l2, &zonas[i].c2) != 5) {
                fprintf(stderr, "Erro: zona %d invalida\n", i + 1);
                free(focos);
                free(zonas);
                fclose(fp);
                return 1;
            }

            if (zonas[i].passo < 0 || zonas[i].passo >= P) {
                fprintf(stderr, "Erro: passo de ativacao %d da zona %d fora do intervalo [0, %d)\n",
                        zonas[i].passo, i + 1, P);
                free(focos);
                free(zonas);
                fclose(fp);
                return 1;
            }
            if (zonas[i].l1 < 0 || zonas[i].l2 >= L || zonas[i].l1 > zonas[i].l2) {
                fprintf(stderr, "Erro: zona %d com limites de linha invalidos\n", i + 1);
                free(focos);
                free(zonas);
                fclose(fp);
                return 1;
            }
            if (zonas[i].c1 < 0 || zonas[i].c2 >= C || zonas[i].c1 > zonas[i].c2) {
                fprintf(stderr, "Erro: zona %d com limites de coluna invalidos\n", i + 1);
                free(focos);
                free(zonas);
                fclose(fp);
                return 1;
            }
        }
    }

    fclose(fp);

    /* --- Preenche a estrutura Config --- */
    cfg->L = L;
    cfg->C = C;
    cfg->P = P;
    cfg->T = T;
    cfg->seed = seed;
    cfg->LIMIAR = LIMIAR;
    cfg->vento_linha = vento_linha;
    cfg->vento_coluna = vento_coluna;
    cfg->intensidade_vento = intensidade;
    cfg->F = F;
    cfg->Z = Z;
    cfg->focos = focos;
    cfg->zonas = zonas;

    return 0;
}

/* ============================================================
 * aplica_focos
 * ============================================================
 * Aplica os focos iniciais de incendio na matriz de estados e
 * define o tempo inicial de queima (2 para vegetacao rasteira e
 * 4 para floresta). Valida se algum foco foi posicionado sobre
 * celula nao combustivel (agua ou solo exposto).
 * Retorna 0 em caso de sucesso e 1 se houver foco invalido.
 */
int aplica_focos(const Config *cfg, int C,
                 const unsigned char *cobertura_arr,
                 unsigned char *estado_arr,
                 int *tempo_arr) {
    for (int i = 0; i < cfg->F; i++) {
        int linha = cfg->focos[i].linha;
        int coluna = cfg->focos[i].coluna;
        long long idx = (long long)linha * C + coluna;
        unsigned char cov = cobertura_arr[idx];

        if (cov == 0 || cov == 1) {
            fprintf(stderr, "Erro: foco (%d, %d) esta sobre celula nao combustivel\n",
                    linha, coluna);
            return 1;
        }

        estado_arr[idx] = 2;   /* em chamas */
        tempo_arr[idx] = (cov == 2) ? 2 : 4;
    }

    return 0;
}

/* ============================================================
 * constroi_mapa_ativacao
 * ============================================================
 * Mapeia cada celula para o menor passo de ativacao dentre todas
 * as zonas de contencao que a recobrem. Celulas sem zona recebem -1.
 * Retorna 0 em caso de sucesso.
 */
int constroi_mapa_ativacao(const Config *cfg, int C, int *ativacao_arr) {
    for (int z = 0; z < cfg->Z; z++) {
        int passo = cfg->zonas[z].passo;
        for (int l = cfg->zonas[z].l1; l <= cfg->zonas[z].l2; l++) {
            for (int c = cfg->zonas[z].c1; c <= cfg->zonas[z].c2; c++) {
                long long idx = (long long)l * C + c;
                if (ativacao_arr[idx] == -1 || passo < ativacao_arr[idx]) {
                    ativacao_arr[idx] = passo;
                }
            }
        }
    }
    return 0;
}

/* ============================================================
 * libera_config
 * ============================================================
 * Libera a memoria alocada para os vetores de focos e zonas da
 * estrutura de configuracao.
 */
void libera_config(Config *cfg) {
    if (cfg->focos != NULL) {
        free(cfg->focos);
        cfg->focos = NULL;
    }
    if (cfg->zonas != NULL) {
        free(cfg->zonas);
        cfg->zonas = NULL;
    }
}

/* ============================================================
 * prepara_terreno
 * ============================================================
 * Aloca a memoria necessaria para todas as matrizes 1D da
 * simulacao e realiza a geracao pseudoaleatoria deterministica
 * da cobertura vegetal e umidade de cada celula, segundo as
 * proporcoes definidas na especificacao.
 */
void prepara_terreno(long long L, long long C, unsigned int seed) {
    total_celulas = L * C;

    cobertura = (unsigned char *)malloc(total_celulas * sizeof(unsigned char));
    umidade = (unsigned char *)malloc(total_celulas * sizeof(unsigned char));
    estado_atual = (unsigned char *)malloc(total_celulas * sizeof(unsigned char));
    proximo_estado = (unsigned char *)malloc(total_celulas * sizeof(unsigned char));
    tempo_atual = (int *)malloc(total_celulas * sizeof(int));
    proximo_tempo = (int *)malloc(total_celulas * sizeof(int));
    ativacao = (int *)malloc(total_celulas * sizeof(int));

    if (!cobertura || !umidade || !estado_atual || !proximo_estado ||
        !tempo_atual || !proximo_tempo || !ativacao) {
        fprintf(stderr, "Erro: falha na alocacao de memoria para o terreno\n");
        exit(1);
    }

    nao_combustiveis = 0;

    for (long long i = 0; i < total_celulas; i++) {
        int val_cob = rand_r(&seed) % 100;

        if (val_cob <= 9) {
            cobertura[i] = 0; estado_atual[i] = 0; // Agua
            nao_combustiveis++;
        } else if (val_cob <= 19) {
            cobertura[i] = 1; estado_atual[i] = 0; // Solo exposto
            nao_combustiveis++;
        } else if (val_cob <= 54) {
            cobertura[i] = 2; estado_atual[i] = 1; // Vegetacao rasteira
        } else {
            cobertura[i] = 3; estado_atual[i] = 1; // Floresta
        }

        umidade[i] = (unsigned char)(rand_r(&seed) % 101);
        tempo_atual[i] = 0;
        ativacao[i] = -1;
    }
}

/* ============================================================
 * gera_relatorio
 * ============================================================
 * Calcula o checksum de forma sequencial na ordem linear da matriz
 * e imprime todos os resultados finais no formato estrito exigido
 * pela secao 11 da especificacao.
 */
void gera_relatorio() {
    unsigned long long checksum = 0;

    for (long long i = 0; i < total_celulas; i++) {
        checksum = checksum * 31ULL + (unsigned long long)estado_atual[i];
        checksum = checksum * 31ULL + (unsigned long long)tempo_atual[i];
    }

    printf("passos: %d\n", passos_executados);
    printf("nao_combustiveis: %lld\n", nao_combustiveis);
    printf("intactas: %lld\n", intactas);
    printf("em_chamas: %lld\n", em_chamas);
    printf("queimadas: %lld\n", queimadas);
    printf("contencao: %lld\n", contencao);
    printf("total_ignicoes: %lld\n", total_ignicoes);
    printf("pico_ignicoes: %d %d\n", pico_ignicoes_passo, pico_ignicoes_qtd);
    printf("percentual_queimado: %.2f\n", percentual_queimado);
    printf("percentual_protegido: %.2f\n", percentual_protegido);
    printf("checksum: %llu\n", checksum);
    printf("tempo: %.6f\n", tempo_de_execucao);
}

/* ============================================================
 * calcular_peso_basico
 * ============================================================
 * Retorna o peso basico de conexao de Moore: 10 para vizinhos
 * ortogonais (|prop_linha| + |prop_coluna| == 1) e 7 para
 * vizinhos diagonais.
 */
static inline int calcular_peso_basico(int prop_linha, int prop_coluna) {
    return (abs(prop_linha) + abs(prop_coluna) == 1) ? 10 : 7;
}

/* ============================================================
 * calcular_alinhamento_com_vento
 * ============================================================
 * Calcula o produto escalar entre o vetor de propagacao (do
 * vizinho para a celula) e a direcao do vento:
 * A = prop_linha * vento_linha + prop_coluna * vento_coluna.
 * Valores variam de -2 (contrario) a +2 (alinhado).
 */
static inline int calcular_alinhamento_com_vento(int prop_linha, int prop_coluna,
                                                int vento_linha, int vento_coluna) {
    return prop_linha * vento_linha + prop_coluna * vento_coluna;
}

/* ============================================================
 * calcular_peso_do_vizinho
 * ============================================================
 * Calcula a contribuicao direcional efetiva de um vizinho:
 * Pv = max(1, peso_basico + intensidade * alinhamento).
 */
static inline int calcular_peso_do_vizinho(int peso_basico,
                                          int intensidade,
                                          int alinhamento_com_vento) {
    int peso = peso_basico + alinhamento_com_vento * intensidade;
    return (peso > 1) ? peso : 1;
}

/* ============================================================
 * calcular_pesos_vizinhos
 * ============================================================
 * Pre-computa a matriz estatica 3x3 de pesos direcionais para
 * todos os deslocamentos de Moore (dl, dc no intervalo [-1, 1]).
 * A celula central (dl=0, dc=0) recebe peso 0.
 */
void calcular_pesos_vizinhos(int vento_linha, int vento_coluna,
                            int intensidade, int pesos[3][3]) {
    for (int dl = -1; dl <= 1; dl++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dl == 0 && dc == 0) {
                pesos[1][1] = 0;
                continue;
            }
            const int prop_linha = -dl;
            const int prop_coluna = -dc;
            const int peso_basico = calcular_peso_basico(prop_linha, prop_coluna);
            const int A = calcular_alinhamento_com_vento(prop_linha, prop_coluna,
                                                        vento_linha, vento_coluna);
            pesos[dl + 1][dc + 1] = calcular_peso_do_vizinho(peso_basico, intensidade, A);
        }
    }
}

/* ============================================================
 * transicao
 * ============================================================
 * Implementa a logica branchless de transicao de estados e tempos
 * de queima de uma celula a partir de seu estado atual, cobertura,
 * umidade, soma dos pesos dos vizinhos em chamas (S) e limiar.
 */
static inline void transicao(int estado, int tempo, int cob, int umid,
                            int S, int limiar,
                            int *novo_estado, int *novo_tempo) {
    int fator_combustivel = 8 * (cob == 2) + 12 * (cob == 3);
    int potencial = (S * fator_combustivel * (100 - umid)) / 100;
    int ignicao = (potencial >= limiar) & (estado == 1);
    int queimando = (estado == 2);
    int apagou = queimando & (tempo == 1);

    *novo_estado = estado + ignicao + apagou;
    *novo_tempo = tempo - queimando + ignicao * (2 + 2 * (cob == 3));
}

/* ============================================================
 * atualizar_celula_borda
 * ============================================================
 * Atualiza uma celula localizada na borda da grade com verificacao
 * de limites de coordenadas nos 8 vizinhos de Moore. Acumula novas
 * ignicoes e celulas que apagaram nos contadores passados por referencia.
 */
static inline void atualizar_celula_borda(int i, int j, int L, int C, int limiar,
                                         const unsigned char *estado_arr,
                                         const int *tempo_arr,
                                         const unsigned char *cob_arr,
                                         const unsigned char *umid_arr,
                                         unsigned char *prox_estado,
                                         int *prox_tempo,
                                         long long *ignicoes,
                                         long long *apagadas) {
    long long idx = (long long)i * C + j;
    int s = 0, n_estado, n_tempo;

    for (int dl = -1; dl <= 1; dl++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dl == 0 && dc == 0) continue;
            int lv = i + dl;
            int cv = j + dc;
            if (lv >= 0 && lv < L && cv >= 0 && cv < C) {
                if (estado_arr[(long long)lv * C + cv] == 2) {
                    s += pesos_vizinhos[dl + 1][dc + 1];
                }
            }
        }
    }

    transicao(estado_arr[idx], tempo_arr[idx], cob_arr[idx], umid_arr[idx],
              s, limiar, &n_estado, &n_tempo);
    prox_estado[idx] = (unsigned char)n_estado;
    prox_tempo[idx] = n_tempo;
    *ignicoes += (estado_arr[idx] == 1) & (n_estado == 2);
    *apagadas += (estado_arr[idx] == 2) & (n_estado == 3);
}

/* ============================================================
 * main
 * ============================================================
 * Nucleo principal do programa na versao sequencial pura.
 * Carrega a configuracao, aloca matrizes, aplica focos,
 * prepara o mapa de ativacao e executa a simulacao sequencialmente.
 * O OpenMP e utilizado estritamente para medir o tempo (omp_get_wtime).
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <arquivo_entrada>\n", argv[0]);
        return 1;
    }

    Config cfg;
    if (le_entrada(argv[1], &cfg) != 0) {
        return 1;
    }

    prepara_terreno(cfg.L, cfg.C, cfg.seed);

    if (aplica_focos(&cfg, cfg.C, cobertura, estado_atual, tempo_atual) != 0) {
        libera_config(&cfg);
        free(cobertura);
        free(umidade);
        free(estado_atual);
        free(proximo_estado);
        free(tempo_atual);
        free(proximo_tempo);
        free(ativacao);
        return 1;
    }

    constroi_mapa_ativacao(&cfg, cfg.C, ativacao);

    /* Calculo exato das celulas combustiveis iniciais (Rasteira + Floresta) */
    long long combustiveis_iniciais = total_celulas - nao_combustiveis;
    em_chamas = cfg.F;
    intactas = combustiveis_iniciais - cfg.F;
    queimadas = 0;
    contencao = 0;
    total_ignicoes = 0;
    pico_ignicoes_passo = -1;
    pico_ignicoes_qtd = 0;
    passos_executados = 0;

    /* Pre-computacao dos pesos direcionais constantes do vento */
    calcular_pesos_vizinhos(cfg.vento_linha, cfg.vento_coluna,
                            cfg.intensidade_vento, pesos_vizinhos);

    const int p_no = pesos_vizinhos[0][0], p_n = pesos_vizinhos[0][1], p_ne = pesos_vizinhos[0][2];
    const int p_o  = pesos_vizinhos[1][0],                              p_e  = pesos_vizinhos[1][2];
    const int p_so = pesos_vizinhos[2][0], p_s = pesos_vizinhos[2][1], p_se = pesos_vizinhos[2][2];

    const int L = cfg.L;
    const int C = cfg.C;
    const int P = cfg.P;
    const int limiar = cfg.LIMIAR;
    const int Z = cfg.Z;
    const Zona *zonas = cfg.zonas;

    /* ============================================================
     * Trecho Cronometrado da Simulacao Sequencial (Secao 12 do PDF)
     * ============================================================ */
    double inicio = omp_get_wtime();

    while (em_chamas > 0 && passos_executados < P) {
        long long step_contencoes = 0;
        long long step_ignicoes = 0;
        long long step_apagadas = 0;

        /* 1. Ativacao das zonas de contencao programadas para o passo atual */
        for (int z = 0; z < Z; z++) {
            if (zonas[z].passo == passos_executados) {
                const int l1 = zonas[z].l1, l2 = zonas[z].l2;
                const int c1 = zonas[z].c1, c2 = zonas[z].c2;

                for (int i = l1; i <= l2; i++) {
                    for (int j = c1; j <= c2; j++) {
                        long long idx = (long long)i * C + j;
                        if (ativacao[idx] == passos_executados && estado_atual[idx] == 1) {
                            estado_atual[idx] = 4;
                            step_contencoes++;
                        }
                    }
                }
            }
        }

        /* 2. Propagacao do fogo e atualizacao das celulas */
        for (int i = 0; i < L; i++) {
            if (i == 0 || i == L - 1) {
                for (int j = 0; j < C; j++) {
                    atualizar_celula_borda(i, j, L, C, limiar,
                                           estado_atual, tempo_atual,
                                           cobertura, umidade,
                                           proximo_estado, proximo_tempo,
                                           &step_ignicoes, &step_apagadas);
                }
                continue;
            }

            /* Celulas da borda esquerda e direita */
            atualizar_celula_borda(i, 0, L, C, limiar,
                                   estado_atual, tempo_atual,
                                   cobertura, umidade,
                                   proximo_estado, proximo_tempo,
                                   &step_ignicoes, &step_apagadas);
            if (C > 1) {
                atualizar_celula_borda(i, C - 1, L, C, limiar,
                                       estado_atual, tempo_atual,
                                       cobertura, umidade,
                                       proximo_estado, proximo_tempo,
                                       &step_ignicoes, &step_apagadas);
            }

            const long long base = (long long)i * C;
            const unsigned char *acima = estado_atual + base - C;
            const unsigned char *meio  = estado_atual + base;
            const unsigned char *abaixo = estado_atual + base + C;

            for (int j = 1; j < C - 1; j++) {
                const int s = p_no * (acima[j - 1] == 2)  + p_n * (acima[j] == 2)  + p_ne * (acima[j + 1] == 2)
                            + p_o  * (meio[j - 1] == 2)                            + p_e  * (meio[j + 1] == 2)
                            + p_so * (abaixo[j - 1] == 2) + p_s * (abaixo[j] == 2) + p_se * (abaixo[j + 1] == 2);
                int novo_estado, novo_tempo;

                transicao(meio[j], tempo_atual[base + j], cobertura[base + j], umidade[base + j],
                          s, limiar, &novo_estado, &novo_tempo);
                proximo_estado[base + j] = (unsigned char)novo_estado;
                proximo_tempo[base + j] = novo_tempo;
                step_ignicoes += (meio[j] == 1) & (novo_estado == 2);
                step_apagadas += (meio[j] == 2) & (novo_estado == 3);
            }
        }

        /* 3. Atualizacao das estatisticas e double-buffering */
        contencao += step_contencoes;
        intactas -= (step_contencoes + step_ignicoes);
        em_chamas += (step_ignicoes - step_apagadas);
        queimadas += step_apagadas;
        total_ignicoes += step_ignicoes;

        if (step_ignicoes > pico_ignicoes_qtd) {
            pico_ignicoes_qtd = (int)step_ignicoes;
            pico_ignicoes_passo = passos_executados;
        }

        passos_executados++;

        /* Troca de ponteiros das matrizes (double-buffering) */
        unsigned char *tmp_e = estado_atual;
        estado_atual = proximo_estado;
        proximo_estado = tmp_e;

        int *tmp_t = tempo_atual;
        tempo_atual = proximo_tempo;
        proximo_tempo = tmp_t;
    }

    tempo_de_execucao = omp_get_wtime() - inicio;

    /* Calculo de percentuais com prevencao de divisao por zero */
    if (combustiveis_iniciais > 0) {
        percentual_queimado = (100.0 * (double)(queimadas + em_chamas)) / (double)combustiveis_iniciais;
        percentual_protegido = (100.0 * (double)contencao) / (double)combustiveis_iniciais;
    } else {
        percentual_queimado = 0.0;
        percentual_protegido = 0.0;
    }

    gera_relatorio();

    /* Liberacao de recursos */
    libera_config(&cfg);
    free(cobertura);
    free(umidade);
    free(estado_atual);
    free(proximo_estado);
    free(tempo_atual);
    free(proximo_tempo);
    free(ativacao);

    return 0;
}
