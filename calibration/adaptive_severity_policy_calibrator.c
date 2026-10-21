#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAX_PEOPLE 500
#define MAX_NAME 100

#define FEATURE_COUNT 6
#define MAX_QUESTIONS 30
#define DEFAULT_QUESTIONS 20
#define CANDIDATE_PAIRS 300

#define LEARNING_RATE 0.045
#define EPSILON 1e-9

/*
    ============================================================
        ADAPTIVE SEVERITY POLICY CALIBRATOR
    ============================================================

    Goal:
    -----
    Instead of asking the manager to invent a severity number
    for every person, this program learns HOW the manager makes
    trade-off decisions.

    It asks a limited number of hypothetical comparison questions.

    Example:
        Person A:
            VIP importance = 9
            seniority = 8
            submitted constraints early = 9
            flexibility = 2
            interpersonal sensitivity = 8
            financial impact = 7

        Person B:
            ...

        "If one of them had to be moved, who would you rather move?"

    From those answers, the program learns feature weights.

    Then, for every real person, the manager provides feature values.
    The program calculates a severity score automatically.

    IMPORTANT:
    ----------
    All internal features are transformed so that a HIGHER value means
    "more expensive / more undesirable to move."

    For example:
        flexibility = 10 means very flexible.
    Internally we use:
        rigidity = 10 - flexibility

    So all features point in the same direction.
*/


typedef struct {
    char name[MAX_NAME];

    /* User-facing values, each from 0 to 10 */
    double vipImportance;
    double seniority;
    double submissionPriority;
    double flexibility;
    double interpersonalSensitivity;
    double financialImpact;

    /* Final results */
    double rawSeverity;
    int severityScore;
} Person;


typedef struct {
    /*
        Internal transformed feature vector.

        All values are normalized to [0, 1].
        Higher = more expensive to move.
    */
    double x[FEATURE_COUNT];
} Profile;


/* ------------------------------------------------------------
   Feature metadata
   ------------------------------------------------------------ */

const char *FEATURE_NAMES[FEATURE_COUNT] = {
    "VIP / business importance",
    "Seniority / organizational priority",
    "Submitted constraints early / earned scheduling priority",
    "Low flexibility (harder to move)",
    "Interpersonal sensitivity",
    "Financial / operational impact"
};


/* ------------------------------------------------------------
   Input helpers
   ------------------------------------------------------------ */

void clearInputBuffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {
        /* discard */
    }
}


int readIntRange(const char *prompt, int min, int max) {
    int value;

    while (1) {
        printf("%s", prompt);

        if (scanf("%d", &value) == 1 &&
            value >= min &&
            value <= max) {

            clearInputBuffer();
            return value;
        }

        printf("Invalid input. Enter a number from %d to %d.\n",
               min, max);

        clearInputBuffer();
    }
}


double readDoubleRange(const char *prompt, double min, double max) {
    double value;

    while (1) {
        printf("%s", prompt);

        if (scanf("%lf", &value) == 1 &&
            value >= min &&
            value <= max) {

            clearInputBuffer();
            return value;
        }

        printf("Invalid input. Enter a value from %.0f to %.0f.\n",
               min, max);

        clearInputBuffer();
    }
}


void readLine(const char *prompt, char *buffer, int size) {
    printf("%s", prompt);

    if (fgets(buffer, size, stdin) == NULL) {
        buffer[0] = '\0';
        return;
    }

    buffer[strcspn(buffer, "\n")] = '\0';
}


void separator(void) {
    printf("============================================================\n");
}


/* ------------------------------------------------------------
   Math helpers
   ------------------------------------------------------------ */

double dot(const double a[], const double b[], int n) {
    double result = 0.0;

    for (int i = 0; i < n; i++) {
        result += a[i] * b[i];
    }

    return result;
}


double vectorDistance(const double a[], const double b[], int n) {
    double sum = 0.0;

    for (int i = 0; i < n; i++) {
        double d = a[i] - b[i];
        sum += d * d;
    }

    return sqrt(sum);
}


