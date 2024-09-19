/*main.c*/
/**** Краткое описание работы
* Реализовано удалённое управление встроенной системой через UART терминал (посредством программы Termite или любой другой).
* После сброса на LCD отображается палитра 16 цветов, и если ранее уже был сохранены имя и цвет, то соответствующая строка
* цвета мигает ранее сохранённым именем.
* На удалённом терминале UART выводится информация и после ввода любой последовательности символов и передачи по UART 
* запускается диалог в котором пользователь сначала вводит имя, а затем номер цвета, и при корректном вводе данные сохраняются в F-RAM.
* Далее после сброса МК (отключения питания) мигает уже ранее введенное имя и строка. При обмене данными на LCD МК выводится принятый пакет данных.
* Настройки UART: скорость 230400 бит/сек; размер кадра 8 бит; стоповый бит 1;  контроля чётности нет.
* Настройки I2C: 
****/
#include "main.h"
#include "frames.h"
#include "game.h"
#include <time.h>
#include "AsciiLib.h"
#include "LCD_ILI9325.h"
#include <stdio.h>
#include <string.h>

#define BUFSZ 64 // Размеры буфферов для приёма/передача байт данных
#define SLAVE_OWN_ADDRESS 0xA0 	// Адресс F-RAM согласно тех. описанию FM24CL16B
#define ADDR_IN_FRAM 0x18	   	// Адрес по которому храним данные в F-RAM
#define KEYVALID 0x12345678		//Ключ валидации данных считанных из F-RAM

void SystemClock_Config(void); // Установка частоты тактирования системы 72 МГц
static __IO uint32_t cnt1ms; // счётчик милисекунд
uint8_t aTxDMABuf[BUFSZ] = "\nSTM32F3D USART2 LL API Example : TX/RX in DMA mode\n";//Буффер передачи UART
__IO uint8_t aRxDMABuf[BUFSZ]; 	// Буффер приёма UART
__IO uint8_t aRxBuf[BUFSZ];		// Буффер куда копируем принятый пакет по UART
__IO uint8_t ubRxsz=0;			// Количество байт в пакете
__IO uint8_t ubErrflag=0;		// Флаг ошибки UART, DMA

void init_UART2_DMA(void);		// Инициализация каналов DMA
void uart_transmit(uint8_t* pBuffer, uint8_t ublenBuff); // Функция запуска передачи пакета по UART с использование DMA
void init_I2C_FRAM(void);		// Инициализация интерфейса I2C связи с памятью F-RAM
uint32_t FM24CL16BG_wr(uint16_t uwWriteAddr, uint8_t* pBuffer, uint8_t ubNumByteToWrite);//Запись пакета данных в память F-RAM по интерфейсу I2C
uint32_t FM24CL16BG_rd(uint16_t uwReadAddr, uint8_t* pBuffer, uint8_t ubNumByteToRead);//Чтение пакета данных из памяти F-RAM по интерфейсу I2C
void LCDprint_buf(uint8_t* pBuffer,uint32_t uiSize); //Печать полученного пакета на экран LCD
void setupButtons(void);

List run(List);
void drawScene(List);
void drawEgg(struct Egg*);
int freq = 4;
char scoreText[6] = {':', 'e', 'r', 'o', 'c', 'S'};
char lifesText[6] = {':', 's', 'e', 'f', 'i', 'L'};
char startText[] = "Press button to start!";
char* printStartText;
char gameOver[] = "Game over!";
char* printGameOver;
char yourScore[] = "Your score:";
char* printYourScore;
char* convertToChar(int n);
char* reverseString(char*);
int isStarted = 0;

