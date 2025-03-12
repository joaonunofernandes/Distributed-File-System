#include "Document_Struct.h"

// Estrutura para armazenar os documentos em memória
Document* documents[MAX_DOCS];
int num_documents = 0;
char base_folder[256];

// Função para salvar os documentos em disco
void save_documents() {
    int fd = open("database.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Error opening database file");
        return;
    }
    
    // Escreve a quantidade de documentos
    write(fd, &num_documents, sizeof(int));
    
    // Escreve cada documento
    for (int i = 0; i < num_documents; i++) {
        write(fd, documents[i], sizeof(Document));
    }
    
    close(fd);
}

// Função para carregar os documentos do disco
void load_documents() {
    int fd = open("database.bin", O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            printf("No database file found. Starting with empty database.\n");
        } else {
            perror("Error opening database file");
        }
        return;
    }
    
    // Lê a quantidade de documentos
    read(fd, &num_documents, sizeof(int));
    
    // Lê cada documento
    for (int i = 0; i < num_documents; i++) {
        documents[i] = malloc(sizeof(Document));
        read(fd, documents[i], sizeof(Document));
    }
    
    close(fd);
}

// Função para adicionar um documento
int add_document(Document* doc) {
    // Atribui um ID único (simples incremento)
    doc->id = num_documents + 1;
    
    // Aloca memória e copia o documento
    documents[num_documents] = malloc(sizeof(Document));
    memcpy(documents[num_documents], doc, sizeof(Document));
    
    num_documents++;
    
    // Salva os documentos em disco
    save_documents();
    
    return doc->id;
}

// Função para buscar um documento pelo ID
Document* find_document(int id) {
    for (int i = 0; i < num_documents; i++) {
        if (documents[i]->id == id) {
            return documents[i];
        }
    }
    return NULL;
}

// Função para remover um documento pelo ID
int remove_document(int id) {
    int index = -1;
    
    // Encontra o índice do documento
    for (int i = 0; i < num_documents; i++) {
        if (documents[i]->id == id) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        return -1; // Documento não encontrado
    }
    
    // Libera a memória do documento
    free(documents[index]);
    
    // Move os documentos restantes
    for (int i = index; i < num_documents - 1; i++) {
        documents[i] = documents[i + 1];
    }
    
    num_documents--;
    
    // Salva os documentos em disco
    save_documents();
    
    return 0;
}

// Função para contar linhas com uma palavra-chave
int count_lines_with_keyword(Document* doc, char* keyword) {
    char full_path[512];
    sprintf(full_path, "%s/%s", base_folder, doc->path);
    
    // Cria os pipes para comunicação com o grep e wc
    int grep_to_wc[2];
    pipe(grep_to_wc);
    
    pid_t pid_grep = fork();
    
    if (pid_grep == 0) { // Processo filho para o grep
        close(grep_to_wc[0]); // Fecha a leitura do pipe
        
        // Redireciona a saída para o pipe
        dup2(grep_to_wc[1], STDOUT_FILENO);
        close(grep_to_wc[1]);
        
        // Executa o grep
        execlp("grep", "grep", "-w", keyword, full_path, NULL);
        perror("grep exec failed");
        exit(1);
    }
    
    close(grep_to_wc[1]); // Fecha a escrita do pipe no processo pai
    
    pid_t pid_wc = fork();
    
    if (pid_wc == 0) { // Processo filho para o wc
        // Redireciona a entrada do pipe
        dup2(grep_to_wc[0], STDIN_FILENO);
        close(grep_to_wc[0]);
        
        // Cria um pipe para capturar a saída do wc
        int pipe_out[2];
        pipe(pipe_out);
        
        // Redireciona a saída para o pipe
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_out[1]);
        
        // Executa o wc para contar linhas
        execlp("wc", "wc", "-l", NULL);
        perror("wc exec failed");
        exit(1);
    }
    
    close(grep_to_wc[0]); // Fecha a leitura do pipe no processo pai
    
    // Espera pelos processos filhos
    int status;
    waitpid(pid_grep, &status, 0);
    waitpid(pid_wc, &status, 0);
    
    // Lê o resultado do wc (o número de linhas)
    char result[20];
    int pipe_result[2];
    pipe(pipe_result);
    
    pid_t pid_cat = fork();
    
    if (pid_cat == 0) {
        dup2(pipe_result[1], STDOUT_FILENO);
        close(pipe_result[0]);
        close(pipe_result[1]);
        
        execlp("cat", "cat", NULL);
        perror("cat exec failed");
        exit(1);
    }
    
    close(pipe_result[1]);
    read(pipe_result[0], result, sizeof(result));
    close(pipe_result[0]);
    
    waitpid(pid_cat, &status, 0);
    
    // Converte para inteiro e retorna
    return atoi(result);
}

