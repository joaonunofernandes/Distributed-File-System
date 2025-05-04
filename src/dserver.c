#include "Document_Struct.h"

// Estrutura para armazenar os documentos em memória (cache)
typedef struct {
    Document* docs[MAX_DOCS]; // Array de ponteiros para documentos armazenados na cache
    int num_docs;             // Contador de documentos atualmente na cache
    int max_size;             // Tamanho máximo da cache (número máximo de documentos permitidos)
    int modified;             // Flag para indicar se houve modificações desde a última gravação em disco
} Cache;

// Variáveis globais
Cache cache;                // Instância da cache que mantém os documentos em memória
char base_folder[256];      // Pasta base onde os ficheiros de documentos estão armazenados
int next_id = 1;            // Contador global para atribuição de IDs únicos aos documentos

// Protótipos das funções (declarações antecipadas)
int add_document(Document* doc);                                             // Adiciona um documento à cache
Document* find_document(int id);                                             // Procura um documento pelo seu ID
int remove_document(int id);                                                 // Remove um documento da cache e marca para remoção no disco
int count_lines_with_keyword(Document* doc, char* keyword);                  // Conta linhas com palavra-chave num documento
int search_documents_with_keyword_parallel(char* keyword, int* result_ids, int nr_processes); // Pesquisa documentos em paralelo
int search_documents_with_keyword_serial(char* keyword, int* result_ids);    // Pesquisa documentos em série
void save_documents();                                                       // Guarda documentos da cache para o disco
void load_documents();                                                       // Carrega documentos do disco para a cache
void handle_signals(int sig);                                                // Trata sinais do sistema operativo (SIGINT, SIGTERM)

// Função para adicionar um documento à cache
int add_document(Document* doc) {
    // Verifica se a cache está cheia (num_docs >= max_size)
    if (cache.num_docs >= cache.max_size) {
        // Se a cache estiver cheia, aplica-se a política FCFS (First Come First Serve)
        // ou seja, remove-se o documento mais antigo (o primeiro adicionado à cache)
        int idx_to_replace = 0; // Índice 0 corresponde ao documento mais antigo na implementação FCFS
        
        // Adiciona mensagem a informar que o FCFS está a ser utilizado
        char msg[256];
        int len = snprintf(msg, sizeof(msg), 
                        "Cache cheia: a aplicar política FCFS para remover documento ID: %d, Título: '%s' para incluir novo documento Título: '%s'\n", 
                        cache.docs[idx_to_replace]->id, 
                        cache.docs[idx_to_replace]->title,
                        doc->title);
        write(STDOUT_FILENO, msg, len);
        
        // Liberta a memória alocada para o documento mais antigo
        // Esta libertação de memória é crucial para evitar fugas de memória (memory leaks)
        free(cache.docs[idx_to_replace]);
        
        // Move todos os documentos uma posição para a esquerda no array
        // Esta operação efetivamente "empurra" todos os documentos uma posição para frente
        // para ocupar o espaço deixado pelo documento removido
        for (int i = 0; i < cache.num_docs - 1; i++) {
            cache.docs[i] = cache.docs[i + 1];
        }
        cache.num_docs--; // Decrementa o contador de documentos
    }
    
    // Aloca memória para o novo documento
    // sizeof(Document) reserva exatamente o espaço necessário para a estrutura Document
    Document* new_doc = malloc(sizeof(Document));
    if (!new_doc) return -1; // Verifica se a alocação falhou
    
    // Copia os dados do documento recebido para a nova memória alocada
    // memcpy realiza uma cópia bit a bit da memória de origem para o destino
    memcpy(new_doc, doc, sizeof(Document));
    
    // Atribui um ID único ao documento e incrementa o contador global
    new_doc->id = next_id++;
    
    // Adiciona o ponteiro do novo documento ao final do array da cache
    cache.docs[cache.num_docs++] = new_doc;
    
    // Marca a cache como modificada, indicando que houve alterações não persistidas em disco
    // A persistência só ocorrerá quando for executado o comando -f (SHUTDOWN) pelo cliente
    cache.modified = 1;
    
    // Retorna o ID atribuído ao documento
    return new_doc->id;
}

// Função para encontrar um documento pelo ID
Document* find_document(int id) {
    // Primeiro, procura o documento na cache (acesso mais rápido por estar em memória)
    for (int i = 0; i < cache.num_docs; i++) {
        if (cache.docs[i]->id == id) {
            return cache.docs[i]; // Retorna o ponteiro para o documento se encontrado
        }
    }
    
    // Se não encontrou na cache, procura no ficheiro da base de dados em disco
    int fd = open("database.bin", O_RDONLY); // Abre o ficheiro apenas para leitura
    if (fd < 0) return NULL; // Se não conseguir abrir o ficheiro, retorna NULL (não encontrado)
    
    // Salta o cabeçalho do ficheiro (next_id e num_docs)
    // lseek posiciona o cursor de leitura do ficheiro após os dois inteiros do cabeçalho
    lseek(fd, 2 * sizeof(int), SEEK_SET);
    
    // Lê documentos um por um do ficheiro até encontrar o ID desejado
    Document disk_doc;
    while (read(fd, &disk_doc, sizeof(Document)) == sizeof(Document)) {
        if (disk_doc.id == id) {
            close(fd); // Fecha o ficheiro pois já encontrámos o que procurávamos
            
            // Se há espaço na cache, adiciona este documento para acesso mais rápido no futuro
            if (cache.num_docs < cache.max_size) {
                Document* doc = malloc(sizeof(Document));
                memcpy(doc, &disk_doc, sizeof(Document));
                
                cache.docs[cache.num_docs++] = doc;
                return cache.docs[cache.num_docs - 1]; // Retorna o ponteiro para o documento na cache
            } else {
                // Se a cache está cheia, cria uma cópia temporária do documento
                // NOTA: Esta abordagem pode causar fuga de memória se o chamador não libertar esta cópia
                Document* temp_doc = malloc(sizeof(Document));
                memcpy(temp_doc, &disk_doc, sizeof(Document));
                return temp_doc;
            }
        }
    }
    
    close(fd); // Fecha o ficheiro se não encontrou o documento
    return NULL; // Documento não encontrado nem na cache nem no disco
}

