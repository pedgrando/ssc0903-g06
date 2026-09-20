#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

#ifdef _WIN32
// O Windows não tem rand_r, então criamos uma versão provisória 
// que só existe se o código for compilado no Windows.
int rand_r(unsigned int *seedp) {
    *seedp = *seedp * 1103515245 + 12345;
    return (unsigned int)(*seedp / 65536) % 32768;
}
#endif

unsigned char *cobertura, *umidade, *estado_atual, *proximo_estado; // char para ocupar menos memória
int *tempo_atual, *proximo_tempo, *ativacao;
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
        } else if (val_cob <= 19) {
            cobertura[i] = 1; estado_atual[i] = 0; // Solo
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

int main(int argc, char *argv[]) {
    
    // [FRENTE 1]: Lógica de abrir arquivo e validar argumentos entra aqui.
    
    // Valores "mockados" só para testar se o código funciona.
    long long L = 1000, C = 1500; 
    unsigned int seed = 2027;

    prepara_terreno(L, C, seed);
    
    // [ESPAÇO DA FRENTE 1 e 2]: Lógica de marcar os focos de incêndio e zonas de contenção.

    double inicio = omp_get_wtime(); // Liga o cronômetro
    
    // [ESPAÇO DA FRENTE 3]: O for de tempo e simulação do fogo fica rodando aqui dentro.
    
    tempo_de_execucao = omp_get_wtime() - inicio; // Desliga cronômetro

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