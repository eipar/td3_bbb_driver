/*
  Guía de TP2
  Técnicas Digitales III Curso R5051 - 2018
  Eugenia Ipar

  Funciones para el BMP180
 */
#include "BMP180.h"

/*  BMP_Init
    Obtiene el file descriptor para el device, inicializándolo

    recibe:     nada
    devuelve:   file descriptor del archivo abierto
                -1 ERROR
                >0 OK
 */
int BMP_Init(void){
  int fd;

	fd = open("/dev/ei_i2c", O_RDWR);

	return fd;
}

/*  BMP_Deinit
    Cierra el dispositivo

    recibe:     file descriptor del device a cerrar
    devuelve:   nada
 */
void BMP_Deinit(int fd){

  close(fd);

}

/*  BMP_SoftReset
    Reset por software del BMP180

    recibe:     file descriptor del device
    devuelve:   0 OK
                1 ERROR
 */
int BMP_SoftReset(int fd){
  uint8_t check[2];

  check[0] = BMP_SOFT_RST;    //Address a escribir
  check[1] = BMP_RST_VAL;     //Valor a escribir

  if(write(fd, (void*)&check, 2) < 0){
    //Error al escribir
    perror("BMP_SoftReset: Error de escritura");
    return BMP_ERR;
  }

  usleep(15000);     //Tarda 4.5ms en reiniciar, le pongo 5ms por seguridad

  return BMP_OK;
}


/*  BMP_GetChipID
    Obtención del ID del BMP180
    Sirve para verificar una correcta inicialización y comunicacion con el dispositivo

    recibe:     file descriptor del device
    devuelve:   0 OK
                1 ERROR
 */
int BMP_GetChipID(int fd){
  uint8_t check;

  check = BMP_CHIP_ID;  //Address a leer

  //Escribo que quiero leer ese address
  if(write(fd, (void*)&check, 1) < 0){
    //Error al escribir
    perror("BMP_GetChipID: Error de escritura");
    return BMP_ERR;
  }

  check = 0;    //Limpio la variale
  //Leo
  if(read(fd,(void*)&check,1) < 0){
		perror("BMP_GetChipID: Error de lectura");
    return BMP_ERR;
	}

  //fprintf(stdout, "check-> %x\n", check);
  if(check != BMP_ID){
    return BMP_ERR;
  }

  //Lo leído es el ID del BMP180, todo ok
  return BMP_OK;
}


/*  BMP_Calibration
    Se obtienen los datos de calibración de la memoria EEPROM del BMP180
    Se cargan en una estructura tipo BMP_coeff_t
    Estos coeficientes se utilizan para calcular la temperatura y presión

    recibe:     fd        ->  file descriptor del device
                coeff     ->  estructura dónde guardar los coeficientes de calibración
    devuelve:   0 OK
                1 ERROR
 */
