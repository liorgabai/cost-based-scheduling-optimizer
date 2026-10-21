#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

int solve_one_to_one(const SchedulerData *data, ScheduleSolution *solution) {
    int n = data->people_count;
    int m = data->slot_count;

    if (n <= 0 || m <= 0 || n > m) {
        solution->feasible = 0;
        return 1;
    }

    long long *u = (long long *)calloc((size_t)n + 1, sizeof(long long));
    long long *v = (long long *)calloc((size_t)m + 1, sizeof(long long));
    int *p = (int *)calloc((size_t)m + 1, sizeof(int));
    int *way = (int *)calloc((size_t)m + 1, sizeof(int));
    long long *minv = (long long *)malloc(((size_t)m + 1) * sizeof(long long));
    unsigned char *used = (unsigned char *)malloc(((size_t)m + 1));

    if (!u || !v || !p || !way || !minv || !used) {
        free(u); free(v); free(p); free(way); free(minv); free(used);
        return 0;
    }

    for (int i = 1; i <= n; i++) {
        p[0] = i;
        int j0 = 0;

        for (int j = 0; j <= m; j++) {
            minv[j] = COST_INF;
            used[j] = 0;
        }

        do {
            used[j0] = 1;
            int i0 = p[j0];

            long long delta = COST_INF;
            int j1 = 0;

            for (int j = 1; j <= m; j++) {
                if (used[j]) continue;

                AvailabilityStatus st = get_status(data, i0 - 1, j - 1);
                long long base_cost;

                if (st == STATUS_AVAILABLE) {
                    base_cost = 0;
                } else if (st == STATUS_UNAVAILABLE) {
                    base_cost = data->people[i0 - 1].severity;
                } else {
                    base_cost = COST_INF / 4;
                }

                long long cur = base_cost - u[i0] - v[j];

                if (cur < minv[j]) {
                    minv[j] = cur;
                    way[j] = j0;
                }

                if (minv[j] < delta) {
                    delta = minv[j];
                    j1 = j;
                }
            }

            if (delta >= COST_INF / 8) {
                free(u); free(v); free(p); free(way); free(minv); free(used);
                solution->feasible = 0;
                return 1;
            }

            for (int j = 0; j <= m; j++) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }

            j0 = j1;
        } while (p[j0] != 0);

        do {
            int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    solution->slot_for_person = (int *)malloc((size_t)n * sizeof(int));
    solution->person_for_slot = (int *)malloc((size_t)m * sizeof(int));

    if (!solution->slot_for_person || !solution->person_for_slot) {
        free(u); free(v); free(p); free(way); free(minv); free(used);
        return 0;
    }

    for (int i = 0; i < n; i++) solution->slot_for_person[i] = -1;
    for (int j = 0; j < m; j++) solution->person_for_slot[j] = -1;

    for (int j = 1; j <= m; j++) {
        if (p[j] >= 1 && p[j] <= n) {
            int person = p[j] - 1;
            int slot = j - 1;
            solution->slot_for_person[person] = slot;
            solution->person_for_slot[slot] = person;
        }
    }

    long long total = 0;

    for (int person = 0; person < n; person++) {
        int slot = solution->slot_for_person[person];
        if (slot < 0) {
            solution->feasible = 0;
            goto cleanup;
        }

        AvailabilityStatus st = get_status(data, person, slot);
        if (st == STATUS_HARD) {
            solution->feasible = 0;
            goto cleanup;
        }

        if (st == STATUS_UNAVAILABLE) {
            total += data->people[person].severity;
        }
    }

    if (data->require_all_slots_filled) {
        for (int slot = 0; slot < m; slot++) {
            if (solution->person_for_slot[slot] < 0) {
                solution->feasible = 0;
                goto cleanup;
            }
        }
    }

    solution->total_cost = total;
    solution->feasible = 1;

cleanup:
    free(u); free(v); free(p); free(way); free(minv); free(used);
    return 1;
}
