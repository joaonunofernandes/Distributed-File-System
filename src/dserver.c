#include "Document_Struct.h"

// Estrutura para armazenar os documentos em memória (cache).
typedef struct {
    Document* docs[MAX_DOCS]; // Array de ponteiros para documentos armazenados na cache.
    int num_docs;             // Contador de documentos atualmente na cache.
    int max_size;             // Tamanho máximo da cache (número máximo de documentos permitidos).
    int modified;             // Flag para indicar se houve modificações desde a última gravação em disco.
} Cache;

// Estrutura para representar uma tarefa de pesquisa.
typedef struct {
    int id;                   // ID do documento a pesquisar.
    char path[MAX_PATH_SIZE]; // Caminho para o ficheiro do documento.
} SearchTask;

// Variáveis globais.
Cache cache;                // Instância da cache que mantém os documentos em memória.
char base_folder[256];      // Pasta base onde os ficheiros de documentos estão armazenados.
int next_id = 1;            // Contador global para atribuição de IDs únicos aos documentos.

// Protótipos das funções.
int add_document(Document* doc);
Document* find_document(int id);
int remove_document(int id);
int count_lines_with_keyword(Document* doc, const char* keyword);
int search_documents_with_keyword_parallel(char* keyword, int* result_ids, int nr_processes);
int search_documents_with_keyword_serial(char* keyword, int* result_ids);
void save_documents();
void load_documents();
void handle_signals(int sig);
void process_search_tasks_child(const SearchTask* tasks_chunk, int num_tasks_in_chunk, const char* keyword, const char* temp_file_path);
Response process_request(Request req);

/**
 * @brief Adiciona um documento à cache e, se a cache estiver cheia, remove o mais antigo (FCFS).
 *
 * Aloca memória para o novo documento, copia os dados, atribui um ID único
 * e atualiza o estado da cache.
 *
 * @param doc Ponteiro para a estrutura Document com os dados do documento a adicionar.
 * @return O ID atribuído ao documento adicionado, ou -1 em caso de erro de alocação.
 */
int add_document(Document* doc) {
    // Verifica se a cache está cheia.
    if (cache.num_docs >= cache.max_size) {
        // Política FCFS: remove o documento mais antigo (índice 0).
        char msg[256];
        int len = snprintf(msg, sizeof(msg),
                        "Cache cheia: a aplicar política FCFS para remover documento ID: %d, Título: '%s' para incluir novo documento Título: '%s'\n",
                        cache.docs[0]->id,
                        cache.docs[0]->title,
                        doc->title);
        write(STDOUT_FILENO, msg, len);

        free(cache.docs[0]); // Liberta a memória do documento removido.

        // Desloca os restantes documentos para a esquerda.
        for (int i = 0; i < cache.num_docs - 1; i++) {
            cache.docs[i] = cache.docs[i + 1];
        }
        cache.num_docs--;
    }

    // Aloca memória para o novo documento.
    Document* new_doc = malloc(sizeof(Document));
    if (!new_doc) {
        perror("Erro ao alocar memória para novo documento");
        return -1; // Falha na alocação.
    }

    memcpy(new_doc, doc, sizeof(Document)); // Copia os dados do documento.
    new_doc->id = next_id++; // Atribui um ID único e incrementa o contador global.
    cache.docs[cache.num_docs++] = new_doc; // Adiciona o novo documento à cache.
    cache.modified = 1; // Marca a cache como modificada.

    return new_doc->id; // Retorna o ID do documento adicionado.
}

/**
 * @brief Procura um documento pelo seu ID, primeiro na cache e depois no ficheiro de persistência.
 *
 * Se encontrado no disco e não na cache (e houver espaço), adiciona-o à cache.
 *
 * @param id O ID do documento a procurar.
 * @return Ponteiro para o Documento encontrado (na cache ou uma cópia temporária do disco),
 * ou NULL se não for encontrado.
 */
Document* find_document(int id) {
    // Procura na cache.
    for (int i = 0; i < cache.num_docs; i++) {
        if (cache.docs[i]->id == id) {
            return cache.docs[i]; // Encontrado na cache.
        }
    }

    // Procura no ficheiro de persistência "database.bin".
    int fd = open("database.bin", O_RDONLY);
    if (fd < 0) {
        return NULL; // Ficheiro não existe ou erro ao abrir.
    }

    // Salta o cabeçalho do ficheiro (next_id e num_docs).
    lseek(fd, 2 * sizeof(int), SEEK_SET);

    Document disk_doc;
    // Lê documentos do ficheiro.
    while (read(fd, &disk_doc, sizeof(Document)) == sizeof(Document)) {
        if (disk_doc.id == id) {
            close(fd);
            // Documento encontrado no disco.
            if (cache.num_docs < cache.max_size) {
                // Adiciona à cache se houver espaço.
                Document* doc_to_cache = malloc(sizeof(Document));
                if (!doc_to_cache) { // Verifica falha na alocação
                    perror("Erro ao alocar memória para colocar documento do disco na cache");
                    // Retorna uma cópia temporária se a alocação para cache falhar, para não perder o documento encontrado
                    // O chamador DEVE libertar esta memória.
                    Document* temp_doc = malloc(sizeof(Document));
                    if(!temp_doc) return NULL; // Não conseguiu alocar nem para a cópia temporária
                    memcpy(temp_doc, &disk_doc, sizeof(Document));
                    return temp_doc;
                }
                memcpy(doc_to_cache, &disk_doc, sizeof(Document));
                cache.docs[cache.num_docs++] = doc_to_cache;
                return doc_to_cache; // Retorna o documento agora na cache.
            } else {
                // Cache cheia, retorna uma cópia temporária.
                // O chamador DEVE libertar esta memória.
                Document* temp_doc = malloc(sizeof(Document));
                if (!temp_doc) {
                    perror("Erro ao alocar memória para cópia temporária do documento do disco");
                    return NULL;
                }
                memcpy(temp_doc, &disk_doc, sizeof(Document));
                return temp_doc;
            }
        }
    }

    close(fd);
    return NULL; // Documento não encontrado.
}

