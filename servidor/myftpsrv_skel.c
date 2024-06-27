#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <err.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 512
#define CMDSIZE 4
#define PARSIZE 100

#define MSG_220 "220 srvFtp version 1.0\r\n"
#define MSG_331 "331 Password required for %s\r\n"
#define MSG_230 "230 User %s logged in\r\n"
#define MSG_530 "530 Login incorrect\r\n"
#define MSG_221 "221 Goodbye\r\n"
#define MSG_550 "550 %s: no such file or directory\r\n"
#define MSG_299 "299 File %s size %ld bytes\r\n"
#define MSG_226 "226 Transfer complete\r\n"

struct sockaddr_in data_stream;


void setSocketData(char* info){
    int p1, p2, h[4];
    char client_ip[16];

    // Leer la informacion proporcionada por el cliente
    sscanf(info, "%d,%d,%d,%d,%d,%d", &h[0],&h[1],&h[2],&h[3],&p1,&p2);
    sprintf(client_ip, "%d.%d.%d.%d", h[0], h[1], h[2], h[3]);

    //La transformo hacia mi estructura
    data_stream.sin_family = AF_INET;
    inet_pton(AF_INET,client_ip, &data_stream.sin_addr);
    data_stream.sin_port = htons(p1 * 256 + p2);
}

/**
 * function: gets the socket that recieves the file to transfer
 * return: socket to client data stream
 */
int getDataDescriptor(){
    int dataDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if(dataDescriptor < 0){
        warn("No se pudo abrir socket");
        return -1;
    }
    if (connect(dataDescriptor, (struct sockaddr*)&data_stream, sizeof(data_stream)) < 0) {
        warn("Error al conectar");
        close(dataDescriptor);
        return -1;
    }
    return dataDescriptor;
}


/**
 * function: receive the commands from the client
 * sd: socket descriptor
 * operation: \0 if you want to know the operation received
 *            OP if you want to check an especific operation
 *            ex: recv_cmd(sd, "USER", param)
 * param: parameters for the operation involve
 * return: only usefull if you want to check an operation
 *         ex: for login you need the seq USER PASS
 *             you can check if you receive first USER
 *             and then check if you receive PASS
 **/
bool recv_cmd(int sd, char *operation, char *param) {
    char buffer[BUFSIZE], *token;
    int recv_s;

    // receive the command in the buffer and check for errors
    recv_s = recv(sd, buffer , BUFSIZE, 0);

    if (recv_s < 0) warn("error receiving data");
    if (recv_s == 0) errx(1, "connection closed by host");

    // expunge the terminator characters from the buffer
    buffer[strcspn(buffer, "\r\n")] = '\0';

    // complex parsing of the buffer
    // extract command receive in operation if not set \0
    // extract parameters of the operation in param if it needed
    token = strtok(buffer, " ");
    if (token == NULL || strlen(token) < 4) {
        warn("not valid ftp command");
        return false;
    } else {
        if (operation[0] == '\0') strcpy(operation, token);
        if (strcmp(operation, token)) {
            warn("abnormal client flow: did not send %s command", operation);
            return false;
        }
        token = strtok(NULL, " ");
        if (token != NULL) strcpy(param, token);
    }
    return true;
}

/**
 * function: sends the file to the client
 * dd: data stream descriptor
 * file: file to transfer
 */
void send_file(int dd, FILE *file){
    char bread[BUFSIZE];
    int readed;
    while((readed = fread(&bread, sizeof(char), BUFSIZE, file)) == BUFSIZE){
        send(dd, bread, readed, 0);
    }
    if(feof(file)){
        send(dd, bread, readed, 0);
    }
}

/**
 * function: send answer to the client
 * sd: file descriptor
 * message: formatting string in printf format
 * ...: variable arguments for economics of formats
 * return: true if not problem arise or else
 * notes: the MSG_x have preformated for these use
 **/
bool send_ans(int sd, char *message, ...){
    char buffer[BUFSIZE];

    va_list args;
    va_start(args, message);

    vsprintf(buffer, message, args);
    va_end(args);
    // send answer preformated and check errors
    int send_s = send(sd,buffer, sizeof buffer, 0);
    if(send_s < 0){
        warn("Error while sending Data");
        return false;
    }
    return true;
}

/**
 * function: RETR operation
 * sd: socket descriptor
 * file_path: name of the RETR file
 **/
