/*
 * EXCERPT from sipconf. This is not a complete program and will not build on its own.
 * Full source is private.
 */

void conf_tick(conference_t *c)
{
    static int16_t in[MAX_PARTICIPANTS][FRAME_SAMPLES];
    int32_t  sum[FRAME_SAMPLES];
    int      has[MAX_PARTICIPANTS];
    uint8_t  payload[FRAME_SAMPLES];
    uint8_t  pkt[12 + FRAME_SAMPLES];
    uint64_t t0 = now_ns();

    memset(sum, 0, sizeof(sum));

    /* 1. Pull one frame per participant and build the full mix. */
    for (int i = 0; i < MAX_PARTICIPANTS; i++) {
        participant_t *p = &c->p[i];
        has[i] = 0;
        if (!p->active)
            continue;
        has[i] = jb_pop(p, in[i]);
        if (has[i])
            for (int s = 0; s < FRAME_SAMPLES; s++)
                sum[s] += in[i][s];
        p->jb_depth_sum += (uint64_t)p->jb_count;
        p->ticks++;
    }

    /* 2. Each participant hears everyone except themselves. */
    for (int i = 0; i < MAX_PARTICIPANTS; i++) {
        participant_t *p = &c->p[i];
        if (!p->active || !p->have_peer)
            continue;

        /* Mix-minus for this listener. */
        int32_t mix[FRAME_SAMPLES];
        int32_t peak = 0;
        for (int s = 0; s < FRAME_SAMPLES; s++) {
            mix[s] = sum[s] - (has[i] ? in[i][s] : 0);
            int32_t a = mix[s] < 0 ? -mix[s] : mix[s];
            if (a > peak)
                peak = a;
        }

        /* Limiter: when several people talk at once the sum can exceed
         * 16 bits. Hard clipping would distort it (and leak intermodulation
         * products into every stream), so scale the frame instead.
         * Instant attack, ~0.5 s release. */
        float target = peak > LIMIT_PEAK ? (float)LIMIT_PEAK / (float)peak : 1.0f;
        if (target < p->out_gain)
            p->out_gain = target;
        else
            p->out_gain += (target - p->out_gain) * LIMIT_RELEASE;
        if (p->out_gain < 0.999f)
            p->limited_frames++;

        for (int s = 0; s < FRAME_SAMPLES; s++)
            payload[s] = g711_ulaw_encode(clamp16((int32_t)((float)mix[s] * p->out_gain)));

        int len = rtp_build(pkt, sizeof(pkt), RTP_PT_PCMU, p->tx_pkts == 0,
                            p->tx_seq, p->tx_ts, p->tx_ssrc,
                            payload, FRAME_SAMPLES);
        if (sendto(p->rtp_fd, pkt, (size_t)len, 0,
                   (struct sockaddr *)&p->rtp_peer, sizeof(p->rtp_peer)) == len)
            p->tx_pkts++;
        p->tx_seq++;
        p->tx_ts += FRAME_SAMPLES;
    }

    uint64_t dt = now_ns() - t0;
    c->ticks++;
    c->tick_ns_sum += dt;
    if (dt > c->tick_ns_max)
        c->tick_ns_max = dt;
}
