#include "Document_Struct.h"

// Estrutura para armazenar os documentos em memória (cache)
typedef struct {
    Document* docs[MAX_DOCS];
    int num_docs;
    int max_size;       // Tamanho máximo da cache
    char policy;        // Política de cache: 'L' para LRU, 'M' para MFU
} Cache;

// Variáveis globais
Cache cache;
char base_folder[256];
int next_id = 1;

// Protótipos das funções ausentes (adicione no início do arquivo, após as declarações existentes)
int add_document(Document* doc);
Document* find_document(int id);
int remove_document(int id);
int count_lines_with_keyword(Document* doc, char* keyword);
int search_documents_with_keyword_parallel(char* keyword, int* result_ids, int nr_processes);
int search_documents_with_keyword_serial(char* keyword, int* result_ids);
void save_documents();

// Agora precisamos implementar essas funções (antes da função process_request)

// Função para adicionar um documento
int add_document(Document* doc) {
    // Verifica se há espaço na cache
    if (cache.num_docs >= cache.max_size) {
        // Se a cache estiver cheia, aplica a política de substituição
        int idx_to_replace = 0;
        
        if (cache.policy == 'L') { // LRU - Least Recently Used
            // Encontra o documento com o timestamp de acesso mais antigo
            time_t oldest_time = time(NULL);
            for (int i = 0; i < cache.num_docs; i++) {
                if (cache.docs[i]->last_access < oldest_time) {
                    oldest_time = cache.docs[i]->last_access;
                    idx_to_replace = i;
                }
            }
        } else { // MFU - Most Frequently Used
            // Encontra o documento com maior contagem de acessos
            int max_count = -1;
            for (int i = 0; i < cache.num_docs; i++) {
                if (cache.docs[i]->access_count > max_count) {
                    max_count = cache.docs[i]->access_count;
                    idx_to_replace = i;
                }
            }
        }
        
        // Remove o documento selecionado
        free(cache.docs[idx_to_replace]);
        
        // Move o último documento para a posição liberada
        if (idx_to_replace < cache.num_docs - 1) {
            cache.docs[idx_to_replace] = cache.docs[cache.num_docs - 1];
        }
        cache.num_docs--;
    }
    
    // Aloca memória para o novo documento
    Document* new_doc = malloc(sizeof(Document));
    if (!new_doc) return -1;
    
    // Copia os dados do documento
    memcpy(new_doc, doc, sizeof(Document));
    
    // Atribui um ID único
    new_doc->id = next_id++;
    
    // Inicializa contadores para políticas de cache
    new_doc->last_access = time(NULL);
    new_doc->access_count = 0;
    
    // Adiciona à cache
    cache.docs[cache.num_docs++] = new_doc;
    
    // Salva os documentos no disco
    save_documents();
    
    return new_doc->id;
}

// Função para encontrar um documento pelo ID
Document* find_document(int id) {
    // Procura na cache primeiro
    for (int i = 0; i < cache.num_docs; i++) {
        if (cache.docs[i]->id == id) {
            // Atualiza estatísticas para políticas de cache
            cache.docs[i]->last_access = time(NULL);
            cache.docs[i]->access_count++;
            return cache.docs[i];
        }
    }
    
    // Se não encontrou na cache, procura no disco
    int fd = open("database.bin", O_RDONLY);
    if (fd < 0) return NULL;
    
    // Pula o cabeçalho (next_id e num_docs)
    lseek(fd, 2 * sizeof(int), SEEK_SET);
    
    // Lê documentos um por um
    Document disk_doc;
    while (read(fd, &disk_doc, sizeof(Document)) == sizeof(Document)) {
        if (disk_doc.id == id) {
            close(fd);
            
            // Se há espaço na cache, adiciona este documento
            if (cache.num_docs < cache.max_size) {
                Document* doc = malloc(sizeof(Document));
                memcpy(doc, &disk_doc, sizeof(Document));
                
                // Inicializa contadores para políticas de cache
                doc->last_access = time(NULL);
                doc->access_count = 1;
                
                cache.docs[cache.num_docs++] = doc;
                return cache.docs[cache.num_docs - 1];
            } else {
                // Se não há espaço, retorna uma cópia temporária
                Document* temp_doc = malloc(sizeof(Document));
                memcpy(temp_doc, &disk_doc, sizeof(Document));
                return temp_doc;
            }
        }
    }
    
    close(fd);
    return NULL;
}

