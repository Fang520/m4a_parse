#include <stdio.h>
#include <stdlib.h>
#include "fdk-aac/aacdecoder_lib.h"

#define MAX_ADTS_FILE_SIZE 10240000
#define MAX_PCM_BUF_SIZE 102400

unsigned char adts_buf[MAX_ADTS_FILE_SIZE];
unsigned int adts_len;
short pcm_buf[MAX_PCM_BUF_SIZE];
int pcm_len = MAX_PCM_BUF_SIZE;

int main(int argc, char **argv)
{
    AAC_DECODER_ERROR fdk_ret;

    FILE *adts_fp = fopen(argv[1], "rb");
    if (!adts_fp)
    {
        printf("open adts file failed\n");
        exit(1);
    }
    adts_len = fread(adts_buf, 1, MAX_ADTS_FILE_SIZE, adts_fp);
    fclose(adts_fp);
    

    HANDLE_AACDECODER fdk = aacDecoder_Open(TT_MP4_ADTS, 1);
    if (!fdk)
    {
        printf("open fdk decoder failed\n");
        exit(1);
    }

    unsigned char* buf_pos[] = {adts_buf};
    unsigned int buf_len[] = {adts_len};
    int left = buf_len[0];
    int first = 1;
    int sn = 0;
    while (left > 0)
    {
        unsigned int valid = left;
        int used;
        
        fdk_ret = aacDecoder_Fill(fdk, buf_pos, buf_len, &valid);
        if (fdk_ret != AAC_DEC_OK)
        {
            printf("aacDecoder_Fill failed\n");
            exit(1);
        }
        used = left - valid;
        buf_pos[0] += used;
        buf_len[0] -= used;
        left = valid;
        
        fdk_ret = aacDecoder_DecodeFrame(fdk, pcm_buf, pcm_len, 0);
        if (fdk_ret == AAC_DEC_NOT_ENOUGH_BITS) {
            printf("not enough bits, continue\n");
            continue;
        }
        if (fdk_ret != AAC_DEC_OK) {
            printf("decode error, continue\n");
            continue;
        }
        if (first)
        {
            CStreamInfo *info = aacDecoder_GetStreamInfo(fdk);
            printf("sampleRate:         %d\n", info->sampleRate);
            printf("frameSize:          %d\n", info->frameSize);
            printf("numChannels:        %d\n", info->numChannels);
            printf("profile:            %d\n", info->profile);
            printf("aot:                %d\n", info->aot);
            printf("channelConfig:      %d\n", info->channelConfig);
            printf("bitRate:            %d\n", info->bitRate);
            printf("aacSamplesPerFrame: %d\n", info->aacSamplesPerFrame);
            first = 0;
        }
        printf("decode successful, %d\n", sn++);
    }

    aacDecoder_Close(fdk);

    return 0;
}