int main(void)
{
	uint32_t i;
    typedef struct {
        char cname[30];
		uint32_t uiKey; // Ключ достоверности данных
        uint8_t  ubNcolr;
    } Ttypename; // структура хренения параметров отображения 
    Ttypename tname; // объект структуры
	enum Tstate {
		NOinit, // состояние после сброса
		TXname, // состояние передача запроса имени
		RXname, // состояние ожидание ответа по запросу имени    
		TXcolr, // состояние передача запроса номера цвета
		RXcolr, // состояние ожидание ответа по запросу номера цвета     
		SAVE,	// состояние сохранения полученных данные в F-RAM  
		END		// состояние бездействия
	}; // состояния автомата
	enum Tstate FSMstate=NOinit;
	SystemClock_Config();
	LL_SYSTICK_EnableIT();
	gpio_lcd_init();
	init_lcd_ili9325();
	init_UART2_DMA();
	init_I2C_FRAM();
    setupButtons();
    
    printStartText = reverseString(startText);
    printGameOver = reverseString(gameOver);
    printYourScore = reverseString(yourScore);
    
    lcd_fill_color(0,0,240,320,LCD_COLOR_WHITE);
    int v = 0;
    int x = 75;
    while (printStartText[v] != '\0') {
        LCDprintChar(printStartText[v], 110, x, LCD_COLOR_WHITE ,LCD_COLOR_BLACK, 1);
        x+=8;
        v++;
    }
    
    while (isStarted == 0) { };
    
    list = listInitialization();
    drawScene(list);
    int t = 0;
    while (isStarted == 1) {
        t++;
        if (t == 3600000) {
            list = run(list);
            drawScene(list);
            t = 0;
        }
    }
    lcd_fill_color(70,40,170,280,LCD_COLOR_WHITE);
    v = 0; x = 120;
    while (printGameOver[v] != '\0') {
        LCDprintChar(printGameOver[v], 110, x, LCD_COLOR_WHITE ,LCD_COLOR_BLACK, 1);
        x+=8;
        v++;
    }
}

void drawEgg(struct Egg* egg) {
    if (egg == NULL) return;
    switch (egg->direction) {
        case leftUp:
        case leftDown:
            lcd_draw_picture(egg->posX, egg->posY, eggSize[0], eggSize[1], leftEggFrames[egg->frame]);
            break;
        case rightUp:
        case rightDown:
            lcd_draw_picture(egg->posX, egg->posY, eggSize[0], eggSize[1], rightEggFrames[egg->frame]);
            break;
    }
}

List run(List list) {
    list = collision(list, woldDir);
    listFunction(&move, list);
    if (rand() % freq == 0) {
        list = addEgg(list, randomEgg(rand() % 4));
    }
    if (lifes <= 0) {
        isStarted = 0;
    }
    return list;
}

void drawScene(List list) {
    lcd_fill_color(0,0,240,320,LCD_COLOR_WHITE);
    listFunction(&drawEgg, list);
    lcd_draw_picture_by_coords(canvasLeftPos[0], canvasLeftPos[1], leftCanvasSize, staticLeftCanvas);
    lcd_draw_picture_by_coords(canvasRightPos[0], canvasRightPos[1], rightCanvasSize, staticRightCanvas);
    switch (woldDir) {
        case leftUp:
            lcd_draw_picture_by_coords(wolfLeftPos[0], wolfLeftPos[1], upWolfSize, wolfLeftUp);
            break;
        case leftDown:
            lcd_draw_picture_by_coords(wolfLeftPos[0], wolfLeftPos[1], downWolfSize, wolfLeftDown);
            break;
        case rightUp:
            lcd_draw_mirror_picture_by_coords(wolfLeftPos[0], wolfLeftPos[1], upWolfSize, wolfLeftUp);
            break;
        case rightDown:
            lcd_draw_mirror_picture_by_coords(wolfLeftPos[0], wolfLeftPos[1], downWolfSize, wolfLeftDown);
            break;
    }
    char* charList = convertToChar(Score);
    int v = 0;
    int x = 30;
    while (charList[v] != '\0') {
        LCDprintChar(charList[v], 220, x, LCD_COLOR_WHITE ,LCD_COLOR_BLACK, 1);
        x+=8;
        v++;
    }
    free(charList);
    x+=10;
    for (int i = 0; i < 6; i++) {
        LCDprintChar(scoreText[i], 220, x + i * 8, LCD_COLOR_WHITE ,LCD_COLOR_BLACK, 1);
        LCDprintChar(lifesText[i], 220, 260 + i * 8, LCD_COLOR_WHITE ,LCD_COLOR_BLACK, 1);        
    }
    LCDprintChar((char)((lifes % 10) + (int)'0'), 220, 240, LCD_COLOR_WHITE ,LCD_COLOR_BLACK, 1);
}

