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
#include <time.h>
#include <stdint.h>

#define MAIN_VALID_MOVES_NUM 512
#define MOVE_EVALUATION_LIMIT 30 // 上位30手までに制限

typedef struct
{
    mcumax_move move;
    int32_t score;
} MoveEvaluation;

int compare_moves(const void *a, const void *b)
{
    MoveEvaluation *move_a = (MoveEvaluation *)a;
    MoveEvaluation *move_b = (MoveEvaluation *)b;
    return move_b->score - move_a->score;
}

mcumax_move search_with_constraints(uint32_t max_time_ms)
{
    mcumax_move best_move = MCUMAX_MOVE_INVALID;
    clock_t start_time = clock();
    mcumax_move valid_moves[MAIN_VALID_MOVES_NUM];
    uint32_t valid_moves_num = mcumax_search_valid_moves(valid_moves, MAIN_VALID_MOVES_NUM);

    MoveEvaluation evaluated_moves[MAIN_VALID_MOVES_NUM];
    uint32_t evaluated_count = 0;

    for (uint32_t i = 0; i < valid_moves_num; i++)
    {
        mcumax_move move = valid_moves[i];
        mcumax_play_move(move);
        int32_t score = mcumax_search(-MCUMAX_SCORE_MAX, MCUMAX_SCORE_MAX, 0, MCUMAX_SQUARE_INVALID, 1, MCUMAX_INTERNAL_NODE);
        mcumax_play_move((mcumax_move){move.to, move.from});
        evaluated_moves[evaluated_count++] = (MoveEvaluation){move, score};
    }

    qsort(evaluated_moves, evaluated_count, sizeof(MoveEvaluation), compare_moves);
    if (evaluated_count > MOVE_EVALUATION_LIMIT)
        evaluated_count = MOVE_EVALUATION_LIMIT;

    for (uint32_t i = 0; i < evaluated_count; i++)
    {
        if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC >= max_time_ms)
            break;

        mcumax_play_move(evaluated_moves[i].move);
        int32_t score = mcumax_search(-MCUMAX_SCORE_MAX, MCUMAX_SCORE_MAX, 0, MCUMAX_SQUARE_INVALID, 2, MCUMAX_INTERNAL_NODE);
        mcumax_play_move((mcumax_move){evaluated_moves[i].move.to, evaluated_moves[i].move.from});

        if (score > best_move.from) // Better move found
            best_move = evaluated_moves[i].move;
    }

    return best_move;
}

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
    else if (!strcmp(token, "go"))
    {
        uint32_t movetime_ms = 1000; // デフォルト1秒
        while ((token = strtok(NULL, " \n")))
        {
            if (!strcmp(token, "movetime"))
            {
                token = strtok(NULL, " \n");
                if (token)
                    movetime_ms = atoi(token);
            }
        }

        mcumax_move best_move = search_with_constraints(movetime_ms);
        printf("bestmove ");
        print_move(best_move);
        printf("\n");
    }
    else if (!strcmp(token, "quit"))
        return true;

    return false;
}

int main()
{
    mcumax_init();

    while (true)
    {
        char line[65536];
        fgets(line, sizeof(line), stdin);

        if (send_uci_command(line))
            break;
    }

    return 0;
}
