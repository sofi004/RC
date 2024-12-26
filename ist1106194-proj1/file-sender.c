#include "packet-format.h"
#include <limits.h>
#include <netdb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  char *file_name = argv[1];
  char *host = argv[2];
  int port = atoi(argv[3]);
  int window_size = atoi(argv[4]);
  
  
  FILE *file = fopen(file_name, "r");
  if (!file) {
    perror("fopen");
    exit(EXIT_FAILURE);
  }

  // Prepare server host address.
  struct hostent *he;
  if (!(he = gethostbyname(host))) {
    perror("gethostbyname");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in srv_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr = *((struct in_addr *)he->h_addr_list[0]),
  };
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  struct timeval tv;

  if (sockfd == -1) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(sockfd, &read_fds);

  size_t data_len; 
  ack_pkt_t ack_pkt;
  ack_pkt.seq_num=-1;
  int retries=0;
  int count=0;
  int i = 0; 
  data_pkt_t window_pkt[window_size];
  int base = 0,atualizar_window=1;
  int last_len=0;

  do { // Generate segments from file, until the the end of the file.
    // Prepare data segment.
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
      exit(EXIT_FAILURE);
    }
    if (base == 0){
      for(i = 0; i < window_size; i++){
        if (!feof(file)){
          data_pkt_t data_pkt;

          data_pkt.seq_num = htonl(i);
          data_len = fread(data_pkt.data, 1, sizeof(data_pkt.data), file);
          window_pkt[i] = data_pkt;
          ssize_t sent_len =
          sendto(sockfd, &window_pkt[i], offsetof(data_pkt_t, data) + data_len, 0, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
          printf("Sending segment %d, size %ld.\n", ntohl(window_pkt[i].seq_num), offsetof(data_pkt_t, data) + data_len);
          if (sent_len != offsetof(data_pkt_t, data) + data_len) {
            fprintf(stderr, "Truncated packet.\n");
            exit(EXIT_FAILURE);
          }
        }
      }
      if(feof(file)){
        last_len=data_len;
      }
      
      
    }
    else if (atualizar_window==1 ){
      data_pkt_t data_pkt;
      if (!feof(file)) {
        if (window_size>1){
          for (i=0;i<window_size-1;i++){
            window_pkt[i] = window_pkt[i + 1];
          }
        }
        data_pkt.seq_num = htonl(base+window_size - 1);
        data_len = fread(data_pkt.data, 1, sizeof(data_pkt.data), file);
        window_pkt[window_size - 1] = data_pkt;
        ssize_t sent_len =
        sendto(sockfd, &window_pkt[window_size-1], offsetof(data_pkt_t, data) + data_len, 0, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
        printf("Sending segment %d, size %ld.\n", ntohl(data_pkt.seq_num), offsetof(data_pkt_t, data) + data_len);
        if (sent_len != offsetof(data_pkt_t, data) + data_len) {
          fprintf(stderr, "Truncated packet.\n");
          exit(EXIT_FAILURE);
        }
        atualizar_window=0;
        if(feof(file)){
          last_len=data_len;
        }
      }
      else{
        count++;
      }
    }

    retries = 0;
    int dup_acks = 0;
    while (retries <= MAX_RETRIES) {
      ack_pkt.seq_num=-1;
      tv.tv_sec = 1;
      tv.tv_usec = 0;
      struct sockaddr_in src_addr;
      socklen_t addr_len = sizeof(src_addr);
      ssize_t recv_len = recvfrom(sockfd, &ack_pkt, sizeof(ack_pkt), 0,(struct sockaddr *)&src_addr, &addr_len);
      if(recv_len == -1 && retries!=MAX_RETRIES){
        for (i=count;i<window_size;i++){
          if ((i==window_size-1) && feof(file)){
            ssize_t sent_len = sendto(sockfd, &window_pkt[i], offsetof(data_pkt_t, data) + last_len, 0, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
            printf("Sending segment %d, size %ld.\n", ntohl(window_pkt[i].seq_num), offsetof(data_pkt_t, data) + last_len);
            if (sent_len != offsetof(data_pkt_t, data) + last_len) {
              fprintf(stderr, "Truncated packet.\n");
              exit(EXIT_FAILURE);
            }
          }
          else{//falha aqui se forem menos chunks do que window size
            ssize_t sent_len = sendto(sockfd, &window_pkt[i], offsetof(data_pkt_t, data) + sizeof(window_pkt[i].data), 0, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
            printf("Sending segment %d, size %ld.\n", ntohl(window_pkt[i].seq_num), offsetof(data_pkt_t, data) + sizeof(window_pkt[i].data));
            if (sent_len != offsetof(data_pkt_t, data) + sizeof(window_pkt[i].data)) {
              fprintf(stderr, "Truncated packet.\n");
              exit(EXIT_FAILURE);
            }
          }
        }
        retries++;
      }
      else if((base + 1)  == ntohl(ack_pkt.seq_num)){
        printf("Received ACK %d.\n", ntohl(ack_pkt.seq_num));
        base++;
        atualizar_window=1;
        break;
      } 

      else if((ntohl(window_pkt[0+count].seq_num)) == ntohl (ack_pkt.seq_num)){
        printf("Unexpected ACK received: %d (expected %d).\n", ntohl(ack_pkt.seq_num), ntohl(window_pkt[0].seq_num));
        dup_acks++;
        if (dup_acks == 3) {
          for (i=count;i<window_size;i++){
            if ((i==window_size-1) && feof(file)){
              ssize_t sent_len = sendto(sockfd, &window_pkt[i], offsetof(data_pkt_t, data) + last_len, 0, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
              printf("Sending segment %d, size %ld.\n", ntohl(window_pkt[i].seq_num), offsetof(data_pkt_t, data) + last_len);
              if (sent_len != offsetof(data_pkt_t, data) + last_len) {
                fprintf(stderr, "Truncated packet.\n");
                exit(EXIT_FAILURE);
              }
            }
            else{
              ssize_t sent_len = sendto(sockfd, &window_pkt[i], offsetof(data_pkt_t, data) + sizeof(window_pkt[i].data), 0, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
              printf("Sending segment %d, size %ld.\n", ntohl(window_pkt[i].seq_num), offsetof(data_pkt_t, data) + sizeof(window_pkt[i].data));
              if (sent_len != offsetof(data_pkt_t, data) + sizeof(window_pkt[i].data)) {
                fprintf(stderr, "Truncated packet.\n");
                exit(EXIT_FAILURE);
              }
            }
          }
          retries++;
        }
      }
      else if(retries==MAX_RETRIES)
      {
        fprintf(stderr, "Mais de 3 tentativas.\n");
        exit(EXIT_FAILURE);
      }
    }
    if(retries > MAX_RETRIES){
      fprintf(stderr, "Mais de 3 tentativas.\n");
      exit(EXIT_FAILURE);
    }
    
    
  } while (!(feof(file) && (base-1) == ntohl(window_pkt[window_size-1].seq_num)));

  // Clean up and exit.
  close(sockfd);
  fclose(file);

  return EXIT_SUCCESS;
}