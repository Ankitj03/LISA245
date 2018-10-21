
#ifdef __USE_CMSIS
#include "LPC17xx.h"
#endif

#include <cr_section_macros.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include "lbc.h"

#define START_BYTES 32
#define DATA_BYTES  32
#define BUFFER_SIZE 1024
#define CORRUPTION_RANGE (1*8)

#define TxRxPort 0
#define Tx_pin 1
#define Rx_pin 0
#define LED_Tx_pin 2
#define LED_Rx_pin 2


#define delayval 0.5
#define ORDER 5
#define DELAY1 100
#define DELAY2 60000


#define AJ 1

static char lookup[START_BYTES] ={ 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
                                  	  0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D,0x5E, 0x5F,
									  0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
									  0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF
                                	};

signed int front = -1;
signed int rear = -1;
char *queue = NULL;
uint32_t matchedIndex = 0;
signed int lookupPosition = -1;
signed int found = 0;
int rx_size;

//Initialize the port and pin as outputs.
void GPIOinitOut(uint8_t portNum, uint32_t pinNum)
{
	if (portNum == 0)
	{
		LPC_GPIO0->FIODIR |= (1 << pinNum);
	}
	else if (portNum == 1)
	{
		LPC_GPIO1->FIODIR |= (1 << pinNum);
	}
	else if (portNum == 2)
	{
		LPC_GPIO2->FIODIR |= (1 << pinNum);
	}
	else
	{
		//puts("Not a valid port!\n");
	}
}

//Initialize the port and pin as input.
void GPIOinitIn(uint8_t portNum, uint32_t pinNum)
{
	if (portNum == 0)
	{
		LPC_GPIO0->FIODIR &= ~(1 << pinNum);
	}
	else if (portNum == 1)
	{
		LPC_GPIO1->FIODIR &= ~(1 << pinNum);
	}
	else if (portNum == 2)
	{
		LPC_GPIO2->FIODIR &= ~(1 << pinNum);
	}
	else
	{
		puts("Not a valid port!\n");
	}
}

void setGPIO(uint8_t portNum, uint32_t pinNum)
{
	if (portNum == 0)
	{
		LPC_GPIO0->FIOSET = (1 << pinNum);
	}
	else if (portNum == 1)
	{
		LPC_GPIO1->FIOSET = (1 << pinNum);
	}
	else if (portNum == 2)
	{
		LPC_GPIO2->FIOSET = (1 << pinNum);
	}
	else
	{
		printf("Not Valid port to set!\n");
	}
}

//Deactivate the pin
void clearGPIO(uint8_t portNum, uint32_t pinNum)
{
	if (portNum == 0)
	{
		LPC_GPIO0->FIOCLR = (1 << pinNum);
	}
	else if (portNum == 1)
	{
		LPC_GPIO1->FIOCLR = (1 << pinNum);
	}
	else if (portNum == 2)
	{
		LPC_GPIO2->FIOCLR = (1 << pinNum);
	}
	else
	{
		printf("Not Valid port to clear!\n");
	}
}

// Generate delay at Tx/Rx side
/*void delay(unsigned long int count)
{
    unsigned long int i;
	for(i = 0; i < count; i++);
}*/

void delay(uint32_t count)
{
	LPC_TIM0->TCR = 0x02;
	LPC_TIM0->PR = 0x00;
	LPC_TIM0->MR0 = count*delayval*(9000000 / 1000-1);
	LPC_TIM0->IR = 0xff;
	LPC_TIM0->MCR = 0x04;
	LPC_TIM0->TCR = 0x01;
	while (LPC_TIM0->TCR & 0x01);
	return;
}



char *integerToBinary2(char data)
{
	char *str = (char *)malloc(9);
	int i = 0;
	for(i=7; i>=0; i--)
		str[i] = ((data >> i) & 1);
    str[8] = '\0';
	return str;
}

char binaryToString(char *data)
{
    int i;
    int d = 0;
    for(i = 0; i < 8; i++)
    {
        d += pow(2, i)*data[i];
    }
    int c = d;
    return c;
}

