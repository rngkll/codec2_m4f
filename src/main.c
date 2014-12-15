#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "main.h"
#include "codec2_core.h"
#include "stm32f4_discovery_audio_mic.h"


/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/

/* Audio recording frequency in Hz */
#define MIC_FREQ                          8000
#define SPEAKER_FREQ                      48000


uint16_t *writebuffer;

/* microphone buffer toggler */
extern uint16_t* pAudioRecBuf_8Khz;
extern __IO uint32_t Data_Status;

uint16_t RecBuf_8Khz[PCM_OUT_SIZE*REC_BUFFERS], RecBuf1_8Khz[PCM_OUT_SIZE*REC_BUFFERS];
uint8_t Switch = 0;

/* If transparent mode enabled, codec2+fdmdv will be bypassed */
__IO uint8_t Transparent_mode = 1;

/* Private variables ---------------------------------------------------------*/

RCC_ClocksTypeDef RCC_Clocks;

__IO int32_t fifoBufferCurrent = 0;
__IO int32_t fifoBufferFullness = 0;
#define MODULATOR_QUEUE_SIZE 4 //XXX: this is queue size, use not less than 3! 8 - is good! 6-works too
uint16_t fifoBuffer[MODULATOR_QUEUE_SIZE][(SPEAKER_FREQ/MIC_FREQ)*320*2];
uint16_t padBuffer[(SPEAKER_FREQ/MIC_FREQ)*320*2];
extern __IO uint32_t Time_Rec_Base;

__IO uint32_t TimingDelay = 0;

uint8_t buffer_switch = 1;
__IO uint32_t XferCplt;
__IO int volume_set = 0;


/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

void EVAL_AUDIO_TransferComplete_CallBack(uint32_t pBuffer, uint32_t Size)
{
	/* Calculate the remaining audio data in the file and the new size
	for the DMA transfer. If the Audio files size is less than the DMA max
	data transfer size, so there is no calculation to be done, just restart
	from the beginning of the file ... */
	/* Check if the end of file has been reached */

	#ifdef AUDIO_MAL_MODE_NORMAL
	if(fifoBufferFullness < 1) {
		/* play padding instead of real stream */
		Audio_MAL_Play((uint32_t)padBuffer, (SPEAKER_FREQ/MIC_FREQ)*320*2*sizeof(short));
	} else {
		Audio_MAL_Play((uint32_t)fifoBuffer[fifoBufferCurrent], (SPEAKER_FREQ/MIC_FREQ)*320*2*sizeof(short));
		fifoBufferFullness--;
		fifoBufferCurrent++;
		if(fifoBufferCurrent >= MODULATOR_QUEUE_SIZE)
		fifoBufferCurrent=0;

		if(volume_set==0)
		volume_set=1;
	}

	#else /* #ifdef AUDIO_MAL_MODE_CIRCULAR */


	#endif /* AUDIO_MAL_MODE_CIRCULAR */
}


/**
* @brief  Manages the DMA Half Transfer complete interrupt.
* @param  None
* @retval None
*/
void EVAL_AUDIO_HalfTransfer_CallBack(uint32_t pBuffer, uint32_t Size)
{
	#ifdef AUDIO_MAL_MODE_CIRCULAR

	#endif /* AUDIO_MAL_MODE_CIRCULAR */

	/* Generally this interrupt routine is used to load the buffer when
	a streaming scheme is used: When first Half buffer is already transferred load
	the new data to the first half of buffer while DMA is transferring data from
	the second half. And when Transfer complete occurs, load the second half of
	the buffer while the DMA is transferring from the first half ... */
	/*
	...........
	*/
}

/**
* @brief  Manages the DMA FIFO error interrupt.
* @param  None
* @retval None
*/
void EVAL_AUDIO_Error_CallBack(void* pData)
{
	/* Stop the program with an infinite loop */
	while (1)
	{}

	/* could also generate a system reset to recover from the error */
	/* .... */
}

/**
* @brief  Get next data sample callback
* @param  None
* @retval Next data sample to be sent
*/
uint16_t EVAL_AUDIO_GetSampleCallBack(void)
{
	return 0;
}


#ifndef USE_DEFAULT_TIMEOUT_CALLBACK
/**
* @brief  Basic management of the timeout situation.
* @param  None.
* @retval None.
*/
uint32_t Codec_TIMEOUT_UserCallback(void)
{
	return (0);
}
#endif /* USE_DEFAULT_TIMEOUT_CALLBACK */
/*----------------------------------------------------------------------------*/


/**
* @brief  Inserts a delay time.
* @param  nTime: specifies the delay time length, in 10 ms.
* @retval None
*/
void Delay(__IO uint32_t nTime)
{
	TimingDelay = nTime;

	while(TimingDelay != 0);
}

/**
* @brief  Decrements the TimingDelay variable.
* @param  None
* @retval None
*/
void TimingDelay_Decrement(void)
{
	if (TimingDelay != 0x00)
	{
		TimingDelay--;
	}
}


