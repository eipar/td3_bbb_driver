/*
  Guía de TP2
  Técnicas Digitales III Curso R5051 - 2018
  Eugenia Ipar

  Funciones para el BMP180
 */
#ifndef BMP180_H_
#define BMP180_H_

/* Includes */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

/* Typedefs */
typedef unsigned char uint8_t;        //Más fácil

//Declaro una estructura dónde guardar todos los coeficientes de calibración
//Para poder calcular temp y presión lueog
typedef struct BMP_coeff_s{
  short int			ac1;
	short int			ac2;
	short int			ac3;
	unsigned short int	ac4;
	unsigned short int	ac5;
	unsigned short int	ac6;
	short int			b1;
	short int			b2;
	short int			mb;
	short int			mc;
	short int			md;
} BMP_coeff_t;

/* Defines */

//Calibration Coefficients
#define BMP_AC1_MSB     0xAA
#define BMP_AC1_LSB     0xAB
#define BMP_AC2_MSB     0xAC
#define BMP_AC2_LSB     0xAD
#define BMP_AC3_MSB     0xAE
#define BMP_AC3_LSB     0xAF
#define BMP_AC4_MSB     0xB0
#define BMP_AC4_LSB     0xB1
#define BMP_AC5_MSB     0xB2
#define BMP_AC5_LSB     0xB3
#define BMP_AC6_MSB     0xB4
#define BMP_AC6_LSB     0xB5
#define BMP_B1_MSB      0xB6
#define BMP_B1_LSB      0xB7
#define BMP_B2_MSB      0xB8
#define BMP_B2_LSB      0xB9
#define BMP_MB_MSB      0xBA
#define BMP_MB_LSB      0xBB
#define BMP_MC_MSB      0xBC
#define BMP_MC_LSB      0xBD
#define BMP_MD_MSB      0xBE
#define BMP_MD_LSB      0xBF

#define BMP_SLAVE       0x77
#define BMP_ID          0x55
#define BMP_RST_VAL     0xB6
#define BMP_GET_TEMP    0x2E

#define BMP_OUT_MSB     0xF6
#define BMP_OUT_LSB     0xF7

#define BMP_CONST_1     32768 //2^16
#define BMP_CONST_2     2048 //2^11
#define BMP_CONST_3     16 //2^4
#define BMP_CONST_4     8 //2^3
#define BMP_DEC         10

//Memory Map
#define BMP_MEAS_CTRL   0xF4
#define BMP_SOFT_RST    0xE0
#define BMP_CHIP_ID     0xD0

#define BMP_OK          0
#define BMP_ERR         1

/* Funciones */
int BMP_Init(void);                             //Inicializa el I2C
void BMP_Deinit(int);                           //Cierra la conexión

int BMP_SoftReset(int);                        //Soft Reset del dispositivo
int BMP_GetChipID(int);                         //Verificación si la conexión está viva o no

float BMP_Read(int, BMP_coeff_t*);              //Lee del sensor y devuelve el resultado

int BMP_Calibration(int, BMP_coeff_t*);        //Calibra el sensor obteniendo los datos de la EEPROM interna de él

#endif
