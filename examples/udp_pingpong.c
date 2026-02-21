/**
 * @brief Example of a UDP ping-pong that includes the server and the clientc code.
 * See how asc_udp_socket(), asc_udp_bind() work.
 * A asc_udp_target() is not included here but see the documentation ig you're curious.
 * 
 * 20/02/2026
 */
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "asclib/core.h"
#include "asclib/net/udp.h"
#include "asclib/sync.h"
#include "asclib/cleanup.h"

uint8_t addr[4] = (uint8_t[]){127, 0, 0, 1};
uint16_t port = 8080;


async(server) {
    detach; // We could await() on the main_task but this is safer.
    udp_node_t* server = clean_on_drop(asc_udp_bind(addr, port));
    printf("[SERVER]: Opened server.\n");
    // Client address and port to send the pong back. 
    udp_peer_t p = {0};
    char buff[100];

    // Receive a UDP message with a 2s timeout.
    // Shorten the ms and you'll ser the fallback code execute.
    ssize_t sz = io_timeout(ssize_t, asc_recvfrom(server, buff, 100, &p), 2000, {
        printf("[SERVER]: Receiving timed out!\n");
        goto __exit;
    });
    if (sz >= 0) {
        char ip_addr[25];
        asc_peer_tostring(&p, ip_addr, 25);
        printf("[SERVER]: Received from %s -> %.*s\n", ip_addr, (int)sz, buff);
    } else {
        printf("[SERVER]: Couldn't receive from client: %s\n", strerror(errno));
        task_return_void();
    }
    // Send the Pong!
    char pong[] = "Pong!";
    // "Useless" timeout since in UDP you don't wait for the client to receive the data.
    sz = io_timeout(ssize_t, asc_sendto(server, pong, 6, p), 2000, {
        printf("[SERVER]: Sending timed out!\n");
        task_return_void();
    });

    if (sz >= 0) {
        printf("[SERVER]: Sent succesfully\n");
    } else {
        printf("[SERVER]: Couldn't receive from client: %s\n", strerror(errno));
    }
__exit:
    printf("[SERVER]: Closed server.\n");
    end_return_void();
}

async(main_task) {

    // No need for a yield because of how UDP works !!
    task_spawn(server);
    // Create the UDP socket.
    udp_node_t* client = clean_on_drop(asc_udp_socket());
    await(asc_sleep(1000));

    // Just for printing but you could NULL out the &p on the receiving.
    udp_peer_t p = {0};
    // Send the Ping!
    char ping[] = "Ping!";
    // We could use asc_udp_target() to lock-in the server addr:port.
    // If you do that the peer parameter is ignored so you can do ```(udp_peer_t){0}```
    await(asc_sendto(client, ping, 6, peer_from(addr, port)));
    char buff[100];
    // Wait for 100ms for the server the respond the Pong! back. 
    ssize_t sz = io_timeout(ssize_t, asc_recvfrom(client, buff, 100, &p), 100, {
        printf("[CLIENT]: Server didn't respond!\n");
        task_return_void();
    });
    if (sz >= 0) {
        char ip_addr[25];
        asc_peer_tostring(&p, ip_addr, 25);
        printf("[CLIENT]: Received from '%s': %.*s\n", ip_addr, (int)sz, buff);
    } else {
        printf("[CLIENT]: Couldn't receive: %s\n", strerror(errno));
    }

    end_return_void();
}


int main() {

    worker_t* w = worker_create(1, task_entry(main_task));
    worker_run(w);
    worker_await_all(w);
    printf("Returned!\n");
    worker_destroy(w);
    return 0;
}