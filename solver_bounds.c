/*
 * solver_bounds.c
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

/*
 * Bounds type.
 */
typedef num_t bounds_t __attribute__((__vector_size__(16)));
#define L           0
#define U           1

/*
 * Bounds info.
 */
struct boundsinfo_s
{
    bounds_t bs;
    cons_t lb;
    cons_t ub;
    proplist_t delays;
};
typedef struct boundsinfo_s *boundsinfo_t;

/*
 * var_t interface.
 */
static size_t bounds_offset;

/*
 * Prototypes.
 */
static void bounds_init(void);
static bounds_t bounds_get(reason_t reason_lb, reason_t reason_ub, var_t x);
static num_t bounds_get_lb(reason_t reason, var_t x);
static num_t bounds_get_ub(reason_t reason, var_t x);
static cons_t bounds_get_lb_cons(var_t x);
static cons_t bounds_get_ub_cons(var_t x);
static void bounds_set_lb_cons(var_t x, cons_t c, num_t lb);
static void bounds_set_ub_cons(var_t x, cons_t c, num_t ub);
static bounds_t bounds_set_lb(reason_t reason, var_t x, num_t lb);
static bounds_t bounds_set_ub(reason_t reason, var_t x, num_t ub);
static void bounds_delay(prop_t prop);
static void bounds_lb_handler(prop_t prop);
static void bounds_x_gt_c_handler(prop_t prop);
static void bounds_x_gt_y_handler(prop_t prop);
static void bounds_x_eq_c_handler(prop_t prop);
static void bounds_x_eq_y_handler(prop_t prop);
static void bounds_x_eq_y_plus_c_handler(prop_t prop);
static void bounds_x_eq_y_mul_c_handler(prop_t prop);
static void bounds_x_eq_y_plus_z_handler(prop_t prop);
static void bounds_x_eq_y_mul_z_handler(prop_t prop);

/*
 * Solver.
 */
static struct solver_s solver_bounds_0 =
{
    bounds_init,
    NULL,
    "bounds"
};
solver_t solver_bounds = &solver_bounds_0;

/*
 * Initialize this solver.
 */
static void bounds_init(void)
{
    register_solver(LB, 2, EVENT_ALL, bounds_lb_handler, make_lookup(T, T));

    register_solver(EQ, 3, EVENT_ALL, bounds_x_eq_y_handler);
    register_solver(EQ_C, 3, EVENT_ALL, bounds_x_eq_c_handler);
    register_solver(GT, 3, EVENT_ALL, bounds_x_gt_y_handler);
    register_solver(GT_C, 3, EVENT_ALL, bounds_x_gt_c_handler);
    register_solver(EQ_PLUS_C, 3, EVENT_ALL, bounds_x_eq_y_plus_c_handler);
    register_solver(EQ_MUL_C, 3, EVENT_ALL, bounds_x_eq_y_mul_c_handler);
    register_solver(EQ_PLUS, 3, EVENT_ALL, bounds_x_eq_y_plus_z_handler);
    register_solver(EQ_MUL, 3, EVENT_ALL, bounds_x_eq_y_mul_z_handler);

    bounds_t bs = {-inf, inf};
    struct boundsinfo_s template = {bs, NULL, NULL, NULL};
    bounds_offset = alloc_extra(WORD_SIZEOF(struct boundsinfo_s),
        (word_t *)&template);
}

/*
 * Get the LB/UB reason.
 */
static inline void bounds_get_lb_reason(reason_t reason, var_t x)
{
    boundsinfo_t info = (boundsinfo_t)extra(x, bounds_offset);
    cons_t c = info->lb;
    if (c == NULL)
        return;
    var_t y = var(c->args[X]);
    if (!match_vars(reason, x, y))
        panic("bounds variables do not match");
    antecedent(reason, c->b);
}
static inline void bounds_get_ub_reason(reason_t reason, var_t x)
{
    boundsinfo_t info = (boundsinfo_t)extra(x, bounds_offset);
    cons_t c = info->ub;
    if (c == NULL)
        return;
    var_t y = var(c->args[X]);
    if (!match_vars(reason, x, y))
        panic("bounds variables do not match");
    antecedent(reason, -c->b);
}

/*
 * Get the bounds.
 */