/**
 * @brief Remove um documento da cache e do ficheiro de persistência "database.bin".
 *
 * Após a remoção do ficheiro, a cache é limpa e recarregada para manter a consistência.
 *
 * @param id O ID do documento a remover.
 * @return 0 em caso de sucesso, -1 se o documento não for encontrado ou ocorrer um erro.
 */
int remove_document(int id) {
    int document_found_in_cache = 0;

    // Remove da cache.
    for (int i = 0; i < cache.num_docs; i++) {
        if (cache.docs[i]->id == id) {
            free(cache.docs[i]);
            for (int j = i; j < cache.num_docs - 1; j++) {
                cache.docs[j] = cache.docs[j + 1];
            }
            cache.num_docs--;
            cache.modified = 1;
            document_found_in_cache = 1;
            break;
        }
    }

    // Remove do ficheiro "database.bin".
    int fd = open("database.bin", O_RDWR);
    if (fd < 0) {
        // Se removeu da cache mas não conseguiu abrir o disco, consideramos sucesso parcial.
        // A persistência tentará corrigir isto mais tarde ou na próxima carga.
        return document_found_in_cache ? 0 : -1;
    }

    int next_id_disk, num_docs_disk;
    if (read(fd, &next_id_disk, sizeof(int)) != sizeof(int) ||
        read(fd, &num_docs_disk, sizeof(int)) != sizeof(int)) {
        close(fd);
        return document_found_in_cache ? 0 : -1; // Erro ao ler cabeçalho.
    }

    Document docs_on_disk[MAX_DOCS]; // Buffer para documentos válidos.
    int valid_docs_count = 0;
    int found_on_disk = 0;

    // Lê todos os documentos do disco.
    for (int i = 0; i < num_docs_disk; i++) {
        Document d;
        if (read(fd, &d, sizeof(Document)) != sizeof(Document)) break; // Erro ou fim de ficheiro.
        if (d.id != id) {
            if (valid_docs_count < MAX_DOCS) { // Previne overflow do buffer
                docs_on_disk[valid_docs_count++] = d;
            }
        } else {
            found_on_disk = 1;
        }
    }

    // Se o documento foi encontrado no disco, reescreve o ficheiro.
    if (found_on_disk) {
        lseek(fd, 0, SEEK_SET); // Volta ao início do ficheiro.
        write(fd, &next_id_disk, sizeof(int));
        write(fd, &valid_docs_count, sizeof(int)); // Novo número de documentos.
        for (int i = 0; i < valid_docs_count; i++) {
            write(fd, &docs_on_disk[i], sizeof(Document));
        }
        // Trunca o ficheiro para o novo tamanho.
        ftruncate(fd, 2 * sizeof(int) + valid_docs_count * sizeof(Document));
        cache.modified = 1; // Marca como modificado pois o disco mudou.
    }
    close(fd);

    if (document_found_in_cache || found_on_disk) {
        // Recarrega a cache para consistência se algo foi alterado.
        // Primeiro, limpa a cache atual.
        for (int i = 0; i < cache.num_docs; i++) {
            free(cache.docs[i]);
            cache.docs[i] = NULL;
        }
        cache.num_docs = 0;
        // A flag 'modified' será tratada pela load_documents ou pela próxima save.
        load_documents(); // Recarrega do disco.
        return 0; // Sucesso.
    }

    return -1; // Documento não encontrado em lado nenhum.
}

/**
 * @brief Conta o número de linhas num ficheiro de documento que contêm uma determinada palavra-chave.
 *
 * Utiliza os comandos 'grep' e 'wc -l' através de pipes e processos filho.
 *
 * @param doc Ponteiro para o Documento cujo ficheiro será analisado.
 * @param keyword A palavra-chave a procurar.
 * @return O número de linhas encontradas, ou -1 em caso de erro.
 */
