/**
 * @file 
 * @brief C Code-Q3 server
 * 
 * This is a chatroom server.
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


typedef unsigned char   UINT8;      // 1 byte 
typedef unsigned int    UINT32;     // 4 byte 
typedef int             INT32;      // 4 byte 

#define MAX_CLIENTS 5
#define BUFFER_SIZE 1024
#define NAME_SIZE 10
#define PORT 3532


typedef struct {
    INT32 socket;
    UINT8 nickname[NAME_SIZE];
} clientInfo_t;

clientInfo_t clients[MAX_CLIENTS];
INT32 client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_client(void *arg);
void broadcast_message(const UINT8 *message, INT32 sender_socket);
void broadcast_image_header(const UINT8 *filename, INT32 size, INT32 sender_socket, const UINT8 *nickname, UINT32 width, UINT32 height);
void broadcast_image_data(const UINT8 *data, size_t data_len, INT32 sender_socket);
INT32 add_client(INT32 socket);
void remove_client(INT32 socket);

int main()
{
    INT32 server_fd;
    INT32 client_fd;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t thread_id;

    printf("Initializing...\n");
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        perror("Could not create socket!");
        exit(EXIT_FAILURE);
    }


    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    INT32 bind_rtnval = bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (bind_rtnval == -1)
    {
        perror("Bind failed!");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    INT32 listen_rtnval = listen(server_fd, MAX_CLIENTS);
    if (listen_rtnval == -1)
    {
        perror("Listen failed!");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Waiting for incoming connections...\n");

    while (1)
    {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1)
        {
            perror("Accept failed!");
            continue;
        }

        printf("New client connection!\n");

        INT32 thread_rtnval = pthread_create(&thread_id, NULL, handle_client, (void *)&client_fd);
        if (thread_rtnval < 0)
        {
            perror("Create thread error!");
            close(client_fd);
            continue;
        }
        
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}

void *handle_client(void *arg)
{
    INT32 client_socket = *(INT32 *)arg;
    UINT8 buffer[BUFFER_SIZE];
    UINT8 nickname[NAME_SIZE];
    INT32 bytes_received;

    
    if ((bytes_received = recv(client_socket, nickname, NAME_SIZE - 1, 0)) > 0)
    {
        nickname[bytes_received] = '\0';
        printf("New client %s joined the chatroom!\n", nickname);

        pthread_mutex_lock(&clients_mutex);
        if (add_client(client_socket))
        {
            strncpy(clients[client_count - 1].nickname, nickname, NAME_SIZE - 1);
        }
        else
        {
            UINT8 *msg = "Max clients reached. Connection refused.\n";
            send(client_socket, msg, strlen(msg), 0);
            close(client_socket);
            pthread_mutex_unlock(&clients_mutex);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&clients_mutex);

        
        UINT8 join_msg[BUFFER_SIZE];
        snprintf(join_msg, BUFFER_SIZE, "%s joined the chatroom\n", nickname);
        broadcast_message(join_msg, client_socket);

        
        while ((bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0)) > 0)
        {
            buffer[bytes_received] = '\0';
            
            if (strncmp(buffer, "/i ", 3) == 0)
            {
                UINT8 filename[256];
                UINT32 image_size;
                UINT32 width;
                UINT32 height;
                if (sscanf(buffer, "/i %s %d %d %d", filename, &image_size, &width, &height) == 4)
                {
                    printf("Received image transfer request from %s: File name = %s, size = %d(%d * %d)\n", nickname, filename, image_size, width, height);
                    broadcast_image_header(filename, image_size, client_socket, nickname, width, height);

                    UINT8 *image_data = (UINT8 *)malloc(image_size);
                    if (image_data == NULL)
                    {
                        perror("Malloc failed!");
                        continue;
                    }

                    INT32 total_received = 0;
                    INT32 received;
                    while (total_received < image_size)
                    {
                        received = recv(client_socket, image_data + total_received, image_size - total_received, 0);
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
                        printf("Successfully received image from %s: %s (%d bytes)\n", nickname, filename, image_size);
                        broadcast_image_data(image_data, image_size, client_socket);
                    }
                    free(image_data);
                }
                else
                {
                    UINT8 *error_msg = "Format is incorrect!\n";
                    send(client_socket, error_msg, strlen(error_msg), 0);
                }
            }
            else
            {                
                UINT8 message[BUFFER_SIZE + NAME_SIZE + 2];
                snprintf(message, sizeof(message), "%s: %s\n", nickname, buffer);
                broadcast_message(message, client_socket);
            }
        }


        printf("Client %s is offline.\n", nickname);
        UINT8 leave_msg[BUFFER_SIZE + NAME_SIZE + 2];
        snprintf(leave_msg, BUFFER_SIZE, "%s left the chatroom\n", nickname);
        broadcast_message(leave_msg, client_socket);

        pthread_mutex_lock(&clients_mutex);
        remove_client(client_socket);
        pthread_mutex_unlock(&clients_mutex);
    }
    else if (bytes_received == 0)
    {
        printf("Client %d is offline.\n", client_socket);
        pthread_mutex_lock(&clients_mutex);
        remove_client(client_socket);
        pthread_mutex_unlock(&clients_mutex);
    }
    else
    {
        perror("Could not receive client's name.");
    }

    close(client_socket);
    pthread_exit(NULL);
}

void broadcast_message(const UINT8 *message, INT32 sender_socket)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i].socket != sender_socket)
        {
            if (send(clients[i].socket, message, strlen(message), 0) == -1)
            {
                perror("Could not send message!");
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void broadcast_image_header(const UINT8 *filename, INT32 size, INT32 sender_socket,
                                    const UINT8 *nickname, UINT32 width, UINT32 height)
{
    pthread_mutex_lock(&clients_mutex);
    UINT8 header[BUFFER_SIZE];
    snprintf(header, BUFFER_SIZE, "/i %s %s %d %d %d\n", nickname, filename, size, width, height);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i].socket != sender_socket)
        {
            if (send(clients[i].socket, header, strlen(header), 0) == -1)
            {
                perror("Image header error!");
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void broadcast_image_data(const UINT8 *data, size_t data_len, INT32 sender_socket)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i].socket != sender_socket)
        {
            ssize_t bytes_sent = 0;
            while (bytes_sent < data_len)
            {
                ssize_t sent = send(clients[i].socket, data + bytes_sent, data_len - bytes_sent, 0);
                if (sent == -1)
                {
                    perror("Image error!");
                    break;
                }
                bytes_sent += sent;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

INT32 add_client(INT32 socket)
{
    if (client_count < MAX_CLIENTS)
    {
        clients[client_count].socket = socket;
        client_count++;
        return 1;
    }
    return 0;
}

void remove_client(INT32 socket)
{
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i].socket == socket)
        {
            for (int j = i; j < client_count - 1; j++)
            {
                clients[j] = clients[j + 1];
            }
            client_count--;
            break;
        }
    }
}