static bounds_t bounds_get(reason_t reason_lb, reason_t reason_ub, var_t x)
{
    boundsinfo_t info = (boundsinfo_t)extra(x, bounds_offset);
    if (reason_lb != NULL)
        bounds_get_lb_reason(reason_lb, x);
    if (reason_ub != NULL)
        bounds_get_ub_reason(reason_ub, x);
    return info->bs;
}

/*
 * Get the LB/UB.
 */
static inline num_t bounds_get_lb(reason_t reason, var_t x)
{
    boundsinfo_t info = (boundsinfo_t)extra(x, bounds_offset);
    if (reason != NULL)
        bounds_get_lb_reason(reason, x);
    return info->bs[L];
}
static inline num_t bounds_get_ub(reason_t reason, var_t x)
{
    boundsinfo_t info = (boundsinfo_t)extra(x, bounds_offset);
    if (reason != NULL)
        bounds_get_ub_reason(reason, x);
    return info->bs[U];
}

/*
 * Get the LB/UB constraints.
 */
static inline cons_t bounds_get_lb_cons(var_t x)
{
    boundsinfo_t info = (boundsinfo_t)extra(x, bounds_offset);
    return info->lb;
}
static inline cons_t bounds_get_ub_cons(var_t x)
{
    boundsinfo_t info = (boundsinfo_t)extra(x, bounds_offset);
    return info->ub;
}

/*
 * Set the LB/UB constraints.
 */
static void bounds_set_lb_cons(var_t x, cons_t c, num_t lb)
{
    boundsinfo_t info = (boundsinfo_t)extra(x, bounds_offset);
    num_t *bs = (num_t *)&info->bs;
    trail(&bs[L]);
    info->bs[L] = lb;
    trail(&info->lb);
    info->lb = c;
    event(info->delays);

    debug("!gBOUNDS!d %s::%s..%s", show_var(x), show_num(info->bs[L]),
        show_num(info->bs[U]));
}
static void bounds_set_ub_cons(var_t x, cons_t c, num_t ub)
{
    boundsinfo_t info = (boundsinfo_t)extra(x, bounds_offset);
    num_t *bs = (num_t *)&info->bs;
    trail(&bs[U]);
    info->bs[U] = ub;
    trail(&info->ub);
    info->ub = c;
    event(info->delays);

    debug("!gBOUNDS!d %s::%s..%s", show_var(x), show_num(info->bs[L]),
        show_num(info->bs[U]));
}

/*
 * Set the LB/UB
 */
static bounds_t bounds_set_lb(reason_t reason, var_t x, num_t lb)
{
    debug("!rSET!d LB(%s) = %s", show_var(x), show_num(lb));
    boundsinfo_t info = (boundsinfo_t)extra(x, bounds_offset);
    if (lb <= info->bs[L])
        return info->bs;
    size_t sp = save(reason);
    cons_t c = make_cons(reason, LB, term_var(x), term_int(lb));
    consequent(reason, c->b);
    propagate(reason);

    if (info->bs[U] < lb)
    {
        reason_t reason = make_reason();
        bounds_get_ub_reason(reason, x);
        consequent(reason, -c->b);
        fail(reason);
    }

    restore(reason, sp);
    bounds_set_lb_cons(x, c, lb);
    return info->bs;
}
static bounds_t bounds_set_ub(reason_t reason, var_t x, num_t ub)
{
    debug("!rSET!d UB(%s) = %s", show_var(x), show_num(ub));
    boundsinfo_t info = (boundsinfo_t)extra(x, bounds_offset);
    if (ub >= info->bs[U])
        return info->bs;
    size_t sp = save(reason);
    cons_t c = make_cons(reason, LB, term_var(x), term_int(ub+1));
    consequent(reason, -c->b);
    propagate(reason);

    if (ub < info->bs[L])
    {
        reason_t reason = make_reason();
        bounds_get_lb_reason(reason, x);
        consequent(reason, c->b);
        fail(reason);
    }

    restore(reason, sp);
    bounds_set_ub_cons(x, c, ub);
    return info->bs;
}

/*
 * Delay a propagator.
 */
