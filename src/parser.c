#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define EXPECTED_COLUMNS 10

typedef struct {
    char *fields[EXPECTED_COLUMNS];
} RawRow;

typedef struct {
    RawRow *rows;
    int count;
    int capacity;
} RawTable;

static char *dup_string(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = (char *)malloc(n);
    if (!copy) return NULL;
    memcpy(copy, s, n);
    return copy;
}

static void trim_in_place(char *s) {
    if (!s) return;

    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;

    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static int split_csv_simple(char *line, char *out[], int max_out) {
    int count = 0;
    char *start = line;

    while (1) {
        if (count >= max_out) return count + 1;
        out[count++] = start;

        char *comma = strchr(start, ',');
        if (!comma) break;

        *comma = '\0';
        start = comma + 1;
    }

    return count;
}

static int push_raw_row(RawTable *table, char *fields[]) {
    if (table->count == table->capacity) {
        int new_capacity = table->capacity == 0 ? 32 : table->capacity * 2;
        RawRow *new_rows = (RawRow *)realloc(
            table->rows, (size_t)new_capacity * sizeof(RawRow)
        );
        if (!new_rows) return 0;
        table->rows = new_rows;
        table->capacity = new_capacity;
    }

    RawRow *row = &table->rows[table->count];
    for (int i = 0; i < EXPECTED_COLUMNS; i++) {
        trim_in_place(fields[i]);
        row->fields[i] = dup_string(fields[i]);
        if (!row->fields[i]) return 0;
    }

    table->count++;
    return 1;
}

static void free_raw_table(RawTable *table) {
    for (int r = 0; r < table->count; r++) {
        for (int c = 0; c < EXPECTED_COLUMNS; c++) {
            free(table->rows[r].fields[c]);
        }
    }
    free(table->rows);
    table->rows = NULL;
    table->count = 0;
    table->capacity = 0;
}

static int parse_int_strict(const char *s, int min_value, int max_value, int *out) {
    if (!s || !*s) return 0;

    errno = 0;
    char *end = NULL;
    long value = strtol(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0') return 0;
    if (value < min_value || value > max_value) return 0;

    *out = (int)value;
    return 1;
}

static int is_leap_year(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int valid_date(const char *s) {
    if (!s || strlen(s) != 10) return 0;
    if (s[4] != '-' || s[7] != '-') return 0;

    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (!isdigit((unsigned char)s[i])) return 0;
    }

    int y = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
    int m = (s[5]-'0')*10 + (s[6]-'0');
    int d = (s[8]-'0')*10 + (s[9]-'0');

    if (y < 1900 || m < 1 || m > 12 || d < 1) return 0;

    static const int days[] = {
        0,31,28,31,30,31,30,31,31,30,31,30,31
    };

    int max_day = days[m];
    if (m == 2 && is_leap_year(y)) max_day = 29;

    return d <= max_day;
}

static int parse_time(const char *s, int *minutes) {
    if (!s || strlen(s) != 5 || s[2] != ':') return 0;
    if (!isdigit((unsigned char)s[0]) ||
        !isdigit((unsigned char)s[1]) ||
        !isdigit((unsigned char)s[3]) ||
        !isdigit((unsigned char)s[4])) {
        return 0;
    }

    int h = (s[0]-'0')*10 + (s[1]-'0');
    int m = (s[3]-'0')*10 + (s[4]-'0');

    if (h < 0 || h > 23 || m < 0 || m > 59) return 0;

    *minutes = h * 60 + m;
    return 1;
}

static int has_comma_or_newline(const char *s) {
    return strchr(s, ',') != NULL ||
           strchr(s, '\n') != NULL ||
           strchr(s, '\r') != NULL;
}

static int find_person(const SchedulerData *data, const char *id) {
    for (int i = 0; i < data->people_count; i++) {
        if (strcmp(data->people[i].id, id) == 0) return i;
    }
    return -1;
}

static int find_slot(const SchedulerData *data, const char *id) {
    for (int i = 0; i < data->slot_count; i++) {
        if (strcmp(data->slots[i].id, id) == 0) return i;
    }
    return -1;
}

static int find_appointment(const SchedulerData *data, const char *id) {
    for (int i = 0; i < data->appointment_count; i++) {
        if (strcmp(data->appointments[i].id, id) == 0) return i;
    }
    return -1;
}

static int parse_status_str(const char *s, AvailabilityStatus *st) {
    if (strcmp(s, "AVAILABLE") == 0) {
        *st = STATUS_AVAILABLE;
        return 1;
    }
    if (strcmp(s, "UNAVAILABLE") == 0) {
        *st = STATUS_UNAVAILABLE;
        return 1;
    }
    if (strcmp(s, "HARD") == 0) {
        *st = STATUS_HARD;
        return 1;
    }
    return 0;
}

const char *mode_name(ScheduleMode mode) {
    switch (mode) {
        case MODE_ONE_TO_ONE: return "ONE_TO_ONE";
        case MODE_WORKFORCE: return "WORKFORCE";
        default: return "UNSET";
    }
}

const char *status_name(AvailabilityStatus status) {
    switch (status) {
        case STATUS_AVAILABLE: return "AVAILABLE";
        case STATUS_UNAVAILABLE: return "UNAVAILABLE";
        case STATUS_HARD: return "HARD";
        default: return "UNKNOWN";
    }
}

AvailabilityStatus get_status(const SchedulerData *data, int person, int slot) {
    return data->status[person * data->slot_count + slot];
}

void set_status(SchedulerData *data, int person, int slot, AvailabilityStatus st) {
    data->status[person * data->slot_count + slot] = st;
}

static int append_person(SchedulerData *data, const Person *p) {
    Person *new_people = (Person *)realloc(
        data->people, (size_t)(data->people_count + 1) * sizeof(Person)
    );
    if (!new_people) return 0;

    data->people = new_people;
    data->people[data->people_count++] = *p;
    return 1;
}

static int append_slot(SchedulerData *data, const Slot *s) {
    Slot *new_slots = (Slot *)realloc(
        data->slots, (size_t)(data->slot_count + 1) * sizeof(Slot)
    );
    if (!new_slots) return 0;

    data->slots = new_slots;
    data->slots[data->slot_count++] = *s;
    return 1;
}

static int append_appointment(SchedulerData *data, const Appointment *a) {
    Appointment *new_items = (Appointment *)realloc(
        data->appointments,
        (size_t)(data->appointment_count + 1) * sizeof(Appointment)
    );
    if (!new_items) return 0;

    data->appointments = new_items;
    data->appointments[data->appointment_count++] = *a;
    return 1;
}

int load_scheduler_file(const char *filename, SchedulerData *data) {
    memset(data, 0, sizeof(*data));

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("ERROR: Could not open input file: %s\n", filename);
        return 0;
    }

    char line[2048];

    if (!fgets(line, sizeof(line), fp)) {
        printf("ERROR: Input file is empty.\n");
        fclose(fp);
        return 0;
    }

    RawTable table = {0};

    int line_number = 1;
    while (fgets(line, sizeof(line), fp)) {
        line_number++;

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';

        if (len == 0) continue;

        char *fields[EXPECTED_COLUMNS + 1] = {0};
        int count = split_csv_simple(line, fields, EXPECTED_COLUMNS + 1);

        if (count != EXPECTED_COLUMNS) {
            printf(
                "ERROR: CSV line %d must contain exactly %d columns.\n"
                "This V1 contract intentionally does not support commas inside fields.\n",
                line_number, EXPECTED_COLUMNS
            );
            free_raw_table(&table);
            fclose(fp);
            return 0;
        }

        if (!push_raw_row(&table, fields)) {
            printf("ERROR: Out of memory while reading input.\n");
            free_raw_table(&table);
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);

    /* PASS 1: CONFIG */
    for (int r = 0; r < table.count; r++) {
        char **f = table.rows[r].fields;
        if (strcmp(f[0], "CONFIG") != 0) continue;

        if (strcmp(f[1], "MODE") == 0) {
            if (data->mode_was_explicit) {
                printf("ERROR: Duplicate CONFIG MODE row.\n");
                free_raw_table(&table);
                return 0;
            }

            if (strcmp(f[2], "ONE_TO_ONE") == 0) {
                data->mode = MODE_ONE_TO_ONE;
            } else if (strcmp(f[2], "WORKFORCE") == 0) {
                data->mode = MODE_WORKFORCE;
            } else {
                printf("ERROR: MODE must be ONE_TO_ONE or WORKFORCE.\n");
                free_raw_table(&table);
                return 0;
            }

            data->mode_was_explicit = 1;
        }
        else if (strcmp(f[1], "REQUIRE_ALL_SLOTS_FILLED") == 0) {
            if (data->require_all_slots_was_explicit) {
                printf("ERROR: Duplicate REQUIRE_ALL_SLOTS_FILLED row.\n");
                free_raw_table(&table);
                return 0;
            }

            if (strcmp(f[2], "YES") == 0) {
                data->require_all_slots_filled = 1;
            } else if (strcmp(f[2], "NO") == 0) {
                data->require_all_slots_filled = 0;
            } else {
                printf("ERROR: REQUIRE_ALL_SLOTS_FILLED must be YES or NO.\n");
                free_raw_table(&table);
                return 0;
            }

            data->require_all_slots_was_explicit = 1;
        }
        else {
            printf("ERROR: Unknown CONFIG key: %s\n", f[1]);
            free_raw_table(&table);
            return 0;
        }
    }

    /* PASS 2: SLOT + PERSON */
    for (int r = 0; r < table.count; r++) {
        char **f = table.rows[r].fields;

        if (strcmp(f[0], "SLOT") == 0) {
            if (!*f[1] || !*f[5] || !*f[6] || !*f[7]) {
                printf("ERROR: SLOT requires ID, DATE, START_TIME and END_TIME.\n");
                free_raw_table(&table);
                return 0;
            }

            if (find_slot(data, f[1]) >= 0) {
                printf("ERROR: Duplicate slot ID: %s\n", f[1]);
                free_raw_table(&table);
                return 0;
            }

            if (!valid_date(f[5])) {
                printf("ERROR: Invalid date for slot %s: %s\n", f[1], f[5]);
                free_raw_table(&table);
                return 0;
            }

            int start_minutes, end_minutes;
            if (!parse_time(f[6], &start_minutes) ||
                !parse_time(f[7], &end_minutes) ||
                start_minutes >= end_minutes) {
                printf("ERROR: Invalid time range for slot %s: %s-%s\n",
                       f[1], f[6], f[7]);
                free_raw_table(&table);
                return 0;
            }

            Slot s = {0};
            snprintf(s.id, sizeof(s.id), "%s", f[1]);
            snprintf(s.date, sizeof(s.date), "%s", f[5]);
            snprintf(s.start_time, sizeof(s.start_time), "%s", f[6]);
            snprintf(s.end_time, sizeof(s.end_time), "%s", f[7]);
            s.start_minutes = start_minutes;
            s.end_minutes = end_minutes;

            if (!append_slot(data, &s)) {
                printf("ERROR: Out of memory while storing slots.\n");
                free_raw_table(&table);
                return 0;
            }
        }

        else if (strcmp(f[0], "PERSON") == 0) {
            if (!*f[1] || !*f[2] || !*f[3]) {
                printf("ERROR: PERSON requires ID, NAME and SEVERITY.\n");
                free_raw_table(&table);
                return 0;
            }

            if (find_person(data, f[1]) >= 0) {
                printf("ERROR: Duplicate person ID: %s\n", f[1]);
                free_raw_table(&table);
                return 0;
            }

            if (has_comma_or_newline(f[2])) {
                printf("ERROR: Person names cannot contain commas or newlines.\n");
                free_raw_table(&table);
                return 0;
            }

            int severity;
            if (!parse_int_strict(f[3], 1, MAX_SEVERITY, &severity)) {
                printf(
                    "ERROR: Severity for %s must be an integer from 1 to %d.\n",
                    f[2], MAX_SEVERITY
                );
                free_raw_table(&table);
                return 0;
            }

            int max_assignments = 0;
            if (*f[4]) {
                if (!parse_int_strict(f[4], 0, 1000000, &max_assignments)) {
                    printf("ERROR: Invalid MAX_ASSIGNMENTS for %s.\n", f[2]);
                    free_raw_table(&table);
                    return 0;
                }
            }

            Person p = {0};
            snprintf(p.id, sizeof(p.id), "%s", f[1]);
            snprintf(p.name, sizeof(p.name), "%s", f[2]);
            p.severity = severity;
            p.max_assignments = max_assignments;

            if (!append_person(data, &p)) {
                printf("ERROR: Out of memory while storing people.\n");
                free_raw_table(&table);
                return 0;
            }
        }
    }

    /* PASS 3: APPOINTMENTS */
    for (int r = 0; r < table.count; r++) {
        char **f = table.rows[r].fields;
        if (strcmp(f[0], "APPOINTMENT") != 0) continue;

        if (!*f[1] || !*f[2] || !*f[9]) {
            printf("ERROR: APPOINTMENT requires ID, NAME and SLOT_ID.\n");
            free_raw_table(&table);
            return 0;
        }

        if (find_appointment(data, f[1]) >= 0) {
            printf("ERROR: Duplicate appointment ID: %s\n", f[1]);
            free_raw_table(&table);
            return 0;
        }

        int slot = find_slot(data, f[9]);
        if (slot < 0) {
            printf("ERROR: Appointment %s references unknown slot %s.\n",
                   f[1], f[9]);
            free_raw_table(&table);
            return 0;
        }

        Appointment a = {0};
        snprintf(a.id, sizeof(a.id), "%s", f[1]);
        snprintf(a.name, sizeof(a.name), "%s", f[2]);
        a.slot_index = slot;

        if (!append_appointment(data, &a)) {
            printf("ERROR: Out of memory while storing appointments.\n");
            free_raw_table(&table);
            return 0;
        }
    }

    if (data->people_count <= 0 || data->slot_count <= 0) {
        printf("ERROR: Input requires at least one PERSON and one SLOT.\n");
        free_raw_table(&table);
        return 0;
    }

    size_t status_count = (size_t)data->people_count * (size_t)data->slot_count;
    data->status = (AvailabilityStatus *)malloc(
        status_count * sizeof(AvailabilityStatus)
    );
    unsigned char *seen = (unsigned char *)calloc(status_count, 1);

    if (!data->status || !seen) {
        printf("ERROR: Out of memory while creating availability matrix.\n");
        free(seen);
        free_raw_table(&table);
        return 0;
    }

    for (size_t i = 0; i < status_count; i++) {
        data->status[i] = STATUS_HARD; /* temporary until explicit row arrives */
    }

    /* PASS 4: AVAILABILITY */
    for (int r = 0; r < table.count; r++) {
        char **f = table.rows[r].fields;
        if (strcmp(f[0], "AVAILABILITY") != 0) continue;

        if (!*f[1] || !*f[8] || !*f[9]) {
            printf("ERROR: AVAILABILITY requires PERSON ID, STATUS and SLOT_ID.\n");
            free(seen);
            free_raw_table(&table);
            return 0;
        }

        int p = find_person(data, f[1]);
        int s = find_slot(data, f[9]);

        if (p < 0) {
            printf("ERROR: AVAILABILITY references unknown person %s.\n", f[1]);
            free(seen);
            free_raw_table(&table);
            return 0;
        }

        if (s < 0) {
            printf("ERROR: AVAILABILITY references unknown slot %s.\n", f[9]);
            free(seen);
            free_raw_table(&table);
            return 0;
        }

        AvailabilityStatus st;
        if (strcmp(f[8], "UNKNOWN") == 0) {
            printf(
                "INPUT VALIDATION FAILED\n"
                "Manual review required: %s × %s is UNKNOWN.\n",
                data->people[p].name, data->slots[s].id
            );
            free(seen);
            free_raw_table(&table);
            return 0;
        }

        if (!parse_status_str(f[8], &st)) {
            printf("ERROR: Invalid STATUS '%s'.\n", f[8]);
            free(seen);
            free_raw_table(&table);
            return 0;
        }

        size_t idx = (size_t)p * (size_t)data->slot_count + (size_t)s;
        if (seen[idx]) {
            printf(
                "ERROR: Duplicate AVAILABILITY row for %s × %s.\n",
                data->people[p].name, data->slots[s].id
            );
            free(seen);
            free_raw_table(&table);
            return 0;
        }

        seen[idx] = 1;
        data->status[idx] = st;
    }

    for (int p = 0; p < data->people_count; p++) {
        for (int s = 0; s < data->slot_count; s++) {
            size_t idx = (size_t)p * (size_t)data->slot_count + (size_t)s;
            if (!seen[idx]) {
                printf(
                    "INPUT VALIDATION FAILED\n"
                    "Missing availability: %s × %s (%s %s-%s)\n",
                    data->people[p].name,
                    data->slots[s].id,
                    data->slots[s].date,
                    data->slots[s].start_time,
                    data->slots[s].end_time
                );
                free(seen);
                free_raw_table(&table);
                return 0;
            }
        }
    }

    /* reject unknown TYPE values */
    for (int r = 0; r < table.count; r++) {
        const char *type = table.rows[r].fields[0];
        if (strcmp(type, "CONFIG") != 0 &&
            strcmp(type, "SLOT") != 0 &&
            strcmp(type, "PERSON") != 0 &&
            strcmp(type, "APPOINTMENT") != 0 &&
            strcmp(type, "AVAILABILITY") != 0) {
            printf("ERROR: Unknown row TYPE: %s\n", type);
            free(seen);
            free_raw_table(&table);
            return 0;
        }
    }

    free(seen);
    free_raw_table(&table);
    return 1;
}

static int slots_overlap(const Slot *a, const Slot *b) {
    if (strcmp(a->date, b->date) != 0) return 0;
    return a->start_minutes < b->end_minutes &&
           b->start_minutes < a->end_minutes;
}

int finalize_and_validate_data(SchedulerData *data) {
    if (data->mode == MODE_UNSET) {
        printf("\nSCHEDULING MODE SETUP\n");
        printf("1. Each person can be assigned to one slot only.\n");
        printf("2. A person may be assigned to multiple slots.\n");

        int choice = prompt_int_range("Choose mode [1-2]: ", 1, 2);
        data->mode = (choice == 1) ? MODE_ONE_TO_ONE : MODE_WORKFORCE;
    }

    if (data->mode == MODE_ONE_TO_ONE) {
        if (!data->require_all_slots_was_explicit) {
            data->require_all_slots_filled =
                prompt_yes_no("Must every time slot be filled? [Y/N]: ");
        }

        if (data->appointment_count > 0) {
            printf(
                "ERROR: APPOINTMENT rows are only valid in WORKFORCE mode.\n"
            );
            return 0;
        }

        if (data->people_count > data->slot_count) {
            printf(
                "NO FEASIBLE SCHEDULE\n"
                "ONE_TO_ONE mode requires at least as many slots as people.\n"
            );
            return 0;
        }

        if (data->require_all_slots_filled &&
            data->people_count != data->slot_count) {
            printf(
                "NO FEASIBLE SCHEDULE\n"
                "Every slot must be filled and every person may be used once, "
                "so the counts must match exactly.\n"
            );
            return 0;
        }
    }

    if (data->mode == MODE_WORKFORCE) {
        if (data->appointment_count <= 0) {
            printf(
                "ERROR: WORKFORCE mode requires at least one APPOINTMENT row.\n"
            );
            return 0;
        }

        /*
         * The workforce solver uses canonical, non-overlapping time slots.
         * Multiple appointments may share the SAME slot and are concurrent.
         * Distinct slot definitions may not partially overlap.
         */
        for (int i = 0; i < data->slot_count; i++) {
            for (int j = i + 1; j < data->slot_count; j++) {
                if (slots_overlap(&data->slots[i], &data->slots[j])) {
                    printf(
                        "ERROR: WORKFORCE slots %s and %s overlap.\n"
                        "Use one canonical slot for concurrent appointments.\n",
                        data->slots[i].id, data->slots[j].id
                    );
                    return 0;
                }
            }
        }

        for (int p = 0; p < data->people_count; p++) {
            if (data->people[p].max_assignments < 0) {
                printf("ERROR: MAX_ASSIGNMENTS cannot be negative.\n");
                return 0;
            }
        }
    }

    return 1;
}

int save_scheduler_state(const char *filename, const SchedulerData *data) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        printf("WARNING: Could not save updated state to %s\n", filename);
        return 0;
    }

    fprintf(
        fp,
        "TYPE,ID,NAME,SEVERITY,MAX_ASSIGNMENTS,DATE,START_TIME,END_TIME,STATUS,SLOT_ID\n"
    );

    fprintf(fp, "CONFIG,MODE,%s,,,,,,,\n", mode_name(data->mode));
    fprintf(
        fp,
        "CONFIG,REQUIRE_ALL_SLOTS_FILLED,%s,,,,,,,\n",
        data->require_all_slots_filled ? "YES" : "NO"
    );

    for (int s = 0; s < data->slot_count; s++) {
        fprintf(
            fp, "SLOT,%s,,,,%s,%s,%s,,\n",
            data->slots[s].id,
            data->slots[s].date,
            data->slots[s].start_time,
            data->slots[s].end_time
        );
    }

    for (int p = 0; p < data->people_count; p++) {
        fprintf(
            fp, "PERSON,%s,%s,%d,%d,,,,,\n",
            data->people[p].id,
            data->people[p].name,
            data->people[p].severity,
            data->people[p].max_assignments
        );
    }

    for (int a = 0; a < data->appointment_count; a++) {
        fprintf(
            fp, "APPOINTMENT,%s,%s,,,,,,,%s\n",
            data->appointments[a].id,
            data->appointments[a].name,
            data->slots[data->appointments[a].slot_index].id
        );
    }

    for (int p = 0; p < data->people_count; p++) {
        for (int s = 0; s < data->slot_count; s++) {
            fprintf(
                fp, "AVAILABILITY,%s,,,,,,,%s,%s\n",
                data->people[p].id,
                status_name(get_status(data, p, s)),
                data->slots[s].id
            );
        }
    }

    fclose(fp);
    return 1;
}

void free_scheduler_data(SchedulerData *data) {
    free(data->people);
    free(data->slots);
    free(data->appointments);
    free(data->status);
    memset(data, 0, sizeof(*data));
}
