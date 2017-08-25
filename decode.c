#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    CStreamInfo *info;
    char pcm_name[64];

    if (argc < 2)
    {
        printf("usage: decode <adts file>\n");
        exit(1);
    }
    memset(pcm_name, 0, 64);
    strncpy(pcm_name, argv[1], strlen(argv[1]) - 5);
    strcat(pcm_name, ".pcm");

    FILE *adts_fp = fopen(argv[1], "rb");
    if (!adts_fp)
    {
        printf("open adts file failed\n");
        exit(1);
    }
    adts_len = fread(adts_buf, 1, MAX_ADTS_FILE_SIZE, adts_fp);
    fclose(adts_fp);

    FILE *pcm_fp = fopen(pcm_name, "wb");
    if (!pcm_fp)
    {
        printf("open pcm file failed\n");
        exit(1);
    }
    
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

        while (1)
        {
            fdk_ret = aacDecoder_DecodeFrame(fdk, pcm_buf, pcm_len, 0);
            if (fdk_ret == AAC_DEC_NOT_ENOUGH_BITS) {
                break;
            }
            if (fdk_ret != AAC_DEC_OK) {
                printf("decode error, continue, frame index: %d, err type: %x\n", sn, fdk_ret);
                continue;
            }
            if (first)
            {
                info = aacDecoder_GetStreamInfo(fdk);
                printf("sampleRate:         %d\n", info->sampleRate);
                printf("frameSize:          %d\n", info->frameSize);
                printf("numChannels:        %d\n", info->numChannels);
                printf("profile:            %d\n", info->profile);
                printf("aot:                %d\n", info->aot);
                printf("channelConfig:      %d\n", info->channelConfig);
                printf("bitRate:            %d\n", info->bitRate);
                printf("aacSamplesPerFrame: %d\n", info->aacSamplesPerFrame);
                printf("outputDelay:        %d\n", info->outputDelay);
                printf("numTotalBytes:      %d\n", info->numTotalBytes);
                pcm_len = info->numChannels * info->frameSize * sizeof(short);
                first = 0;
            }
            int ret = fwrite(pcm_buf, 1, pcm_len, pcm_fp);
            if (ret != pcm_len)
            {
                printf("write pcm file error\n");
            }
            sn++;
        }
    }

    fclose(pcm_fp);
    aacDecoder_Close(fdk);

    printf("decode adts to pcm successful, please use below command to play:\n");
    printf("    aplay -r %d -c %d -f S16_LE %s\n", info->sampleRate, info->numChannels, pcm_name);

    return 0;
}
