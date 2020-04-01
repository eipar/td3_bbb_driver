/*
  Guía de TP2
  Técnicas Digitales III Curso R5051 - 2018
  Eugenia Ipar

  Driver I2C para BeagleBone Black
 */
#include "i2c.h"

/* Autor y esos detalles necesarios para el driver */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eugenia Ipar");
MODULE_VERSION("0.245");

/* Estructura File Operations */
//Así linkeo las funciones comunes con las que va a utilizar el driver
static struct file_operations i2c_ops = {
	.owner = THIS_MODULE,
	.write = i2c_write,
	.read = i2c_read,
	.open = i2c_open,
	.release = i2c_close,
};

/* Estructura of_device_id */
//Matcheo el device con el driver utilizando la propiedad "compatible" del devicetree
//así puedo ejecutar la función probe()
static const struct of_device_id i2c_of_match[] = {
	{.compatible = "ipi,omap4-i2c"},                   //nombre puesto en el .dts
	{},
};
//Función que expone el ID table del driver, que describe qué devices soporta
//Ahora el driver es DT-compatble
MODULE_DEVICE_TABLE(of, i2c_of_match);

/* Estructura platform_driver */
//Un File Operations para el platform driver así linkeo mis funciones
//Le digo dónde esta en el DT
static struct platform_driver i2c_driver = {
	.probe = i2c_probe,
	.remove = i2c_remove,
	.driver = {
		.name	= "ei_i2c",
		.of_match_table = of_match_ptr(i2c_of_match),       //devuelve NULL cuando OF esta deshabilitado
	},
};


/* Método remove */
/* No se necesita más el driver - se desinstala */
static int i2c_remove(struct platform_device *pdev)
{
	printk(KERN_ALERT "ei_i2c: Entro al remove\n");
	return 0;
}

/* Variables Globales Internas */
static dev_t	dispo;
static struct cdev * i2c_cdev;
static struct class * pdev_class;
static struct device * pdev_dev;
static struct resource mem_res;
static struct i2c_data_t i2c_data;
static int i2c_irq = 0;

static struct i2c_data_t i2c_data = {
	.rx_buff_pos=0,
};
static int cond_wake_up_rx=0;
static int cond_wake_up_tx=0;

/* Spinlocks */
static DEFINE_SPINLOCK(ei_i2c_rx_lock);				//Lo uso en el handler

/* Wait Queues */
static DECLARE_WAIT_QUEUE_HEAD(ei_i2c_rx_q);	//Para read/write
static DECLARE_WAIT_QUEUE_HEAD(ei_i2c_tx_q);