char* convertToChar(int n) {
    char* c = (char *)malloc(10 * sizeof(char)); 
    int v = 0; //количество цифр в числе n
    //разбиваем на отдельные символы число n
    while (n > 9)
    {
        c[v++] = (n % 10) + '0';
        n = n / 10;
    }
    c[v++] = n + '0';
    c[v] = '\0';
    return c;
}

char* reverseString(char* string) {
    int i = 0;
    while (string[i] !='\0')
        i++;
    char* c = (char *)malloc((i + 1) * sizeof(char));
    c[i] = '\0';
    int m = 0;
    for (int j = i - 1; j >= 0; j--) {
        c[j] = string[m];
        m++;
    }
    return c;
}

/**** Вспомогательные функции ****/

void setupButtons(void) {
    SET_BIT(RCC ->APB2ENR, RCC_APB2ENR_SYSCFGEN);//разрешаем тактирование SYSCFG  
    SET_BIT(RCC -> AHBENR, RCC_AHBENR_GPIOCEN);  //GPIOC  
    CLEAR_BIT(GPIOC->MODER,GPIO_MODER_MODER4|GPIO_MODER_MODER5|GPIO_MODER_MODER6|GPIO_MODER_MODER7); //PC1,2,4,7 In  S
    SET_BIT(GPIOC->PUPDR,GPIO_PUPDR_PUPDR1_0|GPIO_PUPDR_PUPDR2_0|GPIO_PUPDR_PUPDR4_0|GPIO_PUPDR_PUPDR5_0|GPIO_PUPDR_PUPDR6_0|GPIO_PUPDR_PUPDR7_0);//Pull up PC1,2,4,7  
    SET_BIT(GPIOC->MODER,GPIO_MODER_MODER0_0|GPIO_MODER_MODER2_0);//PC0,3,5,6 Out  
    SET_BIT(GPIOC->OTYPER, GPIO_OTYPER_OT_0|GPIO_OTYPER_OT_2); //режим с открытым стоком  
    SET_BIT(GPIOC->BRR,GPIO_BRR_BR_0|GPIO_BRR_BR_2); //притягиваем к нулю  
    NVIC_SetPriorityGrouping(0xF);   
    NVIC_SetPriority(EXTI4_IRQn,0xF);      
    NVIC_SetPriority(EXTI9_5_IRQn,0xF);   
    SET_BIT(EXTI->FTSR,EXTI_FTSR_FT4|EXTI_FTSR_FT5|EXTI_FTSR_FT6|EXTI_FTSR_FT7);  //разрешаем прерывания внешних линий 2,3,4,6  
    SET_BIT(EXTI->IMR,EXTI_IMR_IM4|EXTI_IMR_IM5|EXTI_IMR_IM6|EXTI_IMR_IM7);  // выбираем в качестве внешних входов EXTI линии:   //EXTI2=PC2 EXTI3=PC3 EXTI4=PC4 EXTI6=PC6       
    SYSCFG->EXTICR[1]=SYSCFG_EXTICR2_EXTI4_PC|SYSCFG_EXTICR2_EXTI5_PC|SYSCFG_EXTICR2_EXTI6_PC|SYSCFG_EXTICR2_EXTI7_PC;  
    NVIC_EnableIRQ(EXTI4_IRQn);  
    NVIC_EnableIRQ(EXTI9_5_IRQn);
    SysTick_Config(0x6DDD00);//прерывание каждые 100мсек 
}

