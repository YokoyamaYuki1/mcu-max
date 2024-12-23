/*
 * mcu-max UCI chess interface example
 *
 * (C) 2022-2024 Gissio
 *
 * License: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mcu-max.h"
#include <time.h> // clock_t のために必要　追加
#define MAIN_VALID_MOVES_NUM 512
//#define LEGAL_MOVES_THRESHOLD 50 // 合法手の総和の閾値

void print_board()
{
    const char *symbols = ".PPNKBRQ.ppnkbrq";

    printf("  +-----------------+\n");

    for (uint32_t y = 0; y < 8; y++)
    {
        printf("%d | ", 8 - y);
        for (uint32_t x = 0; x < 8; x++)
            printf("%c ", symbols[mcumax_get_piece(0x10 * y + x)]);
        printf("|\n");
    }

    printf("  +-----------------+\n");
    printf("    a b c d e f g h\n");

    printf("\n");
}

mcumax_square get_square(char *s)
{
    mcumax_square rank = s[0] - 'a';
    if (rank > 7)
        return MCUMAX_SQUARE_INVALID;

    mcumax_square file = '8' - s[1];
    if (file > 7)
        return MCUMAX_SQUARE_INVALID;

    return 0x10 * file + rank;
}

bool is_square_valid(char *s)
{
    return (get_square(s) != MCUMAX_SQUARE_INVALID);
}

bool is_move_valid(char *s)
{
    return is_square_valid(s) && is_square_valid(s + 2);
}

void print_square(mcumax_square square)
{
    printf("%c%c",
           'a' + ((square & 0x07) >> 0),
           '1' + 7 - ((square & 0x70) >> 4));
}

void print_move(mcumax_move move)
{
    if ((move.from == MCUMAX_SQUARE_INVALID) ||
        (move.to == MCUMAX_SQUARE_INVALID))
        printf("(none)");
    else
    {
        print_square(move.from);
        print_square(move.to);
    }
}
/*削除開始//追加開始
mcumax_move dynamic_search_with_time_limit(uint32_t max_time_ms, clock_t start_time)
{
    mcumax_move best_move = MCUMAX_MOVE_INVALID;
    uint32_t node_max = 100;
    uint32_t max_depth = 30;

    for (uint32_t depth = 1; depth <= max_depth; depth++)
    {
        clock_t current_time = clock();
        double elapsed_time_ms = 1000.0 * (current_time - start_time) / CLOCKS_PER_SEC;

        if (elapsed_time_ms >= max_time_ms)
            break;

        mcumax_move move = mcumax_search_best_move(node_max, depth);
        if (move.from == MCUMAX_SQUARE_INVALID || move.to == MCUMAX_SQUARE_INVALID)
            break;

        best_move = move;

        double remaining_time_ms = max_time_ms - elapsed_time_ms;
        if (remaining_time_ms > 5000)
            node_max *= 2;
        else if (remaining_time_ms < 1000)
            node_max = (node_max > 10) ? node_max / 2 : 10;
    }
    return best_move;
}//追加終了削除終了*/
// 追加開始: 時間制限いっぱいまで探索
#include <stdlib.h> // qsort のために必要

// 1手の評価構造体
typedef struct
{
    mcumax_move move;
    int32_t score;
} MoveEvaluation;

// qsort 用の比較関数（降順）
int compare_moves(const void *a, const void *b)
{
    MoveEvaluation *move_a = (MoveEvaluation *)a;
    MoveEvaluation *move_b = (MoveEvaluation *)b;
    return move_b->score - move_a->score; // 評価の降順にソート
}

// 修正版: 時間制限いっぱいまで探索、組み合わせ数に閾値を設ける
mcumax_move dynamic_search_with_time_limit(uint32_t max_time_ms, clock_t start_time, uint32_t move_limit)
{
    mcumax_move best_move = MCUMAX_MOVE_INVALID;
    uint32_t current_depth = 1;  // 現在の探索深さ

    while (true)
    {
        clock_t current_time = clock();
        double elapsed_time_ms = 1000.0 * (current_time - start_time) / CLOCKS_PER_SEC;

        // 時間制限に達した場合、探索を終了
        if (elapsed_time_ms >= max_time_ms)
            break;

        // 全ての合法手を取得
        mcumax_move valid_moves[MAIN_VALID_MOVES_NUM];
        uint32_t valid_moves_num = mcumax_search_valid_moves(valid_moves, MAIN_VALID_MOVES_NUM);

        // 評価結果を保持する配列を用意
        MoveEvaluation evaluated_moves[MAIN_VALID_MOVES_NUM];
        uint32_t evaluated_count = 0;

        // 各手を評価
        for (uint32_t i = 0; i < valid_moves_num; i++)
        {
            mcumax_move move = valid_moves[i];
            if (move.from == MCUMAX_SQUARE_INVALID || move.to == MCUMAX_SQUARE_INVALID)
                continue;

            // 仮にその手を指した状態でスコアを取得
            mcumax_play_move(move);
            int32_t score = mcumax_search(-MCUMAX_SCORE_MAX, MCUMAX_SCORE_MAX, mcumax.score, mcumax.en_passant_square, 1, MCUMAX_INTERNAL_NODE);
            mcumax_play_move((mcumax_move){move.to, move.from}); // 元の状態に戻す

            evaluated_moves[evaluated_count++] = (MoveEvaluation){move, score};
        }

        // 評価結果をソート（降順）
        qsort(evaluated_moves, evaluated_count, sizeof(MoveEvaluation), compare_moves);

        // 組み合わせが閾値を超える場合は上位 move_limit 個に絞る
        if (evaluated_count > move_limit)
        {
            evaluated_count = move_limit;
        }

        // 次の深さで探索を実行
        for (uint32_t i = 0; i < evaluated_count; i++)
        {
            mcumax_move move = evaluated_moves[i].move;

            // 現在の深さで探索
            mcumax_play_move(move);
            mcumax_move next_best_move = mcumax_search_best_move(0, current_depth);
            mcumax_play_move((mcumax_move){move.to, move.from}); // 元の状態に戻す

            // 最良手を更新
            if (next_best_move.from != MCUMAX_SQUARE_INVALID && next_best_move.to != MCUMAX_SQUARE_INVALID)
            {
                best_move = next_best_move;
            }
        }

        // 探索深さを増やす
        current_depth++;
    }

    return best_move;
}