static void bounds_delay(prop_t prop)
{
    if ((bool)prop->state)
        return;

    cons_t c = constraint(prop);
    sym_t sym = c->sym;
    for (size_t i = 0; i < sym->arity; i++)
    {
        term_t arg = c->args[i];
        if (type(arg) != VAR)
            continue;
        var_t x = var(arg);
        boundsinfo_t info = (boundsinfo_t)extra(x, bounds_offset);
        info->delays = delay(prop, info->delays);
    }

    prop->state = (word_t)true;
}

/*
 * Bounds LB handler.
 */
static void bounds_lb_handler(prop_t prop)
{
    cons_t c = constraint(prop);
    var_t x = var(c->args[X]);
    num_t lb = num(c->args[Y]);
    switch (decision(c->b))
    {
        case TRUE:
        {
            reason_t reason = make_reason();
            num_t ub = bounds_get_ub(reason, x);
            if (ub < lb)
            {
                consequent(reason, -c->b);
                fail(reason);
            }
            num_t lb1 = bounds_get_lb(NULL, x);
            if (lb1 > lb)
            {
                purge(c);
                return;
            }
            if (lb1 < lb)
            {
                cons_t c1 = bounds_get_lb_cons(x);
                if (c1 != NULL)
                    purge(c1);
                bounds_set_lb_cons(x, c, lb);
                return;
            }
            return;
        }
        case FALSE:
        {
            num_t ub = lb-1;
            reason_t reason = make_reason();
            lb = bounds_get_lb(reason, x);
            if (ub < lb)
            {
                consequent(reason, c->b);
                fail(reason);
            }
            num_t ub1 = bounds_get_ub(NULL, x);
            if (ub1 < ub)
            {
                purge(c);
                return;
            }
            if (ub1 > ub)
            {
                cons_t c1 = bounds_get_ub_cons(x);
                if (c1 != NULL)
                    purge(c1);
                bounds_set_ub_cons(x, c, ub);
                return;
            }
            return;
        }
        default:
            return;
    }
}

/*
 * x > c handler.
 */
static void bounds_x_gt_c_handler(prop_t prop)
{
    bounds_delay(prop);
    cons_t c = constraint(prop);
    var_t x = var(c->args[X]);
    num_t k = num(c->args[Y]);
    switch (decision(c->b))
    {
        case TRUE:
        {
            reason_t reason = make_reason(c->b);
            bounds_set_lb(reason, x, k+1);
            return;
        }
        case FALSE:
        {
            reason_t reason = make_reason(-c->b);
            bounds_set_ub(reason, x, k);
            return;
        }
        default:
            return;
    }
}

/*
 * x > y handler.
 */
static void bounds_x_gt_y_handler(prop_t prop)
{
    bounds_delay(prop);
    cons_t c = constraint(prop);
    var_t x = var(c->args[X]);
    var_t y = var(c->args[Y]);
    switch (decision(c->b))
    {
        case TRUE:
        {
            reason_t reason_ubx = make_reason(c->b);
            reason_t reason_lby = make_reason(c->b);
            bounds_t bx = bounds_get(NULL, reason_ubx, x);
            bounds_t by = bounds_get(reason_lby, NULL, y);
            num_t nlbx = by[L]+1, nuby = bx[U]-1;
            if (nlbx > bx[L])
                bounds_set_lb(reason_lby, x, nlbx);
            if (nuby < by[U])
                bounds_set_ub(reason_ubx, y, nuby);
            return;
        }
        case FALSE:
        {
            reason_t reason_lbx = make_reason(-c->b);
            reason_t reason_uby = make_reason(-c->b);
            bounds_t bx = bounds_get(reason_lbx, NULL, x);
            bounds_t by = bounds_get(NULL, reason_uby, y);
            num_t nubx = by[U], nlby = bx[L];
            if (nubx < bx[U])
                bounds_set_ub(reason_uby, x, nubx);
            if (nlby > by[L])
                bounds_set_lb(reason_lbx, y, nlby);
            return;
        }
        default:
            return;
    }
}

/*
 * x = c handler.
 */
