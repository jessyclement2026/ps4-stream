#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <ps4/kernel.h>

#include "common/diagnostics/problem_log.h"
#include "platform/ps4/capture/capture.h"
#include "platform/ps4/capture/request.h"
#include "platform/ps4/system/firmware.h"

#define STREAM_PORT 5900
#define CTRL_PORT   5901
#ifndef HEIGHT
#define HEIGHT      720
#endif

int sceKernelDlsym(int handle, const char *symbol, void **addrp);

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

static void ps4_notify(const char *msg) {
    static int (*fn_notify)(int, void *, size_t, int) = NULL;
    if (!fn_notify) {
        sceKernelDlsym(0x2001, "sceKernelSendNotificationRequest", (void **)&fn_notify);
    }
    if (!fn_notify) return;

    notify_request_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.message, msg, sizeof(req.message) - 1);
    fn_notify(0, &req, sizeof(req), 0);
}

static uint64_t milliseconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int send_all(int sock, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        size_t chunk = len > 32768 ? 32768 : len;
        ssize_t n = send(sock, p, chunk, 0);
        if (n < 0) {
            int e = errno;
            if (e == EAGAIN || e == EWOULDBLOCK || e == EINTR || e == 35 || e == 11 || e == 55) {
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(sock, &wfds);
                struct timeval tv = {2, 0};
                select(sock + 1, NULL, &wfds, NULL, &tv);
                continue;
            }
            return -1;
        }
        if (n == 0) return -1;
        p += n;
        len -= n;
    }
    return 0;
}

static uint32_t mpegts_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i] << 24;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80000000u) ? ((crc << 1) ^ 0x04C11DB7u) : (crc << 1);
        }
    }
    return crc;
}

