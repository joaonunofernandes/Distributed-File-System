#include "Document_Struct.h" // Inclui as definições das estruturas e constantes partilhadas

// Função para enviar um pedido ao servidor e receber a resposta correspondente
Response send_request(Request req) {
    Response resp; // Estrutura para armazenar a resposta do servidor
    memset(&resp, 0, sizeof(Response)); // Inicializa a estrutura de resposta com zeros

    // 1. Abrir o FIFO (pipe nomeado) do servidor para escrita
    //    O cliente escreve a sua requisição neste FIFO.
    int server_fd = open(SERVER_PIPE, O_WRONLY);
    if (server_fd < 0) {
        // Se não conseguir abrir, assume que o servidor não está a correr ou há outro erro.
        write(STDERR_FILENO, "Erro ao abrir pipe do servidor. O servidor está em execução?\n", strlen("Erro ao abrir pipe do servidor. O servidor está em execução?\n"));
        exit(1); // Termina o cliente com erro
    }

    // 2. Criar o FIFO (pipe nomeado) específico deste cliente para receber a resposta
    char client_pipe[64]; // Buffer para o nome do FIFO do cliente
    //    Gera o nome do FIFO usando o PID do cliente para garantir unicidade.
    sprintf(client_pipe, CLIENT_PIPE_FORMAT, getpid());
    unlink(client_pipe); // Remove o FIFO se já existir de uma execução anterior (precaução)

    //    Cria o FIFO com permissões de leitura/escrita para o utilizador, grupo e outros.
    if (mkfifo(client_pipe, 0666) < 0) {
        write(STDERR_FILENO, "Erro ao criar pipe do cliente\n", strlen("Erro ao criar pipe do cliente\n"));
        close(server_fd); // Fecha o FIFO do servidor antes de sair
        exit(1); // Termina o cliente com erro
    }

    // 3. Preencher o PID do cliente na requisição
    //    O servidor usará este PID para saber em que FIFO de cliente deve escrever a resposta.
    req.client_pid = getpid();

    // 4. Enviar a requisição para o servidor através do FIFO do servidor
    write(server_fd, &req, sizeof(Request));
    //    Fecha o descritor do FIFO do servidor após a escrita.
    close(server_fd);

    // 5. Abrir o FIFO do cliente para leitura
    //    O cliente agora espera pela resposta do servidor neste FIFO.
    //    A abertura é bloqueante por defeito, esperando que o servidor abra para escrita.
    int client_fd = open(client_pipe, O_RDONLY);
    if (client_fd < 0) {
        write(STDERR_FILENO, "Erro ao abrir pipe do cliente para leitura\n", strlen("Erro ao abrir pipe do cliente para leitura\n"));
        unlink(client_pipe); // Remove o FIFO do cliente antes de sair
        exit(1); // Termina o cliente com erro
    }

    // 6. Ler a resposta do servidor a partir do FIFO do cliente
    //    A leitura é bloqueante, esperando que o servidor escreva a resposta completa.
    read(client_fd, &resp, sizeof(Response));

    // 7. Fechar e remover o FIFO do cliente
    //    Após receber a resposta, o FIFO já não é necessário.
    close(client_fd);
    unlink(client_pipe);

    // 8. Retornar a resposta recebida do servidor
    return resp;
}

// Imprime uma mensagem de ajuda com as opções de uso do cliente
void print_usage() {
    write(STDERR_FILENO, "Uso:\n", strlen("Uso:\n"));
    write(STDERR_FILENO, "./dclient -a \"título\" \"autores\" \"ano\" \"caminho\" # Adicionar documento\n", strlen("./dclient -a \"título\" \"autores\" \"ano\" \"caminho\" # Adicionar documento\n"));
    write(STDERR_FILENO, "./dclient -c ID # Consultar documento por ID\n", strlen("./dclient -c ID # Consultar documento por ID\n"));
    write(STDERR_FILENO, "./dclient -d ID # Eliminar documento por ID\n", strlen("./dclient -d ID # Eliminar documento por ID\n"));
    write(STDERR_FILENO, "./dclient -l ID \"palavra-chave\" # Contar linhas com palavra-chave num documento\n", strlen("./dclient -l ID \"palavra-chave\" # Contar linhas com palavra-chave num documento\n"));
    write(STDERR_FILENO, "./dclient -s \"palavra-chave\" [nr_processos] # Procurar documentos com palavra-chave (opcional: nº processos)\n", strlen("./dclient -s \"palavra-chave\" [nr_processos] # Procurar documentos com palavra-chave (opcional: nº processos)\n"));
    write(STDERR_FILENO, "./dclient -f # Forçar persistência e encerrar o servidor\n", strlen("./dclient -f # Forçar persistência e encerrar o servidor\n"));
}

