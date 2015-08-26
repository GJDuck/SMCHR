/*
 * solver_linear.c
 * Copyright (C) 2013 National University of Singapore
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <wmmintrin.h>

#include "solver.h"

#define LINEAR_X_EQ_C_ROW
#define LINEAR_CHECK_OVERFLOW

/*
 * Rational operations.
 */
typedef num_t rational_t __attribute__((__vector_size__(16)));
#define N           0
#define D           1

static inline rational_t ALWAYS_INLINE rational(num_t n)
{
    rational_t z = {n, 1};
    return z;
}
static inline num_t ALWAYS_INLINE rational_val(rational_t x)
{
    return x[N] / x[D];
}
static inline rational_t ALWAYS_INLINE rational_normalize(rational_t x)
{
    check(x[D] > 0);
    num_t n = (num_t)gcd((int64_t)x[N], (int64_t)x[D]);
    rational_t d = {n, n};
    rational_t z = _mm_div_pd(x, d);
    return z;
}
static inline void rational_check(const char *where, rational_t x)
{
#ifdef LINEAR_CHECK_OVERFLOW
    if (fabs(x[N]) > NUM_INT_MAX || fabs(x[D]) > NUM_INT_MAX)
    {
        error("linear solver: integer (%s) overflow detected", where);
        bail();
    }
#endif
}
static inline rational_t ALWAYS_INLINE rational_add_0(rational_t x,
    rational_t y)
{
    num_t n = (num_t)gcd((int64_t)x[D], (int64_t)y[D]);
    rational_t mm = {n, n};
    rational_t dd = _mm_shuffle_pd(y, x, 0x3);  // {y[D], x[D]};
    dd = _mm_div_pd(dd, mm);
    rational_t nn = _mm_shuffle_pd(x, y, 0x0);  // {x[N], y[N]};
    nn = _mm_mul_pd(nn, dd);
    rational_t z = {nn[0] + nn[1], dd[1] * y[D]};
    rational_check("addition", z);
    return z;
}
static inline rational_t ALWAYS_INLINE rational_add(rational_t x, rational_t y)
{
    rational_t z = rational_add_0(x, y);
    return rational_normalize(z);
}
static inline rational_t ALWAYS_INLINE rational_sub_0(rational_t x,
    rational_t y)
{
    num_t n = (num_t)gcd((int64_t)x[D], (int64_t)y[D]);
    rational_t mm = {n, n};
    rational_t dd = _mm_shuffle_pd(y, x, 0x3);  // {y[D], x[D]};
    dd = _mm_div_pd(dd, mm);
    rational_t nn = _mm_shuffle_pd(x, y, 0x0);  // {x[N], y[N]};
    nn = _mm_mul_pd(nn, dd);
    rational_t z = {nn[0] - nn[1], dd[1] * y[D]};
    rational_check("subtraction", z);
    return z;
}
static inline rational_t ALWAYS_INLINE rational_sub(rational_t x, rational_t y)
{
    rational_t z = rational_sub_0(x, y);
    return rational_normalize(z);
}
static inline rational_t ALWAYS_INLINE rational_mul_0(rational_t x,
    rational_t y)
{
    rational_t z = _mm_mul_pd(x, y);
    rational_check("multiplication", z);
    return z;
}
static inline rational_t ALWAYS_INLINE rational_mul(rational_t x, rational_t y)
{
    rational_t z = rational_mul_0(x, y);
    return rational_normalize(z);
}
static inline rational_t ALWAYS_INLINE rational_div_0(rational_t x,
    rational_t y)
{
    rational_t z = _mm_shuffle_pd(y, y, 0x01);
    z = _mm_mul_pd(x, z);
    rational_check("division", z);
    z = (z[D] < 0? -z: z);
    return z;
}
static inline rational_t ALWAYS_INLINE rational_div(rational_t x, rational_t y)
{
    rational_t z = rational_div_0(x, y);
    return rational_normalize(z);
}
static inline rational_t ALWAYS_INLINE rational_inv(rational_t x)
{
    rational_t z = _mm_shuffle_pd(x, x, 0x01);
    z = (z[D] < 0? -z: z);
    return z;
}
static inline rational_t ALWAYS_INLINE rational_neg(rational_t x)
{
    rational_t z = {-x[N], x[D]};
    return z;
}

/*
 * Linear variables.
 */
typedef ssize_t lvar_t;
#define LVAR_NIL            ((lvar_t)(0))

typedef struct row_s *row_t;
struct varinfo_s
{
    var_t x;                // This variable.
    row_t row;              // Row if basic, otherwise NULL.
    num_t lb;               // Variable's lower bound.
    num_t ub;               // Variable's upper bound.
    word_t reason_lb;       // Reason for lower bound.
    word_t reason_ub;       // Reason for upper bound.
    rational_t val;         // Variable's current value (numerator).
};
typedef struct varinfo_s *varinfo_t;