/* Método probe */
/* Para cuando alguien inserta o reclama el device */
static int i2c_probe(struct platform_device *pdev){
  int aux;

  //Entro a probe!
  printk(KERN_INFO "ei_i2c: Ingreso a la funcion probe\n");

  /* Primero obtengo una VIRQ para el dispositivo */
  //Mapea una interrupción en el espacio de linux virq
  i2c_irq = irq_of_parse_and_map((pdev->dev).of_node, 0);
	if(i2c_irq == 0){
    //Salió mal, tengo que sacar todo
		platform_driver_unregister(&i2c_driver);
		device_destroy(pdev_class, dispo);
		class_destroy(pdev_class);
		cdev_del (i2c_cdev);
		unregister_chrdev_region(dispo, CANT_DISP);
		printk(KERN_ALERT "ei_i2c: Error al intentar obtener el VIRQ\n");
		return EBUSY; /* errno: Device or resource busy */
	}
 	printk(KERN_INFO "ei_i2c: VIRQ obtenido para ei_i2c = %d\n", i2c_irq);

  /* Implanto el handler de IRQ */
	/* asocio handler con interrupción */
  //Debo indicar el handler a llamar
  //Los flags de la interrupción -> sólo le pongo flanco ascendente
  //Nombre del device
  aux = request_irq(i2c_irq, (irq_handler_t)i2c_int_handler, IRQF_TRIGGER_RISING, "ei_i2c", NULL);
  if(aux != 0){
    //Salió mal, tengo que sacar todo
  	platform_driver_unregister(&i2c_driver);
  	device_destroy(pdev_class, dispo);
  	class_destroy(pdev_class);
  	cdev_del(i2c_cdev);
  	unregister_chrdev_region(dispo, CANT_DISP);
  	printk(KERN_ALERT "ei_i2c: Error al intentar implantar handler de IRQ\n");
  	return EBUSY; /* errno: Device or resource busy */
  }
  printk(KERN_INFO "ei_i2c: Handler de IRQ implantado\n");

  /* Ahora tengo que empezar a leer memoria y asociarla al dispo */
  /* Y pedirla para los pines y registros del I2C */
  // Y EL CLOCK!!!!

  /* Primero leo la zona del I2C del device tree, con los recursos anteriores */
  /* Translate device tree address and return as resource */
  aux = of_address_to_resource((pdev->dev).of_node, 0, &mem_res);
	if (aux){
    //Salió mal, tengo que sacar todo
    //Acordarse de liberar la virq!!!
		free_irq(i2c_irq, NULL);
		platform_driver_unregister(&i2c_driver);
		device_destroy(pdev_class, dispo);
		class_destroy(pdev_class);
		cdev_del(i2c_cdev);
		unregister_chrdev_region(dispo, CANT_DISP);
		printk(KERN_ALERT "ei_i2c: Error al obtener zona de memoria del device tree\n");
		return EBUSY; /* errno: Device or resource busy */
	}

  /* Registro la zona de memoria como IO asociada al dispositivo */
	if(!request_mem_region(mem_res.start, resource_size(&mem_res), "/ocp/ei_i2c")){
    //Salió mal, tengo que sacar todo
		free_irq(i2c_irq, NULL);
		platform_driver_unregister(&i2c_driver);
		device_destroy(pdev_class, dispo);
		class_destroy(pdev_class);
		cdev_del (i2c_cdev);
		unregister_chrdev_region(dispo, CANT_DISP);
		printk(KERN_ALERT "ei_i2c: Error al registrar zona de memoria\n");
		return EBUSY;
 	}

  /* Ya tengo espacio en memoria, ahora tengo que empezar a pedir memoria física */
  /* Pido memoria física en la zona del pad de pines */
  //Page-Table mapping:
  //Control Module de  0x44E1_0000 a 0x44E1_1FFF
  i2c_data.pcontmod = ioremap(CONTMOD_ADD, CONTMOD_LEN);
  if (i2c_data.pcontmod == NULL){
    //Salió mal, tengo que sacar todo
  	free_irq(i2c_irq, NULL);
  	platform_driver_unregister(&i2c_driver);
  	device_destroy(pdev_class, dispo);
  	class_destroy(pdev_class);
  	cdev_del(i2c_cdev);
  	unregister_chrdev_region(dispo, CANT_DISP);
  	printk(KERN_ALERT "ei_i2c: no puedo acceder al control module\n");
  	return -EBUSY;
  }

  /* Pido memoria física en la zona de los registros del i2c, obtenido de la zona del device tree */
  //Como lo hago desde la zona que device tree que pedi antes
  //Uso of_iomap
  // of_iomap - Maps the memory mapped IO for a given device_node
	i2c_data.pi2c = of_iomap((pdev->dev).of_node, 0);
	if (i2c_data.pi2c == NULL){
    //Salió mal, tengo que sacar todo
    //Agregar: desmapear lo que mapee antes
		iounmap(i2c_data.pcontmod);
		free_irq(i2c_irq, NULL);
		platform_driver_unregister(&i2c_driver);
		device_destroy(pdev_class, dispo);
		class_destroy(pdev_class);
		cdev_del(i2c_cdev);
		unregister_chrdev_region(dispo, CANT_DISP);
		printk(KERN_ALERT "ei_i2c: no puedo acceder a los registros del i2c0\n");
		return -EBUSY;
	}

  //me olvide del clock :( LISTO

  /* Pido memoria física en la zona de los registros de la CMPER */
  //tengo que paginar:
  //Clock Module Peripheral Registers de  0x44E0_0000 a 0x44E0_3FFF
  i2c_data.pcm_per = ioremap(CMPER_ADD, CMPER_LEN);
  if (i2c_data.pcm_per == NULL){
    //Salió mal, tengo que sacar todo
  	iounmap(i2c_data.pcontmod);
  	iounmap(i2c_data.pi2c);      //Puedo desmapear con esta también? si
  	free_irq(i2c_irq, NULL);
  	platform_driver_unregister(&i2c_driver);
  	device_destroy(pdev_class, dispo);
  	class_destroy(pdev_class);
  	cdev_del (i2c_cdev);
  	unregister_chrdev_region(dispo, CANT_DISP);
  	printk(KERN_ALERT "ei_i2c: no puedo acceder al clock managment\n");
  	return -EBUSY;
  }

  printk(KERN_INFO "ei_i2c: Finalizo probe exitosamete\n");

  /* Todo salió bien */
  return 0;
}

