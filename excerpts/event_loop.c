/*
 * EXCERPT from sipconf. This is not a complete program and will not build on its own.
 * Full source is private.
 *
 * One thread, one epoll instance. Every input is a file descriptor:
 * SIP socket, one RTP socket per caller, a 20 ms timerfd (the media
 * clock), and a signalfd for clean shutdown.
 */

enum { EV_SIP = 1, EV_TIMER, EV_SIGNAL, EV_RTP };
#define EV_KEY(type, idx) (((uint64_t)(type) << 32) | (uint32_t)(idx))
#define EV_TYPE(key)      ((int)((key) >> 32))
#define EV_IDX(key)       ((int)((key) & 0xFFFFFFFFu))

/* The media clock: fires every FRAME_MS on CLOCK_MONOTONIC. */
static int open_frame_timer(void)
{
    struct itimerspec its = {
        .it_interval = { 0, FRAME_MS * 1000000L },
        .it_value    = { 0, FRAME_MS * 1000000L },
    };
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    timerfd_settime(fd, 0, &its, NULL);
    return fd;
}

static void handle_timer(void)
{
    uint64_t exp;

    if (read(g.timer_fd, &exp, sizeof(exp)) != sizeof(exp))
        return;
    if (exp > 1)
        g.conf.late_ticks += exp - 1;   /* we fell behind: count it */
    if (exp > 5)
        exp = 5;                        /* catch up, but never burst forever */
    while (exp--)
        conf_tick(&g.conf);             /* mix + send one 20 ms frame */
}

    /* ... in main(), after creating the sockets ... */

    for (;;) {
        struct epoll_event evs[MAX_PARTICIPANTS + 3];
        int n = epoll_wait(g.ep, evs, MAX_PARTICIPANTS + 3, -1);
        if (n < 0 && errno == EINTR)
            continue;

        for (int i = 0; i < n; i++) {
            uint64_t key = evs[i].data.u64;
            switch (EV_TYPE(key)) {
            case EV_SIP:    handle_sip();   break;   /* INVITE/ACK/BYE/... */
            case EV_TIMER:  handle_timer(); break;   /* 20 ms media tick   */
            case EV_RTP:    conf_rtp_input(&g.conf.p[EV_IDX(key)]); break;
            case EV_SIGNAL: shutdown_cleanly(); return 0;
            }
        }
    }
