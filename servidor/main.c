/*
  Guía de TP2
  Técnicas Digitales III Curso R5051 - 2018
  Eugenia Ipar

  Estación de Adquisión - Implementada en BBB

  Características:
        1. Actuar como servidor.
        2. Admitir solo 2 pedidos de conexión simultánea y aceptar hasta 1000 conexiones concurrentes.
        3. Establecer los parámetros de operación del dispositivo de medición y la IP a través de línea de comando o fichero de configuración.
        4. Establecer una sesión de transferencia de información con la estación de análisis, la cual deberá finalizarse pasado un periodo de N segundos
          sin actividad o cuando el cliente lo indique. Garantizar que la información enviada a través de la interfaz de red llegue en forma
          ordenada y el formato de los datos a enviar cumpla con DDMMAA,PARAMETRO
        5. Basado en el modelo de controladores para Linux (LKDM) el módulo a realizar debe implementar la API (open, close, read, write, ioctl) necesaria
        para gestionar la I2C en modo maestro. El módulo debe poder configurarse a través de "device tree" y desinstalarse en forma dinámica.
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
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/select.h>
#include <time.h>

//Librería del BMP180
#include "../driver/BMP180.h"

/* Defines */
#define DEBUG(x)        printf(x)
#define SERVER_PORT     10000
#define BACKLOG         2
#define MAX_SIM_CONN    1000
#define SHARED_MEM_SIZE sizeof(float)*4
#define SEM_AVAILABLE   1
#define SEM_UNAVAILABLE 0
#define CANT_SEM        1
#define NUM_SEM_TO_USE  0
#define DEAD_NUM        255     //0xFF

/* Estructuras */
struct thread_parameter{
  int pd;
  int port;
  char msg;
};

/* Global */
int cant_childs = 0;
//Puse las cosas necesarias del semáforo para poder liberarlo en cuanto un hijo muera
//Por si queda tomado
int sem_serv_id;
union semun {
  int val;
  struct semid_ds *buf;
  unsigned short *array;
} sem_attr;
int pid_primogenito = 0;          //PID del proceso primogénito, encargado de la comunicación con el sensor
                                  //Lo hago global así puedo diferenciarlo en el sigchild
float*shm_server;                  //Puntero a la shared memory, así en caso de que muera el proc pid_primogenito
                                  //Puedo indicar su desconexión a los otros hijos del server - los hermanastros (?)

/* Funciones */
/*  initServer
    Inicializa el socket del servidor, vinculándolo al puerto 10000
    Lo deja escuchando por el puerto -listen()- con un backlog de 1000
*/
int initServer(void){
  int servsock;
  int aux;
  struct sockaddr_in addr;

  memset(&addr,0,sizeof(struct sockaddr_in));           //Relleno con 0 para limpiar

  /* Configuro la estructura del socket */
  addr.sin_family = AF_INET;                      //Internet socket
  addr.sin_port = htons(SERVER_PORT);             //Paso a network order byte
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  //Creo el socket
  servsock = socket(AF_INET,SOCK_STREAM,0);
  if(servsock<0){
    perror("server: socket"); exit(-1);
  }
  //Para evitar el problema de "Address already in use" por multiples testeos
  setsockopt(servsock, SOL_SOCKET,  SO_REUSEADDR, &aux, sizeof(aux));

  //Bindeo el socket - le asigno una dirección
  if(bind(servsock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in))){
    perror("server: bind"); exit(-1);
  }

  //Escucho por el socket
  //Dejo escuchando esto
  if(listen(servsock, BACKLOG)){
    perror("server: listen"); exit(-1);
  }

  return servsock;
}

/*  sigchld_handler
    Handler de la señal SIGCHLD
    La señal SIGCHLD se envía al proceso padre una vez que el hijo termina
    Se debe utilizar la función wait() para que termine adecuadamente, y no quede en ZOMBIEs
*/
void sigchld_handler(int s){
  //waitpid() puede sobreescribir errno, así que lo guardo y lo restoreo después
  int saved_errno = errno;
  pid_t auxpid = 0;
  float * auxshmm;

  fprintf(stdout, "server: child killed\n");
  auxpid = wait(NULL);

  //Libero el semáforo
  sem_attr.val = SEM_AVAILABLE;
  if(semctl(sem_serv_id, NUM_SEM_TO_USE, SETVAL, sem_attr) == -1){
    perror("server: sigchld_handler: semctl\n");
    exit(-1);
  }

  //Se murio el primogenito?
  if(auxpid == pid_primogenito){
    fprintf(stdout,"PRIMOGENITO DEAD\n");
    //Escribo en la shared memory 255, así los hijos que leen este número
    //Saben que murió el sensor y lo indican a sus clientes y cierran todo
    auxshmm = shm_server;     //Puntero a la shared memory
    (*auxshmm) = DEAD_NUM;    //Le escribo el 255
  }else{
    cant_childs--;  //Se murió un hijo normal
  }

  errno = saved_errno;
}

