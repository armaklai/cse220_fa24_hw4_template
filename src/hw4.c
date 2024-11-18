#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT1 2201
#define PORT2 2202
#define MAX_PIECES 5
#define BUFFER_SIZE 1024

typedef struct {
    int width;                          // Board width (set by Player 1)
    int height;                         // Board height (set by Player 1)
    int **board1;                       // Dynamic 2D array for Player 1's board
    int **board2;                       // Dynamic 2D array for Player 2's board
    int player1_ready;                  // Indicates if Player 1 is ready
    int player2_ready;                  // Indicates if Player 2 is ready
    int player1_fd;                     // File descriptor for Player 1
    int player2_fd;                     // File descriptor for Player 2
    int player1_hits;                   // Hits by Player 1
    int player2_hits;                   // Hits by Player 2
    int current_turn;                   // Tracks whose turn it is (1 or 2)
} GameState;

const int piece_shapes[7][4][4][2] = {
    // Shape 1: Square (O)
    {{{0, 0}, {0, 1}, {1, 0}, {1, 1}}, {{0, 0}, {0, 1}, {1, 0}, {1, 1}},
     {{0, 0}, {0, 1}, {1, 0}, {1, 1}}, {{0, 0}, {0, 1}, {1, 0}, {1, 1}}},
    // Shape 2: Line (I)
    {{{0, 0}, {0, 1}, {0, 2}, {0, 3}}, {{0, 0}, {1, 0}, {2, 0}, {3, 0}},
     {{0, 0}, {0, 1}, {0, 2}, {0, 3}}, {{0, 0}, {1, 0}, {2, 0}, {3, 0}}},
    // Shape 3: S
    {{{0, 0}, {0, 1}, {1, 0}, {1, -1}}, {{0, 0}, {1, 0}, {1, -1}, {2, -1}},
     {{0, 0}, {0, 1}, {1, 0}, {1, -1}}, {{0, 0}, {1, 0}, {1, -1}, {2, -1}}},
    // Shape 4: L
    {{{0, 0}, {1, 0}, {2, 0}, {2, 1}}, {{0, 0}, {0, 1}, {0, 2}, {1, 2}},
     {{0, 0}, {1, 0}, {2, 0}, {0, -1}}, {{0, 0}, {0, -1}, {0, -2}, {-1, -2}}},
    // Shape 5: Z
    {{{0, 0}, {0, -1}, {1, 0}, {1, 1}}, {{0, 0}, {1, 0}, {1, -1}, {2, -1}},
     {{0, 0}, {0, -1}, {1, 0}, {1, 1}}, {{0, 0}, {1, 0}, {1, -1}, {2, -1}}},
    // Shape 6: J
    {{{0, 0}, {1, 0}, {2, 0}, {2, -1}}, {{0, 0}, {0, -1}, {0, -2}, {1, -2}},
     {{0, 0}, {1, 0}, {2, 0}, {0, 1}}, {{0, 0}, {0, 1}, {0, 2}, {-1, 2}}},
    // Shape 7: T
    {{{0, 0}, {0, -1}, {0, 1}, {1, 0}}, {{0, 0}, {-1, 0}, {1, 0}, {0, -1}},
     {{0, 0}, {0, -1}, {0, 1}, {-1, 0}}, {{0, 0}, {-1, 0}, {1, 0}, {0, 1}}}
};

GameState game_state;

// Function Prototypes
void initialize_game_state();
void free_board(int **board, int height);
int **create_board(int width, int height);
void send_error_packet(int client_fd, int error_code);
void handle_turn(int client_fd);
void handle_begin_packet(char *buffer, int client_fd);
void handle_initialize_packet(char *buffer, int client_fd);
void handle_shoot_packet(char *buffer, int client_fd);
void handle_query_packet(int client_fd);
void handle_forfeit_packet(int client_fd);

void initialize_game_state() {
    memset(&game_state, 0, sizeof(GameState));
    game_state.current_turn = 1; // Player 1 starts
}

int **create_board(int width, int height) {
    int **board = malloc(height * sizeof(int *));
    for (int i = 0; i < height; i++) {
        board[i] = calloc(width, sizeof(int));
    }
    return board;
}
int is_valid_piece(int type, int rotation) {
    return (type >= 1 && type <= 7) && (rotation >= 0 && rotation <= 3);
}
void place_piece(int type, int rotation, int row, int col, int cells[4][2]) {
    for (int i = 0; i < 4; i++) {
        cells[i][0] = row + piece_shapes[type - 1][rotation][i][0];
        cells[i][1] = col + piece_shapes[type - 1][rotation][i][1];
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
int setup_server_socket(int port, struct sockaddr_in *addr) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    addr->sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 2) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    return server_fd;
}


void free_board(int **board, int height) {
    for (int i = 0; i < height; i++) {
        free(board[i]);
    }
    free(board);
}

