#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>


#include "asclib/core.h"
#include "asclib/io.h"
#include "asclib/net.h"
#include "asclib/cleanup.h"
#include "asclib/env.h"

#define PORT 9999


async(handle_client, int client_fd; oneshot_tx_ref_t shutdown_ref) {
    detach;
    int fd = clean_on_drop(arg(client_fd));
    printf("[+]: New Connection -> %d\n", fd);
    char buf[1024];
    while (1) {
        // Wait until user returns something or channels get closed/used
        ssize_t n = any_unwrap_or(ssize_t,
                await_any(
                    asc_read(fd, buf, sizeof(buf)),
                    oneshot_wait_change(arg(shutdown_ref))
                ).cut_losers(),
            { break; }
        );

        if (n >= 4 && memcmp(buf, "file", 4) == 0) {
            int file_fd = asc_open("index.html", O_RDONLY, 0);
            if (file_fd < 0) {
                await(asc_write(fd, "Internal server error", 21));
            } else {
                struct stat st;
                fstat(file_fd, &st);
                printf("[FILE]: Sending %ld bytes via sendfile...\n", st.st_size);
                
                // Zero-copy transfer
                await(asc_sendfile(fd, file_fd, st.st_size));
                close(file_fd);
            }
            continue; 
        }

        // Check for the "exit" command from Python
        if (n >= 4 && memcmp(buf, "exit", 4) == 0) {
            printf("[!] Exit command received...\n");
            
            // CLAIM ownership right before the send as per your design
            oneshot_tx_t tx = clean_on_drop(oneshot_take_tx(arg(shutdown_ref)));
            
            int signal_payload = 0xDEAD;
            await(asc_oneshot_send(tx, &signal_payload));
            break; 
        }
        // Echo back to python
        await(asc_write(fd, "BACK", 5));
    }
    printf("[-]: Closed connection: %d\n", fd);
    end_return_void();
}

async(accept_loop, tcp_listerner_t* listener; oneshot_tx_ref_t shutdown_ref) {
    while (1) {
        int client_fd = await_ret(int, asc_tcp_accept(arg(listener)));
        if (client_fd >= 0) {
            // We pass the REF (the neutral state) to the client
            // No need to take it here, we let the client 'take' it if they need to exit
            task_spawn(handle_client,
                .client_fd = client_fd, 
                .shutdown_ref = arg(shutdown_ref)
            );
        }
    }
    end_return_void();
}

async(main_server) {


    tcp_listerner_t* listener = clean_on_drop(asc_tcp_bind((uint8_t[]){127, 0, 0, 1}, 9999));
    if (!listener) {
        printf("[SERVER]: Binding failed! -> %s\n", strerror(errno));
        task_return_void();
    }

    oneshot_pair_t signal = oneshot_new(sizeof(int), 1);
    oneshot_tx_ref_t tx_ref = oneshot_move_tx(signal);
    
    printf("[SERVER] Running. Waiting for clients...\n");

    int signal_result = 0;

    // 2. The Race: Accept Loop vs. The "Exit" Signal
    await_any(
        task_spawn(accept_loop, .listener = listener, .shutdown_ref = tx_ref),
        asc_oneshot_recv((oneshot_rx_t)signal, &signal_result)
    ).select();

    printf("[SERVER] Graceful shutdown triggered! Payload: 0x%X\n", signal_result);
    
    // clean_on_drop handles listener and rx. 
    // oneshot_assert_closed can be used here for final verification.
    oneshot_drop_rx((oneshot_rx_t)signal);
    oneshot_assert_closed(signal);
    end_return_void();
}


int main() {

    worker_t* w = worker_create(1, task_entry(main_server));
    worker_run(w);
    worker_await_all(w);
    printf("Returned!\n");
    worker_destroy(w);

    // NO MEM LEAKS!!

    return 0;
}