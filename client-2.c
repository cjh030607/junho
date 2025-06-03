#include "../include/client.h"
#include "../include/game.h"
#include "../include/json.h"
#include "../include/board.h"
#include "../libs/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <math.h>

#define SIMULATION_TIME 3.0

static char board_arr[BOARD_SIZE][BOARD_SIZE];


#define INF 1000000000

static struct timespec start_time;
static const double TIME_LIMIT = 2.6;
#define MOVE_ARRAY_SIZE 128

static double elapsed_time() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start_time.tv_sec)
         + (now.tv_nsec - start_time.tv_nsec) * 1e-9;
}
static int time_exceeded() { return elapsed_time() >= TIME_LIMIT; }

static inline void copy_board(char dst[BOARD_SIZE][BOARD_SIZE], char src[BOARD_SIZE][BOARD_SIZE]) {
    memcpy(dst, src, BOARD_SIZE * BOARD_SIZE);
}

static void apply_move_sim(char bd[BOARD_SIZE][BOARD_SIZE], int r1, int c1, int r2, int c2, char me, int is_jump)
{
    if (is_jump) bd[r1][c1] = '.';
    bd[r2][c2] = me;
    char opp = (me == 'R' ? 'B' : 'R');
    for (int d = 0; d < 8; ++d) {
        int nr = r2 + directions[d][0], nc = c2 + directions[d][1];
        if (0 <= nr && nr < BOARD_SIZE && 0 <= nc && nc < BOARD_SIZE && bd[nr][nc] == opp)
            bd[nr][nc] = me;
    }
}

static int mobility(char bd[BOARD_SIZE][BOARD_SIZE], char me)
{
    int cnt = 0;
    for (int r = 0; r < BOARD_SIZE; ++r) {
        for (int c = 0; c < BOARD_SIZE; ++c) {
            if (bd[r][c] == me) {
                for (int d = 0; d < 8; ++d) {
                    int nr = r + directions[d][0], nc = c + directions[d][1];
                    for (int step = 1; step <= 2; ++step,
                        nr += directions[d][0], nc += directions[d][1]) {
                        if (0 <= nr && nr < BOARD_SIZE && 0 <= nc && nc < BOARD_SIZE
                            && bd[nr][nc] == '.') {
                            ++cnt; goto NEXT_CELL;
                        }
                    }
                }
            }
        NEXT_CELL:;
        }
    }
    return cnt;
}

static int evaluate_board(char bd[BOARD_SIZE][BOARD_SIZE], char me)
{
    char opp = (me == 'R' ? 'B' : 'R');
    int my_cnt = 0, opp_cnt = 0;
    for (int i = 0; i < BOARD_SIZE; ++i)
        for (int j = 0; j < BOARD_SIZE; ++j) {
            if      (bd[i][j] == me)  ++my_cnt;
            else if (bd[i][j] == opp) ++opp_cnt;
        }
    int piece_diff = my_cnt - opp_cnt;
    int my_mob = mobility(bd, me);
    int opp_mob = mobility(bd, opp);
    int mob_diff = my_mob - opp_mob;
    return piece_diff * 100 + mob_diff * 10;
}