void send_error_packet(int client_fd, int error_code) {
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "E %d", error_code);
    send(client_fd, response, strlen(response), 0);
}

void handle_turn(int client_fd) {
    char buffer[BUFFER_SIZE];
    int player = (client_fd == game_state.player1_fd) ? 1 : 2;

    if (recv(client_fd, buffer, sizeof(buffer), 0) <= 0) {
        printf("[Server] Player %d disconnected.\n", player);
        close(client_fd);
        return;
    }

    buffer[strcspn(buffer, "\r\n")] = '\0'; // Trim newlines
    printf("[Server] Received from Player %d: %s\n", player, buffer);

    if (!game_state.player1_ready || !game_state.player2_ready) {
        // Setup Phase
        if (buffer[0] == 'B') {
            handle_begin_packet(buffer, client_fd);
        } else if (buffer[0] == 'I') {
            handle_initialize_packet(buffer, client_fd);

            if (game_state.player1_ready && game_state.player2_ready) {
                game_state.current_turn = 1; // Start gameplay with Player 1
                printf("[Server] Both players ready. Starting gameplay.\n");
            }
        } else {
            send_error_packet(client_fd, (buffer[0] == 'B' || buffer[0] == 'I') ? 200 : 100);
        }
        return;
    }

    if (game_state.current_turn != player) {
        printf("[Server] Ignored input from Player %d out of turn.\n", player);
        return;
    }

    if (buffer[0] == 'S') {
        handle_shoot_packet(buffer, client_fd);
        game_state.current_turn = (game_state.current_turn == 1) ? 2 : 1; // Switch turns
    } else if (buffer[0] == 'Q') {
        handle_query_packet(client_fd);
    } else if (buffer[0] == 'F') {
        handle_forfeit_packet(client_fd);
    } else {
        send_error_packet(client_fd, 102); // Invalid packet during gameplay
    }
}


void handle_begin_packet(char *buffer, int client_fd) {
    int player = (client_fd == game_state.player1_fd) ? 1 : 2;

    if (player == 1) {
        int width, height;
        if (sscanf(buffer, "B %d %d", &width, &height) == 2) {
            if (width >= 10 && height >= 10) {
                game_state.width = width;
                game_state.height = height;
                game_state.board1 = create_board(width, height);
                game_state.player1_ready = 1;

                printf("[Server] Player 1 set board to %dx%d\n", width, height);
                send(client_fd, "A", 1, 0);
            } else {
                send_error_packet(client_fd, 200); // Invalid dimensions
            }
        } else {
            send_error_packet(client_fd, 200); // Invalid Begin packet
        }
    } else if (player == 2) {
        if (strcmp(buffer, "B") == 0) {
            game_state.board2 = create_board(game_state.width, game_state.height);
            game_state.player2_ready = 1;

            printf("[Server] Player 2 joined the game.\n");
            send(client_fd, "A", 1, 0);
        } else {
            send_error_packet(client_fd, 200); // Invalid Begin packet
        }
    }
}




