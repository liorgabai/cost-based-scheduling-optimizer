#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *input_file =
        (argc >= 2) ? argv[1] : "schedule_input.csv";

    const char *state_file =
        (argc >= 3) ? argv[2] : "schedule_state_updated.csv";

    SchedulerData data;

    printf("============================================================\n");
    printf("       COST-BASED SCHEDULING OPTIMIZATION SYSTEM\n");
    printf("============================================================\n");
    printf("Input: %s\n", input_file);

    if (!load_scheduler_file(input_file, &data)) {
        return 1;
    }

    if (!finalize_and_validate_data(&data)) {
        free_scheduler_data(&data);
        return 1;
    }

    printf("\nMode: %s\n", mode_name(data.mode));
    printf("People: %d\n", data.people_count);
    printf("Slots: %d\n", data.slot_count);

    if (data.mode == MODE_WORKFORCE) {
        printf("Appointments: %d\n", data.appointment_count);
    }

    int iteration = 1;

    while (1) {
        printf("\n================ OPTIMIZATION ROUND %d ================\n",
               iteration);

        ScheduleSolution solution;

        if (!solve_current_schedule(&data, &solution)) {
            printf("ERROR: Optimization engine failed internally.\n");
            free_scheduler_data(&data);
            return 2;
        }

        if (!solution.feasible) {
            printf("\n============================================================\n");
            printf("NO FEASIBLE SCHEDULE EXISTS\n");
            printf("UNDER THE CURRENT CONFIRMED HARD CONSTRAINTS.\n");
            printf("============================================================\n");
            save_scheduler_state(state_file, &data);
            free_solution(&solution);
            free_scheduler_data(&data);
            return 3;
        }

        int change_count = 0;
        RequiredChange *changes =
            collect_required_changes(&data, &solution, &change_count);

        if (change_count > 0 && !changes) {
            printf("ERROR: Could not allocate manager action list.\n");
            free_solution(&solution);
            free_scheduler_data(&data);
            return 2;
        }

        sort_required_changes(changes, change_count, &data);

        print_full_solution(
            &data, &solution, changes, change_count
        );

        if (change_count == 0) {
            printf("\nSCHEDULE ACCEPTED\n");
            printf("No calls or manual changes are required.\n");

            save_scheduler_state(state_file, &data);
            printf("State saved to: %s\n", state_file);

            free(changes);
            free_solution(&solution);
            break;
        }

        int works = prompt_yes_no(
            "\nDoes this solution work after checking the required "
            "changes in order? [Y/N]: "
        );

        if (works) {
            /*
             * If the manager says the solution works after checking in order,
             * every proposed soft move has been confirmed.
             */
            apply_all_changes_confirmed(
                &data, changes, change_count
            );

            save_scheduler_state(state_file, &data);

            printf("\n============================================================\n");
            printf("SCHEDULE ACCEPTED\n");
            printf("All required changes were confirmed.\n");
            printf("Confirmed availability has been stored.\n");
            printf("State saved to: %s\n", state_file);
            printf("============================================================\n");

            free(changes);
            free_solution(&solution);
            break;
        }

        printf("\nWhich required change could not be made?\n");
        for (int i = 0; i < change_count; i++) {
            int p = changes[i].person_index;
            int s = changes[i].slot_index;

            printf("%d. %s -> %s %s-%s",
                   i + 1,
                   data.people[p].name,
                   data.slots[s].date,
                   data.slots[s].start_time,
                   data.slots[s].end_time);

            if (changes[i].appointment_index >= 0) {
                int a = changes[i].appointment_index;
                printf(" for %s", data.appointments[a].name);
            }

            printf("\n");
        }

        int failed_number = prompt_int_range(
            "Enter the failed change number: ",
            1,
            change_count
        );

        int failed_index = failed_number - 1;

        printf("\nUPDATING KNOWLEDGE\n");
        printf("------------------------------------------------------------\n");

        for (int i = 0; i < failed_index; i++) {
            int p = changes[i].person_index;
            int s = changes[i].slot_index;

            printf("%s | %s %s-%s: UNAVAILABLE -> AVAILABLE\n",
                   data.people[p].name,
                   data.slots[s].date,
                   data.slots[s].start_time,
                   data.slots[s].end_time);
        }

        {
            int p = changes[failed_index].person_index;
            int s = changes[failed_index].slot_index;

            printf("%s | %s %s-%s: UNAVAILABLE -> HARD\n",
                   data.people[p].name,
                   data.slots[s].date,
                   data.slots[s].start_time,
                   data.slots[s].end_time);
        }

        if (failed_index + 1 < change_count) {
            printf(
                "All later proposed changes remain UNAVAILABLE "
                "(not checked yet).\n"
            );
        }

        apply_failed_change_feedback(
            &data, changes, change_count, failed_index
        );

        save_scheduler_state(state_file, &data);
        printf("Updated state saved to: %s\n", state_file);

        printf("\nRe-optimizing with the new confirmed information...\n");

        free(changes);
        free_solution(&solution);

        iteration++;
    }

    free_scheduler_data(&data);
    return 0;
}
