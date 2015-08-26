

struct itr_s
{
    reason_t reason;
    cons_t *start;
    cons_t *end;
};
typedef struct itr_s *itr_t;

static inline void next(itr_t i)
{
    i->start++;
}
static inline bool get(itr_t i, cons_t *c_ptr)
{
    while (i->start < i->end)
    {
        cons_t c = *i->start;
        if (!isdead(c))
        {
            *c_ptr = c;
            return true;
        }
        i->start++;
    }
    return false;
}

cons_t c;
for (itr_t i = lookup(IN, h, p, _); get(i, &c); next(i))
{




}