int count_lines_with_keyword(Document* doc, const char* keyword) {
    if (!doc || !keyword) return -1;

    char full_path[MAX_PATH_SIZE + sizeof(base_folder) + 2]; // +2 para '/' e '\0'.
    snprintf(full_path, sizeof(full_path), "%s/%s", base_folder, doc->path);

    int pipe_grep_wc[2];
    int pipe_wc_parent[2];
    pid_t pid_grep, pid_wc;
    int line_count = -1;

    if (pipe(pipe_grep_wc) < 0 || pipe(pipe_wc_parent) < 0) {
        perror("Erro ao criar pipes para contagem de linhas");
        if (pipe_grep_wc[0] >= 0) { close(pipe_grep_wc[0]); close(pipe_grep_wc[1]); }
        if (pipe_wc_parent[0] >= 0) { close(pipe_wc_parent[0]); close(pipe_wc_parent[1]); }
        return -1;
    }

    // Fork para 'grep'.
    pid_grep = fork();
    if (pid_grep == 0) { // Processo filho (grep).
        close(pipe_grep_wc[0]); // Fecha leitura do pipe grep->wc.
        dup2(pipe_grep_wc[1], STDOUT_FILENO); // Redireciona stdout para escrita no pipe grep->wc.
        close(pipe_grep_wc[1]); // Fecha escrita do pipe grep->wc.

        close(pipe_wc_parent[0]); // Fecha extremidades não usadas do pipe wc->parent.
        close(pipe_wc_parent[1]);

        execlp("grep", "grep", keyword, full_path, (char*)NULL);
        perror("Erro ao executar grep");
        _exit(1); // Termina com erro se execlp falhar.
    } else if (pid_grep < 0) {
        perror("Erro no fork para grep");
        close(pipe_grep_wc[0]); close(pipe_grep_wc[1]);
        close(pipe_wc_parent[0]); close(pipe_wc_parent[1]);
        return -1;
    }

    // Fork para 'wc'.
    pid_wc = fork();
    if (pid_wc == 0) { // Processo filho (wc).
        close(pipe_grep_wc[1]); // Fecha escrita do pipe grep->wc.
        dup2(pipe_grep_wc[0], STDIN_FILENO); // Redireciona stdin para leitura do pipe grep->wc.
        close(pipe_grep_wc[0]); // Fecha leitura do pipe grep->wc.

        close(pipe_wc_parent[0]); // Fecha leitura do pipe wc->parent.
        dup2(pipe_wc_parent[1], STDOUT_FILENO); // Redireciona stdout para escrita no pipe wc->parent.
        close(pipe_wc_parent[1]);

        execlp("wc", "wc", "-l", (char*)NULL);
        perror("Erro ao executar wc");
        _exit(1); // Termina com erro se execlp falhar.
    } else if (pid_wc < 0) {
        perror("Erro no fork para wc");
        close(pipe_grep_wc[0]); close(pipe_grep_wc[1]);
        close(pipe_wc_parent[0]); close(pipe_wc_parent[1]);
        waitpid(pid_grep, NULL, 0); // Limpa processo grep.
        return -1;
    }

    // Processo pai.
    close(pipe_grep_wc[0]); // Fecha ambas as extremidades do pipe grep->wc.
    close(pipe_grep_wc[1]);
    close(pipe_wc_parent[1]); // Fecha escrita do pipe wc->parent.

    char buffer[32];
    ssize_t bytes_read = read(pipe_wc_parent[0], buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        line_count = atoi(buffer);
    } else if (bytes_read == 0) { // EOF, pode significar 0 linhas.
        line_count = 0;
    } else {
        perror("Erro ao ler do pipe wc->parent");
        line_count = -1; // Erro na leitura.
    }

    close(pipe_wc_parent[0]); // Fecha leitura do pipe wc->parent.

    // Espera pelos processos filho.
    waitpid(pid_grep, NULL, 0);
    waitpid(pid_wc, NULL, 0);

    return line_count;
}

/**
 * @brief Procura documentos que contêm uma palavra-chave, de forma sequencial.
 *
 * Itera sobre os documentos na cache e depois no ficheiro de persistência,
 * utilizando 'grep -q' para verificar a presença da palavra-chave.
 *
 * @param keyword A palavra-chave a procurar.
 * @param result_ids Array de inteiros onde os IDs dos documentos encontrados serão armazenados.
 * @return O número de IDs de documentos encontrados e adicionados a result_ids.
 */
int search_documents_with_keyword_serial(char* keyword, int* result_ids) {
    int count = 0;

    // Procura na cache.
    for (int i = 0; i < cache.num_docs && count < MAX_RESULT_IDS; i++) {
        if (count_lines_with_keyword(cache.docs[i], keyword) > 0) {
            result_ids[count++] = cache.docs[i]->id;
        }
    }

    // Procura no ficheiro "database.bin" (documentos não presentes na cache).
    int fd = open("database.bin", O_RDONLY);
    if (fd < 0) {
        return count; // Retorna o que foi encontrado na cache se o disco não puder ser lido.
    }

    int next_id_disk, num_docs_disk;
    read(fd, &next_id_disk, sizeof(int));
    read(fd, &num_docs_disk, sizeof(int));

    Document disk_doc;
    while (read(fd, &disk_doc, sizeof(Document)) == sizeof(Document) && count < MAX_RESULT_IDS) {
        int in_cache = 0;
        // Verifica se o documento do disco já foi processado (estava na cache).
        for (int i = 0; i < count; i++) { // Compara com os já adicionados a result_ids (vindos da cache).
            if (result_ids[i] == disk_doc.id) {
                in_cache = 1;
                break;
            }
        }
        // Adicionalmente, verifica explicitamente contra a cache atual,
        // pois `count_lines_with_keyword` acima já adicionou os da cache.
        // Esta segunda verificação é para o caso de o ID estar na cache mas não ter a keyword.
        if (!in_cache) {
            for (int i = 0; i < cache.num_docs; i++) {
                if (cache.docs[i]->id == disk_doc.id) {
                    in_cache = 1;
                    break;
                }
            }
        }


        if (!in_cache) {
            char full_path[MAX_PATH_SIZE + sizeof(base_folder) + 2];
            snprintf(full_path, sizeof(full_path), "%s/%s", base_folder, disk_doc.path);

            // Usa grep -q para verificar a existência da keyword.
            int pipefd[2];
            if (pipe(pipefd) == -1) {
                perror("Erro ao criar pipe para grep -q na pesquisa sequencial");
                continue;
            }

            pid_t pid = fork();
            if (pid == 0) { // Filho.
                close(pipefd[0]); // Não usado pelo filho.
                // Não precisamos do output do grep, apenas do status de saída.
                // Redirecionar stdout e stderr para /dev/null se quisermos silenciar o grep.
                int dev_null_fd = open("/dev/null", O_WRONLY);
                if (dev_null_fd != -1) {
                    dup2(dev_null_fd, STDOUT_FILENO);
                    dup2(dev_null_fd, STDERR_FILENO);
                    close(dev_null_fd);
                }
                execlp("grep", "grep", "-q", keyword, full_path, (char*)NULL);
                _exit(127); // Se execlp falhar.
            } else if (pid > 0) { // Pai.
                close(pipefd[1]); // Não usado pelo pai.
                close(pipefd[0]); // Não precisamos ler nada.

                int status;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    // Grep encontrou a keyword (status 0).
                    result_ids[count++] = disk_doc.id;
                }
            } else { // Erro no fork.
                perror("Erro no fork para grep -q na pesquisa sequencial");
                close(pipefd[0]);
                close(pipefd[1]);
            }
        }
    }
    close(fd);
    return count;
}