int BMP_Calibration(int fd, BMP_coeff_t*coeff){
  uint8_t check; uint8_t data[2];

  check = 0xAA;
	write(fd,(void*)&check,1);

	if(read(fd,(void*)&data,2) < 0){
		perror("Error al intentar leer");
	}

	coeff->ac1 = (short)data[0] << 8;
	coeff->ac1 |= (short)data[1];

  sleep(1);

  BMP_Deinit(fd);
  fd = BMP_Init();

  data[0] = data[1] = 0;
	//	**********************************************************************

  check = 0xAC;
	write(fd,(void*)&check,1);

	if(read(fd,(void*)&data,2) < 0){
		perror("Error al intentar leer");
	}
	coeff->ac2 = (short)data[0] << 8;
	coeff->ac2 |= (short)data[1];

  sleep(1);

  BMP_Deinit(fd);
  fd = BMP_Init();

  data[0] = data[1] = 0;
	//	**********************************************************************
	check = 0xAE;
	write(fd,(void*)&check,1);

	if(read(fd,(void*)&data,2) < 0){
		perror("Error al intentar leer");
	}
	coeff->ac3 = (short) data[0] << 8;
	coeff->ac3 |= (short)data[1];

  sleep(1);

  BMP_Deinit(fd);
  fd = BMP_Init();

  data[0] = data[1] = 0;
	//	**********************************************************************
	check = 0xB0;
	write(fd,(void*)&check,1);

	if(read(fd,(void*)&data,2) < 0){
		perror("Error al intentar leer");
	}
	coeff->ac4 = (short) data[0] << 8;
	coeff->ac4 |= (short)data[1];

  sleep(1);

  BMP_Deinit(fd);
  fd = BMP_Init();

  data[0] = data[1] = 0;
	//	**********************************************************************
	check = 0xB2;
	write(fd,(void*)&check,1);

	if(read(fd,(void*)&data,2) < 0){
		perror("Error al intentar leer");
	}
	coeff->ac5 = (short) data[0] << 8;
	coeff->ac5 |= (short)data[1];

  sleep(1);

  BMP_Deinit(fd);
  fd = BMP_Init();

  data[0] = data[1] = 0;
	//	**********************************************************************
	check = 0xB4;
	write(fd,(void*)&check,1);

	if(read(fd,(void*)&data,2) < 0){
		perror("Error al intentar leer");
	}
	coeff->ac6 = (short) data[0] << 8;
	coeff->ac6 |= (short)data[1];

  sleep(1);

  BMP_Deinit(fd);
  fd = BMP_Init();

  data[0] = data[1] = 0;
	//	**********************************************************************
	check = 0xB6;
	write(fd,(void*)&check,1);

	if(read(fd,(void*)&data,2) < 0){
		perror("Error al intentar leer");
	}
	coeff->b1 = (short) data[0] << 8;
	coeff->b1 |= (short)data[1];

  sleep(1);

  BMP_Deinit(fd);
  fd = BMP_Init();

  data[0] = data[1] = 0;
	//	**********************************************************************
	check = 0xB8;
	write(fd,(void*)&check,1);

	if(read(fd,(void*)&data,2) < 0){
		perror("Error al intentar leer");
	}
	coeff->b2 = (short) data[0] << 8;
	coeff->b2 |= (short)data[1];

  sleep(1);

  BMP_Deinit(fd);
  fd = BMP_Init();

  data[0] = data[1] = 0;
	//	**********************************************************************
	check = 0xBA;
	write(fd,(void*)&check,1);

	if(read(fd,(void*)&data,2) < 0){
		perror("Error al intentar leer");
	}
	coeff->mb = (short) data[0] << 8;
	coeff->mb |= (short)data[1];

  sleep(1);

  BMP_Deinit(fd);
  fd = BMP_Init();

  data[0] = data[1] = 0;
	//	**********************************************************************
	check = 0xBC;
	write(fd,(void*)&check,1);

	if(read(fd,(void*)&data,2) < 0){
		perror("Error al intentar leer");
	}
	coeff->mc = (short) data[0] << 8;
	coeff->mc |= (short)data[1];

  sleep(1);

  BMP_Deinit(fd);
  fd = BMP_Init();

  data[0] = data[1] = 0;
	//	**********************************************************************
	check = 0xBE;
	write(fd,(void*)&check,1);

	if(read(fd,(void*)&data,2) < 0){
		perror("Error al intentar leer");
	}
	coeff->md = (short) data[0] << 8;
	coeff->md |= (short)data[1];

  BMP_Deinit(fd);
  fd = BMP_Init();

	return BMP_OK;
}


/*  BMP_Read
    Se calcula la temperatura con los coeficientes obtenidos de BMP_Calibration
    En el diagrama de flujo de la hoja de datos indica bien cómo calcularla

    recibe:     fd        ->  file descriptor del device
                coeff     ->  estructura dónde guardar los coeficientes de calibración
    devuelve:   >0  temperatura
                1   ERROR
 */
float BMP_Read(int fd, BMP_coeff_t*coeff){
  float temp;
  uint8_t data[2];
  uint8_t check;
  long UT, X1, X2, B5, T;
  int i;

  //Address y Dato a escribir, para pedir la temperatura
  data[0] = BMP_MEAS_CTRL; data[1] = BMP_GET_TEMP;
  if(write(fd, (void*)&data, 2) < 0){
    //Error al escribir
    perror("BMP_Read: Error de escritura en BMP_MEAS_CTRL");
    return BMP_ERR;
  }

  //Espera para obtener la medición
  for(i=0; i<=0xFFFF; i++);

  //Voy a leer el MSB de la Temperatura
  check = BMP_OUT_MSB;
  if(write(fd, (void*)&check, 1) < 0){
    //Error al escribir
    perror("BMP_Read: Error de escritura en BMP_OUT_MSB");
    return BMP_ERR;
  }

  check = 0;
  if(read(fd,(void*)&check,1) < 0){
		perror("BMP_Calibration: Error de lectura MD");
    return BMP_ERR;
	}
  UT = (long)(check << 8);

  //Ahora leo LSB de la Temperatura
  check = BMP_OUT_LSB;
  if(write(fd, (void*)&check, 1) < 0){
    //Error al escribir
    perror("BMP_Read: Error de escritura en BMP_OUT_MSB");
    return BMP_ERR;
  }

  check = 0;
  if(read(fd,(void*)&check,1) < 0){
		perror("BMP_Calibration: Error de lectura MD");
    return BMP_ERR;
	}

  UT |= check;

  /* Cálculo de la temp tal como esta en el diagrama de flujo */
  X1 = (UT - coeff->ac6) * coeff->ac5 / BMP_CONST_1;
	X2 = coeff->mc * BMP_CONST_2 / (X1 + coeff->md);
	B5 = X1 + X2;
	T = (B5 + BMP_CONST_4) / BMP_CONST_3;

	temp = (float)T;
	temp = temp / BMP_DEC;

	return temp;

}
