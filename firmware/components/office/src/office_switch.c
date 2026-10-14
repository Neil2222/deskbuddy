#include "office_switch.h"

void office_switch_init(office_switch_t *s)
{
    *s = (office_switch_t){0};
}

void office_switch_request(office_switch_t *s, bool office)
{
    s->want_office = office;
}

bool office_switch_busy(const office_switch_t *s)
{
    return s->stage != OFFICE_SETTLED;
}

office_step_t office_switch_step(office_switch_t *s)
{
    office_step_t out = {.next_ms = UINT32_MAX};
    if (s->stage == OFFICE_SETTLED && s->want_office != s->office) {
        // A request arriving during the redraw waits its turn.
        s->office = !s->office;
        s->stage = OFFICE_REDRAW;
        s->presents = 0;
        out.swap = true;
    }
    if (s->stage == OFFICE_REDRAW) {
        if (s->presents < 2) {
            out.redraw_all = s->presents == 0;
            out.redraw_kick = s->presents == 1;
            out.next_ms = 0;
            return out;
        }
        s->stage = OFFICE_SETTLED;
        out.redrawn = true;
        out.next_ms = s->want_office != s->office ? 0 : UINT32_MAX;
    }
    return out;
}

void office_switch_presented(office_switch_t *s)
{
    if (s->stage == OFFICE_REDRAW && s->presents < 2) {
        s->presents++;
    }
}