/**
 * @brief Função executada por cada processo filho na pesquisa paralela.
 *
 * Processa um subconjunto de tarefas de pesquisa (documentos), verifica a presença
 * da palavra-chave usando `count_lines_with_keyword` e escreve os IDs encontrados
 * num ficheiro temporário.
 *
 * @param tasks_chunk Ponteiro para o array de SearchTask (subconjunto de tarefas).
 * @param num_tasks_in_chunk Número de tarefas no subconjunto.
 * @param keyword A palavra-chave a procurar.
 * @param temp_file_path Caminho para o ficheiro temporário onde os resultados do filho serão escritos.
 */
void process_search_tasks_child(const SearchTask* tasks_chunk, int num_tasks_in_chunk, const char* keyword, const char* temp_file_path) {
    char child_debug_msg[256];
    int len = snprintf(child_debug_msg, sizeof(child_debug_msg),
                    "DEBUG: Filho PID %d iniciado para processar %d tarefas. Ficheiro temp: %s\n",
                    getpid(), num_tasks_in_chunk, temp_file_path);
    write(STDOUT_FILENO, child_debug_msg, len);

    int found_ids[MAX_RESULT_IDS];
    int child_count = 0;

    for (int i = 0; i < num_tasks_in_chunk; i++) {
        const SearchTask* current_task = &tasks_chunk[i];
        Document temp_doc_for_count; // Documento temporário.

        temp_doc_for_count.id = current_task->id;
        strncpy(temp_doc_for_count.path, current_task->path, MAX_PATH_SIZE -1);
        temp_doc_for_count.path[MAX_PATH_SIZE - 1] = '\0';

        if (count_lines_with_keyword(&temp_doc_for_count, keyword) > 0) {
            if (child_count < MAX_RESULT_IDS) {
                found_ids[child_count++] = current_task->id;
            } else {
                // Limite de resultados do filho atingido.
                break;
            }
        }
    }

    // Escreve os resultados no ficheiro temporário.
    int temp_fd = open(temp_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (temp_fd < 0) {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "DEBUG: Filho PID %d ERRO ao abrir ficheiro temp %s: %s\n", getpid(), temp_file_path, strerror(errno));
        write(STDERR_FILENO, error_msg, strlen(error_msg));
        exit(1); // Termina com erro.
    }

    write(temp_fd, &child_count, sizeof(int)); // Escreve número de IDs.
    if (child_count > 0) {
        write(temp_fd, found_ids, child_count * sizeof(int)); // Escreve os IDs.
    }
    close(temp_fd);

    len = snprintf(child_debug_msg, sizeof(child_debug_msg),
                    "DEBUG: Filho PID %d terminou. Encontrou %d IDs. Escritos em %s\n",
                    getpid(), child_count, temp_file_path);
    write(STDOUT_FILENO, child_debug_msg, len);
    exit(0); // Filho termina com sucesso.
}

/**
 * @brief Procura documentos que contêm uma palavra-chave, de forma paralela.
 *
 * Distribui a pesquisa por vários processos filho. Cada filho processa um subconjunto
 * de documentos (da cache e do disco) e escreve os seus resultados num ficheiro temporário.
 * O processo pai depois agrega os resultados. Se o número de processos pedidos for <= 1
 * ou o número de tarefas for baixo, recorre à pesquisa sequencial.
 *
 * @param keyword A palavra-chave a procurar.
 * @param result_ids Array de inteiros onde os IDs dos documentos encontrados serão armazenados.
 * @param nr_processes_requested O número de processos filho a utilizar para a pesquisa.
 * @return O número total de IDs de documentos encontrados.
 */
