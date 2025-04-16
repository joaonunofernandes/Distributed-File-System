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

// Função para salvar os documentos em disco
void save_documents() {
    int fd = open("database.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        write(STDERR_FILENO, "Erro ao abrir arquivo de base de dados\n", 38);
        return;
    }
    
    write(STDOUT_FILENO, "DEBUG: Salvando documentos na base de dados...\n", 47);
    
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
    int len = snprintf(msg, sizeof(msg), "DEBUG: Documentos salvos com sucesso. Total: %d\n", total_docs);
    write(STDOUT_FILENO, msg, len);
}

// Função para carregar os documentos do disco
void load_documents() {
    int fd = open("database.bin", O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            write(STDOUT_FILENO, "Arquivo de base de dados não encontrado. Iniciando com base vazia.\n", 65);
            next_id = 1;
        } else {
            write(STDERR_FILENO, "Erro ao abrir arquivo de base de dados\n", 38);
        }
        return;
    }
    
    write(STDOUT_FILENO, "DEBUG: Carregando documentos do disco...\n", 41);
    
    // Lê o próximo ID disponível
    read(fd, &next_id, sizeof(int));
    
    // Lê a quantidade de documentos
    int total_docs;
    read(fd, &total_docs, sizeof(int));
    
    char msg[128];
    int len = snprintf(msg, sizeof(msg), "DEBUG: Encontrados %d documentos no disco. Próximo ID: %d\n", 
                      total_docs, next_id);
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
        write(STDOUT_FILENO, "DEBUG: Cache cheia. Alguns documentos não foram carregados na memória.\n", 70);
    }
    
    close(fd);
    
    len = snprintf(msg, sizeof(msg), "DEBUG: %d documentos carregados na cache\n", cache.num_docs);
    write(STDOUT_FILENO, msg, len);
}

// Outras funções permanecem iguais...