/* Main */
int main(void){
  /* SIGNAL */
  struct sigaction sa;
  /* Semaphore */
  key_t sem_serv_key;
  struct sembuf sem_serv_buff[1];
  /* Shared Memory */
  key_t shm_key;
  int shm_id;
  /* Sockets */
  int sockcreate, sockaccpt;
  struct sockaddr_in addr;
  char buff[INET_ADDRSTRLEN];
  int addr_len = sizeof(struct sockaddr_in);
  /* Select Function */
  fd_set readset;
  int result;
  struct timeval timeout;
  /* Misc */
  char buffbis[4];
  float*aux;
  float resshm;
  /* BMP180 */
  int fd_bmp;
  float temp;
  BMP_coeff_t coeff;

  /* BMP180 */
  //Inicializo el dispositivo
  if((fd_bmp = BMP_Init()) < 0){
    perror("server: BMP_init");
    return -1;
  }

  fprintf(stdout, "BMP180 inited\n");

  if(BMP_GetChipID(fd_bmp) == 1){
		perror("Get Chip ID");
		return -1;
	}

  fprintf(stdout, "BMP180 is online!\n");

  if(BMP_Calibration(fd_bmp, &coeff) == 1){
	   perror("Get Chip ID");
	   return -1;
	}

  fprintf(stdout, "BMP180 has got coefficientes!\n");

  /* SIGCHILD Handler */
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);     //empty
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("server: sigchild: sigaction"); exit(-1);
	}

  /* Semáforo */
  //Genero la llave del semáforo
  //Identifica al set de semáforos
  if((sem_serv_key = ftok("../servidor/main.c", 'S')) == -1){
    perror("server: semaphore: ftok\n");
    exit(-1);
  }

  //Obtengo el identificador del semáforo
  if((sem_serv_id = semget(sem_serv_key, 1, 0666 | IPC_CREAT)) == -1){
    perror("server: semaphore: semget\n");
    exit(-1);
  }

  //Le doy un valor inicial al semáforo, no lo pongo disponible todavía
  sem_attr.val = SEM_UNAVAILABLE;
  if(semctl(sem_serv_id, NUM_SEM_TO_USE, SETVAL, sem_attr) == -1){
    perror("server: semaphore: semctl\n");
    exit(-1);
  }

  /* Shared Memory */
  //Genero la llave de la shared memory
  if((shm_key = ftok("../servidor/main.c", 'M')) == -1){
    perror("server: shared memory: ftok\n");
    exit(-1);
  }

  //Creo y me conecto a la shm
  if((shm_id = shmget(shm_key, SHARED_MEM_SIZE, 0644 | IPC_CREAT)) == -1){
    perror("server: shared memory: shmget\n");
    exit(-1);
  }

  //Me attacheo a la misma
  shm_server = shmat(shm_id, (void*)0, 0);
  if(shm_server == (float*)(-1)){
    perror("server: shared memory: shmat\n");
    exit(-1);
  }

  //Pongo al semáforo disponible
  sem_attr.val = SEM_AVAILABLE;
  if(semctl(sem_serv_id, NUM_SEM_TO_USE, SETVAL, sem_attr) == -1){
    perror("server: semaphore: semctl\n");
    exit(-1);
  }

  /******************************************************/
  /* Fork para el proceso primogénito */
  /* Se va a encargar de la comunicación con el sensor */
  /* Lo hago acá, así hereda la shared memory y el semáforo, pero no necesita el socket */
  pid_primogenito = fork();

  if(!pid_primogenito){
    /* Hijo primogénito */
    pid_primogenito = getpid();       //Obtengo el pid para el sigchld_handler
    while(1){
      fprintf(stdout, "server: primogenito: on! pid: %d \n", pid_primogenito);
      //Leo la temperatura
      temp = BMP_Read(fd_bmp, &coeff);
    	fprintf(stdout, "temp is %.2f\n", temp);

      //Pido el semáforo para escribir en la shared memory
      sem_serv_buff[0].sem_num = NUM_SEM_TO_USE;
      sem_serv_buff[0].sem_op = (-1);
      if(semop(sem_serv_id, sem_serv_buff, 1) == -1){
        perror("server: child: first semop\n");
        exit(-1);
      }
      //fprintf(stdout, "server: primogenito: tiene el semaforo\n");
      //Escribo el valor de la temperatura
      aux = shm_server;
      (*aux) = temp;
      //fprintf(stdout, "server: primogenito: escribio la temp\n");
      //Libero el semáforo
      sem_serv_buff[0].sem_num = NUM_SEM_TO_USE;
      sem_serv_buff[0].sem_op = (1);
      if(semop(sem_serv_id, sem_serv_buff, 1) == -1){
        perror("server: child: second semop\n");
        exit(-1);
      }
      fprintf(stdout, "server: primogenito: libero el semaforo y va a dormir\n");
      //Duermo por 5 minutos - setee este tiempo, dado que la temperatura no cambia tanto de un momento a otro
      sleep(5*60);
    }

  }


  /******************************************************/

  /* Server Padre */
  /* Llamo a una función que me inicializa el servidor */
  sockcreate = initServer();

  /* 2 conexiones en backlog y 1000 al mismo tiempo */
  while(1){

      if(cant_childs < MAX_SIM_CONN){
        //Puedo aceptar conexión
        fprintf(stdout, "server: waiting for connections...\n");

        sockaccpt = accept(sockcreate, (struct sockaddr*)&addr, (socklen_t*)&addr_len);
        if(sockaccpt == -1){
          perror("server: accept\n");
          continue;
        }

        inet_ntop(AF_INET, &(addr.sin_addr), buff, INET_ADDRSTRLEN);    //Esto lo hago para printear la ip
        fprintf(stdout, "server: got connection from %s\n", buff);

        FD_ZERO(&readset);              //Seteo todo a cero
        FD_SET(sockaccpt, &readset);    //Monitoreo el socket dónde acepté la conexión

        timeout.tv_sec = 10*60;          //10 min de timeout
        timeout.tv_usec = 0;

        /******************************************************/
        //Al aceptar conexión, creo un proceso hijo
        if(!fork()){
          /****** Hijo ******/
          cant_childs++;
          close(sockcreate);        //El hijo no necesita el socket que escucha

          while(1){

            //Select - espero a que se escriba en el socket, sino, timeout y cierro todo
            if((result = select(sockaccpt+1, &readset, NULL, NULL, &timeout)) <= 0){
              if(result == 0){
                fprintf(stdout, "server: child timed out from %s\n", buff);
              }else{
                perror("server: child: select\n");
              }
              close(sockaccpt);
              return 0;
            }

            if(FD_ISSET(sockaccpt, &readset)){
              fprintf(stdout, "server: child: has new data\n");

              if(read(sockaccpt, buffbis, sizeof(buffbis)) <= 0){
                perror("server: child: read\n");
                close(sockaccpt);
                return 0;
              }

            }
            fprintf(stdout, "server: child: has read\n");

            if(strcmp(buffbis, "GET") == 0){
              fprintf(stdout, "server: writing to %s ...\n", buff);
              memset(buffbis,0,strlen(buffbis));
              //Lockeo el semáforo para leer de la shrmry
              sem_serv_buff[0].sem_num = NUM_SEM_TO_USE;
              sem_serv_buff[0].sem_op = (-1);
              if(semop(sem_serv_id, sem_serv_buff, 1) == -1){
                perror("server: child: first semop\n");
                exit(-1);
              }

              //Una vez obtenido puedo leer y escribir ahi
              //Escribo el PID del child en el shared memory
              aux = shm_server;
              //Leo de la misma
              resshm = (*aux);

              if(resshm == DEAD_NUM){
                //perror("server: child: sensor is dead! goodbye client!\n");
                fprintf(stdout, "server: child: sensor is dead! goodbye client!\n");
                close(sockaccpt);
                return 0;
              }


              //Lo mando por el socket
              if(write(sockaccpt, &resshm, sizeof(resshm)) < 0){
                perror("server: child: write\n");
                close(sockaccpt);
                return 0;
              }

              //Lo vuelvo a desbloquear - Sem
              sem_serv_buff[0].sem_num = NUM_SEM_TO_USE;
              sem_serv_buff[0].sem_op = (1);
              if(semop(sem_serv_id, sem_serv_buff, 1) == -1){
                perror("server: child: second semop\n");
                exit(-1);
              }

            }else{
              fprintf(stdout, "server: not wrote\n");
            }
          }
          close(sockaccpt);
          exit(0);
        }
        /******************************************************/
        /****** Server Padre ******/
        close(sockaccpt);           //El padre no necesita esto
      }
  }

  //Eliminar Sem y Shared Memory
  if (semctl(sem_serv_id, NUM_SEM_TO_USE, IPC_RMID, sem_attr) == -1) {
    perror("server: semctl: delete\n");
    exit(1);
  }
  //La shared memory se va a eliminar una vez que de deattachen todos
  if(shmctl(shm_id, IPC_RMID, NULL) == -1){
    perror("server: shmctl: delete\n");
    exit(1);
  }
  //Deattach
  if(shmdt((void*)shm_server) == -1){
    perror("server: shmdt: deattach\n");
    exit(1);
  }

  close(sockcreate);
  return 0;
}
