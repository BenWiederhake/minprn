/*
 * minrpn, finds the minimal RPN expression for an arbitrary desired result
 * Copyright Ben Wiederhake, 2016
 * MIT License
 *
 * Compile:
 *   clang++ -std=c++11 -o minrpn minrpn.cpp
 * Compile with warnings:
 *   clang++ -std=c++11 -Weverything -Wno-padded -Wno-c++98-compat -Wno-global-constructors -Wno-exit-time-destructors -Wno-c99-extensions -o minrpn minrpn.cpp
 * Usage:
 *   ./minrpn 2017
 */

#include <cassert>
#include <cmath> /* fabs */
#include <iostream>
#include <map>
#include <stack>
#include <unordered_map>

/* Some configuration / pruning */
typedef long arith_t;
static const arith_t max_relevant = 420 * 3000;
static const arith_t goal = 2017;

enum arith_op : char {
    /* Enums which store the character used to represent them. */
    OP_PLUS = '+', OP_MINUS = '-', OP_DIV = '/', OP_MULT = '*', OP_NONE = '='
};

/* A single node in an expression tree.  "val_left" and "val_right"
 * point to the *value* of an expression, which can be used for lookups.
 * NB: floating-point numbers as keys are usually a bad idea,
 * but here it's fine, since 'val_right' and 'val_left' will be preserved
 * bit-precise. */
struct expr_node {
    arith_t val_left;
    arith_t val_right;
    /* Count of terms.  This is used as a kind of cost function. */
    size_t n_terms;
    arith_op op;
};

/* Minimum shortest-known expression for the goal.
 * Initialized by a very rough upper bound */
static size_t goal_seen_n_terms = goal + 10;

/* Need value->struct lookup. */
typedef std::unordered_map<arith_t, expr_node> list_closed_t;

/* Need value->n_terms insertion/update; min(n_terms) pop; min(n_terms) update.
 * That's an unusual set of requirements, so implement my own class.
 * Note that there's many ways to implement this. */
class list_open_t {
    /* All elements with "min(n_terms)". */
    typedef std::stack<arith_t> min_nterms_t;
    min_nterms_t min_nterms_cached;
    size_t min_nterms = 0;
    /* Keeps track of the actual elements. */
    typedef std::unordered_map<arith_t, expr_node> backing_t;
    backing_t backing;

    void step_recache() {
        min_nterms += 1;
        assert(min_nterms <= goal_seen_n_terms);

        /* Need to manually manage iterator,
         * as the erasing would invalidate it. */
        backing_t::iterator it = backing.begin();
        while (it != backing.end()) {
            /* Manage iterator */
            backing_t::iterator old_it = it++;
            const backing_t::value_type& entry = *old_it;

            /* Actual logic */
            assert(entry.second.n_terms >= min_nterms);
            if (entry.second.n_terms == min_nterms) {
                min_nterms_cached.push(entry.first);
            } else if (entry.second.n_terms >= goal_seen_n_terms
                    && entry.first != goal) {
                /* This invalidates the reference! */
                backing.erase(old_it);
            }
        }
        std::cout << "Now at level " << min_nterms << " (" << size()
            << " open, " << level_size() << " of that on current level)"
            << std::endl;
    }

    void recache() {
        assert(backing.size() > 0);
        assert(min_nterms_cached.size() == 0);
        do {
            step_recache();
        } while (min_nterms_cached.size() == 0);
    }

public:
    /* Insert the given node. */
    void push(arith_t val, const expr_node& node) {
        assert(node.n_terms >= 1);
        /* We will never want to push a node with n_terms smaller or equal to
         * the n_terms of a recently popped node. */
        assert(node.n_terms > min_nterms);

        backing_t::iterator backing_it = backing.find(val);
        if (backing_it == backing.end()) {
            /* Did not exist yet. */
            backing.emplace(val, node);
        } else if (backing_it->second.n_terms > node.n_terms) {
            /* Did exist, and we found a strictly better solution. */
            backing_it->second = node;
        }
        /* Otherwise, the new discovery doesn't add anything interesting,
         * so we can just ignore it. */
    }

    size_t level_size() const {
        return min_nterms_cached.size();
    }

    size_t size() const {
        return backing.size();
    }

    /* Remove some node with the smallest 'n_terms'
     * and return the removed node. */
    void pop_into(arith_t& into_val, expr_node& into_node) {
        assert(size() != 0);
        if (min_nterms_cached.size() == 0) {
            recache();
        }
        into_val = min_nterms_cached.top();
        min_nterms_cached.pop();
        backing_t::iterator it = backing.find(into_val);
        assert(it != backing.end());
        /* Copy */
        into_node = it->second;
        backing.erase(it);
    }