/*
 * Linear row (constraint).
 */
struct entry_s
{
    rational_t c;
    lvar_t x;
};
typedef struct entry_s *entry_t;

struct row_s
{
    lvar_t s;
    num_t d;
    size_t length;
    size_t size;
    entry_t xs;
};
typedef struct row_s *row_t;

/*
 * Linear tableau.
 */
struct tableau_s
{
    size_t length;          // Length of 'rows'
    size_t size;            // Size of 'rows'
    row_t *rows;            // Rows (constraints)
    size_t vars_len;        // Lenfth of 'vars'.
    varinfo_t vars;         // All linear variables.
    size_t slack_id;        // Slack variable ID.
};
typedef struct tableau_s *tableau_t;

/*
 * var_t interface.
 */
static size_t lvar_offset;

/*
 * The global tableau.
 */
static struct tableau_s tableau_0;
#define tableau         (&tableau_0)

/*
 * Prototypes.
 */
static void linear_init(void);
static void linear_reset(void);
static void linear_setlb_reason(lvar_t x, num_t lb, literal_t reason);
static void linear_setub_reason(lvar_t x, num_t ub, literal_t reason);
static void linear_update(lvar_t x, rational_t v0, rational_t v);
static void linear_solve(void);
static bool linear_step(void);
static size_t linear_row_lookup(row_t row, lvar_t x);
static rational_t linear_row_update(row_t rowa, row_t rowb, rational_t n,
    rational_t d, lvar_t s);
static void linear_x_gt_c_handler(prop_t prop);
static void linear_lb_x_handler(prop_t prop);
static void linear_x_gt_y_handler(prop_t prop);
static void linear_x_eq_c_handler(prop_t prop);
static void linear_x_eq_y_handler(prop_t prop);
static void linear_x_eq_y_plus_c_handler(prop_t prop);
static void linear_x_eq_y_plus_z_handler(prop_t prop);
static void linear_x_eq_c_mul_y_handler(prop_t prop);
static void linear_dump(void);

/*
 * Solver.
 */
static struct solver_s solver_linear_0 =
{
    linear_init,
    linear_reset,
    "linear"
};
solver_t solver_linear = &solver_linear_0;

/*
 * Init the solver.
 */
static void linear_init(void)
{
    register_solver(GT_C, 3, EVENT_DECIDE, linear_x_gt_c_handler);
    register_solver(LB, 3, EVENT_DECIDE, linear_lb_x_handler);
#ifdef LINEAR_X_EQ_C_ROW
    register_solver(EQ_C, 3, EVENT_DECIDE, linear_x_eq_c_handler);
#else
    register_solver(EQ_C, 3, EVENT_DECIDE, linear_x_eq_c_handler);
#endif
    register_solver(GT, 3, EVENT_DECIDE, linear_x_gt_y_handler);
    register_solver(EQ, 3, EVENT_DECIDE, linear_x_eq_y_handler);
    register_solver(EQ_PLUS_C, 3, EVENT_DECIDE, linear_x_eq_y_plus_c_handler);
    register_solver(EQ_PLUS, 3, EVENT_DECIDE, linear_x_eq_y_plus_z_handler);
    register_solver(EQ_MUL_C, 3, EVENT_DECIDE, linear_x_eq_c_mul_y_handler);

    lvar_t x = LVAR_NIL;
    lvar_offset = alloc_extra(1, (word_t *)&x);

    memset(tableau, 0, sizeof(struct tableau_s));
    if (!gc_root(tableau, sizeof(struct tableau_s)))
        panic("failed to set GC root for tableau: %s", strerror(errno));
    if (!gc_dynamic_root((void **)&tableau->vars, &tableau->vars_len,
            sizeof(struct varinfo_s)))
        panic("failed to set GC root for linear vars: %s", strerror(errno));
}

/*
 * Reset the solver.
 */
static void linear_reset(void)
{
    for (size_t i = 0; i < tableau->length; i++)
        gc_free(tableau->rows[i]);
    gc_free(tableau->rows);
    buffer_free(tableau->vars, tableau->vars_len*sizeof(struct varinfo_s));
    tableau->length = 0;
    tableau->size = 0;
    tableau->rows = NULL;
    tableau->vars_len = 1;
    size_t size = 0x3FFFFFFF;
    tableau->vars = buffer_alloc(size);
//    tableau->vars[0] = NULL;
    tableau->slack_id = 0;
}