static int ts_send_pat_pmt(int sock, uint8_t *cc_pat, uint8_t *cc_pmt) {
    uint8_t pkt[188];

    memset(pkt, 0xFF, 188);
    pkt[0] = 0x47; pkt[1] = 0x40; pkt[2] = 0x00;
    pkt[3] = 0x10 | ((*cc_pat)++ & 0x0F);
    pkt[4] = 0x00;
    uint8_t pat[16] = {
        0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00,
        0x00, 0x01, 0xF0, 0x00, 0, 0, 0, 0
    };
    uint32_t c1 = mpegts_crc32(pat, 12);
    pat[12] = (c1 >> 24) & 0xFF; pat[13] = (c1 >> 16) & 0xFF;
    pat[14] = (c1 >> 8) & 0xFF;  pat[15] = c1 & 0xFF;
    memcpy(pkt + 5, pat, 16);
    if (send_all(sock, pkt, 188) < 0) return -1;

    memset(pkt, 0xFF, 188);
    pkt[0] = 0x47; pkt[1] = 0x50; pkt[2] = 0x00;
    pkt[3] = 0x10 | ((*cc_pmt)++ & 0x0F);
    pkt[4] = 0x00;
    uint8_t pmt[21] = {
        0x02, 0xB0, 0x12, 0x00, 0x01, 0xC1, 0x00, 0x00,
        0xE1, 0x00, 0xF0, 0x00,
        0x1B, 0xE1, 0x00, 0xF0, 0x00,
        0, 0, 0, 0
    };
    uint32_t c2 = mpegts_crc32(pmt, 17);
    pmt[17] = (c2 >> 24) & 0xFF; pmt[18] = (c2 >> 16) & 0xFF;
    pmt[19] = (c2 >> 8) & 0xFF;  pmt[20] = c2 & 0xFF;
    memcpy(pkt + 5, pmt, 21);
    return send_all(sock, pkt, 188);
}
static int ts_send_video_frame(int sock, uint8_t *cc_vid, uint64_t pts90k,
                               int is_idr, const uint8_t *sps, int sps_len,
                               const uint8_t *pps, int pps_len,
                               const uint8_t *nal, int nal_len,
                               uint8_t *ts_buf, size_t ts_cap) {
    static const uint8_t sc[4] = {0, 0, 0, 1};
    static const uint8_t aud[6] = {0, 0, 0, 1, 0x09, 0xF0};

    uint8_t pes[14] = {
        0x00, 0x00, 0x01, 0xE0, 0x00, 0x00,
        0x80, 0x80, 0x05,
        (uint8_t)(0x21 | (((pts90k >> 30) & 0x07) << 1)),
        (uint8_t)((pts90k >> 22) & 0xFF),
        (uint8_t)(0x01 | (((pts90k >> 15) & 0x7F) << 1)),
        (uint8_t)((pts90k >> 7) & 0xFF),
        (uint8_t)(0x01 | ((pts90k & 0x7F) << 1))
    };

    int nal_has_sc = (nal_len >= 4 && nal[0] == 0 && nal[1] == 0 &&
                     (nal[2] == 1 || (nal[2] == 0 && nal[3] == 1)));
    size_t es_len = sizeof(pes) + sizeof(aud) + (nal_has_sc ? 0 : 4) + (size_t)nal_len;
    if (is_idr && sps_len > 0 && pps_len > 0) {
        int sps_sc = (sps_len >= 4 && sps[0] == 0 && sps[1] == 0 && sps[2] == 0 && sps[3] == 1);
        int pps_sc = (pps_len >= 4 && pps[0] == 0 && pps[1] == 0 && pps[2] == 0 && pps[3] == 1);
        es_len += (sps_sc ? 0 : 4) + (size_t)sps_len + (pps_sc ? 0 : 4) + (size_t)pps_len;
    }

    size_t max_ts_bytes = ((es_len / 184) + 3) * 188;
    if (max_ts_bytes + es_len > ts_cap) return -1;

    uint8_t *es = ts_buf + ts_cap - es_len;
    size_t off = 0;
    memcpy(es + off, pes, sizeof(pes)); off += sizeof(pes);
    memcpy(es + off, aud, sizeof(aud)); off += sizeof(aud);

    if (is_idr && sps_len > 0 && pps_len > 0) {
        int sps_sc = (sps_len >= 4 && sps[0] == 0 && sps[1] == 0 && sps[2] == 0 && sps[3] == 1);
        if (!sps_sc) { memcpy(es + off, sc, 4); off += 4; }
        memcpy(es + off, sps, (size_t)sps_len); off += (size_t)sps_len;

        int pps_sc = (pps_len >= 4 && pps[0] == 0 && pps[1] == 0 && pps[2] == 0 && pps[3] == 1);
        if (!pps_sc) { memcpy(es + off, sc, 4); off += 4; }
        memcpy(es + off, pps, (size_t)pps_len); off += (size_t)pps_len;
    }

    if (!nal_has_sc) { memcpy(es + off, sc, 4); off += 4; }
    memcpy(es + off, nal, (size_t)nal_len); off += (size_t)nal_len;

    size_t pos = 0, ts_out = 0;
    int first = 1;
    while (pos < es_len) {
        uint8_t *pkt = ts_buf + ts_out;
        size_t rem = es_len - pos;
        int need_pcr = first;
        size_t hdr_len = 4;
        size_t adapt_len = 0;

        if (need_pcr) {
            adapt_len = 8;
        }
        if (rem < 184 - adapt_len) {
            adapt_len = 184 - rem;
        }

        pkt[0] = 0x47;
        pkt[1] = (first ? 0x40 : 0x00) | 0x01;
        pkt[2] = 0x00;
        pkt[3] = (adapt_len > 0 ? 0x30 : 0x10) | ((*cc_vid)++ & 0x0F);

        if (adapt_len > 0) {
            pkt[4] = (uint8_t)(adapt_len - 1);
            if (adapt_len > 1) {
                pkt[5] = need_pcr ? (is_idr ? 0x50 : 0x10) : 0x00;
                size_t fill_start = 6;
                if (need_pcr) {
                    uint64_t pcr_base = pts90k;
                    pkt[6]  = (uint8_t)((pcr_base >> 25) & 0xFF);
                    pkt[7]  = (uint8_t)((pcr_base >> 17) & 0xFF);
                    pkt[8]  = (uint8_t)((pcr_base >> 9) & 0xFF);
                    pkt[9]  = (uint8_t)((pcr_base >> 1) & 0xFF);
                    pkt[10] = (uint8_t)(((pcr_base & 0x01) << 7) | 0x7E);
                    pkt[11] = 0x00;
                    fill_start = 12;
                }
                if (4 + adapt_len > fill_start) {
                    memset(pkt + fill_start, 0xFF, 4 + adapt_len - fill_start);
                }
            }
            hdr_len = 4 + adapt_len;
        }

        size_t copy_n = 188 - hdr_len;
        memcpy(pkt + hdr_len, es + pos, copy_n);
        pos += copy_n;
        ts_out += 188;
        first = 0;
    }

    return send_all(sock, ts_buf, ts_out);
}
static void stream_client(int client, int ctrl, FILE *log, void *av_handle,
                          uint8_t *pkt_buf, uint8_t *ts_buf, size_t ts_cap,
                          const uint8_t *sps, int sps_len,
                          const uint8_t *pps, int pps_len, int *stop_server) {
    int nodelay = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    fd_set pfds;
    FD_ZERO(&pfds);
    FD_SET(client, &pfds);
    struct timeval ptv = {0, 50000};
    if (select(client + 1, &pfds, NULL, NULL, &ptv) > 0) {
        char peek[8] = {0};
        int pn = recv(client, peek, 4, MSG_PEEK);
        if (pn == 4 && (!memcmp(peek, "GET ", 4) || !memcmp(peek, "HEAD", 4))) {
            char hdr[1024] = {0};
            int total = 0;
            while (total < (int)sizeof(hdr) - 1) {
                int r = recv(client, hdr + total, 1, 0);
                if (r <= 0) return;
                total += r;
                if (total >= 4 && !memcmp(hdr + total - 4, "\r\n\r\n", 4)) break;
            }
            const char *http_ok =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: video/mp2t\r\n"
                "Cache-Control: no-cache, no-store\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n";
            if (send_all(client, http_ok, strlen(http_ok)) < 0) return;
            if (!memcmp(peek, "HEAD", 4)) return;
        }
    }

    fprintf(log, "MPEG-TS client connected!\n");
    ps4_notify("Stream Viewer connected!");

    uint8_t cc_pat = 0, cc_pmt = 0, cc_vid = 0;
    uint64_t start_ms = milliseconds();
    if (ts_send_pat_pmt(client, &cc_pat, &cc_pmt) < 0) return;
    pbs_ps4_keyframe(av_handle);

    unsigned char info[0x40] = {0};
    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(client, &rfds);
        FD_SET(ctrl, &rfds);
        int maxfd = (client > ctrl ? client : ctrl) + 1;
        struct timeval tv = {0, 0};

        if (select(maxfd, &rfds, NULL, NULL, &tv) > 0) {
            if (FD_ISSET(ctrl, &rfds)) {
                int c = accept(ctrl, NULL, NULL);
                if (c >= 0) close(c);
                *stop_server = 1;
                break;
            }
            if (FD_ISSET(client, &rfds)) {
                char tmp[64];
                if (recv(client, tmp, sizeof(tmp), 0) <= 0) {
                    fprintf(log, "Stream client disconnected.\n");
                    break;
                }
            }
        }

        int rc = pbs_ps4_read(av_handle, pkt_buf, 0x200000, info, 1);
        if (rc > 0 && rc <= 0x200000) {
            uint8_t ntype = (rc >= 4 && pkt_buf[0] == 0 && pkt_buf[1] == 0 && pkt_buf[2] == 1) ? (pkt_buf[3] & 0x1f) :
                            (rc >= 5 && pkt_buf[0] == 0 && pkt_buf[1] == 0 && pkt_buf[2] == 0 && pkt_buf[3] == 1) ? (pkt_buf[4] & 0x1f) :
                            (pkt_buf[0] & 0x1f);
            int is_idr = (ntype == 5);
            if (is_idr) {
                if (ts_send_pat_pmt(client, &cc_pat, &cc_pmt) < 0) break;
            }
            uint64_t pts90k = (milliseconds() - start_ms) * 90ULL;
            if (ts_send_video_frame(client, &cc_vid, pts90k, is_idr,
                                    sps, sps_len, pps, pps_len,
                                    pkt_buf, rc, ts_buf, ts_cap) < 0) break;
        } else if ((uint32_t)rc == 0x80a70004u) {
            pbs_ps4_keyframe(av_handle);
        } else {
            usleep(2000);
        }
    }

    ps4_notify("Stream Viewer disconnected.");
}
int main(void) {
    signal(SIGPIPE, SIG_IGN);

    FILE *log = fopen("/data/ps4-stream.log", "w");
    if (!log) log = stderr;
    setvbuf(log, NULL, _IONBF, 0);
    pbs_problem_open("/data/ps4-stream-problems.txt", "ps4-stream");

    if (!pbs_ps4_firmware_find(kernel_get_fw_version())) {
        ps4_notify("Error: Unsupported PS4 firmware.");
        pbs_problem_finish(log, 1);
        return 1;
    }

    if (milliseconds() < 45000) {
        ps4_notify("Waiting for PS4 RemotePlay service...");
        while (milliseconds() < 45000) {
            usleep(250000);
        }
    }

    int attempt = 0;
    while (1) {
        attempt++;
        if (pbs_ps4_capture_begin(log) == 0) {
            if (pbs_ps4_init() == 0) {
                break;
            }
            pbs_ps4_capture_end();
        }
        if (attempt == 1) {
            ps4_notify("Waiting for Avcap service...");
        }
        usleep(2000000);
    }

    uint32_t params[PBS_PS4_VIDEO_REQUEST_WORDS];
    if (pbs_ps4_make_video_request_at_rate(params, HEIGHT, 0, 0, 60) != 0) {
        ps4_notify("Error: Invalid video request.");
        pbs_ps4_term();
        pbs_ps4_capture_end();
        pbs_problem_finish(log, 1);
        return 1;
    }

    void *av_handle = NULL;
    uint64_t open_began = milliseconds();
    int rc = -1;
    do {
        rc = pbs_ps4_open(&av_handle, params);
        if ((uint32_t)rc != 0x80a70008u || av_handle || milliseconds() - open_began >= 2016u) break;
        usleep(16000);
    } while (milliseconds() - open_began < 2016u);

    if (rc < 0 || !av_handle) {
        ps4_notify("Error: Avcap open failed.");
        pbs_ps4_term();
        pbs_ps4_capture_end();
        pbs_problem_finish(log, 1);
        return 1;
    }

    unsigned char sps[128], pps[128];
    int sps_len = 0, pps_len = 0;
    uint64_t until = milliseconds() + 2000;
    do {
        if (sps_len <= 0) sps_len = pbs_ps4_info(av_handle, 1, sps, sizeof sps);
        if (pps_len <= 0) pps_len = pbs_ps4_info(av_handle, 2, pps, sizeof pps);
        if (sps_len > 0 && pps_len > 0) break;
        usleep(5000);
    } while (milliseconds() < until);

    if (sps_len <= 0 || pps_len <= 0 || pbs_ps4_start(av_handle) < 0) {
        ps4_notify("Error: Avcap start failed.");
        pbs_ps4_close(av_handle);
        pbs_ps4_term();
        pbs_ps4_capture_end();
        pbs_problem_finish(log, 1);
        return 1;
    }

    size_t ts_cap = 0x500000;
    uint8_t *pkt_buf = mmap(NULL, 0x200000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t *ts_buf  = mmap(NULL, ts_cap,   PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pkt_buf == MAP_FAILED || ts_buf == MAP_FAILED) {
        ps4_notify("Error: mmap failed.");
        goto cleanup;
    }

    int server = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(STREAM_PORT);

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(server, 1) < 0) {
        ps4_notify("Error: Port 5900 bind/listen failed.");
        close(server);
        goto cleanup;
    }

    int ctrl = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(ctrl, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in caddr = {0};
    caddr.sin_family = AF_INET;
    caddr.sin_addr.s_addr = INADDR_ANY;
    caddr.sin_port = htons(CTRL_PORT);

    if (bind(ctrl, (struct sockaddr *)&caddr, sizeof(caddr)) < 0 || listen(ctrl, 1) < 0) {
        ps4_notify("Error: Port 5901 bind/listen failed.");
        close(ctrl);
        close(server);
        goto cleanup;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Stream: %s:5900 (Kill:5901)", ip);
    ps4_notify(msg);

    int stop_server = 0;
    while (!stop_server) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server, &fds);
        FD_SET(ctrl, &fds);
        int maxfd = (server > ctrl ? server : ctrl) + 1;
        if (select(maxfd, &fds, NULL, NULL, NULL) < 0) break;

        if (FD_ISSET(ctrl, &fds)) {
            int c = accept(ctrl, NULL, NULL);
            if (c >= 0) close(c);
            ps4_notify("Stream Server shutting down...");
            break;
        }
        if (FD_ISSET(server, &fds)) {
            int client = accept(server, NULL, NULL);
            if (client >= 0) {
                stream_client(client, ctrl, log, av_handle, pkt_buf, ts_buf, ts_cap, sps, sps_len, pps, pps_len, &stop_server);
                close(client);
            }
        }
    }

    close(ctrl);
    close(server);

cleanup:
    pbs_ps4_stop(av_handle);
    pbs_ps4_close(av_handle);
    pbs_ps4_term();
    pbs_ps4_capture_end();
    pbs_problem_finish(log, 0);
    if (log != stderr) fclose(log);
    return 0;
}
