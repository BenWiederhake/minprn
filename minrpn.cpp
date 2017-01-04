/*
 * minrpn, finds the minimal RPN expression for an arbitrary desired result
 * Copyright Ben Wiederhake, 2016
 * MIT License
 *
 * Compile:
 *   clang++ -std=c++11 -o minrpn minrpn.cpp
 * Compile with warnings:
 *   clang++ -std=c++11 -Weverything -Wno-padded -Wno-c++98-compat -Wno-global-constructors -Wno-exit-time-destructors -Wno-float-equal -Wno-c99-extensions -o minrpn minrpn.cpp
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

/* Some very light configuration / pruning */
typedef double arith_t;
static const arith_t max_relevant = 10 * 1000 * 1000;
static const arith_t min_relevant = 1e-7;

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
    /* For "min(n_terms) pop".
     * May contain outdated nodes; see 'contains_val'. */
    typedef std::multimap<size_t, arith_t> by_nterms_t;
    by_nterms_t by_nterms;
    /* Keeps track of which elements actually "exist". */
    typedef std::unordered_map<arith_t, expr_node> contains_val_t;
    contains_val_t contains_val;

public:
    /* Insert the given node. */
    void push(const expr_node& node) {
        assert(node.n_terms >= 1);
        /* If the given node results in a value that hasn't been seen before,
         * this is perfectly fine.  In the other case, it is too difficult to
         * properly update the entry in 'by_nterms', so just treat it as new.
         * 'pop()' will have to deal with the cleanup. */
        contains_val_t::iterator contains_val_it = contains_val.find(node.val);
        if (contains_val_it == contains_val.end()) {
            /* Did not exist yet. */
            by_nterms.emplace(node.n_terms, node.val);
            contains_val.emplace(node.val, node);
        } else if (contains_val_it->second.n_terms > node.n_terms) {
            /* Did exist, and we found a strictly better solution.
             * Note that we leave garbage behind in 'by_nterms'. */
            by_nterms.emplace(node.n_terms, node.val);
            contains_val_it->second = node;
        }
        /* Otherwise, the new discovery doesn't add anything interesting,
         * so we can just ignore it. */
    }

    size_t size() const {
        return contains_val.size();
    }

    size_t size_dead() const {
        assert(by_nterms.size() >= contains_val.size());
        return by_nterms.size() - contains_val.size();
    }

    /* Remove some node with the smallest 'n_terms' and return the removed node.
     * It is possible, but not guaranteed, that a push-after-pop of the same
     * value appears to be overridden by an older push.
     * Example: push(1), push(2), pop(1), push(3), pop(2)
     * (One might expect that the last 'pop' can only possibly "see" the
     *  'n_terms=3' node.)
     * This unintuitive behavior is okay because this access pattern
     * won't happen in this program. */
    expr_node pop() {
        assert(size() != 0);
        while (true) {
            assert(contains_val.size() != 0);
            assert(by_nterms.size() >= contains_val.size());
            
            by_nterms_t::iterator by_nterms_it = by_nterms.begin();
            contains_val_t::iterator contains_val_it =
                contains_val.find(by_nterms_it->second);
            if (contains_val_it == contains_val.end()) {
                /* Whoops, this shouldn't exist anymore. */
                std::cout << "Skipped dead item " << by_nterms_it->second << " at depth " << by_nterms_it->first << std::endl;
                by_nterms.erase(by_nterms_it);
            } else {
                assert(by_nterms_it->first == contains_val_it->second.n_terms);
                /* Great!  Return this. */
                expr_node buf = contains_val_it->second;
                assert(buf.n_terms >= 1);
                by_nterms.erase(by_nterms_it);
                contains_val.erase(contains_val_it);
                return buf;
            }
        };
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

static void discover(const expr_node& node) {
    /* Only add to open list if not already known in closed list.
     * (Avoid rediscovering easily-generated values like 0 or 1.) */
    if (list_closed.count(node.val) != 0) {
        return;
    }
    arith_t abs_val = fabs(node.val);
    if (abs_val <= min_relevant || abs_val >= max_relevant) {
        return;
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
    node.val = a.val / b.val; node.op = OP_DIV;   discover(node);
    node.val = a.val - b.val; node.op = OP_MINUS; discover(node);
    node.val = a.val * b.val; node.op = OP_MULT;  discover(node);
    node.val = a.val + b.val; node.op = OP_PLUS;  discover(node);

    /* Try to avoid needless duplicates */
    if (b.val != a.val) {
        node.val_left = b.val;
        node.val_right = a.val;
        node.val = b.val / a.val; node.op = OP_DIV;   discover(node);
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
        std::cout << " " << node.op;
    }
}

int main() {
    /* Tweak this if you feel like it. */
    arith_t goal = 2017;
    provide(69);
    provide(420);

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

        if (fabs(node.val - goal) <= 1e-5) {
            /* Whee, we found it!
             * First, a dirty hack to ensure bitwise identity: */
            goal = node.val;
            /* Then, we're done with the search. */
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
