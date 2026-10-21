#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int to;
    int rev;
    int cap;
    long long cost;
} Edge;

typedef struct {
    Edge *edges;
    int count;
    int capacity;
} EdgeList;

typedef struct {
    EdgeList *adj;
    int node_count;
} Graph;

typedef struct {
    int node;
    long long dist;
} HeapItem;

typedef struct {
    HeapItem *items;
    int size;
    int capacity;
} MinHeap;

static int edge_list_push(EdgeList *list, Edge e) {
    if (list->count == list->capacity) {
        int new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        Edge *new_edges = (Edge *)realloc(
            list->edges, (size_t)new_capacity * sizeof(Edge)
        );
        if (!new_edges) return 0;
        list->edges = new_edges;
        list->capacity = new_capacity;
    }

    list->edges[list->count++] = e;
    return 1;
}

static int graph_init(Graph *g, int nodes) {
    g->node_count = nodes;
    g->adj = (EdgeList *)calloc((size_t)nodes, sizeof(EdgeList));
    return g->adj != NULL;
}

static void graph_free(Graph *g) {
    if (!g->adj) return;
    for (int i = 0; i < g->node_count; i++) {
        free(g->adj[i].edges);
    }
    free(g->adj);
    g->adj = NULL;
    g->node_count = 0;
}

static int add_edge(Graph *g, int from, int to, int cap, long long cost) {
    Edge forward = {to, g->adj[to].count, cap, cost};
    Edge reverse = {from, g->adj[from].count, 0, -cost};

    if (!edge_list_push(&g->adj[from], forward)) return 0;
    if (!edge_list_push(&g->adj[to], reverse)) return 0;
    return 1;
}

static int heap_push(MinHeap *h, int node, long long dist) {
    if (h->size == h->capacity) {
        int new_capacity = h->capacity == 0 ? 32 : h->capacity * 2;
        HeapItem *new_items = (HeapItem *)realloc(
            h->items, (size_t)new_capacity * sizeof(HeapItem)
        );
        if (!new_items) return 0;
        h->items = new_items;
        h->capacity = new_capacity;
    }

    int i = h->size++;
    h->items[i].node = node;
    h->items[i].dist = dist;

    while (i > 0) {
        int parent = (i - 1) / 2;
        if (h->items[parent].dist <= h->items[i].dist) break;

        HeapItem tmp = h->items[parent];
        h->items[parent] = h->items[i];
        h->items[i] = tmp;
        i = parent;
    }

    return 1;
}

static int heap_pop(MinHeap *h, HeapItem *out) {
    if (h->size == 0) return 0;

    *out = h->items[0];
    h->items[0] = h->items[--h->size];

    int i = 0;
    while (1) {
        int left = 2 * i + 1;
        int right = left + 1;
        int smallest = i;

        if (left < h->size &&
            h->items[left].dist < h->items[smallest].dist) {
            smallest = left;
        }

        if (right < h->size &&
            h->items[right].dist < h->items[smallest].dist) {
            smallest = right;
        }

        if (smallest == i) break;

        HeapItem tmp = h->items[i];
        h->items[i] = h->items[smallest];
        h->items[smallest] = tmp;
        i = smallest;
    }

    return 1;
}

static void heap_free(MinHeap *h) {
    free(h->items);
    h->items = NULL;
    h->size = 0;
    h->capacity = 0;
}

