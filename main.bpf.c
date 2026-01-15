#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct tcp_event_t {
    u32 pid;
    int ret;
    u32 daddr_v4;      // IPv4 Address
    u8  daddr_v6[16];  // IPv6 Address
    u16 family;        // 2 = IPv4, 10 = IPv6
    u16 dport;
    char comm[16];
    u64 duration_ns;
};

// Ring Buffer
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} rb SEC(".maps");

struct req_info_t {
    u64 start_ts;
    char comm[16];
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, struct sock *);
    __type(value, struct req_info_t);
} request_context SEC(".maps");

#define TCP_ESTABLISHED 1
#define TCP_SYN_SENT    2
#define TCP_CLOSE       7
#define AF_INET         2
#define AF_INET6        10

SEC("tracepoint/sock/inet_sock_set_state")
int trace_tcp_state(struct trace_event_raw_inet_sock_set_state *ctx) {
    struct sock *sk = (struct sock *)ctx->skaddr;
    int old_state = ctx->oldstate;
    int new_state = ctx->newstate;

    // 1. Connection Started
    if (new_state == TCP_SYN_SENT) {
        struct req_info_t info = {};
        info.start_ts = bpf_ktime_get_ns();
        bpf_get_current_comm(&info.comm, sizeof(info.comm)); 
        bpf_map_update_elem(&request_context, &sk, &info, BPF_ANY);
        return 0;
    }

    // 2. Connection Failed
    if (old_state == TCP_SYN_SENT && new_state != TCP_ESTABLISHED) {
        int err = 0;
        bpf_probe_read_kernel(&err, sizeof(err), &sk->sk_err);
        if (err == 0) err = 125; 

        struct tcp_event_t *e;
        e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
        if (!e) return 0;

        e->pid = bpf_get_current_pid_tgid() >> 32;
        e->ret = -err;
        
        // Read Family
        bpf_probe_read_kernel(&e->family, sizeof(e->family), &sk->__sk_common.skc_family);
        bpf_probe_read_kernel(&e->dport, sizeof(e->dport), &sk->__sk_common.skc_dport);

        // Read Address based on Family
        if (e->family == AF_INET6) {
            bpf_probe_read_kernel(&e->daddr_v6, sizeof(e->daddr_v6), &sk->__sk_common.skc_v6_daddr);
            e->daddr_v4 = 0;
        } else {
            bpf_probe_read_kernel(&e->daddr_v4, sizeof(e->daddr_v4), &sk->__sk_common.skc_daddr);
        }

        struct req_info_t *info = bpf_map_lookup_elem(&request_context, &sk);
        if (info) {
            bpf_probe_read_kernel_str(&e->comm, sizeof(e->comm), info->comm);
            e->duration_ns = bpf_ktime_get_ns() - info->start_ts;
            bpf_map_delete_elem(&request_context, &sk);
        } else {
            bpf_get_current_comm(&e->comm, sizeof(e->comm));
            e->duration_ns = 0;
        }

        bpf_ringbuf_submit(e, 0);
    }
    
    if (new_state == TCP_ESTABLISHED || new_state == TCP_CLOSE) {
        bpf_map_delete_elem(&request_context, &sk);
    }

    return 0;
}