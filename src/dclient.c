#include "Document_Struct.h" // Inclui as definições das estruturas e constantes partilhadas

/**
 * @brief Envia um pedido (requisição) ao servidor e recebe a resposta correspondente.
 *
 * Esta função é o coração da comunicação do cliente com o servidor. Utiliza dois
 * pipes nomeados (FIFOs) para realizar esta comunicação:
 * 1. FIFO do Servidor (SERVER_PIPE): Usado pelo cliente para enviar o seu pedido ao servidor.
 * É um canal conhecido por todos os clientes e pelo servidor.
 * 2. FIFO do Cliente (client_pipe_XXX): Um FIFO único criado por este cliente, usado
 * pelo servidor para enviar a resposta especificamente a este cliente. O nome
 * inclui o PID (Process ID) do cliente para garantir a sua unicidade.
 *
 * ## Funcionamento Detalhado dos Pipes Nomeados (FIFOs) na Comunicação Cliente-Servidor:
 *
 * ### O que são Pipes Nomeados (FIFOs)?
 * Pipes nomeados, ou FIFOs (First-In, First-Out), são ficheiros especiais no sistema
 * de ficheiros que permitem a comunicação entre processos que não necessitam de
 * ter uma relação de parentesco (como é o caso de pipes anónimos). Funcionam como
 * um tubo: o que é escrito numa extremidade pode ser lido na outra, pela ordem de chegada.
 * São persistentes no sistema de ficheiros (até serem explicitamente removidos),
 * o que permite que processos independentes os encontrem e utilizem através do seu nome.
 *
 * ### Fluxo de Comunicação em `send_request`:
 *
 * 1.  **Abrir o FIFO do Servidor para Escrita (`SERVER_PIPE`):**
 * -   **Porquê?** O cliente precisa de um canal para enviar o seu pedido ao servidor.
 * O `SERVER_PIPE` é o "balcão de atendimento" do servidor, onde todos os clientes
 * enviam os seus pedidos.
 * -   **Onde e Como?** O cliente abre o `SERVER_PIPE` (que já deve ter sido criado
 * pelo servidor) em modo de escrita (`O_WRONLY`).
 * `int server_fd = open(SERVER_PIPE, O_WRONLY);`
 * -   Se esta abertura falhar, geralmente significa que o servidor não está em execução
 * ou que o FIFO não existe.
 *
 * 2.  **Criar o FIFO Específico do Cliente (`client_pipe_PID`):**
 * -   **Porquê?** Após o servidor processar o pedido, ele precisa de enviar uma
 * resposta de volta ao cliente que fez o pedido. Se o servidor escrevesse
 * no `SERVER_PIPE`, todos os clientes poderiam tentar ler, gerando confusão.
 * Assim, cada cliente cria o seu próprio FIFO de resposta, com um nome único
 * (geralmente usando o seu PID - Process ID). O cliente informa o servidor
 * sobre o nome deste FIFO (ou, como neste caso, o servidor constrói o nome
 * do pipe do cliente usando o PID enviado no pedido).
 * -   **Onde e Como?** O cliente gera um nome único para o seu FIFO (ex: `/tmp/client_pipe_1234`).
 * `snprintf(client_pipe, sizeof(client_pipe), CLIENT_PIPE_FORMAT, getpid());`
 * Antes de criar, remove qualquer FIFO com o mesmo nome que possa ter ficado
 * de uma execução anterior (`unlink(client_pipe);`).
 * Depois, cria o FIFO com `mkfifo(client_pipe, 0666);`. As permissões `0666`
 * permitem leitura e escrita pelo proprietário, grupo e outros (o servidor
 * precisará de permissão de escrita).
 *
 * 3.  **Enviar o Pedido ao Servidor:**
 * -   O cliente preenche a estrutura `Request` com os dados da operação e o seu PID.
 * `req.client_pid = getpid();`
 * -   O cliente escreve a estrutura `Request` no `server_fd` (o FIFO do servidor).
 * `write(server_fd, &req, sizeof(Request));`
 *
 * 4.  **Fechar a Extremidade de Escrita do FIFO do Servidor:**
 * -   **Porquê?** O cliente já enviou o seu pedido. Manter a extremidade de escrita
 * aberta desnecessariamente pode ter implicações, especialmente se o servidor
 * espera que todos os escritores fechem o pipe para detetar certas condições.
 * É uma boa prática fechar descritores de ficheiro assim que não são mais necessários.
 * -   **Onde e Como?** `close(server_fd);`
 *
 * 5.  **Abrir o FIFO do Cliente para Leitura:**
 * -   **Porquê?** O cliente agora precisa de esperar e receber a resposta do servidor.
 * Esta resposta virá através do FIFO que o próprio cliente criou.
 * -   **Onde e Como?** O cliente abre o seu `client_pipe` em modo de leitura (`O_RDONLY`).
 * `int client_fd = open(client_pipe, O_RDONLY);`
 * -   **Comportamento Bloqueante:** Esta chamada a `open` para leitura num FIFO é
 * tipicamente bloqueante. O cliente ficará aqui "parado" (bloqueado) até que
 * outro processo (neste caso, o servidor) abra a outra extremidade do mesmo
 * FIFO para escrita. Isto é um mecanismo de sincronização: o cliente espera
 * pacientemente que o servidor esteja pronto para enviar a resposta.
 *
 * 6.  **Ler a Resposta do Servidor:**
 * -   Uma vez que o servidor abriu o `client_pipe` para escrita e enviou a resposta,
 * o cliente pode lê-la. A chamada `read` também é bloqueante; ela espera até
 * que haja dados suficientes no pipe (o tamanho de `Response`) ou que a
 * extremidade de escrita do pipe seja fechada pelo servidor.
 * `read(client_fd, &resp, sizeof(Response));`
 *
 * 7.  **Fechar e Remover o FIFO do Cliente:**
 * -   **Porquê?** A comunicação terminou. O FIFO do cliente já cumpriu o seu propósito
 * para este pedido/resposta específico.
 * -   **Fechar a Extremidade de Leitura:** `close(client_fd);`
 * -   **Remover o FIFO do Sistema de Ficheiros:** `unlink(client_pipe);`
 * Se não for removido, o ficheiro FIFO permanecerá no sistema de ficheiros.
 * É importante limpar estes FIFOs temporários.
 *
 * Esta sequência garante uma comunicação organizada e específica entre um cliente e o servidor.
 *
 * @param req A estrutura `Request` contendo os dados do pedido a ser enviado.
 * @return A estrutura `Response` recebida do servidor.
 */
