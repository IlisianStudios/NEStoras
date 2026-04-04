//
// Created by Pandora on 4/4/26.
//
#pragma once

#ifndef NESTORAS_NESTEST_COMPARE_H
#define NESTORAS_NESTEST_COMPARE_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "cpu.h"
#include "instruction.h"

typedef struct {
    uint16_t pc;
    uint8_t a, x, y, p, sp;
    uint32_t cyc;
    char mnemonic[32];
} NestestLine;

static bool parse_nestest_line(const char *line, NestestLine *out) {
    // C000  4C F5 C5  JMP $C5F5                       A:00 X:00 Y:00 P:24 SP:FD CYC:7
    if (strlen(line) < 10) return false;

    unsigned int pc, a, x, y, p, sp, cyc;

    if (sscanf(line, "%4X", &pc) != 1) return false;
    out->pc = (uint16_t)pc;

    // mnemonic starts at col 16, length up to 32 chars
    char mn_buf[33] = {0};
    strncpy(mn_buf, line + 16, 32);
    // trim trailing spaces
    for (int i = 31; i >= 0; i--) {
        if (mn_buf[i] == ' ' || mn_buf[i] == '\0') mn_buf[i] = '\0';
        else break;
    }
    strncpy(out->mnemonic, mn_buf, 31);

    if (sscanf(line + 48, "A:%2X X:%2X Y:%2X P:%2X SP:%2X PPU:%*[^C]CYC:%u",
       &a, &x, &y, &p, &sp, &cyc)!= 6) return false;

    out->a   = (uint8_t)a;
    out->x   = (uint8_t)x;
    out->y   = (uint8_t)y;
    out->p   = (uint8_t)p;
    out->sp  = (uint8_t)sp;
    out->cyc = cyc;

    return true;
}

typedef struct {
    FILE    *file;
    uint32_t line_num;
} NestestLog;

extern NestestLog nestest_log;

static inline bool nestest_open(NestestLog *log, const char *path) {
    log->file = fopen(path, "r");
    log->line_num = 0;
    return log->file != NULL;
}

static inline void nestest_close(NestestLog *log) {
    if (log->file) fclose(log->file);
    log->file = NULL;
}

// Call this after every cpu_step instead of log_cpu_state.
// Returns false and halts on first mismatch.
static bool nestest_compare(NestestLog *log, const CPU *cpu, const Instruction *inst, uint16_t pc) {
    char line[128];
    if (!fgets(line, sizeof(line), log->file)) {
        printf("[NESTEST] end of reference log at line %u\n", log->line_num);
        return false;
    }
    // strip newline
    line[strcspn(line, "\r\n")] = '\0';
    log->line_num++;

    NestestLine ref;
    if (!parse_nestest_line(line, &ref)) {
        printf("[NESTEST] failed to parse line %u: %s\n", log->line_num, line);
        return false;
    }

    bool ok = true;

    #define CHECK(field, fmt, got, exp) \
        if ((got) != (exp)) { \
            printf("[NESTEST] line %u " field " mismatch: got " fmt " expected " fmt "\n", \
                   log->line_num, (got), (exp)); \
            ok = false; \
        }

    CHECK("PC",  "%04X", pc,        ref.pc)
    CHECK("A",   "%02X", cpu->a,    ref.a)
    CHECK("X",   "%02X", cpu->x,    ref.x)
    CHECK("Y",   "%02X", cpu->y,    ref.y)
    CHECK("P",   "%02X", cpu->status, ref.p)
    CHECK("SP",  "%02X", cpu->sp,   ref.sp)
    CHECK("CYC", "%u",   (uint32_t)cpu->total_cycles, ref.cyc)

    #undef CHECK

    if (!ok) {
        printf("[NESTEST] reference: %s\n", line);
        printf("[NESTEST] yours:     %04X  %-31s A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu\n",
               pc, inst->name,
               cpu->a, cpu->x, cpu->y, cpu->status, cpu->sp, (unsigned long long)cpu->total_cycles);
        return false;
    }

    return true;
}

#endif //NESTORAS_NESTEST_COMPARE_H