int search_documents_with_keyword_parallel(char* keyword, int* result_ids, int nr_processes_requested) {
    char debug_msg[256];
    int len;

    len = snprintf(debug_msg, sizeof(debug_msg),
                    "DEBUG: Iniciando pesquisa paralela com keyword '%s' e %d processos pedidos.\n",
                    keyword, nr_processes_requested);
    write(STDOUT_FILENO, debug_msg, len);

    SearchTask all_tasks[MAX_DOCS * 2]; // Array para todas as tarefas de pesquisa (cache + disco).
    int num_total_tasks = 0;
    int processed_ids_for_task_list[MAX_DOCS * 2]; // Para evitar duplicados ao construir all_tasks.
    int num_processed_ids_for_task_list = 0;

    // Adiciona tarefas da CACHE.
    for (int i = 0; i < cache.num_docs; i++) {
        if (cache.docs[i] != NULL && num_total_tasks < MAX_DOCS * 2) {
            all_tasks[num_total_tasks].id = cache.docs[i]->id;
            strncpy(all_tasks[num_total_tasks].path, cache.docs[i]->path, MAX_PATH_SIZE -1);
            all_tasks[num_total_tasks].path[MAX_PATH_SIZE -1] = '\0';
            num_total_tasks++;

            if(num_processed_ids_for_task_list < MAX_DOCS * 2)
                processed_ids_for_task_list[num_processed_ids_for_task_list++] = cache.docs[i]->id;
        }
    }

    // Adiciona tarefas do DISCO (apenas as que não estão na cache).
    int fd_disk = open("database.bin", O_RDONLY);
    if (fd_disk >= 0) {
        int next_id_disk_header, num_docs_in_db_header;
        if (read(fd_disk, &next_id_disk_header, sizeof(int)) == sizeof(int) &&
            read(fd_disk, &num_docs_in_db_header, sizeof(int)) == sizeof(int)) {
            Document disk_doc;
            for (int i = 0; i < num_docs_in_db_header && num_total_tasks < MAX_DOCS * 2; ++i) {
                if (read(fd_disk, &disk_doc, sizeof(Document)) != sizeof(Document)) break;

                int already_in_tasks = 0;
                for (int k = 0; k < num_processed_ids_for_task_list; k++) {
                    if (disk_doc.id == processed_ids_for_task_list[k]) {
                        already_in_tasks = 1;
                        break;
                    }
                }
                if (!already_in_tasks) {
                    all_tasks[num_total_tasks].id = disk_doc.id;
                    strncpy(all_tasks[num_total_tasks].path, disk_doc.path, MAX_PATH_SIZE-1);
                    all_tasks[num_total_tasks].path[MAX_PATH_SIZE-1] = '\0';
                    num_total_tasks++;
                     if(num_processed_ids_for_task_list < MAX_DOCS * 2) // Adiciona ao tracker
                        processed_ids_for_task_list[num_processed_ids_for_task_list++] = disk_doc.id;
                }
            }
        }
        close(fd_disk);
    } else {
        write(STDOUT_FILENO, "DEBUG: Base de dados 'database.bin' não encontrada na pesquisa paralela. A pesquisa prosseguirá com a cache.\n",
            strlen("DEBUG: Base de dados 'database.bin' não encontrada na pesquisa paralela. A pesquisa prosseguirá com a cache.\n"));
    }


    if (num_total_tasks == 0) {
        write(STDOUT_FILENO, "DEBUG: Nenhuma tarefa de pesquisa para processar.\n", strlen("DEBUG: Nenhuma tarefa de pesquisa para processar.\n"));
        return 0;
    }

    int actual_nr_processes = nr_processes_requested;
    if (actual_nr_processes <= 0) actual_nr_processes = 1;
    if (actual_nr_processes > num_total_tasks) actual_nr_processes = num_total_tasks;
    if (actual_nr_processes > 20) actual_nr_processes = 20; // Limite de segurança de processos.

    // Se poucos processos ou tarefas, usar versão sequencial.
    const int SERIAL_THRESHOLD_TASKS = 10; // Limiar de tarefas.
    if (actual_nr_processes <= 1 || num_total_tasks <= SERIAL_THRESHOLD_TASKS) {
        len = snprintf(debug_msg, sizeof(debug_msg),
                        "DEBUG: A usar versão sequencial para pesquisa. Tarefas: %d, Processos: %d.\n",
                        num_total_tasks, actual_nr_processes);
        write(STDOUT_FILENO, debug_msg, len);
        // A função sequencial já lida com cache e disco.
        return search_documents_with_keyword_serial(keyword, result_ids);
    }

    len = snprintf(debug_msg, sizeof(debug_msg),
                    "DEBUG: A usar pesquisa paralela com %d processos para %d tarefas totais.\n",
                    actual_nr_processes, num_total_tasks);
    write(STDOUT_FILENO, debug_msg, len);

    pid_t pids[actual_nr_processes];
    char temp_files[actual_nr_processes][64];
    int tasks_per_process = num_total_tasks / actual_nr_processes;
    int remainder_tasks = num_total_tasks % actual_nr_processes;
    int current_task_index = 0;

    // Cria processos filho.
    for (int i = 0; i < actual_nr_processes; i++) {
        int tasks_for_this_child = tasks_per_process + (i < remainder_tasks ? 1 : 0);
        if (tasks_for_this_child == 0) {
            pids[i] = -1; // Marca como sem trabalho.
            snprintf(temp_files[i], sizeof(temp_files[i]), "/tmp/search_results_empty_%d_%d.tmp", getpid(), i);
            // Cria ficheiro vazio para consistência ou trata pids[i] == -1 na recolha.
            int empty_fd = open(temp_files[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(empty_fd >=0) { int zero_count = 0; write(empty_fd, &zero_count, sizeof(int)); close(empty_fd); }
            continue;
        }

        snprintf(temp_files[i], sizeof(temp_files[i]), "/tmp/search_results_child_%d_%d.tmp", getpid(), i);
        pids[i] = fork();
        if (pids[i] == 0) { // Processo Filho.
            process_search_tasks_child(&all_tasks[current_task_index], tasks_for_this_child, keyword, temp_files[i]);
            // process_search_tasks_child faz exit().
        } else if (pids[i] < 0) {
            perror("Erro ao criar processo filho para pesquisa paralela");
            pids[i] = -1; // Marca como falha.
             int error_fd = open(temp_files[i], O_WRONLY | O_CREAT | O_TRUNC, 0644); // Tenta criar ficheiro vazio.
            if(error_fd >=0) { int zero_count = 0; write(error_fd, &zero_count, sizeof(int)); close(error_fd); }
        }
        current_task_index += tasks_for_this_child;
    }

    // Espera pelos processos filho.
    for (int i = 0; i < actual_nr_processes; i++) {
        if (pids[i] > 0) {
            waitpid(pids[i], NULL, 0);
        }
    }

    // Agrega resultados dos ficheiros temporários.
    int final_count = 0;
    for (int i = 0; i < actual_nr_processes; i++) {
        int temp_fd = open(temp_files[i], O_RDONLY);
        if (temp_fd >= 0) {
            int num_ids_from_child = 0;
            if (read(temp_fd, &num_ids_from_child, sizeof(int)) == sizeof(int) && num_ids_from_child > 0) {
                if (final_count + num_ids_from_child <= MAX_RESULT_IDS) {
                    read(temp_fd, result_ids + final_count, num_ids_from_child * sizeof(int));
                    final_count += num_ids_from_child;
                } else {
                    // Buffer de resultados principal cheio, descarta restantes.
                    // Lê para limpar o ficheiro, mas não adiciona.
                    int temp_buffer[MAX_RESULT_IDS]; // Buffer temporário para descartar.
                    read(temp_fd, temp_buffer, num_ids_from_child * sizeof(int));
                    write(STDOUT_FILENO, "DEBUG: MAX_RESULT_IDS atingido no pai ao agregar resultados.\n",
                        strlen("DEBUG: MAX_RESULT_IDS atingido no pai ao agregar resultados.\n"));
                }
            }
            close(temp_fd);
            unlink(temp_files[i]); // Apaga ficheiro temporário.
        }
    }
    return final_count;
}


/**
 * @brief Guarda os documentos da cache (se modificada) no ficheiro de persistência "database.bin".
 *
 * Escreve o `next_id`, o número total de documentos e depois cada documento da cache.
 */
void save_documents() {
    if (!cache.modified) return; // Não guarda se não houver modificações.

    int fd = open("database.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Erro ao abrir/criar ficheiro da base de dados para escrita");
        return;
    }

    write(STDOUT_FILENO, "A gravar documentos na base de dados...\n", strlen("A gravar documentos na base de dados...\n"));

    write(fd, &next_id, sizeof(int)); // Guarda o próximo ID.
    write(fd, &cache.num_docs, sizeof(int)); // Guarda o número de documentos.

    for (int i = 0; i < cache.num_docs; i++) {
        if (cache.docs[i] != NULL) { // Verifica se o ponteiro é válido.
            write(fd, cache.docs[i], sizeof(Document));
        }
    }
    close(fd);

    char msg[64];
    snprintf(msg, sizeof(msg), "Gravados %d documentos com sucesso.\n", cache.num_docs);
    write(STDOUT_FILENO, msg, strlen(msg));
    cache.modified = 0; // Reset da flag de modificação.
}

/**
 * @brief Carrega os documentos do ficheiro de persistência "database.bin" para a cache.
 *
 * Lê o `next_id`, o número total de documentos e depois cada documento,
 * adicionando-os à cache até ao limite da cache.
 */
void load_documents() {
    int fd = open("database.bin", O_RDONLY);
    next_id = 1; // Valor por defeito se o ficheiro não existir.
    cache.num_docs = 0;
    cache.modified = 0;

    if (fd < 0) {
        if (errno == ENOENT) {
            write(STDOUT_FILENO, "Ficheiro da base de dados 'database.bin' não encontrado. A iniciar com estado vazio.\n",
                strlen("Ficheiro da base de dados 'database.bin' não encontrado. A iniciar com estado vazio.\n"));
        } else {
            perror("Erro ao tentar abrir 'database.bin' para leitura");
        }
        return;
    }

    write(STDOUT_FILENO, "A carregar documentos do disco ('database.bin')...\n", strlen("A carregar documentos do disco ('database.bin')...\n"));

    if (read(fd, &next_id, sizeof(int)) != sizeof(int)) {
        write(STDERR_FILENO, "Erro ao ler next_id do 'database.bin'. A iniciar com estado vazio.\n", strlen("Erro ao ler next_id do 'database.bin'. A iniciar com estado vazio.\n"));
        next_id = 1; cache.num_docs = 0; close(fd); return;
    }

    int total_docs_on_disk;
    if (read(fd, &total_docs_on_disk, sizeof(int)) != sizeof(int)) {
        write(STDERR_FILENO, "Erro ao ler total_docs_on_disk do 'database.bin'. A iniciar com estado vazio.\n", strlen("Erro ao ler total_docs_on_disk do 'database.bin'. A iniciar com estado vazio.\n"));
        next_id = 1; cache.num_docs = 0; close(fd); return;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "Encontrados %d documentos no disco. Próximo ID a ser usado: %d\n", total_docs_on_disk, next_id);
    write(STDOUT_FILENO, msg, strlen(msg));

    Document doc_from_disk;
    int loaded_count = 0;
    for (int i = 0; i < total_docs_on_disk && cache.num_docs < cache.max_size; i++) {
        if (read(fd, &doc_from_disk, sizeof(Document)) == sizeof(Document)) {
            cache.docs[cache.num_docs] = malloc(sizeof(Document));
            if (cache.docs[cache.num_docs] != NULL) {
                memcpy(cache.docs[cache.num_docs], &doc_from_disk, sizeof(Document));
                cache.num_docs++;
                loaded_count++;
            } else {
                perror("Erro de alocação de memória ao carregar documento da base de dados para a cache");
                break; // Para de carregar se não houver memória.
            }
        } else {
            write(STDERR_FILENO, "Erro ao ler registo de documento do 'database.bin' ou fim inesperado. Carregamento parcial.\n",
                strlen("Erro ao ler registo de documento do 'database.bin' ou fim inesperado. Carregamento parcial.\n"));
            break;
        }
    }
    close(fd);

    char msg_loaded_info[128];
    snprintf(msg_loaded_info, sizeof(msg_loaded_info), "Foram efetivamente adicionados %d documentos à cache nesta sessão de carregamento.\n", loaded_count);
    write(STDOUT_FILENO, msg_loaded_info, strlen(msg_loaded_info));

    snprintf(msg, sizeof(msg), "%d documentos carregados para a cache.\n", cache.num_docs);
    write(STDOUT_FILENO, msg, strlen(msg));
    cache.modified = 0; // Cache acabou de ser carregada, não está modificada.
}


/**
 * @brief Trata sinais do sistema operativo (SIGINT, SIGTERM).
 *
 * Em caso de receção de SIGINT ou SIGTERM, liberta a memória da cache,
 * remove o pipe do servidor e termina o processo. Não guarda documentos neste caso.
 *
 * @param sig O sinal recebido.
 */
void handle_signals(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        write(STDOUT_FILENO, "\nRecebido sinal para terminar o servidor (sem guardar alterações pendentes).\n", strlen("\nRecebido sinal para terminar o servidor (sem guardar alterações pendentes).\n"));

        for (int i = 0; i < cache.num_docs; i++) {
            if(cache.docs[i]) free(cache.docs[i]);
        }
        unlink(SERVER_PIPE); // Remove o FIFO do servidor.
        exit(0);
    }
}

