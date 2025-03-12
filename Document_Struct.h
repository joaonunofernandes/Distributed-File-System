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

#define MAX_TITLE_SIZE 200
#define MAX_AUTHORS_SIZE 200
#define MAX_YEAR_SIZE 4
#define MAX_PATH_SIZE 64
#define MAX_KEYWORD_SIZE 64
#define MAX_DOCS 1000

// Definição das operações
#define ADD_DOC 1
#define QUERY_DOC 2
#define DELETE_DOC 3
#define COUNT_LINES 4
#define SEARCH_DOCS 5
#define SHUTDOWN 6

// Estrutura para representar um documento
typedef struct {
    int id;
    char title[MAX_TITLE_SIZE];
    char authors[MAX_AUTHORS_SIZE];
    char year[MAX_YEAR_SIZE];
    char path[MAX_PATH_SIZE];
} Document;

// Estrutura para a mensagem enviada do cliente para o servidor
typedef struct {
    int operation;
    Document doc;
    char keyword[MAX_KEYWORD_SIZE];
} Request;

// Estrutura para a resposta do servidor para o cliente
typedef struct {
    int status; // 0 para sucesso, outro valor para erro
    Document doc;
    int count; // Para contar linhas
    int ids[MAX_DOCS]; // Para armazenar IDs na busca
    int num_ids; // Quantidade de IDs encontrados
} Response;

// Caminho dos pipes
#define SERVER_PIPE "/tmp/server_pipe"
#define CLIENT_PIPE_FORMAT "/tmp/client_pipe_%d"

#endif