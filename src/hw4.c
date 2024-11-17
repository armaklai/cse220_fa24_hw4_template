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
    int **player1_board;                // Dynamic 2D array for Player 1's board
    int **player2_board;                // Dynamic 2D array for Player 2's board
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
    if (game_state.player1_board) {
        for (int i = 0; i < game_state.height; i++) {
            free(game_state.player1_board[i]);
        }
        free(game_state.player1_board);
        game_state.player1_board = NULL;
    }

    if (game_state.player2_board) {
        for (int i = 0; i < game_state.height; i++) {
            free(game_state.player2_board[i]);
        }
        free(game_state.player2_board);
        game_state.player2_board = NULL;
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
            game_state.player1_board = malloc(height * sizeof(int *));
            for (int i = 0; i < height; i++) {
                game_state.player1_board[i] = calloc(width, sizeof(int));
            }
            game_state.player1_ready = 1;
            send(client_fd, "A", 1, 0);
        } else {
            send_error_packet(client_fd, ERROR_INVALID_BEGIN_PARAMS);
        }
    } else if (player == 2) {
        if (strcmp(buffer, "B") == 0) {
            game_state.player2_board = malloc(game_state.height * sizeof(int *));
            for (int i = 0; i < game_state.height; i++) {
                game_state.player2_board[i] = calloc(game_state.width, sizeof(int));
            }
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
    int **player_board = (player == 1) ? game_state.player1_board : game_state.player2_board;
    int (*player_pieces)[4][2] = (player == 1) ? game_state.player1_pieces : game_state.player2_pieces;

    // Parse the packet header
    char *token = strtok(buffer, " ");
    if (!token || strcmp(token, "I") != 0) {
        send_error_packet(client_fd, ERROR_INVALID_PACKET_TYPE_INIT); // E 101
        return;
    }

    // Parse and validate each piece
    for (int i = 0; i < MAX_PIECES; i++) {
        int type, rotation, row, col;

        // Parse the piece parameters
        if (!(sscanf(strtok(NULL, " "), "%d", &type) &&
              sscanf(strtok(NULL, " "), "%d", &rotation) &&
              sscanf(strtok(NULL, " "), "%d", &col) &&
              sscanf(strtok(NULL, " "), "%d", &row))) {
            send_error_packet(client_fd, ERROR_INVALID_INIT_PARAMS); // E 201
            return;
        }

        // Validate the piece type and rotation
        if (!is_valid_piece(type, rotation)) {
            send_error_packet(client_fd, ERROR_INIT_SHAPE_OUT_OF_RANGE); // E 300 or E 301
            return;
        }

        // Calculate the cells occupied by this piece
        int cells[4][2];
        place_piece(type, rotation, row, col, cells);

        // Validate placement within the player's board
        if (!is_valid_placement(cells, game_state.height, game_state.width, player_board)) {
            send_error_packet(client_fd, ERROR_INIT_SHIP_OUT_OF_BOUNDS); // E 302 or E 303
            return;
        }

        // Place the piece on the player's board
        for (int j = 0; j < 4; j++) {
            int r = cells[j][0];
            int c = cells[j][1];
            player_board[r][c] = player; // Mark cell as occupied by the player's ship
        }

        // Save the piece's positions
        memcpy(player_pieces[i], cells, sizeof(cells));
    }

    // Acknowledge the successful initialization
    send(client_fd, "A", 1, 0);
    printf("[Server] Player %d initialized their board.\n", player);
}


// Handle Shoot Packet
void handle_shoot_packet(char *buffer, int client_fd) {
    char *token = strtok(buffer, " ");
    if (!token || strcmp(token, "S") != 0) {
        send_error_packet(client_fd, ERROR_INVALID_PACKET_TYPE_ACTION); // E 102
        return;
    }

    int row, col;

    // Parse the row and column
    if (!(sscanf(strtok(NULL, " "), "%d", &row) && sscanf(strtok(NULL, " "), "%d", &col))) {
        send_error_packet(client_fd, ERROR_SHOOT_INVALID_PARAMS); // E 202
        return;
    }

    // Check bounds
    if (row < 0 || row >= game_state.height || col < 0 || col >= game_state.width) {
        send_error_packet(client_fd, ERROR_SHOOT_OUT_OF_BOUNDS); // E 400
        return;
    }

    // Determine which board to shoot at
    int **target_board = (client_fd == game_state.player1_fd) ? game_state.player2_board : game_state.player1_board;
    int *opponent_hits = (client_fd == game_state.player1_fd) ? &game_state.player2_hits : &game_state.player1_hits;
    int opponent_fd = (client_fd == game_state.player1_fd) ? game_state.player2_fd : game_state.player1_fd;

    // Check if the cell has already been guessed
    if (target_board[row][col] == -1 || target_board[row][col] == -2) {
        send_error_packet(client_fd, ERROR_SHOOT_ALREADY_GUESSED); // E 401
        return;
    }

    // Process the shot
    char result = 'M';
    if (target_board[row][col] > 0) { // Hit
        result = 'H';
        target_board[row][col] = -2; // Mark as hit
        (*opponent_hits)++;
    } else { // Miss
        target_board[row][col] = -1; // Mark as miss
    }

    // Send shot response
    char response[BUFFER_SIZE];
    int ships_remaining = MAX_PIECES - *opponent_hits;
    snprintf(response, sizeof(response), "R %d %c", ships_remaining, result);
    send(client_fd, response, strlen(response), 0);

    // Check if all ships are sunk
    if (*opponent_hits == MAX_PIECES) {
        send(client_fd, "H 1", 3, 0); // Winning player
        send(opponent_fd, "H 0", 3, 0); // Losing player

        printf("[Server] Player %d wins!\n", (client_fd == game_state.player1_fd) ? 1 : 2);
        printf("[Server] Player %d loses!\n", (client_fd == game_state.player1_fd) ? 2 : 1);

        // Free the boards and reset the game state
        free_board();
        memset(&game_state, 0, sizeof(game_state));
    } else {
        // Switch turns
        game_state.current_turn = (game_state.current_turn == 1) ? 2 : 1;
    }
}


// Handle Query Packet
void handle_query_packet(int client_fd) {
    int player = (client_fd == game_state.player1_fd) ? 1 : 2;
    int **opponent_board = (player == 1) ? game_state.player2_board : game_state.player1_board;
    int opponent_hits = (player == 1) ? game_state.player2_hits : game_state.player1_hits;
    int ships_remaining = MAX_PIECES - opponent_hits;

    // Build the query response
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "G %d", ships_remaining);
    int response_length = strlen(response);

    // Add guesses from the opponent's board in row-major order
    for (int r = 0; r < game_state.height; r++) {
        for (int c = 0; c < game_state.width; c++) {
            if (opponent_board[r][c] == -1) { // Miss
                snprintf(response + response_length, sizeof(response) - response_length, " M %d %d", r, c);
                response_length = strlen(response);
            } else if (opponent_board[r][c] == -2) { // Hit
                snprintf(response + response_length, sizeof(response) - response_length, " H %d %d", r, c);
                response_length = strlen(response);
            }
        }
    }

    // Send the response
    send(client_fd, response, strlen(response), 0);
    printf("[Server] Sent query response to Player %d: %s\n", player, response);
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