void  EXTI4_IRQHandler(void)   
{ 
    EXTI->PR = EXTI_PR_PR4;
    woldDir = rightUp;
    if (isStarted == 0) {
        isStarted = 1;
    }
} 
void EXTI9_5_IRQHandler(void) 
{ 
    switch(EXTI->PR) {
        case EXTI_PR_PR5:
            EXTI->PR = EXTI_PR_PR5;
            woldDir = rightDown;
            break;
        
        case EXTI_PR_PR6:
            EXTI->PR = EXTI_PR_PR6;
            woldDir = leftDown;
            break;
        case EXTI_PR_PR7:
            EXTI->PR = EXTI_PR_PR7;
            woldDir = leftUp;
            break;
    }
}

void LCDprint_buf(uint8_t* pBuffer,uint32_t uiSize)
{//вывод на LCD шестнадцатиричных кодов и ASCII кодов принятого пакета по UART
    char buf[31]; // Буффер для вывода  240/8=30 символов в строке 
    uint8_t cntrow=1; // всего 320/16=20 строк
    uint32_t i;
    lcd_fill_color(0, 16*3, LCD_WIDTH,16*17, LCD_COLOR_BLACK);
    sprintf(buf,"Size %d byte",uiSize);
    LCDprintstr(buf,0, LCD_COLOR_BLACK  ,LCD_COLOR_WHITE); 
    buf[0]=0;
    for(i=0;i<ubRxsz;i++)
    { // Выводим шестнадцатиричные коды принятых данных	
        char bufsym[4]; 
        sprintf(bufsym,"%02X ",aRxBuf[i]);
        strcat(buf,bufsym);
        if(i%8==7)
        {
            LCDprintstr(buf,16*cntrow++, LCD_COLOR_BLACK  ,LCD_COLOR_WHITE); 
            buf[0]=0;
        }
    }
    if((i-1)%8!=7)
    {
        LCDprintstr(buf,16*cntrow++, LCD_COLOR_BLACK  ,LCD_COLOR_WHITE); 
        buf[0]=0;
    }
    for(i=0;i<ubRxsz;i++)
    { // Выводим ASCII коды принятых данных	
        if( (aRxBuf[i]>=0x20) && (aRxBuf[i] <=0x7E) )
            buf[i%8]=aRxBuf[i];
        else
            buf[i%8]='.';//непечатаемый символ заменяем точкой
        if(i%8==7)
        {
            buf[i%8+1]=0;
            LCDprintstr(buf,16*cntrow++, LCD_COLOR_BLACK  ,LCD_COLOR_WHITE); 
        }
    }
    if((i-1)%8!=7)
    {
        buf[i%8]=0;
        LCDprintstr(buf,16*cntrow++, LCD_COLOR_BLACK  ,LCD_COLOR_WHITE); 
    }
}

void uart_transmit(uint8_t* pBuffer, uint8_t ublenBuff)
{//Передача пакета байт длинны ublenBuff по UART
	if(ublenBuff < BUFSZ)
	{
		while(LL_DMA_IsEnabledChannel(DMA1, LL_DMA_CHANNEL_7)== SET){}// ждём отключения канала (передача завершена)
		strncpy((char*)aTxDMABuf,(char*)pBuffer,ublenBuff);
		LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_7, ublenBuff); //Количество передаваемых байт
		LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_7); //Включаем канал
	}
}