int generate_move(char board[BOARD_SIZE][BOARD_SIZE], char player_color,
                  int *out_r1, int *out_c1, int *out_r2, int *out_c2)
{
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    char me = player_color;
    char opp = (me == 'R' ? 'B' : 'R');
    int best_score = -INF;
    int br1 = -1, bc1 = -1, br2 = -1, bc2 = -1;

    // 내 모든 수
    for (int r = 0; r < BOARD_SIZE; ++r)
        for (int c = 0; c < BOARD_SIZE; ++c)
            if (board[r][c] == me)
                for (int d = 0; d < 8; ++d) {
                    int nr = r + directions[d][0], nc = c + directions[d][1];
                    for (int step = 1; step <= 2; ++step,
                         nr += directions[d][0], nc += directions[d][1]) {

                        if (nr < 0 || nr >= BOARD_SIZE || nc < 0 || nc >= BOARD_SIZE ||
                            board[nr][nc] != '.')
                            continue;
                        if (time_exceeded()) goto END_SEARCH;

                        // 시뮬레이션: 내 수 두기
                        char sim[BOARD_SIZE][BOARD_SIZE];
                        copy_board(sim, board);
                        apply_move_sim(sim, r, c, nr, nc, me, step == 2);

                        // 상대의 최선 응수(그리디) 찾기
                        int worst_for_me = INF; // 내 입장에서 가장 안 좋은 점수
                        int found_opp = 0;

                        for (int rr = 0; rr < BOARD_SIZE; ++rr)
                            for (int cc = 0; cc < BOARD_SIZE; ++cc)
                                if (sim[rr][cc] == opp)
                                    for (int dd = 0; dd < 8; ++dd) {
                                        int nnr = rr + directions[dd][0], nnc = cc + directions[dd][1];
                                        for (int s = 1; s <= 2; ++s,
                                            nnr += directions[dd][0], nnc += directions[dd][1]) {
                                            if (nnr < 0 || nnr >= BOARD_SIZE || nnc < 0 || nnc >= BOARD_SIZE ||
                                                sim[nnr][nnc] != '.')
                                                continue;
                                            char sim2[BOARD_SIZE][BOARD_SIZE];
                                            copy_board(sim2, sim);
                                            apply_move_sim(sim2, rr, cc, nnr, nnc, opp, s == 2);

                                            int score = evaluate_board(sim2, me);
                                            if (score < worst_for_me) worst_for_me = score;
                                            found_opp = 1;
                                        }
                                    }
                        if (!found_opp) {
                            // 상대가 둘 수 없으면 평가 함수(즉시 이길 수 있음)
                            worst_for_me = evaluate_board(sim, me);
                        }

                        // 내 수의 결과 중 가장 좋은 것을 선택
                        if (worst_for_me > best_score) {
                            best_score = worst_for_me;
                            br1 = r; bc1 = c; br2 = nr; bc2 = nc;
                        }
                    }
                }
END_SEARCH:

    if (br1 < 0) {
        *out_r1 = *out_c1 = *out_r2 = *out_c2 = 0;
        return 0;
    } else {
        *out_r1 = br1; *out_c1 = bc1;
        *out_r2 = br2; *out_c2 = bc2;
        return 1;
    }
}




static int connect_to_server(const char *ip, const char *port) {
    struct addrinfo hints, *res, *p;
    int sockfd;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(ip, port, &hints, &res) != 0) {
        perror("getaddrinfo");
        return -1;
    }
    for (p = res; p; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) continue;
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(sockfd);
    }
    freeaddrinfo(res);
    if (!p) {
        fprintf(stderr, "Failed to connect to %s:%s\n", ip, port);
        return -1;
    }
    return sockfd;
}

