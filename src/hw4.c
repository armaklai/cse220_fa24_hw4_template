#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT1 2201
#define PORT2 2202
#define BUFFER_SIZE 1024
#define MAX_PIECES 5
#define MAX_CELLS 4

// Data structures for game state
typedef struct {
    int x[MAX_CELLS];
    int y[MAX_CELLS];
} Piece;

typedef struct {
    int width;
    int height;
    int player1_ready;
    int player2_ready;
    Piece player1_pieces[MAX_PIECES];
    Piece player2_pieces[MAX_PIECES];
    int player1_piece_count;
    int player2_piece_count;
    int player1_hits;
    int player2_hits;
    int player_turn; // 1 or 2
    int game_over;
} GameState;

GameState game_state = {0};

// Mutex for thread-safe game state access
pthread_mutex_t game_state_mutex = PTHREAD_MUTEX_INITIALIZER;

// Helper function to parse integers
int parse_int(const char *str) {
    return atoi(str);
}

// Helper function to check if coordinates are within the board
int is_within_board(int x, int y) {
    return x >= 0 && x < game_state.width && y >= 0 && y < game_state.height;
}

// Packet Handlers
void handle_begin_packet(char *buffer, int player) {
    pthread_mutex_lock(&game_state_mutex);
    if (player == 1) {
        int width, height;
        if (sscanf(buffer, "B %d %d", &width, &height) == 2) {
            game_state.width = width;
            game_state.height = height;
            game_state.player1_ready = 1;
            printf("[Server] Player 1 set board to %dx%d\n", width, height);
        } else {
            printf("[Server] Invalid Begin packet from Player 1.\n");
        }
    } else {
        game_state.player2_ready = 1;
        printf("[Server] Player 2 joined the game.\n");
    }
    pthread_mutex_unlock(&game_state_mutex);
}

void handle_initialize_packet(char *buffer, int player) {
    pthread_mutex_lock(&game_state_mutex);
    Piece pieces[MAX_PIECES];
    int piece_count = 0;
    char *token = strtok(buffer, " ");
    if (strcmp(token, "I") != 0) {
        printf("[Server] Invalid Initialize packet.\n");
        pthread_mutex_unlock(&game_state_mutex);
        return;
    }
    while ((token = strtok(NULL, " ")) != NULL && piece_count < MAX_PIECES) {
        int piece_type = parse_int(token);
        int rotation = parse_int(strtok(NULL, " "));
        int column = parse_int(strtok(NULL, " "));
        int row = parse_int(strtok(NULL, " "));
        if (!is_within_board(column, row)) {
            printf("[Server] Invalid piece placement for Player %d.\n", player);
            pthread_mutex_unlock(&game_state_mutex);
            return;
        }
        pieces[piece_count].x[0] = column;
        pieces[piece_count].y[0] = row; // Simplified for illustration
        piece_count++;
    }
    if (player == 1) {
        memcpy(game_state.player1_pieces, pieces, sizeof(pieces));
        game_state.player1_piece_count = piece_count;
        game_state.player1_ready = 1;
    } else {
        memcpy(game_state.player2_pieces, pieces, sizeof(pieces));
        game_state.player2_piece_count = piece_count;
        game_state.player2_ready = 1;
    }
    printf("[Server] Player %d pieces initialized.\n", player);
    pthread_mutex_unlock(&game_state_mutex);
}

void handle_shoot_packet(char *buffer, int player, int client_fd) {
    pthread_mutex_lock(&game_state_mutex);
    int x, y;
    if (sscanf(buffer, "S %d %d", &x, &y) != 2 || !is_within_board(x, y)) {
        send(client_fd, "ERR", strlen("ERR"), 0);
        pthread_mutex_unlock(&game_state_mutex);
        return;
    }
    int hit = 0;
    Piece *opponent_pieces = player == 1 ? game_state.player2_pieces : game_state.player1_pieces;
    int opponent_piece_count = player == 1 ? game_state.player2_piece_count : game_state.player1_piece_count;

    for (int i = 0; i < opponent_piece_count; i++) {
        for (int j = 0; j < MAX_CELLS; j++) {
            if (opponent_pieces[i].x[j] == x && opponent_pieces[i].y[j] == y) {
                hit = 1;
                if (player == 1) game_state.player1_hits++;
                else game_state.player2_hits++;
                break;
            }
        }
        if (hit) break;
    }

    char response[BUFFER_SIZE];
    sprintf(response, "S %d %d %s", x, y, hit ? "HIT" : "MISS");
    send(client_fd, response, strlen(response), 0);

    // Check if game over
    if ((player == 1 && game_state.player1_hits == game_state.player2_piece_count * MAX_CELLS) ||
        (player == 2 && game_state.player2_hits == game_state.player1_piece_count * MAX_CELLS)) {
        game_state.game_over = 1;
        send(client_fd, "H 1", strlen("H 1"), 0);
    }

    game_state.player_turn = player == 1 ? 2 : 1;
    pthread_mutex_unlock(&game_state_mutex);
}

