#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define PORT1 2201
#define PORT2 2202
#define BUFFER_SIZE 1024
#define MAX_PIECES 5

// Error Codes
#define ERROR_INVALID_PACKET_TYPE_BEGIN 100
#define ERROR_INVALID_PACKET_TYPE_INIT 101
#define ERROR_INVALID_PACKET_TYPE_ACTION 102

#define ERROR_INVALID_BEGIN_PARAMS 200
#define ERROR_INVALID_INIT_PARAMS 201
#define ERROR_SHOOT_INVALID_PARAMS 202

#define ERROR_INIT_SHAPE_OUT_OF_RANGE 300
#define ERROR_INIT_ROTATION_OUT_OF_RANGE 301
#define ERROR_INIT_SHIP_OUT_OF_BOUNDS 302
#define ERROR_INIT_SHIP_OVERLAP 303

#define ERROR_SHOOT_OUT_OF_BOUNDS 400
#define ERROR_SHOOT_ALREADY_GUESSED 401

void handle_begin_packet(char *buffer, int client_fd);
void handle_initialize_packet(char *buffer, int client_fd);
void handle_shoot_packet(char *buffer, int client_fd);
void handle_query_packet(int client_fd);
void handle_forfeit_packet(int client_fd);
void *handle_client(void *arg);

typedef struct {
    int width;                          // Board width (set by Player 1)
    int height;                         // Board height (set by Player 1)
    int **board;                        // Dynamic 2D array for board
    int player1_ready;                  // Indicates if Player 1 is ready
    int player2_ready;                  // Indicates if Player 2 is ready
    int player1_fd;                     // File descriptor for Player 1
    int player2_fd;                     // File descriptor for Player 2
    int player1_pieces[MAX_PIECES][4][2]; // Player 1's ship positions
    int player2_pieces[MAX_PIECES][4][2]; // Player 2's ship positions
    int player1_hits;                   // Hits by Player 1
    int player2_hits;                   // Hits by Player 2
    int current_turn;                   // Tracks whose turn it is (1 or 2)
} GameState;

GameState game_state; //game_state
pthread_mutex_t game_state_mutex = PTHREAD_MUTEX_INITIALIZER;

// Tetris shapes
const int piece_shapes[7][4][4][2] = {
    // Shape 1: Square 
    {{{0, 0}, {0, 1}, {1, 0}, {1, 1}}, {{0, 0}, {0, 1}, {1, 0}, {1, 1}},
     {{0, 0}, {0, 1}, {1, 0}, {1, 1}}, {{0, 0}, {0, 1}, {1, 0}, {1, 1}}},
    // Shape 2: Straight Line 
    {{{0, 0}, {0, 1}, {0, 2}, {0, 3}}, {{0, 0}, {1, 0}, {2, 0}, {3, 0}},
     {{0, 0}, {0, 1}, {0, 2}, {0, 3}}, {{0, 0}, {1, 0}, {2, 0}, {3, 0}}},
    // Shape 3: S
    {{{0, 0}, {0, 1}, {1, 0}, {1, -1}}, {{0, 0}, {1, 0}, {1, 1}, {2, 1}},
     {{0, 0}, {0, 1}, {1, 0}, {1, -1}}, {{0, 0}, {1, 0}, {1, 1}, {2, 1}}},
    // Shape 4: L
    {{{0, 0}, {1, 0}, {2, 0}, {2, 1}}, {{0, 0}, {0, -1}, {0, -2}, {1, -2}},
     {{0, 0}, {-1, 0}, {-2, 0}, {-2, -1}}, {{0, 0}, {0, 1}, {0, 2}, {-1, 2}}},
    // Shape 5: Z
    {{{0, 0}, {0, -1}, {1, 0}, {1, 1}}, {{0, 0}, {1, 0}, {1, -1}, {2, -1}},
     {{0, 0}, {0, -1}, {1, 0}, {1, 1}}, {{0, 0}, {1, 0}, {1, -1}, {2, -1}}},
    // Shape 6: J
    {{{0, 0}, {1, 0}, {2, 0}, {2, -1}}, {{0, 0}, {0, 1}, {0, 2}, {1, 2}},
     {{0, 0}, {-1, 0}, {-2, 0}, {-2, 1}}, {{0, 0}, {0, -1}, {0, -2}, {-1, -2}}},
    // Shape 7: T
    {{{0, 0}, {0, -1}, {0, 1}, {1, 0}}, {{0, 0}, {-1, 0}, {1, 0}, {0, 1}},
     {{0, 0}, {0, -1}, {0, 1}, {-1, 0}}, {{0, 0}, {-1, 0}, {1, 0}, {0, -1}}}
};