uint32_t FM24CL16BG_wr(uint16_t uwWriteAddr, uint8_t* pBuffer, uint8_t ubNumByteToWrite)
{//Запись пакета байт длинны ubNumByteToWrite в F-RAM по адресу uwWriteAddr
/**** Согласно тех. описанию FM24CL16B имеется 8 страниц по 256 байт итого 2048 байт 	****/
/**** старшие биты адреса [10..8] номер страницы, передаются вместе с адресом приёмника ****/
	uint32_t uiDatcnt = 0;
    if(uwWriteAddr>2047||ubNumByteToWrite>254) //памяти всего 2048 байт 
		return 1;
	// Начало обмена, передаём на шину адрес приёмника, далее передаём 1 байт адреса памяти и байты данных
	LL_I2C_HandleTransfer(I2C1, SLAVE_OWN_ADDRESS|(uwWriteAddr>>7),// Адрес приёмника и старшие 3 бита адреса[10..8]  
	LL_I2C_ADDRSLAVE_7BIT, // Формат адреса приёмника 7 бит
	1+ubNumByteToWrite, //  NBYTES[7:0]=(1+ubNumByteToWrite), количество передаваемых байт 
	LL_I2C_MODE_AUTOEND, // AUTOEND=1, по окончании передачи NBYTES сформировать признак STOP
	LL_I2C_GENERATE_START_WRITE);//сгенерировать признак START и признак записи в приёмник(LSB=0)
	// Ожидаем пока флаг TXIS установиться, т.е. I2C_TXDR опустел
	while(LL_I2C_IsActiveFlag_TXIS(I2C1) == RESET){};
	// Передаём первый байт=адрес[7..0], далее записываемые данные 
	LL_I2C_TransmitData8(I2C1,(uint8_t)uwWriteAddr);
	while (uiDatcnt != ubNumByteToWrite)
	{  // передаём записываемые данные
		while(LL_I2C_IsActiveFlag_TXIS(I2C1) == RESET){}; //Ожидаем TXIS
		// Передаём текущий байт данных в F-RAM
		LL_I2C_TransmitData8(I2C1,pBuffer[uiDatcnt++]);
	}
	// Ожидаем установку флага STOPF (признак STOP передан)
	while(LL_I2C_IsActiveFlag_STOP(I2C1) == RESET){}; 
	LL_I2C_ClearFlag_STOP(I2C1); // Очищаем флаг STOPF 
	return 0;
}

uint32_t FM24CL16BG_rd( uint16_t uwReadAddr, uint8_t* pBuffer, uint8_t ubNumByteToRead)
{//Чтение пакета байт длинны ubNumByteToRead из F-RAM по адресу uwReadAddr
    uint32_t uiDatcnt = 0;
	if(uwReadAddr>2047||ubNumByteToRead>255) //памяти всего 2048 байт 
		return 1;	
    // Начало обмена:передаём адрес приёмника,1 байт (адрес в памяти),признак записи в приёмник(LSB=0),признак STOP не выставляем
	LL_I2C_HandleTransfer(I2C1, SLAVE_OWN_ADDRESS,LL_I2C_ADDRSLAVE_7BIT,1,LL_I2C_MODE_SOFTEND,LL_I2C_GENERATE_START_WRITE);
	while(LL_I2C_IsActiveFlag_TXIS(I2C1) == RESET){};// ждём флаг TXIS 
	LL_I2C_TransmitData8(I2C1,(uint8_t)uwReadAddr); // Передаём адрес[7..0] 
	// Ожидаем установку флага TC: RELOAD=0, AUTOEND=0 и NBYTES=1(передан)
	while(LL_I2C_IsActiveFlag_TC(I2C1) == RESET){};
	// Продолжение обмена: передаём адрес приёмника и старшие 3 бита адреса[10..8]   
    LL_I2C_HandleTransfer(I2C1, SLAVE_OWN_ADDRESS|(uwReadAddr>>7&0xE),LL_I2C_ADDRSLAVE_7BIT,
		ubNumByteToRead, //  NBYTES[7:0]=ubNumByteToWrite, количество передаваемых байт 
		LL_I2C_MODE_AUTOEND, // AUTOEND=1, по окончании передачи NBYTES сформировать признак STOP
		LL_I2C_GENERATE_START_READ);//сгенерировать признак START и признак чтения из приёмника(LSB=1)
    while (uiDatcnt != ubNumByteToRead)
    {
        while(LL_I2C_IsActiveFlag_RXNE(I2C1) == RESET){}// Ожидаем RXNE, получен байт
        pBuffer[uiDatcnt++]= LL_I2C_ReceiveData8(I2C1); // Сохраняем в буффере
    }    
    // Ожидаем установку флага STOPF (признак STOP передан)
    while(LL_I2C_IsActiveFlag_STOP(I2C1) == RESET){} 
	LL_I2C_ClearFlag_STOP(I2C1); // Очищаем флаг STOPF 
    return 0;
}

