#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/wait.h>


#define BUF_SIZE 1024
#define ADD_SIZE 60
#define MAX_CLIENTS 10
#define UNKNOWN_OPTION_MESSAGE_LEN 24
#define BASE_TEN 10
#define PORT_INFO 100

struct ThreadArgs
{
    int *clnt_sock_ptr;
    pthread_mutex_t *mutex;
    int *clnt_socks;
};

struct ClientInfo {
    char username[BUF_SIZE];
    char ip_address[INET6_ADDRSTRLEN];
    in_port_t port;
};


struct PortData {
    int client[10];
    char message[MAX_CLIENTS][BUF_SIZE];
};

struct ClientThreadArgs {
    int sockfd;
    struct PortData *data;
};


static void parse_arguments(int argc, char *argv[], char **address, char **port);
static void handle_arguments(const char *binary_name, const char *address, const char *port_str, in_port_t *port);
static in_port_t parse_in_port_t(const char *binary_name, const char *port_str);
static void convert_address(const char *address, struct sockaddr_storage *addr);
static int socket_create(int domain, int type, int protocol);
static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void start_listening(int server_fd, int backlog);
_Noreturn static void usage(const char *program_name, int exit_code, const char *message);
void read_childproc(int sig);
void *handle_client(void *arg);
static void setup_signal_handler(void);
static void          *receive_messages(void *arg);
// P2P 채팅 서버의 메인 루프
void run_server(int sockfd,  char *user_name);
void send_socket_info_all(struct ClientInfo *clients, int *clnt_socks, pthread_mutex_t *mutex,int current_client_index);

// P2P
void run_client(int sockfd, char *user_name, const char *file_name, int client_PORT, int cltser_soc);
void *server_input_handler(void *arg);
void *p2preceive_thread(void *server_fd);
void p2preceivemsg(int server_fd);
void p2psend(char *name,  int PORT);


int main(int argc, char *argv[]) {
    char *ip_address;
    char *port_str;
    in_port_t port;
    struct sockaddr_storage addr;
    int sockfd;
    char user_name[BUF_SIZE];

    struct PortData portinfo;
    const char *file_name = (argc == 5) ? argv[4] : NULL;

    pthread_t thread;
    ip_address = NULL;
    port_str = NULL;

    parse_arguments(argc, argv, &ip_address, &port_str);
    handle_arguments(argv[0], ip_address, port_str, &port);
    convert_address(ip_address, &addr);

    sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);




    if (strcmp(argv[1], "-a") == 0) {
        // 서버 모드
        socket_bind(sockfd, &addr, port);
        start_listening(sockfd, MAX_CLIENTS);
        puts("A chat room has been opened..");

        run_server(sockfd, user_name);
    } else if (strcmp(argv[1], "-c") == 0) {


        //---------------------------p2p server ---------------------------------------------


        int client_PORT;

        int server_fd;
        struct sockaddr_in address;

        // Creating socket file descriptor
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("socket failed");
            exit(EXIT_FAILURE);
        }
        // Forcefully attaching socket to the port

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(0);


        if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
            perror("bind failed");
            exit(EXIT_FAILURE);
        }

        // Retrieve the assigned port
        socklen_t len = sizeof(address);
        if (getsockname(server_fd, (struct sockaddr *) &address, &len) == -1) {
            perror("getsockname failed");
            exit(EXIT_FAILURE);
        }


        client_PORT = ntohs(address.sin_port);



        printf("Enter your name: ");
        fflush(stdout);
        fgets(user_name, sizeof(user_name), stdin);


        user_name[strcspn(user_name, "\n")] = 0;
        socket_connect(sockfd, &addr, port);
        run_client(sockfd, user_name, file_name, client_PORT, server_fd);




    }

    // 소켓 닫기
    close(sockfd);
    return 0;
}

