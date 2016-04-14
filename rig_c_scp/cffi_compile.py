"""Build the CFFI interface for the high-performance SCP library."""

import os
import platform
from cffi import FFI
ffi = FFI()

source_dir = os.path.dirname(__file__)

ffi.set_source(
    "_rig_c_scp",
    """
        #include <sys/socket.h>
        
        #include <stdio.h>
        #include <stdlib.h>
        #include <stdint.h>
        #include <stdbool.h>
        #include <string.h>
        
        #include <uv.h>
        
        #include "rs.h"
        
        
        // Return an addrinfo for the supplied hostname
        struct addrinfo *hostname_to_addrinfo(const char *hostname, const char *port)
        {
            struct addrinfo *addrinfo;
            struct addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;  // SpiNNaker only supports IPv4
            hints.ai_socktype = SOCK_DGRAM; // SCP is a datagram based protocol
            int success = getaddrinfo(hostname, port, // The SCP port number
                                      &hints, &addrinfo);
            return (success == 0) ? addrinfo : NULL;
        }
        
        // Allocate memory for a libuv event loop (can't do this from python
        // due to the opaque datatype used by libuv.
        uv_loop_t *malloc_uv_loop_t(void)
        {
            return malloc(sizeof(uv_loop_t));
        }
        
        // Allocate memory for an async event handler (can't do this from
        // python due to the opaque datatype used by libuv.
        uv_async_t *malloc_uv_async_t(void)
        {
            return malloc(sizeof(uv_async_t));
        }
    """,
    libraries=["uv"],
    sources=[os.path.join(source_dir, f)
             for f in ("rs.c",
                       "rs__cancel.c",
                       "rs__process_queue.c",
                       "rs__process_response.c",
                       "rs__queue.c",
                       "rs__scp.c",
                       "rs__transport.c")],
    include_dirs=[source_dir],
    extra_compile_args=["-O3"],
)

ffi.cdef("""
    // Standard library
    struct addrinfo {
        struct sockaddr *ai_addr;
        ...;
    };
    void freeaddrinfo(struct addrinfo *res);
    void *malloc(size_t len);
    void free(void *ptr);
    
    // Lib UV
    typedef enum {
      UV_RUN_DEFAULT = 0,
      UV_RUN_ONCE,
      UV_RUN_NOWAIT
    } uv_run_mode;
    
    typedef struct uv_loop_s uv_loop_t;
    
    int uv_loop_init(uv_loop_t* loop);
    uv_loop_t* uv_default_loop(void);
    
    int uv_run(uv_loop_t*, uv_run_mode mode);
    
    void uv_stop(uv_loop_t*);
    
    typedef struct uv_buf_t {
        char* base;
        size_t len;
        ...;
    } uv_buf_t;
    
    typedef struct uv_async_s {
        void *data;
        ...;
    } uv_async_t;
    typedef void (*uv_async_cb)(uv_async_t* handle);
    int uv_async_init(uv_loop_t* loop, uv_async_t* async, uv_async_cb async_cb);
    int uv_async_send(uv_async_t* async);
    
    // Rig C SCP
    struct rs_conn;
    typedef struct rs_conn rs_conn_t;
    
    typedef void (*rs_send_scp_cb)(rs_conn_t *conn,
                                   int error,
                                   uint16_t cmd_rc,
                                   unsigned int n_args,
                                   uint32_t arg1,
                                   uint32_t arg2,
                                   uint32_t arg3,
                                   uv_buf_t data,
                                   void *cb_data);
    
    
    typedef void (*rs_rw_cb)(rs_conn_t *conn,
                             int error,
                             uint16_t cmd_rc,
                             uv_buf_t data,
                             void *cb_data);
    
    typedef void (*rs_free_cb)(void *cb_data);
    
    rs_conn_t *rs_init(uv_loop_t *loop,
                       const struct sockaddr *addr,
                       size_t scp_data_length,
                       uint64_t timeout,
                       unsigned int n_tries,
                       unsigned int n_outstanding);
    
    int rs_send_scp(rs_conn_t *conn,
                    uint16_t dest_addr,
                    uint8_t dest_cpu,
                    uint16_t cmd_rc,
                    unsigned int n_args_send,
                    unsigned int n_args_recv,
                    uint32_t arg1,
                    uint32_t arg2,
                    uint32_t arg3,
                    uv_buf_t data,
                    size_t data_max_len,
                    rs_send_scp_cb cb,
                    void *cb_data);
    
    int rs_write(rs_conn_t *conn,
                 uint16_t dest_addr,
                 uint8_t dest_cpu,
                 uint32_t address,
                 uv_buf_t data,
                 rs_rw_cb cb,
                 void *cb_data);
    
    int rs_read(rs_conn_t *conn,
                uint16_t dest_addr,
                uint8_t dest_cpu,
                uint32_t address,
                uv_buf_t data,
                rs_rw_cb cb,
                void *cb_data);
    
    void rs_free(rs_conn_t *conn, rs_free_cb cb, void *cb_data);
    
    #define RS_EBAD_RC 1
    #define RS_ETIMEOUT 2
    #define RS_EFREE 3
    
    const char *rs_strerror(int err);
    const char *rs_err_name(int err);
    
    // Utility functions
    struct addrinfo *hostname_to_addrinfo(const char *hostname, const char *port);
    uv_loop_t *malloc_uv_loop_t(void);
    uv_async_t *malloc_uv_async_t(void);
    
    // Callbacks for Python
    extern "Python" void send_scp_cb(rs_conn_t *conn,
                                     int error,
                                     uint16_t cmd_rc,
                                     unsigned int n_args,
                                     uint32_t arg1,
                                     uint32_t arg2,
                                     uint32_t arg3,
                                     uv_buf_t data,
                                     void *cb_data);
    extern "Python" void write_cb(rs_conn_t *conn,
                                  int error,
                                  uint16_t cmd_rc,
                                  uv_buf_t data,
                                  void *cb_data);
    extern "Python" void read_cb(rs_conn_t *conn,
                                 int error,
                                 uint16_t cmd_rc,
                                 uv_buf_t data,
                                 void *cb_data);

    extern "Python" void free_cb(void *cb_data);
    extern "Python" void async_cb(uv_async_t* handle);
    
""")

if __name__ == "__main__":
    ffi.compile()