void normalizeWeights(double weights[]) {
    double sum = 0.0;

    for (int i = 0; i < FEATURE_COUNT; i++) {
        if (weights[i] < 0.0) {
            weights[i] = 0.0;
        }

        sum += weights[i];
    }

    if (sum < EPSILON) {
        for (int i = 0; i < FEATURE_COUNT; i++) {
            weights[i] = 1.0 / FEATURE_COUNT;
        }

        return;
    }

    for (int i = 0; i < FEATURE_COUNT; i++) {
        weights[i] /= sum;
    }
}


/* ------------------------------------------------------------
   Profile generation
   ------------------------------------------------------------ */

double random01(void) {
    return (double)rand() / (double)RAND_MAX;
}


double randomFeatureValue(void) {
    /*
        Generate values in steps of 0.1.
        This makes scenarios easier to read.
    */
    int value = rand() % 11;
    return value / 10.0;
}


Profile randomProfile(void) {
    Profile p;

    for (int i = 0; i < FEATURE_COUNT; i++) {
        p.x[i] = randomFeatureValue();
    }

    return p;
}


/*
    Choose a question that is informative.

    We generate many candidate profile pairs.

    We prefer pairs that:
    1. Are reasonably different from each other.
    2. The current model thinks are close in severity.

    Why?

    If the model already believes:
        A = 0.95
        B = 0.10

    then asking which one the manager would move teaches very little.

    If the model believes:
        A = 0.51
        B = 0.50

    then the answer can strongly distinguish between competing policies.
*/
void chooseAdaptiveQuestion(
    const double weights[],
    Profile *bestA,
    Profile *bestB
) {
    double bestUtility = -1.0;

    for (int k = 0; k < CANDIDATE_PAIRS; k++) {
        Profile a = randomProfile();
        Profile b = randomProfile();

        double severityA = dot(weights, a.x, FEATURE_COUNT);
        double severityB = dot(weights, b.x, FEATURE_COUNT);

        double uncertainty = 1.0 / (fabs(severityA - severityB) + 0.05);
        double diversity = vectorDistance(a.x, b.x, FEATURE_COUNT);

        /*
            Avoid pairs that are nearly identical.
        */
        if (diversity < 0.65) {
            continue;
        }

        double utility = uncertainty * diversity;

        if (utility > bestUtility) {
            bestUtility = utility;
            *bestA = a;
            *bestB = b;
        }
    }

    /*
        Fallback, extremely unlikely.
    */
    if (bestUtility < 0.0) {
        *bestA = randomProfile();
        *bestB = randomProfile();
    }
}


/* ------------------------------------------------------------
   Display hypothetical profiles
   ------------------------------------------------------------ */

double displayValueForFeature(int featureIndex, double internalValue) {
    /*
        Feature #3 internally represents LOW flexibility / rigidity.

        User-facing display should show actual flexibility:
            internal rigidity = 0.8
            displayed flexibility = 2/10
    */
    if (featureIndex == 3) {
        return (1.0 - internalValue) * 10.0;
    }

    return internalValue * 10.0;
}


void printProfile(const char *label, const Profile *p) {
    printf("\n%s\n", label);
    printf("------------------------------------------------------------\n");

    printf("VIP / business importance                 : %.0f/10\n",
           displayValueForFeature(0, p->x[0]));

    printf("Seniority / organizational priority       : %.0f/10\n",
           displayValueForFeature(1, p->x[1]));

    printf("Submitted constraints early               : %.0f/10\n",
           displayValueForFeature(2, p->x[2]));

    printf("Expected flexibility                      : %.0f/10\n",
           displayValueForFeature(3, p->x[3]));

    printf("Interpersonal sensitivity                 : %.0f/10\n",
           displayValueForFeature(4, p->x[4]));

    printf("Financial / operational impact            : %.0f/10\n",
           displayValueForFeature(5, p->x[5]));
}


/* ------------------------------------------------------------
   Learning rule
   ------------------------------------------------------------ */