// Função para processar uma requisição
Response process_request(Request req) {
    Response resp;
    memset(&resp, 0, sizeof(Response));
    
    char msg[256];
    int len;
    
    // Log da requisição recebida
    switch (req.operation) {
        case ADD_DOC:
            len = snprintf(msg, sizeof(msg), "DEBUG: Recebida requisição ADD_DOC do cliente %d. Título: %s\n", 
                           req.client_pid, req.doc.title);
            break;
        case QUERY_DOC:
            len = snprintf(msg, sizeof(msg), "DEBUG: Recebida requisição QUERY_DOC do cliente %d. ID: %d\n", 
                           req.client_pid, req.doc.id);
            break;
        case DELETE_DOC:
            len = snprintf(msg, sizeof(msg), "DEBUG: Recebida requisição DELETE_DOC do cliente %d. ID: %d\n", 
                           req.client_pid, req.doc.id);
            break;
        case COUNT_LINES:
            len = snprintf(msg, sizeof(msg), "DEBUG: Recebida requisição COUNT_LINES do cliente %d. ID: %d, Keyword: %s\n", 
                           req.client_pid, req.doc.id, req.keyword);
            break;
        case SEARCH_DOCS:
            len = snprintf(msg, sizeof(msg), "DEBUG: Recebida requisição SEARCH_DOCS do cliente %d. Keyword: %s, NrProcs: %d\n", 
                           req.client_pid, req.keyword, req.nr_processes);
            break;
        case SHUTDOWN:
            len = snprintf(msg, sizeof(msg), "DEBUG: Recebida requisição SHUTDOWN do cliente %d\n", req.client_pid);
            break;
        default:
            len = snprintf(msg, sizeof(msg), "DEBUG: Recebida requisição desconhecida (%d) do cliente %d\n", 
                           req.operation, req.client_pid);
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
            len = snprintf(msg, sizeof(msg), "DEBUG: Enviando resposta ADD_DOC para cliente %d. ID atribuído: %d\n", 
                          req.client_pid, resp.doc.id);
            break;
        case QUERY_DOC:
            if (resp.status == 0) {
                len = snprintf(msg, sizeof(msg), "DEBUG: Enviando resposta QUERY_DOC para cliente %d. Documento encontrado.\n", 
                              req.client_pid);
            } else {
                len = snprintf(msg, sizeof(msg), "DEBUG: Enviando resposta QUERY_DOC para cliente %d. Documento não encontrado.\n", 
                              req.client_pid);
            }
            break;
        case DELETE_DOC:
            len = snprintf(msg, sizeof(msg), "DEBUG: Enviando resposta DELETE_DOC para cliente %d. Status: %d\n", 
                          req.client_pid, resp.status);
            break;
        case COUNT_LINES:
            if (resp.status == 0) {
                len = snprintf(msg, sizeof(msg), "DEBUG: Enviando resposta COUNT_LINES para cliente %d. Contagem: %d\n", 
                              req.client_pid, resp.count);
            } else {
                len = snprintf(msg, sizeof(msg), "DEBUG: Enviando resposta COUNT_LINES para cliente %d. Documento não encontrado.\n", 
                              req.client_pid);
            }
            break;
        case SEARCH_DOCS:
            len = snprintf(msg, sizeof(msg), "DEBUG: Enviando resposta SEARCH_DOCS para cliente %d. Encontrados: %d documentos\n", 
                          req.client_pid, resp.num_ids);
            break;
        case SHUTDOWN:
            len = snprintf(msg, sizeof(msg), "DEBUG: Enviando resposta SHUTDOWN para cliente %d\n", req.client_pid);
            break;
        default:
            len = snprintf(msg, sizeof(msg), "DEBUG: Enviando resposta de operação inválida para cliente %d\n", req.client_pid);
    }
    write(STDOUT_FILENO, msg, len);
    
    return resp;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        write(STDERR_FILENO, "Uso: ./dserver document_folder [cache_size]\n", 44);
        return 1;
    }
    
    // Armazena a pasta base dos documentos
    strcpy(base_folder, argv[1]);
    
    // Configura o tamanho da cache
    cache.max_size = (argc > 2) ? atoi(argv[2]) : 100; // Default: 100 documentos
    cache.num_docs = 0;
    cache.policy = 'L'; // Default: LRU
    
    write(STDOUT_FILENO, "DEBUG: Iniciando servidor...\n", 29);
    
    // Verifica e cria o arquivo da base de dados se não existir
    int db_fd = open("database.bin", O_RDONLY);
    if (db_fd < 0 && errno == ENOENT) {
        write(STDOUT_FILENO, "DEBUG: Criando nova base de dados...\n", 37);
        db_fd = open("database.bin", O_WRONLY | O_CREAT, 0644);
        if (db_fd >= 0) {
            // Inicializa um arquivo vazio com next_id = 1 e num_docs = 0
            int initial_data[2] = {1, 0};
            write(db_fd, initial_data, sizeof(initial_data));
            close(db_fd);
            write(STDOUT_FILENO, "DEBUG: Base de dados inicializada com sucesso\n", 46);
        } else {
            write(STDERR_FILENO, "DEBUG: Erro ao criar base de dados\n", 35);
        }
    } else if (db_fd >= 0) {
        close(db_fd);
        write(STDOUT_FILENO, "DEBUG: Base de dados existente encontrada\n", 42);
    }
    
    // Carrega os documentos do disco
    load_documents();
    
    // Cria o pipe do servidor
    unlink(SERVER_PIPE);
    write(STDOUT_FILENO, "DEBUG: Tentando criar FIFO do servidor em: " SERVER_PIPE "\n", 57);
    
    if (mkfifo(SERVER_PIPE, 0666) < 0) {
        char error_msg[256];
        int len = snprintf(error_msg, sizeof(error_msg), 
                       "DEBUG: Erro ao criar pipe do servidor: %s\n", strerror(errno));
        write(STDERR_FILENO, error_msg, len);
        return 1;
    }
    
    write(STDOUT_FILENO, "DEBUG: FIFO do servidor criado com sucesso\n", 43);
    
    char msg[256];
    int len = snprintf(msg, sizeof(msg), "Servidor iniciado. Pasta de documentos: %s. Tamanho da cache: %d\n", 
                       base_folder, cache.max_size);
    write(STDOUT_FILENO, msg, len);
    
    write(STDOUT_FILENO, "DEBUG: Abrindo FIFO do servidor para leitura...\n", 48);
    int server_fd = open(SERVER_PIPE, O_RDONLY);
    if (server_fd < 0) {
        char error_msg[256];
        int len = snprintf(error_msg, sizeof(error_msg), 
                       "DEBUG: Erro ao abrir pipe do servidor para leitura: %s\n", strerror(errno));
        write(STDERR_FILENO, error_msg, len);
        return 1;
    }
    
    write(STDOUT_FILENO, "DEBUG: FIFO do servidor aberto com sucesso\n", 43);
    write(STDOUT_FILENO, "DEBUG: Aguardando conexões...\n", 31);
    
    int running = 1;
    
    while (running) {
        Request req;
        Response resp;
        
        // Lê a requisição do cliente
        write(STDOUT_FILENO, "DEBUG: Aguardando requisição...\n", 33);
        ssize_t bytes_read = read(server_fd, &req, sizeof(Request));
        
        if (bytes_read <= 0) {
            // Log do erro
            char error_msg[256];
            int len = snprintf(error_msg, sizeof(error_msg), 
                           "DEBUG: Erro na leitura do pipe do servidor: %s. Reabrindo...\n", 
                           bytes_read == 0 ? "EOF" : strerror(errno));
            write(STDERR_FILENO, error_msg, len);
            
            // Reopen the pipe if it was closed
            close(server_fd);
            server_fd = open(SERVER_PIPE, O_RDONLY);
            if (server_fd < 0) {
                len = snprintf(error_msg, sizeof(error_msg), 
                               "DEBUG: Erro ao reabrir pipe do servidor: %s\n", strerror(errno));
                write(STDERR_FILENO, error_msg, len);
            } else {
                write(STDOUT_FILENO, "DEBUG: Pipe do servidor reaberto com sucesso\n", 45);
            }
            continue;
        }
        
        // Log da requisição recebida
        len = snprintf(msg, sizeof(msg), 
                   "DEBUG: Recebida requisição de %d bytes do cliente %d\n", 
                   (int)bytes_read, req.client_pid);
        write(STDOUT_FILENO, msg, len);
        
        // Processa a requisição
        resp = process_request(req);
        
        // Abre o pipe do cliente para enviar a resposta
        char client_pipe[64];
        sprintf(client_pipe, CLIENT_PIPE_FORMAT, req.client_pid);
        
        len = snprintf(msg, sizeof(msg), "DEBUG: Tentando abrir FIFO do cliente em: %s\n", client_pipe);
        write(STDOUT_FILENO, msg, len);
        
        int client_fd = open(client_pipe, O_WRONLY);
        if (client_fd < 0) {
            char error_msg[256];
            int len = snprintf(error_msg, sizeof(error_msg), 
                           "DEBUG: Erro ao abrir pipe do cliente para escrita: %s\n", strerror(errno));
            write(STDERR_FILENO, error_msg, len);
            continue;
        }
        
        write(STDOUT_FILENO, "DEBUG: FIFO do cliente aberto com sucesso\n", 42);
        
        // Envia a resposta para o cliente
        ssize_t bytes_written = write(client_fd, &resp, sizeof(Response));
        
        if (bytes_written != sizeof(Response)) {
            char error_msg[256];
            int len = snprintf(error_msg, sizeof(error_msg), 
                           "DEBUG: Erro ao escrever no pipe do cliente: %s\n", strerror(errno));
            write(STDERR_FILENO, error_msg, len);
        } else {
            len = snprintf(msg, sizeof(msg), "DEBUG: Enviados %d bytes para o cliente %d\n", 
                       (int)bytes_written, req.client_pid);
            write(STDOUT_FILENO, msg, len);
        }
        
        // Fecha o pipe do cliente
        close(client_fd);
        write(STDOUT_FILENO, "DEBUG: FIFO do cliente fechado\n", 31);
        
        // Se for um pedido de encerramento, para o servidor
        if (req.operation == SHUTDOWN) {
            running = 0;
            write(STDOUT_FILENO, "DEBUG: Recebido comando para encerrar o servidor\n", 49);
        }
    }
    
    // Fecha o pipe do servidor
    close(server_fd);
    unlink(SERVER_PIPE);
    
    write(STDOUT_FILENO, "DEBUG: FIFO do servidor fechado e removido\n", 43);
    
    // Libera a memória dos documentos na cache
    for (int i = 0; i < cache.num_docs; i++) {
        free(cache.docs[i]);
    }
    
    write(STDOUT_FILENO, "DEBUG: Memória da cache liberada\n", 33);
    write(STDOUT_FILENO, "DEBUG: Servidor encerrado com sucesso\n", 39);
    
    return 0;
}