/* Método init */
/* Inicializa todo, se ejecuta al llamar a insmod */
/* Pasos que tengo que seguir:
    - Pedir major
    - Registrar cdev
    - Crear clase y nodo de la clase
    - Crear platform driver
    - Matchear info con dt
    - Registrar platform
  irse guiando con madieu
 */
int i2c_init(void){
  int aux2;
  static struct platform_device *pdev;

  /* Primero tengo que instanciar el dispositivo */
  i2c_cdev = cdev_alloc();             //Obtengo la estructura cdev
  if ((alloc_chrdev_region(&dispo, MENOR, CANT_DISP, "ei_i2c")) < 0){				//Obtengo un major number dinámicamente
		printk(KERN_ALERT "ei_i2c: no se obtuvo un numero mayor\n");
		return -EBUSY;  /* errno: Device or resource busy */
	}
  printk(KERN_INFO "ei_i2c: Major number asignado: %d\n", MAJOR(dispo));

  /* Registro el dispo en el sistema */
  i2c_cdev->ops = &i2c_ops;             //Puntero al file operations anterior
	i2c_cdev->owner = THIS_MODULE;
	i2c_cdev->dev = dispo;                //dev_t struct - dónde guardo el major y minor number
  //Agrego el char device al sistema
	if ((cdev_add(i2c_cdev, dispo, CANT_DISP)) < 0){
    //Si no pudo tengo que liberar el major y minor number
		unregister_chrdev_region(dispo, CANT_DISP);
		printk(KERN_ALERT "ei_i2c: No es posible registrar el dispositivo\n");
		return -EBUSY;  /* errno: Device or resource busy */
	}

  /* Creo la clase visible en /sys/class */
  /* Me devuelve puntero a struct class */
  pdev_class = class_create(THIS_MODULE, "eidev");
	if (IS_ERR(pdev_class)){
    //Elimino el char device y libero major/minor number
		cdev_del(i2c_cdev);
		unregister_chrdev_region(dispo, CANT_DISP);
		printk(KERN_ALERT "ei_i2c: No se pudo crear la clase\n");
		return PTR_ERR(pdev_class); //Devuelvo el error obtenido
	}

  /* Creo el device node dentro de la clase, registra en sysfs */
  /* /dev */
  //parent -> NULL
  //sin data adicional -> NULL
  pdev_dev = device_create(pdev_class, NULL, dispo, NULL, "ei_i2c");
  if (IS_ERR(pdev_dev)){
    //Elimino el char device y libero major/minor number
    //Cada vez voy a tener que ir agregando más cosas acá
    class_destroy(pdev_class);
    cdev_del(i2c_cdev);
    unregister_chrdev_region(dispo, CANT_DISP);
		printk(KERN_ALERT "ei_i2c: No se pudo crear el dispositivo\n");
    return PTR_ERR(pdev_dev);   //Devuelvo el error obtenido
  }

  /* Ahora viene la parte del Platform Device */
  /* Para avisarle al kernel que tiene un I2C (?) */
  //Dynamically allocate a device
  pdev = platform_device_alloc("ei_i2c", -1);
  //printk(KERN_ALERT "ei_i2c: devuelto platform_device_alloc = %x\n", (unsigned int)pdev);
  if (pdev == 0){
    //Libero todo
    device_destroy(pdev_class, dispo);
    class_destroy(pdev_class);
    cdev_del(i2c_cdev);
    unregister_chrdev_region(dispo, CANT_DISP);
    printk(KERN_ALERT "ei_i2c: No paso el platform_device_alloc\n");
    return -EBUSY;  /* errno: Device or resource busy */
  }

  /* Obtengo la información del device tree del dispositivo */
  //Linux board support code calls of_platform_populate(NULL, NULL, NULL, NULL)
  //to kick off discovery of devices at the root of the tree.
  //Yo no lo hago desde el principio del dt, sino desde el match que puse arriba
  aux2 = of_platform_populate(NULL, of_match_ptr(i2c_of_match), NULL , NULL);
	if (aux2 != 0){
    //Libero todo
    device_destroy(pdev_class, dispo);
		class_destroy(pdev_class);
		cdev_del(i2c_cdev);
		unregister_chrdev_region(dispo, CANT_DISP);
		printk(KERN_ALERT "ei_i2c: No paso el of_platform_populate\n");
		return -EBUSY;  /* errno: Device or resource busy */
  }

  /* Registro el driver, una vez realizado esto ingresa a la función probe */
  aux2 = platform_driver_register(&i2c_driver);
  if (aux2 != 0){
    //Libero todo
		device_destroy(pdev_class, dispo);
	 	class_destroy(pdev_class);
	 	cdev_del(i2c_cdev);
	 	unregister_chrdev_region(dispo, CANT_DISP);
		printk(KERN_ALERT "ei_i2c: No paso el platform_driver_register\n");
		return -EBUSY;
  }
	printk(KERN_INFO "ei_i2c: Finalizo init exitosamete\n");

	/* Todo salió bien! :) */
	return 0;
}