static inline row_t linear_getrow(lvar_t x)
{
    return tableau->vars[x].row;
}
static inline void linear_setrow(lvar_t x, row_t row)
{
    tableau->vars[x].row = row;
}
static inline num_t linear_getlb(lvar_t x)
{
    return tableau->vars[x].lb;
}
static inline num_t linear_getub(lvar_t x)
{
    return tableau->vars[x].ub;
}
static inline literal_t linear_getlb_reason(lvar_t x)
{
    return tableau->vars[x].reason_lb;
}
static inline literal_t linear_getub_reason(lvar_t x)
{
    return tableau->vars[x].reason_ub;
}
static inline rational_t linear_getval(lvar_t x)
{
    return tableau->vars[x].val;
}
static inline var_t linear_getvar(lvar_t x);        // XXX
static inline void linear_setval(lvar_t x, rational_t val)
{
    tableau->vars[x].val = val;
}
static inline var_t linear_getvar(lvar_t x)
{
    varinfo_t info = tableau->vars + x;
    return info->x;
}

/*
 * Set the LB of x.
 */
static void linear_setlb_reason(lvar_t x, num_t lb, literal_t reason)
{
    trail(&tableau->vars[x].lb);
    tableau->vars[x].lb = lb;
    trail(&tableau->vars[x].reason_lb);
    tableau->vars[x].reason_lb = reason;
   
    row_t row = linear_getrow(x);
    if (row == NULL)
    {
        rational_t val = linear_getval(x);
        if (rational_val(val) < lb)
            linear_update(x, val, rational(lb));
    }
}

/*
 * Set the UB of x.
 */
static void linear_setub_reason(lvar_t x, num_t ub, literal_t reason)
{
    trail(&tableau->vars[x].ub);
    tableau->vars[x].ub = ub;
    trail(&tableau->vars[x].reason_ub);
    tableau->vars[x].reason_ub = reason;

    row_t row = linear_getrow(x);
    if (row == NULL)
    {
        rational_t val = linear_getval(x);
        if (rational_val(val) > ub)
            linear_update(x, val, rational(ub));
    }
}

/*
 * Update a variable.
 */
static void linear_update(lvar_t x, rational_t v0, rational_t v)
{
    for (size_t i = 0; i < tableau->length; i++)
    {
        row_t row = tableau->rows[i];
        lvar_t s = row->s;
        size_t j = linear_row_lookup(row, x);
        if (row->xs[j].x != x)
            continue;
        rational_t vals = linear_getval(s);
        vals = rational_add(vals,
            rational_mul_0(row->xs[j].c, rational_sub_0(v, v0)));
        linear_setval(s, vals);
    }
    linear_setval(x, v);
}

/*
 * Solve the tableau.
 */
static void linear_solve(void)
{
    while (linear_step())
        ;
    linear_dump();
}

/*
 * Do one solve step.
 */