/*
    User chooses who they would rather MOVE.

    If user chooses A:
        A should have LOWER severity than B.

    Therefore:
        severity(A) < severity(B)

    Since:
        severity(P) = weights dot features(P)

    We update weights in the direction that increases:
        severity(B) - severity(A)

    choiceDirection:
        +1 = prefer moving A
        -1 = prefer moving B

    strength:
        1 = slight preference
        2 = strong preference
*/
void updateWeights(
    double weights[],
    const Profile *a,
    const Profile *b,
    int choiceDirection,
    double strength
) {
    for (int i = 0; i < FEATURE_COUNT; i++) {

        double direction;

        if (choiceDirection == +1) {
            /*
                Prefer moving A.
                Make B more severe relative to A.
            */
            direction = b->x[i] - a->x[i];

        } else {
            /*
                Prefer moving B.
                Make A more severe relative to B.
            */
            direction = a->x[i] - b->x[i];
        }

        weights[i] += LEARNING_RATE * strength * direction;
    }

    normalizeWeights(weights);
}


/* ------------------------------------------------------------
   Policy confidence
   ------------------------------------------------------------ */

/*
    This is a heuristic confidence indicator, not a formal statistical
    guarantee.

    We combine:
    - number of informative answers
    - consistency between answers and current learned model

    This is intentionally presented as "calibration confidence",
    not mathematical proof.
*/
double estimateConfidence(
    int answeredQuestions,
    int consistentAnswers
) {
    if (answeredQuestions == 0) {
        return 0.0;
    }

    double quantity =
        1.0 - exp(-(double)answeredQuestions / 10.0);

    double consistency =
        (double)consistentAnswers / answeredQuestions;

    double confidence =
        100.0 * (0.55 * quantity + 0.45 * consistency);

    if (confidence > 99.0) {
        confidence = 99.0;
    }

    return confidence;
}


/* ------------------------------------------------------------
   Policy calibration loop
   ------------------------------------------------------------ */

void runPolicyCalibration(
    double weights[],
    int questionLimit
) {
    int answered = 0;
    int consistent = 0;

    /*
        Start neutral:
        all factors initially have equal importance.
    */
    for (int i = 0; i < FEATURE_COUNT; i++) {
        weights[i] = 1.0 / FEATURE_COUNT;
    }

    separator();
    printf("POLICY CALIBRATION\n");
    separator();

    printf("\nYou will be shown hypothetical scheduling conflicts.\n");
    printf("Choose the person you would rather ask to move.\n\n");

    printf("There are no objectively correct answers.\n");
    printf("The goal is to learn YOUR scheduling policy.\n\n");

    printf("Answer strength:\n");
    printf("1 = Definitely prefer moving Person A\n");
    printf("2 = Slightly prefer moving Person A\n");
    printf("3 = Approximately equal\n");
    printf("4 = Slightly prefer moving Person B\n");
    printf("5 = Definitely prefer moving Person B\n");
    printf("6 = Not sure / skip this question\n\n");

    for (int q = 1; q <= questionLimit; q++) {

        Profile a, b;
        chooseAdaptiveQuestion(weights, &a, &b);

        separator();
        printf("QUESTION %d OF %d\n", q, questionLimit);
        separator();

        printProfile("PERSON A", &a);
        printProfile("PERSON B", &b);

        printf("\nIf ONE of these people had to be moved outside\n");
        printf("their declared availability, who would you rather contact?\n");

        int choice =
            readIntRange(
                "\nChoice [1=A definitely, 2=A slightly, 3=equal, "
                "4=B slightly, 5=B definitely, 6=not sure]: ",
                1,
                6
            );

        if (choice == 6) {
            printf("\nQuestion skipped. It will not affect the model.\n");
            continue;
        }

        answered++;

        double beforeA = dot(weights, a.x, FEATURE_COUNT);
        double beforeB = dot(weights, b.x, FEATURE_COUNT);

        if (choice == 3) {
            /*
                If user says approximately equal,
                gently pull weights toward making the difference smaller.

                Let diff = severityA - severityB.

                Gradient-like correction:
                    w <- w - eta * diff * (A-B)
            */
            double diff = beforeA - beforeB;

            for (int i = 0; i < FEATURE_COUNT; i++) {
                weights[i] -=
                    LEARNING_RATE *
                    diff *
                    (a.x[i] - b.x[i]);
            }

            normalizeWeights(weights);

            if (fabs(beforeA - beforeB) < 0.12) {
                consistent++;
            }

        } else {
            int direction;
            double strength;

            if (choice == 1) {
                direction = +1;
                strength = 2.0;

                if (beforeA <= beforeB) {
                    consistent++;
                }

            } else if (choice == 2) {
                direction = +1;
                strength = 1.0;

                if (beforeA <= beforeB) {
                    consistent++;
                }

            } else if (choice == 4) {
                direction = -1;
                strength = 1.0;

                if (beforeB <= beforeA) {
                    consistent++;
                }

            } else {
                direction = -1;
                strength = 2.0;

                if (beforeB <= beforeA) {
                    consistent++;
                }
            }

            updateWeights(
                weights,
                &a,
                &b,
                direction,
                strength
            );
        }

        double confidence =
            estimateConfidence(answered, consistent);

        printf("\nCurrent calibration confidence: %.1f%%\n",
               confidence);

        /*
            After at least 10 real answers, let the user stop early.
        */
        if (answered >= 10 && q < questionLimit) {

            printf("\nWould you like to continue refining the policy?\n");
            printf("1. Continue\n");
            printf("2. Stop and use the current model\n");

            int keepGoing =
                readIntRange("Choice [1/2]: ", 1, 2);

            if (keepGoing == 2) {
                break;
            }
        }
    }
}


