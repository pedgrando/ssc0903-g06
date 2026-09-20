/*
 * parte1.c
 *
 * Frente 1 - Entrada e Validacao
 * Responsabilidades:
 *   - Leitura do arquivo texto de entrada.
 *   - Validacao dos argumentos de terminal.
 *   - Validacao estrita dos parametros conforme especificacao.
 *   - Aplicacao dos focos iniciais de incendio.
 *   - Construcao do mapa de ativacao das zonas de contencao.
 *
 * Este arquivo nao possui header correspondente. As declaracoes necessarias
 * devem ser replicadas (ou incluidas) no arquivo principal fire_seq.c.
 */

#include <stdio.h>
#include <stdlib.h>

/* ============================================================
 * Tipos de dados
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
    int T;                  /* numero de threads */
    unsigned int seed;      /* semente para rand_r */
    int LIMIAR;             /* limiar de ignicao */

    int vento_linha;        /* componente vertical do vento */
    int vento_coluna;       /* componente horizontal do vento */
    int intensidade_vento;  /* intensidade do vento (0 a 5) */

    int F;                  /* quantidade de focos iniciais */
    int Z;                  /* quantidade de zonas de contencao */
    Foco *focos;            /* vetor de focos */
    Zona *zonas;            /* vetor de zonas */
} Config;

/* ============================================================
 * Funcoes auxiliares internas
 * ============================================================ */

/* Compara dois focos para ordenacao. Usado na deteccao de duplicatas. */
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
 * Le o arquivo de entrada e preenche a estrutura Config.
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

        /* Verifica focos repetidos apos ordenar por (linha, coluna). */
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
 * Marca os focos iniciais como "em chamas" e define seus tempos
 * de queima. Verifica se cada foco esta sobre celula combustivel.
 * Retorna 0 em caso de sucesso e 1 em caso de erro.
 */
int aplica_focos(const Config *cfg, int C,
                 const unsigned char *cobertura,
                 unsigned char *estado_atual,
                 int *tempo_atual) {
    for (int i = 0; i < cfg->F; i++) {
        int linha = cfg->focos[i].linha;
        int coluna = cfg->focos[i].coluna;
        long long idx = (long long)linha * C + coluna;
        unsigned char cov = cobertura[idx];

        if (cov == 0 || cov == 1) {
            fprintf(stderr, "Erro: foco (%d, %d) esta sobre celula nao combustivel\n",
                    linha, coluna);
            return 1;
        }

        estado_atual[idx] = 2;   /* em chamas */
        tempo_atual[idx] = (cov == 2) ? 2 : 4;
    }

    return 0;
}

/* ============================================================
 * constroi_mapa_ativacao
 * ============================================================
 * Preenche o vetor ativacao com o menor passo de ativacao de
 * cada celula pertencente a uma zona de contencao.
 * Retorna 0 em caso de sucesso e 1 em caso de erro.
 */
int constroi_mapa_ativacao(const Config *cfg, int C, int *ativacao) {
    for (int z = 0; z < cfg->Z; z++) {
        int passo = cfg->zonas[z].passo;

        for (int l = cfg->zonas[z].l1; l <= cfg->zonas[z].l2; l++) {
            for (int c = cfg->zonas[z].c1; c <= cfg->zonas[z].c2; c++) {
                long long idx = (long long)l * C + c;

                if (ativacao[idx] == -1 || passo < ativacao[idx]) {
                    ativacao[idx] = passo;
                }
            }
        }
    }

    return 0;
}

/* ============================================================
 * libera_config
 * ============================================================
 * Libera a memoria alocada para focos e zonas.
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