/* Método exit */
/* Remueve todo, se ejecuta al llamar a rmmod */
//Tengo que hacer todo lo de init y probe, al revés
//AGREGAR LO DE IRQ
//CHEQUEAR QUE ESTE todo
static void i2c_exit (void){
	printk(KERN_INFO "ei_i2c: Entro al exit\n");
  /*Retiro el dispositivo del sistema*/
	iounmap(i2c_data.pcontmod);    //Desmapeo
	iounmap(i2c_data.pi2c);
	iounmap(i2c_data.pcm_per);
	release_mem_region(mem_res.start, resource_size(&mem_res));  //Libero memoria
	free_irq(i2c_irq, NULL);                   //Libero las interrupciones
	platform_driver_unregister(&i2c_driver);   //Desregistro el device y todas sus cosas
	device_destroy(pdev_class, dispo);
	class_destroy(pdev_class);
	cdev_del(i2c_cdev);
	unregister_chrdev_region(dispo, CANT_DISP);
	printk(KERN_ALERT "ei_i2c: Modulo removido\n");  //listo!
}

/* Método open */
int i2c_open(struct inode * pinodo, struct file * archivo){
  int aux = 0, contador = 0;
	printk(KERN_INFO "ei_i2c: Entro al open\n");

  /* Configuro los pines del I2C -pag1422 */
  /*  bit 2-0     Pad functional signal mux select          -> Mode 2  10
      bot 3       Pad pullup/pulldown enabled               -> 0 enabled
      bit 4       Pad pullup/pulldown type selection        -> 1 pullup
      bit 5       Input enable value for the PAD            -> 1 enabled
      bit 6       Select between faster or slower slewrate  -> 0 fast
      Total = 0x2A
   */
	iowrite32(0x2A,i2c_data.pcontmod + SDA_I2C2_PIN_OFF_CONTMOD);
	iowrite32(0x2A,i2c_data.pcontmod + SCL_I2C2_PIN_OFF_CONTMOD);

  /* Ahora tengo que encender el clock */
  /* CM_PER_I2C2_CLKCTRL Register -pag1178 */
  /* Module Mode bits 0-1 -> 0x2 ENABLE: Module is explicity enabled. */
  iowrite32(CMPER_MODULE_ENABLE, i2c_data.pcm_per + CMPER_I2C2_OFF);
	aux = ioread32(i2c_data.pcm_per + CMPER_I2C2_OFF);
	while (aux != CMPER_MODULE_ENABLE){
		msleep(1);
		aux = ioread32(i2c_data.pcm_per + CMPER_I2C2_OFF);
		if (contador > 4){
			printk(KERN_ALERT "ei_i2c: error al encender el clock\n");
			return -EBUSY; //errno
		}
		contador++;
	}

  printk(KERN_INFO "ei_i2c: Clock encendido correctamente!\n");

  /* Inicializo los registros del I2C */
  //-pág4490
  /* Deshabilito el I2C para poder configurarlo */
  iowrite32(0x00, i2c_data.pi2c + I2C_CON);
  /* Prescaler por 32 */
	iowrite32(0x20, i2c_data.pi2c + I2C_PSC);
  /* SCL low time */
	iowrite32(0x07, i2c_data.pi2c + I2C_SCLL);
  /* SCL high time */
	iowrite32(0x09, i2c_data.pi2c + I2C_SCLH);
	/* Master address */
	iowrite32(0x36, i2c_data.pi2c + I2C_OA);
	/* No wakeup, force idle, clock can be cut off */
	iowrite32(0x00, i2c_data.pi2c + I2C_SYSC);
	/* Slave address */
	iowrite32(BMP180_SA, i2c_data.pi2c + I2C_SA);
	/* Ahora si lo habilito */
	iowrite32(0x8400, i2c_data.pi2c + I2C_CON);

  /* Pido una página de memoria */
  //así tengo dónde guardar cosas
	if ((i2c_data.prx_buff = (char *) __get_free_page(GFP_KERNEL)) < 0){
		printk(KERN_ALERT "ei_i2c: No se obtiene memoria para el RX\n");
		return -ENOMEM;
	}

	printk(KERN_INFO "ei_i2c: termino el open\n");
  /* bieeeeeeeeen! */
  return 0;
}