Response send_request(Request req) {
    Response resp; // Estrutura para armazenar a resposta do servidor.
    memset(&resp, 0, sizeof(Response)); // Inicializa a estrutura de resposta com zeros.

    // 1. Abrir o FIFO (pipe nomeado) do servidor para escrita.
    //    O cliente escreve a sua requisição neste FIFO.
    int server_fd = open(SERVER_PIPE, O_WRONLY);
    if (server_fd < 0) {
        // Se não conseguir abrir, assume que o servidor não está em execução ou há outro erro.
        perror("Erro ao abrir pipe do servidor para escrita (send_request)");
        write(STDERR_FILENO, "O servidor está em execução?\n", strlen("O servidor está em execução?\n"));
        exit(EXIT_FAILURE); // Termina o cliente com erro.
    }

    // 2. Criar o FIFO (pipe nomeado) específico deste cliente para receber a resposta.
    char client_pipe[128]; // Buffer para o nome do FIFO do cliente.
    //    Gera o nome do FIFO usando o PID do cliente para garantir unicidade.
    snprintf(client_pipe, sizeof(client_pipe), CLIENT_PIPE_FORMAT, getpid());
    unlink(client_pipe); // Remove o FIFO se já existir de uma execução anterior (precaução).

    //    Cria o FIFO com permissões de leitura/escrita para o utilizador (e servidor).
    if (mkfifo(client_pipe, 0666) < 0) {
        perror("Erro ao criar pipe do cliente (mkfifo)");
        close(server_fd); // Fecha o FIFO do servidor antes de sair.
        exit(EXIT_FAILURE); // Termina o cliente com erro.
    }

    // 3. Preencher o PID do cliente na requisição.
    //    O servidor usará este PID para saber em que FIFO de cliente deve escrever a resposta.
    req.client_pid = getpid();

    // 4. Enviar a requisição para o servidor através do FIFO do servidor.
    ssize_t bytes_escritos = write(server_fd, &req, sizeof(Request));
    if (bytes_escritos < 0) {
        perror("Erro ao escrever no pipe do servidor");
        close(server_fd);
        unlink(client_pipe);
        exit(EXIT_FAILURE);
    }
    if (bytes_escritos != sizeof(Request)) {
        fprintf(stderr, "Erro: Escrita incompleta para o pipe do servidor. Esperado: %zu, Escrito: %zd\n", sizeof(Request), bytes_escritos);
        close(server_fd);
        unlink(client_pipe);
        exit(EXIT_FAILURE);
    }
    //    Fecha o descritor do FIFO do servidor após a escrita.
    close(server_fd);

    // 5. Abrir o FIFO do cliente para leitura.
    //    O cliente agora espera pela resposta do servidor neste FIFO.
    //    A abertura é bloqueante por defeito, esperando que o servidor abra para escrita.
    int client_fd = open(client_pipe, O_RDONLY);
    if (client_fd < 0) {
        perror("Erro ao abrir pipe do cliente para leitura");
        unlink(client_pipe); // Remove o FIFO do cliente antes de sair.
        exit(EXIT_FAILURE); // Termina o cliente com erro.
    }

    // 6. Ler a resposta do servidor a partir do FIFO do cliente.
    //    A leitura é bloqueante, esperando que o servidor escreva a resposta completa.
    ssize_t bytes_lidos = read(client_fd, &resp, sizeof(Response));
    if (bytes_lidos < 0) {
        perror("Erro ao ler a resposta do servidor (read)");
        close(client_fd);
        unlink(client_pipe);
        exit(EXIT_FAILURE);
    } else if (bytes_lidos != sizeof(Response)) {
        fprintf(stderr, "Erro: Leitura incompleta da resposta do servidor. Esperado: %zu, Lido: %zd\n", sizeof(Response), bytes_lidos);
        fprintf(stderr, "Isto pode indicar que o servidor terminou inesperadamente ou que MAX_RESULT_IDS diverge entre cliente e servidor.\n");
        close(client_fd);
        unlink(client_pipe);
        exit(EXIT_FAILURE);
    }

    // 7. Fechar e remover o FIFO do cliente.
    //    Após receber a resposta, o FIFO já não é necessário.
    close(client_fd);
    unlink(client_pipe);

    // 8. Retornar a resposta recebida do servidor.
    return resp;
}

