#ifndef DOCUMENT_H
#define DOCUMENT_H

// Inclusão de bibliotecas standard necessárias para as definições e operações.
#include <stdio.h>      // Para funções de entrada/saída (ex: snprintf, perror)
#include <stdlib.h>     // Para funções utilitárias gerais (ex: atoi, exit, malloc)
#include <string.h>     // Para manipulação de strings (ex: strcmp, strcpy, strlen, memset)
#include <fcntl.h>      // Para controlo de ficheiros (ex: open, O_WRONLY, O_RDONLY)
#include <unistd.h>     // Para chamadas de sistema POSIX (ex: read, write, close, fork, getpid, unlink, access)
#include <sys/types.h>  // Para tipos de dados primitivos do sistema (ex: pid_t, ssize_t)
#include <sys/stat.h>   // Para informações de ficheiros (ex: mkfifo, struct stat)
#include <sys/wait.h>   // Para esperar por processos filho (ex: waitpid)
#include <errno.h>      // Para códigos de erro (ex: errno)
#include <time.h>       // Para funções de tempo (não usado ativamente na lógica FCFS atual)
#include <signal.h>     // Para manipulação de sinais (ex: signal, SIGINT)
#include <limits.h>     // Para limites de tipos de dados (ex: PATH_MAX, se necessário)

// --- Constantes para Tamanhos Máximos ---
// Estas constantes definem os limites de tamanho para vários campos de dados
// e para estruturas internas, ajudando a prevenir estouros de buffer (buffer overflows).

#define MAX_TITLE_SIZE 200      // Tamanho máximo para o título de um documento (bytes).
#define MAX_AUTHORS_SIZE 200    // Tamanho máximo para o(s) autor(es) de um documento (bytes).
#define MAX_YEAR_SIZE 5         // Tamanho máximo para o ano de publicação (4 caracteres + terminador nulo '\0').
#define MAX_PATH_SIZE 64        // Tamanho máximo para o caminho relativo do ficheiro do documento (bytes).
#define MAX_KEYWORD_SIZE 64     // Tamanho máximo para uma palavra-chave de pesquisa (bytes).
#define MAX_DOCS 1500           // Número máximo de documentos que podem ser geridos (na cache e/ou no disco).
#define MAX_RESULT_IDS 1500     // Número máximo de IDs de documentos retornados numa operação de pesquisa.
#define MAX_ARGS_TOTAL_SIZE 512 // Tamanho total máximo combinado dos argumentos para a operação de adicionar documento (-a).

// --- Códigos de Operação Cliente-Servidor ---
// Estes códigos identificam o tipo de operação que o cliente solicita ao servidor.
// São usados no campo `operation` da estrutura `Request`.

#define ADD_DOC 1       // Operação para adicionar um novo documento ao sistema.
#define QUERY_DOC 2     // Operação para consultar a metainformação de um documento existente pelo seu ID.
#define DELETE_DOC 3    // Operação para remover a metainformação de um documento do sistema pelo seu ID.
#define COUNT_LINES 4   // Operação para contar o número de linhas num documento que contêm uma palavra-chave.
#define SEARCH_DOCS 5   // Operação para procurar todos os documentos que contêm uma palavra-chave.
#define SHUTDOWN 6      // Operação para instruir o servidor a encerrar (com persistência dos dados).

/**
 * @brief Estrutura para representar a metainformação de um documento.
 *
 * Contém todos os dados descritivos de um documento gerido pelo sistema.
 */
typedef struct {
    int id;                             // Identificador numérico único atribuído pelo servidor a cada documento.
    char title[MAX_TITLE_SIZE];         // Título do documento.
    char authors[MAX_AUTHORS_SIZE];     // Nome(s) do(s) autor(es) do documento.
    char year[MAX_YEAR_SIZE];           // Ano de publicação do documento (como string).
    char path[MAX_PATH_SIZE];           // Caminho relativo para o ficheiro físico do documento, a partir da pasta base configurada no servidor.
} Document;