/**
 * @brief Processa um pedido recebido de um cliente.
 *
 * Executa a operação solicitada (adicionar, consultar, remover, contar linhas,
 * pesquisar, desligar) e prepara a resposta a ser enviada de volta ao cliente.
 *
 * @param req A estrutura Request contendo os detalhes do pedido do cliente.
 * @return Uma estrutura Response contendo o resultado da operação e o estado.
 */
Response process_request(Request req) {
    Response resp;
    memset(&resp, 0, sizeof(Response)); // Inicializa a resposta.
    char log_msg[512]; // Buffer para mensagens de log.

    // Log do pedido recebido.
    switch (req.operation) {
        case ADD_DOC:
            snprintf(log_msg, sizeof(log_msg), "Recebido pedido ADD_DOC do cliente %d. Título: %.50s...\n", req.client_pid, req.doc.title);
            break;
        case QUERY_DOC:
            snprintf(log_msg, sizeof(log_msg), "Recebido pedido QUERY_DOC do cliente %d. ID: %d\n", req.client_pid, req.doc.id);
            break;
        default:
            snprintf(log_msg, sizeof(log_msg), "Recebido pedido operação %d do cliente %d\n", req.operation, req.client_pid);
    }
    write(STDOUT_FILENO, log_msg, strlen(log_msg));


    switch (req.operation) {
        case ADD_DOC: {
            char full_path[MAX_PATH_SIZE + sizeof(base_folder) + 2];
            if (snprintf(full_path, sizeof(full_path), "%s/%s", base_folder, req.doc.path) >= (int)sizeof(full_path)) {
                write(STDERR_FILENO, "Erro: Caminho completo do ficheiro excede o buffer.\n", strlen("Erro: Caminho completo do ficheiro excede o buffer.\n"));
                resp.status = -4; // Caminho muito longo.
            } else if (access(full_path, R_OK) == 0) { // Verifica se o ficheiro existe e é legível.
                int added_id = add_document(&req.doc);
                if (added_id >= 0) {
                    resp.doc.id = added_id;
                    resp.status = 0; // Sucesso.
                } else {
                    resp.status = -5; // Falha interna ao adicionar (ex: malloc).
                }
            } else {
                snprintf(log_msg, sizeof(log_msg), "Erro ADD_DOC: Ficheiro '%s' não encontrado ou sem permissão (errno: %d %s).\n", full_path, errno, strerror(errno));
                write(STDERR_FILENO, log_msg, strlen(log_msg));
                resp.status = -3; // Ficheiro inválido/inacessível.
            }
            break;
        }
        case QUERY_DOC: {
            Document* doc_found = find_document(req.doc.id);
            if (doc_found) {
                memcpy(&resp.doc, doc_found, sizeof(Document));
                resp.status = 0;
                // Se doc_found foi alocado temporariamente por find_document (cache cheia),
                // essa memória precisa ser libertada após esta cópia.
                // Assumindo que process_request é o único chamador de find_document que pode causar isto:
                int was_from_disk_temp = 1; // Assumir que pode ser temporário.
                for(int i=0; i < cache.num_docs; ++i) { if(cache.docs[i] == doc_found) { was_from_disk_temp = 0; break;} }
                if(was_from_disk_temp) free(doc_found); // Liberta a cópia temporária.

            } else {
                resp.status = -1; // Não encontrado.
            }
            break;
        }
        case DELETE_DOC:
            resp.status = remove_document(req.doc.id);
            break;
        case COUNT_LINES: {
            Document* doc_to_count = find_document(req.doc.id);
            if (doc_to_count) {
                resp.count = count_lines_with_keyword(doc_to_count, req.keyword);
                resp.status = (resp.count >=0) ? 0 : -1; // Sucesso se contagem >= 0.

                int was_from_disk_temp = 1;
                for(int i=0; i < cache.num_docs; ++i) { if(cache.docs[i] == doc_to_count) { was_from_disk_temp = 0; break;} }
                if(was_from_disk_temp) free(doc_to_count);
            } else {
                resp.status = -1;
            }
            break;
        }
        case SEARCH_DOCS:
            if (req.nr_processes > 1) {
                resp.num_ids = search_documents_with_keyword_parallel(req.keyword, resp.ids, req.nr_processes);
            } else {
                resp.num_ids = search_documents_with_keyword_serial(req.keyword, resp.ids);
            }
            resp.status = 0; // Pesquisa sempre retorna 0, mesmo que num_ids seja 0.
            break;
        case SHUTDOWN:
            if (cache.modified) {
                write(STDOUT_FILENO, "Comando SHUTDOWN recebido. A gravar base de dados...\n", strlen("Comando SHUTDOWN recebido. A gravar base de dados...\n"));
                save_documents();
            } else {
                write(STDOUT_FILENO, "Comando SHUTDOWN recebido. Nenhuma alteração pendente para gravar.\n", strlen("Comando SHUTDOWN recebido. Nenhuma alteração pendente para gravar.\n"));
            }
            resp.status = 0;
            break;
        default:
            resp.status = -2; // Operação inválida.
    }
    return resp;
}

