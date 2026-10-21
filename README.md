# Cost-Based Scheduling Optimization System

**C11 · Combinatorial Optimization · Adaptive Preference Elicitation · Bounded AI Architecture**

A scheduling optimization system that turns non-binary human and operational trade-offs into numerical costs, then computes an exact minimum-cost schedule under those costs.

Instead of treating scheduling as a simple available/unavailable problem, the system models how expensive different compromises are, solves two different scheduling formulations with algorithms matched to their structure, and re-optimizes globally when new human information arrives.

> **Design principle:** let the computer solve everything that can be expressed mathematically, and leave the human only the decisions that genuinely require human judgment.

## What the system does

The deterministic core accepts validated structured scheduling data and supports two optimization modes:

- **ONE_TO_ONE** — one person per slot, solved with the **Hungarian Algorithm**.
- **WORKFORCE** — employees may serve multiple appointments across different canonical slots, solved with **Minimum-Cost Maximum-Flow**.

A separate **Adaptive Severity Policy Calibrator** can learn a manager-specific cost policy from pairwise decisions and convert that policy into one severity score per employee.

The architecture also defines a deliberately bounded AI input layer: AI may translate unstructured business information into the strict input schema, but it is not trusted to optimize or invent missing facts.

---

# 1. Core Cost Model

For every person × time-slot pair:

```text
AVAILABLE    -> cost 0
UNAVAILABLE  -> cost = employee severity
HARD         -> forbidden assignment
UNKNOWN      -> validation failure
```

Severity represents the modeled operational/interpersonal cost of violating that person's declared availability. It is not a moral ranking of people.

The optimization objective is:

> **Find a feasible schedule with minimum total damage cost.**

---

# 2. ONE_TO_ONE Optimization

`ONE_TO_ONE` is used when each person must receive exactly one time slot, such as interviews, presentations, one-on-one meetings, or individual appointments.

The problem is formulated as a **Minimum-Cost Bipartite Assignment Problem**.

A naive exact approach may inspect up to `O(n!)` assignments. The implementation instead uses the **Hungarian Algorithm**, giving an exact solution in `O(n^3)` for the square case.

This preserves the global optimum under the defined cost function while replacing factorial enumeration with polynomial-time optimization.

---

# 3. WORKFORCE Optimization

`WORKFORCE` handles cases in which the same employee may serve multiple appointments across the day while respecting capacity and concurrency constraints.

The system builds a flow network and solves it using **Minimum-Cost Maximum-Flow** with successive shortest augmenting paths, **Dijkstra's algorithm**, and **node potentials**.

The model supports:

- repeated employee assignments across different slots,
- per-employee `MAX_ASSIGNMENTS` limits,
- multiple concurrent appointments,
- prevention of assigning one employee to multiple appointments in the same canonical slot,
- forbidden (`HARD`) employee-slot combinations.

The current model assumes canonical, non-overlapping slot definitions. Concurrent appointments reference the same canonical slot.

---

# 4. Persistent Manager Feedback & Global Re-Optimization

A mathematically optimal schedule may still require a human to confirm whether a proposed availability change is actually possible.

The optimizer therefore returns the full minimum-cost schedule together with the required soft changes, ordered from lower to higher severity.

If a proposed move fails:

```text
previously confirmed changes -> AVAILABLE
failed person-slot pair       -> HARD
not-yet-checked changes       -> unchanged
```

The updated knowledge is persisted, and the engine solves the **entire optimization problem again**.

It does not locally patch the previous schedule; it computes a new global minimum under the newly confirmed constraints.

---

# 5. Adaptive Severity Policy Calibration

Severity values can be provided directly, but managers often know how they make trade-offs without being able to invent meaningful numbers for every employee.

The separate calibrator learns that policy instead.

## Calibration process

The manager chooses a configurable budget of **10–30 questions**.

Each question presents two hypothetical profiles described by six factors:

- Business / VIP importance
- Seniority / organizational priority
- Priority earned through timely constraint submission
- Expected flexibility
- Interpersonal sensitivity
- Financial / operational impact

The manager chooses which person they would rather ask to move, including the strength of the preference. Each answer updates the learned feature weights.

## Adaptive question selection

For each calibration step, the system generates up to **300 candidate profile pairs** and scores them using:

- **uncertainty** — preference for pairs whose predicted severities are close,
- **diversity** — avoidance of nearly identical profiles.

Conceptually, the selection heuristic favors:

```text
high model uncertainty × meaningful feature differences
```

After each answer, the weights change, so the next question is selected under the updated model.

After at least 10 answered questions, the program displays its current heuristic calibration confidence and lets the manager either continue refining the model or stop and use the current policy.

## From policy to severity

For each real employee, the learned policy computes a weighted score from the same six features:

```text
raw_severity = learned_weights · employee_features
```

The calibrator then converts the relative policy costs into **one integer severity score per employee**, which can be used directly by the scheduling optimizer.

This allows the learned policy to generalize to employees and employee pairs that were never directly compared during calibration.

---

# 6. Bounded AI Translation Layer

Real scheduling information may arrive as messages, free text, spreadsheets, Word documents, PDFs, or other semi-structured data rather than as a perfect CSV.

The system architecture therefore defines an optional AI layer whose responsibility is limited to **unstructured-to-structured translation**.

### AI may

- identify supplied names, dates, and times,
- map supplied information into the required fields,
- convert natural-language availability into structured rows,
- preserve source values,
- mark ambiguous or missing information as `UNKNOWN`.

### AI may not

- invent availability that was never supplied,
- independently determine severity,
- declare `HARD` without supporting information,
- choose the final schedule,
- replace the deterministic optimizer.

> **AI handles ambiguity and representation. Deterministic algorithms handle validation and optimization.**

**Note:** this repository contains the deterministic optimizer and calibration subsystem. The AI translation layer is an architectural boundary for an optional upstream input adapter; no AI API integration is required to run the core system.

---

# 7. Input Validation

Exact optimization over incorrect input still produces an incorrect result, so validation is treated as part of the system.

The parser rejects, among other cases:

- missing person × slot availability records,
- duplicate availability records,
- unresolved `UNKNOWN` availability,
- invalid references,
- impossible ONE_TO_ONE dimensions,
- overlapping canonical slots in WORKFORCE mode,
- invalid assignment-capacity values.

The solver does not silently guess missing information.

---

# 8. Architecture

```text
Unstructured business information
              |
              v
   Optional bounded AI translation
              |
              v
     Validated structured input
              |
              v
        Cost / constraint model
              |
       +------+------+
       |             |
       v             v
  ONE_TO_ONE      WORKFORCE
   Hungarian      Min-Cost Flow
       |             |
       +------+------+
              |
              v
     Minimum-cost schedule
              |
              v
      Manager verification
              |
        new information
              |
              v
      Global re-optimization

Optional calibration path:

Manager pairwise decisions
              |
              v
Adaptive Severity Policy Calibrator
              |
              v
       Severity scores
              |
              +----> Cost model
```

---

# 9. Project Structure

```text
.
├── CMakeLists.txt
├── README.md
├── include/
│   └── scheduler.h
├── src/
│   ├── main.c
│   ├── parser.c
│   ├── engine.c
│   ├── hungarian.c
│   └── min_cost_flow.c
├── calibration/
│   └── adaptive_severity_policy_calibrator.c
├── examples/
│   ├── one_to_one.csv
│   └── workforce.csv
└── tests/
```

---

# 10. Build

## CMake / CLion

Open the repository directory containing `CMakeLists.txt` in CLion and build the desired target.

The project exposes two targets:

- `scheduling_optimizer`
- `severity_calibrator`

## GCC — scheduling optimizer

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic \
    -Iinclude \
    src/main.c src/parser.c src/hungarian.c \
    src/min_cost_flow.c src/engine.c \
    -o scheduling_optimizer
```

## GCC — severity calibrator

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic \
    calibration/adaptive_severity_policy_calibrator.c \
    -lm \
    -o severity_calibrator
```

---

# 11. Run

## ONE_TO_ONE example

```bash
./scheduling_optimizer examples/one_to_one.csv
```

## WORKFORCE example

```bash
./scheduling_optimizer examples/workforce.csv
```

An optional second argument selects the persisted state file:

```bash
./scheduling_optimizer examples/workforce.csv schedule_state_updated.csv
```

## Severity calibrator

```bash
./severity_calibrator
```

---

## Author

**Lior Gabai**  
B.Sc. Computer Science, Bar-Ilan University  
Expected Graduation: 2028