static void bounds_x_eq_c_handler(prop_t prop)
{
    bounds_delay(prop);
    cons_t c = constraint(prop);
    var_t x = var(c->args[X]);
    num_t k = num(c->args[Y]);
    switch (decision(c->b))
    {
        case TRUE:
        {
            reason_t reason = make_reason(c->b);
            bounds_set_lb(reason, x, k);
            bounds_set_ub(reason, x, k);
            return;
        }
        case FALSE:
        {
            reason_t reason = make_reason(-c->b);
            num_t lbx = bounds_get_lb(reason, x);
            if (k == lbx)
                bounds_set_lb(reason, x, k+1);
            restore(reason, 1);
            num_t ubx = bounds_get_ub(reason, x);
            if (k == ubx)
                bounds_set_ub(reason, x, k-1);
            return;
        }
        default:
            return;
    }
}

/*
 * x = y handler.
 */
static void bounds_x_eq_y_handler(prop_t prop)
{
    bounds_delay(prop);
    cons_t c = constraint(prop);
    var_t x = var(c->args[X]);
    var_t y = var(c->args[Y]);
    reason_t reason_lbx = make_reason();
    reason_t reason_ubx = make_reason();
    reason_t reason_lby = make_reason();
    reason_t reason_uby = make_reason();
    bounds_t bx = bounds_get(reason_lbx, reason_ubx, x);
    bounds_t by = bounds_get(reason_lby, reason_uby, y);
    switch (decision(c->b))
    {
        case TRUE:
        {
            if (by[L] > bx[L])
            {
                antecedent(reason_lby, c->b);
                bounds_set_lb(reason_lby, x, by[L]);
            }
            if (by[U] < bx[U])
            {
                antecedent(reason_uby, c->b);
                bounds_set_ub(reason_uby, x, by[U]);
            }
            if (bx[L] > by[L])
            {
                antecedent(reason_lbx, c->b);
                bounds_set_lb(reason_lbx, y, bx[L]);
            }
            if (bx[U] < by[U])
            {
                antecedent(reason_ubx, c->b);
                bounds_set_ub(reason_ubx, y, bx[U]);
            }
            return;
        }
        case FALSE:
        {
            if (bx[L] == bx[U])
            {
                num_t k = bx[L];
                if (by[L] == k)
                {
                    reason_t reason = make_reason(-c->b);
                    append(reason, reason_lbx);
                    append(reason, reason_ubx);
                    append(reason, reason_lby);
                    bounds_set_lb(reason, y, k+1);
                }
                if (by[U] == k)
                {
                    reason_t reason = make_reason(-c->b);
                    append(reason, reason_lbx);
                    append(reason, reason_ubx);
                    append(reason, reason_uby);
                    bounds_set_ub(reason, y, k-1);
                }
            }
            if (by[L] == by[U])
            {
                num_t k = by[L];
                if (bx[L] == k)
                {
                    reason_t reason = make_reason(-c->b);
                    append(reason, reason_lby);
                    append(reason, reason_uby);
                    append(reason, reason_lbx);
                    bounds_set_lb(reason, x, k+1);
                }
                if (bx[U] == k)
                {
                    reason_t reason = make_reason(-c->b);
                    append(reason, reason_lby);
                    append(reason, reason_uby);
                    append(reason, reason_ubx);
                    bounds_set_ub(reason, x, k-1);
                }
            }
            return;
        }
        default:
            return;
    }
}

/*
 * x = y + c.
 */