/**
 * @brief Imprime uma mensagem de ajuda com as opções de uso do cliente para o STDERR.
 *
 * Esta função é chamada quando o cliente é invocado com argumentos inválidos
 * ou sem argumentos suficientes.
 */
void print_usage() {
    char buffer[1024]; // Buffer para construir a mensagem de ajuda.
    int offset = 0;

    // Construir a mensagem de ajuda completa no buffer.
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "Uso:\n");
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "./dclient -a \"título\" \"autores\" \"ano\" \"caminho\" # Adicionar documento\n");
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "./dclient -c ID # Consultar documento por ID\n");
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "./dclient -d ID # Eliminar documento por ID\n");
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "./dclient -l ID \"palavra-chave\" # Contar linhas com palavra-chave num documento\n");
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "./dclient -s \"palavra-chave\" [nr_processos] # Procurar documentos com palavra-chave (opcional: nº processos)\n");
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "./dclient -f # Forçar persistência e encerrar o servidor\n");

    // Escrever a mensagem de ajuda de uma só vez para STDERR.
    write(STDERR_FILENO, buffer, offset);
}

/**
 * @brief Função principal do cliente.
 *
 * Responsável por processar os argumentos da linha de comandos, construir a
 * estrutura de pedido (`Request`), chamar `send_request` para comunicar com o
 * servidor, e apresentar a resposta (`Response`) ao utilizador.
 *
 * @param argc Número de argumentos da linha de comandos.
 * @param argv Array de strings contendo os argumentos da linha de comandos.
 * @return 0 em caso de sucesso, 1 em caso de erro ou uso incorreto.
 */