void enqueue(char data)
{
    front++;
    if(front == (BUFFER_SIZE - 1)){
        printf("Queue Full\n");
        return;
    }
    queue[front] = data;
}

void enqueueBinary(char data)
{
	char *str = integerToBinary2(data);

	int i = 0;
	for(i=0; i<8; i++)
	{
		enqueue(str[i]);
	}
}

char dequeue()
{
   rear++;
   return queue[rear];
}

int isQueueFull()
{
    if(rear < front)
        return 1;
    else
        return 0;
}

// Add sync data in binary format to queue
void addStartPattern()
{
    int i;
    for(i=0; i<START_BYTES; i++){
    	enqueueBinary(lookup[i]);
    }
}

// Add data in binary format to queue
void addData(char *data)
{
    while(*data != '\0'){
    	enqueueBinary(*data);
        data++;
    }
}

// Calculate offset for payload once sync pattern is found
int calculateOffset(char data)
{
    unsigned int lowerNibble = 0;
    unsigned int upperNibble = 0;
    unsigned int offset = 0;
    
    lowerNibble = (data & 0x0F);
    upperNibble = (((data>>4) & 0x0F));    
    
    if(upperNibble == 5){
        offset += 16;    
    }
    offset += (15 - lowerNibble);
    
    return (offset*8);
}

// Make data with sync pattern
int matchStartPattern(char data)
{
    unsigned int i = 0;
    while((i < START_BYTES)  && (lookupPosition == -1)){
        if(data == lookup[i]){
            matchedIndex = i;
            lookupPosition = i;
            return 1;
        }
        i++;
    }
    if(lookupPosition != -1)
    {
    	lookupPosition++;
        if(data == lookup[lookupPosition]){
            matchedIndex = i;
            return 1;
        }
    }
    return 0;
}

// Extarct data from queue and make for sync pattern, if match found return start
// address of payload
void extractData(char *data, unsigned int matchLevel)
{
    unsigned int offset;
    unsigned int flag = 0;
    unsigned int count = 0;
    int match = -1;
    
    int prev[8] = {0};
    unsigned int flag1 = 0;

    while(isQueueFull()){
        unsigned int i = 0;
        unsigned int d = 0;
        if(flag1 == 0)
        {
            for(i = 0; i < 8; i++)
            {
                prev[i] = dequeue();
                d += pow(2, i)*prev[i];
            }
            flag1 = 1;
        }
        else
        {
            for(i = 0; i < 7; i++)
            {
                prev[i] = prev[i+1];
                d += pow(2, i)*prev[i];
            }
            prev[i] = dequeue();
            d += pow(2, i)*prev[i];
        }
        if(matchStartPattern((char)d)){
            if(flag == 0){
                match = matchedIndex; 
                flag = 1;
            }
            flag1 = 0; 
            count++;
        }else{
            match = -1;
            flag = 0;    
            count = 0;
            flag1 = 1;
            lookupPosition = -1;
        }
        
        if(flag == 1 && (count == matchLevel)){
            printf("Pattern found at index %d\n", match);
            offset = calculateOffset(d);
            printf("Offset = %d\n", offset);
            matchedIndex = 0;
            lookupPosition = -1;
            found = 1;
            break;
        }
    }

    while(offset--){
        dequeue();
    }
    
    int i = 0;
    while(isQueueFull())
    {
        data[i] = dequeue();
        i++;
        if(i == 7)
        {
        	rx_size = binaryToString(data);
        	printf("%d\n", rx_size);
        }
        if(i == (rx_size*12 + 8))
        	break;
    }
//    data[i] = '\0';
}

// Corrupt sync pattern, This is needed to simulation noise in channel.
void corruptStartSequence()
{
    srand(time(NULL));
    int i;
    for(i=0; i < 5; i++){
        int corruptionIndex = rand()%CORRUPTION_RANGE;
        printf("Corrupt Index: %d\n", corruptionIndex);
        queue[corruptionIndex] = ~queue[corruptionIndex];
    }
}