//追加終了

bool send_uci_command(char *line)
{
    char *token = strtok(line, " \n");

    if (!token)
        return false;

    if (!strcmp(token, "uci"))
    {
        printf("id name " MCUMAX_ID "\n");
        printf("id author " MCUMAX_AUTHOR "\n");
        printf("uciok\n");
    }
/*    // "go" コマンドの処理に movetime を追加開始
    else if (!strcmp(token, "go"))
    {
        uint32_t movetime_ms = 100; // デフォルト探索時間
        while ((token = strtok(NULL, " \n")))
        {
            if (!strcmp(token, "movetime"))
            {
                token = strtok(NULL, " \n");
                if (token)
                {
                    movetime_ms = atoi(token); // movetimeを取得
                }
            }
        }
        clock_t start_time = clock();
        mcumax_move best_move = dynamic_search_with_time_limit(movetime_ms, start_time,512);

        printf("bestmove ");
        print_move(best_move);
        printf("\n");
    }
    //追加終了*/
    else if (!strcmp(token, "uci") ||
             !strcmp(token, "ucinewgame"))
        mcumax_init();
    else if (!strcmp(token, "isready"))
        printf("readyok\n");
    else if (!strcmp(token, "d"))
        print_board();
    else if (!strcmp(token, "l"))
    {
        mcumax_move valid_moves[MAIN_VALID_MOVES_NUM];
        uint32_t valid_moves_num = mcumax_search_valid_moves(valid_moves, MAIN_VALID_MOVES_NUM);

        for (uint32_t i = 0; i < valid_moves_num; i++)
        {
            print_move(valid_moves[i]);
            printf(" ");
        }
        printf("\n");
    }
    else if (!strcmp(token, "position"))
    {
        int fen_index = 0;
        char fen_string[256];

        while (token = strtok(NULL, " \n"))
        {
            if (fen_index)
            {
                strcat(fen_string, token);
                strcat(fen_string, " ");

                fen_index++;
                if (fen_index > 6)
                {
                    mcumax_set_fen_position(fen_string);

                    fen_index = 0;
                }
            }
            else
            {
                if (!strcmp(token, "startpos"))
                    mcumax_init();
                else if (!strcmp(token, "fen"))
                {
                    fen_index = 1;
                    strcpy(fen_string, "");
                }
                else if (is_move_valid(token))
                {
                    mcumax_play_move((mcumax_move){
                        get_square(token + 0),
                        get_square(token + 2),
                    });
                }
            }
        }
    }
/*削除開始
    else if (!strcmp(token, "go"))
    {
        mcumax_move move = mcumax_search_best_move(1, 30);
        mcumax_play_move(move);

        printf("bestmove ");
        print_move(move);
        printf("\n");
    }
削除終了*/
    // "go" コマンドの処理に movetime を追加開始
    else if (!strcmp(token, "go"))
    {
        uint32_t movetime_ms = 100; // デフォルト探索時間
        while ((token = strtok(NULL, " \n")))
        {
            if (!strcmp(token, "movetime"))
            {
                token = strtok(NULL, " \n");
                if (token)
                {
                    movetime_ms = atoi(token); // movetimeを取得
                }
            }
        }
        clock_t start_time = clock();
        mcumax_move best_move = dynamic_search_with_time_limit(movetime_ms, start_time,512);

        printf("bestmove ");
        print_move(best_move);
        printf("\n");
    }
    //追加終了
    else if (!strcmp(token, "quit"))
        return true;
    else
        printf("Unknown command: %s\n", token);

    return false;
}

int main()
{
    mcumax_init();

    while (true)
    {
        fflush(stdout);

        char line[65536];
        fgets(line, sizeof(line), stdin);

        if (send_uci_command(line))
            break;
    }
}
