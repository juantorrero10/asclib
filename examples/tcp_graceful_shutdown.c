/**
 * @brief Example program: oneshots, await_any, tcp_bind, accept, write...
 * TCP server that stablishes a connection with a client.
 * Everytime time that the client sends something the server 
 * send "BACK" to the client.
 * If the client sends "exit" the server gracefully shutdown 
 * using a oneshot channel with undefined ownership until fired.
 * 
 * Also a good test case for await_any() and any_unwrap_or().
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "asclib/core.h"
#include "asclib/io.h"
#include "asclib/net.h"
#include "asclib/cleanup.h"
#include "asclib/env.h"

#define PORT 9999


async(handle_client, int client_fd; oneshot_tx_ref_t shutdown_ref) {
    detach; /// < IMPORTANT detach to not let tasks hanging on FINISHED and clean up resources (fd).
    int fd = clean_on_drop(arg(client_fd));
    printf("[+] New Connection: %d\n", fd);
    char buf[1024];
    while (1) {
        // Wait until user returns something or channels get closed/used
        ssize_t n = any_some_or(ssize_t,
                await_any(
                    asc_recv(fd, buf, sizeof(buf)),
                    oneshot_wait_change(arg(shutdown_ref))
                ).cut_losers(),
            {
                printf("[!]: <%d> Other client sent the shutdown signal\n", fd); 
                break; 
            }
        );

        if (n <= 0) {
            printf("[*]: <%d> Error reading form client: %s\n",fd, strerror(errno));
            break;
        }

        // Check for  "exit"
        if (n >= 4 && memcmp(buf, "exit", 4) == 0) {
            printf("[!] Exit command received...\n");
            
            // We only take ownership right when we are about to use it.
            // This ensures that any client can trigger but only the first to do it.
            oneshot_tx_t tx = clean_on_drop(oneshot_take_tx(arg(shutdown_ref)));
            
            int signal_payload = 0xDEAD;
            await(asc_oneshot_send(tx, &signal_payload));
            break; 
        }
        // Echo back
        await(asc_send(fd, "BACK", 5));
    }
    printf("[-] Closed connection: %d\n", fd);
    end_return_void();
}

async(accept_loop, tcp_listerner_t* listener; oneshot_tx_ref_t shutdown_ref) {
    while (1) {
        int client_fd = await_ret(int, asc_tcp_accept(arg(listener)));
        if (client_fd >= 0) {
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

    // Create a oneshot with 1 integer and tracking to verify it is properly closed.
    oneshot_pair_t signal = oneshot_new(sizeof(int), 1);
    oneshot_tx_ref_t tx_ref = oneshot_move_tx(signal);   // Set tx-end to reference mode.
    
    printf("[SERVER] Running. Waiting for clients...\n");

    int signal_result = 0;

    // accept_loop never finishes, 
    // so we wait until shutdown signal an drop accept task with .select()
    // cut_losers() do the same but if we don't want the return value is not recomended.
    await_any(
        task_spawn(accept_loop, .listener = listener, .shutdown_ref = tx_ref),
        asc_oneshot_recv((oneshot_rx_t)signal, &signal_result)
    ).select();

    printf("[SERVER] Graceful shutdown triggered! Payload: 0x%X\n", signal_result);
    
    // clean_on_drop handles listener and rx. 
    // oneshot_assert_closed can be used here for final verification.
    oneshot_drop_rx((oneshot_rx_t)signal);
    oneshot_assert_closed(signal);          // This panic if channel is not properly closed
    end_return_void();
}


int main() {

    worker_t* w = worker_create(1, task_entry(main_server));
    worker_run(w);
    worker_await_all(w);    // Handle client instance are detached so we have to wait for entire thread
    printf("Returned!\n");
    worker_destroy(w);
    return 0;
}