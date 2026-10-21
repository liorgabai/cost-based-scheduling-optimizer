#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int solve_current_schedule(const SchedulerData *data, ScheduleSolution *solution) {
    memset(solution, 0, sizeof(*solution));

    if (data->mode == MODE_ONE_TO_ONE) {
        return solve_one_to_one(data, solution);
    }

    if (data->mode == MODE_WORKFORCE) {
        return solve_workforce(data, solution);
    }

    printf("ERROR: Scheduling mode is not set.\n");
    return 0;
}

void free_solution(ScheduleSolution *solution) {
    free(solution->person_for_slot);
    free(solution->slot_for_person);
    free(solution->person_for_appointment);
    memset(solution, 0, sizeof(*solution));
}

RequiredChange *collect_required_changes(
    const SchedulerData *data,
    const ScheduleSolution *solution,
    int *change_count
) {
    *change_count = 0;

    int max_changes = 0;
    if (data->mode == MODE_ONE_TO_ONE) {
        max_changes = data->people_count;
    } else {
        max_changes = data->appointment_count;
    }

    RequiredChange *changes = NULL;
    if (max_changes > 0) {
        changes = (RequiredChange *)malloc(
            (size_t)max_changes * sizeof(RequiredChange)
        );
        if (!changes) return NULL;
    }

    if (data->mode == MODE_ONE_TO_ONE) {
        for (int p = 0; p < data->people_count; p++) {
            int s = solution->slot_for_person[p];

            if (s >= 0 &&
                get_status(data, p, s) == STATUS_UNAVAILABLE) {
                changes[*change_count].person_index = p;
                changes[*change_count].slot_index = s;
                changes[*change_count].appointment_index = -1;
                changes[*change_count].cost = data->people[p].severity;
                (*change_count)++;
            }
        }
    } else {
        for (int a = 0; a < data->appointment_count; a++) {
            int p = solution->person_for_appointment[a];
            int s = data->appointments[a].slot_index;

            if (p >= 0 &&
                get_status(data, p, s) == STATUS_UNAVAILABLE) {
                changes[*change_count].person_index = p;
                changes[*change_count].slot_index = s;
                changes[*change_count].appointment_index = a;
                changes[*change_count].cost = data->people[p].severity;
                (*change_count)++;
            }
        }
    }

    return changes;
}

static int change_cmp(
    const RequiredChange *a,
    const RequiredChange *b,
    const SchedulerData *data
) {
    if (a->cost != b->cost) return a->cost < b->cost ? -1 : 1;

    /* deterministic tie breaker: original person order */
    if (a->person_index != b->person_index) {
        return a->person_index < b->person_index ? -1 : 1;
    }

    if (a->slot_index != b->slot_index) {
        return a->slot_index < b->slot_index ? -1 : 1;
    }

    if (a->appointment_index != b->appointment_index) {
        return a->appointment_index < b->appointment_index ? -1 : 1;
    }

    (void)data;
    return 0;
}

void sort_required_changes(
    RequiredChange *changes,
    int count,
    const SchedulerData *data
) {
    /* insertion sort is perfectly adequate for the small manager action list */
    for (int i = 1; i < count; i++) {
        RequiredChange key = changes[i];
        int j = i - 1;

        while (j >= 0 && change_cmp(&key, &changes[j], data) < 0) {
            changes[j + 1] = changes[j];
            j--;
        }

        changes[j + 1] = key;
    }
}

static void print_slot_label(const Slot *slot) {
    printf("%s %s-%s", slot->date, slot->start_time, slot->end_time);
}

