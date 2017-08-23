#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define BUF_LEN 10240000
#define MAX_SAMPLE_NUM 102400

char m4a_buf[BUF_LEN];
int m4a_len;
int m4a_samples[MAX_SAMPLE_NUM];
int m4a_sample_num;
int sample_rate;
int sample_channel;
char* es_pos;
int es_len;

void get_adts_head(char head[7], int len)
{
    int freqs[] = {96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};
    int i;
    
    for (i=0; i<sizeof(freqs)/sizeof(int); i++)
    {
        if (freqs[i] == sample_rate)
        {
            break;
        }
    }
    
    head[0]  = 0xff;
    head[1]  = 0xf1;
    head[2]  = 0x40 | (unsigned char)(i<<2);
    //head[3]  = (unsigned char)(sample_channel<<6) | (unsigned char)(((len+7)>>11)&0x3); 
    head[3]  = (unsigned char)(1<<6) | (unsigned char)(((len+7)>>11)&0x3); 
    head[4]  = (unsigned char)(((len+7)>>3)&0xff);
    head[5]  = 0x1f | (unsigned char)(((len+7)<<5)&0xe0);
    head[6]  = 0xfc;
}

void parse_stsd()
{
    char* pos = memmem(m4a_buf, m4a_len, "stsd", 4);
    if (pos)
    {
        pos += 36; //channel
        sample_channel = htons(*(short*)pos);
        printf("channel: %d\n", sample_channel);
        pos += 2; //sample size
        printf("sample size: %d\n", htons(*(short*)pos));
        pos += 2; //pre define
        printf("pre define: %d\n", htons(*(short*)pos));
        pos += 4; //sampel rate
        sample_rate = htonl(*(unsigned int*)pos) >> 16;
        printf("sample rate: %d\n", sample_rate);
    } 
}

void check_m4a_file()
{
    char* pos = memmem(m4a_buf, m4a_len, "stsd", 4);
    if (pos)
    {
        pos += 16;
        if (memcmp(pos, "mp4a", 4) != 0)
        {
            printf("Only support m4a file, not found mp4a type\n");
            exit(1);
        }
    }
    else
    {
        printf("Only support m4a file, not found stsd box\n");
        exit(1);
    }
}

void parse_stsz()
{
    char* pos = memmem(m4a_buf, m4a_len, "stsz", 4);
    int sample_size = 0;
    int table_size = 0;
    int i;
    if (pos)
    {
        pos += 8; //sample size
        sample_size = htonl(*(int*)pos);
        printf("sample size: %d\n", sample_size);
        if (sample_size == 0)
        {
            pos += 4; //table size
            table_size = htonl(*(int*)pos);
            printf("table size: %d\n", table_size);
            for (i=0; i<table_size; i++)
            {
                pos += 4; //each sample size
                m4a_samples[i] = htonl(*(int*)pos);
                //printf("sample %d size: %d\n", i, m4a_samples[i]);
            }
            m4a_sample_num = i;
        }
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("usage: test <file>\n");
        exit(1);
    }

    FILE* fp = fopen(argv[1], "rb");
    if (fp)
    {
        m4a_len = fread(m4a_buf, 1, BUF_LEN, fp);
        fclose(fp);
    }

    check_m4a_file();

    es_len = 0;
    char* pos = memmem(m4a_buf, m4a_len, "mdat", 4);
    if (pos)
    {
        es_pos = pos + 4;
        es_len = htonl(*(int*)(pos - 4)) - 8;
        printf("AAC ES len: %d\n", es_len);
    }

    if (es_len)
    {
        FILE* fp_mdat = fopen("aac.es", "wb");
        if (fp_mdat)
        {
            int len = fwrite(es_pos, 1, es_len, fp_mdat);
            if (len == es_len)
            {
                printf("save acc es successful\n");
            }
            else
            {
                printf("save acc es failed, len=%d expect=%d\n", len, es_len);
            }
            fclose(fp_mdat);
        }
    }

    parse_stsd();
    parse_stsz();
    
	if (m4a_sample_num > 0)
    {
        int adts_len = es_len + m4a_sample_num * 7;
        char* adts_buf = (char*)malloc(adts_len);
        char* dst_pos = adts_buf;
        char* src_pos = es_pos;
        char head[7];
        int len;
        for (int i=0; i<m4a_sample_num; i++)
        {
            len = m4a_samples[i];
            get_adts_head(head, len);
            memcpy(dst_pos, head, 7);
            memcpy(dst_pos + 7, src_pos, len);
            dst_pos += (7 + len);
            src_pos += len;
        }
        FILE* fp_adts = fopen("aac.adts", "wb");
        if (fp_adts)
        {
            len = fwrite(adts_buf, 1, adts_len, fp_adts);
            if (len == adts_len)
            {
                printf("save acc adts successful\n");
            }
            fclose(fp_adts);
        }            
        free(adts_buf);
    }

    return 0;
}    