static void bounds_x_eq_y_plus_c_handler(prop_t prop)
{
    bounds_delay(prop);
    cons_t c = constraint(prop);
    var_t x = var(c->args[X]);
    var_t y = var(c->args[Y]);
    num_t k = num(c->args[Z]);
    bounds_t bc = {k, k};
    reason_t reason_lbx = make_reason();
    reason_t reason_ubx = make_reason();
    reason_t reason_lby = make_reason();
    reason_t reason_uby = make_reason();
    bounds_t bx = bounds_get(reason_lbx, reason_ubx, x);
    bounds_t by = bounds_get(reason_lby, reason_uby, y);
    switch (decision(c->b))
    {
        case TRUE:
        {
            bounds_t bz = by + bc;
            if (bz[L] > bx[L])
            {
                antecedent(reason_lby, c->b);
                bounds_set_lb(reason_lby, x, bz[L]);
            }
            if (bz[U] < bx[U])
            {
                antecedent(reason_uby, c->b);
                bounds_set_ub(reason_uby, x, bz[U]);
            }
            bz = bx - bc;
            if (bz[L] > by[L])
            {
                antecedent(reason_lbx, c->b);
                bounds_set_lb(reason_lbx, y, bz[L]);
            }
            if (bz[U] < by[U])
            {
                antecedent(reason_ubx, c->b);
                bounds_set_ub(reason_ubx, y, bz[U]);
            }
            return;
        }
        case FALSE:
        {
            if (bx[L] == bx[U])
            {
                num_t z = bx[L] - k;
                if (by[L] == z)
                {
                    reason_t reason = make_reason(-c->b);
                    append(reason, reason_lbx);
                    append(reason, reason_ubx);
                    append(reason, reason_lby);
                    bounds_set_lb(reason, y, z+1);
                }
                if (by[U] == z)
                {
                    reason_t reason = make_reason(-c->b);
                    append(reason, reason_lbx);
                    append(reason, reason_ubx);
                    append(reason, reason_uby);
                    bounds_set_ub(reason, y, z-1);
                }
            }
            if (by[L] == by[U])
            {
                num_t z = by[L] + k;
                if (bx[L] == z)
                {
                    reason_t reason = make_reason(-c->b);
                    append(reason, reason_lby);
                    append(reason, reason_uby);
                    append(reason, reason_lbx);
                    bounds_set_lb(reason, x, z+1);
                }
                if (bx[U] == z)
                {
                    reason_t reason = make_reason(-c->b);
                    append(reason, reason_lby);
                    append(reason, reason_uby);
                    append(reason, reason_ubx);
                    bounds_set_ub(reason, x, z-1);
                }
            }
            return;
        }
        default:
            return;
    }
}

/*
 * x = y * c.
 */
static inline bounds_t bounds_mul_c(bounds_t bx, num_t c)
{
    if (c < 0)
        bx = _mm_shuffle_pd(bx, bx, 0x01);
    bounds_t bc = {c, c};
    bx = bx * bc;
    return bx;
}
static inline bounds_t bounds_div_c(bounds_t bx, num_t c)
{
    if (c < 0)
        bx = _mm_shuffle_pd(bx, bx, 0x01);
    else if (c == 0)
    {
        bounds_t bi = {-inf, inf};
        return bi;
    }
    bounds_t bc = {c, c};
    bx = bx / bc;
    bounds_t bz = {ceil(bx[L]), floor(bx[U])};
    return bz;
}
static void bounds_x_eq_y_mul_c_handler(prop_t prop)
{
    bounds_delay(prop);
    cons_t c = constraint(prop);
    var_t x = var(c->args[X]);
    var_t y = var(c->args[Y]);
    num_t k = num(c->args[Z]);
    reason_t reason_lbx = make_reason();
    reason_t reason_ubx = make_reason();
    reason_t reason_lby = make_reason();
    reason_t reason_uby = make_reason();
    bounds_t bx = bounds_get(reason_lbx, reason_ubx, x);
    bounds_t by = bounds_get(reason_lby, reason_uby, y);
    switch (decision(c->b))
    {
        case TRUE:
        {
            bounds_t bz = bounds_mul_c(by, k);
            if (k < 0)
            {
                reason_t tmp;
                tmp = reason_lbx;
                reason_lbx = reason_ubx;
                reason_ubx = tmp;
                tmp = reason_lby;
                reason_lby = reason_uby;
                reason_uby = tmp;
            }
            if (bz[L] > bx[L])
            {
                antecedent(reason_lby, c->b);
                bounds_set_lb(reason_lby, x, bz[L]);
            }
            if (bz[U] < bx[U])
            {
                antecedent(reason_uby, c->b);
                bounds_set_ub(reason_uby, x, bz[U]);
            }
            bz = bounds_div_c(bx, k);
            bool change = false;
            if (bz[L] > by[L])
            {
                change = true;
                antecedent(reason_lbx, c->b);
                bounds_set_lb(reason_lbx, y, bz[L]);
            }
            if (bz[U] < by[U])
            {
                change = true;
                antecedent(reason_ubx, c->b);
                bounds_set_ub(reason_ubx, y, bz[U]);
            }
            if (change)
                schedule(prop);
            return;
        }
        case FALSE:
        {
            if (bx[L] == bx[U])
            {
                num_t z = bx[L] / k;
                if (z == floor(z))
                {
                    if (by[L] == z)
                    {
                        reason_t reason = make_reason(-c->b);
                        append(reason, reason_lbx);
                        append(reason, reason_ubx);
                        append(reason, reason_lby);
                        bounds_set_lb(reason, y, z+1);
                    }
                    if (by[U] == z)
                    {
                        reason_t reason = make_reason(-c->b);
                        append(reason, reason_lbx);
                        append(reason, reason_ubx);
                        append(reason, reason_uby);
                        bounds_set_ub(reason, y, z-1);
                    }
                }
            }
            if (by[L] == by[U])
            {
                num_t z = by[L] * k;
                if (bx[L] == z)
                {
                    reason_t reason = make_reason(-c->b);
                    append(reason, reason_lby);
                    append(reason, reason_uby);
                    append(reason, reason_lbx);
                    bounds_set_lb(reason, x, z+1);
                }
                if (bx[U] == z)
                {
                    reason_t reason = make_reason(-c->b);
                    append(reason, reason_lby);
                    append(reason, reason_uby);
                    append(reason, reason_ubx);
                    bounds_set_ub(reason, x, z-1);
                }
            }
            return;
        }
        default:
            return;
    }
}