void handle_initialize_packet(char *buffer, int client_fd) {
    int player = (client_fd == game_state.player1_fd) ? 1 : 2;
    int **board = (player == 1) ? game_state.board1 : game_state.board2;

    char *token = strtok(buffer, " ");
    if (!token || strcmp(token, "I") != 0) {
        send_error_packet(client_fd, 101);
        return;
    }

    for (int i = 0; i < MAX_PIECES; i++) {
        int type, rotation, row, col;
        if (!(sscanf(strtok(NULL, " "), "%d", &type) &&
              sscanf(strtok(NULL, " "), "%d", &rotation) &&
              sscanf(strtok(NULL, " "), "%d", &col) &&
              sscanf(strtok(NULL, " "), "%d", &row))) {
            send_error_packet(client_fd, 201);
            return;
        }

        if (!is_valid_piece(type, rotation)) {
            send_error_packet(client_fd, 300);
            return;
        }

        int cells[4][2];
        place_piece(type, rotation, row, col, cells);

        if (!is_valid_placement(cells, game_state.height, game_state.width, board)) {
            send_error_packet(client_fd, 302);
            return;
        }

        // Check for overlap
        for (int j = 0; j < 4; j++) {
            if (board[cells[j][0]][cells[j][1]] != 0) {
                send_error_packet(client_fd, 303);
                return;
            }
        }

        // Place the piece on the board
        for (int j = 0; j < 4; j++) {
            board[cells[j][0]][cells[j][1]] = player;
        }
    }

    send(client_fd, "A", 1, 0);
    printf("[Server] Player %d initialized their board.\n", player);
}
void handle_shoot_packet(char *buffer, int client_fd) {
    int player = (client_fd == game_state.player1_fd) ? 1 : 2;
    int opponent = (player == 1) ? 2 : 1;
    int **opponent_board = (opponent == 1) ? game_state.board1 : game_state.board2;

    int row, col;
    if (sscanf(buffer, "S %d %d", &row, &col) != 2) {
        send_error_packet(client_fd, 202);
        return;
    }

    if (row < 0 || row >= game_state.height || col < 0 || col >= game_state.width) {
        send_error_packet(client_fd, 400);
        return;
    }

    if (opponent_board[row][col] < 0) {
        send_error_packet(client_fd, 401);
        return;
    }

    char result = 'M';
    if (opponent_board[row][col] == opponent) {
        result = 'H';
        opponent_board[row][col] = -1;

        // Check if the ship is fully sunk
        int sunk = 1;
        for (int i = 0; i < game_state.height; i++) {
            for (int j = 0; j < game_state.width; j++) {
                if (opponent_board[i][j] == opponent) {
                    sunk = 0;
                    break;
                }
            }
        }

        if (sunk) {
            if (opponent == 1) game_state.player1_hits++;
            else game_state.player2_hits++;
        }
    } else {
        opponent_board[row][col] = -2;
    }

    // Send the response
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "R %d %c", MAX_PIECES - ((opponent == 1) ? game_state.player1_hits : game_state.player2_hits), result);
    send(client_fd, response, strlen(response), 0);

    // Check if the game is over
    if ((opponent == 1 ? game_state.player1_hits : game_state.player2_hits) == MAX_PIECES) {
        send(client_fd, "H 1", 3, 0); // Winner
        send((opponent == 1 ? game_state.player1_fd : game_state.player2_fd), "H 0", 3, 0); // Loser
        free_board(game_state.board1, game_state.height);
        free_board(game_state.board2, game_state.height);
        memset(&game_state, 0, sizeof(game_state)); // Reset game state
    }
}
void handle_query_packet(int client_fd) {
    int player = (client_fd == game_state.player1_fd) ? 1 : 2;
    int **opponent_board = (player == 1) ? game_state.board2 : game_state.board1;

    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "G %d ", MAX_PIECES - ((player == 1) ? game_state.player2_hits : game_state.player1_hits));

    for (int r = 0; r < game_state.height; r++) {
        for (int c = 0; c < game_state.width; c++) {
            if (opponent_board[r][c] == -1) {
                snprintf(response + strlen(response), sizeof(response) - strlen(response), "H %d %d ", r, c);
            } else if (opponent_board[r][c] == -2) {
                snprintf(response + strlen(response), sizeof(response) - strlen(response), "M %d %d ", r, c);
            }
        }
    }

    send(client_fd, response, strlen(response), 0);
}
void handle_forfeit_packet(int client_fd) {
    int player = (client_fd == game_state.player1_fd) ? 1 : 2;
    int opponent_fd = (player == 1) ? game_state.player2_fd : game_state.player1_fd;

    // Send Halt packets
    send(client_fd, "H 0", 3, 0); // Loser
    send(opponent_fd, "H 1", 3, 0); // Winner

    printf("[Server] Player %d forfeited. Player %d wins.\n", player, (player == 1) ? 2 : 1);

    // Cleanup
    free_board(game_state.board1, game_state.height);
    free_board(game_state.board2, game_state.height);
    memset(&game_state, 0, sizeof(game_state)); // Reset game state
}

int main() {
    int server_fd1, server_fd2, client_fd1, client_fd2;
    struct sockaddr_in addr1, addr2;

    initialize_game_state();

    // Setup sockets for Player 1 and Player 2
    server_fd1 = setup_server_socket(PORT1, &addr1);
    server_fd2 = setup_server_socket(PORT2, &addr2);

    printf("[Server] Listening for Player 1 on port %d...\n", PORT1);
    printf("[Server] Listening for Player 2 on port %d...\n", PORT2);

    printf("[Server] Waiting for Player 1 to connect...\n");
    client_fd1 = accept(server_fd1, NULL, NULL);
    if (client_fd1 < 0) {
        perror("Accept failed for Player 1");
        exit(EXIT_FAILURE);
    }
    printf("[Server] Player 1 connected.\n");
    game_state.player1_fd = client_fd1;

    printf("[Server] Waiting for Player 2 to connect...\n");
    client_fd2 = accept(server_fd2, NULL, NULL);
    if (client_fd2 < 0) {
        perror("Accept failed for Player 2");
        exit(EXIT_FAILURE);
    }
    printf("[Server] Player 2 connected.\n");
    game_state.player2_fd = client_fd2;

    // Handle turns for both players
    while (1) {
        if (game_state.current_turn == 1) {
            handle_turn(client_fd1);
        } else {
            handle_turn(client_fd2);
        }
    }

    close(server_fd1);
    close(server_fd2);
    return 0;
}


