int remove_document(int id) {
    // 1. Tenta remover da cache
    for (int i = 0; i < cache.num_docs; i++) {
        if (cache.docs[i]->id == id) {
            free(cache.docs[i]);
            for (int j = i; j < cache.num_docs - 1; j++) {
                cache.docs[j] = cache.docs[j + 1];
            }
            cache.num_docs--;
            cache.modified = 1;
            return 0;
        }
    }

    // 2. Se não está na cache, remove diretamente do ficheiro database.bin
    int fd = open("database.bin", O_RDWR);
    if (fd < 0) return -1;

    int next_id_disk, num_docs_disk;
    if (read(fd, &next_id_disk, sizeof(int)) != sizeof(int) ||
        read(fd, &num_docs_disk, sizeof(int)) != sizeof(int)) {
        close(fd);
        return -1;
    }

    // Lê todos os documentos
    Document docs[MAX_DOCS];
    int valid_docs = 0;

    for (int i = 0; i < num_docs_disk; i++) {
        Document d;
        if (read(fd, &d, sizeof(Document)) != sizeof(Document)) break;
        if (d.id != id) {
            docs[valid_docs++] = d;
        }
    }

    // Reescreve o ficheiro com os documentos válidos
    lseek(fd, 0, SEEK_SET);
    write(fd, &next_id_disk, sizeof(int));
    write(fd, &valid_docs, sizeof(int));
    for (int i = 0; i < valid_docs; i++) {
        write(fd, &docs[i], sizeof(Document));
    }

    // Trunca o ficheiro no tamanho exato
    ftruncate(fd, sizeof(int) * 2 + valid_docs * sizeof(Document));
    close(fd);

    return 0; // Sucesso
}


// Função para contar linhas com uma palavra-chave num documento
int count_lines_with_keyword(Document* doc, char* keyword) {
    if (!doc) return -1; // Verifica se o documento é válido
    
    // Constrói o caminho completo para o ficheiro do documento
    char full_path[MAX_PATH_SIZE + 256];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_folder, doc->path);

    // Variáveis para pipes e PIDs
    int pipe_grep_wc[2];   // Pipe para comunicação: grep -> wc
    int pipe_wc_parent[2]; // Pipe para comunicação: wc -> processo pai
    pid_t pid_grep, pid_wc; // PIDs dos processos filho
    int status;
    int line_count = -1; // Valor padrão em caso de erro

    // Cria os dois pipes necessários para a comunicação entre processos
    if (pipe(pipe_grep_wc) < 0 || pipe(pipe_wc_parent) < 0) {
        perror("Erro ao criar pipes");
        // Fecha os pipes que possam ter sido abertos para evitar vazamento de descritores
        if (pipe_grep_wc[0] >= 0) close(pipe_grep_wc[0]);
        if (pipe_grep_wc[1] >= 0) close(pipe_grep_wc[1]);
        if (pipe_wc_parent[0] >= 0) close(pipe_wc_parent[0]);
        if (pipe_wc_parent[1] >= 0) close(pipe_wc_parent[1]);
        return -1;
    }

    // 1. Fork para o processo 'grep'
    // O grep filtrará as linhas que contêm a palavra-chave diretamente do arquivo
    pid_grep = fork();
    if (pid_grep == 0) { // Processo filho para executar 'grep'
        // Redireciona stdout para o pipe_grep_wc (grep escreverá neste pipe)
        close(pipe_grep_wc[0]); // Fecha a extremidade de leitura do pipe grep->wc (não utilizada por grep)
        dup2(pipe_grep_wc[1], STDOUT_FILENO); // Redireciona stdout para a extremidade de escrita do pipe
        close(pipe_grep_wc[1]); // Fecha o descritor original após dup2 (já foi duplicado)

        // Fecha os pipes não utilizados por este processo
        close(pipe_wc_parent[0]); // Fecha leitura do pipe wc->pai (não utilizada por grep)
        close(pipe_wc_parent[1]); // Fecha escrita do pipe wc->pai (não utilizada por grep)

        // Executa o comando grep, filtrando as linhas que contêm a palavra-chave
        // O grep lê diretamente do arquivo, eliminando a necessidade do cat
        execlp("grep", "grep", keyword, full_path, (char*)NULL);
        perror("Erro ao executar grep");
        _exit(1); // Termina com erro
    } else if (pid_grep < 0) { // Erro ao criar o processo filho
        perror("Erro no fork para grep");
        // Fecha pipes antes de retornar
        close(pipe_grep_wc[0]); 
        close(pipe_grep_wc[1]);
        close(pipe_wc_parent[0]); 
        close(pipe_wc_parent[1]);
        return -1;
    }

    // 2. Fork para o processo 'wc'
    // wc contará o número de linhas que contêm a palavra-chave
    pid_wc = fork();
    if (pid_wc == 0) { // Processo filho para executar 'wc'
        // Redireciona stdin do pipe_grep_wc (wc lerá deste pipe)
        close(pipe_grep_wc[1]); // Fecha a extremidade de escrita do pipe grep->wc (não utilizada por wc)
        dup2(pipe_grep_wc[0], STDIN_FILENO); // Redireciona stdin para a extremidade de leitura do pipe
        close(pipe_grep_wc[0]); // Fecha o descritor original após dup2 (já foi duplicado)

        // Redireciona stdout para o pipe_wc_parent (wc escreverá neste pipe)
        close(pipe_wc_parent[0]); // Fecha a extremidade de leitura do pipe wc->pai (não utilizada por wc)
        dup2(pipe_wc_parent[1], STDOUT_FILENO); // Redireciona stdout para a extremidade de escrita do pipe
        close(pipe_wc_parent[1]); // Fecha o descritor original após dup2 (já foi duplicado)

        // Executa o comando wc -l para contar linhas
        execlp("wc", "wc", "-l", (char*)NULL);
        perror("Erro ao executar wc");
        _exit(1); // Termina com erro
    } else if (pid_wc < 0) { // Erro ao criar o processo filho
        perror("Erro no fork para wc");
        // Fecha pipes e espera pelo filho grep antes de retornar
        close(pipe_grep_wc[0]); 
        close(pipe_grep_wc[1]);
        close(pipe_wc_parent[0]); 
        close(pipe_wc_parent[1]);
        waitpid(pid_grep, NULL, 0); // Limpa o processo grep
        return -1;
    }

    // 3. Processo Pai - Lê o resultado do wc
    // O pai deve fechar todas as extremidades dos pipes que não utiliza
    close(pipe_grep_wc[0]); // Fecha leitura do pipe grep->wc (não utilizada pelo pai)
    close(pipe_grep_wc[1]); // Fecha escrita do pipe grep->wc (não utilizada pelo pai)
    close(pipe_wc_parent[1]); // Fecha escrita do pipe wc->pai (não utilizada pelo pai)
    // Mantém pipe_wc_parent[0] aberto pois o pai vai ler deste pipe

    // Lê o resultado do pipe wc->pai
    // O resultado é o número de linhas que contêm a palavra-chave
    char buffer[32]; // Buffer para armazenar a saída do wc
    ssize_t bytes_read = read(pipe_wc_parent[0], buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0'; // Adiciona terminador nulo para converter em string
        line_count = atoi(buffer); // Converte a string com o número de linhas para inteiro
    } else {
        // Se não houver dados ou ocorrer erro, assume 0 linhas
        line_count = 0;
    }

    // Fecha o descritor de leitura restante no pai
    close(pipe_wc_parent[0]);

    // Espera que todos os processos filhos terminem para evitar processos zombie
    waitpid(pid_grep, &status, 0);
    waitpid(pid_wc, &status, 0);

    return line_count; // Retorna o número de linhas encontradas ou um código de erro
}

