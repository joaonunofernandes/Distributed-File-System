#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#define MAX_TITLE_SIZE 200
#define MAX_AUTHORS_SIZE 200
#define MAX_YEAR_SIZE 5    // 4 caracteres + terminador nulo
#define MAX_PATH_SIZE 200  // Aumentado devido à dimensão do caminho no PC
#define MAX_KEYWORD_SIZE 64
#define MAX_DOCS 2000      // Número máximo de documentos na cache/disco (ajustar conforme necessário)
#define MAX_RESULT_IDS 1000 // Número máximo de IDs retornados numa pesquisa

// Definição das operações possíveis entre cliente e servidor
#define ADD_DOC 1       // Adicionar um documento
#define QUERY_DOC 2     // Consultar um documento por ID
#define DELETE_DOC 3    // Remover um documento por ID
#define COUNT_LINES 4   // Contar linhas com uma palavra-chave num documento
#define SEARCH_DOCS 5   // Procurar documentos que contêm uma palavra-chave
#define SHUTDOWN 6      // Encerrar o servidor (com persistência)

// Estrutura para representar um documento
typedef struct {
    int id;                             // Identificador único do documento
    char title[MAX_TITLE_SIZE];         // Título do documento
    char authors[MAX_AUTHORS_SIZE];     // Autores do documento
    char year[MAX_YEAR_SIZE];           // Ano de publicação
    char path[MAX_PATH_SIZE];           // Caminho relativo para o ficheiro do documento
    time_t last_access;                 // Data/hora do último acesso (mantido por compatibilidade, não usado em FCFS)
    int access_count;                   // Contador de acessos (mantido por compatibilidade, não usado em FCFS)
} Document;

// Estrutura para a mensagem (requisição) enviada do cliente para o servidor
typedef struct {
    int operation;                      // Tipo de operação solicitada (ver defines acima)
    Document doc;                       // Dados do documento (usado em ADD, QUERY, DELETE, COUNT_LINES)
    char keyword[MAX_KEYWORD_SIZE];     // Palavra-chave (usada em COUNT_LINES, SEARCH_DOCS)
    int client_pid;                     // PID do processo cliente (para o servidor saber onde responder)
    int nr_processes;                   // Número de processos a usar na pesquisa concorrente (SEARCH_DOCS)
} Request;

// Estrutura para a resposta enviada do servidor para o cliente
typedef struct {
    int status;                         // Código de estado: 0 para sucesso, negativo para erro
    Document doc;                       // Dados do documento retornado (usado em QUERY_DOC)
    int count;                          // Resultado da contagem de linhas (usado em COUNT_LINES)
    int ids[MAX_RESULT_IDS];            // Array de IDs dos documentos encontrados (usado em SEARCH_DOCS)
    int num_ids;                        // Número de IDs válidos no array 'ids' (usado em SEARCH_DOCS)
} Response;

// Caminhos padrão para os FIFOs (pipes nomeados)
#define SERVER_PIPE "/tmp/server_pipe"              // FIFO principal do servidor (onde os clientes escrevem)
#define CLIENT_PIPE_FORMAT "/tmp/client_pipe_%d"    // Formato para os FIFOs dos clientes (onde o servidor escreve a resposta)

#endif