/* Método close */
/* Libero recursos al cerrar el dispositivo */
int i2c_close (struct inode * pinodo, struct file * archivo){
	printk(KERN_INFO "ei_i2c: Entro al close\n");
	free_page((unsigned long) i2c_data.prx_buff);
	printk(KERN_ALERT "ei_i2c: Cerrando dispositivo\n");
	return 0;
}

/* Handler I2C */
irqreturn_t i2c_int_handler(int irq, void *dev_id, struct pt_regs *regs){
  unsigned int int_data_ready, data_cnt, dat_buf;
	short prev_irq_stat;

  printk(KERN_INFO "ei_i2c: Entro al handler\n");

  /* Pasos: revisar si es por TX o RX */
  /* Lo veo en IRQ Status */
  int_data_ready = ioread32(i2c_data.pi2c + I2C_IRQSTATUS);

  //Si es RX
  if(int_data_ready & I2C_IRQSTAT_RRDY){
		printk(KERN_INFO "ei_i2c: Entro al RX handler\n");
    /* Si entro acá es porque estoy recibiendo datos -> los copio en buffer de Rx */
		/* Pongo un spinlock para deshabilitar interrupciones locales */
		spin_lock (&ei_i2c_rx_lock);
		dat_buf = (ioread32(i2c_data.pi2c + I2C_DATA));   					//Leo la data
		i2c_data.prx_buff[i2c_data.rx_buff_pos] = (dat_buf & 0xFF);	//La guardo y "limpio"
		i2c_data.rx_buff_pos++;																			//Aumento el índice de posición
		if(i2c_data.rx_buff_pos == PAGE_SIZE)												//Verifico si llegué al final de la pág y reseteo
			i2c_data.rx_buff_pos--;
		spin_unlock (&ei_i2c_rx_lock);	//Libero el spinlock

    /* termino de recibir? */
    if(!(data_cnt = (ioread32(i2c_data.pi2c + I2C_CNT) & 0xFFFF))){
			/* Desactivo Rx IRQ */
			iowrite32(I2C_IRQENABLE_RRDY, i2c_data.pi2c + I2C_IRQENABLE_CLR);
			/* Transfer complete */
			cond_wake_up_rx = 1;
			wake_up_interruptible(&ei_i2c_rx_q);				//Despierto para el método read
		}
  } //listo if rxrdy

  //Si es TX
  if(int_data_ready & I2C_IRQSTAT_XRDY){
		printk(KERN_INFO "ei_i2c: Entro al TX handler\n");

		/* Mando el dato por el fifo */
		//iowrite32((i2c_data.ptx_buff[i2c_data.tx_buff_pos] & 0xFF), i2c_data.pi2c + I2C_DATA);
		iowrite32((int)i2c_data.ptx_buff[i2c_data.tx_buff_pos], i2c_data.pi2c + I2C_DATA);					//Escribo lo que quiero
		i2c_data.tx_buff_pos++;
		printk(KERN_INFO "ei_i2c: IRQ Tx: escribio\n");

		/* Si entro acá es porque estoy transmitiendo datos -> los escribo en buffer de salida */
		/* Me fijo si llegue al final del buffer*/
		if ( i2c_data.tx_buff_size == i2c_data.tx_buff_pos ){
			/* Desactivo la IRQ de TX */
			prev_irq_stat = ioread16(i2c_data.pi2c + I2C_IRQENABLE_CLR);													//Leo seteo anterior
			iowrite16 (prev_irq_stat | I2C_IRQENABLE_XRDY, i2c_data.pi2c + I2C_IRQENABLE_CLR);		//Pongo en 1 para limpiar y resetear la IRQ
			/* Ya lo voy despertando */
			cond_wake_up_tx=1;
			wake_up_interruptible(&ei_i2c_tx_q);			//Despierto para el método write

			printk(KERN_INFO "ei_i2c: IRQ TX Desperto\n");
		}
	} //listo if txrdy

  /* Limpio las interrupciones así no vuelve a entrar */
  iowrite32(0xFFFF, i2c_data.pi2c + I2C_IRQSTATUS);

	printk(KERN_INFO "ei_i2c: IRQ Tx: se esta por ir\n");

  /*Vuelvo con la IRQ atendida*/
	return IRQ_HANDLED;
}

