#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <math.h>

#ifdef _WIN32
// O Windows não tem rand_r, então criamos uma versão provisória 
// que só existe se o código for compilado no Windows.
int rand_r(unsigned int *seedp) {
    *seedp = *seedp * 1103515245 + 12345;
    return (unsigned int)(*seedp / 65536) % 32768;
}
#endif


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
                 unsigned char *tempo_atual) {

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
int constroi_mapa_ativacao(const Config *cfg, int C, unsigned char *ativacao) {

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

unsigned char *cobertura, *umidade, *estado_atual, *proximo_estado; // char para ocupar menos memória
unsigned char *tempo_atual, *proximo_tempo, *ativacao;
long long total_celulas;

// Estatísticas 
int passos_executados = 0;
long long nao_combustiveis = 0, intactas = 0, em_chamas = 0;
long long queimadas = 0, contencao = 0, total_ignicoes = 0;
int pico_ignicoes_passo = -1, pico_ignicoes_qtd = 0;
double percentual_queimado = 0.0, percentual_protegido = 0.0;
double tempo_de_execucao = 0.0;

void prepara_terreno(long long L, long long C, unsigned int seed) {
    total_celulas = L * C; // Tranforma o 2D em uma linha 1D
    
    // Aloca a memória da matriz 1D
    cobertura = malloc(total_celulas * sizeof(unsigned char));
    umidade = malloc(total_celulas * sizeof(unsigned char));
    estado_atual = malloc(total_celulas * sizeof(unsigned char));
    proximo_estado = malloc(total_celulas * sizeof(unsigned char));
    tempo_atual = malloc(total_celulas * sizeof(int));
    proximo_tempo = malloc(total_celulas * sizeof(int));
    ativacao = malloc(total_celulas * sizeof(int));

    // Geração determinística exigida
    for (long long i = 0; i < total_celulas; i++) {
        int val_cob = rand_r(&seed) % 100;
        
        if (val_cob <= 9) {
            cobertura[i] = 0; estado_atual[i] = 0; // Água
            nao_combustiveis++;

        } else if (val_cob <= 19) {
            cobertura[i] = 1; estado_atual[i] = 0; // Solo
            nao_combustiveis++;
        } else if (val_cob <= 54) {
            cobertura[i] = 2; estado_atual[i] = 1; // Rasteira
        } else {
            cobertura[i] = 3; estado_atual[i] = 1; // Floresta
        }

        umidade[i] = rand_r(&seed) % 101;
        tempo_atual[i] = 0;
        ativacao[i] = -1;
    }
}

void gera_relatorio() {
    unsigned long long checksum = 0;
    
    // Cálculo sequencial do checksum final
    for (long long i = 0; i < total_celulas; i++) {
        checksum = checksum * 31ULL + (unsigned long long)estado_atual[i];
        checksum = checksum * 31ULL + (unsigned long long)tempo_atual[i];
    }

    // Impressão exigida
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


/*
    As minhas mudanças

*/

int pesos_vizinhos[3][3];

int ativar_zonas(unsigned char *ativacao, unsigned char *estado_atual, int passo_atual, Config *cfg){
    for(int z = 0; z < cfg->Z; z++){    
        const Zona *zona = &cfg->zonas[z];
        if(zona->passo != passo_atual)
            continue;

        int C = cfg->C;
        
        #pragma omp for reduction(+:contencao, intactas)
        for(int i = zona->l1; i <= zona->l2; i++){
            for(int j = zona->c1; j <= zona->c2; j++){
                int idx = i * C + j;
                if(passo_atual == ativacao[idx] && estado_atual[idx] == 1){
                    estado_atual[idx] = 4;
                    contencao++;
                    intactas--;
                }
            }
        }
    }
    return 0;
}

static inline int calcular_peso_basico(int prop_linha, int prop_coluna){
    return abs(prop_linha) + abs(prop_coluna) == 1? 10 : 7;
}

static inline int calcular_alinhamento_com_vento(int prop_linha, int prop_coluna, int vento_linha, int vento_coluna){
    return prop_linha*vento_linha + prop_coluna*vento_coluna;
}

static inline int calcular_peso_do_vizinho(int peso_basico, 
                                    int intensidade, 
                                    int alinhamento_com_vento)
{
    int peso_do_vizinho = peso_basico 
                        + alinhamento_com_vento*intensidade;

    return 1 > peso_do_vizinho ? 1 : peso_do_vizinho;
}

void calcular_pesos_vizinhos(int vento_linha, int vento_coluna, int intensidade, int pesos_vizinhos[3][3]){

    for(int dl=-1; dl<2;dl++){
        for(int dc=-1; dc<2;dc++){
            const int prop_linha = -dl;
            const int prop_coluna = -dc;
            const int peso_basico = calcular_peso_basico(prop_linha, prop_coluna);
            const int A = calcular_alinhamento_com_vento(prop_linha, prop_coluna, vento_linha, vento_coluna);
            const int pv = calcular_peso_do_vizinho(peso_basico, intensidade, A);

            pesos_vizinhos[dl + 1][dc + 1] = pv > 1 ? pv:1;
        }
    }

    pesos_vizinhos[1][1] = 0; //a propria celula nao é vizinha
}

static inline int calcular_potencial_de_ignicao(int s, int fator_combustivel, int umidade){
    return (s*fator_combustivel*(100-umidade)) / 100;
}

void transicao(int estado, int tempo, int cob, int umidade, int S, int limiar, int *novo_estado, int *novo_tempo){
    int fator_combustivel = 8*(cob == 2) + 12*(cob==3);
    int potencial = S * fator_combustivel * (100-umidade) / 100;
    int ignicao = (potencial >= limiar) & (estado == 1);
    int queimando = (estado == 2);
    int apagou = queimando & (tempo == 1);

    *novo_estado = estado + ignicao + apagou;
    *novo_tempo = tempo - queimando + ignicao *(2 + 2*(cob==3));
}

void atualizar_celula_borda(int i, int j, int L, int C, int limiar,
                            const unsigned char *estado_atual, const unsigned char *tempo_atual,
                            const unsigned char *cobertura, const unsigned char *umidade,
                            unsigned char *proximo_estado, unsigned char *proximo_tempo,
                            long long *ignicoes, long long *apagadas
                            )
{
    long long idx = (long long) i * C + j;
    int s = 0, novo_estado, novo_tempo;

    for(int dl = -1; dl <=1 ; dl++){
        for(int dc=-1; dc <=1; dc++){
            int lv = i+dl, cv=j+dc;
            if(lv >= 0 && lv < L && cv >= 0 && cv < C && estado_atual[(long long)lv*C + cv] == 2)
                s += pesos_vizinhos[dl+1][dc+1];
        }
    }

    transicao(estado_atual[idx], tempo_atual[idx], cobertura[idx], umidade[idx], s, limiar, &novo_estado, &novo_tempo);
    proximo_estado[idx] = novo_estado;
    proximo_tempo[idx] = novo_tempo;
    *ignicoes += (estado_atual[idx] == 1) & (proximo_estado[idx] == 2);
    *apagadas += (estado_atual[idx] == 2) & (proximo_estado[idx] == 3);

}

void calcular_proximo_estado(int L, int C, 
                            int vento_linha, int vento_coluna, 
                            int intensidade,

                            const unsigned char *restrict estado_atual, const unsigned char *restrict tempo_atual,
                            const unsigned char *restrict cobertura, const unsigned char *restrict umidade,
                            unsigned char *restrict proximo_estado, unsigned char *restrict proximo_tempo,

                            long long *ignicoes, long long *apagadas, int limiar
){

    long long ign = 0, apag = 0;
    const int p_no = pesos_vizinhos[0][0], p_n = pesos_vizinhos[0][1], p_ne = pesos_vizinhos[0][2];
    const int p_o = pesos_vizinhos[1][0],                                p_e = pesos_vizinhos[1][2];
    const int p_so = pesos_vizinhos[2][0], p_s = pesos_vizinhos[2][1], p_se = pesos_vizinhos[2][2]; 
    
    #pragma omp for schedule(static) reduction(+:ign, apag)
    for(int i = 0; i < L; i++){

        if( i==0 || i== L-1){
            for(int j=0; j<C; j++){
                atualizar_celula_borda(i, j, L, C, limiar, estado_atual, tempo_atual, cobertura, umidade, proximo_estado, proximo_tempo, &ign, &apag);
            }
            continue;
        }

        atualizar_celula_borda(i, 0, L, C, limiar, estado_atual, tempo_atual, cobertura, umidade, proximo_estado, proximo_tempo, &ign, &apag);
        if(C > 1)
            atualizar_celula_borda(i, C-1, L, C, limiar, estado_atual, tempo_atual, cobertura, umidade, proximo_estado, proximo_tempo, &ign, &apag);
        
        const long long base = (long long) i*C;
        const unsigned char *acima = estado_atual+base - C;
        const unsigned char *meio  = estado_atual+base;
        const unsigned char *abaixo = estado_atual + base + C;
        int ign_linha = 0, apag_linha = 0;
        
        #pragma omp simd reduction(+:ign_linha, apag_linha)
        for(int j = 1; j < C-1; j++){

            const int s = p_no * (acima[j - 1] == 2)  + p_n * (acima[j] == 2)  + p_ne * (acima[j + 1] == 2)
                        + p_o  * (meio[j - 1] == 2)                            + p_e  * (meio[j + 1] == 2)
                        + p_so * (abaixo[j - 1] == 2) + p_s * (abaixo[j] == 2) + p_se * (abaixo[j + 1] == 2);
            int novo_estado, novo_tempo;
            
            transicao(meio[j], tempo_atual[base + j], cobertura[base + j], umidade[base + j], s, limiar, &novo_estado, &novo_tempo);
            proximo_estado[base + j] = novo_estado;
            proximo_tempo[base + j] = novo_tempo;
            ign_linha += (meio[j] == 1) & (novo_estado == 2);
            apag_linha += (meio[j] == 2) & (novo_estado == 3);
        }
        ign += ign_linha;
        apag += apag_linha;
    }
    *ignicoes = ign;
    *apagadas = apag;
}




int main(int argc, char *argv[]) {
    
    // [FRENTE 1]: Lógica de abrir arquivo e validar argumentos entra aqui.
    
    // Valores "mockados" só para testar se o código funciona.
    //long long L = 2500, C = 2500; 
    //unsigned int seed = 2027;
    Config cfg;
    if(argc != 2){
        printf("Incorrect number of arguments");
        return 1;
    }
    const char* entrada = argv[1]; 
    
    le_entrada(entrada, &cfg);
    omp_set_num_threads(cfg.T);

    intactas = cfg.L*cfg.C;
    prepara_terreno(cfg.L, cfg.C, cfg.seed);
    aplica_focos(&cfg, cfg.C, cobertura, estado_atual, tempo_atual);
    em_chamas = cfg.F;
    intactas-= cfg.F + nao_combustiveis;
    int combustiveis_iniciais = intactas;
    constroi_mapa_ativacao(&cfg, cfg.C, ativacao);

    // Espaço para verificar focos:
    for(int i = 0; i < cfg.L; i++){
        for(int j = 0; j < cfg.C; j++){
            int idx = i*cfg.C + j;
            //printf("%d\n", ativacao[idx]);
            if(estado_atual[idx] == 2){
                printf("Foco: %d, %d\n", i, j);
            }
        }
    }

    // [ESPAÇO DA FRENTE 1 e 2]: Lógica de marcar os focos de incêndio e zonas de contenção.

    double inicio = omp_get_wtime(); // Liga o cronômetro
    calcular_pesos_vizinhos(cfg.vento_linha, cfg.vento_coluna, cfg.intensidade_vento, pesos_vizinhos);
    //for(int passo = 0; passo < cfg.P; passo++){
    //    ativar_zonas(ativacao, estado_atual, passo, cfg.L, cfg.C);
    //}

    #pragma omp parallel default(none) shared(cfg, ativacao, estado_atual, proximo_estado, tempo_atual, proximo_tempo, cobertura, umidade, pesos_vizinhos, em_chamas, passos_executados, intactas, queimadas, contencao, total_ignicoes, pico_ignicoes_qtd, pico_ignicoes_passo)
    {
        while(em_chamas > 0 && passos_executados < cfg.P){
            
            ativar_zonas(ativacao, estado_atual, passos_executados, &cfg);

            long long ignicoes = 0, apagadas = 0;
            calcular_proximo_estado(cfg.L, cfg.C,
                                    cfg.vento_linha, cfg.vento_coluna,
                                    cfg.intensidade_vento,
                                    estado_atual, tempo_atual, cobertura, umidade,
                                    proximo_estado, proximo_tempo, &ignicoes, &apagadas,
                                    cfg.LIMIAR
            );

            #pragma omp single
            {
                em_chamas += ignicoes - apagadas;
                intactas -= ignicoes;
                queimadas += apagadas;
                total_ignicoes += ignicoes;
                if(ignicoes > pico_ignicoes_qtd){
                    pico_ignicoes_qtd = ignicoes;
                    pico_ignicoes_passo = passos_executados;
                }
                passos_executados++;
                
                unsigned char *estado_temporario = estado_atual;
                estado_atual = proximo_estado;
                proximo_estado = estado_temporario;

                unsigned char *tempo_temporario = tempo_atual;
                tempo_atual = proximo_tempo;
                proximo_tempo = tempo_temporario;
            }
        }
    }
    
    // [ESPAÇO DA FRENTE 3]: O for de tempo e simulação do fogo fica rodando aqui dentro.
    
    tempo_de_execucao = omp_get_wtime() - inicio; // Desliga cronômetro
    libera_config(&cfg);

    if (combustiveis_iniciais > 0) {
        percentual_queimado = (double) 100 * (queimadas + em_chamas) / combustiveis_iniciais;
        percentual_protegido = (double) 100 * contencao / combustiveis_iniciais;
    } else {
        percentual_queimado = 0.0;
        percentual_protegido = 0.0;
    }
    gera_relatorio();

    // Limpeza da memória alocada dinamicamente
    free(cobertura);
    free(umidade);
    free(estado_atual);
    free(proximo_estado);
    free(tempo_atual);
    free(proximo_tempo);
    free(ativacao);
    
    return 0;
}