static void parse_arguments(int argc, char *argv[], char **ip_address, char **port)
{
    int opt;

    opterr = 0;

    while ((opt = getopt(argc, argv, "ach")) != -1)
    {

        switch (opt)
        {
            case 'a':
            {
                break;
            }
            case 'c':
            {
                break;
            }
            case 'h':
            {
                usage(argv[0], EXIT_SUCCESS, NULL);
            }
            case '?':
            {
                char message[UNKNOWN_OPTION_MESSAGE_LEN];

                snprintf(message, sizeof(message), "Unknown option '-%c'.", optopt);
                usage(argv[0], EXIT_FAILURE, message);
            }
            default:
            {
                usage(argv[0], EXIT_FAILURE, NULL);
            }
        }

    }

    if (optind >= argc)
    {
        usage(argv[0], EXIT_FAILURE, "The IP address or hostname is required.");
    }

    if (optind < argc - 3)
    {
        usage(argv[0], EXIT_FAILURE, "Too many arguments.");
    }

    *ip_address = argv[optind];
    *port = argv[optind + 1];
}

static void handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, in_port_t *port)
{
    if (ip_address == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The IP address is required.");
    }

    if (port_str == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "Port argument is missing.");
    }

    *port = parse_in_port_t(binary_name, port_str);
}

in_port_t parse_in_port_t(const char *binary_name, const char *str)
{
    char *endptr;
    uintmax_t parsed_value;

    errno = 0;
    parsed_value = strtoumax(str, &endptr, BASE_TEN);

    if (errno != 0)
    {
        perror("Error parsing in_port_t");
        exit(EXIT_FAILURE);
    }

    if (*endptr != '\0')
    {
        usage(binary_name, EXIT_FAILURE, "Invalid characters in input.");
    }

    if (parsed_value > UINT16_MAX)
    {
        usage(binary_name, EXIT_FAILURE, "in_port_t value out of range.");
    }

    return (in_port_t)parsed_value;
}

