#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 512
#define MSGSIZE 1024

#define COD_HELLO_MSG 220
#define COD_GOODBYE 221 
#define COD_USR_EXISTS 331
#define COD_PASS_OK 230
#define COD_FILE_EXISTS 299
#define COD_TRANSFER_OK 226

/**
 * function: receive and analize the answer from the server
 * sd: socket descriptor
 * code: three leter numerical code to check if received
 * text: normally NULL but if a pointer if received as parameter
 *       then a copy of the optional message from the response
 *       is copied
 * return: result of code checking
 **/
bool recv_msg(int sd, int code, char *text) {
    char buffer[BUFSIZE], message[BUFSIZE];
    int recv_s, recv_code;

    // receive the answer
    recv_s = recv(sd, buffer, BUFSIZE, 0);

    // error checking
    if (recv_s < 0) warn("error receiving data");
    if (recv_s == 0) errx(1, "connection closed by host");

    // parsing the code and message receive from the answer
    sscanf(buffer, "%d %[^\r\n]\r\n", &recv_code, message);
    //printf("%d %s\n", recv_code, message);
    // optional copy of parameters
    if(text) strcpy(text, message);
    // boolean test for the code
    return (code == recv_code) ? true : false;
}

/**
 * function: send command formated to the server
 * sd: socket descriptor
 * operation: four letters command
 * param: command parameters
 **/
void send_msg(int sd, char *operation, char *param) {
    char buffer[BUFSIZE] = "";

    int send_s; 

    // command formating
    if (param != NULL)
        sprintf(buffer, "%s %s\r\n", operation, param);
    else
        sprintf(buffer, "%s\r\n", operation);

    // send command and check for errors
    send_s = send(sd, buffer,sizeof(buffer),0);
    if (send_s < 0) warn("error sending data");
}

/**
 * function: simple input from keyboard
 * return: input without ENTER key
 **/
char * read_input() {
    char *input = malloc(BUFSIZE);
    if (fgets(input, BUFSIZE, stdin)) {
        return strtok(input, "\n");
    }
    return NULL;
}

int setSocketData(int sd){
    
    int data_fd;
    struct sockaddr_in socketData;
    socklen_t dataLen = sizeof(socketData);
    char portArgs[BUFSIZE];

    data_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(data_fd < 0 ){
        errx(1, "Error abriendo socket.");
    }

    socketData.sin_family = AF_INET; //IPv4
    socketData.sin_addr.s_addr = htonl(INADDR_ANY);
    socketData.sin_port = htons(0); //Puerto "al azar"
    if (bind(data_fd, (struct sockaddr*)&socketData, sizeof(socketData)) < 0) {
        errx(1, "Error tratando de bindear socket");
    }

    if (getsockname(data_fd, (struct sockaddr *)&socketData, &dataLen) < 0) {
        close(data_fd);
        errx(1, "Error tratando de obtener informacion para setear al socket");
    }

    //Conversión de endianes para portabilidad
    uint32_t ip_addr = ntohl(socketData.sin_addr.s_addr);
    unsigned char ip[4];
    ip[0] = (ip_addr >> 24) & 0xFF;
    ip[1] = (ip_addr >> 16) & 0xFF;
    ip[2] = (ip_addr >> 8) & 0xFF;
    ip[3] = ip_addr & 0xFF;
    uint16_t portNum = ntohs(socketData.sin_port);

    //Impresion de la direccion por octetos y los valores de puerto segun convencion
    sprintf(portArgs, "%d,%d,%d,%d,%d,%d", ip[0], ip[1], ip[2], ip[3], portNum/256, portNum %256);

    if(listen(data_fd, 1) < 0){
        close(data_fd);
        errx(1, "Error tratando de escuchar por el socket\n");
    }

    send_msg(sd,"PORT", portArgs);

    return data_fd;
}

/**
 * function: login process from the client side
 * sd: socket descriptor
 **/
void authenticate(int sd) {
    char *input, desc[BUFSIZE];
    int code;

    bool bandera = false;

    while(!bandera) {
        // ask for user
        printf("username: ");
        input = read_input();

        // send the command to the server
        send_msg(sd, "USER", input);

        // release memory
        free(input);

        // wait to receive password requirement and check for errors
        if(recv_msg(sd, COD_USR_EXISTS, desc)){
            bandera = true;
        } else {
            errx(1, "No se pudo leer el nombre de usuario.");
        }
    }

    bandera = false;

    while(!bandera){
        // ask for password
        printf("passwd: ");
        input = read_input();

        // send the command to the server
        send_msg(sd, "PASS", input);

        // release memory
        free(input);

        // wait for answer and process it and check for errors
        if(recv_msg(sd, COD_PASS_OK, desc)){
            bandera = true;
        } else {
            errx(1, "No se pudo leer la password");
        }
        //Printf no esta unicamente para debugging, tiene un mensaje piola
        printf("%s\n", desc);
    }
}

