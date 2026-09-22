#include <stdio.h>
#include <stdlib.h>
#include <omp.h> // Usado APENAS para o omp_get_wtime()
#include <math.h>

#ifdef _WIN32
int rand_r(unsigned int *seedp) {
    *seedp = *seedp * 1103515245 + 12345;
    return (unsigned int)(*seedp / 65536) % 32768;
}
#endif

// ============================================================================
// 1. ESTRUTURAS E LEITURA DE ARQUIVO (FRENTE 1)
// ============================================================================
typedef struct { int linha; int coluna; } Foco;
typedef struct { int passo; int l1; int c1; int l2; int c2; } Zona;

typedef struct {
    int L, C, P, T, LIMIAR;
    unsigned int seed;
    int vento_linha, vento_coluna, intensidade_vento;
    int F, Z;
    Foco *focos;
    Zona *zonas;
} Config;

static int compara_focos(const void *a, const void *b) {
    const Foco *fa = (const Foco *)a;
    const Foco *fb = (const Foco *)b;
    if (fa->linha != fb->linha) return fa->linha - fb->linha;
    return fa->coluna - fb->coluna;
}

int le_entrada(const char *caminho, Config *cfg) {
    FILE *fp = fopen(caminho, "r");
    if (fp == NULL) { fprintf(stderr, "Erro: nao foi possivel abrir '%s'\n", caminho); return 1; }

    int L, C, P, T, LIMIAR; unsigned int seed;
    fscanf(fp, "%d %d %d %d %u %d", &L, &C, &P, &T, &seed, &LIMIAR);
    
    int vento_linha, vento_coluna, intensidade;
    fscanf(fp, "%d %d %d", &vento_linha, &vento_coluna, &intensidade);
    
    int F, Z;
    fscanf(fp, "%d %d", &F, &Z);

    Foco *focos = NULL;
    if (F > 0) {
        focos = (Foco *)malloc(F * sizeof(Foco));
        for (int i = 0; i < F; i++) fscanf(fp, "%d %d", &focos[i].linha, &focos[i].coluna);
        qsort(focos, F, sizeof(Foco), compara_focos);
    }

    Zona *zonas = NULL;
    if (Z > 0) {
        zonas = (Zona *)malloc(Z * sizeof(Zona));
        for (int i = 0; i < Z; i++) {
            fscanf(fp, "%d %d %d %d %d", &zonas[i].passo, &zonas[i].l1, &zonas[i].c1, &zonas[i].l2, &zonas[i].c2);
        }
    }
    fclose(fp);

    cfg->L = L; cfg->C = C; cfg->P = P; cfg->T = T; cfg->seed = seed; cfg->LIMIAR = LIMIAR;
    cfg->vento_linha = vento_linha; cfg->vento_coluna = vento_coluna; cfg->intensidade_vento = intensidade;
    cfg->F = F; cfg->Z = Z; cfg->focos = focos; cfg->zonas = zonas;
    return 0;
}

void libera_config(Config *cfg) {
    if (cfg->focos != NULL) free(cfg->focos);
    if (cfg->zonas != NULL) free(cfg->zonas);
}

// ============================================================================
// 2. VARIÁVEIS GLOBAIS E PREPARAÇÃO DE TERRENO
// ============================================================================
unsigned char *cobertura, *umidade, *estado_atual, *proximo_estado;
int *tempo_atual, *proximo_tempo, *ativacao;
long long total_celulas;

int passos_executados = 0;
long long nao_combustiveis = 0, intactas = 0, em_chamas = 0;
long long queimadas = 0, contencao = 0, total_ignicoes = 0;
int pico_ignicoes_passo = -1, pico_ignicoes_qtd = 0;
double percentual_queimado = 0.0, percentual_protegido = 0.0;
double tempo_de_execucao = 0.0;