/**** Функции инициализации ****/
void SystemClock_Config(void)
{ // HCLK(72MHz), PLL (HSE)
	LL_FLASH_SetLatency(LL_FLASH_LATENCY_2); 
	LL_RCC_HSE_Enable(); 
	while(LL_RCC_HSE_IsReady() != 1) {};
	LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE_DIV_1, LL_RCC_PLL_MUL_9);
	LL_RCC_PLL_Enable();
	while(LL_RCC_PLL_IsReady() != 1){};
	LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1); //AHB Prescaler=1
	LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
	while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL){};
	LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2); //APB1 Prescaler=2
	LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1); //APB2 Prescaler=1
	LL_Init1msTick(72000000);
	LL_SetSystemCoreClock(72000000);
}

void init_UART2_DMA(void)
{/**** USART2 приём и передача с использование DMA1 ****/
	// Включаем тактирование ПВВ А, USART2, DMA1
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
	// Настраиваем PA2 - USART2_TX
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_2, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetAFPin_0_7(GPIOA, LL_GPIO_PIN_2, LL_GPIO_AF_7);
	LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_2, LL_GPIO_SPEED_FREQ_HIGH);
	LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_2, LL_GPIO_PULL_DOWN);
	// Настраиваем PA3 - USART2_RX
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_3, LL_GPIO_MODE_ALTERNATE);
	LL_GPIO_SetAFPin_0_7(GPIOA, LL_GPIO_PIN_3, LL_GPIO_AF_7);
	LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_3, LL_GPIO_SPEED_FREQ_HIGH);
	LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_3, LL_GPIO_PULL_UP);
	
	// Настройка канала№7 DMA на передачу
	LL_DMA_ConfigTransfer(DMA1, LL_DMA_CHANNEL_7, 
				LL_DMA_DIRECTION_MEMORY_TO_PERIPH | //Направление передачи из ОЗУ к ПУ
				LL_DMA_PRIORITY_HIGH              | //Приоритет канал№7 высокий
				LL_DMA_MODE_NORMAL                | //Режим работы однократный (не кольцевой)
                LL_DMA_PERIPH_NOINCREMENT         | //Указатель адреса в ПУ неизменен
				LL_DMA_MEMORY_INCREMENT           | //Указатель адреса в ОЗУ увеличивается после каждой передачи байта данных
                LL_DMA_PDATAALIGN_BYTE            | //Размер данных в ПУ - байт
                LL_DMA_MDATAALIGN_BYTE);			//Размер данных в ОЗУ - байт
	LL_DMA_ConfigAddresses(DMA1, LL_DMA_CHANNEL_7, 	//Указание адресов для перемещения данных
                         (uint32_t)aTxDMABuf, 		//Начальный адрес в памяти буффера передачи
                         LL_USART_DMA_GetRegAddr(USART2, LL_USART_DMA_REG_DATA_TRANSMIT),//Адрес регистра USART_TDR  
                         LL_DMA_GetDataTransferDirection(DMA1, LL_DMA_CHANNEL_7));
	LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_7, strlen((char*)aTxDMABuf)); //Количество передаваемых байт
	// Настройка канала№6 DMA на приём
	LL_DMA_ConfigTransfer(DMA1, LL_DMA_CHANNEL_6, 
				LL_DMA_DIRECTION_PERIPH_TO_MEMORY | //Направление передачи от ПУ в ОЗУ
				LL_DMA_PRIORITY_LOW               | //Приоритет канал№6 низкий
				LL_DMA_MODE_CIRCULAR              | //Режим работы кольцевой
				LL_DMA_PERIPH_NOINCREMENT         | //Указатель адреса в ПУ неизменен
				LL_DMA_MEMORY_INCREMENT           | //Указатель адреса в ОЗУ увеличивается после каждого приёма байта данных
				LL_DMA_PDATAALIGN_BYTE            | //Размер данных в ПУ - байт
				LL_DMA_MDATAALIGN_BYTE);            //Размер данных в ОЗУ - байт
	LL_DMA_ConfigAddresses(DMA1, LL_DMA_CHANNEL_6,   	//Указание адресов для перемещения данных
	LL_USART_DMA_GetRegAddr(USART2, LL_USART_DMA_REG_DATA_RECEIVE),//Адрес регистра USART_RDR 
				(uint32_t)aRxDMABuf,			//Начальный адрес в памяти буффера приёма
				LL_DMA_GetDataTransferDirection(DMA1, LL_DMA_CHANNEL_6));
	LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_6, BUFSZ);//Размер кольцевого буфера
 	LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_7); // Разрешение прерывания каналу №7 по завершению передачи
	LL_DMA_EnableIT_TE(DMA1, LL_DMA_CHANNEL_7); // Разрешение прерывания при ошибке обмена
	LL_DMA_EnableIT_TE(DMA1, LL_DMA_CHANNEL_6); // Разрешение прерывания при ошибке обмена
	/**** Настройка параметров USART2(режим UART) ****/
	// Дуплексный режим  чтения/записи TX/RX
	LL_USART_SetTransferDirection(USART2, LL_USART_DIRECTION_TX_RX);
	// Формат кадра данных 8 бит данных, 1 стартовы бит, 1 стоповый бит, бит чётности отключен
	LL_USART_ConfigCharacter(USART2, LL_USART_DATAWIDTH_8B, LL_USART_PARITY_NONE, LL_USART_STOPBITS_1);
	// Передача/приём кадра данных младшими битами вперёд 
	LL_USART_SetTransferBitOrder(USART2, LL_USART_BITORDER_LSBFIRST);
	// Скорость приёма/передачи 230400, частота шины APB1 36МГц
    LL_USART_SetBaudRate(USART2, LL_RCC_GetUSARTClockFreq(LL_RCC_USART2_CLKSOURCE), LL_USART_OVERSAMPLING_16, 230400); 
    LL_USART_EnableIT_IDLE(USART2); // Разрешаем прерывание после прекращения кадров данных
	LL_USART_EnableIT_ERROR(USART2);// Разрешаем прерывание при ошибках USART2 ERROR Interrupt */
	// Разрешаем прерывания 
	NVIC_EnableIRQ(USART2_IRQn);
	NVIC_EnableIRQ(DMA1_Channel7_IRQn);
	NVIC_EnableIRQ(DMA1_Channel6_IRQn);
	// Разрешаем прерывание по приёму/передаче USART2 RX в DMA
    LL_USART_EnableDMAReq_RX(USART2);
	LL_USART_EnableDMAReq_TX(USART2);
	// Включаем в работу каналы DMA, USART2
	LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_7);
	LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_6);
	LL_USART_Enable(USART2);
	
}

