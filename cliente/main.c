/*
  Guía de TP2
  Técnicas Digitales III Curso R5051 - 2018
  Eugenia Ipar

  Estación de Análisis - Implementada en PC

  Características:
        1. Utilizar una interfaz de red para comunicarse con la estación de adquisición utilizando un modelo cliente‐servidor IP. OK
        2. Actuar como cliente. OK
        3. Adquirir los siguientes parámetros (línea de comando o fichero de configuración) al momento de ser ejecutada
              * Direcciones IP de las estaciones de adquisición a supervisar OK
              * Periodicidad de encuesta. OK
        4. Visualizar la información de todas las estaciones de adquisición que se encuentren conectadas. OK
          En caso de que la conexión se interrumpa o no pueda establecerse, se deberá presentar un mensaje indicando dicha condición.
 */

/* Includes */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>

/* Defines */
#define DEBUG(x)        printf(x)
#define SERVER_PORT     10000
#define SEM_NAME        "sem_toprint"
#define SEM_AVAILABLE   1
#define SEM_UNAVAILABLE 0
#define CANT_SEM        1
#define NUM_SEM_TO_USE  0

/* Estructuras */
/* Global */

/* Funciones/Handlers */
/*  sigchld_handler
    Handler de la señal SIGCHLD
    La señal SIGCHLD se envía al proceso padre una vez que el hijo termina
    Se debe utilizar la función wait() para que termine adecuadamente, y no quede en ZOMBIEs
*/
void sigchld_handler(int s){
  //waitpid() puede sobreescribir errno, así que lo guardo y lo restoreo después
  int saved_errno = errno;

  fprintf(stdout, "client: child killed\n");    //Print así yo chequeo
  while(waitpid(-1, NULL, WNOHANG) > 0);        //Espero a recibir señal

  errno = saved_errno;        //Actualizo errno
}

/* Main */
int main(int argc, char*argv[]){
  /* SIGNAL */
  struct sigaction sa;                      //Estructura para trapear la señal de sigchld
  /* Semaphore */
  key_t sem_key;
  struct sembuf sem_buff[1];
  union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
  } sem_attr_client;
  int sem_id;
  /* Sockets */
  int sockclient, cantserv = 0, freq = 0;
  struct hostent *he;
  struct sockaddr_in server_addr;
  /* Misc */
  float auxaux;

  /* Verifico que todo lo que mandó por comando sea correcto */
  if(argc < 3){
    perror("usage: client hostnames frequency\n"); exit(1);
  }

  cantserv = argc - 2;          //Le saco el ./client y la periodicidad
  freq = atoi(argv[argc-1]);    //Último parámetro es la periodicidad

  /* SIGCHILD Handler */
  sa.sa_handler = sigchld_handler;                      //Puntero a mi función del handler
  sigemptyset(&sa.sa_mask);                             //Limpio todo
  sa.sa_flags = SA_RESTART;                             //Flag de restart
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {            //Trapeo la señal
		perror("client: sigchild: sigaction"); exit(-1);
	}

  /* Inicialización Semáforo */
  //Genero la llave del semáforo
  //Identifica al set de semáforos
  if((sem_key = ftok("./main.c", 'C')) == -1){
    perror("client: semaphore: ftok\n");
    exit(-1);
  }

  //Obtengo el identificador del semáforo
  if((sem_id = semget(sem_key, 1, 0664 | IPC_CREAT)) == -1){
    perror("client: semaphore: semget\n");
    exit(-1);
  }

  //Le doy un valor inicial al semáforo y lo dejo disponible
  sem_attr_client.val = SEM_AVAILABLE;
  if(semctl(sem_id, NUM_SEM_TO_USE, SETVAL, sem_attr_client) == -1){
    perror("client: semaphore: semctl\n");
    exit(-1);
  }

  while(1){

      while(cantserv > 0){

        if(!fork()){
          /******* Hijo ******/
          fprintf(stdout, "client: child on\n");
          //abro el socket
          he = gethostbyname(argv[cantserv]);                     //Obtengo la estructura de datos
          if(he == NULL){
            perror("client: child: gesthostbyname\n"); exit(-1);
          }

          sockclient = socket(AF_INET,SOCK_STREAM,0);           //Creo el socket y obtengo el file descriptor
          if(sockclient<0){
            perror("client: child: socket\n"); exit(-1);
          }

          server_addr.sin_family = AF_INET;                             //Internet socket
          server_addr.sin_port = htons(SERVER_PORT);                    //Paso el puerto a Network Order Byte short
          server_addr.sin_addr = *((struct in_addr*)he->h_addr);        //Obtengo la address
          memset(server_addr.sin_zero,0,sizeof(server_addr.sin_zero));  //Lo demás lo relleno de ceros

          /* Trato de conectarme al server */
          if(connect(sockclient,(struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
            perror("client: child: connect\n"); exit(-1);
          }

          while(1){
            //Hay que pedir data al server
            fprintf(stdout, "client: child: request for data\n");

            //Le mando GET pidiendo data
            if(write(sockclient, "GET", 4) < 0){
              perror("client: child: write\n");
              close(sockclient); exit(-1);
            }
            //Espero a recibir
            if(read(sockclient, &auxaux, sizeof(auxaux)) <= 0){
              perror("client: child: read\n");
              close(sockclient); exit(-1);
            }

            //Cuando obtiene data, tengo que printear si obtengo el semáforo
            sem_buff[0].sem_num = NUM_SEM_TO_USE;
            sem_buff[0].sem_op = -1;                //Lockeo el semáforo restandole 1 al valor del mismo -Queda en 0
            //Realizo la operación de resta, queda bloqueado hasta obtener el semáforo
            if(semop(sem_id, sem_buff, 1) == -1){
              perror("client: child: first semop\n");
              exit(-1);
            }

            //Al obtener el semáforo puedo imprimir en pantalla
            fprintf(stdout, "client pid %d: received: %f from %s\n", getpid(), auxaux, he->h_name);

            //Una vez que termine debo desbloquear el semáforo:
            sem_buff[0].sem_num = NUM_SEM_TO_USE;
            sem_buff[0].sem_op = 1;                 //Le sumo 1 al semáforo para desbloquearlo -Queda en 1
            if(semop(sem_id, sem_buff, 1) == -1){
              perror("client: child: second semop\n");
              exit(-1);
            }

            //Duermo - freq es el tiempo de peridicidad obtenido de la línea de comando
            sleep(freq);
          }
          //Termino el hijo
          close(sockclient);
          exit(0);
        }

        /******* Padre ******/
        fprintf(stdout, "client: parent here\n");
        cantserv--;
      }
  }

  //Cerrar todo acá - Elimino el semáforo
  if (semctl(sem_id, NUM_SEM_TO_USE, IPC_RMID, sem_attr_client) == -1) {
    perror("client: semctl: delete");
    exit(1);
  }
  return 0;
}
