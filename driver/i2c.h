/*
  Guía de TP2
  Técnicas Digitales III Curso R5051 - 2018
  Eugenia Ipar

  Driver I2C para BeagleBone Black
 */
#ifndef __i2c_H_
#define __i2c_H_

/* Librerías */
//agregar a medida que se necesite!
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
//#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/uaccess.h>

/* Defines */
#define MENOR         0             //Elijo un minor number para mi driver
#define CANT_DISP     1             //Cantidad de dispositivos que voy a usar

#define BMP180_SA			0x77					//BMP180 Slave Address

/* Zona de registros mapeados a memoria */
//Control Module Registers -pag180
#define CONTMOD_ADD               0x44E10000
#define CONTMOD_LEN				        0x2000

//Clock Module Peripheral Registers -pag179
#define CMPER_ADD					        0x44E00000
#define CMPER_LEN					        0x400
#define CMPER_I2C2_OFF 			      0x44
#define CMPER_MODULE_ENABLE 	    0x2
#define CMPER_MODULE_DISABLE 	    0x1

/* PAD Mapping */
/* GPIO0_2 = SPI0_SCLK = I2C2_SDA MODE 2  (P9. Pin 22) */
/* GPIO0_3 = SPI0_D0   = I2C2_SCL MODE 2  (P9. Pin 21) */
//Offset conf_spi0_sclk conf_spi0_d0
#define SDA_I2C2_PIN_OFF_CONTMOD 0x950
#define SCL_I2C2_PIN_OFF_CONTMOD 0x954

/* I2C Register Mapping */
#define I2C_REVNB_LO          0x00    //Module Revision Register (low bytes) Section 21.4.1.1
#define I2C_REVNB_HI				  0x04    //Module Revision Register (high bytes) Section 21.4.1.2
#define I2C_SYSC					    0x10    //System Configuration Register Section 21.4.1.3
#define I2C_IRQSTATUS_RAW 		0x24    //I2C Status Raw Register Section 21.4.1.4
#define I2C_IRQSTATUS 	 		  0x28    //Status Register Section 21.4.1.5
#define I2C_IRQENABLE_SET 		0x2C    //I2C Interrupt Enable Set Register Section 21.4.1.6
#define I2C_IRQENABLE_CLR 		0x30    //I2C Interrupt Enable Clear Register Section 21.4.1.7
#define I2C_WE 	 				      0x34    //Wakeup Enable Register Section 21.4.1.8
#define I2C_DMARXENABLE_SET	  0x38    //Receive DMA Enable Set Register Section 21.4.1.9
#define I2C_DMATXENABLE_SET 	0x3C    //Transmit DMA Enable Set Register Section 21.4.1.10
#define I2C_DMARXENABLE_CLR 	0x40    //Receive DMA Enable Clear Register Section 21.4.1.11
#define I2C_DMATXENABLE_CLR 	0x44    //Transmit DMA Enable Clear Register Section 21.4.1.12
#define I2C_DMARXWAKE_EN 		  0x48    //Receive DMA Wakeup Register Section 21.4.1.13
#define I2C_DMATXWAKE_EN 		  0x4C    //Transmit DMA Wakeup Register Section 21.4.1.14
#define I2C_SYSS              0x90    //Status Register Section 21.4.1.15
#define I2C_BUF 					    0x94    //Buffer Configuration Register Section 21.4.1.16
#define I2C_CNT 				      0x98    //Data Counter Register Section 21.4.1.17
#define I2C_DATA 				      0x9C    //Data Access Register Section 21.4.1.18
#define I2C_CON 					    0xA4    //I2C Configuration Register Section 21.4.1.19
#define I2C_OA  					    0xA8    //I2C Own Address Register Section 21.4.1.20
#define I2C_SA 					      0xAC    //I2C Slave Address Register Section 21.4.1.21
#define I2C_PSC 					    0xB0    //I2C Clock Prescaler Register Section 21.4.1.22
#define I2C_SCLL 					    0xB4    //I2C SCL Low Time Register Section 21.4.1.23
#define I2C_SCLH 					    0xB8    //I2C SCL High Time Register Section 21.4.1.24
#define I2C_SYSTEST 				  0xBC    //System Test Register Section 21.4.1.25
#define I2C_BUFSTAT 				  0xC0    //I2C Buffer Status Register Section 21.4.1.26
#define I2C_OA1 					    0xC4    //I2C Own Address 1 Register Section 21.4.1.27
#define I2C_OA2 					    0xC8    //I2C Own Address 2 Register Section 21.4.1.28
#define I2C_OA3					      0xCC    //I2C Own Address 3 Register Section 21.4.1.29
#define I2C_ACTOA 				    0xD0    //Active Own Address Register Section 21.4.1.30
#define I2C_SBLOCK 				    0xD4    //I2C Clock Blocking Enable Register

/* I2C Defines */
#define I2C_IRQENABLE_XRDY		0x10    //Transmit data ready interrupt enabled
#define I2C_IRQENABLE_RRDY		0x08    //Receive data ready interrupt enabled
#define I2C_IRQENABLE_ARDY		0x04
#define I2C_IRQENABLE_BB		  0x800

#define I2C_CON_STT				    0x01    //Start condition
#define I2C_CON_STP				    0x02    //Stop condition

#define I2C_IRQSTAT_RRDY		  0x08    //Bit is set when receive data is available
#define I2C_IRQSTAT_XRDY		  0x10    //Bit is set when transmit data is available

#define I2C_IRQSTAT_ARDY		  0x04    //Previous access has been performed and registers are ready to be accessed again.
#define I2C_IRQSTAT_BB			  0x800

/* File operations*/
ssize_t i2c_write(struct file * archivo, const char __user * data_user, size_t cantidad, loff_t * poffset);
ssize_t i2c_read(struct file * archivo, char __user * data_user, size_t cantidad, loff_t * poffset);

int i2c_close(struct inode * pinodo, struct file * archivo);
int i2c_open(struct inode * pinodo, struct file * archivo);

static int i2c_probe(struct platform_device *pdev);
static int i2c_remove(struct platform_device *pdev);

/* I2C Handler */
irqreturn_t i2c_int_handler(int irq, void *dev_id, struct pt_regs *regs);

/* Estructura I2C_data */
struct i2c_data_t {
	void * pcontmod;
	void * pi2c;
	void * pcm_per;
	char * prx_buff;
	unsigned int rx_buff_pos;
	char * ptx_buff;
	unsigned int tx_buff_size;
	unsigned int tx_buff_pos;
};

#endif /* __i2c_H_ */
