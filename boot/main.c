#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <generated/csr.h>
#include <irq.h>
#include <libliteeth/udp.h>
#include <libliteeth/inet.h>
#include <libbase/console.h>
#include <libbase/uart.h>




// #include <wolfssl/wolfcrypt/user_settings.h>
// #include <wolfssl/options.h>
// #include <wolfssl/wolfcrypt/wc_mlkem.h>
// #include <wolfssl/wolfcrypt/random.h>
// #include <wolfssl/ssl.h> 



#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif



// 1. Define your Network Identity
#define MY_IP      IPTOINT(192, 168, 1, 50)
#define SERVER_IP  IPTOINT(192, 168, 1, 100)
#define MY_MAC     {0x02, 0x00, 0x00, 0x00, 0x00, 0x01}
#define UDP_PORT   1234

// 2. The Callback: What to do when a packet arrives
void udp_rx_handler(uint32_t src_ip, uint16_t src_port, uint16_t dst_port, void *data, uint32_t length) {
    printf("\n[UDP RX] Received %u bytes from %08x:%u\n", (unsigned int)length, (unsigned int)src_ip, src_port);
    
    char *payload = (char *)data;
    for(uint32_t i = 0; i < length; i++) {
        putchar(payload[i]);
    }
    printf("\n------------------------------\n");
}

int main(void) {
    uint8_t mac[] = MY_MAC;
    
    uart_init();
    printf("Starting UDP Test Client...\n");

    // Initialize UDP Stack
    udp_start(mac, MY_IP);
    udp_set_callback(udp_rx_handler);

    // 3. Send a predetermined message
    // Note: On bare-metal, we often need to trigger ARP first to find the Server
    printf("Resolving Server MAC via ARP...\n");
    while(udp_arp_resolve(SERVER_IP) == 0) {
        udp_service(); // Must call service to process the ARP reply!
    }
    printf("Server resolved. Sending Test Message...\n");

    // Get the hardware TX buffer
    void *tx_buf = udp_get_tx_buffer();
    char *msg = "Hello from LiteX SoC!";
    uint32_t msg_len = strlen(msg);
    
    // Copy message to hardware and send
    memcpy(tx_buf, msg, msg_len);
    udp_send(UDP_PORT, UDP_PORT, msg_len);

    printf("Message sent. Entering listening loop...\n");

    while(1) {
        // 4. Polling the hardware
        udp_service();
    }

    return 0;
}