void scramble_data(int order, char *s_data, int data_size)
{
	int i = 0;
	char x1, x2;
	for(i = 0; i < data_size; i++)
	{
		if(i >= ((order/2) + 1))
		{
			x1 = s_data[i - ((order/2) + 1)];
		}
		else
		{
			x1 = 0;
		}
		if(i >= order)
		{
			x2 = s_data[i - order];
		}
		else
		{
			x2 = 0;
		}
		s_data[i] = (queue[(33*8) + i] ^ x1 ^ x2);
	}
}

void descramble_data(int order, char *des_data, char *data, int data_size)
{
	int i = 0;
	char x1, x2;
	for(i = 0; i < data_size; i++)
	{
		if(i >= ((order/2) + 1))
		{
			x1 = data[i - ((order/2) + 1)];
		}
		else
		{
			x1 = 0;
		}

		if(i >= order)
		{
			x2 = data[i - order];
		}
		else
		{
			x2 = 0;
		}
		des_data[i] = (x1 ^ x2) ^ data[i];
	}
}

void lbc_encode(char *dest, int data_size, char *src)
{
    char G[8][12] = {G0, G1, G2, G3, G4, G5, G6, G7};
    int k;
    int z = 0;
    for(k = 0; k < (data_size); k+=8)
    {
        int i, j;
        for(i = 0; i < 12; i++)
        {
            for(j = 0; j < 8; j++)
            {
                dest[z + i] += (G[j][i]*src[k + j]);
            }
            dest[z + i] = (dest[z + i]%2);
        }
        z += 12;
    }
}

bool check_error(int data_size, char *temp)
{
    char T[12][4] = {T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11};
    char R[1024] = {0};

    int k;
    int z = 0;
    for(k = 0; k < (data_size); k+=12)
    {
        int i, j;
        for(i = 0; i < 4; i++)
        {
            for(j = 0; j < 12; j++)
            {
                R[z + i] += (T[j][i]*temp[k + j]);
            }
            R[z + i] = (R[z + i]%2);
            if(R[z + i] == 1)
            {
            	//printf("i=%d z=%d k=%d\n",i, z, k);
                return false;
            }
        }
        z += 4;
    }
    return true;
}

void lbc_decode(char *dest, int data_size, char *src)
{
    int i = 0;
    int k = 0;
    for(i = 0; i < (data_size); i += 12)
    {
        int j = 0;
        for(j = 0; j < 8; j++)
        {
            dest[j + k] = src[i + j];
        }
        k +=8;
    }
}