// Função para remover um documento
int remove_document(int id) {
    // Procura na cache
    for (int i = 0; i < cache.num_docs; i++) {
        if (cache.docs[i]->id == id) {
            // Remove da cache
            free(cache.docs[i]);
            
            // Move o último documento para a posição liberada
            if (i < cache.num_docs - 1) {
                cache.docs[i] = cache.docs[cache.num_docs - 1];
            }
            cache.num_docs--;
            
            // Salva os documentos no disco
            save_documents();
            return 0;
        }
    }
    
    // Se não encontrou na cache, verifica se existe no disco
    Document* doc = find_document(id);
    if (doc) {
        // Se encontrou, já foi adicionado à cache, então remove da cache
        free(doc);
        
        // Salva os documentos no disco
        save_documents();
        return 0;
    }
    
    return -1; // Documento não encontrado
}

// Função para contar linhas com uma palavra-chave
int count_lines_with_keyword(Document* doc, char* keyword) {
    if (!doc) return -1;
    
    // Abre o arquivo do documento
    char full_path[MAX_PATH_SIZE + 256];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_folder, doc->path);
    
    int fd = open(full_path, O_RDONLY);
    if (fd < 0) return -1;
    
    // Lê o arquivo linha por linha
    char buffer[4096];
    int count = 0;
    int pos = 0;
    char c;
    
    while (read(fd, &c, 1) == 1) {
        if (c == '\n' || pos >= sizeof(buffer) - 1) {
            // Finaliza a linha
            buffer[pos] = '\0';
            
            // Verifica se a linha contém a palavra-chave
            if (strstr(buffer, keyword) != NULL) {
                count++;
            }
            
            // Reinicia o buffer
            pos = 0;
        } else {
            buffer[pos++] = c;
        }
    }
    
    // Verifica a última linha se não terminou com quebra de linha
    if (pos > 0) {
        buffer[pos] = '\0';
        if (strstr(buffer, keyword) != NULL) {
            count++;
        }
    }
    
    close(fd);
    return count;
}

// Função para buscar documentos com uma palavra-chave (versão serial)
int search_documents_with_keyword_serial(char* keyword, int* result_ids) {
    int count = 0;
    
    // Procura em todos os documentos da cache
    for (int i = 0; i < cache.num_docs && count < MAX_RESULT_IDS; i++) {
        int lines = count_lines_with_keyword(cache.docs[i], keyword);
        if (lines > 0) {
            result_ids[count++] = cache.docs[i]->id;
        }
    }
    
    // Procura nos documentos do disco que não estão na cache
    int fd = open("database.bin", O_RDONLY);
    if (fd < 0) return count;
    
    // Lê o cabeçalho
    int next_id_disk, num_docs_disk;
    read(fd, &next_id_disk, sizeof(int));
    read(fd, &num_docs_disk, sizeof(int));
    
    // Lê cada documento
    Document disk_doc;
    while (read(fd, &disk_doc, sizeof(Document)) == sizeof(Document) && count < MAX_RESULT_IDS) {
        // Verifica se este documento já está na cache
        int in_cache = 0;
        for (int i = 0; i < cache.num_docs; i++) {
            if (cache.docs[i]->id == disk_doc.id) {
                in_cache = 1;
                break;
            }
        }
        
        if (!in_cache) {
            // Abre o arquivo e procura a palavra-chave
            char full_path[MAX_PATH_SIZE + 256];
            snprintf(full_path, sizeof(full_path), "%s/%s", base_folder, disk_doc.path);
            
            int doc_fd = open(full_path, O_RDONLY);
            if (doc_fd >= 0) {
                char buffer[4096];
                int found = 0;
                int pos = 0;
                char c;
                
                while (!found && read(doc_fd, &c, 1) == 1) {
                    if (c == '\n' || pos >= sizeof(buffer) - 1) {
                        buffer[pos] = '\0';
                        if (strstr(buffer, keyword) != NULL) {
                            found = 1;
                        }
                        pos = 0;
                    } else {
                        buffer[pos++] = c;
                    }
                }
                
                // Verifica a última linha
                if (!found && pos > 0) {
                    buffer[pos] = '\0';
                    if (strstr(buffer, keyword) != NULL) {
                        found = 1;
                    }
                }
                
                close(doc_fd);
                
                if (found) {
                    result_ids[count++] = disk_doc.id;
                }
            }
        }
    }
    
    close(fd);
    return count;
}

