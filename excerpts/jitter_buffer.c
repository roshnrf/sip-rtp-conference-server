/*
 * EXCERPT from sipconf. This is not a complete program and will not build on its own.
 * Full source is private.
 */

static void jb_push(participant_t *p, const int16_t *frame)
{
    if (p->jb_count == JB_FRAMES) {          /* full: drop the oldest frame */
        p->jb_head = (p->jb_head + 1) % JB_FRAMES;
        p->jb_count--;
        p->jb_overflow++;
    }
    int tail = (p->jb_head + p->jb_count) % JB_FRAMES;
    memcpy(p->jb[tail], frame, sizeof(p->jb[tail]));
    p->jb_count++;
}

/* Returns 1 and fills out if a frame is available, 0 for silence. */
static int jb_pop(participant_t *p, int16_t *out)
{
    if (!p->jb_primed) {
        if (p->jb_count < JB_PRIME)
            return 0;
        p->jb_primed = 1;
    }
    if (p->jb_count == 0) {
        p->jb_underrun++;
        p->jb_primed = 0;                    /* re-prime before resuming */
        return 0;
    }
    memcpy(out, p->jb[p->jb_head], sizeof(p->jb[0]));
    p->jb_head = (p->jb_head + 1) % JB_FRAMES;
    p->jb_count--;
    return 1;
}

