/**
 * @file 
 * @brief C Code-Q3 client
 * 
 * This is a chatroom client.
 * 
 * @author Jerry Hou <Jerry.Hou@aver.com>
 * @note Copyright (c) 2025, AVer Information, Inc.
 *       All rights reserved. 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

typedef unsigned char   UINT8;  // 1 byte 
typedef int             INT32;  // 4 byte 

#define SERVER "127.0.0.1"
#define PORT 3532
#define BUFFER_SIZE 1024
#define NAME_SIZE 10

void *receive_message(void *arg);
int read_jpeg_dimensions(const UINT8 *filename, INT32 *width, INT32 *height);
void send_image(INT32 socket, const UINT8 *filename);

int main()
{
    INT32 client_fd;
    struct sockaddr_in server_addr;
    UINT8 nickname[NAME_SIZE];
    UINT8 buffer[BUFFER_SIZE];
    pthread_t recv_thread;

    
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Could not create socket!");
        exit(EXIT_FAILURE);
    }

    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER);
    server_addr.sin_port = htons(PORT);

    INT32 connect_rtnval = connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (connect_rtnval == -1)
    {
        perror("Connect error!");
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server\n");

    
    printf("Please enter your name: ");
    fgets(nickname, NAME_SIZE, stdin);
    nickname[strcspn(nickname, "\n")] = '\0';

    INT32 send_rtnval = send(client_fd, nickname, strlen(nickname), 0);
    if (send_rtnval == -1)
    {
        perror("Send error!");
        close(client_fd);
        exit(EXIT_FAILURE);
    }


    INT32 thread_rtnval = pthread_create(&recv_thread, NULL, receive_message, (void *)&client_fd);
    if (thread_rtnval < 0)
    {
        perror("Create thread error!");
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    
    while (fgets(buffer, BUFFER_SIZE, stdin) != NULL)
    {
        buffer[strcspn(buffer, "\n")] = '\0';

        if (strcmp(buffer, "/exit") == 0)
        {
            break;
        }
        else if (strncmp(buffer, "/i ", 3) == 0)
        {
            UINT8 filename[256];
            sscanf(buffer, "/i %s", filename);
            send_image(client_fd, filename);
        }
        else if (send(client_fd, buffer, strlen(buffer), 0) == -1)
        {
            perror("Failed to send message!");
            break;
        }
    }

    
    close(client_fd);
    pthread_cancel(recv_thread);
    pthread_join(recv_thread, NULL);
    return 0;
}

void *receive_message(void *arg)
{
    INT32 client_socket = *(INT32*)arg;
    UINT8 buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    while (1)
    {
        bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0)
        {
            buffer[bytes_received] = '\0';
            
            if (strncmp(buffer, "/i ", 3) == 0)
            {
                UINT8 filename[256];
                UINT8 nickname[256];
                INT32 image_size;
                INT32 width;
                INT32 height;
                if (sscanf(buffer, "/i %s %s %d %d %d", nickname, filename, &image_size, &width, &height) == 5)
                {
                    printf("Received image %s (%d x %d, %d bytes) from %s\n", filename, width, height, image_size, nickname);
                    UINT8 *image_data = (UINT8 *)malloc(image_size);
                    if (image_data == NULL)
                    {
                        perror("Malloc error!");
                        continue;
                    }
                    ssize_t total_received = 0;
                    ssize_t received;
                    while (total_received < image_size)
                    {
                        received = recv(client_socket, image_data + total_received,
                                                image_size - total_received, 0);
                        if (received <= 0)
                        {
                            perror("Failed to receive image!");
                            free(image_data);
                            break;
                        }
                        total_received += received;
                    }
                    if (total_received == image_size)
                    {                        
                        UINT8 save_path[256];
                        snprintf(save_path, sizeof(save_path), "received_%s", filename);
                        FILE *fp = fopen(save_path, "wb");
                        if (fp != NULL)
                        {
                            fwrite(image_data, 1, image_size, fp);
                            fclose(fp);
                            printf("Image saved as %s\n",save_path);
                        }
                        else
                        {
                            perror("Could not save image.\n");
                        }
                    }
                    free(image_data);
                }
                else
                {
                    printf("Image header error\n");
                }
            }
            else
            {
                printf("%s", buffer);
            }
        }
        else if (bytes_received == 0)
        {
            printf("Disconnect from the server.\n");
            break;
        }
        else
        {
            perror("Failed to receive message.\n");
            break;
        }
    }
    pthread_exit(NULL);
}


int read_jpeg_dimensions(const UINT8 *filename, INT32 *width, INT32 *height)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        perror("fopen");
        return -1;
    }

    UINT8 marker[2];

    if (fread(marker, 1, 2, fp) != 2 || marker[0] != 0xFF || marker[1] != 0xD8)
    {
        fclose(fp);
        return -1;
    }

    while (!feof(fp)) {        
        if (fread(marker, 1, 2, fp) != 2)
        {
            break;
        }

        if (marker[0] != 0xFF)
        {
            continue;
        }

        while (marker[1] == 0xFF)
        {
            if (fread(&marker[1], 1, 1, fp) != 1)
            {
                break;
            }
        }
        
        if ((marker[1] >= 0xC0 && marker[1] <= 0xCF) &&
            marker[1] != 0xC4 && marker[1] != 0xC8 && marker[1] != 0xCC)
        {
            fseek(fp, 3, SEEK_CUR);

            UINT8 size_bytes[4];
            if (fread(size_bytes, 1, 4, fp) != 4)
            {
                break;
            }
            *height = (size_bytes[0] << 8) + size_bytes[1];
            *width  = (size_bytes[2] << 8) + size_bytes[3];
            fclose(fp);
            return 0;
        }
        else
        {
            UINT8 len_bytes[2];
            if (fread(len_bytes, 1, 2, fp) != 2)
            {
                break;
            }
            
            INT32 length = (len_bytes[0] << 8) + len_bytes[1];

            
            if (fseek(fp, length - 2, SEEK_CUR) != 0)
            {
                break;
            }
        }
    }

    fclose(fp);
    return -1;
}


void send_image(INT32 socket, const UINT8 *filename)
{
    FILE *fp = fopen((const char *)filename, "rb");
    if (fp == NULL)
    {
        perror("Could not open the file!\n");
        return;
    }

    fseek(fp, 0, SEEK_END);
    INT32 file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    int width = 0, height = 0;
    if (read_jpeg_dimensions((const char *)filename, &width, &height) != 0)
    {
        printf("Failed to read image dimensions.\n");
        fclose(fp);
        return;
    }
    
    UINT8 header[512];
    snprintf((char *)header, sizeof(header), "/i %s %d %d %d", filename, file_size, width, height);
    if (send(socket, header, strlen((char *)header), 0) == -1)
    {
        perror("Image header send error\n");
        fclose(fp);
        return;
    }

    Sleep(10);

    UINT8 buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0)
    {
        if (send(socket, buffer, bytes_read, 0) == -1)
        {
            perror("Failed to send image!");
            break;
        }
    }

    fclose(fp);
    printf("Successfully sent the picture %s (%d bytes, %d x %d).\n", filename, file_size, width, height);
}