int client_run(const char *ip, const char *port, const char *username) {
    int sockfd = connect_to_server(ip, port);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to connect to %s:%s\n", ip, port);
        return EXIT_FAILURE;
    }

    // 1) register 요청
    {
        cJSON *reg = cJSON_CreateObject();
        cJSON_AddStringToObject(reg, "type", "register");
        cJSON_AddStringToObject(reg, "username", username);
        if (send_json(sockfd, reg) < 0) {
            fprintf(stderr, "Failed to send register message\n");
            cJSON_Delete(reg);
            close(sockfd);
            return EXIT_FAILURE;
        }
        cJSON_Delete(reg);
    }

    int waiting_for_result = 0;
    char my_color = 0;

    while (1) {
        cJSON *msg = recv_json(sockfd);
        if (!msg) {
            // 서버 연결이 끊기거나 오류 발생
            break;
        }
        cJSON *jtype = cJSON_GetObjectItem(msg, "type");
        if (!jtype || !jtype->valuestring) {
            cJSON_Delete(msg);
            continue;
        }
        const char *type = jtype->valuestring;

        // 2-1) register_ack
        if (strcmp(type, "register_ack") == 0) {
            printf("Registered as %s\n", username);
            cJSON_Delete(msg);
            continue;
        }
        // 2-2) register_nack
        else if (strcmp(type, "register_nack") == 0) {
            cJSON *jreason = cJSON_GetObjectItem(msg, "reason");
            if (jreason && jreason->valuestring)
                printf("Register failed: %s\n", jreason->valuestring);
            else
                printf("Register failed (unknown reason)\n");
            cJSON_Delete(msg);
            break;
        }
        // 2-3) game_start 
        else if (strcmp(type, "game_start") == 0) {
            printf("Game started\n");
            cJSON *jplayers = cJSON_GetObjectItem(msg, "players");
            if (jplayers && cJSON_IsArray(jplayers)) {
                const cJSON *p0 = cJSON_GetArrayItem(jplayers, 0);
                // 첫 번째 R, 아니면 B
                if (p0 && p0->valuestring && strcmp(username, p0->valuestring) == 0)
                    my_color = 'R';
                else
                    my_color = 'B';
            }

            cJSON_Delete(msg);
            continue;
        }
        // 2-4) your_turn (board + timeout 전달)
        else if (strcmp(type, "your_turn") == 0) {
            cJSON *jbarr = cJSON_GetObjectItem(msg, "board");
            if (jbarr && cJSON_IsArray(jbarr)) {
                int nrows = cJSON_GetArraySize(jbarr);
                printf("Current board:\n");
                for (int i = 0; i < nrows && i < BOARD_SIZE; i++) {
                    const cJSON *jrow = cJSON_GetArrayItem(jbarr, i);
                    if (jrow && jrow->valuestring) {
                        memcpy(board_arr[i], jrow->valuestring, BOARD_SIZE);
                        update_led_matrix(board_arr);
                        char rowbuf[BOARD_SIZE+1];
                        memcpy(rowbuf, jrow->valuestring, BOARD_SIZE);
                        rowbuf[BOARD_SIZE] = '\0';
                        printf("%s\n", rowbuf);
                    }
                }
            }
            // timeout
            cJSON *jtimeout = cJSON_GetObjectItem(msg, "timeout");
            if (jtimeout)
                printf("Timeout: %.1f s\n", jtimeout->valuedouble);

            printf("Your turn (%c)\n", my_color);
            int r1, c1, r2, c2;
            int has_move = generate_move(board_arr, my_color, &r1, &c1, &r2, &c2);

            // move, pass
            cJSON *mv = cJSON_CreateObject();
            cJSON_AddStringToObject(mv, "type", "move");
            cJSON_AddStringToObject(mv, "username", username);
            if (has_move) {
                cJSON_AddNumberToObject(mv, "sx", r1 + 1);
                cJSON_AddNumberToObject(mv, "sy", c1 + 1);
                cJSON_AddNumberToObject(mv, "tx", r2 + 1);
                cJSON_AddNumberToObject(mv, "ty", c2 + 1);
            } else {
                // no valid move -> pass
                cJSON_AddNumberToObject(mv, "sx", 0);
                cJSON_AddNumberToObject(mv, "sy", 0);
                cJSON_AddNumberToObject(mv, "tx", 0);
                cJSON_AddNumberToObject(mv, "ty", 0);
            }
            if (send_json(sockfd, mv) < 0) {
                fprintf(stderr, "Failed to send move/pass message\n");
                cJSON_Delete(mv);
                cJSON_Delete(msg);
                break;
            }
            waiting_for_result = 1;
            cJSON_Delete(mv);
            cJSON_Delete(msg);
            continue;
        }
        // move_ok / invalid_move / pass
        else if (strcmp(type, "move_ok") == 0 ||
                 strcmp(type, "invalid_move") == 0 ||
                 strcmp(type, "pass") == 0)
        {
            if (waiting_for_result) {
                printf("Move result: %s\n", type);
                printf("Next player's turn\n");
                waiting_for_result = 0;
            }
            cJSON *jbarr = cJSON_GetObjectItem(msg, "board");
            if (jbarr && cJSON_IsArray(jbarr)) {
                int nrows = cJSON_GetArraySize(jbarr);
                for (int i = 0; i < nrows && i < BOARD_SIZE; i++) {
                    const cJSON *jrow = cJSON_GetArrayItem(jbarr, i);
                    if (jrow && jrow->valuestring) {
                        memcpy(board_arr[i], jrow->valuestring, BOARD_SIZE);
                    }
                }
                update_led_matrix(board_arr);
            }
            cJSON_Delete(msg);
            continue;
        }
        // game_over
        else if (strcmp(type, "game_over") == 0) {
            printf("Game Over\n");
            cJSON *jbarr = cJSON_GetObjectItem(msg, "board");
            if (jbarr && cJSON_IsArray(jbarr)) {
                int nrows = cJSON_GetArraySize(jbarr);
                printf("Final board:\n");
                for (int i = 0; i < nrows && i < BOARD_SIZE; i++) {
                    const cJSON *jrow = cJSON_GetArrayItem(jbarr, i);
                    if (jrow && jrow->valuestring) {
                        memcpy(board_arr[i], jrow->valuestring, BOARD_SIZE);
                        char rowbuf[BOARD_SIZE+1];
                        memcpy(rowbuf, jrow->valuestring, BOARD_SIZE);
                        rowbuf[BOARD_SIZE] = '\0';
                        printf("%s\n", rowbuf);
                    }
                }
                update_led_matrix(board_arr);
            }
          // score
            cJSON *jscores = cJSON_GetObjectItem(msg, "scores");
            if (jscores && cJSON_IsObject(jscores)) {
                printf("Final scores:\n");
                cJSON *child = jscores->child;
                while (child) {
                    if (child->string) {
                        printf("  %s: %d\n", child->string, child->valueint);
                    }
                    child = child->next;
                }
            }
            cJSON_Delete(msg);
            break;
        }
        cJSON_Delete(msg);
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