// Função para procurar documentos com uma palavra-chave (versão série otimizada)
int search_documents_with_keyword_serial(char* keyword, int* result_ids) {
    int count = 0; // Contador de documentos encontrados
    
    // 1. Procura em todos os documentos da cache primeiro (mais rápido)
    for (int i = 0; i < cache.num_docs && count < MAX_RESULT_IDS; i++) {
        // Usa a função count_lines_with_keyword para verificar se o documento contém a palavra-chave
        int lines = count_lines_with_keyword(cache.docs[i], keyword);
        if (lines > 0) {
            // Encontrou pelo menos uma linha com a palavra-chave neste documento
            result_ids[count++] = cache.docs[i]->id; // Adiciona o ID ao array de resultados
        }
    }
    
    // 2. Procura nos documentos do disco que não estão na cache
    int fd = open("database.bin", O_RDONLY);
    if (fd < 0) return count; // Se não conseguir abrir o ficheiro, retorna os resultados já encontrados
    
    // Lê o cabeçalho do ficheiro database.bin
    int next_id_disk, num_docs_disk;
    read(fd, &next_id_disk, sizeof(int)); // Lê o próximo ID
    read(fd, &num_docs_disk, sizeof(int)); // Lê o número total de documentos
    
    // Lê cada documento do disco e verifica se contém a palavra-chave
    Document disk_doc;
    while (read(fd, &disk_doc, sizeof(Document)) == sizeof(Document) && count < MAX_RESULT_IDS) {
        // Verifica se este documento já está na cache para evitar repetições
        int in_cache = 0;
        for (int i = 0; i < cache.num_docs; i++) {
            if (cache.docs[i]->id == disk_doc.id) {
                in_cache = 1;
                break;
            }
        }
        
        if (!in_cache) {
            // Se não está na cache, usa grep diretamente para verificar se o documento contém a palavra-chave
            char full_path[MAX_PATH_SIZE + 256];
            snprintf(full_path, sizeof(full_path), "%s/%s", base_folder, disk_doc.path);
            
            // Cria um pipe para comunicação entre o processo pai e o grep
            int pipe_grep[2];
            if (pipe(pipe_grep) < 0) {
                continue; // Se não conseguir criar o pipe, passa para o próximo documento
            }
            
            // Fork para executar o grep
            pid_t pid = fork();
            if (pid == 0) { // Processo filho
                // Redireciona stdout para o pipe
                close(pipe_grep[0]); // Fecha a extremidade de leitura
                dup2(pipe_grep[1], STDOUT_FILENO);
                close(pipe_grep[1]);
                
                // Executa grep -q (quiet mode) para apenas retornar o código de saída
                execlp("grep", "grep", "-q", keyword, full_path, (char*)NULL);
                perror("Erro ao executar grep");
                _exit(1);
            } else if (pid > 0) { // Processo pai
                close(pipe_grep[1]); // Fecha a extremidade de escrita
                
                int status;
                waitpid(pid, &status, 0);
                
                // Se grep retornar 0, significa que encontrou a palavra-chave
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    result_ids[count++] = disk_doc.id;
                }
                
                close(pipe_grep[0]);
            } else { // Erro no fork
                close(pipe_grep[0]);
                close(pipe_grep[1]);
            }
        }
    }
    
    close(fd); // Fecha o ficheiro da base de dados
    return count; // Retorna o número de documentos encontrados
}

