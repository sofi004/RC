#include "packet-format.h"
#include <arpa/inet.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char *argv[])
{
  if (argc != 4)
  {
    fprintf(stderr, "Usage: %s <file_name> <port> <window_size>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  char *file_name = argv[1];
  int port = atoi(argv[2]);
  int window_size = atoi(argv[3]);

  if (window_size <= 0 || window_size > 32)
  {
    fprintf(stderr, "Invalid window size. Must be between 1 and 32.\n");
    exit(EXIT_FAILURE);
  }

  FILE *file = fopen(file_name, "w");
  if (!file)
  {
    perror("fopen");
    exit(EXIT_FAILURE);
  }

  // Prepare server socket.
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  struct timeval tv;
  
  if (sockfd == -1)
  {
    perror("socket");
    fclose(file);
    exit(EXIT_FAILURE);
  }

  // Allow address reuse so we can rebind to the same port,
  // after restarting the server.
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
  {
    perror("setsockopt");
    fclose(file);
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in srv_addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
      .sin_port = htons(port),
  };

  if (bind(sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)))
  {
    perror("bind");
    fclose(file);
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "Receiving on port: %d\n", port);

  ssize_t len;
  uint32_t seq_num = 0;
  ack_pkt_t ack_pkt;
  //uint32_t selective_acks = 0;
  struct sockaddr_in src_addr;
  socklen_t addr_len = sizeof(src_addr);
  data_pkt_t data_pkt;

  do
  { // Iterate over segments, until last the segment is detected.
    // Receive segment.
    tv.tv_sec = 4;
    tv.tv_usec = 0;
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
      exit(EXIT_FAILURE);
    }

    len =
        recvfrom(sockfd, &data_pkt, sizeof(data_pkt), 0, (struct sockaddr *)&src_addr, &(socklen_t){sizeof(src_addr)});

    if (len == -1)
    {
      // Timeout occurred, check if file is incomplete
      fprintf(stderr, "Timeout occurred.\n");

      // Check the file size to determine if it's complete.
      fseek(file, 0, SEEK_END);
      if (ftell(file) == 0)
      {
        fprintf(stderr, "File is incomplete, deleting file.\n");
        fclose(file);
        //remove(file_name); // Delete the incomplete file
        exit(EXIT_FAILURE);
      }
      else
      {
        // File is complete, terminate successfully
        fprintf(stderr, "File received completely.\n");
        fclose(file);
        close(sockfd);
        return EXIT_SUCCESS;
      }
    }

    printf("Received segment %d, size %ld.\n", ntohl(data_pkt.seq_num), len);

    fprintf(stderr, "data_seq_num %d\n", ntohl(data_pkt.seq_num));

    // Write data to file.
    fprintf(stderr, "data_seq_num_recv %d\n", ntohl(data_pkt.seq_num));
    fprintf(stderr, "seq_num_recv_expect %d\n", seq_num);
    if(ntohl(data_pkt.seq_num) == seq_num){
      fwrite(data_pkt.data, 1, len - offsetof(data_pkt_t, data), file);
      seq_num++;
    }
    // Send acknoledge.
    ack_pkt.seq_num = htonl(seq_num);
    ack_pkt.selective_acks = 0;
    ack_pkt.selective_acks = htonl(ack_pkt.selective_acks);

    fprintf(stderr, "ack_seq_num%d\n", ntohl(ack_pkt.seq_num));
    ssize_t sent_len =
        sendto(sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&src_addr, addr_len);
    printf("Sending ack %d\n", ntohl(ack_pkt.seq_num));
    if (sent_len != sizeof(ack_pkt))
    {
      fprintf(stderr, "Truncated acknoledge.\n");
      exit(EXIT_FAILURE);
    }
  } while (true);
  
  // Clean up and exit.
  close(sockfd);
  fclose(file);

  return EXIT_SUCCESS;
}