/*
 * x = y + z
 */
static inline bounds_t bounds_sub(bounds_t bx, bounds_t by)
{
    by = _mm_shuffle_pd(by, by, 0x01);
    bx = bx - by;
    return bx;
}
static inline bool bounds_eq(bounds_t bx, bounds_t by)
{
    bounds_t bz = _mm_cmpeq_pd(bx, by);
    int msk = _mm_movemask_pd(bz);
    return (msk == 3);
}
static void bounds_x_eq_y_plus_z_handler(prop_t prop)
{
    bounds_delay(prop);
    cons_t c = constraint(prop);
    var_t x = var(c->args[X]);
    var_t y = var(c->args[Y]);
    var_t z = var(c->args[Z]);
    reason_t reason_lbx = make_reason();
    reason_t reason_ubx = make_reason();
    reason_t reason_lby = make_reason();
    reason_t reason_uby = make_reason();
    reason_t reason_lbz = make_reason();
    reason_t reason_ubz = make_reason();
    bounds_t bx = bounds_get(reason_lbx, reason_ubx, x);
    bounds_t by = bounds_get(reason_lby, reason_uby, y);
    bounds_t bz = bounds_get(reason_lbz, reason_ubz, z);
    bounds_t bx1 = bx, by1 = by, bz1 = bz;
    switch (decision(c->b))
    {
        case TRUE:
        {
            bounds_t bxx = by + bz;
            reason_t reason = make_reason(c->b);
            if (bxx[L] > bx[L])
            {
                append(reason, reason_lby);
                append(reason, reason_lbz);
                bx1 = bounds_set_lb(reason, x, bxx[L]);
            }
            if (bxx[U] < bx[U])
            {
                restore(reason, 1);
                append(reason, reason_uby);
                append(reason, reason_ubz);
                bx1 = bounds_set_ub(reason, x, bxx[U]);
            }
            bounds_t byy = bounds_sub(bx, bz);
            if (byy[L] > by[L])
            {
                restore(reason, 1);
                append(reason, reason_lbx);
                append(reason, reason_ubz);
                by1 = bounds_set_lb(reason, y, byy[L]);
            }
            if (byy[U] < by[U])
            {
                restore(reason, 1);
                append(reason, reason_ubx);
                append(reason, reason_lbz);
                by1 = bounds_set_ub(reason, y, byy[U]);
            }
            bounds_t bzz = bounds_sub(bx, by);
            if (bzz[L] > bz[L])
            {
                restore(reason, 1);
                append(reason, reason_lbx);
                append(reason, reason_uby);
                bz1 = bounds_set_lb(reason, z, bzz[L]);
            }
            if (bzz[U] > bz[U])
            {
                restore(reason, 1);
                append(reason, reason_ubx);
                append(reason, reason_lby);
                bz1 = bounds_set_ub(reason, z, bzz[U]);
            }
            if (!bounds_eq(bx, bx1) || !bounds_eq(by, by1) ||
                    !bounds_eq(bz, bz1))
                schedule(prop);
            return;
        }
        case FALSE:
            return;
        default:
            return;
    }
}