/**
 * @brief Função principal do servidor.
 *
 * Inicializa o servidor, carrega documentos, cria o pipe do servidor,
 * e entra num loop para receber e processar pedidos de clientes.
 * Termina após um pedido SHUTDOWN ou receção de sinal SIGINT/SIGTERM.
 *
 * @param argc Número de argumentos da linha de comandos.
 * @param argv Array de strings dos argumentos da linha de comandos.
 * argv[1] deve ser a pasta de documentos.
 * argv[2] (opcional) o tamanho da cache.
 * @return 0 em caso de terminação bem-sucedida, 1 em caso de erro.
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        write(STDERR_FILENO, "Uso: ./dserver pasta_documentos [tamanho_cache]\n", strlen("Uso: ./dserver pasta_documentos [tamanho_cache]\n"));
        return 1;
    }
    strcpy(base_folder, argv[1]); // Copia a pasta base dos documentos.

    // Configura o tamanho da cache.
    int requested_cache_size = (argc > 2) ? atoi(argv[2]) : 100; // Padrão 100.
    if (requested_cache_size > MAX_DOCS) {
        char warning_msg[128];
        snprintf(warning_msg, sizeof(warning_msg),
                "Aviso: Tamanho da cache pedido (%d) excede o máximo (%d). A usar %d.\n",
                requested_cache_size, MAX_DOCS, MAX_DOCS);
        write(STDOUT_FILENO, warning_msg, strlen(warning_msg));
        cache.max_size = MAX_DOCS;
    } else if (requested_cache_size <= 0) {
        write(STDOUT_FILENO, "Aviso: Tamanho da cache inválido. A usar tamanho padrão 100.\n", strlen("Aviso: Tamanho da cache inválido. A usar tamanho padrão 100.\n"));
        cache.max_size = 100;
    } else {
        cache.max_size = requested_cache_size;
    }
    cache.num_docs = 0;
    cache.modified = 0;

    signal(SIGINT, handle_signals);  // Configura handler para Ctrl+C.
    signal(SIGTERM, handle_signals); // Configura handler para kill.

    write(STDOUT_FILENO, "A iniciar servidor...\n", strlen("A iniciar servidor...\n"));
    load_documents(); // Carrega documentos do disco.

    unlink(SERVER_PIPE); // Remove o pipe se já existir.
    if (mkfifo(SERVER_PIPE, 0666) < 0) { // Cria o FIFO do servidor.
        perror("Erro ao criar pipe do servidor (mkfifo)");
        return 1;
    }
    write(STDOUT_FILENO, "FIFO do servidor criado em " SERVER_PIPE "\n", strlen("FIFO do servidor criado em " SERVER_PIPE "\n"));

    char init_msg[256];
    snprintf(init_msg, sizeof(init_msg), "Servidor iniciado. Pasta de documentos: %s. Tamanho da cache: %d\n", base_folder, cache.max_size);
    write(STDOUT_FILENO, init_msg, strlen(init_msg));

    // Abre o FIFO para leitura (bloqueante).
    int server_fd = open(SERVER_PIPE, O_RDONLY);
    if (server_fd < 0) {
        perror("Erro ao abrir pipe do servidor para leitura");
        unlink(SERVER_PIPE); // Limpeza.
        return 1;
    }
    write(STDOUT_FILENO, "A aguardar ligações de clientes...\n", strlen("A aguardar ligações de clientes...\n"));

    int running = 1;
    while (running) {
        Request current_req;
        ssize_t bytes_read = read(server_fd, &current_req, sizeof(Request));

        if (bytes_read <= 0) { // Erro ou EOF.
            if (bytes_read < 0) perror("Erro na leitura do pipe do servidor");
            else write(STDOUT_FILENO, "EOF no pipe do servidor, a reabrir...\n", strlen("EOF no pipe do servidor, a reabrir...\n"));

            close(server_fd); // Fecha e reabre o pipe para aceitar novas conexões.
            server_fd = open(SERVER_PIPE, O_RDONLY);
            if (server_fd < 0) {
                perror("Erro fatal ao reabrir pipe do servidor");
                running = 0; // Termina o servidor.
            }
            continue;
        }

        Response current_resp = process_request(current_req); // Processa o pedido.

        char client_pipe_name[128];
        snprintf(client_pipe_name, sizeof(client_pipe_name), CLIENT_PIPE_FORMAT, current_req.client_pid);

        int client_fd = open(client_pipe_name, O_WRONLY); // Abre o pipe do cliente para escrita.
        if (client_fd < 0) {
            char error_msg[200];
            snprintf(error_msg, sizeof(error_msg), "Erro ao abrir pipe do cliente %s para escrita: %s\n", client_pipe_name, strerror(errno));
            write(STDERR_FILENO, error_msg, strlen(error_msg));
        } else {
            ssize_t bytes_written = write(client_fd, &current_resp, sizeof(Response));
            if (bytes_written != sizeof(Response)) {
                char error_msg[200];
                snprintf(error_msg, sizeof(error_msg), "Erro ao escrever resposta para o cliente %s: %s\n", client_pipe_name, strerror(errno));
                write(STDERR_FILENO, error_msg, strlen(error_msg));
            }
            close(client_fd); // Fecha o pipe do cliente.
        }

        if (current_req.operation == SHUTDOWN) {
            running = 0; // Termina o loop principal.
            write(STDOUT_FILENO, "Servidor a encerrar após pedido SHUTDOWN.\n", strlen("Servidor a encerrar após pedido SHUTDOWN.\n"));
        }
    }

    close(server_fd);
    unlink(SERVER_PIPE); // Limpeza final do pipe do servidor.

    // Liberta memória da cache.
    for (int i = 0; i < cache.num_docs; i++) {
        if (cache.docs[i]) free(cache.docs[i]);
    }
    write(STDOUT_FILENO, "Memória da cache libertada.\nServidor terminado.\n", strlen("Memória da cache libertada.\nServidor terminado.\n"));
    return 0;
}