/* ------------------------------------------------------------
   Print learned policy
   ------------------------------------------------------------ */

void printPolicyWeights(const double weights[]) {
    separator();
    printf("LEARNED SCHEDULING POLICY\n");
    separator();

    printf("\nEstimated importance of each factor:\n\n");

    for (int i = 0; i < FEATURE_COUNT; i++) {
        printf("%-48s %6.1f%%\n",
               FEATURE_NAMES[i],
               weights[i] * 100.0);
    }

    printf("\nInterpretation:\n");
    printf("The percentages represent the relative influence of each\n");
    printf("factor on the learned severity model.\n");
}


/* ------------------------------------------------------------
   Convert real person data into feature vector
   ------------------------------------------------------------ */

Profile personToProfile(const Person *p) {
    Profile profile;

    profile.x[0] = p->vipImportance / 10.0;
    profile.x[1] = p->seniority / 10.0;
    profile.x[2] = p->submissionPriority / 10.0;

    /*
        High flexibility should LOWER severity.
        Therefore internally use rigidity.
    */
    profile.x[3] = 1.0 - (p->flexibility / 10.0);

    profile.x[4] = p->interpersonalSensitivity / 10.0;
    profile.x[5] = p->financialImpact / 10.0;

    return profile;
}


/* ------------------------------------------------------------
   Real people input
   ------------------------------------------------------------ */

void inputPeople(Person people[], int n) {
    printf("\n");
    separator();
    printf("REAL PEOPLE / ITEMS\n");
    separator();

    printf("\nNow enter the characteristics of the real people\n");
    printf("you want to assign severity scores to.\n\n");

    printf("Use values from 0 to 10.\n");
    printf("You are NOT choosing severity numbers.\n");
    printf("You are only describing each person.\n\n");

    for (int i = 0; i < n; i++) {

        printf("\n------------------------------------------------------------\n");
        printf("PERSON %d OF %d\n", i + 1, n);
        printf("------------------------------------------------------------\n");

        readLine("Name: ", people[i].name, MAX_NAME);

        while (strlen(people[i].name) == 0) {
            printf("Name cannot be empty.\n");
            readLine("Name: ", people[i].name, MAX_NAME);
        }

        people[i].vipImportance =
            readDoubleRange(
                "VIP / business importance [0-10]: ",
                0, 10
            );

        people[i].seniority =
            readDoubleRange(
                "Seniority / organizational priority [0-10]: ",
                0, 10
            );

        people[i].submissionPriority =
            readDoubleRange(
                "How strongly should timely constraint submission "
                "protect this person? [0-10]: ",
                0, 10
            );

        people[i].flexibility =
            readDoubleRange(
                "Expected flexibility if contacted [0-10, "
                "10 = very flexible]: ",
                0, 10
            );

        people[i].interpersonalSensitivity =
            readDoubleRange(
                "Interpersonal sensitivity of asking for a change "
                "[0-10]: ",
                0, 10
            );

        people[i].financialImpact =
            readDoubleRange(
                "Financial / operational impact of moving this person "
                "[0-10]: ",
                0, 10
            );
    }
}