void prepara_terreno(long long L, long long C, unsigned int seed) {
    total_celulas = L * C;
    cobertura = malloc(total_celulas * sizeof(unsigned char));
    umidade = malloc(total_celulas * sizeof(unsigned char));
    estado_atual = malloc(total_celulas * sizeof(unsigned char));
    proximo_estado = malloc(total_celulas * sizeof(unsigned char));
    tempo_atual = malloc(total_celulas * sizeof(int));
    proximo_tempo = malloc(total_celulas * sizeof(int));
    ativacao = malloc(total_celulas * sizeof(int));

    for (long long i = 0; i < total_celulas; i++) {
        int val_cob = rand_r(&seed) % 100;
        if (val_cob <= 9) {
            cobertura[i] = 0; estado_atual[i] = 0; nao_combustiveis++;
        } else if (val_cob <= 19) {
            cobertura[i] = 1; estado_atual[i] = 0; nao_combustiveis++;
        } else if (val_cob <= 54) {
            cobertura[i] = 2; estado_atual[i] = 1;
        } else {
            cobertura[i] = 3; estado_atual[i] = 1;
        }
        umidade[i] = rand_r(&seed) % 101;
        tempo_atual[i] = 0;
        ativacao[i] = -1;
    }
}

int aplica_focos(const Config *cfg, int C, const unsigned char *cobertura, unsigned char *estado_atual, int *tempo_atual) {
    for (int i = 0; i < cfg->F; i++) {
        long long idx = (long long)cfg->focos[i].linha * C + cfg->focos[i].coluna;
        estado_atual[idx] = 2; 
        tempo_atual[idx] = (cobertura[idx] == 2) ? 2 : 4;
    }
    return 0;
}

int constroi_mapa_ativacao(const Config *cfg, int C, int *ativacao) {
    for (int z = 0; z < cfg->Z; z++) {
        int passo = cfg->zonas[z].passo;
        for (int l = cfg->zonas[z].l1; l <= cfg->zonas[z].l2; l++) {
            for (int c = cfg->zonas[z].c1; c <= cfg->zonas[z].c2; c++) {
                long long idx = (long long)l * C + c;
                if (ativacao[idx] == -1 || passo < ativacao[idx]) ativacao[idx] = passo;
            }
        }
    }
    return 0;
}

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

// ============================================================================
// 3. MOTOR FÍSICO SEQUENCIAL (FOGO) - SEM OPENMP
// ============================================================================
int pesos_vizinhos[3][3];

void ativar_zonas(int *ativacao, unsigned char *estado_atual, int passo_atual, Config *cfg){
    for(int z = 0; z < cfg->Z; z++){    
        if(cfg->zonas[z].passo != passo_atual) continue;
        for(int i = cfg->zonas[z].l1; i <= cfg->zonas[z].l2; i++){
            for(int j = cfg->zonas[z].c1; j <= cfg->zonas[z].c2; j++){
                int idx = i * cfg->C + j;
                if(passo_atual == ativacao[idx] && estado_atual[idx] == 1){
                    estado_atual[idx] = 4;
                    contencao++;
                    intactas--;
                }
            }
        }
    }
}