void init_I2C_FRAM(void)
{/**** I2C1 приём и передача ****/
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
    LL_I2C_InitTypeDef I2C_InitStruct = {0};
	// GPIO PB6->I2C1_SCL ; PB7->I2C1_SDA
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
	GPIO_InitStruct.Pin = LL_GPIO_PIN_6|LL_GPIO_PIN_7;
	GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
	GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
	GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
	GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
	GPIO_InitStruct.Alternate = LL_GPIO_AF_4;
	LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1); //разрешаем тактирование I2C1
	LL_RCC_SetI2CClockSource(LL_RCC_I2C1_CLKSOURCE_SYSCLK);//источник тактирования SYSCLK
	LL_I2C_EnableAutoEndMode(I2C1);// Признак STOP автоматически отправляется по завершению передачи
	LL_I2C_DisableOwnAddress2(I2C1);// Альтернативный адрес отключен
	LL_I2C_DisableGeneralCall(I2C1);// Широковещательный режим отключен
	LL_I2C_EnableClockStretching(I2C1);//Приёмник может остановить передачу ведущего, прижав линию SCL к земле
	I2C_InitStruct.PeripheralMode = LL_I2C_MODE_I2C;
	/**** 
    * Fast Mode plus 1MHz with I2CCLK = 72 MHz,
    * rise time = 50ns, fall time = 3ns
	* calculate in I2C_Timing_Configuration_V1.0.1 AN4235
	* __LL_I2C_CONVERT_TIMINGS(0, 0x7, 0, 0xD, 0x2A);
	****/
	I2C_InitStruct.Timing = 0x00700D2A;
	I2C_InitStruct.AnalogFilter = LL_I2C_ANALOGFILTER_ENABLE; //Включить аналоговый фильтр
	I2C_InitStruct.DigitalFilter = 0; //Цифровой фильтр отключен
	I2C_InitStruct.OwnAddress1 = 0;	//Собственный адрес равен нулю
	I2C_InitStruct.TypeAcknowledge = LL_I2C_ACK;//Разрешает отправлять ACK/NACK после приема байта
	I2C_InitStruct.OwnAddrSize = LL_I2C_OWNADDRESS1_7BIT;//Формат адреса 7 бит
	LL_I2C_Init(I2C1, &I2C_InitStruct);
	LL_I2C_SetOwnAddress2(I2C1, 0, LL_I2C_OWNADDRESS2_NOMASK);
  
}