/* Método wirte */
ssize_t i2c_write (struct file * archivo, const char __user * data_user, size_t size, loff_t * poffset){
	int aux;

  printk(KERN_INFO "ei_i2c: Entro al write\n");

	/* Verifico si el buffer que me envío es de como máximo una página */
	if(size > PAGE_SIZE){
		printk(KERN_ALERT "ei_i2c: Por el momento se puede leer solo una pagina de memoria\n");
		return -ENOMEM;
	}

	/* Verifico si el buffer que me envío el usuario está bien */
	//access_ok verifica si el puntero al bloque de memoria en user space es válido o no
	if(!(access_ok(VERIFY_WRITE, data_user, size))){
		printk(KERN_ALERT "ei_i2c: Buffer Tx invalido\n");
		return -ENOMEM;
	}

	/* El bloque está bien, entonces pido memoria para el buffer */
	if((i2c_data.ptx_buff = (char*)kmalloc(size,GFP_KERNEL)) == NULL){
		printk(KERN_ALERT "ei_i2c: No hay memoria disponible para el buffer Tx\n");
		return -ENOMEM;
	}

	/* Copio los datos a enviar a la memoria de kernel */
	if((aux = __copy_from_user(i2c_data.ptx_buff, data_user, size)) < 0){
		printk(KERN_ALERT "ei_i2c: Error al copiar el buffer de usuario al kernel\n");
		return -ENOMEM;
	}

	/* Seteo cuanto es lo que tengo que copiar */
	i2c_data.tx_buff_size = size;
	i2c_data.tx_buff_pos = 0;

	printk(KERN_ALERT "ei_i2c: cant: %d pos: %d\n",i2c_data.tx_buff_size,i2c_data.tx_buff_pos);
	/* Espero por el recurso del bus de datos */
	while( ioread32(i2c_data.pi2c + I2C_IRQSTATUS_RAW) & 0x1000 ){
		msleep(1);
	}

	iowrite32(size, i2c_data.pi2c + I2C_CNT); //Indico cantidad a escribir
	printk(KERN_ALERT "ei_i2c: Llene cantidad=%d\n",size);

	iowrite32(0x77,i2c_data.pi2c + I2C_SA); //Seteo Slave addr

	aux = 0;
	aux = ioread32(i2c_data.pi2c + I2C_CON);
	aux |= 1 << 10; // Master mode
	aux |= 1 << 9; // Transmit mode
	aux |= 1 << 15; // I2C_Enable

	iowrite32(aux, i2c_data.pi2c + I2C_CON);
	printk(KERN_ALERT "ei_i2c: Master mode y transmit mode seteados\n");

	/* Activo la IRQ y mando Start */
	iowrite32(0x10, i2c_data.pi2c + I2C_IRQENABLE_SET);
	printk(KERN_ALERT "ei_i2c: Active INT Tx\n");

	aux = ioread32(i2c_data.pi2c + I2C_CON);
	aux |= 1 << 0;
	iowrite32((int) aux, i2c_data.pi2c + I2C_CON); // Start condition
	printk(KERN_ALERT "ei_i2c: Mande start condition y me voy a dormir en Tx\n");

	/* Pongo a dormir e proceso hasta que termine de enviarse el dato */
	if((aux = wait_event_interruptible(ei_i2c_tx_q, cond_wake_up_tx > 0)) < 0){
		printk(KERN_ALERT "ei_i2c: Error en wait de Tx\n");
		return aux;
	}

	printk(KERN_ALERT "ei_i2c: Desperte en Write\n");
	cond_wake_up_tx = 0;

	/* Mando stop */
	aux = ioread32(i2c_data.pi2c + I2C_CON);
	aux |= 1 << 1;
	iowrite32((int)aux, i2c_data.pi2c + I2C_CON);

	printk(KERN_ALERT "ei_i2c: Mande condicion de stop\n");
	/* Libero la memoria que pedí */
	kfree(i2c_data.ptx_buff);

	return size;
}