static bool linear_step(void)
{
    row_t row;
    lvar_t s, x;
    size_t i, j;
    num_t bs;
    rational_t valx, vals, c;
    for (i = 0; i < tableau->length; i++)
    {
        row = tableau->rows[i];
        s = row->s;
        vals = linear_getval(s);
        bs = linear_getlb(s);
        if (rational_val(vals) < bs)
        {
            reason_t reason = make_reason();
            for (j = 0; j < row->length; j++)
            {
                c = row->xs[j].c;
                x = row->xs[j].x;
                valx = linear_getval(x);
                if (c[N] < 0)
                {
                    num_t lbx = linear_getlb(x);
                    if (rational_val(valx) > lbx)
                        goto found_pivot;
                    antecedent(reason, linear_getlb_reason(x));
                }
                else
                {
                    num_t ubx = linear_getub(x);
                    if (rational_val(valx) < ubx)
                        goto found_pivot;
                    antecedent(reason, linear_getub_reason(x));
                }
            }

            // UNSAT:
            // linear_dump();
            debug("!gLINEAR!d UNSAT [ROW #%zu]", i);
            consequent(reason, -linear_getlb_reason(s));
            fail(reason);
        }

        bs = linear_getub(s);
        if (rational_val(vals) > bs)
        {
            reason_t reason = make_reason();
            for (j = 0; j < row->length; j++)
            {
                c = row->xs[j].c;
                x = row->xs[j].x;
                valx = linear_getval(x);
                if (c[N] > 0)
                {
                    num_t lbx = linear_getlb(x);
                    if (rational_val(valx) > lbx)
                        goto found_pivot;
                    antecedent(reason, linear_getlb_reason(x));
                }
                else
                {
                    num_t ubx = linear_getub(x);
                    if (rational_val(valx) < ubx)
                        goto found_pivot;
                    antecedent(reason, linear_getub_reason(x));
                }
            }

            // UNSAT:
            // linear_dump();
            debug("!gLINEAR!d UNSAT [ROW #%zu]", i);
            consequent(reason, -linear_getub_reason(s));
            fail(reason);
        }
    }
    
    // SAT:
    return false;

found_pivot: 

    debug("!rPIVOT!d x=%s s=%s", show(term_var(linear_getvar(x))),
        show(term_var(linear_getvar(s))));

    stat_pivots++; 

    // XXX: optimize
    rational_t nvalx = rational_add(valx,
        rational_div(rational_sub_0(rational(bs), vals), c));
    linear_setval(x, nvalx);
    linear_setval(s, rational(bs));

    // Pivot:
    for (size_t k = 0; k < row->length; k++)
    {
        rational_t d = rational_div(row->xs[k].c, c);
        row->xs[k].c = rational_neg(d);
    }
    for (size_t k = 0; k < tableau->length; k++)
    {
        row_t rowt = tableau->rows[k];
        lvar_t t = rowt->s;
        if (t == s)
            continue;
        size_t l = linear_row_lookup(rowt, x);
        if (rowt->xs[l].x != x)
            continue;
        rational_t ct = rowt->xs[l].c;
        rational_t cu = rational_div(ct, c);
        rational_t valt = linear_row_update(rowt, row, ct, cu, s);
        linear_setval(t, valt);
    }

    size_t k;
    if (s < x)
    {
        for (k = j; k > 0 && row->xs[k-1].x > s; k--)
        {
            row->xs[k].x = row->xs[k-1].x;
            row->xs[k].c = row->xs[k-1].c;
        }
    }
    else
    {
        for (k = j; k < row->length-1 && row->xs[k+1].x < s; k++)
        {
            row->xs[k].x = row->xs[k+1].x;
            row->xs[k].c = row->xs[k+1].c;
        }
    }
    row->xs[k].x = s;
    row->xs[k].c = rational_inv(c);
    row->s = x;

    linear_setrow(x, row);
    linear_setrow(s, NULL);

    return true;
}

/*
 * Search for a variable in row.
 */
static size_t linear_row_lookup(row_t row, lvar_t x)
{
    entry_t xs = row->xs;
    ssize_t lo = 0, hi = row->length-1;
    while (lo < hi)
    {
        ssize_t mid = (lo + hi) / 2;
        if (x < xs[mid].x)
            hi = mid - 1;
        else if (x > xs[mid].x)
            lo = mid + 1;
        else
            return mid;
    }
    return lo;
}

#if 0
/*
 * For debugging:
 */
static void linear_show_row(row_t row)
{
    message_0("%s = ", show_var(linear_getvar(row->s)));
    for (size_t i = 0; i < row->length; i++)
    {
        message_0("%s*%s", show_num(rational_val(row->xs[i].c)),
            show_var(linear_getvar(row->xs[i].x)));
        if (i != row->length - 1)
            message_0(" + ");
    }
}
#endif

/*
 * Update operation for row:
 */
static rational_t linear_row_update(row_t rowa, row_t rowb, rational_t n,
    rational_t d, lvar_t s)
{
    struct entry_s xs[rowa->length + rowb->length];
    size_t i = 0, j = 0;
    ssize_t k = 0;
    while (i < rowa->length && j < rowb->length)
    {
        if (rowa->xs[i].x < rowb->xs[j].x)
        {
            xs[k].x = rowa->xs[i].x;
            xs[k].c = rowa->xs[i].c;
            k++; i++;
        }
        else if (rowa->xs[i].x > rowb->xs[j].x)
        {
            xs[k].x = rowb->xs[j].x;
            xs[k].c = rational_mul(n, rowb->xs[j].c);
            k++; j++;
        }
        else
        {
            rational_t c = rational_add(rowa->xs[i].c,
                rational_mul_0(n, rowb->xs[j].c));
            if (c[N] == 0)
            {
                i++; j++;
                continue;
            }
            xs[k].x = rowa->xs[i].x;
            xs[k].c = c;
            k++; i++; j++;
        }
    }
    while (i < rowa->length)
    {
        xs[k].x = rowa->xs[i].x;
        xs[k].c = rowa->xs[i].c;
        k++; i++;
    }
    while (j <  rowb->length)
    {
        xs[k].x = rowb->xs[j].x;
        xs[k].c = rational_mul(n, rowb->xs[j].c);
        k++; j++;
    }

    k++;
    if (k > rowa->size)
    {
        gc_free(rowa->xs);
        rowa->xs = (entry_t)gc_malloc(k*sizeof(struct entry_s));
        rowa->size = k;
    }

    rational_t val = rational(0);
    for (i = 0, j = 0; i < k-1; i++, j++)
    {
        lvar_t x = xs[i].x;
        rational_t c = xs[i].c;
        if (i == j && s < x)
        {
            rowa->xs[j].x = s;
            rowa->xs[j].c = d;
            val = rational_add(val, rational_mul_0(d, linear_getval(s)));
            j++;
        }
        rowa->xs[j].x = x;
        rowa->xs[j].c = c;
        val = rational_add(val, rational_mul_0(c, linear_getval(x)));
    }
    if (j < k)
    {
        check(i == j);
        rowa->xs[j].x = s;
        rowa->xs[j].c = d;
        val = rational_add(val, rational_mul_0(d, linear_getval(s)));
    }
    rowa->length = k;

#if 0
    message_0("\t[");
    linear_show_row(rowa);
    message("]");
#endif
    return val;
}