/**
 * @brief Estrutura para a mensagem de pedido (requisição) enviada do cliente para o servidor.
 *
 * Encapsula todos os dados necessários para que o servidor entenda e processe
 * a solicitação do cliente.
 */
typedef struct {
    int operation;                      // Código da operação a ser realizada (ver defines ADD_DOC, QUERY_DOC, etc.).
    Document doc;                       // Estrutura `Document` contendo os dados do documento.
                                        // Usada nas operações ADD_DOC (para enviar novos dados),
                                        // QUERY_DOC (para enviar o ID na `doc.id`),
                                        // DELETE_DOC (para enviar o ID na `doc.id`),
                                        // COUNT_LINES (para enviar o ID na `doc.id`).
    char keyword[MAX_KEYWORD_SIZE];     // Palavra-chave para operações de pesquisa de conteúdo.
                                        // Usada em COUNT_LINES e SEARCH_DOCS.
    int client_pid;                     // PID (Process ID) do processo cliente.
                                        // Essencial para o servidor saber para qual pipe de cliente deve enviar a resposta.
    int nr_processes;                   // Número de processos a serem usados na pesquisa concorrente (SEARCH_DOCS).
                                        // Se não especificado ou <= 1, a pesquisa é sequencial.
} Request;

/**
 * @brief Estrutura para a mensagem de resposta enviada do servidor para o cliente.
 *
 * Contém o resultado da operação solicitada, incluindo um código de estado
 * e quaisquer dados relevantes (como metainformação de um documento ou IDs encontrados).
 */
typedef struct {
    int status;                         // Código de estado da operação:
                                        // 0 indica sucesso.
                                        // Um valor negativo indica um erro específico (ex: -1 para "não encontrado").
    Document doc;                       // Estrutura `Document` contendo os dados do documento retornado.
                                        // Preenchida na resposta a uma operação QUERY_DOC bem-sucedida.
                                        // Na resposta a ADD_DOC, `doc.id` contém o ID do novo documento.
    int count;                          // Resultado da contagem de linhas.
                                        // Preenchido na resposta a uma operação COUNT_LINES bem-sucedida.
    int ids[MAX_RESULT_IDS];            // Array de IDs dos documentos encontrados numa pesquisa.
                                        // Preenchido na resposta a uma operação SEARCH_DOCS.
    int num_ids;                        // Número de IDs válidos presentes no array `ids`.
                                        // Usado em conjunto com `ids` na resposta a SEARCH_DOCS.
} Response;

// --- Nomes dos Pipes Nomeados (FIFOs) para Comunicação ---
// Pipes nomeados (FIFOs) são o mecanismo de comunicação entre processos (IPC)
// escolhido para este sistema. Permitem que o cliente e o servidor, que são
// processos distintos e possivelmente sem relação de parentesco, troquem mensagens.
// A gestão efetiva destes pipes (criação, abertura, leitura, escrita, fecho, remoção)
// é implementada nos ficheiros dclient.c e dserver.c.

/**
 * @brief Caminho padrão para o FIFO principal do servidor.
 *
 * Este é o pipe "público" onde todos os clientes enviam os seus pedidos (estruturas `Request`)
 * para o servidor. O servidor abre este pipe em modo de leitura para receber os pedidos.
 */
#define SERVER_PIPE "/tmp/server_pipe_so"

/**
 * @brief Formato para os nomes dos FIFOs de resposta dos clientes.
 *
 * Cada cliente cria um FIFO único para receber a resposta do servidor.
 * O nome deste FIFO é geralmente construído usando o PID (Process ID) do cliente
 * para garantir a sua unicidade. O servidor usa este nome (construído com o
 * `client_pid` recebido no `Request`) para abrir o pipe do cliente específico
 * em modo de escrita e enviar a estrutura `Response`.
 * Exemplo: /tmp/client_pipe_so_12345 (onde 12345 é o PID do cliente).
 */
#define CLIENT_PIPE_FORMAT "/tmp/client_pipe_so_%d"

#endif