void handle_query_packet(int client_fd) {
    pthread_mutex_lock(&game_state_mutex);
    char response[BUFFER_SIZE];
    sprintf(response, "Q %d %d", game_state.player1_hits, game_state.player2_hits);
    send(client_fd, response, strlen(response), 0);
    pthread_mutex_unlock(&game_state_mutex);
}

void handle_forfeit_packet(int client_fd) {
    pthread_mutex_lock(&game_state_mutex);
    printf("[Server] A player forfeited. Game over.\n");
    send(client_fd, "H 0", strlen("H 0"), 0);
    game_state.game_over = 1;
    pthread_mutex_unlock(&game_state_mutex);
    exit(0); // End server (optional, adjust as needed)
}

void *handle_client(void *arg) {
    int client_fd = *((int *)arg);
    free(arg);
    char buffer[BUFFER_SIZE];

    while (read(client_fd, buffer, BUFFER_SIZE) > 0) {
        if (strncmp(buffer, "B", 1) == 0) {
            handle_begin_packet(buffer, client_fd == PORT1 ? 1 : 2);
        } else if (strncmp(buffer, "I", 1) == 0) {
            handle_initialize_packet(buffer, client_fd == PORT1 ? 1 : 2);
        } else if (strncmp(buffer, "S", 1) == 0) {
            handle_shoot_packet(buffer, client_fd == PORT1 ? 1 : 2, client_fd);
        } else if (strncmp(buffer, "Q", 1) == 0) {
            handle_query_packet(client_fd);
        } else if (strncmp(buffer, "F", 1) == 0) {
            handle_forfeit_packet(client_fd);
        }
        memset(buffer, 0, BUFFER_SIZE);
    }
    close(client_fd);
    return NULL;
}

int main() {
    int server_fd1, server_fd2, *new_client_fd;
    struct sockaddr_in address1, address2;
    int opt = 1;
    int addrlen = sizeof(struct sockaddr_in);

    // Create sockets for Player 1 and Player 2
    if ((server_fd1 = socket(AF_INET, SOCK_STREAM, 0)) == 0 || (server_fd2 = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("[Server] socket() failed.");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    setsockopt(server_fd1, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd2, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind sockets
    address1.sin_family = AF_INET;
    address1.sin_addr.s_addr = INADDR_ANY;
    address1.sin_port = htons(PORT1);
    address2.sin_family = AF_INET;
    address2.sin_addr.s_addr = INADDR_ANY;
    address2.sin_port = htons(PORT2);

    if (bind(server_fd1, (struct sockaddr *)&address1, sizeof(address1)) < 0 ||
        bind(server_fd2, (struct sockaddr *)&address2, sizeof(address2)) < 0) {
        perror("[Server] bind() failed.");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd1, 1) < 0 || listen(server_fd2, 1) < 0) {
        perror("[Server] listen() failed.");
        exit(EXIT_FAILURE);
    }

    printf("[Server] Listening for connections on ports %d and %d...\n", PORT1, PORT2);

    // Accept and handle client connections
    while (1) {
        new_client_fd = malloc(sizeof(int));
        if ((*new_client_fd = accept(server_fd1, (struct sockaddr *)&address1, (socklen_t *)&addrlen)) < 0) {
            perror("[Server] accept() failed for Player 1.");
            free(new_client_fd);
            continue;
        }
        printf("[Server] Player 1 connected.\n");
        pthread_t thread1;
        pthread_create(&thread1, NULL, handle_client, new_client_fd);
        pthread_detach(thread1);

        new_client_fd = malloc(sizeof(int));
        if ((*new_client_fd = accept(server_fd2, (struct sockaddr *)&address2, (socklen_t *)&addrlen)) < 0) {
            perror("[Server] accept() failed for Player 2.");
            free(new_client_fd);
            continue;
        }
        printf("[Server] Player 2 connected.\n");
        pthread_t thread2;
        pthread_create(&thread2, NULL, handle_client, new_client_fd);
        pthread_detach(thread2);
    }

    close(server_fd1);
    close(server_fd2);
    return 0;
}