/****************************************************************************/
/* INTERFACE                                                                */
/****************************************************************************/

/*
 * Init a new lvar_t
 */
static lvar_t linear_init_var(var_t x0)
{
    lvar_t x = tableau->vars_len++;
    if (x0 == (var_t)NULL)
    {
        char *name = unique_name("SLK", &tableau->slack_id);
        x0 = make_var(name);
    }
    varinfo_t info = tableau->vars + x;
    info->x = x0;
    info->row = NULL;
    info->lb = -inf;
    info->ub = inf;
    info->reason_lb = LITERAL_TRUE;
    info->reason_ub = LITERAL_TRUE;
    info->val = rational(0);
    lvar_t *xptr = (lvar_t *)extra(x0, lvar_offset);
    *xptr = x;
    debug("LINEAR INIT %s", show_var(x0));
    return x;
}

/*
 * var -> lvar
 */
static lvar_t linear_var(var_t x)
{
    lvar_t *yptr = (lvar_t *)extra(x, lvar_offset);
    lvar_t y = *yptr;
    if (y != LVAR_NIL)
        return y;
    y = linear_init_var(x);
    return y;
}

/*
 * Row substitution.
 */
static void linear_row_substitute(row_t rowa, row_t rowb, rational_t n)
{
    struct entry_s xs[rowa->length + rowb->length];
    size_t i = 0, j = 0;
    ssize_t k = 0;
    while (i < rowa->length && j < rowb->length)
    {
        if (rowa->xs[i].x == rowb->s)
        {
            i++;
            continue;
        }
        if (rowa->xs[i].x < rowb->xs[j].x)
        {
            xs[k].x = rowa->xs[i].x;
            xs[k].c = rowa->xs[i].c;
            k++; i++;
        }
        else if (rowa->xs[i].x > rowb->xs[j].x)
        {
            xs[k].x = rowb->xs[j].x;
            xs[k].c = rational_mul(n, rowb->xs[j].c);
            k++; j++;
        }
        else
        {
            rational_t c = rational_add(rowa->xs[i].c,
                rational_mul_0(n, rowb->xs[j].c));
            if (c[N] == 0)
            {
                i++; j++;
                continue;
            }
            xs[k].x = rowa->xs[i].x;
            xs[k].c = c;
            k++; i++; j++;
        }
    }
    while (i < rowa->length)
    {
        xs[k].x = rowa->xs[i].x;
        xs[k].c = rowa->xs[i].c;
        k++; i++;
    }
    while (j <  rowb->length)
    {
        xs[k].x = rowb->xs[j].x;
        xs[k].c = rational_mul(n, rowb->xs[j].c);
        k++; j++;
    }

    if (k > rowa->size)
    {
        gc_free(rowa->xs);
        rowa->xs = (entry_t)gc_malloc(k*sizeof(struct entry_s));
        rowa->size = k;
    }
    memcpy(rowa->xs, xs, k*sizeof(struct entry_s));
    rowa->length = k;
}

/*
 * Add a new row.
 */
static void linear_add_row(row_t row)
{
    debug("!gLINEAR!d ADD");

    // Set the basic variable's row:
    linear_setrow(row->s, row);

    // Calculate row's value:
    rational_t val = rational(0);
    for (size_t i = 0; i < row->length; i++)
        val = rational_add(val,
            rational_mul_0(row->xs[i].c, linear_getval(row->xs[i].x)));
    linear_setval(row->s, val);

    // Substitute away basic variables: 
    for (ssize_t i = 0; i < row->length; i++)
    {
        lvar_t x = row->xs[i].x;
        row_t rowb = linear_getrow(x);
        debug("!gLINEAR!d VAR %s[%zu] %s",
            show(term_var(linear_getvar(x))), i,
            (rowb != NULL? "BASIC": "NONBASIC"));
        if (rowb != NULL)
        {
            rational_t n = row->xs[i].c;
            linear_row_substitute(row, rowb, n);
            i = -1;
        }
    }
    
    // Add the row:
    if (tableau->length >= tableau->size)
    {
        tableau->size = (3 * tableau->size) / 2 + 8;
        tableau->rows = (row_t *)gc_realloc(tableau->rows, tableau->size*
            sizeof(row_t));
    }
    tableau->rows[tableau->length++] = row;

    // linear_dump();
}