// Helper Functions
void send_error_packet(int client_fd, int error_code) {
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "E %d", error_code);
    send(client_fd, response, strlen(response), 0);
}
void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    char buffer[BUFFER_SIZE];
    int player = (client_fd == game_state.player1_fd) ? 1 : 2;

    while (recv(client_fd, buffer, sizeof(buffer), 0) > 0) {
        pthread_mutex_lock(&game_state_mutex);

        // Ensure turn-based gameplay
        if (game_state.player1_ready && game_state.player2_ready && game_state.current_turn != player) {
            send(client_fd, "WAIT", 4, 0); // Tell the player to wait for their turn
            pthread_mutex_unlock(&game_state_mutex);
            continue;
        }

        // Game Phase 1: Begin Packets
        if (!game_state.player1_ready || !game_state.player2_ready) {
            if (buffer[0] == 'B') {
                handle_begin_packet(buffer, client_fd);
            } else if (buffer[0] == 'F') {
                handle_forfeit_packet(client_fd);
                pthread_mutex_unlock(&game_state_mutex);
                break; // Game ends immediately on forfeit
            } else {
                send_error_packet(client_fd, ERROR_INVALID_PACKET_TYPE_BEGIN); // E 100
            }
        }
        // Game Phase 2: Initialize Packets
        else if (!game_state.player1_ready || !game_state.player2_ready) {
            if (buffer[0] == 'I') {
                handle_initialize_packet(buffer, client_fd);
            } else if (buffer[0] == 'F') {
                handle_forfeit_packet(client_fd);
                pthread_mutex_unlock(&game_state_mutex);
                break; // Game ends immediately on forfeit
            } else {
                send_error_packet(client_fd, ERROR_INVALID_PACKET_TYPE_INIT); // E 101
            }
        }
        // Game Phase 3: Gameplay (Shoot, Query, Forfeit)
        else {
            if (buffer[0] == 'S') {
                handle_shoot_packet(buffer, client_fd);
            } else if (buffer[0] == 'Q') {
                handle_query_packet(client_fd);
            } else if (buffer[0] == 'F') {
                handle_forfeit_packet(client_fd);
                pthread_mutex_unlock(&game_state_mutex);
                break; // End connection after forfeit
            } else {
                send_error_packet(client_fd, ERROR_INVALID_PACKET_TYPE_ACTION); // E 102
            }
        }

        // If it was a shoot packet, switch turns
        if (buffer[0] == 'S') {
            game_state.current_turn = (game_state.current_turn == 1) ? 2 : 1;
        }

        pthread_mutex_unlock(&game_state_mutex);
    }

    close(client_fd);
    return NULL;
}


void free_board() {
    if (game_state.board) {
        for (int i = 0; i < game_state.height; i++) {
            free(game_state.board[i]);
        }
        free(game_state.board);
        game_state.board = NULL;
    }
}

int is_valid_piece(int type, int rotation) {
    return (type >= 1 && type <= 7) && (rotation >= 0 && rotation <= 3);
}

int is_within_board(int col, int row) {
    return col >= 0 && col < game_state.width && row >= 0 && row < game_state.height;
}

void place_piece(int shape, int rotation, int row, int col, int cells[4][2]) {
    for (int i = 0; i < 4; i++) {
        cells[i][0] = row + piece_shapes[shape - 1][rotation][i][0];
        cells[i][1] = col + piece_shapes[shape - 1][rotation][i][1];
    }
}

int is_valid_placement(int cells[4][2], int height, int width, int **board) {
    for (int i = 0; i < 4; i++) {
        int r = cells[i][0];
        int c = cells[i][1];
        if (r < 0 || r >= height || c < 0 || c >= width || board[r][c] != 0) {
            return 0;
        }
    }
    return 1;
}

// Handle Begin Packet
void handle_begin_packet(char *buffer, int client_fd) {
    int player = (client_fd == game_state.player1_fd) ? 1 : 2;

    if (player == 1) {
        int width, height;
        if (sscanf(buffer, "B %d %d", &width, &height) == 2 && width >= 10 && height >= 10) {
            free_board();
            game_state.width = width;
            game_state.height = height;
            game_state.board = malloc(height * sizeof(int *));
            for (int i = 0; i < height; i++) {
                game_state.board[i] = calloc(width, sizeof(int));
            }
            game_state.player1_ready = 1;
            send(client_fd, "A", 1, 0);
        } else {
            send_error_packet(client_fd, ERROR_INVALID_BEGIN_PARAMS);
        }
    } else if (player == 2) {
        if (strcmp(buffer, "B") == 0) {
            game_state.player2_ready = 1;
            send(client_fd, "A", 1, 0);
        } else {
            send_error_packet(client_fd, ERROR_INVALID_BEGIN_PARAMS);
        }
    }
}
// Handle Initialize Packet
void handle_initialize_packet(char *buffer, int client_fd) {
    int player = (client_fd == game_state.player1_fd) ? 1 : 2;
    int (*player_pieces)[4][2] = (player == 1) ? game_state.player1_pieces : game_state.player2_pieces;

    char *token = strtok(buffer, " ");
    if (!token || strcmp(token, "I") != 0) {
        send_error_packet(client_fd, ERROR_INVALID_PACKET_TYPE_INIT);
        return;
    }

    for (int i = 0; i < MAX_PIECES; i++) {
        int type, rotation, row, col;
        if (!(sscanf(strtok(NULL, " "), "%d", &type) &&
              sscanf(strtok(NULL, " "), "%d", &rotation) &&
              sscanf(strtok(NULL, " "), "%d", &col) &&
              sscanf(strtok(NULL, " "), "%d", &row))) {
            send_error_packet(client_fd, ERROR_INVALID_INIT_PARAMS);
            return;
        }

        if (!is_valid_piece(type, rotation)) {
            send_error_packet(client_fd, ERROR_INIT_SHAPE_OUT_OF_RANGE);
            return;
        }

        int cells[4][2];
        place_piece(type, rotation, row, col, cells);

        if (!is_valid_placement(cells, game_state.height, game_state.width, game_state.board)) {
            send_error_packet(client_fd, ERROR_INIT_SHIP_OUT_OF_BOUNDS);
            return;
        }

        for (int j = 0; j < 4; j++) {
            game_state.board[cells[j][0]][cells[j][1]] = player;
            memcpy(player_pieces[i], cells, sizeof(cells));
        }
    }
    send(client_fd, "A", 1, 0);
}