/**** Обработчики прерываний ****/
void SysTick_Handler(void)
{
	cnt1ms++;
}

void DMA1_Channel6_IRQHandler(void) 
{
	if(LL_DMA_IsEnabledIT_TE(DMA1,LL_DMA_CHANNEL_6) && LL_DMA_IsActiveFlag_TE6(DMA1))
	{//Прерывание по ошибке приёма
		LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_7);
		LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_6);
		ubErrflag|=0x01;
	}
}

void USART2_IRQHandler(void) 
{
    static uint8_t old_pos=0;
    uint8_t pos;
    if (LL_USART_IsEnabledIT_IDLE(USART2) && LL_USART_IsActiveFlag_IDLE(USART2)) 
	{ // Прерывание простоя при приёме
        LL_USART_ClearFlag_IDLE(USART2);        /* Clear IDLE line flag */
		// Вычисляем позицию в буффере
		pos = BUFSZ - LL_DMA_GetDataLength(DMA1, LL_DMA_CHANNEL_6);
		if(pos!=old_pos)
		{
			if (pos > old_pos)  //Новая позиция выше старой
			{
				ubRxsz=pos-old_pos;//копируем в буффер для вывода на экран
				strncpy((char*)aRxBuf,(char*)&aRxDMABuf[old_pos],ubRxsz);
			} 
			else 
			{//Новая позиция ниже старой
				ubRxsz=BUFSZ - old_pos + pos;
				//копируем в буффер для вывода на экран
				strncpy((char*)aRxBuf,(char*)&aRxDMABuf[old_pos],BUFSZ - old_pos);
				if(pos!=0)
					strncpy((char*)&aRxBuf[BUFSZ - old_pos],(char*)aRxDMABuf,pos);
			}
			old_pos = pos;//запоминаем текующую позицию
		}
    }
	else
	{ // Ошибка приёма/передачи
		NVIC_DisableIRQ(USART2_IRQn);
		ubErrflag|=0x04;
	}
}

void DMA1_Channel7_IRQHandler(void) 
{
	if(LL_DMA_IsEnabledIT_TC(DMA1, LL_DMA_CHANNEL_7) && LL_DMA_IsActiveFlag_TC7(DMA1))
	{// Прерывание по опустошению буффера
		LL_DMA_ClearFlag_GI7(DMA1);
		LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_7);//Передали, отключили канал
	}
	else if(LL_DMA_IsEnabledIT_TE(DMA1,LL_DMA_CHANNEL_7) &&  LL_DMA_IsActiveFlag_TE7(DMA1))
	{ //Прерывание по ошибке передачи
		LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_7);
		LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_6);
		ubErrflag|=0x02;
	}
}