/*
 * Add x - y - z row.
 */
static lvar_t linear_x_sub_y_sub_z_row(var_t x0, var_t y0, var_t z0)
{
    lvar_t x = linear_var(x0), y = linear_var(y0), z = linear_var(z0),
           s = linear_init_var((var_t)NULL);
    debug("x = %s; y = %s; z = %s",
        show(term_var(linear_getvar(x))),
        show(term_var(linear_getvar(y))),
        show(term_var(linear_getvar(z))));
    
    row_t row = (row_t)gc_malloc(sizeof(struct row_s));
    row->s = s;
    row->size = row->length = 3;
    entry_t xs = (entry_t)gc_malloc(3*sizeof(struct entry_s));
    row->xs = xs;
    if (x < y)
    {
        if (y < z)
        {
            xs[0].x = x; xs[1].x = y; xs[2].x = z;
            xs[0].c = rational(1); xs[1].c = xs[2].c = rational(-1);
        }
        else if (x < z)
        {
            xs[0].x = x; xs[1].x = z; xs[2].x = y;
            xs[0].c = rational(1); xs[1].c = xs[2].c = rational(-1);   
        }
        else
        {
            xs[0].x = z; xs[1].x = x; xs[2].x = y;
            xs[0].c = xs[2].c = rational(-1); xs[1].c = rational(1);
        }
    }
    else
    {
        if (x < z)
        {
            xs[0].x = y; xs[1].x = x; xs[2].x = z;
            xs[0].c = xs[2].c = rational(-1); xs[1].c = rational(1);
        }
        else if (y < z)
        {
            xs[0].x = y; xs[1].x = z; xs[2].x = x;
            xs[0].c = xs[1].c = rational(-1); xs[2].c = rational(1);
        }
        else
        {
            xs[0].x = z; xs[1].x = y; xs[2].x = x;
            xs[0].c = xs[1].c = rational(-1); xs[2].c = rational(1);
        }
    }
    linear_add_row(row);
    return s;
}

/*
 * Add a x - c*y row.
 */
static lvar_t linear_x_sub_cy_row(var_t x0, num_t c, var_t y0)
{
    lvar_t x = linear_var(x0), y = linear_var(y0),
           s = linear_init_var((var_t)NULL);
    row_t row = (row_t)gc_malloc(sizeof(struct row_s));
    row->s = s;
    row->size = row->length = 2;
    entry_t xs = (entry_t)gc_malloc(2*sizeof(struct entry_s));
    row->xs = xs;
    if (x < y)
    {
        xs[0].x = x; xs[1].x = y;
        xs[0].c = rational(1); xs[1].c = rational(-c);
    }
    else
    {
        xs[0].x = y; xs[1].x = x;
        xs[0].c = rational(-c); xs[1].c = rational(1);
    }
    linear_add_row(row);
    return s;
}

/*
 * Set new lb.
 */
static bool linear_set_lb(lvar_t x, num_t lb, literal_t lit)
{
    num_t lb0 = linear_getlb(x);
    if (lb > lb0)
    {
        num_t ub0 = linear_getub(x);
        if (lb > ub0)
        {
            reason_t reason = make_reason(lit);
            consequent(reason, -linear_getub_reason(x));
            fail(reason);
        }
        linear_setlb_reason(x, lb, lit);
        return true;
    }
    return false;
}

/*
 * Set new ub.
 */
static bool linear_set_ub(lvar_t x, num_t ub, literal_t lit)
{
    num_t ub0 = linear_getub(x);
    if (ub < ub0)
    {
        num_t lb0 = linear_getlb(x);
        if (ub < lb0)
        {
            reason_t reason = make_reason(lit);
            consequent(reason, -linear_getlb_reason(x));
            fail(reason);
        }
        linear_setub_reason(x, ub, lit);
        return true;
    }
    return false;
}

/*
 * s >= c
 */
static void linear_s_geq_c(bvar_t b, var_t s, num_t c)
{
    debug("linear_s_geq_c: %s >= %s", show_var(s), show_num(c));

    reason_t reason = make_reason();

    cons_t lb = make_cons(reason, LB, term_var(s), term_int(c));
    antecedent(reason, b);
    consequent(reason, lb->b);
    redundant(reason);

    undo(reason, 2);
    antecedent(reason, -b);
    consequent(reason, -lb->b);
    redundant(reason);
}