// Função para buscar documentos com uma palavra-chave
int search_documents_with_keyword(char* keyword, int* result_ids) {
    int count = 0;
    
    for (int i = 0; i < num_documents; i++) {
        char full_path[512];
        sprintf(full_path, "%s/%s", base_folder, documents[i]->path);
        
        // Cria um pipe para comunicação
        int pipe_fd[2];
        pipe(pipe_fd);
        
        pid_t pid = fork();
        
        if (pid == 0) { // Processo filho
            close(pipe_fd[0]); // Fecha a leitura do pipe
            
            // Redireciona a saída para o pipe
            dup2(pipe_fd[1], STDOUT_FILENO);
            close(pipe_fd[1]);
            
            // Executa o grep com a opção -q (quiet)
            execlp("grep", "grep", "-q", "-w", keyword, full_path, NULL);
            perror("grep exec failed");
            exit(1);
        }
        
        close(pipe_fd[1]); // Fecha a escrita do pipe no processo pai
        
        // Espera pelo processo filho
        int status;
        waitpid(pid, &status, 0);
        
        // Se o grep encontrou a palavra (código de saída 0)
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            result_ids[count] = documents[i]->id;
            count++;
        }
    }
    
    return count;
}

// Função principal para processar uma requisição
Response process_request(Request req) {
    Response resp;
    memset(&resp, 0, sizeof(Response));
    
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
            resp.num_ids = search_documents_with_keyword(req.keyword, resp.ids);
            resp.status = 0;
            break;
            
        case SHUTDOWN:
            resp.status = 0;
            break;
            
        default:
            resp.status = -2; // Operação inválida
    }
    
    return resp;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s document_folder [cache_size]\n", argv[0]);
        return 1;
    }
    
    // Armazena a pasta base dos documentos
    strcpy(base_folder, argv[1]);
    
    // Carrega os documentos do disco
    load_documents();
    
    // Cria o pipe do servidor
    unlink(SERVER_PIPE);
    if (mkfifo(SERVER_PIPE, 0666) < 0) {
        perror("Error creating server pipe");
        return 1;
    }
    
    printf("Server started. Waiting for connections...\n");
    
    int server_fd = open(SERVER_PIPE, O_RDONLY);
    if (server_fd < 0) {
        perror("Error opening server pipe for reading");
        return 1;
    }
    
    int running = 1;
    
    while (running) {
        Request req;
        Response resp;
        
        // Lê a requisição do cliente
        ssize_t bytes_read = read(server_fd, &req, sizeof(Request));
        
        if (bytes_read <= 0) {
            // Reopen the pipe if it was closed
            close(server_fd);
            server_fd = open(SERVER_PIPE, O_RDONLY);
            continue;
        }
        
        // Processa a requisição
        resp = process_request(req);
        
        // Abre o pipe do cliente para enviar a resposta
        char client_pipe[64];
        sprintf(client_pipe, CLIENT_PIPE_FORMAT, getpid());
        
        int client_fd = open(client_pipe, O_WRONLY);
        if (client_fd < 0) {
            perror("Error opening client pipe for writing");
            continue;
        }
        
        // Envia a resposta para o cliente
        write(client_fd, &resp, sizeof(Response));
        
        // Fecha o pipe do cliente
        close(client_fd);
        
        // Se for um pedido de encerramento, para o servidor
        if (req.operation == SHUTDOWN) {
            running = 0;
        }
    }
    
    // Fecha o pipe do servidor
    close(server_fd);
    unlink(SERVER_PIPE);
    
    // Libera a memória dos documentos
    for (int i = 0; i < num_documents; i++) {
        free(documents[i]);
    }
    
    return 0;
}
