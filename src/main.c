#include <stdio.h>
#include <string.h>


#include "codec2_core.h"
#include "main.h"
#include "stm32f4_discovery_audio_mic.h"

/* Audio recording frequency in Hz */
#define MIC_FREQ                          8000
#define SPEAKER_FREQ                      48000

RCC_ClocksTypeDef RCC_Clocks;

#define MODULATOR_QUEUE_SIZE 4 //XXX: this is queue size, use not less than 3! 8 - is good! 6-works too

__IO uint32_t TimingDelay = 0;
__IO uint8_t Transparent_mode = 1;
__IO int32_t fifoBufferCurrent = 0;
__IO int volume_set = 0;
__IO int32_t fifoBufferFullness = 0;

extern __IO uint32_t Time_Rec_Base;
extern __IO uint32_t Data_Status;

extern uint16_t* pAudioRecBuf_8Khz;
uint16_t *writebuffer;

uint16_t fifoBuffer[MODULATOR_QUEUE_SIZE][(SPEAKER_FREQ/MIC_FREQ)*320*2];
uint16_t padBuffer[(SPEAKER_FREQ/MIC_FREQ)*320*2];
uint16_t RecBuf_8Khz[PCM_OUT_SIZE*REC_BUFFERS], RecBuf1_8Khz[PCM_OUT_SIZE*REC_BUFFERS];



int main(void)
{
	uint8_t			i, Switch = 0;
	int				buffId;

	// Init leds
	STM_EVAL_LEDInit(LED3);
	STM_EVAL_LEDInit(LED4);
	STM_EVAL_LEDInit(LED5);
	STM_EVAL_LEDInit(LED6);
	
	/* SysTick end of count event each 10ms */
	RCC_GetClocksFreq(&RCC_Clocks);
	SysTick_Config(RCC_Clocks.HCLK_Frequency / 100);

	// turn off buffers, so IO occurs immediately
	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	
	codec2_initialize_all(SPEAKER_FREQ == 48000 ? 1 : 0);

	/* fill output fifo */
	fifoBufferFullness=0;
	fifoBufferCurrent=0;

	memset(padBuffer, 0x00, (320*(SPEAKER_FREQ/MIC_FREQ)*2)*sizeof(short));
	memset(fifoBuffer, 0x00, MODULATOR_QUEUE_SIZE * (SPEAKER_FREQ/MIC_FREQ)*320*2*sizeof(short));

	// Initialize I2S interface
	EVAL_AUDIO_SetAudioInterface(AUDIO_INTERFACE_I2S);

	// Initialize output device
	EVAL_AUDIO_Init(OUTPUT_DEVICE_AUTO, 0, SPEAKER_FREQ);

	EVAL_AUDIO_PauseResume(AUDIO_PAUSE);
	Audio_MAL_Play((uint32_t) padBuffer, (320*(SPEAKER_FREQ/MIC_FREQ))*2*sizeof(short));
	EVAL_AUDIO_PauseResume(AUDIO_RESUME);

	/* Start the record */
	MicListenerInit(32000,16, 1);
	MicListenerStart(RecBuf_8Khz, PCM_OUT_SIZE);

	while(1) {
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

		for(i=0; i<320; i++)
		writebuffer[i] = writebuffer[2*i];

		if(fifoBufferFullness < MODULATOR_QUEUE_SIZE-1) {
			buffId = fifoBufferCurrent + fifoBufferFullness;
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

	}



}


//----------------------------------------------------------------
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


void EVAL_AUDIO_TransferComplete_CallBack(uint32_t pBuffer, uint32_t Size)
{
	/* Calculate the remaining audio data in the file and the new size
	for the DMA transfer. If the Audio files size is less than the DMA max
	data transfer size, so there is no calculation to be done, just restart
	from the beginning of the file ... */
	/* Check if the end of file has been reached */

	#ifdef AUDIO_MAL_MODE_NORMAL
	//  XferCplt = 1;
	//  if (WaveDataLength) WaveDataLength -= _MAX_SS;
	//  if (WaveDataLength < _MAX_SS) WaveDataLength = 0;
	if(fifoBufferFullness < 1) {
		/* play padding instead of real stream */
		Audio_MAL_Play((uint32_t)padBuffer, (SPEAKER_FREQ/MIC_FREQ)*320*2*sizeof(short));
		//EVAL_AUDIO_Play(padBuffer, (320*(SPEAKER_FREQ/MIC_FREQ))*2*sizeof(short));
		//STM_EVAL_LEDToggle(LED4);
		//printf("!");
	} else {
		Audio_MAL_Play((uint32_t)fifoBuffer[fifoBufferCurrent], (SPEAKER_FREQ/MIC_FREQ)*320*2*sizeof(short));
		//EVAL_AUDIO_Play(fifoBuffer[fifoBufferCurrent], (SPEAKER_FREQ/MIC_FREQ)*320*2*sizeof(short));
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

