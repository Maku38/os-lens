#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct tcp_event_t {
    u32 pid;
    int ret;
    u32 daddr;
    u16 dport;
    char comm[16];
    u64 duration_ns;
};

// Ring Buffer
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} rb SEC(".maps");

// NEW: A struct to save context (Time + Name)
struct req_info_t {
    u64 start_ts;
    char comm[16]; // Save the name here!
};

// Map: Key = socket pointer, Value = req_info_t
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, struct sock *);
    __type(value, struct req_info_t); // <--- Changed from u64 to struct
} request_context SEC(".maps");

#define TCP_ESTABLISHED 1
#define TCP_SYN_SENT    2
#define TCP_CLOSE       7

SEC("tracepoint/sock/inet_sock_set_state")
int trace_tcp_state(struct trace_event_raw_inet_sock_set_state *ctx) {
    struct sock *sk = (struct sock *)ctx->skaddr;
    int old_state = ctx->oldstate;
    int new_state = ctx->newstate;

    // 1. Connection Started: Save Time AND Name
    if (new_state == TCP_SYN_SENT) {
        struct req_info_t info = {};
        
        info.start_ts = bpf_ktime_get_ns();
        // Capture "curl" right now while it is running!
        bpf_get_current_comm(&info.comm, sizeof(info.comm)); 
        
        bpf_map_update_elem(&request_context, &sk, &info, BPF_ANY);
        return 0;
    }

    // 2. Connection Failed
    if (old_state == TCP_SYN_SENT && new_state != TCP_ESTABLISHED) {
        int err = 0;
        bpf_probe_read_kernel(&err, sizeof(err), &sk->sk_err);

        // Logic for Client Cancel (err 0 -> 125)
        if (err == 0) err = 125; 

        struct tcp_event_t *e;
        e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
        if (!e) return 0;

        e->pid = bpf_get_current_pid_tgid() >> 32;
        e->ret = -err;
        bpf_probe_read_kernel(&e->daddr, sizeof(e->daddr), &sk->__sk_common.skc_daddr);
        bpf_probe_read_kernel(&e->dport, sizeof(e->dport), &sk->__sk_common.skc_dport);

        // --- NAME RETRIEVAL MAGIC ---
        struct req_info_t *info = bpf_map_lookup_elem(&request_context, &sk);
        
        if (info) {
            // Restore the saved name ("curl")
            bpf_probe_read_kernel_str(&e->comm, sizeof(e->comm), info->comm);
            e->duration_ns = bpf_ktime_get_ns() - info->start_ts;
            
            // Clean up
            bpf_map_delete_elem(&request_context, &sk);
        } else {
            // Fallback to "swapper" if map is missing (rare)
            bpf_get_current_comm(&e->comm, sizeof(e->comm));
            e->duration_ns = 0;
        }
        // -----------------------------

        bpf_ringbuf_submit(e, 0);
    }
    
    // Cleanup on success or close
    if (new_state == TCP_ESTABLISHED || new_state == TCP_CLOSE) {
        bpf_map_delete_elem(&request_context, &sk);
    }

    return 0;
}