/**
* @brief  Main program.
* @param  None
* @retval None
*/
int main(void)
{
	/* Initialize LEDS */
	/* Red Led On: buffer overflow */
	STM_EVAL_LEDInit(LED3); 
	STM_EVAL_LEDInit(LED4); //green led
	STM_EVAL_LEDInit(LED5);
	/* Blue Led On: start of application */
	STM_EVAL_LEDInit(LED6);

	STM_EVAL_LEDOn(LED6);

	if(Transparent_mode)
	STM_EVAL_LEDOff(LED3);
	else
	STM_EVAL_LEDOn(LED3);

	/* SysTick end of count event each 10ms */
	RCC_GetClocksFreq(&RCC_Clocks);
	SysTick_Config(RCC_Clocks.HCLK_Frequency / 100);

	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate = 115200;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	STM_EVAL_USART2Init(&USART_InitStructure);

	//unsigned int idx = 0;
	Time_Rec_Base=0;
	int buffId;

	codec2_initialize_all(SPEAKER_FREQ == 48000 ? 1 : 0);

	/* fill output fifo */
	fifoBufferFullness=0;
	fifoBufferCurrent=0;
	Switch = 0;

	/* modulate silence once for padding if happens */
	memset(padBuffer, 0x00, (320*(SPEAKER_FREQ/MIC_FREQ)*2)*sizeof(short));
	memset(fifoBuffer, 0x00, MODULATOR_QUEUE_SIZE * (SPEAKER_FREQ/MIC_FREQ)*320*2*sizeof(short));

	/* Initialize I2S interface */
	EVAL_AUDIO_SetAudioInterface(AUDIO_INTERFACE_I2S);

	/* Initialize the Audio codec and all related peripherals (I2S, I2C, IOExpander, IOs...) */
	EVAL_AUDIO_Init(OUTPUT_DEVICE_AUTO, 0, SPEAKER_FREQ);
	EVAL_AUDIO_PauseResume(AUDIO_PAUSE);
	Audio_MAL_Play((uint32_t) padBuffer, (320*(SPEAKER_FREQ/MIC_FREQ))*2*sizeof(short));
	EVAL_AUDIO_PauseResume(AUDIO_RESUME);

	/* Init Microphone */
	MicListenerInit(I2S_AudioFreq_32k,16, 1);

	/* GLOBAL SCHEDULER
	* DO NOT USE LOOPS INSIDE IT!
	*/
	while(1) {


		Delay(500);

		uint32_t cont = 0;	
		/* Start microphone */
		MicListenerStart(RecBuf_8Khz, PCM_OUT_SIZE);
		/* ~25 samples per second */ 
		while(cont<125) {
			/* we have frame from mike */
			if(Data_Status == 0)
			continue;

			/* Switch the buffers*/
			if (Switch ==1) {
				pAudioRecBuf_8Khz = RecBuf_8Khz;
				writebuffer = RecBuf1_8Khz;
				Switch = 0;
			} else {
				pAudioRecBuf_8Khz = RecBuf1_8Khz;
				writebuffer = RecBuf_8Khz;
				Switch = 1;
			}

			#ifdef USE_ST_FILTER
			//Downsampling 16Khz => 8Khz (this is input for codec, it sampled with 8KHz)
			for(i=0; i<320; i++)
			writebuffer[i] = writebuffer[2*i];
			#endif

			//TODO: modulate, even if no data from mike!
			if(fifoBufferFullness < MODULATOR_QUEUE_SIZE-1) {
				/* get the next free buffer */
				buffId = fifoBufferCurrent + fifoBufferFullness;
				if(buffId >= MODULATOR_QUEUE_SIZE) buffId -= MODULATOR_QUEUE_SIZE;
				assert(buffId >= 0 && buffId < MODULATOR_QUEUE_SIZE);
				codec2_modulate((short *) writebuffer, (short *) fifoBuffer[buffId], Transparent_mode);
				fifoBufferFullness++;

				/* this is hack to remove loud noise at startup */
				if(volume_set==1) {
					STM_EVAL_LEDOff(LED3);
					EVAL_AUDIO_VolumeCtl(90);
					volume_set=2;
				}
			}

			Data_Status = 0;
			++cont;
			STM_EVAL_LEDToggle(LED4); // toggle green led
		}
		/* Stop microphone */
		MicListenerStop();
		/* Turn off led 4 */
		STM_EVAL_LEDOff(LED4); //green led off
	}

	EVAL_AUDIO_Mute(AUDIO_MUTE_ON);
	EVAL_AUDIO_Stop(CODEC_PDWN_HW);
	MicListenerStop();

	while(1) {
		STM_EVAL_LEDToggle(LED5);
		Delay(10);
	}
}


#ifdef  USE_FULL_ASSERT

/**
* @brief  Reports the name of the source file and the source line number
*   where the assert_param error has occurred.
* @param  file: pointer to the source file name
* @param  line: assert_param error line source number
* @retval None
*/
void assert_failed(uint8_t* file, uint32_t line)
{
	/* User can add his own implementation to report the file name and line number,
	ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

	/* Infinite loop */
	while (1)
	{
	}
}
#endif


/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
