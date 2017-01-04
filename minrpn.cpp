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
#include <unordered_set>

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
    arith_t val;
    arith_t val_left;
    arith_t val_right;
    /* Count of terms.  This is used as a kind of cost function. */
    size_t n_terms;
    arith_op op;
};

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
    typedef std::unordered_map<arith_t, expr_node> contains_val_t;
    contains_val_t contains_val;

    void recache() {
        assert(contains_val.size() > 0);
        assert(min_nterms_cached.size() == 0);
        do {
            min_nterms += 1;
            std::cout << "Now looking at level " << min_nterms << std::endl;
            for (const contains_val_t::value_type& entry : contains_val) {
                assert(entry.second.n_terms >= min_nterms);
                if (entry.second.n_terms == min_nterms) {
                    min_nterms_cached.push(entry.first);
                }
            }
        } while (min_nterms_cached.size() == 0);
    }

public:
    /* Insert the given node. */
    void push(const expr_node& node) {
        assert(node.n_terms >= 1);
        /* We will never want to push a node with n_terms smaller or equal to
         * the n_terms of a recently popped node. */
        assert(node.n_terms > min_nterms);

        contains_val_t::iterator contains_val_it = contains_val.find(node.val);
        if (contains_val_it == contains_val.end()) {
            /* Did not exist yet. */
            contains_val.emplace(node.val, node);
        } else if (contains_val_it->second.n_terms > node.n_terms) {
            /* Did exist, and we found a strictly better solution. */
            contains_val_it->second = node;
        }
        /* Otherwise, the new discovery doesn't add anything interesting,
         * so we can just ignore it. */
    }

    size_t size() const {
        return contains_val.size();
    }

    /* Remove some node with the smallest 'n_terms'
     * and return the removed node. */
    expr_node pop() {
        assert(size() != 0);
        if (min_nterms_cached.size() == 0) {
            recache();
        }
        arith_t old_top_val = min_nterms_cached.top();
        min_nterms_cached.pop();
        contains_val_t::iterator it = contains_val.find(old_top_val);
        assert(it != contains_val.end());
        /* Copy */
        expr_node old_top = it->second;
        contains_val.erase(it);
        return old_top;
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

static void provide(arith_t d) {
    expr_node node = {.n_terms = 1, .val = d, .val_left = d, .val_right = d,
                      .op = OP_NONE};
    list_open.push(node);
}

static void print_rpn(arith_t val);
static void discover(const expr_node& node) {
    /* Only add to open list if not already known in closed list.
     * (Avoid rediscovering easily-generated values like 0 or 1.) */
    if (list_closed.count(node.val) != 0) {
        return;
    }
    arith_t abs_val = labs(node.val);
    if (abs_val >= max_relevant) {
        return;
    }
    if (node.val == goal) {
        std::cout << "One way =";
        print_rpn(node.val_left);
        print_rpn(node.val_right);
        std::cout << " +" << std::endl;
    }
    // PRINTME std::cout << "  Discovered " << node.val << " at depth " << node.n_terms << std::endl;
    list_open.push(node);
}

static void generate_against(const expr_node& a, const expr_node& b) {
    expr_node node;
    node.n_terms = a.n_terms + b.n_terms;
    assert(node.n_terms >= 2);

    node.val_left = a.val;
    node.val_right = b.val;
    /*
    if (b.val != 0 && a.val % b.val == 0) {
    node.val = a.val / b.val; node.op = OP_DIV;   discover(node);
    }
    */
    node.val = a.val - b.val; node.op = OP_MINUS; discover(node);
    node.val = a.val * b.val; node.op = OP_MULT;  discover(node);
    node.val = a.val + b.val; node.op = OP_PLUS;  discover(node);

    /* Try to avoid needless duplicates */
    if (b.val != a.val) {
        node.val_left = b.val;
        node.val_right = a.val;
        /*
        if (a.val != 0 && b.val % a.val == 0) {
        node.val = a.val / b.val; node.op = OP_DIV;   discover(node);
        }
        */
        node.val = b.val - a.val; node.op = OP_MINUS; discover(node);
    }
}

static void print_rpn(arith_t val) {
    const expr_node& node = list_closed.at(val);
    assert(node.val == val);
    if (node.op == OP_NONE) {
        std::cout << " " << node.val;
    } else {
        print_rpn(node.val_left);
        print_rpn(node.val_right);
        /* Evil hack: '.op' is both an enum
         * *and* the representing character. */
        std::cout << " " << (char)node.op;
    }
}

int main() {
    /* Tweak this if you feel like it. */
    provide(69);
    provide(420);
    /* Manual seeding (bad hack) */
    {
        list_open.push(expr_node{.n_terms = 2, .val = 69*2, .val_left = 69*1, .val_right = 69, .op = OP_PLUS});
        list_open.push(expr_node{.n_terms = 3, .val = 69*3, .val_left = 69*2, .val_right = 69, .op = OP_PLUS});
        list_open.push(expr_node{.n_terms = 4, .val = 69*4, .val_left = 69*3, .val_right = 69, .op = OP_PLUS});

        list_open.push(expr_node{.n_terms = 2, .val =    1, .val_left = 69*1, .val_right = 69, .op = OP_DIV});
        list_open.push(expr_node{.n_terms = 3, .val =    2, .val_left = 69*2, .val_right = 69, .op = OP_DIV});
        list_open.push(expr_node{.n_terms = 4, .val =    3, .val_left = 69*3, .val_right = 69, .op = OP_DIV});
        list_open.push(expr_node{.n_terms = 5, .val =    4, .val_left = 69*4, .val_right = 69, .op = OP_DIV});
    }

    /* Did you provide at least one value? */
    assert(list_open.size() > 0);

    /* Search */
    while (true) {
        /* Copy */
        expr_node node = list_open.pop();
        std::cout << "Expanding " << node.val << " at depth " << node.n_terms << ", " << list_open.size_dead() << " dead nodes, " << list_open.size() << " open, " << list_closed.size() << " closed." << std::endl;

        /* First add it to the closed list, so it can be
         * "generated against" itself: */
        list_closed.emplace(node.val, node);

        if (node.val == goal) {
            /* Whee, we found it! */
            break;
        }

        for (const list_closed_t::value_type& peer_kv : list_closed) {
            generate_against(node, peer_kv.second);
        }
    }
    std::cout << "Done!  Printing ..." << std::endl;

    /* Printing */
    std::cout << goal << " =";
    print_rpn(goal);
    std::cout << std::endl;

    return 0;
}