int main(int argc, char* argv[]) {
    // Verifica se foi fornecido pelo menos um argumento (a opção de operação).
    if (argc < 2) {
        print_usage(); // Mostra a ajuda se não houver argumentos suficientes.
        return 1;      // Retorna erro.
    }

    // Prepara a estrutura da requisição.
    Request req;
    memset(&req, 0, sizeof(Request)); // Inicializa a requisição com zeros.
    req.nr_processes = 1; // Valor por defeito para o número de processos (usado em -s).

    // Processa os argumentos da linha de comandos para determinar a operação e os dados.
    if (strcmp(argv[1], "-a") == 0) { // Operação: Adicionar Documento.
        if (argc != 6) { // Verifica o número correto de argumentos para -a (programa + opção + 4 args).
            print_usage();
            return 1;
        }

        size_t total_len = 0;
        total_len += strlen(argv[2]); // título
        total_len += strlen(argv[3]); // autores
        total_len += strlen(argv[4]); // ano
        total_len += strlen(argv[5]); // caminho

        if (total_len >= MAX_ARGS_TOTAL_SIZE) { // Validação do tamanho total dos argumentos.
            char error_msg[128];
            snprintf(error_msg, sizeof(error_msg),
                    "Erro: Tamanho total dos dados (título, autores, ano, caminho) excede o limite de %d bytes.\n",
                    MAX_ARGS_TOTAL_SIZE);
            write(STDERR_FILENO, error_msg, strlen(error_msg));
            return 1;
        }

        req.operation = ADD_DOC;
        // Copia os dados do documento dos argumentos para a estrutura da requisição.
        // Usa strncpy para evitar buffer overflows, garantindo terminação nula.
        strncpy(req.doc.title, argv[2], MAX_TITLE_SIZE - 1);
        req.doc.title[MAX_TITLE_SIZE - 1] = '\0';

        strncpy(req.doc.authors, argv[3], MAX_AUTHORS_SIZE - 1);
        req.doc.authors[MAX_AUTHORS_SIZE - 1] = '\0';

        strncpy(req.doc.year, argv[4], MAX_YEAR_SIZE - 1);
        req.doc.year[MAX_YEAR_SIZE - 1] = '\0';

        strncpy(req.doc.path, argv[5], MAX_PATH_SIZE - 1);
        req.doc.path[MAX_PATH_SIZE - 1] = '\0';

        Response resp = send_request(req); // Envia a requisição e recebe a resposta.

        if (resp.status == 0) { // Sucesso.
            char msg[64];
            int len = snprintf(msg, sizeof(msg), "Documento %d indexado\n", resp.doc.id);
            write(STDOUT_FILENO, msg, len);
        } else { // Erro (vindo do servidor).
            char error_msg[128];
            switch (resp.status) {
                case -3:
                    snprintf(error_msg, sizeof(error_msg), "Erro do servidor: Ficheiro não encontrado ou inacessível.\n");
                    break;
                case -4:
                    snprintf(error_msg, sizeof(error_msg), "Erro do servidor: Caminho do ficheiro demasiado longo.\n");
                    break;
                case -5:
                    snprintf(error_msg, sizeof(error_msg), "Erro do servidor: Falha interna ao adicionar o documento.\n");
                    break;
                default:
                    snprintf(error_msg, sizeof(error_msg), "Erro %d ao adicionar documento (resposta do servidor).\n", resp.status);
                    break;
            }
            write(STDERR_FILENO, error_msg, strlen(error_msg));
            return 1;
        }
    }
    else if (strcmp(argv[1], "-c") == 0) { // Operação: Consultar Documento.
        if (argc != 3) { // programa + opção + ID.
            print_usage();
            return 1;
        }

        req.operation = QUERY_DOC;
        req.doc.id = atoi(argv[2]); // Converte o ID (string) para inteiro.

        Response resp = send_request(req);

        if (resp.status == 0) { // Sucesso (documento encontrado).
            char msg[MAX_TITLE_SIZE + MAX_AUTHORS_SIZE + MAX_YEAR_SIZE + MAX_PATH_SIZE + 100]; // Buffer para a mensagem.
            int len = snprintf(msg, sizeof(msg),
                        "Título: %s\nAutores: %s\nAno: %s\nCaminho: %s\n",
                        resp.doc.title, resp.doc.authors, resp.doc.year, resp.doc.path);
            write(STDOUT_FILENO, msg, len);
        } else { // Erro (documento não encontrado).
            write(STDERR_FILENO, "Documento não encontrado.\n", strlen("Documento não encontrado.\n"));
            return 1;
        }
    }
    else if (strcmp(argv[1], "-d") == 0) { // Operação: Eliminar Documento.
        if (argc != 3) { // programa + opção + ID.
            print_usage();
            return 1;
        }

        req.operation = DELETE_DOC;
        req.doc.id = atoi(argv[2]);

        Response resp = send_request(req);

        if (resp.status == 0) { // Sucesso.
            char msg[64];
            int len = snprintf(msg, sizeof(msg), "Entrada de índice %d eliminada.\n", req.doc.id);
            write(STDOUT_FILENO, msg, len);
        } else { // Erro (documento não encontrado ou erro ao eliminar).
            write(STDERR_FILENO, "Erro ao remover documento.\n", strlen("Erro ao remover documento.\n"));
            return 1;
        }
    }
    else if (strcmp(argv[1], "-l") == 0) { // Operação: Contar Linhas.
        if (argc != 4) { // programa + opção + ID + palavra-chave.
            print_usage();
            return 1;
        }

        req.operation = COUNT_LINES;
        req.doc.id = atoi(argv[2]);
        strncpy(req.keyword, argv[3], MAX_KEYWORD_SIZE - 1);
        req.keyword[MAX_KEYWORD_SIZE - 1] = '\0'; // Garante terminação nula.

        Response resp = send_request(req);

        if (resp.status == 0) { // Sucesso.
            char msg[32];
            int len = snprintf(msg, sizeof(msg), "%d\n", resp.count);
            write(STDOUT_FILENO, msg, len);
        } else { // Erro (documento não encontrado ou erro na contagem).
            write(STDERR_FILENO, "Erro ao contar linhas.\n", strlen("Erro ao contar linhas.\n"));
            return 1;
        }
    }
    else if (strcmp(argv[1], "-s") == 0) { // Operação: Procurar Documentos.
        if (argc < 3 || argc > 4) { // Mínimo: prog + opção + keyword. Máximo: prog + opção + keyword + nr_procs.
            print_usage();
            return 1;
        }

        req.operation = SEARCH_DOCS;
        strncpy(req.keyword, argv[2], MAX_KEYWORD_SIZE - 1);
        req.keyword[MAX_KEYWORD_SIZE - 1] = '\0';

        if (argc == 4) { // Número de processos foi especificado.
            req.nr_processes = atoi(argv[3]);
            if (req.nr_processes <= 0) req.nr_processes = 1; // Garante pelo menos 1 processo.
        }
        // Se argc == 3, req.nr_processes mantém o valor por defeito (1).

        Response resp = send_request(req);

        if (resp.status == 0) { // Sucesso.
            // Formata a saída como uma lista de IDs [id1, id2, ...].
            // (MAX_RESULT_IDS * (10 digitos + 2 para ", ")) + 3 para "[]\n" + margem.
            char msg[MAX_RESULT_IDS * 12 + 10];
            int pos = 0; // Posição atual no buffer msg.

            pos += snprintf(msg + pos, sizeof(msg) - pos, "[");
            for (int i = 0; i < resp.num_ids; i++) {
                if (i > 0) {
                    pos += snprintf(msg + pos, sizeof(msg) - pos, ", ");
                }
                pos += snprintf(msg + pos, sizeof(msg) - pos, "%d", resp.ids[i]);
            }
            pos += snprintf(msg + pos, sizeof(msg) - pos, "]\n");

            write(STDOUT_FILENO, msg, pos);
        } else { // Erro.
            write(STDERR_FILENO, "Erro ao procurar documentos.\n", strlen("Erro ao procurar documentos.\n"));
            return 1;
        }
    }
    else if (strcmp(argv[1], "-f") == 0) { // Operação: Encerrar Servidor (com persistência).
         if (argc != 2) { // Apenas programa + opção -f.
            print_usage();
            return 1;
        }
        req.operation = SHUTDOWN;

        Response resp = send_request(req);

        if (resp.status == 0) { // Sucesso.
            write(STDOUT_FILENO, "Servidor está a encerrar...\n", strlen("Servidor está a encerrar...\n"));
        } else { // Erro.
            write(STDERR_FILENO, "Erro ao enviar pedido para encerrar o servidor.\n", strlen("Erro ao enviar pedido para encerrar o servidor.\n"));
            return 1;
        }
    }
    else { // Opção inválida.
        print_usage();
        return 1;
    }

    return 0; // Retorna 0 indicando sucesso na execução do cliente.
}