void calcular_pesos_vizinhos(int vento_linha, int vento_coluna, int intensidade, int pesos_vizinhos[3][3]){
    for(int dl=-1; dl<2; dl++){
        for(int dc=-1; dc<2; dc++){
            int prop_linha = -dl, prop_coluna = -dc;
            int peso_basico = (abs(prop_linha) + abs(prop_coluna) == 1) ? 10 : 7;
            int A = prop_linha*vento_linha + prop_coluna*vento_coluna;
            int pv = peso_basico + A*intensidade;
            pesos_vizinhos[dl + 1][dc + 1] = (pv > 1) ? pv : 1;
        }
    }
    pesos_vizinhos[1][1] = 0;
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

void atualizar_celula_borda(int i, int j, int L, int C, int limiar, const unsigned char *estado_atual, const int *tempo_atual, const unsigned char *cobertura, const unsigned char *umidade, unsigned char *proximo_estado, int *proximo_tempo, long long *ignicoes, long long *apagadas) {
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

void calcular_proximo_estado(int L, int C, const unsigned char *estado_atual, const int *tempo_atual, const unsigned char *cobertura, const unsigned char *umidade, unsigned char *proximo_estado, int *proximo_tempo, long long *ignicoes, long long *apagadas, int limiar) {
    long long ign = 0, apag = 0;
    const int p_no = pesos_vizinhos[0][0], p_n = pesos_vizinhos[0][1], p_ne = pesos_vizinhos[0][2];
    const int p_o = pesos_vizinhos[1][0],                              p_e = pesos_vizinhos[1][2];
    const int p_so = pesos_vizinhos[2][0], p_s = pesos_vizinhos[2][1], p_se = pesos_vizinhos[2][2]; 
    
    for(int i = 0; i < L; i++){
        if(i == 0 || i == L-1){
            for(int j=0; j<C; j++) atualizar_celula_borda(i, j, L, C, limiar, estado_atual, tempo_atual, cobertura, umidade, proximo_estado, proximo_tempo, &ign, &apag);
            continue;
        }
        atualizar_celula_borda(i, 0, L, C, limiar, estado_atual, tempo_atual, cobertura, umidade, proximo_estado, proximo_tempo, &ign, &apag);
        if(C > 1) atualizar_celula_borda(i, C-1, L, C, limiar, estado_atual, tempo_atual, cobertura, umidade, proximo_estado, proximo_tempo, &ign, &apag);
        
        const long long base = (long long) i*C;
        const unsigned char *acima = estado_atual + base - C;
        const unsigned char *meio  = estado_atual + base;
        const unsigned char *abaixo = estado_atual + base + C;
        
        for(int j = 1; j < C-1; j++){
            const int s = p_no * (acima[j-1] == 2) + p_n * (acima[j] == 2) + p_ne * (acima[j+1] == 2)
                        + p_o  * (meio[j-1] == 2)                          + p_e  * (meio[j+1] == 2)
                        + p_so * (abaixo[j-1] == 2)+ p_s * (abaixo[j] == 2)+ p_se * (abaixo[j+1] == 2);
            int novo_estado, novo_tempo;
            transicao(meio[j], tempo_atual[base + j], cobertura[base + j], umidade[base + j], s, limiar, &novo_estado, &novo_tempo);
            proximo_estado[base + j] = novo_estado;
            proximo_tempo[base + j] = novo_tempo;
            ign += (meio[j] == 1) & (novo_estado == 2);
            apag += (meio[j] == 2) & (novo_estado == 3);
        }
    }
    *ignicoes = ign; *apagadas = apag;
}

// ============================================================================
// 4. LOOP PRINCIPAL
// ============================================================================
int main(int argc, char *argv[]) {
    if(argc != 2){ printf("Erro: passe o arquivo de entrada como argumento.\n"); return 1; }
    
    Config cfg;
    if(le_entrada(argv[1], &cfg) != 0) return 1;

    intactas = (long long)cfg.L * cfg.C;
    prepara_terreno(cfg.L, cfg.C, cfg.seed);
    aplica_focos(&cfg, cfg.C, cobertura, estado_atual, tempo_atual);
    
    em_chamas = cfg.F;
    intactas -= (cfg.F + nao_combustiveis);
    long long combustiveis_iniciais = intactas;
    constroi_mapa_ativacao(&cfg, cfg.C, ativacao);

    double inicio = omp_get_wtime(); // Cronômetro ON
    calcular_pesos_vizinhos(cfg.vento_linha, cfg.vento_coluna, cfg.intensidade_vento, pesos_vizinhos);
    
    // Motor sequencial
    while(em_chamas > 0 && passos_executados < cfg.P){
        ativar_zonas(ativacao, estado_atual, passos_executados, &cfg);

        long long ignicoes = 0, apagadas = 0;
        calcular_proximo_estado(cfg.L, cfg.C, estado_atual, tempo_atual, cobertura, umidade, proximo_estado, proximo_tempo, &ignicoes, &apagadas, cfg.LIMIAR);

        em_chamas += ignicoes - apagadas;
        intactas -= ignicoes;
        queimadas += apagadas;
        total_ignicoes += ignicoes;
        
        if(ignicoes > pico_ignicoes_qtd){
            pico_ignicoes_qtd = ignicoes;
            pico_ignicoes_passo = passos_executados;
        }
        passos_executados++;
        
        // Swap dos ponteiros
        unsigned char *estado_temporario = estado_atual;
        estado_atual = proximo_estado;
        proximo_estado = estado_temporario;

        int *tempo_temporario = tempo_atual;
        tempo_atual = proximo_tempo;
        proximo_tempo = tempo_temporario;
    }
    
    tempo_de_execucao = omp_get_wtime() - inicio; // Cronômetro OFF

    if (combustiveis_iniciais > 0) {
        percentual_queimado = (double) 100 * (queimadas + em_chamas) / combustiveis_iniciais;
        percentual_protegido = (double) 100 * contencao / combustiveis_iniciais;
    }

    gera_relatorio();

    libera_config(&cfg);
    free(cobertura); free(umidade); free(estado_atual); free(proximo_estado);
    free(tempo_atual); free(proximo_tempo); free(ativacao);
    
    return 0;
}