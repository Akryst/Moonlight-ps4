#include "../src/audio/audio_orbis.c"
#include <assert.h>
#include <stdio.h>
static int reject_8ch,opened_format,destroyed;
void log_printf(const char *fmt,...){(void)fmt;}
uint32_t sceSysmoduleLoadModuleInternal(enum OrbisSysModuleInternal id){(void)id;return 0;}
int32_t sceUserServiceGetInitialUser(int32_t *id){*id=11;return 0;}
int32_t sceAudioOutInit(void){return 0;}
int32_t sceAudioOutOpen(int32_t user,enum OrbisAudioOutPort type,int32_t index,uint32_t len,uint32_t freq,uint32_t param){(void)user;(void)type;(void)index;(void)len;(void)freq;opened_format=param;return reject_8ch && param==2?-1:5;}
int32_t sceAudioOutClose(int32_t handle){(void)handle;return 0;}
int32_t sceAudioOutOutput(int32_t handle,const void *buffer){(void)handle;(void)buffer;return 0;}
OpusMSDecoder *opus_multistream_decoder_create(opus_int32 rate,int channels,int streams,int coupled,const unsigned char *mapping,int *error){(void)rate;(void)channels;(void)streams;(void)coupled;(void)mapping;*error=0;return (OpusMSDecoder *)(uintptr_t)1;}
void opus_multistream_decoder_destroy(OpusMSDecoder *d){(void)d;destroyed++;}
int opus_multistream_decode(OpusMSDecoder *d,const unsigned char *data,opus_int32 len,opus_int16 *pcm,int size,int fec){(void)d;(void)data;(void)len;(void)pcm;(void)size;(void)fec;return 0;}
int main(void){
    OPUS_MULTISTREAM_CONFIGURATION cfg={0};cfg.channelCount=6;cfg.sampleRate=48000;cfg.samplesPerFrame=240;
    assert(!ar_init(0,&cfg,NULL,0));assert(s_out_channels==8 && opened_format==2);
    short in[]={100,200,300,400,500,600},out[8];push_decoded(in,1);ring_pop(out,1);
    for(int i=0;i<6;i++)assert(out[i]==in[i]);assert(out[6]==0 && out[7]==0);
    ar_cleanup();reject_8ch=1;assert(!ar_init(0,&cfg,NULL,0));assert(s_out_channels==2);
    push_decoded(in,1);ring_pop(out,1);assert(out[0]==100+(300*181>>8)+(500*181>>8));
    short loud[]={32767,32767,32767,0,32767,32767};push_decoded(loud,1);ring_pop(out,1);assert(out[0]==32767 && out[1]==32767);
    ar_cleanup();cfg.samplesPerFrame=999;assert(ar_init(0,&cfg,NULL,0)==-1);assert(destroyed==2);
    puts("Audio: 5.1 expands to HDMI 8ch, stereo fallback, clipping and invalid frame bounds passed");
}