// Função auxiliar para busca em paralelo
void search_process(int start, int end, char* keyword, char* temp_file, char* base_folder) {
    // Abre o arquivo da base de dados
    int fd = open("database.bin", O_RDONLY);
    if (fd < 0) return;
    
    // Pula o cabeçalho
    lseek(fd, 2 * sizeof(int), SEEK_SET);
    
    // Posiciona no início do intervalo desejado
    lseek(fd, start * sizeof(Document), SEEK_CUR);
    
    // Abre o arquivo temporário para escrita
    int temp_fd = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (temp_fd < 0) {
        close(fd);
        return;
    }
    
    // Processa os documentos no intervalo
    Document doc;
    for (int i = 0; i < (end - start) && read(fd, &doc, sizeof(Document)) == sizeof(Document); i++) {
        // Abre o arquivo e procura a palavra-chave
        char full_path[MAX_PATH_SIZE + 256];
        snprintf(full_path, sizeof(full_path), "%s/%s", base_folder, doc.path);
        
        int doc_fd = open(full_path, O_RDONLY);
        if (doc_fd >= 0) {
            char buffer[4096];
            int found = 0;
            int pos = 0;
            char c;
            
            while (!found && read(doc_fd, &c, 1) == 1) {
                if (c == '\n' || pos >= sizeof(buffer) - 1) {
                    buffer[pos] = '\0';
                    if (strstr(buffer, keyword) != NULL) {
                        found = 1;
                    }
                    pos = 0;
                } else {
                    buffer[pos++] = c;
                }
            }
            
            // Verifica a última linha
            if (!found && pos > 0) {
                buffer[pos] = '\0';
                if (strstr(buffer, keyword) != NULL) {
                    found = 1;
                }
            }
            
            close(doc_fd);
            
            if (found) {
                write(temp_fd, &doc.id, sizeof(int));
            }
        }
    }
    
    close(temp_fd);
    close(fd);
}

// Função para buscar documentos com uma palavra-chave (versão paralela)
int search_documents_with_keyword_parallel(char* keyword, int* result_ids, int nr_processes) {
    // Obtém o número total de documentos
    int fd = open("database.bin", O_RDONLY);
    if (fd < 0) return 0;
    
    int next_id_disk, num_docs_disk;
    read(fd, &next_id_disk, sizeof(int));
    read(fd, &num_docs_disk, sizeof(int));
    close(fd);
    
    // Ajusta o número de processos se for maior que o número de documentos
    if (nr_processes > num_docs_disk) {
        nr_processes = num_docs_disk > 0 ? num_docs_disk : 1;
    }
    
    if (nr_processes <= 1 || num_docs_disk <= 1) {
        // Se só há um processo ou um documento, usa a versão serial
        return search_documents_with_keyword_serial(keyword, result_ids);
    }
    
    // Divide os documentos entre os processos
    int docs_per_process = num_docs_disk / nr_processes;
    int remaining = num_docs_disk % nr_processes;
    
    // Array para armazenar os PIDs dos processos filhos
    pid_t pids[nr_processes];
    
    // Cria os processos filhos
    for (int i = 0; i < nr_processes; i++) {
        // Calcula o intervalo de documentos para este processo
        int start = i * docs_per_process + (i < remaining ? i : remaining);
        int end = start + docs_per_process + (i < remaining ? 1 : 0);
        
        // Cria um nome de arquivo temporário único para este processo
        char temp_file[64];
        sprintf(temp_file, "/tmp/search_results_%d.tmp", i);
        
        // Cria o processo filho
        pid_t pid = fork();
        
        if (pid == 0) {
            // Processo filho
            search_process(start, end, keyword, temp_file, base_folder);
            exit(0);
        } else if (pid > 0) {
            // Processo pai
            pids[i] = pid;
        } else {
            // Erro no fork
            write(STDERR_FILENO, "Erro ao criar processo filho\n", strlen("Erro ao criar processo filho\n"));
        }
    }
    
    // Aguarda todos os processos filhos terminarem
    for (int i = 0; i < nr_processes; i++) {
        waitpid(pids[i], NULL, 0);
    }
    
    // Combina os resultados
    int count = 0;
    
    for (int i = 0; i < nr_processes && count < MAX_RESULT_IDS; i++) {
        char temp_file[64];
        sprintf(temp_file, "/tmp/search_results_%d.tmp", i);
        
        int temp_fd = open(temp_file, O_RDONLY);
        if (temp_fd >= 0) {
            int doc_id;
            while (count < MAX_RESULT_IDS && read(temp_fd, &doc_id, sizeof(int)) == sizeof(int)) {
                result_ids[count++] = doc_id;
            }
            close(temp_fd);
            unlink(temp_file); // Remove o arquivo temporário
        }
    }
    
    return count;
}