/*
 * s = c
 */
static void linear_s_eq_c(bvar_t b, var_t s, num_t c)
{
    debug("linear_s_eq_c: %s = %s", show_var(s), show_num(c));

    reason_t reason_lb = make_reason();
    cons_t lb = make_cons(reason_lb, LB, term_var(s), term_int(c));
    antecedent(reason_lb, b);
    consequent(reason_lb, lb->b);
    redundant(reason_lb);

    reason_t reason_ub = make_reason();
    cons_t ub = make_cons(reason_ub, LB, term_var(s), term_int(c+1));
    antecedent(reason_ub, b);
    consequent(reason_ub, -ub->b);
    redundant(reason_ub);

    undo(reason_lb, 2);
    undo(reason_ub, 2);
    reason_t reason = reason_lb;
    append(reason, reason_ub);

    antecedent(reason, -b);
    consequent(reason, -lb->b);
    consequent(reason, ub->b);
    redundant(reason);
}

/*
 * x = y + z handler.
 */
static void linear_x_eq_y_plus_z_handler(prop_t prop)
{
    cons_t c = constraint(prop);
    lvar_t s = linear_x_sub_y_sub_z_row(var(c->args[X]), var(c->args[Y]),
        var(c->args[Z]));
    var_t t = linear_getvar(s);
    linear_s_eq_c(c->b, t, 0);
    annihilate(prop);
}

/*
 * x = y + c handler.
 */
static void linear_x_eq_y_plus_c_handler(prop_t prop)
{
    cons_t c = constraint(prop);
    lvar_t s = linear_x_sub_cy_row(var(c->args[X]), 1, var(c->args[Y]));
    var_t t = linear_getvar(s);
    linear_s_eq_c(c->b, t, num(c->args[Z]));
    annihilate(prop);
}

/*
 * x = y handler.
 */
static void linear_x_eq_y_handler(prop_t prop)
{
    cons_t c = constraint(prop);
    lvar_t s = linear_x_sub_cy_row(var(c->args[X]), 1, var(c->args[Y]));
    var_t t = linear_getvar(s);
    linear_s_eq_c(c->b, t, 0);
    annihilate(prop);
}

/*
 * x = c*y handler.
 */
static void linear_x_eq_c_mul_y_handler(prop_t prop)
{
    cons_t c = constraint(prop);
    lvar_t s = linear_x_sub_cy_row(var(c->args[X]), num(c->args[Z]),
        var(c->args[Y]));
    var_t t = linear_getvar(s);
    linear_s_eq_c(c->b, t, 0);
    annihilate(prop);
}

/*
 * x > y handler.
 */
static void linear_x_gt_y_handler(prop_t prop)
{
    cons_t c = constraint(prop);
    lvar_t s = linear_x_sub_cy_row(var(c->args[X]), 1, var(c->args[Y]));
    var_t t = linear_getvar(s);
    linear_s_geq_c(c->b, t, 1);
    annihilate(prop);
}

/*
 * x = c handler.
 */
static void linear_x_eq_c_handler(prop_t prop)
{
#ifdef LINEAR_X_EQ_C_ROW
    cons_t c = constraint(prop);
    term_t x = c->args[X];
    num_t k = num(c->args[Y]);
    switch (decision(c->b))
    {
        case TRUE:
        {
            reason_t reason = make_reason(c->b);
            cons_t lb = make_cons(reason, LB, x, term_int(k));
            consequent(reason, lb->b);
            propagate(reason);

            restore(reason, 1);
            cons_t ub = make_cons(reason, LB, x, term_int(k+1));
            consequent(reason, -ub->b);
            propagate(reason);

            return;
        }
        case FALSE:
        {
            reason_t reason = make_reason(-c->b);
            cons_t lb = make_cons(reason, LB, x, term_int(k));
            cons_t ub = make_cons(reason, LB, x, term_int(k+1));
            consequent(reason, -lb->b);
            consequent(reason, ub->b);
            propagate(reason);

            return;
        }
        case UNKNOWN:
            return;
    }

#else       /* LINEAR_X_EQ_C_ROW */
    cons_t c = constraint(prop);
    var_t x = var(c->args[X]);
    num_t k = num(c->args[Y]);
    lvar_t xx = linear_var(x);
    switch (decision(c->b))
    {
        case TRUE:
        {
            bool solve_1 = linear_set_lb(xx, k, c->b);
            bool solve_2 = linear_set_ub(xx, k, c->b);
            if (solve_1 || solve_2)
                linear_solve();
            return;
        }
        case FALSE:
        {
            num_t lb = linear_getlb(xx);
            if (lb > k)
                return;
            num_t ub = linear_getub(xx);
            if (ub < k)
                return;
            reason_t reason = make_reason(-c->b);
            cons_t lbc = make_cons(reason, LB, term_var(x), term_int(k));
            cons_t ubc = make_cons(reason, LB, term_var(x), term_int(k+1));
            consequent(reason, ubc->b);
            consequent(reason, -lbc->b);
            redundant(reason);
            kill(prop);
            return;
        }
        case UNKNOWN:
            return;
    }
#endif      /* LINEAR_X_EQ_C_ROW */
}