void print_full_solution(
    const SchedulerData *data,
    const ScheduleSolution *solution,
    const RequiredChange *changes,
    int change_count
) {
    printf("\n============================================================\n");
    printf("                         RESULT\n");
    printf("============================================================\n");

    if (solution->total_cost == 0) {
        printf("A schedule with no rule violations was found.\n");
        printf("Damage Cost: 0\n");
    } else {
        printf("No schedule with Damage Cost 0 was found.\n");
        printf("This is the minimum-cost schedule currently available.\n");
        printf("Minimum Damage Cost: %lld\n", solution->total_cost);
    }

    printf("\nFULL SCHEDULE\n");
    printf("------------------------------------------------------------\n");

    if (data->mode == MODE_ONE_TO_ONE) {
        for (int s = 0; s < data->slot_count; s++) {
            print_slot_label(&data->slots[s]);
            printf(" -> ");

            int p = solution->person_for_slot[s];
            if (p >= 0) {
                printf("%s [%s]\n",
                       data->people[p].name,
                       data->people[p].id);
            } else {
                printf("[UNUSED]\n");
            }
        }
    } else {
        for (int s = 0; s < data->slot_count; s++) {
            printf("\n");
            print_slot_label(&data->slots[s]);
            printf("\n");

            int found = 0;
            for (int a = 0; a < data->appointment_count; a++) {
                if (data->appointments[a].slot_index != s) continue;
                found = 1;

                int p = solution->person_for_appointment[a];
                printf("  %s [%s] -> %s [%s]\n",
                       data->appointments[a].name,
                       data->appointments[a].id,
                       data->people[p].name,
                       data->people[p].id);
            }

            if (!found) {
                printf("  [NO APPOINTMENTS]\n");
            }
        }
    }

    printf("\nTOTAL DAMAGE COST: %lld\n", solution->total_cost);

    if (change_count == 0) {
        printf("\nNo manual intervention is required.\n");
        return;
    }

    printf("\nREQUIRED CHANGES\n");
    printf("------------------------------------------------------------\n");
    printf("Verify these changes in the exact order shown.\n\n");

    for (int i = 0; i < change_count; i++) {
        int p = changes[i].person_index;
        int s = changes[i].slot_index;

        printf("%d. Move %s to ", i + 1, data->people[p].name);
        print_slot_label(&data->slots[s]);

        if (changes[i].appointment_index >= 0) {
            int a = changes[i].appointment_index;
            printf(" for %s", data->appointments[a].name);
        }

        printf("\n   Damage Cost: %d\n", changes[i].cost);
    }

    printf(
        "\nThe order is intentional: lower-severity moves are checked first,\n"
        "so higher-priority people are contacted only if the cheaper changes\n"
        "have already been confirmed.\n"
    );
}

void apply_failed_change_feedback(
    SchedulerData *data,
    const RequiredChange *changes,
    int change_count,
    int failed_index
) {
    if (failed_index < 0 || failed_index >= change_count) return;

    for (int i = 0; i < failed_index; i++) {
        set_status(
            data,
            changes[i].person_index,
            changes[i].slot_index,
            STATUS_AVAILABLE
        );
    }

    set_status(
        data,
        changes[failed_index].person_index,
        changes[failed_index].slot_index,
        STATUS_HARD
    );

    /*
     * Crucially, changes after failed_index are untouched.
     * The manager never reached them, so the system learned nothing new.
     */
}

void apply_all_changes_confirmed(
    SchedulerData *data,
    const RequiredChange *changes,
    int change_count
) {
    for (int i = 0; i < change_count; i++) {
        set_status(
            data,
            changes[i].person_index,
            changes[i].slot_index,
            STATUS_AVAILABLE
        );
    }
}

static int read_line(char *buffer, size_t size) {
    if (!fgets(buffer, (int)size, stdin)) return 0;

    size_t len = strlen(buffer);
    while (len > 0 &&
           (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
        buffer[--len] = '\0';
    }

    return 1;
}

int prompt_yes_no(const char *prompt) {
    char buffer[64];

    while (1) {
        printf("%s", prompt);
        fflush(stdout);

        if (!read_line(buffer, sizeof(buffer))) {
            printf("\nInput ended unexpectedly.\n");
            exit(1);
        }

        for (char *p = buffer; *p; p++) {
            *p = (char)toupper((unsigned char)*p);
        }

        if (strcmp(buffer, "Y") == 0 || strcmp(buffer, "YES") == 0) {
            return 1;
        }

        if (strcmp(buffer, "N") == 0 || strcmp(buffer, "NO") == 0) {
            return 0;
        }

        printf("Please enter Y or N.\n");
    }
}

int prompt_int_range(const char *prompt, int min_value, int max_value) {
    char buffer[64];

    while (1) {
        printf("%s", prompt);
        fflush(stdout);

        if (!read_line(buffer, sizeof(buffer))) {
            printf("\nInput ended unexpectedly.\n");
            exit(1);
        }

        char *end = NULL;
        long value = strtol(buffer, &end, 10);

        if (end != buffer && *end == '\0' &&
            value >= min_value && value <= max_value) {
            return (int)value;
        }

        printf("Please enter a number from %d to %d.\n",
               min_value, max_value);
    }
}