    const expr_node& at(arith_t val) {
        return backing.at(val);
    }
};

/* Search state.  Invariants:
 * - 'list_open' and 'list_closed' contain nodes for mutually exclusive
 *   sets of values
 * - the nodes in 'list_closed' can only be combined in ways that generate
 *   values for which we already have a node in either list.
 * - a node in 'list_closed' represents an expression of minimum 'n_terms'. */
static list_closed_t list_closed;
static list_open_t list_open;

static const expr_node& lookup_best_known(arith_t val) {
    list_closed_t::const_iterator it = list_closed.find(val);
    if (it != list_closed.end()) {
        return it->second;
    }
    return list_open.at(val);
}

static void print_expr(arith_t val) {
    const expr_node& node = lookup_best_known(val);
    if (node.op == OP_NONE) {
        std::cout << val;
    } else {
        std::cout << "(";
        print_expr(node.val_left);
        /* Evil hack: '.op' is both an enum
         * *and* the representing character. */
        std::cout << static_cast<char>(node.op);
        print_expr(node.val_right);
        std::cout << ")";
    }
}

static void provide(arith_t d) {
    expr_node node = {.n_terms = 1, .val_left = d, .val_right = d,
                      .op = OP_NONE};
    list_open.push(d, node);
}

static void discover(arith_t val, const expr_node& node) {
    /* Only add to open list if not already known in closed list.
     * (Avoid rediscovering easily-generated values like 0 or 1.) */
    if (list_closed.count(val) != 0) {
        return;
    }
    arith_t abs_val = labs(val);
    if (abs_val >= max_relevant) {
        return;
    }
    if (node.n_terms >= goal_seen_n_terms) {
        /* Don't care about a node if it can't possibly yield a
         * better expression. */
        return;
    }
    list_open.push(val, node);
    if (val == goal) {
        goal_seen_n_terms = node.n_terms;
        std::cout << "One way (" << node.n_terms << " terms) = ";
        print_expr(goal);
        std::cout << std::endl;
    }
}

static void generate_against(arith_t a_val, const expr_node& a, arith_t b_val, const expr_node& b) {
    expr_node node;
    node.n_terms = a.n_terms + b.n_terms;
    assert(node.n_terms >= 2);

    node.val_left = a_val;
    node.val_right = b_val;
    if (b_val != 0 && a_val % b_val == 0) {
        node.op = OP_DIV;   discover(a_val / b_val, node);
    }
    node.op = OP_MINUS; discover(a_val - b_val, node);
    node.op = OP_MULT ; discover(a_val * b_val, node);
    node.op = OP_PLUS ; discover(a_val + b_val, node);

    /* Try to avoid needless duplicates */
    if (b_val != a_val) {
        node.val_left = b_val;
        node.val_right = a_val;
        if (a_val != 0 && b_val % a_val == 0) {
            node.op = OP_DIV;   discover(b_val / a_val, node);
        }
        node.op = OP_MINUS; discover(b_val - a_val, node);
    }
}

int main() {
    /* Tweak this if you feel like it. */
    provide(69);
    provide(420);

    /* Did you provide at least one value? */
    assert(list_open.size() > 0);

    /* Search */
    size_t counter = 0, next_print = 100;
    expr_node node; /* Actually 'while'-scoped. */
    do {
        if (list_open.size() == 0) {
            std::cout << "Goal can't be reached,"
                " or one of the assumptions was violated." << std::endl;
            return 1;
        }

        arith_t val;
        list_open.pop_into(val, node);
        if (++counter == next_print) {
            std::cout << "Expanding " << val << " at depth " << node.n_terms
                      << ", " << list_open.size() << " open ("
                      << list_open.level_size() << " on current level), "
                      << list_closed.size() << " closed." << std::endl;
            next_print = (next_print * 3) / 2;
        }

        /* First add it to the closed list, so it can be
         * "generated against" itself: */
        list_closed.emplace(val, node);

        assert(val != goal);

        for (const list_closed_t::value_type& peer_kv : list_closed) {
            generate_against(val, node, peer_kv.first, peer_kv.second);
        }
        /* Only loop as long as there's at least one more term that could be shaved off. */
    } while (goal_seen_n_terms > node.n_terms + 1);

    /* Printing */
    std::cout << "Done after " << list_closed.size()
        << " steps.  Turns out, you need only " << goal_seen_n_terms
        << " terms to build " << goal << ":" << std::endl;
    std::cout << goal << " = ";
    print_expr(goal);
    std::cout << std::endl;

    return 0;
}