static int min_cost_max_flow(
    Graph *g,
    int source,
    int sink,
    int required_flow,
    int *out_flow,
    long long *out_cost
) {
    int n = g->node_count;

    long long *potential = (long long *)calloc((size_t)n, sizeof(long long));
    long long *dist = (long long *)malloc((size_t)n * sizeof(long long));
    int *prev_node = (int *)malloc((size_t)n * sizeof(int));
    int *prev_edge = (int *)malloc((size_t)n * sizeof(int));

    if (!potential || !dist || !prev_node || !prev_edge) {
        free(potential); free(dist); free(prev_node); free(prev_edge);
        return 0;
    }

    int flow = 0;
    long long cost = 0;

    while (flow < required_flow) {
        for (int i = 0; i < n; i++) {
            dist[i] = COST_INF;
            prev_node[i] = -1;
            prev_edge[i] = -1;
        }

        dist[source] = 0;

        MinHeap heap = {0};
        if (!heap_push(&heap, source, 0)) {
            heap_free(&heap);
            free(potential); free(dist); free(prev_node); free(prev_edge);
            return 0;
        }

        HeapItem item;

        while (heap_pop(&heap, &item)) {
            int u = item.node;
            if (item.dist != dist[u]) continue;

            for (int ei = 0; ei < g->adj[u].count; ei++) {
                Edge *e = &g->adj[u].edges[ei];
                if (e->cap <= 0) continue;

                long long reduced =
                    e->cost + potential[u] - potential[e->to];

                long long nd = dist[u] + reduced;
                if (nd < dist[e->to]) {
                    dist[e->to] = nd;
                    prev_node[e->to] = u;
                    prev_edge[e->to] = ei;

                    if (!heap_push(&heap, e->to, nd)) {
                        heap_free(&heap);
                        free(potential); free(dist); free(prev_node); free(prev_edge);
                        return 0;
                    }
                }
            }
        }

        heap_free(&heap);

        if (dist[sink] >= COST_INF / 2) break;

        for (int i = 0; i < n; i++) {
            if (dist[i] < COST_INF / 2) {
                potential[i] += dist[i];
            }
        }

        int add = required_flow - flow;
        for (int v = sink; v != source; v = prev_node[v]) {
            if (v < 0 || prev_node[v] < 0) {
                add = 0;
                break;
            }

            Edge *e = &g->adj[prev_node[v]].edges[prev_edge[v]];
            if (e->cap < add) add = e->cap;
        }

        if (add <= 0) break;

        for (int v = sink; v != source; v = prev_node[v]) {
            int u = prev_node[v];
            int ei = prev_edge[v];

            Edge *e = &g->adj[u].edges[ei];
            int rev = e->rev;

            cost += (long long)add * e->cost;
            e->cap -= add;
            g->adj[v].edges[rev].cap += add;
        }

        flow += add;
    }

    *out_flow = flow;
    *out_cost = cost;

    free(potential); free(dist); free(prev_node); free(prev_edge);
    return 1;
}

int solve_workforce(const SchedulerData *data, ScheduleSolution *solution) {
    int P = data->people_count;
    int S = data->slot_count;
    int A = data->appointment_count;

    int source = 0;
    int appointment_offset = 1;
    int person_slot_offset = appointment_offset + A;
    int person_offset = person_slot_offset + P * S;
    int sink = person_offset + P;
    int node_count = sink + 1;

    Graph g = {0};
    if (!graph_init(&g, node_count)) return 0;

    for (int a = 0; a < A; a++) {
        if (!add_edge(&g, source, appointment_offset + a, 1, 0)) {
            graph_free(&g);
            return 0;
        }
    }

    for (int p = 0; p < P; p++) {
        for (int s = 0; s < S; s++) {
            int ps_node = person_slot_offset + p * S + s;
            int person_node = person_offset + p;

            /*
             * capacity 1 here is the key conflict rule:
             * one person can serve at most one appointment in the same slot.
             */
            if (!add_edge(&g, ps_node, person_node, 1, 0)) {
                graph_free(&g);
                return 0;
            }
        }

        int cap = data->people[p].max_assignments == 0
            ? A
            : data->people[p].max_assignments;

        if (!add_edge(&g, person_offset + p, sink, cap, 0)) {
            graph_free(&g);
            return 0;
        }
    }

    for (int a = 0; a < A; a++) {
        int slot = data->appointments[a].slot_index;
        int appointment_node = appointment_offset + a;

        for (int p = 0; p < P; p++) {
            AvailabilityStatus st = get_status(data, p, slot);
            if (st == STATUS_HARD) continue;

            long long cost =
                (st == STATUS_AVAILABLE) ? 0 : data->people[p].severity;

            int ps_node = person_slot_offset + p * S + slot;

            if (!add_edge(&g, appointment_node, ps_node, 1, cost)) {
                graph_free(&g);
                return 0;
            }
        }
    }

    int flow = 0;
    long long total_cost = 0;

    if (!min_cost_max_flow(
        &g, source, sink, A, &flow, &total_cost
    )) {
        graph_free(&g);
        return 0;
    }

    if (flow < A) {
        solution->feasible = 0;
        graph_free(&g);
        return 1;
    }

    solution->person_for_appointment =
        (int *)malloc((size_t)A * sizeof(int));

    if (!solution->person_for_appointment) {
        graph_free(&g);
        return 0;
    }

    for (int a = 0; a < A; a++) {
        solution->person_for_appointment[a] = -1;

        int appointment_node = appointment_offset + a;

        for (int ei = 0; ei < g.adj[appointment_node].count; ei++) {
            Edge *e = &g.adj[appointment_node].edges[ei];

            if (e->to < person_slot_offset ||
                e->to >= person_offset) {
                continue;
            }

            /*
             * Original appointment->personSlot edge had cap 1.
             * cap 0 now means its one unit of flow was used.
             */
            if (e->cap == 0) {
                int relative = e->to - person_slot_offset;
                int p = relative / S;
                solution->person_for_appointment[a] = p;
                break;
            }
        }

        if (solution->person_for_appointment[a] < 0) {
            solution->feasible = 0;
            graph_free(&g);
            return 1;
        }
    }

    solution->total_cost = total_cost;
    solution->feasible = 1;

    graph_free(&g);
    return 1;
}