// Função para salvar os documentos em disco
void save_documents() {
    int fd = open("database.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        write(STDERR_FILENO, "Erro ao abrir arquivo de base de dados\n", strlen("Erro ao abrir arquivo de base de dados\n"));
        return;
    }
    
    write(STDOUT_FILENO, "Salvando documentos na base de dados...\n", strlen("Salvando documentos na base de dados...\n"));
    
    // Escreve o próximo ID disponível
    write(fd, &next_id, sizeof(int));
    
    // Lê todos os documentos do disco
    int total_docs = 0;
    Document all_docs[MAX_DOCS];
    
    // Primeiro, adiciona os documentos da cache
    for (int i = 0; i < cache.num_docs; i++) {
        memcpy(&all_docs[total_docs], cache.docs[i], sizeof(Document));
        total_docs++;
    }
    
    // Escreve a quantidade total de documentos
    write(fd, &total_docs, sizeof(int));
    
    // Escreve cada documento
    for (int i = 0; i < total_docs; i++) {
        write(fd, &all_docs[i], sizeof(Document));
    }
    
    close(fd);
    
    char msg[128];
    int len = snprintf(msg, sizeof(msg), "Documentos salvos com sucesso. Total: %d\n", total_docs);
    write(STDOUT_FILENO, msg, len);
}

// Função para carregar os documentos do disco
void load_documents() {
    int fd = open("database.bin", O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            write(STDOUT_FILENO, "Arquivo de base de dados não encontrado. Iniciando com base vazia.\n", strlen("Arquivo de base de dados não encontrado. Iniciando com base vazia.\n"));
            next_id = 1;
        } else {
            write(STDERR_FILENO, "Erro ao abrir arquivo de base de dados\n", strlen("Erro ao abrir arquivo de base de dados\n"));
        }
        return;
    }
    
    write(STDOUT_FILENO, "Carregando documentos do disco...\n", strlen("Carregando documentos do disco...\n"));
    
    // Lê o próximo ID disponível
    read(fd, &next_id, sizeof(int));
    
    // Lê a quantidade de documentos
    int total_docs;
    read(fd, &total_docs, sizeof(int));
    
    char msg[128];
    int len = snprintf(msg, sizeof(msg), "Encontrados %d documentos no disco. Próximo ID: %d\n", total_docs, next_id);
    write(STDOUT_FILENO, msg, len);
    
    // Inicializa a cache vazia
    cache.num_docs = 0;
    
    // Lê cada documento e adiciona à cache se houver espaço
    Document doc;
    for (int i = 0; i < total_docs && i < cache.max_size; i++) {
        read(fd, &doc, sizeof(Document));
        
        // Adiciona à cache
        cache.docs[cache.num_docs] = malloc(sizeof(Document));
        memcpy(cache.docs[cache.num_docs], &doc, sizeof(Document));
        
        // Atualiza contadores para política de cache
        cache.docs[cache.num_docs]->last_access = time(NULL);
        cache.docs[cache.num_docs]->access_count = 0;
        
        cache.num_docs++;
    }
    
    // Se ainda há documentos no disco mas a cache está cheia, apenas skip
    if (total_docs > cache.max_size) {
        lseek(fd, (total_docs - cache.max_size) * sizeof(Document), SEEK_CUR);
        write(STDOUT_FILENO, "Cache cheia. Alguns documentos não foram carregados na memória.\n", strlen("Cache cheia. Alguns documentos não foram carregados na memória.\n"));
    }
    
    close(fd);
    
    len = snprintf(msg, sizeof(msg), "%d documentos carregados na cache\n", cache.num_docs);
    write(STDOUT_FILENO, msg, len);
}