bool Tx()
{
	char txStr[32];
	printf("Data to be transmitted = ");
	scanf("%s", &txStr);
	getchar();
	getchar();
#ifdef AJ
	printf("Start Tx Logic\n");
	//char *txStr= "Abhishek011413311";
	front = -1;
    GPIOinitOut(TxRxPort, LED_Tx_pin);
    GPIOinitOut(TxRxPort, Tx_pin);
	clearGPIO(TxRxPort, LED_Tx_pin);
	if(queue == NULL)
	{
		queue = (char *)malloc(sizeof(char)*BUFFER_SIZE);
		if(queue == NULL)
		{
        //return -1;
		}
	}
    char *data = (char *)malloc(sizeof(char)*DATA_BYTES*8);
    if(data == NULL){
        //return -1;
    }
    printf("Tx: %s\n", txStr);
    strcpy(data, txStr);
   	addStartPattern();
   	char len = (strlen(txStr) + 2);
   	printf("%d\n", len);
   	enqueueBinary(len);
   	enqueueBinary('A');
   	enqueueBinary('D');
   	addData(data);

//   	// Scrambling
//   	char s_data[1024];
//   	scramble_data(ORDER, s_data, len*8);
//   	memcpy(queue + 256 + 8, s_data, len*8);
//
//   	// LBC encoding
//   	char lbc_data[2048] = {0};
//   	lbc_encode(lbc_data, len*8, queue + 256 + 8);
//   	memcpy(queue + 256 + 8, lbc_data, len*12);

   	//queue[290] = !queue[290]; //To introduce 1 bit error

    int i = 0;
    for(i = 0; i < (front + len*4); i++)
   	{
   		if(queue[i])
   		{
   			setGPIO(TxRxPort, LED_Tx_pin);
   			setGPIO(TxRxPort, Tx_pin);
   			delay(DELAY1);
   		}
   		else
   		{
   			clearGPIO(TxRxPort, LED_Tx_pin);
   			clearGPIO(TxRxPort, Tx_pin);
   			delay(DELAY1);
   		}
   	}
	clearGPIO(TxRxPort, LED_Tx_pin);
	clearGPIO(TxRxPort, Tx_pin);
   	GPIOinitOut(TxRxPort, LED_Rx_pin);
    GPIOinitIn(TxRxPort, Rx_pin);
	clearGPIO(TxRxPort, LED_Rx_pin);

	front = 0;
	memset(queue, 0, BUFFER_SIZE);
   	memset(data, 0, DATA_BYTES*8);
   	delay(DELAY2);
   	printf("TX Done\n");

   	printf("Start Rx Logic\n");
   	while(1)
	{
		if (LPC_GPIO0->FIOPIN & (1 << Rx_pin)) //Wireless
		{
			queue[front] = 1;
			setGPIO(TxRxPort, LED_Rx_pin);
		}
		else
		{
			queue[front] = 0;
			clearGPIO(TxRxPort, LED_Rx_pin);
		}

		front++;
		delay(DELAY1);
		if(front == (BUFFER_SIZE - 1))
		{
			break;
		}
	}
	rear = -1;
	lookupPosition = -1;
	extractData(data, 1); // Extract actual data from stream of data

//	// LBC decoding
//   	if(check_error(rx_size*12, lbc_data))
//   	{
//   		char lbc_received_data[1024] = {0};
//   		lbc_decode(lbc_received_data, rx_size*12, data + 8);
//   		memcpy(data, lbc_received_data, rx_size*8);
//   	}
//   	else
//   	{
//   		printf("Error!!\n");
//   		char lbc_received_data[1024] = {0};
//   		lbc_decode(lbc_received_data, rx_size*12, data + 8);
//   		memcpy(data, lbc_received_data, rx_size*8);
//   		//return false;
//   	}
//
//	// Descrambling
//	char des_data[1024];
//	descramble_data(ORDER, des_data, data, rx_size*8);
//	memcpy(data, des_data, rx_size*8);


	char *ptr;
	int k = 0;
	char *data_received = (char *)malloc(sizeof(char)*(rx_size - 2 + 1));
	int z = 0;
	printf("Rx: ");
	for(ptr = (data + 16); k < ((rx_size - 2)*8); k+=8)
	{
		data_received[z++] = binaryToString(ptr);
		ptr += 8;
	}
	data_received[z] = '\0';
	printf("%s\n", data_received);
	memset(queue, 0, BUFFER_SIZE);
	clearGPIO(TxRxPort, LED_Rx_pin);
	printf("Rx Done!\n");
#else
#endif
	return true;
}