// Função principal do cliente
int main(int argc, char* argv[]) {
    // Verifica se foi fornecido pelo menos um argumento (a opção de operação)
    if (argc < 2) {
        print_usage(); // Mostra a ajuda se não houver argumentos suficientes
        return 1;      // Retorna erro
    }

    // Prepara a estrutura da requisição
    Request req;
    memset(&req, 0, sizeof(Request)); // Inicializa a requisição com zeros
    req.nr_processes = 1; // Valor por defeito para o número de processos (usado em -s)

    // Processa os argumentos da linha de comandos para determinar a operação e os dados
    if (strcmp(argv[1], "-a") == 0) { // Operação: Adicionar Documento
        // Verifica se o número de argumentos está correto para -a
        if (argc < 6 || argc >= 7) {
            print_usage();
            return 1;
        }

        req.operation = ADD_DOC; // Define a operação
        // Copia os dados do documento dos argumentos para a estrutura da requisição
        // Usa strncpy para evitar buffer overflows
        strncpy(req.doc.title, argv[2], MAX_TITLE_SIZE - 1);
        strncpy(req.doc.authors, argv[3], MAX_AUTHORS_SIZE - 1);
        strncpy(req.doc.year, argv[4], MAX_YEAR_SIZE - 1);
        req.doc.year[MAX_YEAR_SIZE - 1] = '\0'; // Garante terminação nula para o ano
        strncpy(req.doc.path, argv[5], MAX_PATH_SIZE - 1);

        // Envia a requisição e recebe a resposta
        Response resp = send_request(req);

        // Verifica o estado da resposta
        if (resp.status == 0) { // Sucesso
            char msg[64];
            // Imprime a mensagem de sucesso com o ID atribuído pelo servidor
            int len = snprintf(msg, sizeof(msg), "Documento %d indexado\n", resp.doc.id);
            write(STDOUT_FILENO, msg, len);
        } else { // Erro
            write(STDERR_FILENO, "Erro ao adicionar documento\n", strlen("Erro ao adicionar documento\n"));
            return 1; // Retorna erro
        }
    }
    else if (strcmp(argv[1], "-c") == 0) { // Operação: Consultar Documento
        // Verifica se o número de argumentos está correto para -c
        if (argc < 3 || argc >= 4) {
            print_usage();
            return 1;
        }

        req.operation = QUERY_DOC; // Define a operação
        req.doc.id = atoi(argv[2]); // Converte o ID (string) para inteiro

        // Envia a requisição e recebe a resposta
        Response resp = send_request(req);

        // Verifica o estado da resposta
        if (resp.status == 0) { // Sucesso (documento encontrado)
            char msg[512]; // Buffer para a mensagem formatada
            // Formata e imprime os detalhes do documento recebido
            int len = snprintf(msg, sizeof(msg),
                        "Título: %s\nAutores: %s\nAno: %s\nCaminho: %s\n",
                        resp.doc.title, resp.doc.authors, resp.doc.year, resp.doc.path);
            write(STDOUT_FILENO, msg, len);
        } else { // Erro (documento não encontrado)
            write(STDERR_FILENO, "Documento não encontrado\n", strlen("Documento não encontrado\n"));
            return 1; // Retorna erro
        }
    }
    else if (strcmp(argv[1], "-d") == 0) { // Operação: Eliminar Documento
        // Verifica se o número de argumentos está correto para -d
        if (argc < 3 || argc >= 4) {
            print_usage();
            return 1;
        }

        req.operation = DELETE_DOC; // Define a operação
        req.doc.id = atoi(argv[2]); // Converte o ID (string) para inteiro

        // Envia a requisição e recebe a resposta
        Response resp = send_request(req);

        // Verifica o estado da resposta
        if (resp.status == 0) { // Sucesso
            char msg[64];
            // Imprime a mensagem de sucesso indicando o ID eliminado
            int len = snprintf(msg, sizeof(msg), "Entrada de índice %d eliminada\n", req.doc.id);
            write(STDOUT_FILENO, msg, len);
        } else { // Erro (documento não encontrado ou erro ao eliminar)
            write(STDERR_FILENO, "Erro ao remover documento\n", strlen("Erro ao remover documento\n"));
            return 1; // Retorna erro
        }
    }
    else if (strcmp(argv[1], "-l") == 0) { // Operação: Contar Linhas
        // Verifica se o número de argumentos está correto para -l
        if (argc < 4 || argc >= 5) {
            print_usage();
            return 1;
        }

        req.operation = COUNT_LINES; // Define a operação
        req.doc.id = atoi(argv[2]); // Converte o ID (string) para inteiro
        // Copia a palavra-chave do argumento para a estrutura da requisição
        strncpy(req.keyword, argv[3], MAX_KEYWORD_SIZE - 1);

        // Envia a requisição e recebe a resposta
        Response resp = send_request(req);

        // Verifica o estado da resposta
        if (resp.status == 0) { // Sucesso
            char msg[32];
            // Imprime a contagem de linhas recebida do servidor
            int len = snprintf(msg, sizeof(msg), "%d\n", resp.count);
            write(STDOUT_FILENO, msg, len);
        } else { // Erro (documento não encontrado ou erro na contagem)
            write(STDERR_FILENO, "Erro ao contar linhas\n", strlen("Erro ao contar linhas\n"));
            return 1; // Retorna erro
        }
    }
    else if (strcmp(argv[1], "-s") == 0) { // Operação: Procurar Documentos
        // Verifica se o número de argumentos está correto para -s (mínimo 3)
        if (argc < 3 || argc >= 5) {
            print_usage();
            return 1;
        }

        req.operation = SEARCH_DOCS; // Define a operação
        // Copia a palavra-chave do argumento para a estrutura da requisição
        strncpy(req.keyword, argv[2], MAX_KEYWORD_SIZE - 1);

        // Verifica se o número de processos foi especificado (argumento opcional)
        if (argc > 3 && argc <5) {
            req.nr_processes = atoi(argv[3]); // Converte o número de processos para inteiro
            // Garante que o número de processos é pelo menos 1
            if (req.nr_processes <= 0) req.nr_processes = 1;
        }

        // Envia a requisição e recebe a resposta
        Response resp = send_request(req);

        // Verifica o estado da resposta
        if (resp.status == 0) { // Sucesso
            // Formata a saída como uma lista de IDs [id1, id2, ...]
            char msg[MAX_RESULT_IDS * 12 + 10]; // Buffer generoso (10 digitos/ID + ', ' + '[' + ']' + '\n')
            int pos = 0; // Posição atual no buffer msg

            // Inicia a string com '['
            pos += snprintf(msg + pos, sizeof(msg) - pos, "[");

            // Itera sobre os IDs recebidos na resposta
            for (int i = 0; i < resp.num_ids; i++) {
                // Adiciona ", " antes do ID, exceto para o primeiro
                if (i > 0) {
                    pos += snprintf(msg + pos, sizeof(msg) - pos, ", ");
                }
                // Adiciona o ID atual à string
                pos += snprintf(msg + pos, sizeof(msg) - pos, "%d", resp.ids[i]);
            }

            // Termina a string com "]\n"
            pos += snprintf(msg + pos, sizeof(msg) - pos, "]\n");

            // Imprime a lista formatada de IDs
            write(STDOUT_FILENO, msg, pos);
        } else { // Erro
            write(STDERR_FILENO, "Erro ao procurar documentos\n", strlen("Erro ao procurar documentos\n"));
            return 1; // Retorna erro
        }
    }
    else if (strcmp(argv[1], "-f") == 0) { // Operação: Encerrar Servidor (com persistência)
        req.operation = SHUTDOWN; // Define a operação

        // Envia a requisição e recebe a resposta
        Response resp = send_request(req);

        // Verifica o estado da resposta
        if (resp.status == 0) { // Sucesso
            write(STDOUT_FILENO, "Servidor está a encerrar\n", strlen("Servidor está a encerrar\n"));
        } else { // Erro
            write(STDERR_FILENO, "Erro ao encerrar servidor\n", strlen("Erro ao encerrar servidor\n"));
            return 1; // Retorna erro
        }
    }
    else { // Opção inválida
        print_usage(); // Mostra a ajuda se a opção não for reconhecida
        return 1;      // Retorna erro
    }

    return 0; // Retorna 0 indicando sucesso na execução do cliente
}