/* Método read */
ssize_t i2c_read (struct file * archivo, char __user * data_user, size_t size, loff_t * poffset){
	int aux;

  printk(KERN_INFO "ei_i2c: Entro al read\n");

	/* Verifico si el buffer que me envío es de como máximo una página */
	if(size > PAGE_SIZE){
		printk(KERN_ALERT "ei_i2c: Por el momento se puede leer solo una pagina de memoria\n");
		return -ENOMEM;
	}

	/* Verifico si el buffer que me envío el usuario está bien */
	if(!(access_ok(VERIFY_WRITE, data_user, size))){
		printk(KERN_ALERT "ei_i2c: Buffer Rx invalido\n");
		return -ENOMEM;
	}

	printk(KERN_ALERT "ei_i2c: Entre al read, chequie tamanio y consistencia. Voy a esperar por recurso\n");

	/* Configuro lectura*/
	i2c_data.rx_buff_pos = 0;

	iowrite32(size, i2c_data.pi2c + I2C_CNT);

	iowrite32(0x77,i2c_data.pi2c + I2C_SA); //Seteo Slave addr

	aux = 0;
	aux = ioread32(i2c_data.pi2c + I2C_CON);
	aux |= 1 << 10; // Master mode
	aux &= ~(1 << 9); // Receive mode
	aux |= 1 << 15; // I2C_Enable
	iowrite32(aux, i2c_data.pi2c + I2C_CON);
	printk(KERN_ALERT "ei_i2c: Master mode y Receive mode seteados\n");

	/* Activo la IRQ de Rx y mando start */
	printk(KERN_ALERT "ei_i2c: Por activar INT Rx\n");

	aux = 0;
	aux = ioread32(i2c_data.pi2c + I2C_IRQENABLE_SET);
	aux |= 1 << 3;
	iowrite32(aux, i2c_data.pi2c + I2C_IRQENABLE_SET);

	aux = 0;
	aux = ioread32(i2c_data.pi2c + I2C_CON);
	aux |= 1;
	iowrite32((int) aux, i2c_data.pi2c + I2C_CON);

	printk(KERN_ALERT "ei_i2c: Me voy a dormir en Rx\n");
	/* Pongo a dormir el proceso hasta que termine de enviarse el dato */
	if((aux = wait_event_interruptible(ei_i2c_rx_q, cond_wake_up_rx > 0)) < 0){
		printk(KERN_ALERT "ei_i2c: Error en wait de Rx\n");
		return aux;
	}
	printk(KERN_ALERT "ei_i2c: Desperte\n");
	cond_wake_up_rx = 0;

	/* Mando stop */
	aux = 0;
	aux = ioread32(i2c_data.pi2c + I2C_CON);
	//aux |= 1;
	aux |= 1 << 1;
	iowrite32((int)aux, i2c_data.pi2c + I2C_CON);

	printk(KERN_ALERT "ei_i2c: Mande condicion de stop\n");

	/* Copio del buffer de read */
	if((aux = __copy_to_user(data_user, i2c_data.prx_buff, size)) < 0){
		printk(KERN_ALERT "ei_i2c: Error al copiar el buffer de Rx al espacio de usuario\n");
		return -ENOMEM;
	}

	return size;
}

/* insmod y rmmod */
module_init(i2c_init);                //Indico qué funciones ejecutan los comandos anteriores respectivamente
module_exit(i2c_exit);