// Função para processar uma requisição
Response process_request(Request req) {
    Response resp;
    memset(&resp, 0, sizeof(Response));
    
    char msg[256];
    int len;
    
    // Log da requisição recebida
    switch (req.operation) {
        case ADD_DOC:
            len = snprintf(msg, sizeof(msg), "Recebida requisição ADD_DOC do cliente %d. Título: %s\n", req.client_pid, req.doc.title);
            break;
        case QUERY_DOC:
            len = snprintf(msg, sizeof(msg), "Recebida requisição QUERY_DOC do cliente %d. ID: %d\n", req.client_pid, req.doc.id);
            break;
        case DELETE_DOC:
            len = snprintf(msg, sizeof(msg), "Recebida requisição DELETE_DOC do cliente %d. ID: %d\n", req.client_pid, req.doc.id);
            break;
        case COUNT_LINES:
            len = snprintf(msg, sizeof(msg), "Recebida requisição COUNT_LINES do cliente %d. ID: %d, Keyword: %s\n", req.client_pid, req.doc.id, req.keyword);
            break;
        case SEARCH_DOCS:
            len = snprintf(msg, sizeof(msg), "Recebida requisição SEARCH_DOCS do cliente %d. Keyword: %s, NrProcs: %d\n", req.client_pid, req.keyword, req.nr_processes);
            break;
        case SHUTDOWN:
            len = snprintf(msg, sizeof(msg), "Recebida requisição SHUTDOWN do cliente %d\n", req.client_pid);
            break;
        default:
            len = snprintf(msg, sizeof(msg), "Recebida requisição desconhecida (%d) do cliente %d\n", req.operation, req.client_pid);
    }
    write(STDOUT_FILENO, msg, len);
    
    // Processamento da requisição (igual ao original)
    switch (req.operation) {
        case ADD_DOC:
            resp.doc.id = add_document(&req.doc);
            resp.status = 0;
            break;
            
        case QUERY_DOC:
            {
                Document* doc = find_document(req.doc.id);
                if (doc) {
                    memcpy(&resp.doc, doc, sizeof(Document));
                    resp.status = 0;
                } else {
                    resp.status = -1; // Documento não encontrado
                }
            }
            break;
            
        case DELETE_DOC:
            resp.status = remove_document(req.doc.id);
            break;
            
        case COUNT_LINES:
            {
                Document* doc = find_document(req.doc.id);
                if (doc) {
                    resp.count = count_lines_with_keyword(doc, req.keyword);
                    resp.status = 0;
                } else {
                    resp.status = -1; // Documento não encontrado
                }
            }
            break;
            
        case SEARCH_DOCS:
            if (req.nr_processes > 1) {
                // Versão paralela
                resp.num_ids = search_documents_with_keyword_parallel(req.keyword, resp.ids, req.nr_processes);
            } else {
                // Versão serial
                resp.num_ids = search_documents_with_keyword_serial(req.keyword, resp.ids);
            }
            resp.status = 0;
            break;
            
        case SHUTDOWN:
            resp.status = 0;
            break;
            
        default:
            resp.status = -2; // Operação inválida
    }
    
    // Log da resposta
    switch (req.operation) {
        case ADD_DOC:
            len = snprintf(msg, sizeof(msg), "Enviando resposta ADD_DOC para cliente %d. ID atribuído: %d\n", 
                          req.client_pid, resp.doc.id);
            break;
        case QUERY_DOC:
            if (resp.status == 0) {
                len = snprintf(msg, sizeof(msg), "Enviando resposta QUERY_DOC para cliente %d. Documento encontrado.\n", 
                              req.client_pid);
            } else {
                len = snprintf(msg, sizeof(msg), "Enviando resposta QUERY_DOC para cliente %d. Documento não encontrado.\n", 
                              req.client_pid);
            }
            break;
        case DELETE_DOC:
            len = snprintf(msg, sizeof(msg), "Enviando resposta DELETE_DOC para cliente %d. Status: %d\n", 
                          req.client_pid, resp.status);
            break;
        case COUNT_LINES:
            if (resp.status == 0) {
                len = snprintf(msg, sizeof(msg), "Enviando resposta COUNT_LINES para cliente %d. Contagem: %d\n", 
                              req.client_pid, resp.count);
            } else {
                len = snprintf(msg, sizeof(msg), "Enviando resposta COUNT_LINES para cliente %d. Documento não encontrado.\n", 
                              req.client_pid);
            }
            break;
        case SEARCH_DOCS:
            len = snprintf(msg, sizeof(msg), "Enviando resposta SEARCH_DOCS para cliente %d. Encontrados: %d documentos\n", 
                          req.client_pid, resp.num_ids);
            break;
        case SHUTDOWN:
            len = snprintf(msg, sizeof(msg), "Enviando resposta SHUTDOWN para cliente %d\n", req.client_pid);
            break;
        default:
            len = snprintf(msg, sizeof(msg), "Enviando resposta de operação inválida para cliente %d\n", req.client_pid);
    }
    write(STDOUT_FILENO, msg, len);
    
    return resp;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        write(STDERR_FILENO, "Uso: ./dserver document_folder [cache_size]\n", strlen("Uso: ./dserver document_folder [cache_size]\n"));
        return 1;
    }
    
    // Armazena a pasta base dos documentos
    strcpy(base_folder, argv[1]);
    
    // Configura o tamanho da cache
    cache.max_size = (argc > 2) ? atoi(argv[2]) : 100; // Default: 100 documentos
    cache.num_docs = 0;
    cache.policy = 'L'; // Default: LRU
    
    write(STDOUT_FILENO, "Iniciando servidor...\n", strlen("Iniciando servidor...\n"));
    
    // Verifica e cria o arquivo da base de dados se não existir
    int db_fd = open("database.bin", O_RDONLY);
    if (db_fd < 0 && errno == ENOENT) {
        write(STDOUT_FILENO, "Criando nova base de dados...\n", strlen("Criando nova base de dados...\n"));
        db_fd = open("database.bin", O_WRONLY | O_CREAT, 0644);
        if (db_fd >= 0) {
            // Inicializa um arquivo vazio com next_id = 1 e num_docs = 0
            int initial_data[2] = {1, 0};
            write(db_fd, initial_data, sizeof(initial_data));
            close(db_fd);
            write(STDOUT_FILENO, "Base de dados inicializada com sucesso\n", strlen("Base de dados inicializada com sucesso\n"));
        } else {
            write(STDERR_FILENO, "Erro ao criar base de dados\n", strlen("Erro ao criar base de dados\n"));
        }
    } else if (db_fd >= 0) {
        close(db_fd);
        write(STDOUT_FILENO, "Base de dados existente encontrada\n", strlen("Base de dados existente encontrada\n"));
    }
    
    // Carrega os documentos do disco
    load_documents();
    
    // Cria o pipe do servidor
    unlink(SERVER_PIPE);
    write(STDOUT_FILENO, "Tentando criar FIFO do servidor em: " SERVER_PIPE "\n", strlen("Tentando criar FIFO do servidor em: " SERVER_PIPE "\n"));
    
    if (mkfifo(SERVER_PIPE, 0666) < 0) {
        char error_msg[256];
        int len = snprintf(error_msg, sizeof(error_msg), "Erro ao criar pipe do servidor: %s\n", strerror(errno));
        write(STDERR_FILENO, error_msg, len);
        return 1;
    }
    
    write(STDOUT_FILENO, "FIFO do servidor criado com sucesso\n", strlen("FIFO do servidor criado com sucesso\n"));
    
    char msg[256];
    int len = snprintf(msg, sizeof(msg), "Servidor iniciado. Pasta de documentos: %s. Tamanho da cache: %d\n", 
                       base_folder, cache.max_size);
    write(STDOUT_FILENO, msg, len);
    
    write(STDOUT_FILENO, "Abrindo FIFO do servidor para leitura...\n", strlen("Abrindo FIFO do servidor para leitura...\n"));
    int server_fd = open(SERVER_PIPE, O_RDONLY);
    if (server_fd < 0) {
        char error_msg[256];
        int len = snprintf(error_msg, sizeof(error_msg), "Erro ao abrir pipe do servidor para leitura: %s\n", strerror(errno));
        write(STDERR_FILENO, error_msg, len);
        return 1;
    }
    
    write(STDOUT_FILENO, "FIFO do servidor aberto com sucesso\n", strlen("FIFO do servidor aberto com sucesso\n"));
    write(STDOUT_FILENO, "Aguardando conexões...\n", strlen("Aguardando conexões...\n"));
    
    int running = 1;
    
    while (running) {
        Request req;
        Response resp;
        
        // Lê a requisição do cliente
        write(STDOUT_FILENO, "Aguardando requisição...\n", 33);
        ssize_t bytes_read = read(server_fd, &req, sizeof(Request));
        
        if (bytes_read <= 0) {
            // Log do erro
            char error_msg[256];
            int len = snprintf(error_msg, sizeof(error_msg), "Erro na leitura do pipe do servidor: %s. Reabrindo...\n", bytes_read == 0 ? "EOF" : strerror(errno));
            write(STDERR_FILENO, error_msg, len);
            
            // Reopen the pipe if it was closed
            close(server_fd);
            server_fd = open(SERVER_PIPE, O_RDONLY);
            if (server_fd < 0) {
                len = snprintf(error_msg, sizeof(error_msg), "Erro ao reabrir pipe do servidor: %s\n", strerror(errno));
                write(STDERR_FILENO, error_msg, len);
            } else {
                write(STDOUT_FILENO, "Pipe do servidor reaberto com sucesso\n", 45);
            }
            continue;
        }
        
        // Log da requisição recebida
        len = snprintf(msg, sizeof(msg), "Recebida requisição de %d bytes do cliente %d\n", (int)bytes_read, req.client_pid);
        write(STDOUT_FILENO, msg, len);
        
        // Processa a requisição
        resp = process_request(req);
        
        // Abre o pipe do cliente para enviar a resposta
        char client_pipe[64];
        sprintf(client_pipe, CLIENT_PIPE_FORMAT, req.client_pid);
        
        len = snprintf(msg, sizeof(msg), "Tentando abrir FIFO do cliente em: %s\n", client_pipe);
        write(STDOUT_FILENO, msg, len);
        
        int client_fd = open(client_pipe, O_WRONLY);
        if (client_fd < 0) {
            char error_msg[256];
            int len = snprintf(error_msg, sizeof(error_msg), "Erro ao abrir pipe do cliente para escrita: %s\n", strerror(errno));
            write(STDERR_FILENO, error_msg, len);
            continue;
        }
        
        write(STDOUT_FILENO, "FIFO do cliente aberto com sucesso\n", 42);
        
        // Envia a resposta para o cliente
        ssize_t bytes_written = write(client_fd, &resp, sizeof(Response));
        
        if (bytes_written != sizeof(Response)) {
            char error_msg[256];
            int len = snprintf(error_msg, sizeof(error_msg), "Erro ao escrever no pipe do cliente: %s\n", strerror(errno));
            write(STDERR_FILENO, error_msg, len);
        } else {
            len = snprintf(msg, sizeof(msg), "Enviados %d bytes para o cliente %d\n", (int)bytes_written, req.client_pid);
            write(STDOUT_FILENO, msg, len);
        }
        
        // Fecha o pipe do cliente
        close(client_fd);
        write(STDOUT_FILENO, "FIFO do cliente fechado\n", 31);
        
        // Se for um pedido de encerramento, para o servidor
        if (req.operation == SHUTDOWN) {
            running = 0;
            write(STDOUT_FILENO, "Recebido comando para encerrar o servidor\n", 49);
        }
    }
    
    // Fecha o pipe do servidor
    close(server_fd);
    unlink(SERVER_PIPE);
    
    write(STDOUT_FILENO, "FIFO do servidor fechado e removido\n", 43);
    
    // Libera a memória dos documentos na cache
    for (int i = 0; i < cache.num_docs; i++) {
        free(cache.docs[i]);
    }
    
    write(STDOUT_FILENO, "Memória da cache liberada\n", 33);
    write(STDOUT_FILENO, "Servidor encerrado com sucesso\n", 39);
    
    return 0;
}