void retr(int sd, char *file_path) {
    FILE *file;    
    long fileSize;
    char buffer[BUFSIZE];

    //Open File
    file = fopen(file_path, "r");

    // check if file exists if not inform error to client
    if(!file){
        send_ans(sd, MSG_530, file_path);
        return;
    }

    // send a success message with the file length
    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    send_ans(sd, MSG_299, file_path, fileSize);

    // important delay for avoid problems with buffer size
    sleep(1);

    // obtener descriptor de la conexion
    int dataDescriptor = getDataDescriptor();

    if(dataDescriptor < 0){
        fclose(file);
        return;
    }

    // send the file
    send_file(dataDescriptor, file);
    
    // close the file
    fclose(file);

    // send a completed transfer message
    send_ans(sd, MSG_226);
    close(dataDescriptor);
}
/**
 * funcion: check valid credentials in ftpusers file
 * user: login user name
 * pass: user password
 * return: true if found or false if not
 **/
bool check_credentials(char *user, char *pass) {
    FILE *file;
    //En ftpusers (que debe estar en el mismo directorio que el servidor)
    //deben figurar "usuario:contraseñas" aceptadas
    char *path = "./ftpusers", *line = NULL, credentials[100];
    size_t line_size = 0;
    bool found = false;

    // make the credential string
    sprintf(credentials, "%s:%s", user, pass);

    // check if ftpusers file it's present
    if ((file = fopen(path, "r"))==NULL) {
        warn("Error opening %s", path);
        return false;
    }

    // search for credential string
    while (getline(&line, &line_size, file) != -1) {
        strtok(line, "\n");
        if (strcmp(line, credentials) == 0) {
            found = true;
            break;
        }
    }

    // close file and release any pointers if necessary
    fclose(file);
    if (line) free(line);

    // return search status
    return found;
}

/**
 * function: login process management
 * sd: socket descriptor
 * return: true if login is succesfully, false if not
 **/
bool authenticate(int sd) {
    char user[PARSIZE], pass[PARSIZE];

    // wait to receive USER action
    recv_cmd(sd, "USER", user);

    // ask for password
    send_ans(sd, MSG_331, user);

    // wait to receive PASS action
    recv_cmd(sd, "PASS", pass);

    // if credentials don't check denied login
    if(!check_credentials(user, pass)){
        send_ans(sd, MSG_530);
        return false;
    }

    // confirm login
    send_ans(sd, MSG_230, user);
    return true;
}

/**
 *  function: execute all commands (RETR|QUIT)
 *  sd: socket descriptor
 **/

void operate(int sd) {
    char op[CMDSIZE], param[PARSIZE];

    while (true) {
        op[0] = param[0] = '\0';
        // check for commands send by the client if not inform and exit
        if(!recv_cmd(sd, op, param)){
                errx(1,"Didnt recieve command");
        }
        if (strcmp(op, "PORT") == 0) {
            setSocketData(param);
        } else if (strcmp(op, "RETR") == 0) {
            retr(sd, param);
        } else if (strcmp(op, "QUIT") == 0) {
            // send goodbye and close connection
            send_ans(sd,MSG_221);
            close(sd);
            break;
        } else {
            // invalid command
            // furute use
        }
    }
}

/**
 * Run with
 *         ./mysrv <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {

    // arguments checking
    if (argc < 2) {
        errx(1, "Port expected as argument");
    } else if (argc > 2) {
        errx(1, "Too many arguments");
    }

    // reserve sockets and variables space
    int master_sd, slave_sd, status;
    struct sockaddr_in slave_addr, *master_addr;
    struct addrinfo hints, *servinfo;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if((status = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }

    // create server socket and check errors
    master_addr =(struct sockaddr_in*) servinfo->ai_addr;

    master_sd = socket(PF_INET, SOCK_STREAM, 0);
    if(master_sd == -1) {
        perror("Failed opening socket\n");
        return -1;
    }

    // bind master socket and check errors
    if(bind(master_sd, (struct sockaddr *)master_addr, servinfo->ai_addrlen) < 0){
        close(master_sd);
        errx(1, "Failed binding master socket");
    }

    // make it listen
    if(listen(master_sd, 5) < 0){
        close(master_sd);
        errx(1, "Failed while listening to the socket.\n");
    }

    // main loop
    while (true) {
        // accept connectiones sequentially and check errors
        socklen_t slave_len = sizeof(slave_addr);
        slave_sd = accept(master_sd, (struct sockaddr *) &slave_addr, &slave_len);
        if(!fork()){
            close(master_sd);

            // send hello
            send_ans(slave_sd, MSG_220);

            // operate only if authenticate is true
            if(authenticate(slave_sd)){
                operate(slave_sd);
            } else {
                close(slave_sd);
                }
                return 0;
            }
        }
    // close server socket
    freeaddrinfo(servinfo);
    close(master_sd);
    return 0;
}
