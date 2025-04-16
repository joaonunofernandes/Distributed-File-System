#include "Document_Struct.h"

// Função para enviar um pedido ao servidor e receber a resposta
Response send_request(Request req) {
    Response resp;
    memset(&resp, 0, sizeof(Response));
    
    // Abre o pipe do servidor para escrita
    int server_fd = open(SERVER_PIPE, O_WRONLY);
    if (server_fd < 0) {
        write(STDERR_FILENO, "Erro ao abrir pipe do servidor. O servidor está rodando?\n", 57);
        exit(1);
    }
    
    // Cria o pipe do cliente
    char client_pipe[64];
    sprintf(client_pipe, CLIENT_PIPE_FORMAT, getpid());
    unlink(client_pipe);
    
    if (mkfifo(client_pipe, 0666) < 0) {
        write(STDERR_FILENO, "Erro ao criar pipe do cliente\n", 30);
        close(server_fd);
        exit(1);
    }
    
    // Adiciona o PID do cliente na requisição
    req.client_pid = getpid();
    
    // Envia a requisição para o servidor
    write(server_fd, &req, sizeof(Request));
    close(server_fd);
    
    // Abre o pipe do cliente para leitura
    int client_fd = open(client_pipe, O_RDONLY);
    if (client_fd < 0) {
        write(STDERR_FILENO, "Erro ao abrir pipe do cliente para leitura\n", 43);
        unlink(client_pipe);
        exit(1);
    }
    
    // Lê a resposta do servidor
    read(client_fd, &resp, sizeof(Response));
    
    // Fecha e remove o pipe do cliente
    close(client_fd);
    unlink(client_pipe);
    
    return resp;
}

// Imprime uma mensagem de uso
void print_usage() {
    write(STDERR_FILENO, "Uso:\n", 5);
    write(STDERR_FILENO, "  ./dclient -a \"título\" \"autores\" \"ano\" \"caminho\" \t(Adicionar documento)\n", 70);
    write(STDERR_FILENO, "  ./dclient -c \"id\" \t\t\t\t(Consultar documento)\n", 48);
    write(STDERR_FILENO, "  ./dclient -d \"id\" \t\t\t\t(Remover documento)\n", 46);
    write(STDERR_FILENO, "  ./dclient -l \"id\" \"palavra-chave\" \t\t(Contar linhas com palavra-chave)\n", 73);
    write(STDERR_FILENO, "  ./dclient -s \"palavra-chave\" [nr_processos] \t(Buscar documentos com palavra-chave)\n", 84);
    write(STDERR_FILENO, "  ./dclient -f \t\t\t\t\t(Encerrar servidor)\n", 47);
}

// Função principal
int main(int argc, char* argv[]) {
    // Verifica se há argumentos suficientes
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    // Prepara a requisição
    Request req;
    memset(&req, 0, sizeof(Request));
    req.nr_processes = 1; // Default: 1 processo
    
    // Processa os argumentos
    if (strcmp(argv[1], "-a") == 0) {
        // Adicionar documento
        if (argc < 6) {
            print_usage();
            return 1;
        }
        
        req.operation = ADD_DOC;
        strncpy(req.doc.title, argv[2], MAX_TITLE_SIZE - 1);
        strncpy(req.doc.authors, argv[3], MAX_AUTHORS_SIZE - 1);
        strncpy(req.doc.year, argv[4], MAX_YEAR_SIZE - 1);
        strncpy(req.doc.path, argv[5], MAX_PATH_SIZE - 1);
        
        // Envia a requisição e recebe a resposta
        Response resp = send_request(req);
        
        if (resp.status == 0) {
            char msg[64];
            int len = snprintf(msg, sizeof(msg), "Document %d indexed\n", resp.doc.id);
            write(STDOUT_FILENO, msg, len);
        } else {
            write(STDERR_FILENO, "Erro ao adicionar documento\n", 28);
            return 1;
        }
    }
    else if (strcmp(argv[1], "-c") == 0) {
        // Consultar documento
        if (argc < 3) {
            print_usage();
            return 1;
        }
        
        req.operation = QUERY_DOC;
        req.doc.id = atoi(argv[2]);
        
        // Envia a requisição e recebe a resposta
        Response resp = send_request(req);
        
        if (resp.status == 0) {
            char msg[512];
            int len = snprintf(msg, sizeof(msg), 
                        "Title: %s\nAuthors: %s\nYear: %s\nPath: %s\n",
                        resp.doc.title, resp.doc.authors, resp.doc.year, resp.doc.path);
            write(STDOUT_FILENO, msg, len);
        } else {
            write(STDERR_FILENO, "Documento não encontrado\n", 26);
            return 1;
        }
    }
    else if (strcmp(argv[1], "-d") == 0) {
        // Remover documento
        if (argc < 3) {
            print_usage();
            return 1;
        }
        
        req.operation = DELETE_DOC;
        req.doc.id = atoi(argv[2]);
        
        // Envia a requisição e recebe a resposta
        Response resp = send_request(req);
        
        if (resp.status == 0) {
            char msg[64];
            int len = snprintf(msg, sizeof(msg), "Index entry %d deleted\n", req.doc.id);
            write(STDOUT_FILENO, msg, len);
        } else {
            write(STDERR_FILENO, "Erro ao remover documento\n", 26);
            return 1;
        }
    }
    else if (strcmp(argv[1], "-l") == 0) {
        // Contar linhas com palavra-chave
        if (argc < 4) {
            print_usage();
            return 1;
        }
        
        req.operation = COUNT_LINES;
        req.doc.id = atoi(argv[2]);
        strncpy(req.keyword, argv[3], MAX_KEYWORD_SIZE - 1);
        
        // Envia a requisição e recebe a resposta
        Response resp = send_request(req);
        
        if (resp.status == 0) {
            char msg[32];
            int len = snprintf(msg, sizeof(msg), "%d\n", resp.count);
            write(STDOUT_FILENO, msg, len);
        } else {
            write(STDERR_FILENO, "Erro ao contar linhas\n", 22);
            return 1;
        }
    }
    else if (strcmp(argv[1], "-s") == 0) {
        // Buscar documentos com palavra-chave
        if (argc < 3) {
            print_usage();
            return 1;
        }
        
        req.operation = SEARCH_DOCS;
        strncpy(req.keyword, argv[2], MAX_KEYWORD_SIZE - 1);
        
        // Verifica se o número de processos foi especificado
        if (argc > 3) {
            req.nr_processes = atoi(argv[3]);
            if (req.nr_processes <= 0) req.nr_processes = 1;
        }
        
        // Envia a requisição e recebe a resposta
        Response resp = send_request(req);
        
        if (resp.status == 0) {
            // Formata a saída como uma lista de IDs
            char msg[MAX_RESULT_IDS * 10 + 10]; // Considerando até 10 caracteres por ID + delimitadores
            int pos = 0;
            
            pos += snprintf(msg + pos, sizeof(msg) - pos, "[");
            
            for (int i = 0; i < resp.num_ids; i++) {
                if (i > 0) {
                    pos += snprintf(msg + pos, sizeof(msg) - pos, ", ");
                }
                pos += snprintf(msg + pos, sizeof(msg) - pos, "%d", resp.ids[i]);
            }
            
            pos += snprintf(msg + pos, sizeof(msg) - pos, "]\n");
            
            write(STDOUT_FILENO, msg, pos);
        } else {
            write(STDERR_FILENO, "Erro ao buscar documentos\n", 26);
            return 1;
        }
    }
    else if (strcmp(argv[1], "-f") == 0) {
        // Encerrar servidor
        req.operation = SHUTDOWN;
        
        // Envia a requisição e recebe a resposta
        Response resp = send_request(req);
        
        if (resp.status == 0) {
            write(STDOUT_FILENO, "Server is shutting down\n", 24);
        } else {
            write(STDERR_FILENO, "Erro ao encerrar servidor\n", 26);
            return 1;
        }
    }
    else {
        print_usage();
        return 1;
    }
    
    return 0;
}