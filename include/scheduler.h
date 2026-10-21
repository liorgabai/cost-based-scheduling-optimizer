#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stddef.h>

#define MAX_ID_LEN 64
#define MAX_NAME_LEN 128
#define MAX_DATE_LEN 11
#define MAX_TIME_LEN 6
#define MAX_SEVERITY 1000000
#define COST_INF 1000000000000000LL

typedef enum {
    MODE_UNSET = 0,
    MODE_ONE_TO_ONE,
    MODE_WORKFORCE
} ScheduleMode;

typedef enum {
    STATUS_AVAILABLE = 0,
    STATUS_UNAVAILABLE,
    STATUS_HARD
} AvailabilityStatus;

typedef struct {
    char id[MAX_ID_LEN];
    char name[MAX_NAME_LEN];
    int severity;
    int max_assignments; /* 0 = unlimited in WORKFORCE mode */
} Person;

typedef struct {
    char id[MAX_ID_LEN];
    char date[MAX_DATE_LEN];
    char start_time[MAX_TIME_LEN];
    char end_time[MAX_TIME_LEN];
    int start_minutes;
    int end_minutes;
} Slot;

typedef struct {
    char id[MAX_ID_LEN];
    char name[MAX_NAME_LEN];
    int slot_index;
} Appointment;

typedef struct {
    ScheduleMode mode;
    int mode_was_explicit;

    int require_all_slots_filled;
    int require_all_slots_was_explicit;

    Person *people;
    int people_count;

    Slot *slots;
    int slot_count;

    Appointment *appointments;
    int appointment_count;

    /* Flattened matrix: status[person * slot_count + slot] */
    AvailabilityStatus *status;
} SchedulerData;

typedef struct {
    int *person_for_slot; /* ONE_TO_ONE, length slot_count, -1 means unused */
    int *slot_for_person; /* ONE_TO_ONE, length people_count */

    int *person_for_appointment; /* WORKFORCE, length appointment_count */

    long long total_cost;
    int feasible;
} ScheduleSolution;

typedef struct {
    int person_index;
    int slot_index;
    int appointment_index; /* -1 in ONE_TO_ONE */
    int cost;
} RequiredChange;

/* parser.c */
int load_scheduler_file(const char *filename, SchedulerData *data);
int finalize_and_validate_data(SchedulerData *data);
void free_scheduler_data(SchedulerData *data);
int save_scheduler_state(const char *filename, const SchedulerData *data);

/* shared helpers */
const char *mode_name(ScheduleMode mode);
const char *status_name(AvailabilityStatus status);
AvailabilityStatus get_status(const SchedulerData *data, int person, int slot);
void set_status(SchedulerData *data, int person, int slot, AvailabilityStatus st);

/* hungarian.c */
int solve_one_to_one(const SchedulerData *data, ScheduleSolution *solution);

/* min_cost_flow.c */
int solve_workforce(const SchedulerData *data, ScheduleSolution *solution);

/* engine.c */
int solve_current_schedule(const SchedulerData *data, ScheduleSolution *solution);
void free_solution(ScheduleSolution *solution);

RequiredChange *collect_required_changes(
    const SchedulerData *data,
    const ScheduleSolution *solution,
    int *change_count
);

void sort_required_changes(
    RequiredChange *changes,
    int count,
    const SchedulerData *data
);

void print_full_solution(
    const SchedulerData *data,
    const ScheduleSolution *solution,
    const RequiredChange *changes,
    int change_count
);

void apply_failed_change_feedback(
    SchedulerData *data,
    const RequiredChange *changes,
    int change_count,
    int failed_index
);

void apply_all_changes_confirmed(
    SchedulerData *data,
    const RequiredChange *changes,
    int change_count
);

/* interactive input */
int prompt_yes_no(const char *prompt);
int prompt_int_range(const char *prompt, int min_value, int max_value);

#endif