void Rx()
{
	getchar();
	getchar();
#ifdef AJ
	// Rx Logic
	printf("Start Rx Logic\n");
   	GPIOinitOut(TxRxPort, LED_Rx_pin);
    GPIOinitIn(TxRxPort, Rx_pin);
	clearGPIO(TxRxPort, LED_Rx_pin);
	if(queue == NULL)
	{
		queue = (char *)malloc(sizeof(char)*BUFFER_SIZE);
		if(queue == NULL){
			//return -1;
		}
	}

    char *data = (char *)malloc(sizeof(char)*DATA_BYTES*8);
    if(data == NULL){
        //return -1;
    }

	memset(queue, 0, BUFFER_SIZE);
   	memset(data, 0, DATA_BYTES*8);
   	front = 0;
   	// Recieve data
   	while(1)
	{
		if (LPC_GPIO0->FIOPIN & (1 << Rx_pin)) //Wireless
		{
			queue[front] = 1;
			clearGPIO(TxRxPort, LED_Rx_pin);
		}
		else
		{
			queue[front] = 0;
			setGPIO(TxRxPort, LED_Rx_pin);
		}

		front++;
		delay(DELAY1);
		if(front == (BUFFER_SIZE - 1))
		{
			break;
		}
	}
	rear = -1;
	lookupPosition = -1;
	extractData(data, 1); // Extract actual data from stream of data

	// LBC decoding
//   	if(check_error(rx_size*12, data + 8))
//   	{
//   		char lbc_received_data[1024] = {0};
//   		lbc_decode(lbc_received_data, rx_size*12, data + 8);
//   		memcpy(data, lbc_received_data, rx_size*8);
//   	}
//   	else
//   	{
//   		printf("Error!!\n");
//   		char lbc_received_data[1024] = {0};
//   		lbc_decode(lbc_received_data, rx_size*12, data + 8);
//   		memcpy(data, lbc_received_data, rx_size*8);
//   		//return;
//   	}
//
//	// Descrambling
//	char des_data[1024];
//	descramble_data(ORDER, des_data, data, rx_size*8);
//	memcpy(data, des_data, rx_size*8);

	printf("Rx: ");
	char *ptr;
	int k = 0;
	char *data_received = (char *)malloc(sizeof(char)*(rx_size - 2 + 1));
	int z = 0;
	for(ptr = (data + 16); k < ((rx_size - 2)*8); k+=8)
	{
		data_received[z++] = binaryToString(ptr);
		ptr += 8;
	}
	data_received[z] = '\0';
	printf("%s\n", data_received);
	if(!strcmp(data_received, "ABHISH"))
	{
		printf("Plz Retransmit\n");
		return;
	}


	// Tx Logic
	printf("Start Tx Logic\n");
	char *txStr= "Abhishek011413311";
	printf("Tx: %s\n", txStr);
	memset(queue, 0, BUFFER_SIZE);
	memset(data, 0, DATA_BYTES*8);



	clearGPIO(TxRxPort, LED_Rx_pin);
    GPIOinitOut(TxRxPort, LED_Tx_pin);
    GPIOinitOut(TxRxPort, Tx_pin);
	clearGPIO(TxRxPort, LED_Tx_pin);

	front = -1;
    strcpy(data, txStr);
   	addStartPattern();
   	char len = (strlen(txStr) + 2);
   	printf("%d\n", len);
   	enqueueBinary(len);
   	enqueueBinary('A');
   	enqueueBinary('D');
   	addData(data);

   	// Scrambling
//   	char s_data[1024];
//   	scramble_data(ORDER, s_data, len*8);
//   	memcpy(queue + 256 + 8, s_data, len*8);
//
//   	// LBC encoding
//   	char lbc_data[2048] = {0};
//   	lbc_encode(lbc_data, len*8, queue + 256 + 8);
//   	memcpy(queue + 256 + 8, lbc_data, len*12);

   	//queue[270] = !queue[270]; //To introduce 1 bit error

    int i = 0;
    for(i = 0; i < (front + len*4); i++)
   	{
   		if(queue[i])
   		{
   			setGPIO(TxRxPort, LED_Tx_pin);
   			setGPIO(TxRxPort, Tx_pin);
   			delay(DELAY1);
   		}
   		else
   		{
   			clearGPIO(TxRxPort, LED_Tx_pin);
   			clearGPIO(TxRxPort, Tx_pin);
   			delay(DELAY1);
   		}
   	}
	clearGPIO(TxRxPort, LED_Tx_pin);
	clearGPIO(TxRxPort, Tx_pin);
	printf("Tx Done!\n");
#else
#endif
}


int main(void)
{
	printf("Welcome to LISA!\n");
	while(1)
	{
		printf("Select:\n1. Transmitter\n2. Receiver\n Your Choice = ");
		int option = 0;
		scanf("%d", &option);
		switch(option)
		{
			case 1: printf("Tx selected\n");
					Tx();
					break;
			case 2: printf("Rx selected\n");
					Rx();
					break;
			default:printf("Invalid choice! Try again\n");

		}
		fflush(stdin);
	}
    return 0;
}