static inline bounds_t bounds_mul(bounds_t bx, bounds_t by)
{
    bounds_t bxy = _mm_mul_pd(bx, by);
    bounds_t b1 = _mm_shuffle_pd(by, by, 0x01);
    bounds_t byx = _mm_mul_pd(bx, b1);
    bounds_t bmin = _mm_min_pd(bxy, byx);
    bounds_t bmax = _mm_max_pd(bxy, byx);
    bounds_t bmin1 = _mm_shuffle_pd(bmin, bmin, 0x01);
    bounds_t bmax1 = _mm_shuffle_pd(bmax, bmax, 0x01);
    bmin = _mm_min_pd(bmin, bmin1);
    bmax = _mm_max_pd(bmax, bmax1);
    bounds_t bz = {bmin[0], bmax[0]};
    return bz;
}
static bounds_t bounds_div(bounds_t bx, bounds_t by)
{
    num_t lby = by[L], uby = by[U];
    if (lby <= 0 && uby >= 0)
    {
        bounds_t bi = {-inf, inf};
        return bi;
    }
    if (uby < 0)
    {
        bx = _mm_shuffle_pd(bx, bx, 0x01);
        bounds_t bz = {0, 0};
        bx = bz - bx;
        num_t tmp = lby;
        lby = -uby;
        uby = -tmp;
    }
    num_t lbx = bx[L], ubx = bx[U];

    if (lbx > 0)
    {
        bounds_t bz = {ceil(lbx/uby), floor(ubx/lby)};
        return bz;
    }
    else if (ubx < 0)
    {
        bounds_t bz = {ceil(lbx/lby), floor(ubx/uby)};
        return bz;
    }
    else
    {
        bounds_t bz = {ceil(lbx/lby), floor(ubx/lby)};
        return bz;
    }
}
static void bounds_x_eq_y_mul_z_handler(prop_t prop)
{
    bounds_delay(prop);
    cons_t c = constraint(prop);
    var_t x = var(c->args[X]);
    var_t y = var(c->args[Y]);
    var_t z = var(c->args[Z]);
    reason_t reason_lbx = make_reason();
    reason_t reason_ubx = make_reason();
    reason_t reason_lby = make_reason();
    reason_t reason_uby = make_reason();
    reason_t reason_lbz = make_reason();
    reason_t reason_ubz = make_reason();
    bounds_t bx = bounds_get(reason_lbx, reason_ubx, x);
    bounds_t by = bounds_get(reason_lby, reason_uby, y);
    bounds_t bz = bounds_get(reason_lbz, reason_ubz, z);
    bounds_t bx1 = bx, by1 = by, bz1 = bz;
    switch (decision(c->b))
    {
        case TRUE:
        {
            bounds_t bxx = bounds_mul(by, bz);
            reason_t reason = make_reason(c->b);
            if (bxx[L] > bx[L])
            {
                append(reason, reason_lby);
                append(reason, reason_lbz);
                bx1 = bounds_set_lb(reason, x, bxx[L]);
            }
            if (bxx[U] < bx[U])
            {
                restore(reason, 1);
                append(reason, reason_uby);
                append(reason, reason_ubz);
                bx1 = bounds_set_ub(reason, x, bxx[U]);
            }
            bounds_t byy = bounds_div(bx, bz);
            if (byy[L] > by[L])
            {
                restore(reason, 1);
                append(reason, reason_lbx);
                append(reason, reason_ubz);
                by1 = bounds_set_lb(reason, y, byy[L]);
            }
            if (byy[U] < by[U])
            {
                restore(reason, 1);
                append(reason, reason_ubx);
                append(reason, reason_lbz);
                by1 = bounds_set_ub(reason, y, byy[U]);
            }
            bounds_t bzz = bounds_div(bx, by);
            if (bzz[L] > bz[L])
            {
                restore(reason, 1);
                append(reason, reason_lbx);
                append(reason, reason_uby);
                bz1 = bounds_set_lb(reason, z, bzz[L]);
            }
            if (bzz[U] > bz[U])
            {
                restore(reason, 1);
                append(reason, reason_ubx);
                append(reason, reason_lby);
                bz1 = bounds_set_ub(reason, z, bzz[U]);
            }
            if (!bounds_eq(bx, bx1) || !bounds_eq(by, by1) ||
                    !bounds_eq(bz, bz1))
                schedule(prop);
            return;
        }
        case FALSE:
            return;
        default:
            return;
    }
}