/* ------------------------------------------------------------
   Calculate severities
   ------------------------------------------------------------ */

void calculateSeverities(
    Person people[],
    int n,
    const double weights[]
) {
    double minimumPositive = 1e100;

    for (int i = 0; i < n; i++) {

        Profile p = personToProfile(&people[i]);

        people[i].rawSeverity =
            dot(weights, p.x, FEATURE_COUNT);

        if (people[i].rawSeverity > EPSILON &&
            people[i].rawSeverity < minimumPositive) {

            minimumPositive = people[i].rawSeverity;
        }
    }

    /*
        If every person somehow gets raw severity 0,
        all are equivalent and receive 1.
    */
    if (minimumPositive == 1e100) {
        for (int i = 0; i < n; i++) {
            people[i].severityScore = 1;
        }

        return;
    }

    /*
        Preserve relative ratios:
        cheapest positive person ~= 1.

        We do NOT force scores into 1..10 because doing that would
        distort the ratios that the scheduling optimizer relies on.
    */
    for (int i = 0; i < n; i++) {

        if (people[i].rawSeverity <= EPSILON) {
            people[i].severityScore = 1;
        } else {
            double relative =
                people[i].rawSeverity / minimumPositive;

            people[i].severityScore =
                (int)lround(relative);

            if (people[i].severityScore < 1) {
                people[i].severityScore = 1;
            }
        }
    }
}


/* ------------------------------------------------------------
   Results
   ------------------------------------------------------------ */

void printResults(
    Person people[],
    int n
) {
    separator();
    printf("SEVERITY CALIBRATION COMPLETE\n");
    separator();

    printf("\nRecommended severity scores:\n\n");

    printf("%-24s %-12s %-15s\n",
           "NAME",
           "SEVERITY",
           "RAW POLICY COST");

    printf("------------------------------------------------------------\n");

    for (int i = 0; i < n; i++) {
        printf("%-24s %-12d %.4f\n",
               people[i].name,
               people[i].severityScore,
               people[i].rawSeverity);
    }

    printf("\n");
    separator();
    printf("COPY INTO SCHEDULING OPTIMIZER\n");
    separator();
    printf("\n");

    for (int i = 0; i < n; i++) {
        printf("%s = %d\n",
               people[i].name,
               people[i].severityScore);
    }

    printf("\nInterpretation:\n\n");

    printf("- A higher score means the optimizer should be more reluctant\n");
    printf("  to violate that person's declared availability.\n\n");

    printf("- The ratios matter more than the absolute numbers.\n\n");

    printf("- If Noa = 8 and Maya = 4, one violation involving Noa is\n");
    printf("  treated approximately like two violations involving Maya.\n\n");

    printf("- These scores are not moral judgments about people.\n");
    printf("  They are numerical representations of the scheduling policy\n");
    printf("  that you taught the calibration engine.\n\n");
}


/* ------------------------------------------------------------
   Main
   ------------------------------------------------------------ */

int main(void) {
    double weights[FEATURE_COUNT];
    Person people[MAX_PEOPLE];

    srand((unsigned int)time(NULL));

    separator();
    printf("       ADAPTIVE SEVERITY POLICY CALIBRATOR\n");
    separator();

    printf("\nThis program learns HOW you prioritize scheduling changes,\n");
    printf("then calculates severity scores automatically.\n\n");

    printf("You do not need to invent severity numbers yourself.\n");

    int questionLimit =
        readIntRange(
            "\nHow many calibration questions would you like to allow?\n"
            "Recommended: 15-30\n"
            "Questions [10-30]: ",
            10,
            MAX_QUESTIONS
        );

    runPolicyCalibration(weights, questionLimit);

    printPolicyWeights(weights);

    int n =
        readIntRange(
            "\nHow many real people/items should be scored? ",
            2,
            MAX_PEOPLE
        );

    inputPeople(people, n);

    calculateSeverities(
        people,
        n,
        weights
    );

    printResults(
        people,
        n
    );

    return 0;
}