/*
 * x > c handler.
 */
static void linear_x_gt_c_handler(prop_t prop)
{
    cons_t c = constraint(prop);
    var_t x = var(c->args[X]);
    num_t k = num(c->args[Y]);
    lvar_t xx = linear_var(x);
    switch (decision(c->b))
    {
        case TRUE:
        {
            if (linear_set_lb(xx, k+1, c->b))
                linear_solve();
            return;
        }
        case FALSE:
        {
            if (linear_set_ub(xx, k, -c->b))
                linear_solve();
            return;
        }
        case UNKNOWN:
            return;
    }
}

/*
 * Bounds handler.
 */
static void linear_lb_x_handler(prop_t prop)
{
    cons_t c = constraint(prop);
    var_t x = var(c->args[X]);
    num_t k = num(c->args[Y]);
    lvar_t xx = linear_var(x);
    switch (decision(c->b))
    {
        case TRUE:
            if (linear_set_lb(xx, k, c->b))
                linear_solve();
            return;
        case FALSE:
            if (linear_set_ub(xx, k-1, -c->b))
                linear_solve();
            return;
        case UNKNOWN:
            return;
    }
}

/****************************************************************************/
/* DEBUGGING                                                                */
/****************************************************************************/

static void linear_dump(void)
{
#ifndef NODEBUG 
    message("!y+-------------------------------------------------------");
    for (size_t i = 0; i < tableau->length; i++)
    {
        row_t row = tableau->rows[i];
        term_t s = term_var(linear_getvar(row->s));
        term_t l = term_num(0);
        for (size_t j = 0; j < row->length; j++)
        {
            term_t x = term_var(linear_getvar(row->xs[j].x));
            term_t c = term_num(rational_val(row->xs[j].c));
            x = term_func(make_func(make_atom("*", 2), c, x));
            if (l == term_num(0))
                l = x;
            else
                l = term_func(make_func(make_atom("+", 2), l, x));
        }
        message("!y| !rROW #%zu!d: !g%s!d = !y%s!d", i,
            show(s), show(l));
    }
    message("!y+-------------------------------------------------------");
    for (lvar_t i = 1; i < tableau->vars_len; i++)
    {
        term_t x = term_var(linear_getvar(i));
        const char *s = show(x);
        num_t lb = linear_getlb(i), ub = linear_getub(i),
            val = rational_val(linear_getval(i));
        const char *fmt = (val >= lb && val <= ub?
            "!y| !d[!g%s = %s!d] [!y%s <= %s <= %s!d]":
            "!y| !d[!r%s = %s!d] [!y%s <= %s <= %s!d] (***)");
        message(fmt, s, show(term_num(val)),
            show(term_num(lb)), s, show(term_num(ub)));
    }
    bool stop = false;
    for (lvar_t i = 1; i < tableau->vars_len; i++)
    {
        row_t row = linear_getrow(i);
        if (row == NULL)
            continue;
        const char *s = show(term_var(linear_getvar(i)));
        rational_t val = linear_getval(i), val1 = rational(0);
        for (size_t j = 0; j < row->length; j++)
            val1 = rational_add(val1,
                rational_mul_0(row->xs[j].c, linear_getval(row->xs[j].x)));
        if (val[N] != val1[N] || val[D] != val1[D])
        {
            message("*** !rERROR!d ***: value for %s mismatch: %s=%s vs. %s=%s",
                s, s, show(term_num(rational_val(val))), s,
                show(term_num(rational_val(val1))));
            stop = true;
        }
        for (ssize_t j = 0; j < row->length; j++)
        {
            lvar_t x = row->xs[j].x;
            for (ssize_t k = j - 1; k >= 0; k--)
            {
                lvar_t y = row->xs[k].x;
                if (y >= x)
                {
                    message("*** !rERROR!d ***: bad ordering for row %s: "
                        "%s (%zu) >= %s (%zu)", s,
                        show(term_var(linear_getvar(y))), y,
                        show(term_var(linear_getvar(x))), x);
                    stop = true;
                }
            }
        }
    }
    message("!y+-------------------------------------------------------");
    if (stop)
        abort();
#endif      /* NODEBUG */
}