// Handle Shoot Packet
void handle_shoot_packet(char *buffer, int client_fd) {
    char *token = strtok(buffer, " ");
    int row = atoi(strtok(NULL, " "));
    int col = atoi(strtok(NULL, " "));

    if (row < 0 || row >= game_state.height || col < 0 || col >= game_state.width) {
        send_error_packet(client_fd, ERROR_SHOOT_OUT_OF_BOUNDS);
        return;
    }

    if (game_state.board[row][col] == -1 || game_state.board[row][col] == -2) {
        send_error_packet(client_fd, ERROR_SHOOT_ALREADY_GUESSED);
        return;
    }

    char result = 'M';
    int opponent = (client_fd == game_state.player1_fd) ? 2 : 1;

    if (game_state.board[row][col] == opponent) {
        result = 'H';
        game_state.board[row][col] = -1;
        if (opponent == 1) game_state.player2_hits++;
        else game_state.player1_hits++;
    } else {
        game_state.board[row][col] = -2;
    }

    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "R %d %c", MAX_PIECES - ((opponent == 1) ? game_state.player1_hits : game_state.player2_hits), result);
    send(client_fd, response, strlen(response), 0);

    if ((opponent == 1 ? game_state.player1_hits : game_state.player2_hits) == MAX_PIECES) {
        send(client_fd, "H 1", 3, 0);
        send((opponent == 1 ? game_state.player1_fd : game_state.player2_fd), "H 0", 3, 0);
        free_board();
        memset(&game_state, 0, sizeof(game_state));
    }
}

// Handle Query Packet
void handle_query_packet(int client_fd) {
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "G %d ", MAX_PIECES - ((client_fd == game_state.player1_fd) ? game_state.player2_hits : game_state.player1_hits));

    for (int r = 0; r < game_state.height; r++) {
        for (int c = 0; c < game_state.width; c++) {
            if (game_state.board[r][c] == -1) {
                snprintf(response + strlen(response), sizeof(response) - strlen(response), "H %d %d ", r, c);
            } else if (game_state.board[r][c] == -2) {
                snprintf(response + strlen(response), sizeof(response) - strlen(response), "M %d %d ", r, c);
            }
        }
    }

    send(client_fd, response, strlen(response), 0);
}

// Handle Forfeit Packet
void handle_forfeit_packet(int client_fd) {
    int opponent_fd = (client_fd == game_state.player1_fd) ? game_state.player2_fd : game_state.player1_fd;

    send(opponent_fd, "H 1", 3, 0); // Opponent wins
    send(client_fd, "H 0", 3, 0);  // Current player loses

    free_board();
    memset(&game_state, 0, sizeof(game_state));
}



// Main Function
int main() {
    int server_fd1, server_fd2, client_fd1, client_fd2;
    struct sockaddr_in address1, address2;

    server_fd1 = socket(AF_INET, SOCK_STREAM, 0);
    server_fd2 = socket(AF_INET, SOCK_STREAM, 0);

    address1.sin_family = AF_INET;
    address1.sin_addr.s_addr = INADDR_ANY;
    address1.sin_port = htons(PORT1);

    address2.sin_family = AF_INET;
    address2.sin_addr.s_addr = INADDR_ANY;
    address2.sin_port = htons(PORT2);

    bind(server_fd1, (struct sockaddr *)&address1, sizeof(address1));
    bind(server_fd2, (struct sockaddr *)&address2, sizeof(address2));

    listen(server_fd1, 1);
    listen(server_fd2, 1);

    client_fd1 = accept(server_fd1, NULL, NULL);
    client_fd2 = accept(server_fd2, NULL, NULL);

    game_state.player1_fd = client_fd1;
    game_state.player2_fd = client_fd2;
    game_state.current_turn = 1;

    pthread_t thread1, thread2;
    pthread_create(&thread1, NULL, handle_client, &client_fd1);
    pthread_create(&thread2, NULL, handle_client, &client_fd2);

    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    free_board();
    close(server_fd1);
    close(server_fd2);

    return 0;
}