void recv_file(int serverDataDescriptor, FILE* file, long int fSize){
    char buffer[BUFSIZE];
    int bufLen;
    long int received = 0;
    for(; (bufLen = recv(serverDataDescriptor, buffer, BUFSIZE, 0)) && received <= fSize; received += bufLen){
        if(bufLen == -1){
            warn("Error tratando de obtener archivo desde el servidor");
            break;
        }
        fwrite(buffer, sizeof(char), bufLen, file);
    }
    close(serverDataDescriptor);
}

/**
 * function: operation get
 * sd: socket descriptor
 * file_name: file name to get from the server
 **/
void get(int sd, char *file_name) {
    char desc[BUFSIZE], buffer[BUFSIZE], text[MSGSIZE];
    long int fileSize;
    FILE *file;

    //Obtenemos el fd del socket por el que vamos a recibir la transferencia
    int data_fd = setSocketData(sd);

    // send the RETR command to the server
    send_msg(sd, "RETR", file_name);

    // check for the response
    if(!recv_msg(sd, COD_FILE_EXISTS, buffer)){
        warn(buffer);
        return;
    }

    // parsing the file size from the answer received
    // "File %s size %ld bytes"
    sscanf(buffer, "File %*s size %ld bytes", &fileSize);

    // open the file to write
    file = fopen(file_name, "w");

    //receive the file
    struct sockaddr_in serverData;
    socklen_t serverDataLen = sizeof(serverData);
    int serverDataDescriptor = accept(data_fd, (struct sockaddr *) &serverData, &serverDataLen);

    recv_file(serverDataDescriptor, file, fileSize);

    // close the file y cerrar datafd
    fclose(file);
    close(data_fd);

    // receive the OK from the server
    if(!recv_msg(sd, COD_TRANSFER_OK, text)) {
        warn(text);
    } else {
        printf("%s\n", text);
    } 
}

/**
 * function: operation quit
 * sd: socket descriptor
 **/
void quit(int sd) {

    // send command QUIT to the client
    //*Quiso decir server?
    //O from the client

    char text[BUFSIZE];
    send_msg(sd, "QUIT", NULL);

    // receive the answer from the server
    if(recv_msg(sd, COD_GOODBYE, text)){
        printf("%s\n", text); 
    } else {
        perror(text);
    }
}

/**
 * function: make all operations (get|quit)
 * sd: socket descriptor
 **/
void operate(int sd) {
    char *input, *op, *param;

    while (true) {
        printf("Operation: ");
        input = read_input();
        if (input == NULL)
            continue; // avoid empty input
        op = strtok(input, " ");
        // free(input);
        if (strcmp(op, "get") == 0) {
            param = strtok(NULL, " ");
            get(sd, param);
        }
        else if (strcmp(op, "quit") == 0) {
            quit(sd);
            break;
        } else {
            // new operations in the future
            printf("TODO: unexpected command\n");
        }
        free(input);
    }
    free(input);
}

/**
 * Run with
 *         ./myftp <SERVER_IP> <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {
    int sd;
    char hello_msg[BUFSIZE];
    struct sockaddr_in addr;

    // arguments checking
    if(argc != 3){
        perror("Cantidad incorrecta de argumentos.\nChau\n");
        return -1;
    }

    // create socket and check for errors
    sd = socket(PF_INET, SOCK_STREAM, 0);
    if(sd == -1){
        perror("No se pudo crear el socket\n");
    } 

    // set socket data   
    // TODO: Revisar esto.
    addr.sin_family = AF_INET; //IPv4
    addr.sin_port = htons(atoi(argv[2]));
    if(inet_pton(AF_INET, argv[1], &(addr.sin_addr)) <= 0){
        perror("Ocurrio un problema parseando la IP.");
        return -1;
    };
    memset(&(addr.sin_zero), 0, 8);
    bind(sd, (struct sockaddr *)&addr, sizeof(addr));
 
    // connect and check for errors
    
    if(connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        perror("Algo salio mal al conectar.");
        close(sd);
        return -1;
    }

    // if receive hello proceed with authenticate and operate if not warning
    char respuesta[BUFSIZE];  

    if(recv_msg(sd, COD_HELLO_MSG, respuesta)){
        //Si recibimos codigo hello todo bien. Mostramos respuesta
        printf("Respuesta: %s", respuesta);
    } else {
        //Si no, todo mal.
        close(sd);
        perror("Fallo la conexion con el servidor. Chau.");
        return -1;
    }

    authenticate(sd);
    operate(sd);

    // close socket
    close(sd);
    return 0;
}