void search_process(int start, int end, char* keyword, char* temp_file, int* cache_ids, int num_cache_ids) {
    char msg[128];
    int len = snprintf(msg, sizeof(msg), "[PID %d] Iniciado: documentos [%d, %d)\n", getpid(), start, end);
    write(STDOUT_FILENO, msg, len);

    int fd = open("database.bin", O_RDONLY);
    if (fd < 0) {
        int temp_fd = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (temp_fd >= 0) close(temp_fd);
        exit(0);
    }

    lseek(fd, 2 * sizeof(int), SEEK_SET);
    lseek(fd, start * sizeof(Document), SEEK_CUR);

    int temp_fd = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (temp_fd < 0) {
        close(fd);
        exit(0);
    }

    int found_ids[MAX_RESULT_IDS];
    int count = 0;
    Document doc;

    for (int i = 0; i < (end - start) && read(fd, &doc, sizeof(Document)) == sizeof(Document); i++) {
        int skip = 0;
        for (int j = 0; j < num_cache_ids; j++) {
            if (doc.id == cache_ids[j]) {
                skip = 1;
                break;
            }
        }
        if (skip) continue;

        char full_path[MAX_PATH_SIZE + 256];
        snprintf(full_path, sizeof(full_path), "%s/%s", base_folder, doc.path);

        int pipefd[2];
        if (pipe(pipefd) == 0) {
            pid_t pid = fork();
            if (pid == 0) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
                execlp("grep", "grep", "-q", keyword, full_path, (char*)NULL);
                _exit(1);
            } else if (pid > 0) {
                close(pipefd[1]);
                int status;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    if (count < MAX_RESULT_IDS) {
                        found_ids[count++] = doc.id;
                        len = snprintf(msg, sizeof(msg), "[PID %d] Documento ID %d contém a keyword\n", getpid(), doc.id);
                        write(STDOUT_FILENO, msg, len);
                    }
                }
                close(pipefd[0]);
            } else {
                close(pipefd[0]);
                close(pipefd[1]);
            }
        }
    }

    write(temp_fd, &count, sizeof(int));
    if (count > 0) {
        write(temp_fd, found_ids, count * sizeof(int));
    }

    len = snprintf(msg, sizeof(msg), "[PID %d] Terminou com %d resultados\n", getpid(), count);
    write(STDOUT_FILENO, msg, len);

    close(temp_fd);
    close(fd);
    exit(0);
}



int search_documents_with_keyword_parallel(char* keyword, int* result_ids, int nr_processes) {
    int count = 0;
    int cache_ids[MAX_DOCS];
    int num_cache_ids = 0;

    for (int i = 0; i < cache.num_docs && count < MAX_RESULT_IDS; i++) {
        int lines = count_lines_with_keyword(cache.docs[i], keyword);
        if (lines > 0) {
            result_ids[count++] = cache.docs[i]->id;
        }
        cache_ids[num_cache_ids++] = cache.docs[i]->id;
    }

    int fd = open("database.bin", O_RDONLY);
    if (fd < 0) {
        write(STDOUT_FILENO, "Base de dados não encontrada. Apenas documentos em cache processados.\n", 70);
        return count;
    }

    int next_id_disk, num_docs_disk;
    if (read(fd, &next_id_disk, sizeof(int)) != sizeof(int) ||
        read(fd, &num_docs_disk, sizeof(int)) != sizeof(int) || num_docs_disk <= 0) {
        close(fd);
        write(STDOUT_FILENO, "Base de dados inválida ou vazia.\n", 34);
        return count;
    }
    close(fd);

    if (nr_processes <= 0) nr_processes = 1;
    if (nr_processes > num_docs_disk) nr_processes = num_docs_disk;
    if (nr_processes > 20) nr_processes = 20;
    if (nr_processes <= 1 || num_docs_disk <= 10) {
        write(STDOUT_FILENO, "A usar versão serial para pesquisa.\n", 36);
        return search_documents_with_keyword_serial(keyword, result_ids + count) + count;
    }

    char debug_msg[128];
    int len = snprintf(debug_msg, sizeof(debug_msg), "A usar pesquisa paralela com %d processos.\n", nr_processes);
    write(STDOUT_FILENO, debug_msg, len);

    int docs_per_process = num_docs_disk / nr_processes;
    int remainder = num_docs_disk % nr_processes;

    pid_t pids[nr_processes];
    char temp_files[nr_processes][64];

    for (int i = 0; i < nr_processes; i++) {
        int start = i * docs_per_process + (i < remainder ? i : remainder);
        int end = start + docs_per_process + (i < remainder ? 1 : 0);

        if (start >= end) continue;

        snprintf(temp_files[i], sizeof(temp_files[i]), "/tmp/search_results_%d_%d.tmp", getpid(), i);

        len = snprintf(debug_msg, sizeof(debug_msg), "A criar processo filho %d para documentos [%d, %d)\n", i, start, end);
        write(STDOUT_FILENO, debug_msg, len);

        pid_t pid = fork();

        if (pid == 0) {
            search_process(start, end, keyword, temp_files[i], cache_ids, num_cache_ids);
            exit(0);
        } else if (pid > 0) {
            pids[i] = pid;
        } else {
            write(STDERR_FILENO, "Erro ao criar processo filho\n", 30);
        }
    }

    for (int i = 0; i < nr_processes; i++) {
        waitpid(pids[i], NULL, 0);
    }

    for (int i = 0; i < nr_processes && count < MAX_RESULT_IDS; i++) {
        int temp_fd = open(temp_files[i], O_RDONLY);
        if (temp_fd >= 0) {
            int num_ids = 0;
            if (read(temp_fd, &num_ids, sizeof(int)) == sizeof(int) && num_ids > 0) {
                int buffer[MAX_RESULT_IDS];
                int bytes = read(temp_fd, buffer, num_ids * sizeof(int));
                if (bytes == num_ids * sizeof(int)) {
                    for (int j = 0; j < num_ids && count < MAX_RESULT_IDS; j++) {
                        result_ids[count++] = buffer[j];
                    }

                    len = snprintf(debug_msg, sizeof(debug_msg), "Processo %d encontrou %d documentos\n", i, num_ids);
                    write(STDOUT_FILENO, debug_msg, len);
                }
            }
            close(temp_fd);
            unlink(temp_files[i]);
        }
    }

    len = snprintf(debug_msg, sizeof(debug_msg), "Total de documentos encontrados: %d\n", count);
    write(STDOUT_FILENO, debug_msg, len);

    return count;
}



void save_documents() {
    if (!cache.modified) return;

    int fd = open("database.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        write(STDERR_FILENO, "Erro ao abrir ficheiro da base de dados\n", strlen("Erro ao abrir ficheiro da base de dados\n"));
        return;
    }

    write(STDOUT_FILENO, "A gravar documentos na base de dados...\n", strlen("A gravar documentos na base de dados...\n"));

    // Grava o next_id (valor global para novos IDs)
    write(fd, &next_id, sizeof(int));

    // Prepara um buffer com documentos válidos da cache
    int total_docs = 0;
    Document valid_docs[MAX_DOCS];

    for (int i = 0; i < cache.num_docs; i++) {
        if (cache.docs[i] != NULL) {
            memcpy(&valid_docs[total_docs], cache.docs[i], sizeof(Document));
            total_docs++;
        }
    }

    // Grava o número total de documentos válidos
    write(fd, &total_docs, sizeof(int));

    // Grava todos os documentos
    for (int i = 0; i < total_docs; i++) {
        write(fd, &valid_docs[i], sizeof(Document));
    }

    close(fd);

    char msg[64];
    int len = snprintf(msg, sizeof(msg), "Gravados %d documentos com sucesso.\n", total_docs);
    write(STDOUT_FILENO, msg, len);

    cache.modified = 0;
}

// Função para carregar os documentos do disco (versão modificada)
void load_documents() {
    // Tenta abrir o ficheiro da base de dados para leitura
    int fd = open("database.bin", O_RDONLY);

    // Inicializa o estado como se não houvesse ficheiro
    next_id = 1; // ID inicial para novos documentos
    cache.num_docs = 0; // Cache vazia inicialmente
    cache.modified = 0; // Nada modificado ainda

    if (fd < 0) {
        // Trata a falha ao abrir o ficheiro
        if (errno == ENOENT) {
            // Ficheiro não existe - isto é normal na primeira execução
            write(STDOUT_FILENO, "Ficheiro da base de dados 'database.bin' não encontrado. A iniciar com estado vazio.\n",
                strlen("Ficheiro da base de dados 'database.bin' não encontrado. A iniciar com estado vazio.\n"));
        } else {
            // Outro erro ao tentar abrir (permissões, etc.)
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Erro ao tentar abrir 'database.bin' para leitura: %s. A iniciar com estado vazio.\n", strerror(errno));
            write(STDERR_FILENO, error_msg, strlen(error_msg));
        }
        // Em qualquer caso de erro, inicia com estado vazio (já inicializado acima)
        return;
    }

    // Se chegou aqui, o ficheiro foi aberto com sucesso para leitura
    write(STDOUT_FILENO, "A carregar documentos do disco ('database.bin')...\n", strlen("A carregar documentos do disco ('database.bin')...\n"));

    // Lê o próximo ID disponível (primeiro valor no ficheiro)
    if (read(fd, &next_id, sizeof(int)) != sizeof(int)) {
        // Erro na leitura do next_id
        write(STDERR_FILENO, "Erro ao ler next_id do 'database.bin'. A iniciar com estado vazio.\n", strlen("Erro ao ler next_id do 'database.bin'. A iniciar com estado vazio.\n"));
        next_id = 1; // Reset para o valor padrão
        cache.num_docs = 0;
        close(fd);
        return;
    }

    // Lê a quantidade total de documentos armazenados
    int total_docs;
    if (read(fd, &total_docs, sizeof(int)) != sizeof(int)) {
        // Erro na leitura do total_docs
        write(STDERR_FILENO, "Erro ao ler total_docs do 'database.bin'. A iniciar com estado vazio.\n", strlen("Erro ao ler total_docs do 'database.bin'. A iniciar com estado vazio.\n"));
        next_id = 1; // Reset
        cache.num_docs = 0;
        close(fd);
        return;
    }

    // Mostra informações sobre os documentos encontrados
    char msg[128];
    int len = snprintf(msg, sizeof(msg), "Encontrados %d documentos no disco. Próximo ID: %d\n", total_docs, next_id);
    write(STDOUT_FILENO, msg, len);

    // Lê cada documento e adiciona à cache se houver espaço
    Document doc;
    int loaded_count = 0; // Contador de documentos carregados com sucesso
    for (int i = 0; i < total_docs && cache.num_docs < cache.max_size; i++) {
        if (read(fd, &doc, sizeof(Document)) == sizeof(Document)) {
            // Adiciona à cache: aloca memória e copia
            cache.docs[cache.num_docs] = malloc(sizeof(Document));
            if (cache.docs[cache.num_docs] != NULL) {
                memcpy(cache.docs[cache.num_docs], &doc, sizeof(Document));
                cache.num_docs++;
                loaded_count++;
            } else {
                // Erro de alocação de memória
                write(STDERR_FILENO, "Erro de alocação de memória ao carregar documento da base de dados.\n", strlen("Erro de alocação de memória ao carregar documento da base de dados.\n"));
                break;
            }
        } else {
            // Erro ao ler um registo ou fim inesperado do ficheiro
            write(STDERR_FILENO, "Erro ao ler registo de documento do 'database.bin'. Carregamento parcial.\n", strlen("Erro ao ler registo de documento do 'database.bin'. Carregamento parcial.\n"));
            break;
        }
    }

    // Se ainda há documentos no disco mas a cache está cheia ou houve erro, informa
    if (total_docs > cache.num_docs && total_docs > loaded_count) {
        write(STDOUT_FILENO, "Cache cheia ou erro durante leitura. Alguns documentos do disco podem não ter sido carregados para a memória.\n",
            strlen("Cache cheia ou erro durante leitura. Alguns documentos do disco podem não ter sido carregados para a memória.\n"));
    }

    close(fd); // Fecha o ficheiro após leitura

    // Informa quantos documentos foram carregados
    len = snprintf(msg, sizeof(msg), "%d documentos carregados para a cache\n", cache.num_docs);
    write(STDOUT_FILENO, msg, len);

    // Importante: Os dados carregados ainda não foram modificados
    cache.modified = 0;
}

// Função para tratar sinais do sistema operativo
void handle_signals(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        // SIGINT é enviado quando o utilizador pressiona Ctrl+C
        // SIGTERM é o sinal padrão de terminação enviado por kill
        write(STDOUT_FILENO, "\nRecebido sinal para terminar o servidor\n", strlen("\nRecebido sinal para terminar o servidor\n"));
        
        // NÃO grava os documentos ao receber sinal, conforme requisito do enunciado
        // A gravação só deve ocorrer com o comando -f do cliente (SHUTDOWN)
        
        // Liberta a memória de todos os documentos na cache para evitar fugas de memória
        for (int i = 0; i < cache.num_docs; i++) {
            free(cache.docs[i]);
        }
        
        // Remove o pipe nomeado (FIFO) do servidor para limpeza
        unlink(SERVER_PIPE);
        
        exit(0); // Termina o processo com código de sucesso
    }
}

// Função para processar um pedido recebido do cliente
Response process_request(Request req) {
    // Inicializa a estrutura de resposta com zeros
    Response resp;
    memset(&resp, 0, sizeof(Response));
    
    char msg[256]; // Buffer para mensagens de log
    int len;
    
    // Regista o pedido recebido no stdout do servidor
    switch (req.operation) {
        case ADD_DOC:
            len = snprintf(msg, sizeof(msg), "Recebido pedido ADD_DOC do cliente %d. Título: %s\n", req.client_pid, req.doc.title);
            break;
        case QUERY_DOC:
            len = snprintf(msg, sizeof(msg), "Recebido pedido QUERY_DOC do cliente %d. ID: %d\n", req.client_pid, req.doc.id);
            break;
        case DELETE_DOC:
            len = snprintf(msg, sizeof(msg), "Recebido pedido DELETE_DOC do cliente %d. ID: %d\n", req.client_pid, req.doc.id);
            break;
        case COUNT_LINES:
            len = snprintf(msg, sizeof(msg), "Recebido pedido COUNT_LINES do cliente %d. ID: %d, Palavra-chave: %s\n", req.client_pid, req.doc.id, req.keyword);
            break;
        case SEARCH_DOCS:
            len = snprintf(msg, sizeof(msg), "Recebido pedido SEARCH_DOCS do cliente %d. Palavra-chave: %s, NrProcs: %d\n", req.client_pid, req.keyword, req.nr_processes);
            break;
        case SHUTDOWN:
            len = snprintf(msg, sizeof(msg), "Recebido pedido SHUTDOWN do cliente %d\n", req.client_pid);
            break;
        default:
            len = snprintf(msg, sizeof(msg), "Recebido pedido desconhecido (%d) do cliente %d\n", req.operation, req.client_pid);
    }
    write(STDOUT_FILENO, msg, len);
    
    // Processa o pedido conforme a operação solicitada
    switch (req.operation) {
        case ADD_DOC:
            // Adiciona um documento à cache
            resp.doc.id = add_document(&req.doc);
            resp.status = 0; // Indica sucesso
            break;
            
        case QUERY_DOC:
            {
                // Procura um documento pelo ID
                Document* doc = find_document(req.doc.id);
                if (doc) {
                    // Se encontrou, copia para a resposta
                    memcpy(&resp.doc, doc, sizeof(Document));
                    resp.status = 0; // Sucesso
                } else {
                    resp.status = -1; // Documento não encontrado
                }
            }
            break;
            
        case DELETE_DOC:
            // Remove um documento
            resp.status = remove_document(req.doc.id);
            break;
            
        case COUNT_LINES:
            {
                // Conta linhas com uma palavra-chave
                Document* doc = find_document(req.doc.id);
                if (doc) {
                    // Se o documento existe, conta as linhas
                    resp.count = count_lines_with_keyword(doc, req.keyword);
                    resp.status = 0; // Sucesso
                } else {
                    resp.status = -1; // Documento não encontrado
                }
            }
            break;
            
        case SEARCH_DOCS:
            // Procura documentos com uma palavra-chave
            if (req.nr_processes > 1) {
                // Versão paralela (com múltiplos processos)
                resp.num_ids = search_documents_with_keyword_parallel(req.keyword, resp.ids, req.nr_processes);
            } else {
                // Versão série (com um único processo)
                resp.num_ids = search_documents_with_keyword_serial(req.keyword, resp.ids);
            }
            resp.status = 0; // Sucesso
            break;
            
        case SHUTDOWN:
            // Este é o ÚNICO ponto onde devemos gravar os documentos
            // conforme requisito do enunciado (persistência apenas com -f)
            if (cache.modified) {
                write(STDOUT_FILENO, "Comando SHUTDOWN recebido. A gravar base de dados...\n", strlen("Comando SHUTDOWN recebido. A gravar base de dados...\n"));
                save_documents(); // Grava os documentos para o disco
            } else {
                write(STDOUT_FILENO, "Comando SHUTDOWN recebido. Nenhuma alteração a gravar.\n", strlen("Comando SHUTDOWN recebido. Nenhuma alteração a gravar.\n"));
            }
            resp.status = 0; // Sucesso
            break;
            
        default:
            // Operação desconhecida ou não suportada
            resp.status = -2; // Código de erro para operação inválida
    }
    
    // Regista a resposta que será enviada
    switch (req.operation) {
        case ADD_DOC:
            len = snprintf(msg, sizeof(msg), "A enviar resposta ADD_DOC para cliente %d. ID atribuído: %d\n", req.client_pid, resp.doc.id);
            break;
        case QUERY_DOC:
            if (resp.status == 0) {
                len = snprintf(msg, sizeof(msg), "A enviar resposta QUERY_DOC para cliente %d. Documento encontrado.\n", req.client_pid);
            } else {
                len = snprintf(msg, sizeof(msg), "A enviar resposta QUERY_DOC para cliente %d. Documento não encontrado.\n", req.client_pid);
            }
            break;
        case DELETE_DOC:
            len = snprintf(msg, sizeof(msg), "A enviar resposta DELETE_DOC para cliente %d. Estado: %d\n", req.client_pid, resp.status);
            break;
        case COUNT_LINES:
            if (resp.status == 0) {
                len = snprintf(msg, sizeof(msg), "A enviar resposta COUNT_LINES para cliente %d. Contagem: %d\n", req.client_pid, resp.count);
            } else {
                len = snprintf(msg, sizeof(msg), "A enviar resposta COUNT_LINES para cliente %d. Documento não encontrado.\n", req.client_pid);
            }
            break;
        case SEARCH_DOCS:
            len = snprintf(msg, sizeof(msg), "A enviar resposta SEARCH_DOCS para cliente %d. Encontrados: %d documentos\n", req.client_pid, resp.num_ids);
            break;
        case SHUTDOWN:
            len = snprintf(msg, sizeof(msg), "A enviar resposta SHUTDOWN para cliente %d\n", req.client_pid);
            break;
        default:
            len = snprintf(msg, sizeof(msg), "A enviar resposta de operação inválida para cliente %d\n", req.client_pid);
    }
    write(STDOUT_FILENO, msg, len);
    
    return resp; // Retorna a resposta para ser enviada ao cliente
}

int main(int argc, char* argv[]) {
    // Verifica se foram fornecidos argumentos suficientes
    if (argc < 2) {
        write(STDERR_FILENO, "Uso: ./dserver pasta_documentos [tamanho_cache]\n", strlen("Uso: ./dserver pasta_documentos [tamanho_cache]\n"));
        return 1;
    }
    
    // Armazena a pasta base dos documentos (argumento obrigatório)
    strcpy(base_folder, argv[1]);
    
        // Configura o tamanho da cache
        int requested_cache_size = (argc > 2) ? atoi(argv[2]) : 100; // Padrão 100

        // Adicionar verificação e aviso
        if (requested_cache_size > MAX_DOCS) {
            char warning_msg[256];
            snprintf(warning_msg, sizeof(warning_msg),
                    "Aviso: Tamanho da cache pedido (%d) excede o máximo de documentos (%d). A usar %d.\n",
                    requested_cache_size, MAX_DOCS, MAX_DOCS);
            write(STDOUT_FILENO, warning_msg, strlen(warning_msg));
            cache.max_size = MAX_DOCS; // Limita ao máximo definido
        } else if (requested_cache_size <= 0) {
            write(STDOUT_FILENO, "Aviso: Tamanho da cache inválido. A usar tamanho padrão 100.\n", strlen("Aviso: Tamanho da cache inválido. A usar tamanho padrão 100.\n"));
            cache.max_size = 100; // Usa o padrão se for inválido
        }
        else {
            cache.max_size = requested_cache_size; // Usa o tamanho pedido
        }

    cache.num_docs = 0; // Inicialmente sem documentos
    cache.modified = 0; // Cache não modificada no início
    
    // Configura os handlers de sinais para terminação graciosa
    signal(SIGINT, handle_signals);  // Ctrl+C
    signal(SIGTERM, handle_signals); // kill
    
    write(STDOUT_FILENO, "A iniciar servidor...\n", strlen("A iniciar servidor...\n"));
    
    // Carrega os documentos do disco para a cache
    load_documents();
    
    // Cria o pipe nomeado (FIFO) do servidor para comunicação com os clientes
    unlink(SERVER_PIPE); // Remove o pipe se já existir
    write(STDOUT_FILENO, "A tentar criar FIFO do servidor em: " SERVER_PIPE "\n", strlen("A tentar criar FIFO do servidor em: " SERVER_PIPE "\n"));
    
    if (mkfifo(SERVER_PIPE, 0666) < 0) {
        // 0666 dá permissões de leitura e escrita para todos os utilizadores
        char error_msg[256];
        int len = snprintf(error_msg, sizeof(error_msg), "Erro ao criar pipe do servidor: %s\n", strerror(errno));
        write(STDERR_FILENO, error_msg, len);
        return 1;
    }
    
    write(STDOUT_FILENO, "FIFO do servidor criado com sucesso\n", strlen("FIFO do servidor criado com sucesso\n"));
    
    // Mostra informações de inicialização
    char msg[256];
    int len = snprintf(msg, sizeof(msg), "Servidor iniciado. Pasta de documentos: %s. Tamanho da cache: %d\n", base_folder, cache.max_size);
    write(STDOUT_FILENO, msg, len);
    
    // Abre o FIFO para leitura (isto bloqueia até que um cliente abra o FIFO para escrita)
    write(STDOUT_FILENO, "A abrir FIFO do servidor para leitura...\n", strlen("A abrir FIFO do servidor para leitura...\n"));
    int server_fd = open(SERVER_PIPE, O_RDONLY);
    if (server_fd < 0) {
        char error_msg[256];
        int len = snprintf(error_msg, sizeof(error_msg), "Erro ao abrir pipe do servidor para leitura: %s\n", strerror(errno));
        write(STDERR_FILENO, error_msg, len);
        return 1;
    }
    
    write(STDOUT_FILENO, "FIFO do servidor aberto com sucesso\n", strlen("FIFO do servidor aberto com sucesso\n"));
    write(STDOUT_FILENO, "A aguardar ligações...\n", strlen("A aguardar ligações...\n"));
    
    int running = 1; // Flag para controlar o loop principal
    
    // Loop principal do servidor
    while (running) {
        Request req; // Estrutura para guardar o pedido do cliente
        Response resp; // Estrutura para a resposta ao cliente
        
        // Lê um pedido do cliente através do FIFO
        write(STDOUT_FILENO, "A aguardar pedido...\n", strlen("A aguardar pedido...\n"));
        ssize_t bytes_read = read(server_fd, &req, sizeof(Request));
        
        if (bytes_read <= 0) {
            // Trata erros de leitura ou EOF (cliente fechou a escrita do pipe)
            char error_msg[256];
            int len = snprintf(error_msg, sizeof(error_msg), "Erro na leitura do pipe do servidor: %s. A reabrir...\n", bytes_read == 0 ? "EOF" : strerror(errno));
            write(STDERR_FILENO, error_msg, len);
            
            // Reabre o pipe se foi fechado (acontece quando todos os escritores fecham o pipe)
            close(server_fd);
            server_fd = open(SERVER_PIPE, O_RDONLY);
            if (server_fd < 0) {
                len = snprintf(error_msg, sizeof(error_msg), "Erro ao reabrir pipe do servidor: %s\n", strerror(errno));
                write(STDERR_FILENO, error_msg, len);
            } else {
                write(STDOUT_FILENO, "Pipe do servidor reaberto com sucesso\n", strlen("Pipe do servidor reaberto com sucesso\n"));
            }
            continue; // Volta ao início do loop
        }
        
        // Regista o pedido recebido
        len = snprintf(msg, sizeof(msg), "Recebido pedido de %d bytes do cliente %d\n", (int)bytes_read, req.client_pid);
        write(STDOUT_FILENO, msg, len);
        
        // Processa o pedido e obtém a resposta
        resp = process_request(req);
        
        // Prepara o nome do pipe do cliente para enviar a resposta
        char client_pipe[64];
        sprintf(client_pipe, CLIENT_PIPE_FORMAT, req.client_pid);
        
        // Tenta abrir o pipe do cliente para escrita
        len = snprintf(msg, sizeof(msg), "A tentar abrir FIFO do cliente em: %s\n", client_pipe);
        write(STDOUT_FILENO, msg, len);
        
        int client_fd = open(client_pipe, O_WRONLY);
        if (client_fd < 0) {
            // Se não conseguir abrir o pipe do cliente, regista o erro e continua
            char error_msg[256];
            int len = snprintf(error_msg, sizeof(error_msg), "Erro ao abrir pipe do cliente para escrita: %s\n", strerror(errno));
            write(STDERR_FILENO, error_msg, len);
            continue; // Volta ao início do loop
        }
        
        write(STDOUT_FILENO, "FIFO do cliente aberto com sucesso\n", strlen("FIFO do cliente aberto com sucesso\n"));
        
        // Envia a resposta para o cliente
        ssize_t bytes_written = write(client_fd, &resp, sizeof(Response));
        
        if (bytes_written != sizeof(Response)) {
            // Se ocorrer erro na escrita, regista o problema
            char error_msg[256];
            int len = snprintf(error_msg, sizeof(error_msg), "Erro ao escrever no pipe do cliente: %s\n", strerror(errno));
            write(STDERR_FILENO, error_msg, len);
        } else {
            // Escrita bem sucedida
            len = snprintf(msg, sizeof(msg), "Enviados %d bytes para o cliente %d\n", (int)bytes_written, req.client_pid);
            write(STDOUT_FILENO, msg, len);
        }
        
        // Fecha o pipe do cliente (a conexão é única para cada pedido)
        close(client_fd);
        write(STDOUT_FILENO, "FIFO do cliente fechado\n", strlen("FIFO do cliente fechado\n"));
        
        // Se foi um pedido de terminação (SHUTDOWN), sai do loop
        if (req.operation == SHUTDOWN) {
            running = 0;
            write(STDOUT_FILENO, "Recebido comando para terminar o servidor\n", strlen("Recebido comando para terminar o servidor\n"));
        }
    }
    
    // Limpeza final: fecha e remove o FIFO do servidor
    close(server_fd);
    unlink(SERVER_PIPE);
    
    write(STDOUT_FILENO, "FIFO do servidor fechado e removido\n", strlen("FIFO do servidor fechado e removido\n"));
    
    // NÃO grava os documentos antes de terminar normalmente
    // Seguindo o requisito de que a gravação só ocorre quando explicitamente solicitada pelo cliente (-f)
    // (Se o pedido foi SHUTDOWN, a gravação já foi feita durante o processamento do pedido)
    
    // Liberta a memória dos documentos na cache para evitar fugas de memória
    for (int i = 0; i < cache.num_docs; i++) {
        free(cache.docs[i]);
    }
    
    write(STDOUT_FILENO, "Memória da cache libertada\n", strlen("Memória da cache libertada\n"));
    write(STDOUT_FILENO, "Servidor terminado com sucesso\n", strlen("Servidor terminado com sucesso\n"));
    
    return 0; // Indica término bem-sucedido
}