static void convert_address(const char *address, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));

    if (inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        addr->ss_family = AF_INET;
    }
    else if (inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        addr->ss_family = AF_INET6;
    }
    else
    {
        fprintf(stderr, "%s is not an IPv4 or an IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

static int socket_create(int domain, int type, int protocol)
{
    int sockfd;

    sockfd = socket(domain, type, protocol);

    if (sockfd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char addr_str[INET6_ADDRSTRLEN];
    in_port_t net_port;
    socklen_t addr_len;

    if (inet_ntop(addr->ss_family, addr->ss_family == AF_INET ? (void *)&(((struct sockaddr_in *)addr)->sin_addr) : (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr), addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Connecting to: %s:%u\n", addr_str, port);
    net_port = htons(port);

    if (addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr = (struct sockaddr_in *)addr;
        ipv4_addr->sin_port = net_port;
        addr_len = sizeof(struct sockaddr_in);
    }
    else if (addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr = (struct sockaddr_in6 *)addr;
        ipv6_addr->sin6_port = net_port;
        addr_len = sizeof(struct sockaddr_in6);
    }
    else
    {
        fprintf(stderr, "Invalid address family: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        char *msg;
        msg = strerror(errno);
        fprintf(stderr, "Error: connect (%d): %s\n", errno, msg);
        exit(EXIT_FAILURE);
    }

    printf("Connected to: %s:%u\n", addr_str, port);
}


static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];
    socklen_t addr_len;
    void     *vaddr;
    in_port_t net_port;

    net_port = htons(port);

    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        addr_len            = sizeof(*ipv4_addr);
        ipv4_addr->sin_port = net_port;
        vaddr               = (void *)&(((struct sockaddr_in *)addr)->sin_addr);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        addr_len             = sizeof(*ipv6_addr);
        ipv6_addr->sin6_port = net_port;
        vaddr                = (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr);
    }
    else
    {
        fprintf(stderr, "Internal error: addr->ss_family must be AF_INET or AF_INET6, was: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(inet_ntop(addr->ss_family, vaddr, addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Binding to: %s:%u\n", addr_str, port);

    if(bind(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        perror("Binding failed");
        fprintf(stderr, "Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    printf("Bound to socket: %s:%u\n", addr_str, port);
}

static void start_listening(int server_fd, int backlog)
{
    if(listen(server_fd, backlog) == -1)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening for incoming connections...\n");
}

_Noreturn static void usage(const char *program_name, int exit_code, const char *message)
{
    if(message)
    {
        fprintf(stderr, "%s\n", message);
    }

    fprintf(stderr, "Usage: %s [-h] <IP address or hostname> <port>\n", program_name);
    fputs("Options:\n", stderr);
    fputs("  -h  Display this help message\n", stderr);
    exit(exit_code);
}

void read_childproc(int sig)
{
    int   status;
    pid_t id = waitpid(-1, &status, WNOHANG);
    (void)sig;

    if(WIFEXITED(status))
    {
        printf("Removed proc id: %d\n", id);
        printf("Child send: %d \n", WEXITSTATUS(status));
    }
}


static void setup_signal_handler(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = read_childproc;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if(sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

static void *receive_messages(void *arg)
{
    struct ClientThreadArgs *args = (struct ClientThreadArgs *)arg;
    int sockfd = args->sockfd;  // Access sockfd from the structure
    struct PortData *data = args->data;

    char message[BUF_SIZE];


    while(1)
    {
        ssize_t str_len = read(sockfd, message, BUF_SIZE - 1);
        if(str_len <= 0)
        {
            perror("read() error");
            break;
        }
        // Use explicit type casting to int

        message[str_len] = 0;


        char *startPtr = strstr(message, "[user");
//        if (portPtr != NULL) {
//
//            char *token = strtok(portPtr, "\n");
//            int index = 0;
//
//
//            while (token != NULL && index < MAX_CLIENTS) {
//
//
//                // Use strcpy to copy the string into the array
//                strcpy(data->message[index], token);
//
//                token = strtok(NULL, "\n");
//                index++;
//            }
//        }

        if (startPtr != NULL) {
            // Find the starting position of the desired substring
            startPtr = strchr(startPtr, ' ');  // Move to the space after "[userX]"

            if (startPtr != NULL) {
                // Find the ending position of the desired substring
                char *endPtr = strchr(startPtr, ']');

                if (endPtr != NULL) {
                    // Calculate the length of the desired substring
                    size_t length = endPtr - startPtr + 2;

                    // Allocate memory for the substring
                    char substring[length];

                    // Copy the substring into the allocated memory
                    strncpy(substring, startPtr, length - 1);

                    // Null-terminate the substring
                    substring[length - 1] = '\0';

                    // Print or use the extracted substring as needed
                    //printf("Extracted substring: %s\n", substring);

                    for(int i; i < MAX_CLIENTS; i++){
                        if (strcmp(data->message[i], "") == 0) {
                            strcpy(data->message[i], substring);
                            break;
                        }
                    }
                }
            }
        }

        printf("\r");

        // Clear the entire line
        printf("\033[K");
//        for (int i = 0; i < MAX_CLIENTS; i++) {
//            printf("client %d: %s\n", i+1, data->message[i]);
//        }

        char *finduser = strstr(message, "user");
        if (finduser != NULL) {
            printf("\nInput message (Q to quit): ");
        }else {
            printf("\t\t\t\t\t\t\t %s \nInput message (Q to quit): ", message);
        }
        fflush(stdout);

    }
    return NULL;
}
void *handle_client(void *arg) {
    struct ThreadArgs *args = (struct ThreadArgs *)arg;
    int clnt_sock = *(args->clnt_sock_ptr);
    pthread_mutex_t *mutex = args->mutex;
    int *clnt_socks = args->clnt_socks;
    char buf[BUF_SIZE];
    char bufcopy[BUF_SIZE];
    char *full_user;
    size_t full_user_size;
    char input_message[2048];

    while (1) {
        // Initialize buf before each read
        memset(buf, 0, sizeof(buf));

        ssize_t str_len = read(clnt_sock, buf, sizeof(buf));
        memcpy(bufcopy, buf, str_len);
        if (str_len <= 0) {
            break;
        }

        // Check if the received message is "has joined the chat."
        if (strstr(bufcopy, "has left the chat.") != NULL) {
            // If the message indicates a new user joining, disable further messages from this client
            pthread_mutex_lock(mutex);
            for (int i = 0; i < MAX_CLIENTS; ++i) {
                if (clnt_socks[i] == clnt_sock) {
                    // Disable further messages from this client
                    clnt_socks[i] = 0;
                    break;
                }
            }
            pthread_mutex_unlock(mutex);

        }


        // Append the value of clnt_sock to buf
        snprintf(input_message, sizeof(input_message), "[user%d] %s", clnt_sock, buf);

        // Write the received message to all connected clients
        pthread_mutex_lock(mutex);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (clnt_socks[i] != 0) {
                // Ensure null-termination
                input_message[str_len + sizeof("current talk:")] = '\0';

                // Use send to send the null-terminated string
                if (send(clnt_socks[i], input_message, str_len + sizeof("current talk:"), 0) == -1) {
                    perror("send");
                    // Handle the error as needed
                }
            }
        }
        // Print the received message on the server side
        printf("\r");

        // Clear the entire line
        printf("\033[K");
        printf("current talk:%d", clnt_sock);
        printf("\t\t\t\t\t\t\t %s \nInput message (Q to quit): ", buf);
        fflush(stdout);
        pthread_mutex_unlock(mutex);
    }

    // Remove the disconnected client from the array
    pthread_mutex_lock(mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clnt_socks[i] == clnt_sock) {
            clnt_socks[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(mutex);
    // 메모리 해제
    free(full_user);
    close(clnt_sock);
    return NULL;
}

void *server_input_handler(void *arg) {

    char input_message[BUF_SIZE];

    struct ThreadArgs *args = (struct ThreadArgs *)arg;
    pthread_mutex_t *mutex = args->mutex;
    int *clnt_socks = args->clnt_socks;


    size_t full_message_size;
    char *full_message;

    while (1) {
        // Get server input
        printf("\rInput message (Q to quit): ");
        fflush(stdout);
        if (fgets(input_message, BUF_SIZE, stdin) == NULL) {
            perror("fgets");
            break;
        }
        if (!strcmp(input_message, "q\n") || !strcmp(input_message, "Q\n")) {
            char leave_message[ADD_SIZE + 2];
            snprintf(leave_message, sizeof(leave_message), "%s has left the chat.\n", "king of room");
            // Broadcast the server input to all clients
            pthread_mutex_lock(mutex);
            for (int i = 0; i < MAX_CLIENTS; ++i) {
                if (clnt_socks[i] != 0) {

                    write(clnt_socks[i], leave_message, strlen(leave_message));
                }
            }
            // Print the original prompt after broadcasting the server input

            pthread_mutex_unlock(mutex);

            exit(0);
        }

        full_message_size = strlen("server") + strlen(": ") + strlen(input_message) + 1;
        full_message = (char *)malloc(full_message_size);

        if (full_message == NULL) {
            perror("malloc() error");
            exit(EXIT_FAILURE);
        }

        snprintf(full_message, full_message_size, "%s: %s", "server", input_message);


        // Broadcast the server input to all clients
        pthread_mutex_lock(mutex);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (clnt_socks[i] != 0) {

                write(clnt_socks[i], full_message, strlen(full_message));
            }
        }
        // Print the original prompt after broadcasting the server input

        pthread_mutex_unlock(mutex);
        free(full_message);
    }

    return NULL;
}

// --server
void run_server(int sockfd, char *user_name) {
    int clnt_socks[MAX_CLIENTS];
    pthread_mutex_t mutex;
    struct ClientInfo clients[MAX_CLIENTS];

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        clnt_socks[i] = 0;
    }

    pthread_mutex_init(&mutex, NULL);
    setup_signal_handler();

    while (1) {
        struct sockaddr_storage clnt_addr;
        socklen_t clnt_addr_len = sizeof(clnt_addr);
        int *clnt_sock_ptr = (int *)malloc(sizeof(int));
        int userIndex = 0;
        char test[300];


        *clnt_sock_ptr = accept(sockfd, (struct sockaddr *)&clnt_addr, &clnt_addr_len);

        if (*clnt_sock_ptr == -1) {
            free(clnt_sock_ptr);
            continue;
        }
        // Print the value stored in *clnt_sock_ptr


        pthread_mutex_lock(&mutex);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (clnt_socks[i] == 0) {
                clnt_socks[i] = *clnt_sock_ptr;
                userIndex = i;
                break;
            }

        }
        pthread_mutex_unlock(&mutex);

        char client_addr_str[INET6_ADDRSTRLEN];
        in_port_t client_port;
        if (inet_ntop(clnt_addr.ss_family, clnt_addr.ss_family == AF_INET ? (void *)&(((struct sockaddr_in *)&clnt_addr)->sin_addr) : (void *)&(((struct sockaddr_in6 *)&clnt_addr)->sin6_addr), client_addr_str, sizeof(client_addr_str)) == NULL) {
            perror("inet_ntop");
            exit(EXIT_FAILURE);
        }

        if (clnt_addr.ss_family == AF_INET) {
            client_port = ntohs(((struct sockaddr_in *)&clnt_addr)->sin_port);
        } else if (clnt_addr.ss_family == AF_INET6) {
            client_port = ntohs(((struct sockaddr_in6 *)&clnt_addr)->sin6_port);
        } else {
            fprintf(stderr, "Invalid address family: %d\n", clnt_addr.ss_family);
            exit(EXIT_FAILURE);
        }


        printf("\r");
        // Clear the entire line
        printf("\033[K");
        printf("New client connected: %s:%d\n", client_addr_str, client_port);

        printf("Client socket value: %d\n", *clnt_sock_ptr);




//        // 클라이언트 정보 구조체에 저장
//
//        snprintf(clients[userIndex].username, sizeof(clients[userIndex].username), "User%d", *clnt_sock_ptr);
//        inet_ntop(clnt_addr.ss_family, (clnt_addr.ss_family == AF_INET) ? (void *)&(((struct sockaddr_in *)&clnt_addr)->sin_addr) : (void *)&(((struct sockaddr_in6 *)&clnt_addr)->sin6_addr), clients[userIndex].ip_address, sizeof(clients[userIndex].ip_address));
//        clients[userIndex].port = ntohs((clnt_addr.ss_family == AF_INET) ? ((struct sockaddr_in *)&clnt_addr)->sin_port : ((struct sockaddr_in6 *)&clnt_addr)->sin6_port);
//
//
//
//        // 클라이언트에게 정보 전송
//        send_socket_info_all(clients, clnt_socks, &mutex, userIndex);




        struct ThreadArgs *thread_args = (struct ThreadArgs *)malloc(sizeof(struct ThreadArgs));
        thread_args->clnt_sock_ptr = clnt_sock_ptr;
        thread_args->mutex = &mutex;
        thread_args->clnt_socks = clnt_socks;

        pthread_t server_input_thread;
        if (pthread_create(&server_input_thread, NULL, server_input_handler, (void *)thread_args) != 0) {
            perror("Thread creation failed");
            close(*clnt_sock_ptr);
            free(clnt_sock_ptr);
            free(thread_args);
            continue;
        }

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, (void *)thread_args) != 0) {
            perror("Thread creation failed");
            close(*clnt_sock_ptr);
            free(clnt_sock_ptr);
            free(thread_args);
            continue;
        }

        pthread_detach(thread);
    }
}
void send_socket_info_all(struct ClientInfo *clients, int *clnt_socks, pthread_mutex_t *mutex, int current_client_index) {
    char socket_str[1024];



    pthread_mutex_lock(mutex);
    // Iterate through all connected clients
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clnt_socks[i] != 0) {
            // Initialize the string with an empty message
            socket_str[0] = '\0';


            // Iterate again to append information about all clients
            for (int j = 0; j < MAX_CLIENTS; ++j) {
                if (clnt_socks[j] != 0) {
                    // Append client information to the string
                    snprintf(socket_str + strlen(socket_str), sizeof(socket_str) - strlen(socket_str),
                             "[%s]%s:%u\n", clients[j].username, clients[j].ip_address, clients[j].port);

                }
            }

            // Send the information to the client
            send(clnt_socks[i], socket_str, strlen(socket_str), 0);



            fflush(stdout);
        }
    }
    pthread_mutex_unlock(mutex);
    pthread_mutex_lock(mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        // Convert port number to string and send it separately
        if (clnt_socks[i] != 0) {
            char port_str[100];
            snprintf(port_str, sizeof(port_str), "%u", clients[i].port);
            send(clnt_socks[i], port_str, strlen(port_str), 0);
        }
    }
    pthread_mutex_unlock(mutex);
}

//---client
void run_client(int sockfd, char *user_name, const char *file_name, int client_PORT, int server_fd){
    pthread_t thread;
    char message[BUF_SIZE];
    char connection_message[ADD_SIZE + 2];

    struct PortData portData;  // Assuming you have an instance of PortData
    struct ClientThreadArgs clientThreadArgs = {sockfd, &portData};

    if (file_name != NULL) {
        // Set a default username for the file reading version
        snprintf(user_name, BUF_SIZE, "FileUser");

        FILE *file = fopen(file_name, "r");
        if (file == NULL) {
            perror("Error opening file");
            exit(EXIT_FAILURE);
        }

        while (fgets(message, sizeof(message), file) != NULL) {
            // Send each line from the file as a separate message
            write(sockfd, message, strlen(message));

            sleep(1);  // Adjust sleep duration if needed
        }

        fclose(file);

    } else {



        snprintf(connection_message, sizeof(connection_message), "%s[%d] has joined the chat.\n", user_name,client_PORT);
        write(sockfd, connection_message, strlen(connection_message));

        if (pthread_create(&thread, NULL, receive_messages, (void *)&clientThreadArgs) != 0) {
            perror("Thread creation failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }


        while (1) {
            size_t full_message_size;
            char *full_message;
            char leave_message[ADD_SIZE + 2];

            fputs("Input message (Q to quit): ", stdout);
            fgets(message, BUF_SIZE, stdin);


            if (!strcmp(message, "q\n") || !strcmp(message, "Q\n")) {

                snprintf(leave_message, sizeof(leave_message), "%s[%d] has left the chat.\n", user_name, client_PORT);
                printf("Client information:\n");
                for (int i = 0; i < MAX_CLIENTS; ++i) {
                    printf("Client %d: %s\n", i + 1, portData.message[i]);
                }
                write(sockfd, leave_message, strlen(leave_message));
                close(sockfd);
                exit(0);
            }
            if (!strcmp(message, "h\n") || !strcmp(message, "H\n")) {


                for (int i = 0; i < MAX_CLIENTS; ++i) {
                    printf("Client %d: %s\n", i + 1, portData.message[i]);
                }
                continue;
            }

            if (!strcmp(message, "z\n") || !strcmp(message, "Z\n")) {

                snprintf(leave_message, sizeof(leave_message), "%s[%d] has left the chat.\n", user_name, client_PORT);

                write(sockfd, leave_message, strlen(leave_message));

                close(sockfd);

                //Printed the server socket addr and port
                //printf("IP address is: %s\n", inet_ntoa(address.sin_addr));
                printf("port is: %u\n", client_PORT);

                if (listen(server_fd, 5) < 0) {
                    perror("listen");
                    exit(EXIT_FAILURE);
                }

                pthread_t tid;
                pthread_create(&tid, NULL, &p2preceive_thread,
                               &server_fd); //Creating thread to keep receiving message in real time

                while(1){

                    p2psend(user_name, client_PORT);

                }


                close(server_fd);


            }

            full_message_size = strlen(user_name) + strlen("[]")+ sizeof(client_PORT) + strlen(": ") + strlen(message) + 1;
            full_message = (char *) malloc(full_message_size);

            if (full_message == NULL) {
                perror("malloc() error");
                exit(EXIT_FAILURE);
            }


            snprintf(full_message, full_message_size, "%s[%d]: %s", user_name,client_PORT, message);
            write(sockfd, full_message, strlen(full_message));


            // Print the original prompt after broadcasting the server input



            free(full_message);

        }
    }

    pthread_join(thread, NULL);
}
//Sending messages to port
void p2psend(char *name,  int PORT)
{

    char message[2048] = {0};
    memset(message, 0, sizeof(message));
    //Fetching port number
    int PORT_server;

    //IN PEER WE TRUST

    printf("Enter the port to send message:"); //Considering each peer will enter different port
    scanf("%d", &PORT_server);

    int sock = 0, valread;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Socket creation error \n");
        return;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY; //INADDR_ANY always gives an IP of 0.0.0.0
    serv_addr.sin_port = htons(PORT_server);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("\nConnection Failed \n");
        return;
    }


    size_t full_message_size;
    char *full_message;

    printf("\r");

    // Clear the entire line
    printf("\033[K");
    printf("Input message (Q to quit): ");
    fgets(message, BUF_SIZE, stdin);

// Calculate the actual length of the port number when converting it to a string
    int port_length = snprintf(NULL, 0, "%d", PORT);
    full_message_size = strlen(name) + strlen("[PORT:") + port_length + strlen("]: ") + strlen(message) + 1;

    full_message = (char *)malloc(full_message_size);

    if (full_message == NULL) {
        perror("malloc() error");
        exit(EXIT_FAILURE);
    }

    // Use sprintf instead of snprintf to directly write into the allocated buffer
    sprintf(full_message, "%s[PORT:%d]: %s", name, PORT, message);


    // Use strlen(full_message) to get the actual length of the string
    send(sock, full_message, strlen(full_message), 0);

    close(sock);
    free(full_message);  // Don't forget to free the allocated memory


}
//Calling receiving every 2 seconds
void *p2preceive_thread(void *server_fd)
{
    int s_fd = *((int *)server_fd);
    while (1)
    {
        sleep(2);
        p2preceivemsg(s_fd);
    }
}

//Receiving messages on our port
void p2preceivemsg(int server_fd)
{
    struct sockaddr_in address;
    int valread;
    char buffer[2000] = {0};


    int addrlen = sizeof(address);
    fd_set current_sockets, ready_sockets;

    //Initialize my current set
    FD_ZERO(&current_sockets);
    FD_SET(server_fd, &current_sockets);
    int k = 0;
    while (1)
    {
        k++;
        ready_sockets = current_sockets;

        if (select(FD_SETSIZE, &ready_sockets, NULL, NULL, NULL) < 0)
        {
            perror("Error");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < FD_SETSIZE; i++)
        {
            if (FD_ISSET(i, &ready_sockets))
            {

                if (i == server_fd)
                {
                    int client_socket;

                    if ((client_socket = accept(server_fd, (struct sockaddr *)&address,
                                                (socklen_t *)&addrlen)) < 0)
                    {
                        perror("accept");
                        exit(EXIT_FAILURE);
                    }
                    FD_SET(client_socket, &current_sockets);
                }
                else
                {
                    memset(buffer, 0, sizeof(buffer));
                    valread = recv(i, buffer, sizeof(buffer), 0);
                    printf("\r");

                    // Clear the entire line
                    printf("\033[K");
                    printf("\t\t\t\t\t\t\t %s\nInput message (Q to quit):\"", buffer);
                    FD_CLR(i, &current_sockets);
                }
            }
        }

        if (k == (FD_SETSIZE * 